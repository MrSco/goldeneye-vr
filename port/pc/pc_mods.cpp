/*
 * The launcher's Mods page on PC (docs/pc-platforms.md P3): a port of the
 * Quest's android/app/src/main/java/com/gevr/port/ModManager.java and
 * ModArchive.java.
 *
 * The launcher (port/vr/vr_launcher.cpp) reads modsStatus and sends
 * modsCommand through pc_services.cpp, as it reaches MainActivity on the Quest:
 *
 *   status: one line per pack, id \t title \t by \t site \t version \t size MB \t
 *           state \t progress \t message \t update version
 *   commands: check (once a run), install:<id> (also updates), remove:<id>, cancel
 *
 * Packs are downloaded from the same places (their authors' site, our GitHub
 * fork, the public MrSco/goldeneye-vr-mods releases) and unpacked into
 * texture-packs/<id> beside the exe: the PC's data folder, where the renderer
 * ($S/texture-packs, ext_tex.c and gfx_pc.cpp) and Oddjob's hat
 * (gevr_oddjobhat.c: texture-packs/oddjob-hat/oddjob_hat.bin) look.
 *
 * XBLA / Bean is never downloaded (ModManager's note): the player picks their own
 * copy of the release in Windows' Open dialog (pc_launcher.cpp pcOpenPicker, in
 * place of MainActivity.openXblaPicker / openCePicker) and importOwn / applyCe
 * run the Quest's native import (port/src/gevr_bean_import.c, gevr_bean_ce.c)
 * on it, reading the file by its path where Android hands over a descriptor.
 * texture-packs/ge007-bean is where the Bean loaders look on PC
 * (GEVR_BEAN_PACK_DIR, gevr_bean_ce.h).
 */

#include <string>
#include <vector>
#include <atomic>
#include <mutex>
#include <cstdio>
#include <cstring>

#include "pc_http.h"
#include "gevr_bean_import.h"
#include "gevr_bean_ce.h"

extern "C" void sysLogPrintf(int level, const char *fmt, ...);

using namespace pcnet;

namespace {

const char *const kMarker = ".gevr-pack";
const char *const kRoot = "texture-packs";   // files/texture-packs on the Quest; $S/texture-packs here

/* A version of a pack's zip: the list's own, or a newer release found on GitHub. */
struct Build {
    std::string version, url;
    long long size = 0;
};

struct Pack {
    std::string id, title, by, site;
    Build listed;
    // GitHub releases to follow (repo empty: none). The release's tag, less
    // tagPrefix and with '-' read as '.', is the version; its file is the
    // asset named assetPrefix...assetSuffix.
    std::string repo, tagPrefix, assetPrefix, assetSuffix;
    // What the zip must hold: the files ending so, at least least of them.
    // A mod (not a texture pack) shares its repository's releases with
    // other mods, so its newest release is its newest tag, not "latest".
    std::vector<std::string> files = { ".png" };
    int least = 100;
    bool mod = false;
    bool hasLatest = false;
    Build latest;

