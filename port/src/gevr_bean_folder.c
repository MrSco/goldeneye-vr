/*
 * The briefing folder's pictures out of the XBLA release, painted over
 * GoldenEye's own folder model (PwalletbondZ), one texture at a time.
 *
 * Ported from DAB's gefolder.c (DabDavis/perfect-dark-dabs-mod adef1dc2,
 * port/src/gefolder.c 62-888, MIT; see port/licenses): the same rows, the same
 * crops, the same paper and grey matching. Rare built the release's folder on
 * the cartridge's, so the geometry and UVs are GoldenEye's and a picture of 4J's
 * is the same picture larger: each one is bound at the address the model's
 * display lists already name (gevrBeanRepaintBind), and the renderer uploads it
 * in place of the ROM's texels while Bean is on (gfx_pc.cpp import_texture).
 *
 * Left out: DAB's build of the release's own folder mesh over GoldenEye's
 * (geFolderBeanBuild), its picture dump switch, and its menu pictures, fonts
 * and backdrop, which are files this port does not package yet.
 *
 * A picture is not always the whole of a texture: GoldenEye splits the
 * photograph of Bond into four 65x65 quarters and each stamp into halves, and
 * repeats one small paper tile across the page. A row names the rectangle of
 * the release's picture that stands for the ROM's texture. And a texture is
 * wider than it looks: the N64 loads whole 64-bit lines, and the renderer
 * normalises a block-loaded texture's coordinates by that padded line, so each
 * picture is padded out the same way, its last column repeated.
 */
#include "gevr_bean_log.h"

#include <ultra64.h>
#include <PR/gbi.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include <bondtypes.h>
#include "gevr_bean_int.h"

#define FOLDER_MAXPICTURES 128

struct folderrow {
	s16 config;   /* which of the model's textures this is */
	s16 width;    /* and what it must be, or the file is not the one we know */
	s16 height;
	s16 picture;  /* the release's picture that carries it */
	s16 tiles;    /* whether the ROM repeats this one across the page */
	s16 mask;     /* the ROM's is a cutout and the release's carries no alpha:
	               * its brightness becomes the alpha, drawn white */
	s16 onpaper;  /* it carries the page behind it: its paper is brought to the
	               * page's colour; 2: its ink is printed on the paper tile */
	f32 u0, v0, u1, v1; /* the part of it that is this texture, as seen */
	s16 grey;     /* the ROM's is an intensity texture the node's shade colours */
};

#define WHOLE 0.0f, 0.0f, 1.0f, 1.0f

