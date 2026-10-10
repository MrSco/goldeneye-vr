"""Check actual gadget/hand transforms against moving, rotated controllers."""
from pathlib import Path
import argparse
import re
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]


def function(text, signature):
    start = text.index(signature)
    end = text.index('{', start) + 1
    depth = 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}')
        end += 1
    return text[start:end]


parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--source-ref', help='Check the earlier GE-X hand mirror')
parser.add_argument('--legacy-gadget-mirror', action='store_true', help='Reproduce the mirror around the gun trim instead of the hand fit')
args = parser.parse_args()
gun = (ROOT / 'src/game/gun.c').read_text(encoding='utf-8')
live_gun = gun
if args.source_ref:
    gun = subprocess.check_output(['git', 'show', args.source_ref + ':src/game/gun.c'],
                                 cwd=ROOT, text=True, encoding='utf-8')
fire = (ROOT / 'src/game/gunfire.c').read_text(encoding='utf-8')
if args.legacy_gadget_mirror:
    fire = subprocess.check_output(['git','show','v0.4.18:src/game/gunfire.c'],cwd=ROOT,text=True,encoding='utf-8')
view = (ROOT / 'src/game/bondview2.c').read_text(encoding='utf-8')
math = (ROOT / 'src/game/matrixmath.c').read_text(encoding='utf-8')
gex = function(gun, 'Gfx *gevrGexDrawItemHand(')
mirror = function(gex, 'if (g_gevrStereo && hand == GUNLEFT)')
scale = function(gex, 'for (i = 0; g_gevrStereo && i < 3; i++)')
legacy = function(function(fire, 'static Gfx *gevrRenderItemHand('), 'if (handnum == GUNLEFT)')
poses = view[view.index('typedef struct\n{\n    s32 item;'):view.index('#define GEVR_ITEMPOSE_COUNT')]
production = '\n'.join(function(math, s) for s in ('void matrix_4x4_set_rotation_around_xyz(',
    'void matrix_column_1_scalar_multiply(', 'void matrix_scalar_multiply('))
