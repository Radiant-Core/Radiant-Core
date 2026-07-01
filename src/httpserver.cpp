// Copyright (c) 2015-2016 The Bitcoin Core developers
// Copyright (c) 2022-2026 The Radiant developers
// Copyright (c) 2018-2019 The Bitcoin developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <httpserver.h>

#include <chainparamsbase.h>
#include <compat.h>
#include <config.h>
#include <logging.h>
#include <netbase.h>
#include <rpc/protocol.h> // For HTTP status codes
#include <shutdown.h>
#include <sync.h>
#include <ui_interface.h>
#include <util/strencodings.h>
#include <util/string.h>
#include <util/system.h>
#include <util/threadnames.h>

#include <event2/buffer.h>
#include <event2/bufferevent.h>
#include <event2/keyvalq_struct.h>
#include <event2/thread.h>
#include <event2/util.h>

#include <support/events.h>

#ifdef EVENT__HAVE_NETINET_IN_H
#include <netinet/in.h>
#ifdef _XOPEN_SOURCE_EXTENDED
#include <arpa/inet.h>
#endif
#endif

#include <sys/stat.h>
#include <sys/types.h>

#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <memory>

/** Maximum size of http request (request line + headers) */
static const size_t MAX_HEADERS_SIZE = 8192;

/**
 * Maximum HTTP post body size. Twice the maximum block size is added to this
 * value in practice.
 */
static const size_t MIN_SUPPORTED_BODY_SIZE = 0x02000000;

/** HTTP request work item */
class HTTPWorkItem final : public HTTPClosure {
public:
    HTTPWorkItem(Config &_config, std::unique_ptr<HTTPRequest> _req,
                 const std::string &_path, const HTTPRequestHandler &_func)
        : req(std::move(_req)), path(_path), func(_func), config(&_config) {}

    void operator()() override { func(*config, req.get(), path); }

    std::unique_ptr<HTTPRequest> req;

private:
    std::string path;
    HTTPRequestHandler func;
    Config *config;
};

/**
 * Simple work queue for distributing work over multiple threads.
 * Work items are simply callable objects.
 */
template <typename WorkItem> class WorkQueue {
private:
    /** Mutex protects entire object */
    Mutex cs;
    std::condition_variable cond;
    std::deque<std::unique_ptr<WorkItem>> queue;
    bool running;
    size_t maxDepth;

public:
    explicit WorkQueue(size_t _maxDepth) : running(true), maxDepth(_maxDepth) {}
    /**
     * Precondition: worker threads have all stopped (they have all been joined)
     */
    ~WorkQueue() {}

    /** Enqueue a work item */
    bool Enqueue(WorkItem *item) {
        LOCK(cs);
        if (queue.size() >= maxDepth) {
            return false;
        }
        queue.emplace_back(std::unique_ptr<WorkItem>(item));
        cond.notify_one();
        return true;
    }

    /** Thread function */
    void Run() {
        while (true) {
            std::unique_ptr<WorkItem> i;
            {
                WAIT_LOCK(cs, lock);
                while (running && queue.empty()) {
                    cond.wait(lock);
                }
                if (!running) {
                    break;
                }
                i = std::move(queue.front());
                queue.pop_front();
            }
            (*i)();
        }
    }

    /** Interrupt and exit loops */
    void Interrupt() {
        LOCK(cs);
        running = false;
        cond.notify_all();
    }
};

struct HTTPPathHandler {
    HTTPPathHandler(const std::string &_prefix, bool _exactMatch,
                    const HTTPRequestHandler &_handler)
        : prefix(_prefix), exactMatch(_exactMatch), handler(_handler) {}
    std::string prefix;
    bool exactMatch;
    HTTPRequestHandler handler;
};

/** HTTP module state */

//! libevent event loop
static struct event_base *eventBase = nullptr;
//! HTTP server
struct evhttp *eventHTTP = nullptr;
//! List of subnets to allow RPC connections from
static std::vector<CSubNet> rpc_allow_subnets;
//! SECURITY (audit 2026-06, H5): allowlist of acceptable HTTP Host header
//! hostnames (lower-cased, port stripped). Used as a DNS-rebinding defense.
//! Empty disables the check (e.g. when the operator opts out).
static std::vector<std::string> rpc_allow_hosts;
//! Whether Host-header validation is enabled.
static bool rpc_validate_host = true;
//! Work queue for handling longer requests off the event loop thread
static WorkQueue<HTTPClosure> *workQueue = nullptr;
//! Handlers for (sub)paths
std::vector<HTTPPathHandler> pathHandlers;
//! Bound listening sockets
std::vector<evhttp_bound_socket *> boundSockets;

