/*
 * Model files: cartridge layout to host layout. See gevr_model.h.
 */

#include <stdlib.h>
#include <string.h>
#include <stddef.h>
#include <ultra64.h>
#include <PR/gbi.h>
#include <bondtypes.h>
#include <bondconstants.h>
#include "platform.h"
#include "system.h"
#include "gevr_model.h"

struct ModelFileHeader *gevrModelPendingHeader = NULL;

/* -------------------------------------------------- the cartridge's records */

/*
 * Each record as the cartridge stores it: big-endian, with a u32 in every slot
 * that becomes a pointer. Field order and sizes follow bondtypes.h with 4-byte
 * pointers; the sizes are the ones tools/gevr_model_probe.py confirmed.
 */
struct cartTexture     { u32 id; u8 width, height, mip, type, depth, sflags, tflags, pad; };           /* 12 */
struct cartNode        { u16 opcode; u16 pad; u32 data, parent, next, prev, child; };                    /* 24 */
struct cartHeader      { u16 animPart; s16 matrixIndex; u32 firstGroup; u16 group1, group2; u16 rwDataIndex, reserved; }; /* 16 */
struct cartGroup       { u32 origin[3]; u16 jointId; s16 matrixIds[3]; u32 childGroup; u32 radius; };   /* 28 */
struct cartDL          { u32 primary, secondary, baseAddr, vertices; u16 numVertices; s8 modelType; u8 pad; }; /* 20 */
struct cartOp05        { s32 numChildren; u32 children, vertices, images; u8 data[400]; u32 unk1a0, baseAddr; }; /* 0x1a8 */
struct cartOp06        { u32 unk[5]; u32 baseAddr; };                                                  /* 24 */
struct cartOp07        { u32 unk00, unk04; s32 numChildren; u32 children, vertices, images; u8 data[400]; u16 unk1a8, rwDataIndex; u32 baseAddr; }; /* 0x1b0 */
struct cartLOD         { u32 minDist, maxDist; u32 affects; u16 rwDataIndex, reserved; };               /* 16 */
struct cartBSP         { u32 point[3], vector[3]; u32 left, right; s16 reserved; u16 rwDataIndex; };    /* 36 */
struct cartBBox        { u32 modelNumber; u32 bounds[6]; };                                            /* 28 */
struct cartOp11        { u32 unk0c[16]; u32 radius; u16 rwDataIndex, unk46; u32 baseAddr; };            /* 76 */
struct cartGunfire     { u32 offset[3], size[3]; u32 image; u32 scale; u16 rwDataIndex, reserved; u32 baseAddr; }; /* 40 */
struct cartShadow      { u32 pos[2], size[2]; u32 image, header; u32 scale; u32 baseAddr; };            /* 32 */
struct cartOp14        { u32 pos[3], scale; };                                                         /* 16 */
struct cartInterlink   { u32 pos[3], pos2[3], scale; };                                                /* 28 */
struct cartOp16        { u32 pos[3]; s16 n0c, n0e, n10, unk12; u32 scale; };                            /* 24 */
struct cartOp17        { s32 hitpart; u32 radiusSq; u32 pos[3]; u32 othernode; u32 scale1, scale2; };   /* 32 */
struct cartSwitch      { u32 controls; u16 rwDataIndex, reserved; };                                   /* 8 */
struct cartGroupSimple { u32 origin[3]; s16 group1; u16 group2; u32 radius; };                          /* 20 */
struct cartDLPrimary   { s32 numVertices; u32 vertices, primary, baseAddr; };                          /* 16 */
struct cartHead        { u16 rwDataIndex; u16 pad; };                                                  /* 4 */
struct cartDLCollision { u32 primary, secondary, vertices; s16 numVertices, numCollisionVertices; u32 collisionVertices, pointUsage; s16 modelType; u16 rwDataIndex; u32 baseAddr; }; /* 32 */
struct cartChild       { u8 numEntries, unk01; u16 unk02; u32 data; };                                 /* 8 */

#define CART_VERTEX_SIZE 16
#define CART_GFX_SIZE    8
#define SEG_MODEL        0x05
#define SEG(addr)        ((addr) >> 24)
#define OFS(addr)        ((addr) & 0x00ffffff)

/* Big-endian reads out of a cartridge record. */
#define R32(f) PD_BE32((u32)(s->f))
#define R16(f) PD_BE16((u16)(s->f))
static f32 rdf(u32 be) { u32 v = PD_BE32(be); f32 f; memcpy(&f, &v, 4); return f; }
#define RF(f)  rdf(s->f)

/* --------------------------------------------------------------- blocks */

enum blockKind {
	BK_SWITCHES,   /* the switch table: n x u32 -> n x uintptr_t */
	BK_TEXTURES,   /* the texture table: n x 12 -> n x sizeof(ModelFileTextures) */
	BK_IMAGE,      /* one image entry outside the table: 12 -> sizeof(sImageTableEntry) */
	BK_NODE,       /* ModelNode: 24 -> sizeof(ModelNode) */
	BK_RODATA,     /* a rodata record, by opcode */
	BK_VTX,        /* an array of drawn vertices: swapped, 16 bytes each either way */
	BK_COLVTX,     /* an array of collision vertices: swapped, with a node reference to relink */
	BK_S16S,       /* an array of s16 */
	BK_GDL,        /* a display list: 8-byte commands -> 16-byte, addresses relinked */
	BK_CHILDREN,   /* Op05/Op07 child table: 8 -> 16 each */
	BK_DATA,       /* bytes copied as they are: embedded textures, opaque data */
};

struct block {
	u32 src;       /* offset in the cartridge file */
	u32 srcSize;   /* bytes in the cartridge file; from the record, or from the gap to the next block */
	u32 dst;       /* offset in the host file */
	u32 dstSize;
	u32 count;     /* elements, where that decides the size */
	u8  kind;
	u8  opcode;    /* BK_RODATA */
	u8  sizeKnown; /* srcSize came from a record or the list's own end, not from the gap */
};

struct conv {
	const u8 *src;
	u32 srcLen;
	struct block *blocks;
	s32 numBlocks;
	s32 maxBlocks;
	s32 numSwitches;
	s32 numTextures;
	u32 texTableSrc;
	const char *name;
	s32 warnings;
	s32 sorted;
};

