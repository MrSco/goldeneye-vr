/* Cosmetic kinematic collision. Sample simulation transforms, never the
 * view-dependent model matrices (which the renderer also packs in place).
 * GE has no separate lift prop type: moving solid props and horizontal
 * door panels supply platforms, including those in custom campaigns. */
#include "gevr_ragdoll_internal.h"
#include "propobj.h"
#include "chrai.h"

extern void door7F0526EC(DoorRecord *door, Mtxf *world);

#define RD_MOVERS 256
#define RD_PROPRANGE 250.0f /* CE props.c: how far around a body to look for props */
typedef struct RdMover {
    PropRecord *prop;
    ObjectRecord *object;
    Model *model;
    int handle, seen;
    Mtxf pose;
    float half[3], velocity[3];
} RdMover;
static RdMover s_movers[RD_MOVERS];

void rdMoversReset(void)
{
    for(int i=0;i<RD_MOVERS;i++)if(s_movers[i].prop)
        pdphysPropDestroy(s_movers[i].handle);
    memset(s_movers,0,sizeof(s_movers));
}

/* Remove scale and reflection from the native row-vector rotation. A box
 * is symmetric, so reflecting one of its axes leaves its volume unchanged. */
static int rdBoxPose(const Mtxf *world, const struct bbox *box,
                     Mtxf *pose, float half[3], float rotation[4])
{
    float low[3]={box->xmin,box->ymin,box->zmin};
    float high[3]={box->xmax,box->ymax,box->zmax};
    memset(pose,0,sizeof(*pose));pose->m[3][3]=1;
    for(int i=0;i<3;i++) {
        float scale=0;
        for(int k=0;k<3;k++)scale+=world->m[i][k]*world->m[i][k];
        scale=sqrtf(scale);
        half[i]=(high[i]-low[i])*0.5f*scale;
        if(!isfinite(half[i]) || half[i]<0.05f || !isfinite(scale) || scale<1e-8f)return 0;
        for(int k=0;k<3;k++) {
            pose->m[i][k]=world->m[i][k]/scale;
            pose->m[3][k]+=(low[i]+high[i])*0.5f*world->m[i][k];
        }
    }
    for(int k=0;k<3;k++) {
        pose->m[3][k]+=world->m[3][k];
        if(!isfinite(pose->m[3][k]))return 0;
    }
    float cross[3]={pose->m[0][1]*pose->m[1][2]-pose->m[0][2]*pose->m[1][1],
        pose->m[0][2]*pose->m[1][0]-pose->m[0][0]*pose->m[1][2],
        pose->m[0][0]*pose->m[1][1]-pose->m[0][1]*pose->m[1][0]};
    float determinant=0;
    for(int k=0;k<3;k++)determinant+=cross[k]*pose->m[2][k];
    if(fabsf(determinant)<0.999f)return 0; /* no sheared custom transforms */
    if(determinant<0)for(int k=0;k<3;k++)pose->m[2][k]=-pose->m[2][k];
    /* Largest quaternion component avoids the trace singularity at 180 deg.
     * Native rows are the columns of the usual column-vector rotation. */
    float trace=pose->m[0][0]+pose->m[1][1]+pose->m[2][2];
    if(trace>0) {
        float s=2*sqrtf(trace+1);
        rotation[3]=0.25f*s;
        rotation[0]=(pose->m[1][2]-pose->m[2][1])/s;
        rotation[1]=(pose->m[2][0]-pose->m[0][2])/s;
        rotation[2]=(pose->m[0][1]-pose->m[1][0])/s;
    } else {
        int i=0;
        if(pose->m[1][1]>pose->m[i][i])i=1;
        if(pose->m[2][2]>pose->m[i][i])i=2;
        int j=(i+1)%3,k=(i+2)%3;
        float s=2*sqrtf(fmaxf(0,1+pose->m[i][i]-pose->m[j][j]-pose->m[k][k]));
        if(s<1e-6f)return 0;
        rotation[i]=0.25f*s;
        rotation[j]=(pose->m[i][j]+pose->m[j][i])/s;
        rotation[k]=(pose->m[i][k]+pose->m[k][i])/s;
        rotation[3]=(pose->m[j][k]-pose->m[k][j])/s;
    }
    return 1;
}

