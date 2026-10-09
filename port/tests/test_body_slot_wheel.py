"""Exercise the shared wheel ring, category choices and spinning preview routing."""
from pathlib import Path
import re
import shutil
import subprocess
import tempfile

ROOT=Path(__file__).resolve().parents[2]
view=(ROOT/'src/game/bondview2.c').read_text(encoding='utf-8')
def function(signature):
    match=re.search(re.escape(signature)+r'\s*\{',view)
    assert match,signature
    start=match.start(); end=view.index('{',start)+1; depth=1
    while depth:
        depth+=(view[end]=='{')-(view[end]=='}'); end+=1
    return view[start:end]

defs='\n'.join(re.findall(r'^#define GEVR_(?:WP_(?:W|H|MODEL_W|MODEL_H)|WC_(?:R0|R1|POP|TEXTR|ROWS|SEGS|GAP|HOLE_SEGS|QUADS|TEXTS))\s+[^\n]+',view,re.M))
defs+='\n'+re.search(r'static const u8 s_gevrWcTint\[.*?\};',view,re.S).group()
defs+='\n'+re.search(r'static const char \*s_gevrWcLabel\[.*?;',view,re.S).group()
defs+='\n'+re.search(r'static struct damage_display_val s_gevrWcVtx\[.*?;',view,re.S).group()
defs+='\n'+re.search(r'static Gfx s_gevrWcDl\[.*?;',view,re.S).group()
production='\n'.join(function(s) for s in (
    'static void gevrWheelVtx(struct damage_display_val *v, f32 cx, f32 cy, f32 r, f32 deg, u32 rgba)',
    'static u32 gevrWheelShade(s32 cat, s32 empty, s32 lit, s32 outer)',
    'static s32 gevrWheelSegmentRow(s32 cat, s32 count, s32 dy, s32 pop, s32 *mid)',
    'static s32 gevrWheelRow(s32 cat, s32 dy, s32 pop, s32 *mid)',
    'static Gfx *gevrWheelDrawSegments(Gfx *gdl, s32 cx, s32 cy, s32 active, s32 count,\n                                 const s32 *categories, const s32 *emptyFlags)',
    'static Gfx *gevrWheelDrawRing(Gfx *gdl, s32 cx, s32 cy, s32 active)',
    'static Gfx *gevrDrawBodySlotWheel(Gfx *gdl, const GevrBodySlotWheel *slot)'))
