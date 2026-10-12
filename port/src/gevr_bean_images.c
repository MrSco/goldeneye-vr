/*
 * GoldenEye's own HUD, menu and effect pictures, repainted from the XBLA
 * release's files/texture/<name>/default.rba as each image loads.
 *
 * DAB (adef1dc2, MIT; see port/licenses) draws these from his own front end
 * and HUD (gefolder.c geFolderMenuPicture, gehud.c, gexfront.c) and binds the
 * world ones at the loaded texture's address (geimpact.c, gehitpuff.c). This
 * port keeps GoldenEye's own draws, so every picture is bound at its image's
 * loaded address (texLoad, image.c) and the renderer uploads it in place of
 * the ROM's texels while Bean is on (gevrBeanRepaint, gfx_pc.cpp).
 *
 * Each row was matched against the cartridge's own texels: the release's
 * picture, shrunk to the ROM's size, correlates 0.77-1.00 with them in the
 * decoded row order and lower turned over (port/tests/test_bean_images.py).
 * The decoded rows are the ROM's memory order, so nothing is flipped here.
 * Left out: the briefing folder's Bond photograph (gevr_bean_folder.c owns
 * 2602-2605), the monitors (DAB draws redrawn art), the fire frames and the
 * bullet hole the check could not confirm.
 */
#include "gevr_bean_log.h"

#include <PR/ultratypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gevr_bean.h"
#include "gevr_bean_int.h"
#include "gevr_romlayout.h"

enum { WHOLE, UL, UR, LL, LR };

struct imagerow {
	u16 image;            /* GoldenEye's image id */
	const char *picture;  /* under texture/, "name#variant" for a made-over one */
	u8 crop;              /* a portrait is one picture over four tiles */
};

