// Copyright (c) 2025-present The Avian Core developers
// Copyright (c) 2025-present The Radiant Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://opensource.org/license/mit/.

#include <webui/webui_internal.h>

#include <amount.h>
#include <config.h>
#include <core_io.h>
#include <httpserver.h>
#include <rpc/jsonrpcrequest.h>
#include <rpc/server.h>
#include <univalue.h>
#include <util/strencodings.h>

#ifdef ENABLE_WALLET
#include <interfaces/chain.h>
#include <key_io.h>
#include <script/ismine.h>
#include <wallet/wallet.h>
#include <wallet/rpcwallet.h>
#endif

#include <fs.h>
#include <util/time.h>

#include <fstream>
#include <string>
#include <vector>

static std::string UrlEncode(const std::string &s)
{
    std::string out;
    out.reserve(s.size() * 3);
    for (unsigned char c : s) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            out += static_cast<char>(c);
        } else {
            char buf[4];
            snprintf(buf, sizeof(buf), "%%%02X", c);
            out += buf;
        }
    }
    return out;
}

static std::string UrlDecode(const std::string &s)
{
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '%' && i + 2 < s.size()) {
            const int hi = HexDigit(s[i + 1]);
            const int lo = HexDigit(s[i + 2]);
            if (hi >= 0 && lo >= 0) {
                out += static_cast<char>((hi << 4) | lo);
                i += 2;
                continue;
            }
        }
        out += s[i];
    }
    return out;
}

// Helper: execute a wallet-scoped RPC call by setting the wallet path in URI.
// Wallet RPCs in Radiant use URI "/wallet/<name>" for routing.
static UniValue WalletRPC(Config &config, const std::string &wallet_name,
                          const std::string &method, UniValue params)
{
    JSONRPCRequest jreq;
    jreq.strMethod = method;
    jreq.params    = std::move(params);
    jreq.URI       = "/wallet/" + UrlEncode(wallet_name);
    return tableRPC.execute(config, jreq);
}

// ---- Wallet management (loaded / available / load / create / unload) -----

static bool HandleWalletsLoaded(HTTPRequest *req)
{
    if (req->GetRequestMethod() != HTTPRequest::GET) { req->WriteReply(HTTP_BAD_METHOD, ""); return false; }
    if (!CheckWebUIHost(req)) return false;
    auto cors = CheckWebUICORS(req);
    if (!cors) return false;
    if (!CheckWebUIAuth(req)) return false;

    UniValue::Object obj;
#ifdef ENABLE_WALLET
    obj.emplace_back("walletSupportEnabled", UniValue(true));
    UniValue::Array arr;
    for (const auto &wallet : GetWallets()) {
        UniValue::Object wobj;
        wobj.emplace_back("name",      UniValue(wallet->GetName()));
        wobj.emplace_back("encrypted", UniValue(wallet->IsCrypted()));
        wobj.emplace_back("locked",    UniValue(wallet->IsLocked()));
        arr.push_back(UniValue(std::move(wobj)));
    }
    obj.emplace_back("wallets", UniValue(std::move(arr)));
#else
    obj.emplace_back("walletSupportEnabled", UniValue(false));
    obj.emplace_back("wallets", UniValue(UniValue::VARR));
#endif
    SetJSONHeaders(req, *cors);
    req->WriteReply(HTTP_OK, UniValue::stringify(obj));
    return true;
}

static bool HandleWalletsAvailable(Config &config, HTTPRequest *req)
{
    if (req->GetRequestMethod() != HTTPRequest::GET) { req->WriteReply(HTTP_BAD_METHOD, ""); return false; }
    if (!CheckWebUIHost(req)) return false;
    auto cors = CheckWebUICORS(req);
    if (!cors) return false;
    if (!CheckWebUIAuth(req)) return false;

#ifdef ENABLE_WALLET
    try {
        JSONRPCRequest jreq;
        jreq.strMethod = "listwalletdir";
        jreq.params    = UniValue(UniValue::VARR);
        UniValue result = tableRPC.execute(config, jreq);
        SetJSONHeaders(req, *cors);
        req->WriteReply(HTTP_OK, UniValue::stringify(result));
    } catch (const JSONRPCError &e) {
        req->WriteReply(HTTP_INTERNAL_SERVER_ERROR, JsonError(e.message));
        return false;
    } catch (const UniValue &e) {
        const std::string msg = e["message"].isStr() ? e["message"].get_str() : "error";
        req->WriteReply(HTTP_INTERNAL_SERVER_ERROR, JsonError(msg));
        return false;
    }
    return true;
#else
    req->WriteReply(HTTP_SERVICE_UNAVAILABLE, R"({"error":"Wallet support not compiled in"})");
    return false;
#endif
}

static bool HandleWalletLoad(Config &config, HTTPRequest *req)
{
    if (req->GetRequestMethod() != HTTPRequest::POST) { req->WriteReply(HTTP_BAD_METHOD, ""); return false; }
    if (!CheckWebUIHost(req)) return false;
    auto cors = CheckWebUICORS(req);
    if (!cors) return false;
    if (!CheckWebUIAuth(req)) return false;

#ifdef ENABLE_WALLET
    UniValue body;
    if (!body.read(req->ReadBody()) || !body.isObject()) {
        req->WriteReply(HTTP_BAD_REQUEST, R"({"error":"Invalid request body"})");
        return false;
    }
    const UniValue &nameVal = body["name"];
    if (!nameVal.isStr() || nameVal.get_str().empty()) {
        req->WriteReply(HTTP_BAD_REQUEST, R"({"error":"\"name\" field required"})");
        return false;
    }
    try {
        JSONRPCRequest jreq;
        jreq.strMethod = "loadwallet";
        UniValue::Array params;
        params.push_back(nameVal);
        jreq.params = UniValue(std::move(params));
        UniValue result = tableRPC.execute(config, jreq);
        UniValue::Object obj;
        obj.emplace_back("success", UniValue(true));
        obj.emplace_back("result",  result);
        SetJSONHeaders(req, *cors);
        req->WriteReply(HTTP_OK, UniValue::stringify(obj));
    } catch (const JSONRPCError &e) {
        req->WriteReply(HTTP_BAD_REQUEST, JsonError(e.message));
        return false;
    } catch (const UniValue &e) {
        const std::string msg = e["message"].isStr() ? e["message"].get_str() : "load error";
        req->WriteReply(HTTP_BAD_REQUEST, JsonError(msg));
        return false;
    }
    return true;
