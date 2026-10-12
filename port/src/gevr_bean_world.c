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
#include "gevr_bean_fog.h"

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

void gevrBeanWorldBeginView(void) { s_opaqueDrawn=0; }

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
	e->tex = t->tex;
	e->alpha = t->alpha;
	e->cut = t->alphatest;
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
	mesh->roomMaterial = 1;
    mesh->worldVertices = 1;
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
			s_room[r].wrap[1] = meshWrap(gdl, &s_room[r].mesh[1]);
		}
		free(opa);
		free(xlu);
	}
    /* Opaque depth-writing geometry can share materials across room boundaries.
     * Transparent surfaces keep the game's room order below. A diagnostic marker
     * retains the old room submission for a controlled comparison. */
    FILE *control=fopen("gevr_room_draws.txt","r");
    int merge=1;
    if (control) { int mode=1; if (fscanf(control,"%d",&mode)==1) merge=mode!=0; fclose(control); }
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
                s_opaqueWrap=meshWrap(s_opaque,&s_opaqueMesh);
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
    roomReferenceFree();
    free(assign);
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
	if (!ensureStage() || room < 0 || room >= s_nrooms || !s_room[room].has) {
		return gdl;
	}
	if (replaced) {
		*replaced = 1;
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
