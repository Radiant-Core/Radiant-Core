// Copyright (c) 2009-2016 The Bitcoin Core developers
// Copyright (c) 2022-2026 The Radiant developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <wallet/crypter.h>

#include <crypto/aes.h>
#include <crypto/hmac_sha512.h>
#include <crypto/sha512.h>
#include <script/script.h>
#include <script/standard.h>
#include <util/strencodings.h>
#include <util/system.h>

#include <string>
#include <vector>

int CCrypter::BytesToKeySHA512AES(const std::vector<uint8_t> &chSalt,
                                  const SecureString &strKeyData, int count,
                                  uint8_t *key, uint8_t *iv) const {
    // This mimics the behavior of openssl's EVP_BytesToKey with an aes256cbc
    // cipher and sha512 message digest. Because sha512's output size (64b) is
    // greater than the aes256 block size (16b) + aes256 key size (32b), there's
    // no need to process more than once (D_0).
    if (!count || !key || !iv) {
        return 0;
    }

    uint8_t buf[CSHA512::OUTPUT_SIZE];
    CSHA512 di;

    di.Write((const uint8_t *)strKeyData.c_str(), strKeyData.size());
    di.Write(chSalt.data(), chSalt.size());
    di.Finalize(buf);

    for (int i = 0; i != count - 1; i++) {
        di.Reset().Write(buf, sizeof(buf)).Finalize(buf);
    }

    memcpy(key, buf, WALLET_CRYPTO_KEY_SIZE);
    memcpy(iv, buf + WALLET_CRYPTO_KEY_SIZE, WALLET_CRYPTO_IV_SIZE);
    memory_cleanse(buf, sizeof(buf));
    return WALLET_CRYPTO_KEY_SIZE;
}

bool CCrypter::SetKeyFromPassphrase(const SecureString &strKeyData,
                                    const std::vector<uint8_t> &chSalt,
                                    const unsigned int nRounds,
                                    const unsigned int nDerivationMethod) {
    if (nRounds < 1 || chSalt.size() != WALLET_CRYPTO_SALT_SIZE) {
        return false;
    }

    // SECURITY (audit 2026-06, M4): both method 0 (legacy CBC) and method 2
    // (CBC+HMAC) use the SAME EVP_sha512()-based KDF to derive the AES key/IV;
    // they differ only in whether Encrypt/Decrypt append+verify a MAC. Any other
    // (unknown / reserved) method is rejected here, which is exactly the fail-
    // safe an older binary relies on when it encounters a newer method.
    int i = 0;
    if (nDerivationMethod == WALLET_CRYPTO_METHOD_SHA512_AESCBC ||
        nDerivationMethod == WALLET_CRYPTO_METHOD_AESCBC_HMAC) {
        i = BytesToKeySHA512AES(chSalt, strKeyData, nRounds, vchKey.data(),
                                vchIV.data());
    }

    if (i != (int)WALLET_CRYPTO_KEY_SIZE) {
        memory_cleanse(vchKey.data(), vchKey.size());
        memory_cleanse(vchIV.data(), vchIV.size());
        return false;
    }

    nMethod = nDerivationMethod;
    fKeySet = true;
    return true;
}

bool CCrypter::SetKey(const CKeyingMaterial &chNewKey,
                      const std::vector<uint8_t> &chNewIV,
                      const unsigned int nDerivationMethodIn) {
    if (chNewKey.size() != WALLET_CRYPTO_KEY_SIZE ||
        chNewIV.size() != WALLET_CRYPTO_IV_SIZE) {
        return false;
    }
    // Reject unknown methods so callers fail closed rather than silently writing
    // an unauthenticated blob.
    if (nDerivationMethodIn != WALLET_CRYPTO_METHOD_SHA512_AESCBC &&
        nDerivationMethodIn != WALLET_CRYPTO_METHOD_AESCBC_HMAC) {
        return false;
    }

    memcpy(vchKey.data(), chNewKey.data(), chNewKey.size());
    memcpy(vchIV.data(), chNewIV.data(), chNewIV.size());

    nMethod = nDerivationMethodIn;
    fKeySet = true;
    return true;
}

