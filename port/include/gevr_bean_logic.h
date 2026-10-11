#ifndef GEVR_BEAN_LOGIC_H
#define GEVR_BEAN_LOGIC_H

#include <stddef.h>
#include <stdint.h>

/* Pure Project Bean pieces: CAFF fields, Xenos triangles, packed mips, UVs.
 * No game headers, so the host test can link this file on its own. */

#define GEVR_BEAN_MAXPAL  64
#define GEVR_BEAN_MAXSECT 32

struct gevrCaffFile {
	uint32_t asset;
	uint32_t start;
	uint32_t size;
	uint8_t sect;
};

struct gevrCaffSect {
	uint32_t size;
	uint64_t offset;
	char name[32];
};

struct gevrCaff {
	const uint8_t *d;
	uint32_t len;
	int be;
	uint32_t numassets;
	uint32_t numfiles;
	uint32_t numsects;
	uint32_t nametable;
	uint32_t labels;
	struct gevrCaffSect sects[GEVR_BEAN_MAXSECT];
	struct gevrCaffFile *files;
};

struct gevrBeanTriIn {
	uint32_t prim;   /* 4 list, 5 fan, 6 strip, 13 quads */
	uint32_t count;
	const uint8_t *idx;
	uint32_t idxBytes;
	uint32_t width;  /* 2 or 4 */
};

int gevrCaffOpen(struct gevrCaff *c, const uint8_t *d, uint32_t len);
void gevrCaffClose(struct gevrCaff *c);
const uint8_t *gevrCaffBlob(const struct gevrCaff *c, int index, uint32_t *outLen);
const char *gevrCaffSectName(const struct gevrCaff *c, int index);
const char *gevrCaffAssetName(const struct gevrCaff *c, uint32_t asset);
int gevrCaffFind(const struct gevrCaff *c, uint32_t asset, const char *prefix);

/* Writes index triples. *out is malloc'd. Returns the triangle count. */
int gevrBeanTriangles(const struct gevrBeanTriIn *in, uint32_t **out);

/* Level 0's element offset when a tiled surface packs its mips.
 * blockw is 1 for 8888 and 4 for DXT. A one-level surface is not packed. */
void gevrBeanPackedMip(uint32_t width, uint32_t height, uint32_t blockw, int packed,
		uint32_t *outX, uint32_t *outY);

/* HD shader UV denominator (4096, 16384, ...), or 0 if the floats disagree. */
float gevrBeanShaderUvScale(const uint8_t *gpu, uint32_t gpulen, uint32_t buffersEnd);

/* 1 when stride-20 vertices carry a 16:16 UV. */
int gevrBeanShaderUv20(const uint8_t *gpu, uint32_t gpulen, uint32_t buffersEnd);

int gevrBeanLogicSelfTest(void);

#endif