#else
    req->WriteReply(HTTP_SERVICE_UNAVAILABLE, R"({"error":"Wallet support not compiled in"})");
    return false;
#endif
}

static bool HandleWalletCreate(Config &config, HTTPRequest *req)
{
    if (req->GetRequestMethod() != HTTPRequest::POST) { req->WriteReply(HTTP_BAD_METHOD, ""); return false; }
    if (!CheckWebUIHost(req)) return false;
    auto cors = CheckWebUICORS(req);
    if (!cors) return false;
    if (!CheckWebUIAuth(req)) return false;

#ifdef ENABLE_WALLET
    UniValue body;
    if (!body.read(req->ReadBody()) || !body.isObject()) {
        req->WriteReply(HTTP_BAD_REQUEST, R"({"error":"Invalid request body"})");
        return false;
    }
    const UniValue &nameVal = body["name"];
    if (!nameVal.isStr() || nameVal.get_str().empty()) {
        req->WriteReply(HTTP_BAD_REQUEST, R"({"error":"\"name\" field required"})");
        return false;
    }
    try {
        JSONRPCRequest jreq;
        jreq.strMethod = "createwallet";
        UniValue::Array params;
        params.push_back(nameVal);
        if (body["disablePrivateKeys"].isBool()) params.push_back(body["disablePrivateKeys"]);
        jreq.params = UniValue(std::move(params));
        UniValue result = tableRPC.execute(config, jreq);
        UniValue::Object obj;
        obj.emplace_back("success", UniValue(true));
        obj.emplace_back("result",  result);
        SetJSONHeaders(req, *cors);
        req->WriteReply(HTTP_OK, UniValue::stringify(obj));
    } catch (const JSONRPCError &e) {
        req->WriteReply(HTTP_BAD_REQUEST, JsonError(e.message));
        return false;
    } catch (const UniValue &e) {
        const std::string msg = e["message"].isStr() ? e["message"].get_str() : "create error";
        req->WriteReply(HTTP_BAD_REQUEST, JsonError(msg));
        return false;
    }
    return true;
#else
    req->WriteReply(HTTP_SERVICE_UNAVAILABLE, R"({"error":"Wallet support not compiled in"})");
    return false;
#endif
}

static bool HandleWalletUnload(Config &config, HTTPRequest *req)
{
    if (req->GetRequestMethod() != HTTPRequest::POST) { req->WriteReply(HTTP_BAD_METHOD, ""); return false; }
    if (!CheckWebUIHost(req)) return false;
    auto cors = CheckWebUICORS(req);
    if (!cors) return false;
    if (!CheckWebUIAuth(req)) return false;

#ifdef ENABLE_WALLET
    UniValue body;
    if (!body.read(req->ReadBody()) || !body.isObject()) {
        req->WriteReply(HTTP_BAD_REQUEST, R"({"error":"Invalid request body"})");
        return false;
    }
    const UniValue &nameVal = body["name"];
    if (!nameVal.isStr() || nameVal.get_str().empty()) {
        req->WriteReply(HTTP_BAD_REQUEST, R"({"error":"\"name\" field required"})");
        return false;
    }
    try {
        JSONRPCRequest jreq;
        jreq.strMethod = "unloadwallet";
        UniValue::Array params;
        params.push_back(nameVal);
        jreq.params = UniValue(std::move(params));
        tableRPC.execute(config, jreq);
        UniValue::Object obj;
        obj.emplace_back("success", UniValue(true));
        SetJSONHeaders(req, *cors);
        req->WriteReply(HTTP_OK, UniValue::stringify(obj));
    } catch (const JSONRPCError &e) {
        req->WriteReply(HTTP_BAD_REQUEST, JsonError(e.message));
        return false;
    } catch (const UniValue &e) {
        const std::string msg = e["message"].isStr() ? e["message"].get_str() : "unload error";
        req->WriteReply(HTTP_BAD_REQUEST, JsonError(msg));
        return false;
    }
    return true;
#else
    req->WriteReply(HTTP_SERVICE_UNAVAILABLE, R"({"error":"Wallet support not compiled in"})");
    return false;
#endif
}

// ---- Per-wallet operations -----------------------------------------------

// Helper: call a wallet-scoped RPC, handle errors, return JSON.
static bool WalletRPCReply(Config &config, HTTPRequest *req,
                            const std::string &wallet_name,
                            const std::string &method, UniValue params,
                            const std::string &cors_origin)
{
    try {
        UniValue result = WalletRPC(config, wallet_name, method, std::move(params));
        SetJSONHeaders(req, cors_origin);
        req->WriteReply(HTTP_OK, UniValue::stringify(result));
        return true;
    } catch (const JSONRPCError &e) {
        req->WriteReply(HTTP_BAD_REQUEST, JsonError(e.message));
        return false;
    } catch (const UniValue &e) {
        const std::string msg = e["message"].isStr() ? e["message"].get_str() : "RPC error";
        req->WriteReply(HTTP_BAD_REQUEST, JsonError(msg));
        return false;
    } catch (const std::exception &e) {
        req->WriteReply(HTTP_INTERNAL_SERVER_ERROR, JsonError(e.what()));
        return false;
    }
}

// GET /webui/api/wallet/<name>/summary
static bool HandleWalletSummary(Config &config, HTTPRequest *req,
                                 const std::string &wallet_name)
{
#ifdef ENABLE_WALLET
    auto wallet = GetWallet(wallet_name);
    if (!wallet) {
        req->WriteReply(HTTP_NOT_FOUND, JsonError("Wallet not found: " + wallet_name));
        return false;
    }
    if (!CheckWebUIHost(req)) return false;
    auto cors = CheckWebUICORS(req);
    if (!cors) return false;
    if (!CheckWebUIAuth(req)) return false;

    UniValue::Object obj;
    obj.emplace_back("name",      UniValue(wallet->GetName()));
    obj.emplace_back("encrypted", UniValue(wallet->IsCrypted()));
    obj.emplace_back("locked",    UniValue(wallet->IsLocked()));

    const Amount bal = wallet->GetBalance();
    obj.emplace_back("balance",        ValueFromAmount(bal));
    obj.emplace_back("balanceSatoshi", UniValue(bal / SATOSHI));

    const Amount unconfirmed = wallet->GetUnconfirmedBalance();
    obj.emplace_back("unconfirmed",        ValueFromAmount(unconfirmed));
    obj.emplace_back("unconfirmedSatoshi", UniValue(unconfirmed / SATOSHI));

    const Amount immature = wallet->GetImmatureBalance();
    obj.emplace_back("immature",        ValueFromAmount(immature));
    obj.emplace_back("immatureSatoshi", UniValue(immature / SATOSHI));

    SetJSONHeaders(req, *cors);
    req->WriteReply(HTTP_OK, UniValue::stringify(obj));
    return true;
#else
    req->WriteReply(HTTP_SERVICE_UNAVAILABLE, R"({"error":"Wallet support not compiled in"})");
    return false;
#endif
}

