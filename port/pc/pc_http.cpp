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
#elif !defined(GEVR_IOS)
#include <curl/curl.h>   // Linux: libcurl in WinHTTP's place
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

#ifdef GEVR_IOS
HttpReply httpSendApple(const HttpRequest &r);   // NSURLSession (port/ios/ios_http.mm)
#endif

HttpReply httpSend(const HttpRequest &r)
{
#ifdef GEVR_IOS
    return httpSendApple(r);
#else
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
    // libcurl, the same contract: redirects followed, the body kept to
    // maxReply or streamed to the sink, HttpError{0} for no answer, {-1} when
    // the sink stops.
    static const bool curlReady = curl_global_init(CURL_GLOBAL_DEFAULT) == CURLE_OK;
    if (!curlReady) throw HttpError{0, "no HTTPS (libcurl)"};
    CURL *curl = curl_easy_init();
    if (!curl) throw HttpError{0, "no HTTPS (libcurl)"};
    struct Sink {
        const HttpRequest *r;
        HttpReply *reply;
        bool stopped;
    } sink{&r, &reply, false};
    auto write = +[](char *data, size_t size, size_t count, void *user) -> size_t {
        Sink *s = (Sink *)user;
        const size_t n = size * count;
        if (s->r->sink) {
            if (!s->r->sink(data, n)) {
                s->stopped = true;
                return 0;   // libcurl stops with CURLE_WRITE_ERROR
            }
        } else if (s->reply->body.size() < s->r->maxReply) {
            s->reply->body.append(data, std::min(n, s->r->maxReply - s->reply->body.size()));
        }
        return n;
    };
    struct curl_slist *headers = nullptr;
    for (const std::string &h : r.headers) headers = curl_slist_append(headers, h.c_str());
    curl_easy_setopt(curl, CURLOPT_URL, r.url.c_str());
    curl_easy_setopt(curl, CURLOPT_USERAGENT, r.agent.c_str());
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 10L);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);   // worker threads
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, (long)r.connectMs);
    // WinHTTP's read timeout is per read; libcurl's nearest is "too slow for too long"
    curl_easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT, 1L);
    curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME, (long)std::max(1, r.readMs / 1000));
    curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, r.method.c_str());
    if (r.method == "HEAD") curl_easy_setopt(curl, CURLOPT_NOBODY, 1L);
    if (r.body) {
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, r.body->data());
        curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE_LARGE, (curl_off_t)r.body->size());
    }
    if (headers) curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &sink);
    const CURLcode rc = curl_easy_perform(curl);
    long status = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
    curl_off_t length = -1;
    curl_easy_getinfo(curl, CURLINFO_CONTENT_LENGTH_DOWNLOAD_T, &length);
    const std::string reason = curl_easy_strerror(rc);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    if (sink.stopped) throw HttpError{-1, "cancelled"};
    if (rc != CURLE_OK) throw HttpError{0, reason};
    reply.status = (int)status;
    reply.length = (long long)length;
    return reply;
#endif
#endif   /* GEVR_IOS */
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
    // A plain SHA-256 (FIPS 180-4): the updater checks its download against
    // GitHub's digest, and no crypto library is needed for that.
    FILE *f = fopen(path.c_str(), "rb");
    if (!f) return std::string();
    static const uint32_t k[64] = {
        0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
        0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
        0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
        0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
        0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
        0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
        0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
        0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};
    uint32_t h[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
    auto rotr = [](uint32_t x, int n) { return (x >> n) | (x << (32 - n)); };
    auto block = [&](const unsigned char *p) {
        uint32_t w[64];
        for (int i = 0; i < 16; i++) w[i] = (uint32_t)p[4 * i] << 24 | (uint32_t)p[4 * i + 1] << 16 | (uint32_t)p[4 * i + 2] << 8 | p[4 * i + 3];
        for (int i = 16; i < 64; i++) {
            const uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
            const uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }
        uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], ff = h[5], g = h[6], hh = h[7];
        for (int i = 0; i < 64; i++) {
            const uint32_t t1 = hh + (rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25)) + ((e & ff) ^ (~e & g)) + k[i] + w[i];
            const uint32_t t2 = (rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
            hh = g; g = ff; ff = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
        }
        h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e; h[5] += ff; h[6] += g; h[7] += hh;
    };
    std::vector<unsigned char> buf(1 << 16);
    unsigned char tail[128];
    size_t tailLen = 0;
    unsigned long long total = 0;
    size_t n;
    while ((n = fread(buf.data(), 1, buf.size(), f)) > 0) {
        total += n;
        size_t i = 0;
        if (tailLen) {
            while (tailLen < 64 && i < n) tail[tailLen++] = buf[i++];
            if (tailLen == 64) { block(tail); tailLen = 0; }
        }
        for (; i + 64 <= n; i += 64) block(buf.data() + i);
        while (i < n) tail[tailLen++] = buf[i++];
    }
    const bool bad = ferror(f) != 0;
    fclose(f);
    if (bad) return std::string();
    tail[tailLen++] = 0x80;
    if (tailLen > 56) {
        while (tailLen < 64) tail[tailLen++] = 0;
        block(tail);
        tailLen = 0;
    }
    while (tailLen < 56) tail[tailLen++] = 0;
    const unsigned long long bits = total * 8;
    for (int i = 7; i >= 0; i--) tail[tailLen++] = (unsigned char)(bits >> (8 * i));
    block(tail);
    std::string hex;
    char two[3];
    for (uint32_t v : h)
        for (int s = 24; s >= 0; s -= 8) { snprintf(two, sizeof(two), "%02x", (unsigned)((v >> s) & 0xff)); hex += two; }
    return hex;
#endif
}

}   // namespace pcnet
