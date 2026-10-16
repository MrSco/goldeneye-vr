/* GoldenEye adapter for Perfect Dark CE's Jolt interface. Death bodies are
 * presentation snapshots, not actors: all modes use the same local settings,
 * and respawns, AI removal and network authority never depend on physics. */
#include "gevr_ragdoll_internal.h"
#include "model.h"
#include "chr.h"
#include "player.h"
#include "bondview.h"
#include "lv.h"
#include "bg.h"
#include "dyn.h"
#include "system.h"
#include "gevr_bean.h"
#include "chrobjdata.h"
#include "matrixmath.h"
#include "gun.h"
#include "bgfog.h"

extern int VrRagdolls, VrDismemberment, VrBodiesStay;
extern ModelRenderData D_8002CCBC;
extern s32 g_gevrExtraPass;

static RdBody s_bodies[RD_BODIES];
static RdMemory *s_garbage;
static unsigned s_frame, s_serial;
static int s_failed, s_warned;

/* CE g_RdSkelHuman (ragdoll.c:205): an average adult after Winter / de Leva;
 * chest = upper + middle trunk, forearm + hand, shin + foot. */
const RdSkel g_RdSkelHuman = {
    { -1, 0, 1, 1, 1, 3, 4, 5, 6, 0, 0, 9, 10, 11, 12 },
    { 1, 1, 1, 1, 1, 1, 1, 0, 0, 1, 1, 1, 1, 0, 0 },
    { -1, 2, -1, 5, 6, 7, 8, -1, -1, 11, 12, 13, 14, -1, -1 },
    { 0, 0, 0, 0, 0, -1, 1, 0, 0, 0, 0, 1, -1, 0, 0 },
    { 11.0f, 26.0f, 5.5f, 2.2f, 2.2f, 1.8f, 1.8f, 0, 0, 11.4f, 11.4f, 4.5f, 4.5f, 0, 0 },
    { 0, 0.30f, 0.17f, 0.15f, 0.15f, 0.12f, 0.12f, 0, 0, 0.19f, 0.19f, 0.16f, 0.16f, 0, 0 },
};

/* CE ragdoll.c:41, 44, 651-654, 12; rd_internal.h RD_KILLFLOOR_Y, RD_COLROOM_MOVE */
#define RD_DISMEMBER_DAMAGE 2.0f  /* weapon damage needed: KF7 = 1, golden gun = 100 */
#define RD_DISMEMBER_CHANCE 40    /* % when strong enough */
#define RD_SPLIT_DAMAGE     50.0f /* torso hits only split the waist this hard */
#define RD_PUSH_HIT   525.0f
#define RD_PUSH_ALL   180.0f
#define RD_PUSH_LIFT  60.0f
#define RD_PUSH_MAXMULT 1.45f
#define RD_PUSH_SCALE 0.8f
#define RD_KILLFLOOR_Y -32000.0f
#define RD_COLROOM_MOVE 12.0f
#define RD_FAR 2000.0f

typedef struct RdSeen {
    ChrRecord *chr;
    Model *model;
    int suppressed, attempted, matrix, hitpart, hitTick, weapon, blastTick;
    float dir[3], blast[3], blastRadius, blastDamage;
} RdSeen;
static RdSeen s_seen[512];

/* Cuts are cosmetic and local: never draw from the game's own random
 * sequence, which co-op and replays depend on. */
static unsigned s_random = 0x9e3779b9u;
static int rdChance(float percent) { s_random = s_random * 1664525u + 1013904223u; return (float)((s_random >> 8) % 100) < percent; }

static float rdDamage(int weapon) { return weapon > 0 && weapon < ITEM_IDS_MAX ? gunItemGetDestructionAmount(weapon) : 0; }

/* CE ragdollGetPushSpeeds (ragdoll.c:656): damage 1 -> 1.0, 2 and above -> 2.0 */
static void rdPushSpeeds(int weapon, float *hit, float *all, float *lift)
{
    float weight = fminf(2.0f, fmaxf(0.5f, rdDamage(weapon)));
    float force = weapon > 0 && weapon < ITEM_IDS_MAX ? fmaxf(0, bondwalkItemGetForceOfImpact(weapon)) : 0;
    float mult = fminf(RD_PUSH_MAXMULT, 0.75f + 0.25f * weight + 0.025f * fminf(force, 10)) * RD_PUSH_SCALE;
    *hit = RD_PUSH_HIT * mult; *all = RD_PUSH_ALL * mult; *lift = RD_PUSH_LIFT * mult;
}

static int rdDead(ChrRecord *chr)
{
    if (!chr || !chr->prop || !chr->model) return 0;
    if (chr->prop->type == PROP_TYPE_VIEWER) {
        int p = getPlayerPointerIndex(chr->prop);
        return p >= 0 && p < getPlayerCount() && g_playerPointers[p] && g_playerPointers[p]->bonddead;
    }
    return chr->actiontype == ACT_DIE || chr->actiontype == ACT_DEAD || chr->actiontype == ACT_BONDDIE;
}

static RdSeen *rdSeen(ChrRecord *chr, int create)
{
    RdSeen *empty = NULL;
    for (int i=0;i<512;i++) {
        RdSeen *s=&s_seen[i];
        if (s->chr == chr) {
            if (s->model != chr->model) { memset(s,0,sizeof(*s)); s->chr=chr; s->model=chr->model; s->matrix=-1; }
            return s;
        }
        if (!s->chr && !empty) empty=s;
    }
    if (create && empty) { empty->chr=chr; empty->model=chr->model; empty->matrix=-1; }
    return create ? empty : NULL;
}

void *rdAlloc(RdBody *rd, size_t size)
{
    if (!size || size > 8*1024*1024) return NULL;
    RdMemory *m=calloc(1,sizeof(*m)+size);
    if (!m) return NULL;
    m->next=rd->memory;
    rd->memory=m;
    return m->bytes;
}

static void rdRelease(RdBody *rd)
{
    if (rd->used) pdphysRagdollDestroy(rd->handle);
    gevrBeanForgetWounds(&rd->model);
    /* Display lists from an earlier view can still be in flight. The frame
     * arena uses double buffering too; keep private mesh storage for 3 frames. */
    while (rd->memory) {
        RdMemory *m=rd->memory;
        rd->memory=m->next;
        m->retired=s_frame;
        m->next=s_garbage;
        s_garbage=m;
    }
    memset(rd,0,sizeof(*rd));
}

void gevrRagdollReset(void)
{
    for (int i=0;i<RD_BODIES;i++) rdRelease(&s_bodies[i]);
    memset(s_seen,0,sizeof(s_seen));
    rdWorldReset();
    pdphysShutdown();
    gevrBeanSetCuts(NULL);
    s_failed=0;
}

