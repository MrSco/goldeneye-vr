/* Production avatar adapter + production joint builders, synthetic guard rig. */
#include <stdbool.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "gevr_avatar.h"
#include "gevr_bodyslot.h"
#include "net/net_core.h"
#include "model.h"
#include "player.h"
#include "matrixmath.h"
#include "objecthandler.h"
#include "quaternion.h"
#include "math_unk_05A9E0.h"
#include "system.h"

static Mtxf view,world;
static ModelNode nodes[10];static union ModelRoData ro[10];
static ModelRwData_HeaderRecord rootRw;
static GevrAvatarPose received;
static float weights[3]={1,1,1};
static int active=1,ready=1,tracked[2]={1,1};
static int focused=1,aiming;
static uint64_t now=1000000;
static coord3d eye;
static struct player remote;
struct player *g_playerPointers[MAX_PLAYER_COUNT];
int g_gevrStereo=1,VrLeftHandedMode;
s32 g_gevrExtraPass;
int gevrVrReady(void) {return ready;}
int gevrVrSessionFocused(void) {return focused;}
s32 gevrStereoShotWorld(s32 role,coord3d *origin,coord3d *dir) {(void)role;memset(origin,0,sizeof(*origin));dir->x=1;dir->y=dir->z=0;return aiming;}
int gevrVrGripTracked(int ctrl) {return tracked[ctrl^(VrLeftHandedMode!=0)];}
int gevrVrGripUsable(int ctrl) {return gevrVrGripTracked(ctrl);}
int gevrVrGripPoseCamera(int ctrl,float pos[3],float q[4]) {
    int physical=ctrl^(VrLeftHandedMode!=0);
    pos[0]=physical?.2f:-.2f;pos[1]=-.1f;pos[2]=-.3f;
    q[0]=q[1]=q[2]=0;q[3]=1;return 1;
}
coord3d *gevrEyePosition(void) {return &eye;}
static float baseYaw;
float gevrStereoBaseYaw(void) {return baseYaw;}
u64 sysGetMicroseconds(void) {return now;}
void sysLogPrintf(s32 level,const char *fmt,...) {(void)level;(void)fmt;}
Mtxf *camGetWorldToScreenMtxf(void) {return &view;}
Mtxf *currentPlayerGetViewToWorldMtxf(void) {return &world;}
s32 getPlayerPointerIndex(PropRecord *p) {(void)p;return 1;}
int netGetAvatarPose(int slot,GevrAvatarPose *p,float w[3]) {(void)slot;*p=received;memcpy(w,weights,sizeof(weights));return active;}
static void *allocations[50];static int allocated;
void *dynAllocate(s32 size) {assert(allocated<50);return allocations[allocated++]=calloc(1,size);}
static void clearFrame(void) {while(allocated)free(allocations[--allocated]);}
Mtxf *modelFindNodeMtx(Model *m,ModelNode *n,s32 which) {
    (void)which;return &m->render_pos[(n==nodes)?0:n->Data->Group.MatrixID0].pos;
}
union ModelRwData *modelGetNodeRwData(Model *m,ModelNode *n) {(void)m;(void)n;return (union ModelRwData *)&rootRw;}
void (*g_ModelJointPositionedFunc)(s32,Mtxf *);
coord3d D_80036094,D_800360A0,D_800360AC,D_800360B8;
void sub_GAME_7F06DEC0(s32 j,s32 flip,ModelSkeleton *s,ModelAnimation *a,uintptr_t bits,coord3d *rot)
{(void)j;(void)flip;(void)s;(void)a;(void)bits;memset(rot,0,sizeof(*rot));}
void sub_GAME_7F06D160(coord3d *a,coord3d *b,f32 t) {for(int k=0;k<3;k++)a->f[k]+=(b->f[k]-a->f[k])*t;}
/* PRODUCTION_BUILDERS */