static const char *kindName(u8 kind)
{
	static const char *const names[] = {
		"switches", "textures", "image", "node", "rodata", "vertices",
		"collision vertices", "s16 array", "display list", "children", "data",
	};
	return kind < ARRAYCOUNT(names) ? names[kind] : "?";
}

static void warn(struct conv *c, const char *what, u32 a, u32 b)
{
	c->warnings++;
	sysLogPrintf(LOG_WARNING, "model %s: %s (0x%X, 0x%X)", c->name, what, a, b);
}

static struct block *findBlock(struct conv *c, u32 src)
{
	s32 i;

	for (i = 0; i < c->numBlocks; i++) {
		if (c->blocks[i].src == src) {
			return &c->blocks[i];
		}
	}

	return NULL;
}

/*
 * Note a block at a cartridge offset. Returns it if it is new, NULL if it
 * was already known (or the offset is not inside the file).
 */
static struct block *mark(struct conv *c, u32 src, u8 kind, u8 opcode, u32 count, u32 srcSize)
{
	struct block *b;

	if (src >= c->srcLen) {
		warn(c, "reference past the end of the file", src, c->srcLen);
		return NULL;
	}

	b = findBlock(c, src);
	if (b) {
		if (b->kind != kind) {
			warn(c, "offset claimed by two block kinds", src, (u32)b->kind << 8 | kind);
		}
		return NULL;
	}

	if (c->numBlocks >= c->maxBlocks) {
		warn(c, "too many blocks", (u32)c->numBlocks, 0);
		return NULL;
	}

	b = &c->blocks[c->numBlocks++];
	memset(b, 0, sizeof(*b));
	b->src = src;
	b->kind = kind;
	b->opcode = opcode;
	b->count = count;
	b->srcSize = srcSize;
	b->sizeKnown = srcSize != 0;
	c->sorted = 0;
	return b;
}

/* A segment-5 address's file offset, or -1 for a null or another segment. */
static s64 modelOfs(u32 addr)
{
	if (addr == 0 || SEG(addr) != SEG_MODEL) {
		return -1;
	}

	return (s64)OFS(addr);
}

static void markRef(struct conv *c, u32 addr, u8 kind, u8 opcode, u32 count, u32 srcSize)
{
	s64 o = modelOfs(addr);

	if (o >= 0) {
		mark(c, (u32)o, kind, opcode, count, srcSize);
	}
}

/* An image entry reference: inside the texture table it needs no block. */
static void markImage(struct conv *c, u32 addr)
{
	s64 o = modelOfs(addr);
	u32 tableEnd = c->texTableSrc + c->numTextures * sizeof(struct cartTexture);

	if (o < 0) {
		return;
	}

	if ((u32)o >= c->texTableSrc && (u32)o < tableEnd) {
		return;
	}

	mark(c, (u32)o, BK_IMAGE, 0, 1, sizeof(struct cartTexture));
}

static u32 cartRodataSize(u8 opcode)
{
	switch (opcode) {
	case MODELNODE_OPCODE_HEADER:      return sizeof(struct cartHeader);
	case MODELNODE_OPCODE_GROUP:       return sizeof(struct cartGroup);
	case MODELNODE_OPCODE_OP03:        return sizeof(struct cartGroup);
	case MODELNODE_OPCODE_DL:          return sizeof(struct cartDL);
	case MODELNODE_OPCODE_OP05:        return sizeof(struct cartOp05);
	case MODELNODE_OPCODE_OP06:        return sizeof(struct cartOp06);
	case MODELNODE_OPCODE_OP07:        return sizeof(struct cartOp07);
	case MODELNODE_OPCODE_LOD:         return sizeof(struct cartLOD);
	case MODELNODE_OPCODE_BSP:         return sizeof(struct cartBSP);
	case MODELNODE_OPCODE_BBOX:        return sizeof(struct cartBBox);
	case MODELNODE_OPCODE_OP11:        return sizeof(struct cartOp11);
	case MODELNODE_OPCODE_GUNFIRE:     return sizeof(struct cartGunfire);
	case MODELNODE_OPCODE_SHADOW:      return sizeof(struct cartShadow);
	case MODELNODE_OPCODE_OP14:        return sizeof(struct cartOp14);
	case MODELNODE_OPCODE_INTERLINK:   return sizeof(struct cartInterlink);
	case MODELNODE_OPCODE_OP16:        return sizeof(struct cartOp16);
	case MODELNODE_OPCODE_OP17:        return sizeof(struct cartOp17);
	case MODELNODE_OPCODE_SWITCH:      return sizeof(struct cartSwitch);
	case MODELNODE_OPCODE_OP20:        return sizeof(struct cartHeader);
	case MODELNODE_OPCODE_GROUPSIMPLE: return sizeof(struct cartGroupSimple);
	case MODELNODE_OPCODE_DLPRIMARY:   return sizeof(struct cartDLPrimary);
	case MODELNODE_OPCODE_HEAD:        return sizeof(struct cartHead);
	case MODELNODE_OPCODE_DLCOLLISION: return sizeof(struct cartDLCollision);
	default:                           return 0;
	}
}

