/* Remote VR avatar adapter. Arm geometry: PD CE's two-bone clutch (MIT);
 * independent torso following: Alex-LeTux's perfect_dark_VR (MIT).
 * See docs/multiplayer-ik.md and port/licenses/{perfect-dark-ce,perfect-dark-vr}.txt. */
#include <stdbool.h>
#include "gevr_avatar.h"
#include "gevr_bodyslot.h"   /* the torso's frame, for mod.attach on another player */
#include "net/net_core.h"
#include "model.h"
#include "objecthandler.h"
#include "player.h"
#include "bondview.h"
#include "matrixmath.h"
#include "dyn.h"
#include "system.h"
#include <math.h>
#include <string.h>

extern int g_gevrStereo, VrLeftHandedMode;
extern s32 g_gevrExtraPass; /* lv.c: a copy's or barrel's view pass, never drawn */
extern int gevrVrReady(void), gevrVrGripTracked(int hand), gevrVrGripUsable(int hand);
extern int gevrVrSessionFocused(void);
extern int gevrVrGripPoseCamera(int hand,float pos[3],float q[4]);
extern s32 gevrStereoShotWorld(s32 hand,coord3d *origin,coord3d *dir);
extern coord3d *gevrEyePosition(void);
extern float gevrStereoBaseYaw(void); /* bondview2.c: stick turns plus game turns, vv_theta degrees */

static float dot(const float *a,const float *b) {return a[0]*b[0]+a[1]*b[1]+a[2]*b[2];}
static int unit(float *v) {float d=sqrtf(dot(v,v));if(!isfinite(d)||d<1e-6f)return 0;for(int k=0;k<3;k++)v[k]/=d;return 1;}
static void cross(const float *a,const float *b,float *c) {c[0]=a[1]*b[2]-a[2]*b[1];c[1]=a[2]*b[0]-a[0]*b[2];c[2]=a[0]*b[1]-a[1]*b[0];}
static void rotate(const float m[3][3],const float *v,float *o) {
    for(int k=0;k<3;k++)o[k]=v[0]*m[0][k]+v[1]*m[1][k]+v[2]*m[2][k];
}
static void basis(const Mtxf *m,float out[3][3]) {
    for(int i=0;i<3;i++) {memcpy(out[i],m->m[i],3*sizeof(float));unit(out[i]);}
}
static void product(const float a[3][3],const float b[3][3],float out[3][3]) {
    for(int i=0;i<3;i++)rotate(a,b[i],out[i]);
}
static void worldMatrix(Model *m,ModelNode *node,Mtxf *out) {
    matrix_4x4_multiply(currentPlayerGetViewToWorldMtxf(),modelFindNodeMtx(m,node,0),out);
}
static void aimBone(const float from[3],const float to[3],const float original[3][3],float out[3][3]);

