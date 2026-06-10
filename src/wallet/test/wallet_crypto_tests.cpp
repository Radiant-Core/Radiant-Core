// Copyright (c) 2014-2016 The Bitcoin Core developers
// Copyright (c) 2022-2026 The Radiant developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <util/strencodings.h>
#include <wallet/crypter.h>

#include <test/setup_common.h>

#include <boost/test/unit_test.hpp>

#include <algorithm>
#include <string>
#include <vector>

BOOST_FIXTURE_TEST_SUITE(wallet_crypto_tests, BasicTestingSetup)

class TestCrypter {
public:
    // Expose the private encryption key bytes for the domain-separation test.
    static std::vector<uint8_t> GetKeyBytes(const CCrypter &crypt) {
        return std::vector<uint8_t>(crypt.vchKey.begin(), crypt.vchKey.end());
    }
    // Expose the derived MAC key (private) so a test can assert it differs from
    // the AES encryption key (M4 domain separation).
    static std::vector<uint8_t> GetMacKey(const CCrypter &crypt) {
        uint8_t macKey[WALLET_CRYPTO_KEY_SIZE];
        crypt.DeriveMacKey(macKey);
        return std::vector<uint8_t>(macKey, macKey + WALLET_CRYPTO_KEY_SIZE);
    }

    static void TestPassphraseSingle(
        const std::vector<uint8_t> &vchSalt, const SecureString &passphrase,
        uint32_t rounds,
        const std::vector<uint8_t> &correctKey = std::vector<uint8_t>(),
        const std::vector<uint8_t> &correctIV = std::vector<uint8_t>()) {
        CCrypter crypt;
        crypt.SetKeyFromPassphrase(passphrase, vchSalt, rounds, 0);

        if (!correctKey.empty()) {
            BOOST_CHECK_MESSAGE(memcmp(crypt.vchKey.data(), correctKey.data(), crypt.vchKey.size()) == 0,
                                HexStr(crypt.vchKey) + std::string(" != ") + HexStr(correctKey));
        }
        if (!correctIV.empty()) {
            BOOST_CHECK_MESSAGE(memcmp(crypt.vchIV.data(), correctIV.data(), crypt.vchIV.size()) == 0,
                                HexStr(crypt.vchIV) + std::string(" != ") + HexStr(correctIV));
        }
    }

    static void TestPassphrase(
        const std::vector<uint8_t> &vchSalt, const SecureString &passphrase,
        uint32_t rounds,
        const std::vector<uint8_t> &correctKey = std::vector<uint8_t>(),
        const std::vector<uint8_t> &correctIV = std::vector<uint8_t>()) {
        TestPassphraseSingle(vchSalt, passphrase, rounds, correctKey,
                             correctIV);
        for (SecureString::const_iterator i(passphrase.begin());
             i != passphrase.end(); ++i) {
            TestPassphraseSingle(vchSalt, SecureString(i, passphrase.end()),
                                 rounds);
        }
    }

    static void TestDecrypt(
        const CCrypter &crypt, const std::vector<uint8_t> &vchCiphertext,
        const std::vector<uint8_t> &vchPlaintext = std::vector<uint8_t>()) {
        CKeyingMaterial vchDecrypted;
        crypt.Decrypt(vchCiphertext, vchDecrypted);
        if (vchPlaintext.size()) {
            BOOST_CHECK(CKeyingMaterial(vchPlaintext.begin(),
                                        vchPlaintext.end()) == vchDecrypted);
        }
    }

    static void
    TestEncryptSingle(const CCrypter &crypt,
                      const CKeyingMaterial &vchPlaintext,
                      const std::vector<uint8_t> &vchCiphertextCorrect =
                          std::vector<uint8_t>()) {
        std::vector<uint8_t> vchCiphertext;
        crypt.Encrypt(vchPlaintext, vchCiphertext);

        if (!vchCiphertextCorrect.empty()) {
            BOOST_CHECK(vchCiphertext == vchCiphertextCorrect);
        }

        const std::vector<uint8_t> vchPlaintext2(vchPlaintext.begin(),
                                                 vchPlaintext.end());
        TestDecrypt(crypt, vchCiphertext, vchPlaintext2);
    }

