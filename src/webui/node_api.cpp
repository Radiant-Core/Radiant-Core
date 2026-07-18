// Copyright (c) 2025-present The Avian Core developers
// Copyright (c) 2025-present The Radiant Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://opensource.org/license/mit/.

#include <webui/webui_internal.h>

#include <addrdb.h>
#include <banman.h>
#include <chainparams.h>
#include <clientversion.h>
#include <config.h>
#include <core_io.h>
#include <fs.h>
#include <httpserver.h>
#include <key_io.h>
#include <logging.h>
#include <net.h>
#include <netbase.h>
#include <psbt.h>
#include <rpc/jsonrpcrequest.h>
#include <rpc/server.h>
#include <streams.h>
#include <txmempool.h>
#include <univalue.h>
#include <uint256.h>
#include <util/moneystr.h>
#include <util/strencodings.h>
#include <util/system.h>
#include <util/time.h>
#include <validation.h>

#include <event2/http.h>

#include <algorithm>
#include <fstream>
#include <limits>
#include <map>
#include <optional>
#include <string>
#include <vector>

// ---- Node status --------------------------------------------------------

static bool HandleNodeStatus(HTTPRequest *req)
{
    if (req->GetRequestMethod() != HTTPRequest::GET) {
        req->WriteReply(HTTP_BAD_METHOD, "");
        return false;
    }
    if (!CheckWebUIHost(req)) return false;
    auto cors = CheckWebUICORS(req);
    if (!cors) return false;
    if (!CheckWebUIAuth(req)) return false;

    UniValue::Object obj;
    obj.emplace_back("version", UniValue(FormatFullVersion()));

    {
        LOCK(cs_main);
        const CBlockIndex *tip = ::ChainActive().Tip();
        obj.emplace_back("network",             UniValue(Params().NetworkIDString()));
        obj.emplace_back("blocks",              UniValue(static_cast<int64_t>(::ChainActive().Height())));
        obj.emplace_back("headers",             UniValue(static_cast<int64_t>(pindexBestHeader ? pindexBestHeader->nHeight : -1)));
        if (tip) {
            obj.emplace_back("bestblockhash",        UniValue(tip->GetBlockHash().GetHex()));
            obj.emplace_back("verificationprogress", UniValue(GuessVerificationProgress(Params().TxData(), tip)));
        } else {
            obj.emplace_back("bestblockhash",        UniValue());
            obj.emplace_back("verificationprogress", UniValue(0.0));
        }
        obj.emplace_back("initialblockdownload", UniValue(IsInitialBlockDownload()));
    }

    obj.emplace_back("connections",
        UniValue(static_cast<int64_t>(g_connman ? g_connman->GetNodeCount(CConnman::CONNECTIONS_ALL) : 0)));
    {
        LOCK(g_mempool.cs);
        obj.emplace_back("mempoolsize", UniValue(static_cast<int64_t>(g_mempool.size())));
    }
    obj.emplace_back("uptime", UniValue(static_cast<int64_t>(GetTime() - GetStartupTime())));

    SetJSONHeaders(req, *cors);
    req->WriteReply(HTTP_OK, UniValue::stringify(obj));
    return true;
}

// ---- Node features ------------------------------------------------------

static bool HandleNodeFeatures(HTTPRequest *req)
{
    if (req->GetRequestMethod() != HTTPRequest::GET) {
        req->WriteReply(HTTP_BAD_METHOD, "");
        return false;
    }
    if (!CheckWebUIHost(req)) return false;
    auto cors = CheckWebUICORS(req);
    if (!cors) return false;
    if (!CheckWebUIAuth(req)) return false;

    UniValue::Object obj;
    obj.emplace_back("network", UniValue(Params().NetworkIDString()));

    UniValue::Object features;
    features.emplace_back("psbt",      UniValue(true));
    features.emplace_back("dsProofs",  UniValue(true));
    features.emplace_back("gbtLight",  UniValue(true));
    features.emplace_back("swapIndex", UniValue(gArgs.GetBoolArg("-txindex", false)));
    obj.emplace_back("features", UniValue(std::move(features)));

    SetJSONHeaders(req, *cors);
    req->WriteReply(HTTP_OK, UniValue::stringify(obj));
    return true;
}

