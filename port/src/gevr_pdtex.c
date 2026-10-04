/**
 * Perfect Dark's texture codec, ported from pdvr src/game/texdecompress.c and
 * src/game/texreset.c (the bit reader) for GoldenEye X's textures
 * (docs/gex-weapons.md). Only the base image is decoded: PD's level-of-detail
 * images, its texture pool and cache, and the N64's TMEM swizzle (stubbed on
 * PC in pdvr too) are left out.
 *
 * Every texel is written in the N64's big-endian layout, which fast3d's
 * importers read. pdvr wrote some formats (IA16, RGBA32, the uncompressed
 * and lookup paths) in host order, which is how GE-X's IA16 magazine texture
 * came out white in Dab's Mod.
 *
 * A texture's first byte: bit 7 LOD data follows, bit 6 zlib, low 6 bits the
 * LOD count. Zlib textures are paletted (format, colour count, 16-bit colours,
 * then per image a width, a height and a rarezip blob of indices); the others
 * carry a 4-bit format, the size and a 4-bit compression method per image.
 */

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <PR/ultratypes.h>
#include <PR/gbi.h>
#include "gevr_gex.h"
#include "gevr_pdtex.h"

#define BE16(x) ((u16)__builtin_bswap16((u16)(x)))
#define BE32(x) ((u32)__builtin_bswap32((u32)(x)))
#define ALIGN(v, a) (((v) + (a) - 1) & ~((a) - 1))

#define TEXCOMPMETHOD_UNCOMPRESSED0      0
#define TEXCOMPMETHOD_UNCOMPRESSED1      1
#define TEXCOMPMETHOD_HUFFMAN            2
#define TEXCOMPMETHOD_HUFFMANPERHCHANNEL 3
#define TEXCOMPMETHOD_RLE                4
#define TEXCOMPMETHOD_LOOKUP             5
#define TEXCOMPMETHOD_HUFFMANLOOKUP      6
#define TEXCOMPMETHOD_RLELOOKUP          7
#define TEXCOMPMETHOD_HUFFMANBLUR        8
#define TEXCOMPMETHOD_RLEBLUR            9

#define TEXFORMAT_RGBA32     0x00
#define TEXFORMAT_RGBA16     0x01
#define TEXFORMAT_RGB24      0x02
#define TEXFORMAT_RGB15      0x03
#define TEXFORMAT_IA16       0x04
#define TEXFORMAT_IA8        0x05
#define TEXFORMAT_IA4        0x06
#define TEXFORMAT_I8         0x07
#define TEXFORMAT_I4         0x08
#define TEXFORMAT_RGBA16_CI8 0x09
#define TEXFORMAT_RGBA16_CI4 0x0a
#define TEXFORMAT_IA16_CI8   0x0b
#define TEXFORMAT_IA16_CI4   0x0c

/* the number of channels, excluding 1-bit alpha channels */
static const s32 g_TexFormatNumChannels[] = { 4, 3, 3, 3, 2, 2, 1, 1, 1, 1, 1, 1, 1 };
/* whether each format supports a 1-bit alpha channel */
static const s32 g_TexFormatHas1BitAlpha[] = { 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0 };
/* non-paletted: values per channel; paletted: values per index */
static const s32 g_TexFormatChannelSizes[] = { 256, 32, 256, 32, 256, 16, 8, 256, 16, 256, 16, 256, 16 };
static const s32 g_TexFormatBitsPerPixel[] = { 32, 16, 24, 15, 16, 8, 4, 8, 4, 16, 16, 16, 16 };
static const s32 g_TexFormatGbiMappings[] = {
	G_IM_FMT_RGBA, G_IM_FMT_RGBA, G_IM_FMT_RGBA, G_IM_FMT_RGBA, G_IM_FMT_IA, G_IM_FMT_IA, G_IM_FMT_IA,
	G_IM_FMT_I, G_IM_FMT_I, G_IM_FMT_CI, G_IM_FMT_CI, G_IM_FMT_CI, G_IM_FMT_CI,
};
static const s32 g_TexFormatDepths[] = {
	G_IM_SIZ_32b, G_IM_SIZ_16b, G_IM_SIZ_32b, G_IM_SIZ_16b, G_IM_SIZ_16b, G_IM_SIZ_8b, G_IM_SIZ_4b,
	G_IM_SIZ_8b, G_IM_SIZ_4b, G_IM_SIZ_8b, G_IM_SIZ_4b, G_IM_SIZ_8b, G_IM_SIZ_4b,
};
static const s32 g_TexFormatLutModes[] = {
	G_TT_NONE, G_TT_NONE, G_TT_NONE, G_TT_NONE, G_TT_NONE, G_TT_NONE, G_TT_NONE,
	G_TT_NONE, G_TT_NONE, G_TT_RGBA16, G_TT_RGBA16, G_TT_IA16, G_TT_IA16,
};

