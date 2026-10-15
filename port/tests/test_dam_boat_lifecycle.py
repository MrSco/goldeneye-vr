"""Report 252ac6ae: removed/recycled Dam boat props must end their ride."""
from pathlib import Path
import argparse
import re
import os
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--verify-baseline', action='store_true')
args = parser.parse_args()


def function(source, signature):
    start = source.index(signature)
    end = source.index('{', start) + 1
    depth = 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]


fixture = r'''
#include <assert.h>
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "gevr_speedboat.h"
typedef int s32; typedef uint32_t u32; typedef unsigned char u8; typedef float f32; typedef unsigned long long u64;
typedef struct coord3d {union {struct {float x,y,z;};float f[3];};} coord3d;
typedef struct {float m[4][4];} Mtxf;
struct ObjectRecord;
struct StandTile {u8 room;};
typedef struct PropRecord {
    int type;struct ObjectRecord *obj;coord3d pos;struct StandTile*stan;u8 rooms[8];u32 flags;
} PropRecord;
struct bbox {float xmin,xmax,ymin,ymax,zmin,zmax;};
struct ModelRoData_BoundingBoxRecord {u32 ModelNumber;struct bbox Bounds;};
struct ModelRoData_GroupRecord {coord3d Origin;};
union ModelRoData {struct ModelRoData_BoundingBoxRecord BoundingBox;struct ModelRoData_GroupRecord Group;};
typedef struct ModelNode {unsigned short Opcode;union ModelRoData*Data;struct ModelNode*Parent,*Next,*Prev,*Child;} ModelNode;
enum {MODELNODE_OPCODE_GROUP=2,MODELNODE_OPCODE_BBOX=10};
struct ModelFileHeader {ModelNode*RootNode;};
struct Model {float scale;struct ModelFileHeader*obj;};
typedef struct ObjectRecord {
    u8 type,state;int obj,pad;u32 flags,flags2,runtime_bitflags;
    PropRecord*prop;struct Model*model;Mtxf mtx;coord3d runtime_pos;float damage,maxdamage;
} ObjectRecord;
struct PropDefHeaderRecord {u8 type;};
struct PadRecord {coord3d pos;void*plink;struct StandTile*stan;};
static struct {struct PadRecord*pads;struct PropDefHeaderRecord*propDefs;} g_CurrentSetup;
static struct player {
    int bonddead,watch_animation_state;
    struct {coord3d collision_position;struct StandTile*current_tile_ptr;} field_488;
    float field_70,stanHeight,field_7C,speedforwards,speedsideways,speedtheta;
    PropRecord*prop;
} player;
static struct player *g_CurrentPlayer=&player;
enum { PROPDEF_DOOR=1,PROPDEF_PROP=3,PROPDEF_KEY,PROPDEF_ALARM,PROPDEF_MAGAZINE,
       PROPDEF_COLLECTABLE,PROPDEF_VEHICHLE,PROPDEF_TANK,PROPDEF_AMMO,PROPDEF_ARMOUR,
       PROPDEF_END=48,PROP_TYPE_OBJ=1,PROP_SPEEDBOAT=290,LEVELID_DAM=33,MEMPOOL_STAGE=1,
       PROPFLAG2_NOFALL=0x100,PROPFLAG2_00020000=0x20000,RUNTIMEBITFLAG_REMOVE=4,
       PROPSTATE_DESTROYED=128,PROPFLAG_00000010=16,LOG_NOTE=1 };
static struct {struct ModelFileHeader*header;float scale;} PitemZ_entries[PROP_SPEEDBOAT+1];
static int g_gevrStereo=1,updates,registrations,deregistrations,poses,variant;
static float g_GlobalTimerDelta=1,D_800364CC=1;
static int VrGestureGripUse,handReload;
static int gevrHandReloadActive(void){return handReload;}
static struct ObjectRecord object;
static struct Model model;
static struct ModelFileHeader header;
static struct PropRecord boat,playerProp;
static struct StandTile tile={7};
static bool netIsActive(void){return false;}
static int getPlayerCount(void){return 1;}
static int gevrRomIsVariant(void){return variant;}
static int isBondInTank(void){return 0;}
static int sizepropdef(const struct PropDefHeaderRecord*p){(void)p;return 1;}
static void sysLogPrintf(int level,const char*fmt,...){(void)level;(void)fmt;}
static void modelLoad(int id){assert(id==PROP_SPEEDBOAT);}
static void* mempAllocBytesInBank(size_t size,int bank){assert(size==sizeof(object) && bank==MEMPOOL_STAGE);return &object;}
static PropRecord*objInitWithModelDef(ObjectRecord*obj,struct ModelFileHeader*h){
    obj->prop=&boat;obj->model=&model;boat.obj=obj;boat.type=PROP_TYPE_OBJ;model.obj=h;return &boat;
}
static void modelSetScale(struct Model*m,float scale){m->scale=scale;}
static void chrpropActivate(PropRecord*p){assert(p==&boat);}
static void chrpropEnable(PropRecord*p){assert(p==&boat);}
static void matrix_4x4_set_position_and_rotation_around_y(float*p,float yaw,Mtxf*m){
    (void)yaw;poses++;memset(m,0,sizeof(*m));for(int i=0;i<3;i++){m->m[i][i]=1;m->m[3][i]=p[i];}
}
static void matrix_scalar_multiply(float scale,float*m){for(int i=0;i<12;i++)m[i]*=scale;}
static struct StandTile*stanFindGroundAtCyl(coord3d*p,float r,void*cb,struct StandTile*hint,float*y,int*centre){
    (void)r;(void)cb;(void)hint;*y=p->y-80;*centre=1;return &tile;
}
static float gevrHandPropDistance(PropRecord*p,const float hand[3]){(void)hand;assert(p==&boat);return 0;}
static void chrpropDeregisterRoom(PropRecord*p,u8 room){(void)room;assert(p);deregistrations++;}
static void chrpropRegisterRooms(PropRecord*p){assert(p);registrations++;}
/* the hull's box, a level down under a group (not where chrobjGetBboxFromObjFile
 * looks): 400 tall in its model, from 100 under its origin */
static union ModelRoData hullBox={{0,{-50,50,-100,300,-200,200}}};
static ModelNode hullBoxNode={MODELNODE_OPCODE_BBOX|0x100,&hullBox,NULL,NULL,NULL,NULL};
/* its group lowers it 50, as the speedboat's does 94 */
static union ModelRoData hullOrigin={.Group={{{{0,-50,0}}}}};
static ModelNode hullGroup={MODELNODE_OPCODE_GROUP,&hullOrigin,NULL,NULL,NULL,&hullBoxNode};
static ModelNode hullRoot={0,NULL,NULL,NULL,NULL,&hullGroup};
static struct ModelRoData_BoundingBoxRecord*chrobjGetBboxFromObjectRecord(ObjectRecord*o){(void)o;return NULL;}
static float get_room_data_float2(void){return 4.0f;}
/* the dock: a floor over the water behind the jetty (z 2000, the island at 0); open water before it */
static struct StandTile*stanFindTileBelowPos(coord3d*p,u8*rooms,float*y){(void)rooms;if(p->z>2150){*y=0;return &tile;}return NULL;}
static u64 sysGetMicroseconds(void){static u64 t;return t+=2000000;}
#define EXTENT(name) static float name(struct ModelRoData_BoundingBoxRecord*b,Mtxf*m){(void)b;(void)m;return 0;}
EXTENT(chrpropSumMatrixPosX) EXTENT(chrpropSumMatrixPosY) EXTENT(chrpropSumMatrixPosZ)
EXTENT(chrpropSumMatrixNegX) EXTENT(chrpropSumMatrixNegY) EXTENT(chrpropSumMatrixNegZ)
static void chrpropUpdateRoomList(PropRecord*p,coord3d*a,coord3d*b,float f){(void)p;(void)a;(void)b;(void)f;updates++;}
/* PRODUCTION */
static void prepare(void){
    static struct PadRecord pads[4];memset(pads,0,sizeof(pads));
    for(int i=0;i<3;i++){pads[i].plink=&tile;pads[i].stan=&tile;pads[i].pos.z=i*2000;}
    pads[0].pos.y=0;pads[1].pos.y=-1000;pads[2].pos.y=0;
    g_CurrentSetup.pads=pads;PitemZ_entries[PROP_SPEEDBOAT].header=&header;header.RootNode=&hullRoot;
    PitemZ_entries[PROP_SPEEDBOAT].scale=.1f;
    memset(&boat,0,sizeof(boat));memset(&model,0,sizeof(model));memset(&player,0,sizeof(player));
    memset(&playerProp,0,sizeof(playerProp));playerProp.rooms[0]=255;
    player.prop=&playerProp;boat.rooms[0]=255;
    gevrDamBoatPrepare(LEVELID_DAM);
    assert(object.prop==&boat && boat.obj==&object);
    // Use the resulting ride coordinates; pad selection is separately tested.
    player.field_488.collision_position.x=s_ride.x;player.field_488.collision_position.z=s_ride.z;
    player.field_70=s_ride.y;
    assert(gevrDamBoatOnB() && gevrDamBoatAboard());
}
int main(int argc,char**argv){
    (void)argv;prepare();
    if(argc>1){
        // objFree leaves prop->obj pointing to the stage object but clears obj->prop.
        object.prop=NULL;object.runtime_bitflags|=RUNTIMEBITFLAG_REMOVE;
        gevrDamBoatDrive(0,80);
        return 0;
    }
    assert(object.damage==1000 && object.maxdamage==0);
    {
        /* on the water (-200 drawn at 4: -800), a tenth of the hull under it;
         * Bond at half its height, not on the jetty's deck */
        const float s=model.scale,lo=(-100-50)*s,hi=(300-50)*s,hull=-800-(lo+.1f*(hi-lo));
        assert(fabsf(boat.pos.y-hull)<.01f && fabsf(object.mtx.m[3][1]-hull)<.01f);
        assert(fabsf(player.field_70-hull)<.01f);   /* the model's origin, its gunwale */
        assert(fabsf(player.prop->pos.y-player.field_70)<.01f);
    }
    float before=s_ride.z;int oldposes=poses;
    gevrDamBoatDrive(0,80);assert(poses>oldposes && s_ride.z!=before);
    gevrDamBoatApplySeat();assert(player.prop->pos.z==s_ride.z);
    for(int kind=0;kind<7;kind++){
        prepare();int drawn=poses,registered=registrations;
        struct ObjectRecord replacement=object;
        if(kind==0)object.prop=NULL;
        if(kind==1)object.runtime_bitflags|=RUNTIMEBITFLAG_REMOVE;
        if(kind==2)object.state|=PROPSTATE_DESTROYED;
        if(kind==3)object.model=NULL;
        if(kind==4){boat.obj=&replacement;replacement.prop=&boat;object.prop=NULL;}
        if(kind==5)boat.type=4;
        if(kind==6)object.prop=&playerProp;
        // Check all public entrances independently before the others clear state.
        gevrDamBoatDrive(0,80);gevrDamBoatApplySeat();
        assert(!gevrDamBoatAboard() && !gevrDamBoatOnB());
        float hand[3]={0};assert(!gevrDamBoatTryHand(hand));
        assert(poses==drawn && registrations==registered);
        // Losing the hull aboard puts Bond back on the north jetty, not in the water.
        const struct PadRecord*jetty=&g_CurrentSetup.pads[s_jetty_pad];
        assert(s_jetty_pad>=0 && s_jetty_pad!=s_island_pad);
        assert(player.field_488.collision_position.x==jetty->pos.x);
        assert(player.field_488.collision_position.z==jetty->pos.z);
        assert(player.field_70==jetty->pos.y && player.prop->pos.y==jetty->pos.y);
        assert(player.field_488.current_tile_ptr==jetty->stan);
    }
    // Astern into the dock: the hull stops at it, its stern on the water.
    prepare();for(int k=0;k<400;k++)gevrDamBoatDrive(0,-80);
    assert(s_ride.z+s_halfLength<=2150+30 && s_ride.z+s_halfLength>2150-30);
    // The hull launched out and lying below the jetty: in reach of its outline,
    // a hand over its edge boards; a grip aboard back at the jetty steps off.
    prepare();assert(gevrDamBoatOnB() && !gevrDamBoatAboard());
    {
        float edge[3]={s_ride.x,0,s_ride.z-s_halfLength*cosf(s_ride.yaw)-40*cosf(s_ride.yaw)};
        edge[0]=s_ride.x-(s_halfLength+40)*sinf(s_ride.yaw);
        // standing a step from its edge
        player.field_488.collision_position.x=s_ride.x;player.field_488.collision_position.z=s_ride.z+s_halfLength+50;
        assert(gevrDamBoatTryHand(edge) && gevrDamBoatAboard());
        assert(gevrDamBoatTryHand(edge) && !gevrDamBoatAboard());
    }
    // With grip to use, B is not the boat's.
    prepare();VrGestureGripUse=1;
    assert(!gevrDamBoatOnB() && gevrDamBoatAboard());VrGestureGripUse=0;
    // Hand reload with GE-X forces grip use: B ejects magazines, not the boat's.
    prepare();handReload=1;assert(!gevrDamBoatOnB() && gevrDamBoatAboard());handReload=0;
    // B at the jetty steps off there; aboard mid-crossing, it does nothing.
    prepare();assert(gevrDamBoatOnB() && !gevrDamBoatAboard());
    assert(player.field_488.collision_position.z==g_CurrentSetup.pads[s_jetty_pad].pos.z);
    assert(player.field_70==g_CurrentSetup.pads[s_jetty_pad].pos.y);
    prepare();s_ride.x=s_ride.anchor_x;s_ride.z=s_ride.anchor_z;
    assert(gevrDamBoatOnB() && gevrDamBoatAboard());
    prepare();object.prop=NULL;assert(!gevrDamBoatAboard());
    // Dead aboard: no warp; the death camera keeps where he fell.
    prepare();player.field_70=-12345;player.bonddead=1;object.state|=PROPSTATE_DESTROYED;
    assert(!gevrDamBoatAboard() && player.field_70==-12345);player.bonddead=0;
    prepare();object.prop=NULL;assert(!gevrDamBoatOnB());
    prepare();object.prop=NULL;float hand[3]={0};assert(!gevrDamBoatTryHand(hand));
    prepare();object.prop=NULL;int old=poses;gevrDamBoatApplySeat();assert(old==poses);
    prepare();gevrDamBoatPrepare(90);assert(!gevrDamBoatAboard() && !gevrDamBoatOnB());
    old=registrations;setupUpdateObjectRoomPosition(NULL);
    object.prop=NULL;setupUpdateObjectRoomPosition(&object);assert(old==registrations);
    puts("PASS: Dam boat health, live ride, destruction/removal back to the jetty, recycled prop slots, lost model, all ride entry points, stage reset and detached room updates under ASan/UBSan");
}
'''


