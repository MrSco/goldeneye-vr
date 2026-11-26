/*
 * The cross-platform sync's ROMs and texture packs (docs/cloud-sync.md, step 3).
 * Every device lists what it has in its own index (files-<id>.txt, beside its
 * device record); the content itself is a blob in the store, named by what is
 * in it, so a ROM two devices have is uploaded once.
 *
 *  - ROMs (*.z64/v64/n64 in the save folder and data/) always; the texture
 *    packs (texture-packs/<pack>/: HD, AI, Bean, mods) only with "Big packs
 *    too" on, a pack as one zip.
 *  - Already here stays (iCloud's rule): an item a device has the first time it
 *    syncs keeps as it is. Only a missing item comes down, and after that first
 *    sync, an item changed on another device does (the newest change wins).
 *  - Nothing is deleted on a device or in the store when a file is removed.
 *    A blob no index names any more is removed by the device that replaced it.
 *  - A download whose target is already there (the game may be using it) waits
 *    in .gevr-sync/ready/ and is put in place at the next launch.
 *
 * In the background: a pass after launch, every 15 minutes, and when the
 * launcher picks a ROM or imports a pack (gevrSyncKick).
 */
#include "gevr_sync_core.h"
#include "gevr_sync_files.h"
#include "gevr_sync_store.h"
#include "pc_http.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "../vr/miniz/miniz.h"

using namespace pcnet;

