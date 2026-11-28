/* GoldenEye adapter for Perfect Dark CE's Jolt interface. Death bodies are
 * presentation snapshots, not actors: all modes use the same local settings,
 * and respawns, AI removal and network authority never depend on physics. */
#include "gevr_ragdoll_internal.h"
#include <string.h>
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
#include "random.h"
#include "cheat.h"

extern int VrRagdolls, VrDismemberment, VrBodiesStay, VrDismemberChance;
extern int netHordeActive(void);

/* Bodies kept: Bodies stay, at most RD_BODIES; a Horde run at most a dozen
 * (user, 2026-10-18: "massive slowdowns as bodies pile up" after three
 * waves, 22 ragdolls down with Bodies stay at 48). */
#define RD_HORDE_BODIES 12
static int rdBodyLimit(void)
{
    int limit=VrBodiesStay>0?VrBodiesStay:RD_BODIES;
    if(limit>RD_BODIES)limit=RD_BODIES;
    if(netHordeActive() && limit>RD_HORDE_BODIES)limit=RD_HORDE_BODIES;
    return limit;
}
extern float VrDismemberDamage;
extern ModelRenderData D_8002CCBC;
extern f32 chrobjFogVisRangeRelated(PropRecord *prop, f32 size); /* propobj.c */
extern f32 getinstsize(Model *model);                             /* model.c */
extern void lerp_rgba_s32_with_rgba_f32(rgba_s32 *dest, s32 enable, rgba_f32 *src); /* propobj.c */
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

/* CE ragdoll.c:41, 44, 651-654, 12; rd_internal.h RD_KILLFLOOR_Y, RD_COLROOM_MOVE.
 * Damage needed and chance are CE's two settings (VrDismemberDamage/Chance). */
#define RD_DISMEMBER_DAMAGE VrDismemberDamage
#define RD_DISMEMBER_CHANCE VrDismemberChance
#define RD_SPLIT_DAMAGE     50.0f /* torso hits only split the waist this hard */
#define RD_PUSH_HIT   525.0f
#define RD_PUSH_ALL   180.0f
#define RD_PUSH_LIFT  60.0f
#define RD_PUSH_MAXMULT 1.45f
#define RD_PUSH_SCALE 0.8f
#define RD_KILLFLOOR_Y -32000.0f
#define RD_DOOR_FADE_S 0.5f   /* a body a moving door touched is gone this soon */
#define RD_COLROOM_MOVE 12.0f
#define RD_WALL_RAYS 8          /* rdRooms: rays out level from the pelvis and each cut piece ... */
#define RD_WALL_REACH 60.0f     /* ... this far, for walls of rooms no portal joins */
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

/*
 * CE dismember.c rdPaintColour (Perfect-Dark-CE b0180009): in Paintball Mode
 * a cut gets a paint colour, red, green and blue each full or off, never
 * black (CE: as PD's paint sparks; GoldenEye's paint splats can be black,
 * explosion.c apptype 2, and a black cut reads as a hole). 0 when it's off:
 * blood. Cosmetic: the ragdolls' own sequence, never the game's.
 */
