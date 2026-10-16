/* Real adapter, mesh surgery, room ingestion and Jolt; synthetic game data only. */
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include "gevr_ragdoll_internal.h"
#include "bg.h"
#include "stan.h"
#include "model.h"
#include "gevr_bean.h"
#include "gevr_ragdoll.c"

int VrRagdolls, VrDismemberment, VrBodiesStay;
s32 g_GlobalTimer,g_gevrExtraPass;
ModelSkeleton skeleton_guard;
ModelRenderData D_8002CCBC;
static struct player players[8];
struct player *g_playerPointers[8];
static int playerCount=1;
s32 getPlayerCount(void) { return playerCount; }
s32 getPlayerPointerIndex(PropRecord *prop) { return prop->chr->chrnum; }
static Mtxf view,world;
Mtxf *currentPlayerGetViewToWorldMtxf(void) { return &world; }
Mtxf *camGetWorldToScreenMtxf(void) { return &view; }
static int drawn;
void subdraw(ModelRenderData *data,Model *model) {
    assert(model->datas && model->obj);
    for(int j=0;j<model->obj->numMatrices;j++)
        for(int k=0;k<16;k++)assert(isfinite(((float *)&model->render_pos[j].pos)[k]));
    drawn++;
}
static void *frameMemory[4096]; static int nframe;
void *dynAllocate(s32 bytes) { assert(nframe<4096); return frameMemory[nframe++]=calloc(1,bytes); }
static void frameFree(void) { while(nframe)free(frameMemory[--nframe]); }
void bondviewTransformManyPosToViewMatrix(RenderPosView *p,s32 n) { memset(p,0,(size_t)n*sizeof(*p)); }
Gfx *bgScissorCurrentPlayerViewDefault(Gfx *g) { return g; }
void sysLogPrintf(s32 level,const char *fmt,...) { (void)level; (void)fmt; }
void gevrBeanForgetWounds(Model *model) { (void)model; }
void gevrBeanSetCuts(const GevrBeanCuts *cuts) { (void)cuts; }
void subcalcmatrices(ModelRenderData *pose,Model *model) { (void)pose; (void)model; assert(!"fixture must supply a pose"); }
void sub_GAME_7F073FC8(s32 alpha) { (void)alpha; }
void modelApplyDistanceRelations(Model *m,ModelNode *n) { (void)m;(void)n; }
void modelApplyToggleRelations(Model *m,ModelNode *n) { (void)m;(void)n; }
void modelApplyReorderRelations(Model *m,ModelNode *n) { (void)m;(void)n; }
/* PRODUCTION_LOOKUPS */

StandTile *firststaninroom[139];
float inv_level_scale=1;
s_room_info g_BgRoomInfo[MAXROOMCOUNT];
s32 g_MaxNumRooms=3;
static bg_room_data roomData[MAXROOMCOUNT];
bg_room_data *ptr_bgdata_room_fileposition_list=roomData;
bg_portal_data_entry *g_BgPortals;
float get_room_data_float1(void) { return 0.2f; }
void bgFindRoomsAlongSegment(coord3d *a,coord3d *b,u8 *initial,u8 *final,s32 *crossed,s32 *count,s32 cap)
{ (void)a;(void)b;(void)cap; memcpy(final,initial,8);crossed[0]=initial[0];*count=1; }
static void *floorStorage[2];
static void floors(void) {
    for(int room=1;room<=2;room++) {
        StandTile *tile=floorStorage[room-1]=calloc(1,8+4*8+8);
        tile->idhi=1;tile->room=room;tile->tail.half=0x4000;
        const int xz[4][2]={{-5000,-5000},{5000,-5000},{5000,5000},{-5000,5000}};
        for(int i=0;i<4;i++){tile->points[i].x=xz[i][0];tile->points[i].z=xz[i][1];tile->points[i].y=room==1?0:50;}
        firststaninroom[room]=tile;
    }
}