// ---- Peers --------------------------------------------------------------

static bool HandleNodePeers(HTTPRequest *req)
{
    if (req->GetRequestMethod() != HTTPRequest::GET) {
        req->WriteReply(HTTP_BAD_METHOD, "");
        return false;
    }
    if (!CheckWebUIHost(req)) return false;
    auto cors = CheckWebUICORS(req);
    if (!cors) return false;
    if (!CheckWebUIAuth(req)) return false;

    if (!g_connman) {
        req->WriteReply(HTTP_SERVICE_UNAVAILABLE, R"({"error":"P2P not available"})");
        return false;
    }

    std::vector<CNodeStats> vstats;
    g_connman->GetNodeStats(vstats);

    UniValue::Array arr;
    for (const CNodeStats &stats : vstats) {
        UniValue::Object peer;
        double pingMs = stats.dMinPing > 0.0 && stats.dMinPing < 1e9
                        ? stats.dMinPing * 1000.0
                        : (stats.dPingTime > 0.0 ? stats.dPingTime * 1000.0 : -1.0);
        peer.emplace_back("id",             UniValue(static_cast<int64_t>(stats.nodeid)));
        peer.emplace_back("addr",           UniValue(stats.addrName));
        peer.emplace_back("subver",         UniValue(stats.cleanSubVer));
        peer.emplace_back("inbound",        UniValue(stats.fInbound));
        peer.emplace_back("bytesrecv",      UniValue(static_cast<int64_t>(stats.nRecvBytes)));
        peer.emplace_back("bytessent",      UniValue(static_cast<int64_t>(stats.nSendBytes)));
        peer.emplace_back("conntime",       UniValue(static_cast<int64_t>(stats.nTimeConnected)));
        peer.emplace_back("lastsend",       UniValue(static_cast<int64_t>(stats.nLastSend)));
        peer.emplace_back("lastrecv",       UniValue(static_cast<int64_t>(stats.nLastRecv)));
        peer.emplace_back("ping",           UniValue(pingMs));
        peer.emplace_back("startingheight", UniValue(static_cast<int64_t>(stats.nStartingHeight)));
        peer.emplace_back("version",        UniValue(static_cast<int64_t>(stats.nVersion)));
        peer.emplace_back("addrlocal",      UniValue(stats.addrLocal));
        arr.push_back(UniValue(std::move(peer)));
    }

    UniValue::Object obj;
    obj.emplace_back("peers", UniValue(std::move(arr)));
    SetJSONHeaders(req, *cors);
    req->WriteReply(HTTP_OK, UniValue::stringify(obj));
    return true;
}

// ---- Banned nodes -------------------------------------------------------

static bool HandleNodeBanned(HTTPRequest *req)
{
    if (req->GetRequestMethod() != HTTPRequest::GET) {
        req->WriteReply(HTTP_BAD_METHOD, "");
        return false;
    }
    if (!CheckWebUIHost(req)) return false;
    auto cors = CheckWebUICORS(req);
    if (!cors) return false;
    if (!CheckWebUIAuth(req)) return false;

    if (!g_banman) {
        req->WriteReply(HTTP_SERVICE_UNAVAILABLE, R"({"error":"BanMan not available"})");
        return false;
    }

    BanTables banTables;
    g_banman->GetBanned(banTables);
    const auto banMap = banTables.toAggregatedMap();

    UniValue::Array arr;
    for (const auto &[subnet, banEntry] : banMap) {
        UniValue::Object entry;
        entry.emplace_back("address",      UniValue(subnet.ToString()));
        entry.emplace_back("banned_until", UniValue(banEntry.nBanUntil));
        entry.emplace_back("ban_created",  UniValue(banEntry.nCreateTime));
        arr.push_back(UniValue(std::move(entry)));
    }

    UniValue::Object obj;
    obj.emplace_back("banned", UniValue(std::move(arr)));
    SetJSONHeaders(req, *cors);
    req->WriteReply(HTTP_OK, UniValue::stringify(obj));
    return true;
}

