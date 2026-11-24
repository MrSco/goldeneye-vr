/*
 * Analytical 3D volumetric smoke and explosions (issue #249).
 *
 * Replaces GoldenEye's camera-facing billboard quads in explosion.c with
 * world-anchored 3D volumetric spheres rendered in a single instanced GL pass
 * per prop (front-faces outside, back-faces when the camera steps inside).
 *
 * Plain C so both the N64 game sources (src/game/explosion.c) and the C++
 * Fast3D renderer (port/fast3d/gfx_pc.cpp, gfx_opengl.cpp) share it directly,
 * and native tests can exercise culling, sorting, chaos throttling and 3D
 * noise generation without a GL context.
 */
#ifndef GEVR_VOLUMETRIC_H
#define GEVR_VOLUMETRIC_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* gDPNoOpTag high-16 prefix: low 16 bits carry the per-frame batch id. */
#define GEVR_VOL_DL_TAG_MASK   0xFFFF0000u
#define GEVR_VOL_DL_TAG_PREFIX 0x56640000u

#define GEVR_VOL_KIND_SMOKE 0u
#define GEVR_VOL_KIND_FIRE  1u

#define GEVR_VOL_MAX_BATCHES 256u
#define GEVR_VOL_MAX_PUFFS   2048u

#define GEVR_VOL_NOISE_SIZE  32
#define GEVR_VOL_ICO_VERTS   42u
#define GEVR_VOL_ICO_INDICES 240u /* 80 triangles */

typedef struct GevrVolumetricPuff {
    float viewPos[3];   /* puff center in game-camera view space (world units) */
    float radius;       /* world-unit sphere radius (matched to classic footprint) */
    float color[4];     /* RGB tint (0..1) + opacity/intensity (0..1) */
    float life;         /* normalized age 0..1 (fire lifecycle or smoke age) */
    float swirl;        /* world-Y swirl angle in radians */
    float seed;         /* stable per-part seed anchoring noise to this puff */
    float worldY;       /* world Y for buoyant noise drift */
    uint8_t kind;       /* GEVR_VOL_KIND_SMOKE or GEVR_VOL_KIND_FIRE */
    uint8_t smallFx;    /* 1 for small bullet-hit flash/puff */
    uint8_t detailTier; /* 0 = 1-tap distant LOD, 1 = 2-tap mobile/Quest, 2 = 3-tap PC */
    uint8_t inside;     /* 1 when camera is inside or near-clipping this puff */
} GevrVolumetricPuff;

typedef struct GevrVolumetricBatch {
    uint32_t firstPuff;
    uint32_t puffCount;
    float proj[16];       /* game projection matrix (GL column-major) */
    float viewToWorld[9]; /* 3x3 column-major rotation: game-view dir -> world dir */
    float timeSec;        /* monotonic animation time in seconds */
    uint8_t isFire;
    uint8_t isSmallFx;
} GevrVolumetricBatch;

/* Reset staging buffers at the start of a new frame's display-list build. */
void gevrVolumetricBeginFrame(void);

/*
 * Enqueue one explosion or smoke prop's active puffs for the current frame.
 * Performs frustum culling, inside-camera classification, and back-to-front
 * depth sorting. Returns batchId (0..GEVR_VOL_MAX_BATCHES-1), or -1 if none
 * of the puffs are visible or the staging pool is full.
 */
int gevrVolumetricPushBatch(const GevrVolumetricPuff *puffs, uint32_t count,
                            const float proj[16], const float viewToWorld[9],
                            float timeSec, int isFire, int isSmallFx);

/*
 * Run once per frame before drawing volumetric batches. Assigns per-puff
 * detail tiers (mobile/Quest vs PC + distant LOD) and, during extreme chaos
 * (e.g. multiple overlapping grenades), thins redundant interior overlapping
 * flame puffs to bound worst-case fragment overdraw while preserving outer
 * silhouettes and smoke visibility parity.
 */
void gevrVolumetricPrepareDraw(int isMobileTier);

const GevrVolumetricBatch *gevrVolumetricGetBatch(int batchId);
const GevrVolumetricPuff *gevrVolumetricGetBatchPuffs(const GevrVolumetricBatch *batch, uint32_t *outCount);

/*
 * Fill outRgba (size * size * size * 4 bytes) with seamless tileable 3D noise:
 *   R: smooth 3D value/Perlin FBM (broad cloud billows)
 *   G: inverted 3D Worley cellular FBM (cauliflower puff lobes)
 *   B: higher-frequency turbulent detail (flame tongues / wisps)
 *   A: phase-shifted cellular billow mask
 */
void gevrVolumetricBuildNoise3D(uint8_t *outRgba, int size);

/*
 * Build a unit-radius subdivision-1 icosphere (42 vertices, 80 CCW outward
 * triangles = 240 uint16 indices). outVerts holds 42 * 3 floats.
 */
void gevrVolumetricBuildIcosphere(float *outVerts, uint32_t *outVertCount,
                                  uint16_t *outIndices, uint32_t *outIndexCount);

#ifdef __cplusplus
}
#endif

#endif /* GEVR_VOLUMETRIC_H */
