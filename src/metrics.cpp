// Copyright (c) 2026 The Radiant Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <metrics.h>

#include <chainparams.h>
#include <clientversion.h>
#include <config.h>
#include <httpserver.h>
#include <net.h>
#include <rpc/server.h>
#include <txmempool.h>
#include <validation.h>

#include <sstream>

static bool MetricsHandler(Config &config, HTTPRequest *req,
                           const std::string &strURI) {
    // Only allow GET requests
    if (req->GetRequestMethod() != HTTPRequest::GET) {
        req->WriteReply(HTTP_BAD_METHOD, "Metrics endpoint only supports GET");
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
