"""Compile the watch's production setup and page colours after a lit Bean model."""
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
source = (ROOT / "src/game/options.c").read_text()
start = source.index("void set_page_rectangle_colors(s32 watch_screen_index, struct WatchVertex *vertices)\n{")
end = source.index("\n\n/**", start)
colours = source[start:end]
start = source.index("Gfx *draw_background_health_and_armor(Gfx *gdl, Mtx *arg1, s32 zoom_squish)\n{")
start = source.index("    gDPPipeSync(gdl++);", start)
setup = source[start:source.index("    // end initial setup.", start)]
c = '''#include <ultra64.h>
#include <PR/gbi.h>
#include <assert.h>
#include "gevr_bean_fog.h"
#include <stdio.h>
struct WatchVertex { struct { u8 r,g,b,a; } color; };
static int watch_item_is_actively_selected;
''' + colours + '''
int main(void) {
 Gfx buffer[32], *gdl=buffer; Mtx matrix, *arg1=&matrix;
''' + setup + '''
 unsigned geometry=G_LIGHTING|G_FOG|G_ZBUFFER|G_TEXTURE_GEN|G_TEXTURE_GEN_LINEAR;
 for(Gfx *g=buffer;g<gdl;g++) {
  if((g->words.w0>>24)==(G_CLEARGEOMETRYMODE&255)) geometry&=~g->words.w1;
  if((g->words.w0>>24)==(G_SETGEOMETRYMODE&255)) geometry|=g->words.w1;
 }
 float fog[2]; u8 rgb[3];
 assert(gevrBeanFogLine(33,9161,0.25f,fog,rgb));
 assert(rgb[0]==0x85&&rgb[1]==0xad&&rgb[2]==0xca);
 assert(fabsf((7450*.25f*fog[0]+fog[1])/255-.5f)<1e-5f);
 assert(gevrBeanFogLine(33,2782,0.25f,fog,rgb));
 assert(fabsf((80000*.25f*fog[0]+fog[1])/255-1)<1e-5f);
 const int stages[]={22,23,24,25,27,28,29,30,31,32,33,34,35,36,37,38,39,41,43};
 for(unsigned i=0;i<sizeof(stages)/sizeof(stages[0]);i++) {
  float reach; assert(gevrBeanFogRange(stages[i],9161,0,&reach,rgb));
  assert(gevrBeanFogLine(stages[i],9161,.25f,fog,rgb));
  assert(fabsf((reach*.25f*fog[0]+fog[1])/255-1)<1e-5f);
 }
 float reach; assert(gevrBeanFogRange(22,9161,1,&reach,rgb)&&reach==5000);
 assert(rgb[0]==0&&rgb[1]==0&&rgb[2]==8);
 assert(gevrBeanFogRange(43,9161,1,&reach,rgb)&&reach==6500);
 assert(rgb[0]==0x57&&rgb[1]==0x5f&&rgb[2]==0x5d);
 const u8 gas[3]={70,180,30};
 assert(gevrBeanFogLineTransition(34,9161,.25f,0,.25f,1,gas,fog,rgb));
 assert(rgb[0]==70&&rgb[1]==180&&rgb[2]==30);
 assert(gevrBeanFogRange(34,9161,0,&reach,rgb));
 reach=-100+(reach+100)*.25f;
 assert(fabsf((reach*.25f*fog[0]+fog[1])/255-1)<1e-5f);
 assert(!gevrBeanFogLine(20,5000,.25f,fog,rgb)); /* Not a retail stage ID. */
 assert(!gevrBeanFogLine(33,5000,0,fog,rgb));
 assert(!geometry); /* RGB bytes are colours again, not signed normals. */
 struct WatchVertex v[20]={0}; set_page_rectangle_colors(2,v);
 for(int i=0;i<20;i++) { assert(v[i].color.g>v[i].color.r && v[i].color.r==v[i].color.b);
  assert(v[i].color.g==(i>=8&&i<12?0xf0:0x70)); }
 puts("Watch palette: production menu clears inherited lighting/fog/depth; page indicators retain green RGB");
}
'''
with tempfile.TemporaryDirectory(prefix="gevr-watch-") as temp:
    path = Path(temp) / "watch.c"
    path.write_text(c)
    exe = Path(temp) / "watch.exe"
    subprocess.run([shutil.which("gcc") or "cc", "-std=c11", "-O2", "-D_LANGUAGE_C", "-DGEVR",
                    "-DPLATFORM_64BIT=1", "-I"+str(ROOT / "include"), "-I"+str(ROOT / "port/include"), str(path), "-lm", "-o", str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
