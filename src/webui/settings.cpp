// Copyright (c) 2025-present The Radiant Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://opensource.org/license/mit/.

#include <webui/webui_internal.h>

#include <fs.h>
#include <util/system.h>
#include <univalue.h>

#include <fstream>
#include <sstream>
#include <string>

static const char *SETTINGS_FILE = "webui-settings.json";

static std::string SettingsFilePath() {
    return (GetDataDir(true) / fs::path(SETTINGS_FILE)).string();
}

static UniValue ReadSettings() {
    UniValue obj(UniValue::VOBJ);
    std::ifstream f(SettingsFilePath());
    if (!f.is_open()) return obj;
    std::ostringstream ss;
    ss << f.rdbuf();
    UniValue v;
    try {
        if (v.read(ss.str()) && v.isObject()) return v;
    } catch (...) {}
    return obj;
}

static bool WriteSettings(const UniValue &s) {
    std::ofstream f(SettingsFilePath(), std::ios::trunc);
    if (!f.is_open()) return false;
    f << UniValue::stringify(s);
    return f.good();
}

bool HandleWebUISettings(HTTPRequest *req) {
    if (!CheckWebUIHost(req)) return false;
    auto cors = CheckWebUICORS(req);
    if (!cors) return false;
    if (!CheckWebUIAuth(req)) return false;

    const std::string origin = cors.value_or("");
    const auto method = req->GetRequestMethod();

    if (method == HTTPRequest::GET) {
        SetJSONHeaders(req, origin);
        req->WriteReply(HTTP_OK, UniValue::stringify(ReadSettings()));
        return true;
    }

    if (method == HTTPRequest::POST) {
        std::string body = req->ReadBody();
        UniValue s;
        if (!s.read(body) || !s.isObject()) {
            SetJSONHeaders(req, origin);
            req->WriteReply(HTTP_BAD_REQUEST, JsonError("invalid settings JSON"));
            return false;
        }
        if (!WriteSettings(s)) {
            SetJSONHeaders(req, origin);
            req->WriteReply(HTTP_INTERNAL_SERVER_ERROR, JsonError("failed to save settings"));
            return false;
        }
        SetJSONHeaders(req, origin);
        req->WriteReply(HTTP_OK, UniValue::stringify(s));
        return true;
    }

    req->WriteReply(HTTP_BAD_METHOD, "");
    return false;
}
