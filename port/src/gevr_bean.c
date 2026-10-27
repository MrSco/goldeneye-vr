/*
 * Bean meshes on GoldenEye's model nodes. Weighted vertices on the native animation matrices.
 * Muzzle flash, slide and cartridge lists are left as the game drew them.
 * Level reset drops the registry and does not walk nodes.
 */

#include "gevr_bean_log.h"
#include "gevr_bean_control.h"

#include <ultra64.h>
#include <PR/gbi.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <bondtypes.h>
#include <bondconstants.h>

#include "dyn.h"
#include "gevr_bean.h"
#include "gevr_romlayout.h"
#include "gevr_mirror.h"
#include "gevr_bean_int.h"
#include "gevr_bean_ce.h"
#include "gevr_skin_mesh.h"
#include "gevr_bean_room_state.h"
#include "gevr_oddjobhat.h"

extern int VrBeanModels;
extern int VrGexGuns;

#define MAX_SWAP  4096
#define MAX_ALLOC 8192
#define MAX_GRP   256
#define MAX_DL    1024
#define MAX_WALK  4096

/* The model instances whose Bean lists the opaque pass drew this frame
 * (gevrBeanNoteOpaque): the translucent pass leaves them be. */
#define MAX_OPAQUE_MODELS 1024
static const void *s_opaqueModels[MAX_OPAQUE_MODELS];
static int s_nopaqueModels;
static void beanGunCollect(void);
#include "gevr_bean_frame.inc.h"

/* First-person file buffers are reused at weapon switches and respawns.
 * Each load owns its replacements until no registry entry refers to it,
 * then the last two frame lists must finish before that storage is freed. */
struct beanGunGroup {
	struct beanGunGroup *next;
	unsigned retiredFrame;
	int references, retired;
};
static struct beanGunGroup *s_gunGroups, *s_registrationGroup;

static Gfx s_blank[1];
static int s_blankReady;
static int s_want;
static int s_available = -1;

static GevrBeanCuts s_cuts;
void gevrBeanSetCuts(const GevrBeanCuts *cuts)
{
    if (cuts) s_cuts = *cuts;
    else memset(&s_cuts, 0, sizeof(s_cuts));
}

struct beanSkinVert {
	float p[4][3], n[4][3];
	uint8_t weight[4];
	int16_t matrix[4];
	GevrVertexTint tint;
};
struct beanSkin {
	struct beanSkinVert *verts;
	Vtx *vtx;
	Gfx *gdl;
	int nverts, ncommands, anchor;
	int head;   /* a Bean head's (emitHead): wounds on the head box land on it */
	GevrStaticMesh *mesh;
	int16_t bones[GEVR_SKIN_BONES];
	int nbones, meshTried;
	int *weld; /* gevr_bean_caps.inc.h: vertices that are the same vertex */
};
static GevrStaticMesh *s_skinMeshes[MAX_ALLOC];
static int s_nskinMeshes;
static const struct beanSkinVert *s_emitSkin;
static struct beanSkin *s_emittedSkin;

static ModelFileHeader *s_registrationOwner;
static int s_registrationWatchArm;
static GevrStaticMesh *s_rigidMeshes[MAX_ALLOC];
static struct beanGunGroup *s_rigidGroups[MAX_ALLOC];
static int s_nrigidMeshes;
static int s_registrationKind, s_rigidMinimum=256;
struct beanSwap {
	ModelFileHeader *owner;
	struct beanGunGroup *group;
	void *node;
	void *original;
	void *ours;
	s16 origType;
	struct beanSkin *skin;
	int deferredMatrix;
	int watchArm;
	void *spent;   /* DAB fpRound: the rocket launcher drawn with nothing in its tube */
	/* A rigid prop's own list and vertices (gevrBeanEmitList), for the copy a
	 * destroyed instance draws (gevrBeanScorchedGdl); NULL for the others. */
	Gfx *emitted;
	void *emittedVtx;
	int emittedVerts, emittedCommands, emittedWorld;
	struct beanScorch *scorch;
	float box[2][3];   /* the emitted list's bounds, in the node's space */
	int hasBox;
	int opaqueOnly;    /* the emitted list has no translucent batch (gevrBeanTransparentGdl) */
};
struct beanScorch;
/* The rigid list gevrBeanEmitList made last: registerPrimary keeps it with its swap. */
static Gfx *s_lastEmit;
static void *s_lastEmitVtx;
static int s_lastEmitVerts, s_lastEmitCommands, s_lastEmitWorld, s_lastEmitOpaque;

static struct beanSwap s_swap[MAX_SWAP];
static int s_nswap;
/* Node selection is repeated for each body part and pass. */
static uint16_t s_swapIndex[MAX_SWAP * 2];
static void *s_alloc[MAX_ALLOC];
static struct beanGunGroup *s_allocGroups[MAX_ALLOC];
static int s_nalloc;

struct beanCache {
	char path[192];
	struct gevrBeanMesh *mesh;
};

static struct beanCache s_cache[48];
static int s_ncache;

static const char *const s_roots[] = {
    "../texture-packs/ge007-bean/files",
	"added-content/xbla/files",
	"added-content/xbla",
	"xbla/files",
	"xbla"
};

enum {
	BEAN_NONE = 0,
	BEAN_CHR,
	BEAN_GUN,
	BEAN_PROP,
	BEAN_HEAD,
	BEAN_HEADBODY
};

struct beanFile {
	const char *name;
	int kind;
	const char *source;
};
static const struct beanFile s_files[] = {
#include "gevr_bean_files.h"
};
struct beanFit {
	const char *name, *source;
	int perm[3], sign[3];
	float scale, bc[3], nc[3];
};
#define BEANFIT(name, source, p0, p1, p2, s0, s1, s2, scale, b0, b1, b2, n0, n1, n2)                         \
	{                                                                                                        \
		name, source, {p0, p1, p2}, {s0, s1, s2}, scale, {b0, b1, b2},                                       \
		{                                                                                                    \
			n0, n1, n2                                                                                       \
		}                                                                                                    \
	}
static const struct beanFit s_fits[] = {
#include "gevr_bean_fits.h"
};
#undef BEANFIT
static const struct beanFit *fitForName(const char *name)
{
	for (unsigned i = 0; i < sizeof(s_fits) / sizeof(s_fits[0]); i++)
		if (!strcmp(name, s_fits[i].name))
			return &s_fits[i];
	return NULL;
}

static void blankInit(void)
{
	if (!s_blankReady) {
		gSPEndDisplayList(s_blank);
		s_blankReady = 1;
	}
}

static int keepAlloc(void *p)
{
	if (!p) {
		return 0;
	}
	if (s_nalloc >= MAX_ALLOC) {
		GEVR_BEAN_LOG("gevr bean: allocation table full\n");
		return 0;
	}
	s_allocGroups[s_nalloc] = s_registrationGroup;
	s_alloc[s_nalloc++] = p;
	return 1;
}

#include "gevr_bean_gun_lifetime.inc.h"

static unsigned swapBucket(const void *node)
{
    uintptr_t key = (uintptr_t)node >> 4;
    key ^= key >> 17;
    key *= 0x9e3779b1u;
    return (unsigned)key & (MAX_SWAP * 2 - 1);
}
static void swapIndexAdd(int i)
{
    unsigned bucket = swapBucket(s_swap[i].node);
    while (s_swapIndex[bucket]) bucket = (bucket + 1) & (MAX_SWAP * 2 - 1);
    s_swapIndex[bucket] = (uint16_t)(i + 1);
}
static void swapIndexRebuild(void)
{
    memset(s_swapIndex, 0, sizeof(s_swapIndex));
    for (int i=0; i<s_nswap; i++) swapIndexAdd(i);
}
static struct beanSwap *swapOf(void *node)
{
    if (!node) return NULL;
    unsigned bucket = swapBucket(node);
    while (s_swapIndex[bucket]) {
        struct beanSwap *s = &s_swap[s_swapIndex[bucket] - 1];
        if (s->node == node) return s;
        bucket = (bucket + 1) & (MAX_SWAP * 2 - 1);
    }
    return NULL;
}

static void swapAdd(void *node, void *original, void *ours, int origType)
{
	if (!node || swapOf(node)) {
		return;
	}
	if (s_nswap >= MAX_SWAP) {
		GEVR_BEAN_LOG("gevr bean: swap table full\n");
		return;
	}
	s_swap[s_nswap].owner = s_registrationOwner;
	s_swap[s_nswap].group = s_registrationGroup;
	if (s_registrationGroup) s_registrationGroup->references++;
	s_swap[s_nswap].node = node;
	s_swap[s_nswap].original = original;
	s_swap[s_nswap].ours = ours ? ours : s_blank;
	s_swap[s_nswap].origType = (s16)origType;
	s_swap[s_nswap].skin = s_emittedSkin;
	s_swap[s_nswap].deferredMatrix = 0;
	s_swap[s_nswap].watchArm = s_registrationWatchArm;
	s_swap[s_nswap].spent = NULL;
	s_swap[s_nswap].emitted = NULL;
	s_swap[s_nswap].emittedVtx = NULL;
	s_swap[s_nswap].emittedVerts = s_swap[s_nswap].emittedCommands = s_swap[s_nswap].emittedWorld = 0;
	s_swap[s_nswap].scorch = NULL;
    s_emittedSkin = NULL;
	swapIndexAdd(s_nswap);
	s_nswap++;
}

static int beanLiveScreen(void *node);

/* GE-X supplies the complete first-person arm. Its watch path needs the
 * native watch pieces and switches, which the Bean arm combines into one list.
 * A monitor's screen the programme has written is the game's (DAB xblamesh.c
 * xblaMeshRenderNode(), gevr_bean_screens.inc.h). */
static struct beanSwap *activeSwap(void *node)
{
    /* Mods: Oddjob's hat keeps the throwing knife's models native (gevr_oddjobhat.c) */
    struct beanSwap *s = gevrBeanIsEnabled() && !gevrHatOwnsNode(node) ? swapOf(node) : NULL;
    return s && !(s->watchArm && VrGexGuns) && !beanLiveScreen(node) ? s : NULL;
}

/* Replacements belong to the renderer. Native lists and model types are also
 * read by aiming, bullet hits and other gameplay walkers; never overwrite them. */
/* DAB fpRound: whether the launcher being drawn has a round in its tube
 * (gunfire.c and gevr_heldgun.c set it for the hand they draw; anything else,
 * the watch's and the panel's preview, shows the launcher loaded) */
static int s_loaded = 1;

void gevrBeanSetLoaded(int loaded)
{
	s_loaded = loaded != 0;
}

void *gevrBeanFadeMark(void *gdl, int alpha)
{
    Gfx *g = gdl;
    if (gevrBeanNodeMode()) return gdl;   /* the node's render mode fades it */
    g->words.w0 = (uintptr_t)G_GEVR_BEAN_FADE << 24;
    g->words.w1 = (uint32_t)(alpha < 0 ? 0 : alpha > 255 ? 255 : alpha);
    return g + 1;
}

void *gevrBeanShownGdl(void *node, void *current)
{
	struct beanSwap *s = activeSwap(node);
	/* GE-X and the VR watch hide the hand by clearing its native list.
	 * Honour that per-draw visibility instead of resurrecting the Bean arm. */
	return s && (current || !s->original) ? (s->spent && !s_loaded ? s->spent : s->ours) : current;
}

/* DAB gebeanFirstPersonHasRound(): the model draws the release's own round,
 * so GoldenEye's rocket is not drawn in its mouth as well */
int gevrBeanHasRound(const void *header)
{
	for (int i = 0; header && i < s_nswap; i++)
		if (s_swap[i].owner == header && s_swap[i].spent && activeSwap(s_swap[i].node) == &s_swap[i])
			return 1;
	return 0;
}

/* whether the release draws any of a model's lists now: a gun in the hand in
 * 2D is then depth-tested on its own (gunfire.c), as GoldenEye X's are */
int gevrBeanDrawsModel(const void *header)
{
	for (int i = 0; header && i < s_nswap; i++)
		if (s_swap[i].owner == header && s_swap[i].ours != s_blank && activeSwap(s_swap[i].node) == &s_swap[i])
			return 1;
	return 0;
}

int gevrBeanShownModelType(void *node, int current)
{
	return activeSwap(node) ? 0 : current;
}

int gevrBeanNodeMode(void)
{
    static int mode = -1;
    if (mode < 0) {
        FILE *f = gevrBeanControlOpen("gevr_bean_nodemode.txt");
        mode = 1;
        if (f) {
            if (fscanf(f, "%d", &mode) != 1) mode = 1;
            fclose(f);
        }
        GEVR_BEAN_LOG("gevr bean controls: nodemode=%d\n", mode);
    }
    return mode != 0;
}

int gevrBeanNodeModeType(void *node, int current)
{
    struct beanSwap *s = activeSwap(node);
    return s && gevrBeanNodeMode() ? s->origType : current;
}

void *gevrBeanNodeMark(void *gdl, int on)
{
    Gfx *g = gdl;
    g->words.w0 = ((uintptr_t)G_GEVR_NODE_STATE << 24) | (on ? GEVR_NODE_ON : GEVR_NODE_OFF);
    g->words.w1 = 0;
    return g + 1;
}

/* Native glass props may be submitted only with flags=2. Their replacement
 * owns both render modes, but must still be reachable in that pass. flags=3
 * already draws it once through the primary path. Preserve hidden/broken lists. */
void gevrBeanNoteOpaque(const void *model)
{
    if (model && s_nopaqueModels < MAX_OPAQUE_MODELS &&
        (s_nopaqueModels == 0 || s_opaqueModels[s_nopaqueModels - 1] != model))
        s_opaqueModels[s_nopaqueModels++] = model;
}

static int opaqueDrawn(const void *model)
{
    for (int i = s_nopaqueModels - 1; i >= 0; i--)
        if (s_opaqueModels[i] == model)
            return 1;
    return 0;
}

/* A type-4 prop's Bean list holds its translucent batches too. Glass needs this
 * pass: drawn in the opaque one alone, the walls behind it can be drawn over
 * it (user, 2026-10-14, the guard tower's windows popping). A list with no
 * translucent batch the opaque pass already drew is not drawn again: a second,
 * clean copy lay over a destroyed prop's scorched one (Dam's drums) and every
 * such prop was drawn twice. One with translucent batches is split over the
 * passes (gevrBeanPassSplit). */
void *gevrBeanTransparentGdl(void *node, void *current, struct Model *model, int flags)
{
    struct beanSwap *s = activeSwap(node);
    return s && s->origType == 4 && (flags & 2) && !(flags & 1) && (current || !s->original) &&
           !(s->opaqueOnly && opaqueDrawn(model))
        ? gevrBeanRenderGdl(node, current, model) : NULL;
}

/* As GoldenEye's own type-4 nodes, solid in the opaque pass and translucent in
 * the translucent one: a list with both draws its solid batches in the first
 * (1) and its translucent ones in the second (2), each triangle once. Drawn
 * whole in both, a guard tower with windows cost its every triangle twice
 * (user, 2026-10-14). 0: the call draws the whole list (a pass drawn alone). */
int gevrBeanPassSplit(void *node, struct Model *model, int flags)
{
    struct beanSwap *s = activeSwap(node);
    if (!s || s->origType != 4 || s->opaqueOnly || !s->emitted)
        return 0;
    if (flags == 1)
        return 1;
    return flags == 2 && opaqueDrawn(model) ? 2 : 0;
}