static u32 hostRodataSize(u8 opcode)
{
	switch (opcode) {
	case MODELNODE_OPCODE_HEADER:      return sizeof(ModelRoData_HeaderRecord);
	case MODELNODE_OPCODE_GROUP:       return sizeof(ModelRoData_GroupRecord);
	case MODELNODE_OPCODE_OP03:        return sizeof(ModelRoData_GroupRecord);
	case MODELNODE_OPCODE_DL:          return sizeof(ModelRoData_DisplayListRecord);
	case MODELNODE_OPCODE_OP05:        return sizeof(ModelRoData_Op05Record);
	case MODELNODE_OPCODE_OP06:        return sizeof(ModelRoData_Op06Record);
	case MODELNODE_OPCODE_OP07:        return sizeof(ModelRoData_Op07Record);
	case MODELNODE_OPCODE_LOD:         return sizeof(ModelRoData_LODRecord);
	case MODELNODE_OPCODE_BSP:         return sizeof(ModelRoData_BSPRecord);
	case MODELNODE_OPCODE_BBOX:        return sizeof(ModelRoData_BoundingBoxRecord);
	case MODELNODE_OPCODE_OP11:        return sizeof(ModelRoData_Op11Record);
	case MODELNODE_OPCODE_GUNFIRE:     return sizeof(ModelRoData_GunfireRecord);
	case MODELNODE_OPCODE_SHADOW:      return sizeof(ModelRoData_ShadowRecord);
	case MODELNODE_OPCODE_OP14:        return sizeof(ModelRoData_Op14Record);
	case MODELNODE_OPCODE_INTERLINK:   return sizeof(ModelRoData_InterlinkageRecord);
	case MODELNODE_OPCODE_OP16:        return sizeof(ModelNode_Op16Record);
	case MODELNODE_OPCODE_OP17:        return sizeof(ModelRoData_Op17Record);
	case MODELNODE_OPCODE_SWITCH:      return sizeof(ModelRoData_SwitchRecord);
	case MODELNODE_OPCODE_OP20:        return sizeof(ModelRoData_HeaderRecord);
	case MODELNODE_OPCODE_GROUPSIMPLE: return sizeof(ModelRoData_GroupSimpleRecord);
	case MODELNODE_OPCODE_DLPRIMARY:   return sizeof(ModelRoData_DisplayListPrimaryRecord);
	case MODELNODE_OPCODE_HEAD:        return sizeof(ModelRoData_HeadPlaceholderRecord);
	case MODELNODE_OPCODE_DLCOLLISION: return sizeof(ModelRoData_DisplayList_CollisionRecord);
	default:                           return 0;
	}
}

/* Commands whose second word is an address, by opcode (F3DEX). */
#define G_OP_MTX     0x01
#define G_OP_MOVEMEM 0x03
#define G_OP_VTX     0x04
#define G_OP_DL      0x06
#define G_OP_ENDDL   0xb8
#define G_OP_SETTIMG 0xfd
#define G_OP_SETZIMG 0xfe
#define G_OP_SETCIMG 0xff

static s32 gfxHasAddress(u8 op)
{
	return op == G_OP_MTX || op == G_OP_MOVEMEM || op == G_OP_VTX || op == G_OP_DL
			|| op == G_OP_SETTIMG || op == G_OP_SETZIMG || op == G_OP_SETCIMG;
}

/* Number of cartridge bytes in the display list at src, through its ENDDL. */
static u32 gdlCartSize(struct conv *c, u32 src)
{
	u32 pos = src;

	while (pos + CART_GFX_SIZE <= c->srcLen) {
		u8 op = c->src[pos];
		pos += CART_GFX_SIZE;
		if (op == G_OP_ENDDL) {
			return pos - src;
		}
	}

	warn(c, "display list runs off the end of the file", src, c->srcLen);
	return c->srcLen - src;
}

/* ------------------------------------------------------------ discovery */

static void discoverRodata(struct conv *c, struct block *b)
{
	const u8 *p = c->src + b->src;
	const u32 cartSize = cartRodataSize(b->opcode);

	if (b->src + cartSize > c->srcLen) {
		warn(c, "rodata record runs past the end of the file", b->src, b->opcode);
		return;
	}

	switch (b->opcode) {
	case MODELNODE_OPCODE_HEADER:
	case MODELNODE_OPCODE_OP20: {
		const struct cartHeader *s = (const void *)p;
		markRef(c, R32(firstGroup), BK_NODE, 0, 1, sizeof(struct cartNode));
		break;
	}
	case MODELNODE_OPCODE_GROUP:
	case MODELNODE_OPCODE_OP03: {
		const struct cartGroup *s = (const void *)p;
		markRef(c, R32(childGroup), BK_NODE, 0, 1, sizeof(struct cartNode));
		break;
	}
	case MODELNODE_OPCODE_OP17: {
		const struct cartOp17 *s = (const void *)p;
		markRef(c, R32(othernode), BK_NODE, 0, 1, sizeof(struct cartNode));
		break;
	}
	case MODELNODE_OPCODE_DL: {
		const struct cartDL *s = (const void *)p;
		u32 n = R16(numVertices);
		markRef(c, R32(primary), BK_GDL, 0, 0, 0);
		markRef(c, R32(secondary), BK_GDL, 0, 0, 0);
		markRef(c, R32(vertices), BK_VTX, 0, n, n * CART_VERTEX_SIZE);
		break;
	}
	case MODELNODE_OPCODE_DLPRIMARY: {
		const struct cartDLPrimary *s = (const void *)p;
		u32 n = R32(numVertices);
		markRef(c, R32(vertices), BK_VTX, 0, n, n * CART_VERTEX_SIZE);
		markRef(c, R32(primary), BK_GDL, 0, 0, 0);
		break;
	}
	case MODELNODE_OPCODE_DLCOLLISION: {
		const struct cartDLCollision *s = (const void *)p;
		u32 n = (u16)R16(numVertices);
		u32 nc = (u16)R16(numCollisionVertices);
		s64 cv;
		u32 i;
		markRef(c, R32(primary), BK_GDL, 0, 0, 0);
		markRef(c, R32(secondary), BK_GDL, 0, 0, 0);
		markRef(c, R32(vertices), BK_VTX, 0, n, n * CART_VERTEX_SIZE);
		markRef(c, R32(collisionVertices), BK_COLVTX, 0, nc, nc * CART_VERTEX_SIZE);
		markRef(c, R32(pointUsage), BK_S16S, 0, n, n * sizeof(s16));
		/* each collision vertex may name the node it belongs to */
		cv = modelOfs(R32(collisionVertices));
		if (cv >= 0) {
			for (i = 0; i < nc && (u32)cv + (i + 1) * CART_VERTEX_SIZE <= c->srcLen; i++) {
				u32 linked = PD_BE32(*(const u32 *)(c->src + cv + i * CART_VERTEX_SIZE + 8));
				markRef(c, linked, BK_NODE, 0, 1, sizeof(struct cartNode));
			}
		}
		break;
	}
	case MODELNODE_OPCODE_LOD: {
		const struct cartLOD *s = (const void *)p;
		markRef(c, R32(affects), BK_NODE, 0, 1, sizeof(struct cartNode));
		break;
	}
	case MODELNODE_OPCODE_SWITCH: {
		const struct cartSwitch *s = (const void *)p;
		markRef(c, R32(controls), BK_NODE, 0, 1, sizeof(struct cartNode));
		break;
	}
	case MODELNODE_OPCODE_BSP: {
		const struct cartBSP *s = (const void *)p;
		markRef(c, R32(left), BK_NODE, 0, 1, sizeof(struct cartNode));
		markRef(c, R32(right), BK_NODE, 0, 1, sizeof(struct cartNode));
		break;
	}
	case MODELNODE_OPCODE_GUNFIRE: {
		const struct cartGunfire *s = (const void *)p;
		markImage(c, R32(image));
		break;
	}
	case MODELNODE_OPCODE_SHADOW: {
		const struct cartShadow *s = (const void *)p;
		markImage(c, R32(image));
		markRef(c, R32(header), BK_NODE, 0, 1, sizeof(struct cartNode));
		break;
	}
	case MODELNODE_OPCODE_OP11: {
		const struct cartOp11 *s = (const void *)p;
		markRef(c, R32(unk0c[15]), BK_DATA, 0, 0, 0);
		break;
	}
	case MODELNODE_OPCODE_OP05:
	case MODELNODE_OPCODE_OP07: {
		/*
		 * The child tables of these two are not fully understood; their
		 * entries are copied as bytes with the references relinked, and the
		 * image entries they name are not widened. Neither opcode appears in
		 * the files checked so far, so say so if one turns up.
		 */
		u32 n, children, vertices, images;
		if (b->opcode == MODELNODE_OPCODE_OP05) {
			const struct cartOp05 *s = (const void *)p;
			n = R32(numChildren); children = R32(children); vertices = R32(vertices); images = R32(images);
		} else {
			const struct cartOp07 *s = (const void *)p;
			n = R32(numChildren); children = R32(children); vertices = R32(vertices); images = R32(images);
			markRef(c, R32(unk00), BK_NODE, 0, 1, sizeof(struct cartNode));
			markRef(c, R32(unk04), BK_NODE, 0, 1, sizeof(struct cartNode));
		}
		warn(c, "opcode 5/7 record present; its child data is only partly converted", b->src, b->opcode);
		markRef(c, children, BK_CHILDREN, 0, n, n * sizeof(struct cartChild));
		markRef(c, vertices, BK_VTX, 0, 0, 0);
		markRef(c, images, BK_DATA, 0, 0, 0);
		break;
	}
	default:
		break;
	}
}