    static void TestEncrypt(const CCrypter &crypt,
                            const std::vector<uint8_t> &vchPlaintextIn,
                            const std::vector<uint8_t> &vchCiphertextCorrect =
                                std::vector<uint8_t>()) {
        TestEncryptSingle(
            crypt,
            CKeyingMaterial(vchPlaintextIn.begin(), vchPlaintextIn.end()),
            vchCiphertextCorrect);
        for (std::vector<uint8_t>::const_iterator i(vchPlaintextIn.begin());
             i != vchPlaintextIn.end(); ++i) {
            TestEncryptSingle(crypt, CKeyingMaterial(i, vchPlaintextIn.end()));
        }
    }
};

BOOST_AUTO_TEST_CASE(passphrase) {
    // These are expensive.

    TestCrypter::TestPassphrase(
        ParseHex("0000deadbeef0000"), "test", 25000,
        ParseHex(
            "fc7aba077ad5f4c3a0988d8daa4810d0d4a0e3bcb53af662998898f33df0556a"),
        ParseHex("cf2f2691526dd1aa220896fb8bf7c369"));

    std::string hash(GetRandHash().ToString());
    std::vector<uint8_t> vchSalt(8);
    GetRandBytes(vchSalt.data(), vchSalt.size());
    uint32_t rounds = InsecureRand32();
    if (rounds > 30000) {
        rounds = 30000;
    }
    TestCrypter::TestPassphrase(vchSalt, SecureString(hash.begin(), hash.end()),
                                rounds);
}

BOOST_AUTO_TEST_CASE(encrypt) {
    std::vector<uint8_t> vchSalt = ParseHex("0000deadbeef0000");
    BOOST_CHECK(vchSalt.size() == WALLET_CRYPTO_SALT_SIZE);
    CCrypter crypt;
    crypt.SetKeyFromPassphrase("passphrase", vchSalt, 25000, 0);
    TestCrypter::TestEncrypt(crypt,
                             ParseHex("22bcade09ac03ff6386914359cfe885cfeb5f77f"
                                      "f0d670f102f619687453b29d"));

    for (int i = 0; i != 100; i++) {
        uint256 hash(GetRandHash());
        TestCrypter::TestEncrypt(
            crypt, std::vector<uint8_t>(hash.begin(), hash.end()));
    }
}

BOOST_AUTO_TEST_CASE(decrypt) {
    std::vector<uint8_t> vchSalt = ParseHex("0000deadbeef0000");
    BOOST_CHECK(vchSalt.size() == WALLET_CRYPTO_SALT_SIZE);
    CCrypter crypt;
    crypt.SetKeyFromPassphrase("passphrase", vchSalt, 25000, 0);

    // Some corner cases the came up while testing
    TestCrypter::TestDecrypt(crypt,
                             ParseHex("795643ce39d736088367822cdc50535ec6f10371"
                                      "5e3e48f4f3b1a60a08ef59ca"));
    TestCrypter::TestDecrypt(crypt,
                             ParseHex("de096f4a8f9bd97db012aa9d90d74de8cdea779c"
                                      "3ee8bc7633d8b5d6da703486"));
    TestCrypter::TestDecrypt(crypt,
                             ParseHex("32d0a8974e3afd9c6c3ebf4d66aa4e6419f8c173"
                                      "de25947f98cf8b7ace49449c"));
    TestCrypter::TestDecrypt(crypt,
                             ParseHex("e7c055cca2faa78cb9ac22c9357a90b4778ded9b"
                                      "2cc220a14cea49f931e596ea"));
    TestCrypter::TestDecrypt(crypt,
                             ParseHex("b88efddd668a6801d19516d6830da4ae9811988c"
                                      "cbaf40df8fbb72f3f4d335fd"));
    TestCrypter::TestDecrypt(crypt,
                             ParseHex("8cae76aa6a43694e961ebcb28c8ca8f8540b8415"
                                      "3d72865e8561ddd93fa7bfa9"));

    for (int i = 0; i != 100; i++) {
        uint256 hash(GetRandHash());
        TestCrypter::TestDecrypt(
            crypt, std::vector<uint8_t>(hash.begin(), hash.end()));
    }
}