void gevrAvatarCapture(GevrAvatarPose *p,struct player *pl) {
    static uint64_t last;
    static float yaw,anchor[3];
    memset(p,0,sizeof(*p));
    if(!g_gevrStereo || !gevrVrReady() || !gevrVrSessionFocused() || !pl || !pl->prop || pl->bonddead || pl->cameramode==1) {last=0;return;}
    Mtxf *v2w=currentPlayerGetViewToWorldMtxf();
    if(!v2w)return;
    float view[3][3];basis(v2w,view);
    gevrPoseMatrixQuat(view,p->head.q);
    coord3d *eye=gevrEyePosition();
    for(int k=0;k<3;k++)p->head.pos[k]=eye->f[k]-pl->prop->pos.f[k];
    p->flags=GEVR_POSE_HEAD | (VrLeftHandedMode?GEVR_POSE_LEFT_HANDED:0);
    uint64_t now=sysGetMicroseconds();float moved[3];
    for(int k=0;k<3;k++)moved[k]=pl->prop->pos.f[k]-anchor[k];
    /* vv_theta is where the head looks: the stick/body yaw plus the headset's
     * own turn. The stick turns the body (head and torso together), so only
     * the headset's turn is a glance. Following vv_theta as a whole left the
     * torso up to 35 degrees behind every stick turn, the head turned to that
     * side while the player looked straight ahead (Quest/PC test 2026-11-25). */
    float base=gevrStereoBaseYaw(),look=remainderf(pl->vv_theta-base,360);
    if(!last || now<last || now-last>250000 || dot(moved,moved)>10000)yaw=look;
    float delta=remainderf(look-yaw,360),dt=last && now>=last?(now-last)*1e-6f:0;
    /* A glance leaves the torso behind; sustained physical turns and walking
     * follow. Limit the head/body separation to 65 degrees. */
    if(fabsf(delta)>35 || fabsf(pl->speedforwards)+fabsf(pl->speedsideways)>0.1f)
        yaw+=delta*(1-expf(-3.0f*fminf(dt,0.1f)));
    delta=remainderf(look-yaw,360);
    if(fabsf(delta)>65)yaw=look-copysignf(65,delta);
    yaw=remainderf(yaw,360);
    p->body_yaw=remainderf(base+yaw,360);last=now;memcpy(anchor,pl->prop->pos.f,sizeof(anchor));
    for(int role=0;role<2;role++) {
        int ctrl=role==GUNRIGHT?1:0;
        float q[4],pos[3],grip[3][3],world[3][3];
        if(!gevrVrGripUsable(ctrl) || !gevrVrGripTracked(ctrl) || !gevrVrGripPoseCamera(ctrl,pos,q))continue;
        for(int k=0;k<3;k++)pos[k]*=100; /* view metres -> unscaled world cm */
        rotate(view,pos,p->hand[role].pos);
        gevrPoseQuatMatrix(q,grip);product(view,grip,world);
        /* The two-handed aim line can differ from the trigger wrist. Carry
         * the rendered barrel's turn into that wrist, retaining its roll. */
        coord3d origin,dir;
        if(pl->hands[role].weaponnum>ITEM_FIST && gevrStereoShotWorld(role,&origin,&dir)) {
            float forward[3],aligned[3][3];
            for(int k=0;k<3;k++)forward[k]=-world[1][k];
            aimBone(forward,dir.f,world,aligned);memcpy(world,aligned,sizeof(world));
        }
        gevrPoseMatrixQuat(world,p->hand[role].q);
        p->flags|=1u<<(role+1);
    }
    if(!gevrPoseValidate(p))memset(p,0,sizeof(*p));
}

/* Per-build overrides use node identity, not matrix numbers shared by other rigs. */
typedef struct AvatarBuild {
    Model *model;
    ModelNode *node[16];
    float rotation[16][4];
    unsigned mask;
    float offset[3]; /* chest translation, view space */
} AvatarBuild;
static AvatarBuild s_build;
typedef struct AvatarCollision {Model *model;RenderPosView *posed,*base;} AvatarCollision;
static AvatarCollision s_collision[GEVR_MAX_PLAYERS];