static const struct imagerow imageRows[] = {
	/* the menu cursor and the HUD sight: one image */
	{ 2236, "bg/sight", WHOLE },
	/* the HUD's ammunition */
	{ 2231, "bg/ammoicon9mm", WHOLE },
	{ 2232, "bg/ammoiconrifle", WHOLE },
	{ 2167, "bg/ammoiconshell", WHOLE },
	{ 2166, "bg/ammoiconknife", WHOLE },
	{ 2165, "bg/ammoicongrenade", WHOLE },
	{ 2161, "bg/ammoiconrocket", WHOLE },
	{ 2162, "bg/ammoiconmine", WHOLE },
	{ 2163, "bg/ammogrenadehand", WHOLE },
	{ 2164, "bg/ammoiconmagnum", WHOLE },
	{ 2233, "bg/ammoicon9mm#gold", WHOLE },
	{ 2234, "bg/ammoiconmine", WHOLE },
	{ 2238, "bg/ammoiconmine#yellow", WHOLE },
	{ 2235, "bg/ammoiconmine#green", WHOLE },
	{ 2464, "bg/ammoicontankshell", WHOLE },
	/* menu marks */
	{ 1, "attract/cross", WHOLE },
	{ 4, "attract/tick", WHOLE },
	{ 2631, "attract/sprocket", WHOLE },
	/* character portraits, four tiles of one picture */
	{ 2632, "characters/boris", UL },
	{ 2633, "characters/boris", UR },
	{ 2634, "characters/boris", LL },
	{ 2635, "characters/boris", LR },
	{ 2636, "characters/ourumov", UL },
	{ 2637, "characters/ourumov", UR },
	{ 2638, "characters/ourumov", LL },
	{ 2639, "characters/ourumov", LR },
	{ 2640, "characters/trevelyan", UL },
	{ 2641, "characters/trevelyan", UR },
	{ 2642, "characters/trevelyan", LL },
	{ 2643, "characters/trevelyan", LR },
	{ 2644, "characters/valentin", UL },
	{ 2645, "characters/valentin", UR },
	{ 2646, "characters/valentin", LL },
	{ 2647, "characters/valentin", LR },
	{ 2648, "characters/xenia", UL },
	{ 2649, "characters/xenia", UR },
	{ 2650, "characters/xenia", LL },
	{ 2651, "characters/xenia", LR },
	{ 2652, "characters/natalya", UL },
	{ 2653, "characters/natalya", UR },
	{ 2654, "characters/natalya", LL },
	{ 2655, "characters/natalya", LR },
	{ 2656, "characters/baron", UL },
	{ 2657, "characters/baron", UR },
	{ 2658, "characters/baron", LL },
	{ 2659, "characters/baron", LR },
	{ 2660, "characters/jaws", UL },
	{ 2661, "characters/jaws", UR },
	{ 2662, "characters/jaws", LL },
	{ 2663, "characters/jaws", LR },
	{ 2664, "characters/mayday", UL },
	{ 2665, "characters/mayday", UR },
	{ 2666, "characters/mayday", LL },
	{ 2667, "characters/mayday", LR },
	{ 2668, "characters/oddjob", UL },
	{ 2669, "characters/oddjob", UR },
	{ 2670, "characters/oddjob", LL },
	{ 2671, "characters/oddjob", LR },
	{ 2682, "characters/who", UL },
	{ 2683, "characters/who", UR },
	{ 2684, "characters/who", LL },
	{ 2685, "characters/who", LR },
	{ 2694, "characters/mishkin", UL },
	{ 2693, "characters/mishkin", UR },
	{ 2691, "characters/mishkin", LL },
	{ 2692, "characters/mishkin", LR },
	/* the stage pictures */
	{ 2578, "level/archicon", WHOLE },
	{ 2579, "level/arecicon", WHOLE },
	{ 2580, "level/arkicon", WHOLE },
	{ 2581, "level/azticon", WHOLE },
	{ 2582, "level/caveicon", WHOLE },
	{ 2583, "level/cradicon", WHOLE },
	{ 2584, "level/crypicon", WHOLE },
	{ 2585, "level/damicon", WHOLE },
	{ 2586, "level/depoicon", WHOLE },
	{ 2587, "level/desticon", WHOLE },
	{ 2588, "level/junicon", WHOLE },
	{ 2589, "level/peteicon", WHOLE },
	{ 2590, "level/runicon", WHOLE },
	{ 2591, "level/sevicon", WHOLE },
	{ 2592, "level/sevbicon", WHOLE },
	{ 2593, "level/sevxicon", WHOLE },
	{ 2594, "level/sevxbicon", WHOLE },
	{ 2595, "level/siloicon", WHOLE },
	{ 2596, "level/staticon", WHOLE },
	{ 2597, "level/traicon", WHOLE },
	{ 2686, "level/smptempleicon", WHOLE },
	{ 2687, "level/smplibicon", WHOLE },
	{ 2688, "level/smpcomplexicon", WHOLE },
	{ 2689, "level/smpcaveicon", WHOLE },
	{ 2695, "bg/where", WHOLE },
	/* bullet holes and impacts */
	{ 2168, "bulletholes/windowhit", WHOLE },
	{ 2169, "bulletholes/bullethit", WHOLE },
	{ 2170, "bulletholes/wallhit", WHOLE },
	{ 2171, "bulletholes/bullethole", WHOLE },   /* DAB geimpact.c GE_IMAGE_IMPACT4 */
	{ 206, "bulletholes/bulletholesplaster", WHOLE },
	{ 2172, "bulletholes/bulletholesplasterrgb", WHOLE },
	{ 1475, "bulletholes/wallhole1", WHOLE },
	{ 2173, "bulletholes/wallhole2", WHOLE },
	{ 1476, "bulletholes/wallhole3", WHOLE },
	{ 2174, "bulletholes/wallhole6", WHOLE },
	{ 2175, "bulletholes/wallhole7", WHOLE },
	{ 1478, "bulletholes/wallhole8", WHOLE },
	{ 1479, "bulletholes/wallhole9", WHOLE },
	/* hit puffs, the laser and its beam */
	{ 2176, "sfx/hit5", WHOLE },
	{ 2177, "sfx/hit10", WHOLE },
	{ 2178, "sfx/hit15", WHOLE },
	{ 2179, "sfx/hit20", WHOLE },
	{ 2180, "sfx/hit25", WHOLE },
	{ 2181, "sfx/hit30", WHOLE },
	{ 2182, "sfx/backhit6", WHOLE },
	{ 2183, "sfx/backhit12", WHOLE },
	{ 2184, "sfx/backhit18", WHOLE },
	{ 2185, "sfx/backhit24", WHOLE },
	{ 2186, "sfx/backhit30", WHOLE },
	{ 2128, "sfx/laser", WHOLE },
	{ 2135, "sfx/beam", WHOLE },
	{ 2134, "sfx/beamhead", WHOLE },
	{ 2136, "sfx/scorch", WHOLE },
	{ 654, "sfx/glass", WHOLE },
	{ 1015, "bg/debris", WHOLE },
	/* the sky's clouds and the blue water */
	{ 2228, "bg/clouds", WHOLE },
	{ 1509, "bg/water2", WHOLE },
};

