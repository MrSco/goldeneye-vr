/*
 * iCloud sync for the Apple builds (port/include/gevr_icloud.h,
 * docs/icloud-sync.md): iPhone/iPad, Apple TV and Vision Pro share one bundle
 * id, so one key-value store and one CloudKit container reach all three.
 *
 * Settings and saves: NSUbiquitousKeyValueStore.
 *   "ini" / "ini@<family>"   goldeneye-vr.ini's keys (shared / this kind of device's)
 *   "cfg" / "cfg@<family>"   goldeneye.ini's "Section.key"s, the same split
 *   "save:<file>"            an eeprom*.bin: { d: bytes, t: seconds }
 * Each is merged three ways against the copy last synced (.gevr-icloud/):
 * what changed here since then goes up, what changed elsewhere comes down.
 * A device joining for the first time takes the cloud's values.
 *
 * Files: CloudKit's private database, zone "GevrFiles". An item (a ROM, or a
 * texture pack's folder) is one GevrItem record (its path, stamp, size, chunk
 * count, generation) and its bytes as GevrChunk records of 32 MB, each one
 * asset, in a small archive format of our own (one entry for a ROM, every
 * file for a pack). Chunks are written first and the item last, so a reader
 * never sees half an upload; a new upload is a new generation and the old
 * chunks go after. ROMs sync by default, packs only with "iCloud: big packs".
 * Nothing deletes: removing a file here leaves the cloud's copy.
 *
 * A download that would replace a file or folder that is already here waits
 * in .gevr-icloud/ready/ and is put in place at the next launch, before the
 * game reads anything.
 */
#import <Foundation/Foundation.h>
#import <CloudKit/CloudKit.h>
#import <UIKit/UIKit.h>
#include <CommonCrypto/CommonDigest.h>

#include <dirent.h>
#include <stdarg.h>
#include <strings.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

extern "C" {
#include <PR/ultratypes.h>
#include "fs.h"
#include "system.h"
}
#include "gevr_icloud.h"
#include "gevr_sync_keys.h"

#if defined(GEVR_VISIONOS)
static const char *const kFamily = "vision";
#elif defined(GEVR_TVOS)
static const char *const kFamily = "tv";
#else
static const char *const kFamily = "ios";
#endif

static const char *const kStateDir = ".gevr-icloud";
static const size_t kChunkBytes = 32u << 20;
static NSString *const kZoneName = @"GevrFiles";