// ---- Peer management (ban / unban / add / disconnect) -------------------

static bool HandlePeerAction(Config &config, HTTPRequest *req, const std::string &action)
{
    if (req->GetRequestMethod() != HTTPRequest::POST) {
        req->WriteReply(HTTP_BAD_METHOD, "");
        return false;
    }
    if (!CheckWebUIHost(req)) return false;
    auto cors = CheckWebUICORS(req);
    if (!cors) return false;
    if (!CheckWebUIAuth(req)) return false;

    UniValue body;
    if (!body.read(req->ReadBody()) || !body.isObject()) {
        req->WriteReply(HTTP_BAD_REQUEST, R"({"error":"Invalid request body"})");
        return false;
    }

    if (action == "ban" || action == "unban") {
        if (!g_banman) {
            req->WriteReply(HTTP_SERVICE_UNAVAILABLE, R"({"error":"BanMan not available"})");
            return false;
        }
        const UniValue &addrVal = body["address"];
        if (!addrVal.isStr()) {
            req->WriteReply(HTTP_BAD_REQUEST, R"({"error":"\"address\" field required"})");
            return false;
        }
        CSubNet subnet;
        if (!LookupSubNet(addrVal.get_str(), subnet) || !subnet.IsValid()) {
            req->WriteReply(HTTP_BAD_REQUEST, R"({"error":"Invalid address/subnet"})");
            return false;
        }
        if (action == "ban") {
            const int64_t bantime = body["bantime"].isNum() ? body["bantime"].get_int64() : 86400;
            g_banman->Ban(subnet, bantime);
        } else {
            g_banman->Unban(subnet);
        }
    } else if (action == "add") {
        if (!g_connman) {
            req->WriteReply(HTTP_SERVICE_UNAVAILABLE, R"({"error":"P2P not available"})");
            return false;
        }
        const UniValue &nodeVal = body["node"];
        if (!nodeVal.isStr()) {
            req->WriteReply(HTTP_BAD_REQUEST, R"({"error":"\"node\" field required"})");
            return false;
        }
        if (!g_connman->AddNode(nodeVal.get_str())) {
            req->WriteReply(HTTP_BAD_REQUEST, R"({"error":"Node already added or invalid address"})");
            return false;
        }
    } else if (action == "disconnect") {
        if (!g_connman) {
            req->WriteReply(HTTP_SERVICE_UNAVAILABLE, R"({"error":"P2P not available"})");
            return false;
        }
        const UniValue &idVal = body["id"];
        if (!idVal.isNum()) {
            req->WriteReply(HTTP_BAD_REQUEST, R"({"error":"\"id\" field required"})");
            return false;
        }
        const NodeId nodeid = static_cast<NodeId>(idVal.get_int64());
        if (!g_connman->DisconnectNode(nodeid)) {
            req->WriteReply(HTTP_NOT_FOUND, R"({"error":"Node not found"})");
            return false;
        }
    }

    UniValue::Object obj;
    obj.emplace_back("success", UniValue(true));
    SetJSONHeaders(req, *cors);
    req->WriteReply(HTTP_OK, UniValue::stringify(obj));
    return true;
}

// ---- Verify message -----------------------------------------------------

