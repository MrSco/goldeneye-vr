/*
 * The update check on PC (docs/pc-platforms.md P3): a port of the Quest's
 * android/app/src/main/java/com/gevr/port/UpdateChecker.java and
 * UpdateVersion.java.
 *
 * The launcher (port/vr/vr_launcher.cpp gevrUpdaterStatus/gevrUpdaterCommand)
 * reads updaterStatus and sends updaterCommand through pc_services.cpp:
 *
 *   status: state \t offered version \t progress \t message \t test builds (0/1) \t installed version
 *           (idle checking uptodate available downloading installing error)
 *   commands: check, update, retry, cancel, testbuilds:0, testbuilds:1
 *
 * Check: the same GitHub releases (MrSco/goldeneye-vr, or GEVR_UPDATES_URL for
 * a test's mock), the newest release (and with test builds on, pre-release)
 * newer than this build that carries the Windows zip tools/package_pc.py makes,
 * GoldenEyeVR-VERSION-windows-x64.zip.
 *
 * Update, where Windows differs from Android's package installer:
 * - Download into updates/ beside the exe, check the size and GitHub's SHA-256
 *   digest (the asset's "digest", as UpdateChecker does for the APK).
 * - Android's checks that the APK is this package, not older and signed with
 *   our key become: the zip unpacks (bounded, ModArchive's rules) into
 *   updates/staging and holds GoldenEyeVR.exe, the exe that is running (the
 *   PC VR GoldenEyeVR-VR.exe restarts into its own new copy) and a VERSION
 *   file naming the offered release, not older than this build. There is no
 *   signature to check: the zip is only trusted as far as GitHub's digest and
 *   HTTPS go.
 * - Install: each file the zip holds replaces the game's own (package_pc.py's:
 *   both exes, phonon.dll, openxr_loader.dll, the Play VR / Play SteamVR
 *   scripts, README, licences, VERSION). A running exe and
 *   a loaded DLL can be renamed though not overwritten, so the current files
 *   become NAME.old, the new ones move in (a failure puts every file back), and
 *   the game starts itself again (pcRestartSelf, from the launcher's thread at
 *   its next status poll) as Android's installer ends the process. The next
 *   start deletes the .old files and the downloads, and says "Updated to vX."
 * - There is no "install unknown apps" permission to ask for ("permission"
 *   never appears) and no installer prompt to cancel.
 */

#include <string>
#include <vector>
#include <mutex>
#include <atomic>
#include <thread>
#include <chrono>
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <cstdlib>

#ifdef _WIN32
#include <windows.h>
#endif

#include "pc_http.h"

extern "C" void sysLogPrintf(int level, const char *fmt, ...);
void pcRestartSelf(void);   // pc_launcher.cpp

using namespace pcnet;

