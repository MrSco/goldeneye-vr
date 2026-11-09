/*
 * The internet lobby for the PC game (docs/pc-platforms.md P3): a port of the
 * Quest's android/app/src/main/java/com/gevr/port/LobbyClient.java.
 *
 * The launcher (port/vr/vr_launcher.cpp) sends it one-line commands and reads
 * one-line events, through pc_services.cpp as it reaches Java on the Quest:
 *
 *   create|visibility|name|version|stage|weapons|maxPlayers   CREATED|code|ownerToken
 *   resume|code|ownerToken|maxPlayers|phase|players[|name]     CREATED|code|ownerToken
 *   refresh|players|open[|maxPlayers]   phase|warmup|players   stop   leave
 *   list|version        LIST_BEGIN, LOBBY|code|name|stage|weapons|players|max|phase, LIST_END
 *   join|code|version   JOINED|joinId|user|credential
 *   offer|sdp64         answer|joinId|sdp64
 *   (polled)            HOST_PEER|id|offer64|user|credential, ANSWER|joinId|sdp64,
 *                       LOBBY_LOST|code|why, ERROR|message
 *
 * Same worker model: one thread runs the commands in order and polls every
 * 2 s (heartbeat every 15 s, join requests every 4 s while waiting and 10 s
 * in a match, an answer for a sent offer). HTTPS is WinHTTP, as Dab's Mod's
 * ghostnet.c does it; JSON is Dab's bounded span reader (netlobby.c) with
 * strings unescaped in full, since offers and answers are SDP.
 */

#include <string>
#include <deque>
#include <set>
#include <vector>
#include <algorithm>
#include <mutex>
#include <thread>
#include <condition_variable>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#ifdef _WIN32
#include <windows.h>
#include <winhttp.h>
#endif

extern "C" void sysLogPrintf(int level, const char *fmt, ...);

