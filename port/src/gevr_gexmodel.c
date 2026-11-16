/**
 * A GoldenEye X gun (a Perfect Dark model file) rebuilt as a GoldenEye model
 * file in cartridge layout, so the engine's own loader takes it from there
 * (ob.c load_resource -> gevr_model.c gevrModelConvert -> texture expansion).
 * docs/gex-weapons.md has the background.
 *
 * The two formats share their node and record layouts and opcode numbers
 * (PD position = GE group 0x02, gun display list = DL 0x04, 0x11, toggle =
 * switch 0x12, star gunfire = DL primary 0x16). What differs:
 *  - PD's header sits at the file's start with a parts table; GE's lives in
 *    the game and the file starts with the switch table, then the textures;
 *  - PD's vertices are 12 bytes (x, y, z, flags, a colour byte offset, s, t)
 *    with colours in a table loaded by G_COL; GE's are 16 with the colour
 *    inline. Each vertex takes the colour of the table loaded before the
 *    G_VTX that reads it, G_VTX lengths become 16 per vertex, and the G_COLs
 *    go (fast3d has no no-op for them; the lists don't branch, so nothing
 *    points into them);
 *  - a position record has no child-group pointer and no radius (drawdist);
 *  - texture numbers name GE-X's table: they are mapped to free ids from
 *    GEVR_GEX_TEX_FIRST, which image.c texLoad serves from the GE-X ROM,
 *    except GE-X's copies of GoldenEye's textures, which take GoldenEye's
 *    ids (the caller's pairs).
 * The display lists go last, in the order modelIterateDisplayLists visits
 * them, as the texture expansion (objecthandler_2.c) needs.
 */

#include <stdlib.h>
#include <string.h>
#include <PR/ultratypes.h>
#include "system.h"
#include "gevr_gex.h"
#include "gevr_gexmodel.h"

#define PD_NODE_POSITION   0x02
#define PD_NODE_GUNDL      0x04
#define PD_NODE_11         0x11
#define PD_NODE_TOGGLE     0x12
#define PD_NODE_STARGUNFIRE 0x16

#define MAX_NODES   512
#define MAX_ARRAYS  64
#define MAX_DLS     128
#define MAX_TEXMAP  (GEVR_GEX_TEX_LAST - GEVR_GEX_TEX_FIRST + 1)

static u32 rd32(const u8 *p) { return (u32)p[0] << 24 | (u32)p[1] << 16 | (u32)p[2] << 8 | p[3]; }
static u16 rd16(const u8 *p) { return (u16)(p[0] << 8 | p[1]); }
static void wr32(u8 *p, u32 v) { p[0] = v >> 24; p[1] = v >> 16; p[2] = v >> 8; p[3] = v; }
static void wr16(u8 *p, u16 v) { p[0] = v >> 8; p[1] = v; }
#define OFS(a) ((a) & 0x00ffffff)
#define SEGOF(a) ((a) >> 24)

u8 *gevrGexPendingFile;
u32 gevrGexPendingLen;

/* --------------------------------------------------- texture id mapping */

static s16 s_texGex[MAX_TEXMAP];   /* GE id - GEVR_GEX_TEX_FIRST -> GE-X texture number */
static s32 s_texCount;

static u32 gexMapTexture(u32 gexnum)
{
	s32 i;

	for (i = 0; i < s_texCount; i++) {
		if (s_texGex[i] == (s16)gexnum) {
			return GEVR_GEX_TEX_FIRST + i;
		}
	}
	if (s_texCount >= MAX_TEXMAP) {
		return 0;
	}
	s_texGex[s_texCount] = (s16)gexnum;
	return GEVR_GEX_TEX_FIRST + s_texCount++;
}

const u8 *gevrGexTextureForId(s32 id, u32 *len)
{
	if (id < GEVR_GEX_TEX_FIRST || id >= GEVR_GEX_TEX_FIRST + s_texCount) {
		return NULL;
	}
	return gevrGexTextureData(s_texGex[id - GEVR_GEX_TEX_FIRST], len);
}

/* ------------------------------------------------------------- the build */

struct pdArray {      /* a vertex array of the source and its place in the output */
	u32 src, count, dst;
	u8 *colours;       /* RGBA per vertex, from the G_COL in force when it was loaded */
	u8 *seen;
	s32 star;          /* star gunfire: read through segment 4, not segment 5 */
	u32 extra;         /* cap vertices after the array's own (gexCap): */
	u16 *extraSrc;     /* each one a copy of this vertex of the array */
};

struct pdDl {
	u32 src, dst, len, outLen;
	struct pdArray *arr;
	s32 flash;          /* uses the extra muzzle matrix, not PD's animated flash joints */
	u8 *cap;            /* PD commands put in before the list's G_ENDDL (gexCap) */
	u32 capLen;
};