    bool own() const { return listed.url.empty(); }
};

bool sameVersion(const std::string &a, const std::string &b)
{
    std::string x = a, y = b;
    for (char &c : x) { if (c == '-') c = '.'; if (c >= 'A' && c <= 'Z') c = (char)(c + 32); }
    for (char &c : y) { if (c == '-') c = '.'; if (c >= 'A' && c <= 'Z') c = (char)(c + 32); }
    return x == y;
}

Pack pack(const char *id, const char *title, const char *by, const char *site, const char *version, const char *url,
          long long size, const char *repo, const char *tagPrefix, const char *assetPrefix, const char *assetSuffix)
{
    Pack p;
    p.id = id; p.title = title; p.by = by; p.site = site;
    p.listed.version = version; p.listed.url = url ? url : ""; p.listed.size = size;
    p.repo = repo ? repo : ""; p.tagPrefix = tagPrefix ? tagPrefix : "";
    p.assetPrefix = assetPrefix ? assetPrefix : ""; p.assetSuffix = assetSuffix ? assetSuffix : "";
    return p;
}

/* A mod's files from GoldenEye VR's mods repository (its release tags tagPrefix + version). */
Pack modPack(const char *id, const char *title, const char *by, const char *version, const char *url, long long size,
             const char *repo, const char *tagPrefix, const char *assetPrefix, const char *assetSuffix,
             std::vector<std::string> files)
{
    Pack p = pack(id, title, by, (std::string("github.com/") + repo).c_str(), version, url, size, repo, tagPrefix,
                  assetPrefix, assetSuffix);
    p.least = (int)files.size();
    p.files = std::move(files);
    p.mod = true;
    return p;
}

/* ModManager.PACKS, the same list (see there for each pack's story). */
std::vector<Pack> packList()
{
    return {
        pack("ge007-hd", "GoldenEye 007 HD", "intermissionfb and GhostlyDark", "evilgames.eu",
             "v2025.12.30",
             "https://evilgames.eu/files/texture-packs/ge007-hd-v2025.12.30-gliden64-png-hd.zip",
             135256079LL,
             "GhostlyDark/GoldenEye-007-HD", "", "ge007-hd-", "-gliden64-png-hd.zip"),
        pack("ge007-hd-ai", "GoldenEye 007 HD + AI", "intermissionfb and GhostlyDark, AI by GoldenEye VR",
             "github.com/MrSco", "2026.10.03.3",
             "https://github.com/MrSco/GoldenEye-007-HD/releases/download/ai-2026.10.03.3/ge007-hd-ai-2026.10.03.3-gliden64-png.zip",
             374042560LL,
             "MrSco/GoldenEye-007-HD", "ai-", "ge007-hd-ai-", "-gliden64-png.zip"),
        pack("ge007-bean", "GoldenEye 007 XBLA / Project Bean", "Rare / 4J Studios, Project Bean",
             "your own copy of the game", "", nullptr, 0, nullptr, nullptr, nullptr, nullptr),
        modPack("oddjob-hat", "Oddjob's hat", "Eurocom / EA (007 Nightfire), GoldenEye VR", "1.1",
                "https://github.com/MrSco/goldeneye-vr-mods/releases/download/oddjob-hat-1.1/oddjob-hat-1.1.zip",
                16626LL, "MrSco/goldeneye-vr-mods", "oddjob-hat-", "oddjob-hat-", ".zip", { "oddjob_hat.bin" }),
    };
}

std::string cleanTabs(std::string s)
{
    for (char &c : s) if (c == '\t' || c == '\n') c = ' ';
    return s;
}

class ModManager {
public:
    ModManager()
        : packs_(packList()), userAgent_(std::string("GoldenEye-VR/") + appVersion() + " (Windows)")
    {
        // a download or unpack the last run didn't finish
        worker_.post([this] { tidy(); });
    }

    std::string status()
    {
        std::lock_guard<std::mutex> lock(lock_);
        std::string sb;
        for (const Pack &p : packs_) {
            std::string state, msg;
            int pr = -1;
            const bool have = installed(p);
            if (p.id == busyId_) {
                state = busyState_;
                pr = state == "importing" ? gevrBeanImportPercent() : progress_.load();
            } else if (p.id == errorId_) {
                // a failed update leaves the installed pack in place (and usable)
                state = have ? "installed" : "error";
                msg = errorMessage_;
            } else {
                state = have ? "installed" : "none";
            }
            const Build &t = target(p);
            std::string version = t.version, update;
            if (p.own()) {
                version = have ? installedVersion(p) : "";
            } else if (have) {
                // what an install would get now (the list's, or a newer release)
                // against what the marker says is installed
                const std::string got = installedVersion(p);
                if (!got.empty()) {
                    if (!sameVersion(t.version, got)) update = t.version;
                    version = got;
                }
            }
            if (!sb.empty()) sb += '\n';
            sb += p.id + "\t" + cleanTabs(p.title) + "\t" + cleanTabs(p.by) + "\t" + p.site + "\t" + cleanTabs(version) + "\t"
                + std::to_string((t.size + (1 << 19)) >> 20) + "\t" + state + "\t" + std::to_string(pr) + "\t"
                + cleanTabs(msg) + "\t" + cleanTabs(update);
        }
        return sb;
    }