/** Check if a network address is allowed to access the HTTP server */
static bool ClientAllowed(const CNetAddr &netaddr) {
    if (!netaddr.IsValid()) {
        return false;
    }
    for (const CSubNet &subnet : rpc_allow_subnets) {
        if (subnet.Match(netaddr)) {
            return true;
        }
    }
    return false;
}

/** Initialize ACL list for HTTP server */
static bool InitHTTPAllowList() {
    rpc_allow_subnets.clear();
    CNetAddr localv4;
    CNetAddr localv6;
    LookupHost("127.0.0.1", localv4, false);
    LookupHost("::1", localv6, false);
    // always allow IPv4 local subnet.
    rpc_allow_subnets.push_back(CSubNet(localv4, 8));
    // always allow IPv6 localhost.
    rpc_allow_subnets.push_back(CSubNet(localv6));
    for (const std::string &strAllow : gArgs.GetArgs("-rpcallowip")) {
        CSubNet subnet;
        LookupSubNet(strAllow, subnet);
        if (!subnet.IsValid()) {
            uiInterface.ThreadSafeMessageBox(
                strprintf("Invalid -rpcallowip subnet specification: %s. "
                          "Valid are a single IP (e.g. 1.2.3.4), a "
                          "network/netmask (e.g. 1.2.3.4/255.255.255.0) or a "
                          "network/CIDR (e.g. 1.2.3.4/24).",
                          strAllow),
                "", CClientUIInterface::MSG_ERROR);
            return false;
        }
        rpc_allow_subnets.push_back(subnet);
    }
    std::string strAllowed;
    for (const CSubNet &subnet : rpc_allow_subnets) {
        strAllowed += subnet.ToString() + " ";
    }
    LogPrint(BCLog::HTTP, "Allowing HTTP connections from: %s\n", strAllowed);
    return true;
}

/**
 * SECURITY (audit 2026-06, H5): normalize a Host header value for comparison.
 * Strips an optional port and surrounding brackets (for IPv6 literals) and
 * lower-cases the result.
 */
static std::string NormalizeHostValue(const std::string &host) {
    std::string h = TrimString(host);
    if (h.empty()) {
        return h;
    }
    // Bracketed IPv6 literal, optionally followed by :port -> strip brackets/port.
    if (h.front() == '[') {
        const size_t close = h.find(']');
        if (close != std::string::npos) {
            return ToLower(h.substr(1, close - 1));
        }
    }
    // host:port -> host. Only treat the last ':' as a port separator when there
    // is a single colon (bare IPv6 literals contain multiple colons and have no
    // port here).
    const size_t firstColon = h.find(':');
    const size_t lastColon = h.rfind(':');
    if (firstColon != std::string::npos && firstColon == lastColon) {
        h = h.substr(0, firstColon);
    }
    return ToLower(h);
}

/**
 * SECURITY (audit 2026-06, H5): build the allowlist of acceptable HTTP Host
 * header values. This is the standard DNS-rebinding defense: a browser tricked
 * into resolving an attacker-controlled hostname to 127.0.0.1 will still send
 * that hostname in the Host header, which we reject.
 *
 * The allowlist always includes loopback names plus every host that RPC binds
 * to (-rpcbind) and any operator-supplied -rpcallowhost entries. Operators who
 * front the node with a reverse proxy that rewrites Host can disable the check
 * with -rpcallowhost=*.
 */
static void InitHostAllowList() {
    rpc_allow_hosts.clear();
    rpc_validate_host = true;

    auto add = [](const std::string &h) {
        const std::string n = NormalizeHostValue(h);
        if (!n.empty()) {
            rpc_allow_hosts.push_back(n);
        }
    };

    // Always-allowed loopback identities.
    add("localhost");
    add("127.0.0.1");
    add("[::1]");
    add("::1");

    // Hosts that RPC is bound to.
    for (const std::string &strRPCBind : gArgs.GetArgs("-rpcbind")) {
        int port = 0;
        std::string host;
        SplitHostPort(strRPCBind, port, host);
        add(host);
    }

    // Operator-supplied extra hostnames (DNS names a reverse proxy may use).
    for (const std::string &strHost : gArgs.GetArgs("-rpcallowhost")) {
        if (strHost == "*") {
            // Explicit opt-out: operator takes responsibility for Host filtering
            // (e.g. a trusted reverse proxy in front of the node).
            rpc_validate_host = false;
            LogPrintf("WARNING: -rpcallowhost=* disables HTTP Host-header "
                      "validation (DNS-rebinding protection). Only use this "
                      "when a trusted proxy enforces Host filtering.\n");
            return;
        }
        add(strHost);
    }
}

