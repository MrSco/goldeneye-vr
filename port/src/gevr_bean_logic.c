#include "gevr_bean_logic.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

static uint32_t be32(const uint8_t *p)
{
	return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

static uint32_t le32(const uint8_t *p)
{
	return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static int fits(uint64_t ofs, uint64_t n, uint64_t size)
{
	return n <= size && ofs <= size - n;
}

static uint32_t hdr32(const struct gevrCaff *c, uint64_t o)
{
	const uint8_t *p;

	if (!fits(o, 4, c->len)) {
		return 0;
	}
	p = c->d + o;
	return c->be ? be32(p) : le32(p);
}

int gevrCaffOpen(struct gevrCaff *c, const uint8_t *d, uint32_t len)
{
	uint32_t stroff[GEVR_BEAN_MAXSECT];
	uint64_t pos, data;
	uint32_t hsize, namelen, sectsize, filesize, total, adb;

	memset(c, 0, sizeof(*c));
	c->d = d;
	c->len = len;
	if (len < 0x68 || memcmp(d, "CAFF", 4) != 0 || d[0x4a] != 0) {
		return 0;
	}
	c->be = d[0x48] == 1;
	hsize = hdr32(c, 0x14);
	c->numassets = hdr32(c, 0x1c);
	c->numfiles = hdr32(c, 0x20);
	c->numsects = d[0x49];
	namelen = hdr32(c, 0x4c);
	sectsize = hdr32(c, 0x50);
	filesize = hdr32(c, 0x64);
	if (c->numsects > GEVR_BEAN_MAXSECT || c->numassets > 0x10000 || c->numfiles > 0x10000) {
		return 0;
	}
	pos = hsize;
	if (!fits(pos, 0x21ull * c->numsects, len)) {
		return 0;
	}
	for (uint32_t i = 0; i < c->numsects; i++) {
		stroff[i] = hdr32(c, pos);
		c->sects[i].size = hdr32(c, pos + 9);
		pos += 0x21;
	}
	for (uint32_t i = 0; i < c->numsects; i++) {
		uint64_t at = pos + stroff[i];
		for (int j = 0; j < 31 && at + (uint64_t)j < len && d[at + (uint64_t)j]; j++) {
			c->sects[i].name[j] = (char)d[at + (uint64_t)j];
		}
	}
	pos += namelen;
	total = hdr32(c, pos);
	c->nametable = (uint32_t)(pos + 4);
	c->labels = (uint32_t)(pos + 4 + 4ull * c->numassets);
	pos = (uint64_t)c->labels + total;
	adb = hdr32(c, pos);
	pos += 4ull + adb;
	if (!fits(pos, 14ull * c->numfiles, len)) {
		return 0;
	}
	c->files = calloc(c->numfiles ? c->numfiles : 1, sizeof(*c->files));
	if (!c->files) {
		return 0;
	}
	for (uint32_t i = 0; i < c->numfiles; i++) {
		c->files[i].asset = hdr32(c, pos);
		c->files[i].start = hdr32(c, pos + 4);
		c->files[i].size = hdr32(c, pos + 8);
		c->files[i].sect = d[pos + 12];
		pos += 14;
	}
	data = (uint64_t)hsize + sectsize + filesize;
	for (uint32_t i = 0; i < c->numsects; i++) {
		c->sects[i].offset = data;
		data += c->sects[i].size;
	}
	return 1;
}

void gevrCaffClose(struct gevrCaff *c)
{
	free(c->files);
	c->files = NULL;
}

const uint8_t *gevrCaffBlob(const struct gevrCaff *c, int index, uint32_t *outLen)
{
	const struct gevrCaffFile *f;
	uint64_t at;

	if (index < 0 || (uint32_t)index >= c->numfiles) {
		return NULL;
	}
	f = &c->files[index];
	if (f->sect == 0 || f->sect > c->numsects) {
		return NULL;
	}
	at = c->sects[f->sect - 1].offset + f->start;
	if (!fits(at, f->size, c->len)) {
		return NULL;
	}
	*outLen = f->size;
	return c->d + at;
}

const char *gevrCaffSectName(const struct gevrCaff *c, int index)
{
	uint8_t sect = c->files[index].sect;
	return sect && sect <= c->numsects ? c->sects[sect - 1].name : "";
}

const char *gevrCaffAssetName(const struct gevrCaff *c, uint32_t asset)
{
	uint32_t off;
	uint64_t at;
	const char *name;
	const char *slash;

	if (asset == 0 || asset > c->numassets) {
		return "";
	}
	off = hdr32(c, c->nametable + 4ull * (asset - 1));
	at = (uint64_t)c->labels + off;
	if (at >= c->len || !memchr(c->d + at, 0, c->len - at)) {
		return "";
	}
	name = (const char *)c->d + at;
	slash = strrchr(name, '\\');
	return slash ? slash + 1 : name;
}

int gevrCaffFind(const struct gevrCaff *c, uint32_t asset, const char *prefix)
{
	size_t n = strlen(prefix);
	for (uint32_t i = 0; i < c->numfiles; i++) {
		if (c->files[i].asset == asset && strncmp(gevrCaffSectName(c, (int)i), prefix, n) == 0) {
			return (int)i;
		}
	}
	return -1;
}

static uint32_t indexAt(const struct gevrBeanTriIn *in, uint32_t i)
{
	if ((uint64_t)(i + 1) * in->width > in->idxBytes) {
		return 0xffffffffu;
	}
	if (in->width == 4) {
		return be32(in->idx + i * 4);
	}
	return ((uint32_t)in->idx[i * 2] << 8) | in->idx[i * 2 + 1];
}

int gevrBeanTriangles(const struct gevrBeanTriIn *in, uint32_t **out)
{
	uint32_t *tris;
	int n = 0;

	*out = NULL;
	if (!in->idx || in->count < 3 || (in->width != 2 && in->width != 4)) {
		return 0;
	}
	if ((uint64_t)in->count * in->width > in->idxBytes) {
		return 0;
	}
	tris = malloc(sizeof(uint32_t) * 3 * ((size_t)in->count * 2));
	if (!tris) {
		return 0;
	}
	if (in->prim == 4) {
		for (uint32_t i = 0; i + 2 < in->count; i += 3, n++) {
			tris[n * 3] = indexAt(in, i);
			tris[n * 3 + 1] = indexAt(in, i + 1);
			tris[n * 3 + 2] = indexAt(in, i + 2);
		}
	} else if (in->prim == 13) {
		for (uint32_t i = 0; i + 3 < in->count; i += 4) {
			uint32_t a = indexAt(in, i);
			uint32_t b = indexAt(in, i + 1);
			uint32_t c = indexAt(in, i + 2);
			uint32_t e = indexAt(in, i + 3);
			tris[n * 3] = a; tris[n * 3 + 1] = b; tris[n * 3 + 2] = c; n++;
			tris[n * 3] = a; tris[n * 3 + 1] = c; tris[n * 3 + 2] = e; n++;
		}
	} else if (in->prim == 5) {
		/* Xenos 5 is a fan. Direct3D's 5 is the strip; reading it that way
		 * left the middle out of Dam's bungee platform. */
		for (uint32_t i = 0; i + 2 < in->count; i++, n++) {
			tris[n * 3] = indexAt(in, 0);
			tris[n * 3 + 1] = indexAt(in, i + 1);
			tris[n * 3 + 2] = indexAt(in, i + 2);
		}
	} else if (in->prim == 6) {
		for (uint32_t i = 0; i + 2 < in->count; i++, n++) {
			if (i & 1) {
				tris[n * 3] = indexAt(in, i + 1);
				tris[n * 3 + 1] = indexAt(in, i);
				tris[n * 3 + 2] = indexAt(in, i + 2);
			} else {
				tris[n * 3] = indexAt(in, i);
				tris[n * 3 + 1] = indexAt(in, i + 1);
				tris[n * 3 + 2] = indexAt(in, i + 2);
			}
		}
	}
	if (n == 0) {
		free(tris);
		return 0;
	}
	*out = tris;
	return n;
}

static uint32_t log2ceil(uint32_t v)
{
	uint32_t n = 0;
	while ((1u << n) < v && n < 31) {
		n++;
	}
	return n;
}

void gevrBeanPackedMip(uint32_t width, uint32_t height, uint32_t blockw, int packed,
		uint32_t *outX, uint32_t *outY)
{
	uint32_t logw, logh;

	*outX = 0;
	*outY = 0;
	if (!packed || blockw == 0) {
		return;
	}
	logw = log2ceil(width);
	logh = log2ceil(height);
	if (logw > 4 && logh > 4) {
		return;
	}
	if (logw > logh) {
		*outY = 16 / blockw;
	} else {
		*outX = 16 / blockw;
	}
}

static int pow2scale(float f, float *out)
{
	float r, p;
	if (!(f > 1.0f / 1048576.0f && f < 1.0f / 32.0f)) {
		return 0;
	}
	r = 1.0f / f;
	p = exp2f(roundf(log2f(r)));
	if (fabsf(r / p - 1.0f) > 0.001f) {
		return 0;
	}
	*out = p;
	return 1;
}

float gevrBeanShaderUvScale(const uint8_t *gpu, uint32_t gpulen, uint32_t buffersEnd)
{
	float found = 0.0f;
	if (!gpu || buffersEnd == 0) {
		return 0.0f;
	}
	for (uint32_t at = (buffersEnd + 3u) & ~3u; at + 4 <= gpulen; at += 4) {
		uint32_t w = be32(gpu + at);
		float f, p;
		memcpy(&f, &w, sizeof(f));
		if (!pow2scale(f, &p)) {
			continue;
		}
		if (found > 0.0f && p != found) {
			return 0.0f;
		}
		found = p;
	}
	return found;
}

int gevrBeanShaderUv20(const uint8_t *gpu, uint32_t gpulen, uint32_t buffersEnd)
{
	int uv = 0;
	if (!gpu || buffersEnd == 0) {
		return 0;
	}
	for (uint32_t at = (buffersEnd + 3u) & ~3u; at + 12 <= gpulen; at += 4) {
		uint32_t w0 = be32(gpu + at);
		uint32_t fmt = (be32(gpu + at + 4) >> 16) & 63;
		if ((w0 & 31) != 0 || !((w0 >> 19) & 1) || (be32(gpu + at + 8) & 0x7fffffffu) != (4u << 8 | 5u)) {
			continue;
		}
		if (fmt == 6) {
			return 0;
		}
		uv |= fmt == 25;
	}
	return uv;
}

int gevrBeanLogicSelfTest(void)
{
	uint8_t idx[16];
	struct gevrBeanTriIn in;
	uint32_t *tris = NULL;
	int n;
	uint32_t ox, oy;
	uint8_t gpu[16];
	float scale;

	for (int i = 0; i < 8; i++) {
		idx[i * 2] = 0;
		idx[i * 2 + 1] = (uint8_t)i;
	}
	memset(&in, 0, sizeof(in));
	in.idx = idx;
	in.idxBytes = sizeof(idx);
	in.width = 2;
	in.prim = 5;
	in.count = 5;
	n = gevrBeanTriangles(&in, &tris);
	if (n != 3 || tris[0] != 0 || tris[1] != 1 || tris[2] != 2 || tris[6] != 0 || tris[7] != 3 || tris[8] != 4) {
		free(tris);
		return 1;
	}
	free(tris);
	in.prim = 6;
	in.count = 4;
	n = gevrBeanTriangles(&in, &tris);
	if (n != 2 || tris[0] != 0 || tris[3] != 2 || tris[4] != 1 || tris[5] != 3) {
		free(tris);
		return 2;
	}
	free(tris);
	in.prim = 4;
	in.count = 6;
	n = gevrBeanTriangles(&in, &tris);
	if (n != 2 || tris[3] != 3) {
		free(tris);
		return 3;
	}
	free(tris);
	in.prim = 13;
	in.count = 4;
	n = gevrBeanTriangles(&in, &tris);
	if (n != 2 || tris[2] != 2 || tris[5] != 3) {
		free(tris);
		return 4;
	}
	free(tris);

	gevrBeanPackedMip(16, 16, 4, 1, &ox, &oy);
	if (ox != 4 || oy != 0) {
		return 5;
	}
	gevrBeanPackedMip(32, 8, 4, 1, &ox, &oy);
	if (ox != 0 || oy != 4) {
		return 6;
	}
	gevrBeanPackedMip(16, 16, 4, 0, &ox, &oy);
	if (ox != 0 || oy != 0) {
		return 7;
	}
	gevrBeanPackedMip(64, 64, 4, 1, &ox, &oy);
	if (ox != 0 || oy != 0) {
		return 8;
	}

	/* 1/128 as a big-endian float, past a 4-byte buffer. */
	gpu[0] = gpu[1] = gpu[2] = gpu[3] = 0;
	gpu[4] = 0x3c;
	gpu[5] = gpu[6] = gpu[7] = 0;
	scale = gevrBeanShaderUvScale(gpu, 8, 4);
	if (scale != 128.0f) {
		return 9;
	}
	gpu[8] = 0x3b;
	gpu[9] = 0x80;
	gpu[10] = gpu[11] = 0; /* 1/256, disagrees */
	scale = gevrBeanShaderUvScale(gpu, 12, 4);
	if (scale != 0.0f) {
		return 10;
	}
	return 0;
}