static ModelNode *nextNode(ModelNode *n) {
    if(n->Child)return n->Child;
    while(n && !n->Next)n=n->Parent;
    return n?n->Next:NULL;
}
static int findRig(Model *m,ModelNode **nodes) {
    memset(nodes,0,16*sizeof(*nodes));int visits=0;
    for(ModelNode *n=m->obj->RootNode;n;n=nextNode(n)) {
        if(++visits>512)return 0;
        if((n->Opcode&255)!=MODELNODE_OPCODE_GROUP)continue;
        int j=n->Data->Group.JointID;
        if(j>0 && j<16 && !nodes[j]) {
            int index=n->Data->Group.MatrixID0;
            if(index<0 || index>=m->obj->numMatrices)return 0;
            nodes[j]=n;
        }
    }
    for(int j=1;j<=9;j++)if(!nodes[j])return 0;
    /* The guard rig's shoulders, elbows and wrists must form these chains. */
    for(int side=0;side<2;side++) {
        int sh=4+side,el=6+side,wr=8+side;
        if(nodes[el]->Parent!=nodes[sh] || nodes[wr]->Parent!=nodes[el])return 0;
    }
    return nodes[3]->Parent==nodes[2] && (m->obj->RootNode->Opcode&255)==MODELNODE_OPCODE_HEADER;
}
static void desired(int joint,const float m[3][3],const float original[3][3],float weight) {
    float q[4],base[4];gevrPoseMatrixQuat(m,q);gevrPoseMatrixQuat(original,base);
    gevrPoseQuatLerp(base,q,weight,s_build.rotation[joint]);s_build.mask|=1u<<joint;
}
static void aimBone(const float from[3],const float to[3],const float original[3][3],float out[3][3]) {
    float a[3],b[3],axis[3],q[4],r[3][3];memcpy(a,from,sizeof(a));memcpy(b,to,sizeof(b));
    if(!unit(a)||!unit(b)) {memcpy(out,original,9*sizeof(float));return;}
    cross(a,b,axis);float d=fmaxf(-1,fminf(1,dot(a,b)));
    if(d<-.9999f) {
        float fallback[3]={0,1,0};if(fabsf(a[1])>.9f) {fallback[0]=1;fallback[1]=0;}
        cross(a,fallback,axis);unit(axis);for(int k=0;k<3;k++)q[k]=axis[k];q[3]=0;
    } else {
        float n=sqrtf(2*(1+d));for(int k=0;k<3;k++)q[k]=axis[k]/n;q[3]=n*.5f;
    }
    gevrPoseQuatMatrix(q,r);product(r,original,out);
}

int gevrAvatarJointRotation(Model *model,ModelNode *node,coord3d *rotation) {
    if(s_build.model!=model)return 0;
    int j=node->Data->Group.JointID;
    if(j<1 || j>15 || s_build.node[j]!=node || !(s_build.mask&(1u<<j)))return 0;
    Mtxf parent,local;float pb[3][3],want[3][3];
    if(!node->Parent)return 0;
    worldMatrix(model,node->Parent,&parent);basis(&parent,pb);
    gevrPoseQuatMatrix(s_build.rotation[j],want);
    matrix_4x4_set_identity(&local);
    for(int i=0;i<3;i++)for(int k=0;k<3;k++)local.m[i][k]=dot(want[i],pb[k]);
    matrix_4x4_get_rotation_around_xyz(&local,rotation);
    return 1;
}
void gevrAvatarJointOffset(Model *model,ModelNode *node) {
    if(s_build.model!=model || node!=s_build.node[2])return;
    for(int j=0;j<3;j++) {
        if(j && !(node->Opcode&(j==1?0x100:0x200)))continue;
        int i=node->Data->Group.MatrixIDs[j];
        if(i>=0 && i<model->obj->numMatrices)
            for(int k=0;k<3;k++)model->render_pos[i].pos.m[3][k]+=s_build.offset[k];
    }
}