namespace {

const char *const kApi = "https://api.github.com/repos/MrSco/goldeneye-vr/releases?per_page=20";
const long long kMaxDownload = 512LL << 20;

/* ---- UpdateVersion ---- */

/* One release as the GitHub API lists it, reduced to what the updater needs. */
struct Release {
    std::string tag;
    bool prerelease = false, draft = false;
    std::string zipName, zipUrl;
    long long zipSize = -1;
    std::string zipSha256;   // hex, when GitHub reports one ("digest": "sha256:...")
    std::string notes;
};

/* "v0.1.12" -> "0.1.12"; trims whitespace. */
std::string normalize(const std::string &tag)
{
    size_t a = 0, b = tag.size();
    while (a < b && isspace((unsigned char)tag[a])) a++;
    while (b > a && isspace((unsigned char)tag[b - 1])) b--;
    std::string t = tag.substr(a, b - a);
    if (!t.empty() && (t[0] == 'v' || t[0] == 'V')) t.erase(0, 1);
    return t;
}

std::vector<std::string> splitDots(const std::string &s)
{
    std::vector<std::string> out;
    size_t start = 0;
    for (;;) {
        const size_t dot = s.find('.', start);
        out.push_back(s.substr(start, dot == std::string::npos ? std::string::npos : dot - start));
        if (dot == std::string::npos) return out;
        start = dot + 1;
    }
}

long long leadingNumber(const std::string &s)
{
    size_t i = 0;
    while (i < s.size() && isdigit((unsigned char)s[i])) i++;
    return i == 0 ? 0 : strtoll(s.substr(0, i).c_str(), nullptr, 10);
}

bool isDigits(const std::string &s)
{
    if (s.empty()) return false;
    for (char c : s) if (!isdigit((unsigned char)c)) return false;
    return true;
}

/* "0.1.13-test.1+abc" -> {"0.1.13", "test.1"} (build metadata after '+' ignored) */
void splitSuffix(std::string v, std::string *core, std::string *suffix)
{
    const size_t plus = v.find('+');
    if (plus != std::string::npos) v.resize(plus);
    const size_t dash = v.find('-');
    *core = dash == std::string::npos ? v : v.substr(0, dash);
    *suffix = dash == std::string::npos ? std::string() : v.substr(dash + 1);
}

/* Negative if a is older than b, zero if the same, positive if newer (semver's order). */
int compare(const std::string &a, const std::string &b)
{
    std::string ca, sa, cb, sb;
    splitSuffix(normalize(a), &ca, &sa);
    splitSuffix(normalize(b), &cb, &sb);
    const std::vector<std::string> pa = splitDots(ca), pb = splitDots(cb);
    for (size_t i = 0; i < std::max(pa.size(), pb.size()); i++) {
        const long long x = i < pa.size() ? leadingNumber(pa[i]) : 0;
        const long long y = i < pb.size() ? leadingNumber(pb[i]) : 0;
        if (x != y) return x < y ? -1 : 1;
    }
    // Same numbers: a release beats any pre-release of it.
    const bool hasA = !sa.empty(), hasB = !sb.empty();
    if (!hasA && !hasB) return 0;
    if (!hasA) return 1;
    if (!hasB) return -1;
    const std::vector<std::string> ia = splitDots(sa), ib = splitDots(sb);
    for (size_t i = 0; i < std::max(ia.size(), ib.size()); i++) {
        if (i >= ia.size()) return -1;   // fewer pieces sorts first
        if (i >= ib.size()) return 1;
        const bool da = isDigits(ia[i]), db = isDigits(ib[i]);
        int c;
        if (da && db) {
            const long long x = strtoll(ia[i].c_str(), nullptr, 10), y = strtoll(ib[i].c_str(), nullptr, 10);
            c = x < y ? -1 : x > y ? 1 : 0;
        } else if (da != db) {
            c = da ? -1 : 1;             // numeric pieces sort before words
        } else {
            c = ia[i].compare(ib[i]);
        }
        if (c != 0) return c < 0 ? -1 : 1;
    }
    return 0;
}

/* The newest release with a Windows zip that is newer than the installed version, or none. */
bool pick(const std::vector<Release> &releases, const std::string &installed, bool includePrereleases, Release *out)
{
    const Release *best = nullptr;
    for (const Release &r : releases) {
        if (r.draft || r.tag.empty() || r.zipUrl.empty()) continue;
        if (r.prerelease && !includePrereleases) continue;
        if (!best || compare(r.tag, best->tag) > 0) best = &r;
    }
    if (best && compare(best->tag, installed) > 0) {
        *out = *best;
        return true;
    }
    return false;
}

/* tools/package_pc.py's zip: GoldenEyeVR-VERSION-windows-x64.zip (the APK's isApkAsset/isPreferredApk). */
bool isWindowsAsset(const std::string &name)
{
    const std::string head = "GoldenEyeVR-", tail = "-windows-x64.zip";
    return name.size() > head.size() + tail.size() && name.compare(0, head.size(), head) == 0
        && name.compare(name.size() - tail.size(), tail.size(), tail) == 0;
}

std::vector<Release> parseReleases(const std::string &json)
{
    std::vector<Release> out;
    jspan arr, o, assets, a;
    if (!jsonParseArray(json, &arr)) throw HttpError{0, "GitHub's answer is not a release list"};
    for (int i = 0; jsonAt(arr, i, &o); i++) {
        Release r;
        r.tag = jsonGetStr(o, "tag_name");
        r.prerelease = jsonGetBool(o, "prerelease", false);
        r.draft = jsonGetBool(o, "draft", false);
        r.notes = jsonGetStr(o, "body");
        if (jsonGet(o, "assets", &assets)) {
            for (int j = 0; jsonAt(assets, j, &a); j++) {
                const std::string name = jsonGetStr(a, "name");
                if (!isWindowsAsset(name)) continue;
                r.zipName = name;
                r.zipUrl = jsonGetStr(a, "browser_download_url");
                r.zipSize = jsonGetInt(a, "size", -1);
                const std::string digest = jsonGetStr(a, "digest");
                r.zipSha256.clear();
                if (digest.compare(0, 7, "sha256:") == 0) {
                    r.zipSha256 = digest.substr(7);
                    for (char &c : r.zipSha256) if (c >= 'A' && c <= 'Z') c = (char)(c + 32);
                }
                break;
            }
        }
        out.push_back(r);
    }
    return out;
}

/* The running exe's file name ("GoldenEyeVR.exe" or "GoldenEyeVR-VR.exe"). */
std::string runningExeName()
{
#ifdef _WIN32
    wchar_t buf[MAX_PATH * 4];
    const DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH * 4);
    if (n > 0 && n < MAX_PATH * 4) {
        const std::string path = narrow(std::wstring(buf, n));
        const size_t slash = path.find_last_of("/\\");
        return slash == std::string::npos ? path : path.substr(slash + 1);
    }
#endif
    return "GoldenEyeVR.exe";
}

