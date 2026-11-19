/* Bean guns' removable magazines: see gevr_beanmag.h. No game headers: the
 * tools and the native tests build this file with the release's loader alone. */
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "gevr_beanmag.h"

/*
 * The PP7 (new/gun/ppk), measured from the release's mesh: the floor plate's
 * seam runs from (y -523.7, z -354.2) at the back strap to (y -552.1, z -161.5)
 * at the front, the same on both sides, and the plate with its finger rest
 * hangs 46 below it. The well's floor is 1.3 cm up the grip at the gun's 4.7
 * times; the body reaches the frame.
 */
#define PP7_MAGAZINE(file) { \
	.source = file, \
	.plane = { 0.0f, 0.98931f, 0.14582f, 569.74f }, \
	.lo = { -90.0f, -640.0f, -420.0f }, \
	.hi = { 90.0f, -150.0f, -40.0f }, \
	.floorDepth = 60.0f, \
	.bodyLength = 370.0f, \
	.bodyDraw = { 0.62f, 0.72f }, \
	.axisSpan = 200.0f, \
	.bodyRgb = { 46, 46, 50 }, \
	.floorRgb = { 10, 10, 12 }, \
}
/* A magazine that is a piece of the mesh on its own, below the gun: the box
 * round it and how much of its top is inside the gun. */
#define ISLAND_MAGAZINE(file, x, ylow, yhigh, zback, zfront, into) { \
	.source = file, \
	.plane = { 0.0f, 1.0f, 0.0f, 0.0f }, \
	.lo = { -(x), ylow, zback }, \
	.hi = { x, yhigh, zfront }, \
	.floorDepth = 4.0f, \
	.bodyRgb = { 46, 46, 50 }, \
	.floorRgb = { 10, 10, 12 }, \
	.island = 1, \
	.inside = into, \
}
/* the four PP7s are one grip: the silenced, silver and gold files cut alike */
static const GevrBeanMagDef s_magDefs[] = {
	PP7_MAGAZINE("gun/ppk"),
	PP7_MAGAZINE("gun/ppksilenced"),
	PP7_MAGAZINE("gun/silverppk"),
	PP7_MAGAZINE("gun/goldppk"),
	/* The DD44 (new/gun/tt33), a pistol as the PP7: its floor plate's seam runs
	 * from (y -599.2, z -1244.1) at the back strap to (y -628.4, z -1027) at
	 * the front, the plate hanging 46 below it. */
	{
		.source = "gun/tt33",
		.plane = { 0.0f, 0.99107f, 0.13330f, 759.69f },
		.lo = { -100.0f, -720.0f, -1300.0f },
		.hi = { 100.0f, -150.0f, -900.0f },
		.floorDepth = 60.0f,
		.bodyLength = 380.0f,
		.bodyDraw = { 0.62f, 0.72f },
		.axisSpan = 200.0f,
		.bodyRgb = { 46, 46, 50 },
		.floorRgb = { 10, 10, 12 },
	},
	/* the rifles and submachine guns: each magazine a piece of its own. The
	 * boxes are measured from the release's meshes; the sniper rifle's mesh has
	 * no magazine to take. */
	ISLAND_MAGAZINE("gun/skorpion", 60.0f, -620.0f, -150.0f, 100.0f, 460.0f, 20.0f),
	ISLAND_MAGAZINE("gun/ak47", 80.0f, -1200.0f, -60.0f, 520.0f, 1300.0f, 20.0f),
	ISLAND_MAGAZINE("gun/uzi", 60.0f, -1400.0f, -600.0f, -140.0f, 120.0f, 0.0f),
	ISLAND_MAGAZINE("gun/mp5k", 130.0f, -1160.0f, -220.0f, 800.0f, 1360.0f, 40.0f),
	ISLAND_MAGAZINE("gun/mp5ksilenced", 130.0f, -1160.0f, -220.0f, 800.0f, 1360.0f, 40.0f),
	ISLAND_MAGAZINE("gun/spectre", 90.0f, -1100.0f, -470.0f, -1020.0f, -630.0f, 30.0f),
	ISLAND_MAGAZINE("gun/m16", 90.0f, -1000.0f, -180.0f, 380.0f, 970.0f, 120.0f),
	/* The RC-P90's lies along the top of the gun, see-through, its rounds two
	 * more pieces inside it: into the gun is down, and it lifts off. */
	{
		.source = "gun/fnp90",
		.plane = { 0.0f, -1.0f, 0.0f, 0.0f },
		.lo = { -130.0f, 100.0f, 700.0f },
		.hi = { 130.0f, 320.0f, 2350.0f },
		.floorDepth = 2.0f,
		.bodyRgb = { 46, 46, 50 },
		.floorRgb = { 10, 10, 12 },
		.island = 1,
		.straight = 1,
		.pullUp = 1,
	},
};