void gevrAvatarApply(ChrRecord *chr,ModelRenderData *data) {
    if(!chr || !chr->prop || chr->prop->type!=PROP_TYPE_VIEWER || !chr->model)return;
    int slot=getPlayerPointerIndex(chr->prop);
    if(slot<0 || slot>=GEVR_MAX_PLAYERS || !g_playerPointers[slot])return;
    s_collision[slot].model=NULL;
    /* Extra passes only place bodies for shot traces; skip the extra matrix
     * builds and dyn memory there (up to ~15 passes a frame with eight players). */
    if(g_gevrExtraPass)return;
    if(chr->actiontype==ACT_DIE || chr->actiontype==ACT_DEAD)return;
    GevrAvatarPose pose;float weight[3];
    if(!netGetAvatarPose(slot,&pose,weight) || !chr->model->anim)return;
    Model *m=chr->model;ModelNode *nodes[16];
    if(!findRig(m,nodes))return;
    /* Preserve the game's animated body hitboxes. Visual IK has no authority
     * over hit testing and no feedback into player movement or the shot rays. */
    RenderPosView *baseline=m->render_pos;
    m->render_pos=dynAllocate(m->obj->numMatrices*sizeof(*m->render_pos));
    if(!m->render_pos) {m->render_pos=baseline;return;}
    s_collision[slot]=(AvatarCollision){m,m->render_pos,baseline};
    ModelRwData_HeaderRecord *root=&modelGetNodeRwData(m,m->obj->RootNode)->Header;
    ModelRwData_HeaderRecord savedRoot=*root;
    float oldYaw=root->unk14;
    float bodyYaw=(360-pose.body_yaw)*(3.14159265358979323846f/180);
    float diff=remainderf(bodyYaw-oldYaw,6.283185307179586f);
    root->unk14=oldYaw+diff*weight[0];root->unk18=0;
    void (*jointCallback)(s32,Mtxf *)=g_ModelJointPositionedFunc;
    g_ModelJointPositionedFunc=NULL;
    modelUpdateMatrices(data,m); /* base walking rig at the independent torso yaw */
    memset(&s_build,0,sizeof(s_build));memcpy(s_build.node,nodes,sizeof(nodes));
    float shift[3];
    for(int k=0;k<3;k++)shift[k]=fmaxf(-35,fminf(35,pose.head.pos[k]))*weight[0];
    Mtxf *w2v=camGetWorldToScreenMtxf();
    for(int k=0;k<3;k++)s_build.offset[k]=shift[0]*w2v->m[0][k]+shift[1]*w2v->m[1][k]+shift[2]*w2v->m[2][k];

    Mtxf head;worldMatrix(m,nodes[3],&head);
    float hb[3][3],tracked[3][3],level[3][3],inv[3][3],delta[3][3],want[3][3];
    basis(&head,hb);gevrPoseQuatMatrix(pose.head.q,tracked);
    float yaw=-pose.body_yaw*(3.14159265358979323846f/180);
    /* Camera right/up/back for this torso heading (the game faces +Z at 0). */
    level[0][0]=-cosf(yaw);level[0][1]=0;level[0][2]=sinf(yaw);
    level[1][0]=0;level[1][1]=1;level[1][2]=0;
    level[2][0]=-sinf(yaw);level[2][1]=0;level[2][2]=-cosf(yaw);
    for(int i=0;i<3;i++)for(int k=0;k<3;k++)inv[i][k]=level[k][i];
    product(tracked,inv,delta);product(delta,hb,want);desired(3,want,hb,weight[0]);

    for(int side=0;side<2;side++) { /* 0 anatomical left, 1 anatomical right */
        int role=(side==0?GUNLEFT:GUNRIGHT)^((pose.flags&GEVR_POSE_LEFT_HANDED)?1:0);
        if(weight[role+1]<=0)continue;
        int sh=4+side,el=6+side,wr=8+side;
        Mtxf sm,em,wm;worldMatrix(m,nodes[sh],&sm);worldMatrix(m,nodes[el],&em);worldMatrix(m,nodes[wr],&wm);
        float a[3],b[3],s[3],target[3],pole[3],ep[3],wp[3],fore[3];
        for(int k=0;k<3;k++) {
            s[k]=sm.m[3][k]+shift[k];a[k]=em.m[3][k]-sm.m[3][k];b[k]=wm.m[3][k]-em.m[3][k];
            /* chrTick temporarily places the viewer prop at its animated
             * pelvis. Movement packets and head offsets use the eye position. */
            target[k]=g_playerPointers[slot]->field_488.collision_position.f[k]
                +pose.head.pos[k]+pose.hand[role].pos[k];
            pole[k]=level[0][k]*(side==0?-.6f:.6f)+level[2][k]*.3f;
        }
        pole[1]-=.6f;
        /* The runtime's grip is at the palm, roughly 4 cm ahead of the wrist. */
        float grip[3][3];gevrPoseQuatMatrix(pose.hand[role].q,grip);
        for(int k=0;k<3;k++)target[k]+=grip[1][k]*4;
        if(!gevrArmSolve(s,target,sqrtf(dot(a,a)),sqrtf(dot(b,b)),pole,a,ep,wp))continue;
        float orig[3][3],aim[3][3];
        for(int k=0;k<3;k++)fore[k]=ep[k]-s[k];
        basis(&sm,orig);aimBone(a,fore,orig,aim);desired(sh,aim,orig,weight[role+1]);
        for(int k=0;k<3;k++)fore[k]=wp[k]-ep[k];
        basis(&em,orig);aimBone(b,fore,orig,aim);desired(el,aim,orig,weight[role+1]);
        /* Guard hand/Pchr frame: barrel -X, up +Z. Native left wrist is
         * turned 180 about Z before attaching its weapon (propobj.c). */
        for(int k=0;k<3;k++) {want[0][k]=grip[1][k]*(side==0?-1:1);want[1][k]=grip[0][k]*(side==0?-1:1);want[2][k]=-grip[2][k];}
        basis(&wm,orig);desired(wr,want,orig,weight[role+1]);
    }
    s_build.model=m;
    modelUpdateMatrices(data,m); /* regenerates auxiliary bend matrices and all descendants */
    s_build.model=NULL;g_ModelJointPositionedFunc=jointCallback;*root=savedRoot;
}

