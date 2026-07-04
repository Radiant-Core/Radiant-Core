// Copyright (c) 2025-present The Avian Core developers
// Copyright (c) 2025-present The Radiant Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://opensource.org/license/mit/.

#include <webui/webui_internal.h>
#include <webui/webui.h>

#include <crypto/hmac_sha256.h>
#include <httpserver.h>
#include <logging.h>
#include <random.h>
#include <fs.h>
#include <util/strencodings.h>
#include <util/system.h>
#include <util/time.h>
#include <univalue.h>

#include <chrono>
#include <cstring>
#include <fstream>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <vector>

// File-local session state — not shared across translation units.
static std::mutex s_webui_session_mutex;
static std::set<std::string> s_webui_session_tokens;
static std::string s_webui_password_hash;
static std::vector<unsigned char> s_webui_password_salt;

// PBKDF2-HMAC-SHA256, single output block. Deliberately limited iterations to
// stay fast enough on low-power hardware (Raspberry Pi etc.).
static constexpr uint32_t WEBUI_PBKDF2_ITERATIONS{100000};

static void Pbkdf2HmacSha256(const std::string &password,
                              const std::vector<unsigned char> &salt,
                              uint32_t iterations,
                              unsigned char out[CHMAC_SHA256::OUTPUT_SIZE])
{
    unsigned char block[4]{0, 0, 0, 1};
    unsigned char u[CHMAC_SHA256::OUTPUT_SIZE];
    CHMAC_SHA256(reinterpret_cast<const uint8_t *>(password.data()), password.size())
        .Write(salt.data(), salt.size())
        .Write(block, sizeof(block))
        .Finalize(u);
    unsigned char t[CHMAC_SHA256::OUTPUT_SIZE];
    std::memcpy(t, u, sizeof(t));
    for (uint32_t i = 1; i < iterations; ++i) {
        CHMAC_SHA256(reinterpret_cast<const uint8_t *>(password.data()), password.size())
            .Write(u, sizeof(u))
            .Finalize(u);
        for (size_t k = 0; k < sizeof(t); ++k) t[k] ^= u[k];
    }
    std::memcpy(out, t, sizeof(t));
}

bool InitWebUIAuth()
{
    const std::string pw = gArgs.GetArg("-webuipassword", "");
    if (!pw.empty()) {
        unsigned char salt[16];
        GetRandBytes(salt, sizeof(salt));
        s_webui_password_salt.assign(salt, salt + sizeof(salt));
        unsigned char digest[CHMAC_SHA256::OUTPUT_SIZE];
        Pbkdf2HmacSha256(pw, s_webui_password_salt, WEBUI_PBKDF2_ITERATIONS, digest);
        s_webui_password_hash = HexStr(digest);
        g_webui_use_password = true;
        LogPrintf("WebUI: using configured password authentication\n");
        return true;
    }

    // Cookie mode: generate random token and write to disk.
    unsigned char rand_bytes[32];
    GetRandBytes(rand_bytes, sizeof(rand_bytes));
    g_webui_token = HexStr(rand_bytes);

    const fs::path filepath     = GetDataDir(true) / fs::path(WEBUI_COOKIE_FILE);
    const fs::path filepath_tmp = fs::path(filepath).concat(".tmp");

    std::ofstream file(filepath_tmp);
    if (!file.is_open()) {
        LogPrintf("WebUI: Unable to open cookie file %s for writing\n", filepath_tmp.string());
        return false;
    }
    file << g_webui_token;
    file.close();

    // On Windows, MoveFileExA(MOVEFILE_REPLACE_EXISTING) can fail when the
    // destination is briefly locked by AV/security software immediately after
    // a previous run wrote it.  Retry up to 5 times with a short sleep so the
    // scanner has time to release the handle; the cookie is regenerated on every
    // startup so the content of any stale file does not matter.
    {
        bool renamed = RenameOver(filepath_tmp, filepath);
        for (int attempt = 1; !renamed && attempt <= 5; ++attempt) {
            std::error_code remove_ec;
            fs::remove(filepath, remove_ec);
            MilliSleep(100 * attempt);   // 100 ms, 200 ms, 300 ms …
            renamed = RenameOver(filepath_tmp, filepath);
        }
        if (!renamed) {
            LogPrintf("WebUI: Unable to rename cookie file to %s\n", filepath.string());
            std::error_code cleanup_ec;
            fs::remove(filepath_tmp, cleanup_ec);
            return false;
        }
    }
    std::error_code ec;
    fs::permissions(filepath,
                    fs::perms::owner_read | fs::perms::owner_write,
                    fs::perm_options::replace, ec);
    if (ec) {
        LogPrintf("WebUI: Unable to set permissions on cookie file: %s\n", ec.message());
    }

    g_webui_cookie_generated = true;
    LogPrintf("Generated WebUI authentication cookie %s\n", filepath.string());
    return true;
}

