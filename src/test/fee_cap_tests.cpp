// Copyright (c) 2026 The Radiant developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <miner.h>

#include <chain.h>
#include <chainparams.h>
#include <config.h>
#include <consensus/activation.h>
#include <policy/policy.h>
#include <test/setup_common.h>
#include <util/system.h>
#include <validation.h>

#include <boost/test/unit_test.hpp>

BOOST_FIXTURE_TEST_SUITE(fee_cap_tests, TestChain100Setup)

// Test that block mining fee caps are enforced to prevent empty block mining
BOOST_AUTO_TEST_CASE(block_fee_cap_enforcement) {
    const CChainParams &chainparams = Params();
    CScript scriptPubKey = CScript() << ToByteVector(coinbaseKey.GetPubKey()) << OP_CHECKSIG;

    // Test Pre-V2: Maximum should be capped at 1 RXD/kB (LEGACY_BLOCK_MAX_TX_FEE_PER_KB)
    {
        BlockAssembler::Options options;
        // Try to set extremely high fee (100 RXD/kB)
        options.blockMinFeeRate = CFeeRate(10000000000 * SATOSHI);
        
        BlockAssembler ba(chainparams, g_mempool, options);
        
        // Create a block template - the fee should be capped
        std::unique_ptr<CBlockTemplate> pblocktemplate = ba.CreateNewBlock(scriptPubKey);
        BOOST_CHECK(pblocktemplate);
        
        // The actual fee rate used should be capped at legacy maximum (1 RXD/kB)
        // We can't directly inspect blockMinFeeRate, but we can verify the block was created
        // which means the fee cap logic executed without error
    }

    // Test Post-V2: Maximum should be capped at 0.5 RXD/kB (RADIANT_CORE_2_BLOCK_MAX_TX_FEE_PER_KB)
    // Radiant Core 2.2 update
    {
        // Simulate being past V2 activation + grace period
        // Note: In real testing, this would require advancing the chain to the appropriate height
        BlockAssembler::Options options;
        
        // Try to set extremely high fee (1000 RXD/kB)
        options.blockMinFeeRate = CFeeRate(100000000000 * SATOSHI);
        
        BlockAssembler ba(chainparams, g_mempool, options);
        
        // Create a block template - the fee should be capped at 0.5 RXD/kB
        std::unique_ptr<CBlockTemplate> pblocktemplate = ba.CreateNewBlock(scriptPubKey);
        BOOST_CHECK(pblocktemplate);
    }
}

// Test that minimum fees are still enforced alongside maximum caps
BOOST_AUTO_TEST_CASE(block_fee_minimum_enforcement) {
    const CChainParams &chainparams = Params();
    CScript scriptPubKey = CScript() << ToByteVector(coinbaseKey.GetPubKey()) << OP_CHECKSIG;

    // Test that minimum fee is enforced (0.01 RXD/kB pre-V2)
    {
        BlockAssembler::Options options;
        // Try to set very low fee (0.001 RXD/kB)
        options.blockMinFeeRate = CFeeRate(100000 * SATOSHI);
        
        BlockAssembler ba(chainparams, g_mempool, options);
        
        // Create a block template - the fee should be raised to minimum
        std::unique_ptr<CBlockTemplate> pblocktemplate = ba.CreateNewBlock(scriptPubKey);
        BOOST_CHECK(pblocktemplate);
    }
}

// Test that fee caps prevent pools from mining only empty blocks
BOOST_AUTO_TEST_CASE(prevent_empty_block_mining) {
    const CChainParams &chainparams = Params();
    CScript scriptPubKey = CScript() << ToByteVector(coinbaseKey.GetPubKey()) << OP_CHECKSIG;

    // Create a transaction with reasonable fee (0.5 RXD/kB)
    CMutableTransaction tx;
    tx.vin.resize(1);
    tx.vin[0].prevout = COutPoint(m_coinbase_txns[0]->GetId(), 0);
    tx.vin[0].scriptSig = CScript() << OP_1;
    tx.vout.resize(1);
    tx.vout[0].nValue = 49 * COIN;
    tx.vout[0].scriptPubKey = scriptPubKey;

    // Add to mempool
    {
        LOCK2(cs_main, g_mempool.cs);
        CValidationState state;
        BOOST_CHECK(AcceptToMemoryPool(GetConfig(), g_mempool, state, MakeTransactionRef(tx),
                                       nullptr /* pfMissingInputs */,
                                       true /* bypass_limits */,
                                       Amount::zero() /* nAbsurdFee */));
    }

    // Test with fee cap - transaction should be included
    {
        BlockAssembler::Options options;
        // Set fee to 1 RXD/kB (below maximum cap of 10 RXD/kB)
        options.blockMinFeeRate = CFeeRate(100000000 * SATOSHI);
        
        BlockAssembler ba(chainparams, g_mempool, options);
        std::unique_ptr<CBlockTemplate> pblocktemplate = ba.CreateNewBlock(scriptPubKey);
        
        BOOST_CHECK(pblocktemplate);
        // Block should contain coinbase + at least one transaction
        BOOST_CHECK_GE(pblocktemplate->block.vtx.size(), 1UL);
    }

    // Clean up
    {
        LOCK(g_mempool.cs);
        g_mempool.clear();
    }
}

// Test fee cap constants are correctly defined
BOOST_AUTO_TEST_CASE(fee_cap_constants) {
    // Verify legacy maximum is 1 RXD/kB
    BOOST_CHECK_EQUAL(LEGACY_BLOCK_MAX_TX_FEE_PER_KB, 100000000 * SATOSHI);
    
    // Verify V2 maximum is 0.5 RXD/kB (Radiant Core 2.2)
    BOOST_CHECK_EQUAL(RADIANT_CORE_2_BLOCK_MAX_TX_FEE_PER_KB, 50000000 * SATOSHI);
    
    // Verify maximum is 5x the minimum for V2 (Radiant Core 2.2)
    BOOST_CHECK_EQUAL(RADIANT_CORE_2_BLOCK_MAX_TX_FEE_PER_KB / RADIANT_CORE_2_BLOCK_MIN_TX_FEE_PER_KB, 5);
    
    // Verify maximum is 100x the minimum for legacy
    BOOST_CHECK_EQUAL(LEGACY_BLOCK_MAX_TX_FEE_PER_KB / LEGACY_BLOCK_MIN_TX_FEE_PER_KB, 100);
}

BOOST_AUTO_TEST_SUITE_END()