static int rdMoverGeometry(PropRecord *prop,Mtxf *pose,float half[3],float q[4])
{
    if((prop->type!=PROP_TYPE_DOOR && prop->type!=PROP_TYPE_OBJ)
        || prop->parent || prop->timetoregen>0 || !(prop->flags&PROPFLAG_ENABLED))return 0;
    ObjectRecord *obj=prop->obj;
    if(!obj || obj->prop!=prop || !obj->model || !obj->model->obj || !obj->model->obj->RootNode
        || (obj->runtime_bitflags&RUNTIMEBITFLAG_REMOVE))return 0;
    /* Use the game's collision eligibility. Hidden pickups and destroyed
     * doors must not leave invisible physics obstacles behind. */
    rect4f *polygon=NULL;int edges=0;float top=0,bottom=0;
    sub_GAME_7F04F244(prop,&polygon,&edges,&top,&bottom);
    if(edges<3)return 0;
    ModelRoData_BoundingBoxRecord *bbox=chrobjGetBboxFromObjectRecord(obj);
    if(!bbox)return 0;
    Mtxf world=obj->mtx;
    struct bbox bounds=bbox->Bounds;
    if(prop->type==PROP_TYPE_DOOR) {
        DoorRecord *door=prop->door;
        if(obj->runtime_bitflags&RUNTIMEBITFLAG_DESTROYED)return 0;
        door7F0526EC(door,&world);
        bounds=door->bbox.Bounds;
        if(door->doorType==DOORTYPE_EYE || door->doorType==DOORTYPE_IRIS) {
            /* These doors articulate multiple leaves. Match the native
             * collision prism, including its reduced height when ajar. */
            if(!polygon || edges>8 || !isfinite(top) || !isfinite(bottom))return 0;
            memset(&world,0,sizeof(world));for(int k=0;k<4;k++)world.m[k][k]=1;
            bounds.xmin=bounds.zmin=1e30f;bounds.xmax=bounds.zmax=-1e30f;
            for(int i=0;i<edges;i++) {
                const coord2d *p=(const coord2d *)polygon+i;
                bounds.xmin=fminf(bounds.xmin,p->x);bounds.xmax=fmaxf(bounds.xmax,p->x);
                bounds.zmin=fminf(bounds.zmin,p->y);bounds.zmax=fmaxf(bounds.zmax,p->y);
            }
            bounds.ymin=bottom;bounds.ymax=top;
        }
    } else memcpy(world.m[3],obj->runtime_pos.f,3*sizeof(float));
    return rdBoxPose(&world,&bounds,pose,half,q);
}

static int rdMoverNear(const Mtxf *pose,const float half[3],const float positions[][3],int count)
{
    float reach=sqrtf(half[0]*half[0]+half[1]*half[1]+half[2]*half[2])+600;
    for(int i=0;i<count;i++) {
        float d=0;
        for(int k=0;k<3;k++)d+=(positions[i][k]-pose->m[3][k])*(positions[i][k]-pose->m[3][k]);
        if(d<reach*reach)return 1;
    }
    return 0;
}

static void rdMoverWake(RdMover *m,const float dv[3],const float velocity[3],float dt,RdBody *bodies,int count)
{
    Mtxf inverse;if(!rdInverse(&m->pose,&inverse))return;
    for(int i=0;i<count;i++)if(bodies[i].used) {
        RdBody *b=&bodies[i];unsigned supported=0;
        for(int p=0;p<b->count;p++) {
            float pos[3];pdphysRagdollGetPartPos(b->handle,p,pos);rdTransform(&inverse,pos,1);
            float margin=b->radius[p]+30;
            if(fabsf(pos[0])>m->half[0]+margin || fabsf(pos[1])>m->half[1]+margin
                || fabsf(pos[2])>m->half[2]+margin)continue;
            pdphysRagdollKeepAwake(b->handle,0.2f);
            /* Only a horizontal supporting surface transfers platform
             * acceleration; doors still push entirely through contacts. */
            if(m->pose.m[1][1]>0.99f && pdphysPropSupportsPart(m->handle,b->handle,p))
                supported|=1u<<rdPiece(b,p);
        }
        if(dv)for(int p=0;p<b->count;p++)if(supported&(1u<<rdPiece(b,p)))
            pdphysRagdollCarry(b->handle,p,dv,velocity,dt);
    }
}