struct build {
	const u8 *pd;
	u32 pdLen;
	u32 nodeSrc[MAX_NODES], nodeDst[MAX_NODES], rodataDst[MAX_NODES];
	s32 numNodes;
	struct pdArray arrays[MAX_ARRAYS];
	s32 numArrays;
	struct pdDl dls[MAX_DLS];
	s32 numDls;
	u32 flashNode;      /* PD part mapped to GoldenEye's muzzle flash switch */
	s32 flashMatrix;    /* appended after the original skeleton, or -1 */
	const char *name;
	const u16 *texPairs;   /* GE-X number, GoldenEye id, ..., 0 */
	s32 cartridgeTextures; /* table entries that took a GoldenEye id */
	s32 failed;
};

static void fail(struct build *b, const char *what, u32 v)
{
	if (!b->failed) {
		sysLogPrintf(LOG_ERROR, "gexmodel: %s: %s (0x%x)", b->name, what, v);
	}
	b->failed = 1;
}

static s32 nodeIndex(struct build *b, u32 src)
{
	s32 i;

	for (i = 0; i < b->numNodes; i++) {
		if (b->nodeSrc[i] == src) {
			return i;
		}
	}
	return -1;
}

/* every node, in tree order (child first): the order GE iterates them */
static void collectNodes(struct build *b, u32 ofs)
{
	while (ofs != 0 && !b->failed) {
		if (ofs + 24 > b->pdLen || b->numNodes >= MAX_NODES) {
			fail(b, "node out of range", ofs);
			return;
		}
		if (nodeIndex(b, ofs) >= 0) {
			return;
		}
		b->nodeSrc[b->numNodes++] = ofs;
		if (rd32(b->pd + ofs + 0x14)) {
			collectNodes(b, OFS(rd32(b->pd + ofs + 0x14)));
		}
		ofs = rd32(b->pd + ofs + 0x0c) ? OFS(rd32(b->pd + ofs + 0x0c)) : 0;
	}
}

/*
 * GE-X's textures for GoldenEye's own guns are GoldenEye's, re-encoded in
 * Perfect Dark's format, and re-encoded they miss the HD packs' checksums
 * (user: the KF7's trigger lost its HD look). The ones the caller pairs
 * with a GoldenEye texture take its id, which texLoad reads from the
 * cartridge; the rest are mapped to GE-X ids.
 */
static u32 mapTexture(struct build *b, u32 gexnum)
{
	const u16 *p;

	for (p = b->texPairs; p != NULL && p[0] != 0; p += 2) {
		if (p[0] == gexnum) {
			return p[1];
		}
	}
	return gexMapTexture(gexnum);
}

static u32 rodataSize(u8 type)
{
	switch (type) {
	case PD_NODE_POSITION:    return 0x1c;   /* GE's group, four bytes longer */
	case PD_NODE_GUNDL:       return 0x14;
	case PD_NODE_11:          return 0x20;
	case PD_NODE_TOGGLE:      return 0x08;
	case PD_NODE_STARGUNFIRE: return 0x10;
	}
	return 0;
}

static struct pdArray *addArray(struct build *b, u32 src, u32 count, s32 star)
{
	s32 i;
	struct pdArray *a;

	for (i = 0; i < b->numArrays; i++) {
		if (b->arrays[i].src == src) {
			return &b->arrays[i];
		}
	}
	if (b->numArrays >= MAX_ARRAYS || src + count * 12 > b->pdLen) {
		fail(b, "vertex array out of range", src);
		return NULL;
	}
	a = &b->arrays[b->numArrays++];
	a->src = src;
	a->count = count;
	a->star = star;
	a->colours = (u8 *)malloc(4 * (count ? count : 1));
	a->seen = (u8 *)calloc(count ? count : 1, 1);
	if (a->colours != NULL) {
		memset(a->colours, 0xff, 4 * (count ? count : 1));   /* white until a G_COL says */
	}
	return a;
}

static u32 dlLength(struct build *b, u32 src)
{
	u32 p = src;

	while (p + 8 <= b->pdLen) {
		u8 op = b->pd[p];

		if (op == 0x06) {
			fail(b, "display list branches (G_DL) are not handled", p);
			return 0;
		}
		p += 8;
		if (op == 0xb8) {
			return p - src;
		}
	}
	fail(b, "display list runs off the file", src);
	return 0;
}

static s32 isFlashNode(struct build *b, u32 node)
{
	s32 depth;

	for (depth = 0; node && depth < b->numNodes; depth++) {
		if (node == b->flashNode) {
			return 1;
		}
		node = OFS(rd32(b->pd + node + 8));
	}
	return 0;
}

static void addDl(struct build *b, u32 addr, struct pdArray *arr, s32 flash)
{
	if (addr == 0) {
		return;
	}
	if (b->numDls >= MAX_DLS) {
		fail(b, "too many display lists", addr);
		return;
	}
	b->dls[b->numDls].src = OFS(addr);
	b->dls[b->numDls].len = dlLength(b, OFS(addr));
	b->dls[b->numDls].outLen = b->dls[b->numDls].len;
	{
		u32 p;

		for (p = 0; p < b->dls[b->numDls].len; p += 8) {
			if (b->pd[OFS(addr) + p] == 0x07) {
				b->dls[b->numDls].outLen -= 8;
			}
		}
	}
	b->dls[b->numDls].arr = arr;
	b->dls[b->numDls].flash = flash;
	if (flash) {
		/* Even a star list without its own G_MTX must start at the muzzle. */
		b->dls[b->numDls].outLen += 8;
	}
	b->numDls++;
}

