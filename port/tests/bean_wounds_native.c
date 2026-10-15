/* Production wound code and model rwdata lookup, supplied by the runner. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <ultra64.h>
#include <bondtypes.h>
#include <bondconstants.h>
#include "gevr_skin_mesh.h"

struct beanSkinVert {
    float weight[4], p[4][3];
    int matrix[4];
};
struct beanSkin {
    int head, anchor, nverts;
    struct beanSkinVert *verts;
};
struct beanSwap { struct beanSkin *skin; };
static struct beanSwap *activeSwap(void *node) { (void)node; return NULL; }
static int nodeOp(ModelNode *node) { return node->Opcode & 255; }
#define GEVR_BEAN_LOG(...) ((void)0)

/* PRODUCTION MODEL LOOKUP */
#include "wounds.inc.h"

static void modelInitRwData(Model *model, ModelNode *root)
{
    (void)root;
    memset(model->datas, 0, 128 * sizeof(u32));
}
/* PRODUCTION MODEL INIT */

int main(int argc, char **argv)
{
    const int nullHead = argc > 1 && !strcmp(argv[1], "null-head");
    _Alignas(8) u32 data[2][128] = {{0}};
    Model models[2] = {0};
    ModelFileHeader bodies[2] = {0}, head = {0};
    ModelNode roots[2] = {0}, sockets[2] = {0}, headroot = {0}, stain = {0};
    union ModelRoData rootdata[2] = {0}, socketdata[2] = {0}, staindata = {0};
    RenderPosView matrices[2] = {0};
    Vertex copies[2][1] = {0};
    const float inverse[9] = {1,0,0,0,1,0,0,0,1}, origin[4] = {0};
    float out[GEVR_SKIN_WOUNDS][4];
    coord3d point = {.x=10, .y=20, .z=30};
    struct beanSkin body = {0}, face = {.head=1};

    head.RootNode = &headroot;
    headroot.Opcode = MODELNODE_OPCODE_GROUP;
    headroot.Child = &stain;
    stain.Opcode = MODELNODE_OPCODE_DLCOLLISION;
    stain.Parent = &headroot;
    stain.Data = &staindata;
    staindata.DisplayListCollisions.RwDataIndex = nullHead ? 2 : 6;
    for (int i=0; i<2; i++) {
        roots[i].Opcode = MODELNODE_OPCODE_HEADER;
        roots[i].Data = &rootdata[i];
        roots[i].Child = &sockets[i];
        sockets[i].Opcode = MODELNODE_OPCODE_HEAD;
        sockets[i].Data = &socketdata[i];
        sockets[i].Parent = &roots[i];
        socketdata[i].HeadPlaceholder.RwDataIndex = 8 + 8*i;
        bodies[i].RootNode = &roots[i];
        bodies[i].numMatrices = 1;
        modelInit(&models[i], &bodies[i], data[i]);
        models[i].render_pos = matrices;
        ModelRwData_HeadPlaceholderRecord *rw = (void *)&data[i][8+8*i];
        rw->ModelFileHeader = &head;
        rw->RwDatas = &data[i][32];
    }
    for (int k=0; k<4; k++) matrices[0].pos.m[k][k]=1;
    /* The other body's socket index interpreted in model 0's data produces
     * 53f092b3's 0x05007818 + 6*4, or 71b515e8's NULL + 2*4. */
    ((ModelRwData_HeadPlaceholderRecord *)(void *)&data[0][16])->RwDatas =
        nullHead ? NULL : (void *)0x05007818;
    for (int i=0; i<2; i++) {
        modelApplyHeadRelations(&models[i], &sockets[i]);
        union ModelRwData *rw = modelGetNodeRwData(&models[i], &stain);
        rw->DisplayListCollisions.Vertices = copies[i];
        gevrBeanNoteWound(&models[i], 8, &roots[i], &stain, copies[i], &point, 140+i);
    }
    assert(headroot.Parent == &sockets[1]);
    for (int i=0; i<2; i++) {
        assert(beanWoundsInAnchor(&models[i], &body, inverse, origin, out) == 1);
        assert(out[0][0]==10 && out[0][1]==20 && out[0][2]==30);
        assert(out[0][3] == 255-(140+i)+256*BEAN_WOUND_RADIUS);
        assert(beanWoundsInAnchor(&models[i], &face, inverse, origin, out) == 1);
        assert(out[0][3] == 255-(140+i)+256*BEAN_WOUND_RADIUS_HEAD);
    }
    /* A body-only bruise never appears on the face. */
    modelApplyHeadRelations(&models[0], &sockets[0]);
    gevrBeanNoteWound(&models[0], 7, &roots[0], &stain, copies[0], &point, 50);
    assert(beanWoundsInAnchor(&models[0], &body, inverse, origin, out) == 2);
    assert(beanWoundsInAnchor(&models[0], &face, inverse, origin, out) == 1);
    /* Returning the stock vertex copy ends its wounds, independently. */
    modelGetNodeRwData(&models[0], &stain)->DisplayListCollisions.Vertices = NULL;
    assert(beanWoundsInAnchor(&models[0], &body, inverse, origin, out) == 0);
    assert(beanWoundsInAnchor(&models[1], &body, inverse, origin, out) == 1);
#ifndef BASELINE
    /* The same instance, header, rwdata and vertex addresses can all be reused.
     * modelInit must clear the old hit before any runtime record is read. */
    modelInit(&models[1], &bodies[1], data[1]);
    memset(data[1], 0xff, sizeof(data[1]));
    assert(beanWoundsInAnchor(&models[1], &body, inverse, origin, out) == 0);
    gevrBeanForgetWounds(&models[0]);
    gevrBeanForgetWounds(&models[0]);
#endif
    printf("Bean wounds (%s): shared head parents, per-instance copies, head/body tint and slot reuse passed\n",
           nullHead ? "null head base" : "segmented head base");
}
