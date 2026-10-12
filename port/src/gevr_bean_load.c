/*
 * Project Bean CAFF -> triangles. Characters, guns, props, rooms and skies
 * all come through here. The display lists are built by gevr_bean.c.
 */

#include "gevr_bean_log.h"

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
	uint32_t vb, tex, prim, count, ib, vs, masktex, matpc;
	uint8_t maskslot;
    uint32_t reflectSpot,reflectLand; uint8_t reflectAmount[4],reflectionDiffuse,reflectionAlphaBase; uint32_t reflectionColour;
	int16_t inst;
	uint8_t blend, alpha, alphatest, terrainBlend;
	uint8_t pal[GEVR_BEAN_MAXPAL];
	uint8_t numpal;
	uint32_t colour;
	int8_t cuff;
	int8_t piece;   /* record 0x30's fourth word, -1 for a 0x01 draw (DAB beandraw.piece) */
};

/*
 * DAB gebean.c fpDrawIsCard(): in a first-person gun file a draw of piece 0
 * is one of the release's flash cards, which it draws only for the frame of a
 * shot - the ZMG's streaks on SKEL_TOP as well as the cards on the muzzle - so
 * it is the flash with whatever hangs off the muzzle's bones. Drawn as part of
 * the gun it was always lit: the RC-P90's cards on SKEL_TOP, the Automatic
 * Shotgun's on the muzzle and SKEL_TOP together, which the bone test alone
 * misses. DAB's fpRows (the 25 GoldenEye guns), with the gold and silver PP7
 * this port maps to files of their own; not the gadgets, whose piece 0 is
 * their body (the taser, the controller) or the trigger hand's cuffs.
 */
static int fpGunFile(const char *source)
{
	static const char *const files[] = {
		"ppk", "ppksilenced", "tt33", "skorpion", "ak47", "uzi", "mp5k", "mp5ksilenced", "spectre", "m16",
		"fnp90", "shotgun", "automaticshotgun", "sniperrifle", "ruger", "goldengun", "laser", "grenadelauncher",
		"rocketlauncher", "knife", "throwingknife", "grenade", "timedmine", "proximitymine", "remotemine",
		"goldppk", "silverPPK",
	};
	for (unsigned i = 0; i < sizeof(files) / sizeof(files[0]); i++) {
		char folder[48];
		snprintf(folder, sizeof(folder), "/gun/%s/", files[i]);
		if (strstr(source, folder))
			return 1;
	}
	return 0;
}

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
		/* DAB's level layout: stride 32 has UV2 at +20, blend at
		 * +24 and the lit colour at +28. UV bytes are not a colour. */
		v->argb = colourArgb(be32(p + (vb->stride == 36 ? 32 : vb->stride == 32 ? 28 : 20)));
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

static int sphereTex(const struct gevrCaff *c,const int *texfile,int ntex,uint32_t t)
{
    if(t>=(uint32_t)ntex)return 0;
    const char *name=gevrCaffAssetName(c,c->files[texfile[t]].asset);
    if(!name)return 0;
    if(!strcmp(name,"_0x0E1C2BF5.tga.bin") || !strcmp(name,"_0x081346B5.tga.bin") ||
       !strcmp(name,"_0x0B9D6195.tga.bin"))return 1;
    if(!strcmp(name,"_0x0C2BCE25.tga.bin") || !strcmp(name,"_0x018248E5.tga.bin"))return 2;
    return 0;
}

/* The two-input scratch/glass shader uses R=c12*normalMap and
 * out=lit*(c13-R)+R. Only its c13=white form fits the screen combiner. */