static void collisionChecks(void) {
    float from[3]={0,200,0},to[3]={0,-200,0},hit[3];
    assert(pdphysRaycast(from,to,hit,NULL,NULL));assert(fabsf(hit[1])<0.1f); /* excludes unrelated floor at 50 */
    assert(pdphysRaycastAnyRoom(from,to,hit,NULL,NULL));assert(fabsf(hit[1]-50)<0.1f);
    static Vtx vertices[8];static Gfx commands[6];
    const int xyz[8][3]={{40,0,-40},{40,40,-40},{40,40,40},{40,0,40},
        {-1000,0,-1000},{1000,0,-1000},{1000,0,1000},{-1000,0,1000}};
    for(int i=0;i<8;i++)for(int k=0;k<3;k++)vertices[i].v.ob[k]=xyz[i][k];
    Gfx *g=commands;gSPVertex(g++, (void *)0x04000000u,8,0);
    gSP1Triangle(g++,0,1,2,0);gSP1Triangle(g++,0,2,3,0);
    gSP1Triangle(g++,4,5,6,0);gSP1Triangle(g++,4,6,7,0);gSPEndDisplayList(g++);
    roomData[1].pos.x=10; /* room transform and 0.2 scale put the wall at x=250 */
    g_BgRoomInfo[1].vertices=vertices;g_BgRoomInfo[1].usize_point_index_binary=sizeof(vertices);
    g_BgRoomInfo[1].ptr_expanded_mapping_info=commands;g_BgRoomInfo[1].usize_primary_DL_binary=sizeof(commands);
    g_BgRoomInfo[1].model_bin_loaded=1;rdWorldUpdate();
    from[0]=100;from[1]=100;to[0]=400;to[1]=100;
    assert(pdphysRaycast(from,to,hit,NULL,NULL));assert(fabsf(hit[0]-250)<0.1f);
    g_BgRoomInfo[1].vertices=NULL;g_BgRoomInfo[1].model_bin_loaded=0;
    assert(pdphysRaycast(from,to,hit,NULL,NULL));assert(fabsf(hit[0]-250)<0.1f); /* copied, not borrowed */
}

static ModelNode headerNode,groups[15],dlNodes[15],headNode;
static union ModelRoData headerRo,groupRo[15],dlRo[15],headRo;
static ModelFileHeader modelHeader,headHeader;
static Gfx lists[15][4]; static Vertex vertices[15][3];
static RenderPosView sourceMatrices[16];
static Model sourceModel; static PropRecord sourceProp; static ChrRecord sourceChr;
static const int parent[15]={-1,0,1,1,1,0,0,3,4,5,6,7,8,9,10};
static const float positions[15][3]={{0,100,0},{0,130,0},{0,155,0},{-20,145,0},{20,145,0},
 {-10,95,0},{10,95,0},{-40,130,0},{40,130,0},{-10,55,0},{10,55,0},
 {-55,115,0},{55,115,0},{-10,10,0},{10,10,0}};