/* Pass over a display list: the colour each vertex is loaded with. */
static void colourVertices(struct build *b, struct pdDl *dl)
{
	u32 p, coltab = 0, colcount = 0;

	for (p = dl->src; p < dl->src + dl->len; p += 8) {
		u32 w0 = rd32(b->pd + p), w1 = rd32(b->pd + p + 4);
		u8 op = w0 >> 24;

		if (op == 0x07) {          /* G_COL: a table of 4-byte colours */
			colcount = (w0 & 0xffff) / 4;
			if (SEGOF(w1) == 5) {
				coltab = OFS(w1);
			} else if (dl->arr != NULL && dl->arr->star) {
				/* star gunfire: the colours follow its vertices, 8-aligned */
				coltab = ((dl->arr->src + 12 * dl->arr->count + 7) & ~7u) + OFS(w1);
			} else {
				fail(b, "colour table outside the file", w1);
				return;
			}
			if (coltab + 4 * colcount > b->pdLen) {
				fail(b, "colour table out of range", w1);
				return;
			}
		} else if (op == 0x04 && dl->arr != NULL) {   /* G_VTX */
			u32 n = (w0 & 0xffff) / 12, i, first;

			if (SEGOF(w1) == 5) {
				if (OFS(w1) < dl->arr->src || (OFS(w1) - dl->arr->src) % 12) {
					fail(b, "vertex load outside its array", w1);
					return;
				}
				first = (OFS(w1) - dl->arr->src) / 12;
			} else {
				first = OFS(w1) / 12;   /* star gunfire: segment 4 is the array */
			}
			for (i = 0; i < n && first + i < dl->arr->count; i++) {
				const u8 *v = b->pd + dl->arr->src + (first + i) * 12;
				u32 c = v[7] >> 2;

				if (dl->arr->seen[first + i] || coltab == 0) {
					continue;
				}
				if (c < colcount) {
					memcpy(dl->arr->colours + 4 * (first + i), b->pd + coltab + 4 * c, 4);
				}
				dl->arr->seen[first + i] = 1;
			}
		}
	}
}

/*
 * Holes Perfect Dark's own hand hid (user: the Cougar's and the Golden Gun's
 * grips were hollow in the headset): GE-X modelled these grips as open shells,
 * since PD's first-person hand always covered their front and butt. In VR they
 * are seen from below. One line a hole: the GE-X model, the display-list node
 * (its offset in the PD file) and a vertex on the open edge loop to close
 * (model units). capLoop finds the loop from the mesh's own open edges and
 * closes it with triangles between its two halves (these grips are left-right
 * symmetric), in the loop's own texture, vertices and shades.
 */
static const struct gexCap { const char *model; u32 node; s16 at[3]; } s_gexCaps[] = {
	{ "GmaianpistolZ", 0x19c, { -16, -134, -18 } },   /* Cougar: the wooden grip's front strap and butt */
	{ "Gleegun1Z",     0x168, { -31,  -88, -15 } },   /* Golden Gun: the grip's butt */
};

#define CAP_MAX_TRIS  512
#define CAP_MAX_LOOP  64

static s32 capSame(const u8 *pd, u32 arr, u16 a, u16 b)
{
	return !memcmp(pd + arr + 12 * a, pd + arr + 12 * b, 6);
}

static s32 capAt(const u8 *pd, u32 arr, u16 v, const s16 at[3])
{
	return (s16)rd16(pd + arr + 12 * v) == at[0] && (s16)rd16(pd + arr + 12 * v + 2) == at[1]
	    && (s16)rd16(pd + arr + 12 * v + 4) == at[2];
}

static void capAddTri(struct pdDl *dl, u8 *out, u32 *len, const u16 v[3])
{
	struct pdArray *a = dl->arr;
	s32 i;

	wr32(out + *len, 0x04000000 | (2 << 20) | 36);   /* G_VTX: three, into slots 0-2 */
	wr32(out + *len + 4, 0x05000000 | (a->src + 12 * (a->count + a->extra)));
	wr32(out + *len + 8, 0xb1000002);                 /* G_TRI4: (0, 1, 2) */
	wr32(out + *len + 12, 0x00000010);
	*len += 16;
	for (i = 0; i < 3; i++) {
		a->extraSrc[a->extra++] = v[i];
	}
}

/* Closes the open edge loop through `at` in a node's triangles (its opaque
 * list dl, and xlu, its other list on the same array, or NULL), adding the cap
 * to dl (see s_gexCaps). */
