/* Run the production pack mesh loader with just asset I/O and draw plumbing.
 * test_packmesh.py supplies generated files in the working directory. */
#include "gevr_modmesh.h"
#include "gevr_modassets.h"
#include "gevr_packmesh.h"
#include "dyn.h"
#include "system.h"
#include <PR/gu.h>
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

FILE *gevrModAssetOpenModel(const char *name) { return fopen(name, "rb"); }
FILE *gevrModAssetOpenModelOpacity(const char *name)
{
    char path[72];
    snprintf(path, sizeof(path), "%s.opaque", name);
    return fopen(path, "rb");
}
uintptr_t osVirtualToPhysical(void *address) { return (uintptr_t)address; }
void gfx_texture_cache_delete(const uint8_t *address) { (void)address; }

void sysLogPrintf(s32 level, const char *format, ...)
{
    va_list args;
    (void)level;
    va_start(args, format);
    vprintf(format, args);
    va_end(args);
    putchar('\n');
}

static Mtx s_matrix;
s32 gevrDynRoom(Gfx *gdl, s32 gfx, s32 bytes)
{
    (void)gdl;
    return gfx <= 4 && bytes <= (s32)sizeof(s_matrix);
}
void *dynAllocate(s32 size)
{
    assert(size == (s32)sizeof(s_matrix));
    return &s_matrix;
}
void guMtxF2L(float source[4][4], Mtx *matrix)
{
    (void)source;
    memset(matrix, 0, sizeof(*matrix));
}

static void checkPasses(const GevrModMesh *mesh, int expectedRgb, int expectedBlendedRgb, int expectedBlendedRgba)
{
    static const Gfx modes[] = {
        gsDPSetRenderMode(G_RM_AA_ZB_OPA_SURF, G_RM_AA_ZB_OPA_SURF2),
        gsDPSetRenderMode(G_RM_AA_ZB_XLU_SURF, G_RM_AA_ZB_XLU_SURF2),
        gsDPSetCombineMode(G_CC_MODULATERGB, G_CC_MODULATERGB2),
        gsDPSetCombineMode(G_CC_MODULATERGBA, G_CC_MODULATERGBA2),
    };
    Gfx draw[4];
    Mtxf pose = {0};
    int blend = 0, rgb = 0, solidRgb = 0, blendedRgb = 0, blendedRgba = 0;
    assert(gevrModMeshDraw(draw, &pose, mesh) == draw + 4);
    const Gfx *list = (const Gfx *)draw[2].words.w1;
    for (int i = 0; i < 64; ++i) {
        for (int k = 0; k < 4; ++k) {
            if (list[i].words.w0 == modes[k].words.w0 && list[i].words.w1 == modes[k].words.w1) {
                if (k < 2) blend = k;
                else rgb = k == 2;
            }
        }
        switch ((list[i].words.w0 >> 24) & 255) {
        case G_TRI1:
            if (rgb) {
                if (blend) ++blendedRgb;
                else ++solidRgb;
            } else if (blend) ++blendedRgba;
            break;
        case G_ENDDL:
            assert(solidRgb == expectedRgb && blendedRgb == expectedBlendedRgb && blendedRgba == expectedBlendedRgba);
            assert(!blend && !rgb);  /* don't leak the override to a later draw */
            return;
        }
    }
    assert(0 && "pack display list did not end");
}

int main(void)
{
    Gfx draw[4];
    Mtxf pose = {0};
    const GevrModMesh *mesh = NULL, *other = NULL;
    assert(gevrModMeshLoad("valid.bin", &mesh) == 1 && mesh != NULL);
    assert(gevrModMeshLoad("valid.bin", &other) == 1 && other == mesh);
    assert(gevrModMeshLoad("bad.bin", &other) == -1 && other == NULL);
    assert(gevrModMeshLoad("legacy.bin", &other) == 0 && other == NULL);

    assert(gevrModMeshDraw(draw, &pose, mesh) == draw + 4);
    assert((draw[2].words.w0 >> 24) == G_DL);
    const Gfx *list = (const Gfx *)draw[2].words.w1;
    int sawVertex = 0, sawTriangle = 0, terminated = 0;
    for (int i = 0; i < 32; ++i) {
        const uintptr_t op = (list[i].words.w0 >> 24) & 255;
        if (op == G_ENDDL) {
            terminated = 1;
            break;
        }
        sawVertex |= op == G_VTX;
        sawTriangle |= op == G_TRI1 || op == G_TRI2;
    }
    assert(sawVertex && sawTriangle && terminated);
    checkPasses(mesh, 0, 0, 1);  /* existing texture-alpha behavior */
    assert(gevrModMeshLoad("mixed.bin", &other) == 1 && other != NULL);
    checkPasses(other, 1, 0, 1); /* only the first picture ignores texture alpha */
    assert(gevrModMeshLoad("vertex-alpha.bin", &other) == 1 && other != NULL);
    checkPasses(other, 0, 1, 1); /* alpha on corners still blends */
    assert(gevrModMeshLoad("invalid-sidecar.bin", &other) == -1 && other == NULL);
    gevrModMeshReset();
    gevrPackMeshCollect();
    return 0;
}
