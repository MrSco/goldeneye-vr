"""Exercise production stereo sight placement, collision scaling and surface depths."""
from pathlib import Path
import os
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / 'src/game/chrprop.c').read_text()
math = (root / 'src/game/matrixmath.c').read_text()
view = (root / 'src/game/bondview2.c').read_text()


def function(text, signature):
    start = text.index(signature)
    opening = text.index('{', start)
    end, depth = opening + 1, 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}')
        end += 1
    return text[start:end]


production = '\n'.join((
    function(math, 'void matrix_4x4_rotate_vector('),
    function(math, 'void mtx4RotateVecInPlace('),
    function(math, 'void mtx4TransformVecInPlace('),
    function(math, 'void matrix_4x4_invert_affine('),
    function(source, 'static void gevrViewToWorldPos('),
    function(view, 'static void gevrShotFromEye('),
    function(view, 's32 gevrStereoShotFromEye('),
    function(source, 'static f32 gevrStereoAimChrDepth('),
    function(source, 'static s32 gevrStereoAimTrace('),
    function(source, 's32 gevrStereoAimPoint(s32 hand, coord3d *out)\n{'),
))
stub = r'''
#include <cassert>
#include <cmath>
#include <cstdint>
#include <initializer_list>
#define GEVR 1
#define TRUE 1
#define FALSE 0
#define M_U32_MAX_VALUE_F 4294967295.0f
#define M_U16_MAX_VALUE_F 65535.0f
using s32=int; using f32=float; using u8=uint8_t; using u32=uint32_t; using s8=int8_t;
struct coord3d { union { struct { float x,y,z; }; float f[3]; }; };
struct coord2d { float x,y; };
struct StandTile {} tile;
struct Mtxf { float m[4][4]; };
struct RenderPos { Mtxf pos; } modelMatrices[5];
struct Model { RenderPos *render_pos; } guardModel{modelMatrices};
struct Bbox { float f[3][2]; };
struct NodeData { struct { Bbox Bounds; } BoundingBox; } bodyData{{{{{-10,10},{-10,10},{-10,10}}}}};
struct ModelNode { int Opcode; NodeData *Data; } bodyNode{10,&bodyData};
struct Chr { u32 chrflags; void *field_20; Model *model; s8 numclosearghs; } guard{0x20,&bodyNode,&guardModel,3};
struct ObjectRecord { Model *model; } tankObject{&guardModel};
struct PropRecord { int type; Chr *chr; StandTile *stan; coord3d pos; u32 flags; ObjectRecord *obj; };
enum { PROP_TYPE_CHR, PROP_TYPE_VIEWER, PROP_TYPE_OBJ, PROP_TYPE_WEAPON, PROP_TYPE_DOOR };
enum { MODELNODE_OPCODE_BBOX=10, GUNRIGHT=0, GUNLEFT=1, ITEM_TANKSHELLS=99, TANK_RUN_STATE_RUNNING=1 };
struct BulletHit { PropRecord *prop; int hitpart; ModelNode *node; float dist; Model *model; };
struct ShotData { coord3d viewOrigin, viewDir, gunpos, dir; int weapon; float maxdist; BulletHit hits[10]; };
struct HitThing { coord3d hitpos; };
struct Portal { int offset_portal; } g_BgPortals[1]={{1}};
PropRecord player{PROP_TYPE_VIEWER,nullptr,&tile,{}}, object{PROP_TYPE_OBJ}, character{PROP_TYPE_CHR,&guard};
PropRecord *props[3]={nullptr,nullptr,&object};
PropRecord **g_OnScreenPropList=props+1, **g_LastOnScreenProp=props+2;
int g_gevrStereo=1;
float s_gevrWalkX=0, s_gevrWalkZ=0, wallDepth=0, propDepth=0, edgeDepth=11;
float D_800364CC=1, g_TankShellSpeed=10, g_TankOrientationAngle=0, g_TankTurretOrientationAngleRad=0, g_TankTurretVerticalAngle=0;
bool tileWalk=false, wallInConnected=false, propCheckOrigin=false, meshHit=false, shotAvailable=true;
bool s_gevrCopyTrace=false, s_gevrPassAimValid[2]={};
coord3d expectedPropOrigin={}, meshPoint={0,0,12}, muzzle={0,0,-10}, barrel={0,0,-1};
Mtxf viewToWorld, worldToView;
int shotCalls[2]={};
void mtx4RotateVecInPlace(Mtxf*,coord3d*);
void mtx4TransformVecInPlace(Mtxf*,coord3d*);
PropRecord *getCurrentPlayerProp() { return &player; }
int getCurrentPlayerWeaponId(int) { return 0; }
PropRecord *get_ptr_for_players_tank() { return nullptr; }
Mtxf *currentPlayerGetViewToWorldMtxf() { return &viewToWorld; }
Mtxf *camGetWorldToScreenMtxf() { return &worldToView; }
Mtxf *modelFindNodeMtx(Model *model,ModelNode*,int) { return &model->render_pos[0].pos; }
bool propobjFindHit(Model*,ModelNode*,coord3d*,coord3d*,HitThing *hit,int *index,ModelNode **node) {
    if (!meshHit) return false;
    hit->hitpos=meshPoint; *index=0; *node=&bodyNode; return true;
}
bool netIsActive() { return false; }
int netGetLocalSlot() { return 0; }
int gevrStereoShot(int hand,coord2d*,coord3d *origin,coord3d *dir) {
    ++shotCalls[hand]; *origin=muzzle; *dir=barrel; return shotAvailable;
}
int gevrShotWalkToGun(StandTile**,PropRecord*,coord3d*) { return 1; }
int walkTilesBetweenPoints_NoCallback(StandTile**,float,float,float,float) { return tileWalk; }
void chrlvStanLineDirIntersection(coord3d*,coord3d*,coord3d *p) { *p={0,0,-edgeDepth}; }
float get_room_data_float1() { return 1; }
float get_room_data_float2() { return 1; }
float bgGetLevelVisibilityScale() { return 1; }
coord3d *bondviewGetCurrentPlayersPosition() { return &player.pos; }
void stanResetHits() {}
int getTileRoom(StandTile*) { return 1; }
int geometryHit(coord3d *from,coord3d *to,HitThing *hit) {
    if (wallDepth > -from->z && wallDepth <= -to->z) {
        hit->hitpos={0,0,-wallDepth}; return 1;
    }
    return 0;
}
int bgTestBulletHitBackground(coord3d *from,coord3d *to,int,HitThing *hit) {
    return wallInConnected ? 0 : geometryHit(from,to,hit);
}
int chrpropFindFirstBgHitInConnectedRooms(int,coord3d *from,coord3d *to,coord3d*,coord3d*,u8*,HitThing *hit) {
    return wallInConnected && geometryHit(from,to,hit) ? 2 : 0;
}
int chrpropFindClosestBgHitRoom(int n,coord3d *from,coord3d *to,coord3d *d,coord3d *s,u8 *v,HitThing *hit) {
    return chrpropFindFirstBgHitInConnectedRooms(n,from,to,d,s,v,hit);
}
int chrpropFindCloserBgHitInVisibleRooms(coord3d*,coord3d*,coord3d*,coord3d*,u8*,HitThing*,int room) { return room; }
int getPlayerPointerIndex(PropRecord*) { return 1; }
int get_cur_playernum() { return 0; }
void checkOrigin(ShotData *shot) {
    if (propCheckOrigin) for (int i=0;i<3;i++) assert(std::fabs(shot->viewOrigin.f[i]-expectedPropOrigin.f[i])<0.001f);
}
void chrTestHit(PropRecord *prop,ShotData *shot) {
    checkOrigin(shot);
    prop->chr->chrflags|=1; ++prop->chr->numclosearghs;
    if (propDepth>0 && propDepth<shot->maxdist) {
        shot->hits[0]={prop,1,&bodyNode,propDepth,&guardModel};
        shot->maxdist=propDepth; // damage registration can cap the trace at the joint
    }
}
void sub_GAME_7F04E9BC(PropRecord *prop,ShotData *shot) {
    checkOrigin(shot);
    if (propDepth > 0 && propDepth < shot->maxdist) {
        shot->hits[0].prop=prop; shot->hits[0].dist=propDepth;
    }
}
'''
tests = r'''
void identity(Mtxf &matrix) {
    matrix={}; for (int i=0;i<4;i++) matrix.m[i][i]=1;
}
void expect(float depth) {
    coord3d origin={0,0,-10}, dir={0,0,-1}, out;
    assert(gevrStereoAimTrace(0,nullptr,&origin,&dir,&out));
    assert(std::fabs(out.z+depth)<0.01f);
}
void expectScaled(float scale,float depth,int hand=GUNRIGHT) {
    D_800364CC=scale;
    coord3d origin={4*scale,6*scale,-10*scale}, dir={0,0,-1}, out;
    expectedPropOrigin={4,6,-10}; propCheckOrigin=true;
    assert(gevrStereoAimTrace(hand,nullptr,&origin,&dir,&out));
    assert(std::fabs(out.x-origin.x)<0.001f);
    assert(std::fabs(out.y-origin.y)<0.001f);
    assert(std::fabs(out.z+depth*scale)<0.001f);
    propCheckOrigin=false;
}
int main() {
    identity(viewToWorld); identity(worldToView); identity(modelMatrices[0].pos);
    // A non-walkable edge in empty air must not put the sight on the muzzle.
    expect(2010);
    edgeDepth=5; expect(2010); // edge behind a muzzle extended past the railing
    wallDepth=500; expect(500); // actual wall beyond the edge
    wallDepth=12; expect(12); // real geometry close to the muzzle still blocks
    wallDepth=500; wallInConnected=true; expect(500);
    g_BgPortals[0].offset_portal=0; expect(500);
    g_LastOnScreenProp=props+3; propDepth=100; expect(100);
    propDepth=600; expect(500); // objects behind a wall cannot win
    wallDepth=0; propDepth=100; expect(100);
    propDepth=0; tileWalk=true; expect(2010);

    // Dam/Surface (0.2) and ordinary levels (1.0): camera hit depths must
    // become rendered depths exactly once, with the same rule for both hands.
    for (float scale : {1.0f,0.2f}) {
        g_LastOnScreenProp=props+2; wallDepth=500;
        expectScaled(scale,500);
        wallDepth=11; expectScaled(scale,11); // a close hit must not be pushed through the surface
        wallDepth=500;
        g_LastOnScreenProp=props+3;
        for (int type : {PROP_TYPE_OBJ,PROP_TYPE_WEAPON,PROP_TYPE_DOOR}) {
            object.type=type; props[2]=&object;
            propDepth=100; expectScaled(scale,100,GUNLEFT);
            propDepth=600; expectScaled(scale,500);
        }
        // An oblique barrel must intersect the same scaled point, not just Z.
        coord3d origin={4*scale,6*scale,-10*scale}, dir={0.6f,0,-0.8f}, out;
        propDepth=100; expectedPropOrigin={4,6,-10}; propCheckOrigin=true;
        assert(gevrStereoAimTrace(GUNRIGHT,nullptr,&origin,&dir,&out));
        assert(std::fabs(out.x-71.5f*scale)<0.001f);
        assert(std::fabs(out.z+100*scale)<0.001f);
        propCheckOrigin=false;

        // A guard's joint is at 100, its hitbox front at 90, and its actual
        // mesh front at 88. None may put the sight inside the guard.
        props[2]=&character; propDepth=100;
        modelMatrices[0].pos.m[3][0]=4;
        modelMatrices[0].pos.m[3][1]=6;
        modelMatrices[0].pos.m[3][2]=-100;
        meshHit=false; expectScaled(scale,90);
        meshHit=true; expectScaled(scale,88);
        assert(guard.chrflags==0x20 && guard.numclosearghs==3);
        // A surface behind a nearer wall must not override that wall.
        meshPoint.z=-10; wallDepth=105; expectScaled(scale,105);
        meshPoint.z=12; wallDepth=500;
        // Safe body-box fallback for a missing mesh, including model scale.
        meshHit=false; modelMatrices[0].pos.m[2][2]=2;
        expectScaled(scale,80);
        modelMatrices[0].pos.m[2][2]=1;
        guard.field_20=nullptr; expectScaled(scale,500);
        guard.field_20=&bodyNode;
        guard.model=nullptr; expectScaled(scale,500); guard.model=&guardModel;
        props[2]=&object; propDepth=0; wallDepth=0;
        coord3d emptyOrigin={0,0,-10*scale}, forward={0,0,-1};
        assert(gevrStereoAimTrace(0,nullptr,&emptyOrigin,&forward,&out));
        assert(std::fabs(out.z-(-10*scale-2000))<0.001f); // keep the no-hit distance
    }
    // The camera-to-world transform must not rescale the shot's world origin.
    D_800364CC=0.2f; propDepth=0; g_LastOnScreenProp=props+2;
    viewToWorld.m[3][2]=1000; worldToView.m[3][2]=-1000;
    wallDepth=500; expectScaled(0.2f,1500);
    identity(viewToWorld); identity(worldToView);

    // A muzzle beyond a nearby wall still gets a sight on the near wall,
    // using the production eye-depth origin that actual bullets use.
    for (float scale : {1.0f,0.2f}) {
        D_800364CC=scale; wallDepth=50;
        muzzle={0,0,-100*scale}; barrel={0,0,-1}; coord3d out;
        assert(gevrStereoAimPoint(GUNRIGHT,&out));
        assert(std::fabs(out.z+50*scale)<0.001f);
        assert(gevrStereoAimPoint(GUNLEFT,&out));
        assert(std::fabs(out.z+50*scale)<0.001f);
    }
    assert(shotCalls[0]==2 && shotCalls[1]==2);
    shotAvailable=false; coord3d missing;
    assert(!gevrStereoAimPoint(GUNRIGHT,&missing));
    player.stan=nullptr;
    coord3d origin={}, dir={0,0,-1}, out;
    assert(!gevrStereoAimTrace(0,nullptr,&origin,&dir,&out));
}
'''
with tempfile.TemporaryDirectory(prefix='gevr-aim-') as tmp:
    tmp = Path(tmp)
    code = tmp / 'aim.cpp'
    code.write_text(stub + production + tests)
    exe = tmp / ('aim.exe' if os.name == 'nt' else 'aim')
    compiler = os.environ.get('CXX', r'C:\Strawberry\c\bin\g++.exe' if os.name == 'nt' else 'c++')
    subprocess.run([compiler, '-std=c++20', str(code), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
print('PASS: floor edges, room hits, both level scales/hands, oblique rays, prop origins, guard surfaces/state, eye-depth wall hits and no-hit cases')
