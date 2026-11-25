/*
 * The cross-platform sync's Google Drive (docs/cloud-sync.md): the player's
 * own Drive as the store of device records, and the sign-in.
 *
 *  - Scope drive.file only: the game sees the files it made and nothing else
 *    of the player's Drive. drive.appdata (the hidden folder) is refused by
 *    the device sign-in ("Invalid device flow scope", tested 2026-11-25).
 *  - The files sit in a "GoldenEye VR" folder in My Drive, each tagged
 *    (appProperties gevr = role, name = record name): the game finds them by
 *    the tag, so a renamed or moved file still works and a trashed one is
 *    made again.
 *  - Sign-in: OAuth's device flow, on every device (no Google Play services on
 *    the Quest, no browser on the Apple TV): the launcher shows a code, the
 *    player enters it at google.com/device on a phone. The refresh token stays
 *    in the save folder's .gevr-sync/google.txt, never synced.
 *
 * The client (GoldenEye VR devices, TVs and Limited Input) is the build's:
 * gevr_google_oauth.h, made by CMake from build/google/oauth.properties, which
 * is never committed. Without it the build has no Google Drive (the launcher
 * shows nothing). GEVR_GOOGLE_CLIENT_ID/_SECRET override it at run time, and
 * GEVR_GOOGLE_OAUTH_URL/GEVR_GOOGLE_API_URL point a test at a stand-in.
 */
#include "gevr_sync_store.h"
#include "pc_http.h"

#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#if __has_include("gevr_google_oauth.h")
#include "gevr_google_oauth.h"
#endif
#ifndef GEVR_GOOGLE_CLIENT_ID
#define GEVR_GOOGLE_CLIENT_ID ""
#define GEVR_GOOGLE_CLIENT_SECRET ""
#endif

using namespace pcnet;

namespace gsync {

static const char kScope[] = "https://www.googleapis.com/auth/drive.file";
static const char kFolderName[] = "GoldenEye VR";
static const char kReadme[] =
    "This folder is GoldenEye VR's sync: your settings and saves, one file per device.\n"
    "Each device writes only its own file and reads the others' when it starts.\n"
    "Please don't edit these files. To stop syncing, unlink Google Drive in the\n"
    "game's launcher (Play > Sync); deleting this folder removes the synced copies.\n";

static std::string clientId() { return envOr("GEVR_GOOGLE_CLIENT_ID", GEVR_GOOGLE_CLIENT_ID); }
static std::string clientSecret() { return envOr("GEVR_GOOGLE_CLIENT_SECRET", GEVR_GOOGLE_CLIENT_SECRET); }
static std::string oauthUrl() { return envOr("GEVR_GOOGLE_OAUTH_URL", "https://oauth2.googleapis.com"); }
static std::string apiUrl() { return envOr("GEVR_GOOGLE_API_URL", "https://www.googleapis.com"); }

bool driveAvailable() { return !clientId().empty() && !clientSecret().empty(); }

static std::string urlEncode(const std::string &s)
{
    static const char *hex = "0123456789ABCDEF";
    std::string o;
    for (unsigned char c : s) {
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            o += (char)c;
        } else {
            o += '%';
            o += hex[c >> 4];
            o += hex[c & 15];
        }
    }
    return o;
}

static std::string form(const std::vector<std::pair<std::string, std::string>> &kv)
{
    std::string o;
    for (const auto &p : kv) {
        if (!o.empty()) o += '&';
        o += urlEncode(p.first) + "=" + urlEncode(p.second);
    }
    return o;
}

/* one request; status 0 and why when nothing answered */
static HttpReply send(const std::string &method, const std::string &url, const std::string *body,
                      const std::vector<std::string> &headers, std::string *why, size_t maxReply = 4u << 20,
                      int readMs = 10000)
{
    HttpRequest r;
    r.followRedirects = false;   // Drive answers a resumable upload's piece with 308, no redirect
    r.method = method;
    r.url = url;
    r.body = body;
    r.headers = headers;
    r.agent = "GoldenEyeVR-Sync";
    r.connectMs = 5000;
    r.readMs = readMs;
    r.maxReply = maxReply;
    try {
        return httpSend(r);
    } catch (const HttpError &e) {
        *why = e.message.empty() ? "no answer" : e.message;
        HttpReply none;
        none.status = 0;
        return none;
    }
}