/**
 * SECURITY (audit 2026-06, H5): validate the HTTP Host header against the
 * allowlist. Returns true if the request should be allowed.
 *
 * Compatibility notes (kept permissive enough not to break normal -rpcbind /
 * -rpcallowip setups):
 *  - A missing Host header is allowed (HTTP/1.0 clients and many RPC libraries
 *    omit it).
 *  - A Host that is a numeric IP literal (IPv4 or IPv6) is always allowed: DNS
 *    rebinding fundamentally requires a *DNS name* in the Host header, so an IP
 *    literal cannot be a rebinding vector. This also means a client connecting
 *    to the node's bound IP (incl. the bind-to-any 0.0.0.0/:: case where there
 *    is no explicit -rpcbind host) is never blocked.
 *  - Only a present, non-numeric (DNS-name) Host that is not in the allowlist
 *    is rejected, which is exactly the rebinding case.
 */
static bool CheckHostHeader(HTTPRequest *req) {
    if (!rpc_validate_host) {
        return true;
    }
    const auto hostOpt = req->GetHeader("host");
    if (!hostOpt) {
        // No Host header: not a browser rebinding attack vector.
        return true;
    }
    const std::string host = NormalizeHostValue(*hostOpt);
    if (host.empty()) {
        return true;
    }
    // Numeric IP literals cannot be used for DNS rebinding; always allow them.
    CNetAddr addr;
    if (LookupHost(host, addr, false /* fAllowLookup */) && addr.IsValid()) {
        return true;
    }
    for (const std::string &allowed : rpc_allow_hosts) {
        if (host == allowed) {
            return true;
        }
    }
    return false;
}

/** HTTP request method as string - use for logging only */
static std::string RequestMethodString(HTTPRequest::RequestMethod m) {
    switch (m) {
        case HTTPRequest::GET:
            return "GET";
        case HTTPRequest::POST:
            return "POST";
        case HTTPRequest::HEAD:
            return "HEAD";
        case HTTPRequest::PUT:
            return "PUT";
        case HTTPRequest::OPTIONS:
            return "OPTIONS";
        default:
            return "unknown";
    }
}

// SECURITY (audit 2026-06, H6): when -debug=httptrace is enabled we must never
// log secret credentials or secret RPC payloads verbatim. The helpers below
// redact the Authorization/Cookie header values and the request/response bodies
// of RPC methods that carry private keys, seeds or passphrases.

/** Returns true if a header name carries a credential that must not be logged. */
static bool IsSensitiveHeaderName(const std::string &name) {
    std::string lower = ToLower(name);
    return lower == "authorization" || lower == "cookie" ||
           lower == "set-cookie" || lower == "proxy-authorization";
}

/**
 * Returns true if the given (already lower-cased) JSON-RPC method name carries
 * secret material in its request and/or response body and should be redacted
 * in httptrace logging.
 */
static bool IsSensitiveRPCMethod(const std::string &methodLower) {
    static const char *const kSensitive[] = {
        "dumpprivkey",     "dumpwallet",         "importprivkey",
        "signrawtransaction", "signrawtransactionwithkey",
        "signrawtransactionwithwallet", "walletpassphrase",
        "walletpassphrasechange", "encryptwallet", "sethdseed", "importwallet",
        "importmulti",     "signmessagewithprivkey",
    };
    for (const char *m : kSensitive) {
        // Match the method name even when it is wrapped in JSON quotes.
        if (methodLower.find(m) != std::string::npos) {
            return true;
        }
    }
    return false;
}

/**
 * Scan an HTTP body for a JSON-RPC "method" value and return true if it names a
 * method whose body must be redacted. Best-effort textual scan (the body is not
 * yet parsed at trace time).
 */
static bool BodyHasSensitiveMethod(const std::string &body) {
    const std::string lower = ToLower(body);
    const size_t pos = lower.find("\"method\"");
    if (pos == std::string::npos) {
        // No method field visible (e.g. non-JSON). Be conservative and treat
        // any body that mentions a sensitive keyword as secret.
        return IsSensitiveRPCMethod(lower);
    }
    return IsSensitiveRPCMethod(lower.substr(pos));
}

/** Join headers for logging, redacting any credential-bearing header value. */
static std::string JoinHeadersRedacted(
    const std::vector<HTTPRequest::NameValuePair> &headersVec) {
    return Join(headersVec, "\n", [](const auto &nvp) {
        const std::string value =
            IsSensitiveHeaderName(nvp.first) ? "[redacted]" : nvp.second;
        return strprintf("%s: %s", nvp.first, value);
    });
}

