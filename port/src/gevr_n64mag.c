/*
 * GoldenEye's own guns' removable magazines: see gevr_n64mag.h.
 */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ultra64.h>
#include <PR/gbi.h>
#include <gbi_extension.h>   /* Rare's G_TRI4 */
#include <bondtypes.h>
#include <bondconstants.h>
#include "system.h"
#include "gevr_bean.h"
#include "gevr_n64mag.h"

#define NM_MAX_MODELS 96      /* model headers holding a gun with a magazine: the hands', guards' guns, previews */
#define NM_MAX_LISTS  12      /* a gun's lists that hold magazine triangles */
#define NM_MAX_CMDS   8192    /* a list's commands */
#define NM_VTX_CACHE  16      /* the F3D vertex cache */
#define NM_SEG_MTX    3u      /* SPSEGMENT_MODEL_MTX */
#define NM_SEG_FILE   5u      /* SPSEGMENT_MODEL_COL1: the model's file */
#define NM_SHELL_FIRST 18     /* the shotguns' shell switches (gunfire.c) */
#define NM_SHELL_LAST  27

#define HANGING(file, x0, y0, z0, x1, y1, z1, in) \
	{ .name = file, .lo = { x0, y0, z0 }, .hi = { x1, y1, z1 }, .into = { 0.0f, 1.0f, 0.0f }, .inside = in }
/* the PP7's grip runs from 0 -63 18 up to 0 27 33 (GwppkZ): the box up it, 7 of its foot under the floor */
#define PP7_MADE(file) \
	{ .name = file, .made = 1, .into = { 0.0f, 0.986f, 0.164f }, .foot = { 0.0f, -70.0f, 16.8f }, .length = 82.0f, \
	  .half = { 8.0f, 20.0f }, .inside = 75.0f, .rgb = { 44, 44, 48 } }