RenderPosView *gevrAvatarCollisionPose(Model *model) {
    for(int i=0;i<GEVR_MAX_PLAYERS;i++)if(s_collision[i].model==model && s_collision[i].posed==model->render_pos)return s_collision[i].base;
    return NULL;
}
ModelNode *gevrAvatarWeaponHand(ChrRecord *chr,int hand,int *physicalLeft) {
    if(!chr || !chr->prop || chr->prop->type!=PROP_TYPE_VIEWER || g_gevrExtraPass || hand<0 || hand>1)return NULL;
    int slot=getPlayerPointerIndex(chr->prop);GevrAvatarPose pose;float weight[3];
    if(!netGetAvatarPose(slot,&pose,weight))return NULL;
    /* An untracked hand keeps the native attachment, not an animated far-side wrist. */
    if(weight[hand+1]<=0)return NULL;
    ModelNode *nodes[16];if(!findRig(chr->model,nodes))return NULL;
    *physicalLeft=(hand==GUNLEFT)^((pose.flags&GEVR_POSE_LEFT_HANDED)?1:0);
    return nodes[*physicalLeft?8:9];
}

/*
 * gevr_modattach.c (mod.attach on another player, #335): the frame that
 * player's attachment on a hand (on 1, where its weapon role) or a body
 * anchor (on 2; bodyAt its centre from the eye, cm: below, right, ahead, as
 * gevr_bodyslots.c gevrBodySlotAt sends it) is laid in, view space in world
 * units, unit cm to a gun model's unit: X the holder's left, Y up, Z ahead,
 * as that player's own headset builds it (bondview2.c gevrModAttachHandFrame,
 * gevr_bodyslots.c gevrBodySlotFrame). A hand: the tracked grip (the palm
 * gevrAvatarApply reaches for is 4 cm ahead of it). The body: the torso's
 * yaw as sent, its frame built from the head as gevrBodyFrameBuild builds
 * this player's. 0 while that hand or the head isn't tracked.
 */