static std::string replyError(const HttpReply &r)
{
    jspan o, err;
    if (jsonParse(r.body, &o)) {
        if (jsonGet(o, "error", &err)) {
            std::string s;
            if (jsonStr(err, &s)) return s;   // OAuth: "invalid_grant"
            const std::string m = jsonGetStr(err, "message");
            if (!m.empty()) return m;          // Drive: {"error": {"message": ...}}
        }
    }
    return "HTTP " + std::to_string(r.status);
}

/* ------------------------------------------------------------ the link */

struct Link {
    std::string refresh, email;
};

static std::string linkPath(const std::string &stateDir) { return stateDir + "/google.txt"; }
/* why Google signed this device out, for the launcher until it links again */
static std::string signedOutPath(const std::string &stateDir) { return stateDir + "/google-signed-out.txt"; }

std::string driveSignedOut(const std::string &stateDir)
{
    std::string text;
    if (!readFile(signedOutPath(stateDir), &text, 4096)) return std::string();
    while (!text.empty() && (text.back() == '\n' || text.back() == '\r')) text.pop_back();
    return text;
}

static bool readLink(const std::string &stateDir, Link *out)
{
    std::string text;
    if (!readFile(linkPath(stateDir), &text, 65536)) return false;
    size_t pos = 0;
    while (pos < text.size()) {
        size_t nl = text.find('\n', pos);
        if (nl == std::string::npos) nl = text.size();
        std::string line = text.substr(pos, nl - pos);
        pos = nl + 1;
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
        if (line.compare(0, 8, "refresh=") == 0) out->refresh = line.substr(8);
        else if (line.compare(0, 6, "email=") == 0) out->email = line.substr(6);
    }
    return !out->refresh.empty();
}

static bool writeLink(const std::string &stateDir, const Link &l)
{
    return writeFileAtomic(linkPath(stateDir), "refresh=" + l.refresh + "\nemail=" + l.email + "\n");
}

bool driveLinked(const std::string &stateDir)
{
    Link l;
    return driveAvailable() && readLink(stateDir, &l);
}

std::string driveEmail(const std::string &stateDir)
{
    Link l;
    readLink(stateDir, &l);
    return l.email;
}

void driveUnlink(const std::string &stateDir)
{
    Link l;
    if (readLink(stateDir, &l)) {
        // Google forgets the grant too (best effort; the file goes either way)
        const std::string body = form({ { "token", l.refresh } });
        std::string why;
        send("POST", oauthUrl() + "/revoke", &body, { "Content-Type: application/x-www-form-urlencoded" }, &why);
    }
    removeFile(linkPath(stateDir));
    removeFile(signedOutPath(stateDir));
}

/* ------------------------------------------------------------ the store */

namespace {

class DriveStore : public Store {
public:
    DriveStore(const std::string &stateDir, const Link &l) : stateDir_(stateDir), link_(l) {}
    std::string label() const override { return "Google Drive" + (link_.email.empty() ? "" : " (" + link_.email + ")"); }

    bool list(std::vector<std::string> *names, std::string *why) override
    {
        ids_.clear();
        names->clear();
        std::string page;
        do {
            const std::string url = apiUrl() + "/drive/v3/files?spaces=drive&pageSize=1000&orderBy=modifiedTime%20desc"
                                    "&fields=nextPageToken,files(id,name,appProperties)&q=" +
                                    urlEncode("appProperties has { key='gevr' and value='record' } and trashed=false") +
                                    (page.empty() ? "" : "&pageToken=" + urlEncode(page));
            HttpReply r;
            if (!api("GET", url, nullptr, {}, &r, why)) return false;
            jspan o, files, f;
            if (!jsonParse(r.body, &o) || !jsonGet(o, "files", &files)) {
                *why = "Drive's list made no sense";
                return false;
            }
            for (int i = 0; jsonAt(files, i, &f); i++) {
                jspan props;
                std::string name = jsonGetStr(f, "name");
                if (jsonGet(f, "appProperties", &props)) {
                    const std::string tagged = jsonGetStr(props, "name");
                    if (!tagged.empty()) name = tagged;   // renamed by the player: the tag still knows
                }
                const std::string id = jsonGetStr(f, "id");
                // newest first: a duplicate (an upload retried) is the older one, left alone
                if (name.empty() || id.empty() || ids_.count(name)) continue;
                ids_[name] = id;
                names->push_back(name);
            }
            page = jsonGetStr(o, "nextPageToken");
        } while (!page.empty());
        listed_ = true;
        return true;
    }

