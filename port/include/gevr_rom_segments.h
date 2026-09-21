#ifndef _GEVR_ROM_SEGMENTS_H_
#define _GEVR_ROM_SEGMENTS_H_

/**
 * The cartridge segments the engine copies out by linker symbol.
 *
 * ge007.ld's BEGIN_SEG macro emitted _<name>SegmentStart / SegmentEnd /
 * SegmentRomStart for each segment, and the game takes their *addresses*:
 *
 *     size = (u32)&_fontbankgothicSegmentEnd - (u32)&_fontbankgothicSegmentStart;
 *     romCopy(dst, &_fontbankgothicSegmentRomStart, size);
 *
 * There is no cartridge link here, so those symbols have no addresses to take.
 * They become ordinary pointer variables instead, filled from the player's ROM
 * at startup, and the call sites drop the '&'. Start and End are set to the
 * segment's first and last byte so that End - Start is still its length, and
 * RomStart points at the same bytes, because on this port romCopy() is a memcpy
 * out of the resident ROM rather than a DMA from a cartridge.
 *
 * Coordinates come from port/src/gevr_rom_manifest.c; see
 * tools/gevr_gen_rom_manifest.py for where they are derived.
 */

enum {
	GEVR_SEG_ANIMATION_DATA,
	GEVR_SEG_ANIMATION_ENTRIES,
	GEVR_SEG_GLOBALIMAGETABLE,
	GEVR_SEG_RAREWARELOGO,
	GEVR_SEG_FONTDL,
	GEVR_SEG_JFONTCHARDATA,
	GEVR_SEG_EFONTCHARDATA,
	GEVR_SEG_FONTBANKGOTHIC,
	GEVR_SEG_FONTZURICHBOLD,
	GEVR_SEG_IMAGES,
	GEVR_SEG_GUNBARREL,
	GEVR_SEG_COUNT
};

extern void *unknown2;
extern void *unknown2_end;

extern unsigned char *_animation_dataSegmentRomStart;
extern unsigned char *_animation_dataSegmentStart;
extern unsigned char *_animation_dataSegmentEnd;

extern unsigned char *_animation_entriesSegmentRomStart;

extern unsigned char *_GlobalimagetableSegmentRomStart;
extern unsigned char *_GlobalimagetableSegmentStart;
extern unsigned char *_GlobalimagetableSegmentEnd;

extern unsigned char *_rarewarelogoSegmentRomStart;
extern unsigned char *_rarewarelogoSegmentStart;
extern unsigned char *_rarewarelogoSegmentEnd;

extern unsigned char *_fontdlSegmentRomStart;
extern unsigned char *_fontdlSegmentRomEnd;

extern unsigned char *_jfontchardataSegmentRomStart;
extern unsigned char *_efontchardataSegmentRomStart;

extern unsigned char *_fontbankgothicSegmentRomStart;
extern unsigned char *_fontbankgothicSegmentStart;
extern unsigned char *_fontbankgothicSegmentEnd;

extern unsigned char *_fontzurichboldSegmentRomStart;
extern unsigned char *_fontzurichboldSegmentStart;
extern unsigned char *_fontzurichboldSegmentEnd;

extern unsigned char *_imagesSegmentRomStart;

/*
 * Not a cartridge segment: on hardware this marked the end of BSS, and boss.c
 * used it as the base of the game's heap, running up to the TLB block. The port
 * allocates that region instead and points this at its base - see
 * gevrRomBindSegments().
 */
extern unsigned char *_bssSegmentEnd;

void gevrRomBindSegments(void);

/* Top of that region, where the TLB scratch block sat on hardware. */
unsigned char *gevrHeapTlbBlock(void);

/* Musicfiles banks from the ROM (ctl host-endian, tbl mapped in place). */
void gevrRomBindMusic(void);

#endif