static const GevrN64MagDef s_defs[] = {
	PP7_MADE("GwppkZ"),
	PP7_MADE("GwppksilZ"),
	PP7_MADE("GsilverwppkZ"),
	PP7_MADE("GgoldwppkZ"),
	/* the DD44's grip: its floor slopes from -70 at the back to -80 at the front, 2..62 along the gun at its top */
	{ .name = "Gtt33Z", .made = 1, .into = { 0.0f, 0.994f, 0.106f }, .foot = { 0.0f, -86.0f, 22.0f }, .length = 98.0f,
	  .half = { 10.0f, 22.0f }, .inside = 88.0f, .rgb = { 44, 44, 48 } },
	/* each a list of its own on the cartridge: the box holds it and no other whole triangle */
	HANGING("GskorpionZ", -9.0f, -53.0f, 94.0f, 9.0f, 41.0f, 162.0f, 12.0f),
	HANGING("Gak47Z", -14.0f, -216.0f, 118.0f, 15.0f, 17.0f, 278.0f, 14.0f),
	/* the ZMG's is in its grip: what hangs below it */
	HANGING("GuziZ", -12.0f, -231.0f, -26.0f, 12.0f, -74.0f, 22.0f, 14.0f),
	/* the D5Ks': short of 210 along the gun, where a triangle of the trigger guard ends */
	HANGING("Gmp5kZ", -15.0f, -183.0f, 107.0f, 15.0f, 13.0f, 209.5f, 14.0f),
	HANGING("Gmp5ksilZ", -15.0f, -183.0f, 107.0f, 15.0f, 13.0f, 209.5f, 14.0f),
	HANGING("GspectreZ", -14.0f, -192.0f, 100.0f, 14.0f, 15.0f, 219.0f, 14.0f),
	HANGING("Gm16Z", -11.0f, -157.0f, 100.0f, 12.0f, 6.0f, 221.0f, 14.0f),
	/* the RC-P90's lies along the top of the receiver, under the sight: it lifts off */
	{ .name = "Gfnp90Z", .lo = { -32.0f, 73.0f, -192.0f }, .hi = { 32.0f, 113.0f, 155.0f }, .into = { 0.0f, -1.0f, 0.0f },
	  .inside = 12.0f, .pullUp = 1 },
	/* the sniper rifle has none: a box under the stock ahead of the trigger, the stock's underside at 13 */
	{ .name = "GsniperrifleZ", .made = 1, .into = { 0.0f, 1.0f, 0.0f }, .foot = { 0.0f, -42.0f, 122.0f }, .length = 78.0f,
	  .half = { 11.0f, 34.0f }, .inside = 23.0f, .rgb = { 40, 40, 44 } },
	/* the shell nearest the muzzle of the five across the receiver's top (switch 22: the last the reserve hides) */
	{ .name = "GshotgunZ", .lo = { -21.0f, 75.0f, 168.0f }, .hi = { 29.0f, 102.0f, 194.0f }, .into = { 0.0f, 1.0f, 0.0f },
	  .single = 1, .switched = 1, .port = { 0.0f, 4.0f, 150.0f } },
	/* the saddle on its left: the front shell's two halves (switches 22 and 27) */
	{ .name = "GautoshotZ", .lo = { -84.0f, 63.0f, 256.0f }, .hi = { 5.0f, 148.0f, 285.0f }, .into = { 0.0f, 1.0f, 0.0f },
	  .single = 1, .switched = 1, .port = { 0.0f, 4.0f, 120.0f } },
	/* the Cougar's cylinder and the grenade launcher's drum (its six chambers): the joint switch 4 names */
	{ .name = "GrugerZ", .joint = 4, .lo = { -40000.0f, -40000.0f, -40000.0f }, .hi = { 40000.0f, 40000.0f, 40000.0f },
	  .into = { 0.0f, 1.0f, 0.0f }, .noPull = 1 },
	{ .name = "GgrenadelaunchZ", .joint = 4, .lo = { -40000.0f, -40000.0f, -40000.0f }, .hi = { 40000.0f, 40000.0f, 40000.0f },
	  .into = { 0.0f, 1.0f, 0.0f }, .noPull = 1 },
	/* the rocket: a box the tube's length down its middle (-2 127, its mouth at 436), tail first */
	{ .name = "GrocketlaunchZ", .made = 1, .into = { 0.0f, 0.0f, -1.0f }, .foot = { -2.0f, 127.0f, 440.0f }, .length = 160.0f,
	  .half = { 13.0f, 13.0f }, .inside = 160.0f, .rgb = { 70, 78, 60 }, .consumed = 1, .noPull = 1 },
	/* the Golden Gun's one round: a small gold box, in at the breech behind the barrel (0 65 104) */
	{ .name = "GgoldengunZ", .made = 1, .into = { 0.0f, 0.0f, 1.0f }, .foot = { 0.0f, 65.0f, -12.0f }, .length = 24.0f,
	  .half = { 4.0f, 4.0f }, .rgb = { 190, 150, 40 }, .single = 1, .port = { 0.0f, 72.0f, 100.0f } },
};

/* letters alike, whatever their case (glibc's strings.h clashes with ultra64.h's bcopy) */
static int nmSame(const char *a, const char *b, size_t n)
{
	for (size_t i = 0; i < n; i++) {
		const int x = a[i] >= 'A' && a[i] <= 'Z' ? a[i] + 32 : a[i], y = b[i] >= 'A' && b[i] <= 'Z' ? b[i] + 32 : b[i];
		if (x != y)
			return 0;
		if (!x)
			return 1;
	}
	return 1;
}

/* a ROM hack's copy of a model (gevr_handpatch.c hpCopyStem): Gwppk1Z is GwppkZ's */
static int nmNameIs(const char *def, const char *name)
{
	const size_t n = strlen(name), d = strlen(def);

	if (n == d && nmSame(def, name, n))
		return 1;
	return n == d + 1 && n >= 3 && name[n - 2] >= '0' && name[n - 2] <= '9' && nmSame(def, name, d - 1) &&
	       (name[n - 1] == 'Z' || name[n - 1] == 'z');
}

const GevrN64MagDef *gevrN64MagFor(const char *name)
{
	for (size_t i = 0; name && i < sizeof(s_defs) / sizeof(s_defs[0]); i++)
		if (nmNameIs(s_defs[i].name, name))
			return &s_defs[i];
	return NULL;
}

