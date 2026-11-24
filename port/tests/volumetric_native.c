/*
 * Native tests for analytical 3D volumetric smoke & explosions (issue #249):
 *   - Unit icosphere topology, unit radius, and consistent outward CCW winding
 *   - Seamless 32^3 RGBA8 3D noise volume bounds, variance, and wrap continuity
 *   - Frustum culling, inside-camera classification, and back-to-front sorting
 *   - Chaos governor: leaves normal scenes intact, thins redundant interior
 *     flames during 4-grenade overlap on mobile/Quest while preserving smoke
 *     occlusion parity (MP fairness) and lowering detail tiers under load
 */
#include "gevr_volumetric.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void check(int ok, const char *msg)
{
    if (!ok) {
        fprintf(stderr, "FAIL: %s\n", msg);
        exit(1);
    }
}

static void makePersp(float out[16], float fovyRad, float aspect)
{
    const float f = 1.0f / tanf(fovyRad * 0.5f);
    memset(out, 0, 16 * sizeof(float));
    out[0] = f / aspect;
    out[5] = f;
    out[10] = -1.001f;
    out[11] = -1.0f;
    out[14] = -2.0f;
}

static GevrVolumetricPuff makePuff(float vx, float vy, float vz, float r, uint8_t kind, float alpha, float life)
{
    GevrVolumetricPuff p;
    memset(&p, 0, sizeof(p));
    p.viewPos[0] = vx;
    p.viewPos[1] = vy;
    p.viewPos[2] = vz;
    p.radius = r;
    p.color[0] = 0.8f;
    p.color[1] = 0.8f;
    p.color[2] = 0.8f;
    p.color[3] = alpha;
    p.life = life;
    p.kind = kind;
    return p;
}

