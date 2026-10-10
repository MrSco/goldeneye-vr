"""Exercise the production movement scaler and live watch controls natively."""
from pathlib import Path
import re
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]


def function(source, signature):
    start = source.index(signature)
    brace = source.index('{', start)
    end, depth = brace + 1, 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]


game = (ROOT / 'src/game/bondview2.c').read_text(encoding='utf-8')
options = (ROOT / 'src/game/options.c').read_text(encoding='utf-8')
menu = (ROOT / 'src/game/mpmenu.c').read_text(encoding='utf-8')
# Compile the actual watch row's switch arm with its production clamp helper.
step = function(options, 'static void gevrVrStep(s32 row, s32 dir)')
arm = step[step.index('        case GEVR_VR_MOVESPEED:'):step.index('        case GEVR_VR_TURN:')]
fixture = '''#include <assert.h>
#include <math.h>
#include "net_rules.h"
typedef float f32;
typedef int s32;
static int online, host, active, VrMovementSpeed, savedHost;
enum {CFG_MOVEMENT_SPEED, GEVR_VR_MOVESPEED};
static int netIsActive(void) {return online;}
static int netActiveMovementSpeed(void) {return active;}
static int gevrNetConfigGet(int field) {(void)field;return active;}
static void gevrNetConfigSet(int field,int value) {
    (void)field;if(host)active=savedHost=value;
}
''' + function(game, 'static void gevrScalePlayerWalk(f32 *x, f32 *z)') + '\n'
fixture += function(options, 'static s32 gevrVrClampStep(') + '\n'
fixture += function(menu, 'static s32 gevrCycled(') + '\n'
fixture += function(menu, 'static void rowMovementSpeedStep(s32 dir)') + '\n'
fixture += 'static void watchStep(s32 dir) {s32 row=GEVR_VR_MOVESPEED,i;switch(row) {\n' + arm + '}}\n'
fixture += '''int main(void) {
    const int percents[]={50,75,100,125,150,175,200};
    for(int i=0;i<7;i++) {
        int mode=netMovementSpeedMode(percents[i]);
        assert(netMovementSpeedPercent(mode)==percents[i]);
        VrMovementSpeed=mode;float x=4,z=-8;gevrScalePlayerWalk(&x,&z);
        assert(fabsf(x-4*percents[i]/100.f)<.00001f);
        assert(fabsf(z+8*percents[i]/100.f)<.00001f);
    }
    VrMovementSpeed=NET_MOVE_NORMAL;watchStep(1);assert(VrMovementSpeed==NET_MOVE_125);
    watchStep(-1);assert(VrMovementSpeed==NET_MOVE_NORMAL);
    VrMovementSpeed=NET_MOVE_200;watchStep(2);assert(VrMovementSpeed==NET_MOVE_50);
    online=host=1;active=NET_MOVE_75;watchStep(1);
    assert(active==NET_MOVE_NORMAL && savedHost==NET_MOVE_NORMAL && VrMovementSpeed==NET_MOVE_50);
    rowMovementSpeedStep(4);assert(active==NET_MOVE_200);
    host=0;watchStep(-1);rowMovementSpeedStep(-1);assert(active==NET_MOVE_200);
    float x=4,z=-8;gevrScalePlayerWalk(&x,&z);assert(x==8 && z==-16);
    online=0;x=4;z=-8;gevrScalePlayerWalk(&x,&z);assert(x==2 && z==-4);
    VrMovementSpeed=999;x=4;z=-8;gevrScalePlayerWalk(&x,&z);assert(x==4 && z==-8);
    return 0;
}
'''
# Guard the integration boundary: scale only animation travel, before physical tracking.
move = function(game, 'void MoveBond(')
assert move.count('gevrScalePlayerWalk(') == 1
assert move.index('headpos_z =') < move.index('gevrScalePlayerWalk(') < move.index('gevrStereoHeadWalk(')
assert 'GEVR_VR_MOVESPEED' in re.search(r's_gevrVrComfort\[\].*?;', options).group()
with tempfile.TemporaryDirectory(prefix='gevr-speed-') as temp:
    source, exe = Path(temp) / 'speed.c', Path(temp) / 'speed.exe'
    source.write_text(fixture, encoding='utf-8')
    subprocess.run([shutil.which('gcc') or 'gcc', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror', '-Wno-unused-parameter',
                    '-I' + str(ROOT / 'port/src/net'), str(source), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
print('PASS: movement multipliers, immediate solo/host watch controls, client authority and room-scale boundary')
