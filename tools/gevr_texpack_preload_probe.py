"""Exercise boot preloading and demand priority without a ROM, PNG decoder or GPU."""
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
loader = (ROOT / "port/fast3d/gevr_texpack.cpp").read_text()
# Keep the production queue, worker, publication and cache code. Only filesystem
# scanning and image IO are replaced with deterministic fixtures.
production = loader[loader.index("namespace gevrtp {"):loader.index("/*\n * Texture dump")]
scan_start = production.index("static void scan(")
scan_end = production.index("// halve (2x2 box)")
production = production[:scan_start] + r"""
static void scan(const std::string &, std::vector<Entry> &entries,
                 std::unordered_map<uint64_t, std::vector<int>> &index) {
    for (int i = 0; i < 8; ++i) {
        Entry e; e.path = std::to_string(i); e.fmt = 4; e.siz = i < 4 ? 0 : 1;
        entries.push_back(std::move(e));
        index[i < 4 ? BOOT_MENU[i].crc : i == 4 ? fixtureGlyphKey
            : i == 6 ? fixtureBackgroundKey : i == 7 ? 0xFACEBABE : 0xDEADBEEF].push_back(i);
    }
}
""" + production[scan_end:]
production += "\n} // namespace gevrtp\n" + loader[loader.index('extern "C" void gevrTexpackPreloadGlyph'):]
renderer = (ROOT / "port/fast3d/gfx_pc.cpp").read_text()
checksum = renderer[renderer.index("static int s_tpSwap = 3;"):renderer.index("/* the highest colour index")]

stub = r"""
#include <algorithm>
#include <atomic>
#include <cassert>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>
#define TPLOG(...) ((void)0)
#ifdef _WIN32
#define strcasecmp _stricmp
#endif
static std::mutex gateMu;
static std::condition_variable gateCv;
static bool inspecting = false, releaseInfo = false;
static std::vector<int> loads;
static uint32_t fixtureGlyphKey;
static uint32_t fixtureBackgroundKey;
static int stbi_info(const char *path, int *w, int *h, int *n) {
    const int id = atoi(path);
    *w = *h = id == 2 ? 8192 : 1024; *n = 4;
    if (id == 2) {
        std::unique_lock<std::mutex> lk(gateMu);
        inspecting = true; gateCv.notify_one();
        gateCv.wait(lk, [] { return releaseInfo; });
    }
    return 1;
}
static uint8_t *stbi_load(const char *path, int *w, int *h, int *, int) {
    const int id = atoi(path);
    { std::lock_guard<std::mutex> lk(gateMu); loads.push_back(id); }
    *w = *h = id == 2 ? 4 : 1024;
    return (uint8_t *)calloc((size_t)*w * *h * 4, 1);
}
static void stbi_image_free(void *p) { free(p); }
"""

