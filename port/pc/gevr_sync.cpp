/*
 * Cross-platform sync (gevr_sync.h, docs/cloud-sync.md): the files around
 * gevr_sync_core.cpp's rules. At launch: every device's record read, the
 * winners written into the settings files and saves, this device's record
 * written back. After the game writes a file: this device's record, in the
 * background. Nothing runs until a store is linked.
 *
 * In the save folder, .gevr-sync/ keeps what this device remembers:
 *   id.txt      this device's id (random, made once)
 *   state.txt   the last sync (gsync::Baseline)
 *   mine.txt    this device's record as last written
 *   folder.txt  a folder used as the store (development; GEVR_SYNC_DIR too)
 */
#include "gevr_sync_core.h"
#include "gevr_sync_store.h"
#if !defined(GEVR_SYNC_TEST) || defined(GEVR_SYNC_DRIVE)
#define GEVR_SYNC_FILES 1   // ROMs and packs (gevr_sync_files.cpp, the PC services' files)
#include "gevr_sync_files.h"
#include "pc_http.h"
#endif

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <memory>
#include <mutex>
#include <random>
#include <string>
#include <thread>
#include <vector>

#include <sys/stat.h>
#ifdef _WIN32
#include <windows.h>
#include <direct.h>
#include <sys/utime.h>
#else
#include <dirent.h>
#include <sys/time.h>
#include <unistd.h>
#endif

#ifndef GEVR_SYNC_TEST
extern "C" {
#include <PR/ultratypes.h>
#include "fs.h"
#include "system.h"
}
#include "gevr_icloud.h"
#include <SDL.h>   // SDL_OpenURL: the sign-in page in the device's browser
#endif
#include "gevr_sync.h"

namespace gsync {

/* ---------------------------------------------------------------- files */

#ifdef _WIN32
static std::wstring W(const std::string &s)
{
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &w[0], n);
    return w;
}
static std::string N(const wchar_t *w)
{
    const int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
    std::string s(n > 0 ? n - 1 : 0, '\0');
    if (n > 1) WideCharToMultiByte(CP_UTF8, 0, w, -1, &s[0], n, nullptr, nullptr);
    return s;
}
#endif

static FILE *openFile(const std::string &path, const char *mode)
{
#ifdef _WIN32
    return _wfopen(W(path).c_str(), W(mode).c_str());
#else
    return fopen(path.c_str(), mode);
#endif
}

static bool readFile(const std::string &path, std::string *out)
{
    out->clear();
    FILE *f = openFile(path, "rb");
    if (!f) return false;
    char buf[65536];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) out->append(buf, n);
    const bool ok = !ferror(f);
    fclose(f);
    return ok;
}