// SECURITY (audit 2026-06, M4): derive a MAC key that is domain-separated from
// the AES encryption key. We MUST NOT reuse vchKey directly as the HMAC key
// (that couples confidentiality and integrity keys). HMAC-SHA512 over a fixed
// label, truncated to 32 bytes, yields an independent 256-bit MAC key.
void CCrypter::DeriveMacKey(uint8_t macKey[WALLET_CRYPTO_KEY_SIZE]) const {
    static const unsigned char MAC_KEY_LABEL[] = "radiant-wallet-mac";
    // length excludes the implicit terminating NUL
    CHMAC_SHA512 hasher(vchKey.data(), vchKey.size());
    hasher.Write(MAC_KEY_LABEL, sizeof(MAC_KEY_LABEL) - 1);
    uint8_t full[CHMAC_SHA512::OUTPUT_SIZE];
    hasher.Finalize(full);
    memcpy(macKey, full, WALLET_CRYPTO_KEY_SIZE);
    memory_cleanse(full, sizeof(full));
}

// SECURITY (audit 2026-06, M4): compute the encrypt-then-MAC tag over the
// domain-separating context label, the IV, and the ciphertext. Binding the IV
// into the MAC prevents an attacker from swapping IVs, and the fixed label
// pins the tag to this wallet-secret context.
void CCrypter::ComputeMac(const uint8_t macKey[WALLET_CRYPTO_KEY_SIZE],
                          const std::vector<uint8_t> &vchCiphertext,
                          uint8_t macOut[WALLET_CRYPTO_MAC_SIZE]) const {
    static const unsigned char MAC_CTX_LABEL[] = "radiant-wallet-aead-v2";
    CHMAC_SHA512 mac(macKey, WALLET_CRYPTO_KEY_SIZE);
    mac.Write(MAC_CTX_LABEL, sizeof(MAC_CTX_LABEL) - 1);
    // Bind the encryption method into the tag so an authenticated blob cannot be
    // reinterpreted under a different (future) authenticated method. NOTE: this
    // does not close a downgrade to method 0 (legacy blobs legitimately carry no
    // MAC, so the verifier simply does not run) -- but forcing method 0 requires
    // rewriting the persisted nDerivationMethod, i.e. full wallet-file write
    // access, which is outside the M4 tamper-detection threat model and still
    // cannot recover plaintext without the passphrase.
    const uint8_t methodByte = static_cast<uint8_t>(nMethod);
    mac.Write(&methodByte, 1);
    mac.Write(vchIV.data(), vchIV.size());
    if (!vchCiphertext.empty()) {
        mac.Write(vchCiphertext.data(), vchCiphertext.size());
    }
    uint8_t full[CHMAC_SHA512::OUTPUT_SIZE];
    mac.Finalize(full);
    memcpy(macOut, full, WALLET_CRYPTO_MAC_SIZE);
    memory_cleanse(full, sizeof(full));
}

// SECURITY (audit 2026-06, M4): method 0 (legacy) remains plain AES-256-CBC with
// NO MAC -- byte-for-byte identical to the historical format, so existing
// wallets are untouched. Method 2 (FIX) is authenticated encrypt-then-MAC: the
// CBC ciphertext is followed by a 32-byte HMAC-SHA512 tag computed over
// (contextLabel || IV || ciphertext) using a MAC key domain-separated from the
// AES key. The tag closes the M4 malleability / padding-oracle surface.
//
// Method-2 on-disk layout (per blob):
//   [ AES-256-CBC ciphertext (== plaintext + PKCS#7 pad) ] [ 32-byte MAC tag ]
// i.e. exactly 32 bytes longer than the equivalent method-0 blob. No record/
// serialization change is needed: the vchCryptedSecret / vchCryptedKey vector
// just grows by 32 bytes, and CMasterKey::nDerivationMethod (which round-trips
// on disk) tells the reader which interpretation to use.
bool CCrypter::Encrypt(const CKeyingMaterial &vchPlaintext,
                       std::vector<uint8_t> &vchCiphertext) const {
    if (!fKeySet) {
        return false;
    }

    // max ciphertext len for a n bytes of plaintext is
    // n + AES_BLOCKSIZE bytes
    vchCiphertext.resize(vchPlaintext.size() + AES_BLOCKSIZE);

    AES256CBCEncrypt enc(vchKey.data(), vchIV.data(), true);
    size_t nLen = enc.Encrypt(vchPlaintext.data(), vchPlaintext.size(),
                              vchCiphertext.data());
    if (nLen < vchPlaintext.size()) {
        return false;
    }
    vchCiphertext.resize(nLen);

    // Method 0: legacy, no MAC -- return exactly the CBC ciphertext (unchanged).
    if (nMethod == WALLET_CRYPTO_METHOD_SHA512_AESCBC) {
        return true;
    }

    // Method 2: encrypt-then-MAC. Append HMAC-SHA512(macKey, label||IV||ct)[0:32].
    uint8_t macKey[WALLET_CRYPTO_KEY_SIZE];
    DeriveMacKey(macKey);
    uint8_t mac[WALLET_CRYPTO_MAC_SIZE];
    ComputeMac(macKey, vchCiphertext, mac);
    memory_cleanse(macKey, sizeof(macKey));
    vchCiphertext.insert(vchCiphertext.end(), mac, mac + WALLET_CRYPTO_MAC_SIZE);
    memory_cleanse(mac, sizeof(mac));
    return true;
}

