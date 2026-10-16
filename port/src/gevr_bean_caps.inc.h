/* Cut caps on Bean's skin. CE caps a hole from the mesh's own border
 * (ragdollBuildCaps, dismember.c:590): edges one piece shares with another,
 * or open edges right at the cut. Bean draws its own skin instead of the
 * native lists, so the same rule runs here on the skin's triangles: the
 * piece each cut triangle was given, its vertices welded by their source
 * data (the skin repeats them per triangle), and an open edge counts when
 * its triangle straddles the cut. The loops depend only on the skin and the
 * cut set, so they are cached; the rim is the posed skin itself, so the cap
 * stays on the edge of the hole. The ragdoll draws the fans. */

/* The skin's vertices that are the same vertex, by their source data */
static const int *beanSkinWeld(struct beanSkin *skin)
{
	if (skin->weld || skin->nverts <= 0)
		return skin->weld;
	int *weld = malloc((size_t)skin->nverts * sizeof(int));
	int size = 1;
	while (size < skin->nverts * 2)
		size <<= 1;
	int *table = malloc((size_t)size * sizeof(int));
	if (!weld || !table) {
		free(weld);
		free(table);
		return NULL;
	}
	for (int i = 0; i < size; i++)
		table[i] = -1;
	for (int v = 0; v < skin->nverts; v++) {
		const struct beanSkinVert *s = &skin->verts[v];
		uint32_t h = 2166136261u;
		const uint8_t *bytes[3] = { (const uint8_t *)s->p, s->weight, (const uint8_t *)s->matrix };
		const size_t lens[3] = { sizeof(s->p), sizeof(s->weight), sizeof(s->matrix) };
		for (int b = 0; b < 3; b++)
			for (size_t i = 0; i < lens[b]; i++)
				h = (h ^ bytes[b][i]) * 16777619u;
		int slot = (int)(h & (uint32_t)(size - 1));
		weld[v] = v;
		for (;; slot = (slot + 1) & (size - 1)) {
			const int other = table[slot];
			if (other < 0) {
				table[slot] = v;
				break;
			}
			const struct beanSkinVert *o = &skin->verts[other];
			if (!memcmp(o->p, s->p, sizeof(s->p)) && !memcmp(o->weight, s->weight, sizeof(s->weight)) &&
			    !memcmp(o->matrix, s->matrix, sizeof(s->matrix))) {
				weld[v] = other;
				break;
			}
		}
	}
	free(table);
	skin->weld = weld;
	return weld;
}

#define BEAN_CAP_LOOPS 16
struct beanCapLoop { int piece, other, first, nedges; };
struct beanCapCache {
	const struct beanSkin *skin;
	int cuts, nloops;
	struct beanCapLoop loops[BEAN_CAP_LOOPS];
	int (*edges)[2]; /* skin vertex indices on the loop's own side */
};
static struct beanCapCache s_capCache[32];
static int s_capNext;

struct beanCapEdge { int a, b, va, vb, piece, other; };
static int beanCapEdgeCmp(const void *x, const void *y)
{
	const struct beanCapEdge *a = x, *b = y;
	return a->a != b->a ? (a->a < b->a ? -1 : 1) : a->b != b->b ? (a->b < b->b ? -1 : 1) : 0;
}

static const struct beanCapCache *beanCapLoops(struct beanSkin *skin, int cuts, const int *tripiece, const int *triother)
{
	for (int i = 0; i < (int)(sizeof(s_capCache) / sizeof(*s_capCache)); i++)
		if (s_capCache[i].skin == skin && s_capCache[i].cuts == cuts)
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
	struct beanCapCache *cache = &s_capCache[s_capNext];
	s_capNext = (s_capNext + 1) % (int)(sizeof(s_capCache) / sizeof(*s_capCache));
	free(cache->edges);
	memset(cache, 0, sizeof(*cache));
	cache->skin = skin;
	cache->cuts = cuts;
	cache->edges = malloc((size_t)(nborder > 0 ? nborder : 1) * sizeof(*cache->edges));
	int stored = 0;
	/* One loop per pair of pieces: one side of one cut */
	for (int i = 0; cache->edges && i < nborder && cache->nloops < BEAN_CAP_LOOPS; i++) {
		int seen = 0;
		for (int j = 0; j < i && !seen; j++)
			seen = border[j].piece == border[i].piece && border[j].other == border[i].other;
		if (seen)
			continue;
		struct beanCapLoop *loop = &cache->loops[cache->nloops];
		loop->piece = border[i].piece;
		loop->other = border[i].other;
		loop->first = stored;
		for (int j = i; j < nborder; j++)
			if (border[j].piece == loop->piece && border[j].other == loop->other) {
				cache->edges[stored][0] = border[j].va;
				cache->edges[stored++][1] = border[j].vb;
			}
		loop->nedges = stored - loop->first;
		if (loop->nedges >= 3)
			cache->nloops++;
		else
			stored = loop->first; /* a stray edge, not a hole */
	}
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
