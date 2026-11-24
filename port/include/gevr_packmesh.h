#ifndef GEVR_PACKMESH_H
#define GEVR_PACKMESH_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define GEVR_PACKMESH_MAX_TEXTURES 32
#define GEVR_PACKMESH_MAX_TRIANGLES 50000
#define GEVR_PACKMESH_MAX_FILE (64u << 20)

typedef struct {
    const uint8_t *rgba;
    int width, height;
} GevrPackPicture;

typedef struct {
    GevrPackPicture pictures[GEVR_PACKMESH_MAX_TEXTURES];
    const uint8_t *corners;
    unsigned textures, triangles;
    int hasWatchMount;
    float watchMount[9];   /* along, normal, centre */
} GevrPackMeshView;

typedef struct {
    float position[3], uv[2];
    uint8_t rgba[4], texture;
} GevrPackCorner;

/* The file owns the bytes referenced by this view. 0: other format;
 * -1: recognized, malformed; 1: valid. */
int gevrPackMeshRead(const uint8_t *file, size_t length, GevrPackMeshView *out);
void gevrPackMeshCorner(const GevrPackMeshView *view, unsigned index, GevrPackCorner *out);

/* Fast3D's full-colour texture path, shared with other host model assets. */
int gevrPackMeshTexture(const void *address, const uint8_t **rgba, int *width, int *height);
/* Called after Fast3D has submitted the frame, including both eyes. */
void gevrPackMeshCollect(void);

#ifdef __cplusplus
}
#endif
#endif
