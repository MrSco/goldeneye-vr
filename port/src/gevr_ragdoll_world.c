/* Static collision, as CE's level.c builds it: the game's walkable floors for
 * the whole stage, plus walls, overhangs and ceilings from each room's render
 * list once it has loaded. Jolt copies the mesh, so unloading the room cannot
 * invalidate collision. */
#include "gevr_ragdoll_internal.h"
#include "bg.h"
#include "stan.h"
#include "system.h"
#include "gbi_extension.h"

extern float inv_level_scale;
#define RD_RENDER_MAXUP 0.35f  /* CE level.c: added when normal.y is below this */
#define RD_ROOMSPERTICK 4      /* CE RD_CEIL_ROOMSPERTICK */
static unsigned char s_rooms[MAXROOMCOUNT];

typedef struct RdTriangles { float *v; int *ix; int count, capacity, failed; } RdTriangles;

static void rdTriangle(RdTriangles *t,const float *a,const float *b,const float *c)
{
    if(t->failed)return;
    if(t->count==t->capacity) {
        int cap=t->capacity?t->capacity*2:128;
        if(cap>65536){t->failed=1;return;}
        float *v=realloc(t->v,(size_t)cap*9*sizeof(float));
        if(!v){t->failed=1;return;} t->v=v;
        int *ix=realloc(t->ix,(size_t)cap*3*sizeof(int));
        if(!ix){t->failed=1;return;} t->ix=ix; t->capacity=cap;
    }
    memcpy(t->v+t->count*9,a,3*sizeof(float));
    memcpy(t->v+t->count*9+3,b,3*sizeof(float));
    memcpy(t->v+t->count*9+6,c,3*sizeof(float));
    for(int k=0;k<3;k++)t->ix[t->count*3+k]=t->count*3+k;
    t->count++;
}

static int rdSubmit(RdTriangles *t,int room,int group)
{
    int ok=!t->failed && t->count && pdphysLevelAddRoomGroup(room,group,t->v,t->count*3,t->ix,t->count);
    free(t->v); free(t->ix); return ok;
}

void rdWorldReset(void) { rdMoversReset(); memset(s_rooms,0,sizeof(s_rooms)); }

void rdWorldInit(void)
{
    rdWorldReset();
    for(int room=1;room<(int)(sizeof(firststaninroom)/sizeof(firststaninroom[0]));room++) {
        StandTile *tile=firststaninroom[room]; RdTriangles triangles={0};
        for(int visited=0;tile && visited<32768;visited++) {
            if(!*(u32 *)tile || tile->room!=room)break;
            int n=(tile->tail.half>>12)&15;
            if(n<3)break;
            for(int i=1;i+1<n;i++) {
                float v[3][3]; int indices[3]={0,i,i+1};
                for(int k=0;k<3;k++) {
                    StandTilePoint *p=&tile->points[indices[k]];
                    v[k][0]=p->x*inv_level_scale; v[k][1]=p->y*inv_level_scale; v[k][2]=p->z*inv_level_scale;
                }
                rdTriangle(&triangles,v[0],v[1],v[2]);
            }
            tile=(StandTile *)((u8 *)tile+8+n*8);
        }
        rdSubmit(&triangles,room,0);
    }
    pdphysLevelFinish(NULL);
}

