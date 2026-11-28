/* Cut caps on Bean's skin. CE caps a hole from the mesh's own border
 * (ragdollBuildCaps, dismember.c:590): edges one piece shares with another,
 * or open edges right at the cut. Bean draws its own skin instead of the
 * native lists, so the same rule runs here on the skin's triangles: the
 * piece each cut triangle was given, its vertices welded by their source
 * data (the skin repeats them per triangle), and an open edge counts when
 * its triangle straddles the cut. The loops depend only on the skin and the
 * cut set, so they are cached; the rim is the posed skin itself, so the cap
 * stays on the edge of the hole. The ragdoll draws the fans. */

static int beanCapFind(int *uf, int x)
{
	while (uf[x] != x)
		x = uf[x] = uf[uf[x]];
	return x;
}

/* The skin's vertices that are the same point, as CE welds by position
 * (dismember.c): two corners are one when a bone they both hang from holds
 * them at the same place in its own space. Matching the whole source record
 * (every weight) kept apart a point Bean repeats with other weights where
 * two of its meshes meet, and a hole's rim fell into open chains, each fanned
 * from its own centre: a sliver of the cap missing at a shoulder (2026-11-28). */
static const int *beanSkinWeld(struct beanSkin *skin)
{
	if (skin->weld || skin->nverts <= 0)
		return skin->weld;
	int *weld = malloc((size_t)skin->nverts * sizeof(int));
	int size = 1;
	while (size < skin->nverts * 8)
		size <<= 1;
	int (*table)[2] = malloc((size_t)size * sizeof(*table)); /* vertex, influence */
	if (!weld || !table) {
		free(weld);
		free(table);
		return NULL;
	}
	for (int i = 0; i < size; i++)
		table[i][0] = -1;
	for (int v = 0; v < skin->nverts; v++)
		weld[v] = v;
	for (int v = 0; v < skin->nverts; v++) {
		const struct beanSkinVert *s = beanSkinReal(skin, v);   /* a neck stub corner as it is */
		for (int w = 0; w < 4; w++) {
			if (!s->weight[w])
				continue;
			uint32_t h = 2166136261u;
			const uint8_t *bytes[2] = { (const uint8_t *)s->p[w], (const uint8_t *)&s->matrix[w] };
			const size_t lens[2] = { sizeof(s->p[w]), sizeof(s->matrix[w]) };
			for (int b = 0; b < 2; b++)
				for (size_t i = 0; i < lens[b]; i++)
					h = (h ^ bytes[b][i]) * 16777619u;
			for (int slot = (int)(h & (uint32_t)(size - 1));; slot = (slot + 1) & (size - 1)) {
				const int other = table[slot][0];
				if (other < 0) {
					table[slot][0] = v;
					table[slot][1] = w;
					break;
				}
				const struct beanSkinVert *o = beanSkinReal(skin, other);
				const int ow = table[slot][1];
				if (o->matrix[ow] == s->matrix[w] && !memcmp(o->p[ow], s->p[w], sizeof(s->p[w]))) {
					weld[beanCapFind(weld, v)] = beanCapFind(weld, other);
					break;
				}
			}
		}
	}
	for (int v = 0; v < skin->nverts; v++)
		weld[v] = beanCapFind(weld, v);
	free(table);
	/* The weld belongs to this level just like the skin it indexes. */
	if (!keepAlloc(weld)) {
		free(weld);
		return NULL;
	}
	skin->weld = weld;
	return weld;
}

#define BEAN_CAP_LOOPS 16
struct beanCapLoop { int piece, other, first, nedges; };
struct beanCapCache {
	const struct beanSkin *skin;
	int cuts, nloops;
	int nverts; /* the skin's size when built: its edges index that many vertices */
	struct beanCapLoop loops[BEAN_CAP_LOOPS];
	int (*edges)[2]; /* skin vertex indices on the loop's own side */
};
static struct beanCapCache s_capCache[32];
static int s_capNext;

/* Skin addresses can be reused by the next level. Never retain their border
 * indices after the level's allocation arena has been freed. */
static void beanCapReset(void)
{
	for (int i = 0; i < (int)(sizeof(s_capCache) / sizeof(*s_capCache)); i++)
		free(s_capCache[i].edges);
	memset(s_capCache, 0, sizeof(s_capCache));
	s_capNext = 0;
}

struct beanCapEdge { int a, b, va, vb, piece, other; };
static int beanCapEdgeCmp(const void *x, const void *y)
{
	const struct beanCapEdge *a = x, *b = y;
	return a->a != b->a ? (a->a < b->a ? -1 : 1) : a->b != b->b ? (a->b < b->b ? -1 : 1) : 0;
}

