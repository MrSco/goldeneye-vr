/*
 * Byte-order passes over cartridge data. See gevr_romswap.h.
 */

#include <stdlib.h>
#include <string.h>
#include <ultra64.h>
#include <memp.h>
#include <bondtypes.h>
#include <assets/animationtable_data.h>
#include "platform.h"
#include "system.h"
#include "gevr_romswap.h"

void gevrRomSwapBriefing(u8 *data, u32 size)
{
	u32 i;
	for (i = 0; i + sizeof(u16) <= size; i += sizeof(u16)) {
		u16 value;
		memcpy(&value, data + i, sizeof(value));
		value = PD_BE16(value);
		memcpy(data + i, &value, sizeof(value));
	}
}

/* ----------------------------------------------------------------- fonts */

/*
 * A font segment on the cartridge:
 *
 *   s32 kerning[13 * 13];
 *   struct { s32 index, baseline, height, width, kerningindex; u32 pixeldata; } chars[94];
 *   u8  pixels[];
 *
 * pixeldata is a byte offset from the start of the segment, which the game
 * turned into a pointer by adding the base of its RAM copy. That worked
 * because a cartridge fontchar and a RAM one were the same 24 bytes. The host
 * fontchar ends in an 8-byte pointer and is 32 bytes, and the chars array
 * itself moves to an 8-byte boundary, so the characters are rebuilt one field
 * at a time into a host-layout font and the pointers resolved here.
 *
 * A pixeldata of 0 is kept as the segment base, which is what base + 0 gave
 * on the cartridge; nothing draws such a character but the pointer stays
 * valid either way.
 */
#define FONT_NUM_CHARS   94
#define FONT_NUM_KERNING (13 * 13)

struct gevrCartFontChar {
	s32 index;
	s32 baseline;
	s32 height;
	s32 width;
	s32 kerningindex;
	u32 pixeldata;
};

struct font *gevrRomSwapFont(const u8 *src, u32 srclen, u8 bank)
{
	const u32 cartCharsOfs  = FONT_NUM_KERNING * sizeof(s32);
	const u32 cartPixelsOfs = cartCharsOfs + FONT_NUM_CHARS * sizeof(struct gevrCartFontChar);
	const s32 *cartKerning = (const s32 *)src;
	const struct gevrCartFontChar *cartChars = (const struct gevrCartFontChar *)(src + cartCharsOfs);
	struct font *font;
	u8 *pixels;
	u32 pixelsLen;
	s32 i;

	if (srclen < cartPixelsOfs) {
		sysFatalError("gevrRomSwapFont: segment is %u bytes, smaller than its tables (%u)", srclen, cartPixelsOfs);
	}

	pixelsLen = srclen - cartPixelsOfs;
	font = mempAllocBytesInBank(sizeof(struct font) + pixelsLen, bank);
	pixels = (u8 *)font + sizeof(struct font);

	for (i = 0; i < FONT_NUM_KERNING; i++) {
		font->kerning[i] = PD_BE32(cartKerning[i]);
	}

	for (i = 0; i < FONT_NUM_CHARS; i++) {
		const struct gevrCartFontChar *c = &cartChars[i];
		const u32 ofs = PD_BE32(c->pixeldata);

		font->chars[i].index        = PD_BE32(c->index);
		font->chars[i].baseline     = PD_BE32(c->baseline);
		font->chars[i].height       = PD_BE32(c->height);
		font->chars[i].width        = PD_BE32(c->width);
		font->chars[i].kerningindex = PD_BE32(c->kerningindex);

		if (ofs == 0) {
			font->chars[i].pixeldata = (u8 *)font;
		} else if (ofs < cartPixelsOfs || ofs >= srclen) {
			sysLogPrintf(LOG_ERROR, "font: char %d pixel offset 0x%X is outside the pixel data (0x%X..0x%X)",
					i, ofs, cartPixelsOfs, srclen);
			font->chars[i].pixeldata = (u8 *)font;
		} else {
			font->chars[i].pixeldata = pixels + (ofs - cartPixelsOfs);
		}
	}

	memcpy(pixels, src + cartPixelsOfs, pixelsLen);

	sysLogPrintf(LOG_NOTE, "font: %u-byte cartridge font -> %u-byte host font (%u bytes of pixels), first char %d x %d",
			srclen, (u32)(sizeof(struct font) + pixelsLen), pixelsLen, font->chars[0].width, font->chars[0].height);

	return font;
}

/* ------------------------------------------------------------ text banks */

