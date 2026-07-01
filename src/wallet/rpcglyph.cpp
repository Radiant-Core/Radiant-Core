// Copyright (c) 2024-2026 The Radiant developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
//
// mintglyph wallet RPC — Glyph v2 commit-reveal minting.
//
// The browser submits validated JSON metadata; this RPC:
//   1. Deterministically encodes the payload as CBOR
//   2. Constructs the commit tx (nftCommitScript / ftCommitScript output)
//   3. Signs the commit tx using the wallet's standard signing infrastructure
//   4. Constructs the reveal tx (glyph output + [sig][pubkey]["gly"][cbor] scriptSig)
//   5. Signs the reveal tx manually (SignatureHash + CKey::SignECDSA)
//   6. Returns both signed tx hexes plus derived token_ref and debug fields
//
// Private keys never leave the node process.

#include <amount.h>
#include <tinyformat.h>
#include <chainparams.h>
#include <config.h>
#include <core_io.h>
#include <hash.h>
#include <key_io.h>
#include <policy/policy.h>
#include <primitives/transaction.h>
#include <rpc/server.h>
#include <rpc/util.h>
#include <script/interpreter.h>
#include <script/script.h>
#include <script/sighashtype.h>
#include <script/standard.h>
#include <util/strencodings.h>
#include <validation.h>
#include <wallet/rpcglyph.h>
#include <wallet/rpcwallet.h>
#include <wallet/wallet.h>

#include <univalue.h>

#include <cstdint>
#include <ctime>
#include <map>
#include <string>
#include <vector>

// ── Minimal CBOR encoder ──────────────────────────────────────────────────
//
// Encodes only the CBOR subset used by Glyph v2 payloads:
//   - Unsigned integers (major 0)
//   - Byte strings (major 2)
//   - Text strings (major 3)
//   - Arrays (major 4)
//   - Maps (major 5)

static void CborHead(std::vector<uint8_t> &out, uint8_t major, uint64_t n) {
    const uint8_t m = major << 5;
    if (n <= 23)           { out.push_back(m | uint8_t(n)); }
    else if (n <= 0xff)    { out.push_back(m | 24); out.push_back(uint8_t(n)); }
    else if (n <= 0xffff)  { out.push_back(m | 25); out.push_back(uint8_t(n >> 8)); out.push_back(uint8_t(n)); }
    else if (n <= 0xffffffff) {
        out.push_back(m | 26);
        out.push_back(uint8_t(n >> 24)); out.push_back(uint8_t(n >> 16));
        out.push_back(uint8_t(n >> 8));  out.push_back(uint8_t(n));
    } else {
        out.push_back(m | 27);
        for (int shift = 56; shift >= 0; shift -= 8) out.push_back(uint8_t(n >> shift));
    }
}

static void CborUint(std::vector<uint8_t> &out, uint64_t n) { CborHead(out, 0, n); }

static void CborBytes(std::vector<uint8_t> &out, const std::vector<uint8_t> &data) {
    CborHead(out, 2, data.size());
    out.insert(out.end(), data.begin(), data.end());
}

static void CborText(std::vector<uint8_t> &out, const std::string &s) {
    CborHead(out, 3, s.size());
    out.insert(out.end(), s.begin(), s.end());
}

// ── CBOR payload construction ─────────────────────────────────────────────

struct GlyphParams {
    std::string type;       // "nft" | "ft" | "container" | "user"
    std::string name;
    std::string ticker;     // FT only
    int64_t     supply;     // atomic units (satoshis for the UTXO value)
    int         decimals;   // FT only
    bool        immutable;
    std::string desc;
    std::string license;
    std::vector<uint8_t> authorRef;    // 36-byte ref, or empty
    std::vector<uint8_t> containerRef; // 36-byte ref, or empty
    std::map<std::string, std::string> attrs;
    std::string mainMimeType;
    std::vector<uint8_t> mainData;     // embedded bytes, or empty
    std::string mainUrl;               // remote URL, or empty
};

// Build the deterministic CBOR encoding of a Glyph v2 payload.
// Key ordering: v, p, name, [ticker, decimals], [type], [desc], [license],
//               [by], [in], [attrs], [main]
static std::vector<uint8_t> EncodeGlyphCbor(const GlyphParams &p) {
    // Protocol flags
    std::vector<int> protocols;
    if (p.type == "ft") protocols.push_back(1);
    else                protocols.push_back(2);
    if (!p.immutable)            protocols.push_back(5);
    if (p.type == "container")   protocols.push_back(7);

    // Count map entries
    size_t nFields = 3; // v, p, name always present
    const bool isFt        = (p.type == "ft");
    const bool needType    = (p.type == "user" || p.type == "container");
    const bool hasDesc     = !p.desc.empty();
    const bool hasLicense  = !p.license.empty();
    const bool hasAuthor   = !p.authorRef.empty();
    const bool hasCont     = !p.containerRef.empty();
    const bool hasAttrs    = !p.attrs.empty();
    const bool hasMainEmb  = !p.mainData.empty();
    const bool hasMainUrl  = !p.mainUrl.empty();
    const bool hasMain     = hasMainEmb || hasMainUrl;

    if (isFt)      nFields += 2; // ticker, decimals
    if (needType)  nFields += 1;
    if (hasDesc)   nFields += 1;
    if (hasLicense) nFields += 1;
    if (hasAuthor) nFields += 1;
    if (hasCont)   nFields += 1;
    if (hasAttrs)  nFields += 1;
    if (hasMain)   nFields += 1;

    std::vector<uint8_t> out;
    CborHead(out, 5, nFields); // map

    // v: 2
    CborText(out, "v");  CborUint(out, 2);

    // p: [...]
    CborText(out, "p");
    CborHead(out, 4, protocols.size());
    for (int pr : protocols) CborUint(out, uint64_t(pr));

    // name
    CborText(out, "name"); CborText(out, p.name);

    // FT-specific
    if (isFt) {
        CborText(out, "ticker");   CborText(out, p.ticker);
        CborText(out, "decimals"); CborUint(out, uint64_t(p.decimals));
    }

    // type (user / container)
    if (needType) {
        CborText(out, "type");
        CborText(out, (p.type == "user") ? "user" : "container");
    }

    if (hasDesc)    { CborText(out, "desc");    CborText(out, p.desc); }
    if (hasLicense) { CborText(out, "license"); CborText(out, p.license); }

    // by: [ref_bytes]  (author ref)
    if (hasAuthor) {
        CborText(out, "by");
        CborHead(out, 4, 1);
        CborBytes(out, p.authorRef);
    }

    // in: [ref_bytes]  (container ref)
    if (hasCont) {
        CborText(out, "in");
        CborHead(out, 4, 1);
        CborBytes(out, p.containerRef);
    }

    // attrs: { key: value, ... }
    if (hasAttrs) {
        CborText(out, "attrs");
        CborHead(out, 5, p.attrs.size());
        for (const auto &[k, v] : p.attrs) {
            CborText(out, k); CborText(out, v);
        }
    }

    // main: { t: mime, b: data } or { u: url }
    if (hasMain) {
        CborText(out, "main");
        if (hasMainEmb) {
            CborHead(out, 5, 2);
            CborText(out, "t");
            CborText(out, p.mainMimeType.empty() ? "application/octet-stream" : p.mainMimeType);
            CborText(out, "b");
            CborBytes(out, p.mainData);
        } else {
            CborHead(out, 5, 1);
            CborText(out, "u");
            CborText(out, p.mainUrl);
        }
    }

    return out;
}

// ── Script building ───────────────────────────────────────────────────────
//
// Opcode values verified against src/script/script.h:
//   OP_INPUTINDEX=0xc0  OP_OUTPOINTTXHASH=0xc8  OP_OUTPOINTINDEX=0xc9
//   OP_REFTYPE_OUTPUT=0xda  OP_NUM2BIN=0x80  OP_CAT=0x7e

// Build the nftCommitScript output scriptPubKey (75 bytes):
//   OP_HASH256 <hash32> OP_EQUALVERIFY "gly" OP_EQUALVERIFY
//   <nft-ref-check-10-bytes>  OP_DUP OP_HASH160 <pkh20> OP_EQUALVERIFY OP_CHECKSIG
static CScript BuildCommitScript(const CKeyID &keyID,
                                 const std::vector<uint8_t> &payloadHash,
                                 bool isSingleton) {
    std::vector<uint8_t> raw;
    raw.push_back(0xaa);           // OP_HASH256
    raw.push_back(0x20);           // push 32 bytes
    raw.insert(raw.end(), payloadHash.begin(), payloadHash.end());
    raw.push_back(0x88);           // OP_EQUALVERIFY
    raw.push_back(0x03);           // push 3 bytes
    raw.push_back(0x67); raw.push_back(0x6c); raw.push_back(0x79); // "gly"
    raw.push_back(0x88);           // OP_EQUALVERIFY
    // OP_INPUTINDEX OP_OUTPOINTTXHASH OP_INPUTINDEX OP_OUTPOINTINDEX
    // OP_4 OP_NUM2BIN OP_CAT OP_REFTYPE_OUTPUT OP_1/2 OP_NUMEQUALVERIFY
    const uint8_t refCheck[10] = {
        0xc0, 0xc8, 0xc0, 0xc9, 0x54, 0x80, 0x7e, 0xda,
        isSingleton ? uint8_t(0x52) : uint8_t(0x51), // OP_2 (NFT) or OP_1 (FT)
        0x9d
    };
    raw.insert(raw.end(), refCheck, refCheck + 10);
    raw.push_back(0x76); raw.push_back(0xa9); // OP_DUP OP_HASH160
    raw.push_back(0x14);                        // push 20 bytes
    raw.insert(raw.end(), keyID.begin(), keyID.end());
    raw.push_back(0x88); raw.push_back(0xac); // OP_EQUALVERIFY OP_CHECKSIG
    return CScript(raw.begin(), raw.end());
}