/* Whether a cartridge offset lies inside a block already laid out. Needs the blocks sorted and sized. */
static s32 covered(struct conv *c, u32 ofs)
{
	u32 lo = 0, hi = (u32)c->numBlocks;
	const struct block *b;

	while (lo < hi) {
		u32 mid = (lo + hi) / 2;
		if (c->blocks[mid].src <= ofs) {
			lo = mid + 1;
		} else {
			hi = mid;
		}
	}

	if (lo == 0) {
		return 0;
	}

	b = &c->blocks[lo - 1];
	return ofs >= b->src && ofs < b->src + b->srcSize;
}

/*
 * The addresses inside display list commands. Nearly all of them land inside
 * blocks the records already describe - a vertex partway into an array, an
 * embedded texture - and those must not become blocks of their own, or the
 * array would be split and padded. Only an address no block covers is new:
 * a branch to a display list, or data the records do not mention. Returns
 * the number of blocks added; run after layout() so the sizes are settled.
 */
static s32 discoverGdlRefs(struct conv *c)
{
	s32 i, added = 0;
	const s32 n = c->numBlocks;

	for (i = 0; i < n; i++) {
		const struct block *b = &c->blocks[i];
		u32 pos;

		if (b->kind != BK_GDL) {
			continue;
		}

		for (pos = b->src; pos + CART_GFX_SIZE <= b->src + b->srcSize; pos += CART_GFX_SIZE) {
			u8 op = c->src[pos];
			u32 w1 = PD_BE32(*(const u32 *)(c->src + pos + 4));

			if (!gfxHasAddress(op) || SEG(w1) != SEG_MODEL || covered(c, OFS(w1))) {
				continue;
			}

			if (mark(c, OFS(w1), op == G_OP_DL ? BK_GDL : op == G_OP_VTX ? BK_VTX : BK_DATA, 0, 0, 0)) {
				added++;
			}
		}
	}

	return added;
}

static void discoverChildren(struct conv *c, struct block *b)
{
	u32 i;

	for (i = 0; i < b->count && b->src + (i + 1) * sizeof(struct cartChild) <= c->srcLen; i++) {
		const struct cartChild *s = (const void *)(c->src + b->src + i * sizeof(struct cartChild));
		markRef(c, R32(data), BK_DATA, 0, 0, 0);
	}
}

static void discover(struct conv *c)
{
	s32 i;
	const u32 switchesSize = c->numSwitches * sizeof(u32);
	const u32 texturesSize = c->numTextures * sizeof(struct cartTexture);
	const u32 root = switchesSize + texturesSize;

	c->texTableSrc = switchesSize;

	if (root + sizeof(struct cartNode) > c->srcLen) {
		warn(c, "file too small for its tables and root node", root, c->srcLen);
		return;
	}

	/* the tables, then the root; the switches point at nodes too */
	if (switchesSize) {
		mark(c, 0, BK_SWITCHES, 0, c->numSwitches, switchesSize);
	}
	if (texturesSize) {
		mark(c, switchesSize, BK_TEXTURES, 0, c->numTextures, texturesSize);
	}
	mark(c, root, BK_NODE, 0, 1, sizeof(struct cartNode));

	for (i = 0; i < c->numSwitches; i++) {
		markRef(c, PD_BE32(*(const u32 *)(c->src + i * 4)), BK_NODE, 0, 1, sizeof(struct cartNode));
	}

	for (i = 0; i < c->numTextures; i++) {
		const struct cartTexture *s = (const void *)(c->src + switchesSize + i * sizeof(struct cartTexture));
		markRef(c, R32(id), BK_DATA, 0, 0, 0); /* an embedded texture, if the id is an address */
	}

	/* everything reachable: new blocks are appended and visited in turn */
	for (i = 0; i < c->numBlocks; i++) {
		struct block *b = &c->blocks[i];

		switch (b->kind) {
		case BK_NODE: {
			const struct cartNode *s = (const void *)(c->src + b->src);
			u8 opcode = (u8)(R16(opcode) & 0xff);
			markRef(c, R32(data), BK_RODATA, opcode, 1, cartRodataSize(opcode));
			markRef(c, R32(parent), BK_NODE, 0, 1, sizeof(struct cartNode));
			markRef(c, R32(next), BK_NODE, 0, 1, sizeof(struct cartNode));
			markRef(c, R32(prev), BK_NODE, 0, 1, sizeof(struct cartNode));
			markRef(c, R32(child), BK_NODE, 0, 1, sizeof(struct cartNode));
			break;
		}
		case BK_RODATA:
			discoverRodata(c, b);
			break;
		case BK_CHILDREN:
			discoverChildren(c, b);
			break;
		default:
			break;
		}
	}
}

