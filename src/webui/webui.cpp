// Copyright (c) 2025-present The Avian Core developers
// Copyright (c) 2025-present The Radiant Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://opensource.org/license/mit/.

#include <webui/webui.h>
#include <webui/webui_internal.h>

#include <config.h>
#include <httpserver.h>
#include <logging.h>
#include <net.h>
#include <univalue.h>
#include <fs.h>
#include <util/strencodings.h>
#include <util/system.h>
#include <util/time.h>
#include <validation.h>
#include <validationinterface.h>

#include <event2/buffer.h>
#include <event2/event.h>
#include <event2/http.h>

#include <algorithm>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <vector>

// ---- Globals (declared here, extern'd in webui_internal.h) ----------

bool g_webui_use_password{false};
std::string g_webui_token;
std::set<std::string> g_webui_allowed_hosts;

// Written by InitWebUIAuth() in auth.cpp; read by StopWebUI().
bool g_webui_cookie_generated{false};

// ---- Server-Sent Events -----------------------------------------------

static std::mutex g_sse_mutex;
static std::vector<evhttp_request *> g_sse_clients;

static void PushSSEEvent(const std::string &event_name, const std::string &json_data)
{
    struct event_base *base = EventBase();
    if (!base) return;
    const std::string frame = "event: " + event_name + "\ndata: " + json_data + "\n\n";
    HTTPEvent *ev = new HTTPEvent(base, /*deleteWhenTriggered=*/true, [frame]() {
        std::lock_guard<std::mutex> lock(g_sse_mutex);
        for (auto *raw : g_sse_clients) {
            evbuffer *buf = evbuffer_new();
            evbuffer_add(buf, frame.data(), frame.size());
            evhttp_send_reply_chunk(raw, buf);
            evbuffer_free(buf);
        }
    });
    struct timeval zero{0, 0};
    ev->trigger(&zero);
}

class WebUINotifier : public CValidationInterface
{
public:
    void UpdatedBlockTip(const CBlockIndex *pindexNew, const CBlockIndex *, bool fInitialDownload) override
    {
        if (fInitialDownload || !pindexNew) return;
        UniValue::Object data;
        data.emplace_back("height", UniValue(static_cast<int64_t>(pindexNew->nHeight)));
        data.emplace_back("hash",   UniValue(pindexNew->GetBlockHash().GetHex()));
        data.emplace_back("time",   UniValue(static_cast<int64_t>(pindexNew->GetBlockTime())));
        PushSSEEvent("block", UniValue::stringify(data));
    }

    void TransactionAddedToMempool(const CTransactionRef &ptxn) override
    {
        UniValue::Object data;
        data.emplace_back("txid", UniValue(ptxn->GetHash().GetHex()));
        PushSSEEvent("mempool", UniValue::stringify(data));
    }
};

static std::unique_ptr<WebUINotifier> g_webui_notifier;

// ---- SSE endpoint -------------------------------------------------------

static bool HandleSSEEvents(Config &, HTTPRequest *req, const std::string &)
{
    if (req->GetRequestMethod() != HTTPRequest::GET) {
        req->WriteReply(HTTP_BAD_METHOD, "");
        return false;
    }
    if (!CheckWebUIHost(req)) return false;

    // EventSource API cannot set Authorization headers; authenticate via a
    // single-use, short-lived ticket minted by HandleSSETicket().
    const auto ticket_param = req->GetQueryParameter("ticket");
    if (!ticket_param || !ConsumeWebUISSETicket(*ticket_param)) {
        req->WriteReply(HTTP_UNAUTHORIZED, R"({"error":"unauthorized"})");
        return false;
    }

    // CORS: check Origin against allowed hosts.
    auto originOpt = req->GetHeader("Origin");
    std::string allowed_origin;
    if (originOpt) {
        std::string hostname = *originOpt;
        const auto scheme_end = hostname.find("://");
        if (scheme_end != std::string::npos) hostname = hostname.substr(scheme_end + 3);
        const auto colon = hostname.find(':');
        if (colon != std::string::npos) hostname = hostname.substr(0, colon);
        if (!g_webui_allowed_hosts.count(hostname)) {
            req->WriteReply(HTTP_FORBIDDEN, R"({"error":"origin not allowed"})");
            return false;
        }
        allowed_origin = *originOpt;
    }

    req->WriteHeader("Content-Type",      "text/event-stream");
    req->WriteHeader("Cache-Control",     "no-cache");
    req->WriteHeader("X-Accel-Buffering", "no");
    req->WriteHeader("Connection",        "keep-alive");
    if (!allowed_origin.empty()) {
        req->WriteHeader("Access-Control-Allow-Origin", allowed_origin);
    }

    evhttp_request *raw = req->GetRaw();
    evhttp_connection *conn = req->GetConnection();

    req->StartChunkedReply(200);
    req->SendChunk(": connected\n\n");

    // Register close callback so we can remove the client from our list.
    if (conn) {
        evhttp_connection_set_closecb(conn, [](evhttp_connection *, void *ctx) {
            auto *raw_ptr = static_cast<evhttp_request *>(ctx);
            std::lock_guard<std::mutex> lock(g_sse_mutex);
            auto &v = g_sse_clients;
            v.erase(std::remove(v.begin(), v.end(), raw_ptr), v.end());
        }, raw);
    }

    {
        std::lock_guard<std::mutex> lock(g_sse_mutex);
        g_sse_clients.push_back(raw);
    }

    return true;
}

// ---- Main dispatcher ----------------------------------------------------