void gevrRagdollForgetChr(ChrRecord *chr)
{
    RdSeen *s=rdSeen(chr,0);
    if (s) memset(s,0,sizeof(*s));
}

int gevrRagdollNeedsPose(ChrRecord *chr)
{
    if (!VrRagdolls || s_failed || !chr || !chr->model || !chr->model->obj) return 0;
    RdSeen *s=rdSeen(chr,1);
    if (!s) return 0;
    if (!rdDead(chr)) { s->suppressed=s->attempted=0; return 0; }
    return !s->attempted && !s->suppressed && !(chr->chrflags & CHRFLAG_HIDDEN)
        && chr->model->obj->Skeleton == &skeleton_guard;
}

int gevrRagdollSuppress(ChrRecord *chr)
{
    if (!VrRagdolls || !rdDead(chr)) return 0;
    RdSeen *s=rdSeen(chr,0);
    return s && s->suppressed;
}

/* A first-person player is deliberately left out of chrTick's visible models.
 * Capture its death before that early return, using fresh float matrices. */
void gevrRagdollCapturePlayer(ChrRecord *chr)
{
    if(!gevrRagdollNeedsPose(chr))return;
    ModelRenderData pose={0};
    pose.basemtx=camGetWorldToScreenMtxf();
    pose.mtxlist=dynAllocate(chr->model->obj->numMatrices*sizeof(Mtxf));
    if(!pose.mtxlist)return;
    subcalcmatrices(&pose,chr->model);
    gevrRagdollCapture(chr);
}

/* CE's shotgun volley (dismember.c:1478-1580): no pellet cuts by itself; a
 * whole volley on one limb of one body can. GoldenEye fires a volley as
 * NUMBER_SHOTGUN_BULLETS calls of the fire handler (chrprop.c). */
#define RD_VOLLEY_MAX 16
static struct {
    int active, pellets, hits, pellethit, parts[RD_VOLLEY_MAX];
    ChrRecord *chr; RdBody *body; int conflict;
    float dir[3];
} s_volley;

/* A volley that killed a living guard: decided once its body exists */
static struct { ChrRecord *chr; int hits, parts[RD_VOLLEY_MAX], tick; } s_pending;

static int rdIsShotgun(int weapon) { return weapon == ITEM_SHOTGUN || weapon == ITEM_AUTOSHOT; }

static void rdVolleyHit(ChrRecord *chr, RdBody *body, int skelpart, const float dir[3])
{
    if (!s_volley.active || s_volley.pellethit) return;
    s_volley.pellethit = 1;
    if (!s_volley.hits) { s_volley.chr = chr; s_volley.body = body; }
    else if (s_volley.chr != chr || s_volley.body != body) s_volley.conflict = 1;
    if (s_volley.hits < RD_VOLLEY_MAX) s_volley.parts[s_volley.hits] = skelpart;
    s_volley.hits++;
    memcpy(s_volley.dir, dir, sizeof(s_volley.dir));
}

void gevrRagdollNoteHit(ChrRecord *chr, int matrix, int hitpart, const float dir[3], int weapon)
{
    if (!VrRagdolls || !chr || !dir || gevrRagdollSuppress(chr)) return;
    RdSeen *s=rdSeen(chr,1);
    if (!s) return;
    if(!rdDead(chr))s->suppressed=s->attempted=0;
    if(matrix<0 && s->matrix>=0 && s->hitpart==hitpart && s->hitTick==g_GlobalTimer)matrix=s->matrix;
    s->matrix=matrix; s->hitpart=hitpart; s->hitTick=g_GlobalTimer; s->weapon=weapon;
    memcpy(s->dir,dir,sizeof(s->dir));
    if (rdIsShotgun(weapon)) rdVolleyHit(chr, NULL, hitpart, dir); /* a hitpart until the body exists */
}

/* These relations live in shared definitions: relink for this instance before
 * following Child, especially HEAD (53f092b3/71b515e8). Never walk another head. */
ModelNode *rdNext(Model *model, ModelNode *node)
{
    int descend=1;
    switch (node->Opcode & 255) {
    case MODELNODE_OPCODE_HEAD: {
        ModelRwData_HeadPlaceholderRecord *h=&modelGetNodeRwData(model,node)->HeadPlaceholder;
        if (h->ModelFileHeader && h->RwDatas) modelApplyHeadRelations(model,node);
        else descend=0;
        break;
    }
    case MODELNODE_OPCODE_LOD: modelApplyDistanceRelations(model,node); break;
    case MODELNODE_OPCODE_SWITCH: modelApplyToggleRelations(model,node); break;
    case MODELNODE_OPCODE_BSP: modelApplyReorderRelations(model,node); break;
    }
    if (descend && node->Child) return node->Child;
    while (node) { if (node->Next) return node->Next; node=node->Parent; }
    return NULL;
}

static int rdParent(RdBody *rd, ModelNode *node)
{
    for (;node;node=node->Parent)
        for (int i=0;i<rd->count;i++) if (rd->nodes[i]==node) return i;
    return -1;
}