// GET /webui/api/wallet/<name>/transactions
static bool HandleWalletTransactions(Config &config, HTTPRequest *req,
                                      const std::string &wallet_name)
{
    if (!CheckWebUIHost(req)) return false;
    auto cors = CheckWebUICORS(req);
    if (!cors) return false;
    if (!CheckWebUIAuth(req)) return false;

#ifdef ENABLE_WALLET
    // No blockhash parameter → listsinceblock returns all wallet transactions from genesis.
    // Passing "" causes Radiant to look up a block with an empty hash (which fails).
    UniValue::Array params; // empty = no args; all params are optional
    return WalletRPCReply(config, req, wallet_name, "listsinceblock",
                          UniValue(std::move(params)), *cors);
#else
    req->WriteReply(HTTP_SERVICE_UNAVAILABLE, R"({"error":"Wallet support not compiled in"})");
    return false;
#endif
}

// GET /webui/api/wallet/<name>/addresses
static bool HandleWalletAddresses(Config &config, HTTPRequest *req,
                                   const std::string &wallet_name)
{
    if (!CheckWebUIHost(req)) return false;
    auto cors = CheckWebUICORS(req);
    if (!cors) return false;
    if (!CheckWebUIAuth(req)) return false;

#ifdef ENABLE_WALLET
    // listreceivedbyaddress(minconf=0, include_empty=true, include_watchonly=true)
    // returns ALL labelled addresses regardless of whether they have received funds.
    UniValue::Array params;
    params.push_back(UniValue(static_cast<int64_t>(0)));
    params.push_back(UniValue(true));
    params.push_back(UniValue(true));
    return WalletRPCReply(config, req, wallet_name, "listreceivedbyaddress",
                          UniValue(std::move(params)), *cors);
#else
    req->WriteReply(HTTP_SERVICE_UNAVAILABLE, R"({"error":"Wallet support not compiled in"})");
    return false;
#endif
}

// GET /webui/api/wallet/<name>/utxos
static bool HandleWalletUTXOs(Config &config, HTTPRequest *req,
                               const std::string &wallet_name)
{
    if (req->GetRequestMethod() != HTTPRequest::GET) { req->WriteReply(HTTP_BAD_METHOD, ""); return false; }
    if (!CheckWebUIHost(req)) return false;
    auto cors = CheckWebUICORS(req);
    if (!cors) return false;
    if (!CheckWebUIAuth(req)) return false;

#ifdef ENABLE_WALLET
    UniValue::Array params;
    params.push_back(UniValue(static_cast<int64_t>(0)));         // minconf
    params.push_back(UniValue(static_cast<int64_t>(9999999)));   // maxconf
    params.push_back(UniValue(UniValue::VARR));                   // addresses = []
    return WalletRPCReply(config, req, wallet_name, "listunspent",
                          UniValue(std::move(params)), *cors);
#else
    req->WriteReply(HTTP_SERVICE_UNAVAILABLE, R"({"error":"Wallet support not compiled in"})");
    return false;
#endif
}

// GET /webui/api/wallet/<name>/coins[?all=1]
// Returns all wallet outputs.  Without ?all only unspent (via ListCoins).
// With ?all=1 also includes spent outputs with a "spent_by" txid.
static bool HandleWalletCoins(Config &config, HTTPRequest *req,
                               const std::string &wallet_name)
{
    if (req->GetRequestMethod() != HTTPRequest::GET) { req->WriteReply(HTTP_BAD_METHOD, ""); return false; }
    if (!CheckWebUIHost(req)) return false;
    auto cors = CheckWebUICORS(req);
    if (!cors) return false;
    if (!CheckWebUIAuth(req)) return false;

#ifdef ENABLE_WALLET
    const bool show_all = req->GetQueryParameter("all").has_value();

    std::shared_ptr<CWallet> pwallet = GetWallet(wallet_name);
    if (!pwallet) {
        req->WriteReply(HTTP_NOT_FOUND, R"({"error":"Wallet not found or not loaded"})");
        return false;
    }

    UniValue::Array utxo_arr;
    Amount total = Amount::zero();

    {
        auto locked_chain = pwallet->chain().lock();
        LOCK(pwallet->cs_wallet);

        // --- Unspent outputs via ListCoins (groups by address) ---
        for (const auto &entry : pwallet->ListCoins(*locked_chain)) {
            for (const auto &coin : entry.second) {
                const CTxOut &txout = coin.tx->tx->vout[coin.i];
                total += txout.nValue;

                std::string addr_str;
                CTxDestination addr;
                if (ExtractDestination(txout.scriptPubKey, addr))
                    addr_str = EncodeDestination(addr, config);

                UniValue::Object u;
                u.emplace_back("txid",          UniValue(coin.tx->GetId().GetHex()));
                u.emplace_back("vout",          UniValue(static_cast<int64_t>(coin.i)));
                u.emplace_back("address",       UniValue(addr_str));
                u.emplace_back("amount",        ValueFromAmount(txout.nValue));
                u.emplace_back("confirmations", UniValue(static_cast<int64_t>(coin.nDepth)));
                u.emplace_back("is_spent",      UniValue(false));
                utxo_arr.push_back(UniValue(std::move(u)));
            }
        }

        // --- Spent outputs (only when ?all=1) ---
        if (show_all) {
            // Build spending map: (txid_hex, vout) -> spending_txid_hex
            std::map<std::pair<std::string, uint32_t>, std::string> spending_map;
            for (const auto &[txid, wtx] : pwallet->mapWallet) {
                const std::string stxid = txid.GetHex();
                for (const auto &vin : wtx.tx->vin) {
                    if (!vin.prevout.IsNull())
                        spending_map[{vin.prevout.GetTxId().GetHex(), vin.prevout.GetN()}] = stxid;
                }
            }

            for (const auto &[txid, wtx] : pwallet->mapWallet) {
                const std::string txid_hex = txid.GetHex();
                for (size_t i = 0; i < wtx.tx->vout.size(); ++i) {
                    const CTxOut &vout = wtx.tx->vout[i];
                    // Only spendable outputs belonging to this wallet
                    if (IsMine(*pwallet, vout.scriptPubKey) != ISMINE_SPENDABLE) continue;
                    // Only spent ones (unspent already covered by ListCoins above)
                    auto sp_it = spending_map.find({txid_hex, static_cast<uint32_t>(i)});
                    if (sp_it == spending_map.end()) continue;

                    std::string addr_str;
                    CTxDestination addr;
                    if (ExtractDestination(vout.scriptPubKey, addr))
                        addr_str = EncodeDestination(addr, config);

                    UniValue::Object u;
                    u.emplace_back("txid",     UniValue(txid_hex));
                    u.emplace_back("vout",     UniValue(static_cast<int64_t>(i)));
                    u.emplace_back("address",  UniValue(addr_str));
                    u.emplace_back("amount",   ValueFromAmount(vout.nValue));
                    u.emplace_back("is_spent", UniValue(true));
                    u.emplace_back("spent_by", UniValue(sp_it->second));
                    utxo_arr.push_back(UniValue(std::move(u)));
                }
            }
        }
    }

    UniValue::Object obj;
    obj.emplace_back("wallet",      UniValue(wallet_name));
    obj.emplace_back("count",       UniValue(static_cast<int64_t>(utxo_arr.size())));
    obj.emplace_back("total_value", ValueFromAmount(total));
    obj.emplace_back("utxos",       UniValue(std::move(utxo_arr)));

    SetJSONHeaders(req, *cors);
    req->WriteReply(HTTP_OK, UniValue::stringify(obj));
    return true;
#else
    req->WriteReply(HTTP_SERVICE_UNAVAILABLE, R"({"error":"Wallet support not compiled in"})");
    return false;
#endif
}

