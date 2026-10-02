/*
 * Hand and arm patches for the first-person models (issue #9): see
 * gevr_handpatch.h. The patch data is gevr_handpatch_data.c (generated).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ultra64.h>
#include <PR/gbi.h>
#include <bondtypes.h>
#include <bondconstants.h>
#include "system.h"
#include "gevr_handpatch.h"

s32 texLoadFromGdl(Gfx *src, s32 srcsize, Gfx *dst, void *texpool);   /* tex.c */

#define HP_MAX_NODES    512
#define HP_MAX_MARKERS  64
#define HP_SLOTS        24     /* model buffers holding a patched model */
#define HP_SLOT_ALLOCS  48
#define HP_VTX_CACHE    16     /* the F3D vertex cache */
#define HP_SEG_MTX      0x03u  /* SPSEGMENT_MODEL_MTX: the model's matrices */

/* ------------------------------------------------ notes from the converter */

static const struct gevrHpModel *s_noted;
static u32 s_src[HP_MAX_NODES], s_dst[HP_MAX_NODES];
static s32 s_numNodes;
static u32 s_markerTex[HP_MAX_MARKERS], s_markerW0[HP_MAX_MARKERS];
static s32 s_numMarkers;

static const struct gevrHpModel *hpFind(const char *name)
{
	s32 i;

	if (name == NULL) {
		return NULL;
	}

	for (i = 0; i < g_gevrHpNumModels; i++) {
		if (strcasecmp(g_gevrHpModels[i].name, name) == 0) {
			return &g_gevrHpModels[i];
		}
	}

	return NULL;
}

/*
 * Faces dropped from a model (issue #24): GfistZ has one triangle textured
 * with the white 1x1 0x5ea glued back to back with a skin one on the same
 * three vertices. The N64 culled it from its one viewpoint. In stereo the
 * hands draw with culling off (VR_CULL_OFF, #9) and a face the list would
 * cull is pushed a hair back, which hides the white side from the fist's
 * modelled side; but from the other side - the way the mirrored left fist
 * faces the player - the white face is the front one, and it won. It is a
 * modelling leftover with no job on either side, so the converter drops it.
 */
static const struct {
	const char *name;
	u32 tex;
} s_hpDrop[] = {
	{ "GfistZ", 0x5ea },
};

s32 gevrHandPatchDropsTexture(const char *name, u32 texnum)
{
	s32 i;

	if (name == NULL) {
		return FALSE;
	}
	for (i = 0; i < (s32)(sizeof(s_hpDrop) / sizeof(s_hpDrop[0])); i++) {
		if (s_hpDrop[i].tex == texnum && strcasecmp(s_hpDrop[i].name, name) == 0) {
			return TRUE;
		}
	}
	return FALSE;
}

s32 gevrHandPatchWants(const char *name)
{
	s_noted = hpFind(name);
	s_numNodes = 0;
	s_numMarkers = 0;
	return s_noted != NULL;
}

void gevrHandPatchNoteNode(u32 src, u32 dst)
{
	if (s_numNodes < HP_MAX_NODES) {
		s_src[s_numNodes] = src;
		s_dst[s_numNodes] = dst;
		s_numNodes++;
	}
}

void gevrHandPatchNoteMarker(u32 texnum, u32 w0)
{
	s32 i;

	for (i = 0; i < s_numMarkers; i++) {
		if (s_markerTex[i] == texnum) {
			return;   /* the first one the model uses stands for it */
		}
	}

	if (s_numMarkers < HP_MAX_MARKERS) {
		s_markerTex[s_numMarkers] = texnum;
		s_markerW0[s_numMarkers] = w0;
		s_numMarkers++;
	}
}

/* ------------------------------------------------------------ allocations */

/*
 * A patch lives as long as the model it was built for, which lives in a buffer
 * the game reuses: the patch built for a buffer is freed when a model loads
 * into that buffer again. Two copies of one gun (dual wield) are two buffers.
 */
static struct {
	u8 *file;
	void *mem[HP_SLOT_ALLOCS];
	s32 n;
} s_slots[HP_SLOTS];

static s32 hpSlot(u8 *file)
{
	s32 i, k;
	s32 empty = -1;

	for (i = 0; i < HP_SLOTS; i++) {
		if (s_slots[i].file == file) {
			for (k = 0; k < s_slots[i].n; k++) {
				free(s_slots[i].mem[k]);
			}
			s_slots[i].n = 0;
			return i;
		}
		if (empty < 0 && s_slots[i].file == NULL) {
			empty = i;
		}
	}

	if (empty < 0) {
		sysLogPrintf(LOG_WARNING, "handpatch: no free slot for a model at %p; not patched", (void *)file);
		return -1;
	}

	s_slots[empty].file = file;
	s_slots[empty].n = 0;
	return empty;
}

