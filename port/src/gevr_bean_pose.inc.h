/* Weighted bind-pose mapping follows DAB's importer (port/licenses/dabs-mod.txt).
 * Pose and commands are copied into the frame arena: model definitions are
 * shared by instances, and the renderer consumes the lists after this walk. */
static int beanInverse3(const float a[9], float out[9])
{
	float d = a[0] * (a[4] * a[8] - a[5] * a[7]) - a[1] * (a[3] * a[8] - a[5] * a[6]) +
	          a[2] * (a[3] * a[7] - a[4] * a[6]);
	if (!isfinite(d) || fabsf(d) < 1e-12f)
		return 0;
	out[0] = (a[4] * a[8] - a[5] * a[7]) / d;
	out[1] = (a[2] * a[7] - a[1] * a[8]) / d;
	out[2] = (a[1] * a[5] - a[2] * a[4]) / d;
	out[3] = (a[5] * a[6] - a[3] * a[8]) / d;
	out[4] = (a[0] * a[8] - a[2] * a[6]) / d;
	out[5] = (a[2] * a[3] - a[0] * a[5]) / d;
	out[6] = (a[3] * a[7] - a[4] * a[6]) / d;
	out[7] = (a[1] * a[6] - a[0] * a[7]) / d;
	out[8] = (a[0] * a[4] - a[1] * a[3]) / d;
	return 1;
}

#include "gevr_bean_wounds.inc.h"
#include "gevr_bean_gpu.inc.h"
#include "gevr_bean_caps.inc.h"

/* A root registry entry is rendered once, before native limb/LOD traversal.
 * Heads, hats and held weapons remain independent native model attachments. */
void *gevrBeanWholeModelGdl(Model *model)
{
    if(!model || !model->obj)return NULL;
    ModelNode *root=model->obj->RootNode;
    struct beanSwap *swap=activeSwap(root);
    return swap && swap->skin && !swap->original ? gevrBeanRenderGdl(root,NULL,model) : NULL;
}

void *gevrBeanWholeJointGdl(ModelHitEntry *entry,ModelHitEntry *first)
{
    /* The joint list may interleave body, weapon and hat models after sorting.
     * Draw each instance once, retaining all native hit and attachment entries. */
    for(ModelHitEntry *p=first;p && p!=entry;p=p->next)
        if(p->model==entry->model)return NULL;
    return gevrBeanWholeModelGdl(entry->model);
}