// GET /webui/api/wallet/<name>/glyph-metadata?ref=<72hex>
static bool HandleWalletGlyphMetadata(Config &config, HTTPRequest *req,
                                       const std::string &wallet_name)
{
    if (req->GetRequestMethod() != HTTPRequest::GET) { req->WriteReply(HTTP_BAD_METHOD, ""); return false; }
    if (!CheckWebUIHost(req)) return false;
    auto cors = CheckWebUICORS(req);
    if (!cors) return false;
    if (!CheckWebUIAuth(req)) return false;

#ifdef ENABLE_WALLET
    // Extract ?ref= from the query string.
    const std::string uri = req->GetURI();
    std::string refHex;
    const size_t qpos = uri.rfind("?ref=");
    if (qpos != std::string::npos) {
        refHex = UrlDecode(uri.substr(qpos + 5));
        const size_t amp = refHex.find('&');
        if (amp != std::string::npos) refHex = refHex.substr(0, amp);
    }
    if (refHex.length() != 72) {
        req->WriteReply(HTTP_BAD_REQUEST, "{\"error\":\"Missing or invalid ?ref= (need 72-char hex)\"}");
        return false;
    }
    UniValue::Array params;
    params.push_back(UniValue(refHex));              // ref
    params.push_back(UniValue(std::string("")));     // minttxid (empty → auto)
    params.push_back(UniValue(false));               // decode=false → raw metadata hex
    return WalletRPCReply(config, req, wallet_name, "getglyphmetadata",
                          UniValue(std::move(params)), *cors);
#else
    req->WriteReply(HTTP_SERVICE_UNAVAILABLE, R"({"error":"Wallet support not compiled in"})");
    return false;
#endif
}

// GET /webui/api/wallet/<name>/glyph
static bool HandleWalletGlyph(Config &config, HTTPRequest *req,
                               const std::string &wallet_name)
{
    if (req->GetRequestMethod() != HTTPRequest::GET) { req->WriteReply(HTTP_BAD_METHOD, ""); return false; }
    if (!CheckWebUIHost(req)) return false;
    auto cors = CheckWebUICORS(req);
    if (!cors) return false;
    if (!CheckWebUIAuth(req)) return false;

#ifdef ENABLE_WALLET
    // minconf=0 so unconfirmed change UTXOs are visible immediately after a send.
    UniValue::Array params;
    params.push_back(UniValue(static_cast<int64_t>(0)));  // minconf
    return WalletRPCReply(config, req, wallet_name, "listglyph",
                          UniValue(std::move(params)), *cors);
#else
    req->WriteReply(HTTP_SERVICE_UNAVAILABLE, R"({"error":"Wallet support not compiled in"})");
    return false;
#endif
}

// POST /webui/api/wallet/<name>/receive-address
static bool HandleWalletReceiveAddress(Config &config, HTTPRequest *req,
                                        const std::string &wallet_name)
{
    if (req->GetRequestMethod() != HTTPRequest::POST) { req->WriteReply(HTTP_BAD_METHOD, ""); return false; }
    if (!CheckWebUIHost(req)) return false;
    auto cors = CheckWebUICORS(req);
    if (!cors) return false;
    if (!CheckWebUIAuth(req)) return false;

#ifdef ENABLE_WALLET
    UniValue body;
    (void)body.read(req->ReadBody());
    const std::string label = (body.isObject() && body["label"].isStr()) ? body["label"].get_str() : "";
    UniValue::Array params;
    params.push_back(UniValue(label));
    return WalletRPCReply(config, req, wallet_name, "getnewaddress",
                          UniValue(std::move(params)), *cors);
#else
    req->WriteReply(HTTP_SERVICE_UNAVAILABLE, R"({"error":"Wallet support not compiled in"})");
    return false;
#endif
}

