/*
 * Analytical 3D volumetric smoke and explosions (issue #249):
 * per-frame batching, frustum culling, back-to-front sorting, chaos-proof
 * overdraw governor, unit icosphere builder, and tileable 3D noise generator.
 */
#include "gevr_volumetric.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

static GevrVolumetricBatch s_batches[GEVR_VOL_MAX_BATCHES];
static GevrVolumetricPuff  s_puffs[GEVR_VOL_MAX_PUFFS];
static uint32_t s_batchCount = 0;
static uint32_t s_puffCount = 0;
static int s_prepared = 0;

void gevrVolumetricBeginFrame(void)
{
    s_batchCount = 0;
    s_puffCount = 0;
    s_prepared = 0;
}

static float clampf_vol(float v, float lo, float hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

/*
 * Conservative frustum cull in game-camera view space (camera looks down -Z).
 * Uses P00 and P11 from the projection matrix with a generous stereo/asymmetry
 * margin so VR off-axis eyes and late-latch reprojection never clip edge puffs.
 */
static int puffVisibleInView(const GevrVolumetricPuff *p, const float proj[16])
{
    const float r = p->radius;
    if (!(r > 0.01f) || !(p->color[3] > 0.002f)) {
        return 0;
    }
    const float vx = p->viewPos[0];
    const float vy = p->viewPos[1];
    const float vz = p->viewPos[2];
    const float distZ = -vz; /* positive in front of camera */

    /* Completely behind the camera (with margin for stereo/reprojection). */
    if (distZ + r * 1.25f < 0.1f) {
        return 0;
    }
    /* Past far draw horizon. */
    if (distZ - r > 32000.0f) {
        return 0;
    }

    const float p00 = fabsf(proj[0]);
    const float p11 = fabsf(proj[5]);
    if (p00 > 0.05f && p11 > 0.05f && distZ > r) {
        /* 1.45x guard band accommodates asymmetric VR eye frustums + head turn replay. */
        const float halfTanX = 1.45f / p00;
        const float halfTanY = 1.45f / p11;
        const float secX = sqrtf(1.0f + halfTanX * halfTanX);
        const float secY = sqrtf(1.0f + halfTanY * halfTanY);
        if (fabsf(vx) > distZ * halfTanX + r * secX) {
            return 0;
        }
        if (fabsf(vy) > distZ * halfTanY + r * secY) {
            return 0;
        }
    }
    return 1;
}

static int comparePuffBackToFront(const void *a, const void *b)
{
    const GevrVolumetricPuff *pa = (const GevrVolumetricPuff *)a;
    const GevrVolumetricPuff *pb = (const GevrVolumetricPuff *)b;
    /* Outside puffs draw before camera-enclosing puffs; within each group,
     * more negative viewPos.z (farther from camera) draws first. */
    if (pa->inside != pb->inside) {
        return (int)pa->inside - (int)pb->inside;
    }
    if (pa->viewPos[2] < pb->viewPos[2]) return -1;
    if (pa->viewPos[2] > pb->viewPos[2]) return 1;
    return 0;
}

int gevrVolumetricPushBatch(const GevrVolumetricPuff *puffs, uint32_t count,
                            const float proj[16], const float viewToWorld[9],
                            float timeSec, int isFire, int isSmallFx)
{
    if (!puffs || count == 0 || !proj || !viewToWorld) {
        return -1;
    }
    if (s_batchCount >= GEVR_VOL_MAX_BATCHES || s_puffCount >= GEVR_VOL_MAX_PUFFS) {
        return -1;
    }

    const uint32_t first = s_puffCount;
    uint32_t kept = 0;

    /* Near-plane + stereo half-IPD margin in view units (proj[14] ~ -2 * znear). */
    const float zNearEst = clampf_vol(fabsf(proj[14]) * 0.5f, 0.5f, 20.0f);
    const float insidePad = zNearEst * 1.25f + 1.5f;

    for (uint32_t i = 0; i < count && s_puffCount < GEVR_VOL_MAX_PUFFS; i++) {
        if (!puffVisibleInView(&puffs[i], proj)) {
            continue;
        }
        GevrVolumetricPuff dst = puffs[i];
        const float vx = dst.viewPos[0];
        const float vy = dst.viewPos[1];
        const float vz = dst.viewPos[2];
        const float dist = sqrtf(vx * vx + vy * vy + vz * vz);
        /* Enclosing threshold includes bounding icosphere scale (1.08) + near/IPD pad. */
        dst.inside = (dist <= dst.radius * 1.12f + insidePad) ? 1u : 0u;
        dst.detailTier = 1u;
        s_puffs[s_puffCount++] = dst;
        kept++;
    }

    if (kept == 0) {
        return -1;
    }

    if (kept > 1) {
        qsort(&s_puffs[first], kept, sizeof(GevrVolumetricPuff), comparePuffBackToFront);
    }

    GevrVolumetricBatch *b = &s_batches[s_batchCount];
    b->firstPuff = first;
    b->puffCount = kept;
    memcpy(b->proj, proj, sizeof(b->proj));
    memcpy(b->viewToWorld, viewToWorld, sizeof(b->viewToWorld));
    b->timeSec = timeSec;
    b->isFire = isFire ? 1u : 0u;
    b->isSmallFx = isSmallFx ? 1u : 0u;
    s_prepared = 0;

    return (int)(s_batchCount++);
}

static float puffScreenCoverage(const GevrVolumetricPuff *p, const float proj[16])
{
    const float dist = sqrtf(p->viewPos[0] * p->viewPos[0] +
                             p->viewPos[1] * p->viewPos[1] +
                             p->viewPos[2] * p->viewPos[2]);
    const float denom = dist > p->radius * 0.65f ? dist : p->radius * 0.65f;
    const float scale = fabsf(proj[5]) > 0.1f ? fabsf(proj[5]) : 1.0f;
    return clampf_vol((p->radius * scale) / (denom > 1.0f ? denom : 1.0f), 0.0f, 1.25f);
}

void gevrVolumetricPrepareDraw(int isMobileTier)
{
    if (s_prepared) {
        return;
    }
    s_prepared = 1;

    /* Mobile/Quest defaults to 1 rich 4-channel 3D noise tap at the analytical
     * chord midpoint (cuts 3D texture bandwidth & ALU in half on Adreno/mobile);
     * PC defaults to 3-tap ray integration. */
    const uint8_t baseTier = isMobileTier ? 0u : 2u;
    const float areaBudget = isMobileTier ? 2.2f : 12.0f;
    const uint32_t largeCap = isMobileTier ? 24u : 160u;

    /* Pass 1: assign per-puff LOD tier and measure total frame screen-area load. */
    float totalWeight = 0.0f;
    uint32_t largeCount = 0;
    for (uint32_t bi = 0; bi < s_batchCount; bi++) {
        GevrVolumetricBatch *b = &s_batches[bi];
        for (uint32_t i = 0; i < b->puffCount; i++) {
            GevrVolumetricPuff *p = &s_puffs[b->firstPuff + i];
            const float cov = puffScreenCoverage(p, b->proj);
            if (cov < 0.045f || p->smallFx) {
                p->detailTier = 0u; /* distant or tiny bullet puff: 1 noise tap */
            } else {
                p->detailTier = baseTier;
                largeCount++;
            }
            totalWeight += cov * cov;
        }
    }

    if (totalWeight <= areaBudget && largeCount <= largeCap) {
        return;
    }

    /*
     * Pass 2 (Chaos Governor): triggers when overlapping blasts or dense smoke
     * plumes exceed the platform fill-rate budget (e.g. close-up grenade blasts
     * or 4 grenades chain-detonating on Quest 3 at 120 Hz).
     *
     * Strategy:
     *   - Spatially cluster-merge heavily overlapping interior puffs (both fire
     *     and smoke, including across overlapping smoke batches).
     *   - For smoke, merging expands the retained puff's radius slightly to cover
     *     the union and optically composites opacity (1 - (1-aA)*(1-0.6*aB)) so
     *     multiplayer sightline occlusion and silhouette coverage NEVER shrink.
     *   - Isolated puffs (outside the merge radius) are NEVER dropped, and every
     *     batch retains at least 3 puffs for a rich multi-lobe 3D silhouette.
     */
    const float pressure = totalWeight / (areaBudget > 0.1f ? areaBudget : 1.0f);
    const float fireMergeFrac = clampf_vol(0.38f + (pressure - 1.0f) * 0.16f, 0.38f, 0.64f);
    const float smokeMergeFrac = clampf_vol(0.36f + (pressure - 1.0f) * 0.14f, 0.36f, 0.60f);

    /* Track retained large smoke puffs across earlier batches in this frame so
     * multi-grenade overlapping plumes share one unified dense 3D cloud. */
    GevrVolumetricPuff *keptSmoke[256];
    uint32_t keptSmokeCount = 0;

    uint32_t keptLargeFire = 0;
    for (uint32_t bi = 0; bi < s_batchCount; bi++) {
        GevrVolumetricBatch *b = &s_batches[bi];
        if (b->isSmallFx) {
            continue;
        }

        const int isFire = b->isFire != 0;
        const float mergeFrac = isFire ? fireMergeFrac : smokeMergeFrac;
        uint32_t writeIdx = 0;
        const uint32_t minKeep = b->puffCount < 3u ? b->puffCount : 3u;
        GevrVolumetricPuff *arr = &s_puffs[b->firstPuff];

        for (uint32_t i = 0; i < b->puffCount; i++) {
            GevrVolumetricPuff cand = arr[i];
            const float cov = puffScreenCoverage(&cand, b->proj);
            const uint32_t remaining = b->puffCount - i;
            int merged = 0;

            /* Only merge while enough puffs remain to preserve a multi-lobe silhouette. */
            if (writeIdx + remaining > minKeep) {
                /* 1) Check against already-retained puffs in this batch. */
                for (uint32_t j = 0; j < writeIdx; j++) {
                    GevrVolumetricPuff *k = &arr[j];
                    if (k->inside != cand.inside) continue;
                    const float dx = cand.viewPos[0] - k->viewPos[0];
                    const float dy = cand.viewPos[1] - k->viewPos[1];
                    const float dz = cand.viewPos[2] - k->viewPos[2];
                    const float d2 = dx * dx + dy * dy + dz * dz;
                    const float rMax = cand.radius > k->radius ? cand.radius : k->radius;
                    const float thresh = rMax * mergeFrac;
                    if (d2 < thresh * thresh) {
                        if (isFire) {
                            if (cand.radius > k->radius) k->radius = cand.radius;
                            if (cand.life < k->life) k->life = cand.life;
                            k->color[3] = clampf_vol(k->color[3] + cand.color[3] * 0.22f, 0.0f, 1.0f);
                        } else {
                            const float dist = sqrtf(d2);
                            const float rUnion = cand.radius + dist * 0.35f;
                            const float rCap = rMax * 1.14f;
                            if (rUnion > k->radius) {
                                k->radius = rUnion < rCap ? rUnion : rCap;
                            }
                            k->color[3] = clampf_vol(1.0f - (1.0f - k->color[3]) * (1.0f - cand.color[3] * 0.60f), 0.0f, 1.0f);
                        }
                        merged = 1;
                        break;
                    }
                }

                /* 2) For smoke under multi-grenade chaos, also merge near-coincident puffs
                 * into earlier smoke batches so 4 overlapping grenades don't quadruple overdraw. */
                if (!merged && !isFire && pressure > 1.25f) {
                    for (uint32_t j = 0; j < keptSmokeCount; j++) {
                        GevrVolumetricPuff *k = keptSmoke[j];
                        if (k->inside != cand.inside) continue;
                        const float dx = cand.viewPos[0] - k->viewPos[0];
                        const float dy = cand.viewPos[1] - k->viewPos[1];
                        const float dz = cand.viewPos[2] - k->viewPos[2];
                        const float d2 = dx * dx + dy * dy + dz * dz;
                        const float rMax = cand.radius > k->radius ? cand.radius : k->radius;
                        const float thresh = rMax * smokeMergeFrac;
                        if (d2 < thresh * thresh) {
                            const float dist = sqrtf(d2);
                            const float rUnion = cand.radius + dist * 0.35f;
                            const float rCap = rMax * 1.14f;
                            if (rUnion > k->radius) {
                                k->radius = rUnion < rCap ? rUnion : rCap;
                            }
                            k->color[3] = clampf_vol(1.0f - (1.0f - k->color[3]) * (1.0f - cand.color[3] * 0.55f), 0.0f, 1.0f);
                            merged = 1;
                            break;
                        }
                    }
                }
            }

            if (merged) {
                continue;
            }
            if (isFire && cov >= 0.045f && keptLargeFire >= largeCap) {
                /* Hard safety ceiling for fireballs: still keep at least 3 puffs per blast. */
                if (writeIdx >= 3u) {
                    continue;
                }
            }
            if (pressure > 1.4f && cand.detailTier > 0u) {
                cand.detailTier = isMobileTier ? 0u : 1u;
            }
            if (isFire && cov >= 0.045f) {
                keptLargeFire++;
            }
            arr[writeIdx++] = cand;
        }
        b->puffCount = writeIdx;

        if (!isFire) {
            for (uint32_t j = 0; j < writeIdx && keptSmokeCount < 256u; j++) {
                keptSmoke[keptSmokeCount++] = &arr[j];
            }
        }
    }
}

const GevrVolumetricBatch *gevrVolumetricGetBatch(int batchId)
{
    if (batchId < 0 || (uint32_t)batchId >= s_batchCount) {
        return NULL;
    }
    return &s_batches[batchId];
}

const GevrVolumetricPuff *gevrVolumetricGetBatchPuffs(const GevrVolumetricBatch *batch, uint32_t *outCount)
{
    if (!batch) {
        if (outCount) *outCount = 0;
        return NULL;
    }
    if (outCount) *outCount = batch->puffCount;
    return &s_puffs[batch->firstPuff];
}

/* --- Tileable 3D Noise Generator (Perlin/Value FBM + Inverted Worley) ---- */

static uint32_t hash3u(int x, int y, int z, uint32_t seed)
{
    uint32_t h = (uint32_t)x * 374761393u + (uint32_t)y * 668265263u +
                 (uint32_t)z * 2147483647u + seed * 1597334677u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return h ^ (h >> 16);
}

static float hash3f(int x, int y, int z, uint32_t seed)
{
    return (float)(hash3u(x, y, z, seed) & 0x00FFFFFFu) * (1.0f / 16777216.0f);
}

static int wrapCoord(int v, int period)
{
    int r = v % period;
    return r < 0 ? r + period : r;
}

static float smoothFade(float t)
{
    return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f);
}