void *gevrBeanPassMark(void *gdl, int pass)
{
    Gfx *g = gdl;
    g->words.w0 = ((uintptr_t)G_GEVR_NODE_STATE << 24) | GEVR_NODE_PASS;
    g->words.w1 = (uint32_t)pass;
    return g + 1;
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

/* Cartridge name -> new/... source and a kind. 0 when this file stays N64. */
static int sourceForName(const char *name, char *source, int sourcelen)
{
	if (!name)
		return BEAN_NONE;
	for (unsigned i = 0; i < sizeof(s_files) / sizeof(s_files[0]); i++) {
		if (!strcmp(name, s_files[i].name)) {
			snprintf(source, sourcelen, "%s", s_files[i].source);
			return s_files[i].kind;
		}
	}
	return BEAN_NONE;
}

static struct gevrBeanMesh *cachedLoad(const char *path, int keepparts)
{
	for (int i = 0; i < s_ncache; i++) {
		if (strcmp(s_cache[i].path, path) == 0) {
			return s_cache[i].mesh;
		}
	}
	{
		struct gevrBeanMesh *mesh = NULL;
		if (!gevrBeanLoad(path, keepparts, &mesh) || !mesh) {
			return NULL;
		}
		if (s_ncache < (int)(sizeof(s_cache) / sizeof(s_cache[0]))) {
			snprintf(s_cache[s_ncache].path, sizeof(s_cache[s_ncache].path), "%s", path);
			s_cache[s_ncache].mesh = mesh;
			s_ncache++;
		}
		return mesh;
	}
}

static const struct gevrEmitVert *s_sortVerts;
static uint32_t *s_sortSpatial;

static uint32_t spatialBits(uint32_t x)
{
    x &= 1023; x = (x | x << 16) & 0x030000ff;
    x = (x | x << 8) & 0x0300f00f;
    x = (x | x << 4) & 0x030c30c3;
    return (x | x << 2) & 0x09249249;
}

static int vertKey(const struct gevrEmitVert *v)
{
	return ((int)(v->reflectionDiffuse != 0) << 30) | ((int)v->decal << 29) | ((int)v->cut << 28)
        | ((v->tex + 1) << 16) | ((int)v->alpha << 8);
}

static int triCmp(const void *a, const void *b)
{
	int ia = *(const int *)a;
	int ib = *(const int *)b;
	int ka = vertKey(&s_sortVerts[ia * 3]);
	int kb = vertKey(&s_sortVerts[ib * 3]);
    if (ka != kb) return (ka > kb) - (ka < kb);
    uintptr_t ra=(uintptr_t)s_sortVerts[ia*3].reflection,rb=(uintptr_t)s_sortVerts[ib*3].reflection;
    if(ra!=rb)return (ra>rb)-(ra<rb);
    int ma=s_sortVerts[ia*3].reflectionDiffuse,mb=s_sortVerts[ib*3].reflectionDiffuse;
    if(ma!=mb)return (ma>mb)-(ma<mb);
    uint32_t ca=s_sortVerts[ia*3].reflectionColour,cb=s_sortVerts[ib*3].reflectionColour;
    if(ca!=cb)return (ca>cb)-(ca<cb);
    int aa=s_sortVerts[ia*3].reflectionAlphaBase,ab=s_sortVerts[ib*3].reflectionAlphaBase;
    if(aa!=ab)return (aa>ab)-(aa<ab);
    if (s_sortSpatial && s_sortSpatial[ia] != s_sortSpatial[ib])
        return (s_sortSpatial[ia] > s_sortSpatial[ib]) - (s_sortSpatial[ia] < s_sortSpatial[ib]);
    return ia - ib;
}

static int texMask(int n)
{
	int m = 0, v = 1;
	while (v < n && m < 15) {
		v <<= 1;
		m++;
	}
	return m;
}

static s16 tcClamp(float v)
{
	if (v > 32767.0f) return 32767;
	if (v < -32768.0f) return -32768;
	return (s16)(v < 0.0f ? v - 0.5f : v + 0.5f);
}

static s16 posClamp(float v)
{
	if (v > 32767.0f) return 32767;
	if (v < -32768.0f) return -32768;
	return (s16)(v < 0.0f ? v - 0.5f : v + 0.5f);
}

static uint32_t beanRenderMode(Gfx **next, int mode1, int mode2, int room)
{
    Gfx *g = *next;
    g->words.w0 = ((uintptr_t)G_GEVR_ROOM_STATE << 24) | (room == 2 ? 2 : room ? 0 : 1);
    g->words.w1 = (uint32_t)(G_RM_PASS | mode2 | (mode1 & 0xffff));
    *next = g + 1;
    return (uint32_t)g->words.w1;
}

/* GoldenEye's water list's fade (unk_092E50.c sub_GAME_7F092E50): sin(phase)
 * * 127 + 128, the phase gaining 0.04 radians a frame; the slide is the
 * renderer's (gevrBeanWaterTransform) */
static Gfx s_waterGdl[2];

void *gevrBeanWaterList(void)
{
	if (!s_waterGdl[1].words.w0)
		gevrBeanWaterTick(0, 0);
	return s_waterGdl;
}

void gevrBeanWaterTick(float delta, float phase)
{
	Gfx *g = s_waterGdl;
	gevrBeanWaterAdvance(delta);
	gDPSetPrimColor(g++, 0, (u32)(sinf(phase) * 127.0f + 128.0f), 0xff, 0xff, 0xff, 0xff);
	gSPEndDisplayList(g++);
}

int gevrBeanEmitList(const struct gevrBeanMesh *mesh, const struct gevrEmitVert *v, int nverts,
		int lit, int dim, int cull, int zbuf, void **gdlOut, void **vtxOut)
{
	int ntri, *order, cmdcap;
	Gfx *gdl, *g;
	Vtx *vtx, *vp;
	int vi = 0;
    GevrWorldVertex *world = NULL;
    int worldFormat=mesh && mesh->worldVertices;
    if(mesh && !lit && !s_emitSkin) for(int i=0;i<mesh->ntex;i++) worldFormat |= mesh->reflection[i]!=NULL;
	if (!mesh || !v || nverts < 3 || !gdlOut || !vtxOut) {
		return 0;
	}
	struct beanSkin *skin = s_emitSkin ? calloc(1, sizeof(*skin)) : NULL;
	if (s_emitSkin && !skin)
		return 0;
	if (skin) {
		skin->verts = malloc((size_t)nverts * sizeof(*skin->verts));
		if (!skin->verts) {
			free(skin);
			return 0;
		}
	}
    if (worldFormat && (lit || s_emitSkin)) return 0;
    ntri = nverts / 3;
    order = malloc((size_t)ntri * sizeof(int));
	/* One texture group can be a single triangle, and gDPLoadTextureBlock writes several commands. */
	cmdcap = 64 + ntri * 40;
	gdl = malloc((size_t)cmdcap * sizeof(Gfx));
    vtx = malloc((size_t)nverts * (worldFormat ? sizeof(GevrWorldVertex) :
                 sizeof(Vtx) + (lit && !skin ? sizeof(GevrVertexTint) : 0)));
    if (worldFormat) world = (GevrWorldVertex *)vtx;
	if (!order || !gdl || !vtx) {
		if (skin) {
			free(skin->verts);
			free(skin);
		}
		free(order);
		free(gdl);
		free(vtx);
		return 0;
	}
    GevrVertexTint *tints = lit && !skin ? (GevrVertexTint *)(vtx+nverts) : NULL;
	for (int i = 0; i < ntri; i++) {
		order[i] = i;
	}
    s_sortSpatial = NULL;
    if (mesh->spatialSort) {
        s_sortSpatial = malloc((size_t)ntri * sizeof(*s_sortSpatial));
        float lo[3] = {1e30f,1e30f,1e30f}, hi[3] = {-1e30f,-1e30f,-1e30f};
        for (int i=0;i<nverts;i++) for (int k=0;k<3;k++) {
            if (v[i].p[k] < lo[k]) lo[k]=v[i].p[k];
            if (v[i].p[k] > hi[k]) hi[k]=v[i].p[k];
        }
        if (s_sortSpatial) for (int i=0;i<ntri;i++) {
            uint32_t key=0;
            for (int k=0;k<3;k++) {
                float centre=(v[i*3].p[k]+v[i*3+1].p[k]+v[i*3+2].p[k])/3;
                float t=hi[k]>lo[k] ? (centre-lo[k])/(hi[k]-lo[k]) : 0;
                uint32_t q=t<=0 ? 0 : t>=1 ? 1023 : (uint32_t)(t*1023);
                key |= spatialBits(q)<<k;
            }
            s_sortSpatial[i]=key;
        }
    }
    s_sortVerts = v;
    qsort(order, (size_t)ntri, sizeof(int), triCmp);
    free(s_sortSpatial); s_sortSpatial=NULL;
	g = gdl;
	vp = vtx;
    /* a model list keeps the node's state when the game asks (G_GEVR_NODE_STATE) */
    const int node = !mesh->roomMaterial;
    if (node) { g->words.w0 = ((uintptr_t)G_GEVR_NODE_STATE << 24) | GEVR_NODE_SAVE; g++->words.w1 = 0; }
	gDPPipeSync(g++);
	gDPSetCycleType(g++, G_CYC_2CYCLE);
	gDPSetTexturePersp(g++, G_TP_PERSP);
	gDPSetTextureLUT(g++, G_TT_NONE);
	gDPSetAlphaCompare(g++, G_AC_NONE);
	gSPClearGeometryMode(g++, G_TEXTURE_GEN | G_TEXTURE_GEN_LINEAR | G_LIGHTING | G_CULL_BOTH);
	if (lit) {
		gSPSetGeometryMode(g++, G_LIGHTING | G_SHADE | G_SHADING_SMOOTH);
	} else {
		gSPSetGeometryMode(g++, G_SHADE | G_SHADING_SMOOTH);
	}
	if (cull) {
		gSPSetGeometryMode(g++, G_CULL_BACK);
	}
	if (!zbuf) {
		gSPClearGeometryMode(g++, G_ZBUFFER);
	} else {
		gSPSetGeometryMode(g++, G_ZBUFFER);
	}
	if (dim) {
		gDPSetPrimColor(g++, 0, 0, 96, 96, 96, 255);
	}
	for (int batch = 0; batch < ntri; ) {
		const struct gevrEmitVert *base = &v[order[batch] * 3];
		int key = vertKey(base);
		int end = batch;
		int tex = base->tex;
		int logicalW = 32, logicalH = 32;
		int maskS, maskT;
		int clampS = tex >= 0 && tex < mesh->ntex && (mesh->texClamp[tex] & 1);
		int clampT = tex >= 0 && tex < mesh->ntex && (mesh->texClamp[tex] & 2);
		while (end < ntri && vertKey(&v[order[end] * 3]) == key && v[order[end]*3].reflection==base->reflection &&
            v[order[end]*3].reflectionDiffuse==base->reflectionDiffuse &&
            v[order[end]*3].reflectionColour==base->reflectionColour &&
            v[order[end]*3].reflectionAlphaBase==base->reflectionAlphaBase) {
			end++;
		}
		if (tex >= 0 && tex < mesh->ntex && mesh->standin[tex]) {
			logicalW = mesh->texW[tex] > 0 ? mesh->texW[tex] : 32;
			logicalH = mesh->texH[tex] > 0 ? mesh->texH[tex] : 32;
			if (logicalW > 512) logicalW = 512;
			if (logicalH > 512) logicalH = 512;
			/* The decoded image keeps its full resolution. Smaller logical
			 * tiles give repeating UVs enough room in signed 10.5 coordinates. */
			float spanS = 0, spanT = 0;
			for (int t = batch; t < end; t++) {
				const struct gevrEmitVert *tri = &v[order[t] * 3];
				for (int k = 0; k < 3; k++) {
					float s = fabsf(tri[k].uv[0] - floorf(tri[0].uv[0]));
					float tt = fabsf(-tri[k].uv[1] - floorf(-tri[0].uv[1]));
					if (s > spanS) spanS = s;
					if (tt > spanT) spanT = tt;
				}
			}
			while (logicalW > 1 && spanS * logicalW * 32 > 32767) logicalW /= 2;
			while (logicalH > 1 && spanT * logicalH * 32 > 32767) logicalH /= 2;
			maskS = texMask(logicalW);
			maskT = texMask(logicalH);
			gSPTexture(g++, 0xffff, 0xffff, 0, G_TX_RENDERTILE, G_ON);
			gDPLoadTextureBlock(g++, mesh->standin[tex], G_IM_FMT_RGBA, G_IM_SIZ_16b,
				16, 16, 0, G_TX_WRAP, G_TX_WRAP, 4, 4, G_TX_NOLOD, G_TX_NOLOD);
			gDPSetTile(g++, G_IM_FMT_RGBA, G_IM_SIZ_16b,
				(((logicalW) * 2) + 7) >> 3, 0, G_TX_RENDERTILE, 0,
				clampT ? G_TX_CLAMP : G_TX_WRAP, maskT, G_TX_NOLOD,
				clampS ? G_TX_CLAMP : G_TX_WRAP, maskS, G_TX_NOLOD);
			gDPSetTileSize(g++, G_TX_RENDERTILE, 0, 0,
				(logicalW - 1) << G_TEXTURE_IMAGE_FRAC, (logicalH - 1) << G_TEXTURE_IMAGE_FRAC);
            /* DAB xblaMeshSetMaterial: tile 1 the same picture, for the nodes'
             * G_CC_TRILERP (fast3d's LOD fraction is 1 without G_TL_LOD) */
            gDPSetTile(g++, G_IM_FMT_RGBA, G_IM_SIZ_16b,
                (((logicalW) * 2) + 7) >> 3, 0, G_TX_RENDERTILE + 1, 0,
                clampT ? G_TX_CLAMP : G_TX_WRAP, maskT, G_TX_NOLOD,
                clampS ? G_TX_CLAMP : G_TX_WRAP, maskS, G_TX_NOLOD);
            gDPSetTileSize(g++, G_TX_RENDERTILE + 1, 0, 0,
                (logicalW - 1) << G_TEXTURE_IMAGE_FRAC, (logicalH - 1) << G_TEXTURE_IMAGE_FRAC);
		} else {
			tex = -1;
			clampS = clampT = 0;
			gSPTexture(g++, 0, 0, 0, G_TX_RENDERTILE, G_OFF);
		}
        uint32_t ownMode;
        int nodeFlags;
		{
			const int edge = zbuf ? G_RM_AA_ZB_TEX_EDGE : G_RM_AA_TEX_EDGE;
			const int edge2 = zbuf ? G_RM_AA_ZB_TEX_EDGE2 : G_RM_AA_TEX_EDGE2;
			const int xlu = zbuf ? G_RM_AA_ZB_XLU_SURF : G_RM_XLU_SURF;
			const int xlu2 = zbuf ? G_RM_AA_ZB_XLU_SURF2 : G_RM_XLU_SURF2;
			const int opa = zbuf ? G_RM_AA_ZB_OPA_SURF : G_RM_AA_OPA_SURF;
			const int opa2 = zbuf ? G_RM_AA_ZB_OPA_SURF2 : G_RM_AA_OPA_SURF2;
			if (base->alpha < 255) {
                if (base->decal && zbuf) ownMode = beanRenderMode(&g, G_RM_AA_ZB_XLU_DECAL, G_RM_AA_ZB_XLU_DECAL2, mesh->roomMaterial);
                else ownMode = beanRenderMode(&g, xlu, xlu2, mesh->roomMaterial);
                nodeFlags = GEVR_NODE_SPAN | GEVR_NODE_XLU;
				if (tex < 0) {
					gDPSetCombineMode(g++, G_CC_SHADE, G_CC_PASS2);
				} else if (dim) {
					gDPSetCombineMode(g++, G_CC_MODULATEIA_PRIM, G_CC_PASS2);
				} else {
					gDPSetCombineMode(g++, G_CC_MODULATEIA, G_CC_PASS2);
				}
			} else if (base->cut) {
                if (base->decal && zbuf) ownMode = beanRenderMode(&g, (edge & ~ZMODE_DEC) | ZMODE_DEC,
                        (edge2 & ~ZMODE_DEC) | ZMODE_DEC, mesh->roomMaterial);
                else ownMode = beanRenderMode(&g, edge, edge2, mesh->roomMaterial);
                nodeFlags = GEVR_NODE_SPAN | GEVR_NODE_CUT | (tex >= 0 ? GEVR_NODE_COMBINE : 0);
				if (tex < 0) {
					gDPSetCombineMode(g++, G_CC_SHADE, G_CC_PASS2);
				} else {
					gDPSetCombineMode(g++, G_CC_MODULATEIA, G_CC_PASS2);
				}
			} else if (base->decal && zbuf) {
				ownMode = beanRenderMode(&g, G_RM_AA_ZB_OPA_DECAL | Z_UPD, G_RM_AA_ZB_OPA_DECAL2, mesh->roomMaterial);
                nodeFlags = GEVR_NODE_SPAN | (tex >= 0 ? GEVR_NODE_COMBINE : 0);
				if (tex < 0) {
					gDPSetCombineMode(g++, G_CC_SHADE, G_CC_PASS2);
				} else if (dim) {
					gDPSetCombineMode(g++, G_CC_MODULATEI_PRIM, G_CC_PASS2);
				} else {
					gDPSetCombineMode(g++, G_CC_MODULATEI, G_CC_PASS2);
				}
			} else if (tex < 0) {
				ownMode = beanRenderMode(&g, opa, opa2, mesh->roomMaterial);
                nodeFlags = 0;
				gDPSetCombineMode(g++, G_CC_SHADE, G_CC_PASS2);
			} else if (dim) {
				ownMode = beanRenderMode(&g, opa, opa2, mesh->roomMaterial);
                nodeFlags = GEVR_NODE_COMBINE;
				gDPSetCombineMode(g++, G_CC_MODULATEI_PRIM, G_CC_PASS2);
			} else {
				ownMode = beanRenderMode(&g, opa, opa2, mesh->roomMaterial);
                nodeFlags = GEVR_NODE_COMBINE;
				gDPSetCombineMode(g++, G_CC_MODULATEI, G_CC_PASS2);
			}
		}
        if (node) {   /* DAB xblaMeshSetSpanMode */
            g->words.w0 = ((uintptr_t)G_GEVR_NODE_STATE << 24) | GEVR_NODE_APPLY | (uint32_t)nodeFlags;
            g++->words.w1 = ownMode;
        }
        int reflect=tex>=0 && base->reflection;
        g->words.w0=((uintptr_t)G_GEVR_NORMAL_TEX1<<24) | (base->reflectionDiffuse==1 ? 1 : 0); g++->words.w1=reflect;
        if(reflect) {
            gDPLoadMultiBlock(g++,base->reflection,256,1,G_IM_FMT_RGBA,G_IM_SIZ_16b,
                             16,16,0,G_TX_CLAMP,G_TX_CLAMP,4,4,G_TX_NOLOD,G_TX_NOLOD);
            if(base->reflectionDiffuse==2) {
                uint32_t c=base->reflectionColour;
                gDPSetPrimColor(g++,0,0,c>>24,(c>>16)&255,(c>>8)&255,c&255);
                gDPSetEnvColor(g++,0,0,0,base->reflectionAlphaBase);
                gDPSetCombineLERP(g++,TEXEL0,0,SHADE,0,0,0,0,SHADE,
                                 PRIMITIVE,COMBINED,TEXEL1,COMBINED,
                                 PRIMITIVE,ENVIRONMENT,TEXEL1,ENVIRONMENT);
            } else if(base->alpha<255 || base->cut) {
                gDPSetCombineLERP(g++,TEXEL0,0,SHADE,0,TEXEL0,0,SHADE,0,
                                 1,COMBINED,TEXEL1,COMBINED,0,0,0,COMBINED);
            } else {
                gDPSetCombineLERP(g++,TEXEL0,0,SHADE,0,0,0,0,SHADE,
                                 1,COMBINED,TEXEL1,COMBINED,0,0,0,COMBINED);
            }
        }
        /* DAB gewater.c: a water picture is GoldenEye's water list's two
         * copies (tile 1 the same picture, gevrBeanWaterTransform), the one
         * faded into the other by the primitive LOD fraction, then shaded */
        if (tex >= 0 && !dim && !reflect && gevrBeanTextureIsWater(mesh->standin[tex])) {
            gSPDisplayList(g++, gevrBeanWaterList());
            gDPSetCombineLERP(g++, TEXEL1, TEXEL0, PRIM_LOD_FRAC, TEXEL0, TEXEL1, TEXEL0, PRIM_LOD_FRAC, TEXEL0,
                              COMBINED, 0, SHADE, 0, COMBINED, 0, SHADE, 0);
        }
        while (batch < end) {
			int take = end - batch;
			if (take > 5) {
				take = 5;
			}
			for (int t = 0; t < take; t++) {
				for (int k = 0; k < 3; k++) {
					const struct gevrEmitVert *src = &v[order[batch + t] * 3 + k];
					float su = src->uv[0], sv = -src->uv[1];
					Vtx temp;
                    Vtx *dst = world ? &temp : &vp[t * 3 + k];
					/* Rebase the whole triangle together. Per-corner wrapping
					 * collapses 0..1 edges and tears triangles crossing a seam. */
					const struct gevrEmitVert *anchor = &v[order[batch + t] * 3];
					su -= clampS ? mesh->texClampBase[tex][0] : floorf(anchor->uv[0]);
					sv += clampT ? mesh->texClampBase[tex][1] + 1.0f : -floorf(-anchor->uv[1]);
					dst->v.ob[0] = posClamp(src->p[0]);
					dst->v.ob[1] = posClamp(src->p[1]);
					dst->v.ob[2] = posClamp(src->p[2]);
					dst->v.flag = 0;
					if (tex >= 0) {
						dst->v.tc[0] = tcClamp(su * (float)logicalW * 32.0f);
						dst->v.tc[1] = tcClamp(sv * (float)logicalH * 32.0f);
					} else {
						dst->v.tc[0] = dst->v.tc[1] = 0;
					}
					if (lit) {
						float length =
						    sqrtf(src->n[0] * src->n[0] + src->n[1] * src->n[1] + src->n[2] * src->n[2]);
						for (int k = 0; k < 3; k++)
							dst->n.n[k] = length > 1e-6f ? (s8)(src->n[k] * 127.0f / length) : 0;
						/* DAB: shade alpha 1 outside translucent materials, as the nodes' combiners lerp by it */
						dst->n.a = node && src->alpha == 255 ? 255 : (uint8_t)((unsigned)src->alpha * src->rgba[3] / 255);
					} else {
						dst->v.cn[0] = src->rgba[0];
						dst->v.cn[1] = src->rgba[1];
						dst->v.cn[2] = src->rgba[2];
						dst->v.cn[3] = node && src->alpha == 255 ? 255 : (uint8_t)((unsigned)src->alpha * src->rgba[3] / 255);
					}
                    if (world) {
                        memcpy(world[vi].p,src->p,sizeof(world[vi].p));
                        memcpy(world[vi].normal,src->n,sizeof(world[vi].normal));
                        memcpy(world[vi].tc,dst->v.tc,sizeof(world[vi].tc));
                        memcpy(world[vi].rgba,dst->v.cn,4);
                    }
                    if (skin) {
                        skin->verts[vi] = s_emitSkin[order[batch + t] * 3 + k];
                        memcpy(skin->verts[vi].tint.rgba, src->rgba, 4);
                        skin->verts[vi].tint.unlit = mesh->watchArm && tex == 9;
                    }
                    if (tints) {
                        memcpy(tints[vi].rgba, src->rgba, 4);
                        tints[vi].unlit = mesh->watchArm && tex == 9;
                    }
					vi++;
				}
			}
            if (skin) {
                g->words.w0 = (G_GEVR_VERTEX_TINT << 24) | (sizeof(struct beanSkinVert) << 8);
                g++->words.w1 = (uintptr_t)&skin->verts[vi - take*3].tint;
            } else if (tints) {
                g->words.w0 = (G_GEVR_VERTEX_TINT << 24) | (sizeof(GevrVertexTint) << 8);
                g++->words.w1 = (uintptr_t)&tints[vi - take*3];
            }
            if (world) {
                g->words.w0 = ((uintptr_t)G_GEVR_WORLD_VTX << 24) | (take*3*sizeof(*world));
                g++->words.w1 = (uintptr_t)(world+vi-take*3);
            } else gSPVertex(g++, osVirtualToPhysical(vp), take * 3, 0);
			for (int t = 0; t < take; t++) {
				gSP1Triangle(g++, t * 3, t * 3 + 1, t * 3 + 2, 0);
			}
			if (!world) vp += take * 3;
			batch += take;
			if (g - gdl > cmdcap - 16) {
				break;
			}
		}
		if (g - gdl > cmdcap - 16) {
			break;
		}
	}
    if (!mesh->roomMaterial) gSPClearGeometryMode(g++, G_FOG);
    g->words.w0=(uintptr_t)G_GEVR_NORMAL_TEX1<<24; g++->words.w1=0;
    g->words.w0 = G_GEVR_VERTEX_TINT << 24; g++->words.w1 = 0;
    if (node) { g->words.w0 = ((uintptr_t)G_GEVR_NODE_STATE << 24) | GEVR_NODE_END; g++->words.w1 = 0; }
	gSPEndDisplayList(g);
	free(order);
	if (skin) {
		skin->gdl = gdl;
		skin->vtx = vtx;
		skin->nverts = vi;
		skin->ncommands = (int)(g - gdl) + 1;
		keepAlloc(skin->verts);
		keepAlloc(skin);
		s_emittedSkin = skin;
	}
	*gdlOut = gdl;
	*vtxOut = vtx;
	/* an unlit rigid list (props): its colours are in the vertices */
	s_lastEmit = !skin && !tints ? gdl : NULL;
	s_lastEmitVtx = vtx;
	s_lastEmitVerts = vi;
	s_lastEmitCommands = (int)(g - gdl) + 1;
	s_lastEmitWorld = world != NULL;
	s_lastEmitOpaque = 1;
	for (int i = 0; i < nverts; i++)
		if (v[i].alpha < 255) s_lastEmitOpaque = 0;
	return 1;
}

static int nodeOp(const ModelNode *n)
{
	return n ? (n->Opcode & 0xff) : 0;
}

static void dlVerts(ModelNode *n, Vertex **verts, int *count, int *modelType, Gfx **primary)
{
	int op = nodeOp(n);
	*verts = NULL;
	*count = 0;
	*modelType = 0;
	*primary = NULL;
	if (op == MODELNODE_OPCODE_DL && n->Data) {
		*verts = (Vertex *)n->Data->DisplayList.Vertices;
		*count = n->Data->DisplayList.numVertices;
		*modelType = n->Data->DisplayList.ModelType;
		*primary = n->Data->DisplayList.Primary;
	} else if (op == MODELNODE_OPCODE_DLCOLLISION && n->Data) {
		*verts = n->Data->DisplayListCollisions.Vertices;
		*count = n->Data->DisplayListCollisions.numVertices;
		*modelType = n->Data->DisplayListCollisions.ModelType;
		*primary = n->Data->DisplayListCollisions.Primary;
	}
}

static float dlSpan(ModelNode *n)
{
	Vertex *verts;
	int count, type;
	Gfx *primary;
	float minv[3], maxv[3], span = 0;
	dlVerts(n, &verts, &count, &type, &primary);
	if (!verts || count <= 0) {
		return 0;
	}
	for (int k = 0; k < 3; k++) {
		minv[k] = 1e9f;
		maxv[k] = -1e9f;
	}
	for (int i = 0; i < count; i++) {
		for (int k = 0; k < 3; k++) {
			float c = verts[i].coord.AsArray[k];
			if (c < minv[k]) minv[k] = c;
			if (c > maxv[k]) maxv[k] = c;
		}
	}
	for (int k = 0; k < 3; k++) {
		if (maxv[k] - minv[k] > span) {
			span = maxv[k] - minv[k];
		}
	}
	return span;
}

static int nodeMatrix(ModelNode *n)
{
	for (; n; n = n->Parent) {
		if (!n->Data)
			continue;
		switch (nodeOp(n)) {
		case MODELNODE_OPCODE_HEADER:
			return n->Data->Header.MatrixIndex;
		case MODELNODE_OPCODE_GROUP:
		case MODELNODE_OPCODE_OP03:
			return n->Data->Group.MatrixIDs[0];
		case MODELNODE_OPCODE_GROUPSIMPLE:
			return n->Data->GroupSimple.Group1;
		}
	}
	return -1;
}

/* First-person Switch 1 gates only the muzzle flash, never the gun body. */
static int gunFlashNode(const ModelFileHeader *header, const ModelNode *node)
{
    if (header->numSwitches <= 1 || !header->Switches[1]) return 0;
    for (; node; node = node->Parent)
        if (node == header->Switches[1]) return 1;
    return 0;
}

static int retainedRigidList(const Gfx *gdl)
{
    uint32_t geometry=0;
    for(int i=0;i<128;i++) {
        unsigned op=(gdl[i].words.w0>>24)&255;
        if(op==(G_SETGEOMETRYMODE&255))geometry|=gdl[i].words.w1;
        if(op==(G_CLEARGEOMETRYMODE&255))geometry&=~gdl[i].words.w1;
        if(op==(G_VTX&255) || op==G_GEVR_WORLD_VTX)
            return !(geometry&(G_LIGHTING|G_TEXTURE_GEN));
        if(op==(G_ENDDL&255) || op==(G_DL&255))return 0;
    }
    return 0;
}

/* An emitted list as a node draws it: retained on the GPU where it can be, and
 * under the node's own matrix. NULL when out of memory. */
static Gfx *wrapEmitted(ModelNode *n, Gfx *gdl)
{
	{
        /* Rigid props, guns and gloves retain their material commands and CPU
         * fallback. Gun storage retires after reload; other storage lasts
         * until the level reset. */
        if (!s_emittedSkin && s_nrigidMeshes < MAX_ALLOC && retainedRigidList(gdl)) {
            GevrStaticMesh *mesh = gevrStaticMeshBuild(gdl);
            /* gevr_rigid_min.txt <N>: props under N triangles take the CPU
             * path again (the A/B switch; 0 retains every prop) */
            if(mesh && s_registrationKind==BEAN_PROP && mesh->numIndices<(uint32_t)s_rigidMinimum*3) {
                gevrStaticMeshFree(mesh);mesh=NULL;
            }
            Gfx *gpu = mesh ? malloc(2 * sizeof(Gfx)) : NULL;
            if (gpu && keepAlloc(gpu)) {
                s_rigidGroups[s_nrigidMeshes] = s_registrationGroup;
                s_rigidMeshes[s_nrigidMeshes++] = mesh;
                gpu[0].words.w0 = (uintptr_t)G_GEVR_MESH << 24;
                gpu[0].words.w1 = (uintptr_t)mesh;
                gSPBranchList(gpu + 1, gdl);
                gdl = gpu;
            } else { free(gpu); gevrStaticMeshFree(mesh); }
        }
        int matrix = nodeMatrix(n);
        if (s_emittedSkin)
			s_emittedSkin->anchor = matrix;
		if (matrix >= 0) {
			Gfx *wrapper = malloc(3 * sizeof(Gfx));
			if (!wrapper || !keepAlloc(wrapper)) {
				free(wrapper);
				return NULL;
			}
			gSPMatrix(wrapper, (void *)(uintptr_t)(0x03000000u + matrix * sizeof(Mtx)),
			          G_MTX_MODELVIEW | G_MTX_LOAD | G_MTX_NOPUSH);
			gSPDisplayList(wrapper + 1, gdl);
			gSPEndDisplayList(wrapper + 2);
			gdl = wrapper;
		}
	}
	return gdl;
}

static void registerPrimary(ModelNode *n, Gfx *gdl, int blankType)
{
	int op = nodeOp(n);
	Gfx *old = NULL;
	int oldType = 0;
	Gfx *raw = gdl;
	if (gdl && gdl != s_blank && !(gdl = wrapEmitted(n, gdl)))
		return;
	if (op == MODELNODE_OPCODE_DL && n->Data) {
		old = n->Data->DisplayList.Primary;
		oldType = n->Data->DisplayList.ModelType;
		swapAdd(n, old, gdl, oldType);
	} else if (op == MODELNODE_OPCODE_DLCOLLISION && n->Data) {
		old = n->Data->DisplayListCollisions.Primary;
		oldType = n->Data->DisplayListCollisions.ModelType;
		swapAdd(n, old, gdl, oldType);
	}
	struct beanSwap *registered = swapOf(n);
	if (registered && gdl != s_blank)
		registered->deferredMatrix = nodeMatrix(n) < 0;
	if (registered && raw && raw == s_lastEmit && !registered->skin && !registered->emitted) {
		registered->emitted = raw;
		registered->emittedVtx = s_lastEmitVtx;
		registered->emittedVerts = s_lastEmitVerts;
		registered->emittedCommands = s_lastEmitCommands;
		registered->emittedWorld = s_lastEmitWorld;
		registered->opaqueOnly = s_lastEmitOpaque;
		for (int k = 0; k < 3; k++) {
			registered->box[0][k] = 1e30f;
			registered->box[1][k] = -1e30f;
		}
		for (int v = 0; v < s_lastEmitVerts; v++)
			for (int k = 0; k < 3; k++) {
				const float p = s_lastEmitWorld ? ((const GevrWorldVertex *)s_lastEmitVtx)[v].p[k]
				                                : ((const Vtx *)s_lastEmitVtx)[v].v.ob[k];
				if (p < registered->box[0][k]) registered->box[0][k] = p;
				if (p > registered->box[1][k]) registered->box[1][k] = p;
			}
		registered->hasBox = s_lastEmitVerts > 0;
	}
	(void)blankType;
}

/*
 * GoldenEye stands its own cameras (the opening's stills, the swirl) against
 * and inside props: its lists cull their backs, so from inside a gas tank it
 * sees past it. Bean's props are two-sided (DAB: a truck cab's and a gate's
 * far walls are single planes), so the camera saw the tank's inside, a dark
 * green screen (user, 2026-10-14, Facility). While such a camera stands inside
 * a Bean prop's bounds, that prop culls its backs.
 */
extern int gevrBeanWorldAuthoredView(void);
int gevrBeanCameraInside(void *node, struct Model *model)
{
    struct beanSwap *s = activeSwap(node);
    if (!s || !s->hasBox || !model || !model->render_pos || !model->obj || !gevrBeanWorldAuthoredView())
        return 0;
    const int matrix = nodeMatrix(node);
    if (matrix < 0 || matrix >= model->obj->numMatrices)
        return 0;
    /* the node's matrix to the camera, row vectors: the camera in the node's space */
    const float (*m)[4] = model->render_pos[matrix].pos.m;
    const float s2 = m[0][0] * m[0][0] + m[0][1] * m[0][1] + m[0][2] * m[0][2];
    if (s2 < 1e-12f)
        return 0;
    for (int k = 0; k < 3; k++) {
        const float p = -(m[3][0] * m[k][0] + m[3][1] * m[k][1] + m[3][2] * m[k][2]) / s2;
        if (p < s->box[0][k] - 1.0f || p > s->box[1][k] + 1.0f)
            return 0;
    }
    return 1;
}

void *gevrBeanCullMark(void *gdl, int on)
{
    Gfx *g = gdl;
    g->words.w0 = ((uintptr_t)G_GEVR_NODE_STATE << 24) | GEVR_NODE_CULL;
    g->words.w1 = on != 0;
    return g + 1;
}

#include "gevr_bean_pilot.inc.h"

struct beanGrp {
	ModelNode *node;
	float o[3];
	int sk;
	int joint;
};

struct beanDl {
	ModelNode *node;
	int grp;
	int simple;
	int modelType;
	float span;
};

struct beanWalk {
	ModelNode *node;
	int grp;
	float ox, oy, oz;
	int simple;
};

static void rotBetween(const float a[3], const float b[3], float R[9])
{
	float c = a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
	float v0 = a[1] * b[2] - a[2] * b[1];
	float v1 = a[2] * b[0] - a[0] * b[2];
	float v2 = a[0] * b[1] - a[1] * b[0];
	float s2 = v0 * v0 + v1 * v1 + v2 * v2;
	if (s2 < 1e-8f) {
		if (c >= 0.0f) {
			memset(R, 0, 9 * sizeof(float));
			R[0] = R[4] = R[8] = 1.0f;
			return;
		}
		{
			float kx = fabsf(a[1]) > 0.9f ? 1.0f : 0.0f;
			float ky = fabsf(a[1]) > 0.9f ? 0.0f : 1.0f;
			float kz = 0.0f;
			R[0] = 2 * kx * kx - 1;
			R[1] = 2 * kx * ky;
			R[2] = 2 * kx * kz;
			R[3] = 2 * ky * kx;
			R[4] = 2 * ky * ky - 1;
			R[5] = 2 * ky * kz;
			R[6] = 2 * kz * kx;
			R[7] = 2 * kz * ky;
			R[8] = 2 * kz * kz - 1;
			return;
		}
	}
	{
		float f = (1.0f - c) / s2;
		R[0] = 1 + f * (-v1 * v1 - v2 * v2);
		R[1] = -v2 + f * (v0 * v1);
		R[2] = v1 + f * (v0 * v2);
		R[3] = v2 + f * (v0 * v1);
		R[4] = 1 + f * (-v0 * v0 - v2 * v2);
		R[5] = -v0 + f * (v1 * v2);
		R[6] = -v1 + f * (v0 * v2);
		R[7] = v0 + f * (v1 * v2);
		R[8] = 1 + f * (-v0 * v0 - v1 * v1);
	}
}

static void applyR(const float R[9], const float *d, float *o)
{
	o[0] = R[0] * d[0] + R[1] * d[1] + R[2] * d[2];
	o[1] = R[3] * d[0] + R[4] * d[1] + R[5] * d[2];
	o[2] = R[6] * d[0] + R[7] * d[1] + R[8] * d[2];
}

static const int s_limbChild[GEVR_BEAN_SK_COUNT] = {
	GEVR_BEAN_SK_BACK, GEVR_BEAN_SK_NECK, -1, -1,
	5, 6, -1,
	8, 9, -1,
	11, 12, -1,
	14, 15, -1
};

static const int s_inherit[GEVR_BEAN_SK_COUNT] = {
	-1, -1, GEVR_BEAN_SK_BACK, GEVR_BEAN_SK_BASE,
	-1, -1, 5,
	-1, -1, 8,
	-1, -1, 11,
	-1, -1, 14
};

/*
 * DAB gebean.c beanGunFlashDraws(): which of a gun pickup's draws are the
 * muzzle flash GoldenEye's prop/chr<gun> models carry as geometry - quads in
 * the plane of the muzzle the game shows only for the frames a shot lasts.
 * A rigid pickup lays every draw into one list, so the flash burned for as
 * long as the gun was held or lay on the floor ("the rcp90 has the muzzle
 * flash static, same with the AR"). It is dropped: GoldenEye's own gunfire
 * node (weaponSetGunfireVisible()) still flashes for the shot.
 *
 * The test is the plane, along Bean's y (the barrel): a quad list wholly in
 * one plane at one end of the gun, or flat quad by quad within the end (the
 * Automatic Shotgun's two layers), or an alpha-tested sprite of a few
 * triangles flat at the end (the Moonraker's). Across the release's
 * twenty-five pickups DAB found the RC-P90, the AR33, the D5K, the Moonraker
 * and the Automatic Shotgun. A bit per draw, the first 64.
 */
static uint64_t beanGunFlashDraws(const struct gevrBeanMesh *mesh)
{
	float lo = 1e18f, hi = -1e18f, dlo[64], dhi[64];
	uint8_t quadsflat[64];
	int ntris[64];
	const int num = mesh->ndraws < 64 ? mesh->ndraws : 64;
	uint64_t out = 0;

	for (int di = 0; di < num; di++) {
		const struct gevrBeanDrawSrc *d = &mesh->draws[di];
		dlo[di] = 1e18f;
		dhi[di] = -1e18f;
		ntris[di] = d->idx && d->pos && d->state ? d->ntris : 0;
		quadsflat[di] = d->prim == 13;
		/* a quad list's triangles come two to a quad */
		for (int q = 0; q + 1 < ntris[di] && quadsflat[di]; q += 2) {
			float qlo = 1e18f, qhi = -1e18f;
			for (int t = q * 3; t < q * 3 + 6; t++) {
				const uint32_t v = d->idx[t];
				if (v < d->nverts && (d->state[v] & GEVR_BEAN_V_READ)) {
					qlo = d->pos[v][1] < qlo ? d->pos[v][1] : qlo;
					qhi = d->pos[v][1] > qhi ? d->pos[v][1] : qhi;
				}
			}
			if (qhi - qlo >= 1.0f)
				quadsflat[di] = 0;
		}
		for (int t = 0; t < ntris[di] * 3; t++) {
			const uint32_t v = d->idx[t];
			if (v < d->nverts && (d->state[v] & GEVR_BEAN_V_READ)) {
				dlo[di] = d->pos[v][1] < dlo[di] ? d->pos[v][1] : dlo[di];
				dhi[di] = d->pos[v][1] > dhi[di] ? d->pos[v][1] : dhi[di];
			}
		}
		if (dlo[di] <= dhi[di]) {
			lo = dlo[di] < lo ? dlo[di] : lo;
			hi = dhi[di] > hi ? dhi[di] : hi;
		}
	}
	if (hi <= lo)
		return 0;
	for (int di = 0; di < num; di++) {
		const struct gevrBeanDrawSrc *d = &mesh->draws[di];
		const float end = (hi - lo) * 0.15f;
		if (d->prim == 13 && dlo[di] <= dhi[di] &&
		    ((dhi[di] - dlo[di] < 1.0f && (dlo[di] <= lo + end || dhi[di] >= hi - end)) ||
		     (quadsflat[di] && (dhi[di] <= lo + end || dlo[di] >= hi - end))))
			out |= 1ull << di;
		if (d->prim == 4 && d->alphatest && ntris[di] > 0 && ntris[di] <= 8 && dlo[di] <= dhi[di] &&
		    dhi[di] - dlo[di] < (hi - lo) * 0.01f && (dlo[di] <= lo + end || dhi[di] >= hi - end))
			out |= 1ull << di;
	}
	return out;
}

/* the gun pickup applyMesh() is laying out, and its flash draws */
static const struct gevrBeanMesh *s_flashMesh;
static uint64_t s_flashDraws;

/* DAB gebeanBuildFirstPerson(): the matrix each of a first-person gun's
 * triangles is drawn under (gunParts()), and the one being laid out */
#define BEAN_GUN_MTX 64
struct beanGunParts {
	int8_t *of;
	int carrier[BEAN_GUN_MTX]; /* the list a matrix's part goes on, -1 none */
	int used[BEAN_GUN_MTX];    /* its triangles */
	int body;
};
static const struct beanGunParts *s_parts;
static const struct gevrBeanMesh *s_partsMesh;
static int s_partWant;
static int s_noRound;   /* laying out the launcher without its round (DAB fpRound) */
/* the watch items' pressing hand (pressHand()): per triangle 0 the rest, 1 the
 * palm, 2 the finger, and the one being laid out */
static const int8_t *s_pressOf;
static const struct gevrBeanMesh *s_pressMesh;
static int s_pressWant;

static int triWanted(const struct gevrBeanMesh *mesh, const struct gevrBeanTri *t, int kind, int hasHead)
{
	if (t->flags & GEVR_BEAN_F_FLASH) {
		return 0;
	}
	if (mesh == s_flashMesh && s_flashDraws && mesh->triSrc) {
		const int draw = mesh->triSrc[t - mesh->tris].draw;
		if (draw >= 0 && draw < 64 && (s_flashDraws >> draw & 1))
			return 0;
	}
	if (kind == BEAN_GUN && s_noRound && (t->flags & GEVR_BEAN_F_ROUND)) {
		return 0;
	}
	if (kind == BEAN_GUN && s_pressOf && mesh == s_pressMesh && t->cuff == -1 &&
	    s_pressOf[t - mesh->tris] != s_pressWant) {
		return 0;
	}
	if (kind == BEAN_GUN && s_parts && mesh == s_partsMesh) {
		return s_parts->of[t - mesh->tris] == s_partWant;
	}
	if (kind == BEAN_GUN && (t->flags & GEVR_BEAN_F_SLIDE)) {
		return 0;
	}
	if (kind == BEAN_HEADBODY) {
		return (t->flags & GEVR_BEAN_F_NECK) &&
		       (t->p[1] + t->p[4] + t->p[7]) / 3 >= mesh->bind[GEVR_BEAN_SK_NECK][1] - 30.0f;
	}
	if (hasHead && (t->flags & GEVR_BEAN_F_NECK) &&
	    (t->p[1] + t->p[4] + t->p[7]) / 3 > mesh->bind[GEVR_BEAN_SK_NECK][1] + 30.0f) {
		return 0;
	}
	return 1;
}

static void fillEmit(const struct gevrBeanTri *t, int texCut, struct gevrEmitVert *e, const float p[3], const float n[3], int corner)
{
	e->p[0] = p[0];
	e->p[1] = p[1];
	e->p[2] = p[2];
	e->n[0] = n[0];
	e->n[1] = n[1];
	e->n[2] = n[2];
	if (t->flags & GEVR_BEAN_F_DECAL) {
		e->p[0] += n[0];
		e->p[1] += n[1];
		e->p[2] += n[2];
	}
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
	e->cut = (uint8_t)(t->alphatest || (t->tex >= 0 && texCut));
	e->decal = (t->flags & GEVR_BEAN_F_DECAL) ? 1 : 0;
}

struct beanRigidBinding {
	int group[64], base, target;
	const struct beanGrp *groups;
	float pivot[MAX_GRP][3];
};
static int rigidTriangleGroup(const struct gevrBeanTri *t, const struct beanRigidBinding *binding)
{
	int winner = binding->base, best = 0;
	for (int i = 0; i < 12; i++) {
		int bone = t->bone[i], score = 0;
		if (bone >= 64 || !t->weight[i])
			continue;
		int group = binding->group[bone];
		for (int j = 0; j < 12; j++)
			if (t->bone[j] < 64 && binding->group[t->bone[j]] == group)
				score += t->weight[j];
		if (score > best) {
			best = score;
			winner = group;
		}
	}
	return winner;
}
/* Whether group g holds one of the model's display lists: only such a group
 * draws the triangles bound to it (DAB gebeanListPositionNode()). */
static int groupHasList(const struct beanDl *dls, int nd, int g)
{
	for (int i = 0; i < nd; i++)
		if (dls[i].grp == g)
			return 1;
	return 0;
}

static void rigidBindings(const struct gevrBeanMesh *mesh, const struct beanFit *fit,
                          const struct beanGrp *groups, int count, int base, struct beanRigidBinding *binding,
                          const struct beanDl *dls, int nd)
{
	binding->base = base;
	binding->groups = groups;
	for (int g = 0; g < count; g++)
		memcpy(binding->pivot[g], groups[g].o, sizeof(binding->pivot[g]));
	for (int b = 0; b < 64; b++)
		binding->group[b] = base;
	if (!fit || base < 0)
		return;
	for (int b = 0; b < mesh->nbones; b++) {
		if (!strncmp(mesh->boneName[b], "SKEL_POSITION", 13) || !strncmp(mesh->boneName[b], "SKEL_BASE", 9))
			continue;
		float at[3], best = 10000.0f;
		for (int k = 0; k < 3; k++)
			at[k] = (mesh->boneBind[b][fit->perm[k]] * fit->sign[k] - fit->bc[k]) * fit->scale + fit->nc[k] +
			        groups[base].o[k];
		for (int g = 0; g < count; g++) {
			/* a group with no list of its own draws nothing: Runway's plane and
			 * gun gave their bodies to the model's root (no list, the body's
			 * place, another matrix) and showed only the propeller and the
			 * barrel (2026-10-19). DAB binds to the lists' position nodes. */
			if (g == base || nodeMatrix(groups[g].node) == nodeMatrix(groups[base].node) || !groupHasList(dls, nd, g))
				continue;
			float dx = at[0] - groups[g].o[0], dy = at[1] - groups[g].o[1], dz = at[2] - groups[g].o[2];
			float distance = dx * dx + dy * dy + dz * dz;
			int held = nodeOp(groups[g].node) == MODELNODE_OPCODE_GROUPSIMPLE;
			if ((held && distance < 2500.0f) ||
			    (!held && dy * dy + dz * dz < 625.0f && dx * dx < 122500.0f)) {
				if (distance < best || binding->group[b] == base) {
					best = distance;
					binding->group[b] = g;
				}
			} else if (!held && distance < best) {
				best = distance;
				binding->group[b] = g;
			}
		}
	}
	/* The ending helicopter's new rotors have different hub positions. Match
	 * its remaining weighted rotor bones to distinct native held matrices. */
	if (strcmp(fit->source, "prop/helicopter"))
		return;
	int counts[64] = {0}, body = -1, taken[MAX_GRP] = {0};
	for (int t = 0; t < mesh->ntris; t++)
		for (int v = 0; v < 12; v++)
			if (mesh->tris[t].bone[v] < 64)
				counts[mesh->tris[t].bone[v]] += mesh->tris[t].weight[v];
	for (int b = 0; b < mesh->nbones; b++)
		if (body < 0 || counts[b] > counts[body])
			body = b;
	for (int b = 0; b < mesh->nbones; b++)
		binding->group[b] = base;
	for (int b = 0; b < mesh->nbones; b++) {
		if (b == body || !counts[b] || binding->group[b] != base)
			continue;
		float at[3], best = 1e30f;
		int target = -1;
		for (int k = 0; k < 3; k++)
			at[k] = (mesh->boneBind[b][fit->perm[k]] * fit->sign[k] - fit->bc[k]) * fit->scale + fit->nc[k] +
			        groups[base].o[k];
		for (int g = 0; g < count; g++) {
			if (taken[g] || nodeOp(groups[g].node) != MODELNODE_OPCODE_GROUPSIMPLE)
				continue;
			float distance = 0;
			for (int k = 0; k < 3; k++)
				distance += (at[k] - groups[g].o[k]) * (at[k] - groups[g].o[k]);
			if (distance < best) {
				best = distance;
				target = g;
			}
		}
		if (target >= 0) {
			binding->group[b] = target;
			taken[target] = 1;
			memcpy(binding->pivot[target], at, sizeof(at));
		}
	}
}

#include "gevr_bean_heads.inc.h"

/* All six sleeves share one release mesh; retain the cartridge's switches. */
static int cuffOf(const ModelFileHeader *header, ModelNode *node, int first)
{
	for (int cuff = 0; header->Switches && cuff < 6 && cuff + first < header->numSwitches; cuff++) {
		ModelNode *toggle = header->Switches[cuff + first];
		ModelNode *controls = toggle && toggle->Data && nodeOp(toggle) == MODELNODE_OPCODE_SWITCH
		                          ? toggle->Data->Switch.Controls
		                          : NULL;
		for (ModelNode *up = node; up; up = up->Parent)
			if (up == toggle || (controls && up == controls))
				return cuff;
	}
	return -1;
}
static int watchTriangleWanted(const struct gevrBeanTri *t, int cuff)
{
	/* char/suitlfhand material order from DAB; suit also dresses Connery. */
	static const int sleeve[12] = {-1, -1, -1, -1, 1, 4, 5, 0, 3, -1, -1, -1};
	if (cuff == -2)
		return 1;
	if (t->tex < 0 || t->tex >= 12)
		return cuff == -1;
	/* The release draws texture 9: its authored green/white light quad. */
	int outfit = sleeve[t->tex];
	return outfit < 0 ? cuff == -1 : cuff == outfit || (t->tex == 8 && cuff == 2);
}

/* DAB beanNodeIsToggled (gebean.c:7987): a list under a toggle is one of the
 * model's switchable pieces, which keeps its own geometry. A switch right
 * under a head's placeholder switches the whole head, not a piece. GoldenEye
 * toggles a switch through the model's part table; one it never names starts
 * visible (model.c modelInitRwData) and stays so. */
static int listToggled(const ModelFileHeader *header, const ModelNode *node)
{
	int walked = 0;
	for (node = node->Parent; node && walked < 64; node = node->Parent, walked++) {
		if (nodeOp(node) == MODELNODE_OPCODE_HEAD)
			break;
		if (nodeOp(node) != MODELNODE_OPCODE_SWITCH || !node->Parent || nodeOp(node->Parent) == MODELNODE_OPCODE_HEAD)
			continue;
		for (int s = 0; header->Switches && s < header->numSwitches; s++)
			if (header->Switches[s] == node)
				return 1;
	}
	return 0;
}

/* A first-person gun's hand and sleeve: GoldenEye shows and hides Switches
 * 8-13 and 35 together (gun.c sub_GAME_7F05E978). The watch inventory and the
 * guns guards hold (gevr_heldgun.c) draw the gun bare. */
static int gunHandNode(const ModelFileHeader *header, const ModelNode *node)
{
	for (; node; node = node->Parent)
		for (int s = 8; s <= 35 && s < header->numSwitches; s = s == 13 ? 35 : s + 1)
			if (header->Switches[s] && node == header->Switches[s])
				return 1;
	return 0;
}

/* DAB beanDrawIsGlove (gebean.c:11505): GoldenEye's glove is the HD gun's one
 * 512x511 picture. It is drawn where GoldenEye draws the hand. */
static int gunGloveTri(const struct gevrBeanMesh *mesh, const struct gevrBeanTri *t)
{
	return t->tex >= 0 && t->tex < mesh->ntex && mesh->texW[t->tex] == 512 && mesh->texH[t->tex] == 511;
}

/* DAB gebean.c:10560: an odd permutation swaps two axes and each negative
 * sign mirrors once. A mirrored fit's triangles are wound the other way. */
static int fitMirrors(const struct beanFit *fit)
{
	int mirror = (fit->perm[0] == 0) + (fit->perm[1] == 1) + (fit->perm[2] == 2) == 1;
	for (int k = 0; k < 3; k++)
		mirror ^= fit->sign[k] < 0;
	return mirror;
}

/* A fitted point in the native model's own rest space. */
static void fitPoint(const struct beanFit *fit, const float *src, float *out)
{
	for (int k = 0; k < 3; k++)
		out[k] = (src[fit->perm[k]] * fit->sign[k] - fit->bc[k]) * fit->scale + fit->nc[k];
}

/*
 * DAB gebeanBuildFirstPerson() (gebean.c:13447): each of the release's bones
 * goes on the nearest of GoldenEye's matrices that has a visible list, within
 * 30 units of its joint, else on the body's; a pistol's slide (part 7) goes
 * on the slide's matrix and whatever else came near it on the frame's. A
 * triangle goes where most of its corners' weight does. GoldenEye then moves
 * the release's own slide, cylinder and drum, and the whole gun stands where
 * its own lists would in every pose (the watch's leaves most matrices alone).
 */
static void gunParts(struct beanGunParts *parts, const ModelFileHeader *header, const struct gevrBeanMesh *mesh,
                     const struct beanFit *fit, const struct beanGrp *grp, const struct beanDl *dls, int nd, int best)
{
	float rest[BEAN_GUN_MTX][3];
	int bonemtx[64];
	int slide = -1, frame = -1, slides = 0;
	memset(parts, 0, sizeof(*parts));
	for (int m = 0; m < BEAN_GUN_MTX; m++)
		parts->carrier[m] = -1;
	parts->body = nodeMatrix(dls[best].node);
	if (parts->body < 0 || parts->body >= BEAN_GUN_MTX || dls[best].grp < 0)
		return;
	/* the first visible list on each matrix carries its part, from its rest */
	for (int i = 0; i < nd; i++) {
		const int m = nodeMatrix(dls[i].node);
		int far = 0;
		for (ModelNode *up = dls[i].node->Parent; up; up = up->Parent)
			far |= nodeOp(up) == MODELNODE_OPCODE_LOD && up->Data->LOD.MinDistance > 0;
		if (m < 0 || m >= BEAN_GUN_MTX || dls[i].grp < 0 || dls[i].simple || far || parts->carrier[m] >= 0 ||
		    listToggled(header, dls[i].node) || gunFlashNode(header, dls[i].node) || gunHandNode(header, dls[i].node))
			continue;
		parts->carrier[m] = i;
		memcpy(rest[m], grp[dls[i].grp].o, sizeof(rest[m]));
	}
	parts->carrier[parts->body] = best;
	memcpy(rest[parts->body], grp[dls[best].grp].o, sizeof(rest[parts->body]));
	for (int b = 0; b < 64; b++) {
		float joint[3], nearest = 30.0f * 30.0f;
		bonemtx[b] = parts->body;
		if (b == 0 || b >= mesh->nbones)
			continue;
		fitPoint(fit, mesh->boneBind[b], joint);
		for (int m = 0; m < BEAN_GUN_MTX; m++) {
			float d = 0;
			if (parts->carrier[m] < 0)
				continue;
			for (int k = 0; k < 3; k++)
				d += (joint[k] - rest[m][k]) * (joint[k] - rest[m][k]);
			if (d < nearest) {
				nearest = d;
				bonemtx[b] = m;
			}
		}
	}
	for (int t = 0; t < mesh->ntris; t++)
		slides += (mesh->tris[t].flags & GEVR_BEAN_F_SLIDE) != 0;
	if (slides && header->numSwitches > 7 && header->Switches[7] && nodeOp(header->Switches[7]) == MODELNODE_OPCODE_GROUP &&
	    header->Switches[7]->Parent) {
		slide = nodeMatrix(header->Switches[7]);
		frame = nodeMatrix(header->Switches[7]->Parent);
		if (slide < 0 || slide >= BEAN_GUN_MTX || frame < 0 || frame >= BEAN_GUN_MTX || slide == frame ||
		    parts->carrier[frame] < 0)
			slide = frame = -1;
	}
	parts->of = malloc((size_t)(mesh->ntris ? mesh->ntris : 1));
	if (!parts->of)
		return;
	for (int t = 0; t < mesh->ntris; t++) {
		const struct gevrBeanTri *tri = &mesh->tris[t];
		int m = parts->body;
		if (slide >= 0 && (tri->flags & GEVR_BEAN_F_SLIDE)) {
			m = slide;
		} else {
			int weight[BEAN_GUN_MTX] = {0}, most = 0;
			for (int c = 0; c < 12; c++) {
				int to;
				if (tri->bone[c] >= 64 || !tri->weight[c])
					continue;
				to = bonemtx[tri->bone[c]];
				if (slide >= 0 && to == slide)
					to = frame;
				weight[to] += tri->weight[c];
				if (weight[to] > most) {
					most = weight[to];
					m = to;
				}
			}
		}
		if (parts->carrier[m] < 0)
			m = parts->body;
		parts->of[t] = (int8_t)m;
		parts->used[m]++;
	}
}

/*
 * The watch laser's and the detonator's pressing hand. In the headset the
 * watch arm is drawn on its own and the hand that presses the watch is
 * GoldenEye's palm and finger lists alone (gunfire.c gevrRenderWatchGripHand),
 * so the release's right hand has to be on those lists: it is the one skinned
 * to SKEL_FINGER (its picture's triangles lie on GoldenEye's palm and finger,
 * the other hand's on GoldenEye's left hand). Its finger goes on GoldenEye's
 * finger (Switches[6]), which presses as the game moves it, and the rest on
 * the root list most of it lies nearest; the left hand and the watch stay on
 * the body's list. The sleeves are cuffOf()'s.
 */
struct beanPress {
	int8_t *of;
	int palm, finger;
};

static void pressHand(struct beanPress *press, const ModelFileHeader *header, const struct gevrBeanMesh *mesh,
                      const struct beanFit *fit, const struct beanDl *dls, int nd, int best)
{
	int fb = -1, votes[MAX_DL] = {0};
	uint8_t right[256] = {0};
	press->of = NULL;
	press->palm = press->finger = -1;
	for (int b = 0; b < mesh->nbones && b < 64; b++)
		if (!strcmp(mesh->boneName[b], "SKEL_FINGER"))
			fb = b;
	if (fb < 0 || !fit || header->numSwitches <= 6 || !header->Switches[6])
		return;
	for (int t = 0; t < mesh->ntris; t++)
		for (int c = 0; c < 12; c++)
			if (mesh->tris[t].bone[c] == fb && mesh->tris[t].weight[c] && mesh->tris[t].tex >= 0)
				right[mesh->tris[t].tex & 255] = 1;
	const int fingerMatrix = nodeMatrix(header->Switches[6]);
	for (int i = 0; i < nd; i++) {
		if (i == best || dls[i].simple || cuffOf(header, dls[i].node, 29) >= 0)
			continue;
		if (press->finger < 0 && nodeMatrix(dls[i].node) == fingerMatrix && fingerMatrix != nodeMatrix(dls[best].node))
			press->finger = i;
	}
	press->of = calloc((size_t)(mesh->ntris ? mesh->ntris : 1), 1);
	if (!press->of)
		return;
	/* the palm: the root list the right hand's triangles lie nearest */
	for (int t = 0; t < mesh->ntris; t++) {
		const struct gevrBeanTri *tri = &mesh->tris[t];
		if (tri->cuff != -1 || tri->tex < 0 || !right[tri->tex & 255])
			continue;
		int weight = 0, total = 0;
		for (int c = 0; c < 12; c++) {
			total += tri->weight[c];
			weight += tri->bone[c] == fb ? tri->weight[c] : 0;
		}
		press->of[t] = weight * 2 > total && press->finger >= 0 ? 2 : 1;
		if (press->of[t] != 1)
			continue;
		float c[3] = {0}, q[3];
		for (int k = 0; k < 3; k++) {
			fitPoint(fit, &tri->p[k * 3], q);
			for (int a = 0; a < 3; a++)
				c[a] += q[a] / 3;
		}
		int nearest = -1;
		float bestd = 1e30f;
		for (int i = 0; i < nd; i++) {
			Vertex *v;
			int vc, type;
			Gfx *primary;
			float rest[3] = {0};
			if (i == press->finger || dls[i].simple || dls[i].grp < 0 || cuffOf(header, dls[i].node, 29) >= 0 ||
			    nodeMatrix(dls[i].node) != nodeMatrix(dls[best].node))
				continue;
			dlVerts(dls[i].node, &v, &vc, &type, &primary);
			for (ModelNode *up = dls[i].node; up; up = up->Parent) {
				const coord3d *o = nodeOp(up) == MODELNODE_OPCODE_GROUP && up->Data ? &up->Data->Group.Origin : NULL;
				for (int a = 0; o && a < 3; a++)
					rest[a] += o->f[a];
			}
			for (int j = 0; j < vc; j++) {
				float d = 0;
				for (int a = 0; a < 3; a++)
					d += (c[a] - v[j].coord.AsArray[a] - rest[a]) * (c[a] - v[j].coord.AsArray[a] - rest[a]);
				if (d < bestd) {
					bestd = d;
					nearest = i;
				}
			}
		}
		if (nearest >= 0)
			votes[nearest]++;
	}
	for (int i = 0; i < nd; i++)
		if (i != best && votes[i] && (press->palm < 0 || votes[i] > votes[press->palm]))
			press->palm = i;
	for (int t = 0; t < mesh->ntris; t++)
		if (press->of[t] == 1 && press->palm < 0)
			press->of[t] = 0;
}

/* An untoggled list on a matrix that took a part: its own geometry gives way to the part's. */
static int gunPartList(const struct beanGunParts *parts, const ModelFileHeader *header, const struct beanDl *dl)
{
	const int m = nodeMatrix(dl->node);
	return parts && parts->of && m >= 0 && m < BEAN_GUN_MTX && m != parts->body && parts->used[m] && !dl->simple &&
	       !listToggled(header, dl->node) && !gunFlashNode(header, dl->node) && !gunHandNode(header, dl->node);
}

/* rest: the rest of the matrix the list is drawn under, taken off every vertex
 * (DAB gebean.c:12941). glove: -1 every triangle, 0 not the glove, 1 the glove. */
#include "gevr_bean_chrome.inc.h"
#include "gevr_bean_screens.inc.h"

static int emitRigid(const struct gevrBeanMesh *mesh, int kind, int hasHead, ModelNode *host,
                     const struct beanFit *fit, const struct beanRigidBinding *binding, int cuff,
                     const float *rest, int glove, void **gdl, void **vtx)
{
	if (kind == BEAN_HEAD || kind == BEAN_HEADBODY)
		return emitHead(mesh, kind, gdl, vtx);
	Vertex *hv;
	int hc, ht;
	Gfx *hp;
	float bmin[3], bmax[3], hmin[3], hmax[3];
	float bext = 0, hext = 0, scale, bc[3], hc_[3];
	int n = 0, cap = 0;
	struct gevrEmitVert *ev = NULL;
	int any = 0;
	dlVerts(host, &hv, &hc, &ht, &hp);
	/* A fitted Bean part already has its vertices, scale and local origin.
	 * Its native node can be only a carrier (no native vertices/list). The
	 * Runway guard keeps bones off listless groups, but rejecting such a
	 * carrier here still dropped its support/body while other parts drew. */
	if (!fit && (!hv || hc <= 0)) {
		return 0;
	}
	for (int k = 0; k < 3; k++) {
		bmin[k] = 1e9f;
		bmax[k] = -1e9f;
		hmin[k] = 1e9f;
		hmax[k] = -1e9f;
	}
	for (int i = 0; i < mesh->ntris; i++) {
		if (!triWanted(mesh, &mesh->tris[i], kind, hasHead)) {
			continue;
		}
		any = 1;
		for (int c = 0; c < 9; c++) {
			float p = mesh->tris[i].p[c];
			int k = c % 3;
			if (p < bmin[k]) bmin[k] = p;
			if (p > bmax[k]) bmax[k] = p;
		}
	}
	if (!any) {
		return 0;
	}
	for (int i = 0; !fit && i < hc; i++) {
		for (int k = 0; k < 3; k++) {
			float c = hv[i].coord.AsArray[k];
			if (c < hmin[k]) hmin[k] = c;
			if (c > hmax[k]) hmax[k] = c;
		}
	}
	for (int k = 0; k < 3; k++) {
		if (bmax[k] - bmin[k] > bext) bext = bmax[k] - bmin[k];
		if (hmax[k] - hmin[k] > hext) hext = hmax[k] - hmin[k];
		bc[k] = (bmin[k] + bmax[k]) * 0.5f;
		hc_[k] = (hmin[k] + hmax[k]) * 0.5f;
	}
	scale = bext > 1e-4f ? hext / bext : 1.0f;
	int mirror = fit && fitMirrors(fit);
	for (int i = 0; i < mesh->ntris; i++) {
		const struct gevrBeanTri *t = &mesh->tris[i];
		if (!triWanted(mesh, t, kind, hasHead) || (mesh->triggerCuffs && t->cuff != cuff) ||
		    (binding && rigidTriangleGroup(t, binding) != binding->target) ||
		    (glove >= 0 && gunGloveTri(mesh, t) != glove)) {
			continue;
		}
		if (n + 3 > cap) {
			cap = cap ? cap * 2 : 1024;
			ev = realloc(ev, (size_t)cap * sizeof(*ev));
			if (!ev) {
				return 0;
			}
		}
		int dropped = 0;
		for (int c = 0; c < 3 && !dropped; c++) {
			float p[3], nr[3];
			int screen = 0;
			if (fit)
				fitPoint(fit, &t->p[c * 3], p);
			/* DAB gebeanBuildRigid(): GoldenEye's monitor screens, before a
			 * part's place is taken off (gevr_bean_screens.inc.h) */
			if (fit && s_screenBuild)
				screen = beanScreenVertex(s_screenBuild, mesh, i, c, p);
			if (screen < 0) {
				dropped = 1;
				break;
			}
			for (int k = 0; k < 3; k++) {
				if (!fit)
					p[k] = (t->p[c * 3 + k] - bc[k]) * scale + hc_[k];
				if (fit && binding && binding->base >= 0 && binding->target >= 0)
					p[k] += binding->groups[binding->base].o[k] - binding->pivot[binding->target][k];
				if (rest)
					p[k] -= rest[k];
				nr[k] = fit ? t->n[c * 3 + fit->perm[k]] * fit->sign[k] : t->n[c * 3 + k];
			}
			int slot = mirror && c ? 3 - c : c;
			fillEmit(t, t->tex >= 0 && t->tex < mesh->ntex && mesh->texCut[t->tex], &ev[n + slot], p, nr, c);
			if (screen == BEAN_SCREEN_BACKING) {
				ev[n + slot].rgba[0] = ev[n + slot].rgba[1] = ev[n + slot].rgba[2] = 0;
				ev[n + slot].rgba[3] = 255;
			}
		}
		if (dropped)
			continue;
		n += 3;
	}
	{
		if (!n) {
			free(ev);
			return 0;
		}
		/* DAB xblamesh.c:7171: characters cull their back faces as the N64 models
		 * do; props and guns stay two-sided (Dam's truck cab and gates lost their
		 * far walls when they culled) */
		int ok = mesh->nativeChrome ? emitChrome(ev, n, host, gdl, vtx)
		                           : gevrBeanEmitList(mesh, ev, n, kind == BEAN_CHR, kind == BEAN_GUN, kind == BEAN_CHR, 1, gdl, vtx);
		free(ev);
		return ok;
	}
}

static int emitSkinned(const struct gevrBeanMesh *mesh, int sk, int hasHead,
                       const float R[GEVR_BEAN_SK_COUNT][9], float scale, const struct beanGrp *grp, int ng,
                       const ModelFileHeader *header, int cuff, int whole, void **gdl, void **vtx)
{
	int n = 0, cap = 0;
	struct beanSkinVert *skin = calloc((size_t)mesh->ntris * 3, sizeof(*skin));
	if (!skin)
		return 0;
	int matrices[16];
	for (int i = 0; i < 16; i++)
		matrices[i] = -1;
	for (int i = 0; i < ng; i++)
		if (grp[i].sk >= 0)
			matrices[grp[i].sk] = nodeMatrix(grp[i].node);
	matrices[GEVR_BEAN_SK_POS] = matrices[GEVR_BEAN_SK_BASE];
	struct gevrEmitVert *ev = NULL;
	if (!mesh->haveBind[sk]) {
		free(skin);
		return 0;
	}
	for (int i = 0; i < mesh->ntris; i++) {
		const struct gevrBeanTri *t = &mesh->tris[i];
		int use = whole ||
		    t->sk == sk || (sk == GEVR_BEAN_SK_BASE && (t->sk == GEVR_BEAN_SK_POS || !mesh->haveBind[t->sk]));
		if (!use || !triWanted(mesh, t, BEAN_CHR, hasHead) || !watchTriangleWanted(t, cuff)) {
			continue;
		}
		if (n + 3 > cap) {
			cap = cap ? cap * 2 : 256;
			ev = realloc(ev, (size_t)cap * sizeof(*ev));
			if (!ev) {
				free(skin);
				return 0;
			}
		}
		for (int c = 0; c < 3; c++) {
			float d[3], p[3], nr[3], nn[3];
			for (int k = 0; k < 3; k++) {
				d[k] = (t->p[c * 3 + k] - mesh->bind[sk][k]) * scale;
				nr[k] = t->n[c * 3 + k];
			}
            int partSk=whole ? t->sk : sk;
            if(partSk==GEVR_BEAN_SK_POS || !mesh->haveBind[partSk])partSk=GEVR_BEAN_SK_BASE;
            for (int w = 0; w < 4; w++) {
                int bone = t->bone[c * 4 + w];
				int bs = bone < mesh->nbones ? mesh->boneSk[bone] : partSk;
				if (bs == GEVR_BEAN_SK_POS)
					bs = GEVR_BEAN_SK_BASE;
				if (matrices[bs] < 0 || !mesh->haveBind[bs])
					bs = partSk;
				skin[n + c].weight[w] = t->weight[c * 4 + w];
				skin[n + c].matrix[w] = matrices[bs];
				float local[3];
				for (int k = 0; k < 3; k++)
					local[k] = (t->p[c * 3 + k] - mesh->bind[bs][k]) * scale;
				applyR(R[bs], local, skin[n + c].p[w]);
				applyR(R[bs], nr, skin[n + c].n[w]);
				if (cuff != -2 && bone < mesh->nbones && header->Switches) {
					static const char *const hands[3] = {"SKEL_HOUR", "SKEL_MINUTE", "SKEL_SECOND"};
					for (int h = 0; h < 3 && h < header->numSwitches; h++) {
						ModelNode *hand = header->Switches[h];
						if (strcmp(mesh->boneName[bone], hands[h]) || !hand || !hand->Data ||
						    nodeOp(hand) != MODELNODE_OPCODE_GROUPSIMPLE)
							continue;
						skin[n + c].matrix[w] = hand->Data->GroupSimple.Group1;
						for (int k = 0; k < 3; k++)
							skin[n + c].p[w][k] -= hand->Data->GroupSimple.Origin.f[k];
					}
				}
			}
			applyR(R[sk], d, p);
			applyR(R[sk], nr, nn);
			fillEmit(t, t->tex >= 0 && t->tex < mesh->ntex && mesh->texCut[t->tex], &ev[n + c], p, nn, c);
		}
		n += 3;
	}
	if (n < 3) {
		free(ev);
		free(skin);
		return 0;
	}
	{
		s_emitSkin = skin;
		int ok = gevrBeanEmitList(mesh, ev, n, 1, 0, 1, 1, gdl, vtx);
		s_emitSkin = NULL;
		free(skin);
		free(ev);
		return ok;
	}
}

#include "gevr_bean_pose.inc.h"
#include "gevr_bean_preview.inc.h"

static void classifyGroups(struct beanGrp *grp, int ng)
{
	int base = -1;
	float baseY = 1e9f;
	int limbCount[4] = { 0, 0, 0, 0 };
	int limbIdx[4][8];
	float limbDist[4][8];
	for (int i = 0; i < ng; i++) {
		grp[i].sk = -1;
	}
	for (int i = 0; i < ng; i++) {
		if (nodeOp(grp[i].node) == MODELNODE_OPCODE_HEADER) {
			baseY = grp[i].o[1];
			base = i;
		}
	}
	if (base < 0 && ng > 0) {
		base = 0;
		baseY = grp[0].o[1];
		for (int i = 1; i < ng; i++) {
			if (grp[i].o[1] < baseY) {
				baseY = grp[i].o[1];
				base = i;
			}
		}
	}
	if (base >= 0) {
		grp[base].sk = GEVR_BEAN_SK_BASE;
        /* GoldenEye retains a pelvis GROUP under its HEADER, on the same
         * root rest position, with its own rotation matrix. DAB's converted model
         * folds that alias into CHRINFO. It is another base here, not BACK:
         * treating it as BACK shifts the torso and every leg assignment. */
        for (int i = 0; i < ng; i++)
            if (i != base && grp[i].joint == 1 &&
                fabsf(grp[i].o[0] - grp[base].o[0]) < 0.001f &&
                fabsf(grp[i].o[1] - grp[base].o[1]) < 0.001f &&
                fabsf(grp[i].o[2] - grp[base].o[2]) < 0.001f)
                grp[i].sk = GEVR_BEAN_SK_BASE;
	}
	for (int i = 0; i < ng; i++) {
		if (grp[i].sk >= 0 || nodeOp(grp[i].node) == MODELNODE_OPCODE_GROUPSIMPLE) {
			continue;
		}
		if (fabsf(grp[i].o[0]) < 30.0f && fabsf(grp[i].o[2]) < 30.0f) {
			if (grp[i].o[1] < 200.0f && grp[i].sk < 0) {
				int have = 0;
				for (int j = 0; j < ng; j++) {
					have |= grp[j].sk == GEVR_BEAN_SK_BACK;
				}
				if (!have) {
					grp[i].sk = GEVR_BEAN_SK_BACK;
				}
			} else if (grp[i].o[1] >= 200.0f) {
				int have = 0;
				for (int j = 0; j < ng; j++) {
					have |= grp[j].sk == GEVR_BEAN_SK_NECK;
				}
				if (!have) {
					grp[i].sk = GEVR_BEAN_SK_NECK;
				}
			}
		}
	}
	for (int i = 0; i < ng; i++) {
		float dx, dy, dz, dist;
		int bucket;
		if (grp[i].sk >= 0 || nodeOp(grp[i].node) == MODELNODE_OPCODE_GROUPSIMPLE) {
			continue;
		}
		bucket = (grp[i].o[1] > 200.0f ? 0 : 2) + (grp[i].o[0] > 0.0f ? 0 : 1);
		dx = grp[i].o[0] - (base >= 0 ? grp[base].o[0] : 0);
		dy = grp[i].o[1] - (base >= 0 ? grp[base].o[1] : 0);
		dz = grp[i].o[2] - (base >= 0 ? grp[base].o[2] : 0);
		dist = fabsf(grp[i].o[0]);
		(void)dx;
		(void)dy;
		(void)dz;
		if (limbCount[bucket] < 8) {
			int at = limbCount[bucket]++;
			while (at > 0 && limbDist[bucket][at - 1] > dist) {
				limbDist[bucket][at] = limbDist[bucket][at - 1];
				limbIdx[bucket][at] = limbIdx[bucket][at - 1];
				at--;
			}
			limbDist[bucket][at] = dist;
			limbIdx[bucket][at] = i;
		}
	}
	{
		static const int skOf[4][3] = {
			{ 4, 5, 6 },
			{ 7, 8, 9 },
			{ 10, 11, 12 },
			{ 13, 14, 15 }
		};
		for (int b = 0; b < 4; b++) {
			int n = limbCount[b] > 3 ? 3 : limbCount[b];
			for (int k = 0; k < n; k++) {
				grp[limbIdx[b][k]].sk = skOf[b][k];
			}
		}
	}
}

static void buildRot(const struct gevrBeanMesh *mesh, const struct beanGrp *grp, int ng,
		float R[GEVR_BEAN_SK_COUNT][9], float *scaleOut)
{
	float n64[GEVR_BEAN_SK_COUNT][3];
	int have[GEVR_BEAN_SK_COUNT];
	float nlen = 0, blen = 0;
	memset(have, 0, sizeof(have));
	for (int s = 0; s < GEVR_BEAN_SK_COUNT; s++) {
		memset(R[s], 0, 9 * sizeof(float));
		R[s][0] = R[s][4] = R[s][8] = 1.0f;
	}
	for (int i = 0; i < ng; i++) {
		if (grp[i].sk >= 0 && grp[i].sk < GEVR_BEAN_SK_COUNT && !have[grp[i].sk]) {
			have[grp[i].sk] = 1;
			n64[grp[i].sk][0] = grp[i].o[0];
			n64[grp[i].sk][1] = grp[i].o[1];
			n64[grp[i].sk][2] = grp[i].o[2];
		}
	}
	for (int s = 0; s < GEVR_BEAN_SK_COUNT; s++) {
		int c = s_limbChild[s];
		float db[3], dn[3], lb, ln;
		if (s == GEVR_BEAN_SK_BASE || c < 0 || !have[s] || !have[c] || !mesh->haveBind[s] || !mesh->haveBind[c]) {
			continue;
		}
		for (int k = 0; k < 3; k++) {
			db[k] = mesh->bind[c][k] - mesh->bind[s][k];
			dn[k] = n64[c][k] - n64[s][k];
		}
		lb = sqrtf(db[0] * db[0] + db[1] * db[1] + db[2] * db[2]);
		ln = sqrtf(dn[0] * dn[0] + dn[1] * dn[1] + dn[2] * dn[2]);
		blen += lb;
		nlen += ln;
	}
	*scaleOut = blen > 1e-3f ? nlen / blen : 1.0f;
	for (int s = 0; s < GEVR_BEAN_SK_COUNT; s++) {
		int c = s_limbChild[s];
		float db[3], dn[3], lb, ln;
		if (c < 0 || !have[s] || !have[c] || !mesh->haveBind[s] || !mesh->haveBind[c]) {
			continue;
		}
		for (int k = 0; k < 3; k++) {
			db[k] = mesh->bind[c][k] - mesh->bind[s][k];
			dn[k] = n64[c][k] - n64[s][k];
		}
		lb = sqrtf(db[0] * db[0] + db[1] * db[1] + db[2] * db[2]);
		ln = sqrtf(dn[0] * dn[0] + dn[1] * dn[1] + dn[2] * dn[2]);
		if (lb < 1e-4f || ln < 1e-4f) {
			continue;
		}
		for (int k = 0; k < 3; k++) {
			db[k] /= lb;
			dn[k] /= ln;
		}
		rotBetween(db, dn, R[s]);
	}
	for (int s = 0; s < GEVR_BEAN_SK_COUNT; s++) {
		int inh = s_inherit[s];
		int c = s_limbChild[s];
		if (inh >= 0 && (c < 0 || !have[s] || !have[c])) {
			memcpy(R[s], R[inh], sizeof(R[s]));
		}
	}
	float rotations[16][9];
	memcpy(rotations, R, sizeof(rotations));
	for (int s = 0; s < 16; s++) {
		int child = s_limbChild[s];
		if (child < 0 || !have[s] || !have[child] || !mesh->haveBind[s] || !mesh->haveBind[child])
			continue;
		float u[3], native[3], length = 0, nlength = 0;
		for (int k = 0; k < 3; k++) {
			u[k] = mesh->bind[child][k] - mesh->bind[s][k];
			native[k] = n64[child][k] - n64[s][k];
			length += u[k] * u[k];
			nlength += native[k] * native[k];
		}
		length = sqrtf(length);
		nlength = sqrtf(nlength);
		if (length < 1e-6f || *scaleOut < 1e-6f)
			continue;
		for (int k = 0; k < 3; k++)
			u[k] /= length;
		float stretch[9], ratio = nlength / (length * *scaleOut);
		for (int i = 0; i < 3; i++)
			for (int j = 0; j < 3; j++)
				stretch[i * 3 + j] = (i == j) + (ratio - 1) * u[i] * u[j];
		for (int i = 0; i < 3; i++)
			for (int j = 0; j < 3; j++) {
				R[s][i * 3 + j] = 0;
				for (int k = 0; k < 3; k++)
					R[s][i * 3 + j] += rotations[s][i * 3 + k] * stretch[k * 3 + j];
			}
	}
	static const int branches[2][3] = {{1, 10, 13}, {2, 4, 7}};
	for (int s = 0; s < 2; s++) {
		float b[4][3], j[4][3], covariance[9] = {0}, gram[9] = {0}, inverse[9], length = 0, targetLength = 0;
		int valid = have[s] && mesh->haveBind[s];
		for (int c = 0; c < 3; c++) {
			int child = branches[s][c];
			valid &= have[child] && mesh->haveBind[child];
			if (!valid)
				break;
			for (int k = 0; k < 3; k++) {
				b[c][k] = (mesh->bind[child][k] - mesh->bind[s][k]) * *scaleOut;
				j[c][k] = n64[child][k] - n64[s][k];
			}
		}
		if (!valid)
			continue;
		for (int k = 0; k < 3; k++) {
			b[3][k] = b[1][(k + 1) % 3] * b[2][(k + 2) % 3] - b[1][(k + 2) % 3] * b[2][(k + 1) % 3];
			j[3][k] = j[1][(k + 1) % 3] * j[2][(k + 2) % 3] - j[1][(k + 2) % 3] * j[2][(k + 1) % 3];
			length += b[3][k] * b[3][k];
			targetLength += j[3][k] * j[3][k];
		}
		if (length < 1e-12f || targetLength < 1e-12f)
			continue;
		for (int k = 0; k < 3; k++)
			j[3][k] *= sqrtf(length / targetLength);
		for (int p = 0; p < 4; p++)
			for (int r = 0; r < 3; r++)
				for (int c = 0; c < 3; c++) {
					covariance[r * 3 + c] += j[p][r] * b[p][c];
					gram[r * 3 + c] += b[p][r] * b[p][c];
				}
		if (!beanInverse3(gram, inverse))
			continue;
		for (int r = 0; r < 3; r++)
			for (int c = 0; c < 3; c++) {
				R[s][r * 3 + c] = 0;
				for (int k = 0; k < 3; k++)
					R[s][r * 3 + c] += covariance[r * 3 + k] * inverse[k * 3 + c];
			}
	}
	memcpy(R[GEVR_BEAN_SK_POS], R[GEVR_BEAN_SK_BASE], sizeof(R[0]));
}

static void applyMesh(ModelFileHeader *header, const struct gevrBeanMesh *mesh, int kind,
                      const struct beanFit *fit)
{
	struct beanWalk *stack;
	ModelNode **seen;
	int nseen = 0, sp = 0, ng = 0, nd = 0, hasHead = 0;
	struct beanGrp *grp;
	struct beanDl *dls;
	float R[GEVR_BEAN_SK_COUNT][9];
	float scale = 1.0f;
	int skinned;
	blankInit();
	if (!header || !header->RootNode || !mesh) {
		return;
	}
    s_registrationKind=kind;
    FILE *minimum=gevrBeanControlOpen("gevr_rigid_min.txt");
    /* Every prop is retained: Jungle's ~100 small palms and plants posed on
     * the CPU were half its record (DRAW/TICK 7.3 -> 3.7 ms in 2D at the first
     * drone gun), and once static meshes took a vertex shader without the
     * skin code (gfx_opengl.cpp s_buildStatic) they cost the GPU no more than
     * the CPU's batches: VR start view 100 -> 120 (2026-10-20). */
    s_rigidMinimum=0;
    if(minimum) {
        int value=0;
        if(fscanf(minimum,"%d",&value)==1 && value>=0 && value<=1000000)s_rigidMinimum=value;
        fclose(minimum);
    }
	s_registrationOwner = header;
	s_registrationWatchArm = kind == BEAN_CHR && mesh->ntex == 12 && header->numSwitches == 10;
	stack = calloc(MAX_WALK, sizeof(*stack));
	seen = calloc(MAX_WALK, sizeof(*seen));
	grp = calloc(MAX_GRP, sizeof(*grp));
	dls = calloc(MAX_DL, sizeof(*dls));
	if (!stack || !seen || !grp || !dls) {
		free(stack);
		free(seen);
		free(grp);
		free(dls);
		return;
	}
	stack[sp].node = header->RootNode;
	stack[sp].grp = -1;
	sp = 1;
	while (sp > 0 && nseen < MAX_WALK) {
		struct beanWalk fr = stack[--sp];
		ModelNode *n = fr.node;
		int op, known = 0;
		if (!n) {
			continue;
		}
		for (int i = 0; i < nseen; i++) {
			if (seen[i] == n) {
				known = 1;
				break;
			}
		}
		if (known) {
			continue;
		}
		seen[nseen++] = n;
		op = nodeOp(n);
		if (op == MODELNODE_OPCODE_HEAD) {
			hasHead = 1;
		}
		if ((op == MODELNODE_OPCODE_GROUP || op == MODELNODE_OPCODE_HEADER ||
		     op == MODELNODE_OPCODE_GROUPSIMPLE) &&
		    n->Data && ng < MAX_GRP) {
			grp[ng].node = n;
			coord3d origin = {0};
			if (op == MODELNODE_OPCODE_GROUP)
				origin = n->Data->Group.Origin;
			if (op == MODELNODE_OPCODE_GROUPSIMPLE)
				origin = n->Data->GroupSimple.Origin;
			grp[ng].o[0] = fr.ox + origin.x;
			grp[ng].o[1] = fr.oy + origin.y;
			grp[ng].o[2] = fr.oz + origin.z;
			grp[ng].joint = op == MODELNODE_OPCODE_GROUP ? n->Data->Group.JointID : 0;
			grp[ng].sk = -1;
			if (n->Next && sp < MAX_WALK) {
				stack[sp] = fr;
				stack[sp].node = n->Next;
				sp++;
			}
			ModelNode *child = n->Child;
			if (!child && op == MODELNODE_OPCODE_HEADER)
				child = n->Data->Header.FirstGroupNode;
			if (!child && op == MODELNODE_OPCODE_GROUP)
				child = n->Data->Group.ChildGroupNode;
			if (child && sp < MAX_WALK) {
				stack[sp].node = child;
				stack[sp].grp = ng;
				stack[sp].ox = grp[ng].o[0];
				stack[sp].oy = grp[ng].o[1];
				stack[sp].oz = grp[ng].o[2];
				stack[sp].simple = fr.simple || op == MODELNODE_OPCODE_GROUPSIMPLE;
				sp++;
			}
			ng++;
			continue;
		}
		if ((op == MODELNODE_OPCODE_DL || op == MODELNODE_OPCODE_DLCOLLISION) && nd < MAX_DL) {
			Vertex *vv;
			int vc, vt;
			Gfx *vp;
			dlVerts(n, &vv, &vc, &vt, &vp);
			dls[nd].node = n;
			dls[nd].grp = fr.grp;
			dls[nd].simple = fr.simple || op == MODELNODE_OPCODE_GROUPSIMPLE;
			dls[nd].modelType = vt;
			dls[nd].span = dlSpan(n);
			nd++;
		}
		if (n->Data && sp + 2 < MAX_WALK) {
			ModelNode *a = op == MODELNODE_OPCODE_SWITCH ? n->Data->Switch.Controls
			               : op == MODELNODE_OPCODE_LOD  ? n->Data->LOD.Affects
			               : op == MODELNODE_OPCODE_BSP  ? n->Data->BSP.leftChild
			                                             : NULL;
			ModelNode *b = op == MODELNODE_OPCODE_BSP ? n->Data->BSP.rightChild : NULL;
			if (a && a != n->Child) {
				stack[sp] = fr;
				stack[sp++].node = a;
			}
			if (b && b != n->Child) {
				stack[sp] = fr;
				stack[sp++].node = b;
			}
		}
		if (n->Next && sp < MAX_WALK) {
			stack[sp] = fr;
			stack[sp].node = n->Next;
			sp++;
		}
		if (n->Child && sp < MAX_WALK) {
			stack[sp] = fr;
			stack[sp].node = n->Child;
			stack[sp].simple = fr.simple || op == MODELNODE_OPCODE_GROUPSIMPLE;
			sp++;
		}
	}
	/* Register only if every replacement fits the level allocation limits. */
	if (s_nswap + nd + 1 > MAX_SWAP || s_nalloc + nd * 5 > MAX_ALLOC) {
		free(stack); free(seen); free(grp); free(dls);
		return;
	}
	skinned = kind == BEAN_CHR;
	if (kind == BEAN_HEADBODY || kind == BEAN_HEAD || kind == BEAN_GUN) {
		skinned = 0;
	}
	if (skinned) {
		int found = 0;
		void *sharedG[GEVR_BEAN_SK_COUNT];
		void *sharedV[GEVR_BEAN_SK_COUNT];
		struct beanSkin *sharedSkin[GEVR_BEAN_SK_COUNT] = {0};
		classifyGroups(grp, ng);
		int watchArm = mesh->ntex == 12 && header->numSwitches == 10;
		int bareDrawn[GEVR_BEAN_SK_COUNT] = {0};
		if (watchArm)
			for (int i = 0; i < ng; i++)
				if (grp[i].sk == GEVR_BEAN_SK_NECK)
					grp[i].sk = -1;
		buildRot(mesh, grp, ng, R, &scale);
		for (int s = 0; s < GEVR_BEAN_SK_COUNT; s++) {
			sharedG[s] = NULL;
			sharedV[s] = NULL;
		}
		for (int i = 0; i < ng; i++) {
			if (grp[i].sk >= 0) {
				found++;
			}
		}
		if (found < 4) {
			skinned = 0;
		} else {
            /* Diagnostic only. Native distance alternatives share the same HD
             * geometry; real visibility switches still require separate limbs. */
            int combined=0; void *combinedG=NULL,*combinedV=NULL;
            FILE *batch=gevrBeanControlOpen("gevr_body_batch.txt"); int batchOn=1;
            if(batch){fscanf(batch,"%d",&batchOn);fclose(batch);}
            GEVR_BEAN_LOG("gevr bean controls: rigid_min=%d body_batch=%d\n",s_rigidMinimum,batchOn);
            if(batchOn && !watchArm && nodeMatrix(header->RootNode)>=0) {
                for(int i=0;i<nd;i++) {
                    int g=dls[i].grp;
                    if(g<0 || grp[g].sk<0 || dls[i].simple)continue;
                    for(ModelNode *a=dls[i].node;a;a=a->Parent)
                        if(nodeOp(a)==MODELNODE_OPCODE_SWITCH)batchOn=0;
                }
                if(batchOn && emitSkinned(mesh,GEVR_BEAN_SK_BASE,hasHead,R,scale,grp,ng,
                                          header,-2,1,&combinedG,&combinedV)) {
                    keepAlloc(combinedG);keepAlloc(combinedV);
                    s_emittedSkin->anchor=nodeMatrix(header->RootNode);
                    swapAdd(header->RootNode,NULL,combinedG,0);
                    combined=1;
                    GEVR_BEAN_LOG("gevr bean body-batch: %d limbs -> one palette, %d triangles\n",found,mesh->ntris);
                }
            }
            for (int i = 0; i < nd; i++) {
                int g = dls[i].grp;
				int sk = (g >= 0 && g < ng) ? grp[g].sk : -1;
				int underJoint = g >= 0 && g < ng && grp[g].joint != 0;
				if (dls[i].simple) {
					if (watchArm)
						registerPrimary(dls[i].node, s_blank, 1);
					continue;
				}
				if (sk < 0) {
					if (!underJoint) {
						registerPrimary(dls[i].node, s_blank, 1);
					}
					continue;
				}
                if(combined) {
                    registerPrimary(dls[i].node,s_blank,1);
                    continue;
                }
                if (watchArm) {
                    int cuff = cuffOf(header, dls[i].node, 4);
					void *watchG = NULL, *watchV = NULL;
					if ((cuff < 0 && bareDrawn[sk]++) ||
					    !emitSkinned(mesh, sk, hasHead, R, scale, grp, ng, header, cuff, 0, &watchG, &watchV)) {
						registerPrimary(dls[i].node, s_blank, 1);
					} else {
						keepAlloc(watchG);
						keepAlloc(watchV);
						registerPrimary(dls[i].node, watchG, 0);
					}
					continue;
				}
				if (!sharedG[sk]) {
					if (!emitSkinned(mesh, sk, hasHead, R, scale, grp, ng, header, -2, 0, &sharedG[sk],
					                 &sharedV[sk])) {
						registerPrimary(dls[i].node, s_blank, 1);
						continue;
					}
					sharedSkin[sk] = s_emittedSkin;
					keepAlloc(sharedG[sk]);
					keepAlloc(sharedV[sk]);
				}
				s_emittedSkin = sharedSkin[sk];
				registerPrimary(dls[i].node, sharedG[sk], 0);
			}
		}
	}
	if (!skinned) {
		int best = -1;
		float bestSpan = -1.0f;
		int restrictType = kind == BEAN_GUN && !fit;
		/* DAB gebean.c:12966: a first-person gun goes on the untoggled lists;
		 * a second pass takes toggled ones for a gun that has nothing else. */
		int untoggled = kind == BEAN_GUN && fit;
		/* A gun's body never goes on GoldenEye's hand when it has a list of
		 * its own: the watch and the weapon panel switch the hand off, and
		 * the throwing knife, whose hand's lists are not under a switch,
		 * went with it (a last pass takes the hand for a model with nothing
		 * else) */
		const int passes = untoggled ? 3 : restrictType ? 2 : 1;
		for (int pass = 0; pass < passes; pass++) {
			for (int i = 0; i < nd; i++) {
				int g = dls[i].grp;
				int underJoint = g >= 0 && g < ng && grp[g].joint != 0;
				if (kind == BEAN_GUN && (dls[i].simple || underJoint || gunFlashNode(header, dls[i].node))) {
					continue;
				}
				if (untoggled && pass < 2 && gunHandNode(header, dls[i].node)) {
					continue;
				}
				if (pass == 0 && restrictType && dls[i].modelType != 3) {
					continue;
				}
				if (pass == 0 && untoggled && listToggled(header, dls[i].node)) {
					continue;
				}
				int far = 0;
				for (ModelNode *up = dls[i].node->Parent; up; up = up->Parent)
					if (nodeOp(up) == MODELNODE_OPCODE_LOD && up->Data->LOD.MinDistance > 0)
						far = 1;
                /* Prefer a fitted root body. With no list on the root, the
                 * fit is the first near list's (DAB gebean.c
                 * gebeanRigidPrimaryList; gevr_bean_fits.h): Surface's dish
                 * fitted to its truss drew 3927 too high on the dish's own
                 * matrix when the widest list was taken (#218). The Runway
                 * plane, helicopter and guns bind as before (ROM models). */
                int rootMatrix=nodeMatrix(header->RootNode);
                int haveRoot=best>=0 && nodeMatrix(dls[best].node)==rootMatrix;
                if ((fit && !far && nodeMatrix(dls[i].node)==rootMatrix && !haveRoot) ||
                    (!fit && dls[i].span>bestSpan) ||
                    (kind==BEAN_PROP && fit && best<0 && !far && !dls[i].simple)) {
					bestSpan = dls[i].span;
					best = i;
				}
			}
			if (best >= 0) {
				break;
			}
		}
		if (best < 0 && fit && nd > 0)
			best = 0;
		if (best >= 0) {
			struct beanRigidBinding binding;
			int partDone[MAX_GRP] = {0}; /* a part's triangles go on one list of its matrix */
			rigidBindings(mesh, fit, grp, ng, dls[best].grp, &binding, dls, nd);
			binding.target = binding.base;
			void *gdl = NULL, *vtx = NULL;
			/* Bean's gun is GoldenEye's model at 4.7 times in GoldenEye's frame
			 * (DAB gebean.c:775): each list takes its matrix's rest off. */
			int gun = kind == BEAN_GUN && fit && dls[best].grp >= 0;
			int glove = 0, hand = -1;
			for (int t = 0; gun && !mesh->triggerCuffs && t < mesh->ntris && !glove; t++)
				glove = triWanted(mesh, &mesh->tris[t], kind, hasHead) && gunGloveTri(mesh, &mesh->tris[t]);
			for (int i = 0; glove && i < nd && hand < 0; i++) {
				int g = dls[i].grp;
				if (i != best && g >= 0 && !dls[i].simple && !grp[g].joint && gunHandNode(header, dls[i].node))
					hand = i;
			}
			/* DAB gebeanBuildRigid(): a prop's mesh is laid out round
			 * GoldenEye's monitor screens (gevr_bean_screens.inc.h) */
			struct beanScreenBuild screens;
			if (kind == BEAN_PROP) {
				beanScreensBuild(&screens, header, mesh, fit, &binding, dls[best].node);
				s_screenBuild = &screens;
			}
			/* DAB gebeanBuildRigid(): a gun's painted muzzle flash; a prop has
			 * none, and a flat end of one is its own geometry. GoldenEye's
			 * weapon models are the ones on its prop_weapon skeleton, as
			 * weaponSetGunfireVisible() tells them. */
			if (kind == BEAN_PROP && header->Skeleton == &skeleton_prop_weapon && mesh->draws && mesh->triSrc) {
				s_flashDraws = beanGunFlashDraws(mesh);
				s_flashMesh = mesh;
				if (s_flashDraws)
					GEVR_BEAN_LOG("gevr bean: %s: muzzle flash draws %llx left out\n", fit ? fit->source : "pickup",
					              (unsigned long long)s_flashDraws);
			}
			struct beanGunParts parts;
			memset(&parts, 0, sizeof(parts));
			struct beanPress press = { NULL, -1, -1 };
			if (gun && mesh->triggerCuffs) {
				pressHand(&press, header, mesh, fit, dls, nd, best);
				if (press.of) {
					s_pressOf = press.of;
					s_pressMesh = mesh;
					s_pressWant = 0;
				}
			}
			if (gun && !mesh->triggerCuffs) {
				gunParts(&parts, header, mesh, fit, grp, dls, nd, best);
				if (parts.of) {
					s_parts = &parts;
					s_partsMesh = mesh;
					s_partWant = parts.body;
				}
			}
			int mainEmitted = emitRigid(mesh, kind, hasHead, dls[best].node, fit,
			                            kind == BEAN_PROP ? &binding : NULL, -1, gun ? grp[dls[best].grp].o : NULL,
			                            gun && !mesh->triggerCuffs ? 0 : -1, &gdl, &vtx);
			if (mainEmitted || kind == BEAN_PROP) {
				if (mainEmitted) {
					keepAlloc(gdl);
					keepAlloc(vtx);
				}
				if (mainEmitted && fit && !strcmp(fit->source, "prop/helicopter"))
					gdl = helicopterWithPilot(dls[best].node, gdl);
				registerPrimary(dls[best].node, mainEmitted ? gdl : s_blank, 0);
				/* DAB fpRound: the release's rocket launcher carries its round
				 * in the tube, so it is laid out without it too, for while the
				 * tube is empty (and GoldenEye's rocket is not drawn over it) */
				int roundTris = 0;
				for (int t = 0; gun && mainEmitted && t < mesh->ntris; t++)
					roundTris += (mesh->tris[t].flags & GEVR_BEAN_F_ROUND) != 0;
				if (roundTris) {
					void *spentG = NULL, *spentV = NULL;
					struct beanSwap *loaded = swapOf(dls[best].node);
					s_noRound = 1;
					if (loaded && loaded->ours != s_blank &&
					    emitRigid(mesh, kind, hasHead, dls[best].node, fit, NULL, -1, grp[dls[best].grp].o,
					              mesh->triggerCuffs ? -1 : 0, &spentG, &spentV)) {
						keepAlloc(spentG);
						keepAlloc(spentV);
						loaded->spent = wrapEmitted(dls[best].node, spentG);
					}
					s_noRound = 0;
				}
				/* the watch items' pressing hand on GoldenEye's palm and finger */
				for (int k = 1; press.of && k <= 2; k++) {
					const int list = k == 1 ? press.palm : press.finger;
					void *handG = NULL, *handV = NULL;
					if (list < 0 || dls[list].grp < 0)
						continue;
					s_pressWant = k;
					if (emitRigid(mesh, kind, hasHead, dls[list].node, fit, NULL, -1, grp[dls[list].grp].o, -1,
					              &handG, &handV)) {
						keepAlloc(handG);
						keepAlloc(handV);
						registerPrimary(dls[list].node, handG, 0);
					}
				}
				s_pressOf = NULL;
				s_pressMesh = NULL;
				/* the gun's moving parts, each on its matrix's list from that list's rest */
				for (int m = 0; parts.of && m < BEAN_GUN_MTX; m++) {
					const int carrier = parts.carrier[m];
					void *partG = NULL, *partV = NULL;
					if (m == parts.body || !parts.used[m] || carrier < 0)
						continue;
					s_partWant = m;
					if (emitRigid(mesh, kind, hasHead, dls[carrier].node, fit, NULL, -1, grp[dls[carrier].grp].o, 0,
					              &partG, &partV)) {
						keepAlloc(partG);
						keepAlloc(partV);
						registerPrimary(dls[carrier].node, partG, 0);
					} else {
						parts.used[m] = 0;
					}
				}
				s_parts = NULL;
				s_partsMesh = NULL;
				for (int i = 0; i < nd; i++) {
					int g = dls[i].grp;
					int underJoint = g >= 0 && g < ng && grp[g].joint != 0;
					if (i == best)
						continue;
					if (mesh->triggerCuffs && press.of && (i == press.palm || i == press.finger) && swapOf(dls[i].node))
						continue;
					if (mesh->triggerCuffs) {
						int cuff = cuffOf(header, dls[i].node, 29);
						void *cuffG = NULL, *cuffV = NULL;
						if (cuff >= 0 &&
						    emitRigid(mesh, kind, hasHead, dls[i].node, fit, NULL, cuff,
						              gun && g >= 0 ? grp[g].o : NULL, -1, &cuffG, &cuffV)) {
							keepAlloc(cuffG);
							keepAlloc(cuffV);
							registerPrimary(dls[i].node, cuffG, 0);
						} else
							registerPrimary(dls[i].node, s_blank, 1);
						continue;
					}
                    /* A head's distance alternatives carry it too. Bond's suit
                     * head (CheadbrosnansuitZ) has a near and a far list, each
                     * under an LOD node, and the head went on the larger one
                     * only: close up, in Frigate's intro, the game drew the
                     * other, blanked, under a body that leaves its own head to
                     * the head model - Bond had none (2026-10-19). The head's
                     * skin goes with it, to pose it on whichever list draws. */
                    int headLod=0;
                    for (ModelNode *up=dls[i].node->Parent;up;up=up->Parent)
                        if (nodeOp(up)==MODELNODE_OPCODE_LOD) headLod=1;
                    if ((kind==BEAN_HEAD || kind==BEAN_HEADBODY) && mainEmitted && headLod) {
                        struct beanSwap *head=swapOf(dls[best].node);
                        s_emittedSkin=head ? head->skin : NULL;
                        registerPrimary(dls[i].node,gdl,0);
                        s_emittedSkin=NULL;
                        continue;
                    }
                    /* Alternate distance lists for the same body matrix must
                     * carry the replacement too. Otherwise a plane's hull
                     * vanishes at its LOD boundary while the rotors remain. */
                    int bodyLod=0;
                    for (ModelNode *up=dls[i].node->Parent;up;up=up->Parent)
                        if (nodeOp(up)==MODELNODE_OPCODE_LOD && up->Data->LOD.MinDistance>0) bodyLod=1;
                    if (kind==BEAN_PROP && mainEmitted && bodyLod
                        && nodeMatrix(dls[i].node)==nodeMatrix(dls[best].node)) {
                        registerPrimary(dls[i].node,gdl,0);
                        continue;
                    }
					if (kind == BEAN_PROP && g >= 0 && g != binding.base && partDone[g]) {
						registerPrimary(dls[i].node, s_blank, 1);
						continue;
					}
					if (kind == BEAN_PROP && g >= 0 && g != binding.base) {
						binding.target = g;
						void *partGdl = NULL, *partVtx = NULL;
						if (emitRigid(mesh, kind, hasHead, dls[i].node, fit, &binding, -1, NULL, -1, &partGdl,
						              &partVtx)) {
							keepAlloc(partGdl);
							keepAlloc(partVtx);
							registerPrimary(dls[i].node, partGdl, 0);
							partDone[g] = 1;
						} else if (!dls[i].simple && !underJoint)
							registerPrimary(dls[i].node, s_blank, 1);
						continue;
					}
					if (gunPartList(&parts, header, &dls[i])) {
						if (i != parts.carrier[nodeMatrix(dls[i].node)])
							registerPrimary(dls[i].node, s_blank, 1);
						continue;
					}
					/* Bean's glove carries its own index finger (DAB keeps the HD
					 * files' kind 2 section), so GoldenEye's, on a joint of its
					 * own (the pistols' Switches[6]), goes with the rest of the
					 * hand rather than drawing a second, N64 finger on the trigger */
					if (gun && glove && underJoint && gunHandNode(header, dls[i].node)) {
						registerPrimary(dls[i].node, s_blank, 1);
						continue;
					}
					if (kind == BEAN_GUN && (dls[i].simple || underJoint || gunFlashNode(header, dls[i].node)))
						continue;
					/* Without Bean's glove GoldenEye's own hand stays (DAB keeps a
					 * toggled list's geometry); with it the glove takes the hand. */
					if (gun && gunHandNode(header, dls[i].node)) {
						void *handG = NULL, *handV = NULL;
						if (!glove)
							continue;
						if (i == hand && emitRigid(mesh, kind, hasHead, dls[i].node, fit, NULL, -1, grp[g].o, 1,
						                           &handG, &handV)) {
							keepAlloc(handG);
							keepAlloc(handV);
							registerPrimary(dls[i].node, handG, 0);
							continue;
						}
					}
					registerPrimary(dls[i].node, s_blank, 1);
				}
			}
			if (kind == BEAN_PROP) {
				s_screenBuild = NULL;
				beanScreensFinish(&screens, header, dls[best].node, fit, mainEmitted);
			}
			s_flashMesh = NULL;
			s_flashDraws = 0;
			s_parts = NULL;
			s_partsMesh = NULL;
			free(parts.of);
			s_pressOf = NULL;
			s_pressMesh = NULL;
			free(press.of);
		}
	}
	free(stack);
	free(seen);
	free(grp);
	free(dls);
}

/* A file header can be reused without a level reset (weapons, watch, intro).
 * DAB forgets a definition on registration too: do not inherit freed geometry. */
void gevrBeanForgetModel(struct ModelFileHeader *header)
{
	int kept = 0;
	for (int i = 0; i < s_nswap; i++)
		if (s_swap[i].owner != header)
			s_swap[kept++] = s_swap[i];
		else
			beanGunUnref(s_swap[i].group);
	s_nswap = kept;
	swapIndexRebuild();
	beanScreenInfoForget(header, 0, 0);
}

/* Different intro headers share the same file buffer. Forget all old node
 * addresses overwritten by this load, even when their header was different. */
void gevrBeanForgetModelStorage(const void *base, u32 size)
{
	uintptr_t start = (uintptr_t)base;
	int kept = 0;
	for (int i = 0; i < s_nswap; i++) {
		uintptr_t node = (uintptr_t)s_swap[i].node;
		if (node < start || node - start >= size)
			s_swap[kept++] = s_swap[i];
		else
			beanGunUnref(s_swap[i].group);
	}
	s_nswap = kept;
	swapIndexRebuild();
	beanScreenInfoForget(NULL, start, size);
}

/* The briefing folder keeps GoldenEye's model; its textures take the release's pictures. */
static void folderRepaint(struct ModelFileHeader *header)
{
	char path[192];
	struct gevrCaff caff;
	uint8_t *file = NULL;
	long size = 0;
	FILE *fp;

	gevrBeanRepaintForgetOwner(GEVR_PAINT_FOLDER);
	if (!findFile("new/prop/walletbond", path, sizeof(path))) {
		GEVR_BEAN_LOG("gevr bean folder: missing new/prop/walletbond");
		return;
	}
	fp = fopen(path, "rb");
	if (!fp) {
		return;
	}
	if (fseek(fp, 0, SEEK_END) == 0 && (size = ftell(fp)) > 0 && size <= 64 * 1024 * 1024) {
		rewind(fp);
		file = malloc((size_t)size);
		if (file && fread(file, 1, (size_t)size, fp) != (size_t)size) {
			free(file);
			file = NULL;
		}
	}
	fclose(fp);
	memset(&caff, 0, sizeof(caff));
	if (file && gevrCaffOpen(&caff, file, (uint32_t)size)) {
		gevrBeanFolderRepaint(header, &caff);
		gevrCaffClose(&caff);
	}
	free(file);
}

void gevrBeanModelApply(struct ModelFileHeader *header, const char *name)
{
	char source[128], path[192];
	int kind;
	struct gevrBeanMesh *mesh;
	if (header) gevrBeanForgetModel(header);
	/* Register while disabled too, so switching on works for loaded models. */
	/* Not in a hack campaign: its models sit under GoldenEye's names but are its own.
	 * Mirror Mode's are GoldenEye's, some flipped (below). */
	if (!header || !name || !gevrBeanIsAvailable() || !gevrRomRetailArt()) {
		return;
	}
	const struct beanFit *fit = fitForName(name);
	struct beanFit flipped;
	if (gevrRomMirrored() && gevrMirrorBeanFlips(name)) {
		/* Wreck flipped this model in its own x: so is the release's, on the
		 * fit's first axis (fitMirrors then winds it the other way). With no
		 * fit its own flipped model stays. */
		if (!fit) {
			GEVR_BEAN_LOG("gevr bean: %s: Mirror Mode's own (no fit to flip)", name);
			return;
		}
		flipped = *fit;
		flipped.sign[0] = -flipped.sign[0];
		flipped.bc[0] = -flipped.bc[0];
		flipped.nc[0] = -flipped.nc[0];
		fit = &flipped;
	}
	if (strcmp(name, "PwalletbondZ") == 0) {
		folderRepaint(header);
	}
	kind = sourceForName(name, source, sizeof(source));
	if (kind == BEAN_NONE) {
		return;
	}
	if (!findFile(source, path, sizeof(path))) {
		GEVR_BEAN_LOG("gevr bean: %s: missing %s", name, source);
		return;
	}
	mesh = cachedLoad(path, 1);
	if (!mesh) {
		GEVR_BEAN_LOG("gevr bean: %s: native fallback, CAFF not decoded (%s)", name, path);
		return;
	}
	if (gevrRomMirrored() && gevrMirrorBeanInPlace(name) && !mesh->mirrorPieces) {
		/* along the release's axis that becomes the model's x */
		gevrBeanMeshMirrorPieces(mesh, fit ? fit->perm[0] : 0);
		mesh->mirrorPieces = 1;
	}
	int before = s_nswap;
	struct beanGunGroup *group = NULL;
	if (kind == BEAN_GUN) group = calloc(1, sizeof(*group));
	if (kind != BEAN_GUN || group) {
		if (group) {
			group->next = s_gunGroups;
			s_gunGroups = group;
		}
		s_registrationGroup = group;
		applyMesh(header, mesh, kind, fit);
		s_registrationGroup = NULL;
		if (group && !group->references) beanGunRetire(group);
	}
	GEVR_BEAN_LOG("gevr bean: %s -> %s: %d draw nodes, %d triangles, %d textures",
	             name, source, s_nswap - before, mesh->ntris, mesh->ntex);
	/* Once the bounded cache is full, this load is temporary. Its display
	 * lists and decoded textures have separate ownership. */
	int cached = 0;
	for (int i = 0; i < s_ncache; i++) {
		if (s_cache[i].mesh == mesh) cached = 1;
	}
	if (!cached) gevrBeanMeshFree(mesh);
}

static Gfx *s_rareLogo;
static const void *s_rareLogoPicture;
static int s_rareLogoTried;

/* The release's Rare logo has no N64 model: GoldenEye draws its own from a
 * cartridge segment (title.c, gevr_rarelogo.c). Its mesh carries no UVs; the
 * gold picture is an environment map, as the cartridge's chrome is. */
void *gevrBeanRareLogoGdl(const float lo[3], const float hi[3], const void **picture)
{
    if (!gevrBeanIsEnabled() || !gevrBeanIsAvailable() || !gevrRomRetailArt()) return NULL;
    if (!s_rareLogoTried) {
        char path[192];
        struct gevrBeanMesh *mesh = NULL;
        s_rareLogoTried = 1;
        if (findFile("new/prop/rarewarelogo", path, sizeof(path))) mesh = cachedLoad(path, 1);
        int tex = -1;
        float mlo[3] = {1e30f, 1e30f, 1e30f}, mhi[3] = {-1e30f, -1e30f, -1e30f};
        for (int i = 0; mesh && i < mesh->ntris; i++) {
            if (tex < 0) tex = mesh->tris[i].tex;
            for (int k = 0; k < 9; k++) {
                if (mesh->tris[i].p[k] < mlo[k % 3]) mlo[k % 3] = mesh->tris[i].p[k];
                if (mesh->tris[i].p[k] > mhi[k % 3]) mhi[k % 3] = mesh->tris[i].p[k];
            }
        }
        if (!mesh || tex < 0 || tex >= mesh->ntex || !mesh->standin[tex] || mhi[1] <= mlo[1]) {
            GEVR_BEAN_LOG("gevr bean: rare logo: the cartridge's (%s)", mesh ? "no picture" : "no new/prop/rarewarelogo");
            return NULL;
        }
        /* as tall as the cartridge's, on its centre */
        const float scale = (hi[1] - lo[1]) / (mhi[1] - mlo[1]);
        const int nverts = mesh->ntris * 3, triangles = mesh->ntris;
        Vtx *verts = malloc((size_t)nverts * sizeof(Vtx));
        Gfx *list = malloc((size_t)(triangles + (triangles + 4) / 5 + 16) * sizeof(Gfx));
        if (!keepAlloc(verts)) {
            free(verts);
            free(list);
            return NULL;
        }
        if (!keepAlloc(list)) {
            free(list);
            return NULL;
        }
        for (int i = 0; i < nverts; i++) {
            const struct gevrBeanTri *t = &mesh->tris[i / 3];
            const float *p = &t->p[(i % 3) * 3], *n = &t->n[(i % 3) * 3];
            const float len = sqrtf(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
            Vtx *dst = &verts[i];
            memset(dst, 0, sizeof(*dst));
            for (int k = 0; k < 3; k++) {
                /* Mirror Mode's logo is turned over in x (its own segment): so is the release's */
                const float side = k == 0 && gevrRomMirrored() ? -1.0f : 1.0f;
                dst->n.ob[k] = posClamp(side * (p[k] - (mlo[k] + mhi[k]) / 2) * scale + (lo[k] + hi[k]) / 2);
                dst->n.n[k] = len > 1e-6f ? (s8)(side * n[k] * 127.0f / len) : 0;
            }
            dst->n.a = 255;
        }
        Gfx *g = list;
        gSPSetGeometryMode(g++, G_ZBUFFER);
        gDPSetRenderMode(g++, G_RM_AA_ZB_OPA_SURF, G_RM_AA_ZB_OPA_SURF2);
        for (int t = 0; t < triangles;) {
            int take = triangles - t;
            if (take > 5) take = 5;
            gSPVertex(g++, osVirtualToPhysical(&verts[t * 3]), take * 3, 0);
            for (int j = 0; j < take; j++) {
                const int second = gevrRomMirrored() ? 2 : 1;   /* turned over, it winds the other way */
                gSP1Triangle(g++, j * 3, j * 3 + second, j * 3 + 3 - second, 0);
            }
            t += take;
        }
        gSPClearGeometryMode(g++, G_ZBUFFER);
        gSPEndDisplayList(g++);
        s_rareLogo = list;
        s_rareLogoPicture = mesh->standin[tex];
        GEVR_BEAN_LOG("gevr bean: rare logo: the release's, %d triangles at %.2fx", triangles, scale);
    }
    *picture = s_rareLogoPicture;
    return s_rareLogo;
}

void gevrBeanReset(void)
{
	beanFrameReset();
	beanCutReset();
	beanCapReset();
	memset(s_wounds, 0, sizeof(s_wounds));
	s_woundUse = 0;
	for (int i = 0; i < s_nrigidMeshes; i++) gevrStaticMeshFree(s_rigidMeshes[i]);
	s_nrigidMeshes = 0;
	for (int i = 0; i < s_nskinMeshes; i++)
		gfx_static_mesh_release(s_skinMeshes[i]);
	s_nskinMeshes = 0;
	for (int i = 0; i < s_nalloc; i++) {
		free(s_alloc[i]);
		s_alloc[i] = NULL;
	}
	s_nalloc = 0;
	beanGunReset();
	s_rareLogo = NULL;   /* its list and picture went with the level */
	s_rareLogoTried = 0;
	s_nswap = 0;
	memset(s_swapIndex, 0, sizeof(s_swapIndex));
	s_nscreenInfo = 0;
	s_screenModel = NULL;
	s_registrationOwner = NULL;
	for (int i = 0; i < s_ncache; i++) {
		gevrBeanMeshFree(s_cache[i].mesh);
		s_cache[i].mesh = NULL;
		s_cache[i].path[0] = 0;
	}
	s_ncache = 0;
	gevrBeanFontReset();
	gevrBeanTexReset();
	gevrBeanRepaintForget();
	gevrBeanWorldReset();
}

void gevrBeanInit(void)
{
	char path[192];
	blankInit();
	s_want = VrBeanModels != 0;
	s_available = findFile("new/char/boilerbond", path, sizeof(path));
}

int gevrBeanIsAvailable(void)
{
	if (s_available < 0) {
		gevrBeanInit();
	}
	return s_available == 1;
}

int gevrBeanIsEnabled(void)
{
	if (s_available < 0) {
		gevrBeanInit();
	}
	return s_want && s_available == 1 && gevrRomRetailArt();
}

void gevrBeanSetEnabled(int on)
{
	s_want = on != 0;
	VrBeanModels = s_want;
}

void gevrBeanToggle(void)
{
	gevrBeanSetEnabled(!s_want);
}

void *gevrBeanRoomAppend(void *gdl, int room, int *replaced)
{
	if (replaced) {
		*replaced = 0;
	}
	if (!gevrBeanIsEnabled()) {
		return gdl;
	}
	return gevrBeanWorldRoom(gdl, room, replaced);
}

void *gevrBeanRoomAppendSecondary(void *gdl, int room, int *replaced)
{
	if (replaced) {
		*replaced = 0;
	}
	if (!gevrBeanIsEnabled()) {
		return gdl;
	}
	return gevrBeanWorldRoom(gdl, room + 100000, replaced);
}

void *gevrBeanSkyAppend(void *gdl, int *drew)
{
	if (drew) {
		*drew = 0;
	}
	if (!gevrBeanIsEnabled()) {
		return gdl;
	}
	return gevrBeanWorldSky(gdl, drew);
}
