/*
 * What the PC's native services share (docs/pc-platforms.md P3): HTTPS through
 * WinHTTP, after Dab's Mod's ghostnet.c; Dab's bounded JSON span reader
 * (netlobby.c) with strings unescaped in full; Base64 as android.util.Base64
 * writes it; files, a small preferences file and bounded zip extraction.
 *
 * The services are ports of the Quest's Java ones: pc_lobby.cpp (LobbyClient),
 * pc_mods.cpp (ModManager), pc_reports.cpp (LogReporter), pc_friends.cpp
 * (FriendsClient) and pc_updater.cpp (UpdateChecker). Paths are UTF-8 and
 * relative to the game's folder (the exe's, where main.c starts the PC game),
 * which is the Quest's data folder on PC.
 */
#ifndef GEVR_PC_HTTP_H
#define GEVR_PC_HTTP_H

#include <string>
#include <vector>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>
#include <condition_variable>
#include <cstdint>

namespace pcnet {

/* ---- JSON: Dab's Mod's bounded reader (port/src/net/netlobby.c) ---- */

struct jspan { const char *p, *e; };

const char *jsonWs(const char *p, const char *e);
const char *jsonSkip(const char *p, const char *e, int depth);
bool jsonParse(const std::string &text, jspan *out);      // an object
bool jsonParseArray(const std::string &text, jspan *out); // an array (GitHub's release lists)
bool jsonGet(jspan obj, const char *key, jspan *out);
bool jsonAt(jspan arr, int index, jspan *out);
bool jsonIsNull(jspan v);
bool jsonStr(jspan v, std::string *out);                  // in full: SDP keeps its \r\n, \uXXXX as UTF-8
long long jsonInt(jspan v, long long def);
std::string jsonGetStr(jspan obj, const char *key, const char *def = "");
long long jsonGetInt(jspan obj, const char *key, long long def);
bool jsonGetBool(jspan obj, const char *key, bool def);   // org.json's optBoolean
std::string q(const std::string &s);                      // a JSON string literal

/* ---- text ---- */

std::string encode64(const std::string &in);        // URL-safe with padding (Base64.NO_WRAP | URL_SAFE)
std::string decode64(const std::string &in);        // either alphabet, padding ignored
std::string encodeBase64(const std::string &in);    // standard alphabet (Base64.NO_WRAP)
std::string clean(std::string s);                   // '|', '\n', '\r', '\t' to spaces: one field of a line
std::vector<std::string> splitBar(const std::string &raw);   // Java's split("\\|", -1)
std::wstring wide(const std::string &s);
std::string narrow(const std::wstring &s);
std::string upper(std::string s);

/* ---- clocks and the build ---- */

long long nowMs();      // steady (SystemClock.elapsedRealtime)
long long wallMs();     // since 1970 (System.currentTimeMillis)
std::string isoUtc(long long ms);   // CrashExitPolicy.isoUtc
const char *appVersion();           // android/app/build.gradle versionName (GEVR_APP_VERSION)
std::string envOr(const char *name, const char *def);   // a test's override of a service's address

/* ---- HTTP(S): WinHTTP ---- */

struct HttpError { int status; std::string message; };   // status 0: no answer (network), -1: cancelled

struct HttpRequest {
    std::string method = "GET";
    std::string url;                       // http:// or https://, any port
    const std::string *body = nullptr;
    std::vector<std::string> headers;      // "Name: value"
    std::string agent = "GoldenEyeVR-PC";
    int connectMs = 5000, readMs = 5000;
    size_t maxReply = 65536;               // what the body keeps (the rest is read and dropped)
    std::function<bool(const char *data, size_t n)> sink;   // streams the body instead; false stops (cancelled)
};

struct HttpReply {
    int status = 0;
    std::string body;
    long long length = -1;                 // Content-Length, when given
};

/* One request; redirects followed (GitHub's assets live on another host).
 * Throws HttpError{0, reason} when nothing answered, {-1} when the sink stopped. */
HttpReply httpSend(const HttpRequest &r);

/* LobbyClient.http: JSON in, the reply's JSON object out; throws HttpError for
 * status >= 400 (the reply's "error" when it has one), 0 for the network. */
std::string httpJson(const char *method, const std::string &url, const std::string *body, const std::string &token);

std::string sha256File(const std::string &path);   // lower-case hex, "" if unreadable

/* ---- files (UTF-8 paths) ---- */

bool isFile(const std::string &path);
bool isDir(const std::string &path);
long long fileSize(const std::string &path);        // -1 if none
long long fileTimeMs(const std::string &path);      // last write, ms since 1970; 0 if none
bool makeDirs(const std::string &path);
bool removeFile(const std::string &path);
bool deleteTree(const std::string &path);           // a file or a folder and all in it
bool movePath(const std::string &from, const std::string &to, bool replace);
std::vector<std::string> listDir(const std::string &path);   // names, without . and ..
bool readFile(const std::string &path, std::string *out, size_t max);
std::string tailFile(const std::string &path, size_t max);   // LogReporter.tail: the last max bytes
bool writeFileAtomic(const std::string &path, const std::string &data);   // a new file renamed over the old
long long freeSpace(const std::string &path);       // bytes free on path's volume, -1 unknown
std::string exeDir();                               // the folder GoldenEyeVR.exe runs from

/* ---- SharedPreferences: key=value lines in gevr_prefs.txt beside the exe ---- */

std::string prefGet(const std::string &key, const std::string &def = "");
long long prefGetInt(const std::string &key, long long def);
void prefSet(const std::string &key, const std::string &value);   // "" removes it
void prefSetInt(const std::string &key, long long value);

/* ---- bounded zip extraction (ModArchive.unpack), miniz ---- */

struct UnzipRules {
    std::vector<std::string> kinds;        // entries whose lower-case names end so ("" = any)
    int least = 0;                         // at least this many of them
    std::string stripPrefix;               // a top folder taken off every name ("" = none)
    long long maxEntry = 64LL << 20;       // ModArchive's per-entry and whole-archive limits
    long long maxTotal = 4LL << 30;
    long long minEntry = 4;
};

/* The entries kept, or throws HttpError{0, why}; progress(percent) false = cancelled ({-1}). */
int unzipBounded(const std::string &zip, const std::string &dir, const UnzipRules &rules,
                 const std::function<bool(int percent)> &progress, std::vector<std::string> *written = nullptr);

/* ---- one worker thread running jobs in order (Executors.newSingleThreadExecutor) ---- */

class SerialWorker {
public:
    SerialWorker() : thread_(&SerialWorker::run, this) { thread_.detach(); }   // lives with the process
    void post(std::function<void()> job)
    {
        std::lock_guard<std::mutex> lock(lock_);
        jobs_.push_back(std::move(job));
        wake_.notify_one();
    }
    bool idle()
    {
        std::lock_guard<std::mutex> lock(lock_);
        return jobs_.empty() && !busy_;
    }

private:
    void run()
    {
        for (;;) {
            std::function<void()> job;
            {
                std::unique_lock<std::mutex> lock(lock_);
                wake_.wait(lock, [this] { return !jobs_.empty(); });
                job = std::move(jobs_.front());
                jobs_.pop_front();
                busy_ = true;
            }
            try { job(); } catch (...) {}
            std::lock_guard<std::mutex> lock(lock_);
            busy_ = false;
        }
    }

    std::mutex lock_;
    std::condition_variable wake_;
    std::deque<std::function<void()>> jobs_;
    bool busy_ = false;
    std::thread thread_;   // last: starts once the queue exists
};

}   // namespace pcnet

#endif