// SECURITY (audit 2026-06, M4): method 0 stays UNAUTHENTICATED CBC (integrity
// historically rests on the downstream CKey::VerifyPubKey() check). Method 2
// verifies the appended MAC CONSTANT-TIME and FAILS CLOSED on any mismatch
// BEFORE attempting the CBC unpad, which removes the padding-oracle / malleabi-
// lity surface entirely. Truncation or extension changes the MAC input and is
// therefore also rejected.
bool CCrypter::Decrypt(const std::vector<uint8_t> &vchCiphertext,
                       CKeyingMaterial &vchPlaintext) const {
    if (!fKeySet) {
        return false;
    }

    std::vector<uint8_t> vchCbc;
    const std::vector<uint8_t> *pCbc = &vchCiphertext;

    if (nMethod == WALLET_CRYPTO_METHOD_AESCBC_HMAC) {
        // Must have at least one AES block plus a full tag.
        if (vchCiphertext.size() < WALLET_CRYPTO_MAC_SIZE + AES_BLOCKSIZE) {
            return false;
        }
        const size_t nCbcLen = vchCiphertext.size() - WALLET_CRYPTO_MAC_SIZE;
        vchCbc.assign(vchCiphertext.begin(), vchCiphertext.begin() + nCbcLen);
        std::vector<uint8_t> vchTag(vchCiphertext.begin() + nCbcLen,
                                    vchCiphertext.end());

        uint8_t macKey[WALLET_CRYPTO_KEY_SIZE];
        DeriveMacKey(macKey);
        uint8_t expected[WALLET_CRYPTO_MAC_SIZE];
        ComputeMac(macKey, vchCbc, expected);
        memory_cleanse(macKey, sizeof(macKey));

        // Constant-time compare; FAIL CLOSED before any unpad on mismatch.
        std::vector<uint8_t> vchExpected(expected, expected + sizeof(expected));
        memory_cleanse(expected, sizeof(expected));
        if (!TimingResistantEqual(vchExpected, vchTag)) {
            return false;
        }
        pCbc = &vchCbc;
    }

    // plaintext will always be equal to or lesser than length of ciphertext
    const std::vector<uint8_t> &vchToDecrypt = *pCbc;
    int nLen = vchToDecrypt.size();

    vchPlaintext.resize(nLen);

    AES256CBCDecrypt dec(vchKey.data(), vchIV.data(), true);
    nLen = dec.Decrypt(vchToDecrypt.data(), vchToDecrypt.size(),
                       vchPlaintext.data());
    if (nLen == 0) {
        return false;
    }
    vchPlaintext.resize(nLen);
    return true;
}

// SECURITY (audit 2026-06, M4): nDerivationMethod selects per-key authenticated
// (method 2) vs legacy (method 0) treatment for the ckey secret. It is the same
// method stored on the wallet's CMasterKey, so the per-key blobs and the
// master-key blob are authenticated consistently for method-2 wallets.
static bool EncryptSecret(const CKeyingMaterial &vMasterKey,
                          const CKeyingMaterial &vchPlaintext,
                          const uint256 &nIV,
                          std::vector<uint8_t> &vchCiphertext,
                          unsigned int nDerivationMethod) {
    CCrypter cKeyCrypter;
    std::vector<uint8_t> chIV(WALLET_CRYPTO_IV_SIZE);
    memcpy(chIV.data(), &nIV, WALLET_CRYPTO_IV_SIZE);
    if (!cKeyCrypter.SetKey(vMasterKey, chIV, nDerivationMethod)) {
        return false;
    }
    return cKeyCrypter.Encrypt(*((const CKeyingMaterial *)&vchPlaintext),
                               vchCiphertext);
}