def build(directory, baseline=False):
    def read(path):
        if baseline:
            return subprocess.check_output(['git', 'show', 'HEAD:' + path], cwd=ROOT, text=True)
        return (ROOT / path).read_text()
    boat = re.sub(r'^#include[^\n]*\n', '', read('src/game/gevr_dam_boat.c'), flags=re.M)
    production = function(read('src/game/chrprop.c'), 'void chrpropDeregisterRooms(')
    production += '\n' + function(read('src/game/loadobjectmodel.c'), 'void setupUpdateObjectRoomPosition(')
    production += '\n' + boat
    c, exe = directory / ('baseline.c' if baseline else 'test.c'), directory / ('baseline' if baseline else 'test')
    c.write_text(fixture.replace('/* PRODUCTION */', production))
    subprocess.run([shutil.which('gcc') or 'gcc', '-std=c11', '-O1', '-g', '-DGEVR',
                    '-fsanitize=address,undefined', '-fno-sanitize-recover=all', '-no-pie',
                    '-I' + str(ROOT / 'port/include'), str(c),
                    str(ROOT / 'port/src/gevr_speedboat.c'), '-lm', '-o', str(exe)], check=True)
    return exe


with tempfile.TemporaryDirectory(prefix='gevr-boat-lifecycle-') as directory:
    temp = Path(directory)
    exe = build(temp)
    subprocess.run([str(exe)], check=True)
    if args.verify_baseline:
        exe = build(temp, baseline=True)
        result = subprocess.run([str(exe), 'removed'], capture_output=True, text=True,
                                env={**os.environ, 'UBSAN_OPTIONS': 'print_stacktrace=1'})
        assert result.returncode != 0 and 'null pointer' in result.stderr, result.stdout + result.stderr
        assert 'chrpropDeregisterRooms' in result.stderr, result.stderr
        print('CONFIRMED: HEAD reproduces report 252ac6ae in chrpropDeregisterRooms after boat removal')
