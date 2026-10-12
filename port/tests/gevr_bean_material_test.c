/*
 * Bean materials on the player's own XBLA files (test_bean_material.py):
 * - an opaque face marked as a decal has a face of its own plane under it
 *   (the wrist watch, the laser watch, the Spectre, the military truck; the
 *   old bucket test marked 521 of the watch's faces with nothing under them,
 *   and a decal draws only over depth already there, so the watch showed
 *   through),
 * - Nintendo's logo has no translucent face (its blended pass at material
 *   alpha 255 drew two thirds of it as glass),
 * - no Dam face is cleared by its material (the reservoir's alpha 0 made
 *   20,000 faces invisible; DAB never applies it to a level).
 *
 * usage: material <files folder>
 */
#include "gevr_bean_int.h"
int gevrBeanTextureScroll(const void *, float, float [2]);
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void videoFreeCachedTexture(const void *addr) { (void)addr; }

static struct gevrBeanMesh *load(const char *files, const char *source)
{
	char path[512];
	struct gevrBeanMesh *mesh = NULL;

	snprintf(path, sizeof(path), "%s/new/%s/default.bin", files, source);
	if (!gevrBeanLoad(path, 1, &mesh) || !mesh) {
		fprintf(stderr, "cannot load %s\n", path);
		exit(2);
	}
	return mesh;
}