void modelUpdateMatrices(ModelRenderData *data,Model *m) {
    process_01_group_heading(data,m,nodes);
    /* Guard parents precede children. The production decoder is used for all
     * GROUP nodes, including its avatar override and auxiliary bend matrices. */
    for(int j=1;j<=9;j++)process_02_position(data,m,&nodes[j]);
}
static void nearAt(float a,float b,int line) {if(!(isfinite(a)&&isfinite(b)&&fabsf(a-b)<.003f)) {fprintf(stderr,"rig line %d: %.6f != %.6f\n",line,a,b);abort();}}
#define near(a,b) nearAt(a,b,__LINE__)
static float length(const float *a,const float *b) {float d=0;for(int k=0;k<3;k++)d+=(a[k]-b[k])*(a[k]-b[k]);return sqrtf(d);}
static void fixture(Model *m,ModelFileHeader *header,ChrRecord *chr,PropRecord *prop,ModelRenderData *data,float scale) {
    memset(nodes,0,sizeof(nodes));memset(ro,0,sizeof(ro));memset(&rootRw,0,sizeof(rootRw));
    matrix_4x4_set_identity(&world);matrix_4x4_set_identity(&view);
    for(int i=0;i<3;i++) {view.m[i][i]=scale;world.m[i][i]=1/scale;}
    header->RootNode=nodes;header->numMatrices=30;m->obj=header;m->scale=1;m->anim=(ModelAnimation *)1;
    m->render_pos=dynAllocate(30*sizeof(*m->render_pos));
    nodes[0].Data=&ro[0];nodes[0].Opcode=MODELNODE_OPCODE_HEADER;
    ro[0].Header.MatrixIndex=0;
    int parent[10]={0,0,1,2,2,2,4,5,6,7};
    const float position[10][3]={{0},{0},{0,70,0},{0,25,0},{-20,15,0},{20,15,0},{-30,0,0},{30,0,0},{-25,0,0},{25,0,0}};
    for(int j=1;j<=9;j++) {
        nodes[j].Data=&ro[j];nodes[j].Opcode=MODELNODE_OPCODE_GROUP|0x300;nodes[j].Parent=&nodes[parent[j]];
        ro[j].Group.JointID=j;ro[j].Group.MatrixID0=j*3;ro[j].Group.MatrixID1=j*3+1;ro[j].Group.MatrixID2=j*3+2;
        memcpy(ro[j].Group.Origin.f,position[j],sizeof(position[j]));
        if(!nodes[parent[j]].Child)nodes[parent[j]].Child=&nodes[j];
        else {ModelNode *n=nodes[parent[j]].Child;while(n->Next)n=n->Next;n->Next=&nodes[j];}
    }
    prop->type=PROP_TYPE_VIEWER;prop->chr=chr;prop->pos.y=100;chr->prop=prop;chr->model=m;
    remote.field_488.collision_position=prop->pos;g_playerPointers[1]=&remote;
    data->basemtx=&view;modelUpdateMatrices(data,m);
    memset(&received,0,sizeof(received));received.flags=7;received.head.q[1]=1; /* neutral camera faces +Z */
    for(int role=0;role<2;role++) {received.hand[role].q[3]=1;received.hand[role].pos[0]=role?-35:35;received.hand[role].pos[1]=-10;received.hand[role].pos[2]=30;}
}
static void rig(float scale,int lefty,float yaw,float height) {
    Model m={0};ModelFileHeader header={0};ChrRecord chr={0};PropRecord prop={0};ModelRenderData data={0};
    fixture(&m,&header,&chr,&prop,&data,scale);
    for(int role=0;role<2;role++)received.hand[role].pos[1]=height;
    prop.pos.y-=70; /* native animated pelvis, below the movement eye */
    if(lefty) {
        received.flags|=GEVR_POSE_LEFT_HANDED;
        GevrTrackedPose tmp=received.hand[0];received.hand[0]=received.hand[1];received.hand[1]=tmp;
    }
    if(yaw!=0) {
        Mtxf turn;matrix_4x4_set_rotation_around_y(-yaw*3.14159265358979323846f/180,&turn);
        for(int j=0;j<3;j++) {
            GevrTrackedPose *p=j?&received.hand[j-1]:&received.head;
            float rot[3][3],turned[3][3],pos[3];gevrPoseQuatMatrix(p->q,rot);
            for(int i=0;i<3;i++)for(int k=0;k<3;k++)turned[i][k]=rot[i][0]*turn.m[0][k]+rot[i][1]*turn.m[1][k]+rot[i][2]*turn.m[2][k];
            for(int k=0;k<3;k++)pos[k]=p->pos[0]*turn.m[0][k]+p->pos[1]*turn.m[1][k]+p->pos[2]*turn.m[2][k];
            memcpy(p->pos,pos,sizeof(pos));gevrPoseMatrixQuat(turned,p->q);
        }
        received.body_yaw=yaw;
    }
    /* Pitch and roll the HMD independently of the torso and gun wrists. */
    float headCamera[3][3],tilted[3][3];
    Mtxf tilt;coord3d tiltAngles={.x=.2f,.y=0,.z=.4f};
    matrix_4x4_set_rotation_around_xyz(&tiltAngles,&tilt);
    gevrPoseQuatMatrix(received.head.q,headCamera);
    for(int i=0;i<3;i++)for(int k=0;k<3;k++)tilted[i][k]=headCamera[i][0]*tilt.m[0][k]+headCamera[i][1]*tilt.m[1][k]+headCamera[i][2]*tilt.m[2][k];
    gevrPoseMatrixQuat(tilted,received.head.q);
    received.head.pos[0]=5;
    ModelRwData_HeaderRecord saved=rootRw;
    RenderPosView *base=m.render_pos;
    gevrAvatarApply(&chr,&data);
    assert(m.render_pos!=base && gevrAvatarCollisionPose(&m)==base);
    assert(!memcmp(&rootRw,&saved,sizeof(rootRw)));
    Mtxf headWorld;matrix_4x4_multiply(&world,modelFindNodeMtx(&m,&nodes[3],0),&headWorld);
    for(int i=0;i<3;i++)for(int k=0;k<3;k++)near(headWorld.m[i][k],tilted[i][k]*(i==1?1:-1));
    for(int side=0;side<2;side++) {
        int role=(side==0?GUNLEFT:GUNRIGHT)^(lefty?1:0);
        Mtxf sh,el,wr;
        matrix_4x4_multiply(&world,modelFindNodeMtx(&m,&nodes[4+side],0),&sh);
        matrix_4x4_multiply(&world,modelFindNodeMtx(&m,&nodes[6+side],0),&el);
        matrix_4x4_multiply(&world,modelFindNodeMtx(&m,&nodes[8+side],0),&wr);
        near(length(sh.m[3],el.m[3]),30);near(length(el.m[3],wr.m[3]),25);
        float grip[3][3];gevrPoseQuatMatrix(received.hand[role].q,grip);
        for(int k=0;k<3;k++) {
            near(wr.m[3][k],remote.field_488.collision_position.f[k]+received.head.pos[k]+received.hand[role].pos[k]+grip[1][k]*4);
            near(wr.m[0][k],grip[1][k]*(side==0?-1:1));near(wr.m[2][k],-grip[2][k]);
        }
        int physicalLeft=-1;assert(gevrAvatarWeaponHand(&chr,role,&physicalLeft)==&nodes[8+side]);assert(physicalLeft==(side==0));
        /* Primary and both half-bend matrices were generated; no invalid axes. */
        for(int k=0;k<16;k++)assert(isfinite(((float *)&m.render_pos[(6+side)*3+2].pos)[k]));
    }
    /* mod.attach on this player's copy (#335): a hand's frame at the tracked
     * grip (X its left, Y up, Z the barrel), a body anchor's on the torso the
     * body yaw turns, upright, its centre where gevrBodySlotCentre puts it */
    {
        const float unit=.085f,bodyAt[3]={65,20,-10},*at=remote.field_488.collision_position.f;
        Mtxf f;float grip[3][3];
        for(int role=0;role<2;role++) {
            assert(gevrAvatarAttachFrame(1,1,role,bodyAt,unit,&f));
            gevrPoseQuatMatrix(received.hand[role].q,grip);
            for(int k=0;k<3;k++) {
                near(f.m[0][k],-grip[0][k]*unit*scale);near(f.m[1][k],-grip[2][k]*unit*scale);near(f.m[2][k],-grip[1][k]*unit*scale);
                near(f.m[3][k],(at[k]+received.head.pos[k]+received.hand[role].pos[k])*scale);
            }
        }
        assert(gevrAvatarAttachFrame(1,2,0,bodyAt,unit,&f));
        const float yr=-yaw*3.14159265358979323846f/180,F[3]={sinf(yr),0,cosf(yr)};
        float R[3],origin[3];
        for(int k=0;k<3;k++) {
            near(f.m[1][k],(k==1)*unit*scale);near(f.m[2][k],F[k]*unit*scale);
            R[k]=-f.m[0][k]/(unit*scale);
            origin[k]=-7.5f*tilted[1][k]+8*tilted[2][k]+7.5f*(k==1)+8*F[k];
        }
        near(R[0]*F[0]+R[2]*F[2],0);near(R[1],0);near(R[0]*R[0]+R[2]*R[2],1);
        for(int k=0;k<3;k++)near(f.m[3][k],(at[k]+received.head.pos[k]+origin[k]-65*(k==1)+20*R[k]-10*F[k])*scale);
        assert(!gevrAvatarAttachFrame(1,0,0,bodyAt,unit,&f) && !gevrAvatarAttachFrame(1,1,2,bodyAt,unit,&f)
               && !gevrAvatarAttachFrame(9,1,0,bodyAt,unit,&f));
        weights[1]=0;assert(!gevrAvatarAttachFrame(1,1,0,bodyAt,unit,&f) && gevrAvatarAttachFrame(1,1,1,bodyAt,unit,&f));
        weights[1]=1;weights[0]=0;assert(!gevrAvatarAttachFrame(1,2,0,bodyAt,unit,&f));weights[0]=1;
        active=0;assert(!gevrAvatarAttachFrame(1,1,0,bodyAt,unit,&f));active=1;
    }
    /* A second view builds from its own base, never the prior camera's pose. */
    m.render_pos=base;modelUpdateMatrices(&data,&m);gevrAvatarApply(&chr,&data);
    assert(gevrAvatarCollisionPose(&m)==base);
    active=0;m.render_pos=base;gevrAvatarApply(&chr,&data);assert(m.render_pos==base && !gevrAvatarCollisionPose(&m));active=1;
    chr.actiontype=ACT_DEAD;gevrAvatarApply(&chr,&data);assert(m.render_pos==base);chr.actiontype=ACT_INIT;
    nodes[6].Parent=&nodes[2];gevrAvatarApply(&chr,&data);assert(m.render_pos==base); /* unsupported rig */
    clearFrame();
}
/* mod.attach without tracking (gevrAvatarModelAttachFrame): a flat player or a
 * bot (no pose), and a VR copy's untracked hand, from the body as drawn. The
 * rig relabelled the cartridge's way round (gevr_bean_mapping_test.c checks the
 * bodies): the left arm 4-6-8 out at +X, the model facing +Z, the guns' nodes
 * Switches[3] on the right wrist 9 and [5] on the left 8. */
