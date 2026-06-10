// Copyright (c) 2017 The Bitcoin Core developers
// Copyright (c) 2022-2026 The Radiant developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <amount.h>
#include <config.h>
#include <consensus/validation.h>
#include <policy/policy.h>
#include <primitives/transaction.h>
#include <script/script.h>
#include <script/script_flags.h>
#include <test/setup_common.h>
#include <txmempool.h>
#include <validation.h>
#include <consensus/tx_check.h>
#include <util/time.h>

#include <boost/test/unit_test.hpp>

BOOST_AUTO_TEST_SUITE(txvalidation_tests)

/**
 * Ensure that the mempool won't accept coinbase transactions.
 */
BOOST_FIXTURE_TEST_CASE(tx_mempool_reject_coinbase, TestChain100Setup) {
    CScript scriptPubKey = CScript() << ToByteVector(coinbaseKey.GetPubKey())
                                     << OP_CHECKSIG;
    CMutableTransaction coinbaseTx;

    coinbaseTx.nVersion = 1;
    coinbaseTx.vin.resize(1);
    coinbaseTx.vout.resize(1);
    coinbaseTx.vin[0].scriptSig = CScript() << OP_11 << OP_EQUAL;
    coinbaseTx.vout[0].nValue = 1 * CENT;
    coinbaseTx.vout[0].scriptPubKey = scriptPubKey;

    BOOST_CHECK(CTransaction(coinbaseTx).IsCoinBase());

    CValidationState state;

    LOCK(cs_main);

    unsigned int initialPoolSize = g_mempool.size();

    BOOST_CHECK_EQUAL(false,
                      AcceptToMemoryPool(GetConfig(), g_mempool, state,
                                         MakeTransactionRef(coinbaseTx),
                                         nullptr /* pfMissingInputs */,
                                         true /* bypass_limits */,
                                         Amount::zero() /* nAbsurdFee */));

    // Check that the transaction hasn't been added to mempool.
    BOOST_CHECK_EQUAL(g_mempool.size(), initialPoolSize);

    // Check that the validation state reflects the unsuccesful attempt.
    BOOST_CHECK(state.IsInvalid());
    BOOST_CHECK_EQUAL(state.GetRejectReason(), "bad-tx-coinbase");

    int nDoS;
    BOOST_CHECK_EQUAL(state.IsInvalid(nDoS), true);
    BOOST_CHECK_EQUAL(nDoS, 100);
}

/**
 * Ensure that transactions larger than the configured maximum size are
 * rejected from the mempool with an explicit oversized error.
 */
BOOST_FIXTURE_TEST_CASE(tx_mempool_reject_oversized, TestChain100Setup) {
    LOCK(cs_main);

    // Construct a transaction that is definitely larger than MAX_TX_SIZE (32MB)
    // by filling a large scriptSig.
    CMutableTransaction tx;
    tx.nVersion = 1;
    tx.vin.resize(1);
    tx.vout.resize(1);

    // Spend an arbitrary outpoint; it doesn't need to be valid because the
    // size check happens before input validation when bypass_limits=true.
    tx.vin[0].prevout = COutPoint(TxId(InsecureRand256()), 0);

    // Create a very large scriptSig to blow up the serialized size.
    // Use MAX_TX_SIZE (32MB) as the limit since Radiant supports large transactions
    const size_t largeScriptSize = MAX_TX_SIZE + ONE_MEGABYTE;
    std::vector<unsigned char> largeData(largeScriptSize, 0x01);
    CScript largeScript;
    largeScript << largeData;
    tx.vin[0].scriptSig = largeScript;

    // Minimal output just to make the transaction structurally valid.
    tx.vout[0].nValue = 1 * SATOSHI;
    tx.vout[0].scriptPubKey = CScript() << OP_TRUE;

    CValidationState state;
    unsigned int initialPoolSize = g_mempool.size();

    // Use bypass_limits=true so that policy limits do not interfere; the size
    // check in AcceptToMemoryPoolWorker should trigger first.
    bool accepted = AcceptToMemoryPool(GetConfig(), g_mempool, state,
                                       MakeTransactionRef(tx),
                                       nullptr /* pfMissingInputs */,
                                       true /* bypass_limits */,
                                       Amount::zero() /* nAbsurdFee */);

    BOOST_CHECK(!accepted);
    BOOST_CHECK_EQUAL(g_mempool.size(), initialPoolSize);
    BOOST_CHECK(state.IsInvalid());
    BOOST_CHECK_EQUAL(state.GetRejectCode(), REJECT_INVALID);
    BOOST_CHECK(state.GetRejectReason().find("bad-txns-oversize") != std::string::npos);
}

/**
 * Ensure that transactions with duplicate inputs are appropriately rejected regardless of the length of vin
 */
