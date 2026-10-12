/* Compile the actual emitter and registry, with authored geometry and a
 * stub decoder. The runner supplies empty local marker files. */
#include "../src/gevr_bean.c"
int gevrRomIsVariant(void) { return 0; } /* GoldenEye: no hack campaign */
#include <assert.h>

void sysLogPrintf(s32 level, const char *fmt, ...) { (void)level; (void)fmt; }

void gfx_static_mesh_release(GevrStaticMesh *mesh) { (void)mesh; }
int VrBeanModels = 1;
int VrGexGuns;
uintptr_t osVirtualToPhysical(void *addr) { return (uintptr_t)addr; }
Vtx *dynAllocateVertices(s32 n) { return calloc(n, sizeof(Vtx)); }
void *dynAllocate(s32 n) { return calloc(1, n); }
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
/* the folder repaint (gevr_bean_folder.c, gevr_bean_tex.c) is tested on its own */
void gevrBeanRepaintForget(void) {}
void gevrBeanRepaintForgetOwner(int owner) { (void)owner; }
int gevrBeanFolderRepaint(struct ModelFileHeader *h, const struct gevrCaff *c) { (void)h; (void)c; return 0; }
int gevrCaffOpen(struct gevrCaff *c, const uint8_t *d, uint32_t len) { (void)c; (void)d; (void)len; return 0; }
void gevrCaffClose(struct gevrCaff *c) { (void)c; }
void gevrBeanFontReset(void) {}
void gevrBeanWorldReset(void) {}
void *gevrBeanWorldRoom(void *g, int r, int *replaced) { (void)r; *replaced = 0; return g; }
void *gevrBeanWorldSky(void *g, int *drew) { *drew = 0; return g; }

#include "gevr_bean_pose_test.inc.h"
#include <gevr_bean_hit_test.inc.h>

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
    assert(loads == 1 && !ro.DisplayList.Primary && ro.DisplayList.ModelType == 3);
    assert(gevrBeanShownGdl(&node, ro.DisplayList.Primary) == NULL);
    assert(gevrBeanShownModelType(&node, ro.DisplayList.ModelType) == 3);
    gevrBeanSetEnabled(1);
    assert(gevrBeanShownGdl(&node, ro.DisplayList.Primary) != NULL);
    assert(gevrBeanShownModelType(&node, ro.DisplayList.ModelType) == 0);
    /* Reloading an unsupported model at the same header/node must clear ours. */
    gevrBeanModelApply(&header, "NoBeanReplacementZ");
    assert(gevrBeanShownGdl(&node, ro.DisplayList.Primary) == NULL);
    assert(gevrBeanShownModelType(&node, ro.DisplayList.ModelType) == 3);
    gevrBeanModelApply(&header, "Gak47Z");
    assert(gevrBeanShownGdl(&node, ro.DisplayList.Primary) != NULL);
    gevrBeanForgetModelStorage(&node, sizeof(node)); /* A different header reuses intro storage. */
    assert(gevrBeanShownGdl(&node, ro.DisplayList.Primary) == NULL);
    gevrBeanModelApply(&header, "Gak47Z");
    gevrBeanForgetModel(&header); /* The GE-X load path also forgets. */
    assert(gevrBeanShownGdl(&node, ro.DisplayList.Primary) == NULL);
    /* Both static and skinned replacements honour GE-X's temporary hide. */
    Gfx *nativeList = (void *)(uintptr_t)0x05000010;
    ro.DisplayList.Primary = nativeList;
    gevrBeanForgetModel(&header);
    gevrBeanModelApply(&header, "Gak47Z");
    Model model = {0}; model.obj = &header;
    assert(gevrBeanShownGdl(&node, nativeList) != nativeList);
    assert(gevrBeanShownGdl(&node, NULL) == NULL);
    assert(gevrBeanRenderGdl(&node, NULL, &model) == NULL);
    assert(gevrBeanRenderGdl(&node, nativeList, &model) != NULL);
    swapOf(&node)->watchArm = 1;
    VrGexGuns = 1;
    assert(gevrBeanShownGdl(&node, nativeList) == nativeList);
    assert(gevrBeanShownModelType(&node, 3) == 3);
    assert(gevrBeanRenderGdl(&node, nativeList, &model) == nativeList);
    VrGexGuns = 0;
    assert(gevrBeanShownGdl(&node, nativeList) != nativeList);
    /* Native GoldenEye keeps both root nodes that DAB's converter folds
     * together. The duplicate pelvis must not steal the torso's BACK slot. */
    union ModelRoData rigData[4] = {0};
    ModelNode rigNodes[4] = {0};
    struct beanGrp rigGroups[4] = {0};
    for (int i = 0; i < 4; i++) {
        rigNodes[i].Data = &rigData[i];
        rigNodes[i].Opcode = i ? MODELNODE_OPCODE_GROUP : MODELNODE_OPCODE_HEADER;
        rigGroups[i].node = &rigNodes[i];
    }
    rigData[0].Header.MatrixIndex = 4; rigData[1].Group.MatrixIDs[0] = 5;
    rigGroups[1].joint = 1;
    rigData[2].Group.MatrixIDs[0] = 1; rigGroups[2].o[1] = 62;
    rigData[3].Group.MatrixIDs[0] = 0; rigGroups[3].o[1] = 534;
    classifyGroups(rigGroups, 4);
    assert(rigGroups[0].sk == GEVR_BEAN_SK_BASE && rigGroups[1].sk == GEVR_BEAN_SK_BASE);
    assert(rigGroups[2].sk == GEVR_BEAN_SK_BACK && rigGroups[3].sk == GEVR_BEAN_SK_NECK);
    hitChecks();
    poseChecks();
    gevrBeanReset();
    puts("Bean UV, vertex alpha and same-level toggle checks passed");
    return 0;
}