static void capLoop(struct build *b, struct pdDl *dl, const struct pdDl *xlu, const s16 at[3])
{
	static u16 tri[CAP_MAX_TRIS][3];
	static u32 texw[CAP_MAX_TRIS][2];
	u16 loop[CAP_MAX_LOOP], left[CAP_MAX_LOOP], right[CAP_MAX_LOOP], cap[3];
	s32 slot[16], n = 0, t, e, f, g, k, len = 0, nl = 0, nr = 0, crossings = 0, start = -1, cross = -1, l, r;
	u32 p, tw0 = 0, tw1 = 0, capLen = 0;
	s32 list;
	struct pdArray *a = dl->arr;
	const u8 *pd = b->pd;
	f32 mid = 0.0f;

	if (a == NULL || a->star) return;
	/* the lists' triangles, as indices into their array, with the texture each was drawn in */
	for (list = 0; list < 2; list++) {
		const struct pdDl *in = list ? xlu : dl;

		for (k = 0; k < 16; k++) slot[k] = -1;
		for (p = in != NULL && in->arr == a ? in->src : 0; in != NULL && in->arr == a && p < in->src + in->len && n < CAP_MAX_TRIS; p += 8) {
			const u32 w0 = rd32(pd + p), w1 = rd32(pd + p + 4);
			const u8 op = w0 >> 24;

			if (op == 0x04 && SEGOF(w1) == 5 && OFS(w1) >= a->src) {
				const s32 count = ((w0 >> 20) & 0xf) + 1, v0 = (w0 >> 16) & 0xf, first = (OFS(w1) - a->src) / 12;

				for (k = 0; k < count && v0 + k < 16; k++) slot[v0 + k] = first + k < (s32)a->count ? first + k : -1;
			} else if (op == 0xc0) {
				tw0 = w0; tw1 = w1;
			} else if (op == 0xb1 || op == 0xbf) {
				for (k = 0; k < (op == 0xb1 ? 4 : 1) && n < CAP_MAX_TRIS; k++) {
					const s32 x = op == 0xb1 ? (w1 >> (8 * k)) & 0xf : ((w1 >> 16) & 0xff) / 10;
					const s32 y = op == 0xb1 ? (w1 >> (8 * k + 4)) & 0xf : ((w1 >> 8) & 0xff) / 10;
					const s32 z = op == 0xb1 ? (w0 >> (4 * k)) & 0xf : (w1 & 0xff) / 10;

					if ((x | y | z) == 0 || x > 15 || y > 15 || z > 15 || slot[x] < 0 || slot[y] < 0 || slot[z] < 0) continue;
					tri[n][0] = slot[x]; tri[n][1] = slot[y]; tri[n][2] = slot[z];
					texw[n][0] = tw0; texw[n][1] = tw1;
					n++;
				}
			}
		}
	}
	/* the loop: from the open edge (a triangle's edge no other triangle shares,
	 * by position) leaving `at`, each next one leaving where the last arrived */
	for (;;) {
		s32 found = -1;
		const u16 from = len ? loop[len - 1] : 0;

		for (t = 0; t < n && found < 0; t++) {
			for (e = 0; e < 3 && found < 0; e++) {
				const u16 v0 = tri[t][e], v1 = tri[t][(e + 1) % 3];
				s32 shared = 0;

				if (len ? !capSame(pd, a->src, v0, from) : !capAt(pd, a->src, v0, at)) continue;
				for (f = 0; f < n && !shared; f++) {
					for (g = 0; g < 3 && !shared; g++) {
						const u16 w0 = tri[f][g], w1 = tri[f][(g + 1) % 3];

						shared = f != t && ((capSame(pd, a->src, v0, w0) && capSame(pd, a->src, v1, w1))
						                 || (capSame(pd, a->src, v0, w1) && capSame(pd, a->src, v1, w0)));
					}
				}
				if (!shared) found = t * 3 + e;
			}
		}
		if (found < 0 || len >= CAP_MAX_LOOP) { len = 0; break; }
		if (start < 0) { start = found / 3; loop[len++] = tri[found / 3][found % 3]; }
		loop[len] = tri[found / 3][(found % 3 + 1) % 3];
		if (capSame(pd, a->src, loop[len], loop[0])) break;   /* round */
		len++;
	}
	if (len < 3) {
		sysLogPrintf(LOG_ERROR, "gexmodel: %s: no open loop at %d %d %d", b->name, at[0], at[1], at[2]);
		return;
	}
	/* its two halves either side of its mean x, joined by two crossing edges */
	for (k = 0; k < len; k++) mid += (s16)rd16(pd + a->src + 12 * loop[k]) / (f32)len;
#define CAP_LEFT(i) ((f32)(s16)rd16(pd + a->src + 12 * loop[((i) % len + len) % len]) < mid)
	for (k = 0; k < len; k++) {
		if (CAP_LEFT(k) != CAP_LEFT(k + 1)) crossings++;
		if (CAP_LEFT(k) && !CAP_LEFT(k + 1)) cross = k;
	}
	if (crossings != 2 || cross < 0) {
		sysLogPrintf(LOG_ERROR, "gexmodel: %s: the loop at %d %d %d is not two halves", b->name, at[0], at[1], at[2]);
		return;
	}
	for (k = cross; CAP_LEFT(k) && nl < len; k--) left[nl++] = loop[(k % len + len) % len];
	for (k = cross + 1; !CAP_LEFT(k) && nr < len; k++) right[nr++] = loop[k % len];
#undef CAP_LEFT
	dl->cap = (u8 *)malloc(8 + 16 * (nl + nr));
	a->extraSrc = (u16 *)malloc(sizeof(u16) * 3 * (nl + nr));
	if (dl->cap == NULL || a->extraSrc == NULL) return;
	if (texw[start][0] != 0) {   /* the texture the loop's own faces are drawn in */
		wr32(dl->cap, texw[start][0]);
		wr32(dl->cap + 4, texw[start][1]);
		capLen = 8;
	}
	/* zipped up from one crossing edge to the other, the shorter diagonal first,
	 * wound against the loop (as the faces it closes) */
	for (l = 0, r = 0; l < nl - 1 || r < nr - 1;) {
		s32 advanceRight = l >= nl - 1;

		if (!advanceRight && r < nr - 1) {
			f32 dl2 = 0.0f, dr2 = 0.0f;

			for (k = 0; k < 3; k++) {
				const f32 lr = (s16)rd16(pd + a->src + 12 * left[l + 1] + 2 * k) - (s16)rd16(pd + a->src + 12 * right[r] + 2 * k);
				const f32 rl = (s16)rd16(pd + a->src + 12 * left[l] + 2 * k) - (s16)rd16(pd + a->src + 12 * right[r + 1] + 2 * k);

				dl2 += lr * lr;
				dr2 += rl * rl;
			}
			advanceRight = dr2 < dl2;
		}
		if (advanceRight) {
			cap[0] = left[l]; cap[1] = right[r + 1]; cap[2] = right[r];
			r++;
		} else {
			cap[0] = left[l]; cap[1] = left[l + 1]; cap[2] = right[r];
			l++;
		}
		capAddTri(dl, dl->cap, &capLen, cap);
	}
	dl->capLen = capLen;
	dl->outLen += capLen;
	sysLogPrintf(LOG_NOTE, "gexmodel: %s: closed a %d-vertex hole with %u triangles", b->name, len, a->extra / 3);
}