int gevrAvatarAttachFrame(int slot,int on,int where,const float bodyAt[3],float unit,Mtxf *out) {
    if(slot<0 || slot>=GEVR_MAX_PLAYERS || !g_playerPointers[slot] || !out || !bodyAt)return 0;
    GevrAvatarPose pose;float weight[3],x[3],y[3],z[3],p[3];
    if(!netGetAvatarPose(slot,&pose,weight))return 0;
    const float *at=g_playerPointers[slot]->field_488.collision_position.f;
    if(on==1) {
        if(where<0 || where>1 || weight[where+1]<=0)return 0;
        float grip[3][3];gevrPoseQuatMatrix(pose.hand[where].q,grip);   /* rows: right, back (the barrel's -Y), down */
        for(int k=0;k<3;k++) {
            x[k]=-grip[0][k];y[k]=-grip[2][k];z[k]=-grip[1][k];
            p[k]=at[k]+pose.head.pos[k]+pose.hand[where].pos[k];
        }
    } else if(on==2) {
        if(weight[0]<=0)return 0;
        float head[3][3],fwd[3],hp[2];gevrPoseQuatMatrix(pose.head.q,head);   /* rows: right, up, back */
        for(int k=0;k<3;k++)fwd[k]=-head[2][k];
        if(!gevrBodyHeading(fwd,head[1],hp))return 0;
        const float yaw=-pose.body_yaw*(3.14159265358979323846f/180);   /* gevrAvatarApply's level frame */
        GevrBodyTorso torso;memset(&torso,0,sizeof(torso));
        torso.heading[0]=sinf(yaw);torso.heading[1]=cosf(yaw);torso.valid=1;
        GevrBodyFrame f;gevrBodyFrameBuild(&f,&torso,hp,fwd,head[1],head[0]);
        for(int k=0;k<3;k++) {
            x[k]=-f.right[k];y[k]=f.up[k];z[k]=f.fwd[k];
            p[k]=at[k]+pose.head.pos[k]+f.origin[k]-bodyAt[0]*f.up[k]+bodyAt[1]*f.right[k]+bodyAt[2]*f.fwd[k];
        }
    } else return 0;
    Mtxf *w2v=camGetWorldToScreenMtxf();
    if(!w2v)return 0;
    for(int k=0;k<3;k++) {
        out->m[0][k]=(x[0]*w2v->m[0][k]+x[1]*w2v->m[1][k]+x[2]*w2v->m[2][k])*unit;
        out->m[1][k]=(y[0]*w2v->m[0][k]+y[1]*w2v->m[1][k]+y[2]*w2v->m[2][k])*unit;
        out->m[2][k]=(z[0]*w2v->m[0][k]+z[1]*w2v->m[1][k]+z[2]*w2v->m[2][k])*unit;
        out->m[3][k]=p[0]*w2v->m[0][k]+p[1]*w2v->m[1][k]+p[2]*w2v->m[2][k]+w2v->m[3][k];
    }
    out->m[0][3]=out->m[1][3]=out->m[2][3]=0;out->m[3][3]=1;
    return 1;
}

/* up, then ahead without up, then left without both: unit and square, each
 * axis kept on its own side (never a cross product: a mirrored matrix stays
 * as the game draws it) */
static int squareAxes(float *up,float *ahead,float *left) {
    if(!unit(up))return 0;
    float d=dot(ahead,up);for(int k=0;k<3;k++)ahead[k]-=d*up[k];
    if(!unit(ahead))return 0;
    float u=dot(left,up),a=dot(left,ahead);for(int k=0;k<3;k++)left[k]-=u*up[k]+a*ahead[k];
    return unit(left);
}

/*
 * gevr_modattach.c (mod.attach without tracking): the frame
 * gevrAvatarAttachFrame gives, from the player's body model as just drawn
 * instead of its tracked pose: chr->model's matrices in this view (chrRenderProp
 * calls this after drawjointlist, before they go to fixed point), the very
 * ones the body was drawn from: the N64 and GE-X lists, and Bean's skin,
 * whose GPU palette reads the same render_pos (gevr_bean_gpu.inc.h).
 *  - A hand (on 1, where its weapon role): the node that hand's gun hangs on
 *    (propobj.c chrEquipWeapon: Switches[3] the right, Switches[5] the left,
 *    under the wrists, joints 9 and 8), the left one turned half about its Z
 *    as chrRenderHeldWeapon turns it. Its axes as the held gun's
 *    (gevr_heldgun.c gevrHeldGunFit: the barrel -X, up +Z, the gun's left -Y),
 *    so a part sits on that hand as on the gun it holds; at the grip, 4 cm
 *    ahead of the wrist, where gevrAvatarApply puts a tracked grip.
 *  - The body (on 2, bodyAt its centre from the eye, cm, as for the tracked
 *    pose): the torso from the rig's joints (findRig; GE JointID = PD part + 1:
 *    1 the pelvis, 3 the neck, 4 and 5 the left and right upper arms): up the
 *    pelvis to the neck, right the left shoulder to the right, ahead
 *    up x right; the eye GEVR_BODY_NECK_* above and ahead of the neck, as
 *    gevrBodyFrameBuild puts it over a tracked torso; the anchor's centre
 *    from there in the torso's terms, as for the tracked pose.
 * Lengths in cm are the view's (the world-to-view matrix's scale). 0 without
 * that node or rig, or a matrix with no axes.
 */