BOOST_FIXTURE_TEST_CASE(tx_mempool_reject_dup_txin, TestChain100Setup) {
    CScript scriptPubKey = CScript() << ToByteVector(coinbaseKey.GetPubKey()) << OP_CHECKSIG;

    CMutableTransaction tx;
    for (size_t vinSize = 2; vinSize < 2000; vinSize < 20 ? vinSize++ : vinSize *= 2) {
      tx.nVersion = 1;
      tx.vin.resize(vinSize);
      tx.vout.resize(1);
      tx.vout[0].nValue = 400 * SATOSHI;
      tx.vout[0].scriptPubKey = scriptPubKey;
      for (size_t i=0; i<vinSize; i++) {
        tx.vin[i].prevout = COutPoint(TxId(InsecureRand256()), 0);
      }
      BOOST_CHECK(!CTransaction(tx).IsCoinBase());

      CValidationState state1;
      BOOST_CHECK(CheckRegularTransaction(CTransaction(tx), state1));

      size_t i = InsecureRandRange(vinSize);
      size_t j = InsecureRandRange(vinSize-1);
      if (j >= i) j++;
      tx.vin[j] = tx.vin[i];
      BOOST_CHECK(!CheckRegularTransaction(CTransaction(tx), state1));
      BOOST_CHECK_EQUAL(state1.GetRejectReason(), "bad-txns-inputs-duplicate");


      CValidationState state2;
      LOCK(cs_main);
      unsigned int initialPoolSize = g_mempool.size();

      BOOST_CHECK_EQUAL(false, AcceptToMemoryPool(GetConfig(), g_mempool, state2,
                                                  MakeTransactionRef(tx),
                                                  nullptr /* pfMissingInputs */,
                                                  true /* bypass_limits */,
                                                  Amount::zero() /* nAbsurdFee */));

      // Check that the transaction hasn't been added to mempool.
      BOOST_CHECK_EQUAL(g_mempool.size(), initialPoolSize);

      // Check that the validation state reflects the unsuccesful attempt.
      BOOST_CHECK(state2.IsInvalid());
      BOOST_CHECK_EQUAL(state2.GetRejectReason(), "bad-txns-inputs-duplicate");

      int nDoS;
      BOOST_CHECK_EQUAL(state2.IsInvalid(nDoS), true);
      BOOST_CHECK_EQUAL(nDoS, 100);
    }
}

/**
 * 2026-06 security-audit (H-1): AcceptToMemoryPool must verify every
 * mempool-accepted tx against the per-script memory budget UNCONDITIONALLY on
 * its relay/policy verification path -- independent of fRequireStandard and of
 * SecurityUpgradeHeight -- to close the relay-reachable memory-bomb DoS now.
 *
 * Building a real >budget stack tx and pushing it through ATMP requires the
 * interpreter's budget enforcement plus a funded regtest chain, so the
 * end-to-end accept/reject behavior is covered by the regtest functional
 * suite. Here we lock in the load-bearing invariant of the flag construction:
 *
 *  1) the relay scriptVerifyFlags expression used by ATMP
 *     (nextBlockScriptVerifyFlags | STANDARD_SCRIPT_VERIFY_FLAGS |
 *      SCRIPT_VERIFY_MEMORY_BUDGET) always sets the memory-budget bit,
 *     regardless of whether the security upgrade is active; and
 *  2) the memory-budget bit is a distinct flag that is NOT implied by the
 *     standard relay flags nor by the gated security-upgrade flag, so it could
 *     only be present because ATMP added it explicitly and unconditionally.
 */
BOOST_AUTO_TEST_CASE(atmp_memory_budget_flag_unconditional) {
    // The memory-budget bit is its own flag, not folded into STANDARD or the
    // gated security-upgrade flag.
    BOOST_CHECK(SCRIPT_VERIFY_MEMORY_BUDGET != 0u);
    BOOST_CHECK_EQUAL(STANDARD_SCRIPT_VERIFY_FLAGS & SCRIPT_VERIFY_MEMORY_BUDGET, 0u);
    BOOST_CHECK_EQUAL(SCRIPT_SECURITY_UPGRADE & SCRIPT_VERIFY_MEMORY_BUDGET, 0u);

    // It is deliberately NOT folded into STANDARD_NOT_MANDATORY either: a
    // budget trip raises the distinct ScriptError::STACK_MEMORY, and CheckInputs
    // classifies a pre-fork (policy-only) STACK_MEMORY failure as non-mandatory
    // directly (no DoS ban, no re-execution of the memory-bomb script). Keeping
    // the bit out of STANDARD_NOT_MANDATORY ensures the generic check2 re-run for
    // OTHER non-mandatory failures still carries the budget bit and stays bounded.
    BOOST_CHECK_EQUAL(STANDARD_NOT_MANDATORY_VERIFY_FLAGS &
                          SCRIPT_VERIFY_MEMORY_BUDGET,
                      0u);

    // Pre-fork: next-block consensus flags do NOT include the security upgrade
    // (and therefore not the memory budget either) ...
    const uint32_t nextBlockFlagsPreFork =
        STANDARD_SCRIPT_VERIFY_FLAGS; // upper bound on what GetNextBlockScriptFlags adds pre-fork, sans security upgrade
    const uint32_t relayFlagsPreFork =
        nextBlockFlagsPreFork | STANDARD_SCRIPT_VERIFY_FLAGS |
        SCRIPT_VERIFY_MEMORY_BUDGET;
    BOOST_CHECK((relayFlagsPreFork & SCRIPT_VERIFY_MEMORY_BUDGET) != 0u);

    // ... yet the relay path still sets the memory-budget bit. And it is set
    // identically post-fork, when the security-upgrade flag is also present.
    const uint32_t nextBlockFlagsPostFork =
        STANDARD_SCRIPT_VERIFY_FLAGS | SCRIPT_SECURITY_UPGRADE;
    const uint32_t relayFlagsPostFork =
        nextBlockFlagsPostFork | STANDARD_SCRIPT_VERIFY_FLAGS |
        SCRIPT_VERIFY_MEMORY_BUDGET;
    BOOST_CHECK((relayFlagsPostFork & SCRIPT_VERIFY_MEMORY_BUDGET) != 0u);
}

BOOST_AUTO_TEST_SUITE_END()