static bool replaceFile(const std::string &from, const std::string &to)
{
#ifdef _WIN32
    return MoveFileExW(W(from).c_str(), W(to).c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
#else
    return rename(from.c_str(), to.c_str()) == 0;
#endif
}

static bool removeFile(const std::string &path)
{
#ifdef _WIN32
    return DeleteFileW(W(path).c_str()) != 0;
#else
    return unlink(path.c_str()) == 0;
#endif
}

static bool writeFileAtomic(const std::string &path, const std::string &data)
{
    const std::string tmp = path + ".sync-tmp";
    FILE *f = openFile(tmp, "wb");
    if (!f) return false;
    bool ok = fwrite(data.data(), 1, data.size(), f) == data.size();
    ok = fflush(f) == 0 && ok;
#ifndef _WIN32
    ok = fsync(fileno(f)) == 0 && ok;
#endif
    ok = fclose(f) == 0 && ok;
    if (ok && replaceFile(tmp, path)) return true;
    removeFile(tmp);
    return false;
}

static void makeDir(const std::string &path)
{
#ifdef _WIN32
    CreateDirectoryW(W(path).c_str(), nullptr);
#else
    mkdir(path.c_str(), 0755);
#endif
}

static std::vector<std::string> listDir(const std::string &dir)
{
    std::vector<std::string> out;
#ifdef _WIN32
    WIN32_FIND_DATAW d;
    HANDLE h = FindFirstFileW(W(dir + "/*").c_str(), &d);
    if (h == INVALID_HANDLE_VALUE) return out;
    do {
        if (!(d.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) out.push_back(N(d.cFileName));
    } while (FindNextFileW(h, &d));
    FindClose(h);
#else
    if (DIR *d = opendir(dir.c_str())) {
        while (struct dirent *e = readdir(d)) {
            if (e->d_name[0] == '.' && (!e->d_name[1] || (e->d_name[1] == '.' && !e->d_name[2]))) continue;
            out.push_back(e->d_name);
        }
        closedir(d);
    }
#endif
    std::sort(out.begin(), out.end());
    return out;
}

static bool isDir(const std::string &path)
{
#ifdef _WIN32
    const DWORD a = GetFileAttributesW(W(path).c_str());
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
#else
    struct stat st;
    return stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
#endif
}

static long long fileSize(const std::string &path)
{
#ifdef _WIN32
    WIN32_FILE_ATTRIBUTE_DATA a;
    if (!GetFileAttributesExW(W(path).c_str(), GetFileExInfoStandard, &a)) return -1;
    return (long long)(((unsigned long long)a.nFileSizeHigh << 32) | a.nFileSizeLow);
#else
    struct stat st;
    return stat(path.c_str(), &st) == 0 ? (long long)st.st_size : -1;
#endif
}

static long long fileTimeMs(const std::string &path)
{
#ifdef _WIN32
    WIN32_FILE_ATTRIBUTE_DATA a;
    if (!GetFileAttributesExW(W(path).c_str(), GetFileExInfoStandard, &a)) return 0;
    const unsigned long long t = ((unsigned long long)a.ftLastWriteTime.dwHighDateTime << 32) | a.ftLastWriteTime.dwLowDateTime;
    return (long long)(t / 10000ULL) - 11644473600000LL;
#else
    struct stat st;
    if (stat(path.c_str(), &st) != 0) return 0;
#if defined(__APPLE__)
    return (long long)st.st_mtimespec.tv_sec * 1000 + st.st_mtimespec.tv_nsec / 1000000;
#else
    return (long long)st.st_mtim.tv_sec * 1000 + st.st_mtim.tv_nsec / 1000000;
#endif
#endif
}

static void setFileTimeMs(const std::string &path, long long ms)
{
    if (ms <= 0) return;
#ifdef _WIN32
    struct _utimbuf t;
    t.actime = t.modtime = (time_t)(ms / 1000);
    _wutime(W(path).c_str(), &t);
#else
    struct timeval tv[2];
    tv[0].tv_sec = tv[1].tv_sec = (time_t)(ms / 1000);
    tv[0].tv_usec = tv[1].tv_usec = (suseconds_t)(ms % 1000) * 1000;
    utimes(path.c_str(), tv);
#endif
}

static long long wallMs()
{
    return (long long)std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::system_clock::now().time_since_epoch()).count();
}

static std::string trimmed(std::string s)
{
    while (!s.empty() && strchr(" \t\r\n", s.back())) s.pop_back();
    size_t b = 0;
    while (b < s.size() && strchr(" \t\r\n", s[b])) b++;
    return s.substr(b);
}

/* ---------------------------------------------------------- folder store */

namespace {

class FolderStore : public Store {
public:
    explicit FolderStore(const std::string &dir) : dir_(dir) {}
    std::string label() const override { return "folder " + dir_; }
    bool list(std::vector<std::string> *names, std::string *why) override
    {
        if (!isDir(dir_)) {
            *why = "no folder " + dir_;
            return false;
        }
        *names = listDir(dir_);
        return true;
    }
    bool read(const std::string &name, std::string *data, std::string *why) override
    {
        if (readFile(dir_ + "/" + name, data)) return true;
        *why = "could not read " + name;
        return false;
    }
    bool write(const std::string &name, const std::string &data, std::string *why) override
    {
        if (writeFileAtomic(dir_ + "/" + name, data)) return true;
        *why = "could not write " + name;
        return false;
    }
    /* blobs: "blob-<id>" beside the records */
    bool listBlobs(std::map<std::string, long long> *blobs, std::string *why) override
    {
        if (!isDir(dir_)) {
            *why = "no folder " + dir_;
            return false;
        }
        blobs->clear();
        for (const std::string &n : listDir(dir_)) {
            if (n.compare(0, 5, "blob-") != 0 || n.find(".sync-tmp") != std::string::npos) continue;
            (*blobs)[n.substr(5)] = fileSize(dir_ + "/" + n);
        }
        return true;
    }
    bool putBlob(const std::string &id, const std::string &, const std::string &localPath, const Progress &progress,
                 std::string *why) override
    {
        return copyFile(localPath, dir_ + "/blob-" + id, progress, why);
    }
    bool getBlob(const std::string &id, const std::string &localPath, const Progress &progress,
                 std::string *why) override
    {
        return copyFile(dir_ + "/blob-" + id, localPath, progress, why);
    }
    bool removeBlob(const std::string &id, std::string *why) override
    {
        if (removeFile(dir_ + "/blob-" + id)) return true;
        *why = "could not remove blob " + id;
        return false;
    }

private:
    static bool copyFile(const std::string &from, const std::string &to, const Progress &progress, std::string *why)
    {
        FILE *in = openFile(from, "rb");
        if (!in) {
            *why = "could not read " + from;
            return false;
        }
        const long long total = fileSize(from);
        const std::string tmp = to + ".sync-tmp";
        FILE *out = openFile(tmp, "wb");
        bool ok = out != nullptr;
        long long done = 0;
        std::vector<char> buf(1 << 20);
        size_t n;
        while (ok && (n = fread(buf.data(), 1, buf.size(), in)) > 0) {
            ok = fwrite(buf.data(), 1, n, out) == n;
            done += (long long)n;
            if (ok && progress && !progress(done, total)) {
                ok = false;
                *why = "stopped";
            }
        }
        ok = ok && !ferror(in);
        fclose(in);
        if (out) ok = fclose(out) == 0 && ok;
        if (ok && replaceFile(tmp, to)) return true;
        removeFile(tmp);
        if (why->empty()) *why = "could not copy to " + to;
        return false;
    }
    std::string dir_;
};

}   // namespace

std::unique_ptr<Store> folderStore(const std::string &dir)
{
    return std::unique_ptr<Store>(new FolderStore(dir));
}

/* ----------------------------------------------------------------- state */

namespace {

#ifdef GEVR_SYNC_TEST
void syncLog(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    fprintf(stderr, "sync: ");
    vfprintf(stderr, fmt, ap);
    fprintf(stderr, "\n");
    va_end(ap);
}
#else
void syncLog(const char *fmt, ...)
{
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    sysLogPrintf(LOG_NOTE, "sync: %s", buf);
}
#endif

std::mutex g_lock;            // everything below
bool g_launched = false;
bool g_active = false;        // a store is linked and the launch sync ran
std::string g_root;           // the save folder ($S): goldeneye.ini, the saves, texture-packs, .gevr-sync
std::string g_gameDir;        // the game's folder ($B, the working folder): goldeneye-vr.ini, the ROMs
std::string g_kind;
std::shared_ptr<Store> g_store;   // shared: a push holds it while the player unlinks
DeviceRecord g_mine;
Baseline g_base;
std::string g_status, g_message;
bool g_pushQueued = false;
bool g_romArrived = false;     // a ROM came: the launcher scans again
long long g_saveCheckedMs = 0; // when the others' saves were last read (steady clock; 0: not since launch)

// the save check at START (gevrSyncSaveCheck*)
int g_checkState = 0;          // GEVR_SAVECHECK_*
unsigned g_checkAttempt = 0;   // a newer check (or Play now) drops an older one's answer
long long g_checkStarted = 0;
std::vector<SaveWrite> g_checkWrites;   // the newer saves found
std::string g_checkFrom;
long long g_checkWhen = 0;     // ms since 1970
bool g_checkCloud = false;     // the newer one is iCloud's (Apple)

// ROMs and packs: the background worker (filesWorker)
std::condition_variable g_filesWake;
bool g_filesKick = false;
[[maybe_unused]] bool g_filesStop = false, g_filesRunning = false;   // (unused in the offline test build)
bool g_filesBusy = false;      // a transfer under way
float g_filesDone = -1;        // its share, -1 unknown
std::string g_filesLine;       // "Sync: uploading ge.z64"

std::string statePath(const std::string &name) { return g_root + "/.gevr-sync/" + name; }

const char *buildKind()
{
#if defined(GEVR_VISIONOS)
    return "vision";
#elif defined(GEVR_TVOS)
    return "tv";
#elif defined(GEVR_IOS)
    return "ios";
#elif defined(ANDROID) && defined(GEVR_FRAME)
    return "frame";
#elif defined(ANDROID)
    return "quest";
#elif defined(GEVR_PCVR)
    return "pcvr";
#else
    return "pc";
#endif
}

const char *kindLabel(const std::string &k)
{
    if (k == "quest") return "Quest";
    if (k == "frame") return "Steam Frame";
    if (k == "pcvr") return "PC VR";
    if (k == "pc") return "PC";
    if (k == "ios") return "iPhone or iPad";
    if (k == "tv") return "Apple TV";
    if (k == "vision") return "Vision Pro";
    return "Device";
}

std::string deviceId()
{
    std::string id;
    if (readFile(statePath("id.txt"), &id)) {
        id = trimmed(id);
        if (id.size() == 16 && id.find_first_not_of("0123456789abcdef") == std::string::npos) return id;
    }
    std::random_device rd;
    char buf[17];
    snprintf(buf, sizeof(buf), "%08x%08x", (unsigned)rd(), (unsigned)rd());
    id = buf;
    writeFileAtomic(statePath("id.txt"), id + "\n");
    return id;
}

/* the store this device syncs with, none when nothing is linked */
std::unique_ptr<Store> linkedStore()
{
    const char *env = getenv("GEVR_SYNC_DIR");
    std::string dir = env ? env : "";
    if (dir.empty()) {
        readFile(statePath("folder.txt"), &dir);
        dir = trimmed(dir);
    }
    if (!dir.empty()) return folderStore(dir);
#if !defined(GEVR_SYNC_TEST) || defined(GEVR_SYNC_DRIVE)
    return driveStore(g_root + "/.gevr-sync");   // null when this device isn't signed in
#else
    return nullptr;
#endif
}

/* goldeneye-vr.ini is the working folder's (VR_INI_PATH), goldeneye.ini the save
 * folder's (CONFIG_PATH "$S/"): one place on the PC, two on the Quest (files/data
 * and files) */
std::string settingsPath(int f)
{
    return (f == FILE_INI ? g_gameDir : g_root) + "/" + kFileNames[f];
}

LocalState readLocal()
{
    LocalState l;
    for (int f = 0; f < FILE_COUNT; f++) l.have[f] = readFile(settingsPath(f), &l.text[f]);
    for (const std::string &n : listDir(g_root)) {
        if (!isSaveName(n)) continue;
        std::string data;
        if (readFile(g_root + "/" + n, &data)) {
            l.saves[n] = data;
            l.saveTimes[n] = fileTimeMs(g_root + "/" + n);
        }
    }
    return l;
}

void backupSave(const std::string &name, const std::string &data)
{
    char stamp[32];
    const time_t now = time(nullptr);
    struct tm tm;
#ifdef _WIN32
    localtime_s(&tm, &now);
#else
    localtime_r(&now, &tm);
#endif
    strftime(stamp, sizeof(stamp), "%Y%m%d-%H%M%S", &tm);
    const std::string prefix = name + ".before-sync-";
    writeFileAtomic(g_root + "/" + prefix + stamp, data);
    std::vector<std::string> old;
    for (const std::string &n : listDir(g_root)) {
        if (n.compare(0, prefix.size(), prefix) == 0) old.push_back(n);
    }
    std::sort(old.begin(), old.end());
    for (size_t i = 0; i + 5 < old.size(); i++) removeFile(g_root + "/" + old[i]);
}

void keepState(const Baseline &base, const DeviceRecord &mine)
{
    writeFileAtomic(statePath("state.txt"), baselineText(base));
    writeFileAtomic(statePath("mine.txt"), recordText(mine));
}

/* this device's record to the store; false leaves it for the next try */
bool upload(Store *store, const DeviceRecord &mine)
{
    std::string why;
    if (store->write(recordName(mine.id), recordText(mine), &why)) return true;
    syncLog("could not upload this device's record: %s", why.c_str());
    return false;
}

void notifyWritten(const std::string &path)
{
#ifndef GEVR_SYNC_TEST
    gevrCloudFileSaved(path.c_str());   // iCloud carries it to the other Apple devices
#else
    (void)path;
#endif
}

long long steadyMs()
{
    return (long long)std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now().time_since_epoch()).count();
}

/* Every device's record; false (and why) when one can't be read: a partial
 * view could put this device's values over one it couldn't read. */
bool readRecords(Store *store, const std::string &mineId, std::vector<DeviceRecord> *others, std::string *storedMine,
                 std::string *why)
{
    std::vector<std::string> names;
    if (!store->list(&names, why)) return false;
    for (const std::string &n : names) {
        std::string id;
        if (!recordNameId(n, &id)) continue;
        std::string text;
        if (!store->read(n, &text, why)) {
            *why = "could not read " + n + " (" + *why + ")";
            return false;
        }
        if (id == mineId) {
            if (storedMine) *storedMine = text;
            continue;
        }
        DeviceRecord r;
        if (recordParse(text, &r) && r.id == id) others->push_back(r);
    }
    return true;
}

/* The launch sync, g_lock held. */
void launchSync()
{
    std::string why;
    std::vector<DeviceRecord> others;
    std::string storedMine;   // this device's record as the store has it
    if (!readRecords(g_store.get(), g_mine.id, &others, &storedMine, &why)) {
        syncLog("could not reach the store (%s): nothing changed", why.c_str());
        g_status = "Sync: couldn't check your other devices' settings and saves";
        return;
    }
    g_saveCheckedMs = steadyMs();
    if (g_mine.files[0].empty() && g_mine.files[1].empty() && g_mine.saves.empty() && !storedMine.empty()) {
        DeviceRecord r;
        if (recordParse(storedMine, &r) && r.id == g_mine.id) {   // our cache was lost
            r.kind = g_mine.kind;
            r.version = g_mine.version;
            g_mine = r;
        }
    }

    const LocalState local = readLocal();
    const long long now = wallMs();
    const bool joining = !g_base.joined;
    Outcome o = sync(g_mine, others, local, g_base, now, true);

    // the files first: a crash after them leaves the next launch to stamp them as this device's (harmless)
    for (int f = 0; f < FILE_COUNT; f++) {
        if (!o.fileChanged[f]) continue;
        const std::string path = settingsPath(f);
        if (!writeFileAtomic(path, o.text[f])) {
            syncLog("could not write %s: nothing changed", kFileNames[f]);
            return;
        }
        notifyWritten(path);
    }
    for (const SaveWrite &w : o.saves) {
        const std::string path = g_root + "/" + w.name;
        auto l = local.saves.find(w.name);
        if (w.backup && l != local.saves.end()) backupSave(w.name, l->second);
        if (!writeFileAtomic(path, w.data)) {
            syncLog("could not write %s", w.name.c_str());
            o.base.saveHash.erase(w.name);   // compared again next time
            continue;
        }
        setFileTimeMs(path, w.t);
        notifyWritten(path);
        syncLog("%s from %s (written %lld)", w.name.c_str(), w.fromDevice.c_str(), w.t);
    }

    o.mine.seen = now;
    g_mine = o.mine;
    g_base = o.base;
    keepState(g_base, g_mine);
    // the store's copy is replaced when it differs (a change, an upload that
    // failed before) or is a day old (the devices list shows when each was seen)
    DeviceRecord stored;
    const bool storedOk = !storedMine.empty() && recordParse(storedMine, &stored);
    DeviceRecord cmp = g_mine;
    cmp.seen = storedOk ? stored.seen : 0;
    if (o.mineChanged || !storedOk || now - stored.seen > 24LL * 3600 * 1000 || recordText(cmp) != storedMine) {
        upload(g_store.get(), g_mine);
    }

    char line[160];
    if (o.settingsTaken) {
        snprintf(line, sizeof(line), "Sync: %d setting%s from your other devices", o.settingsTaken,
                 o.settingsTaken == 1 ? "" : "s");
    } else if (!o.saves.empty()) {
        snprintf(line, sizeof(line), "Sync: your save from %s", o.saves[0].fromDevice.c_str());
    } else {
        snprintf(line, sizeof(line), "Sync: up to date (%d other device%s)", (int)others.size(),
                 others.size() == 1 ? "" : "s");
    }
    g_status = line;
    if (!o.messages.empty()) g_message = o.messages[0];
    g_active = true;
    syncLog("%s%s (%s, %zu other devices, %d settings and %zu saves in)", joining ? "joined: " : "", line,
            g_kind.c_str(), others.size(), o.settingsTaken, o.saves.size());
}

/* After the game wrote a file: this device's changes into its record. */
/* This device's record up, the newest one: uploads run one at a time and each
 * takes the record as it is when its turn comes, so an older one never lands
 * after a newer one. */
std::mutex g_uploadLock;

void uploadMine()
{
    std::lock_guard<std::mutex> one(g_uploadLock);
    std::shared_ptr<Store> store;
    DeviceRecord mine;
    {
        std::lock_guard<std::mutex> hold(g_lock);
        if (!g_active) return;
        store = g_store;
        mine = g_mine;
    }
    // outside the lock: the network can take a while
    if (store) upload(store.get(), mine);
}

void pushNow()
{
    {
        std::lock_guard<std::mutex> hold(g_lock);
        g_pushQueued = false;
        if (!g_active) return;
        const LocalState local = readLocal();
        Outcome o = sync(g_mine, {}, local, g_base, wallMs(), false);
        g_base = o.base;
        if (!o.mineChanged) {
            keepState(g_base, g_mine);
            return;
        }
        g_mine = o.mine;
        keepState(g_base, g_mine);
    }
    uploadMine();
}

}   // namespace

