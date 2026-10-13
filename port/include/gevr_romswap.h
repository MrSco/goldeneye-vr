#ifndef _GEVR_ROMSWAP_H_
#define _GEVR_ROMSWAP_H_

#include <PR/ultratypes.h>

/*
 * Byte-order passes over data copied out of the cartridge.
 *
 * The decomp is written for the N64's big-endian MIPS, and g_RomFile holds
 * the cartridge bytes in that order. The engine reads structures straight out
 * of those bytes, so on a little-endian host every u16 and u32 field comes
 * back reversed. Each function here walks one structure family, right after
 * it is romCopy'd, and swaps every multi-byte field in place, field by field.
 * A blanket 32-bit swap would not do: the structures mix u8, u16 and u32.
 *
 * Perfect Dark's port does the same job in port/src/preprocess/, which is
 * excluded from this build because it is written against Perfect Dark's own
 * structures. This is GoldenEye's equivalent.
 */

/*
 * The ANIMATION_DATA segment: for every animation, the 0x14-byte header that
 * PTR_ANIM_* points at and the four 6-byte bit descriptors it refers to. The
 * root-motion bitstream between them, and the per-frame data in the separate
 * ANIMATION_ENTRIES segment, are read a byte at a time and are left alone.
 */
void gevrRomSwapAnimationData(u8 *data, u32 size);

/*
 * A hack that rebuilt the segment (TND64 moved every record but the first, by
 * 4 to 0x528 bytes; DAB geconvert.c animRecord()): `hack` is its segment, as it
 * came from the cartridge, and hackOfs[i] where its own tables put the
 * animation GoldenEye has at geOfs[i] (animation_table_ptrs1/2, id for id).
 * The game reads records at GoldenEye's offsets (PTR_ANIM_*, ANIM_DATA_* in
 * its code and tables), so the hack's bytes go after GoldenEye's extent,
 * swapped where its tables say, and each of GoldenEye's offsets gets a copy of
 * the hack's header for the same animation, its descriptors and stream moved
 * with the bytes. Returns the new segment (malloc'd, swapped) and its size, or
 * NULL.
 */
u8 *gevrRomRebuildAnimationData(const u8 *hack, u32 hackSize, const u32 *geOfs, const u32 *hackOfs, u32 count,
		u32 *outSize);

/*
 * A font segment (fontbankgothic, fontzurichbold): builds a host-layout
 * struct font in the given memp bank from the cartridge bytes, with every
 * field swapped and each character's pixel-data offset resolved to a pointer.
 * The cartridge fontchar is 24 bytes and the host one 32, so this cannot be
 * done in place.
 */
struct font;
struct font *gevrRomSwapFont(const u8 *src, u32 srclen, u8 bank);

/*
 * A text bank (the L<stage><language> files): a table of big-endian u32
 * offsets from the start of the bank, one per string slot, followed by the
 * strings. Swaps the table in place; the strings are bytes and stay as they
 * are. The table's length is not stored, so it is taken to end where the
 * first string begins.
 */
void gevrRomSwapLangBank(u8 *data, u32 size, const char *name);

/* Ubrief files contain only u16 string ids and difficulty values. */
void gevrRomSwapBriefing(u8 *data, u32 size);

#endif