u32 rdPaintColour(void)
{
    if(!cheatIsActive(CHEAT_PAINTBALL))return 0;
    u32 c=0xff;
    for(int k=1;k<4;k++){s_random=s_random*1664525u+1013904223u;if((s_random>>16)&1)c|=0xffu<<(k*8);}
    if(c==0xff){s_random=s_random*1664525u+1013904223u;c|=0xffu<<(8*(1+(s_random>>16)%3));}
    return c;
}

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
                    if (rd->nbbox<20) {
                        rd->bbox[rd->nbbox]=n; rd->bboxHit[rd->nbbox]=hitpart;
                        rd->bboxSkel[rd->nbbox++]=a->Data->Group.JointID-1;
                    }
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
    if (!rdCopyMeshes(rd)) return 0;
    /* the bruises on a Bean body come down with him (gevr_bean_wounds.inc.h) */
    gevrBeanCopyWounds(src,&rd->model,(size_t)words*4);
    return 1;
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
    /* And the rooms whose walls are near it, portal or not: a body a blast
     * threw at 600 units/s through Dam's room 111 went through a wall of room
     * 119, which no portal joins to it, and fell out of the level (headset
     * log, 2026-11-28). Rays out level from its pelvis and each cut piece
     * find them; a floor or ceiling met (a stacked room's) is left out, as
     * rdCrossCheck leaves it. */
    for(int part=0;part<body->count;part++) {
        if(part!=body->root && !(body->cuts&(1u<<part)))continue;
        const float *at=body->joints[part].m[3];
        for(int d=0;d<RD_WALL_RAYS;d++) {
            const float a=(float)d*(2.0f*(float)M_PI/RD_WALL_RAYS);
            float to[3]={at[0]+cosf(a)*RD_WALL_REACH,at[1],at[2]+sinf(a)*RD_WALL_REACH},hit[3],normal[3];
            int room=-1;
            if(pdphysRaycastAnyRoom(at,to,hit,normal,&room) && room>=0 && room<MAXROOMCOUNT && fabsf(normal[1])<=0.7f)
                nearby[room]=1;
        }
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
    const GevrBeanCuts cuts={gevrRagdollModelCut,gevrRagdollPiece,gevrRagdollRemap,gevrRagdollBeanCap,gevrRagdollModelBody,
                             gevrRagdollDeathPos,gevrRagdollCutSide,gevrRagdollCutFrom};
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
    /* CE create.c (RD_DK_HEADSCALE, RD_DK_ARMSCALE): DK Mode's head and arms
     * as chr.c's joint hook draws them, 4x and 2.5x. The death pose is the
     * drawn one, so the elbows and hands already stand that far out; the
     * head and arms only need their size (the head sank into the floor). */
    const float dkhead=rd->dk ? 4.0f : 1.0f,dkarm=rd->dk ? 2.5f : 1.0f;
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
            rdMad(p[2],bone[1],torso*0.30f*dkhead,pp->a);
            pp->radius=torso*0.26f*dkhead;
            rdSwingTwist(pp,bone[1],localz,0.85f,0.8f,-1.0f,1.0f);
            break;
        case 3: case 4: /* upper arms */
            rdMad(p[j],bone[j],torso*0.11f*dkarm,a);
            rdCapsule(pp,a,p[sk->child[j]],torso*0.12f*dkarm);
            rdSwingTwist(pp,bone[j],localy,1.5f,1.3f,-0.9f,0.9f);
            break;
        case 5: case 6: /* forearms and hands */
            rdMad(p[sk->child[j]],bone[j],torso*0.18f*dkarm,b);
            rdCapsule(pp,p[j],b,torso*0.10f*dkarm);
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
    int limit=rdBodyLimit();
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
    slot->dk=cheatIsActive(CHEAT_DK_MODE);   /* chr.c: every chr, without BUGFIX_R1 */
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
    int walled,flooronly,tris,state;
    rdWorldStats(slot->rooms[0],&walled,&flooronly,&tris,&state);
    sysLogPrintf(LOG_NOTE,"ragdoll: stage walls so far: %d rooms (%d triangles), %d floor-only; body %u died in room %d (%s)",
        walled,tris,flooronly,s_serial,slot->rooms[0],state==1?"walls":state==2?"floor-only":"no walls yet");
    sysLogPrintf(LOG_NOTE,"ragdoll: body %u, %d parts, %d meshes, torso %.1f, hit part %d, weapon %d (damage %.1f), blast %d, cuts 0x%x,"
        " knees %.2f %.2f, elbows %.2f %.2f, hinge Y along bone %d%d%d%d",
        slot->serial,slot->count,slot->nmeshes,slot->torso,hit,weapon,rdDamage(weapon),blasted,slot->cuts,
        rdHingeAngle(slot,11),rdHingeAngle(slot,12),rdHingeAngle(slot,5),rdHingeAngle(slot,6),
        rdHingeAlongBone(slot,11,bone),rdHingeAlongBone(slot,12,bone),rdHingeAlongBone(slot,5,bone),rdHingeAlongBone(slot,6,bone));
}

/*
 * CE's Resting Bodies Turn Solid (ragdoll.c rdMpSolidCheck, newer than the
 * pinned port: every part slower than RD_SOLID_MAXSPEED for RD_SOLID_STILL60
 * ticks and the body stops being simulated), for cut pieces, and as sleep
 * rather than static: a piece lying still sleeps, and a shot or a touch wakes
 * it as before. Jolt's own sleep wants every point under 3 cm/s, and a cut
 * forearm or head settled into a steady 6 units/s roll on a flat floor and
 * kept it, rolling and turning until the body went (user, 2026-11-27: a cut
 * leg "still jittering on the ground"; PC trace: 50 s at 6.1 units/s). Slower
 * than CE's 40 units/s, which stopped a rolling forearm dead at 30: the piece
 * slows down first and stops at the crawl.
 */
#define RD_REST_SPEED 10.0f   /* units/s */
#define RD_REST_TIME 0.3f     /* s: CE RD_SOLID_STILL60 (18 ticks) */
#define RD_REST_BODY 1.0f     /* s: the body itself, within the same 3 units */
#define RD_SHAKE_REACH 12.0f  /* units: a piece rocking in place within this... */
#define RD_SHAKE_TIME 1.0f    /* s: ...this long lies still too (the body: twice) */
#define RD_SHAKE_LATE 6.0f    /* s: awake this long, a body rocks in place within twice the reach, */
#define RD_LATE_SPEED 240.0f  /* units/s: and a piece slower than this rocks however far it swings */
/*
 * CE ragdoll.c rdMpSolidCheck (Perfect-Dark-CE b0180009), Resting Bodies Turn
 * Solid on its default, the Combat Simulator: in a multiplayer match a body
 * whose every part has been slower than 40 units/s for 0.3 s turns static.
 * Other bodies land on it and shots still hit it (blood, no push), but the
 * physics no longer simulates it, which leaves the Quest's physics time to
 * the bodies still falling. A moving lift under it, or a prop it lies on or
 * against going away, makes it move again (rdUnsolid, movers.c); doors pass
 * through it. Solo bodies stay as they were: they still react to shots.
 */
