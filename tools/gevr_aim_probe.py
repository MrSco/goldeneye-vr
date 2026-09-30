"""Exercise the production stereo aim trace with synthetic floor and geometry hits."""
from pathlib import Path
import os
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / 'src/game/chrprop.c').read_text()
start = source.index('static s32 gevrStereoAimTrace(')
trace = source[start:source.index('\ns32 gevrStereoAimPoint(', start)]
stub = r'''
#include <cassert>
#include <cmath>
#include <cstdint>
#define GEVR 1
#define TRUE 1
#define FALSE 0
#define M_U32_MAX_VALUE_F 4294967295.0f
#define M_U16_MAX_VALUE_F 65535.0f
using s32=int; using f32=float; using u8=uint8_t; using u32=uint32_t;
union coord3d { struct { float x,y,z; }; float f[3]; };
struct coord2d { float x,y; };
struct StandTile {} tile;
struct Chr { u32 chrflags; void *field_20, *model; };
struct PropRecord { int type; Chr *chr; StandTile *stan; coord3d pos; };
enum { PROP_TYPE_CHR, PROP_TYPE_VIEWER, PROP_TYPE_OBJ, PROP_TYPE_WEAPON, PROP_TYPE_DOOR };
struct Hit { PropRecord *prop; int hitpart; void *node; float dist; };
struct ShotData { coord3d viewOrigin, viewDir, gunpos, dir; int weapon; float maxdist; Hit hits[10]; };
struct HitThing { coord3d hitpos; };
struct Portal { int offset_portal; } g_BgPortals[1]={{1}};
PropRecord player{PROP_TYPE_VIEWER,nullptr,&tile,{}}, object{PROP_TYPE_OBJ};
PropRecord *props[3]={nullptr,nullptr,&object};
PropRecord **g_OnScreenPropList=props+1, **g_LastOnScreenProp=props+2;
int g_gevrStereo=1;
float s_gevrWalkX=0, s_gevrWalkZ=0, wallDepth=0, propDepth=0, edgeDepth=11;
bool tileWalk=false, wallInConnected=false;
PropRecord *getCurrentPlayerProp() { return &player; }
int getCurrentPlayerWeaponId(int) { return 0; }
void gevrViewToWorldPos(coord3d*) {}
void *currentPlayerGetViewToWorldMtxf() { return nullptr; }
void *camGetWorldToScreenMtxf() { return nullptr; }
void mtx4RotateVecInPlace(void*,coord3d*) {}
void mtx4TransformVecInPlace(void*,coord3d*) {}
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
void chrTestHit(PropRecord*,ShotData*) {}
void sub_GAME_7F04E9BC(PropRecord *prop,ShotData *shot) {
    if (propDepth > 0 && propDepth < shot->maxdist) {
        shot->hits[0].prop=prop; shot->hits[0].dist=propDepth;
    }
}
'''
tests = r'''
void expect(float depth) {
    coord3d origin={0,0,-10}, dir={0,0,-1}, out;
    assert(gevrStereoAimTrace(0,nullptr,&origin,&dir,&out));
    assert(std::fabs(out.z+depth)<0.01f);
}
int main() {
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
    player.stan=nullptr;
    coord3d origin={}, dir={0,0,-1}, out;
    assert(!gevrStereoAimTrace(0,nullptr,&origin,&dir,&out));
}
'''
with tempfile.TemporaryDirectory(prefix='gevr-aim-') as tmp:
    tmp = Path(tmp)
    code = tmp / 'aim.cpp'
    code.write_text(stub + trace + tests)
    exe = tmp / ('aim.exe' if os.name == 'nt' else 'aim')
    compiler = os.environ.get('CXX', r'C:\Strawberry\c\bin\g++.exe' if os.name == 'nt' else 'c++')
    subprocess.run([compiler, '-std=c++20', str(code), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
print('PASS: floor edges, extended muzzle, real walls, connected rooms, props, no-hit and missing tile')