static bool HandleVerifyMessage(Config &config, HTTPRequest *req)
{
    if (req->GetRequestMethod() != HTTPRequest::POST) {
        req->WriteReply(HTTP_BAD_METHOD, "");
        return false;
    }
    if (!CheckWebUIHost(req)) return false;
    auto cors = CheckWebUICORS(req);
    if (!cors) return false;
    if (!CheckWebUIAuth(req)) return false;

    UniValue body;
    if (!body.read(req->ReadBody()) || !body.isObject()) {
        req->WriteReply(HTTP_BAD_REQUEST, R"({"error":"Invalid request body"})");
        return false;
    }

    try {
        JSONRPCRequest jreq;
        UniValue::Array params;
        params.push_back(body["address"].isStr()   ? body["address"]   : UniValue(""));
        params.push_back(body["signature"].isStr()  ? body["signature"] : UniValue(""));
        params.push_back(body["message"].isStr()    ? body["message"]   : UniValue(""));
        jreq.params = UniValue(std::move(params));
        jreq.strMethod = "verifymessage";

        UniValue result = tableRPC.execute(config, jreq);
        UniValue::Object obj;
        obj.emplace_back("valid", result);
        SetJSONHeaders(req, *cors);
        req->WriteReply(HTTP_OK, UniValue::stringify(obj));
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
    return true;
}

// ---- Generic RPC executor -----------------------------------------------

static bool HandleRPC(Config &config, HTTPRequest *req)
{
    if (req->GetRequestMethod() != HTTPRequest::POST) {
        req->WriteReply(HTTP_BAD_METHOD, "");
        return false;
    }
    if (!CheckWebUIHost(req)) return false;
    auto cors = CheckWebUICORS(req);
    if (!cors) return false;
    if (!CheckWebUIAuth(req)) return false;

    UniValue body;
    if (!body.read(req->ReadBody()) || !body.isObject()) {
        req->WriteReply(HTTP_BAD_REQUEST, R"({"error":"Invalid JSON body"})");
        return false;
    }

    const UniValue &methodVal = body["method"];
    const UniValue &paramsVal = body["params"];
    if (!methodVal.isStr()) {
        req->WriteReply(HTTP_BAD_REQUEST, R"({"error":"\"method\" field required"})");
        return false;
    }

    try {
        JSONRPCRequest jreq;
        jreq.strMethod = methodVal.get_str();
        jreq.params    = paramsVal.isArray() ? paramsVal : UniValue(UniValue::VARR);

        // If the caller supplies a wallet name, set the URI so wallet-scoped
        // RPCs (signrawtransactionwithwallet, gettransaction, etc.) find the
        // right wallet even when multiple wallets are loaded.
        const UniValue &walletVal = body["wallet"];
        if (walletVal.isStr() && !walletVal.get_str().empty()) {
            jreq.URI = "/wallet/" + walletVal.get_str();
        }

        UniValue result = tableRPC.execute(config, jreq);
        UniValue::Object reply;
        reply.emplace_back("result", result);
        reply.emplace_back("error",  UniValue());
        SetJSONHeaders(req, *cors);
        req->WriteReply(HTTP_OK, UniValue::stringify(reply));
    } catch (const JSONRPCError &e) {
        UniValue::Object reply;
        reply.emplace_back("result", UniValue());
        UniValue::Object err;
        err.emplace_back("code",    UniValue(static_cast<int64_t>(e.code)));
        err.emplace_back("message", UniValue(e.message));
        reply.emplace_back("error", UniValue(std::move(err)));
        SetJSONHeaders(req, *cors);
        req->WriteReply(HTTP_OK, UniValue::stringify(reply));
    } catch (const UniValue &e) {
        UniValue::Object reply;
        reply.emplace_back("result", UniValue());
        reply.emplace_back("error",  e);
        SetJSONHeaders(req, *cors);
        req->WriteReply(HTTP_OK, UniValue::stringify(reply));
    } catch (const std::exception &e) {
        req->WriteReply(HTTP_INTERNAL_SERVER_ERROR, JsonError(e.what()));
        return false;
    }
    return true;
}

// ---- PSBT helpers -------------------------------------------------------