static float tileValueNoise3D(float x, float y, float z, int period, uint32_t seed)
{
    const float fx = x * (float)period;
    const float fy = y * (float)period;
    const float fz = z * (float)period;
    const int ix = (int)floorf(fx);
    const int iy = (int)floorf(fy);
    const int iz = (int)floorf(fz);
    const float tx = smoothFade(fx - (float)ix);
    const float ty = smoothFade(fy - (float)iy);
    const float tz = smoothFade(fz - (float)iz);

    const int x0 = wrapCoord(ix, period), x1 = wrapCoord(ix + 1, period);
    const int y0 = wrapCoord(iy, period), y1 = wrapCoord(iy + 1, period);
    const int z0 = wrapCoord(iz, period), z1 = wrapCoord(iz + 1, period);

    const float c000 = hash3f(x0, y0, z0, seed);
    const float c100 = hash3f(x1, y0, z0, seed);
    const float c010 = hash3f(x0, y1, z0, seed);
    const float c110 = hash3f(x1, y1, z0, seed);
    const float c001 = hash3f(x0, y0, z1, seed);
    const float c101 = hash3f(x1, y0, z1, seed);
    const float c011 = hash3f(x0, y1, z1, seed);
    const float c111 = hash3f(x1, y1, z1, seed);

    const float c00 = c000 + (c100 - c000) * tx;
    const float c10 = c010 + (c110 - c010) * tx;
    const float c01 = c001 + (c101 - c001) * tx;
    const float c11 = c011 + (c111 - c011) * tx;

    const float c0 = c00 + (c10 - c00) * ty;
    const float c1 = c01 + (c11 - c01) * ty;
    return c0 + (c1 - c0) * tz;
}