tests = r"""
using namespace gevrtp;
static void reset() {
    s_entries.clear(); s_index.clear(); s_queue.clear(); s_done.clear();
    s_bootFonts.clear(); s_bootMenu.clear(); s_bootGlyphs.clear();
    s_bootBackground.clear(); s_bootBackgroundKeys.clear(); s_bootBackgroundBytes = 0;
    s_bootFontBytes = s_bootMenuBytes = s_held = 0;
    s_ready = s_scanned = s_started = false;
}
int main() {
    alignas(4) uint8_t glyph[4096];
    for (unsigned i = 0; i < sizeof(glyph); ++i) glyph[i] = (i * 17) ^ (i >> 2);
    // Original textures must do no preload work.
    gevrTexpackPreloadGlyph(glyph, 8, 3); assert(s_bootGlyphs.empty());
    s_started = true;
    // Match the actual renderer's checksum, including unaligned pointers.
    for (int offset = 0; offset < 4; ++offset) {
        for (int width : {8, 16, 32}) {
            s_bootGlyphs.clear();
            gevrTexpackPreloadGlyph(glyph + offset, width, 12);
            assert(s_bootGlyphs.size() == 1);
            assert(s_bootGlyphs[0] == tp_rice(glyph, offset, width, 12, 1, width));
            gevrTexpackPreloadGlyph(glyph + offset, width, 12);
            assert(s_bootGlyphs.size() == 1); // deduplicate before index publication
        }
    }
    const uint32_t glyphKey = s_bootGlyphs[0];
    // The early ROM-binding path reads cartridge descriptors without keeping
    // pointers or allocating host fonts. Invalid/truncated descriptors are ignored.
    std::vector<uint8_t> font(13 * 13 * 4 + 94 * 24 + 64);
    const uint32_t chars = 13 * 13 * 4, pixels = chars + 94 * 24;
    const auto put = [&font](uint32_t ofs, uint32_t value) {
        for (int i = 0; i < 4; ++i) font[ofs + i] = (uint8_t)(value >> (24 - 8 * i));
    };
    put(chars + 8, 3); put(chars + 12, 8); put(chars + 20, pixels);
    memcpy(font.data() + pixels, glyph, 24);
    s_bootGlyphs.clear();
    gevrTexpackPreloadFont(font.data(), (unsigned)font.size());
    assert(s_bootGlyphs.size() == 1 && s_bootGlyphs[0] == tp_rice(font.data(), pixels, 8, 3, 1, 8));
    s_bootGlyphs.clear();
    gevrTexpackPreloadFont(font.data(), pixels - 1); assert(s_bootGlyphs.empty());
    put(chars + 20, (unsigned)font.size() - 8);
    gevrTexpackPreloadFont(font.data(), (unsigned)font.size()); assert(s_bootGlyphs.empty());

    // The background is one static 440x299 RLE image drawn as 299 I8 rows.
    // Check every requested key against the renderer, including run boundaries.
    std::vector<uint8_t> rle(10), background(440 * 299);
    rle[0] = 1; rle[1] = 184; rle[2] = 1; rle[3] = 43;
    std::vector<uint32_t> expectedRows;
    for (int row = 0; row < 299; ++row) {
        uint8_t a = row & 255, b = (row >> 8) + 42;
        rle.insert(rle.end(), {220, a, 220, b});
        std::fill_n(background.data() + row * 440, 220, a);
        std::fill_n(background.data() + row * 440 + 220, 220, b);
        uint32_t crc = tp_rice(background.data(), row * 440, 440, 1, 1, 440);
        if (std::find(expectedRows.begin(), expectedRows.end(), crc) == expectedRows.end()) expectedRows.push_back(crc);
    }
    gevrTexpackPreloadBackground(rle.data(), (unsigned)rle.size());
    assert(s_bootBackgroundKeys == expectedRows);
    assert(s_bootGlyphs.empty()); // background never consumes the font allowance
    gevrTexpackPreloadBackground(rle.data(), (unsigned)rle.size());
    assert(s_bootBackgroundKeys == expectedRows); // duplicate boot request
    const uint32_t backgroundKey = expectedRows.front();
    s_bootBackgroundKeys.clear();
    gevrTexpackPreloadBackground(rle.data(), (unsigned)rle.size() - 1);
    assert(s_bootBackgroundKeys.empty());
    rle[10] = 0;
    gevrTexpackPreloadBackground(rle.data(), (unsigned)rle.size());
    assert(s_bootBackgroundKeys.empty());
    rle[10] = 220; rle[1] = 183;
    gevrTexpackPreloadBackground(rle.data(), (unsigned)rle.size());
    assert(s_bootBackgroundKeys.empty());
    reset(); s_started = s_scanned = s_ready = true;
    for (int i = 0; i < 3; ++i) {
        Entry e; e.path = "unused"; e.fmt = i == 0 ? 4 : 2; e.siz = 1;
        s_entries.push_back(std::move(e));
    }
    s_index[glyphKey] = {0};
    s_index[0x1234567800000000ULL | 0xBCE9E819] = {1};
    s_index[0x8765432100000000ULL | 0xBCE9E819] = {2};
    preloadGlyph(glyphKey); preloadGlyph(glyphKey);
    assert(s_bootFonts.size() == 1 && s_bootMenu.empty());
    queueBoot(0xBCE9E819, 2, 1, false);
    assert(s_bootMenu.size() == 2); // palette variants
    uint32_t w, h;
    assert(image(1, &w, &h) == nullptr);
    assert(s_queue.size() == 1 && s_queue.front() == 1 && s_bootMenu.front() == 2);
    image(1, &w, &h); assert(s_queue.size() == 1); // no duplicate foreground jobs
    image(0, &w, &h); assert(s_bootFonts.empty() && s_queue.back() == 0);
    s_entries[0].state = DECODING;
    image(0, &w, &h); assert(s_queue.size() == 2); // never duplicate a running decode
    Entry bg; bg.fmt = 4; bg.siz = 1; s_entries.push_back(std::move(bg));
    s_index[backgroundKey] = {3};
    queueBoot(backgroundKey, 4, 1, false, true);
    assert(s_bootBackground.size() == 1 && s_entries[3].bootBackground);
    image(3, &w, &h); assert(s_bootBackground.empty() && s_queue.back() == 3);

    // Run the real worker: header inspection overlaps a visible draw, then
    // rejects an oversized speculative image. The draw must still complete.
    reset(); s_started = true;
    fixtureGlyphKey = glyphKey;
    fixtureBackgroundKey = backgroundKey;
    s_bootGlyphs.push_back(glyphKey); // font request made before scan publication
    s_bootBackgroundKeys.push_back(backgroundKey);
    std::thread(worker, "fixtures").detach();
    {
        std::unique_lock<std::mutex> lk(gateMu);
        assert(gateCv.wait_for(lk, std::chrono::seconds(5), [] { return inspecting; }));
    }
    assert(s_scanned && s_ready);
    image(2, &w, &h); // requested while speculative header inspection is running
    { std::lock_guard<std::mutex> lk(gateMu); releaseInfo = true; }
    gateCv.notify_one();
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    bool retry = false;
    while (std::chrono::steady_clock::now() < deadline) {
        int ids[16]; int n = takeDone(ids, 16);
        for (int i = 0; i < n; ++i) {
            if (ids[i] == 2) { retry = true; image(2, &w, &h); }
        }
        {
            std::lock_guard<std::mutex> lk(s_mu);
            if (s_entries[2].state == READY && s_entries[3].state == UNLOADED
                    && s_bootMenu.empty()) break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    {
        std::lock_guard<std::mutex> lk(s_mu);
        assert(retry && s_entries[2].state == READY);
        assert(s_bootMenuBytes == BOOT_MENU_BUDGET); // two 4 MiB images, third deferred
        assert(s_entries[3].state == UNLOADED);
        assert(s_entries[4].state == READY && s_bootFontBytes == (size_t)4 << 20);
        assert(s_entries[5].state == UNLOADED); // unrelated title animation never warmed
        assert(s_entries[6].state == READY && s_bootBackgroundBytes == BOOT_BACKGROUND_BUDGET);
    }
    image(3, &w, &h); // cache budget never prevents a later on-demand decode
    while (image(3, &w, &h) == nullptr && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    assert(image(3, &w, &h) != nullptr);
    { std::lock_guard<std::mutex> lk(gateMu);
      assert(loads.front() == 4); // legal font ahead of menu artwork
      assert(loads[1] == 6); // background ahead of remaining menu artwork
      for (int id = 0; id < 5; ++id) assert(std::count(loads.begin(), loads.end(), id) == 1);
      assert(std::count(loads.begin(), loads.end(), 5) == 0);
      assert(std::count(loads.begin(), loads.end(), 6) == 1); }
    // A background-only request must wake an otherwise idle worker. Its
    // allowance is full here, so completion means the normal budget fallback.
    { std::lock_guard<std::mutex> lk(s_mu);
      queueBoot(0xFACEBABE, 4, 1, false, true);
      assert(s_entries[7].state == QUEUED); }
    s_cv.notify_one();
    const auto lateDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (std::chrono::steady_clock::now() < lateDeadline) {
        { std::lock_guard<std::mutex> lk(s_mu);
          if (s_entries[7].state == UNLOADED) break; }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    { std::lock_guard<std::mutex> lk(s_mu); assert(s_entries[7].state == UNLOADED && s_bootBackground.empty()); }
    puts("PASS: glyph and background checksums, RLE validation, boot deduplication, demand priority, in-flight requests, separate preload budgets and demand fallback");
    fflush(stdout);
    std::_Exit(0); // production worker is detached and process-lived
}
"""

with tempfile.TemporaryDirectory(prefix="gevr-preload-") as tmp:
    code = Path(tmp) / "preload.cpp"
    code.write_text(stub + checksum + production + tests)
    exe = Path(tmp) / ("preload.exe" if os.name == "nt" else "preload")
    compiler = r"C:\Strawberry\c\bin\g++.exe" if os.name == "nt" else "c++"
    subprocess.run([compiler, "-std=c++20", "-O2", "-pthread", str(code), "-o", str(exe)], check=True)
    subprocess.run([str(exe)], check=True, timeout=15)
