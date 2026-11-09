/*
 * The PC services' HTTP, JSON and text helpers (pc_http.h). The JSON reader and
 * the WinHTTP request were pc_lobby.cpp's; the lobby, mods, reports, friends and
 * updater share them now.
 */

#include "pc_http.h"

#include <algorithm>
#include <chrono>
#include <ctime>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#ifdef _WIN32
#include <windows.h>
#include <winhttp.h>
#include <bcrypt.h>
#endif

#ifndef GEVR_APP_VERSION
#define GEVR_APP_VERSION "0"
#endif

namespace pcnet {

/* ---- JSON: Dab's Mod's bounded reader (port/src/net/netlobby.c) ---- */

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

static bool jsonParseOpen(const std::string &text, char open, jspan *out)
{
    const char *e = text.data() + text.size();
    const char *p = jsonWs(text.data(), e);
    if (p >= e || *p != open) return false;
    const char *end = jsonSkip(p, e, 0);
    if (!end) return false;
    out->p = p;
    out->e = end;
    return true;
}

bool jsonParse(const std::string &text, jspan *out) { return jsonParseOpen(text, '{', out); }
bool jsonParseArray(const std::string &text, jspan *out) { return jsonParseOpen(text, '[', out); }

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

std::string jsonGetStr(jspan obj, const char *key, const char *def)
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

bool jsonGetBool(jspan obj, const char *key, bool def)
{
    jspan v;
    if (!jsonGet(obj, key, &v)) return def;
    if (v.e - v.p == 4 && !memcmp(v.p, "true", 4)) return true;
    if (v.e - v.p == 5 && !memcmp(v.p, "false", 5)) return false;
    std::string s;
    if (jsonStr(v, &s)) {   // org.json reads "true"/"false" strings too
        if (s == "true") return true;
        if (s == "false") return false;
    }
    return def;
}

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

/* ---- text ---- */

static std::string encodeWith(const std::string &in, const char *t)
{
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

std::string encode64(const std::string &in) { return encodeWith(in, "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_"); }
std::string encodeBase64(const std::string &in) { return encodeWith(in, "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/"); }

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

std::string narrow(const std::wstring &w)
{
#ifdef _WIN32
    const int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string s(n > 0 ? n - 1 : 0, '\0');
    if (n > 1) WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, &s[0], n, nullptr, nullptr);
    return s;
#else
    return std::string(w.begin(), w.end());
#endif
}

std::string upper(std::string s)
{
    for (char &c : s) if (c >= 'a' && c <= 'z') c = (char)(c - 32);
    return s;
}

/* ---- clocks and the build ---- */

long long nowMs()
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

long long wallMs()
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}

std::string isoUtc(long long ms)
{
    const time_t t = (time_t)(ms / 1000);
    struct tm tm;
#ifdef _WIN32
    gmtime_s(&tm, &t);
#else
    gmtime_r(&t, &tm);
#endif
    char buf[32];
    strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tm);
    return buf;
}

const char *appVersion() { return GEVR_APP_VERSION; }

std::string envOr(const char *name, const char *def)
{
    const char *v = getenv(name);
    return v && *v ? std::string(v) : std::string(def);
}

/* ---- HTTP(S): WinHTTP, after Dab's Mod's ghostnet.c ---- */

#ifdef _WIN32
static std::string winhttpReason(DWORD err)
{
    // HttpURLConnection's exceptions as LogReporter/ModManager.reason word them
    switch (err) {
    case ERROR_WINHTTP_NAME_NOT_RESOLVED: return "no internet connection";
    case ERROR_WINHTTP_TIMEOUT: return "the connection timed out";
    case ERROR_WINHTTP_CANNOT_CONNECT: return "could not connect";
    case ERROR_WINHTTP_CONNECTION_ERROR: return "the connection was reset";
    case ERROR_WINHTTP_SECURE_FAILURE: return "the secure connection failed";
    default: {
        char m[64];
        snprintf(m, sizeof(m), "network error %lu", (unsigned long)err);
        return m;
    }
    }
}
#endif

