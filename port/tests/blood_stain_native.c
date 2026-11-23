#include <assert.h>
#include <stddef.h>
#include <stdint.h>

typedef int32_t s32;
typedef int16_t s16;
typedef uint16_t u16;
typedef uint8_t u8;

typedef struct Vertex {
    s16 x, y, z;
    s16 index;
    s16 s, t;
    u8 r, g, b, a;
} Vertex;

typedef struct ModelRoData_DisplayList_CollisionRecord {
    void *Primary;
    void *Secondary;
    Vertex *Vertices;
    s16 numVertices;
    s16 numCollisionVertices;
    Vertex *CollisionVertices;
    s16 *PointUsage;
    s16 ModelType;
    u16 RwDataIndex;
    void *BaseAddr;
} ModelRoData_DisplayList_CollisionRecord;

typedef struct ModelRwData_DisplayList_CollisionRecord {
    Vertex *Vertices;
    void *gdl;
} ModelRwData_DisplayList_CollisionRecord;

/* INSERT_BLOOD_STAIN */

static void reset_verts(Vertex *v, int n)
{
    for (int i = 0; i < n; i++) {
        v[i].a = 255;
    }
}

int main(void)
{
    Vertex ro_verts[4] = {0};
    Vertex rw_verts[4] = {0};
    Vertex col_verts[2] = {0};
    s16 usage[4];

    ModelRoData_DisplayList_CollisionRecord ro = {0};
    ModelRwData_DisplayList_CollisionRecord rw = {0};
    ro.Vertices = ro_verts;
    ro.numVertices = 4;
    ro.numCollisionVertices = 2;
    ro.CollisionVertices = col_verts;
    ro.PointUsage = usage;
    rw.Vertices = rw_verts;

    /* 1. Normal -1-terminated chain: 1 -> 3 -> -1 */
    reset_verts(rw_verts, 4);
    col_verts[0].index = 1;
    usage[0] = -1;
    usage[1] = 3;
    usage[2] = -1;
    usage[3] = -1;
    chrPaintBloodStainVertices(&ro, &rw, 0, 143);
    assert(rw_verts[0].a == 255);
    assert(rw_verts[1].a == 143);
    assert(rw_verts[2].a == 255);
    assert(rw_verts[3].a == 143);

    /* 2. Zero-filled PointUsage (ROM hack table): must not hang */
    reset_verts(rw_verts, 4);
    col_verts[0].index = 0;
    usage[0] = 0;
    usage[1] = 0;
    usage[2] = 0;
    usage[3] = 0;
    chrPaintBloodStainVertices(&ro, &rw, 0, 143);
    assert(rw_verts[0].a == 143);
    assert(rw_verts[1].a == 255);

    /* 3. Two-node cycle (report #41): 0 -> 1 -> 0 must terminate */
    reset_verts(rw_verts, 4);
    col_verts[0].index = 0;
    usage[0] = 1;
    usage[1] = 0;
    usage[2] = -1;
    usage[3] = -1;
    chrPaintBloodStainVertices(&ro, &rw, 0, 143);
    assert(rw_verts[0].a == 143);
    assert(rw_verts[1].a == 143);
    assert(rw_verts[2].a == 255);

    /* 4. Out-of-bounds indices & null/uncloned guards */
    reset_verts(rw_verts, 4);
    col_verts[1].index = 99;
    chrPaintBloodStainVertices(&ro, &rw, 1, 143);
    chrPaintBloodStainVertices(&ro, &rw, -1, 143);
    chrPaintBloodStainVertices(&ro, &rw, 2, 143);
    col_verts[0].index = 2;
    usage[2] = 99;
    chrPaintBloodStainVertices(&ro, &rw, 0, 143);
    assert(rw_verts[2].a == 143);

    ro.PointUsage = NULL;
    chrPaintBloodStainVertices(&ro, &rw, 0, 50);
    assert(rw_verts[2].a == 143);

    return 0;
}
