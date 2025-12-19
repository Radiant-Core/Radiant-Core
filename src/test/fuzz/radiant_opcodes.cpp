// Copyright (c) 2022-2026 The Radiant developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <script/interpreter.h>
#include <script/script.h>
#include <streams.h>
#include <version.h>

#include <test/fuzz/fuzz.h>

/**
 * Fuzz target for Radiant-specific opcodes:
 * - OP_PUSHINPUTREF (unique references)
 * - OP_REQUIREINPUTREF
 * - OP_DISALLOWPUSHINPUTREF
 * - OP_DISALLOWPUSHINPUTREFSIBLING
 * - Transaction introspection opcodes (OP_TXINPUTCOUNT, OP_TXOUTPUTCOUNT, etc.)
 * - OP_STATESEPARATOR, OP_STATESEPARATORINDEX_UTXO
 */
void test_one_input(std::vector<uint8_t> buffer) {
    CDataStream ds(buffer, SER_NETWORK, INIT_PROTO_VERSION);
    try {
        int nVersion;
        ds >> nVersion;
        ds.SetVersion(nVersion);
    } catch (const std::ios_base::failure &) {
        return;
    }

    try {
        const CTransaction tx(deserialize, ds);
        const PrecomputedTransactionData txdata(tx);

        // Standard verification flags including Radiant-specific flags
        uint32_t verify_flags = SCRIPT_VERIFY_P2SH |
                                SCRIPT_VERIFY_DERSIG |
                                SCRIPT_VERIFY_STRICTENC |
                                SCRIPT_VERIFY_MINIMALDATA |
                                SCRIPT_VERIFY_CHECKLOCKTIMEVERIFY |
                                SCRIPT_VERIFY_CHECKSEQUENCEVERIFY |
                                SCRIPT_ENABLE_SIGHASH_FORKID |
                                SCRIPT_VERIFY_LOW_S |
                                SCRIPT_VERIFY_NULLFAIL |
                                SCRIPT_64_BIT_INTEGERS |
                                SCRIPT_NATIVE_INTROSPECTION |
                                SCRIPT_ENHANCED_REFERENCES |
                                SCRIPT_PUSH_TX_STATE |
                                SCRIPT_VERIFY_SIGPUSHONLY |
                                SCRIPT_VERIFY_CLEANSTACK;

        for (unsigned int i = 0; i < tx.vin.size(); ++i) {
            CTxOut prevout;
            ds >> prevout;

            const TransactionSignatureChecker checker{&tx, i, prevout.nValue,
                                                      txdata};

            ScriptError serror;
            auto const null_context = std::nullopt;
            
            // Verify the script - we don't care about the result,
            // just that we don't crash or trigger undefined behavior
            VerifyScript(tx.vin.at(i).scriptSig, prevout.scriptPubKey,
                         verify_flags, checker, null_context, &serror);
        }
    } catch (const std::ios_base::failure &) {
        return;
    } catch (const std::runtime_error &) {
        // Some script operations may throw runtime errors
        return;
    }
}
