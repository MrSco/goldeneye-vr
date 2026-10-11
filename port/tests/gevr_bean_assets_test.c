#include "gevr_bean_int.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

/* Host stub: no graphics context exists in the asset decoder tests. */
void videoFreeCachedTexture(const void *addr) { (void)addr; }

int main(int argc, char **argv)
{
    assert(gevrBeanLogicSelfTest() == 0);
    for (int a = 1; a < argc; a++) {
        struct gevrBeanMesh *mesh = NULL;
        if (!gevrBeanLoad(argv[a], 1, &mesh)) {
            FILE *file = fopen(argv[a], "rb");
            assert(file);
            fseek(file, 0, SEEK_END); long size = ftell(file); rewind(file);
            uint8_t *bytes = malloc(size); assert(bytes);
            assert(fread(bytes, 1, size, file) == (size_t)size); fclose(file);
            struct gevrCaff caff;
            int opened = gevrCaffOpen(&caff, bytes, size);
            int noGpu = opened && gevrCaffFind(&caff, 1, ".gpu") < 0;
            if (opened) gevrCaffClose(&caff);
            free(bytes);
            if (noGpu) {
                printf("FALLBACK %s: no mesh GPU payload, keep N64 geometry\n", argv[a]);
                continue;
            }
            fprintf(stderr, "FAILED to load %s\n", argv[a]);
            return 1;
        }
        int missing = 0;
        for (int t = 0; t < mesh->ntex; t++) {
            if (!mesh->standin[t]) missing++;
        }
        for (int t = 0; t < mesh->ntris; t++) {
            const struct gevrBeanTri *tri = &mesh->tris[t];
            assert(tri->sk < GEVR_BEAN_SK_COUNT);
            assert(tri->tex == -1 || (tri->tex >= 0 && tri->tex < mesh->ntex));
            for (int i = 0; i < 9; i++) {
                if (!isfinite(tri->p[i]) || !isfinite(tri->n[i])) {
                    fprintf(stderr, "bad triangle %d coordinate %d: %g normal %g\n", t, i, tri->p[i], tri->n[i]);
                    return 2;
                }
            }
            for (int i = 0; i < 6; i++) assert(isfinite(tri->uv[i]));
        }
        printf("OK %s: %d triangles, %d texture slots, %d undecoded\n",
               argv[a], mesh->ntris, mesh->ntex, missing);
        gevrBeanMeshFree(mesh);
        gevrBeanTexReset();
    }
    return 0;
}