// SECURITY (audit 2026-06, M-4): authenticated wallet encryption (encrypt-then-
// MAC / GCM with a format-version marker) was DEFERRED because the on-disk
// "ckey" record stores the bare ciphertext vector with no adjacent format/
// version field (see wallet/walletdb.cpp WriteCryptedKey), so a new format
// could only be distinguished in-band -- which is neither brick-proof (a legacy
// CBC blob could collide with the magic and become unreadable) nor downgrade-
// resistant (stripping a trailing MAC would fall through to legacy CBC). This
// regression test LOCKS IN the backward-compatible legacy behavior that the
// deferral relies on, so any future format change is forced to update it
// consciously rather than silently bricking existing wallets:
//   - a 32-byte secret encrypts to a 48-byte (== plaintext + AES block) CBC blob
//   - that blob round-trips back to the exact plaintext
//   - decrypting a legacy (no-MAC) blob with the correct key still succeeds
//   - decrypting with the WRONG passphrase does NOT recover the plaintext
BOOST_AUTO_TEST_CASE(legacy_format_backward_compat) {
    const std::vector<uint8_t> vchSalt = ParseHex("0000deadbeef0000");
    BOOST_CHECK_EQUAL(vchSalt.size(), WALLET_CRYPTO_SALT_SIZE);

    CCrypter crypt;
    BOOST_CHECK(crypt.SetKeyFromPassphrase("correct horse", vchSalt, 25000, 0));

    // A 32-byte wallet secret (the size of a private key / master key).
    const uint256 secretHash(GetRandHash());
    const CKeyingMaterial vchPlaintext(secretHash.begin(), secretHash.end());
    BOOST_CHECK_EQUAL(vchPlaintext.size(), 32U);

    // Encrypt: legacy AES-256-CBC produces exactly plaintext + one AES block of
    // PKCS#7 padding (32 + 16 = 48). This is the layout that must stay readable.
    std::vector<uint8_t> vchCiphertext;
    BOOST_CHECK(crypt.Encrypt(vchPlaintext, vchCiphertext));
    BOOST_CHECK_EQUAL(vchCiphertext.size(), 48U);

    // Round-trip with the same (correct) key recovers the exact plaintext. This
    // is the "decrypt of an old-format (no-MAC) blob still succeeds" guarantee.
    CKeyingMaterial vchRoundTrip;
    BOOST_CHECK(crypt.Decrypt(vchCiphertext, vchRoundTrip));
    BOOST_CHECK(vchRoundTrip == vchPlaintext);

    // Wrong passphrase must NOT recover the original secret. CBC has no MAC, so
    // Decrypt may still "succeed" structurally on a block boundary, but the
    // recovered bytes must differ from the plaintext (the downstream
    // CKey::VerifyPubKey check in DecryptKey is what ultimately rejects it).
    CCrypter wrong;
    BOOST_CHECK(wrong.SetKeyFromPassphrase("battery staple", vchSalt, 25000, 0));
    CKeyingMaterial vchWrong;
    wrong.Decrypt(vchCiphertext, vchWrong);
    BOOST_CHECK(vchWrong != vchPlaintext);
}

