/* Compile the actual emitter and registry, with authored geometry and a
 * stub decoder. The runner supplies empty local marker files. */
#include "../src/gevr_bean.c"
#include <assert.h>

int VrBeanModels = 1;
uintptr_t osVirtualToPhysical(void *addr) { return (uintptr_t)addr; }
static int loads;
int gevrBeanLoad(const char *source, int parts, struct gevrBeanMesh **out)
{
    (void)source; (void)parts; loads++;
    struct gevrBeanMesh *m = calloc(1, sizeof(*m));
    m->ntris = 1; m->tris = calloc(1, sizeof(*m->tris));
    m->tris[0].p[3] = 10; m->tris[0].p[7] = 10;
    m->tris[0].tex = -1; m->tris[0].alpha = 255;
    memset(m->tris[0].rgba, 255, 12);
    *out = m;
    return 1;
}
void gevrBeanMeshFree(struct gevrBeanMesh *m) { free(m->tris); free(m); }
void gevrBeanTexReset(void) {}
void gevrBeanWorldReset(void) {}
void *gevrBeanWorldRoom(void *g, int r, int *replaced) { (void)r; *replaced = 0; return g; }
void *gevrBeanWorldSky(void *g, int *drew) { *drew = 0; return g; }

int main(void)
{
    struct gevrBeanMesh mesh = {0};
    uint16_t pixels[256] = {0};
    mesh.ntex = 1; mesh.standin[0] = pixels; mesh.texW[0] = 128; mesh.texH[0] = 64;
    struct gevrEmitVert verts[3] = {0};
    for (int i = 0; i < 3; i++) { verts[i].tex = 0; verts[i].alpha = 255; verts[i].rgba[3] = 128; }
    verts[0].uv[0] = 2; verts[1].uv[0] = 3; verts[2].uv[0] = 2;
    verts[0].uv[1] = 0; verts[1].uv[1] = 0; verts[2].uv[1] = 1;
    void *gdl = NULL, *raw = NULL;
    assert(gevrBeanEmitList(&mesh, verts, 3, 0, 0, 1, 1, &gdl, &raw));
    Gfx *commands = gdl;
    assert((commands[1].words.w0 >> 24) == (G_SETOTHERMODE_H & 0xff));
    assert(commands[1].words.w1 == G_CYC_1CYCLE);
    Vtx *v = raw;
    assert(v[0].v.tc[0] == 0 && v[1].v.tc[0] == 128 * 32);
    assert(v[2].v.tc[1] == -64 * 32 && v[0].v.cn[3] == 128);
    free(gdl); free(raw);
    /* A seam keeps its span, including negative V. */
    verts[0].uv[0] = 0.9f; verts[1].uv[0] = 1.1f;
    assert(gevrBeanEmitList(&mesh, verts, 3, 0, 0, 1, 1, &gdl, &raw));
    v = raw;
    assert(v[1].v.tc[0] > v[0].v.tc[0]);
    free(gdl); free(raw);
    /* A long repeating face uses smaller logical units, without saturation. */
    verts[0].uv[0] = 0; verts[1].uv[0] = 100;
    assert(gevrBeanEmitList(&mesh, verts, 3, 0, 0, 1, 1, &gdl, &raw));
    v = raw;
    assert(v[1].v.tc[0] == 100 * 8 * 32);
    free(gdl); free(raw);

    Vertex host[3] = {0}; host[1].coord.x = 10; host[2].coord.y = 10;
    union ModelRoData ro = {0};
    ro.DisplayList.Vertices = host; ro.DisplayList.numVertices = 3; ro.DisplayList.ModelType = 3;
    ModelNode node = {0}; node.Opcode = MODELNODE_OPCODE_DL; node.Data = &ro;
    ModelFileHeader header = {0}; header.RootNode = &node;
    gevrBeanInit(); gevrBeanSetEnabled(0);
    gevrBeanModelApply(&header, "Gak47Z");
    assert(loads == 1 && ro.DisplayList.Primary);
    assert(gevrBeanShownGdl(&node, ro.DisplayList.Primary) == NULL);
    assert(gevrBeanShownModelType(&node, 0) == 3);
    gevrBeanSetEnabled(1);
    assert(gevrBeanShownGdl(&node, ro.DisplayList.Primary) == ro.DisplayList.Primary);
    assert(gevrBeanShownModelType(&node, ro.DisplayList.ModelType) == 0);
    gevrBeanReset();
    puts("Bean UV, vertex alpha and same-level toggle checks passed");
    return 0;
}
