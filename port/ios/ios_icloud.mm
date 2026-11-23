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
bool g_sync = true, g_packs = false;
std::string g_root;                   // the save folder, absolute, no trailing slash
std::string g_status, g_message;

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

/* goldeneye-vr.ini keys that belong to one kind of device: its display, its
 * frame rate, its touch screen, its room. Everything else is the player's. */
const std::set<std::string> &localIniKeys()
{
    static const std::set<std::string> keys = {
        "FrameRate", "DisplayHz", "SpaceWarp", "EyeMsaa", "CpuLevel", "ShowStats",
        "ScreenDistance", "ScreenFov", "ScreenCurved", "ScreenPassthrough", "ScreenShape", "ScreenHeight",
        "TouchControls", "TouchLayout", "TouchHaptics", "PlayMode", "SeatedMode", "PlayerHeight",
        "MatchCharacterHeight", "WorldScale", "HudDistance", "StereoCrosshair", "PadDefaults",
        "LookUprightFolders", "MicMuted", "LiveSplit", "LiveSplitHost", "ICloudSync", "ICloudPacks",
    };
    return keys;
}

/* goldeneye.ini: the window, the audio device and the input backends are the device's */
bool localCfgKey(const std::string &k)
{
    static const char *const prefixes[] = {
        "Video.", "Audio.", "Input.Use", "Input.FakeGamepads", "Input.FirstGamepadNum",
        "Input.Mouse", "Game.MemorySize", "Game.ExtraSleep",
    };
    for (const char *p : prefixes) {
        if (k.compare(0, strlen(p), p) == 0) return true;
    }
    return false;
}

struct IniFile {
    const char *name;      // in the save folder
    const char *kvBase;    // "ini" or "cfg"
    bool sections;         // goldeneye.ini: keys are "Section.key"
};
const IniFile kIni = { "goldeneye-vr.ini", "ini", false };
const IniFile kCfg = { "goldeneye.ini", "cfg", true };

bool isLocalKey(const IniFile &f, const std::string &k)
{
    return f.sections ? localCfgKey(k) : localIniKeys().count(k) != 0;
}

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
        std::map<std::string, std::string> &dst = isLocalKey(f, kv.first) ? mine : shared;
        auto cur = dst.find(kv.first);
        if (cur != dst.end() && cur->second == kv.second) continue;
        dst[kv.first] = kv.second;
        (isLocalKey(f, kv.first) ? mineChanged : sharedChanged) = true;
    }
    if (sharedChanged) kvSetDict(sharedKey(f), shared);
    if (mineChanged) kvSetDict(familyKey(f), mine);
}