    bool read(const std::string &name, std::string *data, std::string *why) override
    {
        auto it = ids_.find(name);
        if (it == ids_.end()) {
            *why = "no " + name;
            return false;
        }
        HttpReply r;
        if (!api("GET", apiUrl() + "/drive/v3/files/" + it->second + "?alt=media", nullptr, {}, &r, why)) return false;
        *data = r.body;
        return true;
    }

    bool write(const std::string &name, const std::string &data, std::string *why) override
    {
        if (!listed_) {
            std::vector<std::string> names;
            if (!list(&names, why)) return false;
        }
        auto it = ids_.find(name);
        HttpReply r;
        if (it != ids_.end()) {
            const std::string url = apiUrl() + "/upload/drive/v3/files/" + it->second + "?uploadType=media&fields=id";
            if (api("PATCH", url, &data, { "Content-Type: text/plain; charset=UTF-8" }, &r, why)) return true;
            if (r.status != 404) return false;
            ids_.erase(name);   // deleted for good since the list: made again below
        }
        std::string folder;
        if (!rootFolder(&folder, why)) return false;
        std::string id;
        if (!create(name, folder, "record", data, &id, why)) return false;
        ids_[name] = id;
        return true;
    }

    /* ---- blobs: ROMs and packs, tagged gevr=blob, blob=<id> ---- */

    bool listBlobs(std::map<std::string, long long> *blobs, std::string *why) override
    {
        blobFiles_.clear();
        blobs->clear();
        std::string page;
        do {
            const std::string url = apiUrl() + "/drive/v3/files?spaces=drive&pageSize=1000&orderBy=createdTime"
                                    "&fields=nextPageToken,files(id,size,appProperties)&q=" +
                                    urlEncode("appProperties has { key='gevr' and value='blob' } and trashed=false") +
                                    (page.empty() ? "" : "&pageToken=" + urlEncode(page));
            HttpReply r;
            if (!api("GET", url, nullptr, {}, &r, why)) return false;
            jspan o, files, f, props;
            if (!jsonParse(r.body, &o) || !jsonGet(o, "files", &files)) {
                *why = "Drive's list made no sense";
                return false;
            }
            for (int i = 0; jsonAt(files, i, &f); i++) {
                if (!jsonGet(f, "appProperties", &props)) continue;
                const std::string id = jsonGetStr(props, "blob"), fid = jsonGetStr(f, "id");
                if (id.empty() || fid.empty() || blobFiles_.count(id)) continue;   // oldest first: a duplicate is left
                blobFiles_[id] = fid;
                // Drive gives the size as a string
                (*blobs)[id] = atoll(jsonGetStr(f, "size", "0").c_str());
            }
            page = jsonGetStr(o, "nextPageToken");
        } while (!page.empty());
        return true;
    }

    /* Drive's resumable upload: a session, then 4 MiB pieces (a multiple of 256 KiB) */
    bool putBlob(const std::string &id, const std::string &label, const std::string &localPath,
                 const Progress &progress, std::string *why) override
    {
        const long long total = fileSize(localPath);
        if (total < 0) {
            *why = "could not read " + localPath;
            return false;
        }
        std::string folder;
        if (!rootFolder(&folder, why)) return false;
        const std::string meta = "{\"name\":" + q(label) + ",\"parents\":[" + q(folder) +
                                 "],\"appProperties\":{\"gevr\":\"blob\",\"blob\":" + q(id) + "}}";
        HttpReply r;
        if (!api("POST", apiUrl() + "/upload/drive/v3/files?uploadType=resumable&fields=id", &meta,
                 { "Content-Type: application/json; charset=UTF-8", "X-Upload-Content-Type: application/octet-stream",
                   "X-Upload-Content-Length: " + std::to_string(total) }, &r, why)) {
            return false;
        }
        const std::string session = r.location;
        if (session.empty()) {
            *why = "Drive gave no upload address";
            return false;
        }
        FILE *f = openFile(localPath, "rb");
        if (!f) {
            *why = "could not read " + localPath;
            return false;
        }
        static const size_t kPiece = 4u << 20;
        std::string piece;
        long long done = 0;
        bool ok = true;
        do {
            piece.resize(kPiece);
            const size_t n = fread(&piece[0], 1, kPiece, f);
            piece.resize(n);
            if (n == 0 && total > 0) {
                ok = false;
                *why = "the file ended early";
                break;
            }
            const std::string range = total == 0 ? "bytes */0"
                                                  : "bytes " + std::to_string(done) + "-" + std::to_string(done + (long long)n - 1) +
                                                        "/" + std::to_string(total);
            if (!api("PUT", session, &piece, { "Content-Range: " + range }, &r, why, 120000)) {
                ok = false;
                break;
            }
            done += (long long)n;
            if (progress && !progress(done, total)) {
                ok = false;
                *why = "stopped";
                break;
            }
            if (r.status != 308) break;   // 200/201: all of it is there
        } while (done < total);
        fclose(f);
        if (!ok) return false;
        if (r.status != 200 && r.status != 201) {
            *why = "Drive stopped the upload early (HTTP " + std::to_string(r.status) + ")";
            return false;
        }
        jspan o;
        if (jsonParse(r.body, &o) && !jsonGetStr(o, "id").empty()) blobFiles_[id] = jsonGetStr(o, "id");
        return true;
    }