namespace {

std::mutex g_lock;                    // the options, the status and the message
bool g_available = false;             // signed with the entitlements (Info.plist GEVRICloud)
bool g_launched = false;
bool g_workerStarted = false;
bool g_launchDone = false;           // gevrCloudLaunch finished
bool g_joined = false;                // this device takes part in the key-value sync (see gevrCloudLaunch)
bool g_sync = true, g_packs = false;
std::string g_root;                   // the save folder, absolute, no trailing slash
std::string g_status, g_message;
float g_progress = -1;                // a transfer's share done, 0..1; -1 none

std::mutex g_kvLock;                  // the key-value merges: settings saves come from several threads

std::mutex g_workLock;
std::condition_variable g_workWake;
bool g_workKick = false;

void cloudLog(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
void cloudLog(const char *fmt, ...)
{
    char line[512];
    va_list args;
    va_start(args, fmt);
    vsnprintf(line, sizeof(line), fmt, args);
    va_end(args);
    sysLogPrintf(LOG_NOTE, "icloud: %s", line);
}

void setStatus(const std::string &s)
{
    std::lock_guard<std::mutex> hold(g_lock);
    g_status = s;
    g_progress = -1;
}

/* an upload or download under way: the launcher's bar under START */
void setTransfer(const std::string &s, float done)
{
    std::lock_guard<std::mutex> hold(g_lock);
    g_status = s;
    g_progress = done < 0 ? 0 : done > 1 ? 1 : done;
}

void setMessage(const std::string &s)
{
    std::lock_guard<std::mutex> hold(g_lock);
    g_message = s;
}

/* ------------------------------------------------------------------ files */

std::string statePath(const std::string &name) { return g_root + "/" + kStateDir + "/" + name; }

bool readFile(const std::string &path, std::string *out)
{
    FILE *f = fopen(path.c_str(), "rb");
    if (!f) return false;
    out->clear();
    char buf[65536];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) out->append(buf, n);
    fclose(f);
    return true;
}

bool writeFileAtomic(const std::string &path, const std::string &data)
{
    const std::string tmp = path + ".icloud-tmp";
    FILE *f = fopen(tmp.c_str(), "wb");
    if (!f) return false;
    const bool ok = fwrite(data.data(), 1, data.size(), f) == data.size();
    if (fclose(f) != 0 || !ok || rename(tmp.c_str(), path.c_str()) != 0) {
        remove(tmp.c_str());
        return false;
    }
    return true;
}

void makeDirs(const std::string &path)
{
    for (size_t i = 1; i <= path.size(); i++) {
        if (i == path.size() || path[i] == '/') mkdir(path.substr(0, i).c_str(), 0755);
    }
}

void removeTree(const std::string &path)
{
    struct stat st;
    if (lstat(path.c_str(), &st) != 0) return;
    if (S_ISDIR(st.st_mode)) {
        if (DIR *d = opendir(path.c_str())) {
            while (struct dirent *e = readdir(d)) {
                if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
                removeTree(path + "/" + e->d_name);
            }
            closedir(d);
        }
        rmdir(path.c_str());
    } else {
        unlink(path.c_str());
    }
}

std::string hexOf(const unsigned char *p, size_t n)
{
    static const char *h = "0123456789abcdef";
    std::string s;
    for (size_t i = 0; i < n; i++) {
        s += h[p[i] >> 4];
        s += h[p[i] & 15];
    }
    return s;
}

std::string sha256Text(const std::string &data)
{
    unsigned char md[CC_SHA256_DIGEST_LENGTH];
    CC_SHA256(data.data(), (CC_LONG)data.size(), md);
    return hexOf(md, sizeof(md));
}

std::string sha256File(const std::string &path)
{
    FILE *f = fopen(path.c_str(), "rb");
    if (!f) return std::string();
    CC_SHA256_CTX c;
    CC_SHA256_Init(&c);
    std::vector<char> buf(1 << 20);
    size_t n;
    while ((n = fread(buf.data(), 1, buf.size(), f)) > 0) CC_SHA256_Update(&c, buf.data(), (CC_LONG)n);
    fclose(f);
    unsigned char md[CC_SHA256_DIGEST_LENGTH];
    CC_SHA256_Final(md, &c);
    return hexOf(md, sizeof(md));
}

/* -------------------------------------------------------- settings (KVS) */

struct IniFile {
    const char *name;      // in the save folder
    const char *kvBase;    // "ini" or "cfg"
    bool sections;         // goldeneye.ini: keys are "Section.key"
};
/* "2": the store's names since the join rule (gevrCloudLaunch). The first
 * device test (2026-11-22) had a new iPad push its settings over the iPhone's
 * under the old rule; the old names are left alone. */
const IniFile kIni = { "goldeneye-vr.ini", "ini2", false };
const IniFile kCfg = { "goldeneye.ini", "cfg2", true };
NSString *const kSavePrefix = @"save2:";
const char *const kJoinedMarker = "joined2";

/* Which keys travel: the table the cross-platform sync uses too
 * (port/include/gevr_sync_keys.h). A kind's keys go to this family's
 * dictionary; a device's own (the split-screen guest seats, which pad is which
 * player, the audio output, the iCloud rows) never leave the device, and old
 * builds' copies of them in the store are left alone. */
gevrsync::KeyScope keyScope(const IniFile &f, const std::string &k)
{
    return f.sections ? gevrsync::cfgKeyScope(k) : gevrsync::iniKeyScope(k);
}

bool isLocalKey(const IniFile &f, const std::string &k) { return keyScope(f, k) != gevrsync::KEY_SHARED; }

bool isDeviceKey(const IniFile &f, const std::string &k) { return keyScope(f, k) == gevrsync::KEY_DEVICE; }

std::string trimEol(const std::string &s)
{
    size_t e = s.size();
    while (e && (s[e - 1] == '\r' || s[e - 1] == '\n')) e--;
    return s.substr(0, e);
}

struct IniLine {
    std::string text;      // as read, without the line end
    std::string key;       // "" for headers, comments, blanks
    std::string section;   // goldeneye.ini's section it is in
};

std::vector<IniLine> iniLines(const IniFile &f, const std::string &text)
{
    std::vector<IniLine> out;
    std::string section;
    size_t pos = 0;
    while (pos < text.size()) {
        size_t nl = text.find('\n', pos);
        if (nl == std::string::npos) nl = text.size();
        IniLine line;
        line.text = trimEol(text.substr(pos, nl - pos));
        pos = nl + 1;
        const std::string &t = line.text;
        if (!t.empty() && t[0] == '[') {
            const size_t close = t.find(']');
            section = t.substr(1, close == std::string::npos ? std::string::npos : close - 1);
        } else if (!t.empty() && t[0] != ';' && t[0] != '#') {
            size_t eq = t.find('=');
            if (eq != std::string::npos && eq > 0) {
                std::string k = t.substr(0, eq);
                while (!k.empty() && (k.back() == ' ' || k.back() == '\t')) k.pop_back();
                line.key = f.sections ? section + "." + k : k;
            }
        }
        line.section = section;
        out.push_back(line);
    }
    return out;
}

std::string iniValue(const IniLine &l)
{
    const size_t eq = l.text.find('=');
    std::string v = l.text.substr(eq + 1);
    size_t s = 0;
    while (s < v.size() && (v[s] == ' ' || v[s] == '\t')) s++;
    return v.substr(s);
}

std::map<std::string, std::string> iniMap(const IniFile &f, const std::string &text)
{
    std::map<std::string, std::string> m;
    for (const IniLine &l : iniLines(f, text)) {
        if (!l.key.empty()) m[l.key] = iniValue(l);
    }
    return m;
}

/* The file's text with these keys set: lines changed in place, new keys
 * appended (to their section, for goldeneye.ini). */
std::string iniApply(const IniFile &f, const std::string &text, const std::map<std::string, std::string> &set)
{
    std::vector<IniLine> lines = iniLines(f, text);
    std::set<std::string> done;
    for (IniLine &l : lines) {
        if (l.key.empty()) continue;
        auto it = set.find(l.key);
        if (it == set.end()) continue;
        const std::string bare = f.sections ? l.key.substr(l.section.size() + 1) : l.key;
        l.text = bare + "=" + it->second;
        done.insert(l.key);
    }
    std::string out;
    for (const IniLine &l : lines) out += l.text + "\n";
    if (!f.sections) {
        if (lines.empty()) out += "[VR]\n";
        for (const auto &kv : set) {
            if (!done.count(kv.first)) out += kv.first + "=" + kv.second + "\n";
        }
        return out;
    }
    // goldeneye.ini: the missing keys under their own section header
    std::map<std::string, std::vector<std::pair<std::string, std::string>>> bySection;
    for (const auto &kv : set) {
        if (done.count(kv.first)) continue;
        const size_t dot = kv.first.find('.');
        if (dot == std::string::npos) continue;
        bySection[kv.first.substr(0, dot)].push_back({ kv.first.substr(dot + 1), kv.second });
    }
    for (const auto &sec : bySection) {
        out += "\n[" + sec.first + "]\n";
        for (const auto &kv : sec.second) out += kv.first + "=" + kv.second + "\n";
    }
    return out;
}

NSUbiquitousKeyValueStore *kvs() { return [NSUbiquitousKeyValueStore defaultStore]; }

NSString *ns(const std::string &s) { return [NSString stringWithUTF8String:s.c_str()] ?: @""; }

std::map<std::string, std::string> kvDict(NSString *key)
{
    std::map<std::string, std::string> m;
    NSDictionary *d = [kvs() dictionaryForKey:key];
    for (id k in d) {
        id v = d[k];
        if ([k isKindOfClass:[NSString class]] && [v isKindOfClass:[NSString class]]) {
            m[[(NSString *)k UTF8String]] = [(NSString *)v UTF8String];
        }
    }
    return m;
}

void kvSetDict(NSString *key, const std::map<std::string, std::string> &m)
{
    NSMutableDictionary *d = [NSMutableDictionary dictionaryWithCapacity:m.size()];
    for (const auto &kv : m) d[ns(kv.first)] = ns(kv.second);
    [kvs() setDictionary:d forKey:key];
}

NSString *sharedKey(const IniFile &f) { return [NSString stringWithUTF8String:f.kvBase]; }
NSString *familyKey(const IniFile &f) { return [NSString stringWithFormat:@"%s@%s", f.kvBase, kFamily]; }

/* Keys changed in `now` against `before` go up (shared or this family's). */
void iniPush(const IniFile &f, const std::map<std::string, std::string> &before,
             const std::map<std::string, std::string> &now)
{
    std::map<std::string, std::string> shared = kvDict(sharedKey(f)), mine = kvDict(familyKey(f));
    bool sharedChanged = false, mineChanged = false;
    for (const auto &kv : now) {
        auto b = before.find(kv.first);
        if (b != before.end() && b->second == kv.second) continue;
        if (isDeviceKey(f, kv.first)) continue;
        std::map<std::string, std::string> &dst = isLocalKey(f, kv.first) ? mine : shared;
        auto cur = dst.find(kv.first);
        if (cur != dst.end() && cur->second == kv.second) continue;
        dst[kv.first] = kv.second;
        (isLocalKey(f, kv.first) ? mineChanged : sharedChanged) = true;
    }
    if (sharedChanged) kvSetDict(sharedKey(f), shared);
    if (mineChanged) kvSetDict(familyKey(f), mine);
}

/* At launch. Joined: the three-way merge of the file, the copy last synced
 * and the cloud. Joining: the cloud's values win, and only what it lacks goes up. */
void iniLaunchMerge(const IniFile &f, bool joined)
{
    const std::string path = g_root + "/" + f.name;
    const std::string lastPath = statePath(std::string("last-") + f.name);
    std::string text, lastText;
    const bool haveLocal = readFile(path, &text);
    if (joined) readFile(lastPath, &lastText);
    const std::map<std::string, std::string> local = iniMap(f, text), last = iniMap(f, lastText);
    std::map<std::string, std::string> remote = kvDict(sharedKey(f));
    for (const auto &kv : kvDict(familyKey(f))) remote[kv.first] = kv.second;

    std::map<std::string, std::string> take;   // the cloud's values the file gets
    for (const auto &kv : remote) {
        if (isDeviceKey(f, kv.first)) continue;   // an old build's copy of a device's own
        auto l = local.find(kv.first);
        if (l != local.end() && l->second == kv.second) continue;
        if (joined && l != local.end()) {
            auto b = last.find(kv.first);
            const bool changedHere = b == last.end() || b->second != l->second;
            if (changedHere) continue;   // changed here since the last sync: it goes up below
        }
        take[kv.first] = kv.second;
    }
    std::string merged = text;
    if (!take.empty()) {
        merged = iniApply(f, text, take);
        if (!writeFileAtomic(path, merged)) {
            cloudLog("could not write %s: %s", f.name, strerror(errno));
            return;
        }
        cloudLog("%s: %zu value%s from iCloud", f.name, take.size(), take.size() == 1 ? "" : "s");
    }
    // joined: what changed here since the last sync; joining: what the cloud lacks
    iniPush(f, joined ? last : remote, iniMap(f, merged));
    if (haveLocal || !take.empty()) writeFileAtomic(lastPath, merged);
}

void iniSaved(const IniFile &f)
{
    const std::string path = g_root + "/" + f.name;
    const std::string lastPath = statePath(std::string("last-") + f.name);
    std::string text, lastText;
    if (!readFile(path, &text)) return;
    readFile(lastPath, &lastText);
    iniPush(f, iniMap(f, lastText), iniMap(f, text));
    writeFileAtomic(lastPath, text);
}

/* Saves: whole files, never merged (user, 2026-11-22: choose the right one,
 * don't combine them). The one the game wrote last wins: the file's own time,
 * kept with it in the store and put back on the file when it comes down. Two
 * guards: a save with no progress never replaces one with progress (a new
 * install's blank save), and a save that is replaced is kept beside it as
 * <name>.before-icloud-<date>, the last five. */
bool isSaveName(const char *n)
{
    const size_t l = strlen(n);
    return strncmp(n, "eeprom", 6) == 0 && l > 4 && strcmp(n + l - 4, ".bin") == 0;
}

long long fileTime(const std::string &path)
{
    struct stat st;
    return stat(path.c_str(), &st) == 0 ? (long long)st.st_mtime : 0;
}

void savePush(const std::string &name, const std::string &data, long long mtime)
{
    NSData *d = [NSData dataWithBytes:data.data() length:data.size()];
    [kvs() setDictionary:@{ @"d": d, @"t": @(mtime) } forKey:[kSavePrefix stringByAppendingString:ns(name)]];
}

/* Read only, to choose: GoldenEye's EEPROM holds a 32-byte header, then five
 * 0x60-byte save slots (src/game/file2.c fileValidateSaves). A slot in use
 * (flag 0x80 clear) with a finished level (its times), an unlocked cheat or 007
 * mode is progress. Another layout (bytes there in use) reads as progress: no guard. */
bool saveHasProgress(const std::string &data)
{
    if (data.size() < 32 + 5 * 0x60) return true;
    for (int slot = 0; slot < 5; slot++) {
        const unsigned char *s = (const unsigned char *)data.data() + 32 + slot * 0x60;
        if (s[8] & 0x80) continue;   // completion_bitflags: SAVEFLAG_DORESET, a free slot
        if (s[9] & 1) return true;   // flag_007
        for (int i = 14; i < 17; i++) {
            if (s[i]) return true;   // unlocked_cheats_1..3
        }
        for (int i = 18; i < 0x60; i++) {
            if (s[i]) return true;   // times
        }
    }
    return false;   // every slot free or empty: a blank save
}

void saveBackup(const std::string &name, const std::string &data)
{
    char stamp[32];
    const time_t now = time(nullptr);
    struct tm tm;
    localtime_r(&now, &tm);
    strftime(stamp, sizeof(stamp), "%Y%m%d-%H%M%S", &tm);
    const std::string prefix = name + ".before-icloud-";
    writeFileAtomic(g_root + "/" + prefix + stamp, data);
    std::vector<std::string> old;
    if (DIR *d = opendir(g_root.c_str())) {
        while (struct dirent *e = readdir(d)) {
            if (strncmp(e->d_name, prefix.c_str(), prefix.size()) == 0) old.push_back(e->d_name);
        }
        closedir(d);
    }
    std::sort(old.begin(), old.end());
    for (size_t i = 0; i + 5 < old.size(); i++) remove((g_root + "/" + old[i]).c_str());
}

void savesLaunchMerge()
{
    std::set<std::string> names;
    if (DIR *d = opendir(g_root.c_str())) {
        while (struct dirent *e = readdir(d)) {
            if (isSaveName(e->d_name)) names.insert(e->d_name);
        }
        closedir(d);
    }
    for (NSString *k in [kvs() dictionaryRepresentation]) {
        if ([k hasPrefix:kSavePrefix]) {
            const std::string n = [[k substringFromIndex:kSavePrefix.length] UTF8String];
            if (isSaveName(n.c_str()) && n.find('/') == std::string::npos) names.insert(n);
        }
    }
    for (const std::string &n : names) {
        const std::string path = g_root + "/" + n;
        std::string local;
        const bool haveLocal = readFile(path, &local);
        const long long lt = haveLocal ? fileTime(path) : 0;
        NSDictionary *r = [kvs() dictionaryForKey:[kSavePrefix stringByAppendingString:ns(n)]];
        NSData *rd = [r[@"d"] isKindOfClass:[NSData class]] ? r[@"d"] : nil;
        const long long rt = [r[@"t"] respondsToSelector:@selector(longLongValue)] ? [r[@"t"] longLongValue] : 0;
        const std::string remote = rd ? std::string((const char *)rd.bytes, rd.length) : std::string();
        if (haveLocal && rd && local == remote) continue;
        bool keepLocal = haveLocal && (!rd || lt >= rt);
        // the guard: progress is never replaced by a save without any
        if (haveLocal && rd) {
            const bool lp = saveHasProgress(local), rp = saveHasProgress(remote);
            if (lp != rp) keepLocal = lp;
        }
        if (keepLocal) {
            savePush(n, local, lt);
            cloudLog("%s to iCloud (written %lld, iCloud's %lld)", n.c_str(), lt, rt);
            continue;
        }
        if (haveLocal) saveBackup(n, local);
        if (writeFileAtomic(path, remote)) {
            struct timeval tv[2] = { { (time_t)rt, 0 }, { (time_t)rt, 0 } };
            utimes(path.c_str(), tv);
            cloudLog("%s from iCloud (written %lld, this device's %lld%s)", n.c_str(), rt, lt,
                     haveLocal ? ", kept as .before-icloud" : "");
        }
    }
}

/* the game just wrote it: the newest, unless it is a blank save and iCloud's has progress */
void saveSaved(const std::string &name)
{
    const std::string path = g_root + "/" + name;
    std::string data;
    if (!readFile(path, &data)) return;
    if (!saveHasProgress(data)) {
        NSDictionary *r = [kvs() dictionaryForKey:[kSavePrefix stringByAppendingString:ns(name)]];
        NSData *rd = [r[@"d"] isKindOfClass:[NSData class]] ? r[@"d"] : nil;
        if (rd && saveHasProgress(std::string((const char *)rd.bytes, rd.length))) return;
    }
    savePush(name, data, fileTime(path));
}

bool storeHasData()
{
    for (NSString *k in [kvs() dictionaryRepresentation]) {
        if ([k hasPrefix:@"ini2"] || [k hasPrefix:@"cfg2"] || [k hasPrefix:kSavePrefix]) return true;
    }
    return false;
}

/* ------------------------------------------------------------ CloudKit */

/* An item's local state: a ROM file, or a pack folder. */
struct LocalItem {
    std::string path;     // relative to the save folder
    bool dir = false;
    bool pack = false;    // the opt-in tier
    std::string stamp;    // changes when the content may have
    long long bytes = 0;
};

bool romName(const char *n)
{
    if (n[0] == '.' || strncmp(n, "picked", 6) == 0) return false;
    const size_t l = strlen(n);
    if (l < 5) return false;
    const char *e = n + l - 4;
    return !strcasecmp(e, ".z64") || !strcasecmp(e, ".v64") || !strcasecmp(e, ".n64");
}

void walkDir(const std::string &abs, const std::string &rel, std::vector<std::pair<std::string, long long>> *files,
             long long *maxMtime)
{
    DIR *d = opendir(abs.c_str());
    if (!d) return;
    std::vector<std::string> names;
    while (struct dirent *e = readdir(d)) {
        if (e->d_name[0] == '.') continue;
        names.push_back(e->d_name);
    }
    closedir(d);
    std::sort(names.begin(), names.end());
    for (const std::string &n : names) {
        const std::string a = abs + "/" + n, r = rel.empty() ? n : rel + "/" + n;
        struct stat st;
        if (lstat(a.c_str(), &st) != 0) continue;
        if (S_ISDIR(st.st_mode)) {
            walkDir(a, r, files, maxMtime);
        } else if (S_ISREG(st.st_mode)) {
            files->push_back({ r, (long long)st.st_size });
            if ((long long)st.st_mtime > *maxMtime) *maxMtime = st.st_mtime;
        }
    }
}

std::vector<LocalItem> localItems(bool packs)
{
    std::vector<LocalItem> out;
    for (const char *sub : { "", "data" }) {
        const std::string dir = *sub ? g_root + "/" + sub : g_root;
        DIR *d = opendir(dir.c_str());
        if (!d) continue;
        while (struct dirent *e = readdir(d)) {
            if (!romName(e->d_name)) continue;
            struct stat st;
            const std::string abs = dir + "/" + e->d_name;
            if (stat(abs.c_str(), &st) != 0 || !S_ISREG(st.st_mode) || st.st_size <= 0) continue;
            LocalItem it;
            it.path = *sub ? std::string(sub) + "/" + e->d_name : std::string(e->d_name);
            it.bytes = st.st_size;
            it.stamp = std::to_string((long long)st.st_size) + ":" + std::to_string((long long)st.st_mtime);
            out.push_back(it);
        }
        closedir(d);
    }
    if (!packs) return out;
    const std::string tp = g_root + "/texture-packs";
    if (DIR *d = opendir(tp.c_str())) {
        while (struct dirent *e = readdir(d)) {
            const std::string n = e->d_name;
            // our own leftovers (".old-icloud", ".icloud-tmp") and half-made downloads
            if (n[0] == '.' || n.find("icloud") != std::string::npos || n.find(".part") != std::string::npos) continue;
            struct stat st;
            if (stat((tp + "/" + n).c_str(), &st) != 0 || !S_ISDIR(st.st_mode)) continue;
            std::vector<std::pair<std::string, long long>> files;
            long long maxMtime = 0;
            walkDir(tp + "/" + n, "", &files, &maxMtime);
            if (files.empty()) continue;
            LocalItem it;
            it.path = "texture-packs/" + n;
            it.dir = true;
            it.pack = true;
            for (const auto &f : files) it.bytes += f.second;
            it.stamp = std::to_string(files.size()) + ":" + std::to_string(it.bytes) + ":" + std::to_string(maxMtime);
            out.push_back(it);
        }
        closedir(d);
    }
    return out;
}

std::string itemId(const std::string &path)
{
    return sha256Text(path).substr(0, 24);
}

std::string baseName(const std::string &p)
{
    const size_t s = p.rfind('/');
    return s == std::string::npos ? p : p.substr(s + 1);
}

/* A CloudKit call made to wait: the worker thread is ours. Retries what
 * CloudKit says to retry. */
template <typename Start>
NSError *ckWait(Start start)
{
    for (int attempt = 0;; attempt++) {
        __block NSError *err = nil;
        dispatch_semaphore_t done = dispatch_semaphore_create(0);
        start(^(NSError *e) {
            err = e;
            dispatch_semaphore_signal(done);
        });
        dispatch_semaphore_wait(done, DISPATCH_TIME_FOREVER);
        if (!err || attempt >= 3) return err;
        const NSInteger c = err.code;
        if (c != CKErrorRequestRateLimited && c != CKErrorZoneBusy && c != CKErrorServiceUnavailable
            && c != CKErrorNetworkFailure && c != CKErrorNetworkUnavailable) {
            return err;
        }
        NSNumber *after = err.userInfo[CKErrorRetryAfterKey];
        const double wait = after ? std::min(after.doubleValue, 60.0) : 3.0 * (attempt + 1);
        [NSThread sleepForTimeInterval:wait];
    }
}

void configure(CKOperation *op, bool cellular)
{
    op.qualityOfService = NSQualityOfServiceUtility;
    CKOperationConfiguration *c = [[CKOperationConfiguration alloc] init];
    c.allowsCellularAccess = cellular;
    c.timeoutIntervalForResource = 24 * 3600;
    c.qualityOfService = NSQualityOfServiceUtility;
    op.configuration = c;
}

struct Cloud {
    CKContainer *container = nil;
    CKDatabase *db = nil;
    CKRecordZoneID *zone = nil;
};

NSError *ckEnsureZone(const Cloud &c)
{
    return ckWait([&](void (^finish)(NSError *)) {
        CKRecordZone *z = [[CKRecordZone alloc] initWithZoneID:c.zone];
        CKModifyRecordZonesOperation *op = [[CKModifyRecordZonesOperation alloc] initWithRecordZonesToSave:@[ z ]
                                                                                    recordZoneIDsToDelete:nil];
        configure(op, true);
        op.modifyRecordZonesCompletionBlock = ^(NSArray *saved, NSArray *deleted, NSError *e) { finish(e); };
        [c.db addOperation:op];
    });
}

struct ChunkRef { std::string name; double age; };   // a GevrChunk's record name and seconds since it was made

NSError *ckFetchItems(const Cloud &c, std::map<std::string, CKRecord *> *items, std::vector<ChunkRef> *chunks)
{
    NSMutableArray<CKRecord *> *got = [NSMutableArray array];
    NSMutableArray<CKRecord *> *parts = [NSMutableArray array];
    NSError *err = ckWait([&](void (^finish)(NSError *)) {
        [got removeAllObjects];
        [parts removeAllObjects];
        CKFetchRecordZoneChangesConfiguration *cfg = [[CKFetchRecordZoneChangesConfiguration alloc] init];
        cfg.desiredKeys = @[ @"path", @"stamp", @"size", @"chunks", @"gen", @"pack", @"dir", @"hash" ];
        CKFetchRecordZoneChangesOperation *op =
            [[CKFetchRecordZoneChangesOperation alloc] initWithRecordZoneIDs:@[ c.zone ]
                                                 configurationsByRecordZoneID:@{ c.zone: cfg }];
        configure(op, true);
        op.fetchAllChanges = YES;
        op.recordWasChangedBlock = ^(CKRecordID *rid, CKRecord *rec, NSError *e) {
            if (rec && [rec.recordType isEqualToString:@"GevrItem"]) {
                @synchronized(got) { [got addObject:rec]; }
            } else if (rec && [rec.recordType isEqualToString:@"GevrChunk"]) {
                @synchronized(parts) { [parts addObject:rec]; }
            }
        };
        op.fetchRecordZoneChangesCompletionBlock = ^(NSError *e) { finish(e); };
        [c.db addOperation:op];
    });
    if (err) return err;
    for (CKRecord *r in got) {
        NSString *p = r[@"path"];
        if ([p isKindOfClass:[NSString class]]) (*items)[p.UTF8String] = r;
    }
    for (CKRecord *r in parts) {
        const double age = r.creationDate ? -[r.creationDate timeIntervalSinceNow] : 0;
        chunks->push_back({ r.recordID.recordName.UTF8String, age });
    }
    return nil;
}

/* one item record as it is now: nil when there is none */
NSError *ckFetchItem(const Cloud &c, const std::string &id, CKRecord **out)
{
    *out = nil;
    CKRecordID *rid = [[CKRecordID alloc] initWithRecordName:ns("i-" + id) zoneID:c.zone];
    NSMutableArray<CKRecord *> *box = [NSMutableArray array];
    NSError *err = ckWait([&](void (^finish)(NSError *)) {
        CKFetchRecordsOperation *op = [[CKFetchRecordsOperation alloc] initWithRecordIDs:@[ rid ]];
        configure(op, true);
        op.desiredKeys = @[ @"path", @"stamp", @"size", @"chunks", @"gen", @"pack", @"dir", @"hash" ];
        op.fetchRecordsCompletionBlock = ^(NSDictionary<CKRecordID *, CKRecord *> *recs, NSError *e) {
            if (recs[rid]) [box addObject:recs[rid]];
            NSError *one = e && e.code == CKErrorPartialFailure ? e.userInfo[CKPartialErrorsByItemIDKey][rid] : e;
            finish(one && one.code != CKErrorUnknownItem && one.code != CKErrorZoneNotFound ? one : nil);
        };
        [c.db addOperation:op];
    });
    if (!err && box.count) *out = box.firstObject;
    return err;
}

NSError *ckSave(const Cloud &c, NSArray<CKRecord *> *records, NSArray<CKRecordID *> *deletes, bool cellular)
{
    return ckWait([&](void (^finish)(NSError *)) {
        CKModifyRecordsOperation *op = [[CKModifyRecordsOperation alloc] initWithRecordsToSave:records
                                                                             recordIDsToDelete:deletes];
        configure(op, cellular);
        op.savePolicy = CKRecordSaveAllKeys;
        op.modifyRecordsCompletionBlock = ^(NSArray *saved, NSArray *deleted, NSError *e) { finish(e); };
        [c.db addOperation:op];
    });
}

NSError *ckFetchChunk(const Cloud &c, CKRecordID *rid, bool cellular, std::string *assetPath)
{
    NSMutableArray<NSString *> *box = [NSMutableArray array];
    NSError *err = ckWait([&](void (^finish)(NSError *)) {
        CKFetchRecordsOperation *op = [[CKFetchRecordsOperation alloc] initWithRecordIDs:@[ rid ]];
        configure(op, cellular);
        op.desiredKeys = @[ @"data" ];
        op.fetchRecordsCompletionBlock = ^(NSDictionary<CKRecordID *, CKRecord *> *recs, NSError *e) {
            CKRecord *r = recs[rid];
            CKAsset *a = r ? r[@"data"] : nil;
            // the asset's file lasts only as long as the operation's callback: move it out
            if ([a isKindOfClass:[CKAsset class]] && a.fileURL) {
                NSString *dst = [NSTemporaryDirectory() stringByAppendingPathComponent:
                                    [NSString stringWithFormat:@"gevr-chunk-%@", [NSUUID UUID].UUIDString]];
                if ([[NSFileManager defaultManager] moveItemAtPath:a.fileURL.path toPath:dst error:nil]
                    || [[NSFileManager defaultManager] copyItemAtPath:a.fileURL.path toPath:dst error:nil]) {
                    [box addObject:dst];
                }
            }
            finish(e ?: (box.count ? nil : [NSError errorWithDomain:CKErrorDomain code:CKErrorUnknownItem userInfo:nil]));
        };
        [c.db addOperation:op];
    });
    if (!err && box.count) *assetPath = box.firstObject.UTF8String;
    return err;
}

std::string errText(NSError *e)
{
    if (!e) return std::string();
    return std::string(e.localizedDescription.UTF8String ?: "error") + " (" + std::to_string((long)e.code) + ")";
}

/* The archive: "GEVRPK1\n", then per file: u32 name length, name, u64 size,
 * bytes; ends with a u32 0xffffffff. A ROM is one file named "". */
const char kMagic[8] = { 'G', 'E', 'V', 'R', 'P', 'K', '1', '\n' };

struct ChunkWriter {
    const Cloud *cloud;
    std::string id, gen;
    bool cellular = true;
    int chunks = 0;
    long long total = 0, done = 0;   // for the progress line
    std::string label;
    std::string buf;
    NSError *err = nil;