#define IMAGE_IDS 4096
#define NROWS ((int)(sizeof(imageRows) / sizeof(imageRows[0])))

static s16 rowOf[IMAGE_IDS];   /* image id -> row + 1 */
static u8 missing[NROWS];      /* the release's file is not on the device */
static int built;
static u8 reported[NROWS];

static void buildIndex(void)
{
	for (int i = 0; i < NROWS; i++) {
		if (imageRows[i].image < IMAGE_IDS) {
			rowOf[imageRows[i].image] = (s16)(i + 1);
		}
	}
	built = 1;
}

/* the texels a line holds: whole 64-bit lines (gevr_bean_folder.c) */
static int paddedWidth(int width, int depth)
{
	switch (depth) {
	case 3:  /* G_IM_SIZ_32b */
	case 2:  /* G_IM_SIZ_16b */
		return (width + 3) & ~3;
	case 1:  /* G_IM_SIZ_8b */
		return (width + 7) & ~7;
	default:
		return (width + 15) & ~15;
	}
}

/*
 * DAB's made-over ammunition (gefolder.c menuAmmoPicture): the golden gun's
 * round is the 9mm on a gold ramp; the timed and proximity mines are the mine
 * with its red lights turned yellow and green.
 */
static void ammoVariant(u8 *rgba, int count, const char *variant)
{
	for (int k = 0; k < count; k++) {
		u8 *px = rgba + (size_t)k * 4;

		if (strcmp(variant, "gold") == 0) {
			const float l = (px[0] * 0.30f + px[1] * 0.59f + px[2] * 0.11f) / 255.0f;
			static const float ramp[3][3] = { { 70, 45, 0 }, { 235, 180, 40 }, { 255, 245, 185 } };
			const int hi = l > 0.6f;
			const float t = hi ? (l - 0.6f) / 0.4f : l / 0.6f;

			for (int c = 0; c < 3; c++) {
				const float v = ramp[hi][c] + (ramp[hi + 1][c] - ramp[hi][c]) * (t > 1.0f ? 1.0f : t);
				px[c] = (u8)(v + 0.5f);
			}
		} else if (px[0] > px[1] + 48 && px[0] > px[2] + 48) {
			if (strcmp(variant, "yellow") == 0) {
				px[1] = (u8)(px[0] * 0.85f + px[1] * 0.15f);
			} else if (strcmp(variant, "green") == 0) {
				const u8 red = px[0];

				px[0] = px[1];
				px[1] = red;
			}
		}
	}
}

/* the picture last decoded: a portrait's four tiles load one after another */
static char lastName[64];
static u8 *lastRgba;
static int lastW, lastH;

static const u8 *picture(const char *name, int *w, int *h)
{
	static const char *roots[] = { "../texture-packs/ge007-bean/files", "added-content/xbla/files", "xbla/files" };
	char path[256];
	FILE *f = NULL;
	long size = 0;
	u8 *bytes = NULL;
	struct gevrCaff caff;

	if (lastRgba && strcmp(lastName, name) == 0) {
		*w = lastW;
		*h = lastH;
		return lastRgba;
	}
	free(lastRgba);
	lastRgba = NULL;
	lastName[0] = 0;
	for (size_t i = 0; i < sizeof(roots) / sizeof(roots[0]) && !f; i++) {
		snprintf(path, sizeof(path), "%s/texture/%s/default.rba", roots[i], name);
		f = fopen(path, "rb");
	}
	if (!f) {
		return NULL;
	}
	if (fseek(f, 0, SEEK_END) == 0 && (size = ftell(f)) > 0 && size <= 16 * 1024 * 1024) {
		rewind(f);
		bytes = malloc((size_t)size);
		if (bytes && fread(bytes, 1, (size_t)size, f) != (size_t)size) {
			free(bytes);
			bytes = NULL;
		}
	}
	fclose(f);
	memset(&caff, 0, sizeof(caff));
	if (bytes && gevrCaffOpen(&caff, bytes, (uint32_t)size)) {
		for (uint32_t i = 0; i < caff.numfiles && !lastRgba; i++) {
			uint32_t n = 0;
			const uint8_t *b = gevrCaffBlob(&caff, (int)i, &n);

			if (b && n >= 8 && memcmp(b, "texture", 7) == 0 && b[7] == 0) {
				lastRgba = gevrBeanTexDecodeRgba(&caff, (int)i, &lastW, &lastH);
			}
		}
		gevrCaffClose(&caff);
	}
	free(bytes);
	if (!lastRgba) {
		return NULL;
	}
	snprintf(lastName, sizeof(lastName), "%s", name);
	*w = lastW;
	*h = lastH;
	return lastRgba;
}