namespace gsync {

/* ------------------------------------------------------------ the index */

std::string filesIndexName(const std::string &deviceId) { return "files-" + deviceId + ".txt"; }

bool filesIndexNameId(const std::string &name, std::string *id)
{
    if (name.size() <= 10 || name.compare(0, 6, "files-") != 0 || name.compare(name.size() - 4, 4, ".txt") != 0) return false;
    *id = name.substr(6, name.size() - 10);
    return true;
}

static std::vector<std::string> fields(const std::string &line)
{
    std::vector<std::string> f;
    size_t pos = 0;
    for (;;) {
        const size_t tab = line.find('\t', pos);
        f.push_back(line.substr(pos, tab == std::string::npos ? std::string::npos : tab - pos));
        if (tab == std::string::npos) break;
        pos = tab + 1;
    }
    return f;
}

static std::vector<std::string> lines(const std::string &text)
{
    std::vector<std::string> out;
    size_t pos = 0;
    while (pos < text.size()) {
        size_t nl = text.find('\n', pos);
        if (nl == std::string::npos) nl = text.size();
        std::string l = text.substr(pos, nl - pos);
        if (!l.empty() && l.back() == '\r') l.pop_back();
        out.push_back(l);
        pos = nl + 1;
    }
    return out;
}

/* a path is the save folder's: no "..", no leading slash, no backslash */
static bool safePath(const std::string &p)
{
    if (p.empty() || p[0] == '/' || p.find('\\') != std::string::npos || p.find(':') != std::string::npos) return false;
    size_t pos = 0;
    while (pos <= p.size()) {
        size_t s = p.find('/', pos);
        if (s == std::string::npos) s = p.size();
        const std::string part = p.substr(pos, s - pos);
        if (part.empty() || part == "." || part == "..") return false;
        pos = s + 1;
    }
    return true;
}

std::string filesIndexText(const FilesIndex &ix)
{
    std::string o = "gevr-sync-files\t1\n";
    for (const auto &kv : ix) {
        const FileEntry &e = kv.second;
        o += "file\t" + std::to_string(e.t) + "\t" + (e.pack ? "pack" : "rom") + "\t" + std::to_string(e.bytes) + "\t" +
             e.blob + "\t" + e.path + "\n";
    }
    return o;
}

bool filesIndexParse(const std::string &text, FilesIndex *out)
{
    out->clear();
    const std::vector<std::string> ls = lines(text);
    if (ls.empty() || ls[0].compare(0, 16, "gevr-sync-files\t") != 0) return false;
    for (size_t i = 1; i < ls.size(); i++) {
        const std::vector<std::string> f = fields(ls[i]);
        if (f.size() != 6 || f[0] != "file" || !safePath(f[5]) || f[4].empty()) continue;
        const bool pack = f[2] == "pack";
        const size_t slash = f[5].find('/');
        if (pack ? f[5].compare(0, 14, "texture-packs/") != 0 || f[5].find('/', 14) != std::string::npos
                 : slash != std::string::npos) {
            continue;   // a ROM is a name, a pack a folder of texture-packs: nothing else lands
        }
        FileEntry e;
        e.t = strtoll(f[1].c_str(), nullptr, 10);
        e.pack = f[2] == "pack";
        e.bytes = strtoll(f[3].c_str(), nullptr, 10);
        e.blob = f[4];
        e.path = f[5];
        (*out)[e.path] = e;
    }
    return true;
}

/* ------------------------------------------------------- this device's files */

static bool romName(const std::string &n)
{
    if (n.empty() || n[0] == '.' || n.compare(0, 6, "picked") == 0 || n.size() < 5) return false;
    std::string e = n.substr(n.size() - 4);
    for (char &c : e) c = (char)tolower((unsigned char)c);
    return e == ".z64" || e == ".v64" || e == ".n64";
}

/* our own leftovers and half-made ones */
static bool ourLeftover(const std::string &n)
{
    return n.empty() || n[0] == '.' || n.find("icloud") != std::string::npos || n.find(".part") != std::string::npos ||
           n.find(".sync-") != std::string::npos;
}

static void walk(const std::string &abs, const std::string &rel, std::vector<std::pair<std::string, long long>> *files,
                 long long *newest)
{
    std::vector<std::string> names = listDir(abs);
    std::sort(names.begin(), names.end());
    for (const std::string &n : names) {
        if (n.empty() || n[0] == '.') continue;
        const std::string a = abs + "/" + n, r = rel.empty() ? n : rel + "/" + n;
        if (isDir(a)) {
            walk(a, r, files, newest);
        } else if (isFile(a)) {
            files->push_back({ r, fileSize(a) });
            *newest = std::max(*newest, fileTimeMs(a));
        }
    }
}

std::string localPathOf(const std::string &root, const std::string &romDir, const std::string &path, bool pack)
{
    return pack ? root + "/" + path : romDir + "/" + path;
}

std::vector<LocalFile> localFiles(const std::string &root, const std::string &romDir, bool packs)
{
    std::vector<LocalFile> out;
    for (const std::string &n : listDir(romDir)) {
        const std::string abs = romDir + "/" + n;
        if (!romName(n) || !isFile(abs)) continue;
        const long long size = fileSize(abs);
        if (size <= 0) continue;
        LocalFile f;
        f.path = n;
        f.bytes = size;
        f.stamp = std::to_string(size) + ":" + std::to_string(fileTimeMs(abs) / 1000);
        out.push_back(f);
    }
    if (!packs) return out;
    const std::string tp = root + "/texture-packs";
    for (const std::string &n : listDir(tp)) {
        if (ourLeftover(n) || !isDir(tp + "/" + n)) continue;
        std::vector<std::pair<std::string, long long>> files;
        long long newest = 0;
        walk(tp + "/" + n, "", &files, &newest);
        if (files.empty()) continue;
        LocalFile f;
        f.path = "texture-packs/" + n;
        f.pack = true;
        for (const auto &x : files) f.bytes += x.second;
        f.stamp = std::to_string(files.size()) + ":" + std::to_string(f.bytes) + ":" + std::to_string(newest / 1000);
        out.push_back(f);
    }
    return out;
}

/* What is in it: a ROM's SHA-256; a pack's over each file's path, size and SHA-256 */
static std::string contentId(const std::string &root, const std::string &romDir, const LocalFile &f,
                             const std::string &scratch)
{
    if (!f.pack) {
        const std::string h = sha256File(romDir + "/" + f.path);
        return h.empty() ? h : "r" + h;
    }
    std::vector<std::pair<std::string, long long>> files;
    long long newest = 0;
    walk(root + "/" + f.path, "", &files, &newest);
    std::string manifest;
    for (const auto &x : files) {
        const std::string h = sha256File(root + "/" + f.path + "/" + x.first);
        if (h.empty()) return std::string();
        manifest += x.first + "\t" + std::to_string(x.second) + "\t" + h + "\n";
    }
    const std::string tmp = scratch + "/manifest.sync-tmp";
    if (!writeFileAtomic(tmp, manifest)) return std::string();
    const std::string h = sha256File(tmp);
    removeFile(tmp);
    return h.empty() ? h : "p" + h;
}

/* a pack as one zip, stored (textures are compressed already), zip64 for the big ones */
static bool zipPack(const std::string &dir, const std::string &zipPath, std::string *why)
{
    std::vector<std::pair<std::string, long long>> files;
    long long newest = 0;
    walk(dir, "", &files, &newest);
    FILE *out = openFile(zipPath, "wb");
    if (!out) {
        *why = "could not write " + zipPath;
        return false;
    }
    mz_zip_archive zip;
    memset(&zip, 0, sizeof(zip));
    bool ok = mz_zip_writer_init_cfile(&zip, out, MZ_ZIP_FLAG_WRITE_ZIP64) != 0;
    for (size_t i = 0; ok && i < files.size(); i++) {
        FILE *in = openFile(dir + "/" + files[i].first, "rb");
        if (!in) {
            ok = false;
            break;
        }
        const MZ_TIME_T when = 0;
        ok = mz_zip_writer_add_cfile(&zip, files[i].first.c_str(), in, (mz_uint64)files[i].second, &when, nullptr, 0,
                                     MZ_NO_COMPRESSION, nullptr, 0, nullptr, 0) != 0;
        fclose(in);
    }
    ok = ok && mz_zip_writer_finalize_archive(&zip);
    mz_zip_writer_end(&zip);
    ok = fclose(out) == 0 && ok;
    if (!ok) {
        removeFile(zipPath);
        *why = "could not pack " + dir;
    }
    return ok;
}

/* ---------------------------------------------------- what this device synced */

/* .gevr-sync/files-state.txt: path, its stamp here and the entry it is in step with */
static std::map<std::string, FileSynced> readSynced(const std::string &path)
{
    std::map<std::string, FileSynced> m;
    std::string text;
    if (!readFile(path, &text, 16u << 20)) return m;
    for (const std::string &l : lines(text)) {
        const std::vector<std::string> f = fields(l);
        if (f.size() != 4 || !safePath(f[3])) continue;
        m[f[3]] = FileSynced{ f[0], strtoll(f[1].c_str(), nullptr, 10), f[2] };
    }
    return m;
}

static void writeSynced(const std::string &path, const std::map<std::string, FileSynced> &m)
{
    std::string o;
    for (const auto &kv : m) o += kv.second.local + "\t" + std::to_string(kv.second.t) + "\t" + kv.second.blob + "\t" + kv.first + "\n";
    writeFileAtomic(path, o);
}

static std::string stampOf(const std::string &root, const std::string &romDir, const std::string &path, bool pack)
{
    for (const LocalFile &f : localFiles(root, romDir, pack)) {
        if (f.path == path) return f.stamp;
    }
    return std::string();
}

/* ------------------------------------------------------------- the decision */

FileMove decideFileMove(const LocalFile *local, const FileEntry *winner, const FileSynced *synced)
{
    if (!local && !winner) return FileMove::None;
    if (!winner) return FileMove::Up;                 // only here: it goes up
    if (!local) return FileMove::Down;                // only there: it comes down
    if (!synced) return FileMove::Adopt;              // already here stays, whatever the others have
    const bool here = local->stamp != synced->local, there = winner->t != synced->t;
    if (!here && !there) return FileMove::None;
    if (here && !there) return FileMove::Up;          // changed here only
    return FileMove::Down;                            // changed elsewhere (both: the others' copy wins)
}

/* ------------------------------------------------------------------ installing */

static bool installFile(const std::string &staged, const std::string &target)
{
    const size_t slash = target.rfind('/');
    if (slash != std::string::npos) makeDirs(target.substr(0, slash));
    if (isFile(target)) {
        const std::string old = target + ".sync-old";
        removeFile(old);
        if (!movePath(target, old, true)) return false;
        if (!movePath(staged, target, false)) {
            movePath(old, target, true);
            return false;
        }
        removeFile(old);
        return true;
    }
    return movePath(staged, target, false);
}

static bool installDir(const std::string &staged, const std::string &target)
{
    const size_t slash = target.rfind('/');
    if (slash != std::string::npos) makeDirs(target.substr(0, slash));
    const std::string old = target + ".sync-old";
    if (isDir(target)) {
        deleteTree(old);
        if (!movePath(target, old, false)) return false;
    }
    if (!movePath(staged, target, false)) {
        movePath(old, target, false);
        return false;
    }
    deleteTree(old);
    return true;
}

void filesRecover(const std::string &root, const std::string &romDir)
{
    const std::string state = root + "/.gevr-sync";
    // a pass's scratch: partial downloads, a pack being zipped (never anything finished)
    deleteTree(state + "/work");
    makeDirs(state + "/work");
    // ready/: an item is complete once its descriptor is written (after it)
    const std::string ready = state + "/ready";
    for (const std::string &n : listDir(ready)) {
        const bool txt = n.size() > 4 && n.compare(n.size() - 4, 4, ".txt") == 0;
        if (!txt && !isFile(ready + "/" + n + ".txt")) deleteTree(ready + "/" + n);
        if (txt && !isDir(ready + "/" + n.substr(0, n.size() - 4))) removeFile(ready + "/" + n);
    }
    // an install cut between its two renames: the old copy back, or gone
    auto settle = [](const std::string &dir, bool folders) {
        for (const std::string &n : listDir(dir)) {
            if (n.size() <= 9 || n.compare(n.size() - 9, 9, ".sync-old") != 0) continue;
            const std::string old = dir + "/" + n, target = dir + "/" + n.substr(0, n.size() - 9);
            const bool there = folders ? isDir(target) : isFile(target);
            if (!there) movePath(old, target, false);
            else if (folders) deleteTree(old);
            else removeFile(old);
        }
    };
    settle(romDir, false);
    settle(root + "/texture-packs", true);
}

/* .gevr-sync/ready/<n>/: a download that waited ("item" = the file or the pack's
 * folder) and <n>.txt: its path, pack or not, its entry's t and blob */
int filesInstallReady(const std::string &root, const std::string &romDir, std::vector<std::string> *installed)
{
    const std::string state = root + "/.gevr-sync", ready = state + "/ready";
    filesRecover(root, romDir);   // before the game reads its ROMs and packs
    std::map<std::string, FileSynced> synced = readSynced(state + "/files-state.txt");
    int n = 0;
    for (const std::string &name : listDir(ready)) {
        if (name.size() < 5 || name.compare(name.size() - 4, 4, ".txt") != 0) continue;
        const std::string key = name.substr(0, name.size() - 4);
        std::string info;
        readFile(ready + "/" + name, &info, 65536);
        const std::vector<std::string> l = lines(info);
        const std::string item = ready + "/" + key + "/item";
        if (l.size() >= 4 && safePath(l[0])) {
            const bool pack = l[1] == "1";
            const std::string target = localPathOf(root, romDir, l[0], pack);
            const bool ok = pack ? installDir(item, target) : installFile(item, target);
            if (ok) {
                synced[l[0]] = FileSynced{ stampOf(root, romDir, l[0], pack), strtoll(l[2].c_str(), nullptr, 10), l[3] };
                if (installed) installed->push_back(l[0]);
                n++;
            }
        }
        deleteTree(ready + "/" + key);
        removeFile(ready + "/" + name);
    }
    if (n) writeSynced(state + "/files-state.txt", synced);
    return n;
}

/* ------------------------------------------------------------------- a pass */

static std::string baseName(const std::string &p)
{
    const size_t s = p.rfind('/');
    return s == std::string::npos ? p : p.substr(s + 1);
}

FilesResult filesPass(Store *store, const FilesPassArgs &a)
{
    FilesResult res;
    const std::string state = a.root + "/.gevr-sync";
    const std::string work = state + "/work";
    filesRecover(a.root, a.romDir);   // what a quit left mid-step
    auto say = [&](const std::string &line, float done) {
        if (a.progress) a.progress(line, done);
    };

    // everyone's index; this device's own from its cache (the store's copy may lag)
    std::vector<std::string> names;
    std::string why;
    if (!store->list(&names, &why)) {
        res.error = why;
        return res;
    }
    std::map<std::string, FilesIndex> indexes;   // device id -> its index
    std::string storedMine;                      // this device's index as the store has it
    bool haveStoredMine = false;
    for (const std::string &n : names) {
        std::string id, text;
        if (!filesIndexNameId(n, &id)) continue;
        if (id == a.deviceId) {
            haveStoredMine = store->read(n, &storedMine, &why);
            continue;
        }
        if (!store->read(n, &text, &why)) {
            res.error = why;   // a partial view could put an old copy over a newer one
            return res;
        }
        FilesIndex ix;
        if (filesIndexParse(text, &ix)) indexes[id] = ix;
    }
    FilesIndex mine;
    {
        std::string text;
        if (readFile(state + "/files-mine.txt", &text, 16u << 20)) filesIndexParse(text, &mine);
    }
    const FilesIndex mineBefore = mine;

    // the newest entry for each path, and the newest stamp seen
    long long seen = 0;
    std::map<std::string, std::pair<const FileEntry *, std::string>> winners;   // path -> entry, its device
    auto offer = [&](const FilesIndex &ix, const std::string &dev) {
        for (const auto &kv : ix) {
            seen = std::max(seen, kv.second.t);
            auto w = winners.find(kv.first);
            if (w == winners.end() || kv.second.t > w->second.first->t ||
                (kv.second.t == w->second.first->t && dev > w->second.second)) {
                winners[kv.first] = { &kv.second, dev };
            }
        }
    };
    offer(mine, a.deviceId);
    for (const auto &kv : indexes) offer(kv.second, kv.first);
    const long long stamp = std::max(a.now, seen + 1);

    std::map<std::string, FileSynced> synced = readSynced(state + "/files-state.txt");
    std::map<std::string, LocalFile> local;
    for (const LocalFile &f : localFiles(a.root, a.romDir, a.packs)) local[f.path] = f;
    std::set<std::string> pending;   // waiting in ready/ for the next launch
    for (const std::string &n : listDir(state + "/ready")) {
        std::string info;
        if (n.size() > 4 && n.compare(n.size() - 4, 4, ".txt") == 0 && readFile(state + "/ready/" + n, &info, 65536)) {
            pending.insert(lines(info).empty() ? std::string() : lines(info)[0]);
        }
    }

    std::set<std::string> paths;
    for (const auto &kv : local) paths.insert(kv.first);
    for (const auto &kv : winners) {
        if (!kv.second.first->pack || a.packs) paths.insert(kv.first);
    }

    std::map<std::string, long long> blobs;
    bool haveBlobs = false;
    auto blobThere = [&](const std::string &id) -> bool {
        if (!haveBlobs) haveBlobs = store->listBlobs(&blobs, &why);
        return haveBlobs && blobs.count(id);
    };

    for (const std::string &p : paths) {
        if (a.stop && a.stop()) break;
        if (a.hold && a.hold()) {   // a level is loaded: nothing new until the menus
            res.held = true;
            break;
        }
        if (pending.count(p)) continue;
        auto l = local.find(p);
        auto w = winners.find(p);
        auto s = synced.find(p);
        const LocalFile *L = l != local.end() ? &l->second : nullptr;
        const FileEntry *W = w != winners.end() ? w->second.first : nullptr;
        FileMove move = decideFileMove(L, W, s != synced.end() ? &s->second : nullptr);
        if (move == FileMove::None) continue;
        if (move == FileMove::Adopt) {
            synced[p] = FileSynced{ L->stamp, W->t, W->blob };
            continue;
        }
        if (move == FileMove::Up) {
            say("Sync: checking " + baseName(p), -1);
            const std::string id = contentId(a.root, a.romDir, *L, work);
            if (id.empty()) {
                res.failed++;
                res.error = "could not read " + p;
                continue;
            }
            if (W && W->blob == id) {   // the same content as the others': in step
                synced[p] = FileSynced{ L->stamp, W->t, W->blob };
                continue;
            }
            if (!blobThere(id)) {
                if (!haveBlobs) {
                    res.failed++;
                    res.error = why;
                    break;
                }
                std::string upPath = localPathOf(a.root, a.romDir, p, L->pack);
                const std::string zip = work + "/" + id + ".zip";
                if (L->pack) {
                    say("Sync: packing " + baseName(p), -1);
                    if (!zipPack(a.root + "/" + p, zip, &why)) {
                        res.failed++;
                        res.error = why;
                        continue;
                    }
                    upPath = zip;
                }
                const std::string label = L->pack ? baseName(p) + ".zip" : baseName(p);
                const bool ok = store->putBlob(id, label, upPath, [&](long long done, long long total) {
                    say("Sync: uploading " + baseName(p), total > 0 ? (float)done / (float)total : -1);
                    return !(a.stop && a.stop());
                }, &why);
                if (L->pack) removeFile(zip);
                if (!ok) {
                    res.failed++;
                    res.error = why;
                    continue;
                }
                blobs[id] = L->bytes;
            }
            const std::string old = mine.count(p) ? mine[p].blob : std::string();
            mine[p] = FileEntry{ p, id, L->pack, L->bytes, stamp };
            synced[p] = FileSynced{ L->stamp, stamp, id };
            res.moved++;
            // the blob this one replaced, when no other index names it
            if (!old.empty() && old != id) {
                bool named = false;
                for (const auto &ix : indexes) {
                    for (const auto &e : ix.second) named = named || e.second.blob == old;
                }
                for (const auto &e : mine) named = named || (e.first != p && e.second.blob == old);
                if (!named) res.obsolete.push_back(old);
            }
            continue;
        }
        // Down. Already the same here (an install whose record a quit cut off): in step
        if (L) {
            say("Sync: checking " + baseName(p), -1);
            if (contentId(a.root, a.romDir, *L, work) == W->blob) {
                synced[p] = FileSynced{ L->stamp, W->t, W->blob };
                continue;
            }
        }
        if (W->pack && a.freeBytes) {
            const long long free = a.freeBytes(a.root);
            if (free >= 0 && free < W->bytes * 2 + (64LL << 20)) {
                res.failed++;
                res.error = "not enough room for " + baseName(p);
                continue;
            }
        }
        const std::string got = work + "/" + W->blob + (W->pack ? ".zip" : ".bin");
        const bool ok = store->getBlob(W->blob, got, [&](long long done, long long) {
            say("Sync: downloading " + baseName(p), W->bytes > 0 ? (float)done / (float)W->bytes : -1);
            return !(a.stop && a.stop());
        }, &why);
        if (!ok) {
            res.failed++;
            res.error = why;
            continue;
        }
        // whole and the same: a ROM's SHA-256 is its id; a pack's zip has the store's size
        // (and unpacking checks it); a short download never goes in place
        {
            bool whole;
            if (!W->pack) {
                whole = fileSize(got) == W->bytes && "r" + sha256File(got) == W->blob;
            } else {
                if (!haveBlobs) haveBlobs = store->listBlobs(&blobs, &why);
                auto b = blobs.find(W->blob);
                whole = !haveBlobs || b == blobs.end() || b->second <= 0 || fileSize(got) == b->second;
            }
            if (!whole) {
                removeFile(got);
                res.failed++;
                res.error = baseName(p) + " came down incomplete";
                continue;
            }
        }
        std::string item = got;
        const std::string key = W->blob.substr(0, 24);
        const std::string ready = state + "/ready/" + key;
        if (W->pack) {
            deleteTree(ready);
            makeDirs(ready);
            UnzipRules rules;
            rules.least = 1;
            rules.minEntry = 0;
            rules.maxEntry = 8LL << 30;
            rules.maxTotal = 64LL << 30;
            try {
                say("Sync: unpacking " + baseName(p), -1);
                unzipBounded(got, ready + "/item", rules, [&](int) { return !(a.stop && a.stop()); });
            } catch (const HttpError &e) {
                removeFile(got);
                deleteTree(ready);
                res.failed++;
                res.error = e.message;
                continue;
            }
            removeFile(got);
            item = ready + "/item";
        }
        const std::string target = localPathOf(a.root, a.romDir, p, W->pack);
        const bool there = W->pack ? isDir(target) : isFile(target);
        if (!there) {
            const bool placed = W->pack ? installDir(item, target) : installFile(item, target);
            if (W->pack) deleteTree(ready);
            if (!placed) {
                removeFile(got);
                res.failed++;
                res.error = "could not put " + p + " in place";
                continue;
            }
            synced[p] = FileSynced{ stampOf(a.root, a.romDir, p, W->pack), W->t, W->blob };
            res.arrived.push_back(p);
        } else {
            // the game may have it open: in place at the next launch
            makeDirs(ready);
            if (!W->pack && !movePath(got, ready + "/item", true)) {
                removeFile(got);
                res.failed++;
                res.error = "could not keep " + p + " for the next launch";
                continue;
            }
            writeFileAtomic(state + "/ready/" + key + ".txt",
                            p + "\n" + (W->pack ? "1" : "0") + "\n" + std::to_string(W->t) + "\n" + W->blob + "\n");
            res.deferred.push_back(p);
        }
        res.moved++;
    }

    writeSynced(state + "/files-state.txt", synced);
    const std::string mineText = filesIndexText(mine);
    if (mineText != filesIndexText(mineBefore)) writeFileAtomic(state + "/files-mine.txt", mineText);
    // up whenever the store's copy differs: a change, a write that failed before, a trashed file
    if (!haveStoredMine || storedMine != mineText) {
        if (!store->write(filesIndexName(a.deviceId), mineText, &why)) {
            res.failed++;
            res.error = why;
            return res;
        }
    }
    for (const std::string &id : res.obsolete) store->removeBlob(id, &why);
    return res;
}

}   // namespace gsync