// Build the glyph token output scriptPubKey (63 bytes):
//   OP_PUSHINPUTREF[SINGLETON] <ref36> OP_DROP OP_DUP OP_HASH160 <pkh20> OP_EQUALVERIFY OP_CHECKSIG
// ref36 = txid_bytes_LE (32 bytes) + vout_LE32 (4 bytes)
static CScript BuildGlyphOutputScript(const TxId &commitTxId, uint32_t vout,
                                      const CKeyID &keyID, bool isSingleton) {
    std::vector<uint8_t> raw;
    raw.push_back(isSingleton ? 0xd8 : 0xd0); // OP_PUSHINPUTREFSINGLETON / OP_PUSHINPUTREF
    // txid bytes as stored in uint256 are already in LE wire order
    raw.insert(raw.end(), commitTxId.begin(), commitTxId.end());
    // vout as LE32
    raw.push_back(vout & 0xff);
    raw.push_back((vout >> 8) & 0xff);
    raw.push_back((vout >> 16) & 0xff);
    raw.push_back((vout >> 24) & 0xff);
    raw.push_back(0x75);           // OP_DROP
    raw.push_back(0x76); raw.push_back(0xa9); // OP_DUP OP_HASH160
    raw.push_back(0x14);           // push 20 bytes
    raw.insert(raw.end(), keyID.begin(), keyID.end());
    raw.push_back(0x88); raw.push_back(0xac); // OP_EQUALVERIFY OP_CHECKSIG
    return CScript(raw.begin(), raw.end());
}

// ── mintglyph RPC ─────────────────────────────────────────────────────────