static int rdClone(RdBody *rd, ChrRecord *chr)
{
    Model *src=chr->model;
    int words=src->rwdatalen;
    if (words <= 0 || words>4096 || src->obj->numMatrices<=0 || src->obj->numMatrices>RD_MATRICES) return 0;
    rd->model=*src;
    rd->model.chr=NULL;
    rd->model.attachedto=NULL;
    rd->model.attachedto_objinst=NULL;
    rd->model.datas=rdAlloc(rd,(size_t)words*4);
    if (!rd->model.datas) return 0;
    memcpy(rd->model.datas,src->datas,(size_t)words*4);
    rd->matrices=src->obj->numMatrices;
    rd->origin=chr->prop->pos;
    rd->shade=chr->shadecol;
    memset(rd->rooms,255,sizeof(rd->rooms));
    for (int i=0;i<sizeof(chr->prop->rooms) && i<15 && chr->prop->rooms[i]!=255;i++) rd->rooms[i]=chr->prop->rooms[i];
    int visits=0, skelMatrix[RD_SKEL];
    for (int i=0;i<RD_SKEL;i++) skelMatrix[i]=rd->body[i]=-1;
    for (int i=0;i<20;i++) rd->hitskel[i]=-1;
    for (ModelNode *n=src->obj->RootNode;n;n=rdNext(&rd->model,n)) {
        if (++visits>RD_NODES) return 0;
        if ((n->Opcode&255)==MODELNODE_OPCODE_HEAD) {
            ModelRwData_HeadPlaceholderRecord *h=&modelGetNodeRwData(&rd->model,n)->HeadPlaceholder;
            if (h->RwDatas) {
                uintptr_t base=(uintptr_t)src->datas, ptr=(uintptr_t)h->RwDatas;
                if (ptr<base || ptr-base >= (size_t)words*4 || !h->ModelFileHeader
                    || (size_t)h->ModelFileHeader->numRecords*4 > (size_t)words*4-(ptr-base)) return 0;
                h->RwDatas=(u8 *)rd->model.datas+(ptr-base);
            }
        }
        /* CE ragdollCapturePose (create.c:17): skeleton parts by joint
         * number; hands and feet have no body and ride their parent's. */
        if ((n->Opcode&255)==MODELNODE_OPCODE_GROUP) {
            int part=n->Data->Group.JointID-1;
            if (part<0 || part>=RD_SKEL || skelMatrix[part]>=0) continue;
            skelMatrix[part]=n->Data->Group.MatrixID0;
            if (skelMatrix[part]<0 || skelMatrix[part]>=rd->matrices) return 0;
            if (!g_RdSkelHuman.hasbody[part]) continue;
            if (rd->count==RD_PARTS) return 0;
            int i=rd->count++;
            rd->nodes[i]=n;
            rd->primary[i]=skelMatrix[part];
            rd->joint[i]=part+1;
            rd->skel[i]=part;
            rd->body[part]=i;
            rd->parent[i]=rdParent(rd,n->Parent);
            if (rd->parent[i]<0) { if(i) return 0; rd->root=i; }
        }
        /* BBOX ModelNumber is the hitpart (chr.c chrTestHit) */
        if ((n->Opcode&255)==MODELNODE_OPCODE_BBOX && n->Data) {
            int hitpart=n->Data->BoundingBox.ModelNumber;
            for (ModelNode *a=n->Parent;a && hitpart>=0 && hitpart<20;a=a->Parent)
                if ((a->Opcode&255)==MODELNODE_OPCODE_GROUP && a->Data->Group.JointID>=1 && a->Data->Group.JointID<=RD_SKEL) {
                    rd->hitskel[hitpart]=a->Data->Group.JointID-1;
                    break;
                }
        }
    }
    /* The same layout check as CE's: any other rig keeps its death animation */
    int ok=rd->count==11 && rd->root==0 && rd->skel[0]==0;
    for (int part=0;ok && part<RD_SKEL;part++) {
        ok=skelMatrix[part]>=0;
        if (ok && g_RdSkelHuman.hasbody[part] && part)
            ok=rd->body[part]>=0 && rd->parent[rd->body[part]]==rd->body[g_RdSkelHuman.parent[part]];
    }
    if (!ok) {
        if (!s_warned++) sysLogPrintf(LOG_WARNING,"ragdoll: chr %d has an unexpected skeleton layout, using its death animation",chr->chrnum);
        return 0;
    }
    for(int i=0;i<rd->matrices;i++) {
        rdMul(&src->render_pos[i].pos,currentPlayerGetViewToWorldMtxf(),&rd->bind[i]);
        for(int k=0;k<16;k++) if(!isfinite(((float *)&rd->bind[i])[k])) return 0;
        rd->owner[i]=rd->root;
    }
    visits=0;
    for(ModelNode *n=rd->model.obj->RootNode;n;n=rdNext(&rd->model,n)) {
        if(++visits>RD_NODES) return 0;
        int p=rdParent(rd,n);
        if(p<0)p=rd->root;
        int op=n->Opcode&255;
        if(op==MODELNODE_OPCODE_GROUP) {
            for(int k=0;k<3;k++) { int m=n->Data->Group.MatrixIDs[k]; if(m>=0 && m<rd->matrices)rd->owner[m]=p; }
        } else if(op==MODELNODE_OPCODE_GROUPSIMPLE || op==MODELNODE_OPCODE_HEADER) {
            int m=modelFindNodeMtxIndex(n,0); if(m>=0 && m<rd->matrices)rd->owner[m]=p;
        }
    }
    /* Normalized joint frames for Jolt; retain all model scaling and half-bend
     * matrices in offsets so native, HD and Bean all use the same pose. */
    for(int part=0;part<RD_SKEL;part++) {
        Mtxf m=rd->bind[skelMatrix[part]];
        for(int r=0;r<3;r++) {
            float l=sqrtf(m.m[r][0]*m.m[r][0]+m.m[r][1]*m.m[r][1]+m.m[r][2]*m.m[r][2]);
            if(!isfinite(l) || l<1e-6f)return 0;
            for(int k=0;k<3;k++)m.m[r][k]/=l;
        }
        m.m[0][3]=m.m[1][3]=m.m[2][3]=0; m.m[3][3]=1;
        rd->skelInitial[part]=m;
        if(rd->body[part]>=0)rd->initial[rd->body[part]]=rd->joints[rd->body[part]]=m;
    }
    for(int i=0;i<rd->matrices;i++) {
        Mtxf inv;
        if(!rdInverse(&rd->initial[rd->owner[i]],&inv))return 0;
        rdMul(&rd->bind[i],&inv,&rd->offset[i]);
    }
    return rdCopyMeshes(rd);
}

/* CE rdHitpartPart (rd_internal.h:683): the skeleton part with a physics
 * body behind a hitpart (hands -> forearms, feet -> shins), or -1 */
static int rdSkelForHit(const RdBody *rd, int hitpart)
{
    int sp=hitpart>=0 && hitpart<20 ? rd->hitskel[hitpart] : -1;
    while(sp>0 && rd->body[sp]<0)sp=g_RdSkelHuman.parent[sp];
    return sp;
}

/* The body part a recent hit landed on: the hit matrix when the shot found
 * one (BulletHit.room carries the matrix index for model hits), else its hitpart */
static int rdHitPart(RdBody *rd, RdSeen *s)
{
    if (!s || g_GlobalTimer-s->hitTick>120) return -1;
    if(s->matrix>=0 && s->matrix<rd->matrices)return rd->owner[s->matrix];
    int sp=rdSkelForHit(rd,s->hitpart);
    return sp>=0 ? rd->body[sp] : -1;
}

/* CE ragdollCutPartForHit (dismember.c:1268) and the weapon gate of
 * ragdollTryDismemberShot (:1375): strong enough weapons, a chance, no
 * punches; the shotgun cuts only by whole volleys. Returns a body part. */