static const u8 *g_TexBitstring;
static u32 g_TexAccumValue;
static s32 g_TexAccumNumBits;

static void texSetBitstring(const u8 *bitstring)
{
	g_TexBitstring = bitstring;
	g_TexAccumValue = 0;
	g_TexAccumNumBits = 0;
}

static s32 texReadBits(s32 wantnumbits)
{
	while (g_TexAccumNumBits < wantnumbits) {
		g_TexAccumValue = g_TexAccumValue << 8 | *g_TexBitstring;
		g_TexBitstring++;
		g_TexAccumNumBits += 8;
	}

	g_TexAccumNumBits -= wantnumbits;

	return (g_TexAccumValue >> g_TexAccumNumBits) & ((1 << wantnumbits) - 1);
}

/**
 * Copy a list of palette indices to the dst buffer, but ensure each row is
 * aligned to an 8 byte boundary.
 *
 * Return the number of output bytes.
 */
static s32 texAlignIndices(u8 *src, s32 width, s32 height, s32 format, u8 *dst)
{
	u8 *outptr = dst;
	s32 x;
	s32 y;
	s32 indicesperbyte;

	if (format == TEXFORMAT_RGBA16_CI8 || format == TEXFORMAT_IA16_CI8) {
		indicesperbyte = 1;
	} else if (format == TEXFORMAT_RGBA16_CI4 || format == TEXFORMAT_IA16_CI4) {
		indicesperbyte = 2;
	}

	for (y = 0; y < height; y++) {
		for (x = 0; x < width; x += indicesperbyte) {
			*outptr++ = *src++;
		}
	}

	return ALIGN(width, 2) * height / indicesperbyte;
}

/**
 * Inflate Huffman data.
 *
 * This function operates on single channels rather than whole colours.
 * For example, for an RGBA32 image this function may be called once for each
 * channel with chansize = 256. This means the resulting data is in the format
 * RRR...GGG...BBB...AAA..., and the caller must convert it into a proper pixel
 * format.
 *
 * A typical Huffman implementation stores a tree, where each node contains
 * the lookup value and its frequency (number of uses). However, Rare's
 * implementation only stores a list of frequencies. It uses the chansize
 * to know how many values there are.
 */
static void texInflateHuffman(u8 *dst, s32 numiterations, s32 chansize)
{
	u16 frequencies[2048];
	s16 nodes[2048][2];
	s32 i;
	s32 rootindex;
	s32 sum;
	u16 minfreq1;
	u16 minfreq2;
	s32 minindex1;
	s32 minindex2;
	bool done = false;

	// Read the frequencies list
	for (i = 0; i < chansize; i++) {
		frequencies[i] = texReadBits(8);
	}

	// Initialise the tree
	for (i = 0; i < 2048; i++) {
		nodes[i][0] = -1;
		nodes[i][1] = -1;
	}

	// Find the two smallest frequencies
	minfreq1 = 9999;
	minfreq2 = 9999;

	for (i = 0; i < chansize; i++) {
		if (frequencies[i] < minfreq1) {
			if (minfreq2 < minfreq1) {
				minfreq1 = frequencies[i];
				minindex1 = i;
			} else {
				minfreq2 = frequencies[i];
				minindex2 = i;
			}
		} else if (frequencies[i] < minfreq2) {
			minfreq2 = frequencies[i];
			minindex2 = i;
		}
	}

	// Build the tree.
	// For each node in tree, a branch value < 10000 means this branch
	// leads to another node, and the value is the target node's index.
	// A branch value >= 10000 means the branch is a leaf node,
	// and the value is the channel value + 10000.
	while (!done) {
		sum = frequencies[minindex1] + frequencies[minindex2];

		if (sum == 0) {
			sum = 1;
		}

		frequencies[minindex1] = 9999;
		frequencies[minindex2] = 9999;

		if (nodes[minindex1][0] < 0 && nodes[minindex1][1] < 0) {
			nodes[minindex1][0] = minindex1 + 10000;
			rootindex = minindex1;
			frequencies[minindex1] = sum;

			if (nodes[minindex2][0] < 0 && nodes[minindex2][1] < 0) {
				nodes[minindex1][1] = minindex2 + 10000;
			} else {
				nodes[minindex1][1] = minindex2;
			}
		} else if (nodes[minindex2][0] < 0 && nodes[minindex2][1] < 0) {
			nodes[minindex2][0] = minindex2 + 10000;
			rootindex = minindex2;
			frequencies[minindex2] = sum;

			if (nodes[minindex1][0] < 0 && nodes[minindex1][1] < 0) {
				nodes[minindex2][1] = minindex1 + 10000;
			} else {
				nodes[minindex2][1] = minindex1;
			}
		} else {
			for (rootindex = 0; nodes[rootindex][0] >= 0 || nodes[rootindex][1] >= 0 || frequencies[rootindex] < 9999; rootindex++);

			frequencies[rootindex] = sum;
			nodes[rootindex][0] = minindex1;
			nodes[rootindex][1] = minindex2;
		}

		// Find the two smallest frequencies again for the next iteration
		minfreq1 = 9999;
		minfreq2 = 9999;

		for (i = 0; i < chansize; i++) {
			if (frequencies[i] < minfreq1) {
				if (minfreq1 > minfreq2) {
					minfreq1 = frequencies[i];
					minindex1 = i;
				} else {
					minfreq2 = frequencies[i];
					minindex2 = i;
				}
			} else if (frequencies[i] < minfreq2) {
				minfreq2 = frequencies[i];
				minindex2 = i;
			}
		}

		if (minfreq1 == 9999 || minfreq2 == 9999) {
			done = true;
		}
	}

	// Read bits off the bitstring, traverse the tree
	// and write the channel values to dst
	for (i = 0; i < numiterations; i++) {
		s32 indexorvalue = rootindex;

		while (indexorvalue < 10000) {
			indexorvalue = nodes[indexorvalue][texReadBits(1)];
		}

		if (chansize <= 256) {
			dst[i] = indexorvalue - 10000;
		} else {
			u16 *tmp = (u16 *)dst;
			tmp[i] = indexorvalue - 10000;
		}
	}
}

