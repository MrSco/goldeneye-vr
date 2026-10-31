#ifndef GEVR_STATIC_MESH_H
#define GEVR_STATIC_MESH_H

#include <stdint.h>

/*
 * Host-only display-list extension, as G_GEVR_SKY_VTX: a list built once
 * (Bean's rooms and sky, gevr_bean_world.c) also kept as one mesh in GPU
 * buffers made once. DAB's G_MESH_EXT (port/licenses/dabs-mod.txt; its
 * roommesh.c and fast3d gfx_room_kept_run) is the model.
 *
 * The command's w1 is a GevrStaticMesh. It heads a wrapper list, followed by
 * a branch to the list itself. While fast3d runs that list, each run of
 * G_VTX/G_TRI1 commands the mesh covers is drawn from the buffers instead,
 * with the state the commands before it set; a run the renderer cannot draw
 * so (gfx_pc.cpp gevr_mesh_draw_run) is run on the CPU as before.
 */
#define G_GEVR_MESH 0x6F
#define G_GEVR_WORLD_VTX 0x6A
#define G_GEVR_NORMAL_TEX1 0x69
/* HD room coordinates retain sub-unit decal separation and large extents. */
typedef struct GevrWorldVertex {
    float p[3];
    int16_t tc[2];
    uint8_t rgba[4];
    float normal[3];
} GevrWorldVertex;

typedef struct GevrStaticMeshRun {
    const void *start, *end;   /* the run's first command and the one after it */
    uint32_t firstIndex, indexCount;
    float lo[3], hi[3];
    int hasBounds; // static room chunks; skinned runs use their pose, not this box
} GevrStaticMeshRun;

typedef struct GevrStaticMesh {
    const void *vertices;      /* Vtx, as the list loads them, each once */
    uint32_t numVertices;
    int worldVertices;
    const uint32_t *indices;   /* three per triangle, in the list's order */
    uint32_t numIndices;
    const GevrStaticMeshRun *runs;
    uint32_t numRuns;
    void *gpu;                 /* the renderer's buffers, made when first drawn */
    /* Optional, for the buffers only: each distinct vertex once and indices
     * into them (numIndices of them). A Bean skin repeats every shared corner
     * per triangle, the layout its CPU pose and the list's loads keep. */
    const void *gpuVertices;
    uint32_t numGpuVertices;
    const uint32_t *gpuIndices;
} GevrStaticMesh;

#ifdef __cplusplus
extern "C" {
#endif

/* The mesh of a list of state commands and G_VTX/G_TRI1 runs (one
 * gevrBeanEmitList); NULL when it has none, or a triangle uses a vertex an
 * earlier run loaded. */
GevrStaticMesh *gevrStaticMeshBuild(const void *gdl);
/* Gives its GPU buffers back (gfx_pc.cpp) and frees it. */
void gevrStaticMeshFree(GevrStaticMesh *mesh);
/* gfx_pc.cpp: the renderer forgets a mesh's buffers before the mesh goes. */
void gfx_static_mesh_release(GevrStaticMesh *mesh);

#ifdef __cplusplus
}
#endif

#endif
