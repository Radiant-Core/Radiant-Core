// Copyright (c) 2011-2019 The Bitcoin Core developers
// Copyright (c) 2022-2026 The Radiant developers
// Copyright (c) 2017-2019 The Bitcoin developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <validation.h>

#include <chainparams.h>
#include <clientversion.h>
#include <coins.h>
#include <config.h>
#include <consensus/consensus.h>
#include <net.h>
#include <primitives/transaction.h>
#include <script/script.h>
#include <streams.h>
#include <uint256.h>
#include <util/system.h>

#include <test/setup_common.h>

#include <boost/signals2/signal.hpp>
#include <boost/test/unit_test.hpp>

#include <array>
#include <cstdint>
#include <cstdio>
#include <set>
#include <vector>

BOOST_FIXTURE_TEST_SUITE(validation_tests, TestingSetup)

static void TestBlockSubsidyHalvings(const Consensus::Params &consensusParams) {
    int maxHalvings = 64;
    Amount nInitialSubsidy = 50000 * COIN;

    // for height == 0
    Amount nPreviousSubsidy = 2 * nInitialSubsidy;
    BOOST_CHECK_EQUAL(nPreviousSubsidy, 2 * nInitialSubsidy);
    for (int nHalvings = 0; nHalvings < maxHalvings; nHalvings++) {
        int nHeight = nHalvings * consensusParams.nSubsidyHalvingInterval;
        Amount nSubsidy = GetBlockSubsidy(nHeight, consensusParams);
        BOOST_CHECK(nSubsidy <= nInitialSubsidy);
        BOOST_CHECK_EQUAL(nSubsidy, nPreviousSubsidy / 2);
        nPreviousSubsidy = nSubsidy;
    }
    BOOST_CHECK_EQUAL(
        GetBlockSubsidy(maxHalvings * consensusParams.nSubsidyHalvingInterval,
                        consensusParams),
        Amount::zero());
}

static void TestBlockSubsidyHalvings(int nSubsidyHalvingInterval) {
    Consensus::Params consensusParams;
    consensusParams.nSubsidyHalvingInterval = nSubsidyHalvingInterval;
    TestBlockSubsidyHalvings(consensusParams);
}

BOOST_AUTO_TEST_CASE(block_subsidy_test) {
    const auto chainParams = CreateChainParams(CBaseChainParams::MAIN);
    // As in main
    TestBlockSubsidyHalvings(chainParams->GetConsensus());
    // As in regtest
    TestBlockSubsidyHalvings(150);
    // Just another interval
    TestBlockSubsidyHalvings(1000);
}

BOOST_AUTO_TEST_CASE(subsidy_limit_test) {
    const auto chainParams = CreateChainParams(CBaseChainParams::MAIN);
    Amount nSum = Amount::zero();
    for (int nHeight = 0; nHeight < 14000000; nHeight += 1000) {
        Amount nSubsidy = GetBlockSubsidy(nHeight, chainParams->GetConsensus());
        BOOST_CHECK(nSubsidy <= 50000 * COIN);
        nSum += 1000 * nSubsidy;
        BOOST_CHECK(MoneyRange(nSum));
    }
    BOOST_CHECK_EQUAL(nSum, int64_t(2099999999997060000LL) * SATOSHI);
}

static CBlock makeLargeDummyBlock(const size_t num_tx) {
    CBlock block;
    block.vtx.reserve(num_tx);

    for (size_t i = 0; i < num_tx; i++) {
        block.vtx.push_back(MakeTransactionRef());
    }
    return block;
}

/**
 * Test that LoadExternalBlockFile works with the buffer size set below the
 * size of a large block. Currently, LoadExternalBlockFile has the buffer size
 * for CBufferedFile set to 2 * MAX_TX_SIZE. Test with a value of
 * 10 * MAX_TX_SIZE.
 */
BOOST_AUTO_TEST_CASE(validation_load_external_block_file) {
    fs::path tmpfile_name =
        SetDataDir("validation_load_external_block_file") / "block.dat";

    FILE *fp = fopen(tmpfile_name.string().c_str(), "wb+");

    BOOST_CHECK(fp != nullptr);

    const Config &config = GetConfig();
    const CChainParams &chainparams = config.GetChainParams();

    // serialization format is:
    // message start magic, size of block, block

    size_t nwritten = fwrite(std::begin(chainparams.DiskMagic()),
                             CMessageHeader::MESSAGE_START_SIZE, 1, fp);

    BOOST_CHECK_EQUAL(nwritten, 1UL);

    const auto &empty_tx = CTransaction::null;
    size_t empty_tx_size = GetSerializeSize(empty_tx, CLIENT_VERSION);

    size_t num_tx = (10 * MAX_TX_SIZE_CONSENSUS_LEGACY) / empty_tx_size;

    CBlock block = makeLargeDummyBlock(num_tx);

    BOOST_CHECK(GetSerializeSize(block, CLIENT_VERSION) > 2 * MAX_TX_SIZE_CONSENSUS_LEGACY);

    unsigned int size = GetSerializeSize(block, CLIENT_VERSION);
    {
        CAutoFile outs(fp, SER_DISK, CLIENT_VERSION);
        outs << size;
        outs << block;
        outs.release();
    }

    fseek(fp, 0, SEEK_SET);
    BOOST_CHECK_NO_THROW({ LoadExternalBlockFile(config, fp, 0); });
}

