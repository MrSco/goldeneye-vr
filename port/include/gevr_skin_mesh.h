#ifndef GEVR_SKIN_MESH_H
#define GEVR_SKIN_MESH_H

#include "gevr_static_mesh.h"

#define G_GEVR_SKIN 0x6E
#define G_GEVR_VERTEX_TINT 0x6B
#define GEVR_SKIN_BONES 64
#define GEVR_SKIN_WOUNDS 8

typedef struct GevrVertexTint {
    uint8_t rgba[4], unlit, reserved[3];
} GevrVertexTint;

/* Bind vertices stay in GPU storage. Each influence has its own local position
 * and normal because the Bean adapter fits them to the native character rig. */
typedef struct GevrSkinVertex {
    uint8_t vtx[16];
    float p[4][3], n[4][3];
    uint8_t weight[4], bone[4];
    GevrVertexTint tint;
} GevrSkinVertex;

/* DAB's 64 x 12 float affine palette, in anchor space (MIT attribution in
 * port/licenses/dabs-mod.txt). Invalid influences are omitted and renormalized. */
typedef struct GevrSkinPalette {
    float rows[GEVR_SKIN_BONES][12];
    uint32_t valid[2];
} GevrSkinPalette;

typedef struct GevrSkinDraw {
    GevrStaticMesh *mesh;
    GevrSkinPalette palette;
    const void *bindVertices;
    void *posedVertices;
    void *(*pose)(struct GevrSkinDraw *draw);
    int authoredColor;
    /* bullet wounds in anchor space: xyz, then 255 less the bruise's alpha
     * (gevr_bean_wounds.inc.h) */
    int numWounds;
    float wounds[GEVR_SKIN_WOUNDS][4];
} GevrSkinDraw;

#endif
