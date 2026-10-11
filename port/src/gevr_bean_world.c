/*
 * Bean rooms and skies.
 *
 * A room vertex is stored in the file's own units, already relative to that
 * room: bean * stageScale - room.pos. The room matrix multiplies by the
 * level scale, so Dam (4.2801 * 0.23364) lands 1:1 with bg_dam. The conversion
 * offsets in the Perfect Dark fork are a recentering and are not applied.
 *
 * The sky is the camera's turn with the translation removed, scaled so the
 * farthest vertex sits at half the far plane. It is drawn instead of the
 * N64 sky plane.
 */

#include <ultra64.h>
#include <PR/gbi.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <bondconstants.h>

#include "bg.h"
#include "bondview.h"
#include "boss.h"
#include "dyn.h"
#include "fr.h"
#include "matrixmath.h"
#include "gevr_bean_int.h"

#define MAX_ROOMS 160
#define SKY_EXTENT 30000.0f

struct roomSlot {
	Gfx *opa;
	Gfx *xlu;
	void *vtxOpa;
	void *vtxXlu;
	int has;
};

static struct roomSlot s_room[MAX_ROOMS];
static int s_nrooms;
static int s_stage = -9999;
static int s_tried;
static int s_ok;

static Gfx *s_skyGdl;
static void *s_skyVtx;
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
	int cut = t->tex >= 0 && t->tex < mesh->ntex && mesh->texCut[t->tex];
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
	e->tex = t->tex;
	e->alpha = t->alpha;
	e->cut = (uint8_t)(t->alphatest || cut);
	e->decal = (t->flags & GEVR_BEAN_F_DECAL) ? 1 : 0;
}

#include "gevr_bean_rooms.inc.h"

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

static void clearRooms(void)
{
	for (int i = 0; i < MAX_ROOMS; i++) {
		free(s_room[i].opa);
		free(s_room[i].xlu);
		free(s_room[i].vtxOpa);
		free(s_room[i].vtxXlu);
		s_room[i].opa = NULL;
		s_room[i].xlu = NULL;
		s_room[i].vtxOpa = NULL;
		s_room[i].vtxXlu = NULL;
		s_room[i].has = 0;
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
	clearRooms();
	s_stage = stage;
	st = stageOf(stage);
	if (!st || !st->bg) {
		s_tried = 1;
		return 0;
	}
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
		fprintf(stderr, "gevr bean: no HD rooms for stage %d (%s)\n", stage, st->bg);
		return 0;
	}
	assign = malloc((size_t)mesh->ntris * sizeof(int));
	if (!assign) {
		gevrBeanMeshFree(mesh);
		return 0;
	}
	if (!roomReferenceBuild(nrooms))
		fprintf(stderr, "gevr bean: no reference room triangles, using room boxes\n");
	for (int i = 0; i < mesh->ntris; i++) {
		const struct gevrBeanTri *t = &mesh->tris[i];
		float c[3] = { 0, 0, 0 };
		for (int k = 0; k < 3; k++) {
			c[0] += t->p[k * 3] * st->scale;
			c[1] += t->p[k * 3 + 1] * st->scale;
			c[2] += t->p[k * 3 + 2] * st->scale;
		}
		c[0] /= 3.0f;
		c[1] /= 3.0f;
		c[2] /= 3.0f;
		assign[i] = roomFor(c, nrooms);
	}
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
				p[0] = t->p[k * 3] * st->scale - ptr_bgdata_room_fileposition_list[r].pos.x;
				p[1] = t->p[k * 3 + 1] * st->scale - ptr_bgdata_room_fileposition_list[r].pos.y;
				p[2] = t->p[k * 3 + 2] * st->scale - ptr_bgdata_room_fileposition_list[r].pos.z;
				fillVert(mesh, t, &dst[slot + k], p, k);
			}
		}
		if (nopa && gevrBeanEmitList(mesh, opa, nopa, 0, 0, 1, 1, &gdl, &vtx)) {
			s_room[r].opa = gdl;
			s_room[r].vtxOpa = vtx;
			s_room[r].has = 1;
		}
		gdl = NULL;
		vtx = NULL;
		if (nxlu && gevrBeanEmitList(mesh, xlu, nxlu, 0, 0, 0, 1, &gdl, &vtx)) {
			s_room[r].xlu = gdl;
			s_room[r].vtxXlu = vtx;
			s_room[r].has = 1;
		}
		free(opa);
		free(xlu);
	}
	roomReferenceFree();
	free(assign);
	s_nrooms = nrooms;
	s_ok = 1;
	fprintf(stderr, "gevr bean: HD rooms %s, %d tris across %d rooms\n", st->bg, mesh->ntris, nrooms);
	gevrBeanMeshFree(mesh);
	return 1;
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
		fprintf(stderr, "gevr bean: no HD sky %s\n", name);
		return 0;
	}
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
	s_skyOk = 1;
	fprintf(stderr, "gevr bean: HD sky %s, %d tris, extent %.0f\n", name, mesh->ntris, extent);
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
	if (!ensureStage() || room < 0 || room >= s_nrooms || !s_room[room].has) {
		return gdl;
	}
	if (replaced) {
		*replaced = 1;
	}
	list = xlu ? s_room[room].xlu : s_room[room].opa;
	if (!list) {
		return gdl;
	}
	g = gdl;
	gSPDisplayList(g++, list);
	return g;
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
	gSPDisplayList(g++, s_skyGdl);
	gSPSetGeometryMode(g++, G_ZBUFFER);
	if (drew) {
		*drew = 1;
	}
	return g;
}