production += '\n' + poses
production += '''
static GevrItemPose *gevrItemPoseFind(s32 item) {
    if (modelSet && item == ITEM_BUG) return (GevrItemPose *)&s_gevrItemPosesGex[0];
    for (unsigned i=0;i<sizeof(s_gevrItemPoses)/sizeof(s_gevrItemPoses[0]);i++)
        if (s_gevrItemPoses[i].item == item) return &s_gevrItemPoses[i];
    return NULL;
}
'''
production += function(view, 'void gevrStereoItemPose(')
production += '\n' + function(fire, 'static void gevrLeftGadgetMirror(')
production += '\nstatic Mtxf gexHand(s32 hand) { Mtxf base; gevrStereoGunMatrixFit(hand,fit,&base); s32 i; ' + mirror + scale + 'return base; }'
production += '\nstatic Mtxf legacyHand(s32 handnum) { Mtxf armmtx=frame[handnum]; ' + legacy
production += 'matrix_scalar_multiply(IDO_POINT_ONE,armmtx.m[0]); return armmtx; }'
production += '\n' + re.search(r'#define GEVR_GEX_CUFF_Z[^\n]+',live_gun)[0]
production += '\n' + function(live_gun, 'static void gevrGexMtxPoint(const Mtxf *m, const f32 local[3], f32 out[3])\n{')
production += '\n' + function(live_gun, 'static s32 gevrGexWatchFrame(')
production += '\n' + function(live_gun, 'static Gfx *gevrGexDrawItemWatch(')
assert 'gevrGexDrawItemWatch(gdl, &renderdata, hand, &m[GEVR_GEX_RHAND_ARM])' in function(live_gun,'Gfx *gevrGexDrawItemHand(')
harness = r'''
#include <ultra64.h>
#undef GAME_TICKRATE
#include <bondtypes.h>
#include <bondconstants.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#define CHECK(x) do { if (!(x)) { printf("FAIL line %d\n",__LINE__); return 1; } } while(0)
#define GEVR_UNITS_PER_METRE 100.0f
static int g_gevrStereo=1,modelSet;
int VrGexGuns,g_gevrGexOffWatch;
static float fit[3]={3.2f,1.5f,-2.3f},VrGexWatch[4]={2,3,4,1};
static float D_800364CC=1.0f,sizeFactor=1.0f;
static Mtxf frame[2];
static float watchAt[3];static int watchDraws;
static f32 gevrGunSizeFactor(void) { return sizeFactor; }
static s32 gevrStereoGunMatrix(s32 hand,Mtxf *out) { *out=frame[hand]; return 1; }
static f32 *gevrGexGunFit(s32 item) { return fit; }
static s32 gevrStereoGunMatrixFit(s32 hand,const f32 off[3],Mtxf *out) {
    *out=frame[hand];
    for(int j=0;j<3;j++) for(int axis=0;axis<3;axis++) out->m[3][j]+=off[axis]*frame[hand].m[axis][j];
    return 1;
}
Gfx *gevrRenderGexWatch(Gfx *gdl,ModelRenderData *templ,const f32 pos[3],const f32 x[3],const f32 y[3]) {
    if(!g_gevrGexOffWatch) return gdl;
    watchDraws++;memcpy(watchAt,pos,sizeof(watchAt));return gdl+1;
}
/* PRODUCTION */
int main(void) {
    const s32 items[]={ITEM_BUG,ITEM_CAMERA,ITEM_DATATHIEF,ITEM_KEYANALYSERCASE,ITEM_TIMEDMINE};
    for(int set=0;set<2;set++) for(int handed=0;handed<2;handed++) for(int size=1;size<=2;size++) {
        modelSet=VrGexGuns=set;sizeFactor=size;
        for(int step=0;step<4;step++) {
            coord3d angles={0.2f+step*0.3f,-0.7f+step*0.4f,0.4f-step*0.5f};
            Mtxf rotation;matrix_4x4_set_rotation_around_xyz(&angles,&rotation);
            for(int hand=0;hand<2;hand++) {
                frame[hand]=rotation;
                for(int i=0;i<3;i++) for(int j=0;j<3;j++) frame[hand].m[i][j]*=size*(i==0 && handed ? -1:1);
                for(int j=0;j<3;j++) frame[hand].m[3][j]=7.0f+step*(j+1)*3.0f+hand*(j+2);
                Mtxf arm=set ? gexHand(hand):legacyHand(hand);
                Mtxf anchor=frame[hand];if(set) gevrStereoGunMatrixFit(hand,fit,&anchor);
                for(int i=0;i<3;i++) for(int j=0;j<3;j++)
                    CHECK(fabsf(arm.m[i][j]-frame[hand].m[i][j]*0.1f*(i==0 && hand==GUNLEFT ? -1:1))<1e-5f);
                for(int j=0;j<3;j++) CHECK(arm.m[3][j]==anchor.m[3][j]);
                for(unsigned item=0;item<sizeof(items)/sizeof(items[0]);item++) {
                    Mtxf gadget=frame[hand];gevrStereoItemPose(items[item],&gadget);
                    if(hand==GUNLEFT) gevrLeftGadgetMirror(&gadget);
                    GevrItemPose *p=gevrItemPoseFind(items[item]);
                    for(int axis=0;axis<3;axis++) {
                        float local=0;
                        for(int j=0;j<3;j++) local+=(gadget.m[3][j]-arm.m[3][j])*frame[hand].m[axis][j]/size;
                        CHECK(fabsf(local-(p->ofs[axis]-(set?fit[axis]:0))*size*(axis==0 && hand==GUNLEFT ? -1:1))<1e-4f);
                    }
                }
                Gfx commands[8];ModelRenderData templ={0};watchDraws=0;
                CHECK(gevrGexDrawItemWatch(commands,&templ,hand,&arm)==commands+(hand==GUNLEFT));
                CHECK(watchDraws==(hand==GUNLEFT) && !g_gevrGexOffWatch);
                if(hand==GUNLEFT) {
                    float pos[3],x[3],y[3];CHECK(gevrGexWatchFrame(&arm,pos,x,y));
                    for(int j=0;j<3;j++) CHECK(fabsf(watchAt[j]-pos[j])<1e-5f);
                }
            }
        }
    }
    Gfx commands[8];ModelRenderData templ={0};g_gevrStereo=0;watchDraws=0;
    CHECK(gevrGexDrawItemWatch(commands,&templ,GUNLEFT,&frame[GUNLEFT])==commands && !watchDraws);
    g_gevrStereo=1;memset(&frame[GUNLEFT],0,sizeof(Mtxf));
    CHECK(gevrGexDrawItemWatch(commands,&templ,GUNLEFT,&frame[GUNLEFT])==commands && !watchDraws);
    puts("PASS: separate hand/gadget fits, either hand/handedness, moving controllers, both model sets, wrist watch and size cheats");
    return 0;
}
'''
with tempfile.TemporaryDirectory(prefix='gevr-gadget-hand-') as directory:
    temp = Path(directory)
    source, exe = temp / 'test.c', temp / 'test.exe'
    source.write_text(harness.replace('/* PRODUCTION */', production), encoding='utf-8')
    flags = ['-std=c11', '-O2', '-D_LANGUAGE_C', '-DGEVR', '-DVERSION_US']
    flags += ['-I' + str(ROOT / p) for p in ('.', 'include', 'src', 'port/include')]
    subprocess.run([shutil.which('gcc') or 'gcc', *flags, str(source), '-lm', '-o', str(exe)], check=True)
    result = subprocess.run([str(exe)])
    if args.source_ref or args.legacy_gadget_mirror:
        assert result.returncode != 0, 'Earlier hand mirror unexpectedly passed'
        print('CONFIRMED: earlier mirror fails hand/gadget alignment')
    else:
        result.check_returncode()
