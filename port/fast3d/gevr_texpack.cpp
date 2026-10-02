/*
 * Issue #25: GLideN64 ("Rice") hi-res texture packs; see gevr_texpack.h.
 *
 * A pack is a tree of PNGs named IDENT#CRC#F#S[#PAL]_suffix.png, as GLideN64
 * reads them (GLideNHQ/TxHiResLoader.cpp): CRC and PAL are the texture's and
 * palette's checksums in hex, F and S the draw tile's format and texel size.
 * "$" in the CRC or PAL field is a wildcard. The index key is PAL<<32|CRC
 * when a palette checksum is given, PAL alone when the CRC is a wildcard, and
 * CRC otherwise; a match also needs F and S.
 *
 * One thread scans the tree, warms a bounded set of boot menus/fonts, then
 * decodes on request with stb_image, halving
 * anything over 1024 texels on a side (the Quest would only minify it). The
 * render thread takes finished images and uploads them (gfx_pc.cpp).
 */

#include "gevr_texpack.h"
#include "gevr_texpack_preload.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include <dirent.h>
#include <sys/stat.h>
#include <sys/resource.h>
#include <unistd.h>

#ifdef __ANDROID__
#include <android/log.h>
#define TPLOG(...) __android_log_print(ANDROID_LOG_INFO, "GoldenEye-VR", __VA_ARGS__)
#else
#define TPLOG(...) (printf(__VA_ARGS__), printf("\n"))
#endif

#include "external/stb_image.h"   // the implementation is in port/src/ext_tex.c
#include <zlib.h>