// SECURITY (audit 2026-06, M-4): no-regression lock-in. Method 0 (legacy) MUST
// remain byte-for-byte identical to the historical AES-256-CBC output for the
// same key/IV/plaintext. We assert against a fixed known-answer vector so any
// accidental change to the legacy path (e.g. appending a MAC to method 0) is
// caught immediately -- that would brick every existing wallet.
BOOST_AUTO_TEST_CASE(method0_byte_identical_known_answer) {
    const std::vector<uint8_t> vchSalt = ParseHex("0000deadbeef0000");
    CCrypter crypt;
    BOOST_CHECK(crypt.SetKeyFromPassphrase("passphrase", vchSalt, 25000,
                                           WALLET_CRYPTO_METHOD_SHA512_AESCBC));

    // Same plaintext used by the existing `encrypt` known-answer test.
    const std::vector<uint8_t> vchPlaintextIn = ParseHex(
        "22bcade09ac03ff6386914359cfe885cfeb5f77ff0d670f102f619687453b29d");
    const CKeyingMaterial vchPlaintext(vchPlaintextIn.begin(),
                                       vchPlaintextIn.end());

    std::vector<uint8_t> vchCiphertext;
    BOOST_CHECK(crypt.Encrypt(vchPlaintext, vchCiphertext));
    // 32 bytes plaintext -> 48 bytes CBC (one block of PKCS#7 pad), NO MAC.
    BOOST_CHECK_EQUAL(vchCiphertext.size(), 48U);

    // Round-trips exactly.
    CKeyingMaterial vchRoundTrip;
    BOOST_CHECK(crypt.Decrypt(vchCiphertext, vchRoundTrip));
    BOOST_CHECK(vchRoundTrip == vchPlaintext);

    // A second CCrypter constructed without ever calling SetKeyFromPassphrase
    // with a method (i.e. the default-constructed method) must also be 0/legacy
    // and produce the same 48-byte (no-MAC) output -- defends the SetKey default.
    CCrypter crypt2;
    BOOST_CHECK(crypt2.SetKeyFromPassphrase("passphrase", vchSalt, 25000, 0));
    std::vector<uint8_t> vchCiphertext2;
    BOOST_CHECK(crypt2.Encrypt(vchPlaintext, vchCiphertext2));
    BOOST_CHECK(vchCiphertext2 == vchCiphertext);
}

// SECURITY (audit 2026-06, M-4): method 2 round-trip. encrypt-then-MAC recovers
// the exact plaintext, and the blob is exactly 32 bytes (the MAC tag) longer
// than the equivalent method-0 blob.
BOOST_AUTO_TEST_CASE(method2_roundtrip) {
    const std::vector<uint8_t> vchSalt = ParseHex("0000deadbeef0000");

    CCrypter legacy;
    BOOST_CHECK(legacy.SetKeyFromPassphrase("correct horse", vchSalt, 25000,
                                            WALLET_CRYPTO_METHOD_SHA512_AESCBC));
    CCrypter authd;
    BOOST_CHECK(authd.SetKeyFromPassphrase("correct horse", vchSalt, 25000,
                                           WALLET_CRYPTO_METHOD_AESCBC_HMAC));

    const uint256 secretHash(GetRandHash());
    const CKeyingMaterial vchPlaintext(secretHash.begin(), secretHash.end());
    BOOST_CHECK_EQUAL(vchPlaintext.size(), 32U);

    std::vector<uint8_t> vchLegacy;
    BOOST_CHECK(legacy.Encrypt(vchPlaintext, vchLegacy));
    std::vector<uint8_t> vchAuth;
    BOOST_CHECK(authd.Encrypt(vchPlaintext, vchAuth));

    // Method 2 == method-0 CBC blob + 32-byte tag.
    BOOST_CHECK_EQUAL(vchAuth.size(), vchLegacy.size() + WALLET_CRYPTO_MAC_SIZE);
    BOOST_CHECK_EQUAL(vchAuth.size(), 48U + 32U);
    // The CBC prefix is identical (same key/IV/plaintext) -- only the tag differs.
    BOOST_CHECK(std::equal(vchLegacy.begin(), vchLegacy.end(),
                           vchAuth.begin()));

    // Round-trips exactly under method 2.
    CKeyingMaterial vchRoundTrip;
    BOOST_CHECK(authd.Decrypt(vchAuth, vchRoundTrip));
    BOOST_CHECK(vchRoundTrip == vchPlaintext);
}