bool sameName(const std::string &a, const std::string &b)   // NTFS ignores case
{
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); i++)
        if (tolower((unsigned char)a[i]) != tolower((unsigned char)b[i])) return false;
    return true;
}

/* Is the unpacked zip this game, the offered release and not older than this
 * build (Android's package, versionCode and signer checks)? "" when it is, else
 * why not. files: the zip's paths below its top folder; version: its VERSION. */
std::string stagedProblem(const std::vector<std::string> &files, const std::string &versionFile,
                          const std::string &tag, const std::string &installed, const std::string &runningExe)
{
    bool hasFlat = false, hasRunning = false;
    for (const std::string &f : files) {
        hasFlat = hasFlat || sameName(f, "GoldenEyeVR.exe");
        hasRunning = hasRunning || sameName(f, runningExe);
    }
    if (!hasFlat) return "the download is not GoldenEye VR for Windows";
    if (!hasRunning) return "the download has no " + runningExe;
    const std::string version = normalize(versionFile);
    if (version != normalize(tag))
        return "the download is a different version (" + (version.empty() ? std::string("none") : version) + ")";
    if (compare(version, installed) < 0) return "that build is older than the one installed";
    return std::string();
}

std::string statusField(std::string s)
{
    for (char &c : s) if (c == '\t' || c == '\n') c = ' ';
    return s;
}

class UpdateChecker {
public:
    UpdateChecker() : installed_(appVersion()), dir_(exeDir())
    {
        // Did the last run install an update? Say so once, and tidy up.
        const std::string pending = prefGet("updater.pendingVersion");
        if (!pending.empty()) {
            if (compare(installed_, pending) >= 0) notice_ = "Updated to v" + normalize(installed_) + ".";
            else sysLogPrintf(1, "updater: v%s was installed but this is v%s", pending.c_str(), installed_.c_str());
            prefSet("updater.pendingVersion", "");
        }
        worker_.post([this] { deleteDownloads(); });
    }

    /* state \t offered version \t progress \t message \t test builds (0/1) \t installed version */
    std::string status()
    {
        bool restart = false;
        std::string out;
        {
            std::lock_guard<std::mutex> lock(lock_);
            std::string s = state_, m = message_;
            if (m.empty() && (s == "idle" || s == "checking" || s == "uptodate")) m = notice_;
            out = s + "\t" + (hasOffer_ ? normalize(offer_.tag) : "") + "\t" + std::to_string(progress_.load()) + "\t"
                + statusField(m) + "\t" + (testBuilds() ? "1" : "0") + "\t" + normalize(installed_);
            restart = restartPending_;
        }
        if (restart) {
            // the new files are in: start them (on the launcher's thread, which polls this)
            sysLogPrintf(0, "updater: restarting into the update");
            pcRestartSelf();
        }
        return out;
    }