int gevrAvatarModelAttachFrame(ChrRecord *chr,int on,int where,const float bodyAt[3],float unit,Mtxf *out) {
    Model *m=chr?chr->model:NULL;
    if(!m || !m->obj || !m->render_pos || !out)return 0;
    Mtxf *w2v=camGetWorldToScreenMtxf();
    float view=w2v?sqrtf(dot(w2v->m[0],w2v->m[0])):0,x[3],y[3],z[3],p[3];
    if(!isfinite(view) || view<1e-6f)return 0;
    if(on==1) {
        if(where<0 || where>1 || !m->obj->Switches)return 0;
        ModelNode *hand=m->obj->Switches[where==GUNRIGHT?3:5];
        const Mtxf *h=hand?modelFindNodeMtx(m,hand,0):NULL;
        if(!h)return 0;
        const float turn=where==GUNLEFT?-1:1;   /* the left hand's half turn about Z */
        for(int k=0;k<3;k++) {
            x[k]=-turn*h->m[1][k];y[k]=h->m[2][k];z[k]=-turn*h->m[0][k];p[k]=h->m[3][k];
        }
    } else if(on==2) {
        ModelNode *nodes[16];const Mtxf *joint[6];
        if(!bodyAt || !findRig(m,nodes))return 0;
        for(int j=1;j<=5;j++)if(!(joint[j]=modelFindNodeMtx(m,nodes[j],0)))return 0;
        float right[3];
        for(int k=0;k<3;k++) {
            y[k]=joint[3]->m[3][k]-joint[1]->m[3][k];
            right[k]=joint[5]->m[3][k]-joint[4]->m[3][k];
        }
        cross(y,right,z);   /* ahead: up x right, as the world's right x up is behind */
        for(int k=0;k<3;k++)x[k]=-right[k];
        if(!squareAxes(y,z,x))return 0;
        for(int k=0;k<3;k++)
            p[k]=joint[3]->m[3][k]+view*(GEVR_BODY_NECK_UP_CM*y[k]+GEVR_BODY_NECK_AHEAD_CM*z[k]
                                        -bodyAt[0]*y[k]-bodyAt[1]*x[k]+bodyAt[2]*z[k]);
    } else return 0;
    if(on==1) {
        if(!squareAxes(y,z,x))return 0;
        for(int k=0;k<3;k++)p[k]+=4*view*z[k];   /* the grip ahead of the wrist */
    }
    for(int k=0;k<3;k++) {
        out->m[0][k]=x[k]*unit*view;out->m[1][k]=y[k]*unit*view;out->m[2][k]=z[k]*unit*view;out->m[3][k]=p[k];
    }
    out->m[0][3]=out->m[1][3]=out->m[2][3]=0;out->m[3][3]=1;
    return 1;
}

/*
 * gevr_forcepull.c online: where another player's hand is in the world, for
 * a gun flying to it (docs/force-pull.md "Online"). hand is a weapon role
 * (GUNRIGHT, GUNLEFT). The tracked palm as gevrAvatarApply reaches for it;
 * a flat player, a bot or an untracked hand: the chest below the eye.
 */
int gevrNetRemoteHandWorld(int slot,int hand,float out[3]) {
    if(slot<0 || slot>=GEVR_MAX_PLAYERS || hand<0 || hand>1 || !g_playerPointers[slot] || !g_playerPointers[slot]->prop)return 0;
    struct player *p=g_playerPointers[slot];
    GevrAvatarPose pose;float weight[3];
    if(netGetAvatarPose(slot,&pose,weight) && weight[hand+1]>0) {
        float grip[3][3];gevrPoseQuatMatrix(pose.hand[hand].q,grip);
        for(int k=0;k<3;k++)out[k]=p->field_488.collision_position.f[k]+pose.head.pos[k]+pose.hand[hand].pos[k]+grip[1][k]*4;
        return 1;
    }
    for(int k=0;k<3;k++)out[k]=p->field_488.collision_position.f[k];
    out[1]-=40;
    return 1;
}
