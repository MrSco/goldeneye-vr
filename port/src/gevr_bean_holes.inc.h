/* Holes in Rare's level meshes, closed at load (gevr_bean_world.c buildStage).
 *
 * The release draws a level over its clear colour, so where its ground is
 * open the hole showed that colour: on a night level such as Statue, black on
 * black. The Community Edition's domes reach below the horizon (Statue's to
 * -10.6 degrees; the release's stop at +0.7) and the sky showed through
 * instead (user, 2026-11-05: "a sky reflection" on the grass by a hedge, and
 * "a gap between the ground and fence on bean that lets the skybox through").
 *
 * Two kinds, found over all the release's and the CE's levels:
 *
 * - A face drawn translucent by its vertex alpha that fills a gap in a solid
 *   surface of its own picture: two of its edges are those of solid faces of
 *   that picture on its plane. Statue's grass has six, one of them a wedge
 *   by the hedge fading out over nothing; no other level has one. Such a face
 *   is a piece of the surface and is drawn solid, as the ground round it.
 *
 * - A floor whose open edge stops short of a wall standing beside it: the
 *   wall's foot runs parallel at the floor's height, 1 to 30 cm out, and
 *   nothing covers the strip between or lies under it for a metre. Statue's grass stops 7.6 cm short of
 *   the park's fence for 11 m. The strip is laid as the floor's own surface,
 *   its picture, colour and plane carried on to the wall's foot.
 *
 * Positions are the file's; cm is the stage's scale (centimetres a unit).
 */

#define HOLE_FADE_ALPHA 0xf0     /* below this a vertex alpha fades (DAB FADE_ALPHA) */
#define HOLE_GAP_MIN_CM 1.0f
#define HOLE_GAP_MAX_CM 30.0f
#define HOLE_RISE_CM 5.0f        /* the wall's foot this near the floor's height */
#define HOLE_DEPTH_CM 100.0f     /* and nothing this far under the strip: a hole, not a gutter */
#define HOLE_EDGE_MIN_CM 50.0f
#define HOLE_PARALLEL 0.995f
#define HOLE_TUCK_CM 0.5f        /* the strip runs this far under the wall */
#define HOLE_CELL_CM 200.0f
#define HOLE_BIG_CELLS 64

struct holeEdgeKey {
	int64_t a[3], b[3];
	int id;
};

static int holeEdgeCmp(const void *pa, const void *pb)
{
	const struct holeEdgeKey *x = pa, *y = pb;
	for (int k = 0; k < 3; k++) {
		if (x->a[k] != y->a[k]) return x->a[k] < y->a[k] ? -1 : 1;
	}
	for (int k = 0; k < 3; k++) {
		if (x->b[k] != y->b[k]) return x->b[k] < y->b[k] ? -1 : 1;
	}
	return 0;
}