static void *hpAlloc(s32 slot, size_t size)
{
	void *p;

	if (s_slots[slot].n >= HP_SLOT_ALLOCS) {
		return NULL;
	}

	p = calloc(1, size);

	if (p != NULL) {
		s_slots[slot].mem[s_slots[slot].n++] = p;
	}

	return p;
}

/* ------------------------------------------------------------------ build */

static s32 hpMode(void)
{
	FILE *f = fopen("/sdcard/Android/data/com.gevr.port/files/gevr_handpatch.txt", "r");
	s32 mode = 1;

	if (f != NULL) {
		if (fscanf(f, "%d", &mode) != 1) {
			mode = 1;
		}
		fclose(f);
	}

	return mode;
}

static ModelNode *hpNode(u8 *file, u32 ofs)
{
	s32 i;

	for (i = 0; i < s_numNodes; i++) {
		if (s_src[i] == ofs) {
			return (ModelNode *)(file + s_dst[i]);
		}
	}

	return NULL;
}

static ModelRoData_DisplayListRecord *hpDl(u8 *file, u32 ofs)
{
	ModelNode *node = hpNode(file, ofs);

	if (node == NULL || (node->Opcode & 0xff) != MODELNODE_OPCODE_DL || node->Data == NULL) {
		return NULL;
	}

	return &node->Data->DisplayList;
}

/* FNV-1a over the x, y, z of a vertex block, each as a little-endian s16;
 * tools/blender/gevr_hands_patch.py fnv_vertices() hashes the same way. */
static u32 hpFnv(const Vertex *v, s32 n)
{
	u32 h = 0x811c9dc5u;
	s32 i, k;

	for (i = 0; i < n; i++) {
		for (k = 0; k < 3; k++) {
			u16 c = (u16)v[i].coord.AsArray[k];
			h = (h ^ (c & 0xff)) * 0x01000193u;
			h = (h ^ (c >> 8)) * 0x01000193u;
		}
	}

	return h;
}

static s32 hpPosition(u8 *file, const struct gevrHpCorner *c, s16 out[3])
{
	f32 acc[3] = { 0.0f, 0.0f, 0.0f };
	s32 i, k;

	if (c->nmix == 0) {
		ModelRoData_DisplayListRecord *dl = hpDl(file, c->node);
		if (dl == NULL || c->ref < 0 || c->ref >= dl->numVertices) {
			return FALSE;
		}
		for (k = 0; k < 3; k++) {
			out[k] = dl->Vertices[c->ref].coord.AsArray[k];
		}
		return TRUE;
	}

	for (i = 0; i < c->nmix; i++) {
		const struct gevrHpMix *m = &g_gevrHpMixes[c->firstMix + i];
		ModelRoData_DisplayListRecord *dl = hpDl(file, m->node);
		if (dl == NULL || m->idx >= dl->numVertices) {
			return FALSE;
		}
		for (k = 0; k < 3; k++) {
			acc[k] += m->w * (f32)dl->Vertices[m->idx].coord.AsArray[k];
		}
	}

	for (k = 0; k < 3; k++) {
		f32 r = acc[k] < 0.0f ? acc[k] - 0.5f : acc[k] + 0.5f;
		out[k] = (s16)(r < -32768.0f ? -32768.0f : r > 32767.0f ? 32767.0f : r);
	}

	return TRUE;
}

static void hpCmd(Gfx **g, u32 w0, uintptr_t w1)
{
	(*g)->words.w0 = w0;
	(*g)->words.w1 = w1;
	(*g)++;
}

/*
 * One group's triangles, in batches that fit the vertex cache. Corners load
 * under their own matrix (segment 3, the model's matrices), as the model's own
 * lists do: a patch may join parts on two bones (the watch laser's finger).
 */