HARNESS=r'''
#include <ultra64.h>
#include <bondtypes.h>
#include <bondconstants.h>
#include <assert.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "gevr_bodyslots.h"
#define GEVR_WC_COUNT 5
static struct WheelPlayer { Vp viewports[2]; } player;
static void *projection;
static struct WheelPlayer *g_CurrentPlayer=&player;
u8 g_GfxActiveBufferIndex,g_ViBackIndex;
static s32 gevrWeaponPanelLeft,gevrWeaponPanelBody;
static float gevrWeaponPanelRect[4],gevrWeaponPanelAspect;
static s32 j_text_trigger;
static struct { int n[5]; } s_gevrWc={{1,0,1,1,1}};
s16 viGetX(void) { return 320; }
s16 viGetY(void) { return 240; }
void *dynAllocate(s32 bytes) { return calloc(1,bytes); }
Mtx *dynAllocateMatrix(void) { return dynAllocate(sizeof(Mtx)); }
uintptr_t osVirtualToPhysical(void *p) { return (uintptr_t)p; }
static void matrix_4x4_set_identity(Mtxf *m) { memset(m,0,sizeof(*m)); for(int i=0;i<4;i++)m->m[i][i]=1; }
static void matrix_4x4_7F058C64(void) {}
static void matrix_4x4_7F058C88(void) {}
static void matrix_4x4_f32_to_s32(f32 m[4][4],s32 out[4][4]) { memset(out,0,sizeof(Mtx)); }
void guOrtho(Mtx *m,float l,float r,float b,float t,float n,float f,float s) { memset(m,0,sizeof(*m)); }
static void *currentPlayerGetProjectionMatrix(void) { return projection; }
static Gfx *microcode_constructor(Gfx *gdl) { return gdl; }
static Gfx *combiner_bayer_lod_perspective(Gfx *gdl) { return gdl; }
static void gevrWcShorten(const char *in,char *out,s32 size) { snprintf(out,size,"%s",in); }
static int labels,models,shown,shownLeft,whiteLabels;
static Gfx *gevrWpText(Gfx *gdl,const char *text,s32 x,s32 y,u32 tint,s32 height,s32 width,void *unused) {
    assert(width>0 && width<=236 && x>=0 && x<=320 && y>=0 && y<240);
    labels++; whiteLabels+=tint==0xFFFFFFFF; return gdl;
}
static Gfx *gevrDrawWeaponPanelModel(Gfx *gdl,s32 item,s32 x,s32 y,s32 w,s32 h) {
    assert(x==131 && y==96 && w==58 && h==48);
    models++; shown=item; shownLeft=gevrWeaponPanelLeft; return gdl;
}
/* DEFINITIONS */
/* PRODUCTION */
int main(void) {
    Gfx commands[64];
    for (int buffer=0;buffer<2;buffer++) for (int n=1;n<=GEVR_BODY_WHEEL_MAX;n++)
    for (int ctrl=0;ctrl<2;ctrl++) {
        g_GfxActiveBufferIndex=buffer;
        GevrBodySlotWheel slot={0}; slot.ctrl=ctrl; slot.count=n; slot.category=3; slot.index=n-1; slot.ready=1;
        for(int i=0;i<n;i++) { slot.items[i]=ITEM_TASER; snprintf(slot.names[i],48,"item %d",i); }
        gevrWeaponPanelLeft=7; gevrWeaponPanelBody=0; labels=models=whiteLabels=0;
        Gfx *end=gevrDrawBodySlotWheel(commands,&slot);
        assert(end>commands && end<commands+64 && models==1 && shown==ITEM_TASER && shownLeft==(ctrl==0));
        assert(gevrWeaponPanelBody && gevrWeaponPanelLeft==7 && labels>=3 && whiteLabels>=1);
        for(int i=0;i<4;i++) assert(gevrWeaponPanelRect[i]>=0 && gevrWeaponPanelRect[i]<=1);
        const int quads=GEVR_WC_HOLE_SEGS+n*GEVR_WC_SEGS;
        assert(s_gevrWcDl[buffer][quads*3].words.w0==((u32)G_ENDDL<<24));
        for(int i=0;i<quads*4;i++) {
            assert(s_gevrWcVtx[buffer][i].pos.x>=0 && s_gevrWcVtx[buffer][i].pos.x<=320);
            assert(s_gevrWcVtx[buffer][i].pos.y>=0 && s_gevrWcVtx[buffer][i].pos.y<=240);
        }
        slot.items[n-1]=-2; gevrDrawBodySlotWheel(commands,&slot); assert(shown==ITEM_FIST);
        slot.items[n-1]=ITEM_WATCHLASER; gevrDrawBodySlotWheel(commands,&slot); assert(shown==ITEM_WATCHMAGNETATTRACT);
        s32 mid; assert(gevrWheelRow(0,-70,0,&mid)>0);
    }
    /* The original five-category wheel still uses the shared ring. */
    gevrWheelDrawRing(commands,160,120,2);
    puts("PASS: shared slot wheel geometry, 1..24 choices, crop bounds, model/hand routing and original wheel");
}
'''
with tempfile.TemporaryDirectory(prefix='gevr-slot-wheel-') as temp:
    source=Path(temp)/'test.c'; exe=Path(temp)/'test.exe'
    source.write_text(HARNESS.replace('/* DEFINITIONS */',defs).replace('/* PRODUCTION */',production),encoding='utf-8')
    subprocess.run([shutil.which('gcc') or 'gcc','-std=c11','-O2','-fms-extensions','-D_LANGUAGE_C',
        '-I'+str(ROOT),'-I'+str(ROOT/'include'),'-I'+str(ROOT/'src'),'-I'+str(ROOT/'src/game'),
        '-I'+str(ROOT/'port/include'),str(source),'-lm','-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True)
