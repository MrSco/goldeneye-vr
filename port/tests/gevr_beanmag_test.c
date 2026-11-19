/* port/tests/test_bean_magazine.py: gevr_beanmag.c on a made-up grip - a
 * leaning square tube with a foot across its bottom, a hand beside it and a
 * part outside the grip's box. No release files are read here. */
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "gevr_beanmag.h"

#define MAXTRIS 256
static struct gevrBeanTri s_tris[MAXTRIS];
static unsigned char s_eligible[MAXTRIS];
static int s_ntris;

static void tri(const float a[3], const float b[3], const float c[3], int tex, int eligible)
{
	struct gevrBeanTri *t = &s_tris[s_ntris];
	const float *p[3] = { a, b, c };
	memset(t, 0, sizeof(*t));
	for (int v = 0; v < 3; v++) {
		for (int k = 0; k < 3; k++)
			t->p[v * 3 + k] = p[v][k];
		t->n[v * 3 + 1] = 1.0f;
		t->uv[v * 2] = p[v][0];
		t->uv[v * 2 + 1] = p[v][1];
		t->rgba[v * 4] = t->rgba[v * 4 + 1] = t->rgba[v * 4 + 2] = t->rgba[v * 4 + 3] = 255;
		t->bone[v * 4] = 3;
		t->weight[v * 4] = 255;
	}
	t->tex = (int16_t)tex;
	t->alpha = 255;
	s_eligible[s_ntris++] = (unsigned char)eligible;
}

static void quad(const float a[3], const float b[3], const float c[3], const float d[3], int tex, int eligible)
{
	tri(a, b, c, tex, eligible);
	tri(a, c, d, tex, eligible);
}

static float area(const struct gevrBeanTri *t)
{
	float ab[3], ac[3], x[3];
	for (int k = 0; k < 3; k++) {
		ab[k] = t->p[3 + k] - t->p[k];
		ac[k] = t->p[6 + k] - t->p[k];
	}
	x[0] = ab[1] * ac[2] - ab[2] * ac[1];
	x[1] = ab[2] * ac[0] - ab[0] * ac[2];
	x[2] = ab[0] * ac[1] - ab[1] * ac[0];
	return 0.5f * sqrtf(x[0] * x[0] + x[1] * x[1] + x[2] * x[2]);
}

static float side(const float plane[4], const float *p)
{
	return plane[0] * p[0] + plane[1] * p[1] + plane[2] * p[2] + plane[3];
}