/**
 * Inflate runlength-encoded data.
 *
 * This data consists of a 10 bit header followed by a list of directives,
 * where each directive can either be a literal block or a repeat (run) of
 * blocks within a sliding window.
 *
 * The header format is:
 *
 * 3 bits btfieldsize: The size in bits of the backtrack distance fields
 * 3 bits rlfieldsize: The size in bits of the runlen fields
 * 4 bits blocksize: The size in bits of each block of data
 *
 * In the data, the first bit is 0 if it's a literal block or 1 if it's a run.
 *
 * For literal blocks, the next <blocksize> bits should be read and appended to
 * the output stream.
 *
 * For runs, the next <btfieldsize> bits are the backtrack length (in blocks)
 * plus one, and the next <rlfieldsize> bits are the run length (in blocks)
 * minus a calculated fudge value.
 *
 * The fudge value is calculated based on the field sizes. For small runs it is
 * more space efficient to use multiple literal directives rather than a run
 * directive. Because of this, smaller runs are not used and the run lengths
 * in the data can be offset accordingly - this offset is the fudge value.
 *
 * Every run must be followed by a literal block without the 1-bit marker.
 * The algorithm does not support back to back runs.
 */
static void texInflateRle(u8 *dst, s32 blockstotal)
{
	s32 btfieldsize = texReadBits(3);
	s32 rlfieldsize = texReadBits(3);
	s32 blocksize = texReadBits(4);
	s32 cost;
	s32 fudge;
	s32 blocksdone;
	s32 i;

	// Calculate the fudge value
	cost = btfieldsize + rlfieldsize + blocksize + 1;
	fudge = 0;

	while (cost > 0) {
		cost = cost - blocksize - 1;
		fudge++;
	}

	blocksdone = 0;

	while (blocksdone < blockstotal) {
		if (texReadBits(1) == 0) {
			// Found a literal directive
			if (blocksize <= 8) {
				dst[blocksdone] = texReadBits(blocksize);
				blocksdone++;
			} else {
				u16 *tmp = (u16 *)dst;
				tmp[blocksdone] = texReadBits(blocksize);
				blocksdone++;
			}
		} else {
			// Found a run directive
			s32 startblockindex = blocksdone - texReadBits(btfieldsize) - 1;
			s32 runnumblocks = texReadBits(rlfieldsize) + fudge;

			if (blocksize <= 8) {
				for (i = startblockindex; i < startblockindex + runnumblocks; i++) {
					dst[blocksdone] = dst[i];
					blocksdone++;
				}

				// The next instruction must be a literal
				dst[blocksdone] = texReadBits(blocksize);
				blocksdone++;
			} else {
				u16 *tmp = (u16 *)dst;

				for (i = startblockindex; i < startblockindex + runnumblocks; i++) {
					tmp[blocksdone] = tmp[i];
					blocksdone++;
				}

				// The next instruction must be a literal
				tmp[blocksdone] = texReadBits(blocksize);
				blocksdone++;
			}
		}
	}
}