BOOST_AUTO_TEST_CASE(min_relay_fee_upgrade_test) {
    const auto chainParams = CreateChainParams(CBaseChainParams::MAIN);
    const Consensus::Params& params = chainParams->GetConsensus();
    int upgradeHeight = params.radiantCore2UpgradeHeight;

    // Save global state
    CFeeRate originalMinRelayTxFee = ::minRelayTxFee;

    // Case 1: Default global fee (legacy)
    ::minRelayTxFee = CFeeRate(LEGACY_MIN_RELAY_TX_FEE_PER_KB);

    // Before upgrade
    BOOST_CHECK(GetEffectiveMinRelayFee(upgradeHeight - 1, params) == CFeeRate(LEGACY_MIN_RELAY_TX_FEE_PER_KB));
    
    // At upgrade (start of grace period)
    BOOST_CHECK(GetEffectiveMinRelayFee(upgradeHeight, params) == CFeeRate(LEGACY_MIN_RELAY_TX_FEE_PER_KB));

    // During grace period (e.g. 4999 blocks in)
    BOOST_CHECK(GetEffectiveMinRelayFee(upgradeHeight + 4999, params) == CFeeRate(LEGACY_MIN_RELAY_TX_FEE_PER_KB));

    // After grace period (5000 blocks)
    BOOST_CHECK(GetEffectiveMinRelayFee(upgradeHeight + 5000, params) == CFeeRate(RADIANT_CORE_2_MIN_RELAY_TX_FEE_PER_KB));

    // Case 2: User sets higher fee manually
    ::minRelayTxFee = CFeeRate(2 * RADIANT_CORE_2_MIN_RELAY_TX_FEE_PER_KB);
    
    // Should respect the higher manual fee even before upgrade
    BOOST_CHECK(GetEffectiveMinRelayFee(upgradeHeight - 1, params) == ::minRelayTxFee);
    BOOST_CHECK(GetEffectiveMinRelayFee(upgradeHeight + 6000, params) == ::minRelayTxFee);

    // Case 3: User sets lower fee manually (e.g. 0 or very low) - should enforce floor
    ::minRelayTxFee = CFeeRate(Amount::zero());
    
    // Before upgrade: floor is legacy
    BOOST_CHECK(GetEffectiveMinRelayFee(upgradeHeight - 1, params) == CFeeRate(LEGACY_MIN_RELAY_TX_FEE_PER_KB));
    
    // After grace period: floor is new fee
    BOOST_CHECK(GetEffectiveMinRelayFee(upgradeHeight + 5000, params) == CFeeRate(RADIANT_CORE_2_MIN_RELAY_TX_FEE_PER_KB));

    // Restore global state
    ::minRelayTxFee = originalMinRelayTxFee;
}

/**
 * Append a single OP_PUSHINPUTREF opcode followed by its 36-byte (288-bit)
 * reference operand to a script, in the raw on-the-wire encoding the parser
 * (CScript::GetPushRefs / GetScriptOp) expects.
 */
static void AppendPushInputRef(CScript &script, const std::array<uint8_t, 36> &ref) {
    script.insert(script.end(), static_cast<uint8_t>(OP_PUSHINPUTREF));
    script.insert(script.end(), ref.begin(), ref.end());
}

/**
 * Build N distinct 36-byte references deterministically (counter encoded in the
 * leading bytes) and append them as OP_PUSHINPUTREF opcodes to `script`.
 */
static void AppendDistinctPushRefs(CScript &script, size_t n) {
    for (size_t i = 0; i < n; ++i) {
        std::array<uint8_t, 36> ref{};
        ref[0] = static_cast<uint8_t>(i & 0xff);
        ref[1] = static_cast<uint8_t>((i >> 8) & 0xff);
        ref[2] = static_cast<uint8_t>((i >> 16) & 0xff);
        ref[3] = static_cast<uint8_t>((i >> 24) & 0xff);
        // a fixed marker in the high bytes so these never collide with the
        // input-outpoint-derived refs the validator also tracks.
        ref[35] = 0xab;
        AppendPushInputRef(script, ref);
    }
}

/**
 * 2026-06 security-audit (M-ref-validation): a small token-style tx whose output
 * push-refs are fully backed by the spent input's refs is accepted by
 * validateTransactionReferenceOperations. This locks in that the always-on
 * reserve() perf change (and the removal of the earlier ineffective+over-counting
 * distinct-ref cap) did not alter the induction-rule result for a normal backed
 * tx. (Bounded ref-flood resource use is covered by the MAX_TX_SIZE limit, not a
 * separate cap -- see the note in validateTransactionReferenceOperations.)
 */
BOOST_AUTO_TEST_CASE(ref_validation_accepts_backed_refs) {
    CCoinsView dummy;
    CCoinsViewCache coins(&dummy);

    const size_t kRefs = 4;

    CScript inputSpk;
    AppendDistinctPushRefs(inputSpk, kRefs);
    CScript outputSpk;
    AppendDistinctPushRefs(outputSpk, kRefs);

    const COutPoint in(TxId(uint256S(
        "0202020202020202020202020202020202020202020202020202020202020202")), 0);
    coins.AddCoin(in, Coin(CTxOut(1 * SATOSHI, inputSpk), 1, false), false);

    CMutableTransaction mtx;
    mtx.vin.resize(1);
    mtx.vin[0].prevout = in;
    mtx.vout.resize(1);
    mtx.vout[0].nValue = 1 * SATOSHI;
    mtx.vout[0].scriptPubKey = outputSpk;
    const CTransaction tx(mtx);

    BOOST_CHECK(ReferenceParser::validateTransactionReferenceOperations(tx, coins));
}

BOOST_AUTO_TEST_SUITE_END()
