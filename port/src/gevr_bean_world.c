/*
 * Bean rooms and skies.
 *
 * DAB's table targets the converted Perfect Dark level: native file units
 * divided by GoldenEye's level scale, then recentered. GoldenEye instead
 * applies the reciprocal level scale in its room matrix (room_data_float2).
 * Undo that conversion before dealing triangles against the native rooms:
 * bean * (DAB scale / room_data_float2) - native room.pos.
 * The Perfect Dark recentering offset does not apply to native room files.
 *
 * The sky is the camera's turn with the translation removed, scaled so the
 * farthest vertex sits at half the far plane. It is drawn instead of the
 * N64 sky plane.
 */

#include "gevr_bean_log.h"
#include "gevr_bean_control.h"
#include "gevr_bean_fog.h"
#include "gevr_mirror.h"
#include "gevr_bean_ce.h"

#include <ultra64.h>
#include <PR/gbi.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <bondconstants.h>

#include "bg.h"
#include "bgfog.h"
#include "bondview.h"
#include "boss.h"
#include "player.h"
#include "dyn.h"
#include "fr.h"
#include "matrixmath.h"
#include "gevr_bean_int.h"
#include "gevr_bean_room_scale.inc.h"
#include "gevr_static_mesh.h"
#include "gevr_bean_holes.inc.h"
#include "gevr_bean_cache.inc.h"
#include "gevr_bean_doors.inc.h"
static int ensureStage(void);
static int ensureSky(void);
void gevrBeanLoadingTick(float progress);   /* gevr_bean_load.c: the front-load's screen (#269) */
#include "gevr_bean_shots.inc.h"
#include "chrai.h"
#include "loadobjectmodel.h"

#define MAX_ROOMS 160
#define SKY_EXTENT 30000.0f

struct roomSlot {
	Gfx *opa;
	Gfx *xlu;
	void *vtxOpa;
	void *vtxXlu;
	int has;
	/* GoldenEye's own backdrop, which Bean leaves out: drawn as nothing (hiddenRoom) */
	int hidden;
	/* drawn from its own lists where GoldenEye's portal walk reaches it, out of
	 * the merged world (keepOutOfRooms) */
	int byPortals;
	/* opa and xlu as static meshes (gevr_static_mesh.h): the lists drawn, or NULL */
	Gfx *wrap[2];
	GevrStaticMesh *mesh[2];
	/* the Bean geometry's bounds, in the room table's units (world, not room-local) */
	float lo[3], hi[3];
};

static struct roomSlot s_room[MAX_ROOMS];
static int s_nrooms;
static int s_stage = -9999;
static int s_tried;
static int s_ok;
static int s_levelWater;   /* Bean pictures markWater() took for water */
static float s_reachWorld;  /* the HD rooms' box's diagonal, world units (gevrBeanWorldReach()) */
static Gfx *s_opaque, *s_opaqueWrap;
static void *s_opaqueVertices;
static GevrStaticMesh *s_opaqueMesh;
static int s_opaqueDrawn;
static int s_nlampUndo;   /* gevrBeanWorldLampDarken's undo log, below */
static int s_keptRoom = -1;               /* keepOutOfRooms' room, or -1 */
static float s_keptLo[3], s_keptHi[3];   /* its box from GoldenEye's own triangles, room table units */

static int s_cullOutside;   /* this view's camera stands outside the level */
static int s_authoredView;  /* this view is one of GoldenEye's own cameras */
static int s_cullForce;     /* files/gevr_bean_roomcull.txt: 0 by the camera, 1 always, 2 never */
static int cameraOutside(void);

/*
 * GoldenEye stands its own cameras (the opening's stills and swirl, the death
 * and ending cameras) inside rock and under floors, and culls the backs of its
 * opaque rooms; HD rooms are two-sided (4J's decks, treads and roofs are
 * single planes), so there a wall or a floor fills the view (user, 2026-10-14,
 * Facility's opening). DAB gebeanStageTickCamera: while such a camera runs,
 * 48 rays against GoldenEye's own room triangles say whether it stands outside
 * the level, and the HD rooms cull their backs for that view.
 */
void gevrBeanWorldBeginView(int authored)
{
	s_opaqueDrawn = 0;
	s_authoredView = authored;
	const int outside = s_cullForce == 1 || (s_cullForce == 0 && authored && cameraOutside());
	if (outside != s_cullOutside)
		GEVR_BEAN_LOG("gevr bean: camera outside the level %d\n", outside);
	s_cullOutside = outside;
}

int gevrBeanWorldCullsBackFaces(void)
{
	return s_cullOutside;
}

int gevrBeanWorldAuthoredView(void)
{
	return s_authoredView;
}

static Gfx *s_skyGdl;
static void *s_skyVtx;
static Gfx *s_skyWrap;
static GevrStaticMesh *s_skyMesh;
static int s_skyStage = -9999;
static int s_skyTried;
static int s_skyOk;
static int s_ce = -1;
static int s_ceOverlay = -1;  // whether s_ce was looked up with the overlay read

static const char *const s_roots[] = {
    GEVR_BEAN_PACK_DIR "/files",
	"added-content/xbla/files",
	"added-content/xbla",
	"xbla/files",
	"xbla"
};

struct beanStage {
	int stage;
	const char *bg;
	float scale;
	const char *skyCe;
	const char *skyRelease;
};

/* DAB fitted scales in GoldenEye file units (no Perfect Dark recentering).
 * skyRelease: DAB gebeansky.c releaseSkyNames. The release's skies are one
 * picture, Surface's daylit placeholder; it fits only Surface, Dam and
 * Runway, and every other level keeps GoldenEye's sky (a tester on Surface 2,
 * 2026-10-24: "a mixture of night and day"). */
static const struct beanStage s_stages[] = {
    {LEVELID_ARCHIVES, "archives", 1.072400836f, "archives", NULL},
    {LEVELID_CONTROL, "control", 1.616580623f, "control", NULL},
    {LEVELID_FACILITY, "facility", 0.531318270f, NULL, NULL},
    {LEVELID_AZTEC, "aztec", 1.475407422f, "aztec", NULL},
    {LEVELID_BASEMENT, "library", 0.573907333f, "library", NULL},
    {LEVELID_STACK, "library", 0.573907333f, "library", NULL},
    {LEVELID_LIBRARY, "library", 0.573907333f, "library", NULL},
    {LEVELID_CAVERNS, "cavern", 0.501072787f, "caves", NULL},
    {LEVELID_CRADLE, "cradle", 1.828668257f, "cradle", NULL},
    {LEVELID_EGYPT, "temple", 0.921010121f, "temple", NULL},
    {LEVELID_DAM, "dam", 4.280101153f, "dam", "dam"},
    {LEVELID_DEPOT, "depot", 0.805820509f, "depot", NULL},
    {LEVELID_FRIGATE, "frigate", 0.821433927f, "frigate", NULL},
    {LEVELID_TEMPLE, "multitemple", 3.119410587f, "multitemple", NULL},
    {LEVELID_JUNGLE, "jungle", 0.706146978f, NULL, NULL},
    {LEVELID_CAVES, "caves", 0.715800060f, "caves", NULL},
    {LEVELID_STREETS, "streets", 1.239411313f, "streets", NULL},
    {LEVELID_COMPLEX, "complex", 0.401740938f, "complex", NULL},
    {LEVELID_RUNWAY, "runway", 0.508383572f, "runway", "runway"},
    {LEVELID_BUNKER1, "bunker", 0.429214082f, "bunker", NULL},
    {LEVELID_BUNKER2, "bunker2", 0.429214887f, "bunker2", NULL},
    {LEVELID_SURFACE, "surface", 0.497385395f, "sf1", "surface"},
    {LEVELID_SURFACE2, "surface", 0.497385395f, "sf2", NULL},
    {LEVELID_SILO, "silo", 0.615140709f, "silo", NULL},
    {LEVELID_STATUE, "statuepark", 0.007471857f, "statuepark", NULL},
    {LEVELID_TRAIN, "train", 1.300371514f, "train", NULL}};

static int baseStage(int stage)
{
	switch (stage) {
	case LEVELID_BUNKER2_MP: return LEVELID_BUNKER2;
	case LEVELID_ARCHIVES_MP: return LEVELID_ARCHIVES;
	case LEVELID_CAVERNS_MP: return LEVELID_CAVERNS;
	case LEVELID_FACILITY_MP: return LEVELID_FACILITY;
	case LEVELID_EGYPT_MP: return LEVELID_EGYPT;
	case LEVELID_STATUE_MP: return LEVELID_STATUE;
	case LEVELID_CRADLE_MP: return LEVELID_CRADLE;
	default: return stage;
	}
}

static const struct beanStage *stageOf(int stage)
{
	stage = baseStage(stage);
	for (int i = 0; i < (int)(sizeof(s_stages) / sizeof(s_stages[0])); i++) {
		if (s_stages[i].stage == stage) {
			return &s_stages[i];
		}
	}
	return NULL;
}

static int openRel(const char *rel, char *out, int outlen)
{
	FILE *fp = fopen(rel, "rb");
	if (!fp) {
		return 0;
	}
	fclose(fp);
	snprintf(out, outlen, "%s", rel);
	return 1;
}

static int findFile(const char *source, char *out, int outlen)
{
	char rel[192];
	const char *ce = gevrBeanCeRoot();
	// the Community Edition's overlay first (gevr_bean_ce.c)
	if (ce) {
		snprintf(rel, sizeof(rel), "%s/%s/default.bin", ce, source);
		if (openRel(rel, out, outlen)) {
			return 1;
		}
	}
	for (int i = 0; i < (int)(sizeof(s_roots) / sizeof(s_roots[0])); i++) {
		const char *root = s_roots[i];
		size_t n = strlen(root);
		snprintf(rel, sizeof(rel), "%s/%s/default.bin", root, source);
		if (openRel(rel, out, outlen)) {
			return 1;
		}
		if (n < 6 || strcmp(root + n - 6, "/files") != 0) {
			snprintf(rel, sizeof(rel), "%s/files/%s/default.bin", root, source);
			if (openRel(rel, out, outlen)) {
				return 1;
			}
		}
	}
	return 0;
}

static int communityEdition(void)
{
	char path[192];
	const int overlay = gevrBeanCeRoot() != NULL;
	if (s_ce < 0 || s_ceOverlay != overlay) {
		s_ceOverlay = overlay;
		s_ce = findFile("new/skydome/sf2", path, sizeof(path));
	}
	return s_ce;
}

static void fillVert(const struct gevrBeanMesh *mesh, const struct gevrBeanTri *t,
		struct gevrEmitVert *e, const float p[3], int corner)
{
	/* Material alpha testing owns cutouts; texture alpha alone may be
	 * an overlay that needs the opaque surface underneath it. */
	e->p[0] = p[0];
	e->p[1] = p[1];
	e->p[2] = p[2];
	e->n[0] = t->n[corner * 3];
	e->n[1] = t->n[corner * 3 + 1];
	e->n[2] = t->n[corner * 3 + 2];
	e->uv[0] = t->uv[corner * 2];
	e->uv[1] = t->uv[corner * 2 + 1];
	e->nofog = 0;
	e->rgba[0] = t->rgba[corner * 4];
	e->rgba[1] = t->rgba[corner * 4 + 1];
	e->rgba[2] = t->rgba[corner * 4 + 2];
	e->rgba[3] = t->rgba[corner * 4 + 3];
	e->reflection=t->reflection;
    e->reflectionDiffuse=t->reflectionDiffuse;
    e->reflectionColour=t->reflectionColour;e->reflectionAlphaBase=t->reflectionAlphaBase;
	e->tex = t->tex;
	e->alpha = t->alpha;
	e->cut = t->alphatest;
	e->decal = (t->flags & GEVR_BEAN_F_DECAL) ? 1 : 0;
	e->side = t->side;
}