void *gevrBeanRenderGdl(void *node, void *current, Model *model)
{
	struct beanSwap *swap = activeSwap(node);
	struct beanSkin *skin = swap ? swap->skin : NULL;
	if (swap && swap->original && !current)
		return NULL;
	if (gevrBeanIsEnabled() && swap && swap->deferredMatrix && !skin) {
		int matrix = nodeMatrix(node);
		if (matrix >= 0) {
			Gfx *wrapper = beanFrameAllocate(3 * sizeof(Gfx));
			if (wrapper) {
				gSPMatrix(wrapper, (void *)(uintptr_t)(0x03000000u + matrix * sizeof(Mtx)),
				          G_MTX_MODELVIEW | G_MTX_LOAD | G_MTX_NOPUSH);
				gSPDisplayList(wrapper + 1, swap->ours);
				gSPEndDisplayList(wrapper + 2);
				return wrapper;
			}
		}
	}
	if (!gevrBeanIsEnabled() || !skin || !model || !model->render_pos || !model->obj)
		return gevrBeanShownGdl(node, current);
	int count = model->obj->numMatrices;
	int anchorIndex = skin->anchor >= 0 ? skin->anchor : nodeMatrix(node);
	if (anchorIndex < 0 || anchorIndex >= count)
		return current;
	ModelNode *neck = node;
	while (neck && nodeOp(neck) != MODELNODE_OPCODE_GROUP)
		neck = neck->Parent;
	int backIndex = neck ? nodeMatrix(neck->Parent) : -1;
	const float (*anchor)[4] = model->render_pos[anchorIndex].pos.m;
	float linear[9], inverse[9];
	for (int i = 0; i < 3; i++)
		for (int j = 0; j < 3; j++)
			linear[i * 3 + j] = anchor[j][i];
	if (!beanInverse3(linear, inverse))
		return current;
	const int cut = s_cuts.active && s_cuts.active(model);
	Gfx *gpu = cut ? NULL : beanSkinGpuDraw(skin, model, anchorIndex, backIndex, neck, inverse);
	if (gpu)
		return gpu;
	Vtx *vertices = beanFrameAllocate((size_t)skin->nverts * sizeof(Vtx));
	Gfx *commands = beanFrameAllocate((size_t)(skin->ncommands + 3) * sizeof(Gfx));
	if (!vertices || !commands)
		return current;
	memcpy(vertices, skin->vtx, skin->nverts * sizeof(Vtx));
	int owner = anchorIndex;
	int *tripiece = cut ? beanFrameAllocate((size_t)(skin->nverts / 3 + 1) * sizeof(int) * 2) : NULL;
	int *triother = tripiece ? tripiece + skin->nverts / 3 + 1 : NULL;
	for (int v = 0; v < skin->nverts; v++) {
        /* Skin vertices are emitted as independent triangle triples. Choose
         * one piece for the whole triangle, then rehome cross-cut weights in
         * the captured pose. Per-vertex choices would leave stretched edges. */
        if (cut && v % 3 == 0) {
            int matrices[12], pieces[12], scores[12] = {0}, count = 0, best = 0;
            for (int c = v; c < v + 3 && c < skin->nverts; c++) for (int w = 0; w < 4; w++) {
                int matrix = skin->verts[c].matrix[w];
                if (matrix == -2) matrix = anchorIndex;
                if (matrix == -3) matrix = backIndex >= 0 ? backIndex : anchorIndex;
                if (!skin->verts[c].weight[w] || matrix < 0 || matrix >= model->obj->numMatrices) continue;
                int piece = s_cuts.piece(model, matrix), at;
                for (at = 0; at < count && pieces[at] != piece; at++) {}
                if (at == count) { matrices[count] = matrix; pieces[count++] = piece; }
                scores[at] += skin->verts[c].weight[w];
                if (scores[at] > scores[best]) best = at;
            }
            owner = count ? matrices[best] : anchorIndex;
            if (tripiece) {
                int second = -1;
                for (int at = 0; at < count; at++)
                    if (at != best && (second < 0 || scores[at] > scores[second])) second = at;
                tripiece[v / 3] = count ? pieces[best] : s_cuts.piece(model, anchorIndex);
                triother[v / 3] = second >= 0 ? pieces[second] : -1;
            }
        }
		const struct beanSkinVert *src = &skin->verts[v];
		float p[3] = {0}, n[3] = {0}, total = 0;
		for (int w = 0; w < 4; w++) {
			int matrix = src->matrix[w];
			int back = matrix == -3;
			if (matrix == -2)
				matrix = anchorIndex;
			if (back)
				matrix = backIndex >= 0 ? backIndex : anchorIndex;
			if (!src->weight[w] || matrix < 0 || matrix >= count)
				continue;
			float weight = src->weight[w];
			float local[3], normalLocal[3];
			for (int k = 0; k < 3; k++)
				local[k] = src->p[w][k] + (back && neck ? neck->Data->Group.Origin.f[k] : 0);
            memcpy(normalLocal, src->n[w], sizeof(normalLocal));
            if (cut) matrix = s_cuts.remap(model, owner, matrix, local, normalLocal);
            const float (*m)[4] = model->render_pos[matrix].pos.m;
			for (int k = 0; k < 3; k++) {
				p[k] += weight * (local[0] * m[0][k] + local[1] * m[1][k] + local[2] * m[2][k] + m[3][k]);
				n[k] += weight * (normalLocal[0] * m[0][k] + normalLocal[1] * m[1][k] + normalLocal[2] * m[2][k]);
			}
			total += weight;
		}
		if (total <= 0)
			continue;
		for (int k = 0; k < 3; k++)
			p[k] = p[k] / total - anchor[3][k];
		float local[3], normal[3];
		applyR(inverse, p, local);
		applyR(inverse, n, normal);
		float length = sqrtf(normal[0] * normal[0] + normal[1] * normal[1] + normal[2] * normal[2]);
		for (int k = 0; k < 3; k++) {
			vertices[v].v.ob[k] = posClamp(local[k]);
			vertices[v].n.n[k] = length > 1e-6f ? (s8)(normal[k] * 127.0f / length) : 0;
		}
	}
	if (tripiece)
		beanSkinCaps(model, skin, anchorIndex, vertices, tripiece, triother);
	gSPMatrix(commands, (void *)(uintptr_t)(0x03000000u + anchorIndex * sizeof(Mtx)),
	          G_MTX_MODELVIEW | G_MTX_LOAD | G_MTX_NOPUSH);
	memcpy(commands + 1, skin->gdl, skin->ncommands * sizeof(Gfx));
	for (int c = 1; c <= skin->ncommands; c++) {
		if ((commands[c].words.w0 >> 24) == (G_VTX & 255)) {
			uintptr_t offset = commands[c].words.w1 - (uintptr_t)skin->vtx;
			if (offset < (uintptr_t)skin->nverts * sizeof(Vtx))
				commands[c].words.w1 = osVirtualToPhysical((uint8_t *)vertices + offset);
		}
	}
	return commands;
}