/* ---- saves set aside (step 4): <save>.before-sync-<date>, the last five ---- */

/* the set-aside saves, newest first */
std::vector<std::string> savesAside()
{
    std::vector<std::string> out;
    for (const std::string &n : listDir(g_root)) {
        const size_t at = n.find(".before-sync-");
        if (at == std::string::npos || !isSaveName(n.substr(0, at))) continue;
        // only one worth using: a blank one loses to any progress the moment it is back
        std::string data;
        if (readFile(g_root + "/" + n, &data) && saveWhole(data) && saveHasProgress(data)) out.push_back(n);
    }
    std::sort(out.begin(), out.end(), [](const std::string &a, const std::string &b) {
        return a.substr(a.find(".before-sync-")) > b.substr(b.find(".before-sync-"));
    });
    return out;
}

/* launch, before the sync: the one the player chose (.gevr-sync/use-save.txt) back
 * in place, written now, so it is the newest everywhere; the save it replaces is
 * set aside in turn */
void useSaveAside()
{
    std::string chosen;
    if (!readFile(statePath("use-save.txt"), &chosen)) return;
    removeFile(statePath("use-save.txt"));
    chosen = trimmed(chosen);
    const size_t at = chosen.find(".before-sync-");
    if (at == std::string::npos || chosen.find('/') != std::string::npos || chosen.find('\\') != std::string::npos) return;
    const std::string save = chosen.substr(0, at);
    std::string data, current;
    if (!isSaveName(save) || !readFile(g_root + "/" + chosen, &data) || !saveWhole(data)) return;
    if (readFile(g_root + "/" + save, &current) && current != data) backupSave(save, current);
    if (writeFileAtomic(g_root + "/" + save, data)) {
        removeFile(g_root + "/" + chosen);
        notifyWritten(g_root + "/" + save);
        g_message = "Sync: the save you chose is in use, and goes to your other devices.";
        syncLog("%s back in place as %s", chosen.c_str(), save.c_str());
    }
}