static Mtxf identity(void) { Mtxf m={0};for(int i=0;i<4;i++)m.m[i][i]=1;return m; }
static void append(ModelNode *p,ModelNode *c) {
    c->Parent=p;
    ModelNode **last=&p->Child;while(*last)last=&(*last)->Next;*last=c;
}
static void sourceInit(void)
{
    memset(&sourceModel,0,sizeof(sourceModel));
    memset(&sourceChr,0,sizeof(sourceChr));
    memset(&sourceProp,0,sizeof(sourceProp));
    memset(&headerNode,0,sizeof(headerNode)); memset(groups,0,sizeof(groups)); memset(dlNodes,0,sizeof(dlNodes));
    sourceModel.obj=&modelHeader; sourceModel.datas=calloc(512,4); sourceModel.rwdatalen=512;
    sourceModel.scale=1; sourceModel.render_pos=sourceMatrices;
    modelHeader.RootNode=&headerNode; modelHeader.Skeleton=&skeleton_guard; modelHeader.numMatrices=16;
    headerNode.Opcode=MODELNODE_OPCODE_HEADER; headerNode.Data=&headerRo;
    headerRo.Header.RwDataIndex=0;headerRo.Header.MatrixIndex=0;
    sourceChr.prop=&sourceProp; sourceChr.model=&sourceModel; sourceChr.chrnum=0; sourceChr.shadecol.word=0xffffffff;
    sourceProp.chr=&sourceChr; sourceProp.type=PROP_TYPE_CHR; sourceProp.pos.y=100;
    memset(sourceProp.rooms,255,sizeof(sourceProp.rooms));sourceProp.rooms[0]=1;
    sourceMatrices[0].pos=identity();
    for(int i=0;i<15;i++) {
        groups[i].Opcode=MODELNODE_OPCODE_GROUP;groups[i].Data=&groupRo[i];
        groupRo[i].Group.JointID=i+1;
        for(int j=0;j<3;j++)groupRo[i].Group.MatrixIDs[j]=i+1;
        append(parent[i]<0?&headerNode:&groups[parent[i]],&groups[i]);
        dlNodes[i].Opcode=MODELNODE_OPCODE_DLCOLLISION;dlNodes[i].Data=&dlRo[i];append(&groups[i],&dlNodes[i]);
        sourceMatrices[i+1].pos=identity();memcpy(sourceMatrices[i+1].pos.m[3],positions[i],3*sizeof(float));
        dlRo[i].DisplayListCollisions.RwDataIndex=80+i*4;
        dlRo[i].DisplayListCollisions.numVertices=3;
        dlRo[i].DisplayListCollisions.Vertices=vertices[i];
        dlRo[i].DisplayListCollisions.Primary=lists[i];
        ModelRwData_DisplayList_CollisionRecord *rw=&modelGetNodeRwData(&sourceModel,&dlNodes[i])->DisplayListCollisions;
        rw->Vertices=vertices[i];rw->gdl=lists[i];
        memset(vertices[i],0,sizeof(vertices[i]));vertices[i][1].coord.x=3;vertices[i][2].coord.y=3;
        Gfx *g=lists[i];
        gSPMatrix(g++, (void *)(uintptr_t)(0x03000000u+(i+1)*sizeof(Mtx)), G_MTX_MODELVIEW|G_MTX_LOAD);
        gSPVertex(g++, (void *)0x04000000u, 3, 0);
        gSP1Triangle(g++,0,1,2,0);gSPEndDisplayList(g++);
    }
    /* Attached head runtime data points inside the original model allocation. */
    memset(&headNode,0,sizeof(headNode));headNode.Opcode=MODELNODE_OPCODE_HEAD;headNode.Data=&headRo;
    headRo.HeadPlaceholder.RwDataIndex=64;append(&groups[2],&headNode);
    ModelRwData_HeadPlaceholderRecord *head=&modelGetNodeRwData(&sourceModel,&headNode)->HeadPlaceholder;
    headHeader.RootNode=NULL;headHeader.numRecords=4;head->ModelFileHeader=&headHeader;head->RwDatas=sourceModel.datas+400;
}
static int countBodies(void) { int n=0;for(int i=0;i<RD_BODIES;i++)n+=s_bodies[i].used;return n; }
static RdBody *newest(void) { RdBody *b=NULL;for(int i=0;i<RD_BODIES;i++)if(s_bodies[i].used && (!b || b->serial<s_bodies[i].serial))b=&s_bodies[i];return b; }
static void capture(int viewer)
{
    static int sequence;
    int x=(sequence%8)*250,z=(sequence/8%8)*250;sequence++;
    for(int i=0;i<15;i++) { sourceMatrices[i+1].pos=identity();memcpy(sourceMatrices[i+1].pos.m[3],positions[i],3*sizeof(float));sourceMatrices[i+1].pos.m[3][0]+=x;sourceMatrices[i+1].pos.m[3][2]+=z; }
    sourceProp.pos.x=x;sourceProp.pos.z=z;
    sourceChr.actiontype=ACT_STAND;players[0].bonddead=0;
    sourceProp.type=viewer?PROP_TYPE_VIEWER:PROP_TYPE_CHR;
    gevrRagdollNeedsPose(&sourceChr);
    sourceChr.actiontype=viewer?ACT_BONDMULTI:ACT_DIE;players[0].bonddead=viewer;
    gevrRagdollCapture(&sourceChr);
    assert(gevrRagdollSuppress(&sourceChr));
}