static s32 hpEmitGroup(Gfx **g, Vtx **vp, const struct gevrHpGroup *grp, const Vtx *corners)
{
	s16 slotOf[HP_VTX_CACHE * 64];
	u16 batch[HP_VTX_CACHE];
	u16 batchTris[HP_VTX_CACHE * 8];
	s32 nb = 0, nt = 0, t, i, k, emitted = 0;
	const u8 *mtxOf;
	u8 mtx[HP_VTX_CACHE * 64];

	if (grp->numCorners > (u16)(sizeof(slotOf) / sizeof(slotOf[0]))) {
		return 0;
	}

	for (i = 0; i < grp->numCorners; i++) {
		slotOf[i] = -1;
		mtx[i] = g_gevrHpCorners[grp->firstCorner + i].mtx;
	}
	mtxOf = mtx;

	for (t = 0; t <= grp->numTris; t++) {
		const u16 *tri = t < grp->numTris ? g_gevrHpTris[grp->firstTri + t] : NULL;
		s32 need = 0;

		if (tri != NULL) {
			for (k = 0; k < 3; k++) {
				if (slotOf[tri[k]] < 0 && (k == 0 || tri[k] != tri[0]) && (k < 2 || tri[k] != tri[1])) {
					need++;
				}
			}
		}

		if (tri == NULL || nb + need > HP_VTX_CACHE || nt == (s32)(sizeof(batchTris) / sizeof(batchTris[0]))) {
			/* flush: the batch's corners ordered by matrix, one load per matrix */
			s32 start = 0;
			u16 order[HP_VTX_CACHE];
			s32 no = 0;

			for (k = 0; k < 256 && no < nb; k++) {
				for (i = 0; i < nb; i++) {
					if (mtxOf[batch[i]] == k) {
						order[no++] = batch[i];
					}
				}
			}
			for (i = 0; i < no; i++) {
				slotOf[order[i]] = (s16)i;
				(*vp)[i] = corners[order[i]];
			}
			while (start < no) {
				s32 end = start;
				u8 m = mtxOf[order[start]];
				while (end < no && mtxOf[order[end]] == m) {
					end++;
				}
				hpCmd(g, (G_MTX << 24) | ((G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW) << 16) | sizeof(Mtx),
						(uintptr_t)(((HP_SEG_MTX << 24) + (u32)m * sizeof(Mtx)) | 1));
				hpCmd(g, (G_VTX << 24) | ((u32)(((end - start - 1) << 4) | start) << 16) | (u32)((end - start) * sizeof(Vtx)),
						(uintptr_t)&(*vp)[start]);
				start = end;
			}
			for (i = 0; i < nt; i++) {
				const u16 *bt = g_gevrHpTris[grp->firstTri + batchTris[i]];
				hpCmd(g, 0xbfu << 24, ((u32)slotOf[bt[0]] * 10 << 16) | ((u32)slotOf[bt[1]] * 10 << 8) | ((u32)slotOf[bt[2]] * 10));
				emitted++;
			}
			*vp += no;
			for (i = 0; i < nb; i++) {
				slotOf[batch[i]] = -1;
			}
			nb = 0;
			nt = 0;
		}

		if (tri == NULL) {
			break;
		}

		for (k = 0; k < 3; k++) {
			s32 have = FALSE;
			for (i = 0; i < nb; i++) {
				if (batch[i] == tri[k]) {
					have = TRUE;
				}
			}
			if (!have) {
				batch[nb++] = tri[k];
				slotOf[tri[k]] = 0;   /* in the batch; its slot is set at the flush */
			}
		}
		batchTris[nt++] = (u16)t;
	}

	return emitted;
}

static u32 hpMarker(u32 texnum)
{
	s32 i;

	for (i = 0; i < s_numMarkers; i++) {
		if (s_markerTex[i] == texnum) {
			return s_markerW0[i];
		}
	}

	return 0;
}

