/*
 * Bean pictures: DXT and 8888, tiled, with the packed mip tail.
 * A 16x16 stand-in is what the display list binds. import_texture uploads
 * the decoded image when it sees that address.
 */

#include "gevr_bean_logic.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FMT_8888  6
#define FMT_DXT1  18
#define FMT_DXT3  19
#define FMT_DXT5  20
#define TEX_SLOTS 768

struct texSlot {
	char key[160];
	uint8_t *standin;
	uint8_t *rgba;
	int w, h;
};

static struct texSlot s_tex[TEX_SLOTS];
static int s_ntex;

extern void videoFreeCachedTexture(const void *addr);

static uint32_t rd16(const uint8_t *p)
{
	return (uint32_t)p[0] | ((uint32_t)p[1] << 8);
}

static uint32_t rd32(const uint8_t *p)
{
	return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void rgb565(uint32_t c, uint8_t *rgb)
{
	rgb[0] = (uint8_t)(((c >> 11) & 31) * 255 / 31);
	rgb[1] = (uint8_t)(((c >> 5) & 63) * 255 / 63);
	rgb[2] = (uint8_t)((c & 31) * 255 / 31);
}

static void dxtBlock(const uint8_t *block, int kind, uint8_t *dst, uint32_t stride, int w, int h)
{
	const uint8_t *cb = kind == 1 ? block : block + 8;
	uint32_t c0 = rd16(cb);
	uint32_t c1 = rd16(cb + 2);
	uint32_t idx = rd32(cb + 4);
	uint8_t colours[4][4];
	uint8_t alpha[16];
	int x, y;

	rgb565(c0, colours[0]);
	rgb565(c1, colours[1]);
	colours[0][3] = colours[1][3] = colours[2][3] = colours[3][3] = 255;
	if (kind != 1 || c0 > c1) {
		for (int k = 0; k < 3; k++) {
			colours[2][k] = (uint8_t)((2 * colours[0][k] + colours[1][k]) / 3);
			colours[3][k] = (uint8_t)((colours[0][k] + 2 * colours[1][k]) / 3);
		}
	} else {
		for (int k = 0; k < 3; k++) {
			colours[2][k] = (uint8_t)((colours[0][k] + colours[1][k]) / 2);
			colours[3][k] = 0;
		}
		colours[3][3] = 0;
	}
	for (int i = 0; i < 16; i++) {
		alpha[i] = 255;
	}
	if (kind == 3) {
		for (int i = 0; i < 16; i++) {
			uint32_t a = (block[i / 2] >> ((i & 1) * 4)) & 15;
			alpha[i] = (uint8_t)(a * 17);
		}
	} else if (kind == 5) {
		uint32_t a0 = block[0], a1 = block[1];
		uint8_t table[8];
		uint64_t bits = 0;
		table[0] = (uint8_t)a0;
		table[1] = (uint8_t)a1;
		if (a0 > a1) {
			for (int k = 1; k < 7; k++) {
				table[k + 1] = (uint8_t)(((7 - k) * a0 + k * a1) / 7);
			}
		} else {
			for (int k = 1; k < 5; k++) {
				table[k + 1] = (uint8_t)(((5 - k) * a0 + k * a1) / 5);
			}
			table[6] = 0;
			table[7] = 255;
		}
		for (int k = 5; k >= 0; k--) {
			bits = (bits << 8) | block[2 + k];
		}
		for (int i = 0; i < 16; i++) {
			alpha[i] = table[(bits >> (i * 3)) & 7];
		}
	}
	for (y = 0; y < 4 && y < h; y++) {
		for (x = 0; x < 4 && x < w; x++) {
			int i = y * 4 + x;
			const uint8_t *c = colours[(idx >> (i * 2)) & 3];
			uint8_t *o = dst + (uint32_t)y * stride + (uint32_t)x * 4;
			o[0] = c[0];
			o[1] = c[1];
			o[2] = c[2];
			o[3] = kind == 1 ? c[3] : alpha[i];
		}
	}
}

static uint32_t tiledOffset(uint32_t x, uint32_t y, uint32_t width, uint32_t log2bpp)
{
	uint32_t macro = ((x >> 5) + (y >> 5) * ((width + 31) >> 5)) << (log2bpp + 7);
	uint32_t micro = ((x & 7) + ((y & 0xe) << 2)) << log2bpp;
	uint32_t off = macro + ((micro & ~0xfu) << 1) + (micro & 0xf) + ((y & 1) << 4);
	return ((((off & ~0x1ffu) << 3) + ((y & 16) << 7) + ((off & 0x1c0) << 2) +
			(((((y & 8) >> 2) + (x >> 3)) & 3) << 6) + (off & 0x3f)) >> log2bpp);
}

static uint32_t log2u(uint32_t v)
{
	uint32_t n = 0;
	while ((1u << n) < v && n < 31) {
		n++;
	}
	return n;
}

static void endianSwap(uint8_t *data, uint32_t len, int mode)
{
	if (mode == 1) {
		for (uint32_t i = 0; i + 1 < len; i += 2) {
			uint8_t t = data[i];
			data[i] = data[i + 1];
			data[i + 1] = t;
		}
	} else if (mode == 2) {
		for (uint32_t i = 0; i + 3 < len; i += 4) {
			uint8_t t = data[i];
			data[i] = data[i + 3];
			data[i + 3] = t;
			t = data[i + 1];
			data[i + 1] = data[i + 2];
			data[i + 2] = t;
		}
	}
}

static uint8_t *decodeSurface(uint8_t *data, uint32_t dataLen, int format, int packed,
		uint32_t w, uint32_t h)
{
	uint32_t ox = 0, oy = 0, bpe, ew, eh, stride;
	uint8_t *rgba;
	int kind;

	if (w == 0 || h == 0 || w > 4096 || h > 4096) {
		return NULL;
	}
	rgba = malloc((size_t)w * h * 4);
	if (!rgba) {
		return NULL;
	}
	stride = w * 4;
	if (format == FMT_8888) {
		gevrBeanPackedMip(w, h, 1, packed, &ox, &oy);
		bpe = 4;
		ew = (w + 31u) & ~31u;
		eh = (h + 31u) & ~31u;
		if ((uint64_t)ew * eh * bpe > dataLen) {
			free(rgba);
			return NULL;
		}
		for (uint32_t y = 0; y < h; y++) {
			for (uint32_t x = 0; x < w; x++) {
				uint32_t e = tiledOffset(x + ox, y + oy, ew, log2u(bpe));
				if ((uint64_t)e * bpe + bpe > dataLen) {
					free(rgba);
					return NULL;
				}
				const uint8_t *src = data + (size_t)e * bpe;
				uint8_t *dst = rgba + ((size_t)y * w + x) * 4;
				/* Bean's 8888 is RGBA. The shared decoder reads BGRA and the
				 * Bean path swaps it back, which is these bytes as stored. */
				dst[0] = src[0];
				dst[1] = src[1];
				dst[2] = src[2];
				dst[3] = src[3];
			}
		}
		return rgba;
	}
	if (format == FMT_DXT1) {
		kind = 1;
		bpe = 8;
	} else if (format == FMT_DXT3) {
		kind = 3;
		bpe = 16;
	} else if (format == FMT_DXT5) {
		kind = 5;
		bpe = 16;
	} else {
		free(rgba);
		return NULL;
	}
	gevrBeanPackedMip(w, h, 4, packed, &ox, &oy);
	{
		uint32_t bw = (w + 3) / 4;
		uint32_t bh = (h + 3) / 4;
		ew = (bw + 31u) & ~31u;
		eh = (bh + 31u) & ~31u;
		if ((uint64_t)ew * eh * bpe > dataLen) {
			free(rgba);
			return NULL;
		}
		for (uint32_t by = 0; by < bh; by++) {
			for (uint32_t bx = 0; bx < bw; bx++) {
				uint32_t e = tiledOffset(bx + ox, by + oy, ew, log2u(bpe));
				if ((uint64_t)e * bpe + bpe > dataLen) {
					free(rgba);
					return NULL;
				}
				int remainW = (int)w - (int)bx * 4;
				int remainH = (int)h - (int)by * 4;
				dxtBlock(data + (size_t)e * bpe, kind,
						rgba + ((size_t)by * 4 * w + bx * 4) * 4, stride,
						remainW < 4 ? remainW : 4, remainH < 4 ? remainH : 4);
			}
		}
	}
	return rgba;
}

static uint32_t be32b(const uint8_t *p)
{
	return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

static uint32_t be16b(const uint8_t *p)
{
	return ((uint32_t)p[0] << 8) | p[1];
}

void gevrBeanTexReset(void)
{
	for (int i = 0; i < s_ntex; i++) {
		videoFreeCachedTexture(s_tex[i].standin);
		free(s_tex[i].standin);
		free(s_tex[i].rgba);
		s_tex[i].standin = NULL;
		s_tex[i].rgba = NULL;
		s_tex[i].key[0] = 0;
	}
	s_ntex = 0;
}

int gevrBeanTexture(const void *addr, const uint8_t **rgba, int *w, int *h)
{
	if (!addr) {
		return 0;
	}
	for (int i = 0; i < s_ntex; i++) {
		if (s_tex[i].standin == addr) {
			*rgba = s_tex[i].rgba;
			*w = s_tex[i].w;
			*h = s_tex[i].h;
			return s_tex[i].rgba != NULL;
		}
	}
	return 0;
}

/* DAB's placeholder test: at least 90% magenta, no larger than 16x16. */
int gevrBeanTexIsPlaceholder(const void *addr)
{
	const uint8_t *rgba;
	int w, h, magenta = 0;
	if (!gevrBeanTexture(addr, &rgba, &w, &h) || w > 16 || h > 16) return 0;
	for (int i = 0; i < w * h; i++) {
		const uint8_t *p = rgba + i * 4;
		magenta += p[0] >= p[1] + 64 && p[2] >= p[1] + 64 && abs(p[0] - p[2]) < 48;
	}
	return magenta * 10 >= w * h * 9;
}

void gevrBeanTexPaint(const void *addr, uint32_t rgb)
{
	for (int i = 0; i < s_ntex; i++)
		if (s_tex[i].standin == addr) {
			for (int p = 0; p < s_tex[i].w * s_tex[i].h; p++)
				for (int k = 0; k < 3; k++)
					s_tex[i].rgba[p * 4 + k] = (uint8_t)(rgb >> (16 - k * 8));
			return;
		}
}

/* Decode texture file `fileIndex` inside an open CAFF. The gpu bytes are copied. */
const void *gevrBeanTexDecode(const struct gevrCaff *c, int fileIndex, const char *key,
		int *outW, int *outH, int *outAlpha)
{
	uint32_t blen = 0, glen = 0, w, h, base, levels, type;
	const uint8_t *b;
	const uint8_t *g;
	int gi, format, endian, packed;
	uint32_t bpe, ew, eh, need;
	uint8_t *copy, *rgba;
	struct texSlot *slot;

	if (outW) *outW = 0;
	if (outH) *outH = 0;
	if (outAlpha) *outAlpha = 0;
	if (!c || !key) {
		return NULL;
	}
	for (int i = 0; i < s_ntex; i++) {
		if (strcmp(s_tex[i].key, key) == 0) {
			if (outW) *outW = s_tex[i].w;
			if (outH) *outH = s_tex[i].h;
			if (outAlpha && s_tex[i].rgba) {
				for (int p = 3; p < s_tex[i].w * s_tex[i].h * 4; p += 4) {
					if (s_tex[i].rgba[p] != 255) {
						*outAlpha = 1;
						break;
					}
				}
			}
			return s_tex[i].standin;
		}
	}
	if (s_ntex >= TEX_SLOTS) {
		return NULL;
	}
	b = gevrCaffBlob(c, fileIndex, &blen);
	if (!b || blen < 0x40 || memcmp(b, "texture", 7) != 0) {
		return NULL;
	}
	w = be16b(b + 0x24);
	h = be16b(b + 0x26);
	base = be32b(b + 0x28);
	levels = b[0x30];
	type = be32b(b + 0x1c);
	format = b[0x1b] & 0x3f;
	endian = b[0x1a] ? (format == FMT_8888 ? 2 : 1) : 0;
	packed = levels > 1;
	if (type == 4 && blen >= 0x50) {
		uint32_t wt = be32b(b + 0x44), ht = be32b(b + 0x48), lt = be32b(b + 0x4c);
		if (wt > blen - 4 || ht > blen - 4 || lt > blen - 4)
			return NULL;
		w = be32b(b + wt);
		h = be32b(b + ht);
		levels = be32b(b + lt);
		packed = levels > 1;
	}
	gi = gevrCaffFind(c, c->files[fileIndex].asset, ".gpu");
	if (gi >= 0) {
		base = 0;
	} else {
		gi = -1;
		for (uint32_t i = 0; i < c->numfiles; i++) {
			if (strcmp(gevrCaffAssetName(c, c->files[i].asset), "texture pairs") == 0
					&& strncmp(gevrCaffSectName(c, (int)i), ".gpu", 4) == 0) {
				gi = (int)i;
				break;
			}
		}
	}
	g = gi >= 0 ? gevrCaffBlob(c, gi, &glen) : NULL;
	if (!g || w == 0 || h == 0 || w > 4096 || h > 4096 || base > glen) {
		return NULL;
	}
	if (format == FMT_8888) {
		bpe = 4;
		ew = (w + 31u) & ~31u;
		eh = (h + 31u) & ~31u;
	} else if (format == FMT_DXT1 || format == FMT_DXT3 || format == FMT_DXT5) {
		bpe = format == FMT_DXT1 ? 8 : 16;
		ew = (((w + 3) / 4) + 31u) & ~31u;
		eh = (((h + 3) / 4) + 31u) & ~31u;
	} else {
		return NULL;
	}
	need = ew * eh * bpe;
	copy = calloc(need ? need : 1, 1);
	if (!copy) {
		return NULL;
	}
	{
		uint32_t have = glen - base;
		memcpy(copy, g + base, have < need ? have : need);
	}
	endianSwap(copy, need, endian);
	rgba = decodeSurface(copy, need, format, packed, w, h);
	free(copy);
	if (!rgba) {
		return NULL;
	}
	/* DAB's mesh V reversal requires its decoded bottom-row-first images.
	 * Without both steps every CAFF picture is mirrored vertically. */
	for (uint32_t y = 0; y < h / 2; y++)
		for (uint32_t x = 0; x < w * 4; x++) {
			uint8_t *a = rgba + (size_t)y * w * 4 + x;
			uint8_t *b = rgba + (size_t)(h - 1 - y) * w * 4 + x;
			uint8_t tmp = *a; *a = *b; *b = tmp;
		}
	if (outAlpha) {
		*outAlpha = 0;
		for (uint32_t i = 0; i < w * h; i++) {
			if (rgba[i * 4 + 3] != 255) {
				*outAlpha = 1;
				break;
			}
		}
	}
	slot = &s_tex[s_ntex++];
	snprintf(slot->key, sizeof(slot->key), "%s", key);
	slot->rgba = rgba;
	slot->w = (int)w;
	slot->h = (int)h;
	slot->standin = calloc(16 * 16, 2);
	if (!slot->standin) {
		s_ntex--;
		free(rgba);
		return NULL;
	}
	if (outW) *outW = (int)w;
	if (outH) *outH = (int)h;
	return slot->standin;
}