#define RD_SOLID_SPEED 40.0f  /* units/s: CE RD_SOLID_MAXSPEED */

void rdUnsolid(RdBody *rd, const char *why)
{
    if(!rd->solid)return;
    pdphysRagdollSetSolid(rd->handle,0);
    rd->solid=0; rd->solidstill=0;
    sysLogPrintf(LOG_NOTE,"ragdoll: body %u moves again (no longer solid: %s)",rd->serial,why);
}

static void rdRest(RdBody *b, float dt, int asleep)
{
    float fastest[RD_PARTS+1]={0},all=0;
    for(int p=0;p<b->count;p++) {
        const int piece=rdPiece(b,p);
        float d=0;
        for(int k=0;k<3;k++){float x=b->joints[p].m[3][k]-b->restpos[p][k];d+=x*x;}
        if(b->restset && dt>0)fastest[piece]=fmaxf(fastest[piece],sqrtf(d)/dt);
        all=fmaxf(all,fastest[piece]);
        memcpy(b->restpos[p],b->joints[p].m[3],sizeof(b->restpos[p]));
    }
    if(!b->restset){b->restset=1;return;}
    /* A piece lies still while none of its parts strays more than RD_REST_TIME
     * at RD_REST_SPEED from where it was when the count started: a speed test
     * alone kept a forearm shaking against a wall awake for good, every tick
     * fast and the piece going nowhere (user, 2026-11-28) */
    /* The body itself too, over RD_REST_BODY: on the PC a guard slumped against
     * the Dam barrier stayed awake for good at zero speed, its parts never let
     * sleep however still (2026-11-28), costing the physics every tick */
    /* Each piece put to sleep on its own was woken by the next one still
     * rocking against it: a cut head at 234 units/s, a forearm at 78, for good
     * (headset log, 2026-11-28, the arms jittering). Once every piece is
     * resting or rocking in place they sleep together; and a body awake past
     * RD_SHAKE_LATE rocks in place within twice the reach. */
    unsigned every=0;
    int ready=1;
    const float reach=b->awake>=RD_SHAKE_LATE ? RD_SHAKE_REACH*2 : RD_SHAKE_REACH;
    for(int c=-1;c<b->count && !asleep && !b->solid;c++) {
        if(c>=0 && !(b->cuts&(1u<<c)))continue;
        float *still=c>=0 ? &b->still[c] : &b->stillbody;
        const float wait=c>=0 ? RD_REST_TIME : RD_REST_BODY;
        unsigned mask=0;
        float stray=0,wide=0;
        for(int p=0;p<b->count;p++) {
            if(rdPiece(b,p)!=c+1)continue;
            mask|=1u<<p;
            float d=0,w=0;
            for(int k=0;k<3;k++) {
                float x=b->joints[p].m[3][k]-b->anchor[p][k],y=b->joints[p].m[3][k]-b->shakeanchor[p][k];
                d+=x*x; w+=y*y;
            }
            stray=fmaxf(stray,d); wide=fmaxf(wide,w);
        }
        /* A piece rocking in place by a little more than that lies still too
         * once it has kept within RD_SHAKE_REACH for RD_SHAKE_TIME: a cut
         * forearm on the Quest went 10 units/s, now and then 3 units off its
         * anchor, and stayed awake for good (headset log, 2026-11-28) */
        every|=mask;
        float *shake=&b->shake[c+1];
        /* Late, a slow piece counts as rocking however far its joint swings: a
         * cut head spinning on the floor swung its neck joint past the reach
         * and went on at 140 units/s past 8 s (headset log, 2026-11-28) */
        const int lateslow=b->awake>=RD_SHAKE_LATE && fastest[c+1]<RD_LATE_SPEED;
        if(*shake<=0 || (wide>reach*reach && !lateslow)) {
            for(int p=0;p<b->count;p++)if(mask&(1u<<p))memcpy(b->shakeanchor[p],b->joints[p].m[3],sizeof(b->shakeanchor[p]));
            *shake=dt>0 ? dt*0.5f : 1e-3f;
        } else if((*shake+=dt)>=(c>=0 ? RD_SHAKE_TIME : RD_SHAKE_TIME*2)) {
            pdphysRagdollSleepParts(b->handle,mask);
            continue;
        }
        if(*still<=0 || stray>RD_REST_SPEED*RD_REST_TIME*RD_REST_SPEED*RD_REST_TIME) {
            for(int p=0;p<b->count;p++)if(mask&(1u<<p))memcpy(b->anchor[p],b->joints[p].m[3],sizeof(b->anchor[p]));
            *still=dt>0 ? dt*0.5f : 1e-3f;
            ready=0;
            continue;
        }
        if((*still+=dt)<wait){ready=0;continue;}
        pdphysRagdollSleepParts(b->handle,mask);
    }
    if(every && ready && !asleep && !b->solid)pdphysRagdollSleepParts(b->handle,every);
    /* Awake long after it fell: why, now and then, for a headset log. Cut
     * pieces jittered for good on the Quest (user, 2026-11-28) and slept within
     * 4 s on the PC, the same throws and floors. Per piece: its top speed this
     * tick, how far it strayed from its anchor, how long it has lain still. */
    if(asleep || b->solid){b->awake=0;b->awakelog=0;b->moverwakes=0;}
    else if((b->awake+=dt)>=8.0f && (b->awakelog-=dt)<=0) {
        char line[200];int n=0;
        for(int c=-1;c<b->count && n<(int)sizeof(line)-32;c++) {
            if(c>=0 && !(b->cuts&(1u<<c)))continue;
            float stray=0;
            for(int p=0;p<b->count;p++)if(rdPiece(b,p)==c+1) {
                float d=0;
                for(int k=0;k<3;k++){float x=b->joints[p].m[3][k]-b->anchor[p][k];d+=x*x;}
                stray=fmaxf(stray,d);
            }
            n+=snprintf(line+n,sizeof(line)-n," %d:%.1f/%.1f/%.2f/%.2f",c+1,fastest[c+1],sqrtf(stray),c>=0 ? b->still[c] : b->stillbody,b->shake[c+1]);
        }
        sysLogPrintf(LOG_NOTE,"ragdoll: body %u awake %.0f s, cuts 0x%x, %d prop wakes (last type %d at %d %d %d), pieces speed/stray/still/rocking:%s",
                     b->serial,b->awake,b->cuts,b->moverwakes,b->mover[0],b->mover[1],b->mover[2],b->mover[3],line);
        b->awakelog=5.0f; b->moverwakes=0;
    }
    if(b->solid || !gevrMpRules())return;
    b->solidstill=all>RD_SOLID_SPEED ? 0 : b->solidstill+dt;
    if(b->solidstill>=RD_REST_TIME) {
        pdphysRagdollSetSolid(b->handle,1);
        b->solid=1;
        sysLogPrintf(LOG_NOTE,"ragdoll: body %u lying still: solid now",b->serial);
    }
}