/**
 * Populate a lookup table by reading it out of the bit string.
 *
 * The first 11 bits denote the number of colours in the lookup table.
 * The data following this is a list of colours, where each colour is sized
 * according to the texture's format.
 *
 * This function does NOT work with pixel formats of 8 bits or less.
 */
static s32 texBuildLookup(u8 *lookup, s32 bitsperpixel)
{
	s32 numcolours = texReadBits(11);
	s32 i;

	if (bitsperpixel <= 16) {
		u16 *dst = (u16 *)lookup;

		for (i = 0; i < numcolours; i++) {
			dst[i] = texReadBits(bitsperpixel);
		}
	} else if (bitsperpixel <= 24) {
		u32 *dst = (u32 *)lookup;

		for (i = 0; i < numcolours; i++) {
			dst[i] = texReadBits(bitsperpixel);
		}
	} else {
		u32 *dst = (u32 *)lookup;

		for (i = 0; i < numcolours; i++) {
			dst[i] = texReadBits(24) << 8 | texReadBits(bitsperpixel - 24);
		}
	}

	return numcolours;
}

static s32 texGetBitSize(s32 decimal)
{
	s32 count = 0;

	decimal--;

	while (decimal > 0) {
		decimal >>= 1;
		count++;
	}

	return count;
}

static void texReadAlphaBits(u8 *dst, s32 count)
{
	s32 i;

	for (i = 0; i < count; i++) {
		dst[i] = texReadBits(1);
	}
}

/**
 * Read pixel data from the bitstream and write to dst,
 * ensuring each row is aligned according to the pixel format.
 *
 * Return the number of output bytes.
 */
static s32 texReadUncompressed(u8 *dst, s32 width, s32 height, s32 format)
{
	u32 *dst32 = (u32 *)(((uintptr_t)dst + 0xf) & ~0xf);
	u16 *dst16 = (u16 *)(((uintptr_t)dst + 7) & ~7);
	u8 *dst8 = (u8 *)(((uintptr_t)dst + 7) & ~7);
	s32 x;
	s32 y;

	switch (format) {
	case TEXFORMAT_RGBA32:
		for (y = 0; y < height; y++) {
			for (x = 0; x < width; x++) {
				u32 hi = texReadBits(16);

				dst32[x] = BE32(hi << 16 | texReadBits(16));
			}

			dst32 += (width + 3) & 0xffc;
		}

		return ((width + 3) & 0xffc) * height * 4;
	case TEXFORMAT_RGB24:
		for (y = 0; y < height; y++) {
			for (x = 0; x < width; x++) {
				dst32[x] = BE32(texReadBits(24) << 8 | 0xff);
			}

			dst32 += (width + 3) & 0xffc;
		}

		return ((width + 3) & 0xffc) * height * 4;
	case TEXFORMAT_RGBA16:
	case TEXFORMAT_IA16:
		for (y = 0; y < height; y++) {
			for (x = 0; x < width; x++) {
				dst16[x] = BE16(texReadBits(16));
			}

			dst16 += (width + 3) & 0xffc;
		}

		return ((width + 3) & 0xffc) * height * 2;
	case TEXFORMAT_RGB15:
		for (y = 0; y < height; y++) {
			for (x = 0; x < width; x++) {
				dst16[x] = BE16(texReadBits(15) << 1 | 1);
			}

			dst16 += (width + 3) & 0xffc;
		}

		return ((width + 3) & 0xffc) * height * 2;
	case TEXFORMAT_IA8:
	case TEXFORMAT_I8:
		for (y = 0; y < height; y++) {
			for (x = 0; x < width; x++) {
				dst8[x] = texReadBits(8);
			}

			dst8 += (width + 7) & 0xff8;
		}

		return ((width + 7) & 0xff8) * height;
	case TEXFORMAT_IA4:
	case TEXFORMAT_I4:
		for (y = 0; y < height; y++) {
			for (x = 0; x < width; x += 2) {
				dst8[x >> 1] = texReadBits(8);
			}

			dst8 += ((width + 15) & 0xff0) >> 1;
		}

		return (((width + 15) & 0xff0) >> 1) * height;
	}

	return 0;
}

/**
 * Read grouped channel values and convert it to a proper pixel format.
 *
 * For example, for RGBA32 images the input is in the format
 * RRR...GGG...BBB...AAA... and is converted to RGBARGBARGBA...
 *
 * The existence and size of the channels depends on the pixel format.
 */
