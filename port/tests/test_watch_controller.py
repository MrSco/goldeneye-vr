"""Run the production Controls-page render dispatch with a stale world depth."""
from pathlib import Path
import shutil
import subprocess
import tempfile

root=Path(__file__).resolve().parents[2]
source=(root/'src/game/options.c').read_text(encoding='utf-8')
start=source.index('Gfx *draw_watch_controller(Gfx *gdl)');brace=source.index('{',start)
end,depth=brace+1,1
while depth:
    depth+=(source[end]=='{')-(source[end]=='}');end+=1
function=source[start:end]
fixture=r'''
#include <assert.h>
#include <string.h>
#include <stdio.h>
#include <ultra64.h>
#include <bondtypes.h>
#include "game/player.h"
#include "game/options.h"
#include "game/gun.h"
#define WATCH_ROTATION_FRAMES 1
#define WATCH_PERSPECTIVE_FOVY 50.5f
#define WATCH_PERSPECTIVE_ASPECT 1.2838470f
static struct player player;
struct player *g_CurrentPlayer=&player;
static f32 g_WatchControllerSpinAngle,g_WatchControllerSpinSpeed,g_WatchControllerPitch;
static f32 D_80040B30,D_80040B34,D_80040B38;
static s32 D_80040B2C,D_80040B3C,controller_options_index,watch_item_is_actively_selected;
static s32 g_WatchBackgroundGreen;
static coord3d g_ControllerPos={0,200,-200};
static WatchContButtonPositions g_1ContButtonPositions[1],g_2ContLeftButtonPositions[1],g_2ContRightButtonPositions[1];
static int dual,renderCount,clearCount,worldDepth;
static Gfx *begin,*consumed;
Mtx *dynAllocateMatrix(void){static Mtx matrix;return &matrix;}
uintptr_t osVirtualToPhysical(void *p){return (uintptr_t)p;}
void guPerspective(Mtx *m,u16 *norm,float f,float a,float n,float z,float s){(void)m;(void)norm;(void)f;(void)a;(void)n;(void)z;(void)s;}
void matrix_4x4_set_identity(Mtxf *m){memset(m,0,sizeof(*m));}
void matrix_4x4_set_rotation_around_z(f32 a,Mtxf *m){(void)a;matrix_4x4_set_identity(m);}
void matrix_4x4_set_rotation_around_x(f32 a,Mtxf *m){(void)a;matrix_4x4_set_identity(m);}
void matrix_4x4_multiply(Mtxf *a,Mtxf *b,Mtxf *c){(void)a;(void)b;matrix_4x4_set_identity(c);}
void matrix_4x4_set_identity_and_position(coord3d *p,Mtxf *m){(void)p;matrix_4x4_set_identity(m);}
void matrix_4x4_set_lookat_target(Mtxf *m,f32 a,f32 b,f32 c,f32 d,f32 e,f32 f,f32 g,f32 h,f32 i){(void)a;(void)b;(void)c;(void)d;(void)e;(void)f;(void)g;(void)h;(void)i;matrix_4x4_set_identity(m);}
void sub_GAME_7F0A9684(int a,s32 *b,f32 *c,f32 *d){(void)a;(void)b;(void)c;(void)d;}
f32 watchWrapAroundPI(f32 a){return a;}
f32 sub_GAME_7F0A95C4(f32 a,f32 b,f32 c){(void)b;(void)c;return a;}
s8 joyGetStickY(s8 a){(void)a;return 0;}
u32 controllerCheckDualControllerTypesAllowed(void){return dual;}
Gfx *sub_GAME_7F0A6EE8(Gfx *g){return g;}
Gfx *watchRenderController(Gfx *g,Mtxf *m,s32 env,bool buttons,WatchContButtonPositions *p,s8 *slot){
    (void)m;(void)env;(void)buttons;(void)p;(void)slot;
    while(consumed<g){if((consumed->words.w0>>24)==0x7E){worldDepth=0;clearCount++;}consumed++;}
    assert(!worldDepth);assert(clearCount==1);renderCount++;return g;
}
Gfx *watchRenderControllerOpaque(Gfx *g,Mtxf *m,bool b,WatchContButtonPositions *p,s8 *s){return watchRenderController(g,m,255,b,p,s);}
Gfx *display_text_buttons_dual_control(Gfx *g){return g;}
Gfx *sub_GAME_7F0A9AB8(Gfx *g){return g;}
/* INSERT_FUNCTION */
int main(void){
    Gfx commands[128];
    for(dual=0;dual<2;dual++)for(int fading=0;fading<2;fading++){
        memset(commands,0,sizeof(commands));begin=consumed=commands;
        worldDepth=1;renderCount=clearCount=0;g_WatchBackgroundGreen=fading?100:224;
        draw_watch_controller(commands);assert(renderCount==(dual?2:1));assert(clearCount==1);
    }
    puts("PASS: Controls page clears inherited world depth once before opaque/fading single/dual controller models");
}
'''
with tempfile.TemporaryDirectory(prefix='gevr-watch-controller-') as temp:
    c,exe=Path(temp)/'controller.c',Path(temp)/'controller.exe'
    c.write_text(fixture.replace('/* INSERT_FUNCTION */',function),encoding='utf-8')
    args=[shutil.which('gcc') or 'gcc','-std=c11','-O2','-fms-extensions','-Wno-builtin-declaration-mismatch']
    args+=['-D'+v for v in ('GEVR=1','PLATFORM_64BIT=1','_LANGUAGE_C=1','VERSION=2','VERSION_US=1','LANG_US=1')]
    args+=['-I'+str(root/p) for p in ('.','port/include','include','src')]
    subprocess.run(args+[str(c),'-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True)
