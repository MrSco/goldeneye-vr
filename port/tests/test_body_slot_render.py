"""Visible front slots keep their item cues alongside hip reload guides."""
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]


def function(source, signature):
    start = source.index(signature)
    end = source.index('{', start) + 1
    depth = 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]


body = (ROOT / 'src/game/gevr_bodyslots.c').read_text()
video = (ROOT / 'src/fr.c').read_text()
fixture = r'''
#include <assert.h>
#include <stdio.h>
#include <stdint.h>
#include "gevr_bodyslot.h"
typedef int s32;typedef float f32;typedef uint32_t u32;typedef unsigned Gfx;
typedef struct {float m[4][4];} Mtxf;
typedef struct {int zbufferenabled,flags,PropType;union {u32 word;} envcolour;} ModelRenderData;
#define TRUE 1
#define FALSE 0
#define gDPNoOpTag(p,tag) ((void)(p))
#define gSPPerspNormalize(p,n) ((void)(p),(void)(n))
static int s_bodyLive=1,s_bodyFrameValid=1,VrBodySlotShow=1,g_gevrExtraPass;
static struct {struct {unsigned char r,g,b,a;} tileColor;} player;
static typeof(player) *g_CurrentPlayer=&player;
static struct {int slot;float ms;} s_bodyHand[2]={{-1,0},{-1,0}};
static f32 s_bodyCentre[GEVR_BODY_SLOTS][3],D_800364CC=1;
static int items[GEVR_BODY_SLOTS],visible=1,reload[2]={-1,-1},draws,roots,instances[4],drawn[4];
static Gfx *gevrReloadBeltDraw(Gfx*g){return g;}
static int gevrBodyShownItem(int s,int*by){*by=-1;return items[s];}
static void gevrBodyToView(const float*p,float k,float*out){(void)k;for(int i=0;i<3;i++)out[i]=p[i];}
static int gevrBodyInView(const float*p){(void)p;return visible;}
static int gevrReloadBeltAmmo(int ctrl){return reload[ctrl];}
static unsigned gevrBodyCategoryTint(int cat){(void)cat;return 0x80402000;}
static void gevrBodySlotRoot(int s,int item,Mtxf*m){(void)s;(void)item;(void)m;roots++;}
static int gevrBodyItemPosed(int item){return item==40;}
static int matrix_4x4_calc_depth_scale(float a,float b){(void)a;(void)b;return 1;}
static int viGetPerspNorm(void){return 1;}
static void matrix_4x4_7F058C64(void){}
static void matrix_4x4_7F058C88(void){}
static Gfx*gevrHeldGunDrawPosed(int inst,int item,Mtxf*m,ModelRenderData*rd,Gfx*g){
    (void)m;assert(rd->zbufferenabled && rd->PropType==4);
    assert(draws<4 && inst>=0 && inst<4);instances[draws]=inst;drawn[draws++]=item;return g+1;
}
static int g_gevrStereo,g_ViShakeIntensity,g_ViShakeTimer;
/* PRODUCTION */
int main(void){
    Gfx commands[64];
    for(int s=0;s<GEVR_BODY_SLOTS;s++)items[s]=10+s;
    // Both shoulders have items, but front cues use exactly four model instances.
    gevrBodySlotsDraw(commands);
    assert(draws==4 && roots==4);
    assert(drawn[0]==10 && drawn[1]==11 && drawn[2]==14 && drawn[3]==15);
    for(int i=0;i<4;i++)assert(instances[i]==i);
    draws=roots=0;reload[0]=reload[1]=1;
    gevrBodySlotsDraw(commands);
    assert(draws==2 && roots==2 && drawn[0]==14 && drawn[1]==15);
    draws=roots=0;visible=0;
    gevrBodySlotsDraw(commands);assert(!draws && !roots);
    visible=1;VrBodySlotShow=0;gevrBodySlotsDraw(commands);assert(!draws);
    VrBodySlotShow=1;g_gevrExtraPass=1;gevrBodySlotsDraw(commands);assert(!draws);
    g_gevrExtraPass=0;items[GEVR_BS_CHEST]=items[GEVR_BS_BELT]=-1;
    gevrBodySlotsDraw(commands);assert(!draws);
    viShake(20);assert(g_ViShakeIntensity==14 && g_ViShakeTimer==10);
    g_gevrStereo=1;viShake(20);assert(!g_ViShakeIntensity && !g_ViShakeTimer);
    g_gevrStereo=0;viShake(-2);assert(!g_ViShakeIntensity);
    puts("PASS: chest/belt cues, four-instance bound, hip reload isolation, invisible/empty/disabled slots and VR screen-shake suppression");
}
'''
production = function(body, 'Gfx *gevrBodySlotsDraw(') + '\n' + function(video, 'void viShake(')
with tempfile.TemporaryDirectory(prefix='gevr-slot-render-') as directory:
    temp = Path(directory)
    source, exe = temp / 'render.c', temp / 'render'
    source.write_text(fixture.replace('/* PRODUCTION */', production))
    subprocess.run([shutil.which('gcc') or 'gcc', '-std=gnu11', '-O2', '-Wall', '-Werror',
                    '-DGEVR', '-I' + str(ROOT / 'port/include'), str(source),
                    str(ROOT / 'port/src/gevr_bodyslot.c'), '-lm', '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