/** HTTP request callback */
static void http_request_cb(struct evhttp_request *req, void *arg) {
    Config &config = *reinterpret_cast<Config *>(arg);

    // Disable reading to work around a libevent bug, fixed in 2.1.9.
    if (event_get_version_number() >= 0x02010600 &&
        event_get_version_number() < 0x02010900) {
        evhttp_connection *conn = evhttp_request_get_connection(req);
        if (conn) {
            bufferevent *bev = evhttp_connection_get_bufferevent(conn);
            if (bev) {
                bufferevent_disable(bev, EV_READ);
            }
        }
    }
    auto hreq = std::make_unique<HTTPRequest>(req);
    const auto peer = hreq->GetPeer();

    // If HTTPTRACE is enabled, log the request immediately
    // Note: Unlike with regular HTTP logging, we *don't* sanitize any strings
    // coming from the user. HTTPTRACE is an advanced debugging option not
    // intended for general use, so it is felt that this is acceptable.
    // SECURITY (audit 2026-06, H6): even so, credential headers and the bodies
    // of secret-bearing RPC methods (dumpprivkey/dumpwallet/importprivkey/
    // signrawtransaction*/walletpassphrase/encryptwallet/sethdseed/...) are
    // redacted so that enabling httptrace never writes private keys, seeds or
    // passphrases to the log.
    if (LogAcceptCategory(BCLog::HTTPTRACE)) {
        const auto headersVec = hreq->GetAllInputHeaders();
        const std::string headers = JoinHeadersRedacted(headersVec);
        const std::string content = hreq->ReadBody(false);
        const bool redactBody = BodyHasSensitiveMethod(content);
        // Remember so the reply body for the same request is redacted too.
        hreq->SetSensitive(redactBody);
        const std::string loggedContent =
            redactBody ? std::string("[redacted: sensitive RPC method]")
                       : content;
        LogPrintf("<httptrace> Request from %s, method: \"%s\", URI: \"%s\", headers: %u, content: %u bytes\n"
                  "--- HEADERS ---\n%s\n--- CONTENT ---\n%s\n",
                  peer.ToString(), RequestMethodString(hreq->GetRequestMethod()), hreq->GetURI(),
                  headersVec.size(), content.size(), headers, loggedContent);
    }

    // Early address-based allow check
    if (!ClientAllowed(peer)) {
        LogPrint(BCLog::HTTP, "HTTP request from %s rejected: Client network is not allowed RPC access\n",
                 peer.ToString());
        hreq->WriteReply(HTTP_FORBIDDEN);
        return;
    }

    // SECURITY (audit 2026-06, H5): DNS-rebinding defense. Reject requests whose
    // Host header is not an allowlisted hostname (loopback, -rpcbind hosts, or
    // -rpcallowhost entries).
    if (!CheckHostHeader(hreq.get())) {
        LogPrint(BCLog::HTTP,
                 "HTTP request from %s rejected: disallowed Host header\n",
                 peer.ToString());
        hreq->WriteReply(HTTP_FORBIDDEN);
        return;
    }

    const auto method = hreq->GetRequestMethod();

    // Early reject unknown HTTP methods
    if (method == HTTPRequest::UNKNOWN) {
        LogPrint(BCLog::HTTP, "HTTP request from %s rejected: Unknown HTTP request method\n", peer.ToString());
        hreq->WriteReply(HTTP_BADMETHOD);
        return;
    }

    const std::string strURI = hreq->GetURI();

    LogPrint(BCLog::HTTP, "Received a %s request for %s from %s\n",
             RequestMethodString(method), SanitizeString(strURI, SAFE_CHARS_URI).substr(0, 100), peer.ToString());

    // Find registered handler for prefix
    std::string path;
    std::vector<HTTPPathHandler>::const_iterator i = pathHandlers.begin();
    std::vector<HTTPPathHandler>::const_iterator iend = pathHandlers.end();
    for (; i != iend; ++i) {
        bool match = false;
        if (i->exactMatch) {
            match = (strURI == i->prefix);
        } else {
            match = (strURI.substr(0, i->prefix.size()) == i->prefix);
        }
        if (match) {
            path = strURI.substr(i->prefix.size());
            break;
        }
    }

    // Dispatch to worker thread.
    if (i != iend) {
        std::unique_ptr<HTTPWorkItem> item(
            new HTTPWorkItem(config, std::move(hreq), path, i->handler));
        assert(workQueue);
        if (workQueue->Enqueue(item.get())) {
            /* if true, queue took ownership */
            item.release();
        } else {
            LogPrintf("WARNING: request rejected because http work queue depth "
                      "exceeded, it can be increased with the -rpcworkqueue= "
                      "setting\n");
            item->req->WriteReply(HTTP_INTERNAL, "Work queue depth exceeded");
        }
    } else {
        hreq->WriteReply(HTTP_NOTFOUND);
    }
}

/** Callback to reject HTTP requests after shutdown. */
static void http_reject_request_cb(struct evhttp_request *req, void *) {
    LogPrint(BCLog::HTTP, "Rejecting request while shutting down\n");
    evhttp_send_error(req, HTTP_SERVUNAVAIL, nullptr);
}