static void normed(const float *v,float *out) {float d=sqrtf(v[0]*v[0]+v[1]*v[1]+v[2]*v[2]);for(int k=0;k<3;k++)out[k]=v[k]/d;}
static void untracked(float scale,float turn) {
    Model m={0};ModelFileHeader header={0};ChrRecord chr={0};PropRecord prop={0};ModelRenderData data={0};
    fixture(&m,&header,&chr,&prop,&data,scale);
    static const int swap[10]={0,1,2,3,5,4,7,6,9,8};
    for(int j=4;j<=9;j++)ro[j].Group.JointID=swap[j];
    ModelNode *sw[8]={0};sw[3]=&nodes[8];sw[5]=&nodes[9];header.Switches=sw;header.numSwitches=8;
    rootRw.unk14=turn*3.14159265358979323846f/180;
    active=0;modelUpdateMatrices(&data,&m);gevrAvatarApply(&chr,&data);   /* no pose: the game's own body */
    const float unit=.085f,bodyAt[3]={65,20,-10};
    Mtxf f;float e[3];
    assert(!gevrAvatarAttachFrame(1,1,0,bodyAt,unit,&f) && !gevrAvatarAttachFrame(1,2,0,bodyAt,unit,&f));
    for(int role=0;role<2;role++) {
        const Mtxf *h=modelFindNodeMtx(&m,role==GUNRIGHT?&nodes[8]:&nodes[9],0);
        const float t=role==GUNLEFT?-1:1;
        float x[3],y[3],z[3];
        for(int k=0;k<3;k++) {x[k]=-t*h->m[1][k];y[k]=h->m[2][k];z[k]=-t*h->m[0][k];}
        normed(x,x);normed(y,y);normed(z,z);
        assert(gevrAvatarModelAttachFrame(&chr,1,role,bodyAt,unit,&f));
        for(int k=0;k<3;k++) {
            near(f.m[0][k],x[k]*unit*scale);near(f.m[1][k],y[k]*unit*scale);near(f.m[2][k],z[k]*unit*scale);
            near(f.m[3][k],h->m[3][k]+4*scale*z[k]);   /* the grip, 4 cm ahead of the wrist */
        }
        assert(f.m[0][3]==0 && f.m[3][3]==1);
    }
    /* the two hands' frames on their own sides: the right hand's out at the model's -X */
    {
        Mtxf r,l;assert(gevrAvatarModelAttachFrame(&chr,1,GUNRIGHT,bodyAt,unit,&r) && gevrAvatarModelAttachFrame(&chr,1,GUNLEFT,bodyAt,unit,&l));
        const Mtxf *root=&m.render_pos[0].pos;float side=0;
        for(int k=0;k<3;k++)side+=(r.m[3][k]-l.m[3][k])*root->m[0][k];
        assert(side<0);
    }
    /* the body: up the pelvis to the neck, right shoulder to shoulder, ahead the
     * way the model faces (its root's +Z as drawn), turned with it */
    {
        const float *p1=modelFindNodeMtx(&m,&nodes[1],0)->m[3],*p3=modelFindNodeMtx(&m,&nodes[3],0)->m[3];
        const float *left=modelFindNodeMtx(&m,&nodes[5],0)->m[3],*right=modelFindNodeMtx(&m,&nodes[4],0)->m[3];
        float up[3],across[3],ahead[3];
        for(int k=0;k<3;k++) {up[k]=p3[k]-p1[k];across[k]=right[k]-left[k];}
        normed(up,up);normed(across,across);normed(m.render_pos[0].pos.m[2],ahead);
        assert(gevrAvatarModelAttachFrame(&chr,2,GEVR_BS_HIP_GUN,bodyAt,unit,&f));
        for(int k=0;k<3;k++) {
            near(f.m[0][k],-across[k]*unit*scale);near(f.m[1][k],up[k]*unit*scale);near(f.m[2][k],ahead[k]*unit*scale);
            e[k]=p3[k]+scale*(7.5f*up[k]+8*ahead[k]-65*up[k]+20*across[k]-10*ahead[k]);
            near(f.m[3][k],e[k]);
        }
        const float yr=turn*3.14159265358979323846f/180;
        near(ahead[0],sinf(yr));near(ahead[1],0);near(ahead[2],cosf(yr));
    }
    /* nothing to build one from */
    assert(!gevrAvatarModelAttachFrame(&chr,0,0,bodyAt,unit,&f) && !gevrAvatarModelAttachFrame(&chr,1,2,bodyAt,unit,&f)
           && !gevrAvatarModelAttachFrame(&chr,2,0,NULL,unit,&f) && !gevrAvatarModelAttachFrame(NULL,1,0,bodyAt,unit,&f));
    header.Switches=NULL;assert(!gevrAvatarModelAttachFrame(&chr,1,0,bodyAt,unit,&f));header.Switches=sw;
    sw[5]=NULL;assert(!gevrAvatarModelAttachFrame(&chr,1,GUNLEFT,bodyAt,unit,&f) && gevrAvatarModelAttachFrame(&chr,1,GUNRIGHT,bodyAt,unit,&f));
    sw[5]=&nodes[9];
    /* a VR copy with its left hand lost: its tracked arm posed, the other the
     * game's; that hand's frame from the matrices as posed and drawn */
    active=1;weights[2]=0;
    RenderPosView *base=m.render_pos;
    gevrAvatarApply(&chr,&data);
    assert(m.render_pos!=base && !gevrAvatarAttachFrame(1,1,GUNLEFT,bodyAt,unit,&f) && gevrAvatarAttachFrame(1,1,GUNRIGHT,bodyAt,unit,&f));
    assert(gevrAvatarModelAttachFrame(&chr,1,GUNLEFT,bodyAt,unit,&f));
    for(int k=0;k<3;k++)near(f.m[3][k],m.render_pos[ro[9].Group.MatrixID0].pos.m[3][k]+4*f.m[2][k]/unit);
    weights[2]=1;rootRw.unk14=0;m.render_pos=base;
    clearFrame();
}
static void capture(void) {
    Model m={0};ModelFileHeader header={0};ChrRecord chr={0};PropRecord prop={0};ModelRenderData data={0};
    fixture(&m,&header,&chr,&prop,&data,.2f);
    struct player pl={0};pl.prop=&prop;eye=prop.pos;eye.y+=10;
    GevrAvatarPose p;gevrAvatarCapture(&p,&pl);assert(p.flags==7);near(p.hand[0].pos[0],20);near(p.hand[1].pos[0],-20);near(p.head.pos[1],10);
    VrLeftHandedMode=1;gevrAvatarCapture(&p,&pl);assert(p.flags==15);near(p.hand[0].pos[0],-20);near(p.hand[1].pos[0],20);
    tracked[0]=0;gevrAvatarCapture(&p,&pl);assert(p.flags==(GEVR_POSE_HEAD|GEVR_POSE_LEFT|GEVR_POSE_LEFT_HANDED));
    ready=0;gevrAvatarCapture(&p,&pl);assert(!p.flags);
    ready=1;tracked[0]=1;VrLeftHandedMode=0;
    aiming=1;pl.hands[0].weaponnum=ITEM_WPPK;gevrAvatarCapture(&p,&pl);
    float grip[3][3];gevrPoseQuatMatrix(p.hand[0].q,grip);near(-grip[1][0],1);near(-grip[1][1],0);near(-grip[1][2],0);
    aiming=0;focused=0;gevrAvatarCapture(&p,&pl);assert(!p.flags);focused=1;
    /* The stick turns the torso with the head; the headset alone only glances. */
    now+=1000000;gevrAvatarCapture(&p,&pl); /* fresh start: torso under the head */
    for(int dir=-1;dir<=1;dir+=2) {
        for(int i=0;i<60;i++) { /* a second of smooth turning, both ways */
            now+=16667;baseYaw=remainderf(baseYaw+dir*2,360);pl.vv_theta=baseYaw;
            gevrAvatarCapture(&p,&pl);near(remainderf(p.body_yaw-baseYaw,360),0);
        }
        now+=16667;baseYaw=remainderf(baseYaw+dir*45,360);pl.vv_theta=baseYaw; /* a snap */
        gevrAvatarCapture(&p,&pl);near(remainderf(p.body_yaw-baseYaw,360),0);
    }
    for(int i=0;i<60;i++) {now+=16667;pl.vv_theta=remainderf(baseYaw+30,360);gevrAvatarCapture(&p,&pl);}
    near(remainderf(p.body_yaw-baseYaw,360),0);  /* a 30 degree glance keeps the torso */
    for(int i=0;i<120;i++) {now+=16667;pl.vv_theta=remainderf(baseYaw+90,360);gevrAvatarCapture(&p,&pl);}
    assert(fabsf(remainderf(pl.vv_theta-p.body_yaw,360))<=35.5f); /* a physical turn brings it round */
    baseYaw=0;pl.vv_theta=0;clearFrame();
}
int main(void) {rig(1,0,0,-10);rig(.2f,0,0,-40);rig(1,1,0,20);rig(1,0,90,-10);rig(.2f,1,350,20);untracked(1,0);untracked(.2f,90);untracked(1,215);capture();puts("avatar: production rig builders, both wrists, left-handed attachments, scale, collision pose, untracked attachment frames and capture passed");return 0;}