static void sub(const float *a, const float *b, float *o) { for (int k = 0; k < 3; k++) o[k] = a[k] - b[k]; }
static float dot(const float *a, const float *b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
static void cross(const float *a, const float *b, float *o)
{
	o[0] = a[1] * b[2] - a[2] * b[1];
	o[1] = a[2] * b[0] - a[0] * b[2];
	o[2] = a[0] * b[1] - a[1] * b[0];
}

/* unit normal and area */
static float normal(const float *p, float *n)
{
	float e1[3], e2[3];
	sub(p + 3, p, e1);
	sub(p + 6, p, e2);
	cross(e1, e2, n);
	float len = sqrtf(dot(n, n));
	if (len <= 0.0f)
		return 0.0f;
	for (int k = 0; k < 3; k++)
		n[k] /= len;
	return len * 0.5f;
}

/* q (on the plane) inside triangle p, with a little slack */
static int inside(const float *p, const float *n, const float *q)
{
	for (int e = 0; e < 3; e++) {
		float edge[3], to[3], c[3];
		sub(p + ((e + 1) % 3) * 3, p + e * 3, edge);
		sub(q, p + e * 3, to);
		cross(edge, to, c);
		if (dot(c, n) < -1e-3f * dot(edge, edge))
			return 0;
	}
	return 1;
}

/* opaque decal faces with no face of their plane under them */
static int baseless(const struct gevrBeanMesh *m)
{
	int count = 0;
	for (int i = 0; i < m->ntris; i++) {
		const struct gevrBeanTri *t = &m->tris[i];
		float ni[3], c[3];
		if (!(t->flags & GEVR_BEAN_F_DECAL) || t->alpha != 255 || normal(t->p, ni) <= 0.0f)
			continue;
		for (int k = 0; k < 3; k++)
			c[k] = (t->p[k] + t->p[3 + k] + t->p[6 + k]) / 3.0f;
		int found = 0;
		for (int j = 0; j < m->ntris && !found; j++) {
			const struct gevrBeanTri *u = &m->tris[j];
			float nj[3], area = normal(u->p, nj);
			if (j == i || (u->flags & GEVR_BEAN_F_DECAL) || area <= 0.0f || fabsf(dot(ni, nj)) < 0.9f)
				continue;
			float tol = 0.01f * sqrtf(area) + 1e-3f, ok = 1;
			for (int v = 0; v < 3 && ok; v++) {
				float d[3];
				sub(t->p + v * 3, u->p, d);
				ok = fabsf(dot(d, nj)) <= tol;
			}
			if (!ok)
				continue;
			/* its middle, or a corner drawn a tenth toward the middle, over the base */
			for (int v = -1; v < 3 && !found; v++) {
				float q[3], d[3];
				for (int k = 0; k < 3; k++)
					q[k] = v < 0 ? c[k] : t->p[v * 3 + k] * 0.9f + c[k] * 0.1f;
				sub(q, u->p, d);
				float h = dot(d, nj);
				for (int k = 0; k < 3; k++)
					q[k] -= h * nj[k];
				found = inside(u->p, nj, q);
			}
		}
		count += !found;
	}
	return count;
}

int main(int argc, char **argv)
{
	static const char *const rigid[] = { "char/suitlfhand", "gun/watchlaser", "gun/spectre", "prop/miltruck" };
	int bad = 0;

	if (argc < 2) {
		return 2;
	}
	for (unsigned r = 0; r < sizeof(rigid) / sizeof(rigid[0]); r++) {
		struct gevrBeanMesh *m = load(argv[1], rigid[r]);
		int decals = 0, n = baseless(m);
		for (int i = 0; i < m->ntris; i++)
			decals += (m->tris[i].flags & GEVR_BEAN_F_DECAL) != 0;
		printf("%s: %d triangles, %d decal, %d opaque decal with nothing under it\n", rigid[r], m->ntris, decals, n);
        bad |= n != 0;
        if (r == 0) {
            int glass = 0, grey = 0;
            for (int i = 0; i < m->ntris; i++) {
                grey += m->tris[i].tex == 10;
                if (m->tris[i].tex == 11) { glass++; bad |= m->tris[i].alpha == 255; }
            }
            const uint8_t *pic = NULL; int w = 0, h = 0;
            bad |= !gevrBeanTexture(m->standin[11], &pic, &w, &h);
            if (w*h > 0) for (int i=0; i<w*h; i++) bad |= pic[i*4] || pic[i*4+1] || pic[i*4+2] || pic[i*4+3]>=128;
            bad |= grey || glass != 135;
            printf("Watch glass: %d translucent black faces, %d grey shader-input faces\n", glass, grey);
        }
		gevrBeanMeshFree(m);
	}
	{
		struct gevrBeanMesh *m = load(argv[1], "prop/nintendologo");
		int xlu = 0;
		for (int i = 0; i < m->ntris; i++)
			xlu += m->tris[i].alpha < 255;
		printf("prop/nintendologo: %d triangles, %d translucent\n", m->ntris, xlu);
		bad |= xlu != 0;
		gevrBeanMeshFree(m);
	}
	{
		struct gevrBeanMesh *m = load(argv[1], "background/dam");
		int cleared = 0;
		for (int i = 0; i < m->ntris; i++) {
			const struct gevrBeanTri *t = &m->tris[i];
			int seen = t->rgba[3] | t->rgba[7] | t->rgba[11];
			cleared += t->alpha == 0 && seen;
		}
		printf("background/dam: %d triangles, %d cleared by their material\n", m->ntris, cleared);
        bad |= cleared != 0;
        float scroll[2];
        bad |= !gevrBeanTextureScroll(m->standin[71], 1.0f, scroll);
        bad |= fabsf(scroll[0] - .5f)>1e-6f || fabsf(scroll[1]+.5f)>1e-6f;
        gevrBeanTextureScroll(m->standin[71], 2.0f, scroll);
        bad |= fabsf(scroll[0])>1e-6f || fabsf(scroll[1])>1e-6f;
        bad |= gevrBeanTextureScroll(m->standin[0], 1.0f, scroll);
        int paint=0;
        for(int i=0;i<m->ntris;i++) if(m->tris[i].tex==72) {
            paint++; bad |= !(m->tris[i].flags & GEVR_BEAN_F_DECAL) || m->tris[i].alpha==255;
        }
        bad |= paint!=4;
        printf("Dam: reservoir scrolls at 0.5 repeats/s on game time; %d vertex-alpha road-paint faces retain decals\n",paint);
		gevrBeanMeshFree(m);
	}
	puts(bad ? "FAIL" : "Bean material checks passed");
	return bad;
}