const GevrBeanMagDef *gevrBeanMagAt(int n)
{
	return n >= 0 && n < (int)(sizeof(s_magDefs) / sizeof(s_magDefs[0])) ? &s_magDefs[n] : NULL;
}

const GevrBeanMagDef *gevrBeanMagFor(const char *source)
{
	for (int i = 0; source && i < (int)(sizeof(s_magDefs) / sizeof(s_magDefs[0])); i++)
		if (!strcmp(source, s_magDefs[i].source))
			return &s_magDefs[i];
	return NULL;
}

#define MAG_EPS 0.25f

struct magCorner {
	float p[3], n[3], uv[2], rgba[4];
	int from; /* the triangle corner its bones are */
};

struct magList {
	struct gevrBeanTri *tris;
	int *src;
	int n, cap, wantSrc;
};

static int magListPush(struct magList *l, const struct gevrBeanTri *t, int src)
{
	if (l->n == l->cap) {
		int cap = l->cap ? l->cap * 2 : 256;
		struct gevrBeanTri *tris = realloc(l->tris, (size_t)cap * sizeof(*tris));
		if (!tris)
			return 0;
		l->tris = tris;
		if (l->wantSrc) {
			int *s = realloc(l->src, (size_t)cap * sizeof(*s));
			if (!s)
				return 0;
			l->src = s;
		}
		l->cap = cap;
	}
	l->tris[l->n] = *t;
	if (l->wantSrc)
		l->src[l->n] = src;
	l->n++;
	return 1;
}

static float magPlaneSide(const float plane[4], const float *p)
{
	return plane[0] * p[0] + plane[1] * p[1] + plane[2] * p[2] + plane[3];
}

static void magCornerOf(const struct gevrBeanTri *t, int c, struct magCorner *out)
{
	for (int k = 0; k < 3; k++) {
		out->p[k] = t->p[c * 3 + k];
		out->n[k] = t->n[c * 3 + k];
	}
	out->uv[0] = t->uv[c * 2];
	out->uv[1] = t->uv[c * 2 + 1];
	for (int k = 0; k < 4; k++)
		out->rgba[k] = t->rgba[c * 4 + k];
	out->from = c;
}

/* the point of a-b on the plane; its bones are the end's on the side kept */
static void magCornerCross(const struct magCorner *a, float sa, const struct magCorner *b, float sb, int keepA, struct magCorner *out)
{
	const float f = sa / (sa - sb);
	for (int k = 0; k < 3; k++) {
		out->p[k] = a->p[k] + (b->p[k] - a->p[k]) * f;
		out->n[k] = a->n[k] + (b->n[k] - a->n[k]) * f;
	}
	for (int k = 0; k < 2; k++)
		out->uv[k] = a->uv[k] + (b->uv[k] - a->uv[k]) * f;
	for (int k = 0; k < 4; k++)
		out->rgba[k] = a->rgba[k] + (b->rgba[k] - a->rgba[k]) * f;
	out->from = keepA ? a->from : b->from;
}

static void magTriFrom(const struct gevrBeanTri *like, const struct magCorner *c[3], struct gevrBeanTri *out)
{
	*out = *like;
	for (int v = 0; v < 3; v++) {
		for (int k = 0; k < 3; k++) {
			out->p[v * 3 + k] = c[v]->p[k];
			out->n[v * 3 + k] = c[v]->n[k];
		}
		out->uv[v * 2] = c[v]->uv[0];
		out->uv[v * 2 + 1] = c[v]->uv[1];
		for (int k = 0; k < 4; k++) {
			float colour = c[v]->rgba[k];
			out->rgba[v * 4 + k] = (uint8_t)(colour < 0 ? 0 : colour > 255 ? 255 : colour + 0.5f);
			out->bone[v * 4 + k] = like->bone[c[v]->from * 4 + k];
			out->weight[v * 4 + k] = like->weight[c[v]->from * 4 + k];
		}
	}
}