static const struct folderrow folderRows[] = {
	/* config, w, h, picture, tiles, mask, onpaper, u0, v0, u1, v1 */

	/* the paperclip and the cursor's shadow, cutouts in the ROM: masks */
	{  4, 32, 64, 66, 0, 1, 0, WHOLE },                              /* 0x03f6, the paperclip */
	{ 17, 32, 32, 64, 0, 1, 0, WHOLE },                              /* 0x04fd */
	{ 23, 16, 16, 67, 0, 0, 0, WHOLE },                              /* 0x0a47, the film strip's hole */

	/* the royal crest, four quarters (0x0a26-0x0a29) off the release's page,
	 * brought to the colour the page is drawn in */
	{  0, 65, 65,  2, 0, 0, 1, 0.7250f, 0.0300f, 0.8500f, 0.1250f },
	{  1, 65, 65,  2, 0, 0, 1, 0.8500f, 0.0300f, 0.9750f, 0.1250f },
	{  2, 65, 65,  2, 0, 0, 1, 0.7250f, 0.1250f, 0.8500f, 0.2200f },
	{  3, 65, 65,  2, 0, 0, 1, 0.8500f, 0.1250f, 0.9750f, 0.2200f },
	/* the whole crest the menu draws, over one repeat of the paper tile: its
	 * ink alone printed on the tile the page is drawn with */
	{ 12, 64, 64,  2, 0, 0, 2, 0.7250f, 0.0300f, 0.9750f, 0.2200f, 1 }, /* 0x0a46, whole */

	/* Brosnan, in four quarters (0x0a2a-0x0a2d) of the release's 256x512 */
	{  5, 65, 65, 23, 0, 0, 0, 0.0f, 0.0f, 0.5f, 0.5f },             /* 0x0a2a */
	{  6, 65, 65, 23, 0, 0, 0, 0.5f, 0.0f, 1.0f, 0.5f },             /* 0x0a2b */
	{  7, 65, 65, 23, 0, 0, 0, 0.0f, 0.5f, 0.5f, 1.0f },             /* 0x0a2c */
	{  8, 65, 65, 23, 0, 0, 0, 0.5f, 0.5f, 1.0f, 1.0f },             /* 0x0a2d */

	/* the cover and the paper: one tile repeated over the page, so a patch of
	 * the release's picture the size the tile covers, the flattest one,
	 * cross-faded into itself; grey at the ROM tile's brightness, since the
	 * vertex shade colours them */
	{ 10, 64, 64,  0, 1, 0, 0, 0.2420f, 0.1667f, 0.3665f, 0.3627f, 1 }, /* 0x0a42, the cover */
	{ 11, 64, 64,  1, 1, 0, 0, 0.4688f, 0.8125f, 0.5938f, 0.9375f, 1 }, /* 0x0a45, the paper */

	/* the stamps: the OHMSS title as a mask, the red ones with their own
	 * alpha; each piece its share of the word, the ink measured on both sides */
	{  9, 95, 32, 65, 0, 1, 0, 0.0000f, 0.0625f, 0.5000f, 0.9844f }, /* 0x0a43 "OHM" */
	{ 18, 95, 32, 65, 0, 1, 0, 0.5000f, 0.0625f, 1.0000f, 0.9844f }, /* 0x0a44 "SS" */
	{ 21, 95, 32, 70, 0, 0, 0, 0.0762f, 0.0078f, 0.4951f, 1.0000f }, /* 0x0a3a "CLASS" */
	{ 22, 95, 32, 70, 0, 0, 0, 0.4951f, 0.0078f, 0.9141f, 1.0000f }, /* 0x0a3b "SIFIED" */
	{ 19, 95, 32, 69, 0, 0, 0, 0.0742f, 0.0234f, 0.4624f, 0.9609f }, /* 0x0a3c "CONFID" */
	{ 20, 95, 32, 69, 0, 0, 0, 0.4624f, 0.0234f, 0.8506f, 0.9609f }, /* 0x0a3d "ENTIAL" */
	{ 14, 95, 32, 68, 0, 0, 0, 0.5049f, 0.0234f, 0.7285f, 0.9609f }, /* 0x0a3e "EYES" */
	{ 15, 95, 32, 68, 0, 0, 0, 0.7598f, 0.0234f, 0.9990f, 0.9609f }, /* 0x0a3f "ONLY" */
	{ 13, 95, 32, 68, 0, 0, 0, 0.0068f, 0.0234f, 0.1855f, 0.9609f }, /* 0x0a40 "FOR" */
	{ 16, 95, 32, 68, 0, 0, 0, 0.2100f, 0.0234f, 0.4727f, 0.9609f }, /* 0x0a41 "YOUR" */
};

/* the briefing photographs (0x09ea on, 128x64) and the mission slides (0x0a12 on, 68x44) */
#define FOLDER_BRIEF_FIRST   24
#define FOLDER_BRIEF_COUNT   40
#define FOLDER_SLIDE_FIRST   64
#define FOLDER_SLIDE_COUNT   20

/* the release's same shots in its own order, matched picture against picture */
static const u8 slidePicture[FOLDER_SLIDE_COUNT] = {
	13, 18,  4, 21, 19, 20, 22,  3, 15,  9,
	17, 14,  5,  7, 11,  6, 10,  8, 12, 16,
};

static const u8 briefPicture[FOLDER_BRIEF_COUNT] = {
	45, 25, 24, 44, 61, 60, 57, 56, 59, 58,
	63, 62, 49, 48, 37, 36, 53, 52, 47, 46,
	29, 28, 33, 32, 41, 40, 31, 30, 39, 38,
	35, 34, 43, 42, 51, 50, 27, 26, 55, 54,
};

static s32 numBound;
static s32 warned;

/* the open file's pictures: the CAFF files that start "texture\0", in order */
static const struct gevrCaff *pics;
static int picFile[FOLDER_MAXPICTURES];
static int numPics;

/* the texels a line of this texture really holds: whole 64-bit lines */
static s32 folderPaddedWidth(s32 width, s32 depth)
{
	switch (depth) {
	case G_IM_SIZ_32b:
	case G_IM_SIZ_16b:
		return (width + 3) & ~3;
	case G_IM_SIZ_8b:
		return (width + 7) & ~7;
	default:
		return (width + 15) & ~15;
	}
}