static int rdRoomMesh(int room)
{
    s_room_info *r=&g_BgRoomInfo[room];
    Gfx *g=r->ptr_expanded_mapping_info;
    if(!r->vertices || !g || r->usize_primary_DL_binary<=0 || r->usize_point_index_binary<=0)return 0;
    int ncommands=r->usize_primary_DL_binary/sizeof(Gfx);
    if(ncommands>65536)return 0;
    int nvertices=r->usize_point_index_binary/sizeof(Vtx);
    float slots[16][3]; unsigned loaded=0; RdTriangles triangles={0};
    float scale=get_room_data_float1();
    if(!isfinite(scale) || scale<=0)return 0;
    for(int i=0;i<ncommands;i++) {
        unsigned op=(g[i].words.w0>>24)&255;
        if(op==(G_ENDDL&255))break;
        if(op==(G_VTX&255)) {
            unsigned n=((g[i].words.w0>>20)&15)+1, dest=(g[i].words.w0>>16)&15;
            uintptr_t offset=g[i].words.w1&0xffffff;
            if(offset%sizeof(Vtx) || offset/sizeof(Vtx)+n>(unsigned)nvertices || dest+n>16) { loaded=0; continue; }
            for(unsigned j=0;j<n;j++) {
                Vtx *v=&r->vertices[offset/sizeof(Vtx)+j];
                for(int k=0;k<3;k++)slots[dest+j][k]=(v->v.ob[k]+ptr_bgdata_room_fileposition_list[room].pos.f[k])/scale;
                loaded|=1u<<(dest+j);
            }
        } else if(op==(G_TRI1&255) || op==(G_TRI4&255)) {
            int n=op==(G_TRI1&255)?1:4;
            for(int j=0;j<n;j++) {
                unsigned a,b,c;
                if(n==1) { a=((g[i].words.w1>>16)&255)/10; b=((g[i].words.w1>>8)&255)/10; c=(g[i].words.w1&255)/10; }
                else { a=(g[i].words.w1>>(j*8))&15; b=(g[i].words.w1>>(j*8+4))&15; c=(g[i].words.w0>>(j*4))&15; }
                if(a==b || a==c || b==c || a>=16 || b>=16 || c>=16)continue;
                if((loaded & ((1u<<a)|(1u<<b)|(1u<<c)))!=((1u<<a)|(1u<<b)|(1u<<c)))continue;
                /* CE ragdollAddRoomCeiling (level.c:245): only triangles not
                 * facing up; up-facing ones are left to the floors, so stairs
                 * stay the smooth ramps the game walks on */
                float e1[3],e2[3],nrm[3],len;
                for(int k=0;k<3;k++){e1[k]=slots[b][k]-slots[a][k];e2[k]=slots[c][k]-slots[a][k];}
                nrm[0]=e1[1]*e2[2]-e1[2]*e2[1]; nrm[1]=e1[2]*e2[0]-e1[0]*e2[2]; nrm[2]=e1[0]*e2[1]-e1[1]*e2[0];
                len=sqrtf(nrm[0]*nrm[0]+nrm[1]*nrm[1]+nrm[2]*nrm[2]);
                if(len<0.01f || nrm[1]/len>=RD_RENDER_MAXUP)continue;
                rdTriangle(&triangles,slots[a],slots[b],slots[c]);
            }
        } else if(op==(G_DL&255) || op==(G_MTX&255)) {
            /* The room loader emits flat lists. Do not guess the address or
             * transform if a custom campaign supplies a different layout. */
            triangles.failed=1; break;
        }
    }
    const int count=triangles.failed?-1:triangles.count;
    if(count==0)return 1; /* open ground: its floors are all it needs */
    if(!rdSubmit(&triangles,room,1))return 0;
    sysLogPrintf(LOG_NOTE,"ragdoll: room %d collision, %d triangles",room,count);
    return 1;
}

void rdWorldUpdate(void)
{
    int converted=0;
    for(int room=1;room<g_MaxNumRooms && room<MAXROOMCOUNT && converted<RD_ROOMSPERTICK;room++)
        if(!s_rooms[room] && g_BgRoomInfo[room].model_bin_loaded) {
            converted++;
            /* A room's list is the same each time it loads: parse it once.
             * A failed room keeps its native floors for the whole stage. */
            if(!rdRoomMesh(room)) {
                s_rooms[room]=2;
                sysLogPrintf(LOG_WARNING,"ragdoll: room %d keeps floor-only collision",room);
                continue;
            }
            s_rooms[room]=1;
        }
}