static int rdCutForHit(const RdBody *rd, int part, int weapon)
{
    float damage=rdDamage(weapon);
    if(!VrDismemberment || part<0 || part>=rd->count || weapon==ITEM_FIST || rdIsShotgun(weapon)
        || damage<RD_DISMEMBER_DAMAGE || !rdChance(RD_DISMEMBER_CHANCE))return -1;
    if(rd->skel[part]<=1)return damage>=RD_SPLIT_DAMAGE ? rd->body[1] : -1; /* the waist */
    return part;
}

/* CE ragdollShotgunVolleyEnd's limb rule over skeleton parts: every pellet
 * on one limb, the cut nearest the body. */
static int rdVolleyCut(const RdBody *rd, const int *parts, int hits)
{
    int target=-1;
    for(int i=0;i<hits;i++) {
        int sp=parts[i],a;
        if(sp<=1)return -1;
        if(target<0){target=sp;continue;}
        for(a=sp;a>0 && a!=target;a=g_RdSkelHuman.parent[a]){}
        if(a==target)continue;
        for(a=target;a>0 && a!=sp;a=g_RdSkelHuman.parent[a]){}
        if(a!=sp)return -1;
        target=sp;
    }
    return target>1 && VrDismemberment && rdChance(RD_DISMEMBER_CHANCE) ? rd->body[target] : -1;
}

static void rdLog(int level, const char *msg) { sysLogPrintf(level==PDPHYS_LOG_ERROR?LOG_ERROR:level==PDPHYS_LOG_WARNING?LOG_WARNING:LOG_NOTE,"%s",msg); }

static void rdRooms(RdBody *body)
{
    int rooms[64],count=0;
    unsigned char include[MAXROOMCOUNT]={0};
    for(int i=0;i<15 && body->rooms[i]!=255;i++)
        if(body->rooms[i]<MAXROOMCOUNT)include[body->rooms[i]]=1;
    if(g_BgPortals)for(int part=0;part<body->count;part++) {
        if(part!=body->root && !(body->cuts&(1u<<part)))continue;
        coord3d to; memcpy(to.f,body->joints[part].m[3],sizeof(to));
        u8 final[16]; s32 crossed[16],n=0;
        bgFindRoomsAlongSegment(&body->origin,&to,body->rooms,final,crossed,&n,15);
        for(int j=0;j<n;j++)if(crossed[j]>=0 && crossed[j]<MAXROOMCOUNT)include[crossed[j]]=1;
    }
    /* Include immediate neighbours so an attached arm can cross a doorway
     * before the pelvis does. Do not collide with unrelated stacked rooms. */
    unsigned char nearby[MAXROOMCOUNT]; memcpy(nearby,include,sizeof(nearby));
    if(g_BgPortals)for(int i=0;i<PORTMAX && g_BgPortals[i].offset_portal;i++) {
        int a=g_BgPortals[i].connectedRoom1,b=g_BgPortals[i].connectedRoom2;
        if(a<MAXROOMCOUNT && b<MAXROOMCOUNT) { if(include[a])nearby[b]=1; if(include[b])nearby[a]=1; }
    }
    for(int i=0;i<MAXROOMCOUNT && count<64;i++)if(nearby[i])rooms[count++]=i;
    pdphysRagdollSetRooms(body->handle,rooms,count?count:-1);
    /* The rooms its pieces are in decide whether it is drawn */
    int nvis=0;
    memset(body->visrooms,255,sizeof(body->visrooms));
    for(int i=0;i<MAXROOMCOUNT && nvis<15;i++)if(include[i])body->visrooms[nvis++]=i;
}

/* CE ragdollIsAnyPieceOnScreen (rooms.c:460): drawn when one of its rooms
 * is being drawn and any part is inside the whole view (not the rooms'
 * screen boxes, which shrink to the gap of a moving door); the depth buffer
 * hides what is behind walls. posIsOnScreen's tests, per part. */
static int rdOnScreen(RdBody *b)
{
    int shown=0;
    for(int i=0;i<16 && b->visrooms[i]!=255 && !shown;i++)shown=getROOMID_isRendered(b->visrooms[i])!=0;
    if(!shown)return 0;
    const float size=getinstsize(&b->model)*0.5f;
    for(int i=0;i<b->count;i++) {
        coord3d pos; memcpy(pos.f,b->joints[i].m[3],sizeof(pos.f));
        if(fogPositionIsVisibleThroughFog(&pos,size) && camIsPosInScreen(&pos,size))return 1;
    }
    return 0;
}

/* CE ragdollStageLoad (ragdoll.c:97): the backend and the floors are made
 * when the stage starts (its first tick), not by the first death. */
static int rdStart(void)
{
    if(pdphysIsReady())return 1;
    if(s_failed || !VrRagdolls || lvlGetCurrentStageToLoad()==LEVELID_TITLE)return 0;
    if(!pdphysInit(rdLog)) { s_failed=1; return 0; }
    const GevrBeanCuts cuts={gevrRagdollModelCut,gevrRagdollPiece,gevrRagdollRemap};
    gevrBeanSetCuts(&cuts);
    rdWorldInit();
    return 1;
}

/* CE rdTorsoLength (create.c:251): waist to neck; sizes and masses scale with it */
static float rdTorsoLength(const RdBody *rd)
{
    float l=0;
    for(int k=0;k<3;k++){float x=rd->skelInitial[2].m[3][k]-rd->skelInitial[1].m[3][k];l+=x*x;}
    l=sqrtf(l);
    return l<5 ? 40 : l;
}

static void rdBones(const RdBody *rd, float p[RD_SKEL][3], float bone[RD_SKEL][3])
{
    const RdSkel *sk=&g_RdSkelHuman;
    for(int i=0;i<RD_SKEL;i++)memcpy(p[i],rd->skelInitial[i].m[3],sizeof(p[i]));
    for(int i=0;i<RD_SKEL;i++) {
        const float *from=p[i],*to=p[i];
        if(sk->child[i]>=0) to=p[sk->child[i]];
        else if(i==2) { from=p[sk->parent[2]]; to=p[2]; } /* the head points along the spine */
        else if(i==0) to=p[2];
        else { from=p[sk->parent[i]]; to=p[i]; }
        float l=0;
        for(int k=0;k<3;k++){bone[i][k]=to[k]-from[k];l+=bone[i][k]*bone[i][k];}
        l=sqrtf(l);
        for(int k=0;k<3;k++)bone[i][k]=l>1e-6f?bone[i][k]/l:(k==1);
    }
}