/* One triangle across the plane: the part at or above it to up, the rest to
 * down, each a fan in the triangle's own winding. */
static int magTriSplit(const struct gevrBeanTri *t, const float plane[4], int src, struct magList *up, struct magList *down)
{
	struct magCorner in[3], poly[2][5];
	int count[2] = { 0, 0 };
	float s[3];
	for (int c = 0; c < 3; c++) {
		magCornerOf(t, c, &in[c]);
		s[c] = magPlaneSide(plane, in[c].p);
	}
	for (int c = 0; c < 3; c++) {
		const int d = (c + 1) % 3;
		if (s[c] >= 0)
			poly[0][count[0]++] = in[c];
		if (s[c] <= 0)
			poly[1][count[1]++] = in[c];
		if ((s[c] > 0 && s[d] < 0) || (s[c] < 0 && s[d] > 0)) {
			magCornerCross(&in[c], s[c], &in[d], s[d], s[c] > 0, &poly[0][count[0]++]);
			magCornerCross(&in[c], s[c], &in[d], s[d], s[c] < 0, &poly[1][count[1]++]);
		}
	}
	for (int side = 0; side < 2; side++)
		for (int v = 1; v + 1 < count[side]; v++) {
			const struct magCorner *c[3] = { &poly[side][0], &poly[side][v], &poly[side][v + 1] };
			struct gevrBeanTri piece;
			magTriFrom(t, c, &piece);
			if (!magListPush(side ? down : up, &piece, src))
				return 0;
		}
	return 1;
}

static int magTriInBox(const struct gevrBeanTri *t, const float lo[3], const float hi[3])
{
	for (int c = 0; c < 9; c++)
		if (t->p[c] < lo[c % 3] || t->p[c] > hi[c % 3])
			return 0;
	return 1;
}

struct magFlat { float u, v; };

static int magFlatCmp(const void *a, const void *b)
{
	const struct magFlat *p = a, *q = b;
	return p->u < q->u ? -1 : p->u > q->u ? 1 : p->v < q->v ? -1 : p->v > q->v ? 1 : 0;
}

static float magFlatTurn(const struct magFlat *o, const struct magFlat *a, const struct magFlat *b)
{
	return (a->u - o->u) * (b->v - o->v) - (a->v - o->v) * (b->u - o->u);
}

/* Andrew's monotone chain; hull has room for 2n. Anticlockwise in (u, v). */
static int magFlatHull(struct magFlat *pts, int n, struct magFlat *hull)
{
	int k = 0;
	if (n < 3)
		return 0;
	qsort(pts, (size_t)n, sizeof(*pts), magFlatCmp);
	for (int i = 0; i < n; i++) {
		while (k >= 2 && magFlatTurn(&hull[k - 2], &hull[k - 1], &pts[i]) <= 1e-4f)
			k--;
		hull[k++] = pts[i];
	}
	for (int i = n - 2, lower = k + 1; i >= 0; i--) {
		while (k >= lower && magFlatTurn(&hull[k - 2], &hull[k - 1], &pts[i]) <= 1e-4f)
			k--;
		hull[k++] = pts[i];
	}
	return k > 3 ? k - 1 : 0;
}

struct magFrame { float n[3], u[3], v[3], plane[4]; };

static void magFrameOf(const float plane[4], struct magFrame *f)
{
	const float len = sqrtf(plane[0] * plane[0] + plane[1] * plane[1] + plane[2] * plane[2]);
	float along[3] = { 1.0f, 0.0f, 0.0f }, dot, ulen;
	for (int k = 0; k < 3; k++)
		f->n[k] = plane[k] / len;
	for (int k = 0; k < 4; k++)
		f->plane[k] = plane[k] / len;
	if (fabsf(f->n[0]) > 0.9f) {
		along[0] = 0.0f;
		along[2] = 1.0f;
	}
	dot = along[0] * f->n[0] + along[1] * f->n[1] + along[2] * f->n[2];
	for (int k = 0; k < 3; k++)
		f->u[k] = along[k] - dot * f->n[k];
	ulen = sqrtf(f->u[0] * f->u[0] + f->u[1] * f->u[1] + f->u[2] * f->u[2]);
	for (int k = 0; k < 3; k++)
		f->u[k] /= ulen;
	f->v[0] = f->n[1] * f->u[2] - f->n[2] * f->u[1];
	f->v[1] = f->n[2] * f->u[0] - f->n[0] * f->u[2];
	f->v[2] = f->n[0] * f->u[1] - f->n[1] * f->u[0];
}