static s32 texChannelsToPixels(u8 *src, s32 width, s32 height, u8 *dst, s32 format)
{
	u32 *dst32 = (u32 *)dst;
	u16 *dst16 = (u16 *)dst;
	u8 *dst8 = (u8 *)dst;
	s32 x;
	s32 y;
	s32 pos = 0;
	s32 mult = width * height;

	switch (format) {
	case TEXFORMAT_RGBA32:
		for (y = 0; y < height; y++) {
			for (x = 0; x < width; x++) {
				dst32[x] = BE32(src[pos] << 24 | src[pos + mult] << 16 | src[pos + mult * 2] << 8 | src[pos + mult * 3]);
				pos++;
			}

			dst32 += width;
		}

		return width * height * 4;
	case TEXFORMAT_RGB24:
		for (y = 0; y < height; y++) {
			for (x = 0; x < width; x++) {
				dst32[x] = BE32(src[pos] << 24 | src[pos + mult] << 16 | src[pos + mult * 2] << 8 | 0xff);
				pos++;
			}

			dst32 += width;
		}

		return width * height * 4;
	case TEXFORMAT_RGBA16:
		for (y = 0; y < height; y++) {
			for (x = 0; x < width; x++) {
				dst16[x] = BE16(src[pos] << 11 | src[pos + mult] << 6 | src[pos + mult * 2] << 1 | src[pos + mult * 3]);
				pos++;
			}

			dst16 += width;
		}

		return width * height * 2;
	case TEXFORMAT_IA16:
		for (y = 0; y < height; y++) {
			for (x = 0; x < width; x++) {
				dst16[x] = BE16(src[pos] << 8 | src[pos + mult]);
				pos++;
			}

			dst16 += width;
		}

		return width * height * 2;
	case TEXFORMAT_RGB15:
		for (y = 0; y < height; y++) {
			for (x = 0; x < width; x++) {
				dst16[x] = BE16(src[pos] << 11 | src[pos + mult] << 6 | src[pos + mult * 2] << 1 | 1);
				pos++;
			}

			dst16 += width;
		}

		return width * height * 2;
	case TEXFORMAT_IA8:
		for (y = 0; y < height; y++) {
			if ((width + 7) & 0xff8);

			for (x = 0; x < width; x++) {
				dst8[x] = src[pos] << 4 | src[pos + mult];
				pos++;
			}

			dst8 += width;
		}

		return width * height;
	case TEXFORMAT_I8:
		for (y = 0; y < height; y++) {
			for (x = 0; x < width; x++) {
				dst8[x] = src[pos];
				pos++;
			}

			dst8 += width;
		}

		return width * height;
	case TEXFORMAT_IA4:
		for (y = 0; y < height; y++) {
			if ((width + 15) & 0xff0);

			for (x = 0; x < width; x += 2) {
				dst8[x >> 1] = src[pos] << 5 | src[pos + mult * 3] << 4 | src[pos + 1] << 1 | src[pos + mult * 3 + 1];
				pos += 2;
			}

			if (width & 1) {
				pos--;
			}

			dst8 += width;
		}

		return width * height / 2;
	case TEXFORMAT_I4:
		for (y = 0; y < height; y++) {
			for (x = 0; x < width; x += 2) {
				dst8[x >> 1] = src[pos] << 4 | src[pos + 1];
				pos += 2;
			}

			if (width & 1) {
				pos--;
			}

			dst8 += width / 2;
		}

		return width * height / 2;
	}

	return 0;
}

/**
 * Inflate a texture using the provided lookup table.
 *
 * The lookup table is a bitstring of colours in the pixel format described by
 * the format argument. The number of colours in the lookup table is given by
 * the numcolours argument.
 *
 * The data in the global source bitstring is expected to be a tightly packed
 * list of indices into the lookup table. The number of bits for each index
 * is calculated based on the number of colours in the lookup table. For
 * example, if the lookup table contains 8 colours then the indices will be 0-7,
 * which requires 3 bits per index.
 *
 * Return the number of bytes written to dst.
 */