/* Writes the output; with out NULL only measures it. */
static u32 emit(struct build *b, u8 *out, u32 numSwitches, const u32 *switchNodes)
{
	const u8 *pd = b->pd;
	const u32 numTex = rd16(pd + 0x16);
	const u32 texSrc = OFS(rd32(pd + 0x18));
	u32 pos = 0, i, k;

	/* the switch table (filled at the end, once the nodes have offsets) */
	pos += 4 * numSwitches;

	/* the texture table, ids mapped */
	for (i = 0; i < numTex; i++) {
		const u8 *s = pd + texSrc + 12 * i;

		if (out) {
			u32 id = rd32(s);

			memcpy(out + pos, s, 12);
			if (SEGOF(id) == 0) {
				u32 mapped = mapTexture(b, id & 0xfff);

				wr32(out + pos, mapped);
				b->cartridgeTextures += mapped < GEVR_GEX_TEX_FIRST;
			} else {
				fail(b, "embedded textures are not handled", id);
			}
		}
		pos += 12;
	}

	/* nodes, then their records */
	for (i = 0; i < (u32)b->numNodes; i++) {
		b->nodeDst[i] = pos;
		pos += 24;
	}
	for (i = 0; i < (u32)b->numNodes; i++) {
		u8 type = pd[b->nodeSrc[i] + 1];

		b->rodataDst[i] = pos;
		pos += rodataSize(type);
	}

	/* the vertex arrays, 16 bytes a vertex */
	for (k = 0; k < (u32)b->numArrays; k++) {
		b->arrays[k].dst = pos;
		pos += 16 * (b->arrays[k].count + b->arrays[k].extra);
	}

	/* the display lists, last, in iteration order */
	for (k = 0; k < (u32)b->numDls; k++) {
		b->dls[k].dst = pos;
		pos += b->dls[k].outLen;
	}

	if (out == NULL || b->failed) {
		return pos;
	}

	for (i = 0; i < numSwitches; i++) {
		s32 n = switchNodes[i] ? nodeIndex(b, switchNodes[i]) : -1;

		wr32(out + 4 * i, n >= 0 ? 0x05000000 | b->nodeDst[n] : 0);
	}

	for (i = 0; i < (u32)b->numNodes; i++) {
		const u8 *s = pd + b->nodeSrc[i];
		u8 *d = out + b->nodeDst[i];
		u8 type = s[1];
		const u8 *rs = pd + OFS(rd32(s + 4));
		u8 *rd = out + b->rodataDst[i];
		u32 f;
		s32 n;

		/* the node: type, record, parent, next, prev, child */
		wr16(d, type);
		wr16(d + 2, 0);
		wr32(d + 4, 0x05000000 | b->rodataDst[i]);
		for (f = 8; f < 24; f += 4) {
			u32 ref = rd32(s + f);

			n = ref ? nodeIndex(b, OFS(ref)) : -1;
			wr32(d + f, n >= 0 ? 0x05000000 | b->nodeDst[n] : 0);
		}

		switch (type) {
		case PD_NODE_POSITION:
			memcpy(rd, rs, 0x14);          /* pos, part (joint id), matrix ids */
			wr32(rd + 0x14, 0);            /* no child group */
			memcpy(rd + 0x18, rs + 0x14, 4); /* drawdist stands for the radius */
			break;
		case PD_NODE_GUNDL: {
			u32 opa = rd32(rs), xlu = rd32(rs + 4), verts = rd32(rs + 12);

			memset(rd, 0, 0x14);
			for (k = 0; k < (u32)b->numDls; k++) {
				if (opa && b->dls[k].src == OFS(opa)) wr32(rd + 0, 0x05000000 | b->dls[k].dst);
				if (xlu && b->dls[k].src == OFS(xlu)) wr32(rd + 4, 0x05000000 | b->dls[k].dst);
			}
			for (k = 0; k < (u32)b->numArrays; k++) {
				if (verts && b->arrays[k].src == OFS(verts)) wr32(rd + 12, 0x05000000 | b->arrays[k].dst);
			}
			memcpy(rd + 0x10, rs + 0x10, 2);   /* numvertices */
			for (k = 0; k < (u32)b->numArrays; k++) {
				if (verts && b->arrays[k].src == OFS(verts) && b->arrays[k].extra) {
					wr16(rd + 0x10, b->arrays[k].count + b->arrays[k].extra);   /* and a cap's */
				}
			}
			rd[0x12] = rs[0x13];               /* PD's s16 unk12 is GE's s8 model type */
			break;
		}
		case PD_NODE_11:
			memcpy(rd, rs, 0x20);
			wr32(rd + 0x14, 0);   /* unused by PD; GE reads a node reference here */
			break;
		case PD_NODE_TOGGLE: {
			u32 ref = rd32(rs);

			n = ref ? nodeIndex(b, OFS(ref)) : -1;
			wr32(rd, n >= 0 ? 0x05000000 | b->nodeDst[n] : 0);
			memcpy(rd + 4, rs + 4, 4);
			break;
		}
		case PD_NODE_STARGUNFIRE: {
			u32 verts = rd32(rs + 4), gdl = rd32(rs + 8);

			memset(rd, 0, 0x10);
			memcpy(rd, rs, 4);    /* quads */
			for (k = 0; k < (u32)b->numArrays; k++) {
				if (verts && b->arrays[k].src == OFS(verts)) wr32(rd + 4, 0x05000000 | b->arrays[k].dst);
			}
			for (k = 0; k < (u32)b->numDls; k++) {
				if (gdl && b->dls[k].src == OFS(gdl)) wr32(rd + 8, 0x05000000 | b->dls[k].dst);
			}
			break;
		}
		}
	}

	for (k = 0; k < (u32)b->numArrays; k++) {
		const struct pdArray *a = &b->arrays[k];

		for (i = 0; i < a->count + a->extra; i++) {
			const u32 v = i < a->count ? i : a->extraSrc[i - a->count];   /* a cap's: a copy */
			const u8 *s = pd + a->src + 12 * v;
			u8 *d = out + a->dst + 16 * i;

			memcpy(d, s, 6);          /* x, y, z, local to the selected matrix */
			wr16(d + 6, 0);           /* flag */
			memcpy(d + 8, s + 8, 4);  /* s, t */
			memcpy(d + 12, a->colours + 4 * v, 4);
		}
	}

	for (k = 0; k < (u32)b->numDls; k++) {
		const struct pdDl *dl = &b->dls[k];
		u8 *d = out + dl->dst;
		if (dl->flash) {
			wr32(d, 0x01020040);      /* G_MTX: load modelview, no push */
			wr32(d + 4, 0x03000000 | (b->flashMatrix * 64));
			d += 8;
		}

		for (i = 0; i < dl->len + dl->capLen; i += 8) {
			/* a cap's commands go in before the list's last, its G_ENDDL */
			const u8 *at = i + 8 < dl->len ? pd + dl->src + i
			             : i + 8 < dl->len + dl->capLen ? dl->cap + (i + 8 - dl->len)
			             : pd + dl->src + dl->len - 8;
			u32 w0 = rd32(at), w1 = rd32(at + 4);
			u8 op = w0 >> 24;

			if (op == 0x07) {                      /* G_COL: the colours are inline now */
				continue;
			} else if (op == 0x04 && dl->arr != NULL) {   /* G_VTX: 12 -> 16 bytes a vertex */
				u32 n = (w0 & 0xffff) / 12;

				w0 = (w0 & 0xffff0000) | (16 * n);
				if (SEGOF(w1) == 5) {
					w1 = 0x05000000 | (dl->arr->dst + (OFS(w1) - dl->arr->src) / 12 * 16);
				} else {
					w1 = (w1 & 0xff000000) | (OFS(w1) / 12 * 16);
				}
			} else if (op == 0x01 && SEGOF(w1) == 3 && dl->flash) {
				/* KF7's sprite switches between PD joints 37 and 36. Both
				 * must use the fitted muzzle without touching the arm rig. */
				w1 = 0x03000000 | (b->flashMatrix * 64);
			} else if (op == 0xc0 && (w1 & 0xfff) != 0) {   /* texture: GE-X's number to ours */
				w1 = (w1 & ~0xfffu) | (mapTexture(b, w1 & 0xfff) & 0xfff);
			} else if (op == 0xfd && SEGOF(w1) == 5) {
				fail(b, "embedded texture image", w1);
			}
			wr32(d, w0);
			wr32(d + 4, w1);
			d += 8;
		}
	}

	return b->failed ? 0 : pos;
}