// POST /webui/api/wallet/<name>/set-address-label
static bool HandleSetAddressLabel(Config &config, HTTPRequest *req,
                                   const std::string &wallet_name)
{
    if (req->GetRequestMethod() != HTTPRequest::POST) { req->WriteReply(HTTP_BAD_METHOD, ""); return false; }
    if (!CheckWebUIHost(req)) return false;
    auto cors = CheckWebUICORS(req);
    if (!cors) return false;
    if (!CheckWebUIAuth(req)) return false;

#ifdef ENABLE_WALLET
    UniValue body;
    if (!body.read(req->ReadBody()) || !body.isObject()) {
        req->WriteReply(HTTP_BAD_REQUEST, R"({"error":"Invalid request body"})");
        return false;
    }
    if (!body["address"].isStr() || !body["label"].isStr()) {
        req->WriteReply(HTTP_BAD_REQUEST, R"({"error":"\"address\" and \"label\" required"})");
        return false;
    }
    UniValue::Array params;
    params.push_back(body["address"]);
    params.push_back(body["label"]);
    return WalletRPCReply(config, req, wallet_name, "setlabel",
                          UniValue(std::move(params)), *cors);
#else
    req->WriteReply(HTTP_SERVICE_UNAVAILABLE, R"({"error":"Wallet support not compiled in"})");
    return false;
#endif
}

// POST /webui/api/wallet/<name>/send-with-inputs
// Builds, funds, signs, and broadcasts a raw tx using caller-supplied UTXOs.
// Body: { address, amount, inputs: [{txid, vout}, ...], changeAddress? }
// fundrawtransaction keeps the selected inputs and adds a change output + fee.
static bool HandleWalletSendWithInputs(Config &config, HTTPRequest *req,
                                        const std::string &wallet_name)
{
    if (req->GetRequestMethod() != HTTPRequest::POST) { req->WriteReply(HTTP_BAD_METHOD, ""); return false; }
    if (!CheckWebUIHost(req)) return false;
    auto cors = CheckWebUICORS(req);
    if (!cors) return false;
    if (!CheckWebUIAuth(req)) return false;

#ifdef ENABLE_WALLET
    UniValue body;
    if (!body.read(req->ReadBody()) || !body.isObject()) {
        req->WriteReply(HTTP_BAD_REQUEST, R"({"error":"Invalid request body"})");
        return false;
    }
    if (!body["address"].isStr() || !body["amount"].isNum() || !body["inputs"].isArray()) {
        req->WriteReply(HTTP_BAD_REQUEST,
            R"({"error":"\"address\", \"amount\", and \"inputs\" required"})");
        return false;
    }
    if (body["inputs"].size() == 0) {
        req->WriteReply(HTTP_BAD_REQUEST, R"({"error":"\"inputs\" must not be empty"})");
        return false;
    }

    try {
        // 1. Create raw tx: selected inputs → recipient output (no change yet)
        UniValue::Array crParams;
        crParams.push_back(body["inputs"]);
        UniValue::Object output;
        output.emplace_back(body["address"].get_str(), body["amount"]);
        crParams.push_back(UniValue(std::move(output)));
        UniValue rawHex = WalletRPC(config, wallet_name, "createrawtransaction",
                                    UniValue(std::move(crParams)));

        // 2. Fund: wallet adds change output and deducts fee; may add inputs if selected
        //    don't cover fees (rare – UI warns when selected < amount).
        UniValue::Array frParams;
        frParams.push_back(rawHex);
        if (body["changeAddress"].isStr() && !body["changeAddress"].get_str().empty()) {
            UniValue::Object frOpts;
            frOpts.emplace_back("changeAddress", body["changeAddress"]);
            frParams.push_back(UniValue(std::move(frOpts)));
        }
        UniValue funded = WalletRPC(config, wallet_name, "fundrawtransaction",
                                    UniValue(std::move(frParams)));
        const std::string fundedHex = funded["hex"].get_str();

        // 3. Sign with wallet keys
        UniValue::Array signParams;
        signParams.push_back(UniValue(fundedHex));
        UniValue signed_ = WalletRPC(config, wallet_name, "signrawtransactionwithwallet",
                                     UniValue(std::move(signParams)));
        if (!signed_["complete"].getBool()) {
            req->WriteReply(HTTP_BAD_REQUEST,
                R"({"error":"Signing incomplete – wallet may be locked"})");
            return false;
        }
        const std::string signedHex = signed_["hex"].get_str();

        // 4. Broadcast
        UniValue::Array sendParams;
        sendParams.push_back(UniValue(signedHex));
        UniValue txid = WalletRPC(config, wallet_name, "sendrawtransaction",
                                  UniValue(std::move(sendParams)));

        SetJSONHeaders(req, *cors);
        req->WriteReply(HTTP_OK, UniValue::stringify(txid));
        return true;

    } catch (const JSONRPCError &e) {
        req->WriteReply(HTTP_BAD_REQUEST, JsonError(e.message));
        return false;
    } catch (const UniValue &e) {
        const std::string msg = e["message"].isStr() ? e["message"].get_str() : "RPC error";
        req->WriteReply(HTTP_BAD_REQUEST, JsonError(msg));
        return false;
    } catch (const std::exception &e) {
        req->WriteReply(HTTP_INTERNAL_SERVER_ERROR, JsonError(e.what()));
        return false;
    }
#else
    req->WriteReply(HTTP_SERVICE_UNAVAILABLE, R"({"error":"Wallet support not compiled in"})");
    return false;
#endif
}