#ifdef GEVR_SYNC_FILES
/* ---- ROMs and packs: a pass after launch, every 15 minutes, and on a kick ---- */

bool packsOn()
{
    std::string v;
    readFile(statePath("packs.txt"), &v);
    return trimmed(v) == "1";
}

/* A level is loaded (the game is past its title stage, where the menus are):
 * the files worker starts nothing new. Called off the game's thread; reading
 * g_StageNum there is a plain int read. A test sets it with GEVR_SYNC_TEST_BUSY. */
#ifndef GEVR_SYNC_TEST
extern "C" int g_StageNum;   // src/boss.c
bool gameBusy() { return g_StageNum != 90; }   // LEVELID_TITLE
#else
bool gameBusy()
{
    const char *b = getenv("GEVR_SYNC_TEST_BUSY");
    return b && b[0] == '1';
}
#endif

/* one pass, g_lock held on entry and on return (unlocked while it works) */
FilesResult filesRunOnce(std::unique_lock<std::mutex> &lock)
{
    g_filesKick = false;
    const std::string root = g_root, romDir = g_gameDir, device = g_mine.id;
    lock.unlock();
    std::unique_ptr<Store> store = linkedStore();   // its own: the push may use g_store meanwhile
    FilesResult res;
    if (store) {
        FilesPassArgs a;
        a.root = root;
        a.romDir = romDir;
        a.deviceId = device;
        a.packs = packsOn();
        a.now = wallMs();
        a.progress = [](const std::string &line, float done) {
            std::lock_guard<std::mutex> hold(g_lock);
            g_filesBusy = true;
            g_filesLine = line;
            g_filesDone = done;
        };
        a.stop = [] {
            std::lock_guard<std::mutex> hold(g_lock);
            return g_filesStop;
        };
        a.freeBytes = [](const std::string &path) { return pcnet::freeSpace(path); };
        a.hold = gameBusy;
        res = filesPass(store.get(), a);
    }
    lock.lock();
    g_filesBusy = false;
    g_filesDone = -1;
    g_filesLine.clear();
    for (const std::string &p : res.arrived) syncLog("%s came from another device", p.c_str());
    for (const std::string &p : res.deferred) syncLog("%s came from another device: in place next launch", p.c_str());
    if (!res.arrived.empty()) {
        g_message = res.arrived.size() == 1 ? "Sync: " + res.arrived[0] + " came from your other devices."
                                            : "Sync: " + std::to_string(res.arrived.size()) + " files came from your other devices.";
        g_romArrived = true;
    } else if (!res.deferred.empty()) {
        g_message = "Sync: an updated " + res.deferred[0] + " came from another device. It is used from the next start.";
    }
    if (res.failed) {
        syncLog("files: %d failed (%s)", res.failed, res.error.c_str());
        g_status = "Sync: " + std::to_string(res.failed) + (res.failed == 1 ? " file" : " files") +
                   " didn't move: " + res.error;
    } else if (res.moved) {
        syncLog("files: %d moved", res.moved);
    }
    return res;
}