/* The death-pose bend of a hinge, measured as CE does (create.c:617) */
static float rdHingeAngle(const RdBody *rd, int part)
{
    float p[RD_SKEL][3],bone[RD_SKEL][3],cross[3];
    rdBones(rd,p,bone);
    const float *n1=bone[g_RdSkelHuman.parent[part]],*n2=bone[part],*y=rd->skelInitial[part].m[1];
    cross[0]=n1[1]*n2[2]-n1[2]*n2[1]; cross[1]=n1[2]*n2[0]-n1[0]*n2[2]; cross[2]=n1[0]*n2[1]-n1[1]*n2[0];
    float angle=atan2f(cross[0]*y[0]+cross[1]*y[1]+cross[2]*y[2],n1[0]*n2[0]+n1[1]*n2[1]+n1[2]*n2[2]);
    /* GoldenEye's atan2f replaces libm's in the whole library and returns 0..2pi */
    return angle>(float)M_PI ? angle-2*(float)M_PI : angle;
}

/* CE's human hinges turn about each joint's local Y (create.c:593). PD's
 * animated frames put that axis across the limb; log and check GoldenEye's. */
static int rdHingeAlongBone(const RdBody *rd, int part, float bone[RD_SKEL][3])
{
    const float *y=rd->skelInitial[part].m[1];
    return fabsf(y[0]*bone[part][0]+y[1]*bone[part][1]+y[2]*bone[part][2])>0.7f;
}

static void rdCapsule(pdphysRagdollPart *pp, const float a[3], const float b[3], float radius)
{
    pp->shape=PDPHYS_SHAPE_CAPSULE; memcpy(pp->a,a,sizeof(pp->a)); memcpy(pp->b,b,sizeof(pp->b)); pp->radius=radius;
}
static void rdSwingTwist(pdphysRagdollPart *pp, const float twist[3], const float plane[3],
                         float normalcone, float planecone, float twistmin, float twistmax)
{
    pp->joint=PDPHYS_JOINT_SWINGTWIST; memcpy(pp->axis,twist,sizeof(pp->axis)); memcpy(pp->axis2,plane,sizeof(pp->axis2));
    pp->limits[0]=normalcone; pp->limits[1]=planecone; pp->limits[2]=twistmin; pp->limits[3]=twistmax;
}
static void rdMad(const float a[3], const float d[3], float s, float out[3]) { for(int k=0;k<3;k++)out[k]=a[k]+d[k]*s; }

/* CE ragdollCreatePhysics (create.c:455), human branch: shapes, masses and
 * joints from the death pose, scaled by the torso length */
static void rdBuildParts(RdBody *rd, pdphysRagdollPart *parts, const float vel[3])
{
    const RdSkel *sk=&g_RdSkelHuman;
    float p[RD_SKEL][3],bone[RD_SKEL][3],a[3],b[3];
    const float torso=rd->torso,sc=torso/RD_MASS_REFTORSO;
    rdBones(rd,p,bone);
    for(int i=0;i<rd->count;i++) {
        const int j=rd->skel[i];
        pdphysRagdollPart *pp=&parts[i];
        const float *localy=rd->skelInitial[j].m[1],*localz=rd->skelInitial[j].m[2];
        memset(pp,0,sizeof(*pp));
        pp->parent=i==rd->root ? -1 : rd->parent[i];
        memcpy(pp->jointmtx,&rd->skelInitial[j],sizeof(pp->jointmtx));
        memcpy(pp->linvel,vel,sizeof(pp->linvel));
        pp->mass=sk->mass[j]*sc*sc*sc;
        switch(j) {
        case 0: /* pelvis: across the hips */
            rdCapsule(pp,p[9],p[10],torso*0.25f);
            pp->joint=PDPHYS_JOINT_NONE;
            break;
        case 1: /* chest; the lower back bends ~35 degrees, sideways ~25, twists ~25 */
            rdMad(p[1],bone[1],torso*0.30f,a); rdMad(p[2],bone[1],-torso*0.15f,b);
            rdCapsule(pp,a,b,torso*0.25f);
            rdSwingTwist(pp,bone[1],localz,0.6f,0.45f,-0.45f,0.45f);
            break;
        case 2: /* head, up the spine from the neck joint */
            pp->shape=PDPHYS_SHAPE_SPHERE;
            rdMad(p[2],bone[1],torso*0.30f,pp->a);
            pp->radius=torso*0.26f;
            rdSwingTwist(pp,bone[1],localz,0.85f,0.8f,-1.0f,1.0f);
            break;
        case 3: case 4: /* upper arms */
            rdMad(p[j],bone[j],torso*0.11f,a);
            rdCapsule(pp,a,p[sk->child[j]],torso*0.12f);
            rdSwingTwist(pp,bone[j],localy,1.5f,1.3f,-0.9f,0.9f);
            break;
        case 5: case 6: /* forearms and hands */
            rdMad(p[sk->child[j]],bone[j],torso*0.18f,b);
            rdCapsule(pp,p[j],b,torso*0.10f);
            break;
        case 9: case 10: /* thighs: hips swing ~70 degrees forward and back, ~30 sideways */
            rdCapsule(pp,p[j],p[sk->child[j]],torso*0.17f);
            rdSwingTwist(pp,bone[j],localy,1.2f,0.5f,-0.5f,0.5f);
            break;
        case 11: case 12: { /* shins, with the foot from heel to toes */
            rdCapsule(pp,p[j],p[sk->child[j]],torso*0.14f);
            const int foot=sk->child[j];
            const float *fwd=rd->skelInitial[foot].m[2],*up=rd->skelInitial[foot].m[1];
            for(int k=0;k<3;k++) {
                pp->foota[k]=p[foot][k]-up[k]*torso*0.10f-fwd[k]*torso*0.04f;
                pp->footb[k]=p[foot][k]-up[k]*torso*0.12f+fwd[k]*torso*0.36f;
            }
            pp->footradius=torso*0.07f;
            break;
        }
        }
        rd->radius[i]=pp->radius;
        /* Elbows and knees: hinges about the joint's local Y, one way only,
         * widened to hold the death pose but never ~15 degrees backwards */
        if(sk->hingesign[j] && rdHingeAlongBone(rd,j,bone)) {
            /* The joint's Y runs along the bone, so it can't be the hinge:
             * CE's own rule for rigs without known hinge axes
             * (ragdollShapeFromHitboxes, create.c:409) - bend the way it is
             * bent now, about the axis it is bent around */
            const float *n1=bone[sk->parent[j]],*n2=bone[j];
            float cross[3]={n1[1]*n2[2]-n1[2]*n2[1],n1[2]*n2[0]-n1[0]*n2[2],n1[0]*n2[1]-n1[1]*n2[0]};
            const float sinang=sqrtf(cross[0]*cross[0]+cross[1]*cross[1]+cross[2]*cross[2]);
            pp->joint=PDPHYS_JOINT_HINGE;
            memcpy(pp->axis2,n1,sizeof(pp->axis2));
            memcpy(pp->axis3,n2,sizeof(pp->axis3));
            if(sinang>0.15f) {
                for(int k=0;k<3;k++)pp->axis[k]=cross[k]/sinang;
                const float angle=atan2f(sinang,n1[0]*n2[0]+n1[1]*n2[1]+n1[2]*n2[2]);
                pp->limits[0]=-0.1f;
                pp->limits[1]=angle+0.05f>2.4f ? angle+0.05f : 2.4f;
            } else {
                memcpy(pp->axis,localy,sizeof(pp->axis));
                pp->limits[0]=-1.2f; pp->limits[1]=1.2f;
            }
        } else if(sk->hingesign[j]) {
            const float maxbend=(j==5 || j==6) ? 2.55f : 2.45f;
            const float angle=rdHingeAngle(rd,j);
            pp->joint=PDPHYS_JOINT_HINGE;
            memcpy(pp->axis,localy,sizeof(pp->axis));
            memcpy(pp->axis2,bone[sk->parent[j]],sizeof(pp->axis2));
            memcpy(pp->axis3,bone[j],sizeof(pp->axis3));
            if(sk->hingesign[j]>0) { pp->limits[0]=-0.05f; pp->limits[1]=maxbend; }
            else { pp->limits[0]=-maxbend; pp->limits[1]=0.05f; }
            if(angle<pp->limits[0]) {
                pp->limits[0]=angle-0.05f;
                if(sk->hingesign[j]>0 && pp->limits[0]<-0.25f)pp->limits[0]=-0.25f;
            }
            if(angle>pp->limits[1]) {
                pp->limits[1]=angle+0.05f;
                if(sk->hingesign[j]<0 && pp->limits[1]>0.25f)pp->limits[1]=0.25f;
            }
        }
    }
    /* Cut joints stay cut */
    for(int i=0;i<rd->count;i++)if(rd->cuts&(1u<<i))parts[i].joint=PDPHYS_JOINT_NONE;
}