static float tileWorleyInv3D(float x, float y, float z, int cells, uint32_t seed)
{
    const float px = x * (float)cells;
    const float py = y * (float)cells;
    const float pz = z * (float)cells;
    const int cx = (int)floorf(px);
    const int cy = (int)floorf(py);
    const int cz = (int)floorf(pz);

    float minD2 = 1e9f;
    for (int dz = -1; dz <= 1; dz++) {
        for (int dy = -1; dy <= 1; dy++) {
            for (int dx = -1; dx <= 1; dx++) {
                const int nx = cx + dx;
                const int ny = cy + dy;
                const int nz = cz + dz;
                const int wx = wrapCoord(nx, cells);
                const int wy = wrapCoord(ny, cells);
                const int wz = wrapCoord(nz, cells);
                const float jx = (float)nx + hash3f(wx, wy, wz, seed + 11u);
                const float jy = (float)ny + hash3f(wx, wy, wz, seed + 23u);
                const float jz = (float)nz + hash3f(wx, wy, wz, seed + 37u);
                const float rx = jx - px;
                const float ry = jy - py;
                const float rz = jz - pz;
                const float d2 = rx * rx + ry * ry + rz * rz;
                if (d2 < minD2) {
                    minD2 = d2;
                }
            }
        }
    }
    const float d = sqrtf(minD2);
    return clampf_vol(1.0f - d, 0.0f, 1.0f);
}