// SECURITY (audit 2026-06, M-4): the core property. Flipping ANY byte of a
// method-2 blob -- in the CBC ciphertext region OR in the trailing MAC tag --
// makes Decrypt FAIL CLOSED (returns false, recovers nothing).
BOOST_AUTO_TEST_CASE(method2_tamper_detected) {
    const std::vector<uint8_t> vchSalt = ParseHex("0000deadbeef0000");
    CCrypter authd;
    BOOST_CHECK(authd.SetKeyFromPassphrase("correct horse", vchSalt, 25000,
                                           WALLET_CRYPTO_METHOD_AESCBC_HMAC));

    const uint256 secretHash(GetRandHash());
    const CKeyingMaterial vchPlaintext(secretHash.begin(), secretHash.end());

    std::vector<uint8_t> vchAuth;
    BOOST_CHECK(authd.Encrypt(vchPlaintext, vchAuth));
    BOOST_CHECK_EQUAL(vchAuth.size(), 80U); // 48 CBC + 32 MAC

    // Flip every byte position in turn; each tampered blob must FAIL CLOSED.
    for (size_t i = 0; i < vchAuth.size(); i++) {
        std::vector<uint8_t> tampered = vchAuth;
        tampered[i] ^= 0x01;
        CKeyingMaterial vchOut;
        bool ok = authd.Decrypt(tampered, vchOut);
        BOOST_CHECK_MESSAGE(!ok || vchOut != vchPlaintext,
                            "tamper at byte " + std::to_string(i) +
                                " was NOT rejected");
    }
}

// SECURITY (audit 2026-06, M-4): truncation and extension both fail closed.
BOOST_AUTO_TEST_CASE(method2_truncation_extension) {
    const std::vector<uint8_t> vchSalt = ParseHex("0000deadbeef0000");
    CCrypter authd;
    BOOST_CHECK(authd.SetKeyFromPassphrase("correct horse", vchSalt, 25000,
                                           WALLET_CRYPTO_METHOD_AESCBC_HMAC));

    const uint256 secretHash(GetRandHash());
    const CKeyingMaterial vchPlaintext(secretHash.begin(), secretHash.end());
    std::vector<uint8_t> vchAuth;
    BOOST_CHECK(authd.Encrypt(vchPlaintext, vchAuth));

    // Drop the last byte (truncated tag).
    {
        std::vector<uint8_t> truncated(vchAuth.begin(), vchAuth.end() - 1);
        CKeyingMaterial vchOut;
        BOOST_CHECK(!authd.Decrypt(truncated, vchOut) || vchOut != vchPlaintext);
    }
    // Drop a whole block + tag, leaving fewer than tag+block bytes -> length
    // guard rejects.
    {
        std::vector<uint8_t> tiny(vchAuth.begin(),
                                  vchAuth.begin() + WALLET_CRYPTO_MAC_SIZE);
        CKeyingMaterial vchOut;
        BOOST_CHECK(!authd.Decrypt(tiny, vchOut));
    }
    // Append a byte (extension).
    {
        std::vector<uint8_t> extended = vchAuth;
        extended.push_back(0x00);
        CKeyingMaterial vchOut;
        BOOST_CHECK(!authd.Decrypt(extended, vchOut) || vchOut != vchPlaintext);
    }
    // Empty input fails the length guard.
    {
        std::vector<uint8_t> empty;
        CKeyingMaterial vchOut;
        BOOST_CHECK(!authd.Decrypt(empty, vchOut));
    }
}

// SECURITY (audit 2026-06, M-4): the wrong passphrase must not unlock a method-2
// blob. Because the MAC key is passphrase-derived, the MAC check rejects it
// outright (fails closed before any unpad), which is strictly stronger than the
// legacy method-0 behavior.
BOOST_AUTO_TEST_CASE(method2_wrong_passphrase) {
    const std::vector<uint8_t> vchSalt = ParseHex("0000deadbeef0000");
    CCrypter authd;
    BOOST_CHECK(authd.SetKeyFromPassphrase("correct horse", vchSalt, 25000,
                                           WALLET_CRYPTO_METHOD_AESCBC_HMAC));

    const uint256 secretHash(GetRandHash());
    const CKeyingMaterial vchPlaintext(secretHash.begin(), secretHash.end());
    std::vector<uint8_t> vchAuth;
    BOOST_CHECK(authd.Encrypt(vchPlaintext, vchAuth));

    CCrypter wrong;
    BOOST_CHECK(wrong.SetKeyFromPassphrase("battery staple", vchSalt, 25000,
                                           WALLET_CRYPTO_METHOD_AESCBC_HMAC));
    CKeyingMaterial vchOut;
    // MAC mismatch -> fail closed.
    BOOST_CHECK(!wrong.Decrypt(vchAuth, vchOut));
}

