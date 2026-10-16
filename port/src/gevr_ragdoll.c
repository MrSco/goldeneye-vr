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

extern int VrRagdolls, VrDismemberment, VrBodiesStay;
extern ModelRenderData D_8002CCBC;
extern s32 g_gevrExtraPass;

static RdBody s_bodies[RD_BODIES];
static RdMemory *s_garbage;
static unsigned s_frame, s_serial;
static int s_failed;

typedef struct RdSeen {
    ChrRecord *chr;
    Model *model;
    int suppressed, attempted, matrix, hitpart, hitTick;
    float dir[3];
} RdSeen;
static RdSeen s_seen[512];

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

void gevrRagdollNoteHit(ChrRecord *chr, int matrix, int hitpart, const float dir[3])
{
    if (!VrRagdolls || !chr || !dir || gevrRagdollSuppress(chr)) return;
    RdSeen *s=rdSeen(chr,1);
    if (!s) return;
    if(!rdDead(chr))s->suppressed=s->attempted=0;
    if(matrix<0 && s->matrix>=0 && s->hitpart==hitpart && s->hitTick==g_GlobalTimer)matrix=s->matrix;
    s->matrix=matrix; s->hitpart=hitpart; s->hitTick=g_GlobalTimer;
    memcpy(s->dir,dir,sizeof(s->dir));
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
    int visits=0;
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
        if ((n->Opcode&255)==MODELNODE_OPCODE_GROUP) {
            if (rd->count==RD_PARTS) return 0;
            int i=rd->count++;
            rd->nodes[i]=n;
            rd->primary[i]=n->Data->Group.MatrixID0;
            rd->joint[i]=n->Data->Group.JointID;
            rd->parent[i]=rdParent(rd,n->Parent);
            if (rd->primary[i]<0 || rd->primary[i]>=rd->matrices) return 0;
            if (rd->parent[i]<0) { if(i) return 0; rd->root=i; }
        }
    }
    if (rd->count<8) return 0;
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
    for(int i=0;i<rd->count;i++) {
        Mtxf m=rd->bind[rd->primary[i]];
        for(int r=0;r<3;r++) {
            float l=sqrtf(m.m[r][0]*m.m[r][0]+m.m[r][1]*m.m[r][1]+m.m[r][2]*m.m[r][2]);
            if(!isfinite(l) || l<1e-6f)return 0;
            for(int k=0;k<3;k++)m.m[r][k]/=l;
        }
        m.m[0][3]=m.m[1][3]=m.m[2][3]=0; m.m[3][3]=1;
        rd->initial[i]=rd->joints[i]=m;
    }
    for(int i=0;i<rd->matrices;i++) {
        Mtxf inv;
        if(!rdInverse(&rd->initial[rd->owner[i]],&inv))return 0;
        rdMul(&rd->bind[i],&inv,&rd->offset[i]);
    }
    return rdCopyMeshes(rd);
}

static int rdHitPart(RdBody *rd, RdSeen *s)
{
    if (!s || g_GlobalTimer-s->hitTick>120) return -1;
    if(s->matrix>=0 && s->matrix<rd->matrices)return rd->owner[s->matrix];
    /* GE's guard skeleton: paired shoulders 4/5, hips 6/7, elbows 8/9,
     * knees 10/11, wrists 12/13, ankles 14/15. */
    static const int jointByHit[]={-1,14,10,6,15,11,7,1,3,12,8,4,13,9,5,2};
    int j=s->hitpart>=0 && s->hitpart<(int)(sizeof(jointByHit)/sizeof(*jointByHit)) ? jointByHit[s->hitpart] : -1;
    for(int i=0;i<rd->count;i++)if(rd->joint[i]==j)return i;
    return -1;
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
}