/*
 * The release's picture for one tile, padded as the loaded block is: a whole
 * picture keeps its own scale and gains padding beside it; a portrait tile is
 * the tile's own size cut from the picture's corner (decoded rows are bottom
 * first, so the upper tiles are the last rows).
 */
static u8 *makeTile(const struct imagerow *row, int tw, int th, int depth, int *outw, int *outh)
{
	char name[64];
	const char *variant = "";
	const u8 *src;
	int sw = 0, sh = 0, x0 = 0, y0 = 0, cw, ch, w;
	u8 *out;

	snprintf(name, sizeof(name), "%s", row->picture);
	if (strchr(name, '#')) {
		variant = strchr(row->picture, '#') + 1;
		*strchr(name, '#') = 0;
	}
	src = picture(name, &sw, &sh);
	if (!src) {
		return NULL;
	}
	if (row->crop == WHOLE) {
		cw = sw;
		ch = sh;
	} else {
		if (sw < tw || sh < th) {
			return NULL;
		}
		cw = tw;
		ch = th;
		x0 = row->crop == UR || row->crop == LR ? sw - tw : 0;
		y0 = row->crop == UL || row->crop == UR ? sh - th : 0;
	}
	w = (int)(((int64_t)cw * paddedWidth(tw, depth) + tw / 2) / tw);
	if (w < cw) {
		w = cw;
	}
	out = malloc((size_t)w * ch * 4);
	if (!out) {
		return NULL;
	}
	for (int y = 0; y < ch; y++) {
		const u8 *in = src + ((size_t)(y0 + y) * sw + x0) * 4;
		u8 *dst = out + (size_t)y * w * 4;

		memcpy(dst, in, (size_t)cw * 4);
		for (int x = cw; x < w; x++) {
			memcpy(dst + (size_t)x * 4, in + (size_t)(cw - 1) * 4, 4);
		}
	}
	if (*variant) {
		ammoVariant(out, w * ch, variant);
	}
	*outw = w;
	*outh = ch;
	return out;
}

void gevrBeanImageLoaded(int image, const void *data, int width, int height, int depth)
{
	const struct imagerow *row;
	u8 *rgba;
	int w = 0, h = 0, r;

	if (!data) {
		return;
	}
	/* whatever was painted at this address belonged to a texture that has gone */
	gevrBeanRepaintUnbind(data);
	if (image < 0 || image >= IMAGE_IDS || !gevrBeanIsAvailable() || gevrRomIsVariant()) {
		return;
	}
	if (!built) {
		buildIndex();
	}
	r = rowOf[image] - 1;
	if (r < 0 || missing[r]) {
		return;
	}
	row = &imageRows[r];
	rgba = makeTile(row, width, height, depth, &w, &h);
	if (!rgba) {
		missing[r] = 1;
		GEVR_BEAN_LOG("gevr bean images: no %s for image %d", row->picture, image);
		return;
	}
    if (!gevrBeanRepaintBind(GEVR_PAINT_IMAGE, data, rgba, w, h)) {
        GEVR_BEAN_LOG("gevr bean images: repaint capacity exhausted at image %d (%s)", image, row->picture);
    } else if (!reported[r]) {
        reported[r] = 1;
        GEVR_BEAN_LOG("gevr bean images: %d %s native %dx%d -> %dx%d", image, row->picture, width, height, w, h);
    }
}

void gevrBeanImagesReset(void)
{
	free(lastRgba);
	lastRgba = NULL;
	lastName[0] = 0;
	memset(missing, 0, sizeof(missing));
	memset(reported, 0, sizeof(reported));
}