/*
 * A destroyed prop (DAB xblamesh.c xblaMeshBruiseMap 9812, xblaMeshBruiseColours
 * 10712, xblaMeshDeformOffsets 10340). objDeform (propobj.c) gives the instance
 * its own copy of the node's vertices, nine in ten of the upper half's (one in
 * five of the lower's) blackened and all of them pressed down and jittered,
 * and the game's list reads it through segment 4. Bean's list is the model
 * file's, shared and with its own colours, so a destroyed barrel stayed whole
 * and clean (user, 2026-10-14, Dam). Each Bean vertex follows the stock
 * triangle nearest it, by the barycentric weights of its closest point there
 * (Gouraud across that triangle): colour scaled by now/was, moved by the
 * offset. objDeform jitters each stock vertex up to 40 units, seeded by its
 * position, so the stock surface crumples without tearing; three nearest
 * vertices by distance changed sets between neighbouring Bean vertices and
 * tore the mesh into black shards (user, 2026-10-14). The copy draws on the
 * CPU, as DAB's destroyed props do.
 */
struct beanScorch {
	const void *stock;
	int nverts;
	uint16_t (*ref)[3];
	float (*w)[3];
};

static void beanScorchPosition(const struct beanSwap *swap, int i, float p[3])
{
	if (swap->emittedWorld) {
		const GevrWorldVertex *v = (const GevrWorldVertex *)swap->emittedVtx + i;
		p[0] = v->p[0]; p[1] = v->p[1]; p[2] = v->p[2];
	} else {
		const Vtx *v = (const Vtx *)swap->emittedVtx + i;
		p[0] = v->v.ob[0]; p[1] = v->v.ob[1]; p[2] = v->v.ob[2];
	}
}