static UniValue mintglyph(const Config &config, const JSONRPCRequest &request) {
    std::shared_ptr<CWallet> const wallet = GetWalletForJSONRPCRequest(request);
    CWallet *const pwallet = wallet.get();

    if (!EnsureWalletIsAvailable(pwallet, request.fHelp)) return UniValue();

    if (request.fHelp || request.params.size() < 1) {
        throw std::runtime_error(
            RPCHelpMan{"mintglyph",
                "\nMint a Glyph v2 token (NFT, FT, container, or user profile).\n"
                "Builds and signs both the commit and reveal transactions server-side.\n"
                "Private keys never leave the node." +
                HelpRequiringPassphrase(pwallet) + "\n",
                {
                    {"funding_txid",  RPCArg::Type::STR,  false, "", "Txid of the UTXO to fund the mint."},
                    {"funding_vout",  RPCArg::Type::NUM,  false, "", "Vout index of the funding UTXO."},
                    {"to_address",    RPCArg::Type::STR,  false, "", "Destination address for the minted token."},
                    {"type",          RPCArg::Type::STR,  false, "", "Token type: \"nft\", \"ft\", \"container\", or \"user\"."},
                    {"name",          RPCArg::Type::STR,  false, "", "Token name."},
                    {"supply",        RPCArg::Type::NUM,  true,  "1", "Supply in atomic units (satoshis). Default 1 for NFT."},
                    {"ticker",        RPCArg::Type::STR,  true,  "", "Ticker symbol (FT only)."},
                    {"decimals",      RPCArg::Type::NUM,  true,  "0", "Decimal places (FT only)."},
                    {"immutable",     RPCArg::Type::BOOL, true,  "true", "Whether the token is immutable."},
                    {"desc",          RPCArg::Type::STR,  true,  "", "Description."},
                    {"license",       RPCArg::Type::STR,  true,  "", "License string."},
                    {"author",        RPCArg::Type::STR,  true,  "", "72-hex author ref (NFT/container only)."},
                    {"container",     RPCArg::Type::STR,  true,  "", "72-hex container ref (NFT only)."},
                    {"attrs",         RPCArg::Type::OBJ_USER_KEYS, true, "", "Attribute map (string keys and values)."},
                    {"main_type",     RPCArg::Type::STR,  true,  "", "MIME type of embedded content."},
                    {"main_data",     RPCArg::Type::STR,  true,  "", "Embedded content as a hex string."},
                    {"main_url",      RPCArg::Type::STR,  true,  "", "URL for remote content (alternative to main_data)."},
                    {"fee_rate",      RPCArg::Type::NUM,  true,  "0", "Fee rate in sat/kB (0 = use effective minimum)."},
                }}
                .ToString() +
            "\nResult:\n"
            "{\n"
            "  \"commit_txid\": \"hex\",   (string) Commit transaction id\n"
            "  \"commit_hex\":  \"hex\",   (string) Signed commit tx hex\n"
            "  \"reveal_hex\":  \"hex\",   (string) Signed reveal tx hex\n"
            "  \"token_ref\":   \"hex\",   (string) 72-char token reference\n"
            "  \"cbor_hex\":    \"hex\",   (string) Encoded CBOR payload (debug)\n"
            "  \"payload_hash\":\"hex\"    (string) SHA256d of CBOR (debug)\n"
            "}\n"
            "\nExamples:\n" +
            HelpExampleCli("mintglyph",
                "\"<txid>\" 0 \"<address>\" \"nft\" \"My NFT\"") +
            HelpExampleRpc("mintglyph",
                "\"<txid>\", 0, \"<address>\", \"nft\", \"My NFT\"")
        );
    }

    // ── Parse parameters ──────────────────────────────────────────────────

    if (request.params.size() < 5) {
        throw JSONRPCError(RPC_INVALID_PARAMETER,
            "mintglyph requires at least 5 arguments: funding_txid, funding_vout, to_address, type, name");
    }

    const std::string fundingTxidStr = request.params[0].get_str();
    const uint32_t    fundingVout    = request.params[1].get_int();
    const std::string toAddressStr   = request.params[2].get_str();
    const std::string tokenType      = request.params[3].get_str();
    const std::string tokenName      = request.params[4].get_str();

    if (tokenType != "nft" && tokenType != "ft" &&
        tokenType != "container" && tokenType != "user") {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "type must be nft, ft, container, or user");
    }
    if (tokenName.empty()) throw JSONRPCError(RPC_INVALID_PARAMETER, "name must not be empty");

    auto getOptStr  = [&](size_t idx) -> std::string {
        if (request.params.size() > idx && !request.params[idx].isNull()) return request.params[idx].get_str();
        return "";
    };
    auto getOptInt  = [&](size_t idx, int def) -> int {
        if (request.params.size() > idx && !request.params[idx].isNull()) return request.params[idx].get_int();
        return def;
    };
    auto getOptBool = [&](size_t idx, bool def) -> bool {
        if (request.params.size() > idx && !request.params[idx].isNull()) return request.params[idx].get_bool();
        return def;
    };

    GlyphParams gp;
    gp.type      = tokenType;
    gp.name      = tokenName;
    gp.supply    = (request.params.size() > 5 && !request.params[5].isNull())
                   ? request.params[5].get_int64() : int64_t(1);
    gp.ticker    = getOptStr(6);
    gp.decimals  = getOptInt(7, 0);
    gp.immutable = getOptBool(8, true);
    gp.desc      = getOptStr(9);
    gp.license   = getOptStr(10);

    // author ref (72-hex → 36 bytes)
    const std::string authorHex = getOptStr(11);
    if (!authorHex.empty()) {
        if (authorHex.size() != 72) throw JSONRPCError(RPC_INVALID_PARAMETER, "author must be 72 hex chars");
        gp.authorRef = ParseHex(authorHex);
    }

    // container ref (72-hex → 36 bytes)
    const std::string containerHex = getOptStr(12);
    if (!containerHex.empty()) {
        if (containerHex.size() != 72) throw JSONRPCError(RPC_INVALID_PARAMETER, "container must be 72 hex chars");
        gp.containerRef = ParseHex(containerHex);
    }

    // attrs object
    if (request.params.size() > 13 && !request.params[13].isNull()) {
        const UniValue &attrsUV = request.params[13];
        if (!attrsUV.isObject()) throw JSONRPCError(RPC_INVALID_PARAMETER, "attrs must be an object");
        for (const auto &[key, val] : attrsUV.get_obj()) {
            gp.attrs[key] = val.get_str();
        }
    }

    gp.mainMimeType = getOptStr(14);

    // main_data: hex-encoded bytes
    const std::string mainDataHex = getOptStr(15);
    if (!mainDataHex.empty()) {
        if (mainDataHex.size() % 2 != 0) throw JSONRPCError(RPC_INVALID_PARAMETER, "main_data must be even-length hex");
        gp.mainData = ParseHex(mainDataHex);
    }

    gp.mainUrl = getOptStr(16);

    if (!gp.mainData.empty() && !gp.mainUrl.empty()) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Specify either main_data or main_url, not both");
    }

    const int64_t feeRateSatPerKb = getOptInt(17, 0);

    if (tokenType == "ft" && gp.ticker.empty()) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "ticker is required for FT tokens");
    }
    if (gp.supply <= 0) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "supply must be positive");
    }
    // NFT, user, and container tokens are singletons — exactly 1 satoshi regardless of caller input.
    if (tokenType != "ft") {
        gp.supply = 1;
    }

    // ── Wallet setup ──────────────────────────────────────────────────────

    auto locked_chain = pwallet->chain().lock();
    LOCK(pwallet->cs_wallet);
    EnsureWalletIsUnlocked(pwallet);

    // Resolve funding UTXO from wallet map
    TxId fundingTxId(uint256S(fundingTxidStr));
    auto mi = pwallet->mapWallet.find(fundingTxId);
    if (mi == pwallet->mapWallet.end()) {
        throw JSONRPCError(RPC_WALLET_ERROR, "Funding transaction not found in wallet");
    }
    if (fundingVout >= mi->second.tx->vout.size()) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "funding_vout is out of range");
    }
    const CTxOut &fundingOut = mi->second.tx->vout[fundingVout];
    const Amount  fundingSats = fundingOut.nValue;

    // Extract keyID from to_address
    CTxDestination toDest = DecodeDestination(toAddressStr, config.GetChainParams());
    if (!IsValidDestination(toDest)) {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid to_address");
    }
    const CKeyID *pToKeyID = boost::get<CKeyID>(&toDest);
    if (!pToKeyID) {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "to_address must be a P2PKH address");
    }
    const CKeyID toKeyID = *pToKeyID;

    // Get the signing key (must be held by this wallet)
    CKey signingKey;
    if (!pwallet->GetKey(toKeyID, signingKey)) {
        throw JSONRPCError(RPC_WALLET_ERROR, "Private key for to_address not available in wallet");
    }
    const CPubKey signingPubKey = signingKey.GetPubKey();

    // ── Fee calculation ───────────────────────────────────────────────────

    // Get effective minimum relay fee
    const int tipHeight = locked_chain->getHeight().value_or(0);
    const auto &consensus = config.GetChainParams().GetConsensus();
    const CFeeRate effectiveMinRate = GetEffectiveMinRelayFee(tipHeight, consensus);

    // Encode CBOR now so we know payload size for fee estimation
    const std::vector<uint8_t> cborBytes = EncodeGlyphCbor(gp);

    // Use provided fee_rate if valid, otherwise effective minimum
    const CFeeRate feeRate = (feeRateSatPerKb > 0 &&
                              CFeeRate(feeRateSatPerKb * SATOSHI) >= effectiveMinRate)
        ? CFeeRate(feeRateSatPerKb * SATOSHI)
        : effectiveMinRate;

    // Commit tx: 1 P2PKH input (107-byte scriptSig) + 1 commitScript output (75 bytes)
    //            + 1 P2PKH change output (25 bytes) + 10-byte base overhead
    // = 4(ver) + 1(inCount) + (32+4+1+107+4)(input) + 1(outCount)
    //   + (8+1+75)(commitOut) + (8+1+25)(changeOut) + 4(locktime) = 276 bytes
    static const int64_t COMMIT_TX_BYTES = 276;

    // Reveal tx scriptSig: sig(1+72) pubkey(1+33) "gly"(1+3) cbor(overhead+len)
    // sig push: 1 (opcode) + 72 (DER max) + 1 (sighash) = 74 bytes
    // pubkey push: 1 + 33 = 34
    // gly push: 1 + 3 = 4
    // cbor push overhead: opcode(s) + length prefix + data
    const size_t cborLen = cborBytes.size();
    int64_t cborPushOverhead;
    if (cborLen <= 75)         cborPushOverhead = 1;  // single-byte push opcode
    else if (cborLen <= 255)   cborPushOverhead = 2;  // OP_PUSHDATA1 + 1-byte len
    else if (cborLen <= 65535) cborPushOverhead = 3;  // OP_PUSHDATA2 + 2-byte LE len
    else                       cborPushOverhead = 5;  // OP_PUSHDATA4 + 4-byte LE len
    const int64_t cborPushLen = cborPushOverhead + int64_t(cborLen);
    const int64_t revealScriptSigSize = 74 + 34 + 4 + cborPushLen;

    // scriptSig length uses a Bitcoin varint: 1 byte (≤252), 3 bytes (≤65535), 5 bytes (larger)
    int64_t scriptSigVarintSize;
    if (revealScriptSigSize <= 252)        scriptSigVarintSize = 1;
    else if (revealScriptSigSize <= 65535) scriptSigVarintSize = 3;
    else                                   scriptSigVarintSize = 5;

    // Reveal tx: 4(ver) + 1(inCount) + 32+4+varint+scriptSig+4(input) + 1(outCount) + 8+1+63(glyphOut) + 4(lock)
    const int64_t revealTxBytes = 4 + 1 + (32 + 4 + scriptSigVarintSize + revealScriptSigSize + 4) + 1 + (8 + 1 + 63) + 4;

    const Amount commitFeeSats = feeRate.GetFee(COMMIT_TX_BYTES);
    const Amount revealFeeSats = feeRate.GetFee(revealTxBytes);

    // Commit output must cover token supply + reveal fee (reveal spends it)
    const Amount supplySats    = gp.supply * SATOSHI;
    const Amount commitOutSats = supplySats + revealFeeSats;
    const Amount changeSats    = fundingSats - commitOutSats - commitFeeSats;

    if (changeSats < Amount()) {
        const Amount needed = commitOutSats + commitFeeSats;
        throw JSONRPCError(RPC_WALLET_INSUFFICIENT_FUNDS,
            strprintf("Insufficient funds: need %d sat (supply %d + fees %d), have %d sat",
                      needed / SATOSHI, commitOutSats / SATOSHI,
                      commitFeeSats / SATOSHI, fundingSats / SATOSHI));
    }

    // ── Compute CBOR payload hash ─────────────────────────────────────────

    const uint256 payloadHash = Hash(cborBytes);
    std::vector<uint8_t> payloadHashBytes(payloadHash.begin(), payloadHash.end());

    // ── Build commit tx ───────────────────────────────────────────────────

    const bool isSingleton = (tokenType != "ft");

    CMutableTransaction commitTx;
    commitTx.nVersion = 1;
    commitTx.nLockTime = 0;

    // Input: funding UTXO
    CTxIn commitIn;
    commitIn.prevout = COutPoint(fundingTxId, fundingVout);
    commitIn.nSequence = 0xffffffff;
    commitTx.vin.push_back(commitIn);

    // Output 0: commit script (encodes payloadHash + "gly" + ref check + P2PKH)
    const CScript commitScript = BuildCommitScript(toKeyID, payloadHashBytes, isSingleton);
    commitTx.vout.push_back(CTxOut(commitOutSats, commitScript));

    // Output 1: P2PKH change (if above dust)
    if (changeSats > 546 * SATOSHI) {
        // Use the funding output's scriptPubKey as change destination
        commitTx.vout.push_back(CTxOut(changeSats, fundingOut.scriptPubKey));
    }

    // Sign commit tx using wallet infrastructure (input is a known P2PKH UTXO)
    if (!pwallet->SignTransaction(commitTx)) {
        throw JSONRPCError(RPC_WALLET_ERROR, "Failed to sign commit tx (wallet locked or UTXO not in wallet)");
    }

    const CTransaction commitTxFinal(commitTx);
    const TxId commitTxId = commitTxFinal.GetId();
    const std::string commitHex = EncodeHexTx(commitTxFinal);

    // ── Build reveal tx ───────────────────────────────────────────────────

    CMutableTransaction revealTx;
    revealTx.nVersion  = 1;
    revealTx.nLockTime = 0;

    // Input 0: commit UTXO (not yet in mapWallet, so we sign manually below)
    CTxIn revealIn;
    revealIn.prevout  = COutPoint(commitTxId, 0);
    revealIn.nSequence = 0xffffffff;
    revealTx.vin.push_back(revealIn);

    // Output 0: glyph token output
    const CScript glyphOutScript = BuildGlyphOutputScript(commitTxId, 0, toKeyID, isSingleton);
    revealTx.vout.push_back(CTxOut(supplySats, glyphOutScript));
    // No OP_RETURN — CBOR lives in the reveal input's scriptSig (Photonic protocol).
    // No change output — commit output surplus is the implicit reveal fee.

    // ── Sign reveal tx input manually ─────────────────────────────────────
    //
    // The reveal input spends the nftCommitScript / ftCommitScript, which is
    // non-standard and not in mapWallet, so CWallet::SignTransaction cannot
    // handle it. We compute the BIP143 sighash directly and sign with CKey.

    const SigHashType sigHashType = SigHashType().withForkId(); // SIGHASH_ALL | SIGHASH_FORKID (0x41)
    const uint256 sighash = SignatureHash(commitScript, revealTx, 0, sigHashType,
                                          commitOutSats, nullptr, SCRIPT_ENABLE_SIGHASH_FORKID);

    std::vector<uint8_t> sigDER;
    if (!signingKey.SignECDSA(sighash, sigDER)) {
        throw JSONRPCError(RPC_WALLET_ERROR, "Failed to sign reveal tx");
    }
    sigDER.push_back(uint8_t(sigHashType.getRawSigHashType())); // append 0x41

    // Assemble scriptSig: <sig push> <pubkey push> 03 676c79 <cbor push>
    const std::vector<uint8_t> pubKeyBytes(signingPubKey.begin(), signingPubKey.end());
    const std::vector<uint8_t> glyMagic = {0x67, 0x6c, 0x79}; // "gly"

    CScript revealScriptSig;
    revealScriptSig << sigDER
                    << pubKeyBytes
                    << glyMagic
                    << cborBytes;

    revealTx.vin[0].scriptSig = revealScriptSig;

    const CTransaction revealTxFinal(revealTx);
    const std::string revealHex = EncodeHexTx(revealTxFinal);

    // ── Build token_ref (for display) ─────────────────────────────────────
    // ref = commitTxId bytes (LE wire order) + vout 0 (LE32)
    std::string tokenRef;
    {
        std::vector<uint8_t> refBytes(commitTxId.begin(), commitTxId.end());
        refBytes.push_back(0); refBytes.push_back(0); refBytes.push_back(0); refBytes.push_back(0);
        tokenRef = HexStr(refBytes);
    }

    // ── Return results ────────────────────────────────────────────────────

    UniValue::Object result;
    result.reserve(6);
    result.emplace_back("commit_txid",  commitTxId.GetHex());
    result.emplace_back("commit_hex",   commitHex);
    result.emplace_back("reveal_hex",   revealHex);
    result.emplace_back("token_ref",    tokenRef);
    result.emplace_back("cbor_hex",     HexStr(cborBytes));
    result.emplace_back("payload_hash", payloadHash.GetHex());
    return result;
}