    void command(const std::string &cmd)
    {
        std::lock_guard<std::mutex> lock(lock_);
        if (cmd == "check") check();
        else if (cmd == "update") update();
        else if (cmd == "retry") { if (hasOffer_) update(); else check(); }
        else if (cmd == "cancel") cancel();
        else if (cmd == "testbuilds:1" || cmd == "testbuilds:0") {
            prefSet("updater.testBuilds", cmd == "testbuilds:1" ? "1" : "");
            check();
        } else {
            sysLogPrintf(1, "updater: unknown command %s", cmd.c_str());
        }
    }

private:
    static bool testBuilds() { return prefGet("updater.testBuilds") == "1"; }

    bool busy() const { return state_ == "checking" || state_ == "downloading" || state_ == "installing"; }

    void setState(const std::string &s, const std::string &msg)   // with lock_ held
    {
        state_ = s;
        message_ = msg;
    }

    void setStateLocked(const std::string &s, const std::string &msg)
    {
        std::lock_guard<std::mutex> lock(lock_);
        setState(s, msg);
    }

    /* ---- check ---- */

    void check()
    {
        if (busy()) return;
        setState("checking", "");
        const bool tests = testBuilds();
        worker_.post([this, tests] {
            try {
                const std::vector<Release> releases = parseReleases(httpGet(envOr("GEVR_UPDATES_URL", kApi)));
                Release r;
                const bool found = pick(releases, installed_, tests, &r);
                std::lock_guard<std::mutex> lock(lock_);
                if (!found) {
                    hasOffer_ = false;
                    setState("uptodate", "");
                } else {
                    if (!hasOffer_ || r.tag != offer_.tag) downloaded_.clear();
                    offer_ = r;
                    hasOffer_ = true;
                    setState("available", r.prerelease ? "Test build." : "");
                }
                sysLogPrintf(0, "updater: check: installed %s, offer %s", installed_.c_str(), found ? r.tag.c_str() : "none");
            } catch (const HttpError &e) {
                sysLogPrintf(1, "updater: check failed: %s", e.message.c_str());
                setStateLocked("error", "Could not check for updates: " + e.message);
            }
        });
    }

    /* ---- update ---- */

    void update()
    {
        if (!hasOffer_ || busy()) return;
        const Release r = offer_;
        notice_.clear();
        const std::string ready = downloaded_;
        if (!ready.empty() && isFile(ready)) {
            // busy from this moment, so a second press cannot start a second install
            setState("installing", "Installing the update.");
            worker_.post([this, ready, r] { installOrFail(ready, r); });
            return;
        }
        cancelled_ = false;
        setState("downloading", "");
        progress_ = 0;
        worker_.post([this, r] {
            try {
                const std::string zip = download(r);
                verify(zip, r);
                {
                    std::lock_guard<std::mutex> lock(lock_);
                    downloaded_ = zip;
                }
                installOrFail(zip, r);
            } catch (const HttpError &e) {
                progress_ = -1;
                if (cancelled_) {
                    setStateLocked("available", "Update cancelled.");
                } else {
                    sysLogPrintf(1, "updater: update failed: %s", e.message.c_str());
                    setStateLocked("error", e.message);
                }
            }
        });
    }

    /* The launcher's Cancel: a download stops. Once the files are swapped the restart is already due. */
    void cancel()
    {
        if (state_ == "downloading") cancelled_ = true;
    }

    std::string updatesDir()
    {
        const std::string dir = dir_ + "/updates";
        if (!isDir(dir) && !makeDirs(dir)) throw HttpError{0, "could not create " + dir};
        return dir;
    }

