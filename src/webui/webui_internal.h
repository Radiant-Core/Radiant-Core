// Copyright (c) 2025-present The Avian Core developers
// Copyright (c) 2025-present The Radiant Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://opensource.org/license/mit/.

#pragma once

#include <httpserver.h>
#include <rpc/protocol.h>
#include <string>
#include <optional>
#include <set>

// auth.cpp
bool InitWebUIAuth();
bool CheckWebUIHost(HTTPRequest *req);
std::optional<std::string> CheckWebUICORS(HTTPRequest *req);
bool CheckWebUIAuth(HTTPRequest *req);
bool CheckWebUIToken(const std::string &submitted);
bool CheckWebUIPassword(const std::string &submitted);
std::string CreateWebUISessionToken();
void RevokeWebUISessionToken(const std::string &token);
bool ConsumeWebUISSETicket(const std::string &ticket);
bool HandleAuthLogin(HTTPRequest *req);
bool HandleAuthLogout(HTTPRequest *req);
bool HandleSSETicket(HTTPRequest *req);
void SetJSONHeaders(HTTPRequest *req, const std::string &origin);

// Shared auth globals (set during StartWebUI, read by auth.cpp, webui.cpp)
extern bool g_webui_use_password;
extern std::string g_webui_token;
extern std::set<std::string> g_webui_allowed_hosts;
extern bool g_webui_cookie_generated;

// json.cpp
std::string JsonError(const std::string &msg);

// static.cpp
bool HandleAuthInfo(HTTPRequest *req);
bool HandleStaticFile(HTTPRequest *req, const std::string &path);

// node_api.cpp
bool WebUINodeAPIRoute(Config &config, HTTPRequest *req, const std::string &path);

// wallet_api.cpp
bool WebUIWalletsRoute(Config &config, HTTPRequest *req, const std::string &path);
bool WebUIWalletRoute(Config &config, HTTPRequest *req, const std::string &path);

// settings.cpp
bool HandleWebUISettings(HTTPRequest *req);
