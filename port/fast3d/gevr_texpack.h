#pragma once

/*
 * Issue #25: GLideN64 ("Rice") hi-res texture packs. The launcher's Mods page
 * installs a pack into files/texture-packs/<name> (ModManager.java); this
 * indexes its PNGs by the checksum in their names and decodes them on demand
 * on a thread of its own. gfx_pc.cpp computes the checksum of each texture it
 * imports and asks for the match.
 */

#include <cstddef>
#include <cstdint>

namespace gevrtp {

// Once: scan the pack at dir in the background. Nothing matches until the scan is done.
void start(const char *dir);
// True once, the first time it's asked after the scan finished with textures in it.
bool takeIndexReady();
// The entry for a checksum key (see gfx_pc.cpp gevr_texpack_lookup) and the draw
// tile's format and size, or -1.
int find(uint64_t key, uint8_t fmt, uint8_t siz);
// The decoded image (RGBA8, top row first) when it's ready; otherwise NULL, and
// the decode is queued.
const uint8_t *image(int id, uint32_t *w, uint32_t *h);
// Entries whose decode finished (or failed) since the last call.
int takeDone(int *ids, int max);
// Free the least recently used images while more than budget bytes are held.
void trim(size_t budget);

} // namespace gevrtp