static void magFrameAt(const struct magFrame *f, float height, const struct magFlat *at, float out[3])
{
	for (int k = 0; k < 3; k++)
		out[k] = (height - f->plane[3]) * f->n[k] + at->u * f->u[k] + at->v * f->v[k];
}

/* The grip's outline `height` up the well from the plane: its hull, and the
 * hull's middle. 0 when the grip does not reach there. */
static int magOutlineAt(const GevrBeanMagDef *def, const struct magFrame *f, const struct gevrBeanTri *tris, int ntris,
                     const unsigned char *eligible, float height, struct magFlat **hullOut, struct magFlat *middle)
{
	struct magFlat *pts = NULL, *hull;
	int n = 0, cap = 0, count;
	float area = 0, cu = 0, cv = 0;
	*hullOut = NULL;
	for (int i = 0; i < ntris; i++) {
		const struct gevrBeanTri *t = &tris[i];
		if ((eligible && !eligible[i]) || !magTriInBox(t, def->lo, def->hi))
			continue;
		for (int c = 0; c < 3; c++) {
			const float *a = &t->p[c * 3], *b = &t->p[((c + 1) % 3) * 3];
			const float sa = magPlaneSide(f->plane, a) - height, sb = magPlaneSide(f->plane, b) - height;
			float p[3], w;
			if ((sa > 0) == (sb > 0) || sa == sb)
				continue;
			if (n == cap) {
				struct magFlat *grown = realloc(pts, (size_t)(cap = cap ? cap * 2 : 256) * sizeof(*pts));
				if (!grown) {
					free(pts);
					return 0;
				}
				pts = grown;
			}
			w = sa / (sa - sb);
			for (int k = 0; k < 3; k++)
				p[k] = a[k] + (b[k] - a[k]) * w;
			pts[n].u = p[0] * f->u[0] + p[1] * f->u[1] + p[2] * f->u[2];
			pts[n].v = p[0] * f->v[0] + p[1] * f->v[1] + p[2] * f->v[2];
			n++;
		}
	}
	hull = n >= 3 ? malloc((size_t)n * 2 * sizeof(*hull)) : NULL;
	count = hull ? magFlatHull(pts, n, hull) : 0;
	free(pts);
	if (!count) {
		free(hull);
		return 0;
	}
	for (int i = 0; i < count; i++) {
		const struct magFlat *a = &hull[i], *b = &hull[(i + 1) % count];
		const float cross = a->u * b->v - b->u * a->v;
		area += cross;
		cu += (a->u + b->u) * cross;
		cv += (a->v + b->v) * cross;
	}
	if (fabsf(area) < 1e-3f) {
		free(hull);
		return 0;
	}
	middle->u = cu / (3.0f * area);
	middle->v = cv / (3.0f * area);
	*hullOut = hull;
	return count;
}