#include "gevr_bean_rooms.inc.h"

static int cameraOutside(void)
{
	const Mtxf *m = camGetWorldToScreenMtxf();
	float right[3], up[3], look[3], o[3];
	int hits = 0, backs = 0;
	if (!m || !s_tree || !s_referenceCount)
		return 0;
	/* world to camera, row vectors: the rotation's columns are the camera's axes */
	for (int k = 0; k < 3; k++) {
		right[k] = m->m[k][0];
		up[k] = m->m[k][1];
		look[k] = -m->m[k][2];
	}
	const float s2 = right[0] * right[0] + right[1] * right[1] + right[2] * right[2];
	if (s2 < 1e-12f)
		return 0;
	for (int k = 0; k < 3; k++)
		o[k] = -(m->m[3][0] * m->m[k][0] + m->m[3][1] * m->m[k][1] + m->m[3][2] * m->m[k][2]) / s2;
	const float s = sqrtf(s2);
	for (int k = 0; k < 3; k++) {
		right[k] /= s;
		up[k] /= s;
		look[k] /= s;
	}
	const float ty = tanf(viGetFovY() * (3.14159265f / 360.0f));
	const float tx = ty * (viGetViewHeight() > 0 ? (float)viGetViewWidth() / viGetViewHeight() : 4.0f / 3.0f);
	for (int y = 0; y < 6; y++) {
		for (int x = 0; x < 8; x++) {
			const float sx = ((x + 0.5f) / 8 * 2 - 1) * tx, sy = ((y + 0.5f) / 6 * 2 - 1) * ty;
			float d[3], inv[3], best = 1e9f;
			int facing = 0;
			for (int k = 0; k < 3; k++)
				d[k] = look[k] + right[k] * sx + up[k] * sy;
			const float len = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
			for (int k = 0; k < 3; k++) {
				d[k] /= len;
				inv[k] = 1.0f / (d[k] != 0.0f ? d[k] : 1e-9f);
			}
			roomRayNode(0, o, d, inv, &best, &facing);
			if (facing) {
				hits++;
				backs += facing < 0;
			}
		}
	}
	/* two thirds of what it sees the back of GoldenEye's level: outside it;
	 * from inside, a back face is only ever a seam */
	return hits >= 8 && backs * 3 >= hits * 2;
}

/*
 * DAB gebeanstage.c markWaterPictures (port/licenses/dabs-mod.txt): a Bean
 * picture is water when at least two of its triangles, and at least half,
 * lie within 64 units of GoldenEye's water (1508 Caverns, 1511 Dam; on
 * vertices shaded black - Complex's pits - it is none). Here such a triangle
 * must also lie level, every corner within 16 units of the water's height:
 * near was enough to take Dam's waterline concrete, the snow and the rocky
 * grass of its shore for water, and they crept with it (2026-10-14). Dam's
 * reservoir lies within 16 units, Caverns' pools within 4. GoldenEye's slide,
 * 0.25 and 0.1 quarter texels a frame of its own picture, is taken across the
 * ground and into Bean's, which lies bigger and turned against it, from the
 * two pictures' gradients over x and z, each the area-weighted mean.
 */
#define WATER_NEAR 64.0f
#define WATER_LEVEL 16.0f
#define WATER_BLACK 8

struct waterMeasure {
	double grad[2][2], area;
};

static void waterGrad(struct waterMeasure *m, const float v[3][3], const float f[3][2])
{
	const float ax = v[1][0] - v[0][0], az = v[1][2] - v[0][2];
	const float bx = v[2][0] - v[0][0], bz = v[2][2] - v[0][2];
	const float det = ax * bz - az * bx;
	const float area = fabsf(det) * 0.5f;
	if (area < 1.0f)
		return;   /* a wall */
	for (int j = 0; j < 2; j++) {
		const float da = f[1][j] - f[0][j], db = f[2][j] - f[0][j];
		m->grad[j][0] += (da * bz - db * az) / det * area;
		m->grad[j][1] += (db * ax - da * bx) / det * area;
	}
	m->area += area;
}

/*
 * DAB gebeanstage.c markUnderSea() (port/licenses/dabs-mod.txt): what lies
 * under a level's sea is seen through it, as GoldenEye has it. GoldenEye's
 * sea is the sky's water plane (WaterRepeat its height), drawn before
 * everything; Frigate's reflection is the hull built again upside down below
 * its foot, in a translucent leaf at half alpha over the sea. Bean builds the
 * same mirrored hull, on both sides, among the hull's own opaque draws, so it
 * stood under the ship as a second, solid hull and the ship looked sunk deep
 * in the water (user, Quest, 2026-11-19, beside the N64 look).
 *
 * So below the lowest of GoldenEye's opaque geometry, where that is under the
 * sea and GoldenEye draws something translucent there, Bean's triangles are
 * drawn translucent: at that leaf's alpha at the waterline, fading to nothing
 * at the deepest, in the mirror's mean colour and a few steps of fade, turned
 * outwards and culled so the far side's mirror is not seen through the near
 * one's. What is above - the hull's own foot, the decks - is as it was.
 */
#define UNDERSEA_SLACK 2.0f
#define UNDERSEA_STEPS 8

static int underSeaTri(const struct gevrBeanTri *t, float fileScale, float top)
{
	return t->p[1] * fileScale <= top && t->p[4] * fileScale <= top && t->p[7] * fileScale <= top;
}

static void underSeaSwap(uint8_t *a, uint8_t *b, size_t n)
{
	uint8_t tmp[64];
	memcpy(tmp, a, n);
	memcpy(a, b, n);
	memcpy(b, tmp, n);
}

static int markUnderSea(struct gevrBeanMesh *mesh, float fileScale)
{
	struct CurrentEnvironmentRecord *env = fogGetCurrentEnvironmentp();
	float floor = 1e30f, top, bottom = 1e30f, centre[3] = { 0, 0, 0 };
	double mean[3] = { 0, 0, 0 };
	int alpha = 0, num = 0, marked = 0;
	if (!env || !env->IsWater || !s_reference || !s_referenceCount)
		return 0;
	for (int i = 0; i < s_referenceCount; i++)
		if (s_reference[i].opaque && s_reference[i].lo[1] < floor)
			floor = s_reference[i].lo[1];
	/* the sea's height is in world units, the level file's in its own (room_data_float2) */
	if (floor * room_data_float2 >= env->WaterRepeat)
		return 0;
	for (int i = 0; i < s_referenceCount; i++)
		if (!s_reference[i].opaque && s_reference[i].hi[1] <= floor + UNDERSEA_SLACK && s_reference[i].envAlpha > alpha)
			alpha = s_reference[i].envAlpha;
	if (!alpha)
		return 0;
	top = floor + UNDERSEA_SLACK;
	for (int i = 0; i < mesh->ntris; i++) {
		const struct gevrBeanTri *t = &mesh->tris[i];
		if (!underSeaTri(t, fileScale, top))
			continue;
		for (int k = 0; k < 3; k++) {
			if (t->p[k * 3 + 1] * fileScale < bottom)
				bottom = t->p[k * 3 + 1] * fileScale;
			for (int j = 0; j < 3; j++) {
				centre[j] += t->p[k * 3 + j] * fileScale;
				mean[j] += t->rgba[k * 4 + j];
			}
		}
		num += 3;
	}
	if (!num)
		return 0;
	for (int j = 0; j < 3; j++) {
		centre[j] /= num;
		mean[j] = mean[j] / num + 0.5;
	}
	for (int i = 0; i < mesh->ntris; i++) {
		struct gevrBeanTri *t = &mesh->tris[i];
		float e1[3], e2[3], n[3], mid[3];
		if (!underSeaTri(t, fileScale, top))
			continue;
		/* turned to face away from the middle of the mirror, so culling keeps
		 * the side towards the eye */
		for (int j = 0; j < 3; j++) {
			mid[j] = (t->p[j] + t->p[3 + j] + t->p[6 + j]) * fileScale / 3.0f;
			e1[j] = t->p[3 + j] - t->p[j];
			e2[j] = t->p[6 + j] - t->p[j];
		}
		n[0] = e1[1] * e2[2] - e1[2] * e2[1];
		n[1] = e1[2] * e2[0] - e1[0] * e2[2];
		n[2] = e1[0] * e2[1] - e1[1] * e2[0];
		if (n[0] * n[0] + n[1] * n[1] + n[2] * n[2] > 0.0f &&
		    n[0] * (mid[0] - centre[0]) + n[2] * (mid[2] - centre[2]) < 0.0f) {
			underSeaSwap((uint8_t *)&t->p[3], (uint8_t *)&t->p[6], 3 * sizeof(float));
			underSeaSwap((uint8_t *)&t->n[3], (uint8_t *)&t->n[6], 3 * sizeof(float));
			underSeaSwap((uint8_t *)&t->uv[2], (uint8_t *)&t->uv[4], 2 * sizeof(float));
			underSeaSwap(&t->rgba[4], &t->rgba[8], 4);
			underSeaSwap(&t->bone[4], &t->bone[8], 4);
			underSeaSwap(&t->weight[4], &t->weight[8], 4);
		}
		for (int k = 0; k < 3; k++) {
			const float y = t->p[k * 3 + 1] * fileScale;
			const float fade = top > bottom ? (y - bottom) / (top - bottom) : 1.0f;
			const int step = (int)(fade * UNDERSEA_STEPS + 0.5f);
			for (int j = 0; j < 3; j++)
				t->rgba[k * 4 + j] = (uint8_t)mean[j];
			t->rgba[k * 4 + 3] = (uint8_t)(alpha * step / UNDERSEA_STEPS);
		}
		t->alpha = 254;   /* the translucent pass (gevrBeanEmitList) */
		t->alphatest = 0;
		t->side = GEVR_BEAN_SIDE_BACKED;   /* drawn culled */
		marked++;
	}
	if (marked)
		GEVR_BEAN_LOG("gevr bean: %d triangles of the reflection under the sea drawn through it "
		              "(GoldenEye's floor %.0f, sea %.0f, alpha %d)\n", marked, floor, env->WaterRepeat, alpha);
	return marked;
}

