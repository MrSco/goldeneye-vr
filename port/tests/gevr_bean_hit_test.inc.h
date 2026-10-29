/* Compile the two production propobj.c walkers below this fixture. Stub only
 * ray math: the regression concerns which lists/vertices the walkers read. */
#include "bg.h"
#define G_TRI4 0xb1

static Vertex *expectedHitVertices;
static ModelRwData_DisplayList_CollisionRecord collisionRw;
/* a second list's own data (a wreck guest, gevr_bean_material_paths_test.inc.h) */
static ModelNode *guestRwNode;
static ModelRwData_DisplayList_CollisionRecord guestRw;
static int hitTriangles;

/* model.c's own signatures: gevr_bean_wounds.inc.h declares them */
union ModelRwData *modelGetNodeRwData(Model *model, ModelNode *node)
{
	(void)model;
	return (union ModelRwData *)(node && node == guestRwNode ? &guestRw : &collisionRw);
}
s32 modelFindNodeMtxIndex(ModelNode *node, s32 unused)
{
	(void)node;
	(void)unused;
	return 0;
}
static void modelApplyDistanceRelations(Model *m, ModelNode *n)
{
	(void)m;
	(void)n;
}
static void modelApplyToggleRelations(Model *m, ModelNode *n)
{
	(void)m;
	(void)n;
}
static void modelApplyHeadRelations(Model *m, ModelNode *n)
{
	(void)m;
	(void)n;
}
static void matrix_4x4_invert_affine(float in[4][4], float out[4][4]) { memcpy(out, in, sizeof(Mtxf)); }
static void mtx4TransformVecInPlace(Mtxf *m, coord3d *v)
{
	(void)m;
	(void)v;
}
static void mtx4RotateVecInPlace(Mtxf *m, coord3d *v)
{
	(void)m;
	(void)v;
}
int bgTestRayIntersectsBbox(coord3d *p, coord3d *d, s32 *lo, s32 *hi)
{
	(void)p;
	(void)d;
	(void)lo;
	(void)hi;
	return 1;
}
int intersectRayTriangle(Vertex *a, Vertex *b, Vertex *c, coord3d *zero, coord3d *p, coord3d *end,
						 coord3d *dir, HitThing *hit)
{
	(void)zero;
	(void)p;
	(void)end;
	(void)dir;
	assert(a == expectedHitVertices && b == a + 1 && c == a + 2);
	hitTriangles++;
	memset(hit, 0, sizeof(*hit));
	hit->hitpos.z = 10;
	return 1;
}

/* PRODUCTION HIT WALKERS */

static void hitChecks(void)
{
	/* Exactly the segmented pointers the live game keeps after conversion.
	 * A replacement is intentionally a heap pointer outside this model. */
	struct {
		Gfx native[3];
		Vertex vertices[3];
	} storage = {0};
	expectedHitVertices = storage.vertices;
	uintptr_t offset = (uint8_t *)storage.vertices - (uint8_t *)&storage;
	gSPVertex(storage.native, (void *)(0x05000000u + offset + 1), 3, 0);
	gSP1Triangle(storage.native + 1, 0, 1, 2, 0);
	gSPEndDisplayList(storage.native + 2);
	Gfx replacement[1];
	gSPEndDisplayList(replacement);
	RenderPosView matrix = {0};
	for (int k = 0; k < 4; k++)
		matrix.pos.m[k][k] = 1;
	Model model = {0};
	model.render_pos = &matrix;
	coord3d ray = {0}, direction = {0, 0, 1};
	HitThing hit = {0};
	for (int collision = 0; collision < 2; collision++) {
		union ModelRoData data = {0};
		ModelNode node = {0};
		node.Data = &data;
		node.Opcode = collision ? MODELNODE_OPCODE_DLCOLLISION : MODELNODE_OPCODE_DL;
		if (collision) {
			data.DisplayListCollisions.Primary = (Gfx *)0x05000000u;
			data.DisplayListCollisions.Vertices = storage.vertices;
			data.DisplayListCollisions.BaseAddr = &storage;
			data.DisplayListCollisions.ModelType = 3;
			collisionRw.gdl = data.DisplayListCollisions.Primary;
			collisionRw.Vertices = (void *)&storage;
		} else {
			data.DisplayList.Primary = (Gfx *)0x05000000u;
			data.DisplayList.Vertices = storage.vertices;
			data.DisplayList.BaseAddr = &storage;
			data.DisplayList.ModelType = 3;
		}
		union ModelRoData before = data;
		s_nswap = 0;
		registerPrimary(&node, replacement, 0);
		assert(!memcmp(&data, &before, sizeof(data)));
		for (int enabled = 0; enabled < 2; enabled++) {
			gevrBeanSetEnabled(enabled);
			assert(gevrBeanShownGdl(&node, (Gfx *)0x05000000u) ==
				   (enabled ? replacement : (Gfx *)0x05000000u));
			assert(gevrBeanShownModelType(&node, 3) == (enabled ? 0 : 3));
			s32 index = -1;
			ModelNode *found = NULL;
			assert(propobjFindHit(&model, &node, &ray, &direction, &hit, &index, &found));
			assert(index == 0 && found == &node && hit.vtx0 == storage.vertices);
			assert(!memcmp(&data, &before, sizeof(data)));
		}
	}
	assert(hitTriangles == 4);
	s_nswap = 0;
	puts("Bean rendering leaves production aiming/hit walkers on native geometry");
}