// ── dMint script helpers ──────────────────────────────────────────────────

// Returns bytes encoding integer n as a minimal script push (n >= 0).
// OP_0 for 0, OP_1..OP_16 for 1-16, else length-byte + script-number LE bytes.
static std::vector<uint8_t> DmintPushMinimal(int64_t n) {
    std::vector<uint8_t> out;
    if (n == 0) {
        out.push_back(0x00); // OP_0
    } else if (n >= 1 && n <= 16) {
        out.push_back(uint8_t(0x50 + n)); // OP_1..OP_16
    } else {
        // Script-number LE with sign bit
        std::vector<uint8_t> num;
        int64_t abs = (n < 0) ? -n : n;
        while (abs > 0) { num.push_back(uint8_t(abs & 0xff)); abs >>= 8; }
        if (num.back() & 0x80) num.push_back(n < 0 ? 0x80 : 0x00);
        else if (n < 0)        num.back() |= 0x80;
        out.push_back(uint8_t(num.size()));
        out.insert(out.end(), num.begin(), num.end());
    }
    return out;
}

// Returns 5 bytes: push-4-bytes opcode + value as LE uint32.
static std::vector<uint8_t> DmintPush4Bytes(uint32_t n) {
    return { 0x04,
             uint8_t(n & 0xff), uint8_t((n >> 8) & 0xff),
             uint8_t((n >> 16) & 0xff), uint8_t((n >> 24) & 0xff) };
}

// Wraps data bytes with the appropriate script push prefix (direct / PUSHDATA1/2/4).
static std::vector<uint8_t> DmintEncodeDataPush(const std::vector<uint8_t> &data) {
    std::vector<uint8_t> out;
    size_t len = data.size();
    if (len <= 75) {
        out.push_back(uint8_t(len));
    } else if (len <= 255) {
        out.push_back(0x4c); out.push_back(uint8_t(len));
    } else if (len <= 65535) {
        out.push_back(0x4d); out.push_back(uint8_t(len & 0xff)); out.push_back(uint8_t(len >> 8));
    } else {
        out.push_back(0x4e);
        out.push_back(uint8_t(len & 0xff)); out.push_back(uint8_t((len >> 8) & 0xff));
        out.push_back(uint8_t((len >> 16) & 0xff)); out.push_back(uint8_t((len >> 24) & 0xff));
    }
    out.insert(out.end(), data.begin(), data.end());
    return out;
}


// Build ASERT-v2 DAA bytecode for a given halfLife. Mirrors buildAsertDaaBytecode()
// in Photonic packages/lib/src/script.ts (2026-06-19 redesign).
static std::vector<uint8_t> BuildAsertDaaBytecode(int halfLife) {
    const std::vector<uint8_t> radixPush  = DmintPushMinimal(65536);  // "03000001"
    const std::vector<uint8_t> clampPush  = DmintPushMinimal(16384);  // "020040"
    const std::vector<uint8_t> negClamp   = DmintPushMinimal(-16384); // "0200c0"
    const std::vector<uint8_t> hlPush     = DmintPushMinimal(halfLife);
    // PUSH_QUARTER_MAX_TARGET = 0x1fffffffffffffff as 8-byte LE
    static const uint8_t QMT[9] = {0x08,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0x1f};

    std::vector<uint8_t> out;
    out.push_back(0xc5); // OP_TXLOCKTIME
    out.push_back(0x52); out.push_back(0x79); // OP_2 OP_PICK (lastTime)
    out.push_back(0x94); // OP_SUB
    out.push_back(0x53); out.push_back(0x79); // OP_3 OP_PICK (targetTime)
    out.push_back(0x94); // OP_SUB
    out.insert(out.end(), radixPush.begin(), radixPush.end());
    out.push_back(0x95); // OP_MUL
    out.insert(out.end(), hlPush.begin(), hlPush.end());
    out.push_back(0x96); // OP_DIV
    out.insert(out.end(), clampPush.begin(), clampPush.end());
    out.push_back(0xa3); // OP_MIN
    out.insert(out.end(), negClamp.begin(), negClamp.end());
    out.push_back(0xa4); // OP_MAX
    out.push_back(0x7c); // OP_SWAP
    out.insert(out.end(), QMT, QMT + 9);
    out.push_back(0xa3); // OP_MIN
    out.push_back(0x76); // OP_DUP
    out.insert(out.end(), radixPush.begin(), radixPush.end());
    out.push_back(0x96); // OP_DIV
    out.push_back(0x7b); // OP_ROT
    out.push_back(0x95); // OP_MUL
    out.push_back(0x93); // OP_ADD
    out.insert(out.end(), QMT, QMT + 9);
    out.push_back(0xa3); // OP_MIN
    out.push_back(0x76); out.push_back(0x51); out.push_back(0x9f); // DUP OP_1 OP_LESSTHAN
    out.push_back(0x63); // OP_IF
    out.push_back(0x75); out.push_back(0x51); // OP_DROP OP_1
    out.push_back(0x68); // OP_ENDIF
    return out;
}

// Build LWMA-v2 DAA bytecode. Mirrors buildLinearDaaBytecode() in script.ts.
static std::vector<uint8_t> BuildLwmaDaaBytecode() {
    const std::vector<uint8_t> radixPush = DmintPushMinimal(65536);
    const std::vector<uint8_t> clampPush = DmintPushMinimal(16384);
    const std::vector<uint8_t> negClamp  = DmintPushMinimal(-16384);
    static const uint8_t QMT[9] = {0x08,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0x1f};

    std::vector<uint8_t> out;
    out.push_back(0xc5); // OP_TXLOCKTIME
    out.push_back(0x52); out.push_back(0x79); // OP_2 OP_PICK
    out.push_back(0x94); // OP_SUB
    out.push_back(0x53); out.push_back(0x79); // OP_3 OP_PICK
    out.push_back(0x94); // OP_SUB
    out.insert(out.end(), radixPush.begin(), radixPush.end());
    out.push_back(0x95); // OP_MUL
    out.push_back(0x53); out.push_back(0x79); // OP_3 OP_PICK (targetTime as divisor)
    out.push_back(0x96); // OP_DIV
    out.insert(out.end(), clampPush.begin(), clampPush.end());
    out.push_back(0xa3); // OP_MIN
    out.insert(out.end(), negClamp.begin(), negClamp.end());
    out.push_back(0xa4); // OP_MAX
    out.push_back(0x7c); // OP_SWAP
    out.insert(out.end(), QMT, QMT + 9);
    out.push_back(0xa3); // OP_MIN
    out.push_back(0x76); // OP_DUP
    out.insert(out.end(), radixPush.begin(), radixPush.end());
    out.push_back(0x96); // OP_DIV
    out.push_back(0x7b); // OP_ROT
    out.push_back(0x95); // OP_MUL
    out.push_back(0x93); // OP_ADD
    out.insert(out.end(), QMT, QMT + 9);
    out.push_back(0xa3); // OP_MIN
    out.push_back(0x76); out.push_back(0x51); out.push_back(0x9f);
    out.push_back(0x63);
    out.push_back(0x75); out.push_back(0x51);
    out.push_back(0x68);
    return out;
}

// Fixed 21-byte MINIMAL_PUSH bytecode (inlined twice in PartC ELSE_BRANCH).
// Emits the script bytes that, at runtime, convert the top-of-stack script-number
// into a minimal-push byte sequence. See MINIMAL_PUSH_BYTECODE in script.ts.
static const uint8_t MINIMAL_PUSH_BYTES[] = {
    0x76,0x00,0x9c,0x63,0x75,0x01,0x00,0x67,
    0x76,0x60,0xa1,0x63,0x01,0x50,0x93,0x51,
    0x80,0x67,0x82,0x7c,0x7e,0x68,0x68
};
static const size_t MINIMAL_PUSH_SIZE = sizeof(MINIMAL_PUSH_BYTES);