/* the colour the page is drawn in, worked out before anything is bound */
static s32 paperColour[3];
static s32 havePaperColour;

static void folderMeanOf(const u8 *rgba, s32 w, s32 h, s32 border, s32 *out)
{
	s64 sum[3] = { 0, 0, 0 };
	s64 n = 0;

	for (s32 y = 0; y < h; y++) {
		for (s32 x = 0; x < w; x++) {
			/* the border alone, for a picture whose middle is the crest */
			if (border && x >= border && x < w - border && y >= border && y < h - border) {
				continue;
			}

			const u8 *px = rgba + ((size_t)y * w + x) * 4;

			for (s32 k = 0; k < 3; k++) {
				sum[k] += px[k];
			}

			n++;
		}
	}

	for (s32 k = 0; k < 3; k++) {
		out[k] = n ? (s32)(sum[k] / n) : 0;
	}
}

/* shifts a crop by what its edges are away from the page's own paper colour */
static void folderMatchPaper(u8 *rgba, s32 w, s32 h)
{
	s32 edge[3];
	s32 shift[3];

	if (!havePaperColour) {
		return;
	}

	folderMeanOf(rgba, w, h, w < h ? w / 8 : h / 8, edge);

	for (s32 k = 0; k < 3; k++) {
		shift[k] = paperColour[k] - edge[k];
	}

	for (s32 i = 0; i < w * h; i++) {
		u8 *px = rgba + (size_t)i * 4;

		for (s32 k = 0; k < 3; k++) {
			const s32 v = px[k] + shift[k];

			px[k] = (u8)(v < 0 ? 0 : v > 255 ? 255 : v);
		}
	}
}

/* takes everything coarser than an eighth of a repeated patch out, keeping its grain */
static void folderFlatten(u8 *rgba, s32 w, s32 h)
{
	const s32 r = (w < h ? w : h) / 8;
	s32 mean[3];
	u8 *copy;

	if (r < 1) {
		return;
	}

	copy = malloc((size_t)w * h * 4);

	if (!copy) {
		return;
	}

	memcpy(copy, rgba, (size_t)w * h * 4);
	folderMeanOf(copy, w, h, 0, mean);

	for (s32 y = 0; y < h; y++) {
		for (s32 x = 0; x < w; x++) {
			s32 sum[3] = { 0, 0, 0 };
			s32 n = 0;

			/* the blur wraps, as the patch itself does on the page */
			for (s32 dy = -r; dy <= r; dy += 2) {
				const s32 sy = ((y + dy) % h + h) % h;

				for (s32 dx = -r; dx <= r; dx += 2) {
					const s32 sx = ((x + dx) % w + w) % w;
					const u8 *px = copy + ((size_t)sy * w + sx) * 4;

					for (s32 k = 0; k < 3; k++) {
						sum[k] += px[k];
					}

					n++;
				}
			}

			u8 *out = rgba + ((size_t)y * w + x) * 4;

			for (s32 k = 0; k < 3; k++) {
				const s32 v = out[k] - (n ? sum[k] / n : 0) + mean[k];

				out[k] = (u8)(v < 0 ? 0 : v > 255 ? 255 : v);
			}
		}
	}

	free(copy);
}

/* cross-fades a repeated patch into itself over an eighth of it */
static void folderMakeSeamless(u8 *rgba, s32 w, s32 h)
{
	const s32 fx = w / 8;
	const s32 fy = h / 8;

	for (s32 y = 0; y < h; y++) {
		for (s32 x = 0; x < fx; x++) {
			u8 *a = rgba + ((size_t)y * w + x) * 4;
			const u8 *b = rgba + ((size_t)y * w + (w - fx + x)) * 4;
			const s32 t = (fx > 1) ? 255 * x / (fx * 2) : 0; /* half weight at the seam */

			for (s32 k = 0; k < 4; k++) {
				a[k] = (u8)((a[k] * (255 - (127 - t)) + b[k] * (127 - t) + 127) / 255);
			}
		}
	}

	for (s32 y = 0; y < fy; y++) {
		for (s32 x = 0; x < w; x++) {
			u8 *a = rgba + ((size_t)y * w + x) * 4;
			const u8 *b = rgba + ((size_t)(h - fy + y) * w + x) * 4;
			const s32 t = (fy > 1) ? 255 * y / (fy * 2) : 0;

			for (s32 k = 0; k < 4; k++) {
				a[k] = (u8)((a[k] * (255 - (127 - t)) + b[k] * (127 - t) + 127) / 255);
			}
		}
	}
}