void filesWorker()
{
    std::unique_lock<std::mutex> lock(g_lock);
    // a moment for the launch: the game is loading
    g_filesWake.wait_for(lock, std::chrono::seconds(4), [] { return g_filesStop; });
    while (!g_filesStop && g_active) {
        // never during a level: a pack's GBs compete with the game for the CPU and
        // the disk; looked at again every few seconds until the menus
        if (gameBusy()) {
            g_filesWake.wait_for(lock, std::chrono::seconds(5), [] { return g_filesStop; });
            continue;
        }
        const FilesResult r = filesRunOnce(lock);
        if (r.held) continue;   // stopped for a level: the rest once it is over
        g_filesWake.wait_for(lock, std::chrono::minutes(15), [] { return g_filesStop || g_filesKick; });
    }
    g_filesRunning = false;
}
#endif

/* the game's entry points, around the state above */

void launchAt(const std::string &root, const std::string &kind, const std::string &gameDir)
{
    std::lock_guard<std::mutex> hold(g_lock);
    if (g_launched) return;
    g_launched = true;
    g_root = root;
    while (g_root.size() > 1 && (g_root.back() == '/' || g_root.back() == '\\')) g_root.pop_back();
    g_gameDir = gameDir.empty() ? g_root : gameDir;
    while (g_gameDir.size() > 1 && (g_gameDir.back() == '/' || g_gameDir.back() == '\\')) g_gameDir.pop_back();
    g_kind = kind;
    makeDir(g_root + "/.gevr-sync");
    useSaveAside();         // a save set aside that the player chose to use again
#ifdef GEVR_SYNC_FILES
    {
        // downloads that waited for this launch (the game had the file open)
        std::vector<std::string> installed;
        if (filesInstallReady(g_root, g_gameDir, &installed)) {
            for (const std::string &p : installed) syncLog("%s put in place (it came while the game ran)", p.c_str());
            g_message = installed.size() == 1 ? "Sync: " + installed[0] + " came from your other devices."
                                              : "Sync: " + std::to_string(installed.size()) + " files came from your other devices.";
            g_romArrived = true;
        }
    }
#endif
    g_store = linkedStore();
    if (!g_store) return;   // nothing linked: off
    std::string text;
    if (readFile(statePath("state.txt"), &text)) baselineParse(text, &g_base);
    if (readFile(statePath("mine.txt"), &text)) recordParse(text, &g_mine);
    const std::string id = deviceId();
    if (g_mine.id != id) {
        g_mine = DeviceRecord();
        g_mine.id = id;
    }
    // before the sync: a kind's keys go into the record as "key@kind"
    g_mine.kind = kind;
    {
        // the player's name for this device ("Den"), else its kind's
        std::string name;
        readFile(statePath("name.txt"), &name);
        name = trimmed(name);
        g_mine.name = name.empty() ? kindLabel(kind) : name;
    }
#ifdef GEVR_APP_VERSION
    g_mine.version = GEVR_APP_VERSION;
#endif
    syncLog("launch: %s, device %s (%s)", g_store->label().c_str(), id.c_str(), kind.c_str());
    launchSync();
#ifdef GEVR_SYNC_FILES
    if (g_active && !g_filesRunning && !getenv("GEVR_SYNC_NO_FILES_WORKER")) {   // a test runs the passes itself
        g_filesRunning = true;
        g_filesStop = false;
        std::thread(filesWorker).detach();
    }
#endif
}

