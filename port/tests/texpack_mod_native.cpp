// A mod's textures as their own layer of the HD-pack index (gevr_texpack.cpp):
// the production scan over a mod's folder and a pack's, then the answers
// gfx_pc.cpp's tp_find asks for. One process per run: start() runs once.
//   texpack_mod <pack dir or -> <mod dir or -> <key hex> <fmt> <siz>...
// prints, for each key, "KEY mod=<name or -> pack=<name or ->", then "HASMOD n".
#include "gevr_texpack.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

int main(int argc, char **argv) {
    if (argc < 3) return 2;
    const char *pack = strcmp(argv[1], "-") ? argv[1] : nullptr;
    const char *mod = strcmp(argv[2], "-") ? argv[2] : nullptr;
    gevrtp::start(pack, nullptr, mod);
    gevrtp::waitIndex(10000);
    for (int i = 3; i + 2 < argc; i += 3) {
        const uint64_t key = strtoull(argv[i], nullptr, 16);
        const uint8_t fmt = (uint8_t)atoi(argv[i + 1]), siz = (uint8_t)atoi(argv[i + 2]);
        const int m = gevrtp::findMod(key, fmt, siz), p = gevrtp::find(key, fmt, siz, false);
        std::printf("KEY %s mod=%s pack=%s\n", argv[i], m >= 0 ? gevrtp::name(m) : "-", p >= 0 ? gevrtp::name(p) : "-");
    }
    std::printf("HASMOD %d\n", gevrtp::hasMod() ? 1 : 0);
    std::fflush(stdout);
    std::_Exit(0);   // the decode thread waits forever, as in the game
}