static s32 texInflateLookup(s32 width, s32 height, u8 *dst, u8 *lookup, s32 numcolours, s32 format)
{
	u32 *lookup32 = (u32 *)lookup;
	u16 *lookup16 = (u16 *)lookup;
	u32 *dst32 = (u32 *)dst;
	u16 *dst16 = (u16 *)dst;
	u8 *dst8 = (u8 *)dst;
	s32 x;
	s32 y;
	s32 bitspercolour = texGetBitSize(numcolours);

	switch (format) {
	case TEXFORMAT_RGBA32:
		for (y = 0; y < height; y++) {
			for (x = 0; x < width; x++) {
				dst32[x] = BE32(lookup32[texReadBits(bitspercolour)]);
			}

			dst32 += width;
		}

		return width * height * 4;
	case TEXFORMAT_RGB24:
		for (y = 0; y < height; y++) {
			for (x = 0; x < width; x++) {
				dst32[x] = BE32(lookup32[texReadBits(bitspercolour)] << 8);
			}

			dst32 += width;
		}

		return width * height * 4;
	case TEXFORMAT_RGBA16:
	case TEXFORMAT_IA16:
		for (y = 0; y < height; y++) {
			for (x = 0; x < width; x++) {
				dst16[x] = BE16(lookup16[texReadBits(bitspercolour)]);
			}

			dst16 += width;
		}

		return width * height * 2;
	case TEXFORMAT_RGB15:
		for (y = 0; y < height; y++) {
			for (x = 0; x < width; x++) {
				dst16[x] = BE16(lookup16[texReadBits(bitspercolour)] << 1 | 1);
			}

			dst16 += width;
		}

		return width * height * 2;
	case TEXFORMAT_IA8:
	case TEXFORMAT_I8:
		for (y = 0; y < height; y++) {
			for (x = 0; x < width; x++) {
				dst8[x] = lookup16[texReadBits(bitspercolour)];
			}

			dst8 += width;
		}

		return width * height;
	case TEXFORMAT_IA4:
	case TEXFORMAT_I4:
		for (y = 0; y < height; y++) {
			for (x = 0; x < width; x += 2) {
				dst8[x >> 1] = lookup16[texReadBits(bitspercolour)] << 4;

				if (x + 1 < width) {
					dst8[x >> 1] |= lookup[(texReadBits(bitspercolour) * 2) + 1];
				}
			}

			dst8 += width / 2;
		}

		return width * height / 2;
	}

	return 0;
}

/**
 * Like texInflateLookup, but the indices are provided in the src argument
 * as u8s or u16s rather than read from the global bitstring as tightly packed
 * bits.
 *
 * Whether u8s or u16s are expected depends on whether the number of colours
 * in the lookup table. If there are more than 256 colours then it must use
 * u16s, otherwise it expects u8s.
 */
static s32 texInflateLookupFromBuffer(u8 *src, s32 width, s32 height, u8 *dst, u8 *lookup, s32 numcolours, s32 format)
{
	s32 x;
	s32 y;
	u32 *lookup32 = (u32 *)lookup;
	u16 *lookup16 = (u16 *)lookup;
	u8 *src8;
	u16 *src16;
	u32 *dst32 = (u32 *)dst;
	u16 *dst16 = (u16 *)dst;
	u8 *dst8 = (u8 *)dst;

	if (numcolours <= 256) {
		src8 = (u8 *)src;
	} else {
		src16 = (u16 *)src;
	}

	switch (format) {
	case TEXFORMAT_RGBA32:
		for (y = 0; y < height; y++) {
			for (x = 0; x < width; x++) {
				if (numcolours <= 256) {
					dst32[x] = BE32(lookup32[src8[x]]);
				} else {
					dst32[x] = BE32(lookup32[src16[x]]);
				}
			}

			dst32 += width;
			src8 += width;
			src16 += width;
		}

		return width * height * 4;
	case TEXFORMAT_RGB24:
		for (y = 0; y < height; y++) {
			for (x = 0; x < width; x++) {
				if (numcolours <= 256) {
					dst32[x] = BE32(lookup32[src8[x]] << 8 | 0xff);
				} else {
					dst32[x] = BE32(lookup32[src16[x]] << 8 | 0xff);
				}
			}

			dst32 += width;
			src8 += width;
			src16 += width;
		}

		return width * height * 4;
	case TEXFORMAT_RGBA16:
	case TEXFORMAT_IA16:
		for (y = 0; y < height; y++) {
			for (x = 0; x < width; x++) {
				if (numcolours <= 256) {
					dst16[x] = BE16(lookup16[src8[x]]);
				} else {
					dst16[x] = BE16(lookup16[src16[x]]);
				}
			}

			dst16 += width;
			src8 += width;
			src16 += width;
		}

		return width * height * 2;
	case TEXFORMAT_RGB15:
		for (y = 0; y < height; y++) {
			for (x = 0; x < width; x++) {
				if (numcolours <= 256) {
					dst16[x] = BE16(lookup16[src8[x]] << 1 | 1);
				} else {
					dst16[x] = BE16(lookup16[src16[x]] << 1 | 1);
				}
			}

			dst16 += width;
			src8 += width;
			src16 += width;
		}

		return width * height * 2;
	case TEXFORMAT_IA8:
	case TEXFORMAT_I8:
		for (y = 0; y < height; y++) {
			for (x = 0; x < width; x++) {
				if (numcolours <= 256) {
					dst8[x] = lookup16[src8[x]];
				} else {
					dst8[x] = lookup16[src16[x]];
				}
			}

			dst8 += width;
			src8 += width;
			src16 += width;
		}

		return width * height;
	case TEXFORMAT_IA4:
	case TEXFORMAT_I4:
		for (y = 0; y < height; y++) {
			for (x = 0; x < width; x += 2) {
				if (numcolours <= 256) {
					dst8[x >> 1] = lookup16[src8[x]] << 4 | lookup16[src8[x + 1]];
				} else {
					dst8[x >> 1] = lookup16[src16[x]] << 4 | lookup16[src16[x + 1]];
				}
			}

			dst8 += width / 2;
			src8 += width;
			src16 += width;
		}

		return width * height / 2;
	}

	return 0;
}

