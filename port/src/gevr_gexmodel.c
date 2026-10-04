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
 *    GEVR_GEX_TEX_FIRST, which image.c texLoad serves from the GE-X ROM.
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
};

struct pdDl { u32 src, dst, len, outLen; struct pdArray *arr; };   /* outLen: less the G_COLs */

struct build {
	const u8 *pd;
	u32 pdLen;
	u32 nodeSrc[MAX_NODES], nodeDst[MAX_NODES], rodataDst[MAX_NODES];
	s32 numNodes;
	struct pdArray arrays[MAX_ARRAYS];
	s32 numArrays;
	struct pdDl dls[MAX_DLS];
	s32 numDls;
	const char *name;
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

static void addDl(struct build *b, u32 addr, struct pdArray *arr)
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
				wr32(out + pos, gexMapTexture(id & 0xfff));
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
		pos += 16 * b->arrays[k].count;
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

		for (i = 0; i < a->count; i++) {
			const u8 *s = pd + a->src + 12 * i;
			u8 *d = out + a->dst + 16 * i;

			memcpy(d, s, 6);          /* x, y, z */
			wr16(d + 6, 0);           /* flag */
			memcpy(d + 8, s + 8, 4);  /* s, t */
			memcpy(d + 12, a->colours + 4 * i, 4);
		}
	}

	for (k = 0; k < (u32)b->numDls; k++) {
		const struct pdDl *dl = &b->dls[k];
		u8 *d = out + dl->dst;

		for (i = 0; i < dl->len; i += 8) {
			u32 w0 = rd32(pd + dl->src + i), w1 = rd32(pd + dl->src + i + 4);
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
			} else if (op == 0xc0 && (w1 & 0xfff) != 0) {   /* texture: GE-X's number to ours */
				w1 = (w1 & ~0xfffu) | (gexMapTexture(w1 & 0xfff) & 0xfff);
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
		u32 *outLen, u16 *outMatrices, u16 *outTextures)
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

	root = OFS(rd32(pd));
	collectNodes(b, root);

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

			addDl(b, rd32(rs), a);
			addDl(b, rd32(rs + 4), a);
		} else if (type == PD_NODE_STARGUNFIRE) {
			struct pdArray *a = rd32(rs + 4) ? addArray(b, OFS(rd32(rs + 4)), 4 * rd32(rs), 1) : NULL;

			addDl(b, rd32(rs + 8), a);
		}
	}

	for (k = 0; k < (u32)b->numDls && !b->failed; k++) {
		colourVertices(b, &b->dls[k]);
	}

	/* the GE switch slots the caller wants, by PD part number */
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
		*outMatrices = rd16(pd + 0xe);
		*outTextures = rd16(pd + 0x16);
		sysLogPrintf(LOG_NOTE, "gexmodel: %s: %d nodes, %d vertex arrays, %d display lists, %u bytes, %d textures mapped",
				pdname, b->numNodes, b->numArrays, b->numDls, len, s_texCount);
	}
	for (k = 0; k < (u32)b->numArrays; k++) {
		free(b->arrays[k].colours);
		free(b->arrays[k].seen);
	}
	free(b);
	free(pd);
	return out;
}
