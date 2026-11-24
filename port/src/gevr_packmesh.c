/* Self-describing pack meshes. Read explicitly in little endian; do not
 * cast the unaligned corner records or trust file-provided counts. */
#include "gevr_packmesh.h"
#include <math.h>
#include <string.h>

#define CORNER_BYTES 28u

static uint32_t read32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static float readFloat(const uint8_t *p)
{
    const uint32_t bits = read32(p);
    float value;
    memcpy(&value, &bits, sizeof(value));
    return value;
}

void gevrPackMeshCorner(const GevrPackMeshView *view, unsigned index, GevrPackCorner *out)
{
    const uint8_t *p = view->corners + (size_t)index * CORNER_BYTES;
    unsigned k;

    for (k = 0; k < 3; ++k) out->position[k] = readFloat(p + k * 4);
    for (k = 0; k < 2; ++k) out->uv[k] = readFloat(p + 12 + k * 4);
    memcpy(out->rgba, p + 20, sizeof(out->rgba));
    out->texture = p[24];
}

int gevrPackMeshRead(const uint8_t *file, size_t length, GevrPackMeshView *out)
{
    GevrPackMeshView view = {0};
    const uint8_t *p, *end;
    unsigned i, k;
    uint8_t material = 0;

    if (file == NULL || length < 8 || (memcmp(file, "GEVRWPN1", 8) && memcmp(file, "GEVRWPN2", 8))) return 0;
    if (out == NULL || length < 16 || length > GEVR_PACKMESH_MAX_FILE) return -1;
    view.hasWatchMount = file[7] == '2';
    p = file + 8;
    end = file + length;
    view.textures = read32(p);
    p += 4;
    if (view.textures == 0 || view.textures > GEVR_PACKMESH_MAX_TEXTURES) return -1;
    for (i = 0; i < view.textures; ++i) {
        GevrPackPicture *pic = &view.pictures[i];
        size_t bytes;
        if (end - p < 4) return -1;
        pic->width = p[0] | p[1] << 8;
        pic->height = p[2] | p[3] << 8;
        p += 4;
        if (pic->width < 1 || pic->height < 1 || pic->width > 512 || pic->height > 512) return -1;
        bytes = (size_t)pic->width * pic->height * 4;
        if ((size_t)(end - p) < bytes) return -1;
        pic->rgba = p;
        p += bytes;
    }
    if (end - p < 4) return -1;
    view.triangles = read32(p);
    p += 4;
    if (view.triangles == 0 || view.triangles > GEVR_PACKMESH_MAX_TRIANGLES
        || (size_t)(end - p) != (size_t)view.triangles * 3 * CORNER_BYTES + (view.hasWatchMount ? 36 : 0)) return -1;
    view.corners = p;
    for (i = 0; i < view.triangles * 3; ++i, p += CORNER_BYTES) {
        for (k = 0; k < 5; ++k) if (!isfinite(readFloat(p + k * 4))) return -1;
        if (p[24] >= view.textures || (i % 3 && p[24] != material)) return -1;
        material = p[24];
    }
    if (view.hasWatchMount) {
        for (k = 0; k < 9; ++k) {
            view.watchMount[k] = readFloat(p + k * 4);
            if (!isfinite(view.watchMount[k])) return -1;
        }
    }
    *out = view;
    return 1;
}