    /* "check" (once a run), "install:<id>" (also updates), "remove:<id>", "cancel". */
    void command(const std::string &cmd)
    {
        std::lock_guard<std::mutex> lock(lock_);
        if (cmd == "cancel") {
            cancelled_ = true;
            if (busyState_ == "importing") gevrBeanImportCancel();
            return;
        }
        if (cmd == "check") {
            if (!checked_) {
                checked_ = true;
                worker_.post([this] { checkLatest(); });
            }
            return;
        }
        const size_t colon = cmd.find(':');
        if (colon == std::string::npos) return;
        Pack *p = find(cmd.substr(colon + 1));
        if (!p || !busyId_.empty()) return;
        const std::string verb = cmd.substr(0, colon);
        const std::string id = p->id;
        if (verb == "install" && !p->own()) {
            errorId_.clear();
            cancelled_ = false;
            progress_ = 0;
            busyState_ = "downloading";
            busyId_ = id;
            worker_.post([this, id] { run(id, [this, id] { install(id); }); });
        } else if (verb == "remove") {
            errorId_.clear();
            progress_ = -1;
            busyState_ = "installing";
            busyId_ = id;
            worker_.post([this, id] {
                run(id, [id] {
                    deleteTree(std::string(kRoot) + "/" + id);
                    sysLogPrintf(0, "mods: removed %s", id.c_str());
                });
            });
        }
    }

    /* The file the player picked for an own-copy pack (MainActivity.openXblaPicker). */
    void importOwn(const std::string &id, const std::string &path)
    {
        std::lock_guard<std::mutex> lock(lock_);
        Pack *p = find(id);
        if (!p || !p->own() || !busyId_.empty()) return;
        errorId_.clear();
        cancelled_ = false;
        progress_ = 0;
        busyState_ = "importing";
        busyId_ = id;
        worker_.post([this, id, path] { run(id, [this, id, path] { importFrom(id, path); }); });
    }

    /*
     * The Community Edition updater the player picked (MainActivity.openCePicker):
     * applied to the imported Bean copy, into its ce/ folder (port/src/gevr_bean_ce.c).
     */
    void applyCe(const std::string &path)
    {
        std::lock_guard<std::mutex> lock(lock_);
        Pack *p = find("ge007-bean");
        if (!p || !installed(*p) || !busyId_.empty()) return;
        errorId_.clear();
        cancelled_ = false;
        progress_ = -1;
        busyState_ = "patching";
        busyId_ = p->id;
        const std::string id = p->id, beanDir = dir(*p);
        worker_.post([this, id, beanDir, path] {
            run(id, [id, beanDir, path] {
                FILE *f = openFile(path, "rb");
                if (!f) throw HttpError{0, "could not open that file"};
                char msg[256] = "";
                // the native side reads and closes it
                if (gevrBeanCeApply(f, beanDir.c_str(), msg, sizeof(msg)) < 0) throw HttpError{0, msg[0] ? msg : "failed"};
                sysLogPrintf(0, "mods: applied the Community Edition to %s", id.c_str());
            });
        });
    }

private:
    /* ModManager.importFrom: the copy into a hidden folder, then into place. */
    void importFrom(const std::string &id, const std::string &path)
    {
        if (!isDir(kRoot) && !makeDirs(kRoot)) throw HttpError{0, std::string("could not create ") + kRoot};
        const std::string tmp = std::string(kRoot) + "/." + id + ".unpacking";
        const std::string dest = std::string(kRoot) + "/" + id;
        const std::string backup = std::string(kRoot) + "/." + id + ".backup";
        try {
            deleteTree(tmp);
            if (!makeDirs(tmp)) throw HttpError{0, "could not create " + tmp};
            FILE *f = openFile(path, "rb");
            if (!f) throw HttpError{0, "could not open that file"};
            char msg[256] = "";
            // the native side reads and closes it; it writes with the C runtime's
            // fopen and mkdir, so the folder stays relative (the game's folder
            // may have a name those can't open)
            if (gevrBeanImportStream(f, tmp.c_str(), msg, sizeof(msg)) < 0) throw HttpError{0, msg[0] ? msg : "failed"};
            if (!writeFileAtomic(tmp + "/" + kMarker, "own\n")) throw HttpError{0, "could not write the pack's marker"};
            deleteTree(backup);
            if (isDir(dest) && !movePath(dest, backup, false)) throw HttpError{0, "could not preserve installed pack"};
            if (!movePath(tmp, dest, false)) {
                if (isDir(backup) && !movePath(backup, dest, false)) sysLogPrintf(1, "mods: could not restore %s", id.c_str());
                throw HttpError{0, "could not finish importing"};
            }
            deleteTree(backup);
            sysLogPrintf(0, "mods: imported %s from the player's copy", id.c_str());
        } catch (...) {
            deleteTree(tmp);
            throw;
        }
        deleteTree(tmp);
    }