const GevrN64MagDef *gevrN64MagAt(int n)
{
	return n >= 0 && (size_t)n < sizeof(s_defs) / sizeof(s_defs[0]) ? &s_defs[n] : NULL;
}

struct nmList {
	ModelNode *node;
	Gfx *open[2];      /* the node's primary and secondary with the magazine's triangles struck out; NULL as it is */
};

struct nmModel {
	const ModelFileHeader *header;
	const GevrN64MagDef *def;
	struct gevrBeanMagInfo info;
	struct nmList lists[NM_MAX_LISTS];
	int nlists;
	Gfx *magazine;     /* the magazine alone, drawn on info.node */
	Vtx *made;
	int triangles;
	const void *lo, *hi;   /* the file's storage, as far as it is known */
};

static struct nmModel s_models[NM_MAX_MODELS];

static struct nmModel *nmFind(const void *header)
{
	for (int i = 0; header && i < NM_MAX_MODELS; i++)
		if (s_models[i].header == header)
			return &s_models[i];
	return NULL;
}

static void nmFree(struct nmModel *m)
{
	for (int i = 0; i < m->nlists; i++) {
		free(m->lists[i].open[0]);
		free(m->lists[i].open[1]);
	}
	free(m->magazine);
	free(m->made);
	memset(m, 0, sizeof(*m));
}

void gevrN64MagForget(struct ModelFileHeader *header)
{
	struct nmModel *m = nmFind(header);
	if (m)
		nmFree(m);
}

void gevrN64MagForgetStorage(const void *base, unsigned size)
{
	const char *lo = base, *hi = lo + size;
	for (int i = 0; base && i < NM_MAX_MODELS; i++)
		if (s_models[i].header && (const char *)s_models[i].lo >= lo && (const char *)s_models[i].lo < hi)
			nmFree(&s_models[i]);
}

static int nmOp(const ModelNode *node)
{
	return node->Opcode & 0xff;
}

static ModelNode *nmNext(ModelNode *node)
{
	if (node->Child)
		return node->Child;
	while (node && !node->Next)
		node = node->Parent;
	return node ? node->Next : NULL;
}

/* the node hangs from one of the model's switches first..last */
static int nmUnderSwitch(const ModelFileHeader *header, const ModelNode *node, int first, int last)
{
	for (; node; node = node->Parent)
		for (int s = first; s <= last && s < header->numSwitches; s++)
			if (header->Switches[s] && node == header->Switches[s])
				return 1;
	return 0;
}

/* the joint a list is drawn under, when that is the model's root: its matrix, else -1 */
static int nmRootMatrix(const ModelNode *node)
{
	const ModelNode *group = NULL;
	for (const ModelNode *up = node->Parent; up; up = up->Parent) {
		if (nmOp(up) != MODELNODE_OPCODE_GROUP && nmOp(up) != MODELNODE_OPCODE_GROUPSIMPLE)
			continue;
		if (group)
			return -1;   /* a joint under another: the slide, the trigger finger, a cylinder */
		group = up;
	}
	return group && nmOp(group) == MODELNODE_OPCODE_GROUP && group->Data ? group->Data->Group.MatrixID0 : -1;
}

/* the joint a row's piece turns on (GevrN64MagDef.joint), when the list hangs from it: its matrix, else -1 */
static int nmJointMatrix(const ModelFileHeader *header, const GevrN64MagDef *def, const ModelNode *node)
{
	const ModelNode *joint = def->joint > 0 && def->joint < header->numSwitches ? header->Switches[def->joint] : NULL;
	if (!joint || nmOp(joint) != MODELNODE_OPCODE_GROUP || !joint->Data)
		return -1;
	for (const ModelNode *up = node->Parent; up; up = up->Parent)
		if (up == joint)
			return joint->Data->Group.MatrixID0;
	return -1;
}