static int scratchTex(const struct gevrCaff *c,const int *texfile,int ntex,uint32_t t)
{
    if(t>=(uint32_t)ntex)return 0;
    const char *name=gevrCaffAssetName(c,c->files[texfile[t]].asset);
    static const char *const names[]={"_0x059B9F65.tga.bin","_0x00B5FD45.bin",
        "_0x00B5FD45.bmp.bin","_0x03B30B95.tga.bin","_0x018952E5.tga.bin","_0x0F0BCEB5.tga.bin"};
    for(unsigned i=0;name && i<sizeof(names)/sizeof(names[0]);i++)if(!strcmp(name,names[i]))return 1;
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

/* DAB beanReflectingSurfacePictures: Complex binds two animated bumps,
 * a reflection, and its own translucent water picture in slots 0..3.
 * Shader offsets differ between release/CE files; identify the authored
 * binding instead. The largest input is the reflection, never the surface. */
static uint32_t surfaceTex(const struct gevrCaff *c, const uint8_t *st, uint32_t pc,
        uint32_t size, const int *texfile, int ntex, const char *source, uint32_t chosen)
{
    if (!strstr(source, "new/background/complex/") && !strstr(source, "new\\background\\complex\\"))
        return chosen;
    uint32_t extra = be16(st + pc + 10), inputs[4] = {~0u, ~0u, ~0u, ~0u};
    for (uint32_t k = extra; 12 + 8*k + 8 <= size; k++) {
        uint32_t slot = be32(st + pc + 16 + 8*k) >> 16;
        if (slot < 4) inputs[slot] = be32(st + pc + 12 + 8*k);
    }
    for (int k = 0; k < 4; k++) if (inputs[k] >= (uint32_t)ntex) return chosen;
    for (int k = 0; k < 2; k++) {
        uint32_t len = 0;
        const uint8_t *b = gevrCaffBlob(c, texfile[inputs[k]], &len);
        if (!b || len < 0x40 || be32(b + 0x1c) != 5) return chosen;
    }
    const char *name = gevrCaffAssetName(c, c->files[texfile[inputs[2]]].asset);
    return name && !strcmp(name, "_0x0A8065B5.bin") ? inputs[3] : chosen;
}

static int pushTri(struct gevrBeanMesh *mesh, const struct beanVtx *a, const struct beanVtx *b,
		const struct beanVtx *c, int tex, int alpha, int alphatest, const uint8_t *pal,
		int numpal, const int *boneSk, const uint8_t *muzzle, const uint8_t *slide)
{
	struct gevrBeanTri *t;
	const struct beanVtx *corner[3] = { a, b, c };
	int sk = 0, flags = 0, named = 0, flash = 1, slideAll = 1;
	int total[GEVR_BEAN_SK_COUNT] = {0};
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
		for (int s = 0; s < 4; s++) {
			int slot = v->slot[s];
			int b = slot >= 0 && slot < numpal ? pal[slot] : -1;
			t->bone[cnr * 4 + s] = b >= 0 && b < MAX_BONES ? b : 255;
			t->weight[cnr * 4 + s] = b >= 0 && b < MAX_BONES ? v->weight[s] : 0;
		}
        /* DAB deals a triangle by the total weight of all three corners,
         * not the largest individual influence at one corner. */
        for (int s = 0; s < 4; s++) {
            int slot = v->slot[s];
            int bone = slot >= 0 && slot < numpal ? pal[slot] : -1;
            int joint = bone >= 0 && bone < MAX_BONES ? boneSk[bone] : -1;
            if (joint >= 0 && joint < GEVR_BEAN_SK_COUNT)
                total[joint] += v->weight[s];
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
    for (int joint = 1; joint < GEVR_BEAN_SK_COUNT; joint++)
        if (total[joint] > total[sk]) sk = joint;
	t->tex = (int16_t)tex;
	t->alpha = (uint8_t)alpha;
	t->alphatest = (uint8_t)alphatest;
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

/* DAB's picture flags (xblatex.c xblaTexImageInfo()): alpha when a texel is
 * under 0xf0, soft when under one percent of its texels reach it. */
static void textureAlphaFlags(const struct gevrBeanMesh *mesh, uint8_t *alpha, uint8_t *soft)
{
	for (int t = 0; t < MAX_TEX; t++) {
		const uint8_t *rgba;
		int w, h;
		uint64_t opaque = 0, total;
		alpha[t] = soft[t] = 0;
		if (t >= mesh->ntex || !gevrBeanTexture(mesh->standin[t], &rgba, &w, &h) || w <= 0 || h <= 0)
			continue;
		total = (uint64_t)w * (uint64_t)h;
		for (uint64_t i = 0; i < total; i++)
			opaque += rgba[i * 4 + 3] >= 0xf0;
		alpha[t] = opaque < total;
		soft[t] = opaque * 100 < total;
	}
}

static float triNormalArea(const float *p, float *n)
{
	float e1[3], e2[3], len;
	for (int k = 0; k < 3; k++) {
		e1[k] = p[3 + k] - p[k];
		e2[k] = p[6 + k] - p[k];
	}
	n[0] = e1[1] * e2[2] - e1[2] * e2[1];
	n[1] = e1[2] * e2[0] - e1[0] * e2[2];
	n[2] = e1[0] * e2[1] - e1[1] * e2[0];
	len = sqrtf(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
	if (!(len > 0.0f))
		return 0.0f;
	for (int k = 0; k < 3; k++)
		n[k] /= len;
	return len * 0.5f;
}

/* Rigid models' decals: DAB gebean.c beanMarkDecals()/beanDecalPair()/
 * beanOutPointInTri(), pinned adef1dc2; port/licenses/dabs-mod.txt. DAB
 * marks rigid props and guns (gebeanBuildRigid(), gebeanBuildFirstPerson())
 * and never a character: there a decal is drawn only over depth already in
 * place and writes none, so a dial or sleeve wrongly marked showed through. */
#define RDECAL_GRID 32
#define RDECAL_BIGCELLS 512

static int rigidPointInTri(const float *p, const float *n, const float *q)
{
	for (int e = 0; e < 3; e++) {
		const float *a = p + e * 3, *b = p + ((e + 1) % 3) * 3;
		const float ab[3] = { b[0] - a[0], b[1] - a[1], b[2] - a[2] };
		const float aq[3] = { q[0] - a[0], q[1] - a[1], q[2] - a[2] };
		const float c[3] = { ab[1] * aq[2] - ab[2] * aq[1], ab[2] * aq[0] - ab[0] * aq[2], ab[0] * aq[1] - ab[1] * aq[0] };
		if (c[0] * n[0] + c[1] * n[1] + c[2] * n[2] < 0.0f)
			return 0;
	}
	return 1;
}

/* Whether triangle j makes triangle i, whose middle is mid, a decal. DAB's
 * group and bone are its first corner's first influence here, and the
 * trigger sleeve a gun file's draw belongs to. */
static int rigidDecalPair(const struct gevrBeanMesh *mesh, const float (*nrm)[4], const uint8_t *texAlpha,
		int i, int j, const float *mid)
{
	const struct gevrBeanTri *t = &mesh->tris[i], *u = &mesh->tris[j];
	const float *ni = nrm[i], *nu = nrm[j];
	const float ai = nrm[i][3], au = nrm[j][3];
	const int alphai = t->tex >= 0 && t->tex < MAX_TEX && texAlpha[t->tex];
	const int alphau = u->tex >= 0 && u->tex < MAX_TEX && texAlpha[u->tex];
	/* A thousandth of the surface's size: Bean's are exactly in its plane */
	const float tol = 0.001f * sqrtf(au);
	int flat = 1;
	if (j == i || au <= 0.0f || t->bone[0] != u->bone[0] || t->cuff != u->cuff)
		return 0;
	if (ni[0] * nu[0] + ni[1] * nu[1] + ni[2] * nu[2] < 0.999f)
		return 0;
	for (int v = 0; v < 3 && flat; v++) {
		const float *q = &t->p[v * 3];
		flat = fabsf((q[0] - u->p[0]) * nu[0] + (q[1] - u->p[1]) * nu[1] + (q[2] - u->p[2]) * nu[2]) <= tol;
	}
	if (!flat || !rigidPointInTri(u->p, nu, mid))
		return 0;
	return alphai != alphau ? alphai > alphau
		: ai < au * 0.999f ? 1
		: ai <= au * 1.001f && i > j;
}

static void markRigidDecals(struct gevrBeanMesh *mesh, const uint8_t *texAlpha)
{
	const int n = mesh->ntris;
	float (*nrm)[4] = n ? malloc((size_t)n * sizeof(*nrm)) : NULL;
	float (*box)[6] = n ? malloc((size_t)n * sizeof(*box)) : NULL;
	int (*cellof)[6] = n ? malloc((size_t)n * sizeof(*cellof)) : NULL;
	uint8_t *decal = n ? calloc((size_t)n, 1) : NULL;
	int *big = n ? malloc((size_t)n * sizeof(int)) : NULL;
	int *start = NULL, *filed = NULL, *fill = NULL;
	int dims[3] = { 1, 1, 1 }, numbig = 0, numfiled = 0, numcells;
	float lo[3] = { 1e30f, 1e30f, 1e30f }, hi[3] = { -1e30f, -1e30f, -1e30f };
	float cellsize[3] = { 1.0f, 1.0f, 1.0f }, longest = 0.0f;
	if (!nrm || !box || !cellof || !decal || !big)
		goto done;
	for (int i = 0; i < n; i++)
		nrm[i][3] = triNormalArea(mesh->tris[i].p, nrm[i]);
	/* Each triangle's box, grown by its plane tolerance and a margin for the
	 * rounding of the inside test, and the box round them all */
	for (int j = 0; j < n; j++) {
		float extent = 0.0f, grow;
		if (nrm[j][3] <= 0.0f)
			continue;
		for (int k = 0; k < 3; k++) {
			box[j][k] = 1e30f;
			box[j][3 + k] = -1e30f;
			for (int v = 0; v < 3; v++) {
				const float c = mesh->tris[j].p[v * 3 + k];
				box[j][k] = c < box[j][k] ? c : box[j][k];
				box[j][3 + k] = c > box[j][3 + k] ? c : box[j][3 + k];
			}
			extent = box[j][3 + k] - box[j][k] > extent ? box[j][3 + k] - box[j][k] : extent;
		}
		grow = 0.001f * sqrtf(nrm[j][3]) + 0.01f * extent + 0.001f;
		for (int k = 0; k < 3; k++) {
			box[j][k] -= grow;
			box[j][3 + k] += grow;
			lo[k] = box[j][k] < lo[k] ? box[j][k] : lo[k];
			hi[k] = box[j][3 + k] > hi[k] ? box[j][3 + k] : hi[k];
		}
	}
	for (int k = 0; k < 3; k++)
		longest = hi[k] - lo[k] > longest ? hi[k] - lo[k] : longest;
	for (int k = 0; k < 3 && longest > 0.0f; k++) {
		const float ext = hi[k] - lo[k];
		dims[k] = (int)(RDECAL_GRID * ext / longest);
		dims[k] = dims[k] < 1 ? 1 : dims[k] > RDECAL_GRID ? RDECAL_GRID : dims[k];
		cellsize[k] = ext > 0.0f ? ext / dims[k] : 1.0f;
	}
	numcells = dims[0] * dims[1] * dims[2];
	start = calloc((size_t)numcells + 1, sizeof(int));
	fill = malloc((size_t)numcells * sizeof(int));
	if (!start || !fill)
		goto done;
	/* The cells each one's box covers, counted then filed */
	for (int j = 0; j < n; j++) {
		int *c = cellof[j], cells = 1;
		c[0] = -1;
		if (nrm[j][3] <= 0.0f)
			continue;
		for (int k = 0; k < 3; k++) {
			const int a = (int)((box[j][k] - lo[k]) / cellsize[k]);
			const int z = (int)((box[j][3 + k] - lo[k]) / cellsize[k]);
			c[k] = a < 0 ? 0 : a >= dims[k] ? dims[k] - 1 : a;
			c[3 + k] = z < 0 ? 0 : z >= dims[k] ? dims[k] - 1 : z;
			cells *= c[3 + k] - c[k] + 1;
		}
		if (cells > RDECAL_BIGCELLS) {
			big[numbig++] = j;
			c[0] = -1;
			continue;
		}
		for (int x = c[0]; x <= c[3]; x++)
			for (int y = c[1]; y <= c[4]; y++)
				for (int z = c[2]; z <= c[5]; z++) {
					start[(x * dims[1] + y) * dims[2] + z + 1]++;
					numfiled++;
				}
	}
	for (int cell = 0; cell < numcells; cell++)
		start[cell + 1] += start[cell];
	filed = malloc((size_t)(numfiled ? numfiled : 1) * sizeof(int));
	if (!filed)
		goto done;
	memcpy(fill, start, (size_t)numcells * sizeof(int));
	for (int j = 0; j < n; j++) {
		const int *c = cellof[j];
		if (nrm[j][3] <= 0.0f || c[0] < 0)
			continue;
		for (int x = c[0]; x <= c[3]; x++)
			for (int y = c[1]; y <= c[4]; y++)
				for (int z = c[2]; z <= c[5]; z++)
					filed[fill[(x * dims[1] + y) * dims[2] + z]++] = j;
	}
	for (int i = 0; i < n; i++) {
		float mid[3];
		int cell = -1, inside = 1;
		if (nrm[i][3] <= 0.0f)
			continue;
		for (int k = 0; k < 3; k++) {
			mid[k] = (mesh->tris[i].p[k] + mesh->tris[i].p[3 + k] + mesh->tris[i].p[6 + k]) / 3.0f;
			if (!(mid[k] >= lo[k] && mid[k] <= hi[k]))
				inside = 0;
		}
		if (inside) {
			int c[3];
			for (int k = 0; k < 3; k++) {
				c[k] = (int)((mid[k] - lo[k]) / cellsize[k]);
				c[k] = c[k] < 0 ? 0 : c[k] >= dims[k] ? dims[k] - 1 : c[k];
			}
			cell = (c[0] * dims[1] + c[1]) * dims[2] + c[2];
		}
		for (int b = 0; b < numbig && !decal[i]; b++)
			decal[i] = (uint8_t)rigidDecalPair(mesh, (const float (*)[4])nrm, texAlpha, i, big[b], mid);
		for (int f = cell >= 0 ? start[cell] : 0; cell >= 0 && f < start[cell + 1] && !decal[i]; f++)
			decal[i] = (uint8_t)rigidDecalPair(mesh, (const float (*)[4])nrm, texAlpha, i, filed[f], mid);
	}
	for (int i = 0; i < n; i++)
		mesh->tris[i].flags = (uint8_t)((mesh->tris[i].flags & ~GEVR_BEAN_F_DECAL) | (decal[i] ? GEVR_BEAN_F_DECAL : 0));
done:
	free(nrm);
	free(box);
	free(cellof);
	free(decal);
	free(big);
	free(start);
	free(fill);
	free(filed);
}

/* A level's decals: DAB gebeanstage.c markDecals(), decalCovered(),
 * decalPick(), markCandidate(), markPointsOn(), triOther(), decalLater() and
 * pointTriDist(), pinned adef1dc2; port/licenses/dabs-mod.txt. DAB measures
 * in its converted level, Bean units times the stage row's scale
 * (gebeanstagetable.h), so its distances are divided by that scale here.
 * Not ported: markBacked(), markFights() and markOverlaps() (no face here is
 * drawn culled, so DAB's triCulled() is false) and the blended faces a later
 * blended one paints over, which DAB leaves out. */
#define LDECAL_DIST 1.0f
#define LDECAL_COS 0.999f
#define LDECAL_CELL 64.0f
#define LDECAL_SHADE_APART 48.0f
#define LDECAL_GRID_BITS 20
#define LDECAL_PASS_BLEND 1
#define LDECAL_PASS_TWIN 2

static float levelDecalScale(const char *source)
{
	static const struct { const char *bean; float scale; } rows[] = {
		{ "archives", 1.072400836f }, { "control", 1.616580623f }, { "facility", 0.531318270f },
		{ "aztec", 1.475407422f }, { "library", 0.573907333f }, { "cavern", 0.501072787f },
		{ "cradle", 1.828668257f }, { "temple", 0.921010121f }, { "dam", 4.280101153f },
		{ "depot", 0.805820509f }, { "frigate", 0.821433927f }, { "multitemple", 3.119410587f },
		{ "jungle", 0.706146978f }, { "caves", 0.715800060f }, { "streets", 1.239411313f },
		{ "complex", 0.401740938f }, { "runway", 0.508383572f }, { "bunker", 0.429214082f },
		{ "bunker2", 0.429214887f }, { "surface", 0.497385395f }, { "silo", 0.615140709f },
		{ "statuepark", 0.007471857f }, { "train", 1.300371514f },
	};
	const char *name = strstr(source, "background/");
	size_t len;
	if (!name)
		name = strstr(source, "background\\");
	if (!name)
		return 1.0f;
	name += 11;
	len = strcspn(name, "/\\");
	for (unsigned i = 0; i < sizeof(rows) / sizeof(rows[0]); i++)
		if (strlen(rows[i].bean) == len && !strncmp(name, rows[i].bean, len))
			return rows[i].scale;
	return 1.0f;
}

struct levelDecals {
	const struct gevrBeanMesh *mesh;
	const uint8_t *pass, *texAlpha;
	int npass;
	float (*nrm)[4];
	float *shade;
	uint8_t *decal;
	int8_t *full;
	int *base, *head, *next, *item, nent, capent;
	float dist, cell;
};

static uint32_t levelDecalKey(int x, int y, int z)
{
	return ((uint32_t)x * 73856093u ^ (uint32_t)y * 19349663u ^ (uint32_t)z * 83492791u) &
	       ((1u << LDECAL_GRID_BITS) - 1);
}

static uint32_t levelDecalPointKey(const struct levelDecals *l, const float *p)
{
	return levelDecalKey((int)floorf(p[0] / l->cell), (int)floorf(p[1] / l->cell), (int)floorf(p[2] / l->cell));
}

static void levelDecalAdd(struct levelDecals *l, int t)
{
	int lo[3], hi[3], cells = 1;
	const float *p = l->mesh->tris[t].p;
	for (int k = 0; k < 3; k++) {
		const float mn = fminf(p[k], fminf(p[3 + k], p[6 + k])), mx = fmaxf(p[k], fmaxf(p[3 + k], p[6 + k]));
		lo[k] = (int)floorf(mn / l->cell);
		hi[k] = (int)floorf(mx / l->cell);
		cells *= hi[k] - lo[k] + 1;
	}
	/* A triangle that big is sky or distant scenery (tgridAdd()) */
	if (cells > 4096)
		return;
	for (int x = lo[0]; x <= hi[0]; x++)
		for (int y = lo[1]; y <= hi[1]; y++)
			for (int z = lo[2]; z <= hi[2]; z++) {
				const uint32_t key = levelDecalKey(x, y, z);
				if (l->nent >= l->capent) {
					const int cap = l->capent ? l->capent * 2 : 65536;
					int *next = realloc(l->next, (size_t)cap * sizeof(int));
					if (next)
						l->next = next;
					int *item = next ? realloc(l->item, (size_t)cap * sizeof(int)) : NULL;
					if (item)
						l->item = item;
					if (!next || !item)
						return;
					l->capent = cap;
				}
				l->next[l->nent] = l->head[key];
				l->item[l->nent] = t;
				l->head[key] = l->nent++;
			}
}

static float levelDot(const float *a, const float *b)
{
	return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

/* Squared distance from p to the triangle abc (Ericson, closest point on a triangle). */
static float levelPointTriDist(const float *p, const float *a, const float *b, const float *c)
{
	float ab[3], ac[3], ap[3], bp[3], cp[3], q[3], d[3];
	float d1, d2, d3, d4, d5, d6, va, vb, vc, v, w, denom;
	for (int k = 0; k < 3; k++) {
		ab[k] = b[k] - a[k];
		ac[k] = c[k] - a[k];
		ap[k] = p[k] - a[k];
	}
	d1 = levelDot(ab, ap);
	d2 = levelDot(ac, ap);
	if (d1 <= 0 && d2 <= 0) {
		memcpy(q, a, sizeof(q));
		goto done;
	}
	for (int k = 0; k < 3; k++)
		bp[k] = p[k] - b[k];
	d3 = levelDot(ab, bp);
	d4 = levelDot(ac, bp);
	if (d3 >= 0 && d4 <= d3) {
		memcpy(q, b, sizeof(q));
		goto done;
	}
	vc = d1 * d4 - d3 * d2;
	if (vc <= 0 && d1 >= 0 && d3 <= 0) {
		v = d1 / (d1 - d3);
		for (int k = 0; k < 3; k++)
			q[k] = a[k] + ab[k] * v;
		goto done;
	}
	for (int k = 0; k < 3; k++)
		cp[k] = p[k] - c[k];
	d5 = levelDot(ab, cp);
	d6 = levelDot(ac, cp);
	if (d6 >= 0 && d5 <= d6) {
		memcpy(q, c, sizeof(q));
		goto done;
	}
	vb = d5 * d2 - d1 * d6;
	if (vb <= 0 && d2 >= 0 && d6 <= 0) {
		w = d2 / (d2 - d6);
		for (int k = 0; k < 3; k++)
			q[k] = a[k] + ac[k] * w;
		goto done;
	}
	va = d3 * d6 - d5 * d4;
	if (va <= 0 && (d4 - d3) >= 0 && (d5 - d6) >= 0) {
		w = (d4 - d3) / ((d4 - d3) + (d5 - d6));
		for (int k = 0; k < 3; k++)
			q[k] = b[k] + (c[k] - b[k]) * w;
		goto done;
	}
	denom = 1.0f / (va + vb + vc);
	v = vb * denom;
	w = vc * denom;
	for (int k = 0; k < 3; k++)
		q[k] = a[k] + ab[k] * v + ac[k] * w;
done:
	for (int k = 0; k < 3; k++)
		d[k] = p[k] - q[k];
	return levelDot(d, d);
}

static float levelTriDist(const struct levelDecals *l, const float *p, int o)
{
	const float *u = l->mesh->tris[o].p;
	return levelPointTriDist(p, u, u + 3, u + 6);
}

static int levelBlend(const struct levelDecals *l, int i)
{
	return i < l->npass && (l->pass[i] & LDECAL_PASS_BLEND);
}

static int levelTwin(const struct levelDecals *l, int i)
{
	return i < l->npass && (l->pass[i] & LDECAL_PASS_TWIN);
}

static int levelAlpha(const struct levelDecals *l, int i)
{
	const int tex = l->mesh->tris[i].tex;
	return tex >= 0 && tex < MAX_TEX && l->texAlpha[tex];
}

/* Another picture, or the same one painted much darker or lighter by its vertices */
static int levelOther(const struct levelDecals *l, int i, int o)
{
	return l->mesh->tris[o].tex != l->mesh->tris[i].tex || fabsf(l->shade[i] - l->shade[o]) > LDECAL_SHADE_APART;
}

/* markCandidate(MARKFILTER_DECAL) */
static int levelCandidate(const struct levelDecals *l, int i, const float *ni, int o)
{
	const float *nu = l->nrm[o], *tp = l->mesh->tris[i].p, *up = l->mesh->tris[o].p;
	float cosang;
	if (o == i || l->nrm[o][3] <= 0)
		return 0;
	cosang = levelDot(ni, nu);
	if ((cosang < LDECAL_COS && cosang > -LDECAL_COS) || !levelOther(l, i, o))
		return 0;
	for (int k = 0; k < 3; k++) {
		const float rel[3] = { tp[k * 3] - up[0], tp[k * 3 + 1] - up[1], tp[k * 3 + 2] - up[2] };
		if (fabsf(levelDot(rel, nu)) > l->dist)
			return 0;
	}
	return 1;
}

/* decalCovered(): its middle, and its corners and the middles of its edges
 * pulled a tenth of the way in, each on a triangle of another picture */
static int levelCovered(struct levelDecals *l, int i)
{
	const float *p = l->mesh->tris[i].p;
	float pts[7][3], mid[3];
	uint32_t key[7];
	uint8_t hit[7] = { 0 }, done[7] = { 0 };
	if (l->full[i] >= 0)
		return l->full[i];
	l->full[i] = 0;
	if (l->nrm[i][3] <= 0)
		return 0;
	for (int j = 0; j < 3; j++)
		mid[j] = (p[j] + p[3 + j] + p[6 + j]) / 3.0f;
	memcpy(pts[0], mid, sizeof(mid));
	for (int k = 0, n = 1; k < 3; k++) {
		for (int j = 0; j < 3; j++)
			pts[n][j] = p[k * 3 + j] + (mid[j] - p[k * 3 + j]) * 0.1f;
		n++;
		for (int j = 0; j < 3; j++) {
			const float edge = (p[k * 3 + j] + p[((k + 1) % 3) * 3 + j]) * 0.5f;
			pts[n][j] = edge + (mid[j] - edge) * 0.1f;
		}
		n++;
	}
	for (int k = 0; k < 7; k++)
		key[k] = levelDecalPointKey(l, pts[k]);
	for (int k = 0; k < 7; k++) {
		int left = 0;
		if (done[k])
			continue;
		for (int m = k; m < 7; m++)
			if (key[m] == key[k]) {
				done[m] = 1;
				left++;
			}
		for (int e = l->head[key[k]]; e >= 0 && left > 0; e = l->next[e]) {
			const int o = l->item[e];
			if (!levelCandidate(l, i, l->nrm[i], o))
				continue;
			for (int m = k; m < 7; m++)
				if (key[m] == key[k] && !hit[m] && levelTriDist(l, pts[m], o) <= l->dist * l->dist) {
					hit[m] = 1;
					left--;
				}
		}
		if (left > 0)
			return 0;
	}
	return l->full[i] = 1;
}

/* decalLater(): the blended pass is drawn after the opaque one, a twin with
 * its opaque draw, and within a pass the stream's order */
static int levelLater(const struct levelDecals *l, int i, int o)
{
	const int tpass = levelBlend(l, i) && !levelTwin(l, i), upass = levelBlend(l, o) && !levelTwin(l, o);
	return tpass != upass ? tpass > upass : i > o;
}

/* decalPick(): the face triangle i is drawn as the decal of, or -1 */
static int levelPick(struct levelDecals *l, int i, int redo, int *bystrip)
{
	const float *tp = l->mesh->tris[i].p, *ni = l->nrm[i];
	const float ai = l->nrm[i][3], d2 = l->dist * l->dist;
	float mid[3];
	uint32_t keys[7];
	int numkeys = 0;
	*bystrip = 0;
	if (ai <= 0)
		return -1;
	for (int j = 0; j < 3; j++)
		mid[j] = (tp[j] + tp[3 + j] + tp[6 + j]) / 3.0f;
	for (int k = 0; k < 7; k++) {
		float p[3];
		uint32_t key;
		int seen = 0;
		for (int j = 0; j < 3; j++) {
			const float from = k == 0 ? mid[j]
			                   : k < 4 ? tp[(k - 1) * 3 + j]
			                           : (tp[(k - 4) * 3 + j] + tp[((k - 3) % 3) * 3 + j]) * 0.5f;
			p[j] = from + (mid[j] - from) * 0.1f;
		}
		key = levelDecalPointKey(l, p);
		for (int q = 0; q < numkeys; q++)
			seen |= keys[q] == key;
		if (!seen)
			keys[numkeys++] = key;
	}
	for (int c = 0; c < numkeys; c++)
		for (int e = l->head[keys[c]]; e >= 0; e = l->next[e]) {
			const int o = l->item[e];
			const float *up = l->mesh->tris[o].p, *nu = l->nrm[o];
			const int alphai = levelAlpha(l, i), alphau = levelAlpha(l, o);
			const float au = l->nrm[o][3];
			float cosang, d;
			int flat = 1, strip = 0;
			if (o == i || !levelOther(l, i, o))
				continue;
			cosang = levelDot(ni, nu);
			if (au <= 0 || (cosang < LDECAL_COS && cosang > -LDECAL_COS))
				continue;
			for (int k = 0; k < 3 && flat; k++) {
				const float rel[3] = { tp[k * 3] - up[0], tp[k * 3 + 1] - up[1], tp[k * 3 + 2] - up[2] };
				flat = fabsf(levelDot(rel, nu)) <= l->dist;
			}
			if (!flat)
				continue;
			d = levelTriDist(l, mid, o);
			/* Two faces in one plane overlapping along a strip, neither's
			 * middle on the other: a point a tenth in from a corner or an
			 * edge lying inside the other counts */
			if (d > d2) {
				float umid[3];
				int inside = 0;
				for (int j = 0; j < 3; j++)
					umid[j] = (up[j] + up[3 + j] + up[6 + j]) / 3.0f;
				if (levelPointTriDist(umid, tp, tp + 3, tp + 6) <= d2)
					continue;
				for (int k = 0; k < 6 && !inside; k++) {
					float p[3], rel[3], off;
					for (int j = 0; j < 3; j++) {
						const float from = k < 3 ? tp[k * 3 + j] : (tp[(k - 3) * 3 + j] + tp[((k - 2) % 3) * 3 + j]) * 0.5f;
						p[j] = from + (mid[j] - from) * 0.1f;
						rel[j] = p[j] - up[j];
					}
					off = levelDot(rel, nu);
					for (int j = 0; j < 3; j++)
						p[j] -= nu[j] * off;
					/* DAB's 0.0001 square units, in Bean units */
					inside = levelTriDist(l, p, o) <= 0.0001f * d2;
				}
				if (!inside)
					continue;
				strip = 1;
			}
			if (redo && strip && l->decal[o] && l->base[o] != i)
				continue;
			/* Two blended faces: the later paints over the earlier */
			if (levelBlend(l, i) && levelBlend(l, o) && !levelLater(l, i, o))
				continue;
			if ((levelBlend(l, i) && levelBlend(l, o)) ||
			    (strip ? (alphai != alphau ? alphai > alphau : levelLater(l, i, o))
			     : levelCovered(l, i) != levelCovered(l, o) ? levelCovered(l, i)
			     : alphai != alphau ? alphai > alphau
			     : levelCovered(l, i) ? levelLater(l, i, o)
			     : ai < au * 0.999f ? 1
			     : ai <= au * 1.001f && levelLater(l, i, o))) {
				*bystrip = strip;
				return o;
			}
		}
	return -1;
}

static void markLevelDecals(struct gevrBeanMesh *mesh, const uint8_t *pass, int npass, const uint8_t *texAlpha,
		float scale)
{
	struct levelDecals l;
	const int n = mesh->ntris;
	uint8_t *bystrip = n ? calloc((size_t)n, 1) : NULL;
	memset(&l, 0, sizeof(l));
	l.mesh = mesh;
	l.pass = pass;
	l.npass = pass ? npass : 0;
	l.texAlpha = texAlpha;
	l.dist = LDECAL_DIST / (scale > 0.0f ? scale : 1.0f);
	l.cell = LDECAL_CELL / (scale > 0.0f ? scale : 1.0f);
	l.nrm = n ? malloc((size_t)n * sizeof(*l.nrm)) : NULL;
	l.shade = n ? malloc((size_t)n * sizeof(float)) : NULL;
	l.decal = n ? calloc((size_t)n, 1) : NULL;
	l.full = n ? malloc((size_t)n) : NULL;
	l.base = n ? malloc((size_t)n * sizeof(int)) : NULL;
	l.head = malloc(sizeof(int) << LDECAL_GRID_BITS);
	if (!bystrip || !l.nrm || !l.shade || !l.decal || !l.full || !l.base || !l.head)
		goto done;
	for (int i = 0; i < (1 << LDECAL_GRID_BITS); i++)
		l.head[i] = -1;
	memset(l.full, -1, (size_t)n);
	for (int i = 0; i < n; i++) {
		const uint8_t *c = mesh->tris[i].rgba;
		l.nrm[i][3] = triNormalArea(mesh->tris[i].p, l.nrm[i]);
		l.shade[i] = (float)(c[0] + c[1] + c[2] + c[4] + c[5] + c[6] + c[8] + c[9] + c[10]) / 9.0f;
		l.base[i] = -1;
	}
	for (int i = 0; i < n; i++)
		levelDecalAdd(&l, i);
	for (int i = 0; i < n; i++) {
		int strip, o;
		if (l.decal[i])
			continue;
		o = levelPick(&l, i, 0, &strip);
		if (o >= 0) {
			l.decal[i] = 1;
			l.base[i] = o;
			bystrip[i] = (uint8_t)strip;
		}
	}
	/* A face made the decal along a strip of a face that is itself a decal
	 * is decided again without that pick, until nothing changes */
	for (int round = 0; round < 4; round++) {
		int changed = 0;
		for (int i = 0; i < n; i++) {
			const int o = l.base[i];
			int strip, b;
			if (!bystrip[i] || !l.decal[i] || o < 0 || o >= n || !l.decal[o] || l.base[o] == i)
				continue;
			l.decal[i] = 0;
			l.base[i] = -1;
			bystrip[i] = 0;
			changed++;
			b = levelPick(&l, i, 1, &strip);
			if (b >= 0) {
				l.decal[i] = 1;
				l.base[i] = b;
				bystrip[i] = (uint8_t)strip;
			}
		}
		if (!changed)
			break;
	}
	/* A twin stays marked: it is drawn blended over its own draw */
	for (int i = 0; i < n; i++)
		mesh->tris[i].flags = (uint8_t)((mesh->tris[i].flags & ~GEVR_BEAN_F_DECAL) |
		                                (l.decal[i] || levelTwin(&l, i) ? GEVR_BEAN_F_DECAL : 0));
done:
	free(bystrip);
	free(l.nrm);
	free(l.shade);
	free(l.decal);
	free(l.full);
	free(l.base);
	free(l.head);
	free(l.next);
	free(l.item);
}

static int notePass(uint8_t **pass, int *cap, int index, uint8_t value)
{
	if (index >= *cap) {
		int grown = *cap ? *cap * 2 : 4096;
		uint8_t *p;
		while (grown <= index)
			grown *= 2;
		p = realloc(*pass, (size_t)grown);
		if (!p)
			return 0;
		memset(p + *cap, 0, (size_t)(grown - *cap));
		*pass = p;
		*cap = grown;
	}
	(*pass)[index] = value;
	return 1;
}

/* flag bit 8: decal. Kept off the public flags so flash/slide/neck stay 1/2/4. */
#include "gevr_bean_placeholder.inc.h"

struct beanLayers { int base, other, mask, blend, decal; };
/* DAB's two-picture terrain and decal shaders retain an opaque base. */
static struct beanLayers levelLayers(const struct gevrBeanMesh *mesh, const struct beanDraw *d,
        const struct beanVb *vb, const uint8_t *gpu, const uint32_t *tris, int ntris, int levelFade)
{
        int baseTex = d->tex < (uint32_t)mesh->ntex ? (int)d->tex : -1;
        int otherTex = d->masktex < (uint32_t)mesh->ntex ? (int)d->masktex : -1;
        int mask = 0, blendpic = 0, decalpic = 0;
        if (levelFade && vb->stride == 12) baseTex = -1;
        if (levelFade && vb->stride == 32 && baseTex >= 0 && otherTex >= 0) {
            mask = d->alphatest && d->maskslot == 1 && !mesh->texCut[baseTex] && mesh->texCut[otherTex];
            if (mask) baseTex = otherTex;
            if (!mask && !d->alphatest && !d->blend) {
                int amin = 255, amax = 0;
                for (int v = 0; v < ntris*3; v++) if (tris[v] < vb->count) {
                    int a = gpu[vb->off + tris[v]*32 + 24];
                    if (a < amin) amin = a;
                    if (a > amax) amax = a;
                }
                blendpic = d->maskslot == 0 && !mesh->texCut[baseTex] && !mesh->texCut[otherTex]
                         && ((amin == 0 && amax == 255) || d->terrainBlend);
                decalpic = d->maskslot == 1 && mesh->texCut[baseTex] && !mesh->texCut[otherTex];
            }
        }
    return (struct beanLayers){baseTex, otherTex, mask, blendpic, decalpic};
}

/* Identify a terrain shader from a full-range blend, then keep the same
 * material on its partial-range draws (Dam has six near the manhole). */
static void markLevelBlendShaders(const struct gevrBeanMesh *mesh, struct beanDraw *draws,
        int ndraws, const struct beanIb *ibs, int nibs, const uint8_t *data, uint32_t dlen,
        const uint8_t *gpu, uint32_t glen, int uv20)
{
    uint32_t shaders[32][2];
    int known = 0;
    for (int di=0; di<ndraws; di++) {
        struct beanDraw *d = draws+di;
        struct beanVb vb;
        if (d->alphatest || d->blend || d->maskslot != 0 ||
                !readVb(data, dlen, gpu, glen, d->vb, uv20, &vb) || vb.stride != 32) continue;
        for (int i=0; i<nibs; i++) if (ibs[i].obj == d->ib) {
            struct gevrBeanTriIn in = {.prim=d->prim, .count=d->count,
                .idx=gpu+ibs[i].off, .idxBytes=ibs[i].size, .width=ibs[i].width};
            uint32_t *tris = NULL;
            int ntris = gevrBeanTriangles(&in, &tris);
            struct beanLayers layers = levelLayers(mesh, d, &vb, gpu, tris, ntris, 1);
            free(tris);
            if (layers.blend) {
                int seen = 0;
                for (int s=0; s<known; s++) seen |= shaders[s][0] == d->vs && shaders[s][1] == d->matpc;
                if (!seen && known < 32) { shaders[known][0]=d->vs; shaders[known++][1]=d->matpc; }
            }
            break;
        }
    }
    for (int di=0; di<ndraws; di++)
        for (int s=0; s<known; s++)
            if (draws[di].vs == shaders[s][0] && draws[di].matpc == shaders[s][1])
                draws[di].terrainBlend = 1;
}

#include "gevr_bean_motion.inc.h"

/* DAB gebean.c beanVertexDrops[]/beanVertexDropped(), pinned adef1dc2
 * (port/licenses/dabs-mod.txt): by file, vertex buffer (.gpu offset) and
 * vertex range. Silo's console2 and console3 have four quads of Bean's
 * placeholder spiral on their lower screens; Dam's modem box and gate switch
 * have their screen as a quad of its own on one flat grey texel (the modem's
 * one, the gate switch's two lamps), in front of GoldenEye's quad. gevr_bean.c
 * lays such a vertex behind a screen GoldenEye runs a programme on, as DAB's
 * gebeanBuildRigid() does (beanScreenBacking()), and leaves it out elsewhere. */
static const struct {
	const char *source;
	uint32_t vboff;
	uint16_t first, last;
} s_vertexDrops[] = {
	{ "new/prop/console2", 0, 118, 133 },
	{ "new/prop/console3", 0, 118, 133 },
	{ "new/prop/modembox", 1264, 0, 3 },
	{ "new/prop/doorpanel", 1160, 0, 7 },
};

/* path names .../new/prop/NAME/default.bin, DAB's source being new/prop/NAME */
static int sourceIs(const char *path, const char *source)
{
	static const char tail[] = "/default.bin";
	size_t n = strlen(source), len = strlen(path), want = n + sizeof(tail) - 1;
	if (len < want)
		return 0;
	const char *at = path + len - want;
	for (size_t i = 0; i < want; i++) {
		char a = at[i] == '\\' ? '/' : at[i];
		if (a != (i < n ? source[i] : tail[i - n]))
			return 0;
	}
	return at == path || at[-1] == '/' || at[-1] == '\\';
}

static int vertexDropped(const char *path, uint32_t vboff, uint32_t vi)
{
	for (unsigned i = 0; i < sizeof(s_vertexDrops) / sizeof(s_vertexDrops[0]); i++)
		if (s_vertexDrops[i].vboff == vboff && vi >= s_vertexDrops[i].first && vi <= s_vertexDrops[i].last &&
		    sourceIs(path, s_vertexDrops[i].source))
			return 1;
	return 0;
}

static void freeScreenSources(struct gevrBeanMesh *mesh)
{
	for (int i = 0; mesh->draws && i < mesh->ndraws; i++) {
		free(mesh->draws[i].pos);
		free(mesh->draws[i].uv);
		free(mesh->draws[i].state);
		free(mesh->draws[i].idx);
	}
	free(mesh->draws);
	free(mesh->triSrc);
	mesh->draws = NULL;
	mesh->ndraws = 0;
	mesh->triSrc = NULL;
}

/* One draw's buffer and triangles, as DAB's beanReadVb()/beanVertex()/
 * beanTriangles() give them to beanFindScreens()'s users. */
static void keepScreenDraw(struct gevrBeanDrawSrc *ds, const char *path, const struct gevrCaff *caff,
                           const int *texfile, int ntex, uint32_t tex, const uint8_t *gpu, const struct beanVb *vb,
                           float uvscale, int skinned, const float *inst, const uint32_t *tris, int ntris)
{
	const char *name = tex < (uint32_t)ntex ? gevrCaffAssetName(caff, caff->files[texfile[tex]].asset) : NULL;
	ds->tex = tex < (uint32_t)ntex ? (int16_t)tex : -1;
	ds->placeholder = name && strncmp(name, "_0x008C4635", 11) == 0;
	ds->vboff = vb->off;
	ds->pos = malloc((size_t)(vb->count ? vb->count : 1) * sizeof(*ds->pos));
	ds->uv = malloc((size_t)(vb->count ? vb->count : 1) * sizeof(*ds->uv));
	ds->state = calloc(vb->count ? vb->count : 1, 1);
	ds->idx = malloc((size_t)(ntris > 0 ? ntris : 1) * 3 * sizeof(*ds->idx));
	if (!ds->pos || !ds->uv || !ds->state || !ds->idx) {
		free(ds->pos);
		free(ds->uv);
		free(ds->state);
		free(ds->idx);
		ds->pos = NULL;
		ds->uv = NULL;
		ds->state = NULL;
		ds->idx = NULL;
		return;
	}
	for (uint32_t vi = 0; vi < vb->count; vi++) {
		struct beanVtx v;
		int ok = readVtx(gpu, vb, vi, uvscale, skinned, &v);
		for (int j = 0; ok && j < 3; j++)
			ok = fabsf(v.pos[j]) <= 1e8f;
		if (ok && inst) {
			applyInst(v.pos, inst);
		}
		if (ok) {
			memcpy(ds->pos[vi], v.pos, sizeof(ds->pos[vi]));
			memcpy(ds->uv[vi], v.uv, sizeof(ds->uv[vi]));
			ds->state[vi] |= GEVR_BEAN_V_READ;
		}
		if (vertexDropped(path, vb->off, vi))
			ds->state[vi] |= GEVR_BEAN_V_DROPPED;
	}
	if (ntris > 0)
		memcpy(ds->idx, tris, (size_t)ntris * 3 * sizeof(*ds->idx));
	ds->ntris = ntris > 0 ? ntris : 0;
	ds->nverts = vb->count;
}

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
	int original, levelFade, rigidDecals;
	uint8_t texAlpha[MAX_TEX], texSoft[MAX_TEX], *triPass = NULL;
	int passCap = 0;
	int section = -1, afterpieces = 0;
	uint32_t sectionend = 0, repeatvb = ~0u;
	int triggerCuffs;
	uint32_t end, pc, vb = 0, tex = 0, matpc = 0, vs = 0, masktex = ~0u;
	uint8_t maskslot = 0;
    uint32_t reflectSpot=~0u,reflectLand=~0u; int reflectForm=0;
    uint8_t reflectAmount[4]={0},reflectionAlphaBase=0; uint32_t reflectionColour=0;
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
				GEVR_BEAN_LOG("gevr bean: %s pose has %u bones, keeping %d\n", source, count, MAX_BONES);
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
	levelFade = strstr(source, "background/") || strstr(source, "background\\");
	rigidDecals = strstr(source, "/prop/") || strstr(source, "\\prop\\") || strstr(source, "/gun/") ||
	              strstr(source, "\\gun\\");
	triggerCuffs = strstr(source, "/gun/trigger/") != NULL || strstr(source, "\\gun\\trigger\\") != NULL;
	const int fpCards = fpGunFile(source);
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
		} else if (type == 0x02) {
			vs = sz >= 8 ? be32(stream + pc + 4) : 0;
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
            tex = surfaceTex(&caff, stream, pc, sz, texfile, ntex, source, tex);
			matpc = pc;
            masktex = ~0u;
            maskslot = 0;
            reflectSpot=reflectLand=~0u; reflectForm=0;
            memset(reflectAmount,0,sizeof(reflectAmount)); reflectionColour=0; reflectionAlphaBase=0;
            int spotSlot=-1,landSlot=-1,pictureSlot=-1,scratchSlot=-1,validInputs=0;uint32_t scratch=~0u;
            for (uint32_t k = 0; 12 + 8*k + 8 <= sz && fits(pc+12+8*k, 8, slen); k++) {
                uint32_t other = be32(stream+pc+12+8*k);
                int slot=be32(stream+pc+16+8*k)>>16;
                validInputs += other<(uint32_t)ntex;
                int sphere=sphereTex(&caff,texfile,ntex,other);
                if(sphere==1){reflectSpot=other;spotSlot=slot;}
                else if(sphere==2){reflectLand=other;landSlot=slot;}
                else if(other==tex)pictureSlot=slot;
                if(scratchTex(&caff,texfile,ntex,other)){scratch=other;scratchSlot=slot;}
                if (other != tex && other < (uint32_t)ntex && masktex == ~0u) {
                    masktex = other;
                    maskslot = be32(stream+pc+16+8*k) >> 16;
                }
            }
            reflectForm=spotSlot==0 && landSlot==1 && pictureSlot==2;
            if(!reflectForm && validInputs==3 && spotSlot==1 && landSlot==2 && pictureSlot==0)
                reflectForm=3; /* CCTV: the diffuse also tints R. */
            if(!reflectForm && validInputs==2 && scratchSlot==1 && pictureSlot==0) {
                reflectForm=4;reflectSpot=~0u;reflectLand=scratch;
            }
            if(!reflectForm && scratchSlot==0 && pictureSlot==1) {
                reflectForm=2;reflectSpot=~0u;reflectLand=scratch;
            }
            alpha = 0xff;
			colour = 0xffffffu;
		} else if (type == 0x06 && sz >= 28 && be32(stream + pc + 4) == 0 &&
		           (be32(stream + pc + 8) >> 16) == 0x0c) {
            int twoConstants=reflectForm>0 && reflectForm<4 && sz>=44 && (be32(stream+pc+8)&0xffff)==2;
            if((reflectForm==2 || reflectForm==3) && twoConstants)
                for(int k=0;k<3;k++)if(fabsf(bef(stream+pc+28+k*4)-1.0f)>1e-6f)twoConstants=0;
            if((reflectForm==2 || reflectForm==3) && !twoConstants)reflectForm=0;
            if(twoConstants) for(int k=0;k<3;k++) {
                float v=bef(stream+pc+12+k*4);
                reflectAmount[k]=v>=1 ? 255 : v<=0 ? 0 : (uint8_t)(v*255+.5f);
            }
            if(reflectForm==4) {
                int helmet=sz>=60 && (be32(stream+pc+8)&0xffff)==3;
                for(int k=0;helmet && k<4;k++)
                    helmet=fabsf(bef(stream+pc+12+k*4)-1.0f)<1e-6f &&
                        fabsf(bef(stream+pc+28+k*4)-bef(stream+pc+28))<1e-6f &&
                        fabsf(bef(stream+pc+44+k*4)-bef(stream+pc+44))<1e-6f;
                if(!helmet)reflectForm=0;
                else {
                    reflectionAlphaBase=255;reflectionColour=0;
                    for(int k=0;k<4;k++) {
                        float r=bef(stream+pc+44+k*4),c=bef(stream+pc+28+k*4);
                        reflectAmount[k]=r<=0 ? 0 : r>=1 ? 255 : (uint8_t)(r*255+.5f);
                        uint32_t byte=c<=0 ? 0 : c>=1 ? 255 : (uint32_t)(c*255+.5f);
                        reflectionColour=(reflectionColour<<8)|byte;
                    }
                }
            }
            /* c12 controls reflection on this shape; c13 tints its diffuse.
             * The alpha stays c12's: a glass pane's grey (the window's 0.56,
             * the glassware's 0.51), which the release multiplies into the
             * picture's (DAB gebean.c, c_constant0). c13's is 1, and the
             * tower window drew at nearly twice its opacity. */
            uint32_t colourOffset=twoConstants ? 28 : 12;
            float a = bef(stream + pc + 24);
			alpha = a >= 1.0f ? 0xff : a <= 0.0f ? 0 : (uint8_t)(a * 255.0f + 0.5f);
			colour = 0;
			for (int k = 0; k < 3; k++) {
				float cv = bef(stream + pc + colourOffset + k * 4);
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
				d->piece = (int8_t)(part < 0 ? -1 : part > 127 ? 127 : part);
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
				d->vs = vs;
				d->vb = vb;
				d->tex = tex;
                d->reflectionDiffuse=reflectForm==4 ? 2 : reflectForm==3;
                d->reflectionColour=reflectionColour;d->reflectionAlphaBase=reflectionAlphaBase;
                d->reflectSpot=reflectForm ? reflectSpot : ~0u;
                d->reflectLand=reflectForm ? reflectLand : ~0u;
                memcpy(d->reflectAmount,reflectAmount,4);
                d->masktex = masktex;
                d->maskslot = maskslot;
                d->matpc = matpc;
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
				GEVR_BEAN_LOG("gevr bean: draw cap %d hit in %s\n", MAX_DRAWS, source);
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
	mesh->nativeChrome = strstr(source, "/prop/nintendologo/") != NULL ||
                        strstr(source, "\\prop\\nintendologo\\") != NULL;
    mesh->watchArm = !original && ntex == 12 && strstr(source, "suitlfhand") != NULL;
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
	/* Props also own scrolling pictures (the helicopter at 0.9 repeats/s). */
	readTextureScroll(mesh, data, dlen, stream, slen, &caff, texfile);
	fixPlaceholders(mesh, draws, ndraws, source);
	textureAlphaFlags(mesh, texAlpha, texSoft);
    if (levelFade) markLevelBlendShaders(mesh, draws, ndraws, ibs, nibs, data, dlen, gpu, glen, uv20);
	/* A prop's draws are kept for GoldenEye's monitor screens (DAB gebean.c
	 * beanFindScreens() and its users, in gevr_bean.c), and which of them and
	 * which buffer vertices each triangle came from. */
	int triSrcCap = 0;
	if (!levelFade && ndraws > 0 && (strstr(source, "/prop/") || strstr(source, "\\prop\\"))) {
		mesh->draws = calloc((size_t)ndraws, sizeof(*mesh->draws));
		mesh->ndraws = mesh->draws ? ndraws : 0;
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
        const void *drawReflection=NULL;
        if(d->tex<(uint32_t)mesh->ntex &&
           (d->reflectSpot<(uint32_t)mesh->ntex || d->reflectSpot==~0u) && d->reflectLand<(uint32_t)mesh->ntex && (d->reflectAmount[0]|d->reflectAmount[1]|d->reflectAmount[2])) {
            snprintf(key,sizeof(key),"%s#reflection-%x",source,d->matpc);
            drawReflection=mesh->reflection[d->tex]=d->reflectionDiffuse==2
                ? gevrBeanTexReflectRgba(NULL,mesh->standin[d->reflectLand],d->reflectAmount,key)
                : gevrBeanTexReflect(d->reflectSpot==~0u ? NULL : mesh->standin[d->reflectSpot],
                    mesh->standin[d->reflectLand],d->reflectAmount,key);
        }
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
        struct beanLayers layers = levelLayers(mesh, d, &vbrec, gpu, tris, ntris, levelFade);
        int baseTex=layers.base, otherTex=layers.other;
        /* DAB beanArmGlass: 10 is a grey shader input, 11 is the black,
         * translucent glass the release draws once over its watch dial. */
        int watchGlass = !original && ntex == 12 && strstr(source, "suitlfhand") &&
            (d->tex == 10 || d->tex == 11) &&
            (d->masktex == 10 || d->masktex == 11 || d->tex == 11);
        if (watchGlass) baseTex = 11;
        int mask=layers.mask, blendpic=layers.blend, decalpic=layers.decal;
		if (mesh->draws) {
			keepScreenDraw(&mesh->draws[di], source, &caff, texfile, ntex, d->tex, gpu, &vbrec, uvscale, numbones > 0,
			               d->inst >= 0 && d->inst < ninst ? insts[d->inst] : NULL, tris, ntris);
			mesh->draws[di].prim = (uint8_t)d->prim;
			mesh->draws[di].alphatest = (uint8_t)(d->alphatest != 0);
		}
		for (int t = 0; t < ntris; t++) {
			struct beanVtx corner[3];
			int ok = 1;
			for (int k = 0; k < 3; k++) {
				if (!readVtx(gpu, &vbrec, tris[t * 3 + k], uvscale, numbones > 0, &corner[k])) {
					ok = 0;
					break;
				}
                const uint8_t *raw = gpu + vbrec.off + tris[t*3+k]*vbrec.stride;
                if (mask || blendpic || decalpic) {
                    corner[k].uv[0] = (int16_t)be16(raw+20)/uvscale;
                    corner[k].uv[1] = (int16_t)be16(raw+22)/uvscale;
                }
                for (int j=0; j<3; j++) if (fabsf(corner[k].pos[j]) > 1e8f) ok = 0;
				/* DAB ignores the native/fog alpha outside a fading material.
				 * Nintendo's blended shader has no material fade and many zeros.
				 * A first-person gun's material alpha fades nothing (below). */
				if (!(d->blend && ((d->alpha < 255 && !fpCards) || levelFade)))
					corner[k].argb |= 0xff000000u;
                if (mask) corner[k].argb = (corner[k].argb & 0xffffff) | (uint32_t)raw[24]<<24;
				/* DAB tints a level vertex by its material only in a draw of
				 * positions alone; the water's constant (0x131313, alpha 0) is
				 * its shader's, not a colour (gebeanLevelTriangles()). */
				if (!levelFade || vbrec.stride == 12)
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
				int texi = decalpic ? otherTex : baseTex;
				int picAlpha = texi >= 0 && texi < MAX_TEX && texAlpha[texi];
				int picSoft = texi >= 0 && texi < MAX_TEX && texSoft[texi];
				/* Glass is a prop's (DAB gebeanBuildRigid()); DAB's first-person
				 * guns never take the material alpha. The RC-P90's magazine (c12
				 * 0.148 0.124 0.035, alpha 0, in the blended pass) drew as nothing
				 * in the hand, the gun seen through where it is. */
				int fades = 0, glass = !levelFade && !fpCards && d->blend && d->alpha < 255, xlu, cut;
				for (int k = 0; k < 3; k++)
					fades |= (corner[k].argb >> 24) < 0xf0;
				/* A level face is translucent by its picture or its vertices,
				 * never by the material constant, and an alpha-tested opaque
				 * draw is a cut-out (gebeanstage.c texIsXlu(), triFades(),
				 * alphaTested()). A model's is glass - the blended pass under
				 * a material alpha (gebean.c gebeanBuildRigid()) - or a picture
				 * with alpha that fades or is soft (xblamesh.c
				 * xblaMeshDrawSpan()). Anything else in the blended pass is
				 * drawn as the opaque surface it is, writing depth: Nintendo's
				 * logo drew two thirds of itself unsorted, without depth. */
				if (levelFade)
					xlu = !(d->alphatest && !d->blend) && (picSoft || ((picAlpha || d->blend) && fades));
				else
					xlu = glass || (picAlpha && (fades || picSoft));
				/* DAB's opaque leaf cuts a picture with alpha (writeLeaf()) */
				cut = (!watchGlass && d->alphatest) || (!levelFade && texi >= 0 && mesh->texCut[texi]) ||
				      (levelFade && d->blend && picAlpha && !xlu);
				uint8_t a = glass ? d->alpha : xlu ? 254 : 255;
				if (!pushTri(mesh, &corner[0], &corner[1], &corner[2], texi, a, cut,
						d->pal, d->numpal, boneSk, muzzle, slideb)) {
					break;
				}
				/* DAB fpDrawIsCard(): piece 0 is the flash; and beanGunExtent()
				 * leaves the HD gun's small pictures (64x64 and under) out with
				 * the hand's, which takes every flash card - the rocket
				 * launcher's in its mouth is on no piece and no muzzle bone */
				if (fpCards && (d->piece == 0 ||
				                (!original && texi >= 0 && mesh->texW[texi] <= 64 && mesh->texH[texi] <= 64)))
					mesh->tris[mesh->ntris - 1].flags |= GEVR_BEAN_F_FLASH;
				mesh->tris[mesh->ntris - 1].reflection=texi==(int)d->tex ? drawReflection : NULL;
                mesh->tris[mesh->ntris - 1].reflectionDiffuse=d->reflectionDiffuse;
                mesh->tris[mesh->ntris - 1].reflectionColour=d->reflectionColour;
                mesh->tris[mesh->ntris - 1].reflectionAlphaBase=d->reflectionAlphaBase;
				mesh->tris[mesh->ntris - 1].cuff = d->cuff;
				if (mesh->draws && mesh->ntris > triSrcCap) {
					int cap = mesh->tricaps > mesh->ntris ? mesh->tricaps : mesh->ntris;
					struct gevrBeanTriSrc *grown = realloc(mesh->triSrc, (size_t)cap * sizeof(*grown));
					if (grown) {
						mesh->triSrc = grown;
						triSrcCap = cap;
					} else
						freeScreenSources(mesh);
				}
				if (mesh->draws) {
					struct gevrBeanTriSrc *src = &mesh->triSrc[mesh->ntris - 1];
					const struct gevrBeanDrawSrc *ds = &mesh->draws[di];
					src->draw = (int16_t)di;
					src->pane = 0;
					for (int k = 0; k < 3; k++) {
						src->vi[k] = tris[t * 3 + k];
						if (ds->state && src->vi[k] < ds->nverts && (ds->state[src->vi[k]] & GEVR_BEAN_V_DROPPED))
							src->pane |= (uint8_t)(1 << k);
					}
				}
				notePass(&triPass, &passCap, mesh->ntris - 1, d->blend ? LDECAL_PASS_BLEND : 0);
                if (blendpic || decalpic) {
                    int seen = decalpic;
                    for (int k=0; k<3; k++) {
                        const uint8_t *raw = gpu+vbrec.off+tris[t*3+k]*32;
                        corner[k].uv[0] = (int16_t)be16(raw+16)/uvscale;
                        corner[k].uv[1] = (int16_t)be16(raw+18)/uvscale;
                        int a = decalpic ? 255 : raw[24];
                        seen |= a != 0;
                        corner[k].argb = (corner[k].argb & 0xffffff) | (uint32_t)a<<24;
                    }
                    /* The twin is drawn blended over its own draw (DAB: blend, twin) */
                    if (seen && pushTri(mesh, corner, corner+1, corner+2, decalpic ? baseTex : otherTex,
                            254, 0, d->pal, d->numpal, boneSk, muzzle, slideb)) {
                        mesh->tris[mesh->ntris-1].flags |= GEVR_BEAN_F_DECAL;
                        notePass(&triPass, &passCap, mesh->ntris - 1, LDECAL_PASS_BLEND | LDECAL_PASS_TWIN);
                    }
                }
			}
		}
		free(tris);
	}
	if (levelFade)
		markLevelDecals(mesh, triPass, passCap < mesh->ntris ? passCap : mesh->ntris, texAlpha,
		                levelDecalScale(source));
	else if (rigidDecals)
		markRigidDecals(mesh, texAlpha);
	free(triPass);
	free(draws);
	free(ibs);
	gevrCaffClose(&caff);
	free(file);
	if (mesh->ntris <= 0) {
		gevrBeanMeshFree(mesh);
		return 0;
	}
	GEVR_BEAN_LOG("gevr bean: %s %d tris, uv %.0f\n", source, mesh->ntris, uvscale);
	*out = mesh;
	return 1;
}

void gevrBeanMeshFree(struct gevrBeanMesh *m)
{
	if (!m) {
		return;
	}
	freeScreenSources(m);
	free(m->tris);
	free(m);
}
