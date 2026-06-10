// Copyright (c) 2026 The Radiant Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// Script-level tests for OP_BLAKE3 (0xee) and OP_K12 (0xef).
// These exercise the opcodes through EvalScript (the consensus entry point),
// not just the underlying CBlake3 / CK12 hash classes (those are covered in
// crypto_tests.cpp). The goal is to confirm:
//   - The opcodes are disabled when SCRIPT_ENHANCED_REFERENCES is off
//   - They produce the expected hash on the stack when the flag is on
//   - They fail with the right error code on oversize input
//   - Both opcodes report INVALID_OPERAND_SIZE on oversize (symmetric)

#include <crypto/blake3.h>
#include <crypto/k12.h>
#include <policy/policy.h>
#include <script/interpreter.h>
#include <script/script_flags.h>
#include <util/strencodings.h>

#include <test/setup_common.h>

#include <boost/test/unit_test.hpp>

#include <vector>

typedef std::vector<uint8_t> valtype;
typedef std::vector<valtype> stacktype;

BOOST_FIXTURE_TEST_SUITE(script_blake3_k12_tests, BasicTestingSetup)

static stacktype RunOk(const stacktype &original_stack, const CScript &script,
                       uint32_t flags) {
    BaseSignatureChecker sigchecker;
    auto const null_context = std::nullopt;
    ScriptError err = ScriptError::OK;
    stacktype stack{original_stack};
    bool r = EvalScript(stack, script, flags, sigchecker, null_context, &err);
    BOOST_CHECK_MESSAGE(r, "EvalScript returned false; err=" << int(err));
    return stack;
}

static void RunErr(const stacktype &original_stack, const CScript &script,
                   uint32_t flags, ScriptError expected_error) {
    BaseSignatureChecker sigchecker;
    auto const null_context = std::nullopt;
    ScriptError err = ScriptError::OK;
    stacktype stack{original_stack};
    bool r = EvalScript(stack, script, flags, sigchecker, null_context, &err);
    BOOST_CHECK(!r);
    BOOST_CHECK_EQUAL(int(err), int(expected_error));
}

// SCRIPT_ENHANCED_REFERENCES must be set for the hash opcodes to be enabled.
// Use STANDARD_SCRIPT_VERIFY_FLAGS | SCRIPT_ENHANCED_REFERENCES so the test
// runs under the same flag set the mempool uses post-ER activation.
static constexpr uint32_t ER_FLAGS =
    STANDARD_SCRIPT_VERIFY_FLAGS | SCRIPT_ENHANCED_REFERENCES;

// Same as ER_FLAGS but with the 2026-06 security-audit hardening active. Under
// this flag the K12 single-node bound tightens to <8192 (raw max 8191) and the
// per-script memory / opcode-cost budgets are enforced.
static constexpr uint32_t SECURITY_FLAGS = ER_FLAGS | SCRIPT_SECURITY_UPGRADE;

BOOST_AUTO_TEST_CASE(blake3_disabled_without_er_flag) {
    // Without SCRIPT_ENHANCED_REFERENCES, OP_BLAKE3 must behave as a disabled
    // opcode: script fails with DISABLED_OPCODE before consuming the input.
    valtype input{0x00};
    RunErr({input}, CScript() << OP_BLAKE3, 0, ScriptError::DISABLED_OPCODE);
    RunErr({input}, CScript() << OP_BLAKE3,
           STANDARD_SCRIPT_VERIFY_FLAGS, ScriptError::DISABLED_OPCODE);
}

BOOST_AUTO_TEST_CASE(k12_disabled_without_er_flag) {
    valtype input{0x00};
    RunErr({input}, CScript() << OP_K12, 0, ScriptError::DISABLED_OPCODE);
    RunErr({input}, CScript() << OP_K12,
           STANDARD_SCRIPT_VERIFY_FLAGS, ScriptError::DISABLED_OPCODE);
}