// DNS-rebinding protection: reject requests whose Host header doesn't match
// an address we're actually bound to.
bool CheckWebUIHost(HTTPRequest *req)
{
    const auto hostOpt = req->GetHeader("host");
    if (!hostOpt || hostOpt->empty()) {
        req->WriteReply(HTTP_FORBIDDEN, R"({"error":"Missing Host header"})");
        return false;
    }
    std::string hostname = *hostOpt;
    if (!hostname.empty() && hostname.front() == '[') {
        const size_t close = hostname.find(']');
        hostname = (close != std::string::npos) ? hostname.substr(1, close - 1) : hostname;
    } else {
        const size_t colon = hostname.rfind(':');
        if (colon != std::string::npos) hostname = hostname.substr(0, colon);
    }
    if (g_webui_allowed_hosts.count(hostname)) return true;
    req->WriteReply(HTTP_FORBIDDEN, R"({"error":"Host not allowed"})");
    return false;
}

bool CheckWebUIToken(const std::string &submitted)
{
    bool ok = false;
    if (g_webui_use_password) {
        std::lock_guard<std::mutex> lock(s_webui_session_mutex);
        for (const std::string &token : s_webui_session_tokens) {
            if (TimingResistantEqual(submitted, token)) { ok = true; break; }
        }
    } else {
        ok = TimingResistantEqual(submitted, g_webui_token);
    }
    if (!ok) UninterruptibleSleep(std::chrono::milliseconds{250});
    return ok;
}

bool CheckWebUIPassword(const std::string &submitted)
{
    unsigned char digest[CHMAC_SHA256::OUTPUT_SIZE];
    Pbkdf2HmacSha256(submitted, s_webui_password_salt, WEBUI_PBKDF2_ITERATIONS, digest);
    const bool ok = TimingResistantEqual(HexStr(digest), s_webui_password_hash);
    if (!ok) UninterruptibleSleep(std::chrono::milliseconds{250});
    return ok;
}

std::string CreateWebUISessionToken()
{
    unsigned char rand_bytes[32];
    GetRandBytes(rand_bytes, sizeof(rand_bytes));
    const std::string token = HexStr(rand_bytes);
    std::lock_guard<std::mutex> lock(s_webui_session_mutex);
    s_webui_session_tokens.insert(token);
    return token;
}

void RevokeWebUISessionToken(const std::string &token)
{
    std::lock_guard<std::mutex> lock(s_webui_session_mutex);
    s_webui_session_tokens.erase(token);
}

// Single-use SSE ticket system (see webui_internal.h for rationale).
static std::mutex s_sse_ticket_mutex;
static std::map<std::string, std::chrono::steady_clock::time_point> s_sse_tickets;
static constexpr std::chrono::seconds WEBUI_SSE_TICKET_TTL{30};

static void PruneExpiredSSETickets()
{
    const auto now = std::chrono::steady_clock::now();
    for (auto it = s_sse_tickets.begin(); it != s_sse_tickets.end();) {
        it = (it->second < now) ? s_sse_tickets.erase(it) : std::next(it);
    }
}

static std::string CreateWebUISSETicket()
{
    unsigned char rand_bytes[32];
    GetRandBytes(rand_bytes, sizeof(rand_bytes));
    const std::string ticket = HexStr(rand_bytes);
    std::lock_guard<std::mutex> lock(s_sse_ticket_mutex);
    PruneExpiredSSETickets();
    s_sse_tickets.emplace(ticket, std::chrono::steady_clock::now() + WEBUI_SSE_TICKET_TTL);
    return ticket;
}

bool ConsumeWebUISSETicket(const std::string &ticket)
{
    std::lock_guard<std::mutex> lock(s_sse_ticket_mutex);
    PruneExpiredSSETickets();
    return s_sse_tickets.erase(ticket) > 0;
}

bool CheckWebUIAuth(HTTPRequest *req)
{
    const auto authOpt = req->GetHeader("authorization");
    if (!authOpt || authOpt->substr(0, 7) != "Bearer ") {
        req->WriteHeader("WWW-Authenticate", "Bearer realm=\"radiantd-webui\"");
        req->WriteHeader("Content-Type", "application/json");
        req->WriteReply(HTTP_UNAUTHORIZED, R"({"error":"unauthorized"})");
        return false;
    }
    if (!CheckWebUIToken(authOpt->substr(7))) {
        req->WriteHeader("WWW-Authenticate", "Bearer realm=\"radiantd-webui\"");
        req->WriteHeader("Content-Type", "application/json");
        req->WriteReply(HTTP_UNAUTHORIZED, R"({"error":"unauthorized"})");
        return false;
    }
    return true;
}

