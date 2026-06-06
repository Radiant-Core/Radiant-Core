// Copyright (c) 2026 The Radiant Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <metrics.h>

#include <chainparams.h>
#include <clientversion.h>
#include <config.h>
#include <httprpc.h>
#include <httpserver.h>
#include <net.h>
#include <rpc/protocol.h> // For HTTP status codes
#include <rpc/server.h>
#include <txmempool.h>
#include <util/system.h>
#include <validation.h>

#include <sstream>

/**
 * SECURITY (audit 2026-06, H3): default on. When true, the /metrics endpoint
 * requires the same authentication as the JSON-RPC interface. Operators who
 * front /metrics with their own authentication (e.g. a reverse proxy) can opt
 * out with -metricsauth=0.
 */
static const bool DEFAULT_METRICS_AUTH = true;
/** WWW-Authenticate header presented with a 401 from /metrics. */
static const char *METRICS_WWW_AUTH_HEADER_DATA = "Basic realm=\"jsonrpc\"";

static bool MetricsHandler(Config &config, HTTPRequest *req,
                           const std::string &strURI) {
    // Only allow GET requests
    if (req->GetRequestMethod() != HTTPRequest::GET) {
        req->WriteReply(HTTP_BAD_METHOD, "Metrics endpoint only supports GET");
        return false;
    }

    // SECURITY (audit 2026-06, H3): require RPC authentication for /metrics
    // unless explicitly opted out. Previously this endpoint was unauthenticated
    // and exposed node internals (height, peer count, mempool) to anyone able to
    // reach the RPC port. Prometheus scraping keeps working by supplying the
    // same Basic-auth credentials as RPC.
    if (gArgs.GetBoolArg("-metricsauth", DEFAULT_METRICS_AUTH) &&
        !RPCAuthorizedRequest(req)) {
        req->WriteHeader("WWW-Authenticate", METRICS_WWW_AUTH_HEADER_DATA);
        req->WriteReply(HTTP_UNAUTHORIZED, "");
        return false;
    }

    std::ostringstream ss;

    // --- Block Height ---
    {
        // chainActive needs cs_main lock for thread safety
        LOCK(cs_main);
        ss << "# HELP radiant_block_height The current block height\n";
        ss << "# TYPE radiant_block_height gauge\n";
        ss << "radiant_block_height " << ChainActive().Height() << "\n";
    }

    // --- Peer Count ---
    if (g_connman) {
        ss << "# HELP radiant_peers_connected Number of connected peers\n";
        ss << "# TYPE radiant_peers_connected gauge\n";
        ss << "radiant_peers_connected "
           << g_connman->GetNodeCount(CConnman::CONNECTIONS_ALL) << "\n";
    }

    // --- Mempool Stats ---
    // CTxMemPool methods handle their own locking (LOCK(cs))
    ss << "# HELP radiant_mempool_size Number of transactions in mempool\n";
    ss << "# TYPE radiant_mempool_size gauge\n";
    ss << "radiant_mempool_size " << g_mempool.size() << "\n";

    ss << "# HELP radiant_mempool_bytes_dynamic Dynamic memory usage of mempool "
          "in bytes\n";
    ss << "# TYPE radiant_mempool_bytes_dynamic gauge\n";
    ss << "radiant_mempool_bytes_dynamic " << g_mempool.DynamicMemoryUsage()
       << "\n";

    // --- Version Info ---
    ss << "# HELP radiant_info Information about the Radiant node\n";
    ss << "# TYPE radiant_info gauge\n";
    ss << "radiant_info{version=\"" << CLIENT_VERSION_MAJOR << "."
       << CLIENT_VERSION_MINOR << "." << CLIENT_VERSION_REVISION
       << "\"} 1\n";

    req->WriteHeader("Content-Type", "text/plain; version=0.0.4");
    req->WriteReply(HTTP_OK, ss.str());
    return true;
}

void StartPrometheusMetrics(Config &config) {
    LogPrintf("Starting Prometheus Metrics on /metrics\n");
    RegisterHTTPHandler("/metrics", true, MetricsHandler);
}

void StopPrometheusMetrics() {
    UnregisterHTTPHandler("/metrics", true);
}