HttpReply httpSend(const HttpRequest &r)
{
    HttpReply reply;
#ifdef _WIN32
    const std::wstring wurl = wide(r.url);
    URL_COMPONENTS uc;
    memset(&uc, 0, sizeof(uc));
    uc.dwStructSize = sizeof(uc);
    wchar_t host[256] = L"";
    uc.lpszHostName = host;
    uc.dwHostNameLength = 256;
    uc.dwUrlPathLength = (DWORD)-1;
    uc.dwExtraInfoLength = (DWORD)-1;
    if (!WinHttpCrackUrl(wurl.c_str(), (DWORD)wurl.size(), 0, &uc) || !host[0]) throw HttpError{0, "bad address"};
    std::wstring path(uc.lpszUrlPath ? std::wstring(uc.lpszUrlPath, uc.dwUrlPathLength) : L"/");
    if (uc.lpszExtraInfo) path += std::wstring(uc.lpszExtraInfo, uc.dwExtraInfoLength);
    if (path.empty()) path = L"/";
    const bool secure = uc.nScheme == INTERNET_SCHEME_HTTPS;

    HINTERNET session = WinHttpOpen(wide(r.agent).c_str(), WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                    WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session) throw HttpError{0, "no network"};
    WinHttpSetTimeouts(session, r.connectMs, r.connectMs, r.readMs, r.readMs);
    HINTERNET connect = WinHttpConnect(session, host, uc.nPort, 0);
    HINTERNET request = nullptr;
    bool ok = false, stopped = false;
    if (connect) {
        request = WinHttpOpenRequest(connect, wide(r.method).c_str(), path.c_str(), nullptr, WINHTTP_NO_REFERER,
                                     WINHTTP_DEFAULT_ACCEPT_TYPES, secure ? WINHTTP_FLAG_SECURE : 0);
    }
    if (request) {
        std::string headers;
        for (const std::string &h : r.headers) headers += h + "\r\n";
        const std::wstring wh = wide(headers);
        if (WinHttpSendRequest(request, wh.empty() ? WINHTTP_NO_ADDITIONAL_HEADERS : wh.c_str(), wh.empty() ? 0 : (DWORD)-1,
                               r.body ? (LPVOID)r.body->data() : WINHTTP_NO_REQUEST_DATA,
                               r.body ? (DWORD)r.body->size() : 0, r.body ? (DWORD)r.body->size() : 0, 0)
            && WinHttpReceiveResponse(request, nullptr)) {
            DWORD status = 0, size = sizeof(status);
            WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                                WINHTTP_HEADER_NAME_BY_INDEX, &status, &size, WINHTTP_NO_HEADER_INDEX);
            reply.status = (int)status;
            wchar_t len[32];
            size = sizeof(len);
            if (WinHttpQueryHeaders(request, WINHTTP_QUERY_CONTENT_LENGTH, WINHTTP_HEADER_NAME_BY_INDEX,
                                    len, &size, WINHTTP_NO_HEADER_INDEX)) {
                reply.length = _wtoi64(len);
            }
            ok = true;
            std::vector<char> chunk(r.sink ? (1 << 16) : 4096);
            for (;;) {
                DWORD avail = 0, got = 0;
                if (!WinHttpQueryDataAvailable(request, &avail)) { ok = false; break; }
                if (avail == 0) break;
                if (avail > chunk.size()) avail = (DWORD)chunk.size();
                if (!WinHttpReadData(request, chunk.data(), avail, &got)) { ok = false; break; }
                if (got == 0) break;
                // a streamed body only while the answer is a success: an error page is kept as text
                if (r.sink && status < 400) {
                    if (!r.sink(chunk.data(), got)) { stopped = true; break; }
                } else if (reply.body.size() < r.maxReply) {
                    reply.body.append(chunk.data(), std::min<size_t>(got, r.maxReply - reply.body.size()));
                }
            }
        }
    }
    const DWORD err = ok ? 0 : GetLastError();
    if (request) WinHttpCloseHandle(request);
    if (connect) WinHttpCloseHandle(connect);
    WinHttpCloseHandle(session);
    if (stopped) throw HttpError{-1, "cancelled"};
    if (!ok) throw HttpError{0, winhttpReason(err)};
    return reply;
#else
    (void)r;
    throw HttpError{0, "no HTTPS on this build"};
#endif
}

std::string httpJson(const char *method, const std::string &url, const std::string *body, const std::string &token)
{
    HttpRequest r;
    r.method = method;
    r.url = url;
    r.body = body;
    r.headers.push_back("Accept: application/json");
    if (!token.empty()) r.headers.push_back("Authorization: Bearer " + token);
    if (body) r.headers.push_back("Content-Type: application/json");
    const HttpReply reply = httpSend(r);   // LobbyClient's 5 s connect and read
    jspan obj;
    const bool parsed = reply.body.empty() || jsonParse(reply.body, &obj);
    if (reply.status >= 400) {
        std::string e = parsed && !reply.body.empty() ? jsonGetStr(obj, "error") : std::string();
        if (e.empty()) e = "HTTP " + std::to_string(reply.status);
        throw HttpError{reply.status, e};
    }
    if (!parsed) throw HttpError{reply.status, "bad reply"};
    return reply.body.empty() ? std::string("{}") : reply.body;
}

std::string sha256File(const std::string &path)
{
#ifdef _WIN32
    FILE *f = _wfopen(wide(path).c_str(), L"rb");
    if (!f) return std::string();
    BCRYPT_ALG_HANDLE alg = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    unsigned char digest[32];
    bool ok = BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0) == 0
           && BCryptCreateHash(alg, &hash, nullptr, 0, nullptr, 0, 0) == 0;
    std::vector<unsigned char> buf(1 << 16);
    size_t n;
    while (ok && (n = fread(buf.data(), 1, buf.size(), f)) > 0) {
        ok = BCryptHashData(hash, buf.data(), (ULONG)n, 0) == 0;
    }
    ok = ok && !ferror(f) && BCryptFinishHash(hash, digest, sizeof(digest), 0) == 0;
    if (hash) BCryptDestroyHash(hash);
    if (alg) BCryptCloseAlgorithmProvider(alg, 0);
    fclose(f);
    if (!ok) return std::string();
    std::string hex;
    char two[3];
    for (unsigned char b : digest) { snprintf(two, sizeof(two), "%02x", b); hex += two; }
    return hex;
#else
    (void)path;
    return std::string();
#endif
}

}   // namespace pcnet