static void markWater(const struct gevrBeanMesh *mesh, float fileScale)
{
	struct waterMeasure ge, hd;
	float lo[3] = { 1e30f, 1e30f, 1e30f }, hi[3] = { -1e30f, -1e30f, -1e30f };
	int nwater = 0, marked = 0;
	memset(&ge, 0, sizeof(ge));
	memset(&hd, 0, sizeof(hd));
	gevrBeanWaterSetRate(0, 0, 0);
	s_levelWater = 0;
	if (!s_reference || !mesh->ntex)
		return;
	int *water = malloc((size_t)s_referenceCount * sizeof(int));
	int *on = calloc((size_t)mesh->ntex, sizeof(int)), *all = calloc((size_t)mesh->ntex, sizeof(int));
	uint8_t *near = calloc((size_t)mesh->ntris + 1, 1);
	if (!water || !on || !all || !near)
		goto done;
	for (int i = 0; i < s_referenceCount; i++) {
		const struct beanRoomTri *t = &s_reference[i];
		float f[3][2];
		if ((t->tex != 1508 && t->tex != 1511) || t->shade <= WATER_BLACK)
			continue;
		water[nwater++] = i;
		for (int k = 0; k < 3; k++) {
			if (t->lo[k] < lo[k]) lo[k] = t->lo[k];
			if (t->hi[k] > hi[k]) hi[k] = t->hi[k];
		}
		/* s10.5: an eighth is a quarter texel, the unit GoldenEye moves it in */
		for (int v = 0; v < 3; v++) {
			f[v][0] = t->st[v][0] / 8.0f;
			f[v][1] = t->st[v][1] / 8.0f;
		}
		waterGrad(&ge, t->p, (const float (*)[2])f);
	}
	if (!nwater)
		goto done;
	for (int i = 0; i < mesh->ntris; i++) {
		const struct gevrBeanTri *t = &mesh->tris[i];
		float mid[3];
		int inside = 1;
		if (t->tex < 0 || t->tex >= mesh->ntex)
			continue;
		all[t->tex]++;
		for (int k = 0; k < 3; k++) {
			mid[k] = (t->p[k] + t->p[3 + k] + t->p[6 + k]) * fileScale / 3.0f;
			if (mid[k] < lo[k] - WATER_NEAR || mid[k] > hi[k] + WATER_NEAR)
				inside = 0;
		}
		if (!inside)
			continue;
		float best = WATER_NEAR * WATER_NEAR;
		const struct beanRoomTri *nearest = NULL;
		for (int j = 0; j < nwater; j++) {
			const struct beanRoomTri *w = &s_reference[water[j]];
			if (roomBoxDistance(mid, w->lo, w->hi) >= best)
				continue;
			const float d = roomTriangleDistance(mid, w);
			if (d < best) {
				best = d;
				nearest = w;
			}
		}
		if (nearest) {
			/* level, and at the water's height (GoldenEye's water is flat) */
			const float height = (nearest->p[0][1] + nearest->p[1][1] + nearest->p[2][1]) / 3.0f;
			float a[3], b[3], n[3];
			int level = 1;
			for (int k = 0; k < 3; k++) {
				a[k] = (t->p[3 + k] - t->p[k]) * fileScale;
				b[k] = (t->p[6 + k] - t->p[k]) * fileScale;
				if (fabsf(t->p[k * 3 + 1] * fileScale - height) >= WATER_LEVEL)
					level = 0;
			}
			n[0] = a[1] * b[2] - a[2] * b[1];
			n[1] = a[2] * b[0] - a[0] * b[2];
			n[2] = a[0] * b[1] - a[1] * b[0];
			if (!(fabsf(n[1]) > 0.98f * sqrtf(n[0] * n[0] + n[1] * n[1] + n[2] * n[2])))
				level = 0;
			if (level) {
				near[i] = 1;
				on[t->tex]++;
			}
		}
	}
	for (int x = 0; x < mesh->ntex; x++) {
		if (mesh->standin[x] && on[x] >= 2 && on[x] * 2 >= all[x]) {
			gevrBeanTexWaterSet(mesh->standin[x]);
			marked++;
			GEVR_BEAN_LOG("gevr bean water: picture %d moves as GoldenEye's water, %d of its %d triangles on it",
			              x, on[x], all[x]);
		}
	}
	s_levelWater = marked;
	/* Bean's UV is in repeats, drawn as quarter texels of a 32 texel repeat,
	 * t running the other way (gevr_bean.c gevrBeanEmitList) */
	for (int i = 0; marked && i < mesh->ntris; i++) {
		const struct gevrBeanTri *t = &mesh->tris[i];
		float v[3][3], f[3][2];
		if (!near[i] || !gevrBeanTextureIsWater(mesh->standin[t->tex]))
			continue;
		for (int k = 0; k < 3; k++) {
			for (int j = 0; j < 3; j++)
				v[k][j] = t->p[k * 3 + j] * fileScale;
			f[k][0] = t->uv[k * 2] * 128.0f;
			f[k][1] = -t->uv[k * 2 + 1] * 128.0f;
		}
		waterGrad(&hd, (const float (*)[3])v, (const float (*)[2])f);
	}
	if (ge.area > 0 && hd.area > 0) {
		double g[2][2], b[2][2];
		for (int j = 0; j < 2; j++)
			for (int k = 0; k < 2; k++) {
				g[j][k] = ge.grad[j][k] / ge.area;
				b[j][k] = hd.grad[j][k] / hd.area;
			}
		const double det = g[0][0] * g[1][1] - g[0][1] * g[1][0];
		if (fabs(det) > 1e-9) {
			/* the ground's motion under GoldenEye's slide, then Bean's picture's */
			const double dx = (0.25 * g[1][1] - 0.1 * g[0][1]) / det;
			const double dz = (0.1 * g[0][0] - 0.25 * g[1][0]) / det;
			const double rs = b[0][0] * dx + b[0][1] * dz, rt = b[1][0] * dx + b[1][1] * dz;
			gevrBeanWaterSetRate(1, (float)rs, (float)rt);
			GEVR_BEAN_LOG("gevr bean water: %.3f %.3f units a frame, in Bean's picture %.3f %.3f quarter texels "
			              "(GoldenEye's s/t %.4f %.4f %.4f %.4f, Bean's %.4f %.4f %.4f %.4f a unit)",
			              dx, dz, rs, rt, g[0][0], g[0][1], g[1][0], g[1][1], b[0][0], b[0][1], b[1][0], b[1][1]);
		}
	}
done:
	free(water);
	free(on);
	free(all);
	free(near);
}

/*
 * DAB gebeanstage.c (port/licenses/dabs-mod.txt, kept rooms): a GoldenEye
 * room Bean's mesh leaves empty is GoldenEye's own backdrop, which the
 * release remodelled further out and does not draw, when fewer than a quarter
 * of its triangles come within 64 units of Bean's mesh - Dam's room 31, a low
 * grey boulder of a cliff over the far end of the reservoir, stood in the
 * water before the dam in the intro with its own N64 water ("dam intro still
 * shows wacky mountain through the dam/dock", user, 2026-10-14) - or when
 * every one lies on Bean's surface (within 8 units), a patch of GoldenEye's
 * that Bean's own replaced.
 */
#define HIDDEN_NEAR 64.0f
#define HIDDEN_ON 8.0f

static int hiddenRoom(const struct gevrBeanMesh *mesh, float fileScale, int room)
{
	float lo[3] = { 1e30f, 1e30f, 1e30f }, hi[3] = { -1e30f, -1e30f, -1e30f };
	int tris = 0, near = 0, on = 0, nbean = 0;
	struct beanRoomTri *bean;
	for (int i = 0; i < s_referenceCount; i++) {
		if (s_reference[i].room != room)
			continue;
		tris++;
		for (int k = 0; k < 3; k++) {
			if (s_reference[i].lo[k] < lo[k]) lo[k] = s_reference[i].lo[k];
			if (s_reference[i].hi[k] > hi[k]) hi[k] = s_reference[i].hi[k];
		}
	}
	if (!tris)
		return 0;
	/* Bean's triangles that can come within reach of the room */
	bean = malloc((size_t)mesh->ntris * sizeof(*bean));
	if (!bean)
		return 0;
	for (int i = 0; i < mesh->ntris; i++) {
		struct beanRoomTri *b = &bean[nbean];
		int apart = 0;
		for (int k = 0; k < 3; k++) {
			b->lo[k] = 1e30f;
			b->hi[k] = -1e30f;
			for (int v = 0; v < 3; v++) {
				b->p[v][k] = mesh->tris[i].p[v * 3 + k] * fileScale;
				if (b->p[v][k] < b->lo[k]) b->lo[k] = b->p[v][k];
				if (b->p[v][k] > b->hi[k]) b->hi[k] = b->p[v][k];
			}
			if (b->lo[k] > hi[k] + HIDDEN_NEAR || b->hi[k] < lo[k] - HIDDEN_NEAR)
				apart = 1;
		}
		nbean += !apart;
	}
	for (int i = 0; i < s_referenceCount; i++) {
		const struct beanRoomTri *t = &s_reference[i];
		float mid[3], best = HIDDEN_NEAR * HIDDEN_NEAR;
		if (t->room != room)
			continue;
		for (int k = 0; k < 3; k++)
			mid[k] = (t->p[0][k] + t->p[1][k] + t->p[2][k]) / 3.0f;
		for (int j = 0; j < nbean && best >= HIDDEN_ON * HIDDEN_ON; j++) {
			if (roomBoxDistance(mid, bean[j].lo, bean[j].hi) >= best)
				continue;
			const float d = roomTriangleDistance(mid, &bean[j]);
			if (d < best)
				best = d;
		}
		if (best < HIDDEN_NEAR * HIDDEN_NEAR) {
			near++;
			on += best < HIDDEN_ON * HIDDEN_ON;
		}
	}
	free(bean);
	const int hidden = near * 4 < tris || on == tris;
	GEVR_BEAN_LOG("gevr bean: room %d has none of Bean's mesh: %d of its %d triangles near it, %d on it%s",
	              room, near, tris, on, hidden ? ": hidden" : ": GoldenEye's drawn");
	return hidden;
}

/*
 * DAB gebeanstage.c takeBackdrop() (port/licenses/dabs-mod.txt): the level's
 * backdrop. 4J ring some levels with a panorama on a band of triangles far
 * outside the level (Surface's peaks, Dam's, Runway's, Cradle's canyon), whose
 * top fades into the sky. A picture is the backdrop when no corner of any of
 * its triangles lies over the level's rooms in plan, or when at least half its
 * triangles are outside and it reaches past the level by more than a third of
 * the level's size (Cradle's cliffs dip under the platform). DAB draws it
 * after the sky, fogged by each vertex's distance from the eye: a band round
 * the level is as far at the view's edges as in its middle, and fogged by eye
 * depth, as the rooms are, its haze shifted as the head turned. Here nothing
 * is cut by the far plane, so only that fog is needed: its triangles are
 * marked 2 in nofog[] (GEVR_ROOM_RADIAL). Returns how many.
 */
static int markBackdrop(const struct gevrBeanMesh *mesh, float fileScale, int nrooms, uint8_t *nofog)
{
	float mn[2] = { 1e30f, 1e30f }, mx[2] = { -1e30f, -1e30f };
	int *total = calloc((size_t)mesh->ntex + 1, sizeof(int)), *outside = calloc((size_t)mesh->ntex + 1, sizeof(int));
	float (*reach)[4] = calloc((size_t)mesh->ntex + 1, sizeof(*reach));
	int marked = 0, pictures = 0;
	if (!total || !outside || !reach) {
		free(total);
		free(outside);
		free(reach);
		return 0;
	}
	for (int r = 1; r < nrooms; r++) {
		const float x0 = g_BgRoomInfo[r].minbounds.x, x1 = g_BgRoomInfo[r].maxbounds.x;
		const float z0 = g_BgRoomInfo[r].minbounds.z, z1 = g_BgRoomInfo[r].maxbounds.z;
		if (!(x1 > x0) || !(z1 > z0))
			continue;
		mn[0] = fminf(mn[0], x0);
		mn[1] = fminf(mn[1], z0);
		mx[0] = fmaxf(mx[0], x1);
		mx[1] = fmaxf(mx[1], z1);
	}
	for (int t = 0; t < mesh->ntex; t++) {
		reach[t][0] = reach[t][1] = 1e30f;
		reach[t][2] = reach[t][3] = -1e30f;
	}
	for (int i = 0; i < mesh->ntris && mn[0] <= mx[0]; i++) {
		const struct gevrBeanTri *t = &mesh->tris[i];
		int over = 0;
		if (t->tex < 0 || t->tex >= mesh->ntex)
			continue;
		for (int k = 0; k < 3; k++) {
			const float x = t->p[k * 3] * fileScale, z = t->p[k * 3 + 2] * fileScale;
			over |= x >= mn[0] && x <= mx[0] && z >= mn[1] && z <= mx[1];
			reach[t->tex][0] = fminf(reach[t->tex][0], x);
			reach[t->tex][1] = fminf(reach[t->tex][1], z);
			reach[t->tex][2] = fmaxf(reach[t->tex][2], x);
			reach[t->tex][3] = fmaxf(reach[t->tex][3], z);
		}
		total[t->tex]++;
		outside[t->tex] += !over;
	}
	const float size = fmaxf(mx[0] - mn[0], mx[1] - mn[1]);
	for (int i = 0; i < mesh->ntris && mn[0] <= mx[0]; i++) {
		const int x = mesh->tris[i].tex;
		if (x < 0 || x >= mesh->ntex || !total[x])
			continue;
		const float past = fmaxf(fmaxf(mn[0] - reach[x][0], mn[1] - reach[x][1]),
		                         fmaxf(reach[x][2] - mx[0], reach[x][3] - mx[1]));
		if ((outside[x] == total[x] || (outside[x] * 2 >= total[x] && past > size / 3.0f)) && !nofog[i]) {
			nofog[i] = 2;
			marked++;
		}
	}
	for (int x = 0; x < mesh->ntex; x++) {
		const float past = fmaxf(fmaxf(mn[0] - reach[x][0], mn[1] - reach[x][1]),
		                         fmaxf(reach[x][2] - mx[0], reach[x][3] - mx[1]));
		pictures += total[x] && (outside[x] == total[x] || (outside[x] * 2 >= total[x] && past > size / 3.0f));
	}
	if (marked)
		GEVR_BEAN_LOG("gevr bean: backdrop: %d triangles of %d pictures fogged by their distance from the eye\n",
		              marked, pictures);
	free(total);
	free(outside);
	free(reach);
	return marked;
}