void gevrHandPatchApply(struct ModelFileHeader *header, const char *name, void *texpool)
{
	const struct gevrHpModel *model = hpFind(name);
	u8 *file;
	s32 slot, p, mode, applied = 0, triangles = 0, skipped = 0;

	if (model == NULL) {
		return;
	}

	if (s_noted != model) {
		sysLogPrintf(LOG_WARNING, "handpatch %s: the converter noted no layout for it; not patched", name);
		return;
	}

	mode = hpMode();

	if (mode == 0) {
		return;
	}

	file = (u8 *)header->Switches;
	slot = hpSlot(file);

	if (slot < 0) {
		return;
	}

	for (p = 0; p < model->numParts; p++) {
		const struct gevrHpPart *part = &g_gevrHpParts[model->firstPart + p];
		ModelRoData_DisplayListRecord *host = hpDl(file, part->host);
		s32 n, gi, i, ok = host != NULL && host->Primary != NULL;
		s32 numCorners = 0, maxTris = 0, numSrc;
		Vtx *corners, *vtxpool, *vp;
		Gfx *src, *g, *dst, *wrap;

		/* every node the part uses must be the one it was made for */
		for (n = 0; ok && n < part->numNodes; n++) {
			const struct gevrHpNode *hn = &g_gevrHpNodes[part->firstNode + n];
			ModelRoData_DisplayListRecord *dl = hpDl(file, hn->ofs);
			if (dl == NULL || dl->numVertices != hn->numvtx || hpFnv(dl->Vertices, dl->numVertices) != hn->fnv) {
				sysLogPrintf(LOG_WARNING, "handpatch %s: node 0x%04x is not the one the patch was made for; part 0x%04x skipped",
						name, hn->ofs, part->host);
				ok = FALSE;
			}
		}

		if (!ok) {
			skipped++;
			continue;
		}

		for (gi = 0; gi < part->numGroups; gi++) {
			const struct gevrHpGroup *grp = &g_gevrHpGroups[part->firstGroup + gi];
			numCorners += grp->numCorners;
			maxTris += grp->numTris;
		}

		corners = hpAlloc(slot, sizeof(Vtx) * (numCorners + 1));
		vtxpool = hpAlloc(slot, sizeof(Vtx) * (maxTris * 3 + 1));
		numSrc = maxTris * 8 + part->numGroups * 4 + 4;
		src = hpAlloc(slot, sizeof(Gfx) * numSrc);

		if (corners == NULL || vtxpool == NULL || src == NULL) {
			skipped++;
			continue;
		}

		/* no G_TEXTURE of our own: the first marker's expansion writes one at
		 * its texture's level count, as it does for a list that sets none */
		g = src;
		vp = vtxpool;

		for (gi = 0; ok && gi < part->numGroups; gi++) {
			const struct gevrHpGroup *grp = &g_gevrHpGroups[part->firstGroup + gi];
			u32 marker = hpMarker(grp->tex);
			Vtx *gc = corners;

			if (marker == 0) {
				sysLogPrintf(LOG_WARNING, "handpatch %s: texture 0x%03x is not in the model; part 0x%04x skipped",
						name, grp->tex, part->host);
				ok = FALSE;
				break;
			}

			for (i = 0; i < grp->numCorners; i++) {
				const struct gevrHpCorner *c = &g_gevrHpCorners[grp->firstCorner + i];
				Vtx_t *v = &gc[i].v;

				if (!hpPosition(file, c, v->ob)) {
					ok = FALSE;
					break;
				}
				v->flag = 0;
				v->tc[0] = c->s;
				v->tc[1] = c->t;
				if (mode == 2) {
					v->cn[0] = 255; v->cn[1] = 0; v->cn[2] = 255; v->cn[3] = 255;
				} else if ((c->flags & HP_CORNER_INHERIT) && c->nmix == 0) {
					const Vertex *rv = &hpDl(file, c->node)->Vertices[c->ref];   /* checked by hpPosition */
					v->cn[0] = rv->r; v->cn[1] = rv->g; v->cn[2] = rv->b; v->cn[3] = rv->a;
				} else {
					memcpy(v->cn, c->cn, 4);
				}
			}

			if (!ok) {
				sysLogPrintf(LOG_WARNING, "handpatch %s: a corner's vertex is missing; part 0x%04x skipped", name, part->host);
				break;
			}

			/* the texture as the model's own lists load it: the marker, expanded below */
			hpCmd(&g, marker, grp->tex);
			triangles += hpEmitGroup(&g, &vp, grp, gc);
			corners += grp->numCorners;
		}

		if (!ok) {
			skipped++;
			continue;
		}

		hpCmd(&g, (u32)G_ENDDL << 24, 0);

		/* markers become the texture loads for this model's pool */
		dst = hpAlloc(slot, sizeof(Gfx) * (numSrc + part->numGroups * 48 + 16));
		wrap = hpAlloc(slot, sizeof(Gfx) * 3);

		if (dst == NULL || wrap == NULL) {
			skipped++;
			continue;
		}

		texLoadFromGdl(src, (s32)((g - src) * sizeof(Gfx)), dst, texpool);

		/* the host draws its own list, then the patch */
		g = wrap;
		hpCmd(&g, (u32)G_DL << 24, (uintptr_t)host->Primary);
		hpCmd(&g, (u32)G_DL << 24, (uintptr_t)dst);
		hpCmd(&g, (u32)G_ENDDL << 24, 0);
		host->Primary = wrap;
		applied++;
	}

	sysLogPrintf(skipped ? LOG_WARNING : LOG_NOTE, "handpatch %s: %d of %d parts, %d triangles%s",
			name, applied, model->numParts, triangles, mode == 2 ? " (tinted)" : "");
}