static bool HandlePSBTDecode(Config &config, HTTPRequest *req)
{
    if (req->GetRequestMethod() != HTTPRequest::POST) {
        req->WriteReply(HTTP_BAD_METHOD, "");
        return false;
    }
    if (!CheckWebUIHost(req)) return false;
    auto cors = CheckWebUICORS(req);
    if (!cors) return false;
    if (!CheckWebUIAuth(req)) return false;

    UniValue body;
    if (!body.read(req->ReadBody()) || !body.isObject()) {
        req->WriteReply(HTTP_BAD_REQUEST, R"({"error":"Invalid request body"})");
        return false;
    }
    const UniValue &psbtVal = body["psbt"];
    if (!psbtVal.isStr()) {
        req->WriteReply(HTTP_BAD_REQUEST, R"({"error":"\"psbt\" field required"})");
        return false;
    }

    try {
        JSONRPCRequest jreq;
        jreq.strMethod = "decodepsbt";
        UniValue::Array params;
        params.push_back(psbtVal);
        jreq.params = UniValue(std::move(params));

        UniValue result = tableRPC.execute(config, jreq);
        SetJSONHeaders(req, *cors);
        req->WriteReply(HTTP_OK, UniValue::stringify(result));
    } catch (const JSONRPCError &e) {
        req->WriteReply(HTTP_BAD_REQUEST, JsonError(e.message));
        return false;
    } catch (const UniValue &e) {
        const std::string msg = e["message"].isStr() ? e["message"].get_str() : "PSBT decode error";
        req->WriteReply(HTTP_BAD_REQUEST, JsonError(msg));
        return false;
    }
    return true;
}

static bool HandlePSBTBroadcast(Config &config, HTTPRequest *req)
{
    if (req->GetRequestMethod() != HTTPRequest::POST) {
        req->WriteReply(HTTP_BAD_METHOD, "");
        return false;
    }
    if (!CheckWebUIHost(req)) return false;
    auto cors = CheckWebUICORS(req);
    if (!cors) return false;
    if (!CheckWebUIAuth(req)) return false;

    UniValue body;
    if (!body.read(req->ReadBody()) || !body.isObject()) {
        req->WriteReply(HTTP_BAD_REQUEST, R"({"error":"Invalid request body"})");
        return false;
    }
    const UniValue &psbtVal = body["psbt"];
    if (!psbtVal.isStr()) {
        req->WriteReply(HTTP_BAD_REQUEST, R"({"error":"\"psbt\" field required"})");
        return false;
    }

    try {
        // finalizepsbt then sendrawtransaction
        JSONRPCRequest jreq;
        jreq.strMethod = "finalizepsbt";
        {
            UniValue::Array params;
            params.push_back(psbtVal);
            jreq.params = UniValue(std::move(params));
        }
        UniValue finalized = tableRPC.execute(config, jreq);

        if (!finalized["complete"].isBool() || !finalized["complete"].getBool()) {
            req->WriteReply(HTTP_BAD_REQUEST, R"({"error":"PSBT is not complete"})");
            return false;
        }

        JSONRPCRequest sendreq;
        sendreq.strMethod = "sendrawtransaction";
        {
            UniValue::Array params;
            params.push_back(finalized["hex"]);
            sendreq.params = UniValue(std::move(params));
        }
        UniValue txid = tableRPC.execute(config, sendreq);

        UniValue::Object obj;
        obj.emplace_back("txid", txid);
        SetJSONHeaders(req, *cors);
        req->WriteReply(HTTP_OK, UniValue::stringify(obj));
    } catch (const JSONRPCError &e) {
        req->WriteReply(HTTP_BAD_REQUEST, JsonError(e.message));
        return false;
    } catch (const UniValue &e) {
        const std::string msg = e["message"].isStr() ? e["message"].get_str() : "Broadcast error";
        req->WriteReply(HTTP_BAD_REQUEST, JsonError(msg));
        return false;
    }
    return true;
}