void fileSaved(const std::string &path)
{
    const size_t slash = path.find_last_of("/\\");
    const std::string name = slash == std::string::npos ? path : path.substr(slash + 1);
    bool interesting = isSaveName(name);
    for (int f = 0; f < FILE_COUNT; f++) interesting = interesting || name == kFileNames[f];
    if (!interesting) return;
    std::lock_guard<std::mutex> hold(g_lock);
    if (!g_active || g_pushQueued) return;
    g_pushQueued = true;
    // a short wait gathers the several files one change writes
    std::thread([] {
        std::this_thread::sleep_for(std::chrono::milliseconds(1500));
        pushNow();
    }).detach();
}

/* a test's push without the wait */
void pushForTest() { pushNow(); }

/* ---- the save check at START: another device's newer save, before the game reads its own ---- */

static long long saveCheckAgeMs()
{
    const char *a = getenv("GEVR_SYNC_SAVECHECK_AGE_MS");   // a test's "too old"
    return a && a[0] ? atoll(a) : 120000;
}

static bool cloudWatches()
{
#if defined(GEVR_IOS) && !defined(GEVR_SYNC_TEST)
    return gevrCloudAvailable() != 0;
#else
    return false;
#endif
}

/* the Drive (or folder) part, on its own thread; the answer only if still wanted */
static void saveCheckThread(unsigned attempt, DeviceRecord mine, Baseline base)
{
    std::unique_ptr<Store> store = linkedStore();
    std::vector<DeviceRecord> others;
    std::string why;
    bool ok = store != nullptr;
    if (ok) {
        store->setQuick(true);
        ok = readRecords(store.get(), mine.id, &others, nullptr, &why);
    }
    std::vector<SaveWrite> writes;
    if (ok) {
        std::lock_guard<std::mutex> hold(g_lock);
        // what the launch sync would put in place now: the saves part of it
        const Outcome o = sync(mine, others, readLocal(), base, wallMs(), true);
        writes = o.saves;
    }
    std::lock_guard<std::mutex> hold(g_lock);
    if (attempt != g_checkAttempt) return;   // Play now, or a newer try
    if (!ok) {
        syncLog("save check: could not reach the store (%s)", why.c_str());
        g_checkState = GEVR_SAVECHECK_FAILED;
        return;
    }
    g_saveCheckedMs = steadyMs();
    if (!writes.empty()) {
        g_checkWrites = writes;
        g_checkFrom = writes[0].fromDevice;
        g_checkWhen = writes[0].t;
        g_checkCloud = false;
        g_checkState = GEVR_SAVECHECK_NEWER;
        syncLog("save check: %s from %s is newer", writes[0].name.c_str(), g_checkFrom.c_str());
        return;
    }
#if defined(GEVR_IOS) && !defined(GEVR_SYNC_TEST)
    char name[96];
    long long when = 0;
    if (gevrCloudSaveNewer(name, sizeof(name), &when)) {
        g_checkFrom = "your other Apple device";
        g_checkWhen = when * 1000;
        g_checkCloud = true;
        g_checkState = GEVR_SAVECHECK_NEWER;
        return;
    }
#endif
    g_checkState = GEVR_SAVECHECK_NONE;
}

#ifdef GEVR_SYNC_FILES
/* a test's files pass, now: "moved failed arrived deferred error" */
std::string filesPassForTest()
{
    std::unique_lock<std::mutex> lock(g_lock);
    if (!g_active) return "inactive";
    const FilesResult r = filesRunOnce(lock);
    return std::to_string(r.moved) + " " + std::to_string(r.failed) + " " + std::to_string(r.arrived.size()) + " " +
           std::to_string(r.deferred.size()) + " " + (r.held ? "held " : "") + r.error;
}
#endif

#if !defined(GEVR_SYNC_TEST) || defined(GEVR_SYNC_DRIVE)
/* ---- Google Drive's link, for the launcher ---- */

static std::string stateDir() { return g_root + "/.gevr-sync"; }

void driveLink()
{
    std::lock_guard<std::mutex> hold(g_lock);
    if (g_root.empty() || !driveAvailable()) return;
    driveSignInStart(stateDir());
}

void driveCancel() { driveSignInCancel(); }

void driveUnlinkNow()
{
    std::string dir;
    {
        std::lock_guard<std::mutex> hold(g_lock);
        if (g_root.empty()) return;
        dir = stateDir();
        // linked again later (maybe to another account): that is a join, the others' values first
        g_base.joined = false;
        writeFileAtomic(statePath("state.txt"), baselineText(g_base));
        if (g_active && g_store && g_store->label().compare(0, 12, "Google Drive") == 0) {
            g_active = false;
            g_store.reset();
            g_status.clear();
            g_filesStop = true;   // the files worker ends after its current step
            g_filesWake.notify_all();
        }
    }
    driveSignInCancel();
    driveUnlink(dir);   // outside the lock: tells Google too
    syncLog("Google Drive unlinked");
}

int driveInfo(GevrSyncDriveInfo *out)
{
    memset(out, 0, sizeof(*out));
    std::string dir;
    bool active;
    {
        std::lock_guard<std::mutex> hold(g_lock);
        if (g_root.empty() || !driveAvailable()) return out->state = GEVR_SYNC_DRIVE_NONE;
        dir = stateDir();
        active = g_active && g_store && g_store->label().compare(0, 12, "Google Drive") == 0;
        snprintf(out->status, sizeof(out->status), "%s", active ? g_status.c_str() : "");
    }
    out->syncing = active ? 1 : 0;
    const DriveSignIn s = driveSignInState();
    if (s.state == DriveSignIn::STARTING || s.state == DriveSignIn::WAITING) {
        snprintf(out->code, sizeof(out->code), "%s", s.code.c_str());
        snprintf(out->url, sizeof(out->url), "%s", s.url.c_str());
        return out->state = s.state == DriveSignIn::STARTING ? GEVR_SYNC_DRIVE_STARTING : GEVR_SYNC_DRIVE_WAITING;
    }
    if (driveLinked(dir)) {
        snprintf(out->account, sizeof(out->account), "%s", driveEmail(dir).c_str());
        return out->state = GEVR_SYNC_DRIVE_LINKED;
    }
    if (s.state == DriveSignIn::FAILED) {
        snprintf(out->note, sizeof(out->note), "%s", s.message.c_str());
        return out->state = GEVR_SYNC_DRIVE_FAILED;
    }
    const std::string out_ = driveSignedOut(dir);
    if (!out_.empty()) {
        snprintf(out->note, sizeof(out->note), "%s", out_.c_str());
        return out->state = GEVR_SYNC_DRIVE_FAILED;
    }
    return out->state = GEVR_SYNC_DRIVE_UNLINKED;
}
#endif

}   // namespace gsync

