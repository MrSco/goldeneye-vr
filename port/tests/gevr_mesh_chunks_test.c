#include <ultra64.h>
#include <PR/gbi.h>
#include "gevr_static_mesh.h"
#include "gevr_mesh_visibility.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
uintptr_t osVirtualToPhysical(void *p) { return (uintptr_t)p; }
void gfx_static_mesh_release(GevrStaticMesh *m) { (void)m; }
/* PRODUCTION MESH BUILDER */
int main(void)
{
    /* One huge material with three spatial sections, then a state change.
     * Repeated VTX batches share vertices: preserve every triangle and order. */
    Vtx vertices[3][3] = {{{0}}};
    Gfx list[2500], *g = list;
    for (int section = 0; section < 3; section++) {
        for (int v = 0; v < 3; v++) {
            vertices[section][v].v.ob[0] = section * 20 + (v == 1);
            vertices[section][v].v.ob[1] = v == 2;
            vertices[section][v].v.ob[2] = -10;
            vertices[section][v].v.cn[3] = 255;
        }
        for (int i = 0; i < 520; i += 5) {
            gSPVertex(g++, vertices[section], 3, 0);
            for (int t = 0; t < 5; t++) gSP1Triangle(g++, 0, 1, 2, 0);
        }
    }
    gDPSetPrimColor(g++, 0, 0, 255, 0, 0, 255);
    gSPVertex(g++, vertices[0], 3, 0);
    gSP1Triangle(g++, 0, 1, 2, 0);
    gSPEndDisplayList(g++);
    GevrStaticMesh *m = gevrStaticMeshBuild(list);
    assert(m && m->numIndices == 1561 * 3 && m->numVertices == 9);
    assert(m->numRuns == 5);
    const Vtx *v = m->vertices;
    unsigned count = 0;
    for (unsigned r = 0; r < m->numRuns; r++) {
        const GevrStaticMeshRun *run = &m->runs[r];
        assert(run->hasBounds && run->indexCount <= 515 * 3 && run->firstIndex == count);
        assert(((const Gfx *)run->start)->words.w0 >> 24 == (G_VTX & 255));
        for (unsigned i = 0; i < run->indexCount; i++, count++) {
            const Vtx *actual = &v[m->indices[count]];
            const Vtx *expected = &vertices[count / 3 < 1560 ? count / 3 / 520 : 0][count % 3];
            assert(!memcmp(actual, expected, sizeof(Vtx)));
            for (int k = 0; k < 3; k++)
                assert(actual->v.ob[k] >= run->lo[k] && actual->v.ob[k] <= run->hi[k]);
        }
    }
    assert(count == m->numIndices);
    /* Perspective camera, clip w=-z: widening by stereo keeps edge geometry. */
    float mp[16] = {1,0,0,0, 0,1,0,0, 0,0,1,-1, 0,0,0,0};
    float lo[3] = {11,0,-10}, hi[3] = {12,1,-10};
    assert(!gevrMeshBoundsVisible(lo, hi, mp, 0,0,0,.3f));
    assert(gevrMeshBoundsVisible(lo, hi, mp, 3,0,0,.3f));
    lo[0] = -1; hi[0] = 1;
    assert(gevrMeshBoundsVisible(lo, hi, mp, 0,0,0,.3f));
    lo[2] = -1; hi[2] = 1; /* camera crossing never rejected */
    assert(gevrMeshBoundsVisible(lo, hi, mp, 0,0,0,.3f));
    lo[2] = 1; hi[2] = 2;
    assert(!gevrMeshBoundsVisible(lo, hi, mp, 0,0,0,.3f));
    mp[0] = NAN;
    assert(gevrMeshBoundsVisible(lo, hi, mp, 0,0,0,.3f));
    gevrStaticMeshFree(m);
    puts("Mesh chunks: exact triangle/state order, bounds, stereo and camera clipping passed");
    return 0;
}