    Pack *find(const std::string &id)
    {
        for (Pack &p : packs_) if (p.id == id) return &p;
        return nullptr;
    }

    static std::string dir(const Pack &p) { return std::string(kRoot) + "/" + p.id; }
    static bool installed(const Pack &p) { return isFile(dir(p) + "/" + kMarker); }

    /* The version an install wrote into the pack's marker ("" if none). */
    static std::string installedVersion(const Pack &p)
    {
        std::string text;
        if (!readFile(dir(p) + "/" + kMarker, &text, 4096)) return std::string();
        const size_t nl = text.find('\n');
        if (nl != std::string::npos) text.resize(nl);
        while (!text.empty() && (text.back() == '\r' || text.back() == ' ' || text.back() == '\t')) text.pop_back();
        while (!text.empty() && (text[0] == ' ' || text[0] == '\t')) text.erase(0, 1);
        return text;
    }

    /* What an install gets: a release newer than the list's, or the list's. */
    static const Build &target(const Pack &p)
    {
        return p.hasLatest && !sameVersion(p.latest.version, p.listed.version) ? p.latest : p.listed;
    }

    void run(const std::string &id, const std::function<void()> &job)
    {
        try {
            job();
        } catch (const HttpError &e) {
            failed(id, e.message);
        } catch (...) {
            failed(id, "failed");
        }
        std::lock_guard<std::mutex> lock(lock_);
        progress_ = -1;
        busyState_ = "none";
        busyId_.clear();
    }

    void failed(const std::string &id, const std::string &why)
    {
        std::lock_guard<std::mutex> lock(lock_);
        if (cancelled_) {
            sysLogPrintf(0, "mods: %s cancelled", id.c_str());
        } else {
            sysLogPrintf(1, "mods: %s failed: %s", id.c_str(), why.c_str());
            errorMessage_ = why;
            errorId_ = id;
        }
    }

    void setBusy(const char *state, int progress)
    {
        std::lock_guard<std::mutex> lock(lock_);
        busyState_ = state;
        progress_ = progress;
    }

    /* ---- install ---- */