namespace {

const char *const kBase = "https://lobbies.goldeneyevr.com/v1/lobbies";

/* ---- JSON: Dab's Mod's bounded reader (port/src/net/netlobby.c) ---- */

struct jspan { const char *p, *e; };

const char *jsonWs(const char *p, const char *e)
{
    while (p < e && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')) p++;
    return p;
}

const char *jsonSkip(const char *p, const char *e, int depth)
{
    p = jsonWs(p, e);
    if (p >= e || depth > 16) return nullptr;
    if (*p == '"') {
        for (p++; p < e; p++) {
            if (*p == '\\') p++;
            else if (*p == '"') return p + 1;
        }
        return nullptr;
    }
    if (*p == '{' || *p == '[') {
        const char close = *p == '{' ? '}' : ']';
        p = jsonWs(p + 1, e);
        if (p < e && *p == close) return p + 1;
        while (p < e) {
            if (close == '}') {
                p = jsonSkip(p, e, depth + 1);
                if (!p) return nullptr;
                p = jsonWs(p, e);
                if (p >= e || *p != ':') return nullptr;
                p++;
            }
            p = jsonSkip(p, e, depth + 1);
            if (!p) return nullptr;
            p = jsonWs(p, e);
            if (p < e && *p == ',') { p++; continue; }
            if (p < e && *p == close) return p + 1;
            return nullptr;
        }
        return nullptr;
    }
    while (p < e && *p != ',' && *p != '}' && *p != ']' && *p != ' ' && *p != '\n' && *p != '\r' && *p != '\t') p++;
    return p;
}

bool jsonParse(const std::string &text, jspan *out)
{
    const char *e = text.data() + text.size();
    const char *p = jsonWs(text.data(), e);
    if (p >= e || *p != '{') return false;
    const char *end = jsonSkip(p, e, 0);
    if (!end) return false;
    out->p = p;
    out->e = end;
    return true;
}

bool jsonGet(jspan obj, const char *key, jspan *out)
{
    const char *p = obj.p, *e = obj.e;
    const size_t keylen = strlen(key);
    if (p >= e || *p != '{') return false;
    p = jsonWs(p + 1, e);
    while (p < e && *p == '"') {
        const char *k = p + 1;
        const char *kend = jsonSkip(p, e, 1);
        if (!kend) return false;
        p = jsonWs(kend, e);
        if (p >= e || *p != ':') return false;
        const char *v = jsonWs(p + 1, e);
        const char *vend = jsonSkip(v, e, 1);
        if (!vend) return false;
        if ((size_t)(kend - 1 - k) == keylen && memcmp(k, key, keylen) == 0) {
            out->p = v;
            out->e = vend;
            return true;
        }
        p = jsonWs(vend, e);
        if (p < e && *p == ',') p = jsonWs(p + 1, e);
        else break;
    }
    return false;
}

bool jsonAt(jspan arr, int index, jspan *out)
{
    const char *p = arr.p, *e = arr.e;
    int i = 0;
    if (p >= e || *p != '[') return false;
    p = jsonWs(p + 1, e);
    while (p < e && *p != ']') {
        const char *end = jsonSkip(p, e, 1);
        if (!end) return false;
        if (i++ == index) { out->p = p; out->e = end; return true; }
        p = jsonWs(end, e);
        if (p < e && *p == ',') p = jsonWs(p + 1, e);
    }
    return false;
}

bool jsonIsNull(jspan v) { return v.e - v.p == 4 && memcmp(v.p, "null", 4) == 0; }

/* A string in full (SDP keeps its \r\n), \uXXXX as UTF-8. */
bool jsonStr(jspan v, std::string *out)
{
    out->clear();
    const char *p = v.p;
    if (p >= v.e || *p != '"') return false;
    for (p++; p < v.e && *p != '"'; p++) {
        char c = *p;
        if (c == '\\' && p + 1 < v.e) {
            p++;
            switch (*p) {
            case 'n': c = '\n'; break;
            case 'r': c = '\r'; break;
            case 't': c = '\t'; break;
            case 'b': c = '\b'; break;
            case 'f': c = '\f'; break;
            case 'u': {
                unsigned cp = 0;
                for (int i = 0; i < 4 && p + 1 < v.e; i++) {
                    const char h = *++p;
                    cp = cp * 16 + (unsigned)(h >= '0' && h <= '9' ? h - '0' : (h | 32) - 'a' + 10);
                }
                if (cp < 0x80) { out->push_back((char)cp); }
                else if (cp < 0x800) { out->push_back((char)(0xc0 | (cp >> 6))); out->push_back((char)(0x80 | (cp & 0x3f))); }
                else { out->push_back((char)(0xe0 | (cp >> 12))); out->push_back((char)(0x80 | ((cp >> 6) & 0x3f))); out->push_back((char)(0x80 | (cp & 0x3f))); }
                continue;
            }
            default: c = *p; break;
            }
        }
        out->push_back(c);
    }
    return true;
}

long long jsonInt(jspan v, long long def)
{
    char buf[32];
    const long n = (long)(v.e - v.p);
    if (n <= 0 || n >= (long)sizeof(buf) || jsonIsNull(v)) return def;
    memcpy(buf, v.p, n);
    buf[n] = '\0';
    if (!strcmp(buf, "true")) return 1;
    if (!strcmp(buf, "false")) return 0;
    if (!((buf[0] >= '0' && buf[0] <= '9') || buf[0] == '-')) return def;
    return (long long)strtod(buf, nullptr);
}

std::string jsonGetStr(jspan obj, const char *key, const char *def = "")
{
    jspan v;
    std::string s;
    if (jsonGet(obj, key, &v) && jsonStr(v, &s)) return s;
    return def;
}

long long jsonGetInt(jspan obj, const char *key, long long def)
{
    jspan v;
    return jsonGet(obj, key, &v) ? jsonInt(v, def) : def;
}

/* A JSON string literal */
std::string q(const std::string &s)
{
    std::string out = "\"";
    for (unsigned char c : s) {
        switch (c) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:
            if (c < 0x20) { char u[8]; snprintf(u, sizeof(u), "\\u%04x", c); out += u; }
            else out.push_back((char)c);
        }
    }
    return out + "\"";
}

/* ---- Base64, URL-safe with padding (android.util.Base64 NO_WRAP | URL_SAFE) ---- */

std::string encode64(const std::string &in)
{
    static const char *t = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
    std::string out;
    size_t i = 0;
    for (; i + 2 < in.size(); i += 3) {
        const unsigned v = ((unsigned char)in[i] << 16) | ((unsigned char)in[i + 1] << 8) | (unsigned char)in[i + 2];
        out += t[v >> 18]; out += t[(v >> 12) & 63]; out += t[(v >> 6) & 63]; out += t[v & 63];
    }
    if (i + 1 == in.size()) {
        const unsigned v = (unsigned char)in[i] << 16;
        out += t[v >> 18]; out += t[(v >> 12) & 63]; out += "==";
    } else if (i + 2 == in.size()) {
        const unsigned v = ((unsigned char)in[i] << 16) | ((unsigned char)in[i + 1] << 8);
        out += t[v >> 18]; out += t[(v >> 12) & 63]; out += t[(v >> 6) & 63]; out += '=';
    }
    return out;
}

std::string decode64(const std::string &in)
{
    std::string out;
    unsigned v = 0;
    int bits = 0;
    for (char c : in) {
        int d;
        if (c >= 'A' && c <= 'Z') d = c - 'A';
        else if (c >= 'a' && c <= 'z') d = c - 'a' + 26;
        else if (c >= '0' && c <= '9') d = c - '0' + 52;
        else if (c == '-' || c == '+') d = 62;
        else if (c == '_' || c == '/') d = 63;
        else continue;   // '=' and anything else
        v = (v << 6) | (unsigned)d;
        bits += 6;
        if (bits >= 8) { bits -= 8; out.push_back((char)((v >> bits) & 0xff)); }
    }
    return out;
}

std::string clean(std::string s)
{
    for (char &c : s) if (c == '|' || c == '\n' || c == '\r' || c == '\t') c = ' ';
    return s;
}

std::vector<std::string> splitBar(const std::string &raw)
{
    std::vector<std::string> f;
    size_t start = 0;
    for (;;) {
        const size_t bar = raw.find('|', start);
        f.push_back(raw.substr(start, bar == std::string::npos ? std::string::npos : bar - start));
        if (bar == std::string::npos) return f;
        start = bar + 1;
    }
}

/* ---- HTTPS: WinHTTP, after Dab's Mod's ghostnet.c ---- */

struct HttpError { int status; std::string message; };

std::wstring wide(const std::string &s)
{
#ifdef _WIN32
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    std::wstring w(n > 0 ? n - 1 : 0, L'\0');
    if (n > 1) MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, &w[0], n);
    return w;
#else
    return std::wstring(s.begin(), s.end());
#endif
}