/* CE ragdollOnExplosion (ragdoll.c:1318) and ragdollTryDismemberBlast
 * (dismember.c:1593): joints close to the blast can come off (up to two),
 * and every part is thrown away from the centre and up */
static void rdBlastBody(RdBody *b, const float centre[3], float radius, float damage)
{
    static const signed char joints[]={2,3,4,5,6,9,10,11,12,1};
    if(radius<=0)return;
    for(int i=0,cut=0;VrDismemberment && i<(int)sizeof(joints) && cut<2;i++) {
        const int part=joints[i],body=b->body[part];
        if(body<0 || (b->cuts&(1u<<body)))continue;
        float dist=0,reach=part==1 ? radius*0.35f : radius*0.6f;
        for(int k=0;k<3;k++){float x=b->joints[body].m[3][k]-centre[k];dist+=x*x;}
        dist=sqrtf(dist);
        if(dist<reach && rdChance(RD_DISMEMBER_CHANCE*(1-dist/reach)) && rdCut(b,body))cut++;
    }
    const float strength=fminf(2,fmaxf(0.5f,damage))*RD_PUSH_SCALE;
    for(int i=0;i<b->count;i++) {
        float dir[3],dist=0;
        for(int k=0;k<3;k++){dir[k]=b->joints[i].m[3][k]-centre[k];dist+=dir[k]*dir[k];}
        dist=sqrtf(dist);
        if(dist<1){dir[0]=dir[2]=0;dir[1]=1;dist=1;}
        else for(int k=0;k<3;k++)dir[k]/=dist;
        const float falloff=fmaxf(0.15f,1-dist/(radius*1.5f));
        const float speed=fminf(1100,700*strength*falloff);
        const float v[3]={dir[0]*speed,dir[1]*speed*0.5f+speed*0.45f,dir[2]*speed};
        pdphysRagdollAddVelocity(b->handle,i,v);
    }
    pdphysRagdollKeepAwake(b->handle,0.2f);
    b->asleep=0;
}

/* explosion.c, every chr a damage pulse reaches: remembered for the body
 * its death makes on the next tick */
void gevrRagdollExplosionChr(ChrRecord *chr, const coord3d *centre, float radius, float damage)
{
    if(!VrRagdolls || !chr || !centre || gevrRagdollSuppress(chr))return;
    RdSeen *s=rdSeen(chr,1);
    if(!s)return;
    s->blastTick=g_GlobalTimer?g_GlobalTimer:1;
    memcpy(s->blast,centre->f,sizeof(s->blast));
    s->blastRadius=radius; s->blastDamage=damage;
}

/* explosion.c, an explosion's first damage pulse: bodies already lying
 * within its reach are thrown and cut */
void gevrRagdollBlast(const coord3d *centre, float radius, float damage)
{
    if(!VrRagdolls || !pdphysIsReady() || !centre || radius<=0)return;
    for(int i=0;i<RD_BODIES;i++) {
        RdBody *b=&s_bodies[i];
        if(!b->used)continue;
        float dist=0;
        for(int k=0;k<3;k++){float x=b->joints[b->root].m[3][k]-centre->f[k];dist+=x*x;}
        if(dist<radius*radius*2.25f)rdBlastBody(b,centre->f,radius,damage);
    }
}

