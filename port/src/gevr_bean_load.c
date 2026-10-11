/*
 * Project Bean CAFF -> triangles. Characters, guns, props, rooms and skies
 * all come through here. The display lists are built by gevr_bean.c.
 */

#include "gevr_bean_int.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_DRAWS 8192
#define MAX_BONES 64
#define MAX_IBS   4096
#define MAX_INST  256
#define MAX_TEX   256
#define MAX_TRIS  500000

struct beanDraw {
	uint32_t vb, tex, prim, count, ib;
	int16_t inst;
	uint8_t blend, alpha, alphatest;
	uint8_t pal[GEVR_BEAN_MAXPAL];
	uint8_t numpal;
	uint32_t colour;
	int8_t cuff;
};

struct beanIb {
	uint32_t obj, off, size, width;
};

struct beanVb {
	uint32_t stride, off, count;
	int col28, uv20;
};

static uint32_t be32(const uint8_t *p)
{
	return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

static uint32_t be16(const uint8_t *p)
{
	return ((uint32_t)p[0] << 8) | p[1];
}

static float bef(const uint8_t *p)
{
	uint32_t w = be32(p);
	float f;
	memcpy(&f, &w, sizeof(f));
	return f;
}

static int fits(uint64_t ofs, uint64_t n, uint64_t size)
{
	return n <= size && ofs <= size - n;
}

static const char *const s_skelName[GEVR_BEAN_SK_COUNT] = {
	"SKEL_BASE", "SKEL_BACK", "SKEL_NECK", "SKEL_POSITION",
	"SKEL_LF_SHOULDER", "SKEL_LF_ELBOW", "SKEL_LF_WRIST",
	"SKEL_RT_SHOULDER", "SKEL_RT_ELBOW", "SKEL_RT_WRIST",
	"SKEL_LF_HIP", "SKEL_LF_KNEE", "SKEL_LF_ANKLE",
	"SKEL_RT_HIP", "SKEL_RT_KNEE", "SKEL_RT_ANKLE"
};

static int boneIs(const char *name, const char *joint)
{
	size_t n = strlen(joint);
	if (strncmp(name, joint, n) != 0) {
		return 0;
	}
	if (name[n] == '\0') {
		return 1;
	}
	if (name[n] != '_') {
		return 0;
	}
	for (const char *c = name + n; *c; c++) {
		if (*c != '_' && *c != 'W' && *c != 'P' && *c != 'R' && *c != 'E') {
			return 0;
		}
	}
	return 1;
}

static uint32_t colourArgb(uint32_t abgr)
{
	return (abgr & 0xff00ff00u) | ((abgr >> 16) & 0xffu) | ((abgr & 0xffu) << 16);
}

static void unpackNormal(uint32_t w, float *o)
{
	for (int i = 0; i < 3; i++) {
		int v = (int)((w >> (10 * i)) & 1023u);
		if (v & 512) {
			v -= 1024;
		}
		o[i] = v / 511.0f;
	}
}

static int readVb(const uint8_t *data, uint32_t dlen, const uint8_t *gpu, uint32_t glen,
		uint32_t desc, int uv20file, struct beanVb *vb)
{
	uint32_t size;
	memset(vb, 0, sizeof(*vb));
	if (!fits(desc, 16, dlen)) {
		return 0;
	}
	vb->stride = be32(data + desc);
	vb->off = be32(data + desc + 8);
	size = be32(data + desc + 12);
	if ((vb->stride < 12) || (vb->stride != 12 && vb->stride < 16) || !fits(vb->off, size, glen) || vb->stride == 0) {
		return 0;
	}
	if (vb->stride != 12 && vb->stride != 16 && vb->stride != 20 && vb->stride != 24
			&& vb->stride != 28 && vb->stride != 32 && vb->stride != 36) {
		return 0;
	}
	vb->count = size / vb->stride;
	vb->uv20 = vb->stride == 20 && uv20file;
	if (vb->stride == 28) {
		vb->col28 = 1;
		for (uint32_t i = 0; i < vb->count; i++) {
			if (gpu[vb->off + i * 28 + 24] != 0xff) {
				vb->col28 = 0;
				break;
			}
		}
	}
	return 1;
}

struct beanVtx {
	float pos[3];
	float nrm[3];
	float uv[2];
	int slot[4];
	uint8_t weight[4];
	uint32_t argb;
	int hasuv;
};

static int readVtx(const uint8_t *gpu, const struct beanVb *vb, uint32_t i, float uvscale, int skinned, struct beanVtx *v)
{
	const uint8_t *p;
	uint32_t k;
	if (i >= vb->count) {
		return 0;
	}
	p = gpu + vb->off + i * vb->stride;
	memset(v, 0, sizeof(*v));
	v->weight[0] = 255;
	v->argb = 0xffffffffu;
	v->hasuv = 1;
	for (int j = 0; j < 4; j++) {
		v->slot[j] = -1;
	}
	/* The rigid stride-36 shader fetches FLOAT3 at dword 1, normal at
	 * 4, diffuse UV at 5 and colour at 8. Dword 0 is another UV set.
	 * Skinned stride-36 has a different layout (position at dword 0). */
	uint32_t posoff = !skinned && vb->stride == 36 ? 4 : 0;
	for (int j = 0; j < 3; j++) {
		v->pos[j] = bef(p + posoff + j * 4);
		if (!isfinite(v->pos[j])) return 0;
	}
	if (!skinned && vb->stride >= 28) {
		k = posoff + 12;
		v->argb = colourArgb(be32(p + (vb->stride == 36 ? 32 : 20)));
		unpackNormal(be32(p + k), v->nrm);
		v->uv[0] = (int16_t)be16(p + k + 4) / uvscale;
		v->uv[1] = (int16_t)be16(p + k + 6) / uvscale;
		return 1;
	}
	switch (vb->stride) {
	case 28:
	case 32:
	case 36:
		for (int j = 0; j < 4; j++) {
			uint32_t s = be16(p + 12 + j * 2);
			v->slot[j] = (s == 0xf000 || s / 3 >= GEVR_BEAN_MAXPAL) ? -1 : (int)(s / 3);
		}
		if (vb->stride == 36) {
			for (int j = 0; j < 4; j++) {
				v->weight[j] = p[23 - j];
			}
			k = 24;
		} else {
			k = 20;
		}
		v->hasuv = !(vb->stride == 28 && vb->col28);
		if (vb->stride != 28 || vb->col28) {
			v->argb = colourArgb(be32(p + vb->stride - 4));
		}
		break;
	case 20:
		k = 12;
		v->hasuv = vb->uv20;
		if (!vb->uv20) {
			v->argb = colourArgb(be32(p + 16));
		}
		break;
	case 16:
		k = 12;
		v->hasuv = 0;
		break;
	case 24:
		k = 12;
		v->argb = colourArgb(be32(p + 20));
		break;
	case 12:
		return 1;
	default:
		return 0;
	}
	unpackNormal(be32(p + k), v->nrm);
	if (v->hasuv && uvscale > 0.0f) {
		v->uv[0] = (int16_t)be16(p + k + 4) / uvscale;
		v->uv[1] = (int16_t)be16(p + k + 6) / uvscale;
	}
	return 1;
}

static void applyInst(float *p, const float *m)
{
	float x = p[0], y = p[1], z = p[2];
	p[0] = x * m[0] + y * m[4] + z * m[8] + m[12];
	p[1] = x * m[1] + y * m[5] + z * m[9] + m[13];
	p[2] = x * m[2] + y * m[6] + z * m[10] + m[14];
}

static void applyInstN(float *n, const float *m)
{
	float x = n[0], y = n[1], z = n[2];
	n[0] = x * m[0] + y * m[4] + z * m[8];
	n[1] = x * m[1] + y * m[5] + z * m[9];
	n[2] = x * m[2] + y * m[6] + z * m[10];
}

static uint32_t texArea(const struct gevrCaff *c, const int *texfile, int ntex, uint32_t t)
{
	uint32_t len = 0;
	const uint8_t *b;
	if (t >= (uint32_t)ntex) {
		return 0;
	}
	b = gevrCaffBlob(c, texfile[t], &len);
	if (!b || len < 0x28) {
		return 0;
	}
	return (uint32_t)be16(b + 0x24) * be16(b + 0x26);
}

/* Surface-map classification follows DAB's importer; see port/licenses/dabs-mod.txt. */
static int reflectionTex(const struct gevrCaff *c, const int *texfile, int ntex, uint32_t t)
{
	static const char *const names[] = {
	    "_0x059B9F65.tga.bin", "_0x00B5FD45.bin",     "_0x00B5FD45.bmp.bin", "_0x03B30B95.tga.bin",
	    "_0x018952E5.tga.bin", "_0x0F0BCEB5.tga.bin", "_0x0E1C2BF5.tga.bin", "_0x081346B5.tga.bin",
	    "_0x0B9D6195.tga.bin", "_0x0C2BCE25.tga.bin", "_0x018248E5.tga.bin",
	};
	const char *name;
	if (t >= (uint32_t)ntex)
		return 1;
	name = gevrCaffAssetName(c, c->files[texfile[t]].asset);
	for (unsigned i = 0; name && i < sizeof(names) / sizeof(names[0]); i++)
		if (!strcmp(name, names[i]))
			return 1;
	return 0;
}

static uint32_t materialTex(const struct gevrCaff *c, const uint8_t *st, uint32_t pc, uint32_t size,
		uint32_t len, const int *texfile, int ntex)
{
	static const uint16_t samplers[4] = { 0, 0, 1, 2 };
	uint32_t count = size >= 20 ? (size - 12) / 8 : 0;
	uint32_t value[4] = { 0, 0, 0, 0 };
	uint32_t best = 0, bestarea = 0, read = 0;
	int found = 0, geshape = count == 4;
	for (uint32_t k = 0; k < count && fits(pc + 12 + 8 * k, 8, len) && 12 + 8 * k + 8 <= size; k++) {
		uint32_t t = be32(st + pc + 12 + 8 * k);
		uint32_t where = be32(st + pc + 16 + 8 * k);
		uint32_t area = texArea(c, texfile, ntex, t);
		read = k + 1;
		if (k < 4) {
			value[k] = t;
			if ((where >> 16) != k || (k < 3 ? (where & 0xffff) != samplers[k] : (where & 0xffff) < samplers[k])) {
				geshape = 0;
			}
		}
		if (!found || (!reflectionTex(c, texfile, ntex, t) && reflectionTex(c, texfile, ntex, best)) ||
		    (reflectionTex(c, texfile, ntex, t) == reflectionTex(c, texfile, ntex, best) &&
		     area >= bestarea)) {
			found = 1;
			best = t;
			bestarea = area;
		}
	}
	if (geshape && read == 4 && value[3] < (uint32_t)ntex) {
		return value[3];
	}
	return found ? best : 0;
}

static int pushTri(struct gevrBeanMesh *mesh, const struct beanVtx *a, const struct beanVtx *b,
		const struct beanVtx *c, int tex, int alpha, int alphatest, int blend, const uint8_t *pal,
		int numpal, const int *boneSk, const uint8_t *muzzle, const uint8_t *slide)
{
	struct gevrBeanTri *t;
	const struct beanVtx *corner[3] = { a, b, c };
	int sk = 0, bestw = -1, flags = 0, named = 0, flash = 1, slideAll = 1;
	if (mesh->ntris >= MAX_TRIS) {
		return 0;
	}
	if (mesh->ntris >= mesh->tricaps) {
		int cap = mesh->tricaps ? mesh->tricaps * 2 : 1024;
		struct gevrBeanTri *grown;
		if (cap > MAX_TRIS) {
			cap = MAX_TRIS;
		}
		if (cap <= mesh->ntris) {
			return 0;
		}
		grown = realloc(mesh->tris, (size_t)cap * sizeof(*grown));
		if (!grown) {
			return 0;
		}
		mesh->tris = grown;
		mesh->tricaps = cap;
	}
	for (int k = 0; k < numpal; k++) {
		int bone = pal[k];
		if (bone < 0 || bone >= MAX_BONES) {
			continue;
		}
		named++;
		if (!muzzle[bone]) {
			flash = 0;
		}
		if (!slide[bone]) {
			slideAll = 0;
		}
	}
	if (named == 0) {
		flash = 0;
		slideAll = 0;
	}
	t = &mesh->tris[mesh->ntris++];
	memset(t, 0, sizeof(*t));
	for (int cnr = 0; cnr < 3; cnr++) {
		const struct beanVtx *v = corner[cnr];
		int dom = 0, w = -1, bone;
		for (int s = 0; s < 4; s++) {
			int slot = v->slot[s];
			int b = slot >= 0 && slot < numpal ? pal[slot] : -1;
			t->bone[cnr * 4 + s] = b >= 0 && b < MAX_BONES ? b : 255;
			t->weight[cnr * 4 + s] = b >= 0 && b < MAX_BONES ? v->weight[s] : 0;
		}
		for (int s = 0; s < 4; s++) {
			if (v->slot[s] >= 0 && (int)v->weight[s] > w) {
				w = v->weight[s];
				dom = v->slot[s];
			}
		}
		bone = (w >= 0 && dom < numpal) ? pal[dom] : -1;
		if (bone >= 0 && bone < MAX_BONES && w >= bestw) {
			bestw = w;
			sk = boneSk[bone];
		}
		for (int j = 0; j < 3; j++) {
			t->p[cnr * 3 + j] = v->pos[j];
			t->n[cnr * 3 + j] = v->nrm[j];
		}
		t->uv[cnr * 2] = v->uv[0];
		t->uv[cnr * 2 + 1] = v->uv[1];
		t->rgba[cnr * 4] = (uint8_t)((v->argb >> 16) & 255);
		t->rgba[cnr * 4 + 1] = (uint8_t)((v->argb >> 8) & 255);
		t->rgba[cnr * 4 + 2] = (uint8_t)(v->argb & 255);
		t->rgba[cnr * 4 + 3] = (uint8_t)((v->argb >> 24) & 255);
	}
	t->tex = (int16_t)tex;
	t->alpha = (uint8_t)alpha;
	t->alphatest = (uint8_t)alphatest;
	if (blend && t->alpha == 255) {
		t->alpha = 254;
	}
	t->sk = (uint8_t)(sk < 0 ? 0 : sk);
	if (flash) {
		flags |= GEVR_BEAN_F_FLASH;
	}
	if (slideAll) {
		flags |= GEVR_BEAN_F_SLIDE;
	}
	if (t->sk == GEVR_BEAN_SK_NECK) {
		flags |= GEVR_BEAN_F_NECK;
	}
	t->flags = (uint8_t)flags;
	return 1;
}

static float levelUv(const uint8_t *gpu, uint32_t len, const char *source)
{
	int n128 = 0, n256 = 0;
	if (source && strstr(source, "silo")) {
		return 512.0f;
	}
	for (uint32_t at = 0; gpu && at + 4 <= len; at += 4) {
		uint32_t w = be32(gpu + at);
		if (w == 0x3c000000u) {
			n128++;
		} else if (w == 0x3b800000u) {
			n256++;
		}
	}
	if (n128 >= 3 && n128 > n256) {
		return 128.0f;
	}
	if (n256 >= 3 && n256 > n128) {
		return 256.0f;
	}
	return 1024.0f;
}

static float measureUv(const uint8_t *data, uint32_t dlen, const uint8_t *gpu, uint32_t glen,
		const struct beanDraw *draws, int ndraws, const struct beanIb *ibs, int nibs,
		int uv20, int skinned, const struct gevrCaff *c, const int *texfile, int ntex)
{
	uint32_t biggest[MAX_TEX];
	uint8_t drawn[MAX_TEX];
	uint32_t sorted[MAX_TEX];
	int n = 0;
	uint32_t scale;
	memset(biggest, 0, sizeof(biggest));
	memset(drawn, 0, sizeof(drawn));
	for (int di = 0; di < ndraws; di++) {
		const struct beanDraw *d = &draws[di];
		struct beanVb vb;
		uint32_t uvo, *tris = NULL;
		const struct beanIb *ib = NULL;
		struct gevrBeanTriIn in;
		int ntris;
		if (d->tex >= MAX_TEX || !readVb(data, dlen, gpu, glen, d->vb, uv20, &vb)) {
			continue;
		}
		if ((int)d->tex < ntex) {
			uint32_t blen = 0;
			const uint8_t *b = gevrCaffBlob(c, texfile[d->tex], &blen);
			if (b && blen >= 0x28 && (uint32_t)be16(b + 0x24) * be16(b + 0x26) <= 4) {
				continue;
			}
		}
		switch (vb.stride) {
		case 36: uvo = 28; break;
		case 32: case 28: uvo = 24; break;
		case 24: case 20: uvo = 16; break;
		default: continue;
		}
		if (!skinned && vb.stride >= 28) uvo = vb.stride == 36 ? 20 : 16;
		if ((vb.stride == 20 && !vb.uv20) || (vb.stride == 28 && vb.col28)) {
			continue;
		}
		for (int i = 0; i < nibs; i++) {
			if (ibs[i].obj == d->ib) {
				ib = &ibs[i];
				break;
			}
		}
		if (!ib) {
			continue;
		}
		memset(&in, 0, sizeof(in));
		in.prim = d->prim;
		in.count = d->count;
		in.idx = gpu + ib->off;
		in.idxBytes = ib->size;
		in.width = ib->width;
		ntris = gevrBeanTriangles(&in, &tris);
		for (int i = 0; i < ntris * 3; i++) {
			const uint8_t *p;
			int u, w;
			if (tris[i] >= vb.count) {
				continue;
			}
			p = gpu + vb.off + tris[i] * vb.stride + uvo;
			u = (int16_t)be16(p);
			w = (int16_t)be16(p + 2);
			if (u < 0) u = -u;
			if (w < 0) w = -w;
			if ((uint32_t)u > biggest[d->tex]) biggest[d->tex] = (uint32_t)u;
			if ((uint32_t)w > biggest[d->tex]) biggest[d->tex] = (uint32_t)w;
		}
		if (ntris > 0) {
			drawn[d->tex] = 1;
		}
		free(tris);
	}
	for (int t = 0; t < MAX_TEX; t++) {
		if (!drawn[t]) {
			continue;
		}
		int at = n++;
		while (at > 0 && sorted[at - 1] > biggest[t]) {
			sorted[at] = sorted[at - 1];
			at--;
		}
		sorted[at] = biggest[t];
	}
	if (n == 0) {
		return 32768.0f;
	}
	for (scale = 1024; scale < 32768 && scale * 1.25f < sorted[(n - 1) / 2]; scale *= 2) {
	}
	return (float)scale;
}

static void markDecals(struct gevrBeanMesh *mesh)
{
	enum { BUCKETS = 65536 };
	int *head = calloc(BUCKETS, sizeof(int));
	int *next = mesh->ntris ? malloc((size_t)mesh->ntris * sizeof(int)) : NULL;
	if (!head || (mesh->ntris && !next)) {
		free(head);
		free(next);
		return;
	}
	for (int i = 0; i < BUCKETS; i++) {
		head[i] = -1;
	}
	for (int i = 0; i < mesh->ntris; i++) {
		float *p = mesh->tris[i].p;
		float e1[3], e2[3], n[3], len, d;
		int key, nx, ny, nz, dq;
		for (int k = 0; k < 3; k++) {
			e1[k] = p[3 + k] - p[k];
			e2[k] = p[6 + k] - p[k];
		}
		n[0] = e1[1] * e2[2] - e1[2] * e2[1];
		n[1] = e1[2] * e2[0] - e1[0] * e2[2];
		n[2] = e1[0] * e2[1] - e1[1] * e2[0];
		len = sqrtf(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
		if (len < 1e-4f) {
			next[i] = -1;
			continue;
		}
		n[0] /= len;
		n[1] /= len;
		n[2] /= len;
		d = n[0] * p[0] + n[1] * p[1] + n[2] * p[2];
		nx = (int)lroundf(n[0] * 4.0f);
		ny = (int)lroundf(n[1] * 4.0f);
		nz = (int)lroundf(n[2] * 4.0f);
		dq = (int)lroundf(d);
	key = ((uint32_t)nx * 73856093u ^ (uint32_t)ny * 19349663u ^ (uint32_t)nz * 83492791u ^ (uint32_t)dq * 15485863u) & (BUCKETS - 1);
		next[i] = head[key];
		head[key] = i;
	}
	for (int b = 0; b < BUCKETS; b++) {
		float maxArea = 0.0f;
		int count = 0, bigTex = -2;
		for (int i = head[b]; i >= 0; i = next[i]) {
			float *p = mesh->tris[i].p;
			float e1[3], e2[3], cr[3], area;
			for (int k = 0; k < 3; k++) {
				e1[k] = p[3 + k] - p[k];
				e2[k] = p[6 + k] - p[k];
			}
			cr[0] = e1[1] * e2[2] - e1[2] * e2[1];
			cr[1] = e1[2] * e2[0] - e1[0] * e2[2];
			cr[2] = e1[0] * e2[1] - e1[1] * e2[0];
			area = sqrtf(cr[0] * cr[0] + cr[1] * cr[1] + cr[2] * cr[2]);
			count++;
			if (area > maxArea) {
				maxArea = area;
				bigTex = mesh->tris[i].tex;
			}
		}
		if (count < 2 || maxArea <= 0.0f) {
			continue;
		}
		for (int i = head[b]; i >= 0; i = next[i]) {
			float *p = mesh->tris[i].p;
			float e1[3], e2[3], cr[3], area;
			for (int k = 0; k < 3; k++) {
				e1[k] = p[3 + k] - p[k];
				e2[k] = p[6 + k] - p[k];
			}
			cr[0] = e1[1] * e2[2] - e1[2] * e2[1];
			cr[1] = e1[2] * e2[0] - e1[0] * e2[2];
			cr[2] = e1[0] * e2[1] - e1[1] * e2[0];
			area = sqrtf(cr[0] * cr[0] + cr[1] * cr[1] + cr[2] * cr[2]);
			if (mesh->tris[i].tex != bigTex && area < maxArea * 0.25f) {
				mesh->tris[i].flags = (uint8_t)(mesh->tris[i].flags | GEVR_BEAN_F_DECAL);
			}
		}
	}
	free(head);
	free(next);
}

/* flag bit 8: decal. Kept off the public flags so flash/slide/neck stay 1/2/4. */
int gevrBeanLoad(const char *source, int keepparts, struct gevrBeanMesh **out)
{
	FILE *fp;
	long size;
	uint8_t *file = NULL;
	struct gevrCaff caff;
	struct gevrBeanMesh *mesh;
	const uint8_t *data, *gpu, *stream, *pool;
	uint32_t dlen = 0, glen = 0, slen = 0, poollen = 0;
	int idata, igpu, istream, ipool;
	int texfile[MAX_TEX], ntex = 0;
	struct beanDraw *draws;
	struct beanIb *ibs;
	int ndraws = 0, nibs = 0;
	const char *names[MAX_BONES];
	int numnames = 0;
	int boneSk[MAX_BONES];
	int namedSk[MAX_BONES];
	uint8_t muzzle[MAX_BONES], slideb[MAX_BONES];
	int16_t parent[MAX_BONES];
	float bind[MAX_BONES][3];
	int numbones = 0;
	float insts[MAX_INST][16];
	int ninst = 0;
	int original;
	int section = -1, afterpieces = 0;
	uint32_t sectionend = 0, repeatvb = ~0u;
	int triggerCuffs;
	uint32_t end, pc, vb = 0, tex = 0, matpc = 0;
	uint8_t pal[GEVR_BEAN_MAXPAL];
	uint16_t remap[GEVR_BEAN_MAXPAL];
	int numremap = 0;
	uint8_t numpal = 1, passblend = 0, blend = 0, alphatest = 0;
	uint8_t alpha = 0xff;
	uint32_t colour = 0xffffffu;
	int16_t inst = -1;
	uint32_t secend = 0;
	uint8_t secblend = 0;
	int uv20;
	float uvscale;
	uint32_t buffersEnd = 0;
	char key[160];

	*out = NULL;
	if (!source) {
		return 0;
	}
	fp = fopen(source, "rb");
	if (!fp) {
		return 0;
	}
	if (fseek(fp, 0, SEEK_END) != 0 || (size = ftell(fp)) <= 0 || size > 64 * 1024 * 1024) {
		fclose(fp);
		return 0;
	}
	rewind(fp);
	file = malloc((size_t)size);
	if (!file || fread(file, 1, (size_t)size, fp) != (size_t)size) {
		fclose(fp);
		free(file);
		return 0;
	}
	fclose(fp);
	memset(&caff, 0, sizeof(caff));
	if (!gevrCaffOpen(&caff, file, (uint32_t)size)) {
		free(file);
		return 0;
	}
	idata = gevrCaffFind(&caff, 1, ".data");
	igpu = gevrCaffFind(&caff, 1, ".gpu");
	istream = gevrCaffFind(&caff, 1, ".stream");
	ipool = gevrCaffFind(&caff, 2, ".data");
	data = idata >= 0 ? gevrCaffBlob(&caff, idata, &dlen) : NULL;
	gpu = igpu >= 0 ? gevrCaffBlob(&caff, igpu, &glen) : NULL;
	stream = istream >= 0 ? gevrCaffBlob(&caff, istream, &slen) : NULL;
	if (!data || !gpu || !stream) {
		gevrCaffClose(&caff);
		free(file);
		return 0;
	}
	pool = ipool >= 0 ? gevrCaffBlob(&caff, ipool, &poollen) : NULL;
	for (uint32_t at = 0; pool && at < poollen && numnames < MAX_BONES; ) {
		const uint8_t *nul = memchr(pool + at, 0, poollen - at);
		uint32_t e = nul ? (uint32_t)(nul - pool) : poollen;
		if (nul && e - at > 5 && (memcmp(pool + at, "SKEL_", 5) == 0 || memcmp(pool + at, "BEAN_", 5) == 0)) {
			names[numnames++] = (const char *)pool + at;
		}
		at = e + 1;
	}
	for (uint32_t i = 0; i < caff.numfiles && ntex < MAX_TEX; i++) {
		uint32_t blen = 0;
		const uint8_t *b = gevrCaffBlob(&caff, (int)i, &blen);
		if (b && blen >= 8 && memcmp(b, "texture", 7) == 0 && b[7] == 0) {
			texfile[ntex++] = (int)i;
		}
	}
	memset(boneSk, 0, sizeof(boneSk));
	memset(muzzle, 0, sizeof(muzzle));
	memset(slideb, 0, sizeof(slideb));
	for (uint32_t at = 0; at + 0x38 <= dlen; at += 4) {
		if (memcmp(data + at, "pose\0\0\0\0", 8) == 0) {
			uint32_t count = be32(data + at + 0x18);
			uint32_t entries = be32(data + at + 0x34);
			if (count > MAX_BONES) {
				fprintf(stderr, "gevr bean: %s pose has %u bones, keeping %d\n", source, count, MAX_BONES);
				count = MAX_BONES;
			}
			for (uint32_t i = 0; i < count && fits(entries + 52ull * i, 52, dlen); i++) {
				const uint8_t *e = data + entries + 52 * i;
				uint32_t up = be16(e + 40);
				parent[i] = (up == 0xffff || up >= count) ? -1 : (int16_t)up;
				boneSk[i] = -1;
				for (int k = 0; k < 3; k++) {
					bind[i][k] = bef(e + 12 + k * 4);
				}
				if ((int)i < numnames) {
					for (int s = 0; s < GEVR_BEAN_SK_COUNT; s++) {
						if (boneIs(names[i], s_skelName[s])) {
							boneSk[i] = s;
							break;
						}
					}
					muzzle[i] = strncmp(names[i], "SKEL_MUZZLE", 11) == 0 || strstr(names[i], "FLASH") != NULL;
					slideb[i] = strcmp(names[i], "SKEL_SLIDE") == 0;
				}
				numbones = (int)i + 1;
			}
			break;
		}
	}
	for (int i = 0; i < numbones; i++) {
		for (int up = parent[i], steps = 0; up >= 0 && up < numbones && steps < MAX_BONES; up = parent[up], steps++) {
			if (muzzle[up]) {
				muzzle[i] = 1;
			}
			if (slideb[up]) {
				slideb[i] = 1;
			}
		}
	}
	memcpy(namedSk, boneSk, sizeof(namedSk));
	for (int i = 0; i < numbones; i++) {
		if (boneSk[i] >= 0) {
			continue;
		}
		{
			float best = 1e30f;
			int sk = 0;
			for (int j = 0; j < numbones; j++) {
				float d;
				if (namedSk[j] < 0) {
					continue;
				}
				d = (bind[i][0] - bind[j][0]) * (bind[i][0] - bind[j][0])
					+ (bind[i][1] - bind[j][1]) * (bind[i][1] - bind[j][1])
					+ (bind[i][2] - bind[j][2]) * (bind[i][2] - bind[j][2]);
				if (d < best) {
					best = d;
					sk = boneSk[j];
				}
			}
			boneSk[i] = sk;
		}
	}

	draws = calloc(MAX_DRAWS, sizeof(*draws));
	ibs = calloc(MAX_IBS, sizeof(*ibs));
	if (!draws || !ibs) {
		free(draws);
		free(ibs);
		gevrCaffClose(&caff);
		free(file);
		return 0;
	}
	original = strncmp(source, "original/", 9) == 0 || strstr(source, "/original/") != NULL ||
	           strstr(source, "\\original\\") != NULL;
	triggerCuffs = strstr(source, "/gun/trigger/") != NULL || strstr(source, "\\gun\\trigger\\") != NULL;
	pal[0] = 0;
	end = slen >= 8 ? be32(stream + 4) : 0;
	if (end > slen) {
		end = slen;
	}
	pc = 0x24;
	for (int steps = 0; pc + 4 <= end && steps < 100000; steps++) {
		uint32_t tag = be32(stream + pc);
		uint32_t sz = tag >> 16;
		uint32_t type = (tag >> 8) & 0xff;
		if (sz < 4 || !fits(pc, sz, slen)) {
			break;
		}
		blend = secend && pc < secend ? secblend : passblend;
		if (type == 0x16) {
			if (!fits(pc, 12, slen)) {
				break;
			}
			pc = be32(stream + pc + 8);
			continue;
		}
		if (type == 0x19 && sz >= 8) {
			pc = be32(stream + pc + 4);
			continue;
		}
		if (type == 0x1d) {
			break;
		}
		if (type == 0x17 && sz >= 12 && be32(stream + pc + 4) == 2 && original) {
			pc = be32(stream + pc + 8);
			continue;
		}
		if (sectionend && pc >= sectionend) {
			section = -1;
			sectionend = 0;
		}
		if (type == 0x17 && sz >= 12) {
			section = be32(stream + pc + 4);
			sectionend = be32(stream + pc + 8);
			afterpieces = 1;
		} else if (type == 0x2d || type == 0x2e)
			afterpieces = 0;
		if (type == 0x12 && sz >= 12) {
			numremap = be16(stream + pc + 8);
			if (numremap > GEVR_BEAN_MAXPAL)
				numremap = GEVR_BEAN_MAXPAL;
			for (int k = 0; k < numremap; k++) {
				if (!fits(pc + 12 + 8ull * k, 2, slen) || 12u + 8u * k + 2u > sz) {
					numremap = k;
					break;
				}
				remap[k] = be16(stream + pc + 12 + 8 * k);
			}
		} else if (type == 0x2e && sz >= 12) {
			vb = be32(stream + pc + 8);
		} else if (type == 0x1a && sz >= 8) {
			passblend = (be32(stream + pc + 4) & 0xff) == 2;
			secend = 0;
			blend = passblend;
		} else if (type == 0x15 && sz >= 12) {
			secend = be32(stream + pc + 4);
			secblend = (be32(stream + pc + 8) & 0xff) == 2;
		} else if (type == 0x0c && sz >= 12 && be32(stream + pc + 4) == 0x60) {
			alphatest = be32(stream + pc + 8) != 0;
		} else if (type == 0x2d && sz >= 20) {
			tex = materialTex(&caff, stream, pc, sz, slen, texfile, ntex);
			matpc = pc;
			alpha = 0xff;
			colour = 0xffffffu;
		} else if (type == 0x06 && sz >= 28 && be32(stream + pc + 4) == 0 &&
		           (be32(stream + pc + 8) >> 16) == 0x0c) {
			float a = bef(stream + pc + 24);
			alpha = a >= 1.0f ? 0xff : a <= 0.0f ? 0 : (uint8_t)(a * 255.0f + 0.5f);
			colour = 0;
			for (int k = 0; k < 3; k++) {
				float cv = bef(stream + pc + 12 + k * 4);
				uint32_t byte = cv >= 1.0f ? 255 : cv <= 0.0f ? 0 : (uint32_t)(cv * 255.0f + 0.5f);
				colour = (colour << 8) | byte;
			}
		} else if (type == 0x13 && sz >= 12) {
			uint32_t count = be16(stream + pc + 8);
			if (count > GEVR_BEAN_MAXPAL) {
				count = GEVR_BEAN_MAXPAL;
			}
			if (!fits(pc + 12, count, slen)) {
				count = 0;
			}
			memcpy(pal, stream + pc + 12, count);
			numpal = (uint8_t)count;
		} else if (type == 0x21 && sz >= 80) {
			if (ninst < MAX_INST) {
				for (int k = 0; k < 16; k++) {
					insts[ninst][k] = bef(stream + pc + 16 + k * 4);
				}
				inst = (int16_t)ninst++;
			}
		} else if (type == 0x25) {
			inst = -1;
		} else if ((type == 0x01 || type == 0x30) && sz >= 16 && ndraws < MAX_DRAWS) {
			int part = type == 0x30 && sz >= 20 ? (int)be32(stream + pc + 16) : -1;
			if (part <= 1 || keepparts) {
				struct beanDraw *d = &draws[ndraws++];
				d->cuff = -1;
				if (triggerCuffs) {
					static const int pieces[3] = {3, 1, 2}, sections[3] = {4, 0, 5};
					if ((type == 0x01 && section < 0 && afterpieces) ||
					    (repeatvb != ~0u && vb == repeatvb && part < 0 && section < 0)) {
						repeatvb = vb;
						d->cuff = -2;
					} else if (part >= 0 && part < 3)
						d->cuff = pieces[part];
					else if (section >= 3 && section <= 5)
						d->cuff = sections[section - 3];
				}
				d->vb = vb;
				d->tex = tex;
				d->prim = be32(stream + pc + 4);
				d->count = be32(stream + pc + 8);
				d->ib = be32(stream + pc + 12);
				d->inst = inst;
				d->blend = blend;
				d->alpha = alpha;
				d->alphatest = alphatest;
				d->numpal = numpal;
				d->colour = colour;
				for (int k = 0; k < numpal; k++) {
					uint16_t bone = pal[k] < numremap ? remap[pal[k]] : pal[k];
					d->pal[k] = bone < MAX_BONES ? bone : 255;
				}
				(void)matpc;
			}
			if (type == 0x30)
				afterpieces = 1;
		} else if ((type == 0x01 || type == 0x30) && ndraws >= MAX_DRAWS) {
			static int told;
			if (!told) {
				fprintf(stderr, "gevr bean: draw cap %d hit in %s\n", MAX_DRAWS, source);
				told = 1;
			}
		}
		pc += sz;
	}

	for (uint32_t o = 0; o + 16 <= dlen && nibs < MAX_IBS; o += 4) {
		uint32_t obj = be32(data + o);
		uint32_t off = be32(data + o + 4);
		uint32_t sz = be32(data + o + 8);
		uint32_t kind = be32(data + o + 12);
		uint32_t width = kind == 6 ? 4 : 2;
		int known = 0;
		if ((kind != 1 && kind != 6) || sz == 0 || (sz % width) || !fits(off, sz, glen)) {
			continue;
		}
		for (int i = 0; i < nibs; i++) {
			if (ibs[i].obj == obj) {
				known = 1;
				break;
			}
		}
		if (!known) {
			ibs[nibs].obj = obj;
			ibs[nibs].off = off;
			ibs[nibs].size = sz;
			ibs[nibs].width = width;
			nibs++;
		}
	}
	for (int di = 0; di < ndraws; di++) {
		struct beanVb vbrec;
		if (readVb(data, dlen, gpu, glen, draws[di].vb, 0, &vbrec)) {
			uint32_t e = vbrec.off + vbrec.count * vbrec.stride;
			if (e > buffersEnd) {
				buffersEnd = e;
			}
		}
		for (int i = 0; i < nibs; i++) {
			if (ibs[i].obj == draws[di].ib && ibs[i].off + ibs[i].size > buffersEnd) {
				buffersEnd = ibs[i].off + ibs[i].size;
			}
		}
	}
	uv20 = gevrBeanShaderUv20(gpu, glen, buffersEnd);
	if (strstr(source, "background/")) {
		uvscale = levelUv(gpu, glen, source);
	} else if (strstr(source, "skydome/")) {
		uvscale = 16384.0f;
	} else {
		uvscale = gevrBeanShaderUvScale(gpu, glen, buffersEnd);
		if (!(uvscale > 0.0f)) {
			uvscale = measureUv(data, dlen, gpu, glen, draws, ndraws, ibs, nibs, uv20, numbones > 0, &caff, texfile, ntex);
		}
	}

	mesh = calloc(1, sizeof(*mesh));
	if (!mesh) {
		free(draws);
		free(ibs);
		gevrCaffClose(&caff);
		free(file);
		return 0;
	}
	mesh->uvscale = uvscale;
	mesh->triggerCuffs = triggerCuffs;
	mesh->nbones = numbones;
	for (int i = 0; i < numbones; i++) {
		memcpy(mesh->boneBind[i], bind[i], sizeof(bind[i]));
		mesh->boneSk[i] = boneSk[i] < 0 ? 0 : boneSk[i];
		if (i < numnames)
			snprintf(mesh->boneName[i], 64, "%s", names[i]);
	}
	for (int s = 0; s < GEVR_BEAN_SK_COUNT; s++) {
		for (int i = 0; i < numbones; i++) {
			if (namedSk[i] == s) {
				mesh->bind[s][0] = bind[i][0];
				mesh->bind[s][1] = bind[i][1];
				mesh->bind[s][2] = bind[i][2];
				mesh->haveBind[s] = 1;
				break;
			}
		}
	}
	for (int i = 0; i < ntex && mesh->ntex < 256; i++) {
		int w = 0, h = 0, cut = 0;
		snprintf(key, sizeof(key), "%s:%d", source, i);
		mesh->standin[mesh->ntex] = gevrBeanTexDecode(&caff, texfile[i], key, &w, &h, &cut);
		mesh->texW[mesh->ntex] = w;
		mesh->texH[mesh->ntex] = h;
		mesh->texCut[mesh->ntex] = cut;
		/* Material indices address the original table, including failed decodes. */
		mesh->ntex++;
	}
	for (int di = 0; di < ndraws; di++) {
		struct beanDraw *d = &draws[di];
		if (triggerCuffs && d->cuff == -2)
			continue;
		struct beanVb vbrec;
		struct beanIb *ib = NULL;
		struct gevrBeanTriIn in;
		uint32_t *tris = NULL;
		int ntris;
		uint32_t cr = d->colour;
		if (!readVb(data, dlen, gpu, glen, d->vb, uv20, &vbrec)) {
			continue;
		}
		for (int i = 0; i < nibs; i++) {
			if (ibs[i].obj == d->ib) {
				ib = &ibs[i];
				break;
			}
		}
		if (!ib) {
			continue;
		}
		memset(&in, 0, sizeof(in));
		in.prim = d->prim;
		in.count = d->count;
		in.idx = gpu + ib->off;
		in.idxBytes = ib->size;
		in.width = ib->width;
		ntris = gevrBeanTriangles(&in, &tris);
		for (int t = 0; t < ntris; t++) {
			struct beanVtx corner[3];
			int ok = 1;
			for (int k = 0; k < 3; k++) {
				if (!readVtx(gpu, &vbrec, tris[t * 3 + k], uvscale, numbones > 0, &corner[k])) {
					ok = 0;
					break;
				}
				corner[k].argb = (
					((corner[k].argb >> 24) << 24)
					| ((((corner[k].argb >> 16) & 255) * ((cr >> 16) & 255) / 255) << 16)
					| ((((corner[k].argb >> 8) & 255) * ((cr >> 8) & 255) / 255) << 8)
					| ((corner[k].argb & 255) * (cr & 255) / 255));
				if (d->inst >= 0 && d->inst < ninst) {
					applyInst(corner[k].pos, insts[d->inst]);
					applyInstN(corner[k].nrm, insts[d->inst]);
				}
			}
			if (ok) {
				int texi = (int)d->tex < mesh->ntex ? (int)d->tex : -1;
				uint8_t a = d->alpha;
				if (!pushTri(mesh, &corner[0], &corner[1], &corner[2], texi, a,
						d->alphatest || (texi >= 0 && mesh->texCut[texi]), d->blend,
						d->pal, d->numpal, boneSk, muzzle, slideb)) {
					break;
				}
				mesh->tris[mesh->ntris - 1].cuff = d->cuff;
			}
		}
		free(tris);
	}
	markDecals(mesh);
	free(draws);
	free(ibs);
	gevrCaffClose(&caff);
	free(file);
	if (mesh->ntris <= 0) {
		gevrBeanMeshFree(mesh);
		return 0;
	}
	fprintf(stderr, "gevr bean: %s %d tris, uv %.0f\n", source, mesh->ntris, uvscale);
	*out = mesh;
	return 1;
}

void gevrBeanMeshFree(struct gevrBeanMesh *m)
{
	if (!m) {
		return;
	}
	free(m->tris);
	free(m);
}