/** Event dispatcher thread */
static bool ThreadHTTP(struct event_base* base)
{
    util::ThreadRename("http");
    LogPrint(BCLog::HTTP, "Entering http event loop\n");
    event_base_dispatch(base);
    // Event loop will be interrupted by InterruptHTTPServer()
    LogPrint(BCLog::HTTP, "Exited http event loop\n");
    return event_base_got_break(base) == 0;
}

/** Bind HTTP server to specified addresses */
static bool HTTPBindAddresses(struct evhttp *http) {
    int http_port = gArgs.GetArg("-rpcport", BaseParams().RPCPort());
    std::vector<std::pair<std::string, uint16_t>> endpoints;

    // Determine what addresses to bind to
    if (!gArgs.IsArgSet("-rpcallowip")) {
        // Default to loopback if not allowing external IPs.
        endpoints.push_back(std::make_pair("::1", http_port));
        endpoints.push_back(std::make_pair("127.0.0.1", http_port));
        if (gArgs.IsArgSet("-rpcbind")) {
            LogPrintf("WARNING: option -rpcbind was ignored because "
                      "-rpcallowip was not specified, refusing to allow "
                      "everyone to connect\n");
        }
    } else if (gArgs.IsArgSet("-rpcbind")) {
        // Specific bind address.
        for (const std::string &strRPCBind : gArgs.GetArgs("-rpcbind")) {
            int port = http_port;
            std::string host;
            SplitHostPort(strRPCBind, port, host);
            endpoints.push_back(std::make_pair(host, port));
        }
    } else {
        // No specific bind address specified, bind to any.
        endpoints.push_back(std::make_pair("::", http_port));
        endpoints.push_back(std::make_pair("0.0.0.0", http_port));
    }

    // Bind addresses
    for (std::vector<std::pair<std::string, uint16_t>>::iterator i =
             endpoints.begin();
         i != endpoints.end(); ++i) {
        LogPrint(BCLog::HTTP, "Binding RPC on address %s port %i\n", i->first,
                 i->second);
        evhttp_bound_socket *bind_handle = evhttp_bind_socket_with_handle(
            http, i->first.empty() ? nullptr : i->first.c_str(), i->second);
        if (bind_handle) {
            boundSockets.push_back(bind_handle);
        } else {
            LogPrintf("Binding RPC on address %s port %i failed.\n", i->first,
                      i->second);
        }
    }
    return !boundSockets.empty();
}

/** Simple wrapper to set thread name and run work queue */
static void HTTPWorkQueueRun(WorkQueue<HTTPClosure>* queue, int worker_num)
{
    util::ThreadRename(strprintf("httpworker.%i", worker_num));
    queue->Run();
}

/** libevent event log callback */
static void libevent_log_cb(int severity, const char *msg) {
#ifndef EVENT_LOG_WARN
// EVENT_LOG_WARN was added in 2.0.19; but before then _EVENT_LOG_WARN existed.
#define EVENT_LOG_WARN _EVENT_LOG_WARN
#endif
    // Log warn messages and higher without debug category.
    if (severity >= EVENT_LOG_WARN) {
        LogPrintf("libevent: %s\n", msg);
    } else {
        LogPrint(BCLog::LIBEVENT, "libevent: %s\n", msg);
    }
}

bool InitHTTPServer(Config &config) {
    if (!InitHTTPAllowList()) {
        return false;
    }
    // SECURITY (audit 2026-06, H5): build the Host-header allowlist used for
    // DNS-rebinding protection.
    InitHostAllowList();

    // Redirect libevent's logging to our own log
    event_set_log_callback(&libevent_log_cb);
    // Update libevent's log handling. Returns false if our version of
    // libevent doesn't support debug logging, in which case we should
    // clear the BCLog::LIBEVENT flag.
    if (!UpdateHTTPServerLogging(
            LogInstance().WillLogCategory(BCLog::LIBEVENT))) {
        LogInstance().DisableCategory(BCLog::LIBEVENT);
    }

#ifdef WIN32
    evthread_use_windows_threads();
#else
    evthread_use_pthreads();
#endif

    raii_event_base base_ctr = obtain_event_base();

    /* Create a new evhttp object to handle requests. */
    raii_evhttp http_ctr = obtain_evhttp(base_ctr.get());
    struct evhttp *http = http_ctr.get();
    if (!http) {
        LogPrintf("couldn't create evhttp. Exiting.\n");
        return false;
    }

    evhttp_set_timeout(
        http, gArgs.GetArg("-rpcservertimeout", DEFAULT_HTTP_SERVER_TIMEOUT));
    evhttp_set_max_headers_size(http, MAX_HEADERS_SIZE);
    // scale the max body size with our block size so RPC always works for large blocks
    evhttp_set_max_body_size(http, MIN_SUPPORTED_BODY_SIZE +
                                       2 * config.GetExcessiveBlockSize());
    evhttp_set_gencb(http, http_request_cb, &config);

    // Only POST and OPTIONS are supported, but we return HTTP 405 for the
    // others
    evhttp_set_allowed_methods(
        http, EVHTTP_REQ_GET | EVHTTP_REQ_POST | EVHTTP_REQ_HEAD |
                  EVHTTP_REQ_PUT | EVHTTP_REQ_DELETE | EVHTTP_REQ_OPTIONS);

    if (!HTTPBindAddresses(http)) {
        LogPrintf("Unable to bind any endpoint for RPC server\n");
        return false;
    }

    LogPrint(BCLog::HTTP, "Initialized HTTP server\n");
    int workQueueDepth = std::max(
        (long)gArgs.GetArg("-rpcworkqueue", DEFAULT_HTTP_WORKQUEUE), 1L);
    LogPrintf("HTTP: creating work queue of depth %d\n", workQueueDepth);

    workQueue = new WorkQueue<HTTPClosure>(workQueueDepth);
    // transfer ownership to eventBase/HTTP via .release()
    eventBase = base_ctr.release();
    eventHTTP = http_ctr.release();
    return true;
}