/* --------------------------------------------------------------- layout */

static int compareBlocks(const void *pa, const void *pb)
{
	const struct block *a = pa, *b = pb;
	return a->src < b->src ? -1 : a->src > b->src ? 1 : 0;
}

static u32 alignUp(u32 v, u32 a)
{
	return (v + a - 1) & ~(a - 1);
}

/*
 * Order the blocks by cartridge offset, settle the sizes that come from the
 * gap to the next block, and assign each its host offset. Host order equals
 * cartridge order, which keeps the display lists at the tail.
 */
static u32 layout(struct conv *c)
{
	s32 i;
	u32 dstpos = 0;

	qsort(c->blocks, c->numBlocks, sizeof(struct block), compareBlocks);
	c->sorted = 1;

	for (i = 0; i < c->numBlocks; i++) {
		struct block *b = &c->blocks[i];
		u32 next = (i + 1 < c->numBlocks) ? c->blocks[i + 1].src : c->srcLen;
		u32 gap = next - b->src;

		if (b->kind == BK_GDL && !b->sizeKnown) {
			/* a list ends at its own ENDDL */
			b->srcSize = gdlCartSize(c, b->src);
			b->sizeKnown = 1;
		}

		if (b->kind == BK_GDL) {
			b->count = b->srcSize / CART_GFX_SIZE;
		}

		if (!b->sizeKnown) {
			/* settled afresh each pass: a block found later may shorten the gap */
			b->srcSize = gap;
			if (b->kind == BK_VTX) {
				b->count = gap / CART_VERTEX_SIZE;
			}
		} else if (b->srcSize > gap) {
			warn(c, "block overlaps the next one", b->src, b->srcSize - gap);
			b->srcSize = gap;
		}

		switch (b->kind) {
		case BK_SWITCHES: b->dstSize = b->count * sizeof(uintptr_t); break;
		case BK_TEXTURES: b->dstSize = b->count * sizeof(ModelFileTextures); break;
		case BK_IMAGE:    b->dstSize = sizeof(struct sImageTableEntry); break;
		case BK_NODE:     b->dstSize = sizeof(ModelNode); break;
		case BK_RODATA:   b->dstSize = hostRodataSize(b->opcode); break;
		case BK_GDL:      b->dstSize = b->count * sizeof(Gfx); break;
		case BK_CHILDREN: b->dstSize = b->count * sizeof(ModelRoData_Child); break;
		default:          b->dstSize = b->srcSize; break;
		}

		dstpos = alignUp(dstpos, b->kind == BK_DATA ? 16 : 8);
		b->dst = dstpos;
		dstpos += b->dstSize;
	}

	return alignUp(dstpos, 16);
}

/* -------------------------------------------------------------- relinking */

/*
 * A cartridge segment-5 address, as a host segment-5 address. Addresses
 * inside a block map by the block's kind: element-wise where elements grew
 * (the texture table, display lists), byte-wise where they did not.
 */
static u32 resolve(struct conv *c, u32 addr)
{
	u32 ofs, lo, hi;
	struct block *b = NULL;

	if (addr == 0 || SEG(addr) != SEG_MODEL) {
		return addr;
	}

	ofs = OFS(addr);
	lo = 0;
	hi = (u32)c->numBlocks;

	while (lo < hi) {
		u32 mid = (lo + hi) / 2;
		if (c->blocks[mid].src <= ofs) {
			lo = mid + 1;
		} else {
			hi = mid;
		}
	}

	if (lo == 0) {
		warn(c, "reference before the first block", addr, 0);
		return addr;
	}

	b = &c->blocks[lo - 1];

	if (ofs == b->src) {
		return (SEG_MODEL << 24) | b->dst;
	}

	if (ofs >= b->src + b->srcSize) {
		warn(c, "reference into no block", addr, b->src);
		return (SEG_MODEL << 24) | (b->dst + (ofs - b->src));
	}

	switch (b->kind) {
	case BK_TEXTURES: {
		u32 delta = ofs - b->src;
		return (SEG_MODEL << 24) | (b->dst + (delta / sizeof(struct cartTexture)) * sizeof(ModelFileTextures)
				+ delta % sizeof(struct cartTexture));
	}
	case BK_GDL:
		return (SEG_MODEL << 24) | (b->dst + (ofs - b->src) * (sizeof(Gfx) / CART_GFX_SIZE));
	case BK_CHILDREN:
		return (SEG_MODEL << 24) | (b->dst + ((ofs - b->src) / sizeof(struct cartChild)) * sizeof(ModelRoData_Child));
	case BK_VTX:
	case BK_COLVTX:
	case BK_S16S:
	case BK_DATA:
	case BK_SWITCHES:
		return (SEG_MODEL << 24) | (b->dst + (ofs - b->src));
	default:
		warn(c, "reference into the middle of a record", addr, b->src);
		return (SEG_MODEL << 24) | (b->dst + (ofs - b->src));
	}
}

#define PTR(addr) ((void *)(uintptr_t)resolve(c, (addr)))

/* ------------------------------------------------------------ conversion */