    bool flush()
    {
        if (buf.empty() || err) return !err;
        NSString *tmp = [NSTemporaryDirectory() stringByAppendingPathComponent:
                            [NSString stringWithFormat:@"gevr-up-%s-%d", id.c_str(), chunks]];
        if (!writeFileAtomic(tmp.UTF8String, buf)) {
            err = [NSError errorWithDomain:NSPOSIXErrorDomain code:errno userInfo:nil];
            return false;
        }
        CKRecordID *rid = [[CKRecordID alloc] initWithRecordName:ns("c-" + id + "-" + gen + "-" + std::to_string(chunks))
                                                          zoneID:cloud->zone];
        CKRecord *rec = [[CKRecord alloc] initWithRecordType:@"GevrChunk" recordID:rid];
        rec[@"data"] = [[CKAsset alloc] initWithFileURL:[NSURL fileURLWithPath:tmp]];
        err = ckSave(*cloud, @[ rec ], nil, cellular);
        [[NSFileManager defaultManager] removeItemAtPath:tmp error:nil];
        buf.clear();
        if (!err) chunks++;
        return !err;
    }
    bool put(const void *p, size_t n)
    {
        const char *c = (const char *)p;
        while (n && !err) {
            const size_t room = kChunkBytes - buf.size();
            const size_t take = std::min(room, n);
            buf.append(c, take);
            c += take;
            n -= take;
            if (buf.size() >= kChunkBytes && !flush()) return false;
        }
        return !err;
    }
};

bool archiveFile(ChunkWriter *w, const std::string &name, const std::string &abs)
{
    FILE *f = fopen(abs.c_str(), "rb");
    if (!f) return false;
    fseek(f, 0, SEEK_END);
    const unsigned long long size = (unsigned long long)ftell(f);
    fseek(f, 0, SEEK_SET);
    const uint32_t nl = (uint32_t)name.size();
    bool ok = w->put(&nl, 4) && w->put(name.data(), name.size()) && w->put(&size, 8);
    std::vector<char> buf(1 << 20);
    size_t n;
    while (ok && (n = fread(buf.data(), 1, buf.size(), f)) > 0) {
        ok = w->put(buf.data(), n);
        w->done += (long long)n;
        const int pct = w->total ? (int)(w->done * 100 / w->total) : 0;
        setTransfer("iCloud: uploading " + w->label + " " + std::to_string(pct) + "%", pct / 100.0f);
    }
    fclose(f);
    return ok;
}

/* the item record holds this content already: a ROM's hash, a pack's file count and size */
bool sameContent(CKRecord *rec, const LocalItem &it, const std::string &hash)
{
    if ([rec[@"size"] longLongValue] != it.bytes) return false;
    if (it.dir) {
        NSString *st = rec[@"stamp"];
        if (![st isKindOfClass:[NSString class]]) return false;
        const std::string R = st.UTF8String;
        return R.substr(0, R.find(':')) == it.stamp.substr(0, it.stamp.find(':'));
    }
    NSString *h = rec[@"hash"];
    return [h isKindOfClass:[NSString class]] && h.length && hash == h.UTF8String;
}

NSString *genOf(CKRecord *rec)
{
    NSString *g = rec ? rec[@"gen"] : nil;
    return [g isKindOfClass:[NSString class]] ? g : nil;
}

/* Up: chunks first, the item record last. Another device may upload the same
 * item at the same time (two devices with the same pack, 2026-11-22): the
 * record is looked at before and after; if it already holds this content,
 * ours is dropped and theirs adopted (*adopted). */
NSError *uploadItem(const Cloud &c, const LocalItem &it, CKRecord *old, CKRecord **adopted)
{
    *adopted = nil;
    const std::string id = itemId(it.path);
    const std::string abs = g_root + "/" + it.path;
    const std::string hash = it.dir ? std::string() : sha256File(abs);
    CKRecord *now = nil;
    NSError *err = ckFetchItem(c, id, &now);
    if (err) return err;
    if (now && ![genOf(now) isEqualToString:genOf(old)]) {
        if (sameContent(now, it, hash)) {
            *adopted = now;
            cloudLog("%s: already in iCloud from another device", it.path.c_str());
            return nil;
        }
        old = now;
    }

    ChunkWriter w;
    w.cloud = &c;
    w.id = id;
    char gen[16];
    snprintf(gen, sizeof(gen), "%08x", arc4random());
    w.gen = gen;
    w.cellular = !it.pack;
    w.total = it.bytes;
    w.label = baseName(it.path);
    cloudLog("uploading %s (%lld bytes)", it.path.c_str(), it.bytes);
    bool ok = w.put(kMagic, sizeof(kMagic));
    if (it.dir) {
        std::vector<std::pair<std::string, long long>> files;
        long long maxMtime = 0;
        walkDir(abs, "", &files, &maxMtime);
        for (size_t i = 0; ok && i < files.size(); i++) ok = archiveFile(&w, files[i].first, abs + "/" + files[i].first);
    } else {
        ok = ok && archiveFile(&w, "", abs);
    }
    const uint32_t end = 0xffffffffu;
    ok = ok && w.put(&end, 4) && w.flush();
    auto chunkIds = [&](const std::string &g, int n) {
        NSMutableArray<CKRecordID *> *ids = [NSMutableArray array];
        for (int i = 0; i < n; i++) {
            [ids addObject:[[CKRecordID alloc] initWithRecordName:ns("c-" + id + "-" + g + "-" + std::to_string(i))
                                                           zoneID:c.zone]];
        }
        return ids;
    };
    if (!ok) {
        if (w.chunks) ckSave(c, nil, chunkIds(w.gen, w.chunks), w.cellular);   // what did go up
        return w.err ?: [NSError errorWithDomain:NSPOSIXErrorDomain code:EIO userInfo:nil];
    }

    // another device may have finished the same item meanwhile
    err = ckFetchItem(c, id, &now);
    if (!err && now && ![genOf(now) isEqualToString:genOf(old)]) {
        if (sameContent(now, it, hash)) {
            ckSave(c, nil, chunkIds(w.gen, w.chunks), w.cellular);
            *adopted = now;
            cloudLog("%s: another device finished it first, its copy kept", it.path.c_str());
            return nil;
        }
        old = now;
    }

    CKRecordID *rid = [[CKRecordID alloc] initWithRecordName:ns("i-" + id) zoneID:c.zone];
    CKRecord *rec = [[CKRecord alloc] initWithRecordType:@"GevrItem" recordID:rid];
    rec[@"path"] = ns(it.path);
    rec[@"stamp"] = ns(it.stamp);
    rec[@"size"] = @(it.bytes);
    rec[@"chunks"] = @(w.chunks);
    rec[@"gen"] = ns(w.gen);
    rec[@"pack"] = @(it.pack ? 1 : 0);
    rec[@"dir"] = @(it.dir ? 1 : 0);
    rec[@"hash"] = ns(hash);
    err = ckSave(c, @[ rec ], nil, w.cellular);
    if (!err && genOf(old)) {
        ckSave(c, nil, chunkIds(genOf(old).UTF8String, [old[@"chunks"] intValue]), w.cellular);   // best effort
    }
    return err;
}

/* The archive's reader: fed the chunks in order, writes the files under dest. */
struct Unpacker {
    std::string dest;
    std::string err;
    int state = 0;            // 0 magic, 1 name length, 2 name, 3 size, 4 data, 5 done
    std::string hold;         // a field's bytes so far
    uint32_t nameLen = 0;
    std::string name;
    unsigned long long left = 0;
    FILE *out = nullptr;

