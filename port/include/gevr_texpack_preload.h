#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/* Queue an ordinary I8 font glyph while the ROM's font tables load. The
 * pixels are hashed immediately; no pointer into the stage pool is retained. */
void gevrTexpackPreloadGlyph(const unsigned char *pixels, int width, int height);

/* Read the cartridge font's glyph descriptors during ROM binding, before
 * stage initialization. No ROM data is copied or retained by the loader. */
void gevrTexpackPreloadFont(const unsigned char *data, unsigned int size);

/* Warm the exact scanlines used by file select from the ROM's RLE image. */
void gevrTexpackPreloadBackground(const unsigned char *data, unsigned int size);

#ifdef __cplusplus
}
#endif