BOOST_AUTO_TEST_CASE(blake3_basic_hash_via_script) {
    // BLAKE3 of empty input — spec vector:
    // af1349b9f5f9a1a6a0404dea36dcc9499bcb25c9adc112b7cc9a93cae41f3262
    valtype empty;
    valtype expected_empty = ParseHex(
        "af1349b9f5f9a1a6a0404dea36dcc9499bcb25c9adc112b7cc9a93cae41f3262");
    stacktype result = RunOk({empty}, CScript() << OP_BLAKE3, ER_FLAGS);
    BOOST_CHECK(result.size() == 1);
    BOOST_CHECK(result.back() == expected_empty);

    // BLAKE3 of single byte 0x00 — spec vector:
    // 2d3adedff11b61f14c886e35afa036736dcd87a74d27b5c1510225d0f592e213
    valtype b0{0x00};
    valtype expected_b0 = ParseHex(
        "2d3adedff11b61f14c886e35afa036736dcd87a74d27b5c1510225d0f592e213");
    result = RunOk({b0}, CScript() << OP_BLAKE3, ER_FLAGS);
    BOOST_CHECK(result.back() == expected_b0);
}

BOOST_AUTO_TEST_CASE(k12_basic_hash_via_script) {
    // K12("", "") — spec vector:
    // 1ac2d450fc3b4205d19da7bfca1b37513c0803577ac7167f06fe2ce1f0ef39e5
    valtype empty;
    valtype expected_empty = ParseHex(
        "1ac2d450fc3b4205d19da7bfca1b37513c0803577ac7167f06fe2ce1f0ef39e5");
    stacktype result = RunOk({empty}, CScript() << OP_K12, ER_FLAGS);
    BOOST_CHECK(result.size() == 1);
    BOOST_CHECK(result.back() == expected_empty);
}

BOOST_AUTO_TEST_CASE(blake3_at_chunk_boundary_succeeds) {
    // 1024-byte input (CHUNK_LEN) must succeed.
    valtype input(size_t{CBlake3::CHUNK_LEN});
    for (size_t i = 0; i < input.size(); ++i) {
        input[i] = static_cast<uint8_t>(i % 251);
    }
    stacktype result = RunOk({input}, CScript() << OP_BLAKE3, ER_FLAGS);
    BOOST_CHECK_EQUAL(result.back().size(), size_t{CBlake3::OUTPUT_SIZE});
}

BOOST_AUTO_TEST_CASE(blake3_over_chunk_boundary_fails_with_operand_size) {
    // 1025-byte input — one past CHUNK_LEN. After the symmetric pre-size
    // check at src/script/interpreter.cpp:1098, this must report
    // INVALID_OPERAND_SIZE rather than the previous INVALID_STACK_OPERATION
    // catch-all. Regression: ensure the error code stays distinct so SDKs
    // and observability dashboards can detect oversize-input mistakes.
    valtype input(size_t{CBlake3::CHUNK_LEN} + 1, 0xab);
    RunErr({input}, CScript() << OP_BLAKE3, ER_FLAGS,
           ScriptError::INVALID_OPERAND_SIZE);
}

BOOST_AUTO_TEST_CASE(k12_at_max_input_boundary_succeeds) {
    // 8191-byte input (MAX_INPUT_LEN) must succeed under both flag sets.
    BOOST_CHECK_EQUAL(size_t{CK12::MAX_INPUT_LEN}, size_t{8191});
    valtype input(size_t{CK12::MAX_INPUT_LEN});
    for (size_t i = 0; i < input.size(); ++i) {
        input[i] = static_cast<uint8_t>(i % 251);
    }
    stacktype result = RunOk({input}, CScript() << OP_K12, ER_FLAGS);
    BOOST_CHECK_EQUAL(result.back().size(), size_t{CK12::OUTPUT_SIZE});
    result = RunOk({input}, CScript() << OP_K12, SECURITY_FLAGS);
    BOOST_CHECK_EQUAL(result.back().size(), size_t{CK12::OUTPUT_SIZE});
}

