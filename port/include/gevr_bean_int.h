#ifndef GEVR_BEAN_INT_H
#define GEVR_BEAN_INT_H

#include "gevr_bean_logic.h"

#define GEVR_BEAN_SK_BASE 0
#define GEVR_BEAN_SK_BACK 1
#define GEVR_BEAN_SK_NECK 2
#define GEVR_BEAN_SK_POS  3
#define GEVR_BEAN_SK_COUNT 16

#define GEVR_BEAN_F_FLASH 1
#define GEVR_BEAN_F_SLIDE 2
#define GEVR_BEAN_F_NECK  4
#define GEVR_BEAN_F_DECAL 8
#define GEVR_BEAN_F_ROUND 16   /* the rocket launcher's round in its tube (DAB fpRound) */
#define GEVR_BEAN_F_PLAIN 32   /* level shader-only draw, no authored picture/UV */

struct gevrBeanTri {
	float p[9];
	float n[9];
	float uv[6];
	uint8_t bone[12], weight[12];
	uint8_t rgba[12];
    const void *reflection;
    uint8_t reflectionDiffuse; /* 0 screen, 1 diffuse-tinted, 2 coloured */
    uint8_t reflectionAlphaBase;
    uint32_t reflectionColour;
	int8_t cuff;
	int16_t tex;
	uint8_t alpha;
	uint8_t alphatest;
	uint8_t sk;
	uint8_t flags;
};

/* A prop's draws as DAB gebean.c reads them for GoldenEye's monitor screens
 * (beanReadVb(), beanVertex(), beanTriangles(); pinned adef1dc2,
 * port/licenses/dabs-mod.txt): every vertex of the draw's buffer, not only
 * the ones its triangles use, by its index in the buffer. */
#define GEVR_BEAN_V_READ    1 /* beanVertex() read it */
#define GEVR_BEAN_V_DROPPED 2 /* DAB beanVertexDropped() */
struct gevrBeanDrawSrc {
	float (*pos)[3];
	float (*uv)[2];
	uint8_t *state;  /* GEVR_BEAN_V_* */
	uint32_t *idx;   /* three per triangle */
	uint32_t vboff;  /* the buffer's .gpu offset (DAB vb.off) */
	uint32_t nverts;
	int ntris;
	int16_t tex;
	uint8_t placeholder; /* DAB beanScreenPlaceholder: picture _0x008C4635 */
	uint8_t prim;        /* 4 triangle list, 13 quad list (DAB beandraw.prim) */
	uint8_t alphatest;   /* DAB beandraw.alphatest (beanGunFlashDraws()) */
};
/* Which draw and buffer vertices a pushed triangle came from (DAB's mapped[]
 * is per draw and vertex). pane: corners on DAB's beanVertexDrops[]. */
struct gevrBeanTriSrc {
	uint32_t vi[3];
	int16_t draw;
	uint8_t pane;
};

struct gevrBeanMesh {
	struct gevrBeanTri *tris;
	int ntris;
	/* props only (gevr_bean_load.c); NULL otherwise */
	struct gevrBeanDrawSrc *draws;
	int ndraws;
	struct gevrBeanTriSrc *triSrc;
	float bind[GEVR_BEAN_SK_COUNT][3];
	int haveBind[GEVR_BEAN_SK_COUNT];
	const void *standin[256];
    const void *reflection[256];
	int texW[256];
	int texH[256];
	int texCut[256];
	/* Clamp selected axes to one shared repeat, never rebase each triangle. */
	uint8_t texClamp[256]; /* bit 0 U, bit 1 V */
	float texClampBase[256][2];
	int ntex;
	float uvscale;
	int shadows;   /* a level's cast shadows drawn see-through (gevr_bean_load.c markLevelShadows) */
	int triggerCuffs;
	int nativeChrome;
	int roomMaterial;
    int worldVertices, spatialSort;
	int watchArm;
	int nbones;
	float boneBind[64][3];
	char boneName[64][64];
	uint8_t boneSk[64];
	int tricaps;
	int mirrorPieces;   /* Mirror Mode turned its pieces over in place (gevr_mirror.c) */
};

struct gevrEmitVert {
	float p[3];
	float n[3];
	float uv[2];
	uint8_t rgba[4];
	int16_t tex;
	uint8_t alpha;
	uint8_t cut;
	uint8_t decal;
    const void *reflection;
    uint8_t reflectionDiffuse; /* 0 screen, 1 diffuse-tinted, 2 coloured */
    uint8_t reflectionAlphaBase;
    uint32_t reflectionColour;
};

int gevrBeanLoad(const char *source, int keepparts, struct gevrBeanMesh **out);
void gevrBeanMeshFree(struct gevrBeanMesh *m);
/* nverts is 3 per triangle. *vtxOut is malloc'd with the list.
 * zbuf 0 is a sky dome: no depth test, no decal. */
int gevrBeanEmitList(const struct gevrBeanMesh *mesh, const struct gevrEmitVert *v, int nverts,
		int lit, int dim, int cull, int zbuf, void **gdlOut, void **vtxOut);
const void *gevrBeanTexDecode(const struct gevrCaff *c, int fileIndex, const char *key,
		int *outW, int *outH, int *outAlpha);
int gevrBeanTexture(const void *addr, const uint8_t **rgba, int *w, int *h);
int gevrBeanTexIsPlaceholder(const void *addr);
void gevrBeanTexScrollSet(const void *addr, float s, float t);
/* GoldenEye's moving water on a Bean picture (gevr_bean_tex.c): marks one,
 * sets the rate (quarter texels a frame; 0 measured for GoldenEye's own). */
void gevrBeanTexWaterSet(const void *addr);
int gevrBeanTextureIsWater(const void *addr);
void gevrBeanWaterSetRate(int measured, float s, float t);
void gevrBeanWaterAdvance(float delta);
/* G_DL target that sets the water's fade (gevr_bean.c gevrBeanWaterTick) */
void *gevrBeanWaterList(void);
void gevrBeanTexMotionSet(const void *addr, float s, float t, float degrees, float spf,
                          const struct gevrCaff *c, int fileIndex);
const void *gevrBeanTexReflect(const void *spot,const void *land,const uint8_t amount[3],const char *key);
const void *gevrBeanTexReflectRgba(const void *spot,const void *land,const uint8_t amount[4],const char *key);
const void *gevrBeanTexReflectMetal(const void *map,const float hue[3],const char *key);
void gevrBeanTexPaint(const void *addr, uint32_t rgb);
void gevrBeanTexReset(void);
uint8_t *gevrBeanTexDecodeRgba(const struct gevrCaff *c, int fileIndex, int *outW, int *outH);
/* Takes rgba (freed on failure). Bound by native address; owner is a GEVR_PAINT_*. */
int gevrBeanRepaintBind(int owner, const void *addr, uint8_t *rgba, int w, int h);
/* The briefing folder's textures repainted with the release's walletbond pictures. */
struct ModelFileHeader;
int gevrBeanFolderRepaint(struct ModelFileHeader *header, const struct gevrCaff *caff);

void gevrBeanWorldReset(void);
void *gevrBeanWorldRoom(void *gdl, int room, int *replaced);
void *gevrBeanWorldSky(void *gdl, int *drew);
/* 1 and room `room`'s Bean geometry bounds (the room table's world units, as
 * g_BgRoomInfo's), built with the stage; 0 when the room has none. */
int gevrBeanWorldRoomBounds(int room, float lo[3], float hi[3]);

#endif