/* At launch: the three-way merge of the file, the copy last synced and the cloud. */
void iniLaunchMerge(const IniFile &f)
{
    const std::string path = g_root + "/" + f.name;
    const std::string lastPath = statePath(std::string("last-") + f.name);
    std::string text, lastText;
    const bool haveLocal = readFile(path, &text);
    const bool joined = readFile(lastPath, &lastText);
    const std::map<std::string, std::string> local = iniMap(f, text), last = iniMap(f, lastText);
    std::map<std::string, std::string> remote = kvDict(sharedKey(f));
    for (const auto &kv : kvDict(familyKey(f))) remote[kv.first] = kv.second;

    std::map<std::string, std::string> take;   // the cloud's values the file gets
    for (const auto &kv : remote) {
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
    // up: what changed here (everything, on a first sync with an empty cloud)
    const std::map<std::string, std::string> now = iniMap(f, merged);
    // joined: what changed here since the last sync; joining: what the cloud
    // lacks; an empty cloud: everything
    iniPush(f, joined ? last : remote, now);
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

/* Saves: whole files, the newer one when both changed. */
std::map<std::string, std::string> readHashes()
{
    std::map<std::string, std::string> m;
    std::string text;
    if (!readFile(statePath("saves.txt"), &text)) return m;
    size_t pos = 0;
    while (pos < text.size()) {
        size_t nl = text.find('\n', pos);
        if (nl == std::string::npos) nl = text.size();
        const std::string line = text.substr(pos, nl - pos);
        pos = nl + 1;
        const size_t tab = line.find('\t');
        if (tab != std::string::npos) m[line.substr(0, tab)] = line.substr(tab + 1);
    }
    return m;
}

void writeHashes(const std::map<std::string, std::string> &m)
{
    std::string text;
    for (const auto &kv : m) text += kv.first + "\t" + kv.second + "\n";
    writeFileAtomic(statePath("saves.txt"), text);
}

bool isSaveName(const char *n)
{
    const size_t l = strlen(n);
    return strncmp(n, "eeprom", 6) == 0 && l > 4 && strcmp(n + l - 4, ".bin") == 0;
}

void savePush(const std::string &name, const std::string &data, std::map<std::string, std::string> *hashes)
{
    NSData *d = [NSData dataWithBytes:data.data() length:data.size()];
    [kvs() setDictionary:@{ @"d": d, @"t": @((long long)time(nullptr)) } forKey:ns("save:" + name)];
    (*hashes)[name] = sha256Text(data);
}

void savesLaunchMerge()
{
    std::map<std::string, std::string> hashes = readHashes();
    std::set<std::string> names;
    if (DIR *d = opendir(g_root.c_str())) {
        while (struct dirent *e = readdir(d)) {
            if (isSaveName(e->d_name)) names.insert(e->d_name);
        }
        closedir(d);
    }
    for (NSString *k in [kvs() dictionaryRepresentation]) {
        if ([k hasPrefix:@"save:"]) {
            const std::string n = [[k substringFromIndex:5] UTF8String];
            if (isSaveName(n.c_str()) && n.find('/') == std::string::npos) names.insert(n);
        }
    }
    for (const std::string &n : names) {
        const std::string path = g_root + "/" + n;
        std::string local;
        const bool haveLocal = readFile(path, &local);
        NSDictionary *r = [kvs() dictionaryForKey:ns("save:" + n)];
        NSData *rd = [r[@"d"] isKindOfClass:[NSData class]] ? r[@"d"] : nil;
        const long long rt = [r[@"t"] respondsToSelector:@selector(longLongValue)] ? [r[@"t"] longLongValue] : 0;
        const std::string remote = rd ? std::string((const char *)rd.bytes, rd.length) : std::string();
        auto h = hashes.find(n);
        const std::string lh = haveLocal ? sha256Text(local) : std::string();
        const std::string rh = rd ? sha256Text(remote) : std::string();
        if (haveLocal && rd && lh == rh) {
            hashes[n] = lh;
            continue;
        }
        const bool localChanged = haveLocal && (h == hashes.end() || h->second != lh);
        const bool remoteChanged = rd && (h == hashes.end() || h->second != rh);
        bool pull = false, push = false;
        if (localChanged && remoteChanged) {
            struct stat st;
            const long long lt = stat(path.c_str(), &st) == 0 ? (long long)st.st_mtime : 0;
            (rt > lt ? pull : push) = true;
        } else {
            pull = remoteChanged;
            push = localChanged;
        }
        if (pull) {
            if (writeFileAtomic(path, remote)) {
                hashes[n] = rh;
                cloudLog("%s from iCloud", n.c_str());
            }
        } else if (push) {
            savePush(n, local, &hashes);
            cloudLog("%s to iCloud", n.c_str());
        }
    }
    writeHashes(hashes);
}

void saveSaved(const std::string &name)
{
    std::string data;
    if (!readFile(g_root + "/" + name, &data)) return;
    std::map<std::string, std::string> hashes = readHashes();
    const std::string h = sha256Text(data);
    auto it = hashes.find(name);
    if (it != hashes.end() && it->second == h) return;
    savePush(name, data, &hashes);
    writeHashes(hashes);
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

NSError *ckFetchItems(const Cloud &c, std::map<std::string, CKRecord *> *items)
{
    NSMutableArray<CKRecord *> *got = [NSMutableArray array];
    NSError *err = ckWait([&](void (^finish)(NSError *)) {
        [got removeAllObjects];
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
    return nil;
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
        setStatus("iCloud: uploading " + w->label + " " + std::to_string(pct) + "%");
    }
    fclose(f);
    return ok;
}

NSError *uploadItem(const Cloud &c, const LocalItem &it, CKRecord *old)
{
    ChunkWriter w;
    w.cloud = &c;
    w.id = itemId(it.path);
    uint32_t r = arc4random();
    char gen[16];
    snprintf(gen, sizeof(gen), "%08x", r);
    w.gen = gen;
    w.cellular = !it.pack;
    w.total = it.bytes;
    w.label = baseName(it.path);
    cloudLog("uploading %s (%lld bytes)", it.path.c_str(), it.bytes);
    bool ok = w.put(kMagic, sizeof(kMagic));
    const std::string abs = g_root + "/" + it.path;
    std::string hash;
    if (it.dir) {
        std::vector<std::pair<std::string, long long>> files;
        long long maxMtime = 0;
        walkDir(abs, "", &files, &maxMtime);
        for (size_t i = 0; ok && i < files.size(); i++) ok = archiveFile(&w, files[i].first, abs + "/" + files[i].first);
    } else {
        hash = sha256File(abs);
        ok = ok && archiveFile(&w, "", abs);
    }
    const uint32_t end = 0xffffffffu;
    ok = ok && w.put(&end, 4) && w.flush();
    if (!ok) return w.err ?: [NSError errorWithDomain:NSPOSIXErrorDomain code:EIO userInfo:nil];

    CKRecordID *rid = [[CKRecordID alloc] initWithRecordName:ns("i-" + w.id) zoneID:c.zone];
    CKRecord *rec = [[CKRecord alloc] initWithRecordType:@"GevrItem" recordID:rid];
    rec[@"path"] = ns(it.path);
    rec[@"stamp"] = ns(it.stamp);
    rec[@"size"] = @(it.bytes);
    rec[@"chunks"] = @(w.chunks);
    rec[@"gen"] = ns(w.gen);
    rec[@"pack"] = @(it.pack ? 1 : 0);
    rec[@"dir"] = @(it.dir ? 1 : 0);
    rec[@"hash"] = ns(hash);
    NSMutableArray<CKRecordID *> *stale = [NSMutableArray array];
    if (old) {
        NSString *og = old[@"gen"];
        const int oc = [old[@"chunks"] intValue];
        for (int i = 0; [og isKindOfClass:[NSString class]] && i < oc; i++) {
            [stale addObject:[[CKRecordID alloc] initWithRecordName:
                                 [NSString stringWithFormat:@"c-%s-%@-%d", w.id.c_str(), og, i] zoneID:c.zone]];
        }
    }
    NSError *err = ckSave(c, @[ rec ], nil, w.cellular);
    if (!err && stale.count) ckSave(c, nil, stale, w.cellular);   // the old generation: best effort
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
            setStatus("iCloud: downloading " + label + " " + std::to_string(pct) + "%");
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

/* files.txt: path \t local stamp \t cloud stamp, as last synced */
struct Synced { std::string local, remote; };

std::map<std::string, Synced> readSynced()
{
    std::map<std::string, Synced> m;
    std::string text;
    if (!readFile(statePath("files.txt"), &text)) return m;
    size_t pos = 0;
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

void writeSynced(const std::map<std::string, Synced> &m)
{
    std::string text;
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
    err = ckFetchItems(c, &remote);
    if (err) {
        setStatus("iCloud: " + errText(err));
        cloudLog("fetch: %s", errText(err).c_str());
        return;
    }
    std::map<std::string, Synced> synced = readSynced();
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
        if (!L.empty() && !R.empty() && s != synced.end() && s->second.local == L && s->second.remote == R) continue;

        bool up = false, down = false;
        if (!L.empty() && R.empty()) {
            up = true;
        } else if (L.empty() && !R.empty()) {
            down = true;
        } else if (s != synced.end() && s->second.local == L) {
            down = true;    // changed in the cloud only
        } else if (s != synced.end() && s->second.remote == R) {
            up = true;      // changed here only
        } else {
            // never synced here, or both changed: the same content counts as synced
            const LocalItem &li = l->second;
            NSString *rh = r->second[@"hash"];
            const long long rsize = [r->second[@"size"] longLongValue];
            bool same;
            if (li.dir) {
                same = rsize == li.bytes && R.substr(0, R.find(':')) == li.stamp.substr(0, li.stamp.find(':'));
            } else {
                same = rsize == li.bytes && [rh isKindOfClass:[NSString class]] && rh.length
                    && sha256File(g_root + "/" + p) == rh.UTF8String;
            }
            if (same) {
                synced[p] = { L, R };
                continue;
            }
            down = true;    // the cloud's copy wins a conflict: it is what the other devices have
        }
        if (up) {
            err = uploadItem(c, l->second, r != remote.end() ? r->second : nil);
            if (!err) {
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
                    setMessage("A newer " + baseName(p) + " came from iCloud: it is used from the next launch.");
                }
            }
        }
        if (err) {
            failed++;
            lastError = errText(err);
            cloudLog("%s %s: %s", up ? "upload" : "download", p.c_str(), lastError.c_str());
            if (err.code == CKErrorQuotaExceeded) break;
        }
        writeSynced(synced);
    }
    writeSynced(synced);
    if (failed) {
        setStatus("iCloud: " + std::to_string(failed) + " failed - " + lastError);
    } else {
        setStatus(moved ? "iCloud: synced" : "iCloud: up to date");
    }
}

void fileWorker()
{
    NSString *containerId = [[NSBundle mainBundle] objectForInfoDictionaryKey:@"GEVRICloudContainer"];
    if (![containerId isKindOfClass:[NSString class]] || !containerId.length) containerId = @"iCloud.com.mrsco.goldeneyevr";
    Cloud c;
    @try {
        c.container = [CKContainer containerWithIdentifier:containerId];
        c.db = c.container.privateCloudDatabase;
        c.zone = [[CKRecordZoneID alloc] initWithZoneName:kZoneName ownerName:CKCurrentUserDefaultName];
    } @catch (NSException *e) {
        cloudLog("CloudKit unavailable: %s", e.reason.UTF8String ?: "?");
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
        // A device's first sync: give the store a few seconds to bring the
        // other devices' settings down before the game reads its own.
        __block dispatch_semaphore_t arrived = dispatch_semaphore_create(0);
        id observer = [[NSNotificationCenter defaultCenter]
            addObserverForName:NSUbiquitousKeyValueStoreDidChangeExternallyNotification
                        object:kvs()
                         queue:[[NSOperationQueue alloc] init]
                    usingBlock:^(NSNotification *n) {
                        NSNumber *reason = n.userInfo[NSUbiquitousKeyValueStoreChangeReasonKey];
                        if (reason.integerValue == NSUbiquitousKeyValueStoreQuotaViolationChange) {
                            cloudLog("key-value store over its quota");
                        }
                        dispatch_semaphore_signal(arrived);
                    }];
        [kvs() synchronize];
        std::string marker;
        const bool firstSync = !readFile(statePath("joined"), &marker);
        if (firstSync && [kvs() dictionaryRepresentation].count == 0) {
            cloudLog("first sync: waiting for the store");
            dispatch_semaphore_wait(arrived, dispatch_time(DISPATCH_TIME_NOW, 5 * NSEC_PER_SEC));
        }
        {
            std::lock_guard<std::mutex> hold(g_kvLock);
            iniLaunchMerge(kIni);
            iniLaunchMerge(kCfg);
            savesLaunchMerge();
        }
        // the options may have come from the cloud... except they are this device's own keys
        writeFileAtomic(statePath("joined"), "1\n");
        [kvs() synchronize];
        // later changes from other devices are read at the next launch; keep listening only for the log
        (void)observer;
    }

    startWorker();
    cloudLog("on (%s, packs %s, root %s)", kFamily, g_packs ? "on" : "off", g_root.c_str());
}

extern "C" void gevrCloudFileSaved(const char *path)
{
    if (!g_available || !path) return;
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

extern "C" int gevrCloudTakeMessage(char *buf, int size)
{
    std::lock_guard<std::mutex> hold(g_lock);
    if (g_message.empty()) return 0;
    if (buf && size > 0) snprintf(buf, (size_t)size, "%s", g_message.c_str());
    g_message.clear();
    return 1;
}