BOOST_AUTO_TEST_CASE(k12_at_8192_symmetric_across_security_flag) {
    // F1 (K12 off-spec at exactly 8192): the spec-correct raw maximum is 8191
    // because Finalize() appends a 1-byte length_encode(0) so the padded length
    // must stay < 8192. An 8192-byte raw input is therefore off-spec.
    //
    //  - Pre-activation (ER_FLAGS, no SecurityUpgrade): EXACT historical mainnet
    //    behavior is preserved. The size guard rejects only >8192, and CK12's
    //    hard limit is still 8192, so an 8192-byte input still produces its
    //    historical (off-spec) 32-byte hash and the script SUCCEEDS. Tightening
    //    this would be a retroactive hard fork.
    //  - Post-activation (SECURITY_FLAGS): the size guard rejects >8191, so 8192
    //    is rejected up front with INVALID_OPERAND_SIZE.
    valtype input(size_t{8192}, 0xcd);
    stacktype result = RunOk({input}, CScript() << OP_K12, ER_FLAGS);
    BOOST_CHECK_EQUAL(result.back().size(), size_t{CK12::OUTPUT_SIZE});
    RunErr({input}, CScript() << OP_K12, SECURITY_FLAGS,
           ScriptError::INVALID_OPERAND_SIZE);
}

BOOST_AUTO_TEST_CASE(k12_over_max_input_fails_with_operand_size) {
    // 8193-byte input — past every bound; rejected with INVALID_OPERAND_SIZE
    // under both flag sets (legacy guard rejects >8192, security guard >8191).
    valtype input(size_t{8193}, 0xcd);
    RunErr({input}, CScript() << OP_K12, ER_FLAGS,
           ScriptError::INVALID_OPERAND_SIZE);
    RunErr({input}, CScript() << OP_K12, SECURITY_FLAGS,
           ScriptError::INVALID_OPERAND_SIZE);
}

BOOST_AUTO_TEST_CASE(blake3_empty_stack_fails) {
    RunErr({}, CScript() << OP_BLAKE3, ER_FLAGS,
           ScriptError::INVALID_STACK_OPERATION);
}

BOOST_AUTO_TEST_CASE(k12_empty_stack_fails) {
    RunErr({}, CScript() << OP_K12, ER_FLAGS,
           ScriptError::INVALID_STACK_OPERATION);
}

BOOST_AUTO_TEST_CASE(blake3_consumes_one_pushes_one) {
    // Stack effect: (in -- hash). Confirm the input is consumed and exactly
    // one 32-byte element is pushed.
    valtype other{0x42};
    valtype input{0x00};
    stacktype result = RunOk({other, input}, CScript() << OP_BLAKE3, ER_FLAGS);
    BOOST_CHECK_EQUAL(result.size(), 2);
    BOOST_CHECK(result[0] == other);
    BOOST_CHECK_EQUAL(result[1].size(), size_t{CBlake3::OUTPUT_SIZE});
}

BOOST_AUTO_TEST_CASE(k12_consumes_one_pushes_one) {
    valtype other{0x42};
    valtype input{0x00};
    stacktype result = RunOk({other, input}, CScript() << OP_K12, ER_FLAGS);
    BOOST_CHECK_EQUAL(result.size(), 2);
    BOOST_CHECK(result[0] == other);
    BOOST_CHECK_EQUAL(result[1].size(), size_t{CK12::OUTPUT_SIZE});
}

// ---------------------------------------------------------------------------
// HIGH-1: the UTXO twins 0xd4/0xd5 must be DISABLED when
// SCRIPT_ENHANCED_REFERENCES is off, aligning with their OUTPUT twins
// 0xd6/0xd7. Before the fix only 0xd6/0xd7 were in the disabled list.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(refhash_utxo_twins_disabled_without_er_flag) {
    // 0xd4 = OP_REFHASHDATASUMMARY_UTXO
    RunErr({}, CScript() << OP_REFHASHDATASUMMARY_UTXO, 0,
           ScriptError::DISABLED_OPCODE);
    RunErr({}, CScript() << OP_REFHASHDATASUMMARY_UTXO,
           STANDARD_SCRIPT_VERIFY_FLAGS, ScriptError::DISABLED_OPCODE);
    // 0xd5 = OP_REFHASHVALUESUM_UTXOS
    RunErr({}, CScript() << OP_REFHASHVALUESUM_UTXOS, 0,
           ScriptError::DISABLED_OPCODE);
    RunErr({}, CScript() << OP_REFHASHVALUESUM_UTXOS,
           STANDARD_SCRIPT_VERIFY_FLAGS, ScriptError::DISABLED_OPCODE);
}