    std::string download(const Release &r)
    {
        const std::string dir = updatesDir();
        std::string name = r.zipName.empty() ? "update.zip" : r.zipName;
        for (char &c : name) if (!(isalnum((unsigned char)c) || c == '.' || c == '_' || c == '-')) c = '_';
        const std::string part = dir + "/" + name + ".part";
        const std::string zip = dir + "/" + name;
        FILE *f = _wfopen(wide(part).c_str(), L"wb");
        if (!f) throw HttpError{0, "could not save the download"};
        long long got = 0, total = r.zipSize;
        bool tooBig = false, writeFailed = false;
        HttpRequest q;
        q.url = r.zipUrl;
        q.agent = "GoldenEye-VR/" + normalize(installed_);
        q.connectMs = 10000;
        q.readMs = 20000;
        q.headers.push_back("Accept: application/octet-stream");
        q.sink = [&](const char *data, size_t n) {
            if (cancelled_) return false;
            if (fwrite(data, 1, n, f) != n) { writeFailed = true; return false; }
            got += (long long)n;
            if (got > kMaxDownload) { tooBig = true; return false; }
            if (total > 0) progress_ = (int)std::min<long long>(99, got * 100 / total);
            return true;
        };
        HttpReply reply;
        try {
            reply = httpSend(q);
        } catch (const HttpError &e) {
            fclose(f);
            removeFile(part);
            if (tooBig) throw HttpError{0, "download is too large"};
            if (writeFailed) throw HttpError{0, "could not save the download (disk full?)"};
            throw;
        }
        const bool closed = fclose(f) == 0;
        if (reply.status != 200) { removeFile(part); throw HttpError{0, "download failed (HTTP " + std::to_string(reply.status) + ")"}; }
        if (!closed) { removeFile(part); throw HttpError{0, "could not save the download (disk full?)"}; }
        if (r.zipSize > 0 && got != r.zipSize) {
            removeFile(part);
            throw HttpError{0, "download incomplete (" + std::to_string(got) + " of " + std::to_string(r.zipSize) + " bytes)"};
        }
        removeFile(zip);
        if (!movePath(part, zip, true)) {
            removeFile(part);
            throw HttpError{0, "could not save the download"};
        }
        progress_ = 100;
        return zip;
    }

    /* Same bytes GitHub has (UpdateChecker.verify's checksum; the APK checks follow at install). */
    void verify(const std::string &zip, const Release &r)
    {
        if (!r.zipSha256.empty()) {
            const std::string got = sha256File(zip);
            if (got != r.zipSha256) {
                removeFile(zip);
                throw HttpError{0, "the download is damaged (checksum mismatch)"};
            }
        }
    }

    void installOrFail(const std::string &zip, const Release &r)
    {
        try {
            install(zip, r);
        } catch (const HttpError &e) {
            sysLogPrintf(1, "updater: install failed: %s", e.message.c_str());
            prefSet("updater.pendingVersion", "");
            progress_ = -1;
            setStateLocked("error", e.message);
        }
    }

    void install(const std::string &zip, const Release &r)
    {
        setStateLocked("installing", "Installing the update.");
        progress_ = -1;
        const std::string staging = updatesDir() + "/staging";
        deleteTree(staging);
        // GoldenEyeVR-VERSION-windows-x64/...: the zip's one top folder
        std::string top = r.zipName.size() > 4 ? r.zipName.substr(0, r.zipName.size() - 4) + "/" : std::string();
        UnzipRules rules;
        rules.least = 1;
        rules.minEntry = 0;
        rules.maxEntry = kMaxDownload;
        rules.maxTotal = 2 * kMaxDownload;
        rules.stripPrefix = top;
        std::vector<std::string> files;
        unzipBounded(zip, staging, rules, nullptr, &files);

        // this game, the offered version, not older than this one (Android's package, versionCode and signer checks)
        std::string version;
        readFile(staging + "/VERSION", &version, 256);
        const std::string problem = stagedProblem(files, version, r.tag, installed_, runningExeName());
        if (!problem.empty()) {
            deleteTree(staging);
            throw HttpError{0, problem};
        }

        // Every file aside (a running exe and a loaded DLL can be renamed), then the new ones in.
        std::vector<std::string> aside, placed;
        std::string failure;
        for (const std::string &f : files) {
            const std::string dest = dir_ + "/" + f;
            const size_t slash = dest.find_last_of('/');
            if (!makeDirs(dest.substr(0, slash))) { failure = "could not create the folder for " + f; break; }
            if (isFile(dest)) {
                removeFile(dest + ".old");
                if (!movePath(dest, dest + ".old", true)) { failure = "could not move " + f + " aside"; break; }
                aside.push_back(f);
            }
            if (!movePath(staging + "/" + f, dest, false)) { failure = "could not put " + f + " in place"; break; }
            placed.push_back(f);
        }
        if (!failure.empty()) {
            // everything as it was
            for (const std::string &f : placed) removeFile(dir_ + "/" + f);
            for (const std::string &f : aside) movePath(dir_ + "/" + f + ".old", dir_ + "/" + f, true);
            deleteTree(staging);
            throw HttpError{0, "Update failed: " + failure};
        }
        std::string old;
        for (const std::string &f : aside) old += (old.empty() ? "" : ";") + f + ".old";
        prefSet("updater.oldFiles", old);
        prefSet("updater.pendingVersion", normalize(r.tag));
        deleteTree(staging);
        sysLogPrintf(0, "updater: installed %s (%zu files, %zu replaced); restarting", r.tag.c_str(), files.size(), aside.size());
        std::lock_guard<std::mutex> lock(lock_);
        setState("installing", "Updated. Restarting.");
        restartPending_ = true;
    }