/* a plain triangle in the mesh's winding (a x b along normal), one colour */
static int magPlainPush(struct magList *l, const struct gevrBeanTri *like, const float a[3], const float b[3],
                     const float c[3], const float normal[3], const unsigned char rgb[3], int src)
{
	struct gevrBeanTri t = *like;
	const float *p[3] = { a, b, c };
	float ab[3], ac[3], cross[3];
	for (int k = 0; k < 3; k++) {
		ab[k] = b[k] - a[k];
		ac[k] = c[k] - a[k];
	}
	cross[0] = ab[1] * ac[2] - ab[2] * ac[1];
	cross[1] = ab[2] * ac[0] - ab[0] * ac[2];
	cross[2] = ab[0] * ac[1] - ab[1] * ac[0];
	if (cross[0] * normal[0] + cross[1] * normal[1] + cross[2] * normal[2] < 0) {
		p[1] = c;
		p[2] = b;
	}
	for (int v = 0; v < 3; v++) {
		for (int k = 0; k < 3; k++) {
			t.p[v * 3 + k] = p[v][k];
			t.n[v * 3 + k] = normal[k];
		}
		t.uv[v * 2] = t.uv[v * 2 + 1] = 0.0f;
		t.rgba[v * 4] = rgb[0];
		t.rgba[v * 4 + 1] = rgb[1];
		t.rgba[v * 4 + 2] = rgb[2];
		t.rgba[v * 4 + 3] = 255;
		for (int k = 0; k < 4; k++) {
			t.bone[v * 4 + k] = like->bone[k];
			t.weight[v * 4 + k] = like->weight[k];
		}
	}
	t.tex = -1;
	t.alpha = 255;
	t.alphatest = 0;
	t.flags = 0;
	t.reflection = NULL;
	t.reflectionDiffuse = t.reflectionAlphaBase = 0;
	t.reflectionColour = 0;
	return magListPush(l, &t, src);
}

void gevrBeanMagFree(GevrBeanMagBuilt *built)
{
	if (!built)
		return;
	free(built->open);
	free(built->openSrc);
	free(built->mag);
	memset(built, 0, sizeof(*built));
}

struct magKey { int k[3], tri; };

static int magKeyCmp(const void *a, const void *b)
{
	const struct magKey *p = a, *q = b;
	for (int k = 0; k < 3; k++)
		if (p->k[k] != q->k[k])
			return p->k[k] < q->k[k] ? -1 : 1;
	return 0;
}

static int magRoot(int *parent, int i)
{
	while (parent[i] != i) {
		parent[i] = parent[parent[i]];
		i = parent[i];
	}
	return i;
}

/* member[i] = 1 for the gun's triangles in pieces that lie wholly inside the
 * box: a piece is the triangles joined corner to corner (half a unit apart or
 * less). The count of them; 0 when there is none or memory ran out. */
static int magIslands(const GevrBeanMagDef *def, const struct gevrBeanTri *tris, int ntris,
                      const unsigned char *eligible, unsigned char *member)
{
	struct magKey *keys = malloc((size_t)ntris * 3 * sizeof(*keys));
	int *parent = malloc((size_t)ntris * sizeof(*parent));
	unsigned char *out = calloc((size_t)ntris, 1);
	int n = 0, count = 0;
	if (!keys || !parent || !out) {
		free(keys);
		free(parent);
		free(out);
		return 0;
	}
	for (int i = 0; i < ntris; i++) {
		parent[i] = i;
		for (int c = 0; (!eligible || eligible[i]) && c < 3; c++, n++) {
			for (int k = 0; k < 3; k++)
				keys[n].k[k] = (int)lroundf(tris[i].p[c * 3 + k] * 2.0f);
			keys[n].tri = i;
		}
	}
	qsort(keys, (size_t)n, sizeof(*keys), magKeyCmp);
	for (int j = 1; j < n; j++)
		if (!magKeyCmp(&keys[j - 1], &keys[j]))
			parent[magRoot(parent, keys[j].tri)] = magRoot(parent, keys[j - 1].tri);
	for (int i = 0; i < ntris; i++)
		if ((!eligible || eligible[i]) && !magTriInBox(&tris[i], def->lo, def->hi))
			out[magRoot(parent, i)] = 1;
	for (int i = 0; i < ntris; i++) {
		member[i] = (!eligible || eligible[i]) && !out[magRoot(parent, i)];
		count += member[i];
	}
	free(keys);
	free(parent);
	free(out);
	return count;
}

/* a hull as a fan at `height`, drawn in about its middle, facing normal */
static int magHullPush(struct magList *l, const struct gevrBeanTri *like, const struct magFrame *f,
                       const struct magFlat *hull, int count, const struct magFlat *middle, float height, float draw,
                       const float normal[3], const unsigned char rgb[3])
{
	float centre[3];
	magFrameAt(f, height, middle, centre);
	for (int i = 0; i < count; i++) {
		struct magFlat ring[2];
		float a[3], b[3];
		for (int e = 0; e < 2; e++) {
			const struct magFlat *h = &hull[(i + e) % count];
			ring[e].u = middle->u + (h->u - middle->u) * draw;
			ring[e].v = middle->v + (h->v - middle->v) * draw;
		}
		magFrameAt(f, height, &ring[0], a);
		magFrameAt(f, height, &ring[1], b);
		if (!magPlainPush(l, like, centre, a, b, normal, rgb, -1))
			return 0;
	}
	return 1;
}