static void convertVertices(struct conv *c, const u8 *src, u8 *dst, u32 count, s32 collision)
{
	u32 i;

	for (i = 0; i < count; i++) {
		const u8 *s = src + i * CART_VERTEX_SIZE;
		Vertex *d = (Vertex *)(dst + i * CART_VERTEX_SIZE);

		d->coord.x = (s16)PD_BE16(*(const u16 *)(s + 0));
		d->coord.y = (s16)PD_BE16(*(const u16 *)(s + 2));
		d->coord.z = (s16)PD_BE16(*(const u16 *)(s + 4));
		d->index   = (s16)PD_BE16(*(const u16 *)(s + 6));

		if (collision) {
			/* the node this point belongs to, kept as a segment-5 offset */
			d->LinkedTo = resolve(c, PD_BE32(*(const u32 *)(s + 8)));
			d->CollisionRelatedIndex = (s16)PD_BE16(*(const u16 *)(s + 12));
			d->CollisionReserved     = (s16)PD_BE16(*(const u16 *)(s + 14));
		} else {
			d->s = (s16)PD_BE16(*(const u16 *)(s + 8));
			d->t = (s16)PD_BE16(*(const u16 *)(s + 10));
			memcpy(&d->r, s + 12, 4);
		}
	}
}

static void convertGdl(struct conv *c, const u8 *src, Gfx *dst, u32 count)
{
	u32 i;

	for (i = 0; i < count; i++) {
		const u8 *s = src + i * CART_GFX_SIZE;
		u32 w0 = PD_BE32(*(const u32 *)(s + 0));
		u32 w1 = PD_BE32(*(const u32 *)(s + 4));
		u8 op = (u8)(w0 >> 24);
		uintptr_t hw1 = w1;

		if (gfxHasAddress(op) && SEG(w1) != 0) {
			/*
			 * A segmented address. Segment 5 is this file and moves with the
			 * layout; the others (4 for the runtime vertex buffer) are set
			 * at draw time. Either way the renderer wants the tag bit.
			 */
			if (SEG(w1) == SEG_MODEL) {
				hw1 = resolve(c, w1);
			}
			hw1 |= 1;
		}

		dst[i].words.w0 = w0;
		dst[i].words.w1 = hw1;
	}
}

static void convertTextureEntry(struct conv *c, const struct cartTexture *s, ModelFileTextures *d)
{
	u32 id = R32(id);

	d->TextureID   = SEG(id) == SEG_MODEL ? resolve(c, id) : id;
	d->Width       = s->width;
	d->Height      = s->height;
	d->MipMapTiles = s->mip;
	d->Type        = s->type;
	d->RenderDepth = s->depth;
	d->sflags      = s->sflags;
	d->tflags      = s->tflags;
}

static void convertImageEntry(struct conv *c, const struct cartTexture *s, struct sImageTableEntry *d)
{
	u32 id = R32(id);

	d->index  = SEG(id) == SEG_MODEL ? resolve(c, id) : id;
	d->width  = s->width;
	d->height = s->height;
	d->level  = s->mip;
	d->format = s->type;
	d->depth  = s->depth;
	d->flagsS = s->sflags;
	d->flagsT = s->tflags;
	d->pad    = s->pad;
}

static void convertCoord3(coord3d *d, const u32 *be)
{
	d->x = rdf(be[0]);
	d->y = rdf(be[1]);
	d->z = rdf(be[2]);
}