static bool DecryptSecret(const CKeyingMaterial &vMasterKey,
                          const std::vector<uint8_t> &vchCiphertext,
                          const uint256 &nIV, CKeyingMaterial &vchPlaintext,
                          unsigned int nDerivationMethod) {
    CCrypter cKeyCrypter;
    std::vector<uint8_t> chIV(WALLET_CRYPTO_IV_SIZE);
    memcpy(chIV.data(), &nIV, WALLET_CRYPTO_IV_SIZE);
    if (!cKeyCrypter.SetKey(vMasterKey, chIV, nDerivationMethod)) {
        return false;
    }
    return cKeyCrypter.Decrypt(vchCiphertext,
                               *((CKeyingMaterial *)&vchPlaintext));
}

static bool DecryptKey(const CKeyingMaterial &vMasterKey,
                       const std::vector<uint8_t> &vchCryptedSecret,
                       const CPubKey &vchPubKey, CKey &key,
                       unsigned int nDerivationMethod) {
    CKeyingMaterial vchSecret;
    if (!DecryptSecret(vMasterKey, vchCryptedSecret, vchPubKey.GetHash(),
                       vchSecret, nDerivationMethod)) {
        return false;
    }

    if (vchSecret.size() != 32) {
        return false;
    }

    key.Set(vchSecret.begin(), vchSecret.end(), vchPubKey.IsCompressed());
    return key.VerifyPubKey(vchPubKey);
}

bool CCryptoKeyStore::SetCrypted() {
    LOCK(cs_KeyStore);
    if (fUseCrypto) {
        return true;
    }
    if (!mapKeys.empty()) {
        return false;
    }
    fUseCrypto = true;
    return true;
}

bool CCryptoKeyStore::IsLocked() const {
    if (!IsCrypted()) {
        return false;
    }
    LOCK(cs_KeyStore);
    return vMasterKey.empty();
}

bool CCryptoKeyStore::Lock() {
    if (!SetCrypted()) {
        return false;
    }

    {
        LOCK(cs_KeyStore);
        vMasterKey.clear();
    }

    NotifyStatusChanged(this);
    return true;
}

bool CCryptoKeyStore::Unlock(const CKeyingMaterial &vMasterKeyIn,
                             bool accept_no_keys,
                             unsigned int nDerivationMethod) {
    {
        LOCK(cs_KeyStore);
        if (!SetCrypted()) {
            return false;
        }

        // Always pass when there are no encrypted keys
        bool keyPass = mapCryptedKeys.empty();
        bool keyFail = false;
        CryptedKeyMap::const_iterator mi = mapCryptedKeys.begin();
        for (; mi != mapCryptedKeys.end(); ++mi) {
            const CPubKey &vchPubKey = (*mi).second.first;
            const std::vector<uint8_t> &vchCryptedSecret = (*mi).second.second;
            CKey key;
            if (!DecryptKey(vMasterKeyIn, vchCryptedSecret, vchPubKey, key,
                            nDerivationMethod)) {
                keyFail = true;
                break;
            }
            keyPass = true;
            if (fDecryptionThoroughlyChecked) {
                break;
            }
        }
        if (keyPass && keyFail) {
            LogPrintf("The wallet is probably corrupted: Some keys decrypt but "
                      "not all.\n");
            assert(false);
        }
        if (keyFail || (!keyPass && !accept_no_keys)) {
            return false;
        }
        vMasterKey = vMasterKeyIn;
        // Remember the active derivation method so subsequently-added keys
        // (AddKeyPubKey) and reads (GetKey) use the same authenticated/legacy
        // treatment as the rest of this wallet.
        nCryptoMethod = nDerivationMethod;
        fDecryptionThoroughlyChecked = true;
    }
    NotifyStatusChanged(this);
    return true;
}