/* An island magazine (gevr_beanmag.h): the pieces in the box whole, a lid on
 * their top; the gun without them, floored where they went in. Heights run
 * from 0 at the magazine's top (the end inside the gun) down its length. */
static int magBuildIsland(const GevrBeanMagDef *def, const struct gevrBeanTri *tris, int ntris,
                          const unsigned char *eligible, GevrBeanMagBuilt *built)
{
	struct magList open = { NULL, NULL, 0, 0, 1 }, mag = { NULL, NULL, 0, 0, 0 };
	struct magFlat *mouthHull = NULL, *lidHull = NULL, *upHull = NULL, *downHull = NULL, mouthAt, lidAt, upAt, downAt;
	unsigned char *member = calloc((size_t)ntris, 1);
	const struct gevrBeanTri *like = NULL;
	struct magFrame f;
	float plane[4] = { def->plane[0], def->plane[1], def->plane[2], 0.0f }, top = -1e30f, bottom = 1e30f;
	float sum[3] = { 0, 0, 0 }, down[3], length, mouthHeight, along;
	int nmouth, nlid, nup = 0, ndown = 0, ok = 1, count;
	count = member ? magIslands(def, tris, ntris, eligible, member) : 0;
	if (!count) {
		free(member);
		return 0;
	}
	magFrameOf(plane, &f);
	for (int i = 0; ok && i < ntris; i++) {
		if (!member[i]) {
			ok = magListPush(&open, &tris[i], i);
			continue;
		}
		if (!like)
			like = &tris[i];
		for (int c = 0; c < 3; c++) {
			const float h = magPlaneSide(f.plane, &tris[i].p[c * 3]);
			if (h > top) top = h;
			if (h < bottom) bottom = h;
			for (int k = 0; k < 3; k++)
				sum[k] += tris[i].p[c * 3 + k];
		}
		ok = magListPush(&mag, &tris[i], i);
	}
	if (!ok)
		goto fail;
	built->nfoot = mag.n;
	for (int k = 0; k < 3; k++)
		built->foot[k] = built->hold[k] = sum[k] / (count * 3);
	f.plane[3] = -top;
	length = top - bottom;
	if (length < 1.0f)
		goto fail;
	/* the mouth: where the magazine meets the gun, never right on its top edge */
	mouthHeight = -(def->inside > 0.03f * length ? def->inside : 0.03f * length);
	/* a straight one's outline is read at its middle: the RC-P90's flat magazine
	 * touches the gun along one end only, and its outline there is a sliver */
	nmouth = magOutlineAt(def, &f, tris, ntris, member, def->straight ? -0.5f * length : mouthHeight, &mouthHull,
	                      &mouthAt);
	nlid = magOutlineAt(def, &f, tris, ntris, member, def->straight ? -0.5f * length : -0.02f * length, &lidHull,
	                    &lidAt);
	if (!nmouth)
		goto fail;
	magFrameAt(&f, mouthHeight, &mouthAt, built->mouth);
	memcpy(built->axis, f.n, sizeof(f.n));
	if (!def->straight) {
		/* into the gun as its top third runs: a curved or leaning magazine's own way */
		nup = magOutlineAt(def, &f, tris, ntris, member, -0.10f * length, &upHull, &upAt);
		ndown = magOutlineAt(def, &f, tris, ntris, member, -0.35f * length, &downHull, &downAt);
		if (nup && ndown) {
			float a[3], b[3], len = 0;
			magFrameAt(&f, -0.10f * length, &upAt, a);
			magFrameAt(&f, -0.35f * length, &downAt, b);
			for (int k = 0; k < 3; k++)
				len += (a[k] - b[k]) * (a[k] - b[k]);
			len = sqrtf(len);
			for (int k = 0; len > 1e-3f && k < 3; k++)
				built->axis[k] = (a[k] - b[k]) / len;
		}
	}
	along = built->axis[0] * f.n[0] + built->axis[1] * f.n[1] + built->axis[2] * f.n[2];
	for (int k = 0; k < 3; k++) {
		built->top[k] = built->mouth[k] + built->axis[k] * (-mouthHeight / (along > 0.2f ? along : 1.0f));
		down[k] = -f.n[k];
	}
	if (nlid)
		ok = magHullPush(&mag, like, &f, lidHull, nlid, &lidAt, -0.02f * length, 0.98f, f.n, def->bodyRgb);
	if (ok)
		ok = magHullPush(&open, like, &f, mouthHull, nmouth, &mouthAt, mouthHeight + def->floorDepth, 0.97f, down,
		                 def->floorRgb);
	free(mouthHull);
	free(lidHull);
	free(upHull);
	free(downHull);
	mouthHull = lidHull = upHull = downHull = NULL;
	if (!ok)
		goto fail;
	free(member);
	built->open = open.tris;
	built->openSrc = open.src;
	built->nopen = open.n;
	built->mag = mag.tris;
	built->nmag = mag.n;
	return 1;
fail:
	free(mouthHull);
	free(lidHull);
	free(upHull);
	free(downHull);
	free(member);
	free(open.tris);
	free(open.src);
	free(mag.tris);
	memset(built, 0, sizeof(*built));
	return 0;
}