static const struct beanCapCache *beanCapLoops(struct beanSkin *skin, int cuts, const int *tripiece, const int *triother)
{
	for (int i = 0; i < (int)(sizeof(s_capCache) / sizeof(*s_capCache)); i++)
		/* beanCutCaps writes posed[edge] for the skin as it is now: an entry
		 * for a reused address with another vertex count overran that frame
		 * buffer into a ragdoll's heap list (crash 72eaa2c0, v0.5.13) */
		if (s_capCache[i].skin == skin && s_capCache[i].cuts == cuts && s_capCache[i].nverts == skin->nverts)
			return &s_capCache[i];
	const int *weld = beanSkinWeld(skin);
	const int ntri = skin->nverts / 3;
	if (!weld || ntri <= 0)
		return NULL;
	struct beanCapEdge *edges = malloc((size_t)ntri * 3 * sizeof(*edges));
	struct beanCapEdge *border = malloc((size_t)ntri * 3 * sizeof(*border));
	if (!edges || !border) {
		free(edges);
		free(border);
		return NULL;
	}
	int nedges = 0, nborder = 0;
	for (int t = 0; t < ntri; t++)
		for (int e = 0; e < 3; e++) {
			const int va = t * 3 + e, vb = t * 3 + (e + 1) % 3, a = weld[va], b = weld[vb];
			if (a == b)
				continue;
			struct beanCapEdge *x = &edges[nedges++];
			x->a = a < b ? a : b;
			x->b = a < b ? b : a;
			x->va = va;
			x->vb = vb;
			x->piece = tripiece[t];
			x->other = triother[t];
		}
	qsort(edges, nedges, sizeof(*edges), beanCapEdgeCmp);
	for (int e = 0; e < nedges;) {
		int end = e;
		while (end < nedges && edges[end].a == edges[e].a && edges[end].b == edges[e].b)
			end++;
		for (int i = e; i < end; i++) {
			int mine = 0, other = -1;
			for (int j = e; j < end; j++) {
				if (edges[j].piece == edges[i].piece)
					mine++;
				else if (other < 0)
					other = edges[j].piece;
			}
			/* Shared with another piece, or open on a triangle across the cut */
			if (mine != 1 || (other < 0 && end - e == 1 && edges[i].other < 0) || (other < 0 && end - e > 1))
				continue;
			border[nborder] = edges[i];
			border[nborder++].other = other >= 0 ? other : edges[i].other;
		}
		e = end;
	}
	free(edges);
	int *uf = malloc((size_t)skin->nverts * sizeof(int));
	struct beanCapCache *cache = &s_capCache[s_capNext];
	s_capNext = (s_capNext + 1) % (int)(sizeof(s_capCache) / sizeof(*s_capCache));
	free(cache->edges);
	memset(cache, 0, sizeof(*cache));
	cache->skin = skin;
	cache->nverts = skin->nverts;
	cache->cuts = cuts;
	cache->edges = malloc((size_t)(nborder > 0 ? nborder : 1) * sizeof(*cache->edges));
	int stored = 0;
	/* One loop per hole: each side of a cut (a pair of pieces), split into
	 * its border loops by shared vertices as CE's ragdollBuildCaps does
	 * (union-find, dismember.c). One fan over all of a side spanned every
	 * hole there from their common centre: a Bean guard's hip has two (24
	 * and 9 edges), and stray edges well off the joint pulled it out
	 * further - red flaps standing off a severed leg (2026-11-27). */
#define SAME(x) (border[x].piece == border[i].piece && border[x].other == border[i].other)
	for (int i = 0; uf && cache->edges && i < nborder && cache->nloops < BEAN_CAP_LOOPS; i++) {
		int seen = 0;
		for (int j = 0; j < i && !seen; j++)
			seen = SAME(j);
		if (seen)
			continue;
		for (int v = 0; v < skin->nverts; v++)
			uf[v] = v;
		for (int j = i; j < nborder; j++)
			if (SAME(j))
				uf[beanCapFind(uf, border[j].a)] = beanCapFind(uf, border[j].b);
		for (int j = i; j < nborder && cache->nloops < BEAN_CAP_LOOPS; j++) {
			if (!SAME(j))
				continue;
			const int root = beanCapFind(uf, border[j].a);
			int done = 0;
			for (int k = i; k < j && !done; k++)
				done = SAME(k) && beanCapFind(uf, border[k].a) == root;
			if (done)
				continue;
			struct beanCapLoop *loop = &cache->loops[cache->nloops];
			loop->piece = border[i].piece;
			loop->other = border[i].other;
			loop->first = stored;
			for (int k = j; k < nborder; k++)
				if (SAME(k) && beanCapFind(uf, border[k].a) == root) {
					cache->edges[stored][0] = border[k].va;
					cache->edges[stored++][1] = border[k].vb;
				}
			loop->nedges = stored - loop->first;
			if (loop->nedges >= 3)
				cache->nloops++;
			else
				stored = loop->first; /* a stray edge, not a hole */
		}
	}
#undef SAME
	free(uf);
	free(border);
	return cache;
}

/* After a cut skin is posed: its holes' rims, in the anchor matrix's space */
static void beanSkinCaps(const struct Model *model, struct beanSkin *skin, int anchorIndex, const Vtx *posed,
                         const int *tripiece, const int *triother)
{
	if (!s_cuts.cap)
		return;
	const struct beanCapCache *cache = beanCapLoops(skin, s_cuts.active(model), tripiece, triother);
	for (int l = 0; cache && l < cache->nloops; l++) {
		const struct beanCapLoop *loop = &cache->loops[l];
		s16 (*rim)[2][3] = beanFrameAllocate((size_t)loop->nedges * sizeof(*rim));
		if (!rim)
			return;
		for (int e = 0; e < loop->nedges; e++)
			for (int k = 0; k < 2; k++)
				memcpy(rim[e][k], posed[cache->edges[loop->first + e][k]].v.ob, sizeof(rim[e][k]));
		s_cuts.cap(model, anchorIndex, (const s16 (*)[2][3])rim, loop->nedges, loop->piece, loop->other);
	}
}