/* Diagnostics for bodies seen in walls: the pelvis moved through level
 * geometry this step. In its own rooms it went through a wall it collides
 * with; only in other rooms, the room filter left that wall out. Once per
 * body. */
static void rdCrossCheck(RdBody *b, const float before[3], float dt)
{
    if(b->crossed || b->asleep)return;
    const float *after=b->joints[b->root].m[3];
    float moved=0;
    for(int k=0;k<3;k++){float x=after[k]-before[k];moved+=x*x;}
    if(moved<1)return;
    float hit[3],normal[3]; int room=-1;
    const int own=pdphysRagdollRaycast(b->handle,before,after,hit,normal,&room);
    if(!own && !pdphysRaycastAnyRoom(before,after,hit,normal,&room))return;
    if(fabsf(normal[1])>0.7f && !own)return; /* a floor of a stacked room */
    b->crossed=1;
    int walled,flooronly,tris,state;
    rdWorldStats(room,&walled,&flooronly,&tris,&state);
    sysLogPrintf(LOG_WARNING,"ragdoll: body %u's pelvis went through %s geometry (room %d, %s, normal y %.2f) at %.0f units/s; its rooms %d %d %d %d",
        b->serial,own?"its own":"another room's",room,state==1?"walls":state==2?"floor-only":"no walls",normal[1],
        sqrtf(moved)/dt,b->visrooms[0]==255?-1:b->visrooms[0],b->visrooms[1]==255?-1:b->visrooms[1],
        b->visrooms[2]==255?-1:b->visrooms[2],b->visrooms[3]==255?-1:b->visrooms[3]);
}

/* A body that falls asleep and is woken again and again never stays awake
 * the 8 s rdRest reports: a cut piece jittered on the Quest with no report
 * (user, 2026-11-28). Every third waking within 10 s, what moved and what
 * woke it, for a headset log. */