/* The reply's JSON object; throws HttpError as LobbyClient.http does (status >= 400,
 * the reply's "error" when it has one, status 0 for a network failure). */
std::string http(const char *method, const std::string &url, const std::string *body, const std::string &token)
{
#ifdef _WIN32
    // https://host/path?query: the addresses are this file's own
    const size_t scheme = url.find("://");
    const size_t slash = scheme == std::string::npos ? std::string::npos : url.find('/', scheme + 3);
    if (slash == std::string::npos) throw HttpError{0, "bad address"};
    const std::wstring host = wide(url.substr(scheme + 3, slash - scheme - 3));
    const std::wstring path = wide(url.substr(slash));
    const bool secure = url.compare(0, 8, "https://") == 0;
    HINTERNET session = WinHttpOpen(L"GoldenEyeVR-PC", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                    WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session) throw HttpError{0, "no network"};
    WinHttpSetTimeouts(session, 5000, 5000, 5000, 5000);   // LobbyClient's 5 s connect and read
    HINTERNET connect = WinHttpConnect(session, host.c_str(),
                                       secure ? INTERNET_DEFAULT_HTTPS_PORT : INTERNET_DEFAULT_HTTP_PORT, 0);
    HINTERNET request = nullptr;
    std::string reply;
    DWORD status = 0;
    bool ok = false;
    if (connect) {
        request = WinHttpOpenRequest(connect, wide(method).c_str(), path.c_str(), nullptr, WINHTTP_NO_REFERER,
                                     WINHTTP_DEFAULT_ACCEPT_TYPES, secure ? WINHTTP_FLAG_SECURE : 0);
    }
    if (request) {
        std::string headers = "Accept: application/json\r\n";
        if (!token.empty()) headers += "Authorization: Bearer " + token + "\r\n";
        if (body) headers += "Content-Type: application/json\r\n";
        const std::wstring wh = wide(headers);
        if (WinHttpSendRequest(request, wh.c_str(), (DWORD)-1,
                               body ? (LPVOID)body->data() : WINHTTP_NO_REQUEST_DATA,
                               body ? (DWORD)body->size() : 0, body ? (DWORD)body->size() : 0, 0)
            && WinHttpReceiveResponse(request, nullptr)) {
            DWORD size = sizeof(status);
            WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                                WINHTTP_HEADER_NAME_BY_INDEX, &status, &size, WINHTTP_NO_HEADER_INDEX);
            ok = true;
            for (;;) {
                DWORD avail = 0, got = 0;
                char chunk[4096];
                if (!WinHttpQueryDataAvailable(request, &avail) || avail == 0) break;
                if (avail > sizeof(chunk)) avail = sizeof(chunk);
                if (!WinHttpReadData(request, chunk, avail, &got) || got == 0) break;
                if (reply.size() < 65536) reply.append(chunk, got);   // LobbyClient's cap
            }
        }
    }
    const DWORD err = ok ? 0 : GetLastError();
    if (request) WinHttpCloseHandle(request);
    if (connect) WinHttpCloseHandle(connect);
    WinHttpCloseHandle(session);
    if (!ok) {
        char m[64];
        snprintf(m, sizeof(m), "network error %lu", (unsigned long)err);
        throw HttpError{0, m};
    }
    jspan obj;
    const bool parsed = reply.empty() || jsonParse(reply, &obj);
    if (status >= 400) {
        std::string e = parsed && !reply.empty() ? jsonGetStr(obj, "error") : std::string();
        if (e.empty()) e = "HTTP " + std::to_string(status);
        throw HttpError{(int)status, e};
    }
    if (!parsed) throw HttpError{(int)status, "bad reply"};
    return reply.empty() ? std::string("{}") : reply;