// ---------------------------------------------------------------------------
// H1/H2: per-script peak stack-memory budget. A script that repeatedly OP_DUPs
// a large element balloons stack bytes far past any legitimate use. With
// SCRIPT_SECURITY_UPGRADE (consensus, future) OR SCRIPT_VERIFY_MEMORY_BUDGET
// (relay/policy, active now) the bomb is rejected (STACK_SIZE) before it
// exhausts memory; without either flag the historical behavior (no byte budget)
// is preserved and the same script succeeds.
//
// The byte budget is MAX_SCRIPT_STACK_MEMORY_USAGE (128 MB). The dup count is
// derived from that constant so these tests stay correct if the budget changes.
// We use a 2 MB element to keep the script (opcode count) and per-element
// overhead small relative to the budget.
// ---------------------------------------------------------------------------
static constexpr size_t MEM_ELEM = size_t{2} * 1000 * 1000; // 2 MB element

// Smallest number of dups whose resulting stack (1 seed + dups copies) strictly
// exceeds the budget.
static int DupsToExceedBudget() {
    // (1 + dups) * MEM_ELEM > MAX_SCRIPT_STACK_MEMORY_USAGE
    size_t needElems = (size_t(MAX_SCRIPT_STACK_MEMORY_USAGE) / MEM_ELEM) + 1;
    return int(needElems); // dups; total elems = needElems + 1, strictly over
}

BOOST_AUTO_TEST_CASE(stack_memory_bomb_rejected_under_security_upgrade) {
    valtype big(MEM_ELEM, 0x00);
    int const dups = DupsToExceedBudget();
    CScript script;
    for (int i = 0; i < dups; ++i) {
        script << OP_DUP;
    }

    // (1) Rejected under SCRIPT_SECURITY_UPGRADE (consensus, post-fork).
    RunErr({big}, script, SECURITY_FLAGS, ScriptError::STACK_MEMORY);

    // (1) Also rejected under a flag-set that only carries the relay/policy
    // SCRIPT_VERIFY_MEMORY_BUDGET bit (no SecurityUpgrade) — proving the mempool
    // guard closes the memory bomb pre-fork.
    RunErr({big}, script, ER_FLAGS | SCRIPT_VERIFY_MEMORY_BUDGET,
           ScriptError::STACK_MEMORY);

    // (3) Under plain ER_FLAGS (neither budget flag) the bomb SUCCEEDS: this is
    // the historical mainnet behavior and must be unchanged before the fork, so
    // the element-count cap (MAX_STACK_SIZE) is the only limit and is not hit.
    stacktype result = RunOk({big}, script, ER_FLAGS);
    BOOST_CHECK_EQUAL(result.size(), size_t(dups) + 1);
}

// (2) A script that drives the stack to just UNDER the budget must pass under
// both budget-enforcing flag sets. We seed one 2 MB element and dup it up to
// one element short of the budget.
BOOST_AUTO_TEST_CASE(stack_memory_just_under_budget_passes) {
    valtype big(MEM_ELEM, 0x11);
    // Largest element count whose total stays <= budget.
    size_t maxElems = size_t(MAX_SCRIPT_STACK_MEMORY_USAGE) / MEM_ELEM;
    BOOST_REQUIRE(maxElems >= 2);
    int const dups = int(maxElems) - 1; // seed (1) + dups == maxElems
    CScript script;
    for (int i = 0; i < dups; ++i) {
        script << OP_DUP;
    }

    stacktype result = RunOk({big}, script, SECURITY_FLAGS);
    BOOST_CHECK_EQUAL(result.size(), maxElems);
    result = RunOk({big}, script, ER_FLAGS | SCRIPT_VERIFY_MEMORY_BUDGET);
    BOOST_CHECK_EQUAL(result.size(), maxElems);
}