bool CCryptoKeyStore::AddKeyPubKey(const CKey &key, const CPubKey &pubkey) {
    LOCK(cs_KeyStore);
    if (!IsCrypted()) {
        return CBasicKeyStore::AddKeyPubKey(key, pubkey);
    }

    if (IsLocked()) {
        return false;
    }

    std::vector<uint8_t> vchCryptedSecret;
    CKeyingMaterial vchSecret(key.begin(), key.end());
    if (!EncryptSecret(vMasterKey, vchSecret, pubkey.GetHash(),
                       vchCryptedSecret, nCryptoMethod)) {
        return false;
    }

    if (!AddCryptedKey(pubkey, vchCryptedSecret)) {
        return false;
    }
    return true;
}

bool CCryptoKeyStore::AddCryptedKey(
    const CPubKey &vchPubKey, const std::vector<uint8_t> &vchCryptedSecret) {
    LOCK(cs_KeyStore);
    if (!SetCrypted()) {
        return false;
    }

    mapCryptedKeys[vchPubKey.GetID()] = make_pair(vchPubKey, vchCryptedSecret);
    ImplicitlyLearnRelatedKeyScripts(vchPubKey);
    return true;
}

bool CCryptoKeyStore::HaveKey(const CKeyID &address) const {
    LOCK(cs_KeyStore);
    if (!IsCrypted()) {
        return CBasicKeyStore::HaveKey(address);
    }
    return mapCryptedKeys.count(address) > 0;
}

bool CCryptoKeyStore::GetKey(const CKeyID &address, CKey &keyOut) const {
    LOCK(cs_KeyStore);
    if (!IsCrypted()) {
        return CBasicKeyStore::GetKey(address, keyOut);
    }

    CryptedKeyMap::const_iterator mi = mapCryptedKeys.find(address);
    if (mi != mapCryptedKeys.end()) {
        const CPubKey &vchPubKey = (*mi).second.first;
        const std::vector<uint8_t> &vchCryptedSecret = (*mi).second.second;
        return DecryptKey(vMasterKey, vchCryptedSecret, vchPubKey, keyOut,
                          nCryptoMethod);
    }
    return false;
}

bool CCryptoKeyStore::GetPubKey(const CKeyID &address,
                                CPubKey &vchPubKeyOut) const {
    LOCK(cs_KeyStore);
    if (!IsCrypted()) {
        return CBasicKeyStore::GetPubKey(address, vchPubKeyOut);
    }

    CryptedKeyMap::const_iterator mi = mapCryptedKeys.find(address);
    if (mi != mapCryptedKeys.end()) {
        vchPubKeyOut = (*mi).second.first;
        return true;
    }

    // Check for watch-only pubkeys
    return CBasicKeyStore::GetPubKey(address, vchPubKeyOut);
}

std::set<CKeyID> CCryptoKeyStore::GetKeys() const {
    LOCK(cs_KeyStore);
    if (!IsCrypted()) {
        return CBasicKeyStore::GetKeys();
    }
    std::set<CKeyID> set_address;
    for (const auto &mi : mapCryptedKeys) {
        set_address.insert(mi.first);
    }
    return set_address;
}

bool CCryptoKeyStore::EncryptKeys(CKeyingMaterial &vMasterKeyIn,
                                  unsigned int nDerivationMethod) {
    LOCK(cs_KeyStore);
    if (!mapCryptedKeys.empty() || IsCrypted()) {
        return false;
    }

    fUseCrypto = true;
    // Persist the active method so later AddKeyPubKey / GetKey use the matching
    // authenticated (method 2) or legacy (method 0) treatment.
    nCryptoMethod = nDerivationMethod;
    for (const KeyMap::value_type &mKey : mapKeys) {
        const CKey &key = mKey.second;
        CPubKey vchPubKey = key.GetPubKey();
        CKeyingMaterial vchSecret(key.begin(), key.end());
        std::vector<uint8_t> vchCryptedSecret;
        if (!EncryptSecret(vMasterKeyIn, vchSecret, vchPubKey.GetHash(),
                           vchCryptedSecret, nDerivationMethod)) {
            return false;
        }
        if (!AddCryptedKey(vchPubKey, vchCryptedSecret)) {
            return false;
        }
    }
    mapKeys.clear();
    return true;
}