// Build the full dMint contract scriptPubKey.
// Mirrors dMintScript() in Photonic packages/lib/src/script.ts (v2 format, 2026-06-19).
//
// contractRef36 / tokenRef36 are the 36-byte outpoint refs (txid_LE + vout_LE32).
static CScript BuildDmintScript(
    const std::vector<uint8_t> &contractRef36,
    const std::vector<uint8_t> &tokenRef36,
    int64_t maxHeight,
    int64_t reward,
    int64_t target,          // computed from MAX_TARGET / difficulty
    int     algoId,          // 0=sha256d, 1=blake3, 2=k12
    int     daaId,           // 0=fixed, 1=epoch, 2=asert, 3=lwma, 4=schedule
    int     targetTime,      // seconds (DAA target block time)
    int     halfLife,        // ASERT half-life in seconds (asert mode only)
    uint32_t genesisTimestamp // current time (used as lastTime for initial state)
) {
    // ── State script (10 items) ──
    // height=0 | d8:contractRef | d0:tokenRef | maxHeight | reward |
    // algoId | daaId | targetTime | lastTime(4bytes) | target
    std::vector<uint8_t> state;
    // item 1: height=0 → OP_0
    state.push_back(0x00);
    // item 2: d8 + contractRef36 (OP_PUSHINPUTREFSINGLETON + 36 bytes)
    state.push_back(0xd8);
    state.insert(state.end(), contractRef36.begin(), contractRef36.end());
    // item 3: d0 + tokenRef36
    state.push_back(0xd0);
    state.insert(state.end(), tokenRef36.begin(), tokenRef36.end());
    // items 4-8: variable pushes
    { auto b = DmintPushMinimal(maxHeight); state.insert(state.end(), b.begin(), b.end()); }
    { auto b = DmintPushMinimal(reward);    state.insert(state.end(), b.begin(), b.end()); }
    { auto b = DmintPushMinimal(algoId);    state.insert(state.end(), b.begin(), b.end()); }
    { auto b = DmintPushMinimal(daaId);     state.insert(state.end(), b.begin(), b.end()); }
    { auto b = DmintPushMinimal(targetTime); state.insert(state.end(), b.begin(), b.end()); }
    // item 9: lastTime as fixed 4 bytes
    { auto b = DmintPush4Bytes(genesisTimestamp); state.insert(state.end(), b.begin(), b.end()); }
    // item 10: target (minimal push of 63-bit value)
    { auto b = DmintPushMinimal(target);    state.insert(state.end(), b.begin(), b.end()); }

    // ── Middle literal (items 2-8, baked into PartC for runtime reconstruction) ──
    // d8:contractRef | d0:tokenRef | maxHeight | reward | algoId | daaId | targetTime
    std::vector<uint8_t> middle;
    middle.push_back(0xd8);
    middle.insert(middle.end(), contractRef36.begin(), contractRef36.end());
    middle.push_back(0xd0);
    middle.insert(middle.end(), tokenRef36.begin(), tokenRef36.end());
    { auto b = DmintPushMinimal(maxHeight);  middle.insert(middle.end(), b.begin(), b.end()); }
    { auto b = DmintPushMinimal(reward);     middle.insert(middle.end(), b.begin(), b.end()); }
    { auto b = DmintPushMinimal(algoId);     middle.insert(middle.end(), b.begin(), b.end()); }
    { auto b = DmintPushMinimal(daaId);      middle.insert(middle.end(), b.begin(), b.end()); }
    { auto b = DmintPushMinimal(targetTime); middle.insert(middle.end(), b.begin(), b.end()); }
    const std::vector<uint8_t> middlePush = DmintEncodeDataPush(middle);

    // ── Bytecode PartA (16 bytes, fixed for STATE_ITEM_COUNT=10) ──
    // c0=OP_INPUTINDEX c8=OP_OUTPOINTTXHASH 59=OP_9 79=OP_PICK 7e=OP_CAT a8=OP_SHA256
    // 5d=OP_13 5e=OP_14 7a=OP_ROLL
    static const uint8_t PART_A[] = {
        0xc0,0xc8, 0x59,0x79,0x7e,0xa8, 0x5d,0x79,0x5d,0x79,0x7e,0xa8, 0x7e, 0x5e,0x7a,0x7e
    };

    // ── Bytecode PartB = B1 + B2 + DAA + B4 ──
    static const uint8_t PART_B1[] = {
        0xbc,0x01,0x14,0x7f,0x77,0x58,0x7f,0x04,0x00,0x00,0x00,0x00,0x88,0x81,0x76,0x00,0xa2,0x69
    };
    static const uint8_t PART_B2[] = { 0x51,0x79,0x7c,0xa2,0x69 };
    static const uint8_t PART_B4[] = { 0x6b,0x75,0x75,0x75,0x75 };

    std::vector<uint8_t> daaBytes;
    switch (daaId) {
        case 2: daaBytes = BuildAsertDaaBytecode(halfLife); break;
        case 3: daaBytes = BuildLwmaDaaBytecode(); break;
        default: break; // fixed: no extra bytes
    }

    // ── Bytecode PartC ──
    // PROLOGUE: input/output script-ref checks, height increment, branch on maxHeight
    static const uint8_t PROLOGUE[] = {
        0x57,0x7a,0xe5,0x00,0xa0,0x69,0x56,0x7a,0xe6,0x00,0xa0,0x69,0x01,0xd0,0x53,0x79,
        0x7e,0x0c,0xde,0xc0,0xe9,0xaa,0x76,0xe3,0x78,0xe4,0xa2,0x69,0xe6,0x9d,0x7e,0xaa,
        0x76,0xe4,0x7b,0x9d,0x54,0x7a,0x81,0x8b,0x76,0x53,0x7a,0x9c,0x53,0x7a,0xde,0x78,
        0x91,0x81,0x54,0x7a,0xe6,0x93,0x9d,0x63
    };
    // IF_BRANCH: final mint → burn output check
    static const uint8_t IF_BRANCH[] = {
        0x6c,0x75, 0x52,0x79,0xcd,0x01,0xd8,0x53,0x79,0x7e,0x01,0x6a,0x7e,0x88
    };
    // ELSE_BRANCH static prefix: singleton-count check + DUP before MINIMAL_PUSH
    static const uint8_t ELSE_PREFIX[] = { 0x78,0xde,0x51,0x9d, 0x76 };
    // After second MINIMAL_PUSH: continuation epilogue
    static const uint8_t ELSE_SUFFIX[] = {
        0x7e, // CAT newTarget
        0x53,0x79,0xec,0x78,0x88, // 3 PICK STATESCRIPTBYTECODE_OUTPUT_NOSEP OVER EQUALVERIFY
        0x53,0x79,0xea,0xc0,0xe9,0x88, // 3 PICK OUTPUTCODESCRIPTBYTECODE INPUTINDEX OUTPUT EQUALVERIFY
        0x53,0x79,0xcc,0x51,0x9d, // 3 PICK OUTPUTVALUE 1 NUMEQUALVERIFY
        0x75 // DROP
    };
    // c55480547c7e7e: TXLOCKTIME 4 NUM2BIN OP_4 SWAP CAT CAT
    static const uint8_t ELSE_MID[] = { 0xc5,0x54,0x80,0x54,0x7c,0x7e,0x7e };
    static const uint8_t PART_C_EPILOGUE[] = { 0x68,0x6d,0x75,0x51 };

    // Assemble full script
    std::vector<uint8_t> script;
    // State script
    script.insert(script.end(), state.begin(), state.end());
    // OP_CODESEPARATOR
    script.push_back(0xbd);
    // PartA
    script.insert(script.end(), PART_A, PART_A + sizeof(PART_A));
    // PoW hash opcode
    const uint8_t powOpcodes[] = {0xaa, 0xee, 0xef}; // sha256d, blake3, k12
    script.push_back(powOpcodes[algoId < 3 ? algoId : 0]);
    // PartB
    script.insert(script.end(), PART_B1, PART_B1 + sizeof(PART_B1));
    script.insert(script.end(), PART_B2, PART_B2 + sizeof(PART_B2));
    script.insert(script.end(), daaBytes.begin(), daaBytes.end());
    script.insert(script.end(), PART_B4, PART_B4 + sizeof(PART_B4));
    // PartC
    script.insert(script.end(), PROLOGUE, PROLOGUE + sizeof(PROLOGUE));
    script.insert(script.end(), IF_BRANCH, IF_BRANCH + sizeof(IF_BRANCH));
    script.push_back(0x67); // OP_ELSE
    // ELSE_BRANCH
    script.insert(script.end(), ELSE_PREFIX, ELSE_PREFIX + sizeof(ELSE_PREFIX));
    script.insert(script.end(), MINIMAL_PUSH_BYTES, MINIMAL_PUSH_BYTES + MINIMAL_PUSH_SIZE); // DUP newHeight, MINIMAL_PUSH
    script.insert(script.end(), middlePush.begin(), middlePush.end()); // push middle literal
    script.push_back(0x7e); // OP_CAT
    script.insert(script.end(), ELSE_MID, ELSE_MID + sizeof(ELSE_MID)); // append lastTime
    script.push_back(0x6c); // OP_FROMALTSTACK (newTarget)
    script.insert(script.end(), MINIMAL_PUSH_BYTES, MINIMAL_PUSH_BYTES + MINIMAL_PUSH_SIZE); // MINIMAL_PUSH newTarget
    script.insert(script.end(), ELSE_SUFFIX, ELSE_SUFFIX + sizeof(ELSE_SUFFIX));
    // Epilogue (ENDIF 2DROP DROP OP_1)
    script.insert(script.end(), PART_C_EPILOGUE, PART_C_EPILOGUE + sizeof(PART_C_EPILOGUE));

    return CScript(script.begin(), script.end());
}

// Build the P2PKH scriptPubKey for a given key ID (OP_DUP OP_HASH160 <pkh> OP_EQUALVERIFY OP_CHECKSIG).
static CScript BuildP2PKH(const CKeyID &keyID) {
    std::vector<uint8_t> raw = {0x76, 0xa9, 0x14};
    raw.insert(raw.end(), keyID.begin(), keyID.end());
    raw.push_back(0x88); raw.push_back(0xac);
    return CScript(raw.begin(), raw.end());
}

