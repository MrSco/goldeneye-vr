/* Real adapter, mesh surgery, room ingestion and Jolt; synthetic game data only. */
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include "gevr_ragdoll_internal.h"
#include "bg.h"
#include "stan.h"
#include "model.h"
#include "gevr_bean.h"
#include "propobj.h"
#include "matrixmath.h"
#include "chrai.h"
#include "gevr_ragdoll.c"

int VrRagdolls, VrDismemberment, VrBodiesStay;
s32 lvlGetCurrentStageToLoad(void) { return LEVELID_DAM; }
static float weaponDamage=1;
f32 gunItemGetDestructionAmount(ITEM_IDS item) { (void)item; return weaponDamage; }
f32 bondwalkItemGetForceOfImpact(ITEM_IDS item) { (void)item; return 1; }
static int roomsShown=1;
u8 getROOMID_isRendered(int room) { (void)room; return roomsShown; }
bool camIsPosInScreen(coord3d *pos,f32 margin) { (void)pos;(void)margin; return 1; }
s32 fogPositionIsVisibleThroughFog(coord3d *pos,f32 range) { (void)pos;(void)range; return 1; }
f32 getinstsize(Model *model) { (void)model; return 100; }
s32 g_GlobalTimer,g_gevrExtraPass;
stagesetup g_CurrentSetup;
static PropRecord *activeProps;
PropRecord *chrpropGetActiveTail(void) { return activeProps; }
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
        {-1000,6,-1000},{1000,6,-1000},{1000,6,1000},{-1000,6,1000}};
    for(int i=0;i<8;i++)for(int k=0;k<3;k++)vertices[i].v.ob[k]=xyz[i][k];
    Gfx *g=commands;gSPVertex(g++, (void *)0x04000000u,8,0);
    gSP1Triangle(g++,0,1,2,0);gSP1Triangle(g++,0,2,3,0);
    gSP1Triangle(g++,4,6,5,0);gSP1Triangle(g++,4,7,6,0);gSPEndDisplayList(g++);
    roomData[1].pos.x=10; /* room transform and 0.2 scale put the wall at x=250 */
    g_BgRoomInfo[1].vertices=vertices;g_BgRoomInfo[1].usize_point_index_binary=sizeof(vertices);
    g_BgRoomInfo[1].ptr_expanded_mapping_info=commands;g_BgRoomInfo[1].usize_primary_DL_binary=sizeof(commands);
    g_BgRoomInfo[1].model_bin_loaded=1;rdWorldUpdate();
    from[0]=100;from[1]=100;to[0]=400;to[1]=100;
    assert(pdphysRaycast(from,to,hit,NULL,NULL));assert(fabsf(hit[0]-250)<0.1f);
    float down[3]={100,200,100},below[3]={100,-200,100};
    assert(pdphysRaycast(down,below,hit,NULL,NULL));assert(fabsf(hit[1])<0.1f); /* native floor kept, render floor left out */
    g_BgRoomInfo[1].vertices=NULL;g_BgRoomInfo[1].model_bin_loaded=0;
    assert(pdphysRaycast(from,to,hit,NULL,NULL));assert(fabsf(hit[0]-250)<0.1f); /* copied, not borrowed */
}

static ModelNode headerNode,groups[15],dlNodes[15],headNode;
static union ModelRoData headerRo,groupRo[15],dlRo[15],headRo;
static ModelFileHeader modelHeader,headHeader;
static Gfx lists[15][4]; static Vertex vertices[15][3];
static RenderPosView sourceMatrices[16];
static Model sourceModel; static PropRecord sourceProp; static ChrRecord sourceChr;
/* GoldenEye's guard joints 1-15 are CE's human parts 0-14 */
static const int parent[15]={-1,0,1,1,1,3,4,5,6,0,0,9,10,11,12};
static const float positions[15][3]={{0,100,0},{0,108,0},{0,150,0},{-20,145,0},{20,145,0},
 {-22,118,0},{22,118,0},{-24,92,0},{24,92,0},{-10,95,0},{10,95,0},
 {-10,55,0},{10,55,0},{-10,14,0},{10,14,0}};

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

