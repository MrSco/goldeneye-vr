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
 * One thread scans the tree, then decodes on request with stb_image, halving
 * anything over 1024 texels on a side (the Quest would only minify it). The
 * render thread takes finished images and uploads them (gfx_pc.cpp).
 */

#include "gevr_texpack.h"

#include <atomic>
#include <condition_variable>
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

#ifdef __ANDROID__
#include <android/log.h>
#define TPLOG(...) __android_log_print(ANDROID_LOG_INFO, "GoldenEye-VR", __VA_ARGS__)
#else
#define TPLOG(...) (printf(__VA_ARGS__), printf("\n"))
#endif

#include "external/stb_image.h"   // the implementation is in port/src/ext_tex.c

namespace gevrtp {

enum { UNLOADED, QUEUED, READY, FAILED };

struct Entry {
    std::string path;
    uint8_t fmt, siz;
    int state = UNLOADED;
    std::vector<uint8_t> rgba;
    uint32_t w = 0, h = 0;
    uint64_t lastUse = 0;
};

static std::vector<Entry> s_entries;                          // fixed once the scan is published
static std::unordered_map<uint64_t, std::vector<int>> s_index;
static std::mutex s_mu;
static std::condition_variable s_cv;
static std::deque<int> s_queue;
static std::vector<int> s_done;
static std::atomic<bool> s_started{false};
static std::atomic<bool> s_ready{false};
static bool s_announced = false;
static uint64_t s_tick = 0;
static size_t s_held = 0;                                     // bytes of decoded images

static const int MAX_SIDE = 1024;

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
    if (d == nullptr) return;
    struct dirent *de;
    while ((de = readdir(d)) != nullptr) {
        const char *name = de->d_name;
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
        if (!parseName(name, &key, &fmt, &siz)) continue;
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
    std::vector<Entry> entries;
    std::unordered_map<uint64_t, std::vector<int>> index;
    scan(dir, entries, index);
    TPLOG("texpack: %zu textures indexed in %s", entries.size(), dir.c_str());
    {
        std::lock_guard<std::mutex> lk(s_mu);
        s_entries.swap(entries);
        s_index.swap(index);
    }
    s_ready = !s_entries.empty();

    while (true) {
        int id;
        {
            std::unique_lock<std::mutex> lk(s_mu);
            s_cv.wait(lk, [] { return !s_queue.empty(); });
            id = s_queue.front();
            s_queue.pop_front();
        }
        const std::string &path = s_entries[id].path;   // never changes after publishing
        int w = 0, h = 0, n = 0;
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
                s_held += rgba.size();
                e.rgba.swap(rgba);
                e.w = uw;
                e.h = uh;
                e.state = READY;
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

} // namespace gevrtp