// ---- Mining info --------------------------------------------------------

static bool HandleNodeMining(Config &config, HTTPRequest *req)
{
    if (req->GetRequestMethod() != HTTPRequest::GET) {
        req->WriteReply(HTTP_BAD_METHOD, "");
        return false;
    }
    if (!CheckWebUIHost(req)) return false;
    auto cors = CheckWebUICORS(req);
    if (!cors) return false;
    if (!CheckWebUIAuth(req)) return false;

    try {
        JSONRPCRequest jreq;
        jreq.strMethod = "getmininginfo";
        jreq.params    = UniValue(UniValue::VARR);
        UniValue result = tableRPC.execute(config, jreq);
        SetJSONHeaders(req, *cors);
        req->WriteReply(HTTP_OK, UniValue::stringify(result));
    } catch (const JSONRPCError &e) {
        req->WriteReply(HTTP_INTERNAL_SERVER_ERROR, JsonError(e.message));
        return false;
    } catch (const UniValue &e) {
        const std::string msg = e["message"].isStr() ? e["message"].get_str() : "getmininginfo error";
        req->WriteReply(HTTP_INTERNAL_SERVER_ERROR, JsonError(msg));
        return false;
    }
    return true;
}

// ---- Debug log tail -----------------------------------------------------

// Reads the tail of debug.log for the WebUI log viewer.
//   ?tail=N   initial load — return the last N lines (default 500).
//   ?from=B   follow — return bytes from offset B to EOF.
// The response's "next" field is the current end-of-file offset; the client
// passes it back as ?from on the next poll to fetch only what was appended.
// A single response is capped at MAX_LOG_CHUNK bytes so a long-idle client
// (or a client with a stale cursor after log rotation) never pulls the whole
// file at once.
static bool HandleNodeLogs(HTTPRequest *req)
{
    if (req->GetRequestMethod() != HTTPRequest::GET) {
        req->WriteReply(HTTP_BAD_METHOD, "");
        return false;
    }
    if (!CheckWebUIHost(req)) return false;
    auto cors = CheckWebUICORS(req);
    if (!cors) return false;
    if (!CheckWebUIAuth(req)) return false;

    constexpr int64_t MAX_LOG_CHUNK = 256 * 1024;

    const fs::path log_path = LogInstance().m_file_path;

    UniValue::Object obj;

    // Logging to file disabled (-nodebuglogfile) or file not created yet.
    std::error_code ec;
    if (log_path.empty() || !fs::exists(log_path, ec)) {
        obj.emplace_back("enabled", UniValue(false));
        obj.emplace_back("path",    UniValue(log_path.empty() ? "" : log_path.string()));
        obj.emplace_back("data",    UniValue(""));
        obj.emplace_back("next",    UniValue(static_cast<int64_t>(0)));
        obj.emplace_back("size",    UniValue(static_cast<int64_t>(0)));
        SetJSONHeaders(req, *cors);
        req->WriteReply(HTTP_OK, UniValue::stringify(obj));
        return true;
    }

    fs::ifstream file(log_path, std::ios::binary);
    if (!file.is_open()) {
        req->WriteReply(HTTP_INTERNAL_SERVER_ERROR, JsonError("unable to open log file"));
        return false;
    }

    file.seekg(0, std::ios::end);
    const int64_t size = static_cast<int64_t>(file.tellg());

    const auto fromParam = req->GetQueryParameter("from");
    const bool follow    = fromParam.has_value();

    int64_t start;
    if (follow) {
        int64_t from = 0;
        try { from = std::stoll(*fromParam); } catch (...) { from = 0; }
        // A cursor past EOF means the file was rotated/shrunk (ShrinkDebugFile);
        // fall back to reading the tail rather than nothing.
        start = (from < 0 || from > size) ? std::max<int64_t>(0, size - MAX_LOG_CHUNK)
                                          : from;
    } else {
        // Initial load: grab the last chunk, trimmed to `tail` lines below.
        start = std::max<int64_t>(0, size - MAX_LOG_CHUNK);
    }

    // Hard cap on bytes returned in one response.
    if (size - start > MAX_LOG_CHUNK) start = size - MAX_LOG_CHUNK;

    std::string data;
    if (size > start) {
        file.seekg(start, std::ios::beg);
        data.resize(static_cast<size_t>(size - start));
        file.read(data.data(), static_cast<std::streamsize>(data.size()));
        data.resize(static_cast<size_t>(file.gcount()));
    }

    // Initial load only: our start offset is arbitrary (size - MAX_LOG_CHUNK),
    // so drop the leading partial line to begin on a clean boundary. In follow
    // mode `start` is a precise cursor and the client reassembles split lines,
    // so trimming here would drop data.
    if (!follow && start > 0) {
        const size_t nl = data.find('\n');
        if (nl != std::string::npos) data.erase(0, nl + 1);
    }

    // Initial load: keep only the last `tail` lines (default 500, clamped).
    if (!follow) {
        int64_t tailLines = 500;
        if (const auto tailParam = req->GetQueryParameter("tail")) {
            try { tailLines = std::stoll(*tailParam); } catch (...) {}
        }
        tailLines = std::clamp<int64_t>(tailLines, 1, 10000);

        size_t cut = data.size();
        int64_t seen = 0;
        // Ignore a trailing newline so the final line isn't double-counted.
        size_t scan = (cut > 0 && data[cut - 1] == '\n') ? cut - 1 : cut;
        while (scan > 0) {
            const size_t nl = data.rfind('\n', scan - 1);
            if (nl == std::string::npos) { cut = 0; break; }
            if (++seen >= tailLines) { cut = nl + 1; break; }
            scan = nl;
        }
        if (cut > 0 && cut <= data.size()) data.erase(0, cut);
    }

    obj.emplace_back("enabled", UniValue(true));
    obj.emplace_back("path",    UniValue(log_path.string()));
    obj.emplace_back("data",    UniValue(data));
    obj.emplace_back("next",    UniValue(size));
    obj.emplace_back("size",    UniValue(size));

    SetJSONHeaders(req, *cors);
    req->WriteReply(HTTP_OK, UniValue::stringify(obj));
    return true;
}

