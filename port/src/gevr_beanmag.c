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
/* the four PP7s are one grip: the silenced, silver and gold files cut alike */
static const GevrBeanMagDef s_magDefs[] = {
	PP7_MAGAZINE("gun/ppk"),
	PP7_MAGAZINE("gun/ppksilenced"),
	PP7_MAGAZINE("gun/silverppk"),
	PP7_MAGAZINE("gun/goldppk"),
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
	for (int k = 0; k < 3; k++)
		built->top[k] = built->mouth[k] + built->axis[k] * reach;

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