int main(void)
{
    view=world=identity();for(int i=0;i<8;i++)g_playerPointers[i]=&players[i];
    sourceInit();floors();
    assert(!gevrRagdollNeedsPose(&sourceChr));assert(!pdphysIsReady());
    VrRagdolls=1;VrBodiesStay=48;
    for(int mode=1;mode<=8;mode*=2) {
        playerCount=mode;
        for(int i=0;i<50;i++) { capture(mode>1);assert(countBodies()==(i<48?i+1:48)); }
        for(int tick=0;tick<120;tick++) { g_GlobalTimer++;gevrRagdollTick(1); }
        for(int i=0;i<RD_BODIES;i++)for(int part=0;part<s_bodies[i].count;part++) {
            float p[3];pdphysRagdollGetPartPos(s_bodies[i].handle,part,p);
            assert(isfinite(p[0]) && isfinite(p[1]) && isfinite(p[2]) && p[1]>-2 && p[1]<300);
        }
        RdBody *b=newest();unsigned serial=b->serial;
        gevrRagdollCapture(&sourceChr);assert(newest()->serial==serial); /* extra view cannot duplicate a death */
        ModelRwData_HeadPlaceholderRecord *h=&modelGetNodeRwData(&b->model,&headNode)->HeadPlaceholder;
        assert(h->RwDatas==b->model.datas+400 && h->RwDatas!=sourceModel.datas+400);
        assert(b->meshes[0].rw->Vertices!=vertices[0]);
        float age=b->age;Mtxf pose=b->joints[0];
        for(int i=0;i<10;i++)gevrRagdollTick(0);
        assert(b->age==age && !memcmp(&b->joints[0],&pose,sizeof(pose)));
        gevrRagdollForgetChr(&sourceChr); /* network actor slot freed/reused */
        assert(countBodies()==48);
        VrBodiesStay=12;gevrRagdollTick(1);assert(countBodies()==12);
        VrBodiesStay=24;for(int i=0;i<20;i++)capture(mode>1);assert(countBodies()==24);
        VrBodiesStay=48;
        gevrRagdollReset();assert(countBodies()==0 && !pdphysIsReady());
    }
    playerCount=1;capture(0);
    RdBody *b=newest();
    collisionChecks();
    float rest,ms;int steps;
    assert(pdphysSelfTest(&rest,&steps,&ms));assert(rest>19 && rest<22);
    assert(!gevrRagdollModelCut(&b->model));
    assert(!rdCut(b,0)); /* pelvis stays joined */
    /* Make a shoulder's triangle reference its parent's matrix, as a native seam does. */
    RdMesh *mesh=&b->meshes[3];
    mesh->originalCommands[0].words.w1=0x03000000u+2*sizeof(Mtx);
    assert(rdCut(b,3));assert(!rdCut(b,3));
    assert((mesh->commands[0].words.w1&0xffffff)==4*sizeof(Mtx));
    float p[3]={3,4,5},normal[3]={0,1,0};
    assert(gevrRagdollRemap(&b->model,4,2,p,normal)==4);
    float expected[3]={3,4,5};rdTransform(&b->bind[2],expected,1);
    rdTransform(&b->bind[4],p,1);
    for(int k=0;k<3;k++)assert(fabsf(p[k]-expected[k])<1e-4f);
    Gfx *g=calloc(16384,sizeof(Gfx));drawn=0;
    gevrRagdollRender(g);assert(drawn==1);frameFree();
    view.m[3][0]=200;gevrRagdollRender(g);frameFree();free(g);view=identity();
    VrBodiesStay=0;
    for(int i=0;i<340;i++){g_GlobalTimer++;gevrRagdollTick(1);}
    assert(!countBodies());
    capture(0);VrRagdolls=0;gevrRagdollTick(1);
    assert(!pdphysIsReady() && !gevrRagdollSuppress(&sourceChr));
    for(int i=0;i<3;i++)gevrRagdollTick(0);
    assert(!s_garbage);free(sourceModel.datas);for(int i=0;i<2;i++)free(floorStorage[i]);frameFree();
    puts("PASS: real Jolt; 1/2/4/8-player deaths; 12/24/48 bodies; pause, duplicate views, actor reuse, private heads/vertices, cuts, affine remap, draw, fade and teardown");
    return 0;
}
