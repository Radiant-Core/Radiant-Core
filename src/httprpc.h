// Copyright (c) 2015 The Bitcoin Core developers
// Copyright (c) 2022-2026 The Radiant developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#pragma once

#include <httpserver.h>
#include <rpc/server.h>

#include <map>
#include <string>

class Config;

class HTTPRPCRequestProcessor {
private:
    Config &config;
    RPCServer &rpcServer;

    bool ProcessHTTPRequest(HTTPRequest *request);

public:
    HTTPRPCRequestProcessor(Config &configIn, RPCServer &rpcServerIn)
        : config(configIn), rpcServer(rpcServerIn) {}

    static bool DelegateHTTPRequest(HTTPRPCRequestProcessor *requestProcessor,
                                    HTTPRequest *request) {
        return requestProcessor->ProcessHTTPRequest(request);
    }
};

/**
 * SECURITY (audit 2026-06, H3): check whether an HTTP request carries valid RPC
 * credentials (the same single-user/multi-user/cookie check used by the JSON-RPC
 * endpoint). Used to gate auxiliary endpoints such as /metrics. Returns true iff
 * the request's Authorization header authenticates successfully. Safe to call
 * after StartHTTPRPC() (which initializes the credential state).
 */
bool RPCAuthorizedRequest(HTTPRequest *req);

/**
 * Start HTTP RPC subsystem.
 * Precondition; HTTP and RPC has been started.
 */
bool StartHTTPRPC(HTTPRPCRequestProcessor &httpRPCRequestProcessor);

/** Interrupt HTTP RPC subsystem */
void InterruptHTTPRPC();

/**
 * Stop HTTP RPC subsystem.
 * Precondition; HTTP and RPC has been stopped.
 */
void StopHTTPRPC();

/**
 * Start HTTP REST subsystem.
 * Precondition; HTTP and RPC has been started.
 */
void StartREST();

/** Interrupt RPC REST subsystem */
void InterruptREST();

/**
 * Stop HTTP REST subsystem.
 * Precondition; HTTP and RPC has been stopped.
 */
void StopREST();