// POST /webui/api/wallet/<name>/send
// Body: { address, amount, comment?, changeAddress? }
// When changeAddress is provided, routes through createrawtransaction →
// fundrawtransaction (with changeAddress) → sign → broadcast so change
// lands at the specified address rather than a wallet-generated one.
static bool HandleWalletSend(Config &config, HTTPRequest *req,
                              const std::string &wallet_name)
{
    if (req->GetRequestMethod() != HTTPRequest::POST) { req->WriteReply(HTTP_BAD_METHOD, ""); return false; }
    if (!CheckWebUIHost(req)) return false;
    auto cors = CheckWebUICORS(req);
    if (!cors) return false;
    if (!CheckWebUIAuth(req)) return false;

#ifdef ENABLE_WALLET
    UniValue body;
    if (!body.read(req->ReadBody()) || !body.isObject()) {
        req->WriteReply(HTTP_BAD_REQUEST, R"({"error":"Invalid request body"})");
        return false;
    }
    if (!body["address"].isStr() || !body["amount"].isNum()) {
        req->WriteReply(HTTP_BAD_REQUEST, R"({"error":"\"address\" and \"amount\" required"})");
        return false;
    }

    const bool hasChangeAddr = body["changeAddress"].isStr() && !body["changeAddress"].get_str().empty();

    if (!hasChangeAddr) {
        UniValue::Array params;
        params.push_back(body["address"]);
        params.push_back(body["amount"]);
        if (body["comment"].isStr()) params.push_back(body["comment"]);
        return WalletRPCReply(config, req, wallet_name, "sendtoaddress",
                              UniValue(std::move(params)), *cors);
    }

    // Route through raw tx flow to honour the requested change address.
    try {
        // 1. Create raw tx with empty inputs (fundrawtransaction will select them)
        UniValue crInputs(UniValue::VARR);
        UniValue::Object crOutput;
        crOutput.emplace_back(body["address"].get_str(), body["amount"]);
        UniValue::Array crParams;
        crParams.push_back(std::move(crInputs));
        crParams.push_back(UniValue(std::move(crOutput)));
        UniValue rawHex = WalletRPC(config, wallet_name, "createrawtransaction",
                                    UniValue(std::move(crParams)));

        // 2. Fund: wallet selects inputs, adds change output at changeAddress
        UniValue::Object frOpts;
        frOpts.emplace_back("changeAddress", body["changeAddress"]);
        UniValue::Array frParams;
        frParams.push_back(rawHex);
        frParams.push_back(UniValue(std::move(frOpts)));
        UniValue funded = WalletRPC(config, wallet_name, "fundrawtransaction",
                                    UniValue(std::move(frParams)));
        const std::string fundedHex = funded["hex"].get_str();

        // 3. Sign
        UniValue::Array signParams;
        signParams.push_back(UniValue(fundedHex));
        UniValue signed_ = WalletRPC(config, wallet_name, "signrawtransactionwithwallet",
                                     UniValue(std::move(signParams)));
        if (!signed_["complete"].getBool()) {
            req->WriteReply(HTTP_BAD_REQUEST,
                R"({"error":"Signing incomplete – wallet may be locked"})");
            return false;
        }

        // 4. Broadcast
        UniValue::Array sendParams;
        sendParams.push_back(UniValue(signed_["hex"].get_str()));
        UniValue txid = WalletRPC(config, wallet_name, "sendrawtransaction",
                                  UniValue(std::move(sendParams)));

        SetJSONHeaders(req, *cors);
        req->WriteReply(HTTP_OK, UniValue::stringify(txid));
        return true;

    } catch (const JSONRPCError &e) {
        req->WriteReply(HTTP_BAD_REQUEST, JsonError(e.message));
        return false;
    } catch (const UniValue &e) {
        const std::string msg = e["message"].isStr() ? e["message"].get_str() : "RPC error";
        req->WriteReply(HTTP_BAD_REQUEST, JsonError(msg));
        return false;
    } catch (const std::exception &e) {
        req->WriteReply(HTTP_INTERNAL_SERVER_ERROR, JsonError(e.what()));
        return false;
    }
#else
    req->WriteReply(HTTP_SERVICE_UNAVAILABLE, R"({"error":"Wallet support not compiled in"})");
    return false;
#endif
}

// POST /webui/api/wallet/<name>/signmessage
static bool HandleWalletSignMessage(Config &config, HTTPRequest *req,
                                     const std::string &wallet_name)
{
    if (req->GetRequestMethod() != HTTPRequest::POST) { req->WriteReply(HTTP_BAD_METHOD, ""); return false; }
    if (!CheckWebUIHost(req)) return false;
    auto cors = CheckWebUICORS(req);
    if (!cors) return false;
    if (!CheckWebUIAuth(req)) return false;

#ifdef ENABLE_WALLET
    UniValue body;
    if (!body.read(req->ReadBody()) || !body.isObject()) {
        req->WriteReply(HTTP_BAD_REQUEST, R"({"error":"Invalid request body"})");
        return false;
    }
    if (!body["address"].isStr() || !body["message"].isStr()) {
        req->WriteReply(HTTP_BAD_REQUEST, R"({"error":"\"address\" and \"message\" required"})");
        return false;
    }
    UniValue::Array params;
    params.push_back(body["address"]);
    params.push_back(body["message"]);
    return WalletRPCReply(config, req, wallet_name, "signmessage",
                          UniValue(std::move(params)), *cors);
#else
    req->WriteReply(HTTP_SERVICE_UNAVAILABLE, R"({"error":"Wallet support not compiled in"})");
    return false;
#endif
}

// POST /webui/api/wallet/<name>/unlock
static bool HandleWalletUnlock(Config &config, HTTPRequest *req,
                                const std::string &wallet_name)
{
    if (req->GetRequestMethod() != HTTPRequest::POST) { req->WriteReply(HTTP_BAD_METHOD, ""); return false; }
    if (!CheckWebUIHost(req)) return false;
    auto cors = CheckWebUICORS(req);
    if (!cors) return false;
    if (!CheckWebUIAuth(req)) return false;

#ifdef ENABLE_WALLET
    UniValue body;
    if (!body.read(req->ReadBody()) || !body.isObject()) {
        req->WriteReply(HTTP_BAD_REQUEST, R"({"error":"Invalid request body"})");
        return false;
    }
    if (!body["passphrase"].isStr()) {
        req->WriteReply(HTTP_BAD_REQUEST, R"({"error":"\"passphrase\" field required"})");
        return false;
    }
    const int64_t timeout = body["timeout"].isNum() ? body["timeout"].get_int64() : 300;
    UniValue::Array params;
    params.push_back(body["passphrase"]);
    params.push_back(UniValue(timeout));
    return WalletRPCReply(config, req, wallet_name, "walletpassphrase",
                          UniValue(std::move(params)), *cors);
#else
    req->WriteReply(HTTP_SERVICE_UNAVAILABLE, R"({"error":"Wallet support not compiled in"})");
    return false;
#endif
}