// (1, boundary) Crossing the budget by a single byte must reject. Seed the
// stack to exactly the budget with full 2 MB elements plus a 1-byte element,
// then OP_DUP so the cumulative total ticks over the budget.
BOOST_AUTO_TEST_CASE(stack_memory_one_byte_over_budget_rejected) {
    size_t maxElems = size_t(MAX_SCRIPT_STACK_MEMORY_USAGE) / MEM_ELEM;
    BOOST_REQUIRE_EQUAL(size_t(MAX_SCRIPT_STACK_MEMORY_USAGE) % MEM_ELEM, 0u);
    valtype big(MEM_ELEM, 0x22);
    valtype onebyte{0x33};
    // Seed: maxElems full elements summing to exactly the budget, plus a 1-byte
    // element on top. Initial total = budget + 1, so the very first post-opcode
    // check rejects. Use OP_DUP of the 1-byte top to trigger an opcode whose
    // post-check sees total > budget.
    stacktype seed;
    for (size_t i = 0; i < maxElems; ++i) {
        seed.push_back(big);
    }
    seed.push_back(onebyte);
    // total now = budget + 1; an OP_NOP-free op that mutates then checks:
    CScript script;
    script << OP_DUP; // pushes another 1-byte copy; total = budget + 2
    RunErr(seed, script, SECURITY_FLAGS, ScriptError::STACK_MEMORY);
    RunErr(seed, script, ER_FLAGS | SCRIPT_VERIFY_MEMORY_BUDGET,
           ScriptError::STACK_MEMORY);
}

// (4) The incremental counter must equal the naive full-sum across a mixed
// push / dup / cat / drop sequence, so a LEGITIMATE script that holds two large
// elements simultaneously (well under budget) is NOT falsely rejected. This
// exercises the size-changing paths (OP_DUP/OP_2DUP push copies, OP_CAT grows a
// top element in place then pops, OP_SIZE pushes then OP_DROP/OP_2DROP shrink)
// that the incremental accounting must track exactly.
BOOST_AUTO_TEST_CASE(stack_memory_incremental_matches_naive_mixed_ops) {
    // Two 8 MB elements -> 16 MB live, far below the 128 MB budget.
    valtype a(size_t{8} * 1000 * 1000, 0xaa);
    valtype b(size_t{8} * 1000 * 1000, 0xbb);

    // Sequence: stack starts {a, b}.
    //  OP_2DUP  -> {a, b, a, b}        (push two copies; 32 MB)
    //  OP_CAT   -> {a, b, (a||b)}      (cat top two into one 16 MB element)
    //  OP_SIZE  -> {a, b, (a||b), len} (push numeric size of top)
    //  OP_DROP  -> {a, b, (a||b)}      (drop the size)
    //  OP_DUP   -> {a, b, (a||b), (a||b)} (duplicate the 16 MB concat; 48 MB)
    //  OP_2DROP -> {a, b}              (drop the two concatenations)
    // Final stack: {a, b}. No intermediate peak exceeds the budget.
    CScript script;
    script << OP_2DUP << OP_CAT << OP_SIZE << OP_DROP << OP_DUP << OP_2DROP;

    // Must pass under the security flag (budget enforced) — proving the
    // incremental counter never spuriously exceeds the budget for a legitimate
    // two-large-element working set.
    stacktype result = RunOk({a, b}, script, SECURITY_FLAGS);
    BOOST_REQUIRE_EQUAL(result.size(), size_t{2});
    BOOST_CHECK(result[0] == a);
    BOOST_CHECK(result[1] == b);

    // And identically under plain ER_FLAGS (no budget) — same accept decision,
    // confirming the accounting did not change behavior.
    stacktype result2 = RunOk({a, b}, script, ER_FLAGS);
    BOOST_CHECK_EQUAL(result2.size(), size_t{2});
    BOOST_CHECK(result2[1] == b);
}

BOOST_AUTO_TEST_SUITE_END()