bool UpdateHTTPServerLogging(bool enable) {
#if LIBEVENT_VERSION_NUMBER >= 0x02010100
    if (enable) {
        event_enable_debug_logging(EVENT_DBG_ALL);
    } else {
        event_enable_debug_logging(EVENT_DBG_NONE);
    }
    return true;
#else
    // Can't update libevent logging if version < 02010100
    return false;
#endif
}

std::thread threadHTTP;
static std::vector<std::thread> g_thread_http_workers;

void StartHTTPServer() {
    LogPrint(BCLog::HTTP, "Starting HTTP server\n");
    int rpcThreads =
        std::max((long)gArgs.GetArg("-rpcthreads", DEFAULT_HTTP_THREADS), 1L);
    LogPrintf("HTTP: starting %d worker threads\n", rpcThreads);
    threadHTTP = std::thread(ThreadHTTP, eventBase);

    for (int i = 0; i < rpcThreads; i++) {
        g_thread_http_workers.emplace_back(HTTPWorkQueueRun, workQueue, i);
    }
}

void InterruptHTTPServer() {
    LogPrint(BCLog::HTTP, "Interrupting HTTP server\n");
    if (eventHTTP) {
        // Reject requests on current connections
        evhttp_set_gencb(eventHTTP, http_reject_request_cb, nullptr);
    }
    if (workQueue) {
        workQueue->Interrupt();
    }
}

void StopHTTPServer() {
    LogPrint(BCLog::HTTP, "Stopping HTTP server\n");
    if (workQueue) {
        LogPrint(BCLog::HTTP, "Waiting for HTTP worker threads to exit\n");
        for (auto &thread : g_thread_http_workers) {
            thread.join();
        }
        g_thread_http_workers.clear();
        delete workQueue;
        workQueue = nullptr;
    }
    // Unlisten sockets, these are what make the event loop running, which means
    // that after this and all connections are closed the event loop will quit.
    for (evhttp_bound_socket *socket : boundSockets) {
        evhttp_del_accept_socket(eventHTTP, socket);
    }
    boundSockets.clear();
    if (eventBase) {
        LogPrint(BCLog::HTTP, "Waiting for HTTP event thread to exit\n");
        threadHTTP.join();
    }
    if (eventHTTP) {
        evhttp_free(eventHTTP);
        eventHTTP = nullptr;
    }
    if (eventBase) {
        event_base_free(eventBase);
        eventBase = nullptr;
    }
    LogPrint(BCLog::HTTP, "Stopped HTTP server\n");
}

struct event_base *EventBase() {
    return eventBase;
}

static void httpevent_callback_fn(evutil_socket_t, short, void *data) {
    // Static handler: simply call inner handler
    HTTPEvent *self = static_cast<HTTPEvent *>(data);
    self->handler();
    if (self->deleteWhenTriggered) {
        delete self;
    }
}

HTTPEvent::HTTPEvent(struct event_base *base, bool _deleteWhenTriggered,
                     const std::function<void()> &_handler)
    : deleteWhenTriggered(_deleteWhenTriggered), handler(_handler) {
    ev = event_new(base, -1, 0, httpevent_callback_fn, this);
    assert(ev);
}
HTTPEvent::~HTTPEvent() {
    event_free(ev);
}
void HTTPEvent::trigger(struct timeval *tv) {
    if (tv == nullptr) {
        // Immediately trigger event in main thread.
        event_active(ev, 0, 0);
    } else {
        // Trigger after timeval passed.
        evtimer_add(ev, tv);
    }
}
HTTPRequest::HTTPRequest(struct evhttp_request *_req)
    : req(_req), replySent(false) {}
