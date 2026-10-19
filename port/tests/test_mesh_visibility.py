"""Chunk rejection retains original, presented and controller scope views."""
from pathlib import Path
import os, shutil, subprocess, tempfile
ROOT = Path(__file__).resolve().parents[2]
source=(ROOT/'port/fast3d/gfx_pc.cpp').read_text()
code=source[source.index('static bool gevr_mesh_run_visible('):source.index('static bool gevr_mesh_draw_run(')]
fixture=r'''
#include <cassert>
#include <cstring>
#include <cstdio>
#include "gevr_static_mesh.h"
#include "gevr_mesh_visibility.h"
#include "gfx_rendering_api.h"
static struct {float MP_matrix[4][4];} rsp;
static float vr_clip_margin_x_const,vr_clip_margin_x_w,vr_clip_margin_y_w;
static float s_gevrPresentA[4][4],s_gevrPresentB[4]; static bool s_gevrPresentReady;
static struct {bool on; float mx,mxw,my,myw,near_w,far;} s_gevrSlack; static bool gVrFlatPass;
#define GEVR_FAR_DEPTH_SCALE .3f
static bool scopeSees,inverted,called;
static GfxClipParameters clip() {return {false,inverted};}
static bool scope(const float*mp,const float*lo,const float*hi) {
 called=true; assert(mp[5]==(inverted?-1:1)); return scopeSees;
}
static GfxRenderingAPI api; static GfxRenderingAPI*gfx_rapi=&api;
/* PRODUCTION */
int main() {
 float mp[16]={1,0,0,0,0,1,0,0,0,0,1,-1,0,0,0,0}; memcpy(rsp.MP_matrix,mp,sizeof(mp));
 api.get_clip_parameters=clip; api.scope_sees_mesh=scope;
 GevrStaticMeshRun run={}; run.lo[0]=20;run.hi[0]=21;run.lo[2]=run.hi[2]=-10;
 assert(!gevr_mesh_run_visible(&run) && called);
 scopeSees=true; assert(gevr_mesh_run_visible(&run));
 inverted=true; assert(gevr_mesh_run_visible(&run));
 scopeSees=false; inverted=false;
 s_gevrPresentReady=true; s_gevrPresentA[0][0]=1;s_gevrPresentA[1][1]=1;s_gevrPresentA[3][3]=1;
 s_gevrPresentB[0]=-15; assert(gevr_mesh_run_visible(&run));
 s_gevrPresentReady=false; vr_clip_margin_x_const=12;assert(gevr_mesh_run_visible(&run));
 vr_clip_margin_x_const=0;run.lo[0]=0;run.hi[0]=1; called=false;
 assert(gevr_mesh_run_visible(&run) && !called);
 puts("Chunk visibility: original eyes, stereo margin, presented eyes and scope union passed");
}
'''
with tempfile.TemporaryDirectory(prefix='gevr-visibility-') as temp:
    cpp=Path(temp)/'visibility.cpp';cpp.write_text(fixture.replace('/* PRODUCTION */',code))
    exe=Path(temp)/'visibility.exe'
    sanitize=['-fsanitize=address,undefined','-fno-omit-frame-pointer','-no-pie'] if os.environ.get('BEAN_SANITIZE') else []
    subprocess.run([shutil.which('g++') or 'c++','-std=c++17','-O2',*sanitize,
                    '-I'+str(ROOT/'port/include'),'-I'+str(ROOT/'port/fast3d'),str(cpp),'-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True)