void gevrRomSwapLangBank(u8 *data, u32 size, const char *name)
{
	u32 *table = (u32 *)data;
	const u32 words = size / 4;
	u32 first = size;   /* where the strings start: the smallest non-zero offset */
	u32 count, i;

	for (i = 0; i < words; i++) {
		const u32 ofs = PD_BE32(table[i]);

		if (ofs != 0 && ofs < first) {
			first = ofs;
		}

		if (i * 4 >= first) {
			break; /* into the strings */
		}
	}

	count = first / 4;
	if (count > words) {
		count = words;
	}

	for (i = 0; i < count; i++) {
		table[i] = PD_BE32(table[i]);
	}

	sysLogPrintf(LOG_NOTE, "text: %s, %u bytes, %u slots", name ? name : "?", size, count);
}

/* ------------------------------------------------------------ animations */

/*
 * Every animation header in the segment, by cartridge offset. Generated from
 * the PTR_ANIM_* defines; the null placeholders (value 1) are not in it.
 */
static const u32 gevrAnimHeaderOffsets[] = {
#include "gevr_anim_offsets.inc"
};

/*
 * How one animation is laid out in the segment, verified against the
 * cartridge for five animations:
 *
 *   [ 4 x 6-byte ModelAnimBitField ][ root-motion bitstream ][ 0x14-byte header ]
 *     ^ header->bitDescriptors        ^ header->bitStream     ^ PTR_ANIM_*
 *
 * Both fields are offsets from the segment base, and bitDescriptors + 0x18 is
 * always bitStream. Only the first 0x14 bytes of ModelAnimation exist on the
 * cartridge; the fields after bitStream are not stored.
 */
#define ANIM_HEADER_SIZE      0x14
#define ANIM_NUM_DESCRIPTORS  4
#define ANIM_DESCRIPTORS_SIZE (ANIM_NUM_DESCRIPTORS * sizeof(ModelAnimBitField))

static void gevrSwapAnimHeader(ModelAnimation *h)
{
	h->address        = PD_BE32(h->address);
	h->unk04          = PD_BE16(h->unk04);
	h->bitDescriptors = PD_BE32(h->bitDescriptors);
	h->unk0C          = PD_BE16(h->unk0C);
	h->unk0E          = PD_BE16(h->unk0E);
	h->bitStream      = PD_BE32(h->bitStream);
}

static void gevrSwapAnimDescriptors(ModelAnimBitField *d)
{
	s32 i;

	for (i = 0; i < ANIM_NUM_DESCRIPTORS; i++) {
		d[i].bitOffset   = PD_BE16(d[i].bitOffset);
		d[i].valueOffset = PD_BE16(d[i].valueOffset);
	}
}

void gevrRomSwapAnimationData(u8 *data, u32 size)
{
	/*
	 * Several tables can name the same animation. Swapping a block twice
	 * would put its bytes back, so each block is marked off as it is done.
	 * Headers and descriptor blocks are 4-byte aligned, so one flag per
	 * 4 bytes covers them.
	 */
	u8 *done = calloc(size / 4 + 1, 1);
	u32 headers = 0, descriptors = 0, bad = 0;
	u32 i;

	if (!done) {
		sysFatalError("gevrRomSwapAnimationData: out of memory");
	}

	for (i = 0; i < ARRAYCOUNT(gevrAnimHeaderOffsets); i++) {
		const u32 ofs = gevrAnimHeaderOffsets[i];
		ModelAnimation *h;

		if (ofs + ANIM_HEADER_SIZE > size) {
			sysLogPrintf(LOG_ERROR, "anim: header %u at 0x%X runs past the segment (%u bytes)", i, ofs, size);
			bad++;
			continue;
		}

		h = (ModelAnimation *)(data + ofs);

		if (!done[ofs / 4]) {
			gevrSwapAnimHeader(h);
			done[ofs / 4] = 1;
			headers++;
		}

		if (h->bitDescriptors + ANIM_DESCRIPTORS_SIZE > size
				|| h->bitDescriptors + ANIM_DESCRIPTORS_SIZE != h->bitStream
				|| h->bitStream > ofs) {
			/*
			 * The invariant that ties the three parts together does not hold,
			 * so either the swap or the layout above is wrong for this one.
			 * Say so rather than swap descriptors at a bogus offset.
			 */
			sysLogPrintf(LOG_ERROR, "anim: header 0x%X decodes to descriptors 0x%X stream 0x%X frames %u - not the expected layout",
					ofs, h->bitDescriptors, h->bitStream, h->unk04);
			bad++;
			continue;
		}

		if (!done[h->bitDescriptors / 4]) {
			gevrSwapAnimDescriptors((ModelAnimBitField *)(data + h->bitDescriptors));
			done[h->bitDescriptors / 4] = 1;
			descriptors++;
		}
	}

	free(done);

	sysLogPrintf(bad ? LOG_ERROR : LOG_NOTE, "anim: swapped %u headers and %u descriptor blocks in %u bytes, %u bad",
			headers, descriptors, size, bad);
}
