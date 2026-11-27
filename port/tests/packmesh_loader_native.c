/* Run the production pack mesh loader with just asset I/O and draw plumbing.
 * test_packmesh.py supplies generated files in the working directory. */
#include "gevr_modmesh.h"
#include "gevr_modassets.h"
#include "gevr_packmesh.h"
#include "dyn.h"
#include "system.h"
#include <PR/gu.h>
#include <assert.h>
#include <math.h>
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

/* Each frame matrix records its float source so the split draw's offsets can be read back. */
static Mtx s_matrix[3];
static float s_poses[3][4][4];
static int s_allocated;
s32 gevrDynRoom(Gfx *gdl, s32 gfx, s32 bytes)
{
    (void)gdl;
    return gfx <= 12 && bytes <= (s32)sizeof(s_matrix);
}
void *dynAllocate(s32 size)
{
    assert(size == (s32)sizeof(s_matrix[0]) && s_allocated < 3);
    return &s_matrix[s_allocated++];
}
void guMtxF2L(float source[4][4], Mtx *matrix)
{
    memset(matrix, 0, sizeof(*matrix));
    memcpy(s_poses[matrix - s_matrix], source, sizeof(s_poses[0]));
}

/* triangles in a list */
static int listTriangles(const Gfx *list)
{
    int triangles = 0;
    for (int i = 0; i < 512; ++i) {
        switch ((list[i].words.w0 >> 24) & 255) {
        case G_TRI1: ++triangles; break;
        case G_ENDDL: return triangles;
        }
    }
    assert(0 && "list did not end");
    return -1;
}

/* the split body: the rest at the pose, then each moving group at its offset */
static void checkSplit(const GevrModMesh *mesh)
{
    static const unsigned bad[][GEVR_MODPART_GROUPS] = {{4, 0}, {2, 2}};
    static const unsigned bolt[GEVR_MODPART_GROUPS] = {2, 0};
    static const float offset[GEVR_MODPART_GROUPS][3] = {{0, 0, -44}, {0, 0, 0}};
    const Mtxf pose = {.m = {{0,0,2,0}, {0,2,0,0}, {-2,0,0,0}, {5,6,7,1}}};
    Gfx draw[12];
    for (unsigned i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i)
        assert(!gevrModMeshSplit(mesh, bad[i]));   /* a picture past the file's, one picture in both groups */
    assert(gevrModMeshSplit(mesh, bolt) && gevrModMeshSplit(mesh, bolt));   /* built once, then found */
    s_allocated = 0;
    Gfx *end = gevrModMeshDrawParts(draw, &pose, mesh, bolt, offset);
    assert(end == draw + 6 && s_allocated == 2);
    assert((draw[2].words.w0 >> 24) == G_DL && (draw[4].words.w0 >> 24) == G_DL);
    assert(listTriangles((const Gfx *)draw[2].words.w1) == 1);   /* picture 0's triangle stays */
    assert(listTriangles((const Gfx *)draw[4].words.w1) == 1);   /* picture 1's moves */
    assert(!memcmp(s_poses[0], pose.m, sizeof(s_poses[0])));
    /* 44 back along the body's own z is 88 along the pose's +x, from its origin */
    assert(s_poses[1][3][0] == 5 + 88 && s_poses[1][3][1] == 6 && s_poses[1][3][2] == 7);
    assert(!memcmp(s_poses[1], pose.m, 3 * sizeof(s_poses[0][0])));
    s_allocated = 0;
    assert(gevrModMeshDrawParts(draw, &pose, mesh, NULL, offset) == draw + 4 && s_allocated == 1);   /* unsplit: whole */
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
    s_allocated = 0;
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
    /* The body mirrors X, swaps Y/Z, scales the model and shifts its root. */
    const Mtxf fit = {.m = {{-2,0,0,0}, {0,0,2,0}, {0,2,0,0}, {12,13,14,1}}};
    const float muzzle[3] = {3,5,7};
    float point[3];
    gevrModMeshPoint(NULL, muzzle, point);
    assert(point[0] == 3 && point[1] == 5 && point[2] == 7);
    gevrModMeshPoint(&fit, muzzle, point);
    assert(point[0] == 6 && point[1] == 27 && point[2] == 24);
    const GevrModMesh *mesh = NULL, *other = NULL;
    assert(gevrModMeshLoad("valid.bin", &mesh) == 1 && mesh != NULL);
    assert(gevrModMeshLoad("valid.bin", &other) == 1 && other == mesh);
    assert(gevrModMeshLoad("bad.bin", &other) == -1 && other == NULL);
    {
        float centre[3], along[3], normal[3];
        assert(!gevrModMeshWrist(mesh, centre, along, normal));   /* GEVRWPN1: none */
        assert(gevrModMeshLoad("wrist.bin", &other) == 1 && other != NULL);
        assert(gevrModMeshWrist(other, centre, along, normal));
        /* along (0,2,0) ahead, normal (0,0,3) up, centre (0.01,0.02,0.03) m:
         * unit vectors and mm in X left, Y up, Z ahead */
        assert(along[0] == 0 && along[1] == 0 && along[2] == 1);
        assert(normal[0] == 0 && normal[1] == 1 && normal[2] == 0);
        assert(fabsf(centre[0] + 10) < 1e-3f && fabsf(centre[1] - 30) < 1e-3f && fabsf(centre[2] - 20) < 1e-3f);
    }
    assert(gevrModMeshLoad("legacy.bin", &other) == 0 && other == NULL);

    s_allocated = 0;
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
    checkSplit(other);           /* its second picture as a moving part */
    checkPasses(other, 1, 0, 1); /* the whole body's list is unchanged by a split */
    assert(gevrModMeshLoad("vertex-alpha.bin", &other) == 1 && other != NULL);
    checkPasses(other, 0, 1, 1); /* alpha on corners still blends */
    assert(gevrModMeshLoad("invalid-sidecar.bin", &other) == -1 && other == NULL);
    gevrModMeshReset();
    gevrPackMeshCollect();
    return 0;
}