    bool getBlob(const std::string &id, const std::string &localPath, const Progress &progress,
                 std::string *why) override
    {
        if (!blobFiles_.count(id)) {
            std::map<std::string, long long> all;
            if (!listBlobs(&all, why)) return false;
        }
        auto it = blobFiles_.find(id);
        if (it == blobFiles_.end()) {
            *why = "no such file in Drive (another device may have replaced it)";
            return false;
        }
        for (int attempt = 0; attempt < 2; attempt++) {
            if (!token(why)) return false;
            const std::string tmp = localPath + ".sync-part";
            FILE *out = openFile(tmp, "wb");
            if (!out) {
                *why = "could not write " + tmp;
                return false;
            }
            long long done = 0;
            bool full = false;
            HttpRequest r;
            r.url = apiUrl() + "/drive/v3/files/" + it->second + "?alt=media";
            r.headers = { "Authorization: Bearer " + access_ };
            r.agent = "GoldenEyeVR-Sync";
            r.connectMs = 5000;
            r.readMs = 60000;
            r.sink = [&](const char *data, size_t n) {
                if (fwrite(data, 1, n, out) != n) {
                    full = true;
                    return false;
                }
                done += (long long)n;
                return !progress || progress(done, -1);
            };
            HttpReply reply;
            try {
                reply = httpSend(r);
            } catch (const HttpError &e) {
                fclose(out);
                removeFile(tmp);
                *why = full ? "the disk is full" : e.status == -1 ? "stopped" : e.message;
                return false;
            }
            const bool closed = fclose(out) == 0;
            if (reply.status == 401 && attempt == 0) {
                removeFile(tmp);
                access_.clear();
                continue;
            }
            if (reply.status != 200 || !closed) {
                removeFile(tmp);
                *why = reply.status != 200 ? replyError(reply) : "could not write " + tmp;
                return false;
            }
            removeFile(localPath);
            if (!movePath(tmp, localPath, true)) {
                removeFile(tmp);
                *why = "could not write " + localPath;
                return false;
            }
            return true;
        }
        return false;
    }

    bool removeBlob(const std::string &id, std::string *why) override
    {
        auto it = blobFiles_.find(id);
        if (it == blobFiles_.end()) return true;   // not there: nothing to do
        HttpReply r;
        if (!api("DELETE", apiUrl() + "/drive/v3/files/" + it->second, nullptr, {}, &r, why)) return false;
        blobFiles_.erase(it);
        return true;
    }

private:
    /* an API call with the access token, refreshed once when it ran out */
    bool api(const std::string &method, const std::string &url, const std::string *body,
             std::vector<std::string> headers, HttpReply *r, std::string *why, int readMs = 10000)
    {
        for (int attempt = 0; attempt < 2; attempt++) {
            if (!token(why)) return false;
            std::vector<std::string> h = headers;
            h.push_back("Authorization: Bearer " + access_);
            *r = send(method, url, body, h, why, 4u << 20, readMs);
            if (r->status == 401 && attempt == 0) {
                access_.clear();
                continue;
            }
            if ((r->status >= 200 && r->status < 300) || r->status == 308) return true;
            if (r->status) *why = replyError(*r);
            return false;
        }
        return false;
    }

    bool token(std::string *why)
    {
        if (!access_.empty() && nowMs() < accessUntil_) return true;
        return refresh(why);
    }

