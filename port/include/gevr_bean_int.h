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

struct gevrBeanTri {
	float p[9];
	float n[9];
	float uv[6];
	uint8_t bone[12], weight[12];
	uint8_t rgba[12];
	int8_t cuff;
	int16_t tex;
	uint8_t alpha;
	uint8_t alphatest;
	uint8_t sk;
	uint8_t flags;
};

struct gevrBeanMesh {
	struct gevrBeanTri *tris;
	int ntris;
	float bind[GEVR_BEAN_SK_COUNT][3];
	int haveBind[GEVR_BEAN_SK_COUNT];
	const void *standin[256];
	int texW[256];
	int texH[256];
	int texCut[256];
	int ntex;
	float uvscale;
	int triggerCuffs;
	int nbones;
	float boneBind[64][3];
	char boneName[64][64];
	uint8_t boneSk[64];
	int tricaps;
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

#endif
