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
    gevrModMeshReset();
    gevrPackMeshCollect();
    return 0;
}