/* The node's own triangles, as indices into its vertices (segment 4) */
static int beanScorchTriangles(ModelNode *node, int count, uint16_t (**out)[3])
{
	Vertex *vertices;
	int vcount, type, n = 0, cap = 0;
	Gfx *gdl;
	unsigned slot[16];
	uint16_t (*tris)[3] = NULL;
	dlVerts(node, &vertices, &vcount, &type, &gdl);
	*out = NULL;
	if (!gdl)
		return 0;
	if (((uintptr_t)gdl >> 24) == SPSEGMENT_MODEL_COL1) {
		void *base = nodeOp(node) == MODELNODE_OPCODE_DL ? node->Data->DisplayList.BaseAddr
		                                                       : node->Data->DisplayListCollisions.BaseAddr;
		gdl = (Gfx *)((uint8_t *)base + ((uintptr_t)gdl & 0xffffff));
	}
	for (int k = 0; k < 16; k++)
		slot[k] = 0xffff;
	for (int c = 0; c < 8192; c++) {
		const unsigned w0 = gdl[c].words.w0, op = w0 >> 24;
		const uintptr_t w1 = gdl[c].words.w1;
		unsigned t[4][3];
		int nt = 0;
		if (op == (G_ENDDL & 255))
			break;
		if (op == (G_VTX & 255)) {
			const unsigned first = (w0 >> 16) & 15, nv = (w0 & 0xffff) / sizeof(Vertex);
			const unsigned index = (unsigned)(w1 & 0xffffff) / sizeof(Vertex);
			for (unsigned v = 0; v < nv && first + v < 16; v++)
				slot[first + v] = (w1 >> 24) == SPSEGMENT_MODEL_VTX && index + v < (unsigned)count ? index + v : 0xffff;
		} else if (op == (G_TRI1 & 255)) {
			t[0][0] = (w1 >> 16 & 255) / 10;
			t[0][1] = (w1 >> 8 & 255) / 10;
			t[0][2] = (w1 & 255) / 10;
			nt = 1;
		} else if (op == 0xb1) {
			for (int k = 0; k < 4; k++) {
				const unsigned a = w1 >> (k * 8) & 15, b = w1 >> (k * 8 + 4) & 15, cc = w0 >> (k * 4) & 15;
				if (a || b || cc) {
					t[nt][0] = a;
					t[nt][1] = b;
					t[nt][2] = cc;
					nt++;
				}
			}
		}
		for (int k = 0; k < nt; k++) {
			if (t[k][0] >= 16 || t[k][1] >= 16 || t[k][2] >= 16 || slot[t[k][0]] == 0xffff ||
			    slot[t[k][1]] == 0xffff || slot[t[k][2]] == 0xffff)
				continue;
			if (n == cap) {
				cap = cap ? cap * 2 : 256;
				uint16_t (*grown)[3] = realloc(tris, (size_t)cap * sizeof(*tris));
				if (!grown) {
					free(tris);
					return 0;
				}
				tris = grown;
			}
			for (int v = 0; v < 3; v++)
				tris[n][v] = (uint16_t)slot[t[k][v]];
			n++;
		}
	}
	*out = tris;
	return n;
}

/* The point of triangle abc nearest p, as barycentric weights (Ericson,
 * Real-Time Collision Detection 5.1.5); returns the squared distance. */
static float beanScorchClosest(const float p[3], const float a[3], const float b[3], const float c[3], float w[3])
{
	float ab[3], ac[3], ap[3], bp[3], cp[3];
	for (int k = 0; k < 3; k++) {
		ab[k] = b[k] - a[k];
		ac[k] = c[k] - a[k];
		ap[k] = p[k] - a[k];
		bp[k] = p[k] - b[k];
		cp[k] = p[k] - c[k];
	}
#define DOT3(x, y) ((x)[0] * (y)[0] + (x)[1] * (y)[1] + (x)[2] * (y)[2])
	const float d1 = DOT3(ab, ap), d2 = DOT3(ac, ap), d3 = DOT3(ab, bp), d4 = DOT3(ac, bp);
	const float d5 = DOT3(ab, cp), d6 = DOT3(ac, cp);
	const float va = d3 * d6 - d5 * d4, vb = d5 * d2 - d1 * d6, vc = d1 * d4 - d3 * d2;
#undef DOT3
	if (d1 <= 0 && d2 <= 0) {
		w[0] = 1; w[1] = 0; w[2] = 0;
	} else if (d3 >= 0 && d4 <= d3) {
		w[0] = 0; w[1] = 1; w[2] = 0;
	} else if (vc <= 0 && d1 >= 0 && d3 <= 0) {
		const float v = d1 / (d1 - d3);
		w[0] = 1 - v; w[1] = v; w[2] = 0;
	} else if (d6 >= 0 && d5 <= d6) {
		w[0] = 0; w[1] = 0; w[2] = 1;
	} else if (vb <= 0 && d2 >= 0 && d6 <= 0) {
		const float v = d2 / (d2 - d6);
		w[0] = 1 - v; w[1] = 0; w[2] = v;
	} else if (va <= 0 && (d4 - d3) >= 0 && (d5 - d6) >= 0) {
		const float v = (d4 - d3) / ((d4 - d3) + (d5 - d6));
		w[0] = 0; w[1] = 1 - v; w[2] = v;
	} else {
		const float denom = 1.0f / (va + vb + vc);
		w[1] = vb * denom;
		w[2] = vc * denom;
		w[0] = 1 - w[1] - w[2];
	}
	float d = 0;
	for (int k = 0; k < 3; k++) {
		const float q = w[0] * a[k] + w[1] * b[k] + w[2] * c[k] - p[k];
		d += q * q;
	}
	return d;
}