static void convertRodata(struct conv *c, const struct block *b, u8 *dstbuf)
{
	const u8 *p = c->src + b->src;
	void *dst = dstbuf + b->dst;

	memset(dst, 0, b->dstSize);

	switch (b->opcode) {
	case MODELNODE_OPCODE_HEADER:
	case MODELNODE_OPCODE_OP20: {
		const struct cartHeader *s = (const void *)p;
		ModelRoData_HeaderRecord *d = dst;
		d->AnimPart    = R16(animPart);
		d->MatrixIndex = (s16)R16(matrixIndex);
		d->FirstGroup  = PTR(R32(firstGroup));
		d->Group1      = R16(group1);
		d->Group2      = R16(group2);
		d->RwDataIndex = R16(rwDataIndex);
		d->reserved    = R16(reserved);
		break;
	}
	case MODELNODE_OPCODE_GROUP:
	case MODELNODE_OPCODE_OP03: {
		const struct cartGroup *s = (const void *)p;
		ModelRoData_GroupRecord *d = dst;
		convertCoord3(&d->Origin, s->origin);
		d->JointID      = R16(jointId);
		d->MatrixIDs[0] = (s16)R16(matrixIds[0]);
		d->MatrixIDs[1] = (s16)R16(matrixIds[1]);
		d->MatrixIDs[2] = (s16)R16(matrixIds[2]);
		d->ChildGroup   = PTR(R32(childGroup));
		d->BoundingVolumeRadius = RF(radius);
		break;
	}
	case MODELNODE_OPCODE_DL: {
		const struct cartDL *s = (const void *)p;
		ModelRoData_DisplayListRecord *d = dst;
		d->Primary     = PTR(R32(primary));
		d->Secondary   = PTR(R32(secondary));
		d->BaseAddr    = NULL;
		d->Vertices    = PTR(R32(vertices));
		d->numVertices = R16(numVertices);
		d->ModelType   = s->modelType;
		break;
	}
	case MODELNODE_OPCODE_OP05: {
		const struct cartOp05 *s = (const void *)p;
		ModelRoData_Op05Record *d = dst;
		d->NumChildren = (s32)R32(numChildren);
		d->Children    = PTR(R32(children));
		d->Vertices    = PTR(R32(vertices));
		d->Images      = PTR(R32(images));
		memcpy(d->Data, s->data, sizeof(d->Data));
		d->unk1A0      = R32(unk1a0);
		d->BaseAddr    = NULL;
		break;
	}
	case MODELNODE_OPCODE_OP06: {
		const struct cartOp06 *s = (const void *)p;
		ModelRoData_Op06Record *d = dst;
		d->unk00 = R32(unk[0]); d->unk04 = R32(unk[1]); d->unk08 = R32(unk[2]);
		d->unk0C = R32(unk[3]); d->unk10 = R32(unk[4]);
		d->BaseAddr = NULL;
		break;
	}
	case MODELNODE_OPCODE_OP07: {
		const struct cartOp07 *s = (const void *)p;
		ModelRoData_Op07Record *d = dst;
		d->unk00       = PTR(R32(unk00));
		d->unk04       = PTR(R32(unk04));
		d->NumChildren = (s32)R32(numChildren);
		d->Children    = PTR(R32(children));
		d->Vertices    = PTR(R32(vertices));
		d->Images      = PTR(R32(images));
		memcpy(d->Data, s->data, sizeof(d->Data));
		d->unk1A8      = R16(unk1a8);
		d->RwDataIndex = R16(rwDataIndex);
		d->BaseAddr    = NULL;
		break;
	}
	case MODELNODE_OPCODE_LOD: {
		const struct cartLOD *s = (const void *)p;
		ModelRoData_LODRecord *d = dst;
		d->MinDistance = RF(minDist);
		d->MaxDistance = RF(maxDist);
		d->Affects     = PTR(R32(affects));
		d->RwDataIndex = R16(rwDataIndex);
		d->reserved    = R16(reserved);
		break;
	}
	case MODELNODE_OPCODE_BSP: {
		const struct cartBSP *s = (const void *)p;
		ModelRoData_BSPRecord *d = dst;
		convertCoord3(&d->Point, s->point);
		convertCoord3(&d->Vector, s->vector);
		d->leftChild   = PTR(R32(left));
		d->rightChild  = PTR(R32(right));
		d->reserved    = (s16)R16(reserved);
		d->RwDataIndex = R16(rwDataIndex);
		break;
	}
	case MODELNODE_OPCODE_BBOX: {
		const struct cartBBox *s = (const void *)p;
		ModelRoData_BoundingBoxRecord *d = dst;
		s32 i;
		d->ModelNumber = R32(modelNumber);
		for (i = 0; i < 6; i++) {
			d->Bounds.AsArray[i] = rdf(s->bounds[i]);
		}
		break;
	}
	case MODELNODE_OPCODE_OP11: {
		const struct cartOp11 *s = (const void *)p;
		ModelRoData_Op11Record *d = dst;
		s32 i;
		for (i = 0; i < 15; i++) {
			d->unk0c[i] = R32(unk0c[i]);
		}
		d->unk0c[15] = resolve(c, R32(unk0c[15])); /* a reference; the game adds the base */
		d->BoundingVolumeRadius = RF(radius);
		d->RwDataIndex = R16(rwDataIndex);
		d->unk46       = R16(unk46);
		d->BaseAddr    = NULL;
		break;
	}
	case MODELNODE_OPCODE_GUNFIRE: {
		const struct cartGunfire *s = (const void *)p;
		ModelRoData_GunfireRecord *d = dst;
		convertCoord3(&d->Offset, s->offset);
		convertCoord3(&d->Size, s->size);
		d->Image       = PTR(R32(image));
		d->Scale       = RF(scale);
		d->RwDataIndex = R16(rwDataIndex);
		d->reserved    = R16(reserved);
		d->BaseAddr    = NULL;
		break;
	}
	case MODELNODE_OPCODE_SHADOW: {
		const struct cartShadow *s = (const void *)p;
		ModelRoData_ShadowRecord *d = dst;
		d->pos.x  = rdf(s->pos[0]);  d->pos.y  = rdf(s->pos[1]);
		d->size.x = rdf(s->size[0]); d->size.y = rdf(s->size[1]);
		d->image    = PTR(R32(image));
		d->Header   = PTR(R32(header));
		d->Scale    = RF(scale);
		d->BaseAddr = NULL;
		break;
	}
	case MODELNODE_OPCODE_OP14: {
		const struct cartOp14 *s = (const void *)p;
		ModelRoData_Op14Record *d = dst;
		convertCoord3(&d->pos, s->pos);
		d->Scale = RF(scale);
		break;
	}
	case MODELNODE_OPCODE_INTERLINK: {
		const struct cartInterlink *s = (const void *)p;
		ModelRoData_InterlinkageRecord *d = dst;
		convertCoord3(&d->pos, s->pos);
		convertCoord3(&d->pos2, s->pos2);
		d->Scale = RF(scale);
		break;
	}
	case MODELNODE_OPCODE_OP16: {
		const struct cartOp16 *s = (const void *)p;
		ModelNode_Op16Record *d = dst;
		convertCoord3(&d->pos, s->pos);
		d->nodeindex0c = (s16)R16(n0c);
		d->nodeindex0e = (s16)R16(n0e);
		d->nodeindex10 = (s16)R16(n10);
		d->unk12       = (s16)R16(unk12);
		d->Scale       = RF(scale);
		break;
	}
	case MODELNODE_OPCODE_OP17: {
		const struct cartOp17 *s = (const void *)p;
		ModelRoData_Op17Record *d = dst;
		d->hitpart  = (s32)R32(hitpart);
		d->radiusSq = RF(radiusSq);
		convertCoord3(&d->pos, s->pos);
		d->othernode = PTR(R32(othernode));
		d->scale1   = RF(scale1);
		d->scale2   = RF(scale2);
		break;
	}
	case MODELNODE_OPCODE_SWITCH: {
		const struct cartSwitch *s = (const void *)p;
		ModelRoData_SwitchRecord *d = dst;
		d->Controls    = PTR(R32(controls));
		d->RwDataIndex = R16(rwDataIndex);
		d->reserved    = R16(reserved);
		break;
	}
	case MODELNODE_OPCODE_GROUPSIMPLE: {
		const struct cartGroupSimple *s = (const void *)p;
		ModelRoData_GroupSimpleRecord *d = dst;
		convertCoord3(&d->Origin, s->origin);
		d->Group1 = (s16)R16(group1);
		d->Group2 = R16(group2);
		d->BoundingVolumeRadius = RF(radius);
		break;
	}
	case MODELNODE_OPCODE_DLPRIMARY: {
		const struct cartDLPrimary *s = (const void *)p;
		ModelRoData_DisplayListPrimaryRecord *d = dst;
		d->numVertices = (s32)R32(numVertices);
		d->Vertices    = PTR(R32(vertices));
		d->Primary     = PTR(R32(primary));
		d->BaseAddr    = NULL;
		break;
	}
	case MODELNODE_OPCODE_HEAD: {
		const struct cartHead *s = (const void *)p;
		ModelRoData_HeadPlaceholderRecord *d = dst;
		d->RwDataIndex = R16(rwDataIndex);
		break;
	}
	case MODELNODE_OPCODE_DLCOLLISION: {
		const struct cartDLCollision *s = (const void *)p;
		ModelRoData_DisplayList_CollisionRecord *d = dst;
		d->Primary              = PTR(R32(primary));
		d->Secondary            = PTR(R32(secondary));
		d->Vertices             = PTR(R32(vertices));
		d->numVertices          = (s16)R16(numVertices);
		d->numCollisionVertices = (s16)R16(numCollisionVertices);
		d->CollisionVertices    = PTR(R32(collisionVertices));
		d->PointUsage           = PTR(R32(pointUsage));
		d->ModelType            = (s16)R16(modelType);
		d->RwDataIndex          = R16(rwDataIndex);
		d->BaseAddr             = NULL;
		break;
	}
	default:
		warn(c, "rodata of unknown opcode copied as bytes", b->src, b->opcode);
		memcpy(dst, p, b->srcSize < b->dstSize ? b->srcSize : b->dstSize);
		break;
	}
}