static void rdRewakeLog(RdBody *b)
{
    if(b->age-b->rewakefrom>10.0f){b->rewakefrom=b->age;b->rewakes=0;}
    if(++b->rewakes%3)return;
    char line[200];int n=0;
    for(int p=0;p<b->count && n<(int)sizeof(line)-24;p++) {
        float d=0;
        for(int k=0;k<3;k++){float x=b->joints[p].m[3][k]-b->restpos[p][k];d+=x*x;}
        if(d>0.01f)n+=snprintf(line+n,sizeof(line)-n," %d(piece %d):%.1f",p,rdPiece(b,p),sqrtf(d));
    }
    sysLogPrintf(LOG_NOTE,"ragdoll: body %u woke again, %d times in %.1f s, cuts 0x%x, %d prop wakes (last type %d at %d %d %d); moved:%s",
                 b->serial,b->rewakes,b->age-b->rewakefrom,b->cuts,b->moverwakes,b->mover[0],b->mover[1],b->mover[2],b->mover[3],
                 n ? line : " nothing");
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
    if(ticks60<=0)return;
    float dt=fminf(ticks60/60.0f,0.1f);
    int limit=rdBodyLimit();
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
        float before[3];memcpy(before,b->joints[b->root].m[3],sizeof(before));
        if(!(b->asleep && asleep))pdphysRagdollGetJoints(b->handle,(float (*)[16])b->joints);
        if(b->asleep && !asleep)rdRewakeLog(b);
        b->asleep=asleep;
        rdRest(b,dt,asleep);
        rdCrossCheck(b,before,dt);
        /* Fell out of the level (CE :1708) */
        if(b->joints[b->root].m[3][1]<RD_KILLFLOOR_Y) {
            sysLogPrintf(LOG_NOTE,"ragdoll: body %u fell out of the level, removing it",b->serial);
            rdRelease(b);
            continue;
        }
        if(!VrBodiesStay) {
            b->alpha=fmaxf(0,fminf(255,(5.5f-b->age)*170));
            if(b->alpha<=0){rdRelease(b);continue;}
        }
        if(b->doorhit) {
            b->doorfade+=dt;
            b->alpha=fminf(b->alpha,255.0f*(1.0f-b->doorfade/RD_DOOR_FADE_S));
            if(b->alpha<=0)rdRelease(b);
        }
    }
}

/*
 * A moving door and the bodies (propobj.c sub_GAME_7F0448A8). A ragdoll is not a
 * prop, so a door never saw one: it closed through the body, which lay there
 * clipped for good with Bodies stay on (owner, 2026-11-23, Bean in a doorway).
 * Now a body the door touches fades in RD_DOOR_FADE_S. The door does not wait
 * for it, as it does not wait for a kept body past the original lifetime
 * (chraction.c gevrBodyDoorHit): the dead guard's own body holds the door for
 * that lifetime, so door timing is the original's with ragdolls on or off, for
 * everyone online and in ghost runs (owner, 2026-11-23).
 */
int gevrRagdollBodies(void) { return VrRagdolls ? RD_BODIES : 0; }

static void rdPushPiece(RdBody *b, int part, const float v[3]);

/* Test hook (bondview2.c "cut<N>"): CE skeleton part N off every body within
 * radius of a point (Bond), as a shot's cut (rdCut). Returns how many were cut. */
int gevrRagdollCutNear(const float at[3], float radius, int skelpart)
{
    int n=0;
    if(skelpart<0 || skelpart>=RD_SKEL)return 0;
    for(int i=0;i<RD_BODIES;i++) {
        RdBody *b=&s_bodies[i];
        if(!b->used || b->body[skelpart]<0)continue;
        float d=0;
        for(int k=0;k<3;k++){float x=b->joints[b->root].m[3][k]-at[k];d+=x*x;}
        if(d<=radius*radius && rdCut(b,b->body[skelpart])) {
            /* thrown clear, as a shot's push throws it, so both ends show */
            const float up[3]={0,260,0};
            rdPushPiece(b,b->body[skelpart],up);
            n++;
        }
    }
    return n;
}

/* The parts of a body's biggest piece, the one a door or the truck fades it
 * for: a leg the grenade launcher threw into a doorway faded the torso lying
 * behind boxes far from it (user, 2026-11-28). A piece cut off and in the way
 * is left to the door. */
int gevrRagdollBodyParts(int body, coord3d *pos, int max)
{
    if (body<0 || body>=RD_BODIES || !pos || max<=0) return 0;
    RdBody *b=&s_bodies[body];
    if (!b->used || b->doorhit) return 0;
    int size[RD_PARTS+1]={0},biggest=rdPiece(b,b->root),n=0;
    for (int i=0;i<b->count;i++) size[rdPiece(b,i)]++;
    for (int p=0;p<=RD_PARTS;p++) if (size[p]>size[biggest]) biggest=p;
    for (int i=0;i<b->count && n<max;i++) {
        if (rdPiece(b,i)!=biggest) continue;
        for (int k=0;k<3;k++) pos[n].f[k]=b->joints[i].m[3][k];
        n++;
    }
    return n;
}

/* The Blood patch's pool under a body (gevr_blood.c): its serial, its root in
 * world units and whether it has come to rest (asleep, or down a second and a
 * half). 0 for an empty or fading slot. */