static struct beanScorch *beanScorchMap(struct beanSwap *swap, const Vertex *stock, int count)
{
	struct beanScorch *sc = swap->scorch;
	if (sc && sc->stock == stock && sc->nverts == swap->emittedVerts)
		return sc;
	if (!sc) {
		sc = calloc(1, sizeof(*sc));
		if (!sc || !keepAlloc(sc)) { free(sc); return NULL; }
		swap->scorch = sc;
	}
	int n = swap->emittedVerts;
	uint16_t (*ref)[3] = malloc((size_t)n * sizeof(*ref));
	float (*w)[3] = malloc((size_t)n * sizeof(*w));
	if (!ref || !w || !keepAlloc(ref) || !keepAlloc(w)) { free(ref); free(w); sc->stock = NULL; return NULL; }
	uint16_t (*tris)[3] = NULL;
	const int ntris = beanScorchTriangles(swap->node, count, &tris);
	for (int i = 0; i < n && ntris > 0; i++) {
		float p[3], best = 1e30f, bw[3] = {1, 0, 0};
		int bt = 0;
		beanScorchPosition(swap, i, p);
		for (int t = 0; t < ntris; t++) {
			float a[3] = {stock[tris[t][0]].coord.x, stock[tris[t][0]].coord.y, stock[tris[t][0]].coord.z};
			float b[3] = {stock[tris[t][1]].coord.x, stock[tris[t][1]].coord.y, stock[tris[t][1]].coord.z};
			float c[3] = {stock[tris[t][2]].coord.x, stock[tris[t][2]].coord.y, stock[tris[t][2]].coord.z};
			float w[3];
			const float d = beanScorchClosest(p, a, b, c, w);
			if (d < best) {
				best = d;
				bt = t;
				memcpy(bw, w, sizeof(bw));
			}
		}
		for (int k = 0; k < 3; k++) {
			ref[i][k] = tris[bt][k];
			w[i][k] = bw[k];
		}
	}
	free(tris);
	/* no triangles to follow (a list this walk cannot read): the three nearest vertices */
	for (int i = 0; i < n && ntris <= 0; i++) {
		float p[3], best[3] = {1e30f, 1e30f, 1e30f};
		int bi[3] = {0, 0, 0};
		beanScorchPosition(swap, i, p);
		for (int s = 0; s < count && s < 65536; s++) {
			float dx = p[0] - stock[s].coord.x, dy = p[1] - stock[s].coord.y, dz = p[2] - stock[s].coord.z;
			float d = dx * dx + dy * dy + dz * dz;
			for (int k = 0; k < 3; k++) {
				if (d < best[k]) {
					for (int m = 2; m > k; m--) { best[m] = best[m - 1]; bi[m] = bi[m - 1]; }
					best[k] = d; bi[k] = s;
					break;
				}
			}
		}
		float total = 0;
		for (int k = 0; k < 3; k++) {
			w[i][k] = best[k] < 1e29f ? 1.0f / (best[k] + 1.0f) : 0.0f;
			ref[i][k] = (uint16_t)bi[k];
			total += w[i][k];
		}
		for (int k = 0; k < 3; k++)
			w[i][k] = total > 0 ? w[i][k] / total : (k == 0);
	}
	sc->stock = stock;
	sc->nverts = n;
	sc->ref = ref;
	sc->w = w;
	return sc;
}