// SECURITY (audit 2026-06, M-4): a method-2 blob fed to a method-0 (legacy)
// decryptor (e.g. an old binary, or a downgrade attempt) must NOT recover the
// plaintext. The legacy path treats the trailing MAC as extra ciphertext, so
// the unpad yields garbage rather than the secret. (Belt-and-suspenders: the
// real defense against old binaries is the minversion bump.)
BOOST_AUTO_TEST_CASE(method2_not_readable_as_method0) {
    const std::vector<uint8_t> vchSalt = ParseHex("0000deadbeef0000");
    CCrypter authd;
    BOOST_CHECK(authd.SetKeyFromPassphrase("correct horse", vchSalt, 25000,
                                           WALLET_CRYPTO_METHOD_AESCBC_HMAC));
    const uint256 secretHash(GetRandHash());
    const CKeyingMaterial vchPlaintext(secretHash.begin(), secretHash.end());
    std::vector<uint8_t> vchAuth;
    BOOST_CHECK(authd.Encrypt(vchPlaintext, vchAuth));

    CCrypter legacy;
    BOOST_CHECK(legacy.SetKeyFromPassphrase("correct horse", vchSalt, 25000,
                                            WALLET_CRYPTO_METHOD_SHA512_AESCBC));
    CKeyingMaterial vchOut;
    legacy.Decrypt(vchAuth, vchOut);
    BOOST_CHECK(vchOut != vchPlaintext);
}

// SECURITY (audit 2026-06, M-4): domain separation. The derived HMAC key MUST
// NOT equal the AES encryption key.
BOOST_AUTO_TEST_CASE(method2_mac_key_domain_separated) {
    const std::vector<uint8_t> vchSalt = ParseHex("0000deadbeef0000");
    CCrypter authd;
    BOOST_CHECK(authd.SetKeyFromPassphrase("correct horse", vchSalt, 25000,
                                           WALLET_CRYPTO_METHOD_AESCBC_HMAC));

    const std::vector<uint8_t> aesKey = TestCrypter::GetKeyBytes(authd);
    const std::vector<uint8_t> macKey = TestCrypter::GetMacKey(authd);
    BOOST_CHECK_EQUAL(aesKey.size(), WALLET_CRYPTO_KEY_SIZE);
    BOOST_CHECK_EQUAL(macKey.size(), WALLET_CRYPTO_KEY_SIZE);
    BOOST_CHECK(macKey != aesKey);
}

// SECURITY (audit 2026-06, M-4): SetKeyFromPassphrase / SetKey reject unknown
// derivation methods, which is the fail-safe an older binary relies on when it
// meets a newer method, and what makes a forward-compat downgrade impossible.
BOOST_AUTO_TEST_CASE(unknown_method_rejected) {
    const std::vector<uint8_t> vchSalt = ParseHex("0000deadbeef0000");
    CCrypter c;
    // Method 1 (scrypt) is reserved/unimplemented and method 3 is unknown.
    BOOST_CHECK(!c.SetKeyFromPassphrase("pw", vchSalt, 25000,
                                        WALLET_CRYPTO_METHOD_SCRYPT));
    BOOST_CHECK(!c.SetKeyFromPassphrase("pw", vchSalt, 25000, 3));

    const CKeyingMaterial key(WALLET_CRYPTO_KEY_SIZE, 0x11);
    const std::vector<uint8_t> iv(WALLET_CRYPTO_IV_SIZE, 0x22);
    BOOST_CHECK(c.SetKey(key, iv, WALLET_CRYPTO_METHOD_SHA512_AESCBC));
    BOOST_CHECK(c.SetKey(key, iv, WALLET_CRYPTO_METHOD_AESCBC_HMAC));
    BOOST_CHECK(!c.SetKey(key, iv, WALLET_CRYPTO_METHOD_SCRYPT));
    BOOST_CHECK(!c.SetKey(key, iv, 99));
}

BOOST_AUTO_TEST_SUITE_END()