    void install(const std::string &id)
    {
        Build b;
        std::vector<std::string> files;
        int least;
        std::string site;
        {
            std::lock_guard<std::mutex> lock(lock_);
            const Pack &p = *find(id);
            b = target(p);
            files = p.files;
            least = p.least;
            site = p.site;
        }
        if (!isDir(kRoot) && !makeDirs(kRoot)) throw HttpError{0, std::string("could not create ") + kRoot};
        // the zip and the unpacked files side by side (with an old version of the
        // pack still there during an update), with room to spare
        const long long need = b.size * 23 / 10;
        const long long free = freeSpace(kRoot);
        if (free >= 0 && free < need) {
            throw HttpError{0, "not enough free space (" + std::to_string(need >> 20) + " MB needed)"};
        }
        const std::string zip = std::string(kRoot) + "/." + id + ".zip";
        const std::string tmp = std::string(kRoot) + "/." + id + ".unpacking";
        const std::string dest = std::string(kRoot) + "/" + id;
        const std::string backup = std::string(kRoot) + "/." + id + ".backup";
        try {
            download(site, b, zip);
            setBusy("installing", 0);
            deleteTree(tmp);
            UnzipRules rules;
            rules.kinds = files;
            rules.least = least;
            const int n = unzipBounded(zip, tmp, rules, [this](int percent) {
                if (cancelled_) return false;
                progress_ = percent;
                return true;
            });
            if (n < least) throw HttpError{0, "the download holds " + std::to_string(n) + " files, not the pack"};
            if (!writeFileAtomic(tmp + "/" + kMarker, b.version + "\n")) throw HttpError{0, "could not write the pack's marker"};
            deleteTree(backup);
            if (isDir(dest) && !movePath(dest, backup, false)) throw HttpError{0, "could not preserve installed pack"};
            if (!movePath(tmp, dest, false)) {
                if (isDir(backup) && !movePath(backup, dest, false)) sysLogPrintf(1, "mods: could not restore %s", id.c_str());
                throw HttpError{0, "could not finish installing"};
            }
            deleteTree(backup);
            sysLogPrintf(0, "mods: installed %s %s: %d files", id.c_str(), b.version.c_str(), n);
        } catch (...) {
            removeFile(zip);
            deleteTree(tmp);
            throw;
        }
        removeFile(zip);
        deleteTree(tmp);
    }

    void download(const std::string &site, const Build &b, const std::string &out)
    {
        FILE *f = openFile(out, "wb");
        if (!f) throw HttpError{0, "could not write " + out};
        long long got = 0;
        bool tooBig = false, writeFailed = false;
        HttpRequest r;
        r.url = b.url;
        r.agent = userAgent_;
        r.connectMs = 15000;
        r.readMs = 30000;
        r.sink = [&](const char *data, size_t n) {
            if (cancelled_) return false;
            if (fwrite(data, 1, n, f) != n) { writeFailed = true; return false; }
            got += (long long)n;
            if (got > b.size) { tooBig = true; return false; }
            progress_ = (int)std::min<long long>(99, got * 100 / std::max<long long>(1, b.size));
            return true;
        };
        HttpReply reply;
        try {
            reply = httpSend(r);
        } catch (const HttpError &e) {
            fclose(f);
            if (tooBig) throw HttpError{0, "the download is bigger than expected"};
            if (writeFailed) throw HttpError{0, "could not write the download (disk full?)"};
            throw;
        }
        const bool closed = fclose(f) == 0;
        if (reply.status != 200) throw HttpError{0, site + " answered HTTP " + std::to_string(reply.status)};
        if (!closed) throw HttpError{0, "could not write the download (disk full?)"};
        // the size the list (or the release) gave: a changed file is not the pack we checked
        if (got != b.size) {
            throw HttpError{0, "the download is " + std::to_string(got) + " bytes, expected " + std::to_string(b.size)
                               + " (the pack may have been updated)"};
        }
    }

    /* ---- updates ---- */