    bool safeName(const std::string &n)
    {
        if (n.empty()) return true;
        if (n[0] == '/' || n.find('\\') != std::string::npos) return false;
        size_t pos = 0;
        while (pos <= n.size()) {
            size_t s = n.find('/', pos);
            if (s == std::string::npos) s = n.size();
            const std::string part = n.substr(pos, s - pos);
            if (part.empty() || part == "." || part == "..") return false;
            pos = s + 1;
        }
        return true;
    }
    bool need(const char *&p, size_t &n, size_t want)
    {
        const size_t take = std::min(want - hold.size(), n);
        hold.append(p, take);
        p += take;
        n -= take;
        return hold.size() == want;
    }
    void feed(const char *p, size_t n)
    {
        while (n && err.empty() && state != 5) {
            switch (state) {
            case 0:
                if (!need(p, n, 8)) return;
                if (memcmp(hold.data(), kMagic, 8) != 0) err = "not an archive";
                hold.clear();
                state = 1;
                break;
            case 1:
                if (!need(p, n, 4)) return;
                memcpy(&nameLen, hold.data(), 4);
                hold.clear();
                if (nameLen == 0xffffffffu) {
                    state = 5;
                } else if (nameLen > 4096) {
                    err = "bad name";
                } else {
                    state = nameLen ? 2 : 3;
                    name.clear();
                }
                break;
            case 2:
                if (!need(p, n, nameLen)) return;
                name = hold;
                hold.clear();
                state = 3;
                break;
            case 3: {
                if (!need(p, n, 8)) return;
                memcpy(&left, hold.data(), 8);
                hold.clear();
                if (!safeName(name)) {
                    err = "bad name " + name;
                    break;
                }
                const std::string path = name.empty() ? dest + "/__file" : dest + "/" + name;
                const size_t slash = path.rfind('/');
                makeDirs(path.substr(0, slash));
                out = fopen(path.c_str(), "wb");
                if (!out) err = std::string("could not write ") + path + ": " + strerror(errno);
                state = 4;
                if (!left) {
                    fclose(out);
                    out = nullptr;
                    state = 1;
                }
                break;
            }
            case 4: {
                const size_t take = (size_t)std::min<unsigned long long>(left, n);
                if (fwrite(p, 1, take, out) != take) err = std::string("write failed: ") + strerror(errno);
                p += take;
                n -= take;
                left -= take;
                if (!left) {
                    fclose(out);
                    out = nullptr;
                    state = 1;
                }
                break;
            }
            }
        }
    }
    ~Unpacker()
    {
        if (out) fclose(out);
    }
};

/* A downloaded item put in place: now, or at the next launch when something
 * is already there (the game may be reading it). */
bool installNow(const std::string &staged, const std::string &relPath, bool dir)
{
    const std::string target = g_root + "/" + relPath;
    makeDirs(target.substr(0, target.rfind('/')));
    if (!dir) return rename((staged + "/__file").c_str(), target.c_str()) == 0;
    struct stat st;
    const std::string old = target + ".old-icloud";
    if (stat(target.c_str(), &st) == 0) {
        removeTree(old);
        if (rename(target.c_str(), old.c_str()) != 0) return false;
    }
    if (rename(staged.c_str(), target.c_str()) != 0) {
        rename(old.c_str(), target.c_str());
        return false;
    }
    removeTree(old);
    return true;
}

/* .gevr-icloud/ready/<id>/: the item's files, and "item.txt": path, dir, stamp */
void installReady()
{
    const std::string ready = statePath("ready");
    DIR *d = opendir(ready.c_str());
    if (!d) return;
    std::vector<std::string> ids;
    while (struct dirent *e = readdir(d)) {
        const size_t l = strlen(e->d_name);
        if (e->d_name[0] == '.' || (l > 4 && !strcmp(e->d_name + l - 4, ".txt"))) continue;
        ids.push_back(e->d_name);
    }
    closedir(d);
    for (const std::string &id : ids) {
        const std::string dir = ready + "/" + id;
        std::string info;
        if (!readFile(ready + "/" + id + ".txt", &info)) {
            removeTree(dir);
            continue;
        }
        const size_t a = info.find('\n');
        const std::string rel = info.substr(0, a);
        const bool isDir = a != std::string::npos && info.compare(a + 1, 1, "1") == 0;
        if (installNow(isDir ? dir + "/files" : dir, rel, isDir)) {
            cloudLog("installed %s from iCloud", rel.c_str());
        } else {
            cloudLog("could not install %s: %s", rel.c_str(), strerror(errno));
        }
        removeTree(dir);
        remove((ready + "/" + id + ".txt").c_str());
    }
}

NSError *downloadItem(const Cloud &c, CKRecord *rec, const std::string &relPath, bool *deferred)
{
    *deferred = false;
    const std::string id = itemId(relPath);
    NSString *gen = rec[@"gen"];
    const int chunks = [rec[@"chunks"] intValue];
    const long long size = [rec[@"size"] longLongValue];
    const bool dir = [rec[@"dir"] intValue] != 0;
    const bool pack = [rec[@"pack"] intValue] != 0;
    if (![gen isKindOfClass:[NSString class]] || chunks <= 0) {
        return [NSError errorWithDomain:CKErrorDomain code:CKErrorUnknownItem userInfo:nil];
    }
    const std::string staging = statePath("dl-" + id);
    removeTree(staging);
    makeDirs(dir ? staging + "/files" : staging);
    Unpacker u;
    u.dest = dir ? staging + "/files" : staging;
    const std::string label = baseName(relPath);
    cloudLog("downloading %s (%d chunk%s)", relPath.c_str(), chunks, chunks == 1 ? "" : "s");
    long long got = 0;
    for (int i = 0; i < chunks; i++) {
        CKRecordID *rid = [[CKRecordID alloc] initWithRecordName:
                              [NSString stringWithFormat:@"c-%s-%@-%d", id.c_str(), gen, i] zoneID:c.zone];
        std::string asset;
        NSError *err = ckFetchChunk(c, rid, !pack, &asset);
        if (err) {
            removeTree(staging);
            return err;
        }
        FILE *f = fopen(asset.c_str(), "rb");
        std::vector<char> buf(1 << 20);
        size_t n;
        while (f && (n = fread(buf.data(), 1, buf.size(), f)) > 0 && u.err.empty()) {
            u.feed(buf.data(), n);
            got += (long long)n;
            const int pct = size ? (int)std::min<long long>(100, got * 100 / size) : 0;
            setTransfer("iCloud: downloading " + label + " " + std::to_string(pct) + "%", pct / 100.0f);
        }
        if (f) fclose(f);
        remove(asset.c_str());
        if (!u.err.empty()) break;
    }
    if (u.err.empty() && u.state != 5) u.err = "archive cut short";
    if (!u.err.empty()) {
        removeTree(staging);
        cloudLog("download of %s failed: %s", relPath.c_str(), u.err.c_str());
        return [NSError errorWithDomain:NSPOSIXErrorDomain code:EIO userInfo:@{ NSLocalizedDescriptionKey: ns(u.err) }];
    }
    struct stat st;
    if (stat((g_root + "/" + relPath).c_str(), &st) == 0) {
        // something is here, and the game may be using it: at the next launch
        const std::string ready = statePath("ready");
        makeDirs(ready);
        removeTree(ready + "/" + id);
        rename(staging.c_str(), (ready + "/" + id).c_str());
        writeFileAtomic(ready + "/" + id + ".txt", relPath + "\n" + (dir ? "1" : "0") + "\n");
        *deferred = true;
        return nil;
    }
    if (!installNow(dir ? staging + "/files" : staging, relPath, dir)) {
        const int e = errno;
        removeTree(staging);
        return [NSError errorWithDomain:NSPOSIXErrorDomain code:e userInfo:nil];
    }
    removeTree(staging);
    return nil;
}

/* files.txt: "#cloud <id>", then path \t local stamp \t cloud stamp, as last
 * synced with that database (cloudId). Another database's stamps, or a file
 * from before the line (whose database is unknown), count as never synced. */
struct Synced { std::string local, remote; };

std::map<std::string, Synced> readSynced(const std::string &cloud)
{
    std::map<std::string, Synced> m;
    std::string text;
    if (!readFile(statePath("files.txt"), &text)) return m;
    const std::string head = "#cloud " + cloud + "\n";
    if (cloud.empty() || text.compare(0, head.size(), head) != 0) return m;
    size_t pos = head.size();
    while (pos < text.size()) {
        size_t nl = text.find('\n', pos);
        if (nl == std::string::npos) nl = text.size();
        const std::string line = text.substr(pos, nl - pos);
        pos = nl + 1;
        const size_t a = line.find('\t'), b = a == std::string::npos ? a : line.find('\t', a + 1);
        if (b == std::string::npos) continue;
        m[line.substr(0, a)] = { line.substr(a + 1, b - a - 1), line.substr(b + 1) };
    }
    return m;
}

void writeSynced(const std::map<std::string, Synced> &m, const std::string &cloud)
{
    std::string text = "#cloud " + cloud + "\n";
    for (const auto &kv : m) text += kv.first + "\t" + kv.second.local + "\t" + kv.second.remote + "\n";
    writeFileAtomic(statePath("files.txt"), text);
}

std::string localStampOf(const std::string &rel, bool pack)
{
    for (const LocalItem &it : localItems(pack)) {
        if (it.path == rel) return it.stamp;
    }
    return std::string();
}

/*
 * This CloudKit database's own id: a GevrJoined record "files-db" (a record
 * type the schema has already, so Production needs no new deploy) holding a
 * random id, made by the first device to look. Development builds and
 * TestFlight's are two databases (docs/icloud-sync.md): a test build's
 * last-synced stamps made every TestFlight record look changed in the cloud,
 * and a pack that differed at all (Bean with or without its Community Edition
 * overlay) came down whole on a device that had it already (the owner,
 * 2026-11-23, iPhone and Apple TV). "" when it cannot be read or made.
 */
std::string cloudId(const Cloud &c)
{
    static std::string known;   // one database for the process's life
    if (!known.empty()) return known;
    CKRecordID *rid = [[CKRecordID alloc] initWithRecordName:@"files-db" zoneID:c.zone];
    for (int attempt = 0; attempt < 2; attempt++) {
        NSMutableArray<NSString *> *box = [NSMutableArray array];
        NSError *err = ckWait([&](void (^finish)(NSError *)) {
            CKFetchRecordsOperation *op = [[CKFetchRecordsOperation alloc] initWithRecordIDs:@[ rid ]];
            configure(op, true);
            op.desiredKeys = @[ @"by" ];
            op.fetchRecordsCompletionBlock = ^(NSDictionary<CKRecordID *, CKRecord *> *recs, NSError *e) {
                NSString *by = recs[rid] ? recs[rid][@"by"] : nil;
                if ([by isKindOfClass:[NSString class]] && by.length) [box addObject:by];
                NSError *one = e && e.code == CKErrorPartialFailure ? e.userInfo[CKPartialErrorsByItemIDKey][rid] : e;
                finish(one && one.code != CKErrorUnknownItem && one.code != CKErrorZoneNotFound ? one : nil);
            };
            [c.db addOperation:op];
        });
        if (err) return std::string();
        if (box.count) return known = box.firstObject.UTF8String;
        // none yet: ours, unless another device makes it first (then read theirs)
        CKRecord *rec = [[CKRecord alloc] initWithRecordType:@"GevrJoined" recordID:rid];
        rec[@"by"] = [NSUUID UUID].UUIDString;
        err = ckWait([&](void (^finish)(NSError *)) {
            CKModifyRecordsOperation *op = [[CKModifyRecordsOperation alloc] initWithRecordsToSave:@[ rec ]
                                                                                 recordIDsToDelete:nil];
            configure(op, true);
            op.savePolicy = CKRecordSaveIfServerRecordUnchanged;
            op.modifyRecordsCompletionBlock = ^(NSArray *saved, NSArray *deleted, NSError *e) { finish(e); };
            [c.db addOperation:op];
        });
        if (!err) return known = ((NSString *)rec[@"by"]).UTF8String;
        cloudLog("files-db: %s", errText(err).c_str());
    }
    return std::string();
}

/* What one item does in a pass. L, R: its stamp here and in iCloud ("" for
 * none); s: as last synced with this database, or null; same(): whether the
 * two copies hold the same content, asked only when it decides. */
enum class Move { None, Adopt, Up, Down };

template <typename Same>
Move decideMove(const std::string &L, const std::string &R, const Synced *s, Same same)
{
    if (L.empty() && R.empty()) return Move::None;
    if (R.empty()) return Move::Up;
    if (L.empty()) return Move::Down;
    if (s && s->local == L && s->remote == R) return Move::None;
    if (s && s->local == L) {
        // changed in the cloud only: unless it is the same content under
        // another device's stamp (two devices uploaded it at once)
        return same() ? Move::Adopt : Move::Down;
    }
    if (s && s->remote == R) return Move::Up;   // changed here only
    if (s) {
        // both changed since they were in step: the cloud's copy wins, it is
        // what the other devices have
        return same() ? Move::Adopt : Move::Down;
    }
    // never synced with this database (a new device, a build from another
    // CloudKit environment, files.txt from before its "#cloud" line): a ROM
    // or pack already here stays as it is, whatever iCloud's copy holds, and
    // from now on its changes move either way
    return Move::Adopt;
}

/* One pass: every item in one place and not the other moves; an item changed
 * on one side since the last sync moves that way. */
void syncFiles(const Cloud &c, bool packs)
{
    setStatus("iCloud: checking");
    NSError *err = ckEnsureZone(c);
    if (err) {
        setStatus("iCloud: " + errText(err));
        cloudLog("zone: %s", errText(err).c_str());
        return;
    }
    std::map<std::string, CKRecord *> remote;
    std::vector<ChunkRef> chunkRefs;
    err = ckFetchItems(c, &remote, &chunkRefs);
    if (err) {
        setStatus("iCloud: " + errText(err));
        cloudLog("fetch: %s", errText(err).c_str());
        return;
    }
    {
        std::set<std::string> named;
        for (const auto &kv : remote) {
            NSString *g = genOf(kv.second);
            const int n = [kv.second[@"chunks"] intValue];
            for (int i = 0; g && i < n; i++) named.insert("c-" + itemId(kv.first) + "-" + g.UTF8String + "-" + std::to_string(i));
        }
        NSMutableArray<CKRecordID *> *orphans = [NSMutableArray array];
        for (const ChunkRef &ch : chunkRefs) {
            if (!named.count(ch.name) && ch.age > 24 * 3600) {
                [orphans addObject:[[CKRecordID alloc] initWithRecordName:ns(ch.name) zoneID:c.zone]];
            }
        }
        for (NSUInteger i = 0; i < orphans.count; i += 200) {
            NSArray *part = [orphans subarrayWithRange:NSMakeRange(i, MIN((NSUInteger)200, orphans.count - i))];
            if (!ckSave(c, nil, part, false)) cloudLog("removed %lu unused chunk%s", (unsigned long)part.count, part.count == 1 ? "" : "s");
        }
    }
    const std::string cloud = cloudId(c);
    if (cloud.empty()) {
        setStatus("iCloud: unavailable");
        cloudLog("files: this database's id could not be read; nothing moves this pass");
        return;
    }
    std::map<std::string, Synced> synced = readSynced(cloud);
    std::map<std::string, LocalItem> local;
    for (const LocalItem &it : localItems(packs)) local[it.path] = it;

    std::set<std::string> paths;
    for (const auto &kv : local) paths.insert(kv.first);
    for (const auto &kv : remote) {
        const bool pack = [kv.second[@"pack"] intValue] != 0;
        if (!pack || packs) paths.insert(kv.first);
    }
    int moved = 0, failed = 0;
    std::string lastError;
    for (const std::string &p : paths) {
        {
            std::lock_guard<std::mutex> hold(g_lock);
            if (!g_sync) break;
        }
        struct stat pending;
        if (stat(statePath("ready/" + itemId(p) + ".txt").c_str(), &pending) == 0) continue;   // in place next launch
        auto l = local.find(p);
        auto r = remote.find(p);
        auto s = synced.find(p);
        const std::string L = l != local.end() ? l->second.stamp : std::string();
        NSString *rs = r != remote.end() ? r->second[@"stamp"] : nil;
        const std::string R = [rs isKindOfClass:[NSString class]] ? rs.UTF8String : std::string();
        const Move move = decideMove(L, R, s != synced.end() ? &s->second : nullptr, [&]() {
            return sameContent(r->second, l->second, l->second.dir ? std::string() : sha256File(g_root + "/" + p));
        });
        if (move == Move::None) continue;
        if (move == Move::Adopt) {
            if (s == synced.end()) cloudLog("%s: kept as it is here (first sync with this iCloud)", p.c_str());
            synced[p] = { L, R };
            writeSynced(synced, cloud);
            continue;
        }
        const bool up = move == Move::Up, down = move == Move::Down;
        if (up) {
            CKRecord *adopted = nil;
            err = uploadItem(c, l->second, r != remote.end() ? r->second : nil, &adopted);
            if (!err && adopted) {
                NSString *st = adopted[@"stamp"];
                synced[p] = { L, [st isKindOfClass:[NSString class]] ? st.UTF8String : std::string() };
            } else if (!err) {
                synced[p] = { L, l->second.stamp };
                moved++;
            }
        } else if (down) {
            bool deferred = false;
            err = downloadItem(c, r->second, p, &deferred);
            if (!err) {
                const bool pack = [r->second[@"pack"] intValue] != 0;
                // a deferred one is stamped at the next pass, once it is in place
                if (!deferred) synced[p] = { localStampOf(p, pack), R };
                moved++;
                const bool rom = !pack;
                if (!deferred) {
                    setMessage(rom ? "ROM " + baseName(p) + " downloaded from iCloud."
                                   : "Pack " + baseName(p) + " downloaded from iCloud.");
                } else {
                    setMessage("An updated " + baseName(p) + " came from another device: it is used from the next launch.");
                }
            }
        }
        if (err) {
            failed++;
            lastError = errText(err);
            cloudLog("%s %s: %s", up ? "upload" : "download", p.c_str(), lastError.c_str());
            if (err.code == CKErrorQuotaExceeded) break;
        }
        writeSynced(synced, cloud);
    }
    writeSynced(synced, cloud);
    if (moved || failed) cloudLog("files: %d moved, %d failed (%zu items)", moved, failed, paths.size());
    if (failed) {
        setStatus("iCloud: " + std::to_string(failed) + " failed - " + lastError);
        setMessage("iCloud could not sync " + std::to_string(failed) + (failed == 1 ? " file: " : " files: ") + lastError);
    } else {
        setStatus(moved ? "iCloud: synced" : "iCloud: up to date");
    }
}

bool makeCloud(Cloud *c)
{
    NSString *containerId = [[NSBundle mainBundle] objectForInfoDictionaryKey:@"GEVRICloudContainer"];
    if (![containerId isKindOfClass:[NSString class]] || !containerId.length) containerId = @"iCloud.com.mrsco.goldeneyevr";
    @try {
        c->container = [CKContainer containerWithIdentifier:containerId];
        c->db = c->container.privateCloudDatabase;
        c->zone = [[CKRecordZoneID alloc] initWithZoneName:kZoneName ownerName:CKCurrentUserDefaultName];
    } @catch (NSException *e) {
        cloudLog("CloudKit unavailable: %s", e.reason.UTF8String ?: "?");
        return false;
    }
    return true;
}

/* The record a device leaves when it first puts settings in the store: a new
 * device that finds the store empty but this record there knows the other
 * devices' settings are still on their way, and pushes nothing.
 * 1: there, 0: not there, -1: could not tell (no account, no network). */
int joinedMarker(bool create)
{
    Cloud c;
    if (!makeCloud(&c)) return -1;
    CKRecordID *rid = [[CKRecordID alloc] initWithRecordName:@"joined" zoneID:c.zone];
    NSMutableArray<NSNumber *> *box = [NSMutableArray array];
    dispatch_semaphore_t done = dispatch_semaphore_create(0);
    CKOperationConfiguration *cfg = [[CKOperationConfiguration alloc] init];
    cfg.timeoutIntervalForRequest = 8;
    cfg.timeoutIntervalForResource = 10;
    cfg.qualityOfService = NSQualityOfServiceUserInitiated;
    if (create) {
        if (ckEnsureZone(c)) return -1;
        CKRecord *rec = [[CKRecord alloc] initWithRecordType:@"GevrJoined" recordID:rid];
        rec[@"by"] = ns(kFamily);
        CKModifyRecordsOperation *op = [[CKModifyRecordsOperation alloc] initWithRecordsToSave:@[ rec ] recordIDsToDelete:nil];
        op.configuration = cfg;
        op.savePolicy = CKRecordSaveAllKeys;
        op.modifyRecordsCompletionBlock = ^(NSArray *saved, NSArray *deleted, NSError *e) {
            [box addObject:@(e ? -1 : 1)];
            dispatch_semaphore_signal(done);
        };
        [c.db addOperation:op];
    } else {
        CKFetchRecordsOperation *op = [[CKFetchRecordsOperation alloc] initWithRecordIDs:@[ rid ]];
        op.configuration = cfg;
        op.desiredKeys = @[];
        op.fetchRecordsCompletionBlock = ^(NSDictionary<CKRecordID *, CKRecord *> *recs, NSError *e) {
            int v = recs[rid] ? 1 : -1;
            if (!recs[rid] && e) {
                NSError *one = e.code == CKErrorPartialFailure ? e.userInfo[CKPartialErrorsByItemIDKey][rid] : e;
                if (one.code == CKErrorUnknownItem || one.code == CKErrorZoneNotFound) v = 0;
            }
            [box addObject:@(v)];
            dispatch_semaphore_signal(done);
        };
        [c.db addOperation:op];
    }
    if (dispatch_semaphore_wait(done, dispatch_time(DISPATCH_TIME_NOW, 12 * NSEC_PER_SEC)) != 0) return -1;
    return box.firstObject.intValue;
}

void fileWorker()
{
    Cloud c;
    if (!makeCloud(&c)) {
        setStatus("");
        return;
    }
    for (;;) {
        bool sync, packs;
        {
            std::lock_guard<std::mutex> hold(g_lock);
            sync = g_sync;
            packs = g_packs;
        }
        if (sync) {
            @autoreleasepool {
                CKAccountStatus status = CKAccountStatusCouldNotDetermine;
                CKAccountStatus *statusOut = &status;
                ckWait([&](void (^finish)(NSError *)) {
                    [c.container accountStatusWithCompletionHandler:^(CKAccountStatus s, NSError *e) {
                        *statusOut = s;
                        finish(nil);
                    }];
                });
                if (status == CKAccountStatusAvailable) {
                    syncFiles(c, packs);
                } else {
                    setStatus(status == CKAccountStatusNoAccount ? "iCloud: not signed in" : "iCloud: unavailable");
                }
            }
        } else {
            setStatus("");
        }
        std::unique_lock<std::mutex> hold(g_workLock);
        g_workWake.wait_for(hold, std::chrono::minutes(15), [] { return g_workKick; });
        g_workKick = false;
    }
}

void startWorker()
{
    if (g_workerStarted) return;
    g_workerStarted = true;
    std::thread(fileWorker).detach();
}

void kick()
{
    std::lock_guard<std::mutex> hold(g_workLock);
    g_workKick = true;
    g_workWake.notify_all();
}

/* the launcher's two rows, before goldeneye-vr.ini is read: from its text */
void readOptions()
{
    std::string text;
    if (!readFile(g_root + "/goldeneye-vr.ini", &text)) return;
    const std::map<std::string, std::string> m = iniMap(kIni, text);
    auto s = m.find("ICloudSync");
    auto p = m.find("ICloudPacks");
    if (s != m.end()) g_sync = atoi(s->second.c_str()) != 0;
    if (p != m.end()) g_packs = atoi(p->second.c_str()) != 0;
}

}   // namespace