#else
    (void)method; (void)url; (void)body; (void)token;
    throw HttpError{0, "no HTTPS on this build"};
#endif
}

/* ---- LobbyClient ---- */

using Clock = std::chrono::steady_clock;
long long nowMs() { return std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now().time_since_epoch()).count(); }

class LobbyClient {
public:
    LobbyClient() : worker_(&LobbyClient::run, this) {}

    std::string event()
    {
        std::lock_guard<std::mutex> lock(eventsLock_);
        if (events_.empty()) return std::string();
        std::string e = events_.front();
        events_.pop_front();
        return e;
    }

    void command(const std::string &raw)
    {
        std::lock_guard<std::mutex> lock(cmdLock_);
        commands_.push_back(raw);
        cmdWake_.notify_one();
    }

    /* Wait behind queued commands for the lobby's removal, up to ms (the app is quitting). */
    void stopAndWait(int ms)
    {
        command("stop");
        const long long until = nowMs() + ms;
        for (;;) {
            {
                std::lock_guard<std::mutex> lock(cmdLock_);
                if (commands_.empty() && !busy_) break;
            }
            if (nowMs() >= until) {
                sysLogPrintf(1, "lobby: removal not confirmed within %d ms", ms);
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
    }

private:
    void push(const std::string &e)
    {
        std::lock_guard<std::mutex> lock(eventsLock_);
        events_.push_back(e);
    }

    void error(const std::string &message)
    {
        sysLogPrintf(1, "lobby: %s", message.c_str());
        push("ERROR|" + clean(message));
    }

    void run()
    {
        long long nextPoll = nowMs() + 2000;
        for (;;) {
            std::string cmd;
            {
                std::unique_lock<std::mutex> lock(cmdLock_);
                cmdWake_.wait_for(lock, std::chrono::milliseconds(std::max(0LL, nextPoll - nowMs())),
                                  [this] { return !commands_.empty(); });
                if (!commands_.empty()) {
                    cmd = commands_.front();
                    commands_.pop_front();
                    busy_ = true;
                }
            }
            if (!cmd.empty()) {
                try { execute(cmd); }
                catch (const HttpError &e) { error("Lobby service: " + e.message); }
                catch (...) { error("Lobby service: failed"); }
                std::lock_guard<std::mutex> lock(cmdLock_);
                busy_ = false;
                continue;
            }
            if (nowMs() >= nextPoll) {
                poll();
                nextPoll = nowMs() + 2000;
            }
        }
    }

    void execute(const std::string &raw)
    {
        const std::vector<std::string> f = splitBar(raw);
        const std::string &c = f[0];
        if (c == "create") {
            if (f.size() != 7) return;
            stop();
            name_ = f[2];
            lastKeepalive_ = nowMs();
            players_ = 1;
            maxPlayers_ = atoi(f[6].c_str());
            open_ = true;
            phase_ = "waiting";
            pendingCreate_ = "{\"visibility\":" + q(f[1]) + ",\"name\":" + q(f[2]) + ",\"version\":" + std::to_string(atoi(f[3].c_str()))
                + ",\"stage\":" + std::to_string(atoi(f[4].c_str())) + ",\"weapons\":" + std::to_string(atoi(f[5].c_str()))
                + ",\"maxPlayers\":" + std::to_string(atoi(f[6].c_str())) + "}";
            registerPending();
        } else if (c == "stop") {
            stop();
        } else if (c == "leave") {
            forget();   // a host leaving players behind: the lobby stays for the one they elect
        } else if (c == "resume") {
            if (f.size() != 6 && f.size() != 7) return;
            pendingCreate_.clear();
            code_ = f[1];
            ownerToken_ = f[2];
            name_ = f.size() == 7 ? f[6] : "";
            lastKeepalive_ = nowMs();
            heartbeatPaused_ = false;
            maxPlayers_ = atoi(f[3].c_str());
            phase_ = f[4];
            players_ = atoi(f[5].c_str());
            open_ = players_ < maxPlayers_;
            joinId_.clear();
            joinToken_.clear();
            offerSent_ = answerReceived_ = false;
            hosting_ = true;
            nextHeartbeat_ = nextRequests_ = 0;
            seenRequests_.clear();
            push("CREATED|" + code_ + "|" + ownerToken_);
        } else if (c == "refresh") {
            lastKeepalive_ = nowMs();
            if (f.size() == 3 || f.size() == 4) {
                const int newPlayers = atoi(f[1].c_str());
                const bool newOpen = f[2] == "1";
                const int newMax = f.size() == 4 ? atoi(f[3].c_str()) : maxPlayers_;
                if (newPlayers != players_ || newOpen != open_ || newMax != maxPlayers_) {
                    players_ = newPlayers;
                    open_ = newOpen;
                    maxPlayers_ = newMax;
                    nextHeartbeat_ = 0;
                }
            }
        } else if (c == "phase") {
            lastKeepalive_ = nowMs();
            if (f.size() == 3 && (f[1] == "warmup" || f[1] == "in_progress")) {
                phase_ = f[1];
                players_ = atoi(f[2].c_str());
                open_ = players_ < maxPlayers_;
                nextHeartbeat_ = nextRequests_ = 0;
            }
        } else if (c == "list") {
            const int version = atoi(f.size() > 1 ? f[1].c_str() : "0");
            const std::string reply = http("GET", std::string(kBase) + "?version=" + std::to_string(version), nullptr, "");
            jspan obj, rows, row;
            jsonParse(reply, &obj);
            push("LIST_BEGIN");
            int listed = 0;
            if (jsonGet(obj, "lobbies", &rows)) {
                for (int i = 0; jsonAt(rows, i, &row); i++, listed++) {
                    push("LOBBY|" + jsonGetStr(row, "code") + "|" + clean(jsonGetStr(row, "name")) + "|"
                         + std::to_string(jsonGetInt(row, "stage", 0)) + "|" + std::to_string(jsonGetInt(row, "weapons", 0)) + "|"
                         + std::to_string(jsonGetInt(row, "players", 0)) + "|" + std::to_string(jsonGetInt(row, "maxPlayers", 0)) + "|"
                         + jsonGetStr(row, "phase", "waiting"));
                }
            }
            push("LIST_END");
            sysLogPrintf(0, "lobby: %d open games (version %d)", listed, version);
        } else if (c == "join") {
            if (f.size() != 3) return;
            joinId_.clear();
            joinToken_.clear();
            offerSent_ = answerReceived_ = false;
            std::string requested = f[1];
            while (!requested.empty() && requested.back() == ' ') requested.pop_back();
            while (!requested.empty() && requested.front() == ' ') requested.erase(0, 1);
            for (char &ch : requested) ch = (char)toupper((unsigned char)ch);
            const std::string body = "{\"version\":" + std::to_string(atoi(f[2].c_str())) + "}";
            const std::string reply = http("POST", std::string(kBase) + "/" + requested + "/joins", &body, "");
            jspan obj;
            jsonParse(reply, &obj);
            joinId_ = jsonGetStr(obj, "id");
            joinToken_ = jsonGetStr(obj, "joinToken");
            code_ = requested;
            push("JOINED|" + joinId_ + "|" + relayCredentials("{\"id\":" + q(joinId_) + "}", joinToken_));
        } else if (c == "offer") {
            if (f.size() != 2 || joinId_.empty()) return;
            const std::string body = "{\"sdp\":" + q(decode64(f[1])) + "}";
            http("PUT", std::string(kBase) + "/" + code_ + "/joins/" + joinId_ + "/offer", &body, joinToken_);
            offerSent_ = true;
        } else if (c == "answer") {
            if (f.size() != 3 || !hosting_) return;
            const std::string body = "{\"sdp\":" + q(decode64(f[2])) + "}";
            http("PUT", std::string(kBase) + "/" + code_ + "/joins/" + f[1] + "/answer", &body, ownerToken_);
        }
    }

    void poll()
    {
        try {
            const long long now = nowMs();
            const bool keepalive = now - lastKeepalive_ <= 60000;
            if (hosting_ || !pendingCreate_.empty()) {
                if (!keepalive && !heartbeatPaused_) {
                    sysLogPrintf(0, "lobby: heartbeat paused for %s: no keepalive for 60 s", code_.c_str());
                    heartbeatPaused_ = true;
                } else if (keepalive && heartbeatPaused_) {
                    sysLogPrintf(0, "lobby: heartbeat resumed for %s", code_.c_str());
                    heartbeatPaused_ = false;
                    nextHeartbeat_ = nextRequests_ = 0;
                }
            }
            if (!pendingCreate_.empty() && keepalive) registerPending();
            if (hosting_ && !code_.empty()) {
                if (!keepalive) return;
                if (now >= nextHeartbeat_) {
                    std::string state = "{\"players\":" + std::to_string(players_) + ",\"open\":" + (open_ ? "true" : "false")
                        + ",\"phase\":" + q(phase_) + ",\"maxPlayers\":" + std::to_string(maxPlayers_);
                    if (!name_.empty()) state += ",\"name\":" + q(name_);
                    state += "}";
                    try { http("PUT", std::string(kBase) + "/" + code_, &state, ownerToken_); }
                    catch (const HttpError &e) { if (lost(e)) return; throw; }
                    nextHeartbeat_ = nowMs() + 15000;
                }
                if (open_ && nowMs() >= nextRequests_) {
                    std::string reply;
                    try { reply = http("GET", std::string(kBase) + "/" + code_ + "/joins", nullptr, ownerToken_); }
                    catch (const HttpError &e) { if (lost(e)) return; throw; }
                    nextRequests_ = nowMs() + (phase_ == "waiting" ? 4000 : 10000);
                    jspan obj, reqs, r;
                    jsonParse(reply, &obj);
                    if (jsonGet(obj, "requests", &reqs)) {
                        for (int i = 0; jsonAt(reqs, i, &r); i++) {
                            const std::string id = jsonGetStr(r, "id");
                            if (id.empty() || seenRequests_.count(id)) continue;
                            push("HOST_PEER|" + id + "|" + encode64(jsonGetStr(r, "offer")) + "|"
                                 + relayCredentials("{}", ownerToken_));
                            seenRequests_.insert(id);
                        }
                    }
                }
            } else if (!joinId_.empty() && offerSent_ && !answerReceived_) {
                const std::string reply = http("GET", std::string(kBase) + "/" + code_ + "/joins/" + joinId_ + "/answer", nullptr, joinToken_);
                jspan obj, answer;
                jsonParse(reply, &obj);
                if (jsonGet(obj, "answer", &answer) && !jsonIsNull(answer)) {
                    std::string sdp;
                    jsonStr(answer, &sdp);
                    push("ANSWER|" + joinId_ + "|" + encode64(sdp));
                    answerReceived_ = true;
                }
            }
            lastError_.clear();
        } catch (const HttpError &e) {
            const std::string message = "Lobby service: " + e.message;
            if (message != lastError_) error(message);
            lastError_ = message;
        }
    }

    /* TURN is a fallback: a refused relay leaves "|" and ICE gathers STUN only. */
    std::string relayCredentials(const std::string &body, const std::string &token)
    {
        try {
            const std::string reply = http("POST", std::string(kBase) + "/" + code_ + "/turn", &body, token);
            jspan obj;
            jsonParse(reply, &obj);
            return clean(jsonGetStr(obj, "username")) + "|" + clean(jsonGetStr(obj, "credential"));
        } catch (const HttpError &e) {
            sysLogPrintf(1, "lobby: relay credentials unavailable; direct connection only: %s", e.message.c_str());
            return "|";
        }
    }

    void registerPending()
    {
        if (pendingCreate_.empty()) return;
        const std::string body = pendingCreate_;
        const std::string reply = http("POST", kBase, &body, "");
        jspan obj;
        jsonParse(reply, &obj);
        code_ = jsonGetStr(obj, "code");
        ownerToken_ = jsonGetStr(obj, "ownerToken");
        pendingCreate_.clear();
        hosting_ = true;
        nextHeartbeat_ = nextRequests_ = 0;
        seenRequests_.clear();
        push("CREATED|" + code_ + "|" + ownerToken_);
    }

    void stop()
    {
        pendingCreate_.clear();
        if (hosting_ && !code_.empty()) {
            try {
                http("DELETE", std::string(kBase) + "/" + code_, nullptr, ownerToken_);
                sysLogPrintf(0, "lobby: removed %s", code_.c_str());
            } catch (const HttpError &e) {
                if (e.status == 404 || e.status == 410) sysLogPrintf(0, "lobby: %s already gone (HTTP %d)", code_.c_str(), e.status);
                else sysLogPrintf(1, "lobby: failed to remove %s: %s", code_.c_str(), e.message.c_str());
            }
        }
        forget();
    }

    void forget()
    {
        pendingCreate_.clear();
        hosting_ = false;
        phase_ = "waiting";
        code_.clear();
        ownerToken_.clear();
        name_.clear();
        lastKeepalive_ = 0;
        heartbeatPaused_ = false;
        joinId_.clear();
        joinToken_.clear();
        offerSent_ = answerReceived_ = false;
        seenRequests_.clear();
    }

    bool lost(const HttpError &e)
    {
        if (!hosting_ || (e.status != 404 && e.status != 410)) return false;
        const std::string lostCode = code_;
        sysLogPrintf(0, "lobby: %s is gone (HTTP %d: %s)", lostCode.c_str(), e.status, e.message.c_str());
        forget();
        push("LOBBY_LOST|" + lostCode + "|" + clean(e.message));
        return true;
    }

    std::mutex eventsLock_, cmdLock_;
    std::condition_variable cmdWake_;
    std::deque<std::string> events_, commands_;
    bool busy_ = false;
    std::set<std::string> seenRequests_;
    std::string code_, ownerToken_, name_, joinId_, joinToken_, phase_ = "waiting", lastError_, pendingCreate_;
    bool hosting_ = false, offerSent_ = false, answerReceived_ = false, open_ = true, heartbeatPaused_ = false;
    int players_ = 1, maxPlayers_ = 4;
    long long nextHeartbeat_ = 0, nextRequests_ = 0, lastKeepalive_ = 0;
    std::thread worker_;   // last: starts once the state above exists
};

LobbyClient *s_lobby;

void lobbyAtExit(void)
{
    // MainActivity.onDestroy's lobbies.shutdown(): take the hosted lobby down
    if (s_lobby) s_lobby->stopAndWait(1500);
}

LobbyClient &lobby()
{
    if (!s_lobby) {
        s_lobby = new LobbyClient();   // lives as long as the process, as the Quest's
        atexit(lobbyAtExit);
    }
    return *s_lobby;
}

}   // namespace

std::string pcLobbyEvent(void) { return s_lobby ? s_lobby->event() : std::string(); }
void pcLobbyCommand(const char *cmd) { lobby().command(cmd ? cmd : ""); }