/* the texels of one of the model's textures, once texLoad has put its address in */
static const u8 *folderTexels(const ModelFileTextures *tc)
{
	/* a texture that did not load keeps its id, which is no address */
	return tc->TextureID >= 0x10000 ? (const u8 *)tc->TextureID : NULL;
}

/* the mean brightness of a ROM I4/I8 texture as loaded, and its spread */
static s32 folderRomGrey(const ModelFileTextures *tc, s32 *mean, s32 *spread)
{
	const s32 w = folderPaddedWidth(tc->Width, tc->RenderDepth);
	const u8 *texels = folderTexels(tc);
	s64 sum = 0;
	s64 sumsq = 0;
	s64 n = 0;

	if (!texels || (tc->RenderDepth != G_IM_SIZ_4b && tc->RenderDepth != G_IM_SIZ_8b)) {
		return 0;
	}

	for (s32 y = 0; y < tc->Height; y++) {
		for (s32 x = 0; x < tc->Width; x++) {
			s32 v;

			if (tc->RenderDepth == G_IM_SIZ_4b) {
				const u8 b = texels[((size_t)y * w + x) / 2];

				v = ((x & 1) ? (b & 0xf) : (b >> 4)) * 17;
			} else {
				v = texels[(size_t)y * w + x];
			}

			sum += v;
			sumsq += v * v;
			n++;
		}
	}

	if (!n) {
		return 0;
	}

	*mean = (s32)(sum / n);
	*spread = (s32)sqrtf((f32)(sumsq / n - (sum / n) * (sum / n)));

	return 1;
}

/* a picture brought to a ROM intensity texture's brightness and grain, neutral in hue */
static void folderMatchGrey(u8 *rgba, s32 w, s32 h, s32 grey, s32 spread)
{
	s32 mean[3];
	f64 sumsq = 0;
	f32 gain;

	folderMeanOf(rgba, w, h, 0, mean);

	for (s32 i = 0; i < w * h; i++) {
		const u8 *px = rgba + (size_t)i * 4;
		const f32 d = ((px[0] - mean[0]) + (px[1] - mean[1]) + (px[2] - mean[2])) / 3.0f;

		sumsq += d * d;
	}

	gain = sumsq > 0 ? spread / sqrtf((f32)(sumsq / ((f64)w * h))) : 0.0f;

	for (s32 i = 0; i < w * h; i++) {
		u8 *px = rgba + (size_t)i * 4;

		for (s32 k = 0; k < 3; k++) {
			const s32 v = grey + (s32)((px[k] - mean[k]) * gain);

			px[k] = (u8)(v < 0 ? 0 : v > 255 ? 255 : v);
		}
	}
}

/*
 * The rectangle of src a row names, padded out to the shape of the tile it
 * stands in for, RGBA8, malloc'd. Rows are written as the picture is seen and
 * the picture arrives bottom row first (gevrBeanTexDecodeRgba), so v turns over.
 */
static u8 *folderCrop(const u8 *src, s32 srcw, s32 srch, const struct folderrow *row,
		s32 tilewidth, s32 paddedwidth, s32 *outw, s32 *outh)
{
	s32 x0 = (s32)(row->u0 * srcw + 0.5f);
	s32 y0 = (s32)((1.0f - row->v1) * srch + 0.5f);
	s32 x1 = (s32)(row->u1 * srcw + 0.5f);
	s32 y1 = (s32)((1.0f - row->v0) * srch + 0.5f);
	s32 cw, ch, w;
	u8 *out;

	if (x0 < 0) x0 = 0;
	if (y0 < 0) y0 = 0;
	if (x1 > srcw) x1 = srcw;
	if (y1 > srch) y1 = srch;

	cw = x1 - x0;
	ch = y1 - y0;

	if (cw <= 0 || ch <= 0) {
		return NULL;
	}

	/* the picture keeps its scale and the padding goes beside it */
	w = tilewidth > 0 ? (s32)(((s64)cw * paddedwidth + tilewidth / 2) / tilewidth) : cw;

	if (w < cw) {
		w = cw;
	}

	out = malloc((size_t)w * ch * 4);

	if (!out) {
		return NULL;
	}

	for (s32 y = 0; y < ch; y++) {
		const u8 *in = src + (((size_t)(y0 + y) * srcw) + x0) * 4;
		u8 *dst = out + (size_t)y * w * 4;

		memcpy(dst, in, (size_t)cw * 4);

		for (s32 x = cw; x < w; x++) {
			memcpy(dst + (size_t)x * 4, in + (size_t)(cw - 1) * 4, 4);
		}
	}

	if (row->tiles) {
		folderFlatten(out, w, ch);
		folderMakeSeamless(out, w, ch);
	}

	if (row->onpaper) {
		folderMatchPaper(out, w, ch);
	}

	/* a cutout: the release draws the shape with its brightness */
	if (row->mask) {
		for (s32 i = 0; i < w * ch; i++) {
			u8 *px = out + (size_t)i * 4;
			const s32 lum = px[0] > px[1] ? (px[0] > px[2] ? px[0] : px[2]) : (px[1] > px[2] ? px[1] : px[2]);

			px[0] = px[1] = px[2] = 0xff;
			px[3] = (u8)lum;
		}
	}

	*outw = w;
	*outh = ch;

	return out;
}

