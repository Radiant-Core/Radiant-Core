// Copyright (c) 2009-2016 The Bitcoin Core developers
// Copyright (c) 2022-2026 The Radiant developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#pragma once

#include <keystore.h>
#include <serialize.h>
#include <support/allocators/secure.h>

#include <atomic>

const unsigned int WALLET_CRYPTO_KEY_SIZE = 32;
const unsigned int WALLET_CRYPTO_SALT_SIZE = 8;
const unsigned int WALLET_CRYPTO_IV_SIZE = 16;

// SECURITY (audit 2026-06, M4): CMasterKey::nDerivationMethod values. This
// field is an on-disk, read-on-load version selector that governs how a wallet's
// ciphertext blobs (both the master-key blob and every per-key "ckey" secret)
// are interpreted. It round-trips on disk, so legacy wallets are never misread.
//
//   0 = legacy EVP_sha512() KDF + AES-256-CBC, UNAUTHENTICATED. This is the
//       historical format; method-0 ciphertext layout and bytes are unchanged.
//   1 = RESERVED for the documented-but-unimplemented scrypt() KDF.
//   2 = EVP_sha512() KDF + AES-256-CBC encrypt-then-HMAC-SHA512 (authenticated).
//       Closes the M4 malleability / padding-oracle surface by appending a
//       32-byte MAC tag over (contextLabel || IV || ciphertext) and verifying it
//       constant-time, FAIL-CLOSED, BEFORE any CBC unpad.
//
// An old binary that does not implement method 2 fails safe: SetKeyFromPassphrase
// returns false for an unknown method (cannot unlock, no corruption), and the
// wallet's bumped minversion (FEATURE_WALLETCRYPT_AEAD) causes such a binary to
// refuse to load the wallet outright (DBErrors::TOO_NEW).
const unsigned int WALLET_CRYPTO_METHOD_SHA512_AESCBC = 0;
const unsigned int WALLET_CRYPTO_METHOD_SCRYPT = 1; // reserved, not implemented
const unsigned int WALLET_CRYPTO_METHOD_AESCBC_HMAC = 2;

// Size of the appended authentication tag for method 2 (HMAC-SHA512 truncated to
// 32 bytes). Truncated-MAC is standard practice (e.g. SHA-512/256-style use).
const unsigned int WALLET_CRYPTO_MAC_SIZE = 32;

// SECURITY (audit 2026-06, M3): minimum EVP_BytesToKey(SHA512) iteration count
// for NEWLY encrypted wallets. The historical floor of 25000 rounds was tuned
// for ~2009 hardware (under 0.1s on a 1.86 GHz Pentium M) and is far too weak
// against modern offline brute-forcing of the wallet passphrase.
//
// This floor applies ONLY to new encryptions / passphrase changes: existing
// wallets persist and reuse their own per-wallet nDeriveIterations (see
// CMasterKey), so raising the floor is fully backward compatible -- old wallets
// still decrypt with their stored, lower iteration count. WALLET_CRYPTO_SALT_SIZE
// is deliberately left unchanged (changing it would break existing wallets).
//
// NOTE: the effective clamp for new encryptions is also enforced in
// CWallet::EncryptWallet / ChangeWalletPassphrase (wallet.cpp); that clamp must
// use this same constant for the floor to take effect end-to-end.
const unsigned int WALLET_CRYPTO_KDF_MIN_ITERATIONS = 200000;

/**
 * Private key encryption is done based on a CMasterKey, which holds a salt and
 * random encryption key.
 *
 * CMasterKeys are encrypted using AES-256-CBC using a key derived using
 * derivation method nDerivationMethod (0 == EVP_sha512()) and derivation
 * iterations nDeriveIterations. vchOtherDerivationParameters is provided for
 * alternative algorithms which may require more parameters (such as scrypt).
 *
 * Wallet Private Keys are then encrypted using AES-256-CBC with the
 * double-sha256 of the public key as the IV, and the master key's key as the
 * encryption key (see keystore.[ch]).
 */

/** Master key for wallet encryption */
class CMasterKey {
public:
    std::vector<uint8_t> vchCryptedKey;
    std::vector<uint8_t> vchSalt;
    //! 0 = EVP_sha512()
    //! 1 = scrypt()
    unsigned int nDerivationMethod;
    unsigned int nDeriveIterations;
    //! Use this for more parameters to key derivation, such as the various
    //! parameters to scrypt
    std::vector<uint8_t> vchOtherDerivationParameters;

    SERIALIZE_METHODS(CMasterKey, obj) {
        READWRITE(obj.vchCryptedKey, obj.vchSalt, obj.nDerivationMethod, obj.nDeriveIterations, obj.vchOtherDerivationParameters);
    }

    CMasterKey() {
        // SECURITY (audit 2026-06, M3): default iteration floor for new master
        // keys raised from the legacy 25000 (tuned for a 1.86 GHz Pentium M) to
        // WALLET_CRYPTO_KDF_MIN_ITERATIONS. Existing wallets are unaffected:
        // they deserialize their own stored nDeriveIterations over this default.
        nDeriveIterations = WALLET_CRYPTO_KDF_MIN_ITERATIONS;
        nDerivationMethod = 0;
        vchOtherDerivationParameters = std::vector<uint8_t>(0);
    }
};

typedef std::vector<uint8_t, secure_allocator<uint8_t>> CKeyingMaterial;

namespace wallet_crypto_tests {
class TestCrypter;
}