static void capture_nocheck(void)
{
    sourceChr.actiontype=ACT_STAND;gevrRagdollNeedsPose(&sourceChr);
    sourceChr.actiontype=ACT_DIE;gevrRagdollCapture(&sourceChr);
}

/* The CE tuning: skeleton table, shapes, hinges, cut rules, culling, layout check */
static void portChecks(void)
{
    VrRagdolls=1;VrBodiesStay=48;VrDismemberment=1;playerCount=1;
    capture(0);
    RdBody *b=newest();
    assert(b->count==11);
    for(int part=0;part<RD_SKEL;part++)
        assert((b->body[part]>=0)==(g_RdSkelHuman.hasbody[part]!=0));
    assert(b->owner[8]==b->body[5] && b->owner[9]==b->body[6]);   /* hands ride the forearms */
    assert(b->owner[14]==b->body[11] && b->owner[15]==b->body[12]); /* feet ride the shins */
    assert(fabsf(b->torso-42)<0.01f);
    pdphysRagdollPart parts[RD_PARTS];const float zero[3]={0};
    rdBuildParts(b,parts,zero);
    const float sc=42/RD_MASS_REFTORSO;
    assert(fabsf(parts[b->body[9]].mass-11.4f*sc*sc*sc)<1e-3f);
    assert(parts[b->body[0]].joint==PDPHYS_JOINT_NONE && parts[b->body[0]].parent==-1);
    for(int j=5;j<=12;j++) if(j==5 || j==6 || j==11 || j==12) assert(parts[b->body[j]].joint==PDPHYS_JOINT_HINGE);
    assert(parts[b->body[3]].joint==PDPHYS_JOINT_SWINGTWIST && parts[b->body[9]].joint==PDPHYS_JOINT_SWINGTWIST);
    assert(parts[b->body[2]].shape==PDPHYS_SHAPE_SPHERE && fabsf(parts[b->body[2]].radius-42*0.26f)<1e-3f);
    assert(parts[b->body[11]].footradius>0 && parts[b->body[11]].footb[1]<parts[b->body[11]].foota[1]+1);
    /* A knee whose Y runs across the leg (PD's animated frames): CE's hinge
     * about local Y, bending one way only, ~140 degrees */
    Mtxf knee=b->skelInitial[11];
    memset(knee.m,0,sizeof(float)*12);knee.m[0][1]=-1;knee.m[1][0]=1;knee.m[2][2]=1;
    b->skelInitial[11]=knee;rdBuildParts(b,parts,zero);
    pdphysRagdollPart *shin=&parts[b->body[11]];
    assert(shin->joint==PDPHYS_JOINT_HINGE && shin->axis[0]==1 && shin->axis[1]==0);
    assert(fabsf(shin->limits[0]+0.05f)<1e-4f && fabsf(shin->limits[1]-2.45f)<1e-4f);
    /* Weapon damage: KF7-class shots never cut, magnum-class about 40% */
    int cuts=0;weaponDamage=1;
    for(int i=0;i<200;i++)cuts+=rdCutForHit(b,b->body[9],ITEM_AK47)>=0;
    assert(!cuts);
    weaponDamage=2;
    for(int i=0;i<1000;i++)cuts+=rdCutForHit(b,b->body[9],ITEM_RUGER)>=0;
    assert(cuts>300 && cuts<500);
    for(int i=0;i<200;i++){assert(rdCutForHit(b,b->body[9],ITEM_FIST)<0);assert(rdCutForHit(b,b->body[9],ITEM_SHOTGUN)<0);}
    for(int i=0;i<200;i++)assert(rdCutForHit(b,b->body[1],ITEM_RUGER)<0); /* the chest needs a split-the-waist hit */
    weaponDamage=100;cuts=0;
    for(int i=0;i<200;i++){int c=rdCutForHit(b,b->body[1],ITEM_GOLDENGUN);assert(c<0 || c==b->body[1]);cuts+=c>=0;}
    assert(cuts>0);
    VrDismemberment=0;for(int i=0;i<200;i++)assert(rdCutForHit(b,b->body[9],ITEM_GOLDENGUN)<0);VrDismemberment=1;
    weaponDamage=1;
    /* Shotgun volleys: all on one leg cuts at the hip nearest the body, two limbs never */
    const int leg[3]={11,9,11},limbs[3]={11,9,5},torso[2]={9,1};
    cuts=0;for(int i=0;i<500;i++){int c=rdVolleyCut(b,leg,3);assert(c<0 || c==b->body[9]);cuts+=c>=0;}
    assert(cuts>100 && cuts<300);
    for(int i=0;i<200;i++){assert(rdVolleyCut(b,limbs,3)<0);assert(rdVolleyCut(b,torso,2)<0);}
    /* Hitparts map through the model's BBOX nodes, hands to forearms */
    b->hitskel[9]=7;assert(rdSkelForHit(b,9)==5); /* HIT_LEFT_HAND */
    /* Cut limits: no hands or feet bodies, never the pelvis */
    assert(!rdCut(b,b->body[0]));
    /* Drawn only while one of its rooms is drawn */
    unsigned serialBefore=newest()->serial;
    Gfx *g=calloc(16384,sizeof(Gfx));
    roomsShown=0;gevrRagdollTick(0);drawn=0;gevrRagdollRender(g);assert(drawn==0);frameFree();
    roomsShown=1;gevrRagdollTick(0);drawn=0;gevrRagdollRender(g);assert(drawn>=1);frameFree();free(g);
    /* CE's blasts: a guard killed by one is thrown up and away; bodies
     * already down within reach lose up to two joints to it */
    sourceChr.actiontype=ACT_STAND;gevrRagdollNeedsPose(&sourceChr);
    coord3d centre={{0}};centre.x=sourceProp.pos.x+60;centre.y=60;centre.z=sourceProp.pos.z;
    gevrRagdollExplosionChr(&sourceChr,&centre,280,1);
    sourceChr.actiontype=ACT_DIE;gevrRagdollCapture(&sourceChr);
    RdBody *thrown=newest();
    assert(gevrRagdollSuppress(&sourceChr) && thrown->serial>serialBefore);
    float before[3],after[3];pdphysRagdollGetPartPos(thrown->handle,thrown->root,before);
    for(int i=0;i<6;i++){g_GlobalTimer++;gevrRagdollTick(1);}
    pdphysRagdollGetPartPos(thrown->handle,thrown->root,after);
    assert(after[1]>before[1]+5 && after[0]<before[0]); /* up, and away from the centre */
    int blastcuts=0;
    for(int n=0;n<20;n++) {
        capture(0);RdBody *body=newest();
        coord3d at;memcpy(at.f,body->joints[body->body[1]].m[3],sizeof(at.f));
        gevrRagdollBlast(&at,280,1);
        int pieces=0;for(int i=0;i<body->count;i++)pieces+=(body->cuts>>i)&1;
        assert(pieces<=2);
        for(int i=0;i<body->count;i++)if(body->cuts&(1u<<i))assert(g_RdSkelHuman.cutradius[body->skel[i]]>0);
        blastcuts+=pieces;
    }
    assert(blastcuts>0);
    VrDismemberment=0;capture(0);{RdBody *body=newest();coord3d at;memcpy(at.f,body->joints[0].m[3],sizeof(at.f));
        gevrRagdollBlast(&at,280,1);assert(!body->cuts);}VrDismemberment=1;
    /* Another rig layout keeps its death animation */
    groupRo[5].Group.JointID=0;
    unsigned serial=newest()->serial;
    (void)serialBefore;
    capture_nocheck();
    assert(newest()->serial==serial && !gevrRagdollSuppress(&sourceChr));
    groupRo[5].Group.JointID=6;
    gevrRagdollReset();
    puts("PASS: CE human skeleton (11 bodies, hands/feet riding), masses, hinges, damage/chance/shotgun/blast cut rules, blast throws, room culling, layout fallback");
}

#include "ragdoll_movers_test.inc.h"

int main(void)
{
    view=world=identity();for(int i=0;i<8;i++)g_playerPointers[i]=&players[i];
    sourceInit();floors();
    movingCollisionChecks();
    guardLiftChecks();
    portChecks();VrDismemberment=VrRagdolls=0;fflush(stdout);
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