int gevrBeanMagBuild(const GevrBeanMagDef *def, const struct gevrBeanTri *tris, int ntris,
                     const unsigned char *eligible, GevrBeanMagBuilt *built)
{
	struct magList open = { NULL, NULL, 0, 0, 1 }, mag = { NULL, NULL, 0, 0, 0 };
	struct magFlat *mouthHull = NULL, *farHull = NULL, *floorHull = NULL, mouthAt, farAt, floorAt;
	struct magFrame f;
	const struct gevrBeanTri *like = NULL;
	int nmouth, nfar, nfloor, ok = 1, corners = 0;
	float footSum[3] = { 0, 0, 0 }, reach;
	if (!built)
		return 0;
	memset(built, 0, sizeof(*built));
	if (!def || !tris || ntris <= 0)
		return 0;
	if (def->island)
		return magBuildIsland(def, tris, ntris, eligible, built);
	magFrameOf(def->plane, &f);
	for (int i = 0; ok && i < ntris; i++) {
		const struct gevrBeanTri *t = &tris[i];
		float lowest = 1e30f, highest = -1e30f;
		if ((eligible && !eligible[i]) || !magTriInBox(t, def->lo, def->hi)) {
			ok = magListPush(&open, t, i);
			continue;
		}
		for (int c = 0; c < 3; c++) {
			const float s = magPlaneSide(f.plane, &t->p[c * 3]);
			if (s < lowest) lowest = s;
			if (s > highest) highest = s;
		}
		if (lowest >= -MAG_EPS)
			ok = magListPush(&open, t, i);
		else if (highest <= MAG_EPS)
			ok = magListPush(&mag, t, i);
		else
			ok = magTriSplit(t, f.plane, i, &open, &mag);
		if (!like && lowest < -MAG_EPS)
			like = t;
	}
	if (!ok || !mag.n || !like)
		goto fail;
	built->nfoot = mag.n;
	for (int i = 0; i < mag.n; i++)
		for (int c = 0; c < 9; c++)
			footSum[c % 3] += mag.tris[i].p[c];
	corners = mag.n * 3;
	for (int k = 0; k < 3; k++)
		built->foot[k] = footSum[k] / corners;

	nmouth = magOutlineAt(def, &f, tris, ntris, eligible, 0.5f, &mouthHull, &mouthAt);
	nfar = magOutlineAt(def, &f, tris, ntris, eligible, def->axisSpan, &farHull, &farAt);
	nfloor = magOutlineAt(def, &f, tris, ntris, eligible, def->floorDepth, &floorHull, &floorAt);
	if (!nmouth)
		goto fail;
	/* up the well: through the grip's outlines' middles, else straight off the plane */
	{
		float a[3], b[3], len = 0;
		/* the outline is read just above the plane, where the grip still is; the
		 * mouth, and the body's foot, are on the plane itself */
		magFrameAt(&f, 0.5f, &mouthAt, a);
		magFrameAt(&f, 0.0f, &mouthAt, built->mouth);
		memcpy(built->axis, f.n, sizeof(f.n));
		if (nfar) {
			magFrameAt(&f, def->axisSpan, &farAt, b);
			for (int k = 0; k < 3; k++)
				len += (b[k] - a[k]) * (b[k] - a[k]);
			len = sqrtf(len);
			for (int k = 0; len > 1e-3f && k < 3; k++)
				built->axis[k] = (b[k] - a[k]) / len;
		}
	}
	reach = def->bodyLength / (built->axis[0] * f.n[0] + built->axis[1] * f.n[1] + built->axis[2] * f.n[2]);
	for (int k = 0; k < 3; k++) {
		built->top[k] = built->mouth[k] + built->axis[k] * reach;
		/* the hand closes round the body a third of the way up from its foot */
		built->hold[k] = built->mouth[k] + built->axis[k] * reach * 0.33f;
	}

	/* the body: the mouth's outline drawn in, up the well, and its top */
	for (int i = 0; ok && i < nmouth; i++) {
		struct magFlat ring[2];
		float low[2][3], high[2][3], out[3], mid[3], lowMid[3];
		for (int e = 0; e < 2; e++) {
			const struct magFlat *h = &mouthHull[(i + e) % nmouth];
			ring[e].u = mouthAt.u + (h->u - mouthAt.u) * def->bodyDraw[0];
			ring[e].v = mouthAt.v + (h->v - mouthAt.v) * def->bodyDraw[1];
			magFrameAt(&f, 0.0f, &ring[e], low[e]);
			for (int k = 0; k < 3; k++)
				high[e][k] = low[e][k] + built->axis[k] * reach;
		}
		magFrameAt(&f, 0.0f, &mouthAt, lowMid);
		/* out of the side: from the body's middle line, square to the well */
		{
			float along = 0;
			for (int k = 0; k < 3; k++) {
				mid[k] = (low[0][k] + low[1][k]) * 0.5f - lowMid[k];
				along += mid[k] * built->axis[k];
			}
			for (int k = 0; k < 3; k++)
				out[k] = mid[k] - along * built->axis[k];
			along = sqrtf(out[0] * out[0] + out[1] * out[1] + out[2] * out[2]);
			for (int k = 0; k < 3; k++)
				out[k] = along > 1e-6f ? out[k] / along : f.u[k];
		}
		ok = magPlainPush(&mag, like, low[0], low[1], high[1], out, def->bodyRgb, -1) &&
		     magPlainPush(&mag, like, low[0], high[1], high[0], out, def->bodyRgb, -1) &&
		     magPlainPush(&mag, like, built->top, high[0], high[1], built->axis, def->bodyRgb, -1);
	}
	/* the empty well's floor, facing out of the mouth */
	for (int i = 0; ok && i < nfloor; i++) {
		struct magFlat ring[2];
		float a[3], b[3], middle[3], down[3];
		for (int e = 0; e < 2; e++) {
			const struct magFlat *h = &floorHull[(i + e) % nfloor];
			ring[e].u = floorAt.u + (h->u - floorAt.u) * 0.97f;
			ring[e].v = floorAt.v + (h->v - floorAt.v) * 0.97f;
		}
		magFrameAt(&f, def->floorDepth, &ring[0], a);
		magFrameAt(&f, def->floorDepth, &ring[1], b);
		magFrameAt(&f, def->floorDepth, &floorAt, middle);
		for (int k = 0; k < 3; k++)
			down[k] = -f.n[k];
		ok = magPlainPush(&open, like, middle, a, b, down, def->floorRgb, -1);
	}
	free(mouthHull);
	free(farHull);
	free(floorHull);
	mouthHull = farHull = floorHull = NULL;
	if (!ok)
		goto fail;
	built->open = open.tris;
	built->openSrc = open.src;
	built->nopen = open.n;
	built->mag = mag.tris;
	built->nmag = mag.n;
	return 1;
fail:
	free(mouthHull);
	free(farHull);
	free(floorHull);
	free(open.tris);
	free(open.src);
	free(mag.tris);
	memset(built, 0, sizeof(*built));
	return 0;
}