/**
 * Blur the pixels in the image with the surrounding pixels.
 */
static void texBlur(u8 *pixels, s32 width, s32 height, s32 method, s32 chansize)
{
	s32 x;
	s32 y;

	for (y = 0; y < height; y++) {
		for (x = 0; x < width; x++) {
			s32 cur = pixels[y * width + x] + chansize * 2;
			s32 left = x > 0 ? pixels[y * width + x - 1] : 0;
			s32 above = y > 0 ? pixels[(y - 1) * width + x] : 0;
			s32 aboveleft = x > 0 && y > 0 ? pixels[(y - 1) * width + x - 1] : 0;

			switch (method) {
			case 0:
				pixels[y * width + x] = (cur + left) % chansize;
				break;
			case 1:
				pixels[y * width + x] = (cur + above) % chansize;
				break;
			case 2:
				pixels[y * width + x] = (cur + aboveleft) % chansize;
				break;
			case 3:
				pixels[y * width + x] = (cur + (left + above - aboveleft)) % chansize;
				break;
			case 4:
				pixels[y * width + x] = (cur + ((above - aboveleft) / 2 + left)) % chansize;
				break;
			case 5:
				pixels[y * width + x] = (cur + ((left - aboveleft) / 2 + above)) % chansize;
				break;
			case 6:
				pixels[y * width + x] = (cur + ((left + above) / 2)) % chansize;
				break;
			}
		}
	}
}

/* A zlib texture's base image: the palette, then the first image's indices. */
static s32 texInflateZlibBase(u8 *dst, GevrPdTex *out)
{
	u16 palette[256];
	static u8 indices[0x2000];
	s32 i, numcolours, format, width, height, bytesout;
	u32 len;

	format = texReadBits(8);
	numcolours = texReadBits(8) + 1;

	for (i = 0; i < numcolours; i++) {
		palette[i] = texReadBits(16);
	}

	width = texReadBits(8);
	height = texReadBits(8);

	if (format < TEXFORMAT_RGBA16_CI8 || format > TEXFORMAT_IA16_CI4 || width * height > (s32)sizeof(indices)) {
		return 0;
	}

	/* every field so far is whole bytes, so the rarezip blob starts here:
	 * 0x11 0x73, a 24-bit length, raw deflate */
	if (g_TexBitstring[0] != 0x11) {
		return 0;
	}
	len = (u32)g_TexBitstring[2] << 16 | (u32)g_TexBitstring[3] << 8 | g_TexBitstring[4];
	if (len > sizeof(indices) || gevrGexInflate(g_TexBitstring + 5, 0x4000, indices, len) != (s32)len) {
		return 0;
	}

	bytesout = texAlignIndices(indices, width, height, format, dst);

	if (bytesout & 1) {
		bytesout++;
	}

	out->tlutoffset = bytesout;

	for (i = 0; i < numcolours; i++) {
		dst[bytesout + 0] = palette[i] >> 8;
		dst[bytesout + 1] = palette[i] & 0xff;
		bytesout += 2;
	}

	if (numcolours & 1) {
		dst[bytesout + 0] = dst[bytesout - 2];
		dst[bytesout + 1] = dst[bytesout - 1];
		bytesout += 2;
		numcolours++;
	}

	out->width = width;
	out->height = height;
	out->format = format;
	out->numcolours = numcolours;
	return (bytesout + 7) & ~7;
}