void gevrRagdollCapture(ChrRecord *chr)
{
    if(!gevrRagdollNeedsPose(chr) || !chr->model->render_pos)return;
    RdSeen *seen=rdSeen(chr,0);
    if(seen)seen->attempted=1;
    if(!pdphysIsReady()) {
        if(!pdphysInit(rdLog)) { s_failed=1; return; }
        const GevrBeanCuts cuts={gevrRagdollModelCut,gevrRagdollPiece,gevrRagdollRemap};
        gevrBeanSetCuts(&cuts);
        rdWorldInit();
    }
    rdWorldUpdate();
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
    pdphysRagdollPart parts[RD_PARTS]={0};
    float velocity[3]={0};
    if(seen && g_GlobalTimer-seen->hitTick<=120) {
        float l=sqrtf(seen->dir[0]*seen->dir[0]+seen->dir[1]*seen->dir[1]+seen->dir[2]*seen->dir[2]);
        if(l>1e-6f)for(int k=0;k<3;k++)velocity[k]=seen->dir[k]*120/l;
    }
    /* The native explosion death already supplies a launch velocity in game
     * units per tick. Preserve it, with a bounded conversion to units/second. */
    float fall=0;
    for(int k=0;k<3;k++)fall+=chr->fallspeed.f[k]*chr->fallspeed.f[k];
    if(fall>1 && isfinite(fall)) {
        float factor=fminf(60,600/sqrtf(fall));
        for(int k=0;k<3;k++)velocity[k]=chr->fallspeed.f[k]*factor;
    }
    for(int i=0;i<slot->count;i++) {
        pdphysRagdollPart *p=&parts[i];
        p->parent=slot->parent[i];
        memcpy(p->jointmtx,&slot->initial[i],sizeof(p->jointmtx));
        memcpy(p->a,slot->initial[i].m[3],sizeof(p->a));
        int child=-1; float length=0;
        for(int j=0;j<slot->count;j++)if(slot->parent[j]==i) {
            float d=0; for(int k=0;k<3;k++){float x=slot->initial[j].m[3][k]-p->a[k];d+=x*x;}
            if(d>length){length=d;child=j;}
        }
        length=sqrtf(length);
        p->radius=fmaxf(3,fminf(12,length*0.22f));
        if(child>=0 && length>5) {
            p->shape=PDPHYS_SHAPE_CAPSULE;
            for(int k=0;k<3;k++) {
                float d=slot->initial[child].m[3][k]-p->a[k];
                p->b[k]=p->a[k]+d*0.78f; p->a[k]+=d*0.22f;
                p->axis[k]=d/length;
            }
        } else {
            p->shape=PDPHYS_SHAPE_SPHERE;
            p->radius=slot->joint[i]==3?9:5;
            memcpy(p->axis,slot->initial[i].m[1],sizeof(p->axis));
        }
        slot->radius[i]=p->radius;
        memcpy(p->axis2,slot->initial[i].m[0],sizeof(p->axis2));
        p->joint=p->parent<0?PDPHYS_JOINT_NONE:PDPHYS_JOINT_SWINGTWIST;
        p->limits[0]=p->limits[1]=slot->joint[i]<=3?0.5f:1.3f;
        p->limits[2]=-0.6f; p->limits[3]=0.6f;
        p->mass=slot->joint[i]==1?16:slot->joint[i]==2?18:slot->joint[i]==3?5:3;
        memcpy(p->linvel,velocity,sizeof(velocity));
    }
    slot->handle=pdphysRagdollCreate(parts,slot->count);
    if(slot->handle<0){rdRelease(slot);return;}
    slot->used=1; slot->serial=++s_serial; slot->alpha=255;
    int hit=rdHitPart(slot,seen);
    if(VrDismemberment && hit>=0)rdCut(slot,hit);
    rdRooms(slot);
    if(seen)seen->suppressed=1;
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
    if(!pdphysIsReady() || ticks60<=0)return;
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
    for(int i=0;i<RD_BODIES;i++)if(s_bodies[i].used && !pdphysRagdollIsAsleep(s_bodies[i].handle))rdRooms(&s_bodies[i]);
    pdphysStep(dt);
    for(int i=0;i<RD_BODIES;i++) {
        RdBody *b=&s_bodies[i]; if(!b->used)continue;
        b->age+=dt;
        pdphysRagdollGetJoints(b->handle,(float (*)[16])b->joints);
        if(!VrBodiesStay) {
            b->alpha=fmaxf(0,fminf(255,(5.5f-b->age)*170));
            if(b->alpha<=0)rdRelease(b);
        }
    }
}

void gevrRagdollShot(ShotData *shot)
{
    if(!VrRagdolls || !pdphysIsReady() || !shot || shot->viewDir.z>=-1e-6f)return;
    float depth=shot->maxdist;
    for(int i=0;i<10;i++)if(shot->hits[i].prop && shot->hits[i].dist<depth)depth=shot->hits[i].dist;
    float distance=fminf(65535,(depth+shot->viewOrigin.z)/-shot->viewDir.z);
    if(distance<=0)return;
    float to[3],hit[3]; int handle,part;
    for(int k=0;k<3;k++)to[k]=shot->gunpos.f[k]+shot->dir.f[k]*distance;
    if(!pdphysRaycastRagdoll(shot->gunpos.f,to,0,hit,&handle,&part))return;
    for(int i=0;i<RD_BODIES;i++)if(s_bodies[i].used && s_bodies[i].handle==handle) {
        float v[3]; for(int k=0;k<3;k++)v[k]=shot->dir.f[k]*180;
        pdphysRagdollAddVelocity(handle,part,v);
        if(VrDismemberment && shot->weapon!=ITEM_FIST)rdCut(&s_bodies[i],part);
        return;
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
        /* The depth buffer occludes other rooms. Distance culling bounds CPU
         * work while detached pieces retain their own transforms. */
        float d=0;
        for(int k=0;k<3;k++){float x=b->joints[b->root].m[3][k]-currentPlayerGetViewToWorldMtxf()->m[3][k];d+=x*x;}
        if(d>10000.0f*10000.0f)continue;
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