/*
 * DAB gebeanstage.c markOverlaps() (port/licenses/dabs-mod.txt): Bean's solid
 * faces drawn culled where GoldenEye's own face under them has another room's
 * space behind it - the first of GoldenEye's faces a line back from it meets,
 * within 64 units, faces the same way (a wall of the space behind, not the far
 * side of a slab), and that space is closed on all six sides by GoldenEye's
 * faces turned into it (not the open air under a roof's edge or behind a
 * gantry's panel). GoldenEye's rooms overlap and rely on their faces being
 * culled: Aztec's computer room by the exhaust vent is a box whose west wall
 * runs through the vent's last 50 units, and from the vent GoldenEye shows the
 * room through the back of that wall, the body armour on the floor. Bean's
 * wall, drawn two-sided, was a stone face across the vent. Marked as a sheet's
 * face (GEVR_BEAN_SIDE_BACKED): its back culled.
 */
#define OVERLAP_REACH 64.0f
#define OVERLAP_SPACE 400.0f

static int markOverlaps(struct gevrBeanMesh *mesh, float fileScale)
{
	static const float axes[6][3] = { { 1, 0, 0 }, { -1, 0, 0 }, { 0, 1, 0 }, { 0, -1, 0 }, { 0, 0, 1 }, { 0, 0, -1 } };
	int count = 0;
	if (!s_tree)
		return 0;
	for (int i = 0; i < mesh->ntris; i++) {
		struct gevrBeanTri *t = &mesh->tris[i];
		float p[3][3], e1[3], e2[3], n[3], mid[3], o[3], back[3], nf[3], best = OVERLAP_REACH, len, pd;
		if ((t->flags & (GEVR_BEAN_F_DECAL | GEVR_BEAN_F_PAINTED)) || t->alpha < 255 || t->side ||
		    (t->tex >= 0 && t->tex < mesh->ntex && mesh->texCut[t->tex]))
			continue;
		for (int v = 0; v < 3; v++)
			for (int k = 0; k < 3; k++)
				p[v][k] = t->p[v * 3 + k] * fileScale;
		for (int k = 0; k < 3; k++) {
			e1[k] = p[1][k] - p[0][k];
			e2[k] = p[2][k] - p[0][k];
		}
		n[0] = e1[1] * e2[2] - e1[2] * e2[1];
		n[1] = e1[2] * e2[0] - e1[0] * e2[2];
		n[2] = e1[0] * e2[1] - e1[1] * e2[0];
		len = sqrtf(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
		if (!(len > 0.0f))
			continue;
		for (int k = 0; k < 3; k++) {
			n[k] /= len;
			mid[k] = (p[0][k] + p[1][k] + p[2][k]) / 3.0f;
			back[k] = -n[k];
			o[k] = mid[k] - n[k];
		}
		pd = n[0] * mid[0] + n[1] * mid[1] + n[2] * mid[2];
		/* GoldenEye has a face here, facing the same way */
		{
			float dist = 16.0f;   /* 4 units, squared */
			int ref = -1;
			roomReferenceNearestTri(0, mid, &dist, &ref);
			if (ref < 0 || !roomReferenceNormal(ref, nf))
				continue;
			const float dot = nf[0] * n[0] + nf[1] * n[1] + nf[2] * n[2];
			if (dot < 0.9f)
				continue;
		}
		/* the first of GoldenEye's faces behind it faces the same way: a space */
		if (roomRayFirst(o, back, n, pd, OVERLAP_REACH, &best, nf) < 0 ||
		    nf[0] * n[0] + nf[1] * n[1] + nf[2] * n[2] < 0.5f || best < 8.0f)
			continue;
		/* and the space between is closed all round */
		{
			const float in[3] = { o[0] + back[0] * best * 0.5f, o[1] + back[1] * best * 0.5f, o[2] + back[2] * best * 0.5f };
			int closed = 1;
			for (int a = 0; a < 6 && closed; a++) {
				float far = OVERLAP_SPACE, na[3];
				closed = roomRayFirst(in, axes[a], n, pd, OVERLAP_SPACE, &far, na) >= 0 &&
				         na[0] * axes[a][0] + na[1] * axes[a][1] + na[2] * axes[a][2] < 0.0f;
			}
			if (!closed)
				continue;
		}
		t->side = GEVR_BEAN_SIDE_BACKED;
		count++;
	}
	if (count)
		GEVR_BEAN_LOG("gevr bean: %d faces over another room's space drawn culled\n", count);
	return count;
}

static int roomFor(const float c[3], int nrooms)
{
	if (s_tree) {
		float distance = 1e30f;
		int room = -1;
		roomReferenceNearest(0, c, &distance, &room);
		if (room >= 0)
			return room;
	}
	int best = -1;
	float bestVol = 1e30f;
	int near = -1;
	float nearD = 1e30f;
	for (int r = 0; r < nrooms; r++) {
		float mn[3], mx[3], vol, d, mid[3];
		int inside = 1;
		mn[0] = g_BgRoomInfo[r].minbounds.x;
		mn[1] = g_BgRoomInfo[r].minbounds.y;
		mn[2] = g_BgRoomInfo[r].minbounds.z;
		mx[0] = g_BgRoomInfo[r].maxbounds.x;
		mx[1] = g_BgRoomInfo[r].maxbounds.y;
		mx[2] = g_BgRoomInfo[r].maxbounds.z;
		vol = 1.0f;
		for (int k = 0; k < 3; k++) {
			float ext = mx[k] - mn[k];
			if (!(ext > 0.0f)) {
				inside = 0;
				vol = 0.0f;
				break;
			}
			vol *= ext;
			if (c[k] < mn[k] - 1.0f || c[k] > mx[k] + 1.0f) {
				inside = 0;
			}
			mid[k] = (mn[k] + mx[k]) * 0.5f;
		}
		if (vol > 0.0f && inside && vol < bestVol) {
			bestVol = vol;
			best = r;
		}
		if (vol > 0.0f) {
			float dx = c[0] - mid[0], dy = c[1] - mid[1], dz = c[2] - mid[2];
			d = dx * dx + dy * dy + dz * dz;
			if (d < nearD) {
				nearD = d;
				near = r;
			}
		}
	}
	return best >= 0 ? best : near;
}

/*
 * A list made of state commands and runs of G_VTX/G_TRI1 (gevrBeanEmitList)
 * as one mesh: each distinct vertex once, three indices per triangle in the
 * list's order, and where each run starts and ends, so fast3d can draw a run
 * from buffers made once (gevr_static_mesh.h; DAB keeps its room runs the
 * same way, fast3d gfx_room_read_run).
 */
#include "gevr_static_mesh_build.inc.h"

/* G_GEVR_MESH ahead of the list, then on into the list itself */
static Gfx *meshWrap(Gfx *list, GevrStaticMesh **mesh)
{
	Gfx *w;
	*mesh = gevrStaticMeshBuild(list);
	if (!*mesh)
		return NULL;
	w = malloc(2 * sizeof(Gfx));
	if (!w) {
		gevrStaticMeshFree(*mesh);
		*mesh = NULL;
		return NULL;
	}
	w[0].words.w0 = (uintptr_t)G_GEVR_MESH << 24;
	w[0].words.w1 = (uintptr_t)*mesh;
	gSPBranchList(&w[1], list);
	return w;
}

static void clearRooms(void)
{
    roomReferenceFree();
    shotsFree();
    s_cullOutside = 0;
    gevrStaticMeshFree(s_opaqueMesh); free(s_opaqueWrap);
    s_nlampUndo = 0;   /* its colours went with the lists (gevrBeanWorldLampDarken) */
    free(s_opaque); free(s_opaqueVertices);
    s_opaqueMesh=NULL; s_opaqueWrap=NULL; s_opaque=NULL; s_opaqueVertices=NULL;
    s_opaqueDrawn=0;
	for (int i = 0; i < MAX_ROOMS; i++) {
		for (int k = 0; k < 2; k++) {
			gevrStaticMeshFree(s_room[i].mesh[k]);
			free(s_room[i].wrap[k]);
			s_room[i].mesh[k] = NULL;
			s_room[i].wrap[k] = NULL;
		}
		free(s_room[i].opa);
		free(s_room[i].xlu);
		free(s_room[i].vtxOpa);
		free(s_room[i].vtxXlu);
		s_room[i].opa = NULL;
		s_room[i].xlu = NULL;
		s_room[i].vtxOpa = NULL;
		s_room[i].vtxXlu = NULL;
		s_room[i].has = 0;
		s_room[i].hidden = 0;
		s_room[i].byPortals = 0;
	}
	s_nrooms = 0;
	s_ok = 0;
	s_keptRoom = -1;
}

/* GoldenEye's doors as the setup places them, in world units: the bound
 * pad's box and, for one that slides, its move fully open (door7F0526EC:
 * (frac, unkac, unkb0) a unit of openPosition, maxFrac of them) */
static int levelDoors(struct beanDoorIn **out)
{
	struct beanDoorIn *doors = NULL;
	int n = 0, cap = 0;
	*out = NULL;
	if (!g_CurrentSetup.propDefs || !g_CurrentSetup.boundpads)
		return 0;
	for (PropDefHeaderRecord *p = g_CurrentSetup.propDefs; p->type != PROPDEF_END;
	     p = (PropDefHeaderRecord *)((u32 *)p + sizepropdef(p))) {
		DoorRecord *door = (DoorRecord *)p;
		const BoundPadRecord *pad;
		struct beanDoorIn *d;
		if (p->type != PROPDEF_DOOR || !door->prop || door->pad < 0)
			continue;
		if (n == cap) {
			int grown = cap ? cap * 2 : 64;
			struct beanDoorIn *g = realloc(doors, (size_t)grown * sizeof(*g));
			if (!g)
				break;
			doors = g;
			cap = grown;
		}
		pad = &g_CurrentSetup.boundpads[door->pad];
		d = &doors[n++];
		memset(d, 0, sizeof(*d));
		for (int k = 0; k < 3; k++) {
			d->pos[k] = pad->pos.f[k];
			d->up[k] = pad->up.f[k];
			d->look[k] = pad->look.f[k];
		}
		d->lo[0] = pad->bbox.xmin;
		d->hi[0] = pad->bbox.xmax;
		d->lo[1] = pad->bbox.ymin;
		d->hi[1] = pad->bbox.ymax;
		d->lo[2] = pad->bbox.zmin;
		d->hi[2] = pad->bbox.zmax;
		if (door->doorType == DOORTYPE_SLIDING || door->doorType == DOORTYPE_FLEXI1 ||
		    door->doorType == DOORTYPE_FLEXI2 || door->doorType == DOORTYPE_FLEXI3 ||
		    door->doorType == DOORTYPE_VERTICAL) {
			d->move[0] = door->frac * door->maxFrac;
			d->move[1] = door->unkac * door->maxFrac;
			d->move[2] = door->unkb0 * door->maxFrac;
		}
	}
	*out = doors;
	return n;
}

static void fitLevelOpenings(struct gevrBeanMesh *mesh, const struct beanStage *st)
{
	struct beanDoorIn *doors = NULL;
	const int ndoors = levelDoors(&doors);
	float (*ge)[3][3] = NULL;
	int nge = 0;
	if (ndoors > 0 && s_referenceCount > 0)
		ge = malloc((size_t)s_referenceCount * sizeof(*ge));
	for (int i = 0; ge && i < s_referenceCount; i++) {
		if (!s_reference[i].opaque)
			continue;
		for (int v = 0; v < 3; v++)
			for (int k = 0; k < 3; k++)
				ge[nge][v][k] = s_reference[i].p[v][k] * room_data_float2;
		nge++;
	}
	const struct beanDoorGe geTris = { (const float (*)[3][3])ge, nge };
	const struct beanDoorResult r = beanFitDoorways(mesh, st->scale, doors, ndoors, &geTris);
	if (r.doors)
		GEVR_BEAN_LOG("gevr bean: %s doorways: %d doors, %d vertices pulled onto their edges, %d reveals, "
		              "%d slot triangles over %d doors\n", st->bg, r.doors, r.moved, r.reveals, r.slotTris, r.slotDoors);
	free(ge);
	free(doors);
	int faded, strips;
	beanCloseLevelHoles(mesh, st->scale, &faded, &strips);
	if (faded || strips)
		GEVR_BEAN_LOG("gevr bean: %s holes: %d faded faces made solid, %d strips to a wall's foot\n",
		              st->bg, faded, strips);
}

/*
 * DAB gebeanstage.c keptOutOf[] (port/licenses/dabs-mod.txt). Aztec's room 45
 * is the closet under the exhaust hatch, a box of metal panels with the body
 * armour on its floor. Bean runs the stone wall of room 18 beside it down the
 * middle of the box, so a mossy slab stood across the closet by the armour,
 * the sky over its top edge; GoldenEye shows the whole closet. What of
 * another room stands inside the closet's box (any of the points of a 12-step
 * grid across a face, 4 units in from GoldenEye's own walls of it) is dealt
 * to room 18, and the two rooms, which overlap, are drawn from their own
 * lists, out of the merged world, only where GoldenEye's portal walk reaches
 * them: the wall from room 18 (left out, room 18 had no back wall and the
 * guidance room's glass showed the closet's box and the rooms past it), the
 * closet from inside it. While the camera is in the closet, the props of the
 * rooms beside it within its box are left out as well
 * (gevrBeanWorldHidesProp()): Bean's monitors on room 18's wall hang into the
 * closet's top, and its white post at pad 139 stands through the east wall.
 */
static const struct {
	short room, to;
	int stage;
} s_keptOutOf[] = {
	{ 45, 18, LEVELID_AZTEC },
};
#define KEPTOUT_MARGIN 4.0f       /* inside the box by this much: its walls are its neighbours' too */
#define KEPTOUT_STEPS 12          /* points across a face, a side */
#define KEPTOUT_PROPREACH 48.0f   /* a prop of another room this far outside the box across is kept out too */

static int keepOutOfRooms(const struct gevrBeanMesh *mesh, float fileScale, int *assign, int nrooms)
{
	int count = 0;
	s_keptRoom = -1;
	for (size_t i = 0; i < sizeof(s_keptOutOf) / sizeof(s_keptOutOf[0]); i++) {
		const int r = s_keptOutOf[i].room, to = s_keptOutOf[i].to;
		float lo[3] = { 1e30f, 1e30f, 1e30f }, hi[3] = { -1e30f, -1e30f, -1e30f };
		int own = 0, moved = 0;
		if (s_keptOutOf[i].stage != s_stage || r <= 0 || r >= nrooms || to <= 0 || to >= nrooms)
			continue;
		for (int j = 0; j < s_referenceCount; j++) {
			if (s_reference[j].room != r)
				continue;
			own++;
			for (int v = 0; v < 3; v++)
				for (int k = 0; k < 3; k++) {
					lo[k] = s_reference[j].p[v][k] < lo[k] ? s_reference[j].p[v][k] : lo[k];
					hi[k] = s_reference[j].p[v][k] > hi[k] ? s_reference[j].p[v][k] : hi[k];
				}
		}
		if (!own)
			continue;
		for (int t = 0; t < mesh->ntris; t++) {
			const struct gevrBeanTri *tri = &mesh->tris[t];
			int inside = 0;
			if (assign[t] <= 0 || assign[t] == r || assign[t] == to)
				continue;
			/* any of it inside: the slab's middle is above the closet's ceiling,
			 * its lower edge well inside */
			for (int a = 0; a <= KEPTOUT_STEPS && !inside; a++)
				for (int b = 0; a + b <= KEPTOUT_STEPS && !inside; b++) {
					const float wa = (float)a / KEPTOUT_STEPS, wb = (float)b / KEPTOUT_STEPS;
					int in = 1;
					for (int k = 0; k < 3 && in; k++) {
						const float q = (tri->p[k] * wa + tri->p[3 + k] * wb + tri->p[6 + k] * (1.0f - wa - wb)) * fileScale;
						in = q > lo[k] + KEPTOUT_MARGIN && q < hi[k] - KEPTOUT_MARGIN;
					}
					inside = in;
				}
			if (inside) {
				assign[t] = to;
				moved++;
			}
		}
		s_room[r].byPortals = s_room[to].byPortals = 1;
		s_keptRoom = r;
		for (int k = 0; k < 3; k++) {
			s_keptLo[k] = lo[k];
			s_keptHi[k] = hi[k];
		}
		count += moved;
		GEVR_BEAN_LOG("gevr bean: room %d's box (world) %.0f %.0f %.0f .. %.0f %.0f %.0f: %d triangles of other rooms in it dealt to room %d; rooms %d and %d drawn by the portal walk\n",
		              r, lo[0] * room_data_float2, lo[1] * room_data_float2, lo[2] * room_data_float2, hi[0] * room_data_float2,
		              hi[1] * room_data_float2, hi[2] * room_data_float2, moved, to, r, to);
	}
	return count;
}

/* a prop at pos, in rooms[] (0xff ends it), left out of this view: the camera
 * is in a kept-out room (keepOutOfRooms()) and the prop, of another room, at
 * its box or within KEPTOUT_PROPREACH of it across (DAB gebeanStageHidesProp()) */
int gevrBeanWorldHidesProp(const float pos[3], const unsigned char *rooms, int nrooms, int camRoom)
{
	if (!s_ok || s_keptRoom <= 0 || camRoom != s_keptRoom || !pos)
		return 0;
	for (int k = 0; rooms && k < nrooms && rooms[k] != 0xff; k++)
		if (rooms[k] == s_keptRoom)
			return 0;
	for (int k = 0; k < 3; k++) {
		const float reach = k == 1 ? -KEPTOUT_MARGIN : KEPTOUT_PROPREACH;
		if (!(pos[k] > s_keptLo[k] * room_data_float2 - reach && pos[k] < s_keptHi[k] * room_data_float2 + reach))
			return 0;
	}
	return 1;
}

static int buildStage(int stage)
{
	const struct beanStage *st;
	char source[96], path[192];
	struct gevrBeanMesh *mesh = NULL;
	int *assign;
	int nrooms;
	float fileScale;
	clearRooms();
	s_stage = stage;
	st = stageOf(stage);
	if (!st || !st->bg) {
		s_tried = 1;
		return 0;
	}
	fileScale = beanRoomFileScale(st->scale, room_data_float2);
	if (!(fileScale > 0.0f))
		return 0;
	/* Room boxes are filled at level load. If this draw is early, try again. */
	if (!ptr_bgdata_room_fileposition_list || g_MaxNumRooms <= 0) {
		return 0;
	}
	nrooms = g_MaxNumRooms;
	if (nrooms > MAXROOMCOUNT) {
		nrooms = MAXROOMCOUNT;
	}
	if (nrooms > MAX_ROOMS) {
		nrooms = MAX_ROOMS;
	}
	{
		int anyBox = 0;
		for (int r = 0; r < nrooms; r++) {
			if (g_BgRoomInfo[r].maxbounds.x > g_BgRoomInfo[r].minbounds.x
					&& g_BgRoomInfo[r].maxbounds.y > g_BgRoomInfo[r].minbounds.y
					&& g_BgRoomInfo[r].maxbounds.z > g_BgRoomInfo[r].minbounds.z) {
				anyBox = 1;
				break;
			}
		}
		if (!anyBox) {
			return 0;
		}
	}
	snprintf(source, sizeof(source), "new/background/%s", st->bg);
	s_tried = 1;
	if (!findFile(source, path, sizeof(path)) || !gevrBeanLoad(path, 1, &mesh) || !mesh) {
		GEVR_BEAN_LOG("gevr bean: no HD rooms for stage %d (%s)\n", stage, st->bg);
		return 0;
	}
	/* Mirror Mode's level is GoldenEye's about x = 0: so are the release's rooms,
	 * before they are matched to the cartridge's room triangles */
	if (gevrRomMirrored())
		gevrBeanMeshMirrorX(mesh);
	mesh->roomMaterial = 1;
    mesh->worldVertices = 1;
    /* The world's triangles a run (a draw). 4096: at the Dam's start, 24 fewer
     * draws a frame than 2048, the eyes' GPU time no higher (2026-10-14).
     * files/gevr_world_run.txt sets it, for A/B. */
    int worldRun = 4096;
    {
        FILE *f = gevrBeanControlOpen("gevr_world_run.txt");
        if (f) {
            if (fscanf(f, "%d", &worldRun) != 1 || worldRun < 256 || worldRun > 65536) worldRun = 4096;
            fclose(f);
        }
        GEVR_BEAN_LOG("gevr bean controls: world_run=%d", worldRun);
    }
	if (!roomReferenceBuild(nrooms))
		GEVR_BEAN_LOG("gevr bean: no reference room triangles, using room boxes\n");
	/* the doorways round GoldenEye's doors, then the holes left in the ground,
	 * before the triangles are dealt to the rooms */
	fitLevelOpenings(mesh, st);
	/* DAB gebeanstage.c build() (cc90f36): a black face of a plain draw (no
	 * UV, no picture of its own) that the loader's paintLevelUnlit() lit is
	 * black again where GoldenEye's own surface under it is dark too, so the
	 * black was paint and not light Rare left out (DAB: Bunker's monitor wall).
	 * DAB's brightness weighs the vertices by the picture's colour; here it
	 * is the brightest vertex colour, of the triangle within 32 of DAB's units.
	 * A black plain decal (left black by the loader) lies on a whole surface:
	 * left out where GoldenEye's is lit (Aztec's slab by the shuttle bay),
	 * marked painted here and dealt to no room below. */
	if (s_referenceCount) {
		const float reach = 32.0f * fileScale / st->scale;
		int kept = 0, dropped = 0;
		for (int i = 0; i < mesh->ntris; i++) {
			struct gevrBeanTri *t = &mesh->tris[i];
			float c[3], d = reach * reach;
			int ref = -1;
			if (!(t->flags & GEVR_BEAN_F_UNLITPLAIN))
				continue;
			for (int j = 0; j < 3; j++)
				c[j] = (t->p[j] + t->p[3 + j] + t->p[6 + j]) * fileScale / 3.0f;
			roomReferenceNearestTri(0, c, &d, &ref);
			if (ref >= 0 && s_reference[ref].shade < 4) {
				for (int k = 0; k < 3; k++)
					t->rgba[k * 4] = t->rgba[k * 4 + 1] = t->rgba[k * 4 + 2] = 0;
				kept++;
			} else if (t->flags & GEVR_BEAN_F_DECAL) {
				t->flags |= GEVR_BEAN_F_PAINTED;
				dropped++;
			}
		}
		if (dropped)
			GEVR_BEAN_LOG("gevr bean: %s: %d black decals of draws with no UV or picture left out over GoldenEye's lit surface\n",
			              st->bg, dropped);
		if (kept)
			GEVR_BEAN_LOG("gevr bean: %s: %d black faces of draws with no UV or picture kept black over GoldenEye's dark surface\n",
			              st->bg, kept);
	}
	assign = malloc((size_t)mesh->ntris * sizeof(int));
	uint8_t *nofog = calloc((size_t)mesh->ntris + 1, 1);   /* DAB tri->nofog */
	if (!assign || !nofog) {
		free(assign);
		free(nofog);
		gevrBeanMeshFree(mesh);
		return 0;
	}
	/* from the cache when it is the same level, mesh and rooms (#269) */
	uint64_t roomsKey = 0;
	int roomsCached = 0;
	if (gevrBeanCacheStamp) {
		roomsKey = beanCacheTris(mesh, 1469598103934665603ull);
		roomsKey = beanCacheFnv(roomsKey, &stage, sizeof(stage));
		roomsKey = beanCacheFnv(roomsKey, &nrooms, sizeof(nrooms));
		roomsKey = beanCacheFnv(roomsKey, &fileScale, sizeof(fileScale));
		const int mirrored = gevrRomMirrored() != 0, tree = s_tree != NULL;
		roomsKey = beanCacheFnv(roomsKey, &mirrored, sizeof(mirrored));
		roomsKey = beanCacheFnv(roomsKey, &tree, sizeof(tree));
		for (int r = 0; r < nrooms; r++)
			roomsKey = beanCacheFnv(beanCacheFnv(roomsKey, &g_BgRoomInfo[r].minbounds, sizeof(g_BgRoomInfo[r].minbounds)), &g_BgRoomInfo[r].maxbounds, sizeof(g_BgRoomInfo[r].maxbounds));
		size_t size = 0;
		uint8_t *blob = (uint8_t *)beanCacheRead(path, "rooms", roomsKey, &size);
		if (blob && size == (size_t)mesh->ntris * (sizeof(int) + 1)) {
			memcpy(assign, blob, (size_t)mesh->ntris * sizeof(int));
			memcpy(nofog, blob + (size_t)mesh->ntris * sizeof(int), (size_t)mesh->ntris);
			roomsCached = 1;
			GEVR_BEAN_LOG("gevr bean: %s: rooms from the cache\n", st->bg);
		}
		free(blob);
	}
	for (int i = 0; i < mesh->ntris && !roomsCached; i++) {
		const struct gevrBeanTri *t = &mesh->tris[i];
		float c[3] = { 0, 0, 0 };
		for (int k = 0; k < 3; k++) {
			c[0] += t->p[k * 3] * fileScale;
			c[1] += t->p[k * 3 + 1] * fileScale;
			c[2] += t->p[k * 3 + 2] * fileScale;
		}
		c[0] /= 3.0f;
		c[1] /= 3.0f;
		c[2] /= 3.0f;
		/* GoldenEye's nearest triangle gives the room (roomFor()) and whether
		 * it is drawn without fog */
		int ref = -1;
		float distance = 1e30f;
		if (s_tree && !(t->flags & GEVR_BEAN_F_PAINTED))
			roomReferenceNearestTri(0, c, &distance, &ref);
		assign[i] = (t->flags & GEVR_BEAN_F_PAINTED) ? -1 : ref >= 0 ? s_reference[ref].room : roomFor(c, nrooms);
		nofog[i] = ref >= 0 && s_reference[ref].nofog;
	}
	if (gevrBeanCacheStamp && !roomsCached) {
		const size_t size = (size_t)mesh->ntris * (sizeof(int) + 1);
		uint8_t *blob = (uint8_t *)malloc(size);
		if (blob) {
			memcpy(blob, assign, (size_t)mesh->ntris * sizeof(int));
			memcpy(blob + (size_t)mesh->ntris * sizeof(int), nofog, (size_t)mesh->ntris);
			beanCacheWrite(path, "rooms", roomsKey, blob, size);
			free(blob);
		}
	}
	markBackdrop(mesh, fileScale, nrooms, nofog);
	{
		int nofogs = 0;
		for (int i = 0; i < mesh->ntris; i++)
			nofogs += nofog[i] == 1;
		if (nofogs)
			GEVR_BEAN_LOG("gevr bean: %s: %d triangles over GoldenEye's surfaces drawn without fog\n", st->bg, nofogs);
	}
	markOverlaps(mesh, fileScale);
	markWater(mesh, fileScale);
	markUnderSea(mesh, fileScale);
	keepOutOfRooms(mesh, fileScale, assign, nrooms);
	shotsBuild(mesh, assign, st->scale);
	for (int r = 0; r < nrooms; r++) {
		int nopa = 0, nxlu = 0;
		gevrBeanLoadingTick(0.6f + 0.2f * (float)r / (float)nrooms);
		struct gevrEmitVert *opa = NULL, *xlu = NULL;
		void *gdl = NULL, *vtx = NULL;
		for (int i = 0; i < mesh->ntris; i++) {
			if (assign[i] != r) {
				continue;
			}
			if (mesh->tris[i].alpha < 255) {
				nxlu++;
			} else {
				nopa++;
			}
		}
		if (nopa + nxlu == 0) {
			if (r > 0)
				s_room[r].hidden = hiddenRoom(mesh, fileScale, r);
			continue;
		}
		if (nopa) {
			opa = malloc((size_t)nopa * 3 * sizeof(*opa));
		}
		if (nxlu) {
			xlu = malloc((size_t)nxlu * 3 * sizeof(*xlu));
		}
		if ((nopa && !opa) || (nxlu && !xlu)) {
			free(opa);
			free(xlu);
			continue;
		}
		nopa = nxlu = 0;
		for (int k = 0; k < 3; k++) {
			s_room[r].lo[k] = 1e30f;
			s_room[r].hi[k] = -1e30f;
		}
		for (int i = 0; i < mesh->ntris; i++) {
			const struct gevrBeanTri *t;
			struct gevrEmitVert *dst;
			int slot;
			if (assign[i] != r) {
				continue;
			}
			t = &mesh->tris[i];
			if (t->alpha < 255) {
				dst = xlu;
				slot = nxlu;
				nxlu += 3;
			} else {
				dst = opa;
				slot = nopa;
				nopa += 3;
			}
			for (int k = 0; k < 3; k++) {
				float p[3];
				p[0] = t->p[k * 3] * fileScale - ptr_bgdata_room_fileposition_list[r].pos.x;
				p[1] = t->p[k * 3 + 1] * fileScale - ptr_bgdata_room_fileposition_list[r].pos.y;
				p[2] = t->p[k * 3 + 2] * fileScale - ptr_bgdata_room_fileposition_list[r].pos.z;
				fillVert(mesh, t, &dst[slot + k], p, k);
				dst[slot + k].nofog = nofog[i];
				for (int j = 0; j < 3; j++) {
					const float world = p[j] + ptr_bgdata_room_fileposition_list[r].pos.f[j];
					if (world < s_room[r].lo[j])
						s_room[r].lo[j] = world;
					if (world > s_room[r].hi[j])
						s_room[r].hi[j] = world;
				}
			}
		}
		/* DAB draws the level two-sided: decks and stair undersides
		 * are single sheets in Bean. */
		if (nopa && gevrBeanEmitList(mesh, opa, nopa, 0, 0, 0, 1, &gdl, &vtx)) {
			s_room[r].opa = gdl;
			s_room[r].vtxOpa = vtx;
			s_room[r].has = 1;
			s_room[r].wrap[0] = meshWrap(gdl, &s_room[r].mesh[0]);
		}
		gdl = NULL;
		vtx = NULL;
		if (nxlu && gevrBeanEmitList(mesh, xlu, nxlu, 0, 0, 0, 1, &gdl, &vtx)) {
			s_room[r].xlu = gdl;
			s_room[r].vtxXlu = vtx;
			s_room[r].has = 1;
			/* Dam's valley room holds 16,000 tree triangles of one picture: at 512
			 * a run they were 32 draws a frame; the opaque world's size (below)
			 * makes them a few. The runs keep their order, which blending needs. */
			s_meshRunTris = worldRun;
			s_room[r].wrap[1] = meshWrap(gdl, &s_room[r].mesh[1]);
			s_meshRunTris = 512;
		}
		free(opa);
		free(xlu);
	}
    /* Opaque depth-writing geometry can share materials across room boundaries.
     * Transparent surfaces keep the game's room order below. A diagnostic marker
     * retains the old room submission for a controlled comparison. */
    FILE *control=gevrBeanControlOpen("gevr_room_draws.txt");
    int merge=1;
    if (control) { int mode=1; if (fscanf(control,"%d",&mode)==1) merge=mode!=0; fclose(control); }
    GEVR_BEAN_LOG("gevr bean room-draws: %d\n",merge);
    if (merge) {
        int count=0;
        for (int i=0;i<mesh->ntris;i++) if (assign[i]>=0 && !s_room[assign[i]].byPortals && mesh->tris[i].alpha==255) count+=3;
        struct gevrEmitVert *opaque=malloc((size_t)count*sizeof(*opaque));
        if (opaque && count) {
            int vi=0;
            for (int i=0;i<mesh->ntris;i++) if (assign[i]>=0 && !s_room[assign[i]].byPortals && mesh->tris[i].alpha==255)
                for (int k=0;k<3;k++) {
                    float p[3]; for (int j=0;j<3;j++) p[j]=mesh->tris[i].p[k*3+j]*fileScale;
                    fillVert(mesh,&mesh->tris[i],&opaque[vi],p,k);
                    opaque[vi++].nofog=nofog[i];
                }
            mesh->spatialSort=1;
            void *list=NULL;
            if (gevrBeanEmitList(mesh,opaque,count,0,0,0,1,&list,&s_opaqueVertices)) {
                s_opaque=list;
                /* Dam's two landscape pictures hold 64,000 of its 90,000 opaque
                 * triangles: at 512 a run they made 126 of 234 draws, and a wide
                 * view's draw calls cost a turning frame up to 13 ms on Quest 3.
                 * 2048 cut its p99 by 2.9 ms with no GPU cost (tools/perf). */
                s_meshRunTris=worldRun;
                s_opaqueWrap=meshWrap(s_opaque,&s_opaqueMesh);
                s_meshRunTris=512;
                /* CPU replay of the float list is also complete if GPU creation fails. */
                for (int r=0;r<nrooms;r++) {
                    if (s_room[r].byPortals) continue;   /* drawn on its own (keepOutOfRooms) */
                    gevrStaticMeshFree(s_room[r].mesh[0]); free(s_room[r].wrap[0]);
                    free(s_room[r].opa); free(s_room[r].vtxOpa);
                    s_room[r].mesh[0]=NULL; s_room[r].wrap[0]=NULL;
                    s_room[r].opa=NULL; s_room[r].vtxOpa=NULL;
                }
                GEVR_BEAN_LOG("gevr bean: opaque world %d tris, %u spatial material chunks\n",
                              count/3,s_opaqueMesh ? s_opaqueMesh->numRuns : 0);
            }
        }
        free(opaque);
    }
    /* GoldenEye's own triangles stay for the camera test (gevrBeanWorldBeginView) */
    free(assign);
    free(nofog);
    {
        FILE *force = gevrBeanControlOpen("gevr_bean_roomcull.txt");
        s_cullForce = 0;
        if (force) {
            if (fscanf(force, "%d", &s_cullForce) != 1) s_cullForce = 0;
            fclose(force);
        }
        GEVR_BEAN_LOG("gevr bean: %d of GoldenEye's room triangles for the camera test, roomcull=%d\n",
                      s_referenceCount, s_cullForce);
    }
	{
		float lo[3] = { 1e30f, 1e30f, 1e30f }, hi[3] = { -1e30f, -1e30f, -1e30f };
		for (int r = 0; r < nrooms; r++)
			for (int k = 0; s_room[r].has && k < 3; k++) {
				lo[k] = fminf(lo[k], s_room[r].lo[k]);
				hi[k] = fmaxf(hi[k], s_room[r].hi[k]);
			}
		s_reachWorld = 0.0f;
		if (lo[0] <= hi[0]) {
			const float dx = hi[0] - lo[0], dy = hi[1] - lo[1], dz = hi[2] - lo[2];
			s_reachWorld = sqrtf(dx * dx + dy * dy + dz * dz) * room_data_float2 * 1.05f;
		}
	}
	s_nrooms = nrooms;
	s_ok = 1;
	GEVR_BEAN_LOG("gevr bean: HD rooms %s, %d tris across %d rooms, native scale %.8f\n", st->bg, mesh->ntris, nrooms, fileScale);
	gevrBeanMeshFree(mesh);
	return 1;
}

int gevrBeanRoomFog(float line[2], uint8_t rgb[3])
{
    if (!s_ok || !gevrBeanIsEnabled() || !g_CurrentPlayer) return 0;
    float share, fraction; uint8_t target[3];
    fogGetBeanTransition(&share,&fraction,target);
    /* Dam's fog reaches further the lower the camera stands (DAB gebeanStageFog():
     * cam_pos.y, the player's own but in the opening and closing shots). The
     * release's heights (2782, 9161) are its own world's: the room table's units,
     * Dam's file scale being 1 - DAB's conversion moved them up by 13219 to fit
     * Dam's -7418..8914 in sixteen bits. current_model_pos is the camera's room's
     * origin in world units, neither the camera nor the release's space. */
    const coord3d *camera = bondviewGetCurrentPlayersPosition();
    const float height = camera && room_data_float2 > 0.0f ? camera->y / room_data_float2 : 0.0f;
    return gevrBeanFogLineTransition(baseStage(s_stage), height,
                           bgGetLevelVisibilityScale(), communityEdition(),share,fraction,target,line,rgb);
}

static int ensureStage(void)
{
	int stage = (int)bossGetStageNum();
	if (s_tried && s_stage == stage) {
		return s_ok;
	}
	return buildStage(stage);
}

static void clearSky(void)
{
	gevrStaticMeshFree(s_skyMesh);
	free(s_skyWrap);
	s_skyMesh = NULL;
	s_skyWrap = NULL;
	free(s_skyGdl);
	free(s_skyVtx);
	s_skyGdl = NULL;
	s_skyVtx = NULL;
	s_skyOk = 0;
}

#include "gevr_bean_sky.inc.h"

static int buildSky(int stage)
{
	const struct beanStage *st;
	const char *name;
	char source[96], path[192];
	struct gevrBeanMesh *mesh = NULL;
	struct gevrEmitVert *ev = NULL;
	float extent = 0, k;
	void *gdl = NULL, *vtx = NULL;
	clearSky();
	s_skyStage = stage;
	s_skyTried = 1;
	st = stageOf(stage);
	if (!st) {
		return 0;
	}
	name = communityEdition() ? st->skyCe : st->skyRelease;
	if (!name) {
		return 0;
	}
	snprintf(source, sizeof(source), "new/skydome/%s", name);
	if (!findFile(source, path, sizeof(path)) || !gevrBeanLoad(path, 1, &mesh) || !mesh) {
		GEVR_BEAN_LOG("gevr bean: no HD sky %s\n", name);
		return 0;
	}
	/* its horizon with the level's, in Mirror Mode */
	if (gevrRomMirrored())
		gevrBeanMeshMirrorX(mesh);
	for (int i = 0; i < mesh->ntris; i++) {
		for (int c = 0; c < 3; c++) {
			float x = mesh->tris[i].p[c * 3];
			float y = mesh->tris[i].p[c * 3 + 1];
			float z = mesh->tris[i].p[c * 3 + 2];
			float r = sqrtf(x * x + y * y + z * z);
			if (r > extent) {
				extent = r;
			}
		}
	}
	if (!(extent > 0.0f)) {
		gevrBeanMeshFree(mesh);
		return 0;
	}
	mesh->roomMaterial=2; /* sky: retain cleared fog */
	beanSkyMaterials(mesh);
	k = SKY_EXTENT / extent;
	ev = malloc((size_t)mesh->ntris * 3 * sizeof(*ev));
	if (!ev) {
		gevrBeanMeshFree(mesh);
		return 0;
	}
	for (int i = 0; i < mesh->ntris; i++) {
		for (int c = 0; c < 3; c++) {
			float p[3];
			p[0] = mesh->tris[i].p[c * 3] * k;
			p[1] = mesh->tris[i].p[c * 3 + 1] * k;
			p[2] = mesh->tris[i].p[c * 3 + 2] * k;
			fillVert(mesh, &mesh->tris[i], &ev[i * 3 + c], p, c);
			ev[i * 3 + c].decal = 0;
		}
	}
	if (!gevrBeanEmitList(mesh, ev, mesh->ntris * 3, 0, 0, 0, 0, &gdl, &vtx)) {
		free(ev);
		gevrBeanMeshFree(mesh);
		return 0;
	}
	free(ev);
	s_skyGdl = gdl;
	s_skyVtx = vtx;
	s_skyWrap = meshWrap(s_skyGdl, &s_skyMesh);
	s_skyOk = 1;
	GEVR_BEAN_LOG("gevr bean: HD sky %s, %d tris, extent %.0f\n", name, mesh->ntris, extent);
	gevrBeanMeshFree(mesh);
	return 1;
}

int gevrBeanWorldPrepare(void)
{
	gevrBeanLoadingTick(0.05f);
	ensureSky();
	gevrBeanLoadingTick(0.15f);
	return ensureStage();
}

static int ensureSky(void)
{
	int stage = (int)bossGetStageNum();
	if (s_skyTried && s_skyStage == stage) {
		return s_skyOk;
	}
	return buildSky(stage);
}

void gevrBeanWorldReset(void)
{
	clearRooms();
	clearSky();
	s_stage = -9999;
	s_tried = 0;
	s_skyStage = -9999;
	s_skyTried = 0;
}

void *gevrBeanWorldRoom(void *gdl, int room, int *replaced)
{
	int xlu = 0;
	Gfx *g, *list;
	if (replaced) {
		*replaced = 0;
	}
	if (room >= 100000) {
		xlu = 1;
		room -= 100000;
	}
	if (!ensureStage() || room < 0 || room >= s_nrooms || (!s_room[room].has && !s_room[room].hidden)) {
		return gdl;
	}
	if (replaced) {
		*replaced = 1;   /* a hidden room's GoldenEye list is not drawn either */
	}
    if (!xlu && s_opaque) {
        /* a room drawn on its own (keepOutOfRooms): under bg.c's matrix for
         * the room and its scissor, ahead of the merged world's */
        g = gdl;
        if (s_room[room].byPortals && s_room[room].opa)
            gSPDisplayList(g++, s_room[room].wrap[0] ? s_room[room].wrap[0] : s_room[room].opa);
        if (s_opaqueDrawn || !g_CurrentPlayer) return g;
        Mtx *packed=dynAllocateMatrix();
        if (!packed) return g;
        Mtxf matrix; memset(&matrix,0,sizeof(matrix)); matrix.m[3][3]=1;
        for (int k=0;k<3;k++) {
            matrix.m[k][k]=room_data_float2;
            matrix.m[3][k]=-g_CurrentPlayer->current_model_pos.f[k];
        }
        matrix_4x4_f32_to_s32(matrix.m,packed->m);
        g=bgScissorCurrentPlayerViewDefault(g);
        gSPMatrix(g++,osVirtualToPhysical(packed),G_MTX_MODELVIEW|G_MTX_LOAD|G_MTX_NOPUSH);
        gSPDisplayList(g++,s_opaqueWrap ? s_opaqueWrap : s_opaque);
        s_opaqueDrawn=1;
        struct bbox2d box;
        if (bgGet2dBboxByRoomId(room,&box))
            g=bgScissorCurrentPlayerViewF(g,box.min.x,box.min.y,box.max.x,box.max.y);
        return g;
    }
    list = xlu ? s_room[room].xlu : s_room[room].opa;
	if (list && s_room[room].wrap[xlu])
		list = s_room[room].wrap[xlu];
	if (!list) {
		return gdl;
	}
	g = gdl;
	gSPDisplayList(g++, list);
	return g;
}

/* Whether the stage's Bean geometry has room `room`, and its bounds. */
/*
 * Shot-out lamps darken the HD rooms (DAB gelights.c geLightsDarken(), port/
 * licenses/dabs-mod.txt). GoldenEye's lightfixture.c still breaks the fixture
 * a shot meets and takes its N64 vertices, and those of every fixture
 * triangle within 100 units, to a quarter; with Bean drawing the room nothing
 * of that showed: the glass fell and the lamp stayed lit. Each triangle
 * GoldenEye darkens (darken_triangle_in_room) darkens Bean's vertices within
 * LAMP_REACH of it, to a quarter as well - by distance to the triangle, not a
 * box: a box square to the axes round a sloping ceiling strip took in the
 * wall under it (DAB, Bunker). The room's lists change in place and their
 * meshes are sent to the GPU again; the colours come back at the level's start
 * (lightFixtureInitTables).
 */
#define LAMP_REACH 16.0f   /* DAB FIXTURE_MARGIN, room units */

struct lampUndo {
	uint8_t *rgba;
	uint8_t rgb[3];
};
static struct lampUndo *s_lampUndo;
static int s_nlampUndo, s_lampUndoCap;

static float lampTriDistance2(const float p[3], const float t[3][3])
{
	/* the nearest point of the triangle (Ericson, Real-Time Collision Detection 5.1.5) */
	float ab[3], ac[3], ap[3], q[3];
	for (int k = 0; k < 3; k++) {
		ab[k] = t[1][k] - t[0][k];
		ac[k] = t[2][k] - t[0][k];
		ap[k] = p[k] - t[0][k];
	}
	const float d1 = ab[0] * ap[0] + ab[1] * ap[1] + ab[2] * ap[2];
	const float d2 = ac[0] * ap[0] + ac[1] * ap[1] + ac[2] * ap[2];
	float bp[3], cp[3];
	for (int k = 0; k < 3; k++) {
		bp[k] = p[k] - t[1][k];
		cp[k] = p[k] - t[2][k];
	}
	const float d3 = ab[0] * bp[0] + ab[1] * bp[1] + ab[2] * bp[2];
	const float d4 = ac[0] * bp[0] + ac[1] * bp[1] + ac[2] * bp[2];
	const float d5 = ab[0] * cp[0] + ab[1] * cp[1] + ab[2] * cp[2];
	const float d6 = ac[0] * cp[0] + ac[1] * cp[1] + ac[2] * cp[2];
	const float vc = d1 * d4 - d3 * d2, vb = d5 * d2 - d1 * d6, va = d3 * d6 - d5 * d4;
	if (d1 <= 0.0f && d2 <= 0.0f) {
		for (int k = 0; k < 3; k++) q[k] = t[0][k];
	} else if (d3 >= 0.0f && d4 <= d3) {
		for (int k = 0; k < 3; k++) q[k] = t[1][k];
	} else if (vc <= 0.0f && d1 >= 0.0f && d3 <= 0.0f) {
		const float v = d1 / (d1 - d3);
		for (int k = 0; k < 3; k++) q[k] = t[0][k] + v * ab[k];
	} else if (d6 >= 0.0f && d5 <= d6) {
		for (int k = 0; k < 3; k++) q[k] = t[2][k];
	} else if (vb <= 0.0f && d2 >= 0.0f && d6 <= 0.0f) {
		const float w = d2 / (d2 - d6);
		for (int k = 0; k < 3; k++) q[k] = t[0][k] + w * ac[k];
	} else if (va <= 0.0f && (d4 - d3) >= 0.0f && (d5 - d6) >= 0.0f) {
		const float w = (d4 - d3) / ((d4 - d3) + (d5 - d6));
		for (int k = 0; k < 3; k++) q[k] = t[1][k] + w * (t[2][k] - t[1][k]);
	} else {
		const float denom = 1.0f / (va + vb + vc), v = vb * denom, w = vc * denom;
		for (int k = 0; k < 3; k++) q[k] = t[0][k] + ab[k] * v + ac[k] * w;
	}
	float d = 0.0f;
	for (int k = 0; k < 3; k++) d += (p[k] - q[k]) * (p[k] - q[k]);
	return d;
}

static int lampDarkenOne(uint8_t *rgba)
{
	for (int i = 0; i < s_nlampUndo; i++)
		if (s_lampUndo[i].rgba == rgba)
			return 0;   /* already a quarter */
	if (s_nlampUndo == s_lampUndoCap) {
		int cap = s_lampUndoCap ? s_lampUndoCap * 2 : 256;
		struct lampUndo *grown = realloc(s_lampUndo, (size_t)cap * sizeof(*grown));
		if (!grown)
			return 0;
		s_lampUndo = grown;
		s_lampUndoCap = cap;
	}
	s_lampUndo[s_nlampUndo].rgba = rgba;
	memcpy(s_lampUndo[s_nlampUndo].rgb, rgba, 3);
	s_nlampUndo++;
	rgba[0] >>= 2;
	rgba[1] >>= 2;
	rgba[2] >>= 2;
	return 1;
}

/* count vertices, as Vtx or GevrWorldVertex, at off in the level's space */
static int lampDarkenVertices(void *verts, int count, int world, const float off[3], const float tri[3][3],
                              const float lo[3], const float hi[3])
{
	int n = 0;
	for (int i = 0; verts && i < count; i++) {
		float p[3];
		uint8_t *rgba;
		if (world) {
			GevrWorldVertex *w = &((GevrWorldVertex *)verts)[i];
			for (int k = 0; k < 3; k++) p[k] = w->p[k] + off[k];
			rgba = w->rgba;
		} else {
			Vtx *v = &((Vtx *)verts)[i];
			for (int k = 0; k < 3; k++) p[k] = v->v.ob[k] + off[k];
			rgba = v->v.cn;
		}
		if (p[0] < lo[0] || p[0] > hi[0] || p[1] < lo[1] || p[1] > hi[1] || p[2] < lo[2] || p[2] > hi[2])
			continue;
		if (lampTriDistance2(p, tri) <= LAMP_REACH * LAMP_REACH)
			n += lampDarkenOne(rgba);
	}
	return n;
}

/* the vertices a list loads (gevrBeanEmitList: G_VTX or G_GEVR_WORLD_VTX runs) */
static int lampDarkenList(Gfx *gdl, const float off[3], const float tri[3][3], const float lo[3], const float hi[3])
{
	int n = 0;
	for (Gfx *g = gdl; g; g++) {
		const unsigned op = (unsigned)(g->words.w0 >> 24) & 0xff;
		if (op == (G_ENDDL & 0xff))
			break;
		if (op == (G_VTX & 0xff))
			n += lampDarkenVertices((void *)g->words.w1, (int)((g->words.w0 & 0xffff) / sizeof(Vtx)), 0, off, tri, lo, hi);
		else if (op == G_GEVR_WORLD_VTX)
			n += lampDarkenVertices((void *)g->words.w1, (int)((g->words.w0 & 0xffff) / sizeof(GevrWorldVertex)), 1, off,
			                        tri, lo, hi);
	}
	return n;
}

static int lampDarkenMesh(GevrStaticMesh *m, const float off[3], const float tri[3][3], const float lo[3], const float hi[3])
{
	if (!m || !m->vertices)
		return 0;
	const int n = lampDarkenVertices((void *)m->vertices, (int)m->numVertices, m->worldVertices, off, tri, lo, hi);
	if (n)
		gfx_static_mesh_release(m);   /* drawn again from its vertices */
	return n;
}

void gevrBeanWorldLampDarken(int room, const short a[3], const short b[3], const short c[3])
{
	if (!s_ok || room < 0 || room >= s_nrooms)
		return;
	float tri[3][3], lo[3], hi[3];
	const short *v[3] = { a, b, c };
	for (int k = 0; k < 3; k++) {
		lo[k] = 1e30f;
		hi[k] = -1e30f;
		for (int i = 0; i < 3; i++) {
			tri[i][k] = v[i][k] + ptr_bgdata_room_fileposition_list[room].pos.f[k];
			if (tri[i][k] - LAMP_REACH < lo[k]) lo[k] = tri[i][k] - LAMP_REACH;
			if (tri[i][k] + LAMP_REACH > hi[k]) hi[k] = tri[i][k] + LAMP_REACH;
		}
	}
	static const float zero[3] = { 0.0f, 0.0f, 0.0f };
	int n = lampDarkenList(s_opaque, zero, tri, lo, hi) + lampDarkenMesh(s_opaqueMesh, zero, tri, lo, hi);
	for (int r = 0; r < s_nrooms; r++) {
		if (!s_room[r].has || s_room[r].lo[0] > hi[0] || s_room[r].hi[0] < lo[0] || s_room[r].lo[1] > hi[1] ||
		    s_room[r].hi[1] < lo[1] || s_room[r].lo[2] > hi[2] || s_room[r].hi[2] < lo[2])
			continue;
		const float *off = ptr_bgdata_room_fileposition_list[r].pos.f;
		n += lampDarkenList(s_room[r].opa, off, tri, lo, hi) + lampDarkenList(s_room[r].xlu, off, tri, lo, hi);
		n += lampDarkenMesh(s_room[r].mesh[0], off, tri, lo, hi) + lampDarkenMesh(s_room[r].mesh[1], off, tri, lo, hi);
	}
	if (n)
		GEVR_BEAN_LOG("gevr bean: a shot-out lamp darkened %d of Bean's vertices in room %d\n", n, room);
}

/* the level's start: every lamp lit again */
void gevrBeanWorldLampsReset(void)
{
	if (!s_nlampUndo)
		return;
	for (int i = 0; i < s_nlampUndo; i++)
		memcpy(s_lampUndo[i].rgba, s_lampUndo[i].rgb, 3);
	s_nlampUndo = 0;
	gfx_static_mesh_release(s_opaqueMesh);
	for (int r = 0; r < s_nrooms; r++) {
		gfx_static_mesh_release(s_room[r].mesh[0]);
		gfx_static_mesh_release(s_room[r].mesh[1]);
	}
}

/* a room hiddenRoom() left out, on the level built now (bg.c's shot test:
 * never builds one) */
/* the HD level as built draws room `room` and no water of its own (bg.c keeps
 * GoldenEye's water leaves then) */
int gevrBeanWorldRoomWantsWater(int room)
{
	return s_tried && s_ok && s_stage == (int)bossGetStageNum() && room > 0 && room < s_nrooms &&
	       s_room[room].has && !s_levelWater;
}

/*
 * How far the HD level reaches, in world units: its rooms' box's diagonal and a
 * twentieth, while it is served (DAB gebeanstage.c gebeanStageTickFar(): "at least
 * the length of the HD mesh's box, so that nothing of the level is ever cut").
 * 0 when it is not. bg.c's room and portal tests and bgfog.c's prop tests take
 * it, never the projection, so GoldenEye's own fog and the guards' sight stay.
 */
float gevrBeanWorldReach(void)
{
	return s_tried && s_ok && s_stage == (int)bossGetStageNum() ? s_reachWorld : 0.0f;
}

int gevrBeanWorldRoomHidden(int room)
{
	return s_tried && s_ok && s_stage == (int)bossGetStageNum() && room >= 0 && room < s_nrooms &&
	       s_room[room].hidden;
}

int gevrBeanWorldRoomBounds(int room, float lo[3], float hi[3])
{
	if (!ensureStage() || room < 0 || room >= s_nrooms || !s_room[room].has)
		return 0;
	for (int k = 0; k < 3; k++) {
		lo[k] = s_room[room].lo[k];
		hi[k] = s_room[room].hi[k];
	}
	return 1;
}

void *gevrBeanWorldSky(void *gdl, int *drew)
{
	Mtxf mf;
	Mtx *packed;
	f32 zrange[2];
	f32 scale;
	Gfx *g;
	if (drew) {
		*drew = 0;
	}
	if (!ensureSky() || !s_skyGdl) {
		return gdl;
	}
	packed = dynAllocateMatrix();
	if (!packed || !camGetWorldToScreenMtxf()) {
		return gdl;
	}
	mf = *camGetWorldToScreenMtxf();
	viGetZRange(zrange);
	scale = zrange[1] * 0.5f / SKY_EXTENT;
	if (!(scale > 0.0f)) {
		scale = 1.0f;
	}
	/* The dome sits at half the far plane: on Surface (far 2500, 20 units a
	 * metre) 60 m off, where the eyes' separation (the shader's constant clip
	 * x shift) put its cloud cap in a disc overhead that followed the player
	 * (v0.5.14-test.16 tester). Scaled whole, x, y, z and w alike, it projects
	 * to the same place with that shift SKY_AT_INFINITY times smaller: as far
	 * as a sky is, and out of reach of the late head-position correction too. */
#define SKY_AT_INFINITY 1024.0f
	for (int i = 0; i < 3; i++) {
		for (int j = 0; j < 3; j++) {
			mf.m[i][j] *= scale * SKY_AT_INFINITY;
		}
		mf.m[3][i] = 0.0f;
	}
	mf.m[3][3] *= SKY_AT_INFINITY;
	matrix_4x4_f32_to_s32(mf.m, packed->m);
	g = gdl;
	/*
	 * Under the dome's lowest ring: the level's own sky colour, as GoldenEye
	 * fills a level with no clouds (sky.c skyRender()), or with the Community
	 * Edition's storm over Surface 2 its dome's horizon, which its fog takes too
	 * (DAB gebeansky.c gebeanSkyRender(), gebeanSkyFogColour()). Unfilled it was
	 * the frame's clear colour, black below the dome where no mesh covers it.
	 */
	{
		struct CurrentEnvironmentRecord *env = fogGetCurrentEnvironmentp();
		uint8_t rgb[3] = { 0, 0, 0 };
		float end;
		if (env) {
			rgb[0] = (uint8_t)env->Red;
			rgb[1] = (uint8_t)env->Green;
			rgb[2] = (uint8_t)env->Blue;
		}
		if (communityEdition() && baseStage(s_stage) == 43)
			gevrBeanFogRange(43, 0.0f, 1, &end, rgb);
		gDPPipeSync(g++);
		gDPSetCycleType(g++, G_CYC_FILL);
		g = viSetFillColor(g, rgb[0], rgb[1], rgb[2]);
		gDPSetRenderMode(g++, G_RM_NOOP, G_RM_NOOP2);
		gDPFillRectangle(g++, viGetViewLeft(), viGetViewTop(), viGetViewLeft() + viGetViewWidth() - 1,
		                 viGetViewTop() + viGetViewHeight() - 1);
		gDPPipeSync(g++);
	}
	gSPMatrix(g++, osVirtualToPhysical(currentPlayerGetProjectionMatrix()),
		G_MTX_PROJECTION | G_MTX_LOAD | G_MTX_NOPUSH);
	gSPClearGeometryMode(g++, G_ZBUFFER | G_CULL_BOTH | G_LIGHTING | G_FOG);
	gSPMatrix(g++, osVirtualToPhysical(packed), G_MTX_MODELVIEW | G_MTX_LOAD | G_MTX_NOPUSH);
	gSPDisplayList(g++, s_skyWrap ? s_skyWrap : s_skyGdl);
	gSPSetGeometryMode(g++, G_ZBUFFER);
	if (drew) {
		*drew = 1;
	}
	return g;
}