/* a list as the model's record names it: a pointer, or an offset into its file (segment 5) */
static Gfx *nmListOf(const ModelRoData_DisplayListRecord *dl, const Gfx *list)
{
	const uintptr_t v = (uintptr_t)list;
	if (!list)
		return NULL;
	if (sizeof(uintptr_t) > 4 && (v >> 32) == 0 && ((v >> 24) & 0xf) == NM_SEG_FILE)
		return dl->BaseAddr ? (Gfx *)((u8 *)dl->BaseAddr + (v & 0x00fffffe)) : NULL;
	return (Gfx *)list;
}

static const Vertex *nmVertices(const ModelRoData_DisplayListRecord *dl, uintptr_t w1)
{
	const int tagged = (w1 & 1) != 0;
	const int untagged = sizeof(uintptr_t) > 4 && (w1 >> 32) == 0 && (w1 & 0x0f000000) != 0;
	if (tagged || untagged)
		return ((w1 >> 24) & 0xf) == NM_SEG_FILE && dl->BaseAddr ? (const Vertex *)((const u8 *)dl->BaseAddr + (w1 & 0x00fffffe))
		                                                         : NULL;
	return (const Vertex *)w1;
}

struct nmScan {
	const GevrN64MagDef *def;
	const ModelRoData_DisplayListRecord *dl;
	int rootMatrix;
	int eligible;        /* the node's triangles may be the magazine's */
	/* the matrix in force as the list starts: a secondary list loads none of its own and is
	 * drawn straight after its primary, on the primary's last (a cylinder's, a drum's) */
	int startMatrix;
	/* out */
	int ncmd, ntri, nsel, endMatrix;
	float sum[3], lo[3], hi[3];
};

static int nmInside(const struct nmScan *s, const Vertex *v)
{
	for (int k = 0; k < 3; k++) {
		const float p = (float)v->coord.AsArray[k];
		if (p < s->def->lo[k] || p > s->def->hi[k])
			return 0;
	}
	return 1;
}

/*
 * One list, command by command. keep: 0 only counts and measures; 1 writes
 * the list to out with the magazine's triangles struck out, 2 with every
 * other triangle struck out. A struck triangle's command is emptied (G_TRI1)
 * or its three corners zeroed (Rare's G_TRI4, which skips an all-zero one).
 */
static int nmWalk(struct nmScan *s, const Gfx *list, int keep, Gfx *out)
{
	const Vertex *cache[NM_VTX_CACHE] = {0};
	int cacheMtx[NM_VTX_CACHE], mtx = s->startMatrix, n = 0;

	for (int i = 0; i < NM_VTX_CACHE; i++)
		cacheMtx[i] = -1;
	s->ncmd = s->ntri = s->nsel = 0;
	for (;; list++, n++) {
		u32 w0 = (u32)list->words.w0;
		const uintptr_t w1full = (uintptr_t)list->words.w1;
		u32 w1 = (u32)w1full;
		const u8 op = (u8)(w0 >> 24);

		if (n >= NM_MAX_CMDS)
			return 0;
		if (op == (u8)G_MTX) {
			/* the model's matrices are segment 3 */
			mtx = ((w1 >> 24) & 0xf) == NM_SEG_MTX ? (int)((w1 & 0x00fffffe) / sizeof(Mtx)) : -1;
		} else if (op == (u8)G_VTX) {
			const int count = (int)((w0 >> 20) & 0xf) + 1, first = (int)((w0 >> 16) & 0xf);
			const Vertex *v = nmVertices(s->dl, w1full);
			for (int k = 0; k < count && first + k < NM_VTX_CACHE; k++) {
				cache[first + k] = v ? v + k : NULL;
				cacheMtx[first + k] = mtx;
			}
		} else if (op == (u8)G_TRI1 || op == (u8)G_TRI4) {
			const int ntri = op == (u8)G_TRI1 ? 1 : 4;
			for (int t = 0; t < ntri; t++) {
				int idx[3], sel = s->eligible;
				if (op == (u8)G_TRI1) {
					idx[0] = (int)((w1 >> 16) & 0xff) / 10;
					idx[1] = (int)((w1 >> 8) & 0xff) / 10;
					idx[2] = (int)(w1 & 0xff) / 10;
				} else {
					idx[0] = (int)((w1 >> (8 * t)) & 0xf);
					idx[1] = (int)((w1 >> (8 * t + 4)) & 0xf);
					idx[2] = (int)((w0 >> (4 * t)) & 0xf);
					if (!idx[0] && !idx[1] && !idx[2])
						continue;   /* an empty one of the four */
				}
				for (int c = 0; c < 3 && sel; c++)
					sel = idx[c] < NM_VTX_CACHE && cache[idx[c]] && cacheMtx[idx[c]] == s->rootMatrix &&
					      nmInside(s, cache[idx[c]]);
				s->ntri++;
				if (sel) {
					s->nsel++;
					for (int c = 0; !keep && c < 3; c++)
						for (int k = 0; k < 3; k++) {
							const float p = (float)cache[idx[c]]->coord.AsArray[k];
							s->sum[k] += p;
							if (p < s->lo[k]) s->lo[k] = p;
							if (p > s->hi[k]) s->hi[k] = p;
						}
				}
				if ((keep == 1 && sel) || (keep == 2 && !sel)) {
					if (op == (u8)G_TRI1) {
						w0 = (u32)G_NOOP << 24;
						w1 = 0;
					} else {
						w1 &= ~((u32)0xff << (8 * t));
						w0 &= ~((u32)0xf << (4 * t));
					}
				}
			}
		}
		if (out) {
			out[n] = *list;
			if (op == (u8)G_TRI1 || op == (u8)G_TRI4) {
				out[n].words.w0 = w0;
				out[n].words.w1 = w1;
			}
		}
		if (op == (u8)G_ENDDL) {
			s->ncmd = n + 1;
			s->endMatrix = mtx;
			return 1;
		}
	}
}