namespace gevrtp {

enum { UNLOADED, QUEUED, DECODING, READY, FAILED };

struct Entry {
    std::string path;
    uint8_t fmt, siz;
    int state = UNLOADED;
    std::vector<uint8_t> rgba;
    uint32_t w = 0, h = 0;
    uint64_t lastUse = 0;
    bool bootFont = false;
    bool bootBackground = false;
};

static std::vector<Entry> s_entries;                          // fixed once the scan is published
static std::unordered_map<uint64_t, std::vector<int>> s_index;
static std::mutex s_mu;
static std::condition_variable s_cv;
static std::deque<int> s_queue;
static std::deque<int> s_bootFonts, s_bootMenu, s_bootBackground;
static std::vector<uint32_t> s_bootGlyphs;                     // keys requested before indexing finishes
static std::vector<uint32_t> s_bootBackgroundKeys;
static size_t s_bootFontBytes = 0, s_bootMenuBytes = 0;
static size_t s_bootBackgroundBytes = 0;
static std::vector<int> s_done;
static std::atomic<bool> s_started{false};
static std::atomic<bool> s_ready{false};
static std::atomic<bool> s_scanned{false};   // the scan is over, textures in it or not
static bool s_announced = false;
static uint64_t s_tick = 0;
static size_t s_held = 0;                                     // bytes of decoded images

static const int MAX_SIDE = 1024;
// Speculative warming is bounded independently of the normal 160 MiB cache.
// Separate allowances keep large menu artwork from crowding out the legal text.
static const size_t BOOT_FONT_BUDGET = (size_t)8 << 20;
static const size_t BOOT_MENU_BUDGET = (size_t)8 << 20;
static const size_t BOOT_BACKGROUND_BUDGET = (size_t)4 << 20;
static const size_t BOOT_SOURCE_LIMIT = (size_t)16 << 20;

struct BootTexture { uint32_t crc; uint8_t fmt, siz; };
// Common folder/menu art, independent of pack directory layout. In particular,
// don't warm all of UI/ or Title and menus. The 299 background scanlines are
// selected separately from the ROM's gunbarrel image, rather than by folder.
static const BootTexture BOOT_MENU[] = {
    {0x551AADB3, 4, 0}, // dossier cover
    {0xD34954E9, 4, 0}, // paper
    {0xB6C88513, 4, 0}, // Royal Coat of Arms
    {0x20AEEE9E, 4, 0}, {0xC97BB1FB, 4, 0}, // Brosnan portrait, top
    {0xCEEB36E2, 4, 0}, {0xC654D2CA, 4, 0}, // Brosnan portrait, bottom
    {0x5A516EDA, 0, 3}, {0x63999CD0, 0, 3}, // copy / erase
    {0x7BFD977B, 3, 1}, // SELECT FILE
    {0xD1FC843A, 2, 1}, {0xBCE9E819, 2, 1}, // cross / check
    {0xC06E0D45, 2, 0}, // dot
    {0xF3286ADF, 3, 1}, {0xBD8ABEF7, 3, 1}, // CLASSIFIED
    {0xBF6B4224, 3, 1}, {0x8D87559E, 3, 1}, // CONFIDENTIAL
    {0x8A16827B, 3, 1}, {0xF04D7982, 3, 1}, // EYES ONLY
    {0x1DEDE94C, 3, 1}, {0x01B6B209, 3, 1}, // FOR YOUR
    {0xBDEE869C, 3, 1}, {0xD11BA786, 3, 1}, // OHMSS
};

// s_mu held. Palette variants of the same menu icon are harmless to warm;
// its texture CRC is the low word of the combined palette/texture index key.
static void queueBoot(uint32_t crc, uint8_t fmt, uint8_t siz, bool font, bool background = false) {
    const auto queueMatches = [&](const std::vector<int> &ids) {
        for (int id : ids) {
            Entry &e = s_entries[id];
            if (e.fmt != fmt || e.siz != siz || e.state != UNLOADED) continue;
            e.state = QUEUED;
            e.bootFont = font;
            e.bootBackground = background;
            (font ? s_bootFonts : background ? s_bootBackground : s_bootMenu).push_back(id);
        }
    };
    if (font || background) {
        // These I8 textures have no palette. Avoid a full index walk for
        // every font glyph and each of the 299 background rows.
        auto it = s_index.find(crc);
        if (it != s_index.end()) queueMatches(it->second);
    } else {
        for (const auto &slot : s_index) {
            if ((uint32_t)slot.first == crc) queueMatches(slot.second);
        }
    }
}

static uint32_t bootI8Checksum(const uint8_t *pixels, int width, int height) {
    // Same Rice checksum as gfx_pc.cpp's I8 imports, using local state so
    // ROM binding can overlap the render thread safely.
    const uint8_t *base = (const uint8_t *)((uintptr_t)pixels & ~(uintptr_t)3);
    intptr_t row = pixels - base;
    uint32_t crc = 0;
    for (int y = height - 1; y >= 0; --y, row += width) {
        uint32_t e = 0;
        for (int x = width - 4; x >= 0; x -= 4) {
            const intptr_t a = row + x;
            const uint32_t word = base[a ^ 3] | (uint32_t)base[(a + 1) ^ 3] << 8
                | (uint32_t)base[(a + 2) ^ 3] << 16 | (uint32_t)base[(a + 3) ^ 3] << 24;
            e = word ^ (uint32_t)x;
            crc = ((crc << 4) | (crc >> 28)) + e;
        }
        crc += e ^ (uint32_t)y;
    }
    return crc;
}

static void preloadGlyph(uint32_t crc) {
    if (!s_started) return; // Original textures: no background work
    std::lock_guard<std::mutex> lk(s_mu);
    if (!s_scanned) {
        if (s_bootGlyphs.size() < 188 && std::find(s_bootGlyphs.begin(), s_bootGlyphs.end(), crc) == s_bootGlyphs.end())
            s_bootGlyphs.push_back(crc);
    } else {
        queueBoot(crc, 4, 1, true);
        s_cv.notify_one();
    }
}

// "0123ABCD" or "$" -> value / wildcard; false if neither
static bool parseHex(const std::string &s, uint32_t *v, bool *wild) {
    *wild = false;
    if (s == "$") {
        *wild = true;
        *v = 0;
        return true;
    }
    if (s.empty() || s.size() > 8) return false;
    char *end = nullptr;
    unsigned long x = strtoul(s.c_str(), &end, 16);
    if (end == nullptr || *end != 0) return false;
    *v = (uint32_t)x;
    return true;
}

static bool parseName(const char *name, uint64_t *key, uint8_t *fmt, uint8_t *siz) {
    size_t len = strlen(name);
    if (len < 5 || strcasecmp(name + len - 4, ".png") != 0) return false;

    // IDENT # CRC # F # S [# PAL] _ suffix
    std::string s(name, len);
    size_t us = s.rfind('_');
    if (us == std::string::npos) return false;
    std::string suffix = s.substr(us + 1);
    if (strcasecmp(suffix.c_str(), "all.png") != 0 && strcasecmp(suffix.c_str(), "ciByRGBA.png") != 0
            && strcasecmp(suffix.c_str(), "allciByRGBA.png") != 0) {
        return false;   // _rgb/_a pairs and .bmp: not used by the packs we list
    }
    std::vector<std::string> f;
    size_t pos = 0;
    std::string head = s.substr(0, us);
    while (true) {
        size_t at = head.find('#', pos);
        f.push_back(head.substr(pos, at == std::string::npos ? std::string::npos : at - pos));
        if (at == std::string::npos) break;
        pos = at + 1;
    }
    if (f.size() != 4 && f.size() != 5) return false;

    uint32_t crc, pal = 0, fv, sv;
    bool crcWild, palWild = true, dummy;
    if (!parseHex(f[1], &crc, &crcWild)) return false;
    if (!parseHex(f[2], &fv, &dummy) || dummy || fv > 4) return false;
    if (!parseHex(f[3], &sv, &dummy) || dummy || sv > 3) return false;
    if (f.size() == 5 && !parseHex(f[4], &pal, &palWild)) return false;

    if (!palWild) {
        *key = crcWild ? (uint64_t)pal : ((uint64_t)pal << 32 | crc);
    } else {
        if (crcWild || crc == 0) return false;
        *key = crc;
    }
    *fmt = (uint8_t)fv;
    *siz = (uint8_t)sv;
    return true;
}

static void scan(const std::string &dir, std::vector<Entry> &out,
                 std::unordered_map<uint64_t, std::vector<int>> &index) {
    DIR *d = opendir(dir.c_str());
    if (d == nullptr) {
        TPLOG("texpack: can't open %s (%s)", dir.c_str(), strerror(errno));
        return;
    }
    struct dirent *de;
    int seen = 0, bad = 0;
    while ((de = readdir(d)) != nullptr) {
        const char *name = de->d_name;
        ++seen;
        if (name[0] == '.' || strncmp(name, "~!~", 3) == 0) continue;   // as GLideN64 skips them
        std::string path = dir + "/" + name;
        bool isDir = de->d_type == DT_DIR;
        if (de->d_type == DT_UNKNOWN) {
            struct stat st;
            isDir = stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
        }
        if (isDir) {
            scan(path, out, index);
            continue;
        }
        uint64_t key;
        uint8_t fmt, siz;
        if (!parseName(name, &key, &fmt, &siz)) {
            if (bad++ < 3) TPLOG("texpack: not a pack texture name: %s", name);
            continue;
        }
        std::vector<int> &slot = index[key];
        bool dup = false;
        for (int i : slot) {
            if (out[i].fmt == fmt && out[i].siz == siz) {
                dup = true;
                break;
            }
        }
        if (dup) continue;
        Entry e;
        e.path = path;
        e.fmt = fmt;
        e.siz = siz;
        slot.push_back((int)out.size());
        out.push_back(std::move(e));
    }
    closedir(d);
    if (bad > 0) TPLOG("texpack: %s: %d entries, %d names skipped", dir.c_str(), seen, bad);
}

// halve (2x2 box) until both sides are at most MAX_SIDE
static void shrink(std::vector<uint8_t> &px, uint32_t &w, uint32_t &h) {
    while ((w > MAX_SIDE || h > MAX_SIDE) && w >= 2 && h >= 2) {
        uint32_t nw = w / 2, nh = h / 2;
        std::vector<uint8_t> out((size_t)nw * nh * 4);
        for (uint32_t y = 0; y < nh; ++y) {
            const uint8_t *r0 = &px[(size_t)(2 * y) * w * 4];
            const uint8_t *r1 = &px[(size_t)(2 * y + 1) * w * 4];
            uint8_t *o = &out[(size_t)y * nw * 4];
            for (uint32_t x = 0; x < nw; ++x) {
                for (int c = 0; c < 4; ++c) {
                    o[x * 4 + c] = (uint8_t)((r0[8 * x + c] + r0[8 * x + 4 + c] + r1[8 * x + c] + r1[8 * x + 4 + c] + 2) / 4);
                }
            }
        }
        px.swap(out);
        w = nw;
        h = nh;
    }
}

static void worker(std::string dir) {
#ifdef __ANDROID__
    // issue #52: never ahead of the game's own thread (it decodes flat out as a level starts)
    setpriority(PRIO_PROCESS, (id_t)gettid(), 10);
#endif
    std::vector<Entry> entries;
    std::unordered_map<uint64_t, std::vector<int>> index;
    scan(dir, entries, index);
    TPLOG("texpack: %zu textures indexed in %s", entries.size(), dir.c_str());
    {
        std::lock_guard<std::mutex> lk(s_mu);
        s_entries.swap(entries);
        s_index.swap(index);
        for (uint32_t crc : s_bootGlyphs) queueBoot(crc, 4, 1, true);
        s_bootGlyphs.clear();
        for (uint32_t crc : s_bootBackgroundKeys) queueBoot(crc, 4, 1, false, true);
        s_bootBackgroundKeys.clear();
        for (const BootTexture &t : BOOT_MENU) queueBoot(t.crc, t.fmt, t.siz, false);
        TPLOG("texpack: boot preload queued %zu font glyphs, %zu menu textures, %zu background rows (8/8/4 MiB)",
              s_bootFonts.size(), s_bootMenu.size(), s_bootBackground.size());
        s_ready = !s_entries.empty();
        s_scanned = true;
    }

    while (true) {
        int id;
        bool preload;
        {
            std::unique_lock<std::mutex> lk(s_mu);
            s_cv.wait(lk, [] {
                return !s_queue.empty() || !s_bootFonts.empty() || !s_bootBackground.empty() || !s_bootMenu.empty();
            });
            preload = s_queue.empty();
            auto &queue = !preload ? s_queue : !s_bootFonts.empty() ? s_bootFonts
                : !s_bootBackground.empty() ? s_bootBackground : s_bootMenu;
            id = queue.front();
            queue.pop_front();
            s_entries[id].state = DECODING;
        }
        const std::string &path = s_entries[id].path;   // never changes after publishing
        int w = 0, h = 0, n = 0;
        if (preload) {
            // Inspect before allocating, so a custom 4K/8K pack cannot turn
            // a small boot preload into a large temporary allocation.
            const Entry &e = s_entries[id];
            size_t &held = e.bootFont ? s_bootFontBytes : e.bootBackground ? s_bootBackgroundBytes : s_bootMenuBytes;
            const size_t budget = e.bootFont ? BOOT_FONT_BUDGET
                : e.bootBackground ? BOOT_BACKGROUND_BUDGET : BOOT_MENU_BUDGET;
            bool fits = stbi_info(path.c_str(), &w, &h, &n) && w > 0 && h > 0
                && (uint64_t)w * h * 4 <= BOOT_SOURCE_LIMIT;
            uint32_t pw = (uint32_t)w, ph = (uint32_t)h;
            while ((pw > MAX_SIDE || ph > MAX_SIDE) && pw >= 2 && ph >= 2) { pw /= 2; ph /= 2; }
            const uint64_t bytes = (uint64_t)pw * ph * 4;
            if (!fits || bytes > budget - held) {
                std::lock_guard<std::mutex> lk(s_mu);
                s_entries[id].state = UNLOADED; // on-demand decoding remains available
                // A draw may have requested it while the header was being
                // inspected. Wake the renderer's pending job so it retries
                // through the foreground queue rather than waiting forever.
                s_done.push_back(id);
                continue;
            }
        }
        uint8_t *px = stbi_load(path.c_str(), &w, &h, &n, 4);
        std::vector<uint8_t> rgba;
        uint32_t uw = 0, uh = 0;
        if (px != nullptr && w > 0 && h > 0) {
            rgba.assign(px, px + (size_t)w * h * 4);
            uw = (uint32_t)w;
            uh = (uint32_t)h;
            shrink(rgba, uw, uh);
        } else {
            TPLOG("texpack: could not decode %s", path.c_str());
        }
        if (px != nullptr) stbi_image_free(px);
        {
            std::lock_guard<std::mutex> lk(s_mu);
            Entry &e = s_entries[id];
            if (!rgba.empty()) {
                if (preload) (e.bootFont ? s_bootFontBytes : e.bootBackground ? s_bootBackgroundBytes : s_bootMenuBytes) += rgba.size();
                s_held += rgba.size();
                e.rgba.swap(rgba);
                e.w = uw;
                e.h = uh;
                e.state = READY;
                e.lastUse = ++s_tick;   // the newest: trim() mustn't free it before it's uploaded (#52)
            } else {
                e.state = FAILED;
            }
            s_done.push_back(id);
        }
    }
}

void start(const char *dir) {
    bool expected = false;
    if (dir == nullptr || dir[0] == 0 || !s_started.compare_exchange_strong(expected, true)) return;
    std::thread(worker, std::string(dir)).detach();
}

bool takeIndexReady() {
    if (s_announced || !s_ready) return false;
    s_announced = true;
    return true;
}

void waitIndex(int ms) {
    for (int waited = 0; s_started && !s_scanned && waited < ms; waited += 10) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}

const char *name(int id) {
    if (!s_ready || id < 0 || id >= (int)s_entries.size()) return "";
    const std::string &p = s_entries[id].path;   // never changes after publishing
    const size_t slash = p.find_last_of('/');
    return p.c_str() + (slash == std::string::npos ? 0 : slash + 1);
}

int find(uint64_t key, uint8_t fmt, uint8_t siz) {
    if (!s_ready) return -1;
    auto it = s_index.find(key);   // read-only after publishing
    if (it == s_index.end()) return -1;
    for (int i : it->second) {
        if (s_entries[i].fmt == fmt && s_entries[i].siz == siz) return i;
    }
    return -1;
}

const uint8_t *image(int id, uint32_t *w, uint32_t *h) {
    if (!s_ready || id < 0 || id >= (int)s_entries.size()) return nullptr;
    std::lock_guard<std::mutex> lk(s_mu);
    Entry &e = s_entries[id];
    e.lastUse = ++s_tick;
    if (e.state == READY) {
        *w = e.w;
        *h = e.h;
        return e.rgba.data();
    }
    if (e.state == UNLOADED) {
        e.state = QUEUED;
        s_queue.push_back(id);
        s_cv.notify_one();
    } else if (e.state == QUEUED) {
        // A visible texture always goes ahead of speculative boot warming.
        auto &queue = e.bootFont ? s_bootFonts : e.bootBackground ? s_bootBackground : s_bootMenu;
        auto it = std::find(queue.begin(), queue.end(), id);
        if (it != queue.end()) {
            queue.erase(it);
            s_queue.push_back(id);
            s_cv.notify_one();
        }
    }
    return nullptr;
}

int takeDone(int *ids, int max) {
    if (!s_ready) return 0;
    std::lock_guard<std::mutex> lk(s_mu);
    int n = 0;
    while (n < max && !s_done.empty()) {
        ids[n++] = s_done.back();
        s_done.pop_back();
    }
    return n;
}

void trim(size_t budget) {
    if (!s_ready) return;
    std::lock_guard<std::mutex> lk(s_mu);
    while (s_held > budget) {
        int oldest = -1;
        for (int i = 0; i < (int)s_entries.size(); ++i) {
            if (s_entries[i].state == READY && (oldest < 0 || s_entries[i].lastUse < s_entries[oldest].lastUse)) {
                oldest = i;
            }
        }
        if (oldest < 0) break;
        Entry &e = s_entries[oldest];
        s_held -= e.rgba.size();
        std::vector<uint8_t>().swap(e.rgba);
        e.state = UNLOADED;   // decoded again if it's needed again
    }
}

/*
 * Texture dump for tools/texai: the textures a pack lacks, as GLideN64 would
 * dump them (its names, RGBA PNG), plus a line each in index.tsv. Written on a
 * thread of its own: entering a level imports hundreds of textures at once.
 */
struct DumpJob {
    std::string name, line;
    std::vector<uint8_t> rgba;
    uint32_t w, h;
};
static std::mutex s_dumpMu;
static std::condition_variable s_dumpCv;
static std::deque<DumpJob> s_dumpQueue;
static std::string s_dumpDir;

static void pngChunk(FILE *f, const char *type, const uint8_t *data, uint32_t len) {
    uint8_t be[4] = { (uint8_t)(len >> 24), (uint8_t)(len >> 16), (uint8_t)(len >> 8), (uint8_t)len };
    fwrite(be, 1, 4, f);
    fwrite(type, 1, 4, f);
    if (len) fwrite(data, 1, len, f);
    uLong crc = crc32(0, (const Bytef *)type, 4);
    if (len) crc = crc32(crc, data, len);
    uint8_t c[4] = { (uint8_t)(crc >> 24), (uint8_t)(crc >> 16), (uint8_t)(crc >> 8), (uint8_t)crc };
    fwrite(c, 1, 4, f);
}

static bool writePng(const std::string &path, const DumpJob &j) {
    // filter byte 0 (none) before each row, then deflate
    std::vector<uint8_t> raw((size_t)(j.w * 4 + 1) * j.h);
    for (uint32_t y = 0; y < j.h; ++y) {
        raw[(size_t)y * (j.w * 4 + 1)] = 0;
        memcpy(&raw[(size_t)y * (j.w * 4 + 1) + 1], &j.rgba[(size_t)y * j.w * 4], (size_t)j.w * 4);
    }
    uLongf zlen = compressBound((uLong)raw.size());
    std::vector<uint8_t> z(zlen);
    if (compress2(z.data(), &zlen, raw.data(), (uLong)raw.size(), 6) != Z_OK) return false;
    FILE *f = fopen(path.c_str(), "wb");
    if (f == nullptr) return false;
    static const uint8_t sig[8] = { 0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n' };
    fwrite(sig, 1, 8, f);
    const uint8_t ihdr[13] = { (uint8_t)(j.w >> 24), (uint8_t)(j.w >> 16), (uint8_t)(j.w >> 8), (uint8_t)j.w,
                               (uint8_t)(j.h >> 24), (uint8_t)(j.h >> 16), (uint8_t)(j.h >> 8), (uint8_t)j.h,
                               8, 6, 0, 0, 0 };   // 8-bit RGBA, no interlace
    pngChunk(f, "IHDR", ihdr, 13);
    pngChunk(f, "IDAT", z.data(), (uint32_t)zlen);
    pngChunk(f, "IEND", nullptr, 0);
    return fclose(f) == 0;
}

static void dumpWorker() {
#ifdef __ANDROID__
    setpriority(PRIO_PROCESS, (id_t)gettid(), 10);
#endif
    unsigned written = 0;
    while (true) {
        DumpJob j;
        {
            std::unique_lock<std::mutex> lk(s_dumpMu);
            s_dumpCv.wait(lk, [] { return !s_dumpQueue.empty(); });
            j = std::move(s_dumpQueue.front());
            s_dumpQueue.pop_front();
        }
        if (!writePng(s_dumpDir + "/" + j.name, j)) {
            TPLOG("texdump: could not write %s (%s)", j.name.c_str(), strerror(errno));
            continue;
        }
        FILE *f = fopen((s_dumpDir + "/index.tsv").c_str(), "a");
        if (f != nullptr) {
            fputs(j.line.c_str(), f);
            fclose(f);
        }
        if ((++written % 100) == 0) TPLOG("texdump: %u textures written to %s", written, s_dumpDir.c_str());
    }
}

void dumpStart(const char *dir) {
    static std::atomic<bool> started{false};
    bool expected = false;
    if (dir == nullptr || !started.compare_exchange_strong(expected, true)) return;
    s_dumpDir = dir;
    mkdir(dir, 0777);
    TPLOG("texdump: on, writing to %s", dir);
    std::thread(dumpWorker).detach();
}

void dump(const char *name, const uint8_t *rgba, uint32_t w, uint32_t h, uint32_t stride, const char *indexLine) {
    if (s_dumpDir.empty() || w == 0 || h == 0) return;
    DumpJob j;
    j.name = name;
    j.line = indexLine;
    j.w = w;
    j.h = h;
    j.rgba.resize((size_t)w * h * 4);
    for (uint32_t y = 0; y < h; ++y) memcpy(&j.rgba[(size_t)y * w * 4], rgba + (size_t)y * stride, (size_t)w * 4);
    {
        std::lock_guard<std::mutex> lk(s_dumpMu);
        s_dumpQueue.push_back(std::move(j));
    }
    s_dumpCv.notify_one();
}

} // namespace gevrtp

extern "C" void gevrTexpackPreloadGlyph(const unsigned char *pixels, int width, int height) {
    if (!gevrtp::s_started || pixels == nullptr || width < 4 || width > 256 || height <= 0 || height > 256) return;
    gevrtp::preloadGlyph(gevrtp::bootI8Checksum(pixels, width, height));
}

extern "C" void gevrTexpackPreloadFont(const unsigned char *data, unsigned int size) {
    // Cartridge layout shared with gevrRomSwapFont: 13x13 kerning words,
    // then 94 descriptors of six big-endian words, followed by I8 pixels.
    const uint32_t chars = 13 * 13 * 4, pixels = chars + 94 * 24;
    if (!gevrtp::s_started || data == nullptr || size < pixels) return;
    const auto word = [data](uint32_t ofs) {
        return (uint32_t)data[ofs] << 24 | (uint32_t)data[ofs + 1] << 16
            | (uint32_t)data[ofs + 2] << 8 | data[ofs + 3];
    };
    for (uint32_t i = 0; i < 94; ++i) {
        const uint32_t ch = chars + i * 24;
        const uint32_t h = word(ch + 8), w = word(ch + 12), ofs = word(ch + 20);
        if (w > 256 || h == 0 || h > 256 || ofs < pixels || ofs > size) continue;
        const uint32_t width = (w + 7) & 0xF8;
        if ((uint64_t)width * h > size - ofs) continue;
        gevrTexpackPreloadGlyph(data + ofs, (int)width, (int)h);
    }
}

extern "C" void gevrTexpackPreloadBackground(const unsigned char *data, unsigned int size) {
    if (!gevrtp::s_started || data == nullptr || size < 10) return;
    // titleRenderFolderMenuBackgroundLines draws this one static image as
    // 299 separate 440x1 I8 loads. Decode its RLE once at ROM binding so all
    // those exact pack keys can be warmed long before file select opens.
    constexpr size_t width = 440, rows = 299, total = width * rows;
    if (((data[0] << 8) | data[1]) != width || ((data[2] << 8) | data[3]) != rows) return;
    std::vector<uint8_t> pixels(total);
    size_t src = 10, dst = 0;
    while (dst < total) {
        if (src + 2 > size) return;
        const size_t count = data[src];
        if (count == 0 || count > total - dst) return;
        std::fill_n(pixels.data() + dst, count, data[src + 1]);
        src += 2;
        dst += count;
    }
    std::lock_guard<std::mutex> lk(gevrtp::s_mu);
    for (size_t row = 0; row < rows; ++row) {
        const uint32_t crc = gevrtp::bootI8Checksum(pixels.data() + row * width, (int)width, 1);
        if (!gevrtp::s_scanned) {
            auto &keys = gevrtp::s_bootBackgroundKeys;
            if (keys.size() < rows && std::find(keys.begin(), keys.end(), crc) == keys.end()) keys.push_back(crc);
        } else {
            gevrtp::queueBoot(crc, 4, 1, false, true);
        }
    }
    TPLOG("texpack: file-select background preload queued (%zu row keys, 4 MiB allowance)", rows);
    gevrtp::s_cv.notify_one();
}
