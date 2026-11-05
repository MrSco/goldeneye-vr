/* gevr_bean_holes.inc.h on made-up levels, and on the release's Statue when
 * its file is given (argv[1]): the faces and strips it closes. */
#include "../src/gevr_bean_load.c"
#include "../src/gevr_bean_holes.inc.h"

void videoFreeCachedTexture(const void *addr) { (void)addr; }

#define CHECK(c)                                                         \
	do {                                                                 \
		if (!(c)) {                                                      \
			fprintf(stderr, "holes: %s failed at line %d\n", #c, __LINE__); \
			exit(1);                                                     \
		}                                                                \
	} while (0)

static struct gevrBeanTri *add(struct gevrBeanMesh *m, int tex, const float p[9], const float uv[6], uint8_t a0, uint8_t a1, uint8_t a2)
{
	struct gevrBeanTri *t = &m->tris[m->ntris++];
	memset(t, 0, sizeof(*t));
	memcpy(t->p, p, sizeof(t->p));
	memcpy(t->uv, uv, sizeof(t->uv));
	for (int k = 0; k < 3; k++) {
		t->n[k * 3 + 1] = 1.0f;
		t->rgba[k * 4] = t->rgba[k * 4 + 1] = t->rgba[k * 4 + 2] = 200;
	}
	t->rgba[3] = a0;
	t->rgba[7] = a1;
	t->rgba[11] = a2;
	t->tex = (int16_t)tex;
	t->alpha = a0 < 255 || a1 < 255 || a2 < 255 ? 254 : 255;
	return t;
}

static void quad(struct gevrBeanMesh *m, int tex, float x0, float z0, float x1, float z1, float y)
{
	/* winding up (+y), the UVs the positions over 100 */
	const float a[9] = { x0, y, z0, x0, y, z1, x1, y, z1 }, b[9] = { x0, y, z0, x1, y, z1, x1, y, z0 };
	const float ua[6] = { x0 / 100, z0 / 100, x0 / 100, z1 / 100, x1 / 100, z1 / 100 };
	const float ub[6] = { x0 / 100, z0 / 100, x1 / 100, z1 / 100, x1 / 100, z0 / 100 };
	add(m, tex, a, ua, 255, 255, 255);
	add(m, tex, b, ub, 255, 255, 255);
}

static void wall(struct gevrBeanMesh *m, int tex, float x, float z0, float z1, float y0, float y1)
{
	const float a[9] = { x, y0, z0, x, y0, z1, x, y1, z1 }, b[9] = { x, y0, z0, x, y1, z1, x, y1, z0 };
	const float uv[6] = { 0, 0, 1, 0, 1, 1 };
	struct gevrBeanTri *t = add(m, tex, a, uv, 255, 255, 255);
	t->n[0] = t->n[3] = t->n[6] = -1.0f;
	t = add(m, tex, b, uv, 255, 255, 255);
	t->n[0] = t->n[3] = t->n[6] = -1.0f;
}

static struct gevrBeanMesh *level(void)
{
	struct gevrBeanMesh *m = calloc(1, sizeof(*m));
	m->tris = calloc(64, sizeof(*m->tris));
	m->ntex = 2;
	return m;
}

int main(int argc, char **argv)
{
	int faded, strips;

	/* Ground of picture 0 to x 1000, a fence at x 1007.6 (cm a unit = 1):
	 * one strip, the ground carried on to the fence's foot and under it */
	{
		struct gevrBeanMesh *m = level();
		quad(m, 0, 0, 0, 1000, 1000, 0);
		wall(m, 1, 1007.6f, 0, 1000, 0, 250);
		const int before = m->ntris;
		beanCloseLevelHoles(m, 1.0f, &faded, &strips);
		CHECK(faded == 0 && strips == 1 && m->ntris == before + 2);
		for (int i = before; i < m->ntris; i++) {
			const struct gevrBeanTri *t = &m->tris[i];
			float n[3];
			CHECK(t->tex == 0 && t->alpha == 255 && !(t->flags & GEVR_BEAN_F_DECAL));
			CHECK(holeNormal(t->p, n) > 0.0f && n[1] > 0.99f);   /* the floor's winding */
			for (int c = 0; c < 3; c++) {
				CHECK(t->p[c * 3] >= 999.99f && t->p[c * 3] <= 1008.11f && fabsf(t->p[c * 3 + 1]) < 1e-3f);
				CHECK(fabsf(t->uv[c * 2] - t->p[c * 3] / 100) < 1e-3f);   /* its picture carried on */
				CHECK(t->rgba[c * 4] == 200 && t->rgba[c * 4 + 3] == 255);
			}
		}
		gevrBeanMeshFree(m);
	}

	/* Not closed: a gap past 30 cm, a wall standing 10 cm up, a gutter with a
	 * floor 40 cm down, a strip something already covers */
	{
		struct gevrBeanMesh *m = level();
		quad(m, 0, 0, 0, 1000, 1000, 0);
		wall(m, 1, 1040, 0, 1000, 0, 250);
		beanCloseLevelHoles(m, 1.0f, &faded, &strips);
		CHECK(strips == 0);
		gevrBeanMeshFree(m);

		m = level();
		quad(m, 0, 0, 0, 1000, 1000, 0);
		wall(m, 1, 1010, 0, 1000, 10, 250);
		beanCloseLevelHoles(m, 1.0f, &faded, &strips);
		CHECK(strips == 0);
		gevrBeanMeshFree(m);

		m = level();
		quad(m, 0, 0, 0, 1000, 1000, 0);
		quad(m, 0, 1000, 0, 1010, 1000, -40);
		wall(m, 1, 1010, 0, 1000, 0, 250);
		beanCloseLevelHoles(m, 1.0f, &faded, &strips);
		CHECK(strips == 0);
		gevrBeanMeshFree(m);

		m = level();
		quad(m, 0, 0, 0, 1000, 1000, 0);
		quad(m, 0, 995, 0, 1010, 1000, 1);   /* a kerb's top, not sharing the edge */
		wall(m, 1, 1010, 0, 1000, 0, 250);
		beanCloseLevelHoles(m, 1.0f, &faded, &strips);
		CHECK(strips == 0);
		gevrBeanMeshFree(m);
	}

	/* A fading face of picture 0 in a gap of picture 0's ground: solid. The
	 * same face standing alone (a light shaft), or of another picture: not. */
	{
		const float gap[9] = { 100, 0, 100, 100, 0, 200, 200, 0, 200 };
		const float uv[6] = { 1, 1, 1, 2, 2, 2 };
		for (int kind = 0; kind < 3; kind++) {
			struct gevrBeanMesh *m = level();
			/* the gap's neighbours across its edges 0-1 and 1-2 */
			const float left[9] = { 0, 0, 100, 100, 0, 200, 100, 0, 100 };
			const float top[9] = { 100, 0, 200, 200, 0, 300, 200, 0, 200 };
			if (kind != 1) {
				add(m, 0, left, uv, 255, 255, 255);
				add(m, 0, top, uv, 255, 255, 255);
			}
			struct gevrBeanTri *t = add(m, kind == 2 ? 1 : 0, gap, uv, 255, 0, 255);
			const int index = (int)(t - m->tris);
			beanCloseLevelHoles(m, 1.0f, &faded, &strips);
			t = &m->tris[index];
			if (kind == 0) {
				CHECK(faded == 1 && t->alpha == 255 && t->rgba[7] == 255);
			} else {
				CHECK(faded == 0 && t->alpha == 254 && t->rgba[7] == 0);
			}
			gevrBeanMeshFree(m);
		}
	}

	/* The release's Statue: six faded faces, the 11 m strip along the fence */
	if (argc > 1) {
		struct gevrBeanMesh *m = NULL;
		CHECK(gevrBeanLoad(argv[1], 1, &m) && m);
		const int before = m->ntris;
		beanCloseLevelHoles(m, 0.007471857f, &faded, &strips);
		printf("statuepark: faded %d strips %d\n", faded, strips);
		CHECK(faded == 6 && strips >= 1 && m->ntris == before + strips * 2);
		int fence = 0;
		for (int i = before; i < m->ntris; i++) {
			const float *p = m->tris[i].p;
			fence |= m->tris[i].tex == 5 && p[0] < -860000 && p[0] > -990000 && p[2] < -30000 && p[2] > -140000;
		}
		CHECK(fence);
		gevrBeanMeshFree(m);
	}
	printf("bean holes: ok\n");
	return 0;
}