static void nmCmd(Gfx **g, u32 w0, uintptr_t w1)
{
	(*g)->words.w0 = w0;
	(*g)->words.w1 = w1;
	(*g)++;
}

static s16 nmRound(float v)
{
	const float r = v < 0.0f ? v - 0.5f : v + 0.5f;
	return (s16)(r < -32768.0f ? -32768.0f : r > 32767.0f ? 32767.0f : r);
}

/* the made box: eight corners and its twelve triangles, each wound both ways
 * (a magazine in the hand is seen from every side, whatever the draw culls) */
static void nmMadeBox(const GevrN64MagDef *def, int rootMatrix, Vtx *v, Gfx **g)
{
	static const u8 faces[12][3] = { { 0, 1, 2 }, { 1, 3, 2 }, { 4, 6, 5 }, { 5, 6, 7 }, { 0, 4, 1 }, { 1, 4, 5 },
		                         { 2, 3, 6 }, { 3, 7, 6 }, { 0, 2, 4 }, { 2, 6, 4 }, { 1, 5, 3 }, { 3, 5, 7 } };
	const float *a = def->into;
	/* across the gun, and square to both: along the gun for a box that stands up */
	const float deep[3] = { 0.0f, -a[2], a[1] };

	for (int i = 0; i < 8; i++) {
		const float w = (i & 1) ? def->half[0] : -def->half[0];
		const float d = (i & 2) ? def->half[1] : -def->half[1];
		const float l = (i & 4) ? def->length : 0.0f;
		/* the end faces a little lighter than the sides, so its shape reads */
		const float shade = (i & 4) ? 1.0f : 1.35f;
		memset(&v[i], 0, sizeof(v[i]));
		v[i].v.ob[0] = nmRound(def->foot[0] + w + a[0] * l);
		v[i].v.ob[1] = nmRound(def->foot[1] + deep[1] * d + a[1] * l);
		v[i].v.ob[2] = nmRound(def->foot[2] + deep[2] * d + a[2] * l);
		for (int k = 0; k < 3; k++) {
			const float c = (float)def->rgb[k] * shade;
			v[i].v.cn[k] = (u8)(c > 255.0f ? 255.0f : c);
		}
		v[i].v.cn[3] = 255;
	}
	nmCmd(g, ((u32)G_MTX << 24) | ((u32)(G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW) << 16) | (u32)sizeof(Mtx),
	      (uintptr_t)(((NM_SEG_MTX << 24) + (u32)rootMatrix * (u32)sizeof(Mtx)) | 1));
	nmCmd(g, ((u32)G_VTX << 24) | ((u32)((7 << 4) | 0) << 16) | (u32)(8 * sizeof(Vtx)), (uintptr_t)v);
	for (int f = 0; f < 12; f++) {
		nmCmd(g, (u32)G_TRI1 << 24,
		      ((u32)faces[f][0] * 10 << 16) | ((u32)faces[f][1] * 10 << 8) | ((u32)faces[f][2] * 10));
		nmCmd(g, (u32)G_TRI1 << 24,
		      ((u32)faces[f][0] * 10 << 16) | ((u32)faces[f][2] * 10 << 8) | ((u32)faces[f][1] * 10));
	}
}

