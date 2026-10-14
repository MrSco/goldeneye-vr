"""Hidden Bean runs skip posing; mirrored skins retain the GPU path."""
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
source = (ROOT / 'port/fast3d/gfx_pc.cpp').read_text()
start = source.index('static bool gevr_mesh_draw_run(')
end = source.index('\nextern "C" void gfx_opengl_mesh_settle', start)
production = source[start:end]
fixture = r'''
#include <cassert>
#include <cstring>
#include <cstdio>
#include "gfx_rendering_api.h"
enum { G_CULL_BACK=1, G_CULL_FRONT=2, G_CULL_BOTH=3, G_LIGHTING=4,
       G_TEXTURE_GEN=8, G_FOG=16, MAX_VERTICES=16, MAX_LIGHTS=7 };
struct LoadedVertex {float x,y,z,w;};
struct Light {float col[3];};
static struct {
    bool modelview_hidden_stack[2]; unsigned modelview_matrix_stack_size=1;
    unsigned geometry_mode; void *vertex_colors;
    LoadedVertex loaded_vertices[32];
    unsigned current_num_lights; Light current_lights[8];
    float fog_mul, fog_offset;
} rsp;
static struct {struct {unsigned char r,g,b,a;} fog_color;} rdp;
static bool s_gevrMeshOn=true, s_gevrSkinOn=true, gevrCullOff, gevrCullMirror; static uint8_t s_beanFadeAlpha=255;
static bool fbActive=true, s_gevrMeshProbe, s_beanRoomFogLinear, gevrRoomDl;
static float s_beanRoomFogLine[2];
static unsigned s_gevrMeshCulled, s_gevrMeshCulledTris, s_gevrMeshFallbacks;
static unsigned s_gevrMeshDraws, s_gevrMeshTris, s_gevrSkinDraws, s_gevrSkinTris;
static unsigned gevr_perf_tri_calls, gevr_perf_draws, gevr_perf_tris, gevr_perf_room_tris, g_gevrTrisThisFrame;
static struct {bool ok,use_grayscale; unsigned num_inputs; GfxClipParameters clip;} s_gevrMeshState;
struct Live {void push_back(void*){}} s_gevrMeshLive;
static GfxRenderingAPI api; static GfxRenderingAPI *gfx_rapi=&api;
static int probes,draws,flushes;
static GfxMeshDrawParams captured;
static bool invert;
static bool vr_is_initialized(){return true;}
static bool gevr_mesh_run_visible(const GevrStaticMeshRun*){return true;}
static void gfx_sp_tri1(int,int,int,bool){
    probes++; gevr_perf_tri_calls++; s_gevrMeshState.ok=true;
    s_gevrMeshState.clip.invert_y=invert;
}
static bool gevr_mesh_params(GfxMeshDrawParams*p){*p={};return true;}
static void calculate_normal_dir(const Light*,float*){}
static void gfx_flush(){flushes++;}
static bool draw(void**gpu,const void*,uint32_t,const uint32_t*,uint32_t,uint32_t,uint32_t,const GfxMeshDrawParams*p){
    draws++;captured=*p;*gpu=(void*)1;return true;
}
/* PRODUCTION */
static void reset(){
    rsp.geometry_mode=0;rsp.modelview_hidden_stack[0]=false;
    gevrCullOff=gevrCullMirror=false;s_gevrMeshOn=s_gevrSkinOn=true;
    probes=draws=flushes=0;invert=false;s_beanFadeAlpha=255;
}
int main(){
    api.draw_mesh=draw;
    GevrStaticMesh mesh={};GevrStaticMeshRun run={};run.indexCount=30000;
    GevrSkinDraw skin={};skin.palette.valid[0]=1;skin.palette.rows[0][3]=42;
    // Hidden geometry must be consumed even when the GPU path is unavailable.
    for(int gpu=0;gpu<2;gpu++) for(int skinned=0;skinned<2;skinned++){
        reset();s_gevrMeshOn=gpu;rsp.modelview_hidden_stack[0]=true;
        assert(gevr_mesh_draw_run(&mesh,&run,skinned?&skin:nullptr));
        assert(!draws && !probes && !flushes);
    }
    reset();rsp.geometry_mode=G_CULL_BOTH;
    assert(gevr_mesh_draw_run(&mesh,&run,&skin));assert(!draws && !probes);
    // Every mirrored/inverted winding combination stays on the GPU.
    for(int mirror=0;mirror<2;mirror++)for(int inverted=0;inverted<2;inverted++)for(int face=0;face<3;face++){
        reset();gevrCullMirror=mirror;invert=inverted;rsp.geometry_mode=face;
        assert(gevr_mesh_draw_run(&mesh,&run,&skin));
        int expected=face;if(mirror && face)expected=3-face;if(inverted)expected=-expected;
        assert(draws==1 && probes==1 && flushes==1 && captured.cull==expected);
        assert(captured.skinned && captured.palette.rows[0][3]==42);
        assert(rsp.geometry_mode==(unsigned)face);
    }
    // The CPU's backface depth push and generated UVs remain explicit fallbacks.
    reset();gevrCullOff=true;rsp.geometry_mode=G_CULL_BACK;
    assert(!gevr_mesh_draw_run(&mesh,&run,&skin));assert(!draws && !probes);
    reset();rsp.geometry_mode=G_TEXTURE_GEN;
    assert(!gevr_mesh_draw_run(&mesh,&run,&skin));assert(!draws && !probes);
    reset();gevrCullMirror=true;rsp.geometry_mode=G_CULL_BACK;
    assert(!gevr_mesh_draw_run(&mesh,&run,nullptr));assert(!draws && !probes);
    // A fading body (G_GEVR_BEAN_FADE) draws on the CPU, which scales its alpha.
    reset();s_beanFadeAlpha=128;
    assert(!gevr_mesh_draw_run(&mesh,&run,&skin));assert(!draws && !probes);
    assert(!gevr_mesh_draw_run(&mesh,&run,nullptr));assert(!draws && !probes);
    puts("PASS: hidden runs bypass CPU posing and draw setup; mirrored skin winding matches CPU semantics; special fallbacks and fading bodies preserved");
}
'''
with tempfile.TemporaryDirectory(prefix='gevr-mesh-dispatch-') as directory:
    temp = Path(directory)
    cpp, exe = temp / 'dispatch.cpp', temp / 'dispatch'
    cpp.write_text(fixture.replace('/* PRODUCTION */', production))
    subprocess.run([shutil.which('g++') or 'g++', '-std=c++17', '-O2', '-Wall', '-Werror',
                    '-I' + str(ROOT / 'port/include'), '-I' + str(ROOT / 'port/fast3d'),
                    str(cpp), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