HTTPRequest::~HTTPRequest() {
    if (!replySent) {
        // Keep track of whether reply was sent to avoid request leaks
        LogPrintf("%s: Unhandled request\n", __func__);
        WriteReply(HTTP_INTERNAL, "Unhandled request");
    }
    // evhttpd cleans up the request, as long as a reply was sent.
}

std::optional<std::string> HTTPRequest::GetHeader(const std::string &hdr) const {
    const struct evkeyvalq *headers = evhttp_request_get_input_headers(req);
    assert(headers);
    const char *val = evhttp_find_header(headers, hdr.c_str());
    if (val) {
        return val;
    } else {
        return std::nullopt;
    }
}

std::vector<HTTPRequest::NameValuePair> HTTPRequest::GetAllHeaders(bool input) const {
    std::vector<NameValuePair> ret;
    const evkeyvalq *headers = input ? evhttp_request_get_input_headers(req)
                                     : evhttp_request_get_output_headers(req);
    assert(headers);

    // Note: we would use TAILQ_FOREACH here but that's not always defined on all platforms, so
    // we must do this.
    for (const evkeyval *header = headers->tqh_first; header != nullptr; header = header->next.tqe_next) {
        ret.emplace_back(header->key, header->value);
    }

    return ret;
}

std::string HTTPRequest::ReadBody(bool drain) {
    std::string ret;
    struct evbuffer *buf = evhttp_request_get_input_buffer(req);
    if (!buf) {
        return ret;
    }
    const size_t size = evbuffer_get_length(buf);
    /**
     * Trivial implementation: if this is ever a performance bottleneck,
     * internal copying can be avoided in multi-segment buffers by using
     * evbuffer_peek and an awkward loop. Though in that case, it'd be even
     * better to not copy into an intermediate string but use a stream
     * abstraction to consume the evbuffer on the fly in the parsing algorithm.
     */
    const char *data = (const char *)evbuffer_pullup(buf, size);

    // returns nullptr in case of empty buffer.
    if (!data) {
        return ret;
    }
    ret.assign(data, size);
    if (drain) evbuffer_drain(buf, size);
    return ret;
}

void HTTPRequest::WriteHeader(const std::string &hdr,
                              const std::string &value) {
    struct evkeyvalq *headers = evhttp_request_get_output_headers(req);
    assert(headers);
    evhttp_add_header(headers, hdr.c_str(), value.c_str());
}

/**
 * Closure sent to main thread to request a reply to be sent to a HTTP request.
 * Replies must be sent in the main loop in the main http thread, this cannot be
 * done from worker threads.
 */
void HTTPRequest::WriteReply(int nStatus, const std::string &strReply) {
    assert(!replySent && req);
    if (ShutdownRequested()) {
        WriteHeader("Connection", "close");
    }

    // If HTTPTRACE is enabled, log what we are replying with
    // SECURITY (audit 2026-06, H6): redact credential headers and, for requests
    // flagged as secret-bearing (e.g. dumpprivkey), the reply body too.
    if (LogAcceptCategory(BCLog::HTTPTRACE)) {
        const auto headersVec = GetAllOutputHeaders();
        bool isBinary = false;
        for (const auto &nvp : headersVec) {
            const auto & [name, value] = nvp;
            // Set the isBinary flag if we are outputting binary (this is for REST .bin output mode)
            if (!isBinary && name == "Content-Type" && value == "application/octet-stream") isBinary = true;
        }
        const std::string headers = JoinHeadersRedacted(headersVec);
        const char *content_desc = "";
        std::string hexStrReply;
        if (isBinary) {
            // If we are outputting binary (REST .bin mode), we will encode the data as hex first,
            // to keep log files tidy.
            content_desc = " (binary data, hex encoded)";
            hexStrReply = HexStr(strReply);
        }
        std::string redactedReply;
        if (sensitive) {
            content_desc = " (redacted: sensitive RPC method)";
            redactedReply = "[redacted]";
        }
        const std::string &content =
            sensitive ? redactedReply : (isBinary ? hexStrReply : strReply);
        LogPrintf("<httptrace> Writing reply to %s, status: %d, headers: %u, content: %u bytes\n"
                  "--- HEADERS ---\n%s\n--- CONTENT%s ---\n%s\n",
                  GetPeer().ToString(), nStatus, headersVec.size(), strReply.size(), headers, content_desc, content);
    }

    // Send event to main http thread to send reply message
    struct evbuffer *evb = evhttp_request_get_output_buffer(req);
    assert(evb);
    evbuffer_add(evb, strReply.data(), strReply.size());
    auto req_copy = req;
    HTTPEvent *ev = new HTTPEvent(eventBase, true, [req_copy, nStatus] {
        evhttp_send_reply(req_copy, nStatus, nullptr, nullptr);
        // Re-enable reading from the socket. This is the second part of the
        // libevent workaround above.
        if (event_get_version_number() >= 0x02010600 &&
            event_get_version_number() < 0x02010900) {
            evhttp_connection *conn = evhttp_request_get_connection(req_copy);
            if (conn) {
                bufferevent *bev = evhttp_connection_get_bufferevent(conn);
                if (bev) {
                    bufferevent_enable(bev, EV_READ | EV_WRITE);
                }
            }
        }
    });
    ev->trigger(nullptr);
    replySent = true;
    // transferred back to main thread.
    req = nullptr;
}