void gevrRagdollCapture(ChrRecord *chr)
{
    if(!gevrRagdollNeedsPose(chr) || !chr->model->render_pos)return;
    RdSeen *seen=rdSeen(chr,0);
    if(seen)seen->attempted=1;
    if(!rdStart())return;
    int limit=VrBodiesStay>0 ? VrBodiesStay : RD_BODIES;
    if(limit>RD_BODIES)limit=RD_BODIES;
    int count=0; RdBody *slot=NULL,*oldest=NULL;
    for(int i=0;i<RD_BODIES;i++) {
        RdBody *b=&s_bodies[i];
        if(!b->used) { if(!slot)slot=b; continue; }
        count++; if(!oldest || b->serial<oldest->serial)oldest=b;
    }
    if(count>=limit || !slot) { slot=oldest; rdRelease(slot); }
    if(!slot || !rdClone(slot,chr)) { if(slot)rdRelease(slot); return; }
    float velocity[3]={0};
    /* The native explosion death already supplies a launch velocity in game
     * units per tick. Preserve it, with a bounded conversion to units/second.
     * (CE also carries the walking speed; GoldenEye keeps no previous root.) */
    const int blasted=seen && seen->blastTick && g_GlobalTimer-seen->blastTick<=60
        && (g_GlobalTimer-seen->hitTick>120 || seen->blastTick>=seen->hitTick);
    float fall=0;
    for(int k=0;k<3;k++)fall+=chr->fallspeed.f[k]*chr->fallspeed.f[k];
    if(!blasted && fall>1 && isfinite(fall)) {
        float factor=fminf(60,600/sqrtf(fall));
        for(int k=0;k<3;k++)velocity[k]=chr->fallspeed.f[k]*factor;
    }
    slot->torso=rdTorsoLength(slot);
    pdphysRagdollPart parts[RD_PARTS];
    rdBuildParts(slot,parts,velocity);
    slot->handle=pdphysRagdollCreate(parts,slot->count);
    if(slot->handle<0){rdRelease(slot);return;}
    slot->used=1; slot->serial=++s_serial; slot->alpha=255;
    int hit=blasted ? -1 : rdHitPart(slot,seen),weapon=!blasted && seen && g_GlobalTimer-seen->hitTick<=120 ? seen->weapon : 0;
    /* CE ragdollOnChrDeath (ragdoll.c:1063): a strong push on the part that
     * was hit, a smaller push and a little lift on the whole body */
    if(weapon && seen) {
        float l=sqrtf(seen->dir[0]*seen->dir[0]+seen->dir[1]*seen->dir[1]+seen->dir[2]*seen->dir[2]);
        float hitspeed,allspeed,liftspeed,push[3],all[3];
        rdPushSpeeds(weapon,&hitspeed,&allspeed,&liftspeed);
        if(l>1e-6f) {
            for(int k=0;k<3;k++){push[k]=seen->dir[k]/l*hitspeed;all[k]=seen->dir[k]/l*allspeed;}
            all[1]+=liftspeed;
            pdphysRagdollAddVelocity(slot->handle,-1,all);
            if(hit>=0)pdphysRagdollAddVelocity(slot->handle,hit,push);
        }
    }
    int cut=rdCutForHit(slot,hit,weapon);
    if(s_pending.chr==chr && g_GlobalTimer-s_pending.tick<=120) {
        int volley[RD_VOLLEY_MAX];
        for(int i=0;i<s_pending.hits;i++)volley[i]=rdSkelForHit(slot,s_pending.parts[i]);
        cut=rdVolleyCut(slot,volley,s_pending.hits);
        s_pending.chr=NULL;
    }
    if(cut>=0)rdCut(slot,cut);
    if(blasted)rdBlastBody(slot,seen->blast,seen->blastRadius,seen->blastDamage);
    rdRooms(slot);
    slot->onscreen=rdOnScreen(slot);
    if(seen)seen->suppressed=1;
    float p[RD_SKEL][3],bone[RD_SKEL][3];
    rdBones(slot,p,bone);
    sysLogPrintf(LOG_NOTE,"ragdoll: body %u, %d parts, %d meshes, torso %.1f, hit part %d, weapon %d (damage %.1f), blast %d, cuts 0x%x,"
        " knees %.2f %.2f, elbows %.2f %.2f, hinge Y along bone %d%d%d%d",
        slot->serial,slot->count,slot->nmeshes,slot->torso,hit,weapon,rdDamage(weapon),blasted,slot->cuts,
        rdHingeAngle(slot,11),rdHingeAngle(slot,12),rdHingeAngle(slot,5),rdHingeAngle(slot,6),
        rdHingeAlongBone(slot,11,bone),rdHingeAlongBone(slot,12,bone),rdHingeAlongBone(slot,5,bone),rdHingeAlongBone(slot,6,bone));
}

static float rdCameraDistance2(const float pos[3])
{
    const Mtxf *eye=currentPlayerGetViewToWorldMtxf();
    float d=0;
    for(int k=0;k<3;k++){float x=pos[k]-eye->m[3][k];d+=x*x;}
    return d;
}

void gevrRagdollTick(int ticks60)
{
    s_frame++;
    RdMemory **m=&s_garbage;
    while(*m) {
        if(s_frame-(*m)->retired>=3) { RdMemory *old=*m; *m=old->next; free(old); }
        else m=&(*m)->next;
    }
    if(!VrRagdolls) {
        if(pdphysIsReady())gevrRagdollReset();
        return;
    }
    if(!rdStart())return;
    /* Culled with the game's tick-time view, as chrTick culls guards; the
     * extra eye and barrel passes draw the same set */
    for(int i=0;i<RD_BODIES;i++)if(s_bodies[i].used)s_bodies[i].onscreen=rdOnScreen(&s_bodies[i]);
    if(ticks60<=0)return;
    float dt=fminf(ticks60/60.0f,0.1f);
    int limit=VrBodiesStay>0?VrBodiesStay:RD_BODIES;
    if(limit>RD_BODIES)limit=RD_BODIES;
    for(;;) {
        RdBody *old=NULL; int count=0;
        for(int i=0;i<RD_BODIES;i++)if(s_bodies[i].used) {
            count++; if(!old || s_bodies[i].serial<old->serial)old=&s_bodies[i];
        }
        if(count<=limit)break;
        rdRelease(old);
    }
    rdWorldUpdate();
    rdMoversUpdate(dt,s_bodies,RD_BODIES);
    /* CE ragdollTick (ragdoll.c:1645): collision rooms every tick near the
     * camera or after the pelvis moves 12 units, else every 4th tick (staggered) */
    for(int i=0;i<RD_BODIES;i++) {
        RdBody *b=&s_bodies[i];
        if(!b->used || (b->asleep && pdphysRagdollIsAsleep(b->handle)))continue;
        float moved=0;
        for(int k=0;k<3;k++){float x=b->joints[b->root].m[3][k]-b->colroompos[k];moved+=x*x;}
        if(rdCameraDistance2(b->joints[b->root].m[3])<RD_FAR*RD_FAR || moved>RD_COLROOM_MOVE*RD_COLROOM_MOVE
            || ((s_frame/2)+i)%4==0) {
            rdRooms(b);
            memcpy(b->colroompos,b->joints[b->root].m[3],sizeof(b->colroompos));
        }
    }
    pdphysStep(dt);
    for(int i=0;i<RD_BODIES;i++) {
        RdBody *b=&s_bodies[i]; if(!b->used)continue;
        b->age+=dt;
        /* Still asleep after the step: the pose has not changed (CE :1691) */
        const int asleep=pdphysRagdollIsAsleep(b->handle);
        if(!(b->asleep && asleep))pdphysRagdollGetJoints(b->handle,(float (*)[16])b->joints);
        b->asleep=asleep;
        /* Fell out of the level (CE :1708) */
        if(b->joints[b->root].m[3][1]<RD_KILLFLOOR_Y) {
            sysLogPrintf(LOG_NOTE,"ragdoll: body %u fell out of the level, removing it",b->serial);
            rdRelease(b);
            continue;
        }
        if(!VrBodiesStay) {
            b->alpha=fmaxf(0,fminf(255,(5.5f-b->age)*170));
            if(b->alpha<=0)rdRelease(b);
        }
    }
}