// Sign a P2PKH input manually. Returns [sig][pubkey] scriptSig.
static CScript SignP2PKH(const CKey &key, const CPubKey &pubKey,
                          const CScript &scriptPubKey,
                          const CMutableTransaction &tx, int inputIdx, Amount value) {
    const SigHashType sht = SigHashType().withForkId();
    const uint256 sighash  = SignatureHash(scriptPubKey, CTransaction(tx), inputIdx, sht, value,
                                            nullptr, SCRIPT_ENABLE_SIGHASH_FORKID);
    std::vector<uint8_t> sig;
    if (!key.SignECDSA(sighash, sig)) throw JSONRPCError(RPC_WALLET_ERROR, "SignECDSA failed");
    sig.push_back(uint8_t(sht.getRawSigHashType()));
    CScript s;
    s << sig << std::vector<uint8_t>(pubKey.begin(), pubKey.end());
    return s;
}

// ── CBOR helpers for dMint payloads ──────────────────────────────────────────

// Encode the FT dMint glyph CBOR payload (p:[1,4] + dmint field).
static std::vector<uint8_t> EncodeFtDmintCbor(
    const std::string &name,
    const std::string &ticker,
    const std::string &desc,
    int     algoId,
    int     numContracts,
    int64_t maxHeight,
    int64_t reward,
    int64_t premine,
    int64_t difficulty,
    int     daaId,          // 0=fixed, others use daaParams
    int     targetBlockTime,
    int     halfLife,
    const std::string &mainMimeType,
    const std::vector<uint8_t> &mainData,
    const std::string &mainUrl)
{
    const bool hasMainEmb = !mainData.empty();
    const bool hasMain    = hasMainEmb || !mainUrl.empty();

    // Count top-level map fields: v, p, name, ticker, [desc], [main], dmint
    size_t nFields = 5; // v, p, name, ticker, dmint (always)
    if (!desc.empty()) nFields++;
    if (hasMain) nFields++;

    std::vector<uint8_t> out;
    CborHead(out, 5, nFields);
    CborText(out, "v"); CborUint(out, 2);
    CborText(out, "p"); CborHead(out, 4, 2); CborUint(out, 1); CborUint(out, 4); // [1,4]
    CborText(out, "name"); CborText(out, name);
    CborText(out, "ticker"); CborText(out, ticker);
    if (!desc.empty()) { CborText(out, "desc"); CborText(out, desc); }

    if (hasMain) {
        CborText(out, "main");
        if (hasMainEmb) {
            CborHead(out, 5, 2);
            CborText(out, "t");
            CborText(out, mainMimeType.empty() ? "application/octet-stream" : mainMimeType);
            CborText(out, "b");
            CborBytes(out, mainData);
        } else {
            CborHead(out, 5, 1);
            CborText(out, "u");
            CborText(out, mainUrl);
        }
    }

    // dmint object
    // fields: algo, numContracts, maxHeight, reward, premine, diff, [daa]
    const bool hasDaa = (daaId != 0); // "fixed" = 0 needs no daa sub-object
    size_t dmintFields = 6; // algo, numContracts, maxHeight, reward, premine, diff
    if (hasDaa) dmintFields++;
    CborText(out, "dmint");
    CborHead(out, 5, dmintFields);
    CborText(out, "algo");         CborUint(out, uint64_t(algoId));
    CborText(out, "numContracts"); CborUint(out, uint64_t(numContracts));
    CborText(out, "maxHeight");    CborUint(out, uint64_t(maxHeight));
    CborText(out, "reward");       CborUint(out, uint64_t(reward));
    CborText(out, "premine");      CborUint(out, uint64_t(premine));
    CborText(out, "diff");         CborUint(out, uint64_t(difficulty));

    if (hasDaa) {
        // daa sub-object: mode, targetBlockTime, [halfLife]
        const bool hasHl = (daaId == 2); // asert
        CborText(out, "daa");
        CborHead(out, 5, hasHl ? size_t(3) : size_t(2));
        CborText(out, "mode");           CborUint(out, uint64_t(daaId));
        CborText(out, "targetBlockTime"); CborUint(out, uint64_t(targetBlockTime));
        if (hasHl) { CborText(out, "halfLife"); CborUint(out, uint64_t(halfLife)); }
    }

    return out;
}

// Encode the NFT link glyph CBOR payload {v:2, p:[2], loc:0}.
static std::vector<uint8_t> EncodeNftLinkCbor() {
    std::vector<uint8_t> out;
    CborHead(out, 5, 3); // map(3)
    CborText(out, "v"); CborUint(out, 2);
    CborText(out, "p"); CborHead(out, 4, 1); CborUint(out, 2); // [2] = NFT
    CborText(out, "loc"); CborUint(out, 0);
    return out;
}

// ── mintdmint RPC ─────────────────────────────────────────────────────────