/* the picture last decoded, since the rows that share one are written together */
static u8 *lastPicture;
static s32 lastIndex = -1;
static s32 lastWidth;
static s32 lastHeight;

static void folderDropCache(void)
{
	free(lastPicture);
	lastPicture = NULL;
	lastIndex = -1;
}

static const u8 *folderPicture(s32 index, s32 *w, s32 *h)
{
	if (index != lastIndex || !lastPicture) {
		int iw = 0, ih = 0;

		folderDropCache();
		lastPicture = index >= 0 && index < numPics ? gevrBeanTexDecodeRgba(pics, picFile[index], &iw, &ih) : NULL;
		lastWidth = iw;
		lastHeight = ih;
		lastIndex = lastPicture ? index : -1;
	}

	*w = lastWidth;
	*h = lastHeight;

	return lastPicture;
}

/* the paper tile as it is bound, for the crest to be printed on */
static u8 *paperTile;
static s32 paperTileWidth;
static s32 paperTileHeight;

/* prints a picture's ink, whatever is darker than its paper by more than the
 * paper's grain, on the page's paper tile scaled to its size */
static void folderPrintOnPaper(u8 *rgba, s32 w, s32 h)
{
	const s32 border = (w < h ? w : h) / 8;
	s32 edge[3];
	s32 level;
	f64 sumsq = 0;
	s64 n = 0;
	s32 grain;

	if (!paperTile || w <= 0 || h <= 0) {
		return;
	}

	folderMeanOf(rgba, w, h, border, edge);
	level = (edge[0] + edge[1] + edge[2]) / 3;

	for (s32 y = 0; y < h; y++) {
		for (s32 x = 0; x < w; x++) {
			if (x >= border && x < w - border && y >= border && y < h - border) {
				continue;
			}

			const u8 *px = rgba + ((size_t)y * w + x) * 4;
			const s32 d = (px[0] + px[1] + px[2]) / 3 - level;

			sumsq += d * d;
			n++;
		}
	}

	grain = n ? (s32)sqrtf((f32)(sumsq / n)) : 0;

	for (s32 y = 0; y < h; y++) {
		for (s32 x = 0; x < w; x++) {
			u8 *px = rgba + ((size_t)y * w + x) * 4;
			const u8 *pp = paperTile + (((size_t)(y * paperTileHeight / h) * paperTileWidth)
					+ x * paperTileWidth / w) * 4;
			s32 ink = level - (px[0] + px[1] + px[2]) / 3 - grain;

			if (ink < 0) {
				ink = 0;
			}

			for (s32 k = 0; k < 3; k++) {
				const s32 v = pp[k] - ink;

				px[k] = (u8)(v < 0 ? 0 : v);
			}

			px[3] = 0xff;
		}
	}
}