#ifndef GEVR_SYNC_TEST
static std::string absoluteOf(const char *path)
{
    if (path && fsPathIsAbsolute(path)) return path;
    char cwd[1024];
#ifdef _WIN32
    std::string r = _getcwd(cwd, sizeof(cwd)) ? cwd : ".";
#else
    std::string r = getcwd(cwd, sizeof(cwd)) ? cwd : ".";
#endif
    if (path && path[0] && strcmp(path, ".") != 0) r += std::string("/") + path;
    return r;
}

extern "C" void gevrSyncLaunch(void)
{
    const char *kind = getenv("GEVR_SYNC_KIND");   // a test's device kind
    // the save folder ($S) and the game's ($B: the working folder, where
    // goldeneye-vr.ini and the launcher's ROMs are); the Quest's are two
    const std::string save = absoluteOf(fsFullPath("$S"));   // one at a time: fsFullPath has one buffer
    const std::string game = absoluteOf(fsFullPath("$B"));
    gsync::launchAt(save, kind && kind[0] ? kind : gsync::buildKind(), game);
}

extern "C" void gevrSyncFileSaved(const char *path)
{
    if (path) gsync::fileSaved(path);
}
#endif

#if !defined(GEVR_SYNC_TEST) || defined(GEVR_SYNC_DRIVE)
extern "C" int gevrSyncDrive(GevrSyncDriveInfo *out) { return gsync::driveInfo(out); }
extern "C" void gevrSyncDriveLink(void) { gsync::driveLink(); }
extern "C" void gevrSyncDriveCancel(void) { gsync::driveCancel(); }
extern "C" void gevrSyncDriveUnlink(void) { gsync::driveUnlinkNow(); }
#endif

extern "C" void gevrSyncStatus(char *buf, int size)
{
    if (!buf || size <= 0) return;
    std::lock_guard<std::mutex> hold(gsync::g_lock);
    snprintf(buf, (size_t)size, "%s", gsync::g_status.c_str());
}

extern "C" int gevrSyncTakeMessage(char *buf, int size)
{
    std::lock_guard<std::mutex> hold(gsync::g_lock);
    if (gsync::g_message.empty()) return 0;
    if (buf && size > 0) snprintf(buf, (size_t)size, "%s", gsync::g_message.c_str());
    gsync::g_message.clear();
    return 1;
}

extern "C" int gevrSyncActive(void)
{
    std::lock_guard<std::mutex> hold(gsync::g_lock);
    return gsync::g_active ? 1 : 0;
}

extern "C" int gevrSyncSaveCheckNeeded(void)
{
    std::lock_guard<std::mutex> hold(gsync::g_lock);
    if (gsync::g_store) {
        // the launch couldn't read the others' saves, or that was a while ago
        if (!gsync::g_saveCheckedMs || gsync::steadyMs() - gsync::g_saveCheckedMs > gsync::saveCheckAgeMs()) return 1;
    }
    return gsync::cloudWatches() ? 1 : 0;   // iCloud's look is local and instant: every START
}

extern "C" void gevrSyncSaveCheckBegin(void)
{
    std::lock_guard<std::mutex> hold(gsync::g_lock);
    const unsigned attempt = ++gsync::g_checkAttempt;
    gsync::g_checkState = GEVR_SAVECHECK_CHECKING;
    gsync::g_checkStarted = gsync::steadyMs();
    gsync::g_checkWrites.clear();
    gsync::g_checkCloud = false;
    if (gsync::g_store) {
        std::thread(gsync::saveCheckThread, attempt, gsync::g_mine, gsync::g_base).detach();
        return;
    }
#if defined(GEVR_IOS) && !defined(GEVR_SYNC_TEST)
    char name[96];
    long long when = 0;
    if (gevrCloudSaveNewer(name, sizeof(name), &when)) {
        gsync::g_checkFrom = "your other Apple device";
        gsync::g_checkWhen = when * 1000;
        gsync::g_checkCloud = true;
        gsync::g_checkState = GEVR_SAVECHECK_NEWER;
        return;
    }
#endif
    gsync::g_checkState = GEVR_SAVECHECK_NONE;
}

extern "C" int gevrSyncSaveCheckPoll(GevrSyncSaveCheck *out)
{
    std::lock_guard<std::mutex> hold(gsync::g_lock);
    // no answer in time (a slow network): as good as none
    if (gsync::g_checkState == GEVR_SAVECHECK_CHECKING && gsync::steadyMs() - gsync::g_checkStarted > 8000) {
        gsync::g_checkAttempt++;
        gsync::g_checkState = GEVR_SAVECHECK_FAILED;
    }
    if (out) {
        memset(out, 0, sizeof(*out));
        out->state = gsync::g_checkState;
        snprintf(out->from, sizeof(out->from), "%s", gsync::g_checkFrom.c_str());
        if (gsync::g_checkWhen > 0) {
            const time_t t = (time_t)(gsync::g_checkWhen / 1000);
            struct tm tm;
#ifdef _WIN32
            localtime_s(&tm, &t);
#else
            localtime_r(&t, &tm);
#endif
            strftime(out->when, sizeof(out->when), "%Y-%m-%d %H:%M", &tm);
        }
    }
    return gsync::g_checkState;
}

