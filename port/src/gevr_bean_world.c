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
static Gfx *s_opaque, *s_opaqueWrap;
static void *s_opaqueVertices;
static GevrStaticMesh *s_opaqueMesh;
static int s_opaqueDrawn;

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

static const char *const s_roots[] = {
    "../texture-packs/ge007-bean/files",
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

/* DAB fitted scales in GoldenEye file units (no Perfect Dark recentering). */
static const struct beanStage s_stages[] = {
    {LEVELID_ARCHIVES, "archives", 1.072400836f, "archives", NULL},
    {LEVELID_CONTROL, "control", 1.616580623f, "control", NULL},
    {LEVELID_FACILITY, "facility", 0.531318270f, NULL, NULL},
    {LEVELID_AZTEC, "aztec", 1.475407422f, "aztec", NULL},
    {LEVELID_BASEMENT, "library", 0.573907333f, "library", NULL},
    {LEVELID_STACK, "library", 0.573907333f, "library", NULL},
    {LEVELID_LIBRARY, "library", 0.573907333f, "library", NULL},
    {LEVELID_CAVERNS, "cavern", 0.501072787f, "caves", "caverns"},
    {LEVELID_CRADLE, "cradle", 1.828668257f, "cradle", NULL},
    {LEVELID_EGYPT, "temple", 0.921010121f, "temple", "egyptian"},
    {LEVELID_DAM, "dam", 4.280101153f, "dam", "dam"},
    {LEVELID_DEPOT, "depot", 0.805820509f, "depot", NULL},
    {LEVELID_FRIGATE, "frigate", 0.821433927f, "frigate", "frigate"},
    {LEVELID_TEMPLE, "multitemple", 3.119410587f, "multitemple", "temple"},
    {LEVELID_JUNGLE, "jungle", 0.706146978f, NULL, NULL},
    {LEVELID_CAVES, "caves", 0.715800060f, "caves", NULL},
    {LEVELID_STREETS, "streets", 1.239411313f, "streets", NULL},
    {LEVELID_COMPLEX, "complex", 0.401740938f, "complex", "complex"},
    {LEVELID_RUNWAY, "runway", 0.508383572f, "runway", "runway"},
    {LEVELID_BUNKER1, "bunker", 0.429214082f, "bunker", "bunker"},
    {LEVELID_BUNKER2, "bunker2", 0.429214887f, "bunker2", "bunker2"},
    {LEVELID_SURFACE, "surface", 0.497385395f, "sf1", "surface"},
    {LEVELID_SURFACE2, "surface", 0.497385395f, "sf2", "surface2"},
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
	if (s_ce < 0) {
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

static void markWater(const struct gevrBeanMesh *mesh, float fileScale)
{
	struct waterMeasure ge, hd;
	float lo[3] = { 1e30f, 1e30f, 1e30f }, hi[3] = { -1e30f, -1e30f, -1e30f };
	int nwater = 0, marked = 0;
	memset(&ge, 0, sizeof(ge));
	memset(&hd, 0, sizeof(hd));
	gevrBeanWaterSetRate(0, 0, 0);
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
    s_cullOutside = 0;
    gevrStaticMeshFree(s_opaqueMesh); free(s_opaqueWrap);
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
	}
	s_nrooms = 0;
	s_ok = 0;
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
	assign = malloc((size_t)mesh->ntris * sizeof(int));
	if (!assign) {
		gevrBeanMeshFree(mesh);
		return 0;
	}
	if (!roomReferenceBuild(nrooms))
		GEVR_BEAN_LOG("gevr bean: no reference room triangles, using room boxes\n");
	for (int i = 0; i < mesh->ntris; i++) {
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
		assign[i] = roomFor(c, nrooms);
	}
	markWater(mesh, fileScale);
	for (int r = 0; r < nrooms; r++) {
		int nopa = 0, nxlu = 0;
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
        for (int i=0;i<mesh->ntris;i++) if (assign[i]>=0 && mesh->tris[i].alpha==255) count+=3;
        struct gevrEmitVert *opaque=malloc((size_t)count*sizeof(*opaque));
        if (opaque && count) {
            int vi=0;
            for (int i=0;i<mesh->ntris;i++) if (assign[i]>=0 && mesh->tris[i].alpha==255)
                for (int k=0;k<3;k++) {
                    float p[3]; for (int j=0;j<3;j++) p[j]=mesh->tris[i].p[k*3+j]*fileScale;
                    fillVert(mesh,&mesh->tris[i],&opaque[vi++],p,k);
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
    return gevrBeanFogLineTransition(baseStage(s_stage), g_CurrentPlayer->current_model_pos.y,
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
        if (s_opaqueDrawn || !g_CurrentPlayer) return gdl;
        Mtx *packed=dynAllocateMatrix();
        if (!packed) return gdl;
        Mtxf matrix; memset(&matrix,0,sizeof(matrix)); matrix.m[3][3]=1;
        for (int k=0;k<3;k++) {
            matrix.m[k][k]=room_data_float2;
            matrix.m[3][k]=-g_CurrentPlayer->current_model_pos.f[k];
        }
        matrix_4x4_f32_to_s32(matrix.m,packed->m);
        g=bgScissorCurrentPlayerViewDefault(gdl);
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
	for (int i = 0; i < 3; i++) {
		for (int j = 0; j < 3; j++) {
			mf.m[i][j] *= scale;
		}
		mf.m[3][i] = 0.0f;
	}
	matrix_4x4_f32_to_s32(mf.m, packed->m);
	g = gdl;
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