int main(void)
{
	/* the tube: 40 wide (x), 80 deep (z), from y -100 to y 300, leaning 0.25 in z a unit up */
	const float lean = 0.25f;
	const float plane[4] = { 0.0f, 1.0f, 0.0f, 0.0f };   /* the foot is everything below y 0 */
	GevrBeanMagDef def;
	GevrBeanMagBuilt built;
	float whole = 0, kept = 0, foot = 0, made = 0, corner[2][4][3];
	int hand, outside, floor = 0, body = 0, passed = 0;

	assert(gevrBeanMagFor("gun/ppk") && gevrBeanMagFor("gun/goldppk") && !gevrBeanMagFor("gun/ak47") && !gevrBeanMagFor(NULL));
	assert(gevrBeanMagAt(0) == gevrBeanMagFor("gun/ppk") && !gevrBeanMagAt(-1) && !gevrBeanMagAt(1000));

	for (int level = 0; level < 2; level++) {
		const float y = level ? 300.0f : -100.0f;
		const float xs[4] = { -20, 20, 20, -20 }, zs[4] = { -40, -40, 40, 40 };
		for (int c = 0; c < 4; c++) {
			corner[level][c][0] = xs[c];
			corner[level][c][1] = y;
			corner[level][c][2] = zs[c] + lean * y;
		}
	}
	/* four walls that run straight through the plane, and the foot's bottom */
	for (int c = 0; c < 4; c++)
		quad(corner[0][c], corner[0][(c + 1) % 4], corner[1][(c + 1) % 4], corner[1][c], 5, 1);
	quad(corner[0][0], corner[0][1], corner[0][2], corner[0][3], 5, 1);
	/* the hand's, across the plane inside the grip's box: not the gun's to cut */
	hand = s_ntris;
	{
		const float a[3] = { 30, -50, 0 }, b[3] = { 30, 50, 0 }, c[3] = { 30, 0, 30 };
		tri(a, b, c, 4, 0);
	}
	/* the gun's, below the plane but outside the grip's box: a trigger guard, say */
	outside = s_ntris;
	{
		const float a[3] = { 0, -50, 400 }, b[3] = { 10, -50, 400 }, c[3] = { 0, -40, 400 };
		tri(a, b, c, 5, 1);
	}

	memset(&def, 0, sizeof(def));
	def.source = "test";
	memcpy(def.plane, plane, sizeof(plane));
	def.lo[0] = -60; def.lo[1] = -150; def.lo[2] = -150;
	def.hi[0] = 60; def.hi[1] = 350; def.hi[2] = 200;
	def.floorDepth = 40.0f;
	def.bodyLength = 200.0f;
	def.bodyDraw[0] = 0.5f;
	def.bodyDraw[1] = 0.75f;
	def.axisSpan = 100.0f;
	def.bodyRgb[0] = 40; def.bodyRgb[1] = 41; def.bodyRgb[2] = 42;
	def.floorRgb[0] = 10; def.floorRgb[1] = 11; def.floorRgb[2] = 12;

	assert(gevrBeanMagBuild(&def, s_tris, s_ntris, s_eligible, &built));

	/* nothing is lost or made of the gun's own: the walls' halves add up */
	for (int i = 0; i < s_ntris; i++)
		whole += area(&s_tris[i]);
	for (int i = 0; i < built.nopen; i++) {
		const int src = built.openSrc[i];
		if (src < 0) {
			/* the floor: plain, dark, 40 up the well, facing out of the mouth */
			floor++;
			assert(built.open[i].tex == -1 && built.open[i].rgba[0] == 10 && built.open[i].rgba[1] == 11);
			for (int v = 0; v < 3; v++) {
				assert(fabsf(side(plane, &built.open[i].p[v * 3]) - 40.0f) < 1e-2f);
				assert(built.open[i].n[v * 3 + 1] == -1.0f);
			}
			continue;
		}
		kept += area(&built.open[i]);
		if (src == hand || src == outside) {
			assert(!memcmp(&built.open[i], &s_tris[src], sizeof(s_tris[src])));
			passed++;
			continue;
		}
		for (int v = 0; v < 3; v++)
			assert(side(plane, &built.open[i].p[v * 3]) >= -1e-3f);
		assert(built.open[i].tex == 5 && built.open[i].bone[0] == 3 && built.open[i].weight[0] == 255);
	}
	assert(passed == 2 && floor >= 3);
	assert(built.nfoot > 0 && built.nmag > built.nfoot);
	for (int i = 0; i < built.nmag; i++) {
		const struct gevrBeanTri *t = &built.mag[i];
		if (i < built.nfoot) {
			foot += area(t);
			for (int v = 0; v < 3; v++) {
				assert(side(plane, &t->p[v * 3]) <= 1e-3f);
				/* a cut corner keeps the wall's own picture: uv was laid along x and y */
				assert(fabsf(t->uv[v * 2] - t->p[v * 3]) < 1e-2f && fabsf(t->uv[v * 2 + 1] - t->p[v * 3 + 1]) < 1e-2f);
			}
			assert(t->tex == 5);
		} else {
			body++;
			made += area(t);
			assert(t->tex == -1 && t->rgba[0] == 40 && t->rgba[2] == 42 && t->rgba[3] == 255);
			for (int v = 0; v < 3; v++) {
				const float up = side(plane, &t->p[v * 3]);
				/* inside the grip all the way up: half its width, three quarters its depth */
				assert(up >= -1e-3f && up <= 200.0f + 1e-2f);
				assert(fabsf(t->p[v * 3]) <= 10.0f + 1e-2f);
				/* the outline is read half a unit above the plane, so the lean moves it an eighth */
				assert(fabsf(t->p[v * 3 + 2] - lean * up) <= 30.0f + 0.2f);
			}
		}
	}
	assert(fabsf(kept + foot - whole) < 1e-2f * whole);
	assert(body == 4 * 3 && made > 0);   /* four sides of two and a top's four */

	/* the well runs up the tube's lean, not straight off the plane */
	{
		const float len = sqrtf(1.0f + lean * lean);
		assert(fabsf(built.axis[0]) < 1e-3f && fabsf(built.axis[1] - 1.0f / len) < 1e-3f
		    && fabsf(built.axis[2] - lean / len) < 1e-3f);
		assert(fabsf(built.mouth[0]) < 1e-2f && fabsf(built.mouth[1]) < 1e-2f && fabsf(built.mouth[2]) < 0.2f);
		assert(fabsf(side(plane, built.top) - 200.0f) < 1e-1f && fabsf(built.top[2] - lean * built.top[1]) < 0.2f);
		assert(built.foot[1] < 0 && built.foot[1] > -100.0f);
	}
	gevrBeanMagFree(&built);
	assert(!built.open && !built.mag && !built.nopen);

	/* nothing below the plane: no magazine, and nothing left allocated */
	def.plane[3] = 500.0f;
	assert(!gevrBeanMagBuild(&def, s_tris, s_ntris, s_eligible, &built) && !built.open && !built.mag);
	/* the hand alone is nobody's magazine */
	def.plane[3] = 0.0f;
	memset(s_eligible, 0, sizeof(s_eligible));
	assert(!gevrBeanMagBuild(&def, s_tris, s_ntris, s_eligible, &built));
	assert(!gevrBeanMagBuild(NULL, s_tris, s_ntris, NULL, &built) && !gevrBeanMagBuild(&def, NULL, 0, NULL, &built));

	puts("PASS: Bean magazine cut - foot split along the plane with nothing lost, floored well, body inside the grip up its lean, the hand and far parts untouched");
	return 0;
}