#define NM_MADE_CMDS 26

void gevrN64MagApply(struct ModelFileHeader *header, const char *name)
{
	const GevrN64MagDef *def = gevrN64MagFor(name);
	struct nmModel *m = NULL;
	struct nmScan scans[NM_MAX_LISTS][2];
	const Gfx *sources[NM_MAX_LISTS][2];
	ModelNode *host = NULL;
	int hostMatrix = -1, total = 0, cmds = 0;
	float sum[3] = { 0 }, lo[3] = { 1e9f, 1e9f, 1e9f }, hi[3] = { -1e9f, -1e9f, -1e9f };
	Gfx *g;

	if (header)
		gevrN64MagForget(header);
	if (!header || !def || !header->RootNode)
		return;
	for (int i = 0; i < NM_MAX_MODELS && !m; i++)
		if (!s_models[i].header)
			m = &s_models[i];
	if (!m) {
		sysLogPrintf(LOG_WARNING, "n64 magazine: %s: no room for another model", name);
		return;
	}
	memset(m, 0, sizeof(*m));
	for (ModelNode *node = header->RootNode; node; node = nmNext(node)) {
		const ModelRoData_DisplayListRecord *dl;
		struct nmScan found[2];
		const Gfx *src[2] = { NULL, NULL };
		int rootMatrix, jointMatrix, any = 0;

		if (nmOp(node) != MODELNODE_OPCODE_DL || !node->Data)
			continue;
		dl = &node->Data->DisplayList;
		rootMatrix = nmRootMatrix(node);
		jointMatrix = nmJointMatrix(header, def, node);
		if (jointMatrix < 0) {
			/* not the hand and sleeve (switches 8-13, 35), the flash, the slide or the finger */
			if (rootMatrix < 0 || nmUnderSwitch(header, node, 1, 13) || nmUnderSwitch(header, node, 35, 35))
				continue;
			/* a made magazine is drawn on the gun's first list that sets its own render mode: its picture too */
			if (!host || (!host->Data->DisplayList.ModelType && dl->ModelType)) {
				host = node;
				hostMatrix = rootMatrix;
			}
			if (def->made || def->joint)
				continue;   /* a joint's piece is the joint's lists alone */
		} else {
			rootMatrix = jointMatrix;
		}
		for (int which = 0; which < 2; which++) {
			struct nmScan *s = &found[which];
			const Gfx *list = nmListOf(dl, which ? dl->Secondary : dl->Primary);
			src[which] = list;
			memset(s, 0, sizeof(*s));
			s->def = def;
			s->dl = dl;
			s->rootMatrix = rootMatrix;
			s->startMatrix = which && src[0] ? found[0].endMatrix : -1;
			s->eligible = !def->switched || nmUnderSwitch(header, node, NM_SHELL_FIRST, NM_SHELL_LAST);
			for (int k = 0; k < 3; k++) {
				s->lo[k] = 1e9f;
				s->hi[k] = -1e9f;
			}
			if (!list || !nmWalk(s, list, 0, NULL))
				s->nsel = 0;
			any += s->nsel;
		}
		if (!any)
			continue;
		if (m->nlists == NM_MAX_LISTS) {
			sysLogPrintf(LOG_WARNING, "n64 magazine: %s: its magazine is on more than %d lists", name, NM_MAX_LISTS);
			nmFree(m);
			return;
		}
		for (int which = 0; which < 2; which++) {
			scans[m->nlists][which] = found[which];
			sources[m->nlists][which] = src[which];
			total += found[which].nsel;
			for (int k = 0; k < 3 && found[which].nsel; k++) {
				sum[k] += found[which].sum[k];
				if (found[which].lo[k] < lo[k]) lo[k] = found[which].lo[k];
				if (found[which].hi[k] > hi[k]) hi[k] = found[which].hi[k];
			}
			if (found[which].nsel)
				cmds += found[which].ncmd;
		}
		m->lists[m->nlists++].node = node;
	}
	if (!host || hostMatrix < 0 || (!def->made && !total)) {
		sysLogPrintf(LOG_NOTE, "n64 magazine: %s: nothing of it found in the model (gestures run the game's reload)", name);
		memset(m, 0, sizeof(*m));
		return;
	}
	if (def->made) {
		/* the host's own list first, every triangle struck out: its picture is the box's */
		struct nmScan s = { .def = def, .dl = &host->Data->DisplayList, .rootMatrix = hostMatrix, .startMatrix = -1 };
		const Gfx *list = nmListOf(s.dl, s.dl->Primary);
		if (!list || !nmWalk(&s, list, 0, NULL)) {
			memset(m, 0, sizeof(*m));
			return;
		}
		m->magazine = malloc((size_t)(s.ncmd + NM_MADE_CMDS + 1) * sizeof(Gfx));
		m->made = malloc(8 * sizeof(Vtx));
		if (!m->magazine || !m->made) {
			nmFree(m);
			return;
		}
		s.eligible = 0;
		nmWalk(&s, list, 2, m->magazine);
		g = m->magazine + s.ncmd - 1;   /* over its end */
		nmMadeBox(def, hostMatrix, m->made, &g);
		nmCmd(&g, (u32)G_ENDDL << 24, 0);
		for (int k = 0; k < 3; k++) {
			m->info.axis[k] = def->into[k];
			m->info.foot[k] = def->foot[k];
			m->info.top[k] = def->foot[k] + def->into[k] * def->length;
			m->info.mouth[k] = m->info.top[k] - def->into[k] * def->inside;
			m->info.hold[k] = def->foot[k] + def->into[k] * def->length * 0.4f;
		}
		if (def->single) {
			/* a loose round: held by its middle, its middle to the port */
			for (int k = 0; k < 3; k++) {
				m->info.foot[k] = m->info.hold[k] = m->info.top[k] = def->foot[k] + def->into[k] * def->length * 0.5f;
				m->info.mouth[k] = def->port[k];
			}
		}
		m->info.made = !def->single && !def->consumed;
		m->triangles = 12;
	} else {
		float far = -1e9f, near = 1e9f, topSum[3] = { 0 }, footSum[3] = { 0 };
		int ntop = 0, nfoot = 0;

		host = m->lists[0].node;
		hostMatrix = scans[0][0].rootMatrix;
		m->magazine = malloc((size_t)(cmds + 1) * sizeof(Gfx));
		if (!m->magazine) {
			nmFree(m);
			return;
		}
		g = m->magazine;
		for (int i = 0; i < m->nlists; i++)
			for (int which = 0; which < 2; which++) {
				struct nmScan *s = &scans[i][which];
				if (!s->nsel)
					continue;
				/* the magazine alone: this list with every other triangle struck out, its end left off */
				nmWalk(s, sources[i][which], 2, g);
				g += s->ncmd - 1;
				if (def->single)
					continue;   /* the gun keeps the shell it is a copy of */
				m->lists[i].open[which] = malloc((size_t)s->ncmd * sizeof(Gfx));
				if (!m->lists[i].open[which]) {
					nmFree(m);
					return;
				}
				nmWalk(s, sources[i][which], 1, m->lists[i].open[which]);
			}
		nmCmd(&g, (u32)G_ENDDL << 24, 0);
		/* its top and its foot: the corners of its box nearest each end of the way in */
		for (int c = 0; c < 8; c++) {
			float p[3], along = 0;
			for (int k = 0; k < 3; k++) {
				p[k] = (c >> k) & 1 ? hi[k] : lo[k];
				along += p[k] * def->into[k];
			}
			if (along > far) far = along;
			if (along < near) near = along;
		}
		for (int c = 0; c < 8; c++) {
			float p[3], along = 0;
			for (int k = 0; k < 3; k++) {
				p[k] = (c >> k) & 1 ? hi[k] : lo[k];
				along += p[k] * def->into[k];
			}
			if (along >= far - 1e-3f) {
				for (int k = 0; k < 3; k++) topSum[k] += p[k];
				ntop++;
			}
			if (along <= near + 1e-3f) {
				for (int k = 0; k < 3; k++) footSum[k] += p[k];
				nfoot++;
			}
		}
		for (int k = 0; k < 3; k++) {
			const float middle = sum[k] / (float)(total * 3);
			m->info.axis[k] = def->into[k];
			if (def->single) {
				/* a shell: taken where it sits, its middle to the port */
				m->info.foot[k] = m->info.hold[k] = m->info.top[k] = middle;
				m->info.mouth[k] = def->port[k];
			} else if (def->joint) {
				/* a cylinder: held by its middle and seated where it sits, in its joint's own space */
				m->info.foot[k] = m->info.hold[k] = m->info.top[k] = m->info.mouth[k] = middle;
			} else {
				m->info.top[k] = ntop ? topSum[k] / (float)ntop : middle;
				m->info.foot[k] = nfoot ? footSum[k] / (float)nfoot : middle;
				m->info.mouth[k] = m->info.top[k] - def->into[k] * def->inside;
				m->info.hold[k] = 0.5f * (m->info.top[k] + m->info.foot[k]);
			}
		}
		m->triangles = total;
	}
	m->header = header;
	m->def = def;
	m->lo = host->Data->DisplayList.BaseAddr;
	m->info.node = host;
	m->info.matrix = hostMatrix;
	m->info.pullUp = def->pullUp;
	m->info.single = def->single;
	m->info.noPull = def->noPull;
	m->info.consumed = def->consumed;
	sysLogPrintf(LOG_NOTE, "n64 magazine: %s: %d triangles on %d list(s)%s, top %.0f %.0f %.0f, mouth %.0f %.0f %.0f", name,
	             m->triangles, m->nlists, def->made ? " (made)" : def->single ? " (a shell)" : "", m->info.top[0],
	             m->info.top[1], m->info.top[2], m->info.mouth[0], m->info.mouth[1], m->info.mouth[2]);
}