void gevrVolumetricBuildNoise3D(uint8_t *outRgba, int size)
{
    if (!outRgba || size <= 0) {
        return;
    }
    const float inv = 1.0f / (float)size;
    for (int z = 0; z < size; z++) {
        const float fz = ((float)z + 0.5f) * inv;
        for (int y = 0; y < size; y++) {
            const float fy = ((float)y + 0.5f) * inv;
            for (int x = 0; x < size; x++) {
                const float fx = ((float)x + 0.5f) * inv;

                /* R: smooth 3-octave FBM */
                const float fbm =
                    tileValueNoise3D(fx, fy, fz, 4, 101u) * 0.57f +
                    tileValueNoise3D(fx, fy, fz, 8, 211u) * 0.29f +
                    tileValueNoise3D(fx, fy, fz, 16, 307u) * 0.14f;

                /* G: puffy inverted Worley FBM (billowing smoke lobes) */
                const float worley =
                    tileWorleyInv3D(fx, fy, fz, 4, 401u) * 0.62f +
                    tileWorleyInv3D(fx, fy, fz, 8, 509u) * 0.26f +
                    tileWorleyInv3D(fx, fy, fz, 16, 613u) * 0.12f;

                /* B: crisp higher-frequency turbulent detail */
                const float turb =
                    tileValueNoise3D(fx, fy, fz, 8, 719u) * 0.55f +
                    tileValueNoise3D(fx, fy, fz, 16, 823u) * 0.45f;

                /* A: phase-shifted Worley lobe mask */
                const float lobe =
                    tileWorleyInv3D(fx, fy, fz, 6, 929u) * 0.68f +
                    tileWorleyInv3D(fx, fy, fz, 12, 1031u) * 0.32f;

                const size_t idx = ((size_t)(z * size + y) * (size_t)size + (size_t)x) * 4u;
                outRgba[idx + 0] = (uint8_t)clampf_vol(fbm * 255.0f + 0.5f, 0.0f, 255.0f);
                outRgba[idx + 1] = (uint8_t)clampf_vol(worley * 255.0f + 0.5f, 0.0f, 255.0f);
                outRgba[idx + 2] = (uint8_t)clampf_vol(turb * 255.0f + 0.5f, 0.0f, 255.0f);
                outRgba[idx + 3] = (uint8_t)clampf_vol(lobe * 255.0f + 0.5f, 0.0f, 255.0f);
            }
        }
    }
}