unsigned gevrRagdollBodyRest(int body, coord3d *root, int *resting)
{
    if (body<0 || body>=RD_BODIES || !root || !resting) return 0;
    const RdBody *b=&s_bodies[body];
    if (!b->used || b->doorhit || b->alpha<=0) return 0;
    for (int k=0;k<3;k++) root->f[k]=b->joints[b->root].m[3][k];
    *resting=b->asleep || b->age>1.5f;
    return b->serial;
}

void gevrRagdollDoorFade(int body)
{
    if (body<0 || body>=RD_BODIES) return;
    RdBody *b=&s_bodies[body];
    if (!b->used || b->doorhit) return;
    b->doorhit=1;
    b->doorfade=0;
    sysLogPrintf(LOG_NOTE,"ragdoll: body %u is in a moving door's way; it fades",b->serial);
}

void gevrRagdollDoorFadeLog(int body, int part, const float at[3], int truck, const float by[3],
                            const float *outline, int edges, float bottom, float top)
{
    if (body<0 || body>=RD_BODIES || !s_bodies[body].used || s_bodies[body].doorhit) return;
    float lo[2]={1e30f,1e30f},hi[2]={-1e30f,-1e30f};
    for (int e=0;e<edges && outline;e++)for (int k=0;k<2;k++) {
        lo[k]=fminf(lo[k],outline[e*2+k]); hi[k]=fmaxf(hi[k],outline[e*2+k]);
    }
    sysLogPrintf(LOG_NOTE,"ragdoll: body %u's part %d at %.0f %.0f %.0f is in the way of a moving %s at %.0f %.0f %.0f: "
                 "its outline x %.0f..%.0f z %.0f..%.0f (%d edges), y %.0f..%.0f",
                 s_bodies[body].serial,part,at[0],at[1],at[2],truck ? "truck" : "door",by[0],by[1],by[2],
                 lo[0],hi[0],lo[1],hi[1],edges,bottom,top);
}

/* A shot body bleeds where it was hit, as a living guard does
 * (chrHandleBulletHit -> chrCreateBloodStain, chr.c:3876): the hit box of
 * that part nearest the hit, the point pushed half out from its joint and
 * half a joint into the body, in that box's matrix. The stain paints the
 * body's own vertex copies, and Bean's wound follows it. The game's random
 * sequence (the stain's shade) is put back: the body is cosmetic. */