extern "C" void gevrSyncSaveCheckUse(void)
{
    std::lock_guard<std::mutex> hold(gsync::g_lock);
    if (gsync::g_checkState != GEVR_SAVECHECK_NEWER) return;
    if (gsync::g_checkCloud) {
#if defined(GEVR_IOS) && !defined(GEVR_SYNC_TEST)
        gevrCloudSaveUseNewer();
#endif
    } else {
        // as the launch sync puts a winner in place: this device's set aside first
        const gsync::LocalState local = gsync::readLocal();
        for (const gsync::SaveWrite &w : gsync::g_checkWrites) {
            const std::string path = gsync::g_root + "/" + w.name;
            auto l = local.saves.find(w.name);
            if (l != local.saves.end()) gsync::backupSave(w.name, l->second);
            if (!gsync::writeFileAtomic(path, w.data)) continue;
            gsync::setFileTimeMs(path, w.t);
            gsync::notifyWritten(path);
            gsync::g_base.saveHash[w.name] = gsync::hashText(w.data);
            gsync::g_base.saveT[w.name] = w.t;
            gsync::syncLog("save check: %s from %s in place", w.name.c_str(), w.fromDevice.c_str());
        }
        gsync::writeFileAtomic(gsync::statePath("state.txt"), gsync::baselineText(gsync::g_base));
    }
    gsync::g_checkWrites.clear();
    gsync::g_checkState = GEVR_SAVECHECK_IDLE;
}

extern "C" void gevrSyncSaveCheckDone(void)
{
    std::lock_guard<std::mutex> hold(gsync::g_lock);
    gsync::g_checkAttempt++;   // a check still running is dropped
    gsync::g_checkWrites.clear();
    gsync::g_checkState = GEVR_SAVECHECK_IDLE;
}

extern "C" void gevrSyncKick(void)
{
    std::lock_guard<std::mutex> hold(gsync::g_lock);
    gsync::g_filesKick = true;
    gsync::g_filesWake.notify_all();
}

extern "C" int gevrSyncProgress(float *done, char *buf, int size)
{
    std::lock_guard<std::mutex> hold(gsync::g_lock);
    if (!gsync::g_filesBusy) return 0;
    if (done) *done = gsync::g_filesDone;
    if (buf && size > 0) snprintf(buf, (size_t)size, "%s", gsync::g_filesLine.c_str());
    return 1;
}

extern "C" int gevrSyncTakeRomArrived(void)
{
    std::lock_guard<std::mutex> hold(gsync::g_lock);
    const bool r = gsync::g_romArrived;
    gsync::g_romArrived = false;
    return r ? 1 : 0;
}

extern "C" int gevrSyncPacks(void)
{
#ifdef GEVR_SYNC_FILES
    std::lock_guard<std::mutex> hold(gsync::g_lock);
    return !gsync::g_root.empty() && gsync::packsOn() ? 1 : 0;
#else
    return 0;
#endif
}

extern "C" void gevrSyncSetPacks(int on)
{
    std::lock_guard<std::mutex> hold(gsync::g_lock);
    if (gsync::g_root.empty()) return;
    gsync::writeFileAtomic(gsync::statePath("packs.txt"), on ? "1\n" : "0\n");
    gsync::g_filesKick = true;   // a pass now, with or without them
    gsync::g_filesWake.notify_all();
}

extern "C" void gevrSyncDeviceName(char *buf, int size)
{
    if (!buf || size <= 0) return;
    std::lock_guard<std::mutex> hold(gsync::g_lock);
    std::string name;
    if (!gsync::g_root.empty()) gsync::readFile(gsync::statePath("name.txt"), &name);
    name = gsync::trimmed(name);
    if (name.empty()) name = gsync::kindLabel(gsync::g_kind.empty() ? gsync::buildKind() : gsync::g_kind);
    snprintf(buf, (size_t)size, "%s", name.c_str());
}

extern "C" void gevrSyncSetDeviceName(const char *name)
{
    std::string n = gsync::trimmed(name ? name : "");
    for (char &c : n) {
        if (c == '\t' || c == '\n' || c == '\r') c = ' ';
    }
    if (n.size() > 40) n.resize(40);
    std::lock_guard<std::mutex> hold(gsync::g_lock);
    if (gsync::g_root.empty()) return;
    gsync::writeFileAtomic(gsync::statePath("name.txt"), n + "\n");
    if (gsync::g_active) {
        // the record carries it: the other devices' notes name this one by it
        gsync::g_mine.name = n.empty() ? gsync::kindLabel(gsync::g_kind) : n;
        gsync::keepState(gsync::g_base, gsync::g_mine);
        std::thread(gsync::uploadMine).detach();
    }
}

extern "C" int gevrSyncSavesAside(char (*names)[96], int max)
{
    std::lock_guard<std::mutex> hold(gsync::g_lock);
    if (gsync::g_root.empty()) return 0;
    const std::vector<std::string> all = gsync::savesAside();
    int n = 0;
    for (const std::string &s : all) {
        if (n >= max) break;
        snprintf(names[n++], 96, "%s", s.c_str());
    }
    return n;
}

extern "C" void gevrSyncUseSaveAside(const char *name)
{
    std::lock_guard<std::mutex> hold(gsync::g_lock);
    if (gsync::g_root.empty() || !name) return;
    gsync::writeFileAtomic(gsync::statePath("use-save.txt"), std::string(name) + "\n");
}

extern "C" void gevrSyncOpenUrl(const char *url)
{
#ifndef GEVR_SYNC_TEST
    if (url && url[0] && SDL_OpenURL(url) != 0) gsync::syncLog("could not open %s: %s", url, SDL_GetError());
#else
    (void)url;
#endif
}

extern "C" int gevrSyncSaveChosen(char *buf, int size)
{
    std::lock_guard<std::mutex> hold(gsync::g_lock);
    if (gsync::g_root.empty()) return 0;
    std::string v;
    if (!gsync::readFile(gsync::statePath("use-save.txt"), &v)) return 0;
    if (buf && size > 0) snprintf(buf, (size_t)size, "%s", gsync::trimmed(v).c_str());
    return 1;
}
