#include "gevr_watch_status.h"
#include "gevr_hud_geometry.h"
#include "gevr_pause_menu.h"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
using s32 = int; using s16 = short; using s8 = signed char;
using f32 = float; using f64 = double;
#define M_PI_F 3.14159265358979323846f
#define TRUE 1
#define FALSE 0
#define MAX_PLAYER_COUNT 8
struct coord16 { short x,y,z; };
struct damage_display_val { coord16 pos, normal; struct { unsigned char r,g,b,a; } colour; };
struct WatchVertex { unsigned char data[16]; };
#define GAUGE_BAR_VERTEX_PAIR_STRIDE (2 * sizeof(WatchVertex))
struct Vertex { coord16 coord; };
struct ModelNode;
struct ModelRoData_DisplayListRecord { Vertex *Vertices; unsigned numVertices; };
union ModelData { float pivot[3]; struct { ModelNode *Controls; } Switch; ModelRoData_DisplayListRecord DisplayList; };
struct ModelNode { int Opcode; ModelData *Data; };
struct { ModelNode **Switches; } s_gevrWatchHeader;
float s_gevrWatchStatusRadius, s_gevrWatchStatusPlane;
struct Mtxf { float m[4][4]; }; using Mtx = Mtxf;
enum { STATE, MATRIX, VERTEX, TRIANGLE, LIST, END };
struct Gfx { int op; void *arg; } output[256];
struct Allocation { unsigned char *data; size_t size; };
std::vector<Allocation> allocations;
/* C production functions assign the typeless allocator directly. */
struct Allocated { void *data; template<class T> operator T*() const { return (T*)data; } };
Allocated dynAllocate(size_t size) {
    auto *p = (unsigned char *)calloc(1,size+32);
    assert(p); memset(p+size,0xbd,32); allocations.push_back({p,size}); return {p};
}
Mtx *dynAllocateMatrix() { return (Mtx *)dynAllocate(sizeof(Mtx)); }
uintptr_t osVirtualToPhysical(const void *p) { return (uintptr_t)p; }
/* Decode the engine's packed 16.16 values for transform assertions. */
void guMtxF2L(float m[4][4], Mtx *out) {
    for(int i=0;i<4;i++)for(int j=0;j<4;j++)out->m[i][j]=(int32_t)(m[i][j]*65536.0f)/65536.0f;
}
void record(Gfx *p,int op,void *arg) {
    bool valid = (uintptr_t)p >= (uintptr_t)output && (uintptr_t)p < (uintptr_t)(output+256);
    for(auto a:allocations) {
        auto address=(uintptr_t)p, begin=(uintptr_t)a.data;
        if(address>=begin && address+sizeof(*p)<=begin+a.size)valid=true;
    }
    assert(valid); *p={op,arg};
}
void reset() {
    for(auto a:allocations) {
        for(size_t i=0;i<32;i++)assert(a.data[a.size+i]==0xbd);
        free(a.data);
    }
    allocations.clear(); memset(output,0,sizeof(output));
}
#define gSPVertex(p,a,...) record(p,VERTEX,(void*)(a))
#define gSP1Triangle(p,...) record(p,TRIANGLE,nullptr)
#define gSP2Triangles(p,...) do { record(p,TRIANGLE,nullptr); record(p,TRIANGLE,nullptr); } while(0)
#define gSPEndDisplayList(p) record(p,END,nullptr)
#define gSPMatrix(p,a,...) record(p,MATRIX,(void*)(a))
#define gSPDisplayList(p,a) record(p,LIST,(void*)(a))
#define gDPPipeSync(p) record(p,STATE,nullptr)
#define gSPClearGeometryMode(p,...) record(p,STATE,nullptr)
#define gSPSetGeometryMode(p,...) record(p,STATE,nullptr)
#define gSPTexture(p,...) record(p,STATE,nullptr)
#define gDPSetCycleType(p,...) record(p,STATE,nullptr)
#define gDPSetRenderMode(p,...) record(p,STATE,nullptr)
#define gDPSetAlphaCompare(p,...) record(p,STATE,nullptr)
#define gDPSetCombineMode(p,...) record(p,STATE,nullptr)
struct player { int bonddead; float bondhealth,bondarmour; } players[8];
player *g_CurrentPlayer=&players[0],*g_playerPointers[8];
int g_gevrStereo=1,VrWatchFaceStatus=GEVR_WATCH_FACE_ON,VrWatchGesturePause=1,VrLeftHandedMode;
bool connected, spectator, downed;
int localSlot=7;
bool netIsActive(){return connected;}
int netGetLocalSlot(){return localSlot;}
int gevrSpectating(){return spectator;}
int gevrCoopLocalDowned(){return downed;}
void gevrPauseLocalRadar(GevrPauseRadarView *radar) {
    *radar={};
    if(connected) {radar->visible=1;radar->count=2;radar->blips[0]={0,0,255,255,255,160};radar->blips[1]={0,-.5f,255,0,0,160};}
}
/* INSERT_GAUGES */
/* INSERT_GAUGE_DL */
/* INSERT_FIT */
/* INSERT_RENDER */
int fullSegments(const damage_display_val *v) {
    int count=0;for(int i=0;i<46;i++)count+=v[i].colour.a==255;return count;
}
int main() {
    for(int i=0;i<8;i++)g_playerPointers[i]=&players[i];
    Vertex vertices[4]={{{-100,5,-100}},{{100,5,-100}},{{100,5,100}},{{-100,5,100}}};
    ModelData clock{},faceData{},toggle{};faceData.DisplayList={vertices,4};
    ModelNode clockNode{21,&clock},faceNode{4,&faceData},toggleNode{18,&toggle};
    toggle.Switch.Controls=&faceNode;
    ModelNode *switches[]={&clockNode,&clockNode,&clockNode,&toggleNode};
    s_gevrWatchHeader.Switches=switches;
    gevrWatchStatusFit();assert(fabsf(s_gevrWatchStatusRadius-94)<.001f && s_gevrWatchStatusPlane==5.25f);
    for(int choice=-1;choice<=3;choice++) {
        assert(gevrWatchShowsStandard(0,0,choice));
        assert(gevrWatchShowsStandard(1,1,choice));
        assert(gevrWatchShowsStandard(1,0,choice)==(choice!=GEVR_WATCH_FACE_ONLY));
    }
    Mtxf wrist{};for(int i=0;i<4;i++)wrist.m[i][i]=1;
    players[0].bondhealth=1;players[0].bondarmour=.5f;
    Gfx *end=gevrRenderWatchStatus(output,&wrist);assert(end>output);
    damage_display_val *health;
    // Locate lists without depending on incidental render-state command counts.
    std::vector<Gfx*> lists;
    for(Gfx *g=output;g<end;g++)if(g->op==LIST)lists.push_back((Gfx*)g->arg);
    assert(lists.size()==2);
    health=(damage_display_val *)lists[0][0].arg;
    int full=fullSegments(health);assert(full>0);
    for(int i=0;i<92;i++)assert(std::hypot(health[i].pos.x,health[i].pos.y)<GEVR_WATCH_STATUS_UNITS);
    auto *savedHealth=health;
    players[0].bondhealth=.25f;
    lists.clear();end=gevrRenderWatchStatus(output,&wrist);
    for(Gfx *g=output;g<end;g++)if(g->op==LIST)lists.push_back((Gfx*)g->arg);
    health=(damage_display_val *)lists[0][0].arg;
    assert(health!=savedHealth && fullSegments(health)<full && fullSegments(savedHealth)==full);
    reset();connected=true;players[7].bondhealth=0;players[7].bondarmour=1;
    end=gevrRenderWatchStatus(output,&wrist);int dotCount=0,listCount=0;
    for(Gfx *g=output;g<end;g++) {
        if(g->op==LIST && listCount++==0) {auto *v=(damage_display_val *)((Gfx*)g->arg)[0].arg;assert(!fullSegments(v));}
        if(g->op==VERTEX) {auto *v=(damage_display_val *)g->arg; if(dotCount++==1)assert(v[0].pos.y>0);}
    }
    assert(dotCount==2);reset();
    for(int disabled=0;disabled<4;disabled++) {
        VrWatchFaceStatus=disabled==0?GEVR_WATCH_FACE_OFF:GEVR_WATCH_FACE_ON;
        g_gevrStereo=disabled!=1; spectator=disabled==2;downed=disabled==3;
        assert(gevrRenderWatchStatus(output,&wrist)==output && allocations.empty());
    }
    float transform[4][4];g_gevrStereo=1;spectator=downed=false;
    for(int lefty=0;lefty<2;lefty++) {
        wrist.m[1][1]=lefty?-2:2;wrist.m[0][0]=wrist.m[2][2]=2;wrist.m[3][0]=17;
        gevrWatchStatusMatrix(wrist.m,clock.pivot,5.25f,94,lefty,transform);
        assert(transform[0][0]*(lefty?-1:1)>0 && transform[1][2]<0);
        assert(transform[3][0]==17 && fabsf(transform[3][1]-(lefty?-10.5f:10.5f))<.001f);
    }
    // A 26 cm forearm in a 0.2-scale stage: arbitrary wrist rotations must
    // retain the face's radius after matrix packing (under 0.2 mm error).
    const float nativeScale=.0013f, faceRadius=134.6f;
    for(int angle=0;angle<360;angle+=7) {
        const float a=angle*M_PI_F/180;
        wrist={};wrist.m[3][3]=1;
        wrist.m[0][0]=nativeScale*cosf(a);wrist.m[0][2]=nativeScale*sinf(a);
        wrist.m[1][1]=nativeScale;
        wrist.m[2][0]=-nativeScale*sinf(a);wrist.m[2][2]=nativeScale*cosf(a);
        gevrWatchStatusMatrix(wrist.m,clock.pivot,5.25f,faceRadius,0,transform);
        Mtx packed;guMtxF2L(transform,&packed);
        float error=0;
        for(int axis=0;axis<3;axis++) {
            float delta=(packed.m[0][axis]-transform[0][axis])*GEVR_WATCH_STATUS_UNITS;
            error+=delta*delta;
        }
        assert(sqrtf(error)/.2f<.02f); // cm in physical space
    }
    GevrWatchGestureState gesture{0,0,1};
    assert(!gevrWatchGestureTick(&gesture,100,1,1,0));
    assert(!gevrWatchGestureTick(&gesture,599,1,1,0));
    assert(gevrWatchGestureTick(&gesture,600,1,1,0) && gesture.pressUntil==700);
    assert(!gevrWatchGestureTick(&gesture,650,0,1,0) && !gesture.pressUntil && !gesture.heldSince);
    assert(!gevrWatchGestureTick(&gesture,651,1,1,0));
    assert(gevrWatchGestureTick(&gesture,1151,1,1,0));
    gevrWatchGestureTick(&gesture,1200,1,1,1);
    assert(!gevrWatchGestureTick(&gesture,1800,1,1,0));
    gevrWatchGestureTick(&gesture,1801,1,0,0);gevrWatchGestureTick(&gesture,1802,1,1,0);
    assert(gevrWatchGestureTick(&gesture,2302,1,1,0));
    gesture={0,0,1};gevrWatchGestureTick(&gesture,UINT32_MAX-300,1,1,0);
    assert(gevrWatchGestureTick(&gesture,199,1,1,0));
    reset();puts("PASS: production wrist geometry, dial fit, live local gauges, radar, allocation bounds, display policy and gesture lifecycle");
}