static void rdStain(RdBody *b, int part, const float hit[3], const float dir[3])
{
    int best=-1; float bestd=1e30f;
    for(int i=0;i<b->nbbox;i++) {
        int sp=b->bboxSkel[i];
        while(sp>0 && b->body[sp]<0)sp=g_RdSkelHuman.parent[sp];
        if(sp<0 || b->body[sp]!=part)continue;
        const int m=modelFindNodeMtxIndex(b->bbox[i],0);
        if(m<0 || m>=b->matrices)continue;
        Mtxf w; rdMul(&b->offset[m],&b->joints[b->owner[m]],&w);
        float d=0; for(int k=0;k<3;k++){float x=w.m[3][k]-hit[k];d+=x*x;}
        if(d<bestd){bestd=d;best=i;}
    }
    if(best<0)return;
    ModelNode *node=b->bbox[best];
    const int m=modelFindNodeMtxIndex(node,0);
    Mtxf w,inverse; rdMul(&b->offset[m],&b->joints[b->owner[m]],&w);
    if(!rdInverse(&w,&inverse))return;
    const float size=getjointsize(&b->model,node)*0.5f;
    float p[3];
    for(int k=0;k<3;k++)p[k]=hit[k]+(hit[k]-w.m[3][k])*0.5f-size*dir[k];
    rdTransform(&inverse,p,1);
    coord3d local; memcpy(local.f,p,sizeof(local.f));
    const u64 seed=g_randomSeed;
    chrCreateBloodStain(&b->model,b->bboxHit[best],node,&local);
    g_randomSeed=seed;
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
        if(shot->weapon!=ITEM_FIST)rdStain(b,part,hit,shot->dir.f);
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

/* A body's own model (&s_bodies[i].model): its slot by address. Bean asks for
 * every vertex of every cut body, and a search of all the slots each time was
 * 13% of the game thread at a pile of bodies (Bunker II, 2026-10-30). */
static RdBody *rdModel(const Model *model)
{
    const uintptr_t first=(uintptr_t)&s_bodies[0].model, at=(uintptr_t)model-first;
    if((uintptr_t)model<first || at%sizeof(RdBody) || at/sizeof(RdBody)>=RD_BODIES)return NULL;
    RdBody *b=&s_bodies[at/sizeof(RdBody)];
    return b->used ? b : NULL;
}
/* rdPiece of each matrix's owner, kept until the body or its cuts change: it
 * walks the parts up, and was asked for each influence of each vertex */
static const u8 *rdPieces(RdBody *b)
{
    if(b->pieceSerial!=b->serial || b->pieceCuts!=b->cuts) {
        for(int m=0;m<b->matrices;m++)b->piece[m]=(u8)rdPiece(b,b->owner[m]);
        b->pieceSerial=b->serial; b->pieceCuts=b->cuts;
    }
    return b->piece;
}
int gevrRagdollModelCut(const Model *model) { RdBody *b=rdModel(model); return b ? (int)b->cuts : 0; }
unsigned gevrRagdollModelBody(const Model *model) { RdBody *b=rdModel(model); return b ? b->serial : 0; }

/* A point of the body as it was at death (rd->bind: each matrix's world frame then) */
void gevrRagdollDeathPos(const Model *model, int matrix, const float local[3], float out[3])
{
    RdBody *b=rdModel(model);
    memcpy(out,local,3*sizeof(float));
    if(b && matrix>=0 && matrix<b->matrices)rdTransform(&b->bind[matrix],out,1);
}

/*
 * Bean's skin is split at a cut by triangle, each to the piece that moves it
 * most; a long coat's skirt or front mixes the two across a wide band, and
 * the split zigzagged through it, red caps filling the teeth (user,
 * 2026-11-28: the coats after a leg or the waist was cut). A triangle across
 * the cut goes instead to the side of the cut's plane its middle is on: the
 * plane through the joint at death, square to the bone below it (or, at the
 * end of a limb, the bone above).
 */
int gevrRagdollCutSide(const Model *model, int piece, int other, const float p[3])
{
    RdBody *b=rdModel(model);
    if(!b)return -1;
    for(int j=0;j<b->count;j++) {
        if(!(b->cuts&(1u<<j)) || b->parent[j]<0)continue;
        const int child=rdPiece(b,j),parent=rdPiece(b,b->parent[j]);
        if(!((child==piece && parent==other) || (child==other && parent==piece)))continue;
        int below=-1;
        for(int k=0;k<b->count && below<0;k++)if(b->parent[k]==j)below=k;
        const float *at=b->initial[j].m[3];
        const float *from=below>=0 ? at : b->initial[b->parent[j]].m[3];
        const float *to=below>=0 ? b->initial[below].m[3] : at;
        float n[3],l=0,d=0;
        for(int k=0;k<3;k++){n[k]=to[k]-from[k];l+=n[k]*n[k];}
        if(l<1e-6f)return -1;
        for(int k=0;k<3;k++)d+=(p[k]-at[k])*n[k];
        return d>0 ? child : parent;
    }
    return -1;
}
/* Of a piece cut off, the matrix of the part it was cut from and *own that of
 * the part cut: the bones either side of the cut, where Bean holds a skin's
 * weight from across it (gevr_bean_pose.inc.h beanCutFrame); -1 for the
 * body's own piece */
int gevrRagdollCutFrom(const Model *model, int piece, int *own)
{
    RdBody *b=rdModel(model);
    if(!b)return -1;
    for(int j=0;j<b->count;j++) {
        if(!(b->cuts&(1u<<j)) || b->parent[j]<0 || rdPiece(b,j)!=piece || rdPiece(b,b->parent[j])==piece)continue;
        if(own)*own=b->primary[j];
        return b->primary[b->parent[j]];
    }
    return -1;
}
/* Bean's skin found a hole's rim (gevr_bean_caps.inc.h): drawn after the body */
void gevrRagdollBeanCap(const Model *model,int matrix,const s16 (*rim)[2][3],int nedges,int piece,int other)
{
    RdBody *b=rdModel(model);
    if(b && b->nbean==RD_MAXBEAN) {
        static int warned;
        if(!warned++)sysLogPrintf(LOG_WARNING,"ragdoll: body %u has more than %d Bean rims this view; the rest go uncapped",b->serial,RD_MAXBEAN);
        return;
    }
    if(!b || nedges<3 || matrix<0 || matrix>=b->matrices)return;
    /* CE's reach (ragdollBuildCaps, dismember.c): a hole is closed only within
     * 1.6 cut radii of its joint, on this side's frame. Bean's skin has open
     * rims far off on triangles with a trace of the other piece's weight - a
     * sleeve's cuff at the wrist after a shoulder cut, 2.5 cut radii away.
     * A rim is closed when any of it comes that near, not its middle: a coat's
     * opening after a thigh cut runs from the hip down to the hem, its middle
     * out of reach, and stayed open - a hollow coat, the hip's disc floating
     * inside it (user, 2026-11-28, Quest). */
    const int childside=piece>0 && rdPiece(b,b->parent[piece-1])==other;
    const int cut=childside ? piece-1 : other>0 && rdPiece(b,b->parent[other-1])==piece ? other-1 : -1;
    if(cut>=0 && b->parent[cut]>=0) {
        float joint[3];
        Mtxf world,inverse; rdMul(&b->offset[matrix],&b->joints[b->owner[matrix]],&world);
        /* the parent's side: where the joint was on that piece (its hole) */
        if(childside) memcpy(joint,b->joints[cut].m[3],sizeof(joint));
        else {
            memcpy(joint,b->initial[cut].m[3],sizeof(joint));
            if(!rdInverse(&b->initial[b->parent[cut]],&inverse))return;
            rdTransform(&inverse,joint,1); rdTransform(&b->joints[b->parent[cut]],joint,1);
        }
        const float reach=g_RdSkelHuman.cutradius[b->skel[cut]]*b->torso*1.6f;
        float nearest=1e30f;
        for(int e=0;e<nedges;e++)for(int k=0;k<2;k++) {
            float at[3]={rim[e][k][0],rim[e][k][1],rim[e][k][2]},d=0;
            rdTransform(&world,at,1);
            for(int j=0;j<3;j++){float x=at[j]-joint[j];d+=x*x;}
            nearest=fminf(nearest,d);
        }
        if(nearest>reach*reach)return;
    }
    b->bean[b->nbean].matrix=matrix; b->bean[b->nbean].nedges=nedges; b->bean[b->nbean].rim=rim;
    b->bean[b->nbean].cut=cut;
    b->bean[b->nbean].piece=piece; b->bean[b->nbean++].other=other;
}
int gevrRagdollPiece(const Model *model,int matrix)
{
    RdBody *b=rdModel(model);
    return b && matrix>=0 && matrix<b->matrices ? rdPieces(b)[matrix] : 0;
}
int gevrRagdollRemap(const Model *model,int owner,int matrix,float p[3],float n[3])
{
    RdBody *b=rdModel(model);
    if(!b || !b->cuts || owner<0 || owner>=b->matrices || matrix<0 || matrix>=b->matrices)return matrix;
    const u8 *piece=rdPieces(b);
    if(piece[owner]==piece[matrix])return matrix;
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
        /* Rooms and camera are now those of the view being drawn. */
        b->onscreen=rdOnScreen(b);
        if(!b->onscreen)continue;
        b->model.render_pos=dynAllocate(b->matrices*sizeof(RenderPosView));
        if(!b->model.render_pos)continue;
        for(int j=0;j<b->matrices;j++) {
            Mtxf world;
            rdMul(&b->offset[j],&b->joints[b->owner[j]],&world);
            rdMul(&world,camGetWorldToScreenMtxf(),&b->model.render_pos[j].pos);
        }
        b->nbean=0; /* this view's Bean rims arrive during subdraw */
        ModelRenderData render=D_8002CCBC;
        render.gdl=gdl; render.flags=3;
        /* Fogged as the living guard was (chr.c): the root's depth through the
         * game's prop fog. Unfogged, a body shot in fog showed its textures the
         * moment it fell (Surface 2, 2026-10-26). */
        const f32 depth=-b->model.render_pos[0].pos.m[3][2];   /* the chr's zDepth (sub_GAME_7F06C768) */
        rgba_f32 fog;
        const s32 fogged=gevrFogDepthColor(depth,&fog);
        if(!fogged)continue;
        /* And faded by distance as he was (chrRenderProp's chrobjFogVisRangeRelated,
         * which reads only the prop's zDepth): a guard faint in the distance came
         * in solid the moment he fell (player report, Jungle and Surface 2). The
         * corpse's own fade (b->alpha) on top. */
        PropRecord at; memset(&at,0,sizeof(at)); at.zDepth=depth;
        const f32 range=chrobjFogVisRangeRelated(&at,getinstsize(&b->model));
        const s32 alpha=(s32)(range*b->alpha);
        if(alpha<=0)continue;
        render.PropType=alpha<255?8:7;
        render.envcolour.word=0x5a000000u|(alpha<255?(u8)alpha:0);
        rgba_s32 shade={.r=b->shade.r,.g=b->shade.g,.b=b->shade.b,.a=b->shade.a};
        lerp_rgba_s32_with_rgba_f32(&shade,fogged,&fog);
        render.fogcolour.word=((u32)shade.r<<24)|((u32)shade.g<<16)|((u32)shade.b<<8)|(u32)shade.a;
        /* VR_FADE_DEPTH as chrRenderProp: fading, his solid pixels write depth */
        if(alpha<255)gDPNoOpTag(render.gdl++,0x56620000u|(u8)alpha);
        subdraw(&render,&b->model);
        if(alpha<255)gDPNoOpTag(render.gdl++,0x56620000u|0xFFu);
        {
            /* the cut caps fade with the body (gevr_ragdoll_mesh.c reads b->alpha) */
            const f32 own=b->alpha;
            b->alpha=(f32)alpha;
            gdl=rdDrawCaps(b,render.gdl);
            b->alpha=own;
        }
        bondviewTransformManyPosToViewMatrix(b->model.render_pos,b->matrices);
    }
    return gdl;
}