u8 *gevrGexBuildModel(const char *pdname, u32 numSwitches, const s32 *switchParts,
		const u16 *texturePairs, u32 *outLen, u16 *outMatrices, u16 *outTextures)
{
	struct build *b;
	u32 pdLen = 0, root, len, i, k;
	u32 switchNodes[64];
	u8 *pd = gevrGexFileLoad(pdname, &pdLen);
	u8 *out = NULL;

	if (pd == NULL || pdLen < 0x1c || numSwitches > 64) {
		free(pd);
		return NULL;
	}
	b = (struct build *)calloc(1, sizeof(*b));
	b->pd = pd;
	b->pdLen = pdLen;
	b->name = pdname;
	b->texPairs = texturePairs;

	root = OFS(rd32(pd));
	collectNodes(b, root);

	/* Resolve switches before collecting lists: all descendants of switch 1
	 * share a dedicated flash matrix, appended beyond the animation's joints. */
	memset(switchNodes, 0, sizeof(switchNodes));
	{
		const u32 parts = OFS(rd32(pd + 8));
		const u16 numparts = rd16(pd + 0xc);

		for (i = 0; i < numSwitches; i++) {
			for (k = 0; switchParts != NULL && switchParts[i] >= 0 && k < numparts; k++) {
				if ((s16)rd16(pd + parts + 4 * numparts + 2 * k) == switchParts[i]) {
					switchNodes[i] = OFS(rd32(pd + parts + 4 * k));
				}
			}
		}
	}
	b->flashNode = numSwitches > 1 && switchParts != NULL && switchParts[1] == 90 ? switchNodes[1] : 0;
	b->flashMatrix = b->flashNode ? rd16(pd + 0xe) : -1;

	/* each node's arrays and display lists, in iteration order */
	for (i = 0; i < (u32)b->numNodes && !b->failed; i++) {
		const u8 *s = pd + b->nodeSrc[i];
		u8 type = s[1];
		const u8 *rs = pd + OFS(rd32(s + 4));

		if (rodataSize(type) == 0) {
			fail(b, "node type not handled", type);
			break;
		}
		if (type == PD_NODE_GUNDL) {
			struct pdArray *a = rd32(rs + 12) ? addArray(b, OFS(rd32(rs + 12)), rd16(rs + 0x10), 0) : NULL;

			addDl(b, rd32(rs), a, isFlashNode(b, b->nodeSrc[i]));
			addDl(b, rd32(rs + 4), a, isFlashNode(b, b->nodeSrc[i]));
		} else if (type == PD_NODE_STARGUNFIRE) {
			struct pdArray *a = rd32(rs + 4) ? addArray(b, OFS(rd32(rs + 4)), 4 * rd32(rs), 1) : NULL;

			addDl(b, rd32(rs + 8), a, isFlashNode(b, b->nodeSrc[i]));
		}
	}

	for (k = 0; k < (u32)b->numDls && !b->failed; k++) {
		colourVertices(b, &b->dls[k]);
	}
	for (i = 0; i < sizeof(s_gexCaps) / sizeof(s_gexCaps[0]) && !b->failed; i++) {
		s32 n = strcmp(pdname, s_gexCaps[i].model) ? -1 : nodeIndex(b, s_gexCaps[i].node);
		const u8 *s = n >= 0 ? pd + b->nodeSrc[n] : NULL;

		const u32 xlu = s != NULL ? rd32(pd + OFS(rd32(s + 4)) + 4) : 0;
		const struct pdDl *other = NULL;

		for (k = 0; xlu != 0 && k < (u32)b->numDls; k++) {
			if (b->dls[k].src == OFS(xlu)) other = &b->dls[k];
		}
		for (k = 0; s != NULL && s[1] == PD_NODE_GUNDL && k < (u32)b->numDls; k++) {
			if (b->dls[k].src == OFS(rd32(pd + OFS(rd32(s + 4)))) && b->dls[k].arr != NULL && b->dls[k].arr->extra == 0) {
				capLoop(b, &b->dls[k], other, s_gexCaps[i].at);
				break;
			}
		}
	}

	len = b->failed ? 0 : emit(b, NULL, numSwitches, switchNodes);
	if (len) {
		out = (u8 *)calloc(1, len);
		if (emit(b, out, numSwitches, switchNodes) != len) {
			free(out);
			out = NULL;
		}
	}
	if (out != NULL) {
		*outLen = len;
		*outMatrices = rd16(pd + 0xe) + (b->flashMatrix >= 0);
		*outTextures = rd16(pd + 0x16);
		sysLogPrintf(LOG_NOTE, "gexmodel: %s: %d nodes, %d vertex arrays, %d display lists, %u bytes, %d of %u textures GoldenEye's own, %d GE-X ids mapped",
				pdname, b->numNodes, b->numArrays, b->numDls, len, b->cartridgeTextures, rd16(pd + 0x16), s_texCount);
	}
	for (k = 0; k < (u32)b->numArrays; k++) {
		free(b->arrays[k].colours);
		free(b->arrays[k].seen);
		free(b->arrays[k].extraSrc);
	}
	for (k = 0; k < (u32)b->numDls; k++) {
		free(b->dls[k].cap);
	}
	free(b);
	free(pd);
	return out;
}