static void convert(struct conv *c, u8 *dstbuf)
{
	s32 i;
	u32 j;

	for (i = 0; i < c->numBlocks; i++) {
		const struct block *b = &c->blocks[i];
		const u8 *src = c->src + b->src;
		u8 *dst = dstbuf + b->dst;

		switch (b->kind) {
		case BK_SWITCHES: {
			uintptr_t *d = (uintptr_t *)dst;
			for (j = 0; j < b->count; j++) {
				d[j] = resolve(c, PD_BE32(*(const u32 *)(src + j * 4)));
			}
			break;
		}
		case BK_TEXTURES:
			for (j = 0; j < b->count; j++) {
				convertTextureEntry(c, (const struct cartTexture *)(src + j * sizeof(struct cartTexture)),
						(ModelFileTextures *)(dst + j * sizeof(ModelFileTextures)));
			}
			break;
		case BK_IMAGE:
			convertImageEntry(c, (const struct cartTexture *)src, (struct sImageTableEntry *)dst);
			break;
		case BK_NODE: {
			const struct cartNode *s = (const void *)src;
			ModelNode *d = (ModelNode *)dst;
			memset(d, 0, sizeof(*d));
			d->Opcode = R16(opcode);
			d->Data   = PTR(R32(data));
			d->Parent = PTR(R32(parent));
			d->Next   = PTR(R32(next));
			d->Prev   = PTR(R32(prev));
			d->Child  = PTR(R32(child));
			break;
		}
		case BK_RODATA:
			convertRodata(c, b, dstbuf);
			break;
		case BK_VTX:
			convertVertices(c, src, dst, b->srcSize / CART_VERTEX_SIZE, 0);
			break;
		case BK_COLVTX:
			convertVertices(c, src, dst, b->srcSize / CART_VERTEX_SIZE, 1);
			break;
		case BK_S16S:
			for (j = 0; j + 1 < b->srcSize; j += 2) {
				*(u16 *)(dst + j) = PD_BE16(*(const u16 *)(src + j));
			}
			break;
		case BK_GDL:
			convertGdl(c, src, (Gfx *)dst, b->count);
			break;
		case BK_CHILDREN:
			for (j = 0; j < b->count; j++) {
				const struct cartChild *s = (const void *)(src + j * sizeof(struct cartChild));
				ModelRoData_Child *d = (ModelRoData_Child *)(dst + j * sizeof(ModelRoData_Child));
				d->NumEntries = s->numEntries;
				d->unk01 = s->unk01;
				d->unk02 = R16(unk02);
				d->unk04 = PTR(R32(data));
			}
			break;
		case BK_DATA:
		default:
			memcpy(dst, src, b->srcSize);
			break;
		}
	}
}

/* ---------------------------------------------------------------- entry */

u32 gevrModelConvert(u8 *data, u32 size, u32 capacity, s32 numSwitches, s32 numTextures, const char *name)
{
	struct conv c;
	u8 *out;
	u32 outSize;
	s32 i, nodes = 0, gdls = 0, embedded = 0;

	memset(&c, 0, sizeof(c));
	c.src = data;
	c.srcLen = size;
	c.numSwitches = numSwitches;
	c.numTextures = numTextures;
	c.name = name ? name : "?";
	c.maxBlocks = (s32)(size / 4) + 64;
	c.blocks = malloc(c.maxBlocks * sizeof(struct block));

	if (!c.blocks) {
		sysFatalError("gevrModelConvert: out of memory for %d blocks", c.maxBlocks);
	}

	discover(&c);

	if (c.numBlocks == 0) {
		free(c.blocks);
		sysLogPrintf(LOG_ERROR, "model %s: nothing found to convert (%u bytes, %d switches, %d textures)",
				c.name, size, numSwitches, numTextures);
		return 0;
	}

	/*
	 * The records give every block but what only the display lists refer to.
	 * Those references can only be judged against a settled layout, and a
	 * list they turn up may refer to more, so it goes round until nothing new
	 * appears.
	 */
	do {
		outSize = layout(&c);
	} while (discoverGdlRefs(&c) > 0);

	if (outSize > capacity) {
		free(c.blocks);
		sysLogPrintf(LOG_ERROR, "model %s: host layout needs %u bytes but the buffer holds %u",
				c.name, outSize, capacity);
		return 0;
	}

	out = calloc(1, outSize);
	if (!out) {
		sysFatalError("gevrModelConvert: out of memory for %u bytes", outSize);
	}

	convert(&c, out);

	for (i = 0; i < c.numBlocks; i++) {
		if (c.blocks[i].kind == BK_NODE) nodes++;
		if (c.blocks[i].kind == BK_GDL) gdls++;
		if (c.blocks[i].kind == BK_DATA) embedded++;
	}

	memcpy(data, out, outSize);
	free(out);
	free(c.blocks);

	sysLogPrintf(c.warnings ? LOG_WARNING : LOG_NOTE,
			"model %s: %u -> %u bytes, %d blocks (%d nodes, %d display lists, %d data blocks), %d warnings",
			c.name, size, outSize, c.numBlocks, nodes, gdls, embedded, c.warnings);

	return outSize;
}