/* A non-zlib texture's base image. */
static s32 texInflateNonZlibBase(u8 *dst, GevrPdTex *out)
{
	static u8 scratch[0x8000];
	static u8 lookup[0x1000 * 4];
	s32 format = texReadBits(4);
	s32 width = texReadBits(8);
	s32 height = texReadBits(8);
	s32 compmethod = texReadBits(4);
	s32 value, j, bytesout = 0;

	if (width * height > 0x2000 || format > TEXFORMAT_I4) {
		return 0;
	}

	switch (compmethod) {
	case TEXCOMPMETHOD_UNCOMPRESSED0:
	case TEXCOMPMETHOD_UNCOMPRESSED1:
		bytesout = texReadUncompressed(dst, width, height, format);
		break;
	case TEXCOMPMETHOD_HUFFMAN:
		texInflateHuffman(scratch, g_TexFormatNumChannels[format] * width * height, g_TexFormatChannelSizes[format]);
		if (g_TexFormatHas1BitAlpha[format]) {
			texReadAlphaBits(&scratch[width * height * 3], width * height);
		}
		bytesout = texChannelsToPixels(scratch, width, height, dst, format);
		break;
	case TEXCOMPMETHOD_HUFFMANPERHCHANNEL:
		for (j = 0; j < g_TexFormatNumChannels[format]; j++) {
			texInflateHuffman(&scratch[width * height * j], width * height, g_TexFormatChannelSizes[format]);
		}
		if (g_TexFormatHas1BitAlpha[format]) {
			texReadAlphaBits(&scratch[width * height * 3], width * height);
		}
		bytesout = texChannelsToPixels(scratch, width, height, dst, format);
		break;
	case TEXCOMPMETHOD_RLE:
		texInflateRle(scratch, g_TexFormatNumChannels[format] * width * height);
		if (g_TexFormatHas1BitAlpha[format]) {
			texReadAlphaBits(&scratch[width * height * 3], width * height);
		}
		bytesout = texChannelsToPixels(scratch, width, height, dst, format);
		break;
	case TEXCOMPMETHOD_LOOKUP:
		value = texBuildLookup(lookup, g_TexFormatBitsPerPixel[format]);
		bytesout = texInflateLookup(width, height, dst, lookup, value, format);
		break;
	case TEXCOMPMETHOD_HUFFMANLOOKUP:
		value = texBuildLookup(lookup, g_TexFormatBitsPerPixel[format]);
		texInflateHuffman(scratch, width * height, value);
		bytesout = texInflateLookupFromBuffer(scratch, width, height, dst, lookup, value, format);
		break;
	case TEXCOMPMETHOD_RLELOOKUP:
		value = texBuildLookup(lookup, g_TexFormatBitsPerPixel[format]);
		texInflateRle(scratch, width * height);
		bytesout = texInflateLookupFromBuffer(scratch, width, height, dst, lookup, value, format);
		break;
	case TEXCOMPMETHOD_HUFFMANBLUR:
		value = texReadBits(3);
		texInflateHuffman(scratch, g_TexFormatNumChannels[format] * width * height, g_TexFormatChannelSizes[format]);
		texBlur(scratch, width, g_TexFormatNumChannels[format] * height, value, g_TexFormatChannelSizes[format]);
		if (g_TexFormatHas1BitAlpha[format]) {
			texReadAlphaBits(&scratch[width * height * 3], width * height);
		}
		bytesout = texChannelsToPixels(scratch, width, height, dst, format);
		break;
	case TEXCOMPMETHOD_RLEBLUR:
		value = texReadBits(3);
		texInflateRle(scratch, g_TexFormatNumChannels[format] * width * height);
		texBlur(scratch, width, g_TexFormatNumChannels[format] * height, value, g_TexFormatChannelSizes[format]);
		if (g_TexFormatHas1BitAlpha[format]) {
			texReadAlphaBits(&scratch[width * height * 3], width * height);
		}
		bytesout = texChannelsToPixels(scratch, width, height, dst, format);
		break;
	default:
		return 0;
	}

	out->width = width;
	out->height = height;
	out->format = format;
	out->numcolours = 0;
	out->tlutoffset = 0;
	return bytesout;
}

s32 gevrPdTexDecode(const u8 *comp, u32 len, GevrPdTex *out)
{
	s32 iszlib, bytesout;
	u8 *dst;

	memset(out, 0, sizeof(*out));
	if (comp == NULL || len < 2) {
		return 0;
	}
	iszlib = (comp[0] & 0x40) >> 6;
	/* 0x2000 texels at most, four bytes each, rows padded, and a palette */
	dst = (u8 *)calloc(1, 0x2000 * 4 + 0x1000);
	if (dst == NULL) {
		return 0;
	}
	texSetBitstring(comp + 1);
	bytesout = iszlib ? texInflateZlibBase(dst, out) : texInflateNonZlibBase(dst, out);
	if (bytesout <= 0) {
		free(dst);
		return 0;
	}
	out->gbiformat = g_TexFormatGbiMappings[out->format];
	out->depth = g_TexFormatDepths[out->format];
	out->lutmode = g_TexFormatLutModes[out->format];
	out->data = dst;
	out->size = bytesout;
	return 1;
}