void *gevrBeanScorchedGdl(void *node, void *shown, const void *stockVertices, const void *nowVertices, int count)
{
	struct beanSwap *swap = activeSwap(node);
	const Vertex *was = stockVertices, *now = nowVertices;
	if (!gevrBeanIsEnabled() || !swap || swap->skin || !swap->emitted || swap->deferredMatrix ||
	    !was || !now || was == now || count <= 0 || swap->emittedVerts <= 0)
		return shown;
	struct beanScorch *sc = beanScorchMap(swap, was, count);
	if (!sc)
		return shown;
	const int n = swap->emittedVerts;
	const size_t vsize = swap->emittedWorld ? sizeof(GevrWorldVertex) : sizeof(Vtx);
	uint8_t *vertices = beanFrameAllocate((size_t)n * vsize);
	Gfx *commands = beanFrameAllocate((size_t)(swap->emittedCommands + 3) * sizeof(Gfx));
	if (!vertices || !commands)
		return shown;
	memcpy(vertices, swap->emittedVtx, (size_t)n * vsize);
	for (int i = 0; i < n; i++) {
		float colour[3] = {0, 0, 0}, offset[3] = {0, 0, 0};
		for (int k = 0; k < 3; k++) {
			const Vertex *a = &was[sc->ref[i][k]], *b = &now[sc->ref[i][k]];
			const float wk = sc->w[i][k];
			const u8 wa[3] = {a->r, a->g, a->b}, nb[3] = {b->r, b->g, b->b};
			for (int c = 0; c < 3; c++)
				colour[c] += wk * (wa[c] ? fminf((float)nb[c] / (float)wa[c], 1.0f) : 1.0f);
			offset[0] += wk * (float)(b->coord.x - a->coord.x);
			offset[1] += wk * (float)(b->coord.y - a->coord.y);
			offset[2] += wk * (float)(b->coord.z - a->coord.z);
		}
		if (swap->emittedWorld) {
			GevrWorldVertex *v = (GevrWorldVertex *)vertices + i;
			for (int c = 0; c < 3; c++) {
				v->rgba[c] = (u8)(v->rgba[c] * colour[c] + 0.5f);
				v->p[c] += offset[c];
			}
		} else {
			Vtx *v = (Vtx *)vertices + i;
			for (int c = 0; c < 3; c++) {
				v->v.cn[c] = (u8)(v->v.cn[c] * colour[c] + 0.5f);
				v->v.ob[c] = posClamp((float)v->v.ob[c] + offset[c]);
			}
		}
	}
	/* the list again, its vertex loads pointed at the copy and without the
	 * G_GEVR_MESH header that would draw the shared buffers */
	Gfx *list = commands + 3;
	int matrix = nodeMatrix(node);
	Gfx *head = commands;
	memcpy(list, swap->emitted, (size_t)swap->emittedCommands * sizeof(Gfx));
	const uintptr_t base = (uintptr_t)swap->emittedVtx, end = base + (uintptr_t)n * vsize;
	for (int c = 0; c < swap->emittedCommands; c++) {
		const unsigned op = (unsigned)(list[c].words.w0 >> 24) & 255;
		if (op == (G_VTX & 255) || op == G_GEVR_WORLD_VTX) {
			const uintptr_t at = (uintptr_t)list[c].words.w1;
			if (at >= base && at < end)
				list[c].words.w1 = (uintptr_t)(vertices + (at - base));
		}
	}
	if (matrix < 0)
		return list;
	gSPMatrix(head, (void *)(uintptr_t)(0x03000000u + matrix * sizeof(Mtx)),
	          G_MTX_MODELVIEW | G_MTX_LOAD | G_MTX_NOPUSH);
	gSPDisplayList(head + 1, list);
	gSPEndDisplayList(head + 2);
	return head;
}
