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

#include <algorithm>
#include <chrono>
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

private:
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
std::string g_root;           // the save folder
std::string g_kind;
std::unique_ptr<Store> g_store;
DeviceRecord g_mine;
Baseline g_base;
std::string g_status, g_message;
bool g_pushQueued = false;

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
    return nullptr;
}

LocalState readLocal()
{
    LocalState l;
    for (int f = 0; f < FILE_COUNT; f++) l.have[f] = readFile(g_root + "/" + kFileNames[f], &l.text[f]);
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

/* The launch sync, g_lock held. */
void launchSync()
{
    std::string why;
    std::vector<std::string> names;
    if (!g_store->list(&names, &why)) {
        syncLog("could not reach the store (%s): nothing changed", why.c_str());
        g_status = "Sync: couldn't reach your other devices' settings";
        return;
    }
    std::vector<DeviceRecord> others;
    std::string storedMine;   // this device's record as the store has it
    for (const std::string &n : names) {
        std::string id;
        if (!recordNameId(n, &id)) continue;
        std::string text;
        if (!g_store->read(n, &text, &why)) {
            // a partial view could put this device's values over one it couldn't read
            syncLog("could not read %s (%s): nothing changed", n.c_str(), why.c_str());
            g_status = "Sync: couldn't reach your other devices' settings";
            return;
        }
        if (id == g_mine.id) {
            storedMine = text;
            continue;
        }
        DeviceRecord r;
        if (recordParse(text, &r) && r.id == id) others.push_back(r);
    }
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
        const std::string path = g_root + "/" + kFileNames[f];
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
void pushNow()
{
    std::unique_ptr<Store> store;
    DeviceRecord mine;
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
        mine = g_mine;
    }
    // outside the lock: the network can take a while; one push runs at a time (the worker)
    upload(g_store.get(), mine);
}

}   // namespace

/* the game's entry points, around the state above */

void launchAt(const std::string &root, const std::string &kind)
{
    std::lock_guard<std::mutex> hold(g_lock);
    if (g_launched) return;
    g_launched = true;
    g_root = root;
    while (g_root.size() > 1 && (g_root.back() == '/' || g_root.back() == '\\')) g_root.pop_back();
    g_kind = kind;
    makeDir(g_root + "/.gevr-sync");
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
    if (g_mine.name.empty()) g_mine.name = kindLabel(kind);
#ifdef GEVR_APP_VERSION
    g_mine.version = GEVR_APP_VERSION;
#endif
    syncLog("launch: %s, device %s (%s)", g_store->label().c_str(), id.c_str(), kind.c_str());
    launchSync();
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
        static std::mutex one;   // one push at a time
        std::lock_guard<std::mutex> hold(one);
        pushNow();
    }).detach();
}

/* a test's push without the wait */
void pushForTest() { pushNow(); }

}   // namespace gsync

#ifndef GEVR_SYNC_TEST
extern "C" void gevrSyncLaunch(void)
{
    const char *root = fsFullPath("$S");
    std::string r;
    if (root && fsPathIsAbsolute(root)) {
        r = root;
    } else {
        char cwd[1024];
#ifdef _WIN32
        r = _getcwd(cwd, sizeof(cwd)) ? cwd : ".";
#else
        r = getcwd(cwd, sizeof(cwd)) ? cwd : ".";
#endif
        if (root && root[0] && strcmp(root, ".") != 0) r += std::string("/") + root;
    }
    const char *kind = getenv("GEVR_SYNC_KIND");   // a test's device kind
    gsync::launchAt(r, kind && kind[0] ? kind : gsync::buildKind());
}

extern "C" void gevrSyncFileSaved(const char *path)
{
    if (path) gsync::fileSaved(path);
}
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