/* --- Unit Subdivision-1 Icosphere (42 verts, 80 outward CCW triangles) --- */

static uint16_t icoAddMidpoint(float *verts, uint32_t *vcount,
                               uint16_t a, uint16_t b,
                               uint32_t *edgeKeys, uint16_t *edgeMid, uint32_t *edgeCount)
{
    const uint16_t lo = a < b ? a : b;
    const uint16_t hi = a < b ? b : a;
    const uint32_t key = ((uint32_t)lo << 16) | (uint32_t)hi;
    for (uint32_t i = 0; i < *edgeCount; i++) {
        if (edgeKeys[i] == key) {
            return edgeMid[i];
        }
    }
    const uint16_t idx = (uint16_t)(*vcount);
    float mx = 0.5f * (verts[a * 3 + 0] + verts[b * 3 + 0]);
    float my = 0.5f * (verts[a * 3 + 1] + verts[b * 3 + 1]);
    float mz = 0.5f * (verts[a * 3 + 2] + verts[b * 3 + 2]);
    const float len = sqrtf(mx * mx + my * my + mz * mz);
    if (len > 1e-6f) {
        mx /= len;
        my /= len;
        mz /= len;
    }
    verts[idx * 3 + 0] = mx;
    verts[idx * 3 + 1] = my;
    verts[idx * 3 + 2] = mz;
    (*vcount)++;
    edgeKeys[*edgeCount] = key;
    edgeMid[*edgeCount] = idx;
    (*edgeCount)++;
    return idx;
}

