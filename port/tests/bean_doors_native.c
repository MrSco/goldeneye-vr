/* gevr_bean_doors.inc.h (DAB closeDoorGaps/fillDoorReveals/fillDoorSlots) on a
 * made-up doorway, world units (cm a unit = 1). */
#include "../src/gevr_bean_load.c"
#include "../src/gevr_bean_holes.inc.h"
#include "../src/gevr_bean_doors.inc.h"

void videoFreeCachedTexture(const void *addr) { (void)addr; }

#define CHECK(c)                                                         \
	do {                                                                 \
		if (!(c)) {                                                      \
			fprintf(stderr, "doors: %s failed at line %d\n", #c, __LINE__); \
			exit(1);                                                     \
		}                                                                \
	} while (0)

/* an upright rectangle x0..x1, y0..y1 at z, facing +z */
static void sheet(struct gevrBeanMesh *m, float x0, float x1, float y0, float y1, float z)
{
	const float q[4][3] = { { x0, y0, z }, { x1, y0, z }, { x1, y1, z }, { x0, y1, z } };
	const int idx[2][3] = { { 0, 1, 2 }, { 0, 2, 3 } };
	for (int h = 0; h < 2; h++) {
		struct gevrBeanTri *t = &m->tris[m->ntris++];
		memset(t, 0, sizeof(*t));
		for (int c = 0; c < 3; c++) {
			memcpy(&t->p[c * 3], q[idx[h][c]], sizeof(q[0]));
			t->n[c * 3 + 2] = 1.0f;
			t->uv[c * 2] = q[idx[h][c]][0] / 100.0f;
			t->uv[c * 2 + 1] = q[idx[h][c]][1] / 100.0f;
			t->rgba[c * 4] = t->rgba[c * 4 + 1] = t->rgba[c * 4 + 2] = t->rgba[c * 4 + 3] = 255;
		}
		t->alpha = 255;
	}
}

int main(void)
{
	/* A door 100 wide (x -50..50), 210 tall, 10 thick (z -5..5), sliding 100
	 * along +x. GoldenEye's normal is up x look: look -x for +z. */
	struct beanDoorIn door = { { 0, 0, 0 }, { 0, 1, 0 }, { -1, 0, 0 }, { -5, 0, -50 }, { 5, 210, 50 }, { 100, 0, 0 } };
	struct beanDoorBox *boxes = NULL;
	CHECK(doorBoxesFrom(&door, 1, &boxes) == 1);
	CHECK(boxes[0].thin == 0 && fabsf(boxes[0].axis[0][2] - 1.0f) < 1e-5f);
	CHECK(fabsf(boxes[0].mid[1] - 105.0f) < 1e-3f && boxes[0].slide[2] == -1 && fabsf(boxes[0].travel[2] - 100.0f) < 1e-3f);
	free(boxes);

	/* Bean's wall 8 in front of the door's face side, its opening 5 wider
	 * each side (x -55..55) and to y 215; nothing behind it */
	{
		struct gevrBeanMesh *m = calloc(1, sizeof(*m));
		m->tris = calloc(16, sizeof(*m->tris));
		m->ntex = 1;
		sheet(m, -300, -55, 0, 300, 8);
		sheet(m, 55, 300, 0, 300, 8);
		sheet(m, -55, 55, 215, 300, 8);
		struct beanDoorIn swing = door;   /* a slide's own side stays open */
		swing.move[0] = 0.0f;
		const int before = m->ntris;
		const struct beanDoorResult r = beanFitDoorways(m, 1.0f, &swing, 1, NULL);
		printf("doorway: %d doors, %d pulled, %d reveals, %d slot tris\n", r.doors, r.moved, r.reveals, r.slotTris);
		CHECK(r.doors == 1 && r.moved > 0 && r.reveals > 0);
		CHECK(m->ntris == before + r.reveals * 2);
		/* the opening's sides and top on the door's edges */
		int sides = 0, top = 0;
		for (int i = 0; i < before; i++)
			for (int c = 0; c < 3; c++) {
				const float *p = &m->tris[i].p[c * 3];
				CHECK(fabsf(fabsf(p[0]) - 55.0f) > 1e-3f || p[1] > 214.0f);
				sides += fabsf(fabsf(p[0]) - 50.0f) < 1e-3f;
				top += fabsf(p[1] - 210.0f) < 1e-3f;
			}
		CHECK(sides > 0 && top > 0);
		/* the reveals run from the wall back to the door's middle */
		for (int i = before; i < m->ntris; i++) {
			float zlo = 1e30f, zhi = -1e30f;
			for (int c = 0; c < 3; c++) {
				zlo = fminf(zlo, m->tris[i].p[c * 3 + 2]);
				zhi = fmaxf(zhi, m->tris[i].p[c * 3 + 2]);
			}
			CHECK(fabsf(zlo) < 1e-3f && fabsf(zhi - 8.0f) < 1e-3f);
		}
		gevrBeanMeshFree(m);
	}

	/* The wall the door slides into: GoldenEye's in the door's thickness
	 * (x -50..-250 at z 0), Bean's a sheet behind the door's face (z 2), so
	 * the open door drew over it. Wall goes in front of the face (z 7). */
	{
		struct gevrBeanMesh *m = calloc(1, sizeof(*m));
		m->tris = calloc(16, sizeof(*m->tris));
		m->ntex = 1;
		sheet(m, -300, -50, 0, 300, 2);
		sheet(m, 50, 300, 0, 300, 2);
		sheet(m, -50, 50, 210, 300, 2);
		const float ge[2][3][3] = { { { -250, 0, 0 }, { -50, 0, 0 }, { -50, 210, 0 } },
		                            { { -250, 0, 0 }, { -50, 210, 0 }, { -250, 210, 0 } } };
		const struct beanDoorGe geTris = { ge, 2 };
		struct beanDoorIn left = door;
		left.move[0] = -100.0f;   /* into the wall at -x */
		const int before = m->ntris;
		const struct beanDoorResult r = beanFitDoorways(m, 1.0f, &left, 1, &geTris);
		printf("slot: %d slot tris over %d doors\n", r.slotTris, r.slotDoors);
		CHECK(r.slotDoors == 1 && r.slotTris >= 2);
		int patches = 0;
		for (int i = before; i < m->ntris; i++) {
			const float *p = m->tris[i].p;
			if (fabsf(p[2] - 7.0f) > 1e-3f || fabsf(p[5] - 7.0f) > 1e-3f || fabsf(p[8] - 7.0f) > 1e-3f) continue;
			float n[3];
			CHECK(holeNormal(p, n) > 0.0f && n[2] > 0.99f);   /* facing out of the door, as the donor */
			for (int c = 0; c < 3; c++) CHECK(p[c * 3] <= -50.0f + 1e-3f && p[c * 3] >= -150.0f - 1e-3f);
			patches++;
		}
		CHECK(patches >= 2);
		gevrBeanMeshFree(m);
	}
	printf("bean doors: ok\n");
	return 0;
}