/** Encryption/decryption context with key information */
class CCrypter {
    // for test access to chKey/chIV
    friend class wallet_crypto_tests::TestCrypter;

private:
    std::vector<uint8_t, secure_allocator<uint8_t>> vchKey;
    std::vector<uint8_t, secure_allocator<uint8_t>> vchIV;
    bool fKeySet;
    //! Which CMasterKey::nDerivationMethod governs Encrypt/Decrypt for this
    //! context. Set by SetKey/SetKeyFromPassphrase. Method 0 = legacy
    //! unauthenticated AES-256-CBC; method 2 = AES-256-CBC encrypt-then-MAC.
    unsigned int nMethod;

    int BytesToKeySHA512AES(const std::vector<uint8_t> &chSalt,
                            const SecureString &strKeyData, int count,
                            uint8_t *key, uint8_t *iv) const;

    //! Derive the method-2 MAC key from the AES encryption key. Domain-separated
    //! from the AES key (HMAC-SHA512(vchKey, "radiant-wallet-mac")[0:32]) so the
    //! MAC key is never equal to the encryption key.
    void DeriveMacKey(uint8_t macKey[WALLET_CRYPTO_KEY_SIZE]) const;

    //! Compute the method-2 tag over (contextLabel || IV || ciphertext).
    void ComputeMac(const uint8_t macKey[WALLET_CRYPTO_KEY_SIZE],
                    const std::vector<uint8_t> &vchCiphertext,
                    uint8_t macOut[WALLET_CRYPTO_MAC_SIZE]) const;

public:
    bool SetKeyFromPassphrase(const SecureString &strKeyData,
                              const std::vector<uint8_t> &chSalt,
                              const unsigned int nRounds,
                              const unsigned int nDerivationMethod);
    bool Encrypt(const CKeyingMaterial &vchPlaintext,
                 std::vector<uint8_t> &vchCiphertext) const;
    bool Decrypt(const std::vector<uint8_t> &vchCiphertext,
                 CKeyingMaterial &vchPlaintext) const;
    //! Set the raw key/IV directly. nDerivationMethodIn selects the (un)authenti-
    //! cated mode; defaults to legacy method 0 to keep existing callers unchanged.
    bool SetKey(const CKeyingMaterial &chNewKey,
                const std::vector<uint8_t> &chNewIV,
                const unsigned int nDerivationMethodIn =
                    WALLET_CRYPTO_METHOD_SHA512_AESCBC);

    void CleanKey() {
        memory_cleanse(vchKey.data(), vchKey.size());
        memory_cleanse(vchIV.data(), vchIV.size());
        fKeySet = false;
    }

    CCrypter() {
        fKeySet = false;
        nMethod = WALLET_CRYPTO_METHOD_SHA512_AESCBC;
        vchKey.resize(WALLET_CRYPTO_KEY_SIZE);
        vchIV.resize(WALLET_CRYPTO_IV_SIZE);
    }

    ~CCrypter() { CleanKey(); }
};

/**
 * Keystore which keeps the private keys encrypted.
 * It derives from the basic key store, which is used if no encryption is
 * active.
 */
class CCryptoKeyStore : public CBasicKeyStore {
private:
    CKeyingMaterial vMasterKey GUARDED_BY(cs_KeyStore);

    //! if fUseCrypto is true, mapKeys must be empty
    //! if fUseCrypto is false, vMasterKey must be empty
    std::atomic<bool> fUseCrypto;

    //! keeps track of whether Unlock has run a thorough check before
    bool fDecryptionThoroughlyChecked;

    //! SECURITY (audit 2026-06, M4): the CMasterKey::nDerivationMethod in force
    //! for the currently-unlocked wallet. This governs how the per-key "ckey"
    //! secrets in mapCryptedKeys are interpreted (method 0 = legacy CBC, method 2
    //! = authenticated CBC+HMAC). It is supplied by the caller on Unlock /
    //! EncryptKeys (the CWallet layer reads it from the matching CMasterKey).
    unsigned int nCryptoMethod GUARDED_BY(cs_KeyStore){
        WALLET_CRYPTO_METHOD_SHA512_AESCBC};

protected:
    using CryptedKeyMap =
        std::map<CKeyID, std::pair<CPubKey, std::vector<uint8_t>>>;

    bool SetCrypted();

    //! will encrypt previously unencrypted keys, using the given derivation
    //! method for the per-key authenticated/legacy mode.
    bool EncryptKeys(CKeyingMaterial &vMasterKeyIn,
                     unsigned int nDerivationMethod =
                         WALLET_CRYPTO_METHOD_SHA512_AESCBC);

    bool Unlock(const CKeyingMaterial &vMasterKeyIn, bool accept_no_keys = false,
                unsigned int nDerivationMethod =
                    WALLET_CRYPTO_METHOD_SHA512_AESCBC);
    CryptedKeyMap mapCryptedKeys GUARDED_BY(cs_KeyStore);

public:
    CCryptoKeyStore()
        : fUseCrypto(false), fDecryptionThoroughlyChecked(false) {}

    bool IsCrypted() const { return fUseCrypto; }
    bool IsLocked() const;
    bool Lock();

    virtual bool AddCryptedKey(const CPubKey &vchPubKey,
                               const std::vector<uint8_t> &vchCryptedSecret);
    bool AddKeyPubKey(const CKey &key, const CPubKey &pubkey) override;
    bool HaveKey(const CKeyID &address) const override;
    bool GetKey(const CKeyID &address, CKey &keyOut) const override;
    bool GetPubKey(const CKeyID &address, CPubKey &vchPubKeyOut) const override;
    std::set<CKeyID> GetKeys() const override;

    /**
     * Wallet status (encrypted, locked) changed.
     * Note: Called without locks held.
     */
    boost::signals2::signal<void(CCryptoKeyStore *wallet)> NotifyStatusChanged;
};