static float holeNormal(const float *p, float n[3])
{
	const float a[3] = { p[3] - p[0], p[4] - p[1], p[5] - p[2] };
	const float b[3] = { p[6] - p[0], p[7] - p[1], p[8] - p[2] };
	n[0] = a[1] * b[2] - a[2] * b[1];
	n[1] = a[2] * b[0] - a[0] * b[2];
	n[2] = a[0] * b[1] - a[1] * b[0];
	const float len = sqrtf(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
	if (!(len > 0.0f)) {
		n[0] = n[1] = n[2] = 0.0f;
		return 0.0f;
	}
	for (int k = 0; k < 3; k++) n[k] /= len;
	return len * 0.5f;
}

/* An edge's key, its corners in order, at 1/1000 of a cm */
static void holeKey(const float *p, int i, int j, float cm, struct holeEdgeKey *key)
{
	int64_t u[3], v[3];
	for (int k = 0; k < 3; k++) {
		u[k] = llroundf(p[i * 3 + k] * cm * 1000.0f);
		v[k] = llroundf(p[j * 3 + k] * cm * 1000.0f);
	}
	int swap = 0;
	for (int k = 0; k < 3; k++) {
		if (u[k] != v[k]) {
			swap = u[k] > v[k];
			break;
		}
	}
	for (int k = 0; k < 3; k++) {
		key->a[k] = swap ? v[k] : u[k];
		key->b[k] = swap ? u[k] : v[k];
	}
}

static int holeSolid(const struct gevrBeanTri *t)
{
	return t->alpha == 255 && !(t->flags & GEVR_BEAN_F_DECAL);
}

/* The first kind: faded faces filling a gap in their own solid surface */
static int holeSolidFadedGaps(struct gevrBeanMesh *mesh, float cm)
{
	const int n = mesh->ntris;
	struct holeEdgeKey *keys = malloc((size_t)n * 3 * sizeof(*keys));
	int nkeys = 0, made = 0;
	if (!keys) return 0;
	for (int i = 0; i < n; i++) {
		const struct gevrBeanTri *t = &mesh->tris[i];
		if (!holeSolid(t)) continue;
		for (int e = 0; e < 3; e++) {
			holeKey(t->p, e, (e + 1) % 3, cm, &keys[nkeys]);
			keys[nkeys++].id = i;
		}
	}
	qsort(keys, (size_t)nkeys, sizeof(*keys), holeEdgeCmp);
	for (int i = 0; i < n; i++) {
		struct gevrBeanTri *t = &mesh->tris[i];
		float nt[3];
		int fades = 0, shared = 0;
		if (t->alpha != 254 || (t->flags & (GEVR_BEAN_F_DECAL | GEVR_BEAN_F_PLAIN)) || t->tex < 0)
			continue;
		for (int k = 0; k < 3; k++) fades |= t->rgba[k * 4 + 3] < HOLE_FADE_ALPHA;
		if (!fades || !(holeNormal(t->p, nt) > 0.0f)) continue;
		for (int e = 0; e < 3; e++) {
			struct holeEdgeKey key;
			holeKey(t->p, e, (e + 1) % 3, cm, &key);
			const struct holeEdgeKey *hit = bsearch(&key, keys, (size_t)nkeys, sizeof(*keys), holeEdgeCmp);
			if (!hit) continue;
			/* every solid face on this edge, either side of the one found */
			const struct holeEdgeKey *lo = hit, *hi = hit;
			while (lo > keys && !holeEdgeCmp(lo - 1, &key)) lo--;
			while (hi + 1 < keys + nkeys && !holeEdgeCmp(hi + 1, &key)) hi++;
			for (const struct holeEdgeKey *k = lo; k <= hi; k++) {
				const struct gevrBeanTri *o = &mesh->tris[k->id];
				float no[3];
				holeNormal(o->p, no);
				if (o->tex == t->tex && fabsf(no[0] * nt[0] + no[1] * nt[1] + no[2] * nt[2]) > 0.98f) {
					shared++;
					break;
				}
			}
		}
		if (shared < 2) continue;
		t->alpha = 255;
		for (int k = 0; k < 3; k++) t->rgba[k * 4 + 3] = 255;
		made++;
	}
	free(keys);
	return made;
}

/* A uniform grid over x and z: (cell, item) pairs sorted by cell */
struct holeCell {
	int64_t cell;
	int id;
};

static int holeCellCmp(const void *pa, const void *pb)
{
	const struct holeCell *x = pa, *y = pb;
	if (x->cell != y->cell) return x->cell < y->cell ? -1 : 1;
	return x->id - y->id;
}

static int64_t holeCellOf(int gx, int gz)
{
	return ((int64_t)gx << 32) ^ (int64_t)(uint32_t)gz;
}

struct holeGrid {
	struct holeCell *cells;
	int n, cap;
	int *big;
	int nbig, capbig;
	float size;
};

static int holeGridAdd(struct holeGrid *g, int id, float x0, float z0, float x1, float z1, int bigOk)
{
	const int gx0 = (int)floorf(x0 / g->size), gx1 = (int)floorf(x1 / g->size);
	const int gz0 = (int)floorf(z0 / g->size), gz1 = (int)floorf(z1 / g->size);
	const int64_t count = (int64_t)(gx1 - gx0 + 1) * (gz1 - gz0 + 1);
	if (bigOk && count > HOLE_BIG_CELLS) {
		if (g->nbig == g->capbig) {
			int cap = g->capbig ? g->capbig * 2 : 256;
			int *p = realloc(g->big, (size_t)cap * sizeof(*p));
			if (!p) return 0;
			g->big = p;
			g->capbig = cap;
		}
		g->big[g->nbig++] = id;
		return 1;
	}
	if (count > 4096) return 1;   /* a wall's foot this long is not one */
	for (int gx = gx0; gx <= gx1; gx++)
		for (int gz = gz0; gz <= gz1; gz++) {
			if (g->n == g->cap) {
				int cap = g->cap ? g->cap * 2 : 4096;
				struct holeCell *p = realloc(g->cells, (size_t)cap * sizeof(*p));
				if (!p) return 0;
				g->cells = p;
				g->cap = cap;
			}
			g->cells[g->n].cell = holeCellOf(gx, gz);
			g->cells[g->n++].id = id;
		}
	return 1;
}

static void holeGridDone(struct holeGrid *g)
{
	qsort(g->cells, (size_t)g->n, sizeof(*g->cells), holeCellCmp);
}

static void holeGridFree(struct holeGrid *g)
{
	free(g->cells);
	free(g->big);
	memset(g, 0, sizeof(*g));
}

/* The items in the cell holding (x, z): [*first, return) */
static int holeGridFind(const struct holeGrid *g, float x, float z, int *first)
{
	const int64_t cell = holeCellOf((int)floorf(x / g->size), (int)floorf(z / g->size));
	int lo = 0, hi = g->n;
	while (lo < hi) {
		int mid = (lo + hi) / 2;
		if (g->cells[mid].cell < cell) lo = mid + 1;
		else hi = mid;
	}
	*first = lo;
	while (hi < g->n && g->cells[hi].cell == cell) hi++;
	return hi;
}

/* Barycentric weights of (x, z) in a face seen from above */
static int holeWeights(const float *p, float x, float z, float w[3])
{
	const float d = (p[5] - p[8]) * (p[0] - p[6]) + (p[6] - p[3]) * (p[2] - p[8]);
	if (fabsf(d) < 1e-12f) return 0;
	w[0] = ((p[5] - p[8]) * (x - p[6]) + (p[6] - p[3]) * (z - p[8])) / d;
	w[1] = ((p[8] - p[2]) * (x - p[6]) + (p[0] - p[6]) * (z - p[8])) / d;
	w[2] = 1.0f - w[0] - w[1];
	return 1;
}

/* Whether a face (not a decal) is at (x, z) from below - under y by up to
 * depth - to above y by up to rise */
static int holeCovered(const struct gevrBeanMesh *mesh, const struct holeGrid *g, float x, float y, float z,
                       float depth, float rise)
{
	int first;
	const int end = holeGridFind(g, x, z, &first);
	for (int pass = 0; pass < 2; pass++) {
		const int count = pass ? g->nbig : end - first;
		for (int c = 0; c < count; c++) {
			const struct gevrBeanTri *o = &mesh->tris[pass ? g->big[c] : g->cells[first + c].id];
			float w[3];
			if (!holeWeights(o->p, x, z, w) || w[0] < -1e-5f || w[1] < -1e-5f || w[2] < -1e-5f) continue;
			const float at = w[0] * o->p[1] + w[1] * o->p[4] + w[2] * o->p[7];
			if (at > y - depth && at < y + rise) return 1;
		}
	}
	return 0;
}

/* A corner of the strip: the floor face carried on to (x, z) on its plane */
static void holeCorner(const struct gevrBeanTri *f, float x, float z, float *pos, float *nrm, float *uv, uint8_t *rgba)
{
	float w[3];
	if (!holeWeights(f->p, x, z, w)) w[0] = w[1] = w[2] = 1.0f / 3.0f;
	pos[0] = x;
	pos[1] = w[0] * f->p[1] + w[1] * f->p[4] + w[2] * f->p[7];
	pos[2] = z;
	for (int k = 0; k < 3; k++) nrm[k] = w[0] * f->n[k] + w[1] * f->n[3 + k] + w[2] * f->n[6 + k];
	for (int k = 0; k < 2; k++) uv[k] = w[0] * f->uv[k] + w[1] * f->uv[2 + k] + w[2] * f->uv[4 + k];
	for (int k = 0; k < 4; k++) {
		float c = w[0] * f->rgba[k] + w[1] * f->rgba[4 + k] + w[2] * f->rgba[8 + k];
		rgba[k] = (uint8_t)(c < 0.0f ? 0.0f : c > 255.0f ? 255.0f : c + 0.5f);
	}
}

struct holeStrip {
	int floor;
	float p[4][2];   /* the floor's edge from u0 to u1, then the wall's foot at u1 and u0 */
};

/* The second kind: floors stopping short of a wall beside them */
static int holeBridgeWallGaps(struct gevrBeanMesh *mesh, float cm)
{
	const int n = mesh->ntris;
	const float unit = 1.0f / cm;
	struct holeEdgeKey *keys = malloc((size_t)n * 3 * sizeof(*keys));
	uint8_t *open = calloc((size_t)n, 1);   /* bit e: edge e (corners e, e+1) has no other face */
	struct holeGrid walls = { .size = HOLE_CELL_CM * unit }, cover = { .size = HOLE_CELL_CM * unit };
	int (*foot)[3] = NULL;   /* a wall's foot: face, corners i and j */
	int nfoot = 0, capfoot = 0, nstrips = 0, capstrips = 0, made = 0, nkeys = 0;
	struct holeStrip *strips = NULL;
	if (!keys || !open) goto done;

	for (int i = 0; i < n; i++) {
		const struct gevrBeanTri *t = &mesh->tris[i];
		if (t->flags & GEVR_BEAN_F_DECAL) continue;
		for (int e = 0; e < 3; e++) {
			holeKey(t->p, e, (e + 1) % 3, cm, &keys[nkeys]);
			keys[nkeys++].id = i * 3 + e;
		}
	}
	qsort(keys, (size_t)nkeys, sizeof(*keys), holeEdgeCmp);
	for (int a = 0; a < nkeys;) {
		int b = a + 1;
		while (b < nkeys && !holeEdgeCmp(&keys[a], &keys[b])) b++;
		if (b - a == 1) open[keys[a].id / 3] |= (uint8_t)(1 << (keys[a].id % 3));
		a = b;
	}

	/* the walls' feet: a solid upright face's edge with its third corner above */
	for (int i = 0; i < n; i++) {
		const struct gevrBeanTri *t = &mesh->tris[i];
		float nt[3];
		if (!holeSolid(t) || !(holeNormal(t->p, nt) > 0.0f) || fabsf(nt[1]) > 0.3f) continue;
		if (!holeGridAdd(&cover, i, fminf(t->p[0], fminf(t->p[3], t->p[6])), fminf(t->p[2], fminf(t->p[5], t->p[8])),
		                 fmaxf(t->p[0], fmaxf(t->p[3], t->p[6])), fmaxf(t->p[2], fmaxf(t->p[5], t->p[8])), 1))
			goto done;
		for (int e = 0; e < 3; e++) {
			const float *A = &t->p[e * 3], *B = &t->p[((e + 1) % 3) * 3], *C = &t->p[((e + 2) % 3) * 3];
			const float dx = B[0] - A[0], dz = B[2] - A[2], l2 = dx * dx + dz * dz;
			if (l2 < 100.0f * unit * unit) continue;
			const float u = ((C[0] - A[0]) * dx + (C[2] - A[2]) * dz) / l2;
			if (C[1] < A[1] + (B[1] - A[1]) * u + HOLE_RISE_CM * unit) continue;
			if (nfoot == capfoot) {
				int cap = capfoot ? capfoot * 2 : 1024;
				int (*p)[3] = realloc(foot, (size_t)cap * sizeof(*p));
				if (!p) goto done;
				foot = p;
				capfoot = cap;
			}
			foot[nfoot][0] = i;
			foot[nfoot][1] = e;
			foot[nfoot][2] = (e + 1) % 3;
			if (!holeGridAdd(&walls, nfoot, fminf(A[0], B[0]), fminf(A[2], B[2]), fmaxf(A[0], B[0]), fmaxf(A[2], B[2]), 0))
				goto done;
			nfoot++;
		}
	}
	/* every other face covers ground too */
	for (int i = 0; i < n; i++) {
		const struct gevrBeanTri *t = &mesh->tris[i];
		float nt[3];
		if ((t->flags & GEVR_BEAN_F_DECAL) || (holeSolid(t) && holeNormal(t->p, nt) > 0.0f && fabsf(nt[1]) <= 0.3f))
			continue;
		if (!holeGridAdd(&cover, i, fminf(t->p[0], fminf(t->p[3], t->p[6])), fminf(t->p[2], fminf(t->p[5], t->p[8])),
		                 fmaxf(t->p[0], fmaxf(t->p[3], t->p[6])), fmaxf(t->p[2], fmaxf(t->p[5], t->p[8])), 1))
			goto done;
	}
	holeGridDone(&walls);
	holeGridDone(&cover);

	for (int i = 0; i < n; i++) {
		const struct gevrBeanTri *t = &mesh->tris[i];
		float nt[3];
		if (!open[i] || !holeSolid(t) || !(holeNormal(t->p, nt) > 0.0f) || fabsf(nt[1]) < 0.7f) continue;
		const float cx = (t->p[0] + t->p[3] + t->p[6]) / 3.0f, cz = (t->p[2] + t->p[5] + t->p[8]) / 3.0f;
		for (int e = 0; e < 3; e++) {
			if (!(open[i] & (1 << e))) continue;
			const float *A = &t->p[e * 3], *B = &t->p[((e + 1) % 3) * 3];
			const float L = hypotf(B[0] - A[0], B[2] - A[2]);
			if (L < HOLE_EDGE_MIN_CM * unit) continue;
			const float ex = (B[0] - A[0]) / L, ez = (B[2] - A[2]) / L;
			float nx = -ez, nz = ex;
			if ((A[0] - cx) * nx + (A[2] - cz) * nz < 0.0f) {
				nx = -nx;
				nz = -nz;
			}
			/* the nearest wall's foot alongside, over the cells the edge crosses */
			float bestGap = 1e30f, bestU0 = 0, bestU1 = 0;
			const int gx0 = (int)floorf(fminf(A[0], B[0]) / walls.size) - 1, gx1 = (int)floorf(fmaxf(A[0], B[0]) / walls.size) + 1;
			const int gz0 = (int)floorf(fminf(A[2], B[2]) / walls.size) - 1, gz1 = (int)floorf(fmaxf(A[2], B[2]) / walls.size) + 1;
			if ((int64_t)(gx1 - gx0 + 1) * (gz1 - gz0 + 1) > 4096) continue;
			for (int gx = gx0; gx <= gx1; gx++)
				for (int gz = gz0; gz <= gz1; gz++) {
					int first;
					const int end = holeGridFind(&walls, (gx + 0.5f) * walls.size, (gz + 0.5f) * walls.size, &first);
					for (int c = first; c < end; c++) {
						const struct gevrBeanTri *w = &mesh->tris[foot[walls.cells[c].id][0]];
						const float *WA = &w->p[foot[walls.cells[c].id][1] * 3], *WB = &w->p[foot[walls.cells[c].id][2] * 3];
						const float wl = hypotf(WB[0] - WA[0], WB[2] - WA[2]);
						if (!(wl > 0.0f) || fabsf(((WB[0] - WA[0]) * ex + (WB[2] - WA[2]) * ez) / wl) < HOLE_PARALLEL) continue;
						const float d1 = (WA[0] - A[0]) * nx + (WA[2] - A[2]) * nz, d2 = (WB[0] - A[0]) * nx + (WB[2] - A[2]) * nz;
						if (d1 < HOLE_GAP_MIN_CM * unit || d1 > HOLE_GAP_MAX_CM * unit ||
						    d2 < HOLE_GAP_MIN_CM * unit || d2 > HOLE_GAP_MAX_CM * unit)
							continue;
						const float u1 = (WA[0] - A[0]) * ex + (WA[2] - A[2]) * ez, u2 = (WB[0] - A[0]) * ex + (WB[2] - A[2]) * ez;
						const float s0 = fmaxf(0.0f, fminf(u1, u2)), s1 = fminf(L, fmaxf(u1, u2));
						if (s1 - s0 < 0.3f * fminf(L, wl)) continue;
						const float um = (s0 + s1) * 0.5f, ey = A[1] + (B[1] - A[1]) * um / L;
						const float wy = u2 != u1 ? WA[1] + (WB[1] - WA[1]) * (um - u1) / (u2 - u1) : (WA[1] + WB[1]) * 0.5f;
						if (fabsf(wy - ey) > HOLE_RISE_CM * unit) continue;
						const float gap = (d1 + d2) * 0.5f;
						if (gap < bestGap) {
							bestGap = gap;
							bestU0 = s0;
							bestU1 = s1;
						}
					}
				}
			if (bestGap > HOLE_GAP_MAX_CM * unit) continue;
			/* open along the strip, half way out: no face there at the floor's height
			 * or under it (a gutter's floor, a step down) */
			int gaps = 0;
			for (int s = 0; s < 5; s++) {
				const float u = bestU0 + (bestU1 - bestU0) * (s + 0.5f) / 5.0f;
				const float x = A[0] + ex * u + nx * bestGap * 0.5f, z = A[2] + ez * u + nz * bestGap * 0.5f;
				gaps += !holeCovered(mesh, &cover, x, A[1] + (B[1] - A[1]) * u / L, z,
				                      HOLE_DEPTH_CM * unit, HOLE_RISE_CM * unit);
			}
			if (gaps < 4) continue;
			if (nstrips == capstrips) {
				int cap = capstrips ? capstrips * 2 : 64;
				struct holeStrip *p = realloc(strips, (size_t)cap * sizeof(*p));
				if (!p) goto done;
				strips = p;
				capstrips = cap;
			}
			struct holeStrip *s = &strips[nstrips++];
			const float out = bestGap + HOLE_TUCK_CM * unit;
			s->floor = i;
			s->p[0][0] = A[0] + ex * bestU0;
			s->p[0][1] = A[2] + ez * bestU0;
			s->p[1][0] = A[0] + ex * bestU1;
			s->p[1][1] = A[2] + ez * bestU1;
			s->p[2][0] = s->p[1][0] + nx * out;
			s->p[2][1] = s->p[1][1] + nz * out;
			s->p[3][0] = s->p[0][0] + nx * out;
			s->p[3][1] = s->p[0][1] + nz * out;
		}
	}

	if (nstrips) {
		struct gevrBeanTri *tris = realloc(mesh->tris, (size_t)(n + nstrips * 2) * sizeof(*tris));
		if (!tris) goto done;
		mesh->tris = tris;
		for (int k = 0; k < nstrips; k++) {
			static const int order[2][3] = { { 0, 1, 2 }, { 0, 2, 3 } };
			const struct gevrBeanTri ground = mesh->tris[strips[k].floor];
			float nf[3], nb[3];
			holeNormal(ground.p, nf);
			for (int half = 0; half < 2; half++) {
				struct gevrBeanTri *b = &mesh->tris[mesh->ntris];
				*b = ground;
				for (int c = 0; c < 3; c++) {
					const float *xz = strips[k].p[order[half][c]];
					holeCorner(&ground, xz[0], xz[1], &b->p[c * 3], &b->n[c * 3], &b->uv[c * 2], &b->rgba[c * 4]);
				}
				/* the floor's winding */
				if (holeNormal(b->p, nb) > 0.0f && nb[0] * nf[0] + nb[1] * nf[1] + nb[2] * nf[2] < 0.0f) {
					float p[3], nn[3], uv[2];
					uint8_t c[4];
					memcpy(p, &b->p[3], sizeof(p)); memcpy(&b->p[3], &b->p[6], sizeof(p)); memcpy(&b->p[6], p, sizeof(p));
					memcpy(nn, &b->n[3], sizeof(nn)); memcpy(&b->n[3], &b->n[6], sizeof(nn)); memcpy(&b->n[6], nn, sizeof(nn));
					memcpy(uv, &b->uv[2], sizeof(uv)); memcpy(&b->uv[2], &b->uv[4], sizeof(uv)); memcpy(&b->uv[4], uv, sizeof(uv));
					memcpy(c, &b->rgba[4], sizeof(c)); memcpy(&b->rgba[4], &b->rgba[8], sizeof(c)); memcpy(&b->rgba[8], c, sizeof(c));
				}
				mesh->ntris++;
			}
		}
		made = nstrips;
	}

done:
	free(keys);
	free(open);
	free(foot);
	free(strips);
	holeGridFree(&walls);
	holeGridFree(&cover);
	return made;
}

/* Both, the first first: a face it makes solid is ground the second sees */
static void beanCloseLevelHoles(struct gevrBeanMesh *mesh, float cm, int *faded, int *strips)
{
	*faded = *strips = 0;
	if (!mesh || mesh->ntris <= 0 || !(cm > 0.0f)) return;
	*faded = holeSolidFadedGaps(mesh, cm);
	*strips = holeBridgeWallGaps(mesh, cm);
}