static void WriteNoStoreHeaders(HTTPRequest *req)
{
    req->WriteHeader("Cache-Control", "no-store, no-cache, must-revalidate, max-age=0");
    req->WriteHeader("Pragma",        "no-cache");
    req->WriteHeader("Expires",       "0");
}

static bool WebUIDispatch(Config &config, HTTPRequest *req, const std::string &)
{
    std::string uri = req->GetURI();
    const size_t qmark = uri.find('?');
    const std::string path = (qmark != std::string::npos) ? uri.substr(0, qmark) : uri;

    // True for /webui/api and every path beginning with /webui/api/
    const bool is_api = (path.rfind("/webui/api", 0) == 0);

    // CORS preflight — OPTIONS to any /webui/api path.
    if (req->GetRequestMethod() == HTTPRequest::OPTIONS && is_api) {
        if (!CheckWebUIHost(req)) return false;
        auto cors = CheckWebUICORS(req);
        if (!cors) return false;
        if (!cors->empty()) {
            req->WriteHeader("Access-Control-Allow-Origin",  *cors);
            req->WriteHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
            req->WriteHeader("Access-Control-Allow-Headers", "Authorization, Content-Type");
            req->WriteHeader("Access-Control-Max-Age",       "600");
        }
        req->WriteReply(204);
        return true;
    }

    // API routes — set no-store on every response so neither the browser cache
    // nor any intermediate layer retains API data across node restarts.
    if (is_api) {
        WriteNoStoreHeaders(req);
        if (path == "/webui/api/auth/info")              return HandleAuthInfo(req);
        if (path == "/webui/api/auth/login")             return HandleAuthLogin(req);
        if (path == "/webui/api/auth/logout")            return HandleAuthLogout(req);
        if (path == "/webui/api/events/ticket")          return HandleSSETicket(req);
        if (path == "/webui/api/events")                 return HandleSSEEvents(config, req, path);
        if (path == "/webui/api/rpc")                    return WebUINodeAPIRoute(config, req, path);
        if (path.rfind("/webui/api/node/",    0) == 0)  return WebUINodeAPIRoute(config, req, path);
        if (path == "/webui/api/verifymessage")          return WebUINodeAPIRoute(config, req, path);
        if (path.rfind("/webui/api/psbt/",    0) == 0)  return WebUINodeAPIRoute(config, req, path);
        if (path.rfind("/webui/api/wallets/", 0) == 0)  return WebUIWalletsRoute(config, req, path);
        if (path.rfind("/webui/api/wallet/",  0) == 0)  return WebUIWalletRoute(config, req, path);
        if (path == "/webui/api/settings")               return HandleWebUISettings(req);
        req->WriteReply(HTTP_NOT_FOUND, R"({"error":"not found"})");
        return false;
    }

    // Static file serving (no auth required).
    return HandleStaticFile(req, path);
}

// ---- Lifecycle ----------------------------------------------------------

void StartWebUI()
{
    if (!InitWebUIAuth()) {
        LogPrintf("WebUI: Failed to initialise authentication, web UI will not start\n");
        return;
    }

    // Build set of hosts allowed for DNS-rebinding protection.
    g_webui_allowed_hosts = {"127.0.0.1", "localhost", "::1"};
    if (!gArgs.GetArgs("-rpcallowip").empty()) {
        for (const std::string &bind : gArgs.GetArgs("-rpcbind")) {
            int port{0};
            std::string host;
            SplitHostPort(bind, port, host);
            if (!host.empty()) g_webui_allowed_hosts.insert(host);
        }
    }


    g_webui_notifier = std::make_unique<WebUINotifier>();
    RegisterValidationInterface(g_webui_notifier.get());

    RegisterHTTPHandler("/webui/", false, WebUIDispatch);

    LogPrintf("WebUI endpoint started at /webui/ (token in %s)\n",
              (GetDataDir(true) / fs::path(WEBUI_COOKIE_FILE)).string());
}

void InterruptWebUI()
{
    // Close all open SSE connections so no more frames are dispatched.
    // UnregisterValidationInterface is intentionally deferred to StopWebUI()
    // to avoid calling it while g_reg_unsafe=true (which triggers a harmless
    // but noisy WARNING from validationinterface.cpp).
    std::vector<evhttp_request *> to_close;
    {
        std::lock_guard<std::mutex> lock(g_sse_mutex);
        to_close = std::move(g_sse_clients);
    }
    if (to_close.empty()) return;

    struct event_base *base = EventBase();
    if (!base) return;

    auto *clients = new std::vector<evhttp_request *>(std::move(to_close));
    event_base_once(base, -1, EV_TIMEOUT,
        [](evutil_socket_t, short, void *arg) {
            auto *vec = static_cast<std::vector<evhttp_request *> *>(arg);
            for (auto *raw : *vec) {
                evhttp_send_reply_end(raw);
            }
            delete vec;
        }, clients, nullptr);
}

void StopWebUI()
{
    // Unregister here (called from Shutdown(), after SetValidationInterfaceRegistrationsUnsafe(false))
    // to avoid the "called from outside init/shutdown" warning.
    if (g_webui_notifier) {
        UnregisterValidationInterface(g_webui_notifier.get());
    }
    g_webui_notifier.reset();
    UnregisterHTTPHandler("/webui/", false);
    if (g_webui_cookie_generated) {
        try {
            fs::remove(GetDataDir(true) / fs::path(WEBUI_COOKIE_FILE));
        } catch (const fs::filesystem_error &e) {
            LogPrintf("WebUI: Unable to remove cookie file: %s\n", e.code().message());
        }
        g_webui_cookie_generated = false;
    }
}
