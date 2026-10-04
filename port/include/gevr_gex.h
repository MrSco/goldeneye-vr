#ifndef GEVR_GEX_H
#define GEVR_GEX_H

/*
 * GoldenEye X (a Perfect Dark NTSC 1.1 ROM hack) read from the player's own
 * patched ROM, data/gex.z64, for its first-person guns (docs/gex-weapons.md).
 * Nothing from it ships with the port.
 */
#include <PR/ultratypes.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Loads and checks the ROM on first use; 0 when it is missing or not GE-X 6a. */
s32 gevrGexOpen(void);

/* A model or other file from PD's file table by name, inflated if it is
 * 1173-compressed. The caller frees it. NULL if absent. */
u8 *gevrGexFileLoad(const char *name, u32 *outSize);

/* A texture's compressed bytes as PD stores them (first byte: bit 7 LOD data,
 * bit 6 zlib, low 6 bits the LOD count). Points into the resident ROM. */
const u8 *gevrGexTextureData(s32 texnum, u32 *outSize);

/* The resident ROM, for the animation reader (gevr_pdanim.c); NULL if not open. */
const u8 *gevrGexRom(u32 *size);

/* Inflates a raw deflate stream (PD's 1173 files, zlib-coded textures). */
s32 gevrGexInflate(const u8 *src, u32 srclen, u8 *dst, u32 dstlen);

/* Inflates one of Perfect Dark's rarezip blobs (0x11 0x73, a 24-bit length,
 * raw deflate) as its texture images carry them; returns where the bytes
 * after the stream start, or NULL. GoldenEye's own blobs have no length. */
const u8 *gevrGexInflateRzip(const u8 *src, u8 *dst, u32 dstlen);

#ifdef __cplusplus
}
#endif

#endif