CService HTTPRequest::GetPeer() const {
    evhttp_connection *con = evhttp_request_get_connection(req);
    CService peer;
    if (con) {
        // evhttp retains ownership over returned address string
        const char *address = "";
        uint16_t port = 0;
        evhttp_connection_get_peer(con, (char **)&address, &port);
        peer = LookupNumeric(address, port);
    }
    return peer;
}

std::string HTTPRequest::GetURI() const {
    return evhttp_request_get_uri(req);
}

struct evhttp_connection *HTTPRequest::GetConnection() const {
    return req ? evhttp_request_get_connection(req) : nullptr;
}

void HTTPRequest::StartChunkedReply(int nStatus) {
    assert(!replySent && req);
    replySent = true;
    // Schedule on the libevent thread.  req is NOT nulled: SendChunk() and
    // GetRaw() must remain usable after this call returns.
    auto req_copy = req;
    HTTPEvent *ev = new HTTPEvent(eventBase, /*deleteWhenTriggered=*/true,
        [req_copy, nStatus] {
            evhttp_send_reply_start(req_copy, nStatus, nullptr);
        });
    ev->trigger(nullptr);
}

void HTTPRequest::SendChunk(const std::string &chunk) {
    assert(replySent && req);
    auto req_copy = req;
    std::string data = chunk;
    HTTPEvent *ev = new HTTPEvent(eventBase, /*deleteWhenTriggered=*/true,
        [req_copy, data] {
            evbuffer *buf = evbuffer_new();
            evbuffer_add(buf, data.data(), data.size());
            evhttp_send_reply_chunk(req_copy, buf);
            evbuffer_free(buf);
        });
    ev->trigger(nullptr);
}

std::optional<std::string> HTTPRequest::GetQueryParameter(const std::string &name) const {
    if (!req) return std::nullopt;
    const char *uri_str = evhttp_request_get_uri(req);
    if (!uri_str) return std::nullopt;
    // Parse using libevent's URI helpers.
    struct evhttp_uri *parsed = evhttp_uri_parse(uri_str);
    if (!parsed) return std::nullopt;
    const char *query_str = evhttp_uri_get_query(parsed);
    std::optional<std::string> result;
    if (query_str) {
        struct evkeyvalq params;
        if (evhttp_parse_query_str(query_str, &params) == 0) {
            const char *val = evhttp_find_header(&params, name.c_str());
            if (val) result = val;
            evhttp_clear_headers(&params);
        }
    }
    evhttp_uri_free(parsed);
    return result;
}

HTTPRequest::RequestMethod HTTPRequest::GetRequestMethod() const {
    switch (evhttp_request_get_command(req)) {
        case EVHTTP_REQ_GET:
            return GET;
        case EVHTTP_REQ_POST:
            return POST;
        case EVHTTP_REQ_HEAD:
            return HEAD;
        case EVHTTP_REQ_PUT:
            return PUT;
        case EVHTTP_REQ_OPTIONS:
            return OPTIONS;
        default:
            return UNKNOWN;
    }
}

void RegisterHTTPHandler(const std::string &prefix, bool exactMatch,
                         const HTTPRequestHandler &handler) {
    LogPrint(BCLog::HTTP, "Registering HTTP handler for %s (exactmatch %d)\n",
             prefix, exactMatch);
    pathHandlers.emplace_back(prefix, exactMatch, handler);
}

void UnregisterHTTPHandler(const std::string &prefix, bool exactMatch) {
    std::vector<HTTPPathHandler>::iterator i = pathHandlers.begin();
    std::vector<HTTPPathHandler>::iterator iend = pathHandlers.end();
    for (; i != iend; ++i) {
        if (i->prefix == prefix && i->exactMatch == exactMatch) {
            break;
        }
    }
    if (i != iend) {
        LogPrint(BCLog::HTTP,
                 "Unregistering HTTP handler for %s (exactmatch %d)\n", prefix,
                 exactMatch);
        pathHandlers.erase(i);
    }
}