extern "C" int gevrCloudAvailable(void)
{
    static int avail = -1;
    if (avail < 0) {
        id v = [[NSBundle mainBundle] objectForInfoDictionaryKey:@"GEVRICloud"];
        avail = [v respondsToSelector:@selector(boolValue)] && [v boolValue] ? 1 : 0;
    }
    return avail;
}

extern "C" void gevrCloudLaunch(void)
{
    if (g_launched || !gevrCloudAvailable()) return;
    g_launched = true;
    g_available = true;
    char cwd[1024];
    const char *root = fsFullPath("$S");
    if (root && root[0] == '/') {
        g_root = root;
    } else if (getcwd(cwd, sizeof(cwd))) {
        g_root = cwd;
        if (root && strcmp(root, ".") != 0 && root[0]) g_root += std::string("/") + root;
    } else {
        g_root = ".";
    }
    while (g_root.size() > 1 && g_root.back() == '/') g_root.pop_back();
    makeDirs(statePath(""));
    readOptions();
    installReady();   // downloads that waited for this launch
    for (NSNotificationName name in @[ UIApplicationWillEnterForegroundNotification, UISceneWillEnterForegroundNotification ]) {
        [[NSNotificationCenter defaultCenter] addObserverForName:name object:nil queue:nil
                                                      usingBlock:^(NSNotification *n) { kick(); }];
    }
    if (!g_sync) {
        cloudLog("off (ICloudSync=0)");
        return;
    }

    @autoreleasepool {
        // Joining: a device takes part in the settings sync once the store
        // has the other devices' settings (they win), or once CloudKit says no
        // device has joined yet (it is the first, and its settings go up).
        // Until then it pushes nothing: a new device whose store has not come
        // down yet would otherwise put its own settings over everyone's.
        dispatch_semaphore_t arrived = dispatch_semaphore_create(0);
        [[NSNotificationCenter defaultCenter]
            addObserverForName:NSUbiquitousKeyValueStoreDidChangeExternallyNotification
                        object:kvs()
                         queue:[[NSOperationQueue alloc] init]
                    usingBlock:^(NSNotification *n) {
                        NSNumber *reason = n.userInfo[NSUbiquitousKeyValueStoreChangeReasonKey];
                        if (reason.integerValue == NSUbiquitousKeyValueStoreQuotaViolationChange) {
                            cloudLog("key-value store over its quota");
                        }
                        if (g_launchDone && !g_joined && storeHasData()) {
                            cloudLog("the other devices' settings arrived: used from the next launch");
                            setMessage("Your settings from your other devices arrived from iCloud: they are used from the next launch.");
                        }
                        dispatch_semaphore_signal(arrived);
                    }];
        [kvs() synchronize];
        std::string marker;
        const bool joined = readFile(statePath(kJoinedMarker), &marker);
        bool join = joined;
        if (!join) {
            if (!storeHasData()) {
                cloudLog("first sync: waiting for the store");
                dispatch_semaphore_wait(arrived, dispatch_time(DISPATCH_TIME_NOW, 5 * NSEC_PER_SEC));
            }
            if (storeHasData()) {
                join = true;
                cloudLog("joining: the other devices' settings win");
            } else {
                const int there = joinedMarker(false);
                if (there == 0) {
                    join = true;
                    cloudLog("joining as the first device: this device's settings go up");
                } else {
                    cloudLog(there > 0 ? "the other devices' settings have not arrived yet: nothing goes up until they do"
                                       : "could not reach iCloud: settings stay on this device for now");
                }
            }
        }
        if (join) {
            {
                std::lock_guard<std::mutex> hold(g_kvLock);
                iniLaunchMerge(kIni, joined);
                iniLaunchMerge(kCfg, joined);
                savesLaunchMerge();
                g_joined = true;
            }
            if (!joined) {
                writeFileAtomic(statePath(kJoinedMarker), "1\n");
                std::thread([] { joinedMarker(true); }).detach();
            }
            [kvs() synchronize];
        }
    }

    g_launchDone = true;
    startWorker();
    cloudLog("on (%s, packs %s, root %s)", kFamily, g_packs ? "on" : "off", g_root.c_str());
}