    bool refresh(std::string *why)
    {
        const std::string body = form({ { "client_id", clientId() }, { "client_secret", clientSecret() },
                                        { "refresh_token", link_.refresh }, { "grant_type", "refresh_token" } });
        HttpReply r = send("POST", oauthUrl() + "/token", &body, { "Content-Type: application/x-www-form-urlencoded" }, why);
        jspan o;
        if (r.status == 200 && jsonParse(r.body, &o)) {
            access_ = jsonGetStr(o, "access_token");
            accessUntil_ = nowMs() + (jsonGetInt(o, "expires_in", 3600) - 60) * 1000;
            if (!access_.empty()) return true;
        }
        if (r.status) {
            *why = replyError(r);
            // the player took the access away (or the token aged out): signed out here too,
            // and the launcher says so until Drive is linked again
            if (*why == "invalid_grant") {
                *why = "Google Drive signed this device out. Link it again to keep syncing.";
                removeFile(linkPath(stateDir_));
                writeFileAtomic(signedOutPath(stateDir_), *why + "\n");
            }
        }
        return false;
    }

    /* the GoldenEye VR folder: the oldest tagged one, made (with its README) when none is */
    bool rootFolder(std::string *id, std::string *why)
    {
        if (!folder_.empty()) {
            *id = folder_;
            return true;
        }
        const std::string url = apiUrl() + "/drive/v3/files?spaces=drive&pageSize=10&orderBy=createdTime&fields=files(id)&q=" +
                                urlEncode("appProperties has { key='gevr' and value='root' } and trashed=false");
        HttpReply r;
        if (!api("GET", url, nullptr, {}, &r, why)) return false;
        jspan o, files, f;
        if (jsonParse(r.body, &o) && jsonGet(o, "files", &files) && jsonAt(files, 0, &f)) {
            folder_ = jsonGetStr(f, "id");
        }
        if (folder_.empty()) {
            const std::string meta = "{\"name\":" + q(kFolderName) +
                                     ",\"mimeType\":\"application/vnd.google-apps.folder\",\"appProperties\":{\"gevr\":\"root\"}}";
            if (!api("POST", apiUrl() + "/drive/v3/files?fields=id", &meta, { "Content-Type: application/json; charset=UTF-8" },
                     &r, why)) {
                return false;
            }
            jspan made;
            if (jsonParse(r.body, &made)) folder_ = jsonGetStr(made, "id");
            if (folder_.empty()) {
                *why = "Drive made no folder";
                return false;
            }
            std::string readmeId, ignore;
            create("README.txt", folder_, "readme", kReadme, &readmeId, &ignore);
        }
        *id = folder_;
        return true;
    }

    bool create(const std::string &name, const std::string &parent, const std::string &role, const std::string &data,
                std::string *id, std::string *why)
    {
        static const char boundary[] = "gevr-sync-part-7c1e";
        const std::string meta = "{\"name\":" + q(name) + ",\"mimeType\":\"text/plain\",\"parents\":[" + q(parent) +
                                 "],\"appProperties\":{\"gevr\":" + q(role) + ",\"name\":" + q(name) + "}}";
        std::string body = std::string("--") + boundary + "\r\nContent-Type: application/json; charset=UTF-8\r\n\r\n" + meta +
                           "\r\n--" + boundary + "\r\nContent-Type: text/plain; charset=UTF-8\r\n\r\n" + data + "\r\n--" +
                           boundary + "--\r\n";
        HttpReply r;
        if (!api("POST", apiUrl() + "/upload/drive/v3/files?uploadType=multipart&fields=id", &body,
                 { std::string("Content-Type: multipart/related; boundary=") + boundary }, &r, why)) {
            return false;
        }
        jspan o;
        if (jsonParse(r.body, &o)) *id = jsonGetStr(o, "id");
        if (id->empty()) {
            *why = "Drive made no file";
            return false;
        }
        return true;
    }

    std::string stateDir_;
    Link link_;
    std::string access_;
    long long accessUntil_ = 0;
    std::map<std::string, std::string> ids_;   // record name -> Drive file id
    std::map<std::string, std::string> blobFiles_;   // blob id -> Drive file id
    bool listed_ = false;
    std::string folder_;
};

}   // namespace

std::unique_ptr<Store> driveStore(const std::string &stateDir)
{
    Link l;
    if (!driveAvailable() || !readLink(stateDir, &l)) return nullptr;
    return std::unique_ptr<Store>(new DriveStore(stateDir, l));
}

/* ------------------------------------------------------------ the sign-in */