static void icoPushOutwardTri(const float *verts, uint16_t *indices, uint32_t *icount,
                              uint16_t a, uint16_t b, uint16_t c)
{
    const float *va = &verts[a * 3];
    const float *vb = &verts[b * 3];
    const float *vc = &verts[c * 3];
    const float e1x = vb[0] - va[0], e1y = vb[1] - va[1], e1z = vb[2] - va[2];
    const float e2x = vc[0] - va[0], e2y = vc[1] - va[1], e2z = vc[2] - va[2];
    const float cx = e1y * e2z - e1z * e2y;
    const float cy = e1z * e2x - e1x * e2z;
    const float cz = e1x * e2y - e1y * e2x;
    const float nx = va[0] + vb[0] + vc[0];
    const float ny = va[1] + vb[1] + vc[1];
    const float nz = va[2] + vb[2] + vc[2];
    if (cx * nx + cy * ny + cz * nz < 0.0f) {
        uint16_t tmp = b;
        b = c;
        c = tmp;
    }
    indices[(*icount)++] = a;
    indices[(*icount)++] = b;
    indices[(*icount)++] = c;
}

void gevrVolumetricBuildIcosphere(float *outVerts, uint32_t *outVertCount,
                                  uint16_t *outIndices, uint32_t *outIndexCount)
{
    const float t = (1.0f + sqrtf(5.0f)) * 0.5f;
    const float base[12][3] = {
        { -1.0f,  t, 0.0f }, {  1.0f,  t, 0.0f }, { -1.0f, -t, 0.0f }, {  1.0f, -t, 0.0f },
        {  0.0f, -1.0f,  t }, {  0.0f,  1.0f,  t }, {  0.0f, -1.0f, -t }, {  0.0f,  1.0f, -t },
        {  t, 0.0f, -1.0f }, {  t, 0.0f,  1.0f }, { -t, 0.0f, -1.0f }, { -t, 0.0f,  1.0f }
    };
    static const uint16_t faces[20][3] = {
        {  0, 11,  5 }, {  0,  5,  1 }, {  0,  1,  7 }, {  0,  7, 10 }, {  0, 10, 11 },
        {  1,  5,  9 }, {  5, 11,  4 }, { 11, 10,  2 }, { 10,  7,  6 }, {  7,  1,  8 },
        {  3,  9,  4 }, {  3,  4,  2 }, {  3,  2,  6 }, {  3,  6,  8 }, {  3,  8,  9 },
        {  4,  9,  5 }, {  2,  4, 11 }, {  6,  2, 10 }, {  8,  6,  7 }, {  9,  8,  1 }
    };

    uint32_t vcount = 12;
    for (int i = 0; i < 12; i++) {
        const float len = sqrtf(base[i][0] * base[i][0] + base[i][1] * base[i][1] + base[i][2] * base[i][2]);
        outVerts[i * 3 + 0] = base[i][0] / len;
        outVerts[i * 3 + 1] = base[i][1] / len;
        outVerts[i * 3 + 2] = base[i][2] / len;
    }

    uint32_t edgeKeys[64];
    uint16_t edgeMid[64];
    uint32_t edgeCount = 0;
    uint32_t icount = 0;

    for (int f = 0; f < 20; f++) {
        const uint16_t a = faces[f][0];
        const uint16_t b = faces[f][1];
        const uint16_t c = faces[f][2];
        const uint16_t ab = icoAddMidpoint(outVerts, &vcount, a, b, edgeKeys, edgeMid, &edgeCount);
        const uint16_t bc = icoAddMidpoint(outVerts, &vcount, b, c, edgeKeys, edgeMid, &edgeCount);
        const uint16_t ca = icoAddMidpoint(outVerts, &vcount, c, a, edgeKeys, edgeMid, &edgeCount);

        icoPushOutwardTri(outVerts, outIndices, &icount, a, ab, ca);
        icoPushOutwardTri(outVerts, outIndices, &icount, b, bc, ab);
        icoPushOutwardTri(outVerts, outIndices, &icount, c, ca, bc);
        icoPushOutwardTri(outVerts, outIndices, &icount, ab, bc, ca);
    }

    if (outVertCount) *outVertCount = vcount;
    if (outIndexCount) *outIndexCount = icount;
}