extern "C" void gevrCloudFileSaved(const char *path)
{
    if (!g_available || !g_joined || !path) return;
    {
        std::lock_guard<std::mutex> hold(g_lock);
        if (!g_sync) return;
    }
    const char *slash = strrchr(path, '/');
    const char *name = slash ? slash + 1 : path;
    @autoreleasepool {
        std::lock_guard<std::mutex> hold(g_kvLock);
        if (!strcmp(name, kIni.name)) {
            iniSaved(kIni);
        } else if (!strcmp(name, kCfg.name)) {
            iniSaved(kCfg);
        } else if (isSaveName(name)) {
            saveSaved(name);
        }
    }
}

extern "C" void gevrCloudKick(void)
{
    if (g_available) kick();
}

extern "C" void gevrCloudSetOptions(int sync, int packs)
{
    if (!g_launched) return;   // a build without iCloud
    bool changed;
    {
        std::lock_guard<std::mutex> hold(g_lock);
        changed = g_sync != (sync != 0) || g_packs != (packs != 0);
        g_sync = sync != 0;
        g_packs = packs != 0;
    }
    if (!changed) return;
    cloudLog("sync %s, packs %s", sync ? "on" : "off", packs ? "on" : "off");
    // turned on in a launch that began with it off: the files start now, the
    // settings merge waits for the next launch (the game has read its files)
    if (sync) startWorker();
    kick();
}

extern "C" void gevrCloudStatus(char *buf, int size)
{
    if (!buf || size <= 0) return;
    std::lock_guard<std::mutex> hold(g_lock);
    snprintf(buf, (size_t)size, "%s", g_sync ? g_status.c_str() : "");
}

extern "C" int gevrCloudProgress(float *done, char *buf, int size)
{
    std::lock_guard<std::mutex> hold(g_lock);
    if (!g_sync || g_progress < 0) return 0;
    if (done) *done = g_progress;
    if (buf && size > 0) snprintf(buf, (size_t)size, "%s", g_status.c_str());
    return 1;
}

extern "C" int gevrCloudTakeMessage(char *buf, int size)
{
    std::lock_guard<std::mutex> hold(g_lock);
    if (g_message.empty()) return 0;
    if (buf && size > 0) snprintf(buf, (size_t)size, "%s", g_message.c_str());
    g_message.clear();
    return 1;
}