    /* Each followed pack's latest GitHub release and its file; a failure only logs. */
    void checkLatest()
    {
        std::vector<Pack> packs;
        {
            std::lock_guard<std::mutex> lock(lock_);
            packs = packs_;
        }
        for (const Pack &p : packs) {
            if (p.repo.empty()) continue;
            try {
                std::string text;
                jspan o;
                bool found = false;
                if (p.mod) {
                    // the mods repository's newest release of this mod (newest first)
                    text = httpGet("https://api.github.com/repos/" + p.repo + "/releases?per_page=50");
                    jspan all, r;
                    if (!jsonParseArray(text, &all)) throw HttpError{0, "bad reply"};
                    for (int i = 0; !found && jsonAt(all, i, &r); i++) {
                        if (!jsonGetBool(r, "draft", false) && !jsonGetBool(r, "prerelease", false)
                            && jsonGetStr(r, "tag_name").compare(0, p.tagPrefix.size(), p.tagPrefix) == 0) {
                            o = r;
                            found = true;
                        }
                    }
                    if (!found) throw HttpError{0, "no release of it yet"};
                } else {
                    text = httpGet("https://api.github.com/repos/" + p.repo + "/releases/latest");
                    if (!jsonParse(text, &o)) throw HttpError{0, "bad reply"};
                }
                std::string tag = jsonGetStr(o, "tag_name");
                if (tag.compare(0, p.tagPrefix.size(), p.tagPrefix) == 0) tag = tag.substr(p.tagPrefix.size());
                std::string version = tag;
                for (char &c : version) if (c == '-') c = '.';
                jspan assets, a;
                Build latest;
                bool have = false;
                if (jsonGet(o, "assets", &assets)) {
                    for (int i = 0; jsonAt(assets, i, &a); i++) {
                        const std::string name = jsonGetStr(a, "name");
                        const long long size = jsonGetInt(a, "size", -1);
                        const std::string url = jsonGetStr(a, "browser_download_url");
                        if (name.compare(0, p.assetPrefix.size(), p.assetPrefix) == 0 && name.size() >= p.assetSuffix.size()
                            && name.compare(name.size() - p.assetSuffix.size(), p.assetSuffix.size(), p.assetSuffix) == 0
                            && size > 0 && !url.empty()) {
                            latest = Build{version, url, size};
                            have = true;
                            break;
                        }
                    }
                }
                if (have) {
                    std::lock_guard<std::mutex> lock(lock_);
                    Pack *live = find(p.id);
                    live->latest = latest;
                    live->hasLatest = true;
                }
                sysLogPrintf(0, "mods: %s: latest release %s", p.id.c_str(), have ? latest.version.c_str() : "has no pack file");
            } catch (const HttpError &e) {
                sysLogPrintf(1, "mods: %s: could not look for a newer release: %s", p.id.c_str(), e.message.c_str());
            }
        }
    }

    std::string httpGet(const std::string &url)
    {
        HttpRequest r;
        r.url = url;
        r.agent = userAgent_;
        r.connectMs = 10000;
        r.readMs = 20000;
        r.maxReply = 8 << 20;
        r.headers = { "Accept: application/vnd.github+json", "X-GitHub-Api-Version: 2022-11-28" };
        const HttpReply reply = httpSend(r);
        if (reply.status != 200) throw HttpError{0, "GitHub answered HTTP " + std::to_string(reply.status)};
        return reply.body;
    }

    /* ---- helpers ---- */

    void tidy()
    {
        for (const std::string &name : listDir(kRoot)) {
            const std::string path = std::string(kRoot) + "/" + name;
            if (name.size() > 8 && name[0] == '.' && name.compare(name.size() - 7, 7, ".backup") == 0) {
                const std::string dest = std::string(kRoot) + "/" + name.substr(1, name.size() - 8);
                if (!isDir(dest) && !isFile(dest)) {
                    if (!movePath(path, dest, false)) sysLogPrintf(1, "mods: could not recover %s", dest.c_str());
                    continue;
                }
            }
            if (name[0] == '.') deleteTree(path);
        }
    }

    std::mutex lock_;
    std::vector<Pack> packs_;
    const std::string userAgent_;
    std::string busyId_;                 // the pack being downloaded or installed
    std::string busyState_ = "none";
    std::atomic<int> progress_{-1};
    std::atomic<bool> cancelled_{false};
    std::string errorId_, errorMessage_;
    bool checked_ = false;               // the latest releases were looked up this run
    SerialWorker worker_;                // last: its thread starts once the state above exists
};

ModManager *s_mods;

ModManager &mods()
{
    if (!s_mods) s_mods = new ModManager();   // lives as long as the process, as the Quest's
    return *s_mods;
}

}   // namespace

std::string pcModsStatus(void) { return mods().status(); }
void pcModsCommand(const char *cmd) { mods().command(cmd ? cmd : ""); }
void pcModsImportOwn(const char *id, const std::string &path) { mods().importOwn(id ? id : "", path); }
void pcModsApplyCe(const std::string &path) { mods().applyCe(path); }