    /* The last update's leftovers: its download and the files it replaced. */
    void deleteDownloads()
    {
        deleteTree(dir_ + "/updates");
        std::string list = prefGet("updater.oldFiles");
        if (list.empty()) return;
        std::string keep;
        size_t start = 0;
        while (start <= list.size()) {
            size_t semi = list.find(';', start);
            if (semi == std::string::npos) semi = list.size();
            const std::string f = list.substr(start, semi - start);
            start = semi + 1;
            if (f.size() < 5 || f.compare(f.size() - 4, 4, ".old") != 0 || f.find("..") != std::string::npos) continue;
            const std::string path = dir_ + "/" + f;
            bool gone = !isFile(path);
            // the old exe may still be closing (pcRestartSelf starts this run first)
            for (int i = 0; i < 20 && !gone; i++) {
                gone = removeFile(path) || !isFile(path);
                if (!gone) std::this_thread::sleep_for(std::chrono::milliseconds(250));
            }
            if (!gone) keep += (keep.empty() ? "" : ";") + f;
        }
        prefSet("updater.oldFiles", keep);
    }

    std::string httpGet(const std::string &url)
    {
        HttpRequest q;
        q.url = url;
        q.agent = "GoldenEye-VR/" + normalize(installed_);
        q.connectMs = 10000;
        q.readMs = 20000;
        q.maxReply = 8 << 20;
        q.headers = { "Accept: application/vnd.github+json", "X-GitHub-Api-Version: 2022-11-28" };
        const HttpReply reply = httpSend(q);
        if (reply.status == 403 || reply.status == 429) throw HttpError{0, "GitHub's rate limit was reached; try again in an hour"};
        if (reply.status != 200) throw HttpError{0, "GitHub answered HTTP " + std::to_string(reply.status)};
        if (reply.body.size() >= q.maxReply) throw HttpError{0, "GitHub's answer is too large"};
        return reply.body;
    }

    std::mutex lock_;
    std::string state_ = "idle", message_;
    std::atomic<int> progress_{-1};    // percent while downloading, else -1
    Release offer_;                    // what "Update" installs
    bool hasOffer_ = false;
    std::string downloaded_;           // verified zip for offer_, if any
    std::string notice_;               // "Updated to v0.1.13.", shown while nothing else is
    std::atomic<bool> cancelled_{false};
    bool restartPending_ = false;
    const std::string installed_, dir_;
    SerialWorker worker_;              // last
};

UpdateChecker *s_updater;

UpdateChecker &updater()
{
    if (!s_updater) s_updater = new UpdateChecker();   // lives as long as the process, as the Quest's
    return *s_updater;
}

}   // namespace

std::string pcUpdaterStatus(void) { return updater().status(); }
void pcUpdaterCommand(const char *cmd) { updater().command(cmd ? cmd : ""); }