namespace {

std::mutex s_lock;
DriveSignIn s_signIn;      // what the launcher shows
unsigned s_attempt = 0;    // a newer sign-in stops an older one

void setSignIn(unsigned attempt, const DriveSignIn &v)
{
    std::lock_guard<std::mutex> hold(s_lock);
    if (attempt == s_attempt) s_signIn = v;
}

bool current(unsigned attempt)
{
    std::lock_guard<std::mutex> hold(s_lock);
    return attempt == s_attempt;
}

/* the account's address, to show which Drive is linked (best effort) */
std::string accountEmail(const std::string &access)
{
    std::string why;
    HttpReply r = send("GET", apiUrl() + "/drive/v3/about?fields=user(emailAddress)", nullptr,
                       { "Authorization: Bearer " + access }, &why);
    jspan o, u;
    if (r.status == 200 && jsonParse(r.body, &o) && jsonGet(o, "user", &u)) return jsonGetStr(u, "emailAddress");
    return std::string();
}

void signInThread(unsigned attempt, std::string stateDir)
{
    DriveSignIn st;
    st.state = DriveSignIn::STARTING;
    setSignIn(attempt, st);
    std::string why;
    const std::string body = form({ { "client_id", clientId() }, { "scope", kScope } });
    HttpReply r = send("POST", oauthUrl() + "/device/code", &body, { "Content-Type: application/x-www-form-urlencoded" }, &why);
    jspan o;
    if (r.status != 200 || !jsonParse(r.body, &o)) {
        st.state = DriveSignIn::FAILED;
        st.message = "Couldn't reach Google: " + (r.status ? replyError(r) : why);
        setSignIn(attempt, st);
        return;
    }
    const std::string device = jsonGetStr(o, "device_code");
    st.code = jsonGetStr(o, "user_code");
    st.url = jsonGetStr(o, "verification_url", "https://www.google.com/device");
    long long interval = jsonGetInt(o, "interval", 5) * 1000;
    const long long until = nowMs() + jsonGetInt(o, "expires_in", 1800) * 1000;
    st.state = DriveSignIn::WAITING;
    setSignIn(attempt, st);

    while (current(attempt)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(interval));
        if (!current(attempt)) return;
        if (nowMs() > until) {
            st.state = DriveSignIn::FAILED;
            st.message = "The code ran out before it was entered. Try again.";
            setSignIn(attempt, st);
            return;
        }
        const std::string poll = form({ { "client_id", clientId() }, { "client_secret", clientSecret() },
                                        { "device_code", device },
                                        { "grant_type", "urn:ietf:params:oauth:grant-type:device_code" } });
        r = send("POST", oauthUrl() + "/token", &poll, { "Content-Type: application/x-www-form-urlencoded" }, &why);
        if (r.status == 0) continue;   // the network blinked: ask again
        const std::string err = r.status == 200 ? std::string() : replyError(r);
        if (err == "authorization_pending") continue;
        if (err == "slow_down") {
            interval += 5000;
            continue;
        }
        if (!err.empty()) {
            st.state = DriveSignIn::FAILED;
            st.message = err == "access_denied" ? "Google Drive wasn't allowed. Try again to link it."
                                                : "Google said: " + err;
            setSignIn(attempt, st);
            return;
        }
        jspan t;
        Link l;
        if (jsonParse(r.body, &t)) l.refresh = jsonGetStr(t, "refresh_token");
        if (l.refresh.empty()) {
            st.state = DriveSignIn::FAILED;
            st.message = "Google gave no lasting sign-in. Try again.";
            setSignIn(attempt, st);
            return;
        }
        l.email = accountEmail(jsonGetStr(t, "access_token"));
        removeFile(signedOutPath(stateDir));
        if (!writeLink(stateDir, l)) {
            st.state = DriveSignIn::FAILED;
            st.message = "Couldn't save the sign-in in the save folder.";
            setSignIn(attempt, st);
            return;
        }
        st.state = DriveSignIn::DONE;
        st.message = l.email;
        setSignIn(attempt, st);
        return;
    }
}

}   // namespace

void driveSignInStart(const std::string &stateDir)
{
    unsigned attempt;
    {
        std::lock_guard<std::mutex> hold(s_lock);
        attempt = ++s_attempt;
        s_signIn = DriveSignIn();
        s_signIn.state = DriveSignIn::STARTING;
    }
    std::thread(signInThread, attempt, stateDir).detach();
}

void driveSignInCancel()
{
    std::lock_guard<std::mutex> hold(s_lock);
    s_attempt++;
    s_signIn = DriveSignIn();
}

DriveSignIn driveSignInState()
{
    std::lock_guard<std::mutex> hold(s_lock);
    return s_signIn;
}

}   // namespace gsync