std::optional<std::string> CheckWebUICORS(HTTPRequest *req)
{
    const auto originOpt = req->GetHeader("origin");
    if (!originOpt) return std::string{};

    std::string host = *originOpt;
    if (host.substr(0, 7) == "http://")  host = host.substr(7);
    if (host.substr(0, 8) == "https://") host = host.substr(8);
    if (!host.empty() && host.front() == '[') {
        size_t close = host.find(']');
        host = (close != std::string::npos) ? host.substr(1, close - 1) : host;
    } else {
        size_t colon = host.rfind(':');
        if (colon != std::string::npos) host = host.substr(0, colon);
    }
    if (g_webui_allowed_hosts.count(host)) return *originOpt;

    req->WriteReply(HTTP_FORBIDDEN, R"({"error":"CORS: origin not allowed"})");
    return std::nullopt;
}

void SetJSONHeaders(HTTPRequest *req, const std::string &allowed_origin)
{
    req->WriteHeader("Content-Type", "application/json");
    if (!allowed_origin.empty()) {
        req->WriteHeader("Access-Control-Allow-Origin", allowed_origin);
    }
}

bool HandleAuthLogin(HTTPRequest *req)
{
    if (req->GetRequestMethod() != HTTPRequest::POST) { req->WriteReply(HTTP_BAD_METHOD, ""); return false; }
    if (!CheckWebUIHost(req)) return false;
    auto cors = CheckWebUICORS(req);
    if (!cors) return false;

    if (!g_webui_use_password) {
        req->WriteReply(HTTP_BAD_REQUEST, R"({"error":"Password authentication is not enabled"})");
        return false;
    }
    UniValue body;
    if (!body.read(req->ReadBody()) || !body.isObject()) {
        req->WriteReply(HTTP_BAD_REQUEST, R"({"error":"Invalid request body"})");
        return false;
    }
    const UniValue &passVal = body["password"];
    if (!passVal.isStr() || passVal.get_str().empty()) {
        req->WriteReply(HTTP_BAD_REQUEST, R"({"error":"\"password\" field required"})");
        return false;
    }
    if (!CheckWebUIPassword(passVal.get_str())) {
        req->WriteHeader("WWW-Authenticate", "Bearer realm=\"radiantd-webui\"");
        req->WriteReply(HTTP_UNAUTHORIZED, R"({"error":"unauthorized"})");
        return false;
    }
    UniValue::Object obj;
    obj.emplace_back("success", UniValue(true));
    obj.emplace_back("token",   UniValue(CreateWebUISessionToken()));
    SetJSONHeaders(req, *cors);
    req->WriteReply(HTTP_OK, UniValue::stringify(obj));
    return true;
}

bool HandleAuthLogout(HTTPRequest *req)
{
    if (req->GetRequestMethod() != HTTPRequest::POST) { req->WriteReply(HTTP_BAD_METHOD, ""); return false; }
    if (!CheckWebUIHost(req)) return false;
    auto cors = CheckWebUICORS(req);
    if (!cors) return false;
    if (!CheckWebUIAuth(req)) return false;

    if (g_webui_use_password) {
        const auto authOpt = req->GetHeader("authorization");
        if (authOpt && authOpt->substr(0, 7) == "Bearer ") {
            RevokeWebUISessionToken(authOpt->substr(7));
        }
    }
    UniValue::Object obj;
    obj.emplace_back("success", UniValue(true));
    SetJSONHeaders(req, *cors);
    req->WriteReply(HTTP_OK, UniValue::stringify(obj));
    return true;
}

bool HandleSSETicket(HTTPRequest *req)
{
    if (req->GetRequestMethod() != HTTPRequest::POST) { req->WriteReply(HTTP_BAD_METHOD, ""); return false; }
    if (!CheckWebUIHost(req)) return false;
    auto cors = CheckWebUICORS(req);
    if (!cors) return false;
    if (!CheckWebUIAuth(req)) return false;

    UniValue::Object obj;
    obj.emplace_back("ticket", UniValue(CreateWebUISSETicket()));
    SetJSONHeaders(req, *cors);
    req->WriteReply(HTTP_OK, UniValue::stringify(obj));
    return true;
}