void rdMoversUpdate(float dt,RdBody *bodies,int count)
{
    if(!pdphysIsReady() || dt<=0)return;
    /* CE ragdollUpdateNearbyProps (props.c:1084): the pelvis and each cut
     * piece, and props whose position is within reach of one of them */
    float positions[RD_BODIES*RD_PARTS][3];int npositions=0;
    for(int i=0;i<count;i++)if(bodies[i].used)for(int p=0;p<bodies[i].count;p++)
        if((p==bodies[i].root || (bodies[i].cuts&(1u<<p))) && npositions<RD_BODIES*RD_PARTS)
            pdphysRagdollGetPartPos(bodies[i].handle,p,positions[npositions++]);
    for(int i=0;i<RD_MOVERS;i++)s_movers[i].seen=0;
    /* Only nearby solid props need Jolt bodies. Settled corpses and detached
     * pieces participate in selection too, so their supports persist. */
    for(PropRecord *p=chrpropGetActiveTail();p;p=p->prev) {
        if (p->type!=PROP_TYPE_DOOR && p->type!=PROP_TYPE_OBJ) continue;
        Mtxf pose;float half[3],q[4];
        if(!npositions)break;
        int close=0;
        for(int i=0;i<npositions && !close;i++) {
            float d2=0;
            for(int k=0;k<3;k++){float x=positions[i][k]-p->pos.f[k];d2+=x*x;}
            close=d2<(RD_PROPRANGE+400)*(RD_PROPRANGE+400);
        }
        int tracked=0;
        for(int i=0;i<RD_MOVERS && !tracked;i++)tracked=s_movers[i].prop==p;
        if((!close && !tracked) || !rdMoverGeometry(p,&pose,half,q) || !rdMoverNear(&pose,half,positions,npositions))continue;
        RdMover *m=NULL,*freeSlot=NULL;
        for(int i=0;i<RD_MOVERS;i++) {
            if(s_movers[i].prop==p)m=&s_movers[i];
            if(!s_movers[i].prop && !freeSlot)freeSlot=&s_movers[i];
        }
        if(m && (m->object!=p->obj || m->model!=p->obj->model)) {
            rdMoverWake(m,NULL,NULL,0,bodies,count);pdphysPropDestroy(m->handle);
            memset(m,0,sizeof(*m));freeSlot=m;m=NULL;
        }
        if(!m) {
            if(!freeSlot)continue;
            int handle=pdphysPropCreateBox(half);if(handle<0)continue;
            m=freeSlot;m->prop=p;m->object=p->obj;m->model=p->obj->model;m->handle=handle;
            pdphysPropMoveRot(handle,pose.m[3],q,0);
            m->pose=pose;memcpy(m->half,half,sizeof(half));
        }
        int rooms[PROPRECORD_STAN_ROOM_LEN],nrooms=0;
        for(int i=0;i<PROPRECORD_STAN_ROOM_LEN && p->rooms[i]!=255;i++)rooms[nrooms++]=p->rooms[i];
        if(!nrooms && p->stan)rooms[nrooms++]=p->stan->room;
        pdphysPropSetRooms(m->handle,rooms,nrooms?nrooms:-1);
        float velocity[3],dv[3];int changed=0,resized=0;
        float distance2=0,rotationDot=0,sizeChange=0;
        for(int k=0;k<3;k++) {
            float delta=pose.m[3][k]-m->pose.m[3][k];distance2+=delta*delta;
            velocity[k]=delta/dt;
            dv[k]=velocity[k]-m->velocity[k];
            changed|=fabsf(dv[k])>0.01f;
            resized|=fabsf(half[k]-m->half[k])>0.001f;
            sizeChange+=fabsf(half[k]-m->half[k]);
            for(int j=0;j<3;j++)rotationDot+=pose.m[k][j]*m->pose.m[k][j];
        }
        for(int i=0;i<4;i++)for(int j=0;j<3;j++)changed|=fabsf(pose.m[i][j]-m->pose.m[i][j])>0.001f;
        /* Network corrections, script warps and shape replacements have no
         * meaningful physical velocity. Avoid launching bodies at warp speed. */
        float radius=sqrtf(half[0]*half[0]+half[1]*half[1]+half[2]*half[2]);
        int warp=(sqrtf(distance2)+radius*sqrtf(fmaxf(0,3-rotationDot))+sizeChange)>2000*dt;
        if(changed || resized)rdMoverWake(m,warp?NULL:dv,velocity,dt,bodies,count);
        if(warp)memset(velocity,0,sizeof(velocity));
        if(resized && !pdphysPropResizeBox(m->handle,half))continue;
        /* Submit stationary endpoints too: kinematic velocity must stop. */
        pdphysPropMoveRot(m->handle,pose.m[3],q,warp?0:dt);
        m->pose=pose;memcpy(m->half,half,sizeof(half));memcpy(m->velocity,velocity,sizeof(velocity));m->seen=1;
    }
    for(int i=0;i<RD_MOVERS;i++)if(s_movers[i].prop && !s_movers[i].seen) {
        rdMoverWake(&s_movers[i],NULL,NULL,0,bodies,count);
        pdphysPropDestroy(s_movers[i].handle);memset(&s_movers[i],0,sizeof(s_movers[i]));
    }
}