// ---- Router -------------------------------------------------------------

bool WebUINodeAPIRoute(Config &config, HTTPRequest *req, const std::string &path)
{
    if (path == "/webui/api/rpc")                    return HandleRPC(config, req);
    if (path == "/webui/api/node/status")            return HandleNodeStatus(req);
    if (path == "/webui/api/node/mining")            return HandleNodeMining(config, req);
    if (path == "/webui/api/node/logs")              return HandleNodeLogs(req);
    if (path == "/webui/api/node/features")          return HandleNodeFeatures(req);
    if (path == "/webui/api/node/peers")             return HandleNodePeers(req);
    if (path == "/webui/api/node/banned")            return HandleNodeBanned(req);
    if (path == "/webui/api/node/peers/ban")         return HandlePeerAction(config, req, "ban");
    if (path == "/webui/api/node/peers/unban")       return HandlePeerAction(config, req, "unban");
    if (path == "/webui/api/node/peers/add")         return HandlePeerAction(config, req, "add");
    if (path == "/webui/api/node/peers/disconnect")  return HandlePeerAction(config, req, "disconnect");
    if (path == "/webui/api/verifymessage")          return HandleVerifyMessage(config, req);
    if (path == "/webui/api/psbt/decode")            return HandlePSBTDecode(config, req);
    if (path == "/webui/api/psbt/broadcast")         return HandlePSBTBroadcast(config, req);

    req->WriteReply(HTTP_NOT_FOUND, JsonError("unknown node API path"));
    return false;
}