s32 gevrGexPartMatrix(const char *pdname, s32 part)
{
	u32 pdLen = 0, k;
	u8 *pd = gevrGexFileLoad(pdname, &pdLen);
	s32 mtx = -1;

	if (pd != NULL && pdLen >= 0x1c) {
		const u32 parts = OFS(rd32(pd + 8));
		const u16 numparts = rd16(pd + 0xc);

		for (k = 0; k < numparts && parts + 6 * (u32)numparts <= pdLen; k++) {
			if ((s16)rd16(pd + parts + 4 * numparts + 2 * k) == part) {
				const u32 node = OFS(rd32(pd + parts + 4 * k));
				const u32 ro = node + 8 <= pdLen ? OFS(rd32(pd + node + 4)) : pdLen;

				/* PD's position: pos, joint, its matrix (modelrodata_position) */
				if (ro + 0x10 <= pdLen && pd[node + 1] == PD_NODE_POSITION) {
					mtx = (s16)rd16(pd + ro + 0x0e);
				}
				break;
			}
		}
	}
	free(pd);
	return mtx;
}

/* the bounds of every vertex under a node: its children, not its siblings */
static void partBounds(const u8 *pd, u32 pdLen, u32 node, s32 depth, f32 lo[3], f32 hi[3], s32 *n)
{
	u32 ofs = node + 0x18 <= pdLen && rd32(pd + node + 0x14) ? OFS(rd32(pd + node + 0x14)) : 0;
	s32 guard = 0, k;

	while (ofs != 0 && ofs + 0x18 <= pdLen && depth < 16 && guard++ < 256) {
		if (pd[ofs + 1] == PD_NODE_GUNDL) {
			const u32 ro = OFS(rd32(pd + ofs + 4));
			const u32 v = ro + 0x14 <= pdLen ? OFS(rd32(pd + ro + 12)) : 0;
			const u32 count = ro + 0x14 <= pdLen ? rd16(pd + ro + 0x10) : 0;
			u32 i;

			for (i = 0; v != 0 && i < count && v + 12 * (i + 1) <= pdLen; i++, (*n)++) {
				for (k = 0; k < 3; k++) {
					const f32 c = (s16)rd16(pd + v + 12 * i + 2 * k);

					lo[k] = *n == 0 || c < lo[k] ? c : lo[k];
					hi[k] = *n == 0 || c > hi[k] ? c : hi[k];
				}
			}
		}
		partBounds(pd, pdLen, ofs, depth + 1, lo, hi, n);
		ofs = rd32(pd + ofs + 0x0c) ? OFS(rd32(pd + ofs + 0x0c)) : 0;
	}
}

s32 gevrGexPartCentre(const char *pdname, const s32 *partList, s32 count, f32 out[3])
{
	u32 pdLen = 0, k;
	u8 *pd = gevrGexFileLoad(pdname, &pdLen);
	f32 lo[3] = { 0, 0, 0 }, hi[3] = { 0, 0, 0 };
	s32 n = 0, p;

	for (p = 0; pd != NULL && pdLen >= 0x1c && p < count; p++) {
		const u32 parts = OFS(rd32(pd + 8));
		const u16 numparts = rd16(pd + 0xc);

		for (k = 0; k < numparts && parts + 6 * (u32)numparts <= pdLen; k++) {
			if ((s16)rd16(pd + parts + 4 * numparts + 2 * k) == partList[p]) {
				partBounds(pd, pdLen, OFS(rd32(pd + parts + 4 * k)), 0, lo, hi, &n);
				break;
			}
		}
	}
	free(pd);
	for (p = 0; p < 3; p++) out[p] = 0.5f * (lo[p] + hi[p]);
	return n > 0;
}