/* The piece of body part 'part' and everything joined to it */
static void rdPushPiece(RdBody *b, int part, const float v[3])
{
    if(!b->cuts) { pdphysRagdollAddVelocity(b->handle,-1,v); return; }
    for(int i=0;i<b->count;i++)if(rdPiece(b,i)==rdPiece(b,part))pdphysRagdollAddVelocity(b->handle,i,v);
}

void gevrRagdollShot(ShotData *shot)
{
    if(s_volley.active){s_volley.pellets++;s_volley.pellethit=0;}
    if(!VrRagdolls || !pdphysIsReady() || !shot || shot->viewDir.z>=-1e-6f)return;
    float depth=shot->maxdist;
    for(int i=0;i<10;i++)if(shot->hits[i].prop && shot->hits[i].dist<depth)depth=shot->hits[i].dist;
    float distance=fminf(65535,(depth+shot->viewOrigin.z)/-shot->viewDir.z);
    if(distance<=0)return;
    float to[3],hit[3]; int handle,part;
    for(int k=0;k<3;k++)to[k]=shot->gunpos.f[k]+shot->dir.f[k]*distance;
    if(!pdphysRaycastRagdoll(shot->gunpos.f,to,0,hit,&handle,&part))return;
    for(int i=0;i<RD_BODIES;i++)if(s_bodies[i].used && s_bodies[i].handle==handle) {
        RdBody *b=&s_bodies[i];
        if(part<0 || part>=b->count)return;
        if(rdIsShotgun(shot->weapon))rdVolleyHit(NULL,b,b->skel[part],shot->dir.f);
        /* CE ragdollOnChrShot (ragdoll.c:858): automatic weapons push a body
         * at most every 4 ticks; nothing builds up over repeated shots */
        if(b->lastpush && g_GlobalTimer-b->lastpush<4)return;
        b->lastpush=g_GlobalTimer?g_GlobalTimer:1;
        float hitspeed,allspeed,liftspeed,push[3],all[3];
        rdPushSpeeds(shot->weapon,&hitspeed,&allspeed,&liftspeed);
        for(int k=0;k<3;k++){push[k]=shot->dir.f[k]*hitspeed*0.45f;all[k]=shot->dir.f[k]*allspeed*0.15f;}
        rdPushPiece(b,part,all);
        pdphysRagdollAddVelocity(handle,part,push);
        pdphysRagdollKeepAwake(handle,0.2f);
        b->asleep=0;
        const int cut=rdCutForHit(b,part,shot->weapon);
        if(cut>=0)rdCut(b,cut);
        return;
    }
}

/* chrprop.c's shotgun branch: a volley of pellets, each one call of the fire
 * handler (CE ragdollShotgunVolleyBegin/End) */
void gevrRagdollVolley(int begin)
{
    if(begin) { memset(&s_volley,0,sizeof(s_volley)); s_volley.active=VrRagdolls; return; }
    if(!s_volley.active)return;
    s_volley.active=0;
    if(!VrDismemberment || s_volley.conflict || s_volley.pellets<=0 || s_volley.hits!=s_volley.pellets
        || s_volley.hits>RD_VOLLEY_MAX)return;
    if(s_volley.body) {
        RdBody *b=s_volley.body;
        if(!b->used)return;
        const int cut=rdVolleyCut(b,s_volley.parts,s_volley.hits);
        if(cut>=0)rdCut(b,cut);
    } else if(s_volley.chr && rdDead(s_volley.chr)) {
        /* Its body is made by the next chrTick */
        s_pending.chr=s_volley.chr; s_pending.hits=s_volley.hits; s_pending.tick=g_GlobalTimer;
        memcpy(s_pending.parts,s_volley.parts,sizeof(s_pending.parts));
    }
}

static RdBody *rdModel(const Model *model)
{
    for(int i=0;i<RD_BODIES;i++)if(s_bodies[i].used && &s_bodies[i].model==model)return &s_bodies[i];
    return NULL;
}
int gevrRagdollModelCut(const Model *model) { RdBody *b=rdModel(model); return b ? (int)b->cuts : 0; }
int gevrRagdollPiece(const Model *model,int matrix)
{
    RdBody *b=rdModel(model);
    return b && matrix>=0 && matrix<b->matrices ? rdPiece(b,b->owner[matrix]) : 0;
}
int gevrRagdollRemap(const Model *model,int owner,int matrix,float p[3],float n[3])
{
    RdBody *b=rdModel(model);
    if(!b || !b->cuts || owner<0 || owner>=b->matrices || matrix<0 || matrix>=b->matrices)return matrix;
    if(rdPiece(b,b->owner[owner])==rdPiece(b,b->owner[matrix]))return matrix;
    Mtxf inv,map;
    if(!rdInverse(&b->bind[owner],&inv))return matrix;
    rdMul(&b->bind[matrix],&inv,&map);
    rdTransform(&map,p,1); rdTransform(&map,n,0);
    return owner;
}

Gfx *gevrRagdollRender(Gfx *gdl)
{
    if(!VrRagdolls || !pdphysIsReady())return gdl;
    gdl=bgScissorCurrentPlayerViewDefault(gdl);
    sub_GAME_7F073FC8(0); /* the native upright shadow does not follow a falling rig */
    for(int i=0;i<RD_BODIES;i++) {
        RdBody *b=&s_bodies[i]; if(!b->used)continue;
        if(!b->onscreen)continue;
        b->model.render_pos=dynAllocate(b->matrices*sizeof(RenderPosView));
        if(!b->model.render_pos)continue;
        for(int j=0;j<b->matrices;j++) {
            Mtxf world;
            rdMul(&b->offset[j],&b->joints[b->owner[j]],&world);
            rdMul(&world,camGetWorldToScreenMtxf(),&b->model.render_pos[j].pos);
        }
        ModelRenderData render=D_8002CCBC;
        render.gdl=gdl; render.flags=3;
        render.PropType=b->alpha<255?8:7;
        render.envcolour.word=0x5a000000u|(b->alpha<255?(u8)b->alpha:0);
        render.fogcolour=b->shade;
        subdraw(&render,&b->model);
        gdl=rdDrawCaps(b,render.gdl);
        bondviewTransformManyPosToViewMatrix(b->model.render_pos,b->matrices);
    }
    return gdl;
}