// POST /webui/api/wallet/<name>/lock
static bool HandleWalletLock(Config &config, HTTPRequest *req,
                              const std::string &wallet_name)
{
    if (req->GetRequestMethod() != HTTPRequest::POST) { req->WriteReply(HTTP_BAD_METHOD, ""); return false; }
    if (!CheckWebUIHost(req)) return false;
    auto cors = CheckWebUICORS(req);
    if (!cors) return false;
    if (!CheckWebUIAuth(req)) return false;

#ifdef ENABLE_WALLET
    return WalletRPCReply(config, req, wallet_name, "walletlock",
                          UniValue(UniValue::VARR), *cors);
#else
    req->WriteReply(HTTP_SERVICE_UNAVAILABLE, R"({"error":"Wallet support not compiled in"})");
    return false;
#endif
}

// POST /webui/api/wallet/<name>/encrypt
static bool HandleWalletEncrypt(Config &config, HTTPRequest *req,
                                 const std::string &wallet_name)
{
    if (req->GetRequestMethod() != HTTPRequest::POST) { req->WriteReply(HTTP_BAD_METHOD, ""); return false; }
    if (!CheckWebUIHost(req)) return false;
    auto cors = CheckWebUICORS(req);
    if (!cors) return false;
    if (!CheckWebUIAuth(req)) return false;

#ifdef ENABLE_WALLET
    UniValue body;
    if (!body.read(req->ReadBody()) || !body.isObject()) {
        req->WriteReply(HTTP_BAD_REQUEST, R"({"error":"Invalid request body"})");
        return false;
    }
    if (!body["passphrase"].isStr()) {
        req->WriteReply(HTTP_BAD_REQUEST, R"({"error":"\"passphrase\" field required"})");
        return false;
    }
    UniValue::Array params;
    params.push_back(body["passphrase"]);
    return WalletRPCReply(config, req, wallet_name, "encryptwallet",
                          UniValue(std::move(params)), *cors);
#else
    req->WriteReply(HTTP_SERVICE_UNAVAILABLE, R"({"error":"Wallet support not compiled in"})");
    return false;
#endif
}

// POST /webui/api/wallet/<name>/change-passphrase
static bool HandleWalletChangePassphrase(Config &config, HTTPRequest *req,
                                          const std::string &wallet_name)
{
    if (req->GetRequestMethod() != HTTPRequest::POST) { req->WriteReply(HTTP_BAD_METHOD, ""); return false; }
    if (!CheckWebUIHost(req)) return false;
    auto cors = CheckWebUICORS(req);
    if (!cors) return false;
    if (!CheckWebUIAuth(req)) return false;

#ifdef ENABLE_WALLET
    UniValue body;
    if (!body.read(req->ReadBody()) || !body.isObject()) {
        req->WriteReply(HTTP_BAD_REQUEST, R"({"error":"Invalid request body"})");
        return false;
    }
    if (!body["oldPassphrase"].isStr() || !body["newPassphrase"].isStr()) {
        req->WriteReply(HTTP_BAD_REQUEST, R"({"error":"\"oldPassphrase\" and \"newPassphrase\" required"})");
        return false;
    }
    UniValue::Array params;
    params.push_back(body["oldPassphrase"]);
    params.push_back(body["newPassphrase"]);
    return WalletRPCReply(config, req, wallet_name, "walletpassphrasechange",
                          UniValue(std::move(params)), *cors);
#else
    req->WriteReply(HTTP_SERVICE_UNAVAILABLE, R"({"error":"Wallet support not compiled in"})");
    return false;
#endif
}

// POST /webui/api/wallet/<name>/psbt/create
static bool HandleWalletPSBTCreate(Config &config, HTTPRequest *req,
                                    const std::string &wallet_name)
{
    if (req->GetRequestMethod() != HTTPRequest::POST) { req->WriteReply(HTTP_BAD_METHOD, ""); return false; }
    if (!CheckWebUIHost(req)) return false;
    auto cors = CheckWebUICORS(req);
    if (!cors) return false;
    if (!CheckWebUIAuth(req)) return false;

#ifdef ENABLE_WALLET
    UniValue body;
    if (!body.read(req->ReadBody()) || !body.isObject()) {
        req->WriteReply(HTTP_BAD_REQUEST, R"({"error":"Invalid request body"})");
        return false;
    }
    UniValue::Array params;
    params.push_back(body["inputs"].isArray()  ? body["inputs"]  : UniValue(UniValue::VARR));
    params.push_back(body["outputs"].isArray() ? body["outputs"] : UniValue(UniValue::VARR));
    params.push_back(UniValue(0)); // locktime
    // Build options: accept either a full "options" object or a "changeAddress" string
    if (body["options"].isObject()) {
        params.push_back(body["options"]);
    } else if (body["changeAddress"].isStr() && !body["changeAddress"].get_str().empty()) {
        UniValue::Object opts;
        opts.emplace_back("changeAddress", UniValue(body["changeAddress"].get_str()));
        params.push_back(UniValue(std::move(opts)));
    } else {
        params.push_back(UniValue(UniValue::VOBJ));
    }
    return WalletRPCReply(config, req, wallet_name, "walletcreatefundedpsbt",
                          UniValue(std::move(params)), *cors);
#else
    req->WriteReply(HTTP_SERVICE_UNAVAILABLE, R"({"error":"Wallet support not compiled in"})");
    return false;
#endif
}

// POST /webui/api/wallet/<name>/psbt/sign
static bool HandleWalletPSBTSign(Config &config, HTTPRequest *req,
                                  const std::string &wallet_name)
{
    if (req->GetRequestMethod() != HTTPRequest::POST) { req->WriteReply(HTTP_BAD_METHOD, ""); return false; }
    if (!CheckWebUIHost(req)) return false;
    auto cors = CheckWebUICORS(req);
    if (!cors) return false;
    if (!CheckWebUIAuth(req)) return false;

#ifdef ENABLE_WALLET
    UniValue body;
    if (!body.read(req->ReadBody()) || !body.isObject() || !body["psbt"].isStr()) {
        req->WriteReply(HTTP_BAD_REQUEST, R"({"error":"\"psbt\" field required"})");
        return false;
    }
    UniValue::Array params;
    params.push_back(body["psbt"]);
    return WalletRPCReply(config, req, wallet_name, "walletprocesspsbt",
                          UniValue(std::move(params)), *cors);
#else
    req->WriteReply(HTTP_SERVICE_UNAVAILABLE, R"({"error":"Wallet support not compiled in"})");
    return false;
#endif
}