int main(void)
{
    /* 1. Icosphere geometry & outward CCW winding */
    {
        float verts[GEVR_VOL_ICO_VERTS * 3];
        uint16_t indices[GEVR_VOL_ICO_INDICES];
        uint32_t vc = 0, ic = 0;
        gevrVolumetricBuildIcosphere(verts, &vc, indices, &ic);
        check(vc == GEVR_VOL_ICO_VERTS, "icosphere produces 42 vertices");
        check(ic == GEVR_VOL_ICO_INDICES, "icosphere produces 240 indices (80 tris)");
        for (uint32_t i = 0; i < vc; i++) {
            float x = verts[i * 3 + 0], y = verts[i * 3 + 1], z = verts[i * 3 + 2];
            float len = sqrtf(x * x + y * y + z * z);
            check(fabsf(len - 1.0f) < 1e-4f, "every icosphere vertex lies on the unit sphere");
        }
        for (uint32_t t = 0; t < ic; t += 3) {
            uint16_t ia = indices[t], ib = indices[t + 1], icc = indices[t + 2];
            check(ia < vc && ib < vc && icc < vc, "icosphere triangle indices in range");
            const float *a = &verts[ia * 3], *b = &verts[ib * 3], *c = &verts[icc * 3];
            float e1[3] = { b[0] - a[0], b[1] - a[1], b[2] - a[2] };
            float e2[3] = { c[0] - a[0], c[1] - a[1], c[2] - a[2] };
            float cr[3] = {
                e1[1] * e2[2] - e1[2] * e2[1],
                e1[2] * e2[0] - e1[0] * e2[2],
                e1[0] * e2[1] - e1[1] * e2[0]
            };
            float centroid[3] = { a[0] + b[0] + c[0], a[1] + b[1] + c[1], a[2] + b[2] + c[2] };
            float dot = cr[0] * centroid[0] + cr[1] * centroid[1] + cr[2] * centroid[2];
            check(dot > 0.0f, "every icosphere triangle has outward CCW winding");
        }
    }

    /* 2. Tileable 3D noise volume */
    {
        const int sz = GEVR_VOL_NOISE_SIZE;
        uint8_t *tex = (uint8_t *)malloc((size_t)sz * sz * sz * 4u);
        check(tex != NULL, "allocate 3D noise buffer");
        gevrVolumetricBuildNoise3D(tex, sz);
        uint32_t minC[4] = { 255, 255, 255, 255 };
        uint32_t maxC[4] = { 0, 0, 0, 0 };
        for (size_t i = 0; i < (size_t)sz * sz * sz; i++) {
            for (int c = 0; c < 4; c++) {
                uint8_t v = tex[i * 4 + c];
                if (v < minC[c]) minC[c] = v;
                if (v > maxC[c]) maxC[c] = v;
            }
        }
        for (int c = 0; c < 4; c++) {
            check(maxC[c] - minC[c] >= 80u, "each 3D noise channel has meaningful dynamic range");
        }
        /* Seam continuity check across periodic X boundary (x=0 vs x=sz-1). */
        double diffSum = 0.0;
        for (int z = 0; z < sz; z++) {
            for (int y = 0; y < sz; y++) {
                size_t i0 = ((size_t)(z * sz + y) * sz + 0) * 4u;
                size_t i1 = ((size_t)(z * sz + y) * sz + (sz - 1)) * 4u;
                diffSum += fabs((double)tex[i0] - (double)tex[i1]);
            }
        }
        double avgSeam = diffSum / (double)(sz * sz);
        check(avgSeam < 28.0, "3D noise wraps smoothly across periodic boundary");
        free(tex);
    }

    /* 3. Culling, inside-camera classification, and back-to-front sorting */
    {
        float proj[16];
        float id3[9] = { 1,0,0, 0,1,0, 0,0,1 };
        makePersp(proj, 1.3f, 1.0f);

        gevrVolumetricBeginFrame();
        GevrVolumetricPuff input[5] = {
            makePuff(0.0f, 0.0f,  500.0f, 100.0f, GEVR_VOL_KIND_SMOKE, 0.8f, 0.2f), /* behind camera -> culled */
            makePuff(0.0f, 0.0f, -200.0f,  80.0f, GEVR_VOL_KIND_SMOKE, 0.8f, 0.2f), /* near outside */
            makePuff(0.0f, 0.0f, -900.0f,  80.0f, GEVR_VOL_KIND_SMOKE, 0.8f, 0.2f), /* far outside */
            makePuff(0.0f, 0.0f,  -40.0f, 150.0f, GEVR_VOL_KIND_SMOKE, 0.8f, 0.2f), /* enclosing camera */
            makePuff(9000.0f, 0.0f, -200.0f, 50.0f, GEVR_VOL_KIND_SMOKE, 0.8f, 0.2f) /* off frustum -> culled */
        };
        int bid = gevrVolumetricPushBatch(input, 5, proj, id3, 1.0f, 0, 0);
        check(bid == 0, "visible batch assigned id 0");
        gevrVolumetricPrepareDraw(1);

        const GevrVolumetricBatch *b = gevrVolumetricGetBatch(bid);
        uint32_t n = 0;
        const GevrVolumetricPuff *out = gevrVolumetricGetBatchPuffs(b, &n);
        check(n == 3, "culled behind-camera and off-frustum puffs, kept 3");
        check(out[0].inside == 0 && out[0].viewPos[2] == -900.0f, "farthest outside puff draws first");
        check(out[1].inside == 0 && out[1].viewPos[2] == -200.0f, "closer outside puff draws second");
        check(out[2].inside == 1 && out[2].viewPos[2] == -40.0f, "camera-enclosing puff draws last as back-face");

        /* Dam-scale (D_800364CC = 0.2): puff 3m ahead (60 view units) with 1m radius (20 view units)
         * must remain outside (depth-tested) rather than triggering inside-cloud back-face mode. */
        gevrVolumetricBeginFrame();
        GevrVolumetricPuff damPuff = makePuff(0.0f, 0.0f, -60.0f, 20.0f, GEVR_VOL_KIND_SMOKE, 0.8f, 0.2f);
        int damId = gevrVolumetricPushBatch(&damPuff, 1, proj, id3, 1.0f, 0, 0);
        check(damId == 0, "Dam-scale puff queued");
        gevrVolumetricPrepareDraw(1);
        const GevrVolumetricPuff *damOut = gevrVolumetricGetBatchPuffs(gevrVolumetricGetBatch(damId), &n);
        check(n == 1 && damOut[0].inside == 0, "Dam-scale puff 3m away stays outside/depth-tested");
    }

    /* 4. Chaos governor: 4 overlapping grenades on mobile/Quest spatially cluster-merge
     * redundant flames and stacked smoke while preserving full multi-lobe silhouettes
     * and never dropping isolated smoke puffs (MP sightline fairness). */
    {
        float proj[16];
        float id3[9] = { 1,0,0, 0,1,0, 0,0,1 };
        makePersp(proj, 1.4f, 1.0f);

        gevrVolumetricBeginFrame();
        int fireIds[4], smokeIds[4];
        for (int g = 0; g < 4; g++) {
            GevrVolumetricPuff flames[40];
            for (int i = 0; i < 40; i++) {
                float ox = (float)((i % 5) - 2) * 18.0f + (float)g * 30.0f;
                float oy = (float)(((i / 5) % 4) - 1) * 18.0f;
                float oz = -240.0f - (float)(i / 20) * 20.0f;
                flames[i] = makePuff(ox, oy, oz, 220.0f, GEVR_VOL_KIND_FIRE, 1.0f, (float)i / 40.0f);
            }
            fireIds[g] = gevrVolumetricPushBatch(flames, 40, proj, id3, 2.5f, 1, 0);
            check(fireIds[g] >= 0, "fire chaos batch queued");

            GevrVolumetricPuff smoke[10];
            for (int i = 0; i < 10; i++) {
                smoke[i] = makePuff((float)g * 30.0f, (float)i * 25.0f, -260.0f, 260.0f, GEVR_VOL_KIND_SMOKE, 0.7f, 0.3f);
            }
            smokeIds[g] = gevrVolumetricPushBatch(smoke, 10, proj, id3, 2.5f, 0, 0);
            check(smokeIds[g] >= 0, "smoke chaos batch queued");
        }
        /* Isolated distant smoke cloud well outside the chaos cluster must stay untouched. */
        GevrVolumetricPuff isolated = makePuff(0.0f, 0.0f, -1500.0f, 120.0f, GEVR_VOL_KIND_SMOKE, 0.75f, 0.2f);
        int isoId = gevrVolumetricPushBatch(&isolated, 1, proj, id3, 2.5f, 0, 0);
        check(isoId >= 0, "isolated smoke batch queued");

        gevrVolumetricPrepareDraw(1); /* Quest / mobile tier */

        uint32_t totalFire = 0, totalSmoke = 0;
        for (int g = 0; g < 4; g++) {
            uint32_t fn = 0, sn = 0;
            const GevrVolumetricPuff *sp = gevrVolumetricGetBatchPuffs(gevrVolumetricGetBatch(smokeIds[g]), &sn);
            gevrVolumetricGetBatchPuffs(gevrVolumetricGetBatch(fireIds[g]), &fn);
            check(fn >= 3, "every chaos explosion retains visible core/silhouette puffs");
            check(sn >= 3, "every chaos smoke plume retains multi-lobe silhouette");
            /* Optical opacity compositing boosts retained merged puffs to preserve occlusion. */
            if (sn < 10) {
                check(sp[0].color[3] >= 0.7f, "merged smoke retains or increases optical opacity");
            }
            totalFire += fn;
            totalSmoke += sn;
        }
        uint32_t isoN = 0;
        gevrVolumetricGetBatchPuffs(gevrVolumetricGetBatch(isoId), &isoN);
        check(isoN == 1, "isolated smoke puff outside chaos cluster is never dropped");
        check(totalFire < 80, "chaos governor substantially thins 160 overlapping close-up flame spheres on Quest");
        check(totalSmoke < 40 && totalSmoke >= 12, "chaos governor merges heavily overlapping smoke layers while preserving coverage");
    }

    printf("PASS: volumetric smoke & explosions: icosphere, 3D noise, culling/sorting, inside-cloud & Quest chaos governor\n");
    return 0;
}