static UniValue mintdmint(const Config &config, const JSONRPCRequest &request) {
    std::shared_ptr<CWallet> const wallet = GetWalletForJSONRPCRequest(request);
    CWallet *const pwallet = wallet.get();
    if (!EnsureWalletIsAvailable(pwallet, request.fHelp)) return UniValue();

    if (request.fHelp || request.params.size() < 7) {
        throw std::runtime_error(
            RPCHelpMan{"mintdmint",
                "\nDeploy a Glyph v2 dMint (decentralized minting) token.\n"
                "Creates both the commit and reveal transactions server-side.\n"
                "Private keys never leave the node.\n"
                "The reveal tx has 4 inputs (FT commit, contract singleton, NFT link commit, fee funding)\n"
                "and must be broadcast after the commit tx is confirmed." +
                HelpRequiringPassphrase(pwallet) + "\n",
                {
                    {"funding_txid",     RPCArg::Type::STR, false, "", "Txid of the UTXO funding the commit tx."},
                    {"funding_vout",     RPCArg::Type::NUM, false, "", "Vout index of the funding UTXO."},
                    {"to_address",       RPCArg::Type::STR, false, "", "Destination address (must own the key)."},
                    {"name",             RPCArg::Type::STR, false, "", "Token name."},
                    {"ticker",           RPCArg::Type::STR, false, "", "Ticker symbol."},
                    {"num_mints",        RPCArg::Type::NUM, false, "", "Total number of mints allowed (maxHeight)."},
                    {"reward",           RPCArg::Type::NUM, false, "", "Tokens per mint in atomic units."},
                    {"difficulty",       RPCArg::Type::NUM, true,  "10",    "Initial mining difficulty."},
                    {"algorithm",        RPCArg::Type::STR, true,  "blake3","PoW algorithm: sha256d, blake3, k12."},
                    {"daa_mode",         RPCArg::Type::STR, true,  "asert", "DAA mode: fixed, asert, lwma."},
                    {"target_block_time",RPCArg::Type::NUM, true,  "60",    "Target seconds between mints (DAA)."},
                    {"half_life",        RPCArg::Type::NUM, true,  "240",   "ASERT half-life in seconds."},
                    {"num_contracts",    RPCArg::Type::NUM, true,  "1",     "Number of mining contracts (1 for adaptive DAA)."},
                    {"premine",          RPCArg::Type::NUM, true,  "0",     "Tokens immediately minted to to_address."},
                    {"desc",             RPCArg::Type::STR, true,  "",      "Description."},
                    {"main_type",        RPCArg::Type::STR, true,  "",      "MIME type of embedded content (e.g. image/png)."},
                    {"main_data",        RPCArg::Type::STR, true,  "",      "Embedded content as hex (e.g. token image)."},
                    {"main_url",         RPCArg::Type::STR, true,  "",      "URL for remote content (alternative to main_data)."},
                    {"fee_rate",         RPCArg::Type::NUM, true,  "0",     "Fee rate sat/kB (0 = minimum)."},
                }}
                .ToString() +
            "\nResult:\n"
            "{\n"
            "  \"commit_txid\":   \"hex\",  Commit transaction id\n"
            "  \"commit_hex\":    \"hex\",  Signed commit tx hex\n"
            "  \"reveal_hex\":    \"hex\",  Signed reveal tx hex\n"
            "  \"ft_ref\":        \"hex\",  72-char FT token reference\n"
            "  \"contract_refs\": [\"hex\",...], Contract reference(s)\n"
            "  \"nft_ref\":       \"hex\",  72-char NFT link reference\n"
            "  \"ft_cbor_hex\":   \"hex\",  FT CBOR payload (debug)\n"
            "}\n");
    }

    // ── Parse parameters ──────────────────────────────────────────────────
    auto getOptStr  = [&](size_t i) -> std::string {
        return (request.params.size() > i && !request.params[i].isNull()) ? request.params[i].get_str() : "";
    };
    auto getOptInt  = [&](size_t i, int64_t def) -> int64_t {
        return (request.params.size() > i && !request.params[i].isNull()) ? request.params[i].get_int64() : def;
    };

    const std::string fundingTxidStr = request.params[0].get_str();
    const uint32_t    fundingVout    = uint32_t(request.params[1].get_int());
    const std::string toAddressStr   = request.params[2].get_str();
    const std::string tokenName      = request.params[3].get_str();
    const std::string tokenTicker    = request.params[4].get_str();
    const int64_t     numMints       = request.params[5].get_int64();
    const int64_t     reward         = request.params[6].get_int64();
    const int64_t     difficulty     = getOptInt(7,  10);
    const std::string algorithmStr   = getOptStr(8).empty() ? "blake3" : getOptStr(8);
    const std::string daaModeStr     = getOptStr(9).empty() ? "asert"  : getOptStr(9);
    const int         targetBlockTime= int(getOptInt(10, 60));
    const int         halfLife       = int(getOptInt(11, 240));
    const int         numContracts   = int(getOptInt(12, 1));
    const int64_t     premine        = getOptInt(13, 0);
    const std::string desc           = getOptStr(14);
    const std::string mainMimeType   = getOptStr(15);
    const std::string mainDataHex    = getOptStr(16);
    std::vector<uint8_t> mainData;
    if (!mainDataHex.empty()) {
        if (mainDataHex.size() % 2 != 0)
            throw JSONRPCError(RPC_INVALID_PARAMETER, "main_data must be even-length hex");
        mainData = ParseHex(mainDataHex);
    }
    const std::string mainUrl        = getOptStr(17);
    if (!mainData.empty() && !mainUrl.empty())
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Specify either main_data or main_url, not both");
    const int64_t     feeRateSatPerKb= getOptInt(18, 0);

    // Validate
    if (tokenName.empty())   throw JSONRPCError(RPC_INVALID_PARAMETER, "name must not be empty");
    if (tokenTicker.empty()) throw JSONRPCError(RPC_INVALID_PARAMETER, "ticker must not be empty");
    if (numMints <= 0)       throw JSONRPCError(RPC_INVALID_PARAMETER, "num_mints must be positive");
    if (reward <= 0)         throw JSONRPCError(RPC_INVALID_PARAMETER, "reward must be positive");
    if (difficulty <= 0)     throw JSONRPCError(RPC_INVALID_PARAMETER, "difficulty must be positive");
    if (numContracts < 1 || numContracts > 32)
        throw JSONRPCError(RPC_INVALID_PARAMETER, "num_contracts must be 1-32");
    if (premine < 0)         throw JSONRPCError(RPC_INVALID_PARAMETER, "premine must be >= 0");

    const std::map<std::string, int> algoMap  = {{"sha256d",0},{"blake3",1},{"k12",2}};
    const std::map<std::string, int> daaMap   = {{"fixed",0},{"epoch",1},{"asert",2},{"lwma",3},{"schedule",4}};
    if (!algoMap.count(algorithmStr)) throw JSONRPCError(RPC_INVALID_PARAMETER, "algorithm must be sha256d, blake3, or k12");
    if (!daaMap.count(daaModeStr))    throw JSONRPCError(RPC_INVALID_PARAMETER, "daa_mode must be fixed, asert, lwma, epoch, or schedule");
    const int algoId = algoMap.at(algorithmStr);
    const int daaId  = daaMap.at(daaModeStr);

    // MAX_TARGET / difficulty (matches dMintDiffToTarget in script.ts)
    const int64_t target = int64_t(0x7fffffffffffffffLL / difficulty);

    // ── Wallet setup ──────────────────────────────────────────────────────
    auto locked_chain = pwallet->chain().lock();
    LOCK(pwallet->cs_wallet);
    EnsureWalletIsUnlocked(pwallet);

    TxId fundingTxId(uint256S(fundingTxidStr));
    auto mi = pwallet->mapWallet.find(fundingTxId);
    if (mi == pwallet->mapWallet.end())
        throw JSONRPCError(RPC_WALLET_ERROR, "Funding transaction not found in wallet");
    if (fundingVout >= mi->second.tx->vout.size())
        throw JSONRPCError(RPC_INVALID_PARAMETER, "funding_vout out of range");
    const CTxOut &fundingOut  = mi->second.tx->vout[fundingVout];
    const Amount  fundingSats = fundingOut.nValue;

    CTxDestination toDest = DecodeDestination(toAddressStr, config.GetChainParams());
    if (!IsValidDestination(toDest))
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid to_address");
    const CKeyID *pToKeyID = boost::get<CKeyID>(&toDest);
    if (!pToKeyID) throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "to_address must be a P2PKH address");
    const CKeyID toKeyID = *pToKeyID;

    CKey signingKey;
    if (!pwallet->GetKey(toKeyID, signingKey))
        throw JSONRPCError(RPC_WALLET_ERROR, "Private key for to_address not available in wallet");
    const CPubKey signingPubKey = signingKey.GetPubKey();
    const std::vector<uint8_t> pubKeyBytes(signingPubKey.begin(), signingPubKey.end());
    const CScript p2pkhScript = BuildP2PKH(toKeyID);

    // ── Fee rate ──────────────────────────────────────────────────────────
    const int tipHeight = locked_chain->getHeight().value_or(0);
    const auto &consensus = config.GetChainParams().GetConsensus();
    const CFeeRate effectiveMinRate = GetEffectiveMinRelayFee(tipHeight, consensus);
    const CFeeRate feeRate = (feeRateSatPerKb > 0 &&
                               CFeeRate(feeRateSatPerKb * SATOSHI) >= effectiveMinRate)
        ? CFeeRate(feeRateSatPerKb * SATOSHI) : effectiveMinRate;

    // ── CBOR payloads ─────────────────────────────────────────────────────
    const std::vector<uint8_t> ftCbor = EncodeFtDmintCbor(
        tokenName, tokenTicker, desc,
        algoId, numContracts, numMints, reward, premine, difficulty,
        daaId, targetBlockTime, halfLife,
        mainMimeType, mainData, mainUrl);
    const std::vector<uint8_t> nftCbor = EncodeNftLinkCbor();

    // ── scriptSig sizes for fee estimation ───────────────────────────────
    // FT reveal scriptSig: [sig(74)] [pubkey(34)] [gly(4)] [cbor(overhead+len)]
    auto cborPushOverhead = [](size_t len) -> int64_t {
        if (len <= 75)    return 1;
        if (len <= 255)   return 2;
        if (len <= 65535) return 3;
        return 5;
    };
    auto varintSize = [](int64_t n) -> int64_t {
        return n <= 252 ? 1 : n <= 65535 ? 3 : 5;
    };

    const int64_t ftSigSize  = 74 + 34 + 4 + cborPushOverhead(ftCbor.size())  + int64_t(ftCbor.size());
    const int64_t nftSigSize = 74 + 34 + 4 + cborPushOverhead(nftCbor.size()) + int64_t(nftCbor.size());
    const int64_t p2pkhSigSize = 107; // standard compressed-pubkey P2PKH scriptSig

    // Estimate dMint contract script size (builds a dummy script with same structure)
    // Use zero refs just for size estimation; actual refs are 36 bytes each.
    static const std::vector<uint8_t> dummyRef36(36, 0x00);
    const CScript dummyContract = BuildDmintScript(dummyRef36, dummyRef36,
        numMints, reward, target, algoId, daaId, targetBlockTime, halfLife, 0);
    const int64_t contractScriptSize = int64_t(dummyContract.size());

    // ── Commit tx size ────────────────────────────────────────────────────
    // Inputs:  1 × P2PKH funding (148 bytes)
    // Outputs: 1 FT commit (75 bytes) + numContracts × P2PKH (25 bytes each)
    //          + 1 NFT commit (75 bytes) + 1 P2PKH reveal-fee output (25 bytes) + 1 change (25 bytes)
    const int64_t commitTxBytes =
        4 + 1 + (32 + 4 + 1 + p2pkhSigSize + 4)      // input
        + 1
        + (8 + 1 + 75)                                  // FT commit
        + int64_t(numContracts) * (8 + 1 + 25)          // singleton P2PKH(s)
        + (8 + 1 + 75)                                  // NFT commit
        + (8 + 1 + 25)                                  // reveal-fee P2PKH
        + (8 + 1 + 25)                                  // change
        + 4;
    const Amount commitFeeSats = feeRate.GetFee(commitTxBytes);

    // ── Reveal tx size ────────────────────────────────────────────────────
    // Inputs: FT commit, numContracts×P2PKH singleton, NFT commit, P2PKH fee-reservoir
    // Outputs: numContracts×dMint contract, NFT link, [premine FT], change(in fee-reservoir)
    const int64_t revealTxBytes =
        4 + 1
        + (32 + 4 + varintSize(ftSigSize)  + ftSigSize  + 4)          // FT commit in
        + int64_t(numContracts) * (32 + 4 + 1 + p2pkhSigSize + 4)     // singleton ins
        + (32 + 4 + varintSize(nftSigSize) + nftSigSize + 4)          // NFT commit in
        + (32 + 4 + 1 + p2pkhSigSize + 4)                              // fee-reservoir in
        + 1
        + int64_t(numContracts) * (8 + 1 + contractScriptSize)         // dMint contract outs
        + (8 + 1 + 63)                                                  // NFT link out
        + (premine > 0 ? (8 + 1 + 63) : 0)                             // premine FT out
        + 4;
    const Amount revealFeeSats = feeRate.GetFee(revealTxBytes);

    // ── Funding check ─────────────────────────────────────────────────────
    // Commit outputs (sats): 1(FT commit) + nc×1(singletons) + 1(NFT commit) + revealReserve
    // revealReserve covers: nc×1sat(contracts) + 1sat(NFT) + premine + revealFee
    const int64_t revealReserve = int64_t(numContracts) + 1 + premine + (revealFeeSats / SATOSHI);
    const int64_t commitSmallOuts = 1 + int64_t(numContracts) + 1; // FT + singletons + NFT
    const int64_t changeSats = (fundingSats / SATOSHI) - (commitFeeSats / SATOSHI)
                                - commitSmallOuts - revealReserve;
    if (changeSats < 0) {
        throw JSONRPCError(RPC_WALLET_INSUFFICIENT_FUNDS,
            strprintf("Insufficient funds: need ~%d sat, have %d sat",
                      (commitFeeSats / SATOSHI) + commitSmallOuts + revealReserve,
                      fundingSats / SATOSHI));
    }

    // ── Build commit tx ───────────────────────────────────────────────────
    const uint256 ftPayloadHash  = Hash(ftCbor);
    const uint256 nftPayloadHash = Hash(nftCbor);
    std::vector<uint8_t> ftHashBytes(ftPayloadHash.begin(),  ftPayloadHash.end());
    std::vector<uint8_t> nftHashBytes(nftPayloadHash.begin(), nftPayloadHash.end());

    CMutableTransaction commitTx;
    commitTx.nVersion  = 2;
    commitTx.nLockTime = 0;

    CTxIn commitFundIn;
    commitFundIn.prevout  = COutPoint(fundingTxId, fundingVout);
    commitFundIn.nSequence = 0xffffffff;
    commitTx.vin.push_back(commitFundIn);

    // vout 0: FT commit (isSingleton=false → OP_REFTYPE_OUTPUT 1)
    commitTx.vout.push_back(CTxOut(1 * SATOSHI, BuildCommitScript(toKeyID, ftHashBytes, false)));
    // vout 1..nc: P2PKH singletons
    for (int i = 0; i < numContracts; i++)
        commitTx.vout.push_back(CTxOut(1 * SATOSHI, p2pkhScript));
    // vout nc+1: NFT link commit (isSingleton=true → OP_REFTYPE_OUTPUT 2)
    commitTx.vout.push_back(CTxOut(1 * SATOSHI, BuildCommitScript(toKeyID, nftHashBytes, true)));
    // vout nc+2: P2PKH reveal-fee reservoir
    commitTx.vout.push_back(CTxOut(revealReserve * SATOSHI, p2pkhScript));
    // vout nc+3: change (optional)
    if (changeSats > 546)
        commitTx.vout.push_back(CTxOut(changeSats * SATOSHI, fundingOut.scriptPubKey));

    if (!pwallet->SignTransaction(commitTx))
        throw JSONRPCError(RPC_WALLET_ERROR, "Failed to sign commit tx");

    const CTransaction commitTxFinal(commitTx);
    const TxId commitTxId  = commitTxFinal.GetId();
    const std::string commitHex = EncodeHexTx(commitTxFinal);

    // ── Derive outpoint refs (txid LE + vout LE32) ───────────────────────
    auto makeRef36 = [&](uint32_t vout) -> std::vector<uint8_t> {
        std::vector<uint8_t> ref(commitTxId.begin(), commitTxId.end());
        ref.push_back(vout & 0xff); ref.push_back((vout >> 8) & 0xff);
        ref.push_back((vout >> 16) & 0xff); ref.push_back((vout >> 24) & 0xff);
        return ref;
    };
    const std::vector<uint8_t> ftRef36  = makeRef36(0);
    const std::vector<uint8_t> nftRef36 = makeRef36(uint32_t(numContracts + 1));

    // ── Build reveal tx ───────────────────────────────────────────────────
    CMutableTransaction revealTx;
    revealTx.nVersion  = 1;
    const uint32_t genesisTs = uint32_t(std::time(nullptr));
    revealTx.nLockTime = genesisTs;

    // Inputs
    // in 0: FT commit
    { CTxIn in; in.prevout = COutPoint(commitTxId, 0); in.nSequence = 0xffffffff; revealTx.vin.push_back(in); }
    // in 1..nc: P2PKH singletons
    for (int i = 0; i < numContracts; i++) {
        CTxIn in; in.prevout = COutPoint(commitTxId, uint32_t(1 + i)); in.nSequence = 0xffffffff;
        revealTx.vin.push_back(in);
    }
    // in nc+1: NFT commit
    { CTxIn in; in.prevout = COutPoint(commitTxId, uint32_t(numContracts + 1)); in.nSequence = 0xffffffff; revealTx.vin.push_back(in); }
    // in nc+2: fee reservoir
    { CTxIn in; in.prevout = COutPoint(commitTxId, uint32_t(numContracts + 2)); in.nSequence = 0xffffffff; revealTx.vin.push_back(in); }

    // Outputs
    // out 0..nc-1: dMint contract scripts
    for (int i = 0; i < numContracts; i++) {
        const std::vector<uint8_t> contractRef36 = makeRef36(uint32_t(1 + i));
        const CScript dmintScript = BuildDmintScript(contractRef36, ftRef36,
            numMints, reward, target, algoId, daaId, targetBlockTime, halfLife, genesisTs);
        revealTx.vout.push_back(CTxOut(1 * SATOSHI, dmintScript));
    }
    // out nc: NFT link output (OP_PUSHINPUTREFSINGLETON <nftRef36> OP_DROP P2PKH)
    revealTx.vout.push_back(CTxOut(1 * SATOSHI, BuildGlyphOutputScript(commitTxId, uint32_t(numContracts + 1), toKeyID, true)));
    // out nc+1: premine FT output
    if (premine > 0)
        revealTx.vout.push_back(CTxOut(premine * SATOSHI, BuildGlyphOutputScript(commitTxId, 0, toKeyID, false)));
    // No explicit change; fee reservoir minus contract/NFT/premine values = implicit fee

    // ── Sign reveal tx inputs ──────────────────────────────────────────────
    const std::vector<uint8_t> glyMagic = {0x67, 0x6c, 0x79};

    // Input 0: FT commit (non-standard, manual scriptSig)
    {
        const CScript &commitScript0 = commitTxFinal.vout[0].scriptPubKey;
        const SigHashType sht = SigHashType().withForkId();
        const uint256 sh = SignatureHash(commitScript0, CTransaction(revealTx), 0, sht,
                                          1 * SATOSHI, nullptr, SCRIPT_ENABLE_SIGHASH_FORKID);
        std::vector<uint8_t> sig; signingKey.SignECDSA(sh, sig); sig.push_back(uint8_t(sht.getRawSigHashType()));
        CScript ss; ss << sig << pubKeyBytes << glyMagic << ftCbor;
        revealTx.vin[0].scriptSig = ss;
    }
    // Inputs 1..nc: P2PKH singletons
    for (int i = 0; i < numContracts; i++) {
        revealTx.vin[1 + i].scriptSig = SignP2PKH(signingKey, signingPubKey, p2pkhScript,
                                                    revealTx, 1 + i, 1 * SATOSHI);
    }
    // Input nc+1: NFT commit (non-standard, manual scriptSig)
    {
        const int nftInIdx = numContracts + 1;
        const CScript &commitScriptNft = commitTxFinal.vout[nftInIdx].scriptPubKey;
        const SigHashType sht = SigHashType().withForkId();
        const uint256 sh = SignatureHash(commitScriptNft, CTransaction(revealTx), nftInIdx, sht,
                                          1 * SATOSHI, nullptr, SCRIPT_ENABLE_SIGHASH_FORKID);
        std::vector<uint8_t> sig; signingKey.SignECDSA(sh, sig); sig.push_back(uint8_t(sht.getRawSigHashType()));
        CScript ss; ss << sig << pubKeyBytes << glyMagic << nftCbor;
        revealTx.vin[nftInIdx].scriptSig = ss;
    }
    // Input nc+2: fee reservoir (P2PKH, manual)
    {
        const int feeInIdx = numContracts + 2;
        revealTx.vin[feeInIdx].scriptSig = SignP2PKH(signingKey, signingPubKey, p2pkhScript,
                                                      revealTx, feeInIdx, revealReserve * SATOSHI);
    }

    const CTransaction revealTxFinal(revealTx);
    const std::string revealHex = EncodeHexTx(revealTxFinal);

    // ── Build result ──────────────────────────────────────────────────────
    UniValue::Object result;
    result.emplace_back("commit_txid",  commitTxId.GetHex());
    result.emplace_back("commit_hex",   commitHex);
    result.emplace_back("reveal_hex",   revealHex);
    result.emplace_back("ft_ref",       HexStr(ftRef36));
    {
        UniValue::Array crefs;
        for (int i = 0; i < numContracts; i++) crefs.push_back(UniValue(HexStr(makeRef36(uint32_t(1 + i)))));
        result.emplace_back("contract_refs", std::move(crefs));
    }
    result.emplace_back("nft_ref",      HexStr(nftRef36));
    result.emplace_back("ft_cbor_hex",  HexStr(ftCbor));
    return result;
}

// ── Registration ──────────────────────────────────────────────────────────

void RegisterGlyphRPCCommands(CRPCTable &t) {
    // clang-format off
    static const ContextFreeRPCCommand commands[] = {
        { "wallet", "mintglyph", mintglyph,
          {"funding_txid","funding_vout","to_address","type","name",
           "supply","ticker","decimals","immutable","desc","license",
           "author","container","attrs","main_type","main_data","main_url",
           "fee_rate"} },
        { "wallet", "mintdmint", mintdmint,
          {"funding_txid","funding_vout","to_address","name","ticker",
           "num_mints","reward","difficulty","algorithm","daa_mode",
           "target_block_time","half_life","num_contracts","premine","desc",
           "main_type","main_data","main_url","fee_rate"} },
    };
    // clang-format on
    for (const auto &cmd : commands) t.appendCommand(cmd.name, &cmd);
}