int gevrN64GunMagazine(const void *header, struct gevrBeanMagInfo *out)
{
	const struct nmModel *m = nmFind(header);
	if (!m)
		return 0;
	if (out)
		*out = m->info;
	return 1;
}

int gevrN64MagStats(const void *header, int *triangles, int *lists)
{
	const struct nmModel *m = nmFind(header);
	if (!m)
		return 0;
	*triangles = m->triangles;
	*lists = m->nlists;
	return 1;
}

extern int gevrBeanMagazineState(void);   /* gevr_bean.c: what gun.c set for this draw */

void *gevrN64MagGdl(void *node, void *current, int secondary)
{
	const int state = gevrBeanMagazineState();

	if (state == GEVR_BEAN_MAG_IN || !node || !current)
		return current;
	for (int i = 0; i < NM_MAX_MODELS; i++) {
		const struct nmModel *m = &s_models[i];
		if (!m->header)
			continue;
		if (state != GEVR_BEAN_MAG_OUT) {
			/* the magazine alone, or its placeholder: on its host's primary */
			if (m->info.node == node)
				return secondary ? NULL : m->magazine;
			continue;
		}
		for (int k = 0; k < m->nlists; k++)
			if (m->lists[k].node == node)
				return m->lists[k].open[secondary ? 1 : 0] ? m->lists[k].open[secondary ? 1 : 0] : current;
	}
	return current;
}