/* a row's picture as it is to be bound, or NULL where the model's texture is not the one expected */
static u8 *folderMake(const struct ModelFileHeader *header, const struct folderrow *row, s32 *outw, s32 *outh)
{
	const ModelFileTextures *tc;
	s32 srcw = 0, srch = 0;
	const u8 *src;
	u8 *crop;

	if (row->config < 0 || row->config >= header->numtextures) {
		return NULL;
	}

	tc = &header->Textures[row->config];

	/* another folder than the one these rows were written against keeps the ROM's art */
	if (tc->Width != row->width || tc->Height != row->height || !folderTexels(tc)) {
		if (!warned) {
			warned = 1;
			GEVR_BEAN_LOG("gevr bean folder: texture %d is %dx%d and this expects %dx%d; "
					"the folder keeps the ROM's art", row->config, tc->Width, tc->Height,
					row->width, row->height);
		}

		return NULL;
	}

	src = folderPicture(row->picture, &srcw, &srch);

	if (!src) {
		return NULL;
	}

	crop = folderCrop(src, srcw, srch, row, tc->Width, folderPaddedWidth(tc->Width, tc->RenderDepth), outw, outh);

	if (!crop) {
		return NULL;
	}

	if (row->grey) {
		s32 grey, spread;

		if (folderRomGrey(tc, &grey, &spread)) {
			folderMatchGrey(crop, *outw, *outh, grey, spread);
		}
	}

	if (row->onpaper == 2) {
		folderPrintOnPaper(crop, *outw, *outh);
	}

	return crop;
}

static void folderBind(const struct ModelFileHeader *header, const struct folderrow *row)
{
	s32 outw = 0, outh = 0;
	u8 *crop = folderMake(header, row, &outw, &outh);

	if (crop && gevrBeanRepaintBind(folderTexels(&header->Textures[row->config]), crop, outw, outh)) {
		numBound++;
	}
}

int gevrBeanFolderRepaint(struct ModelFileHeader *header, const struct gevrCaff *caff)
{
	struct folderrow row;

	numBound = 0;
	warned = 0;

	if (!header || !header->Textures || !caff) {
		return 0;
	}

	pics = caff;
	numPics = 0;

	for (uint32_t i = 0; i < caff->numfiles && numPics < FOLDER_MAXPICTURES; i++) {
		uint32_t blen = 0;
		const uint8_t *b = gevrCaffBlob(caff, (int)i, &blen);

		if (b && blen >= 8 && memcmp(b, "texture", 7) == 0 && b[7] == 0) {
			picFile[numPics++] = (int)i;
		}
	}

	/* the page's own colour, before anything is bound */
	havePaperColour = 0;

	for (size_t i = 0; i < sizeof(folderRows) / sizeof(folderRows[0]); i++) {
		const struct folderrow *r = &folderRows[i];
		s32 w = 0, h = 0;
		const u8 *rgba;
		u8 *patch;

		if (!r->tiles || r->picture != 1) {
			continue;
		}

		rgba = folderPicture(r->picture, &w, &h);

		if (!rgba) {
			continue;
		}

		patch = folderCrop(rgba, w, h, r, 0, 0, &w, &h);

		if (patch) {
			folderMeanOf(patch, w, h, 0, paperColour);
			havePaperColour = 1;
			free(patch);
		}
	}

	/* the paper tile itself, as it will be bound, for the crest to be printed on */
	for (size_t i = 0; i < sizeof(folderRows) / sizeof(folderRows[0]) && !paperTile; i++) {
		if (folderRows[i].tiles && folderRows[i].picture == 1) {
			paperTile = folderMake(header, &folderRows[i], &paperTileWidth, &paperTileHeight);
		}
	}

	for (size_t i = 0; i < sizeof(folderRows) / sizeof(folderRows[0]); i++) {
		folderBind(header, &folderRows[i]);
	}

	free(paperTile);
	paperTile = NULL;

	/* the briefing photographs and the mission slides */
	memset(&row, 0, sizeof(row));
	row.u1 = 1.0f;
	row.v1 = 1.0f;

	for (s32 i = 0; i < FOLDER_BRIEF_COUNT; i++) {
		row.config = (s16)(FOLDER_BRIEF_FIRST + i);
		row.width = 128;
		row.height = 64;
		row.picture = (s16)briefPicture[i];
		folderBind(header, &row);
	}

	for (s32 i = 0; i < FOLDER_SLIDE_COUNT; i++) {
		row.config = (s16)(FOLDER_SLIDE_FIRST + i);
		row.width = 68;
		row.height = 44;
		row.picture = (s16)slidePicture[i];
		folderBind(header, &row);
	}

	folderDropCache();
	pics = NULL;

	GEVR_BEAN_LOG("gevr bean folder: %d of the folder's %d pictures are the release's (%d in its file)",
			numBound, header->numtextures, numPics);

	return numBound;
}