// POST /webui/api/wallet/<name>/backup-export
// Calls backupwallet to a temp file, reads it, returns the bytes as
// application/octet-stream so the browser can Save-As wherever it likes.
static bool HandleWalletBackupExport(Config &config, HTTPRequest *req,
                                     const std::string &wallet_name)
{
    if (req->GetRequestMethod() != HTTPRequest::POST) { req->WriteReply(HTTP_BAD_METHOD, ""); return false; }
    if (!CheckWebUIHost(req)) return false;
    auto cors = CheckWebUICORS(req);
    if (!cors) return false;
    if (!CheckWebUIAuth(req)) return false;

#ifdef ENABLE_WALLET
    // Build a unique temp path that won't collide with concurrent requests.
    const std::string tmpName = strprintf("radiant-webui-backup-%d.dat",
                                          static_cast<int64_t>(GetTimeMicros()));
    const fs::path tmpPath = fs::temp_directory_path() / tmpName;
    const std::string tmpStr = tmpPath.string();

    // Ask Core to write the wallet backup to the temp file.
    try {
        UniValue::Array params;
        params.push_back(UniValue(tmpStr));
        WalletRPC(config, wallet_name, "backupwallet", UniValue(std::move(params)));
    } catch (const JSONRPCError &e) {
        req->WriteReply(HTTP_BAD_REQUEST, JsonError(e.message));
        return false;
    } catch (const UniValue &e) {
        const std::string msg = e["message"].isStr() ? e["message"].get_str() : "backup error";
        req->WriteReply(HTTP_BAD_REQUEST, JsonError(msg));
        return false;
    } catch (const std::exception &e) {
        req->WriteReply(HTTP_INTERNAL_SERVER_ERROR, JsonError(e.what()));
        return false;
    }

    // Read the temp file into memory, then delete it.
    std::string bytes;
    {
        std::ifstream f(tmpPath, std::ios::binary);
        if (!f) {
            fs::remove(tmpPath);
            req->WriteReply(HTTP_INTERNAL_SERVER_ERROR,
                            JsonError("backup succeeded but temp file could not be read"));
            return false;
        }
        bytes.assign(std::istreambuf_iterator<char>(f),
                     std::istreambuf_iterator<char>());
    }
    fs::remove(tmpPath);

    // Suggest a dated filename for the Save-As dialog.
    const std::string safeWallet = wallet_name.empty() ? "wallet" : wallet_name;
    const std::string disposition = "attachment; filename=\"" + safeWallet + "-backup.dat\"";

    req->WriteHeader("Content-Type",        "application/octet-stream");
    req->WriteHeader("Content-Disposition", disposition);
    req->WriteHeader("Access-Control-Allow-Origin", *cors);
    req->WriteHeader("Cache-Control",       "no-store");
    req->WriteReply(HTTP_OK, bytes);
    return true;
#else
    req->WriteReply(HTTP_SERVICE_UNAVAILABLE, R"({"error":"Wallet support not compiled in"})");
    return false;
#endif
}

// ---- Router --------------------------------------------------------------

// Parses "/webui/api/wallets/<action>" → action
bool WebUIWalletsRoute(Config &config, HTTPRequest *req, const std::string &path)
{
    static const std::string prefix = "/webui/api/wallets/";
    if (path.size() <= prefix.size()) {
        req->WriteReply(HTTP_NOT_FOUND, JsonError("not found"));
        return false;
    }
    const std::string action = path.substr(prefix.size());

    if (action == "loaded")    return HandleWalletsLoaded(req);
    if (action == "available") return HandleWalletsAvailable(config, req);
    if (action == "load")      return HandleWalletLoad(config, req);
    if (action == "create")    return HandleWalletCreate(config, req);
    if (action == "unload")    return HandleWalletUnload(config, req);

    req->WriteReply(HTTP_NOT_FOUND, JsonError("unknown wallet management action: " + action));
    return false;
}

// Parses "/webui/api/wallet/<name>/<action>" → (wallet_name, action)
bool WebUIWalletRoute(Config &config, HTTPRequest *req, const std::string &path)
{
    static const std::string prefix = "/webui/api/wallet/";
    if (path.size() <= prefix.size()) {
        req->WriteReply(HTTP_NOT_FOUND, JsonError("not found"));
        return false;
    }
    const std::string rest = path.substr(prefix.size()); // "<name>/<action>"
    const auto slash = rest.find('/');
    if (slash == std::string::npos) {
        req->WriteReply(HTTP_NOT_FOUND, JsonError("missing action"));
        return false;
    }
    const std::string wallet_name = UrlDecode(rest.substr(0, slash));
    const std::string action      = rest.substr(slash + 1);

    if (action == "summary")           return HandleWalletSummary(config, req, wallet_name);
    if (action == "transactions")      return HandleWalletTransactions(config, req, wallet_name);
    if (action == "addresses")         return HandleWalletAddresses(config, req, wallet_name);
    if (action == "utxos")             return HandleWalletUTXOs(config, req, wallet_name);
    if (action == "coins")             return HandleWalletCoins(config, req, wallet_name);
    if (action == "glyph")             return HandleWalletGlyph(config, req, wallet_name);
    if (action == "glyph-metadata")    return HandleWalletGlyphMetadata(config, req, wallet_name);
    if (action == "receive-address")   return HandleWalletReceiveAddress(config, req, wallet_name);
    if (action == "set-address-label") return HandleSetAddressLabel(config, req, wallet_name);
    if (action == "send")              return HandleWalletSend(config, req, wallet_name);
    if (action == "send-with-inputs")  return HandleWalletSendWithInputs(config, req, wallet_name);
    if (action == "signmessage")       return HandleWalletSignMessage(config, req, wallet_name);
    if (action == "unlock")            return HandleWalletUnlock(config, req, wallet_name);
    if (action == "lock")              return HandleWalletLock(config, req, wallet_name);
    if (action == "encrypt")           return HandleWalletEncrypt(config, req, wallet_name);
    if (action == "change-passphrase") return HandleWalletChangePassphrase(config, req, wallet_name);
    if (action == "psbt/create")       return HandleWalletPSBTCreate(config, req, wallet_name);
    if (action == "psbt/sign")         return HandleWalletPSBTSign(config, req, wallet_name);
    if (action == "backup-export")     return HandleWalletBackupExport(config, req, wallet_name);

    req->WriteReply(HTTP_NOT_FOUND, JsonError("unknown wallet action: " + action));
    return false;
}
