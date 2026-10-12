"""Execute production scope capture/replay against a GL state recorder.

The real shader projection also runs under GLES in test_skin_shader.py.
"""
from pathlib import Path
import os, re, shutil, subprocess, tempfile
ROOT = Path(__file__).resolve().parents[2]
source = (ROOT / 'port/fast3d/gfx_opengl.cpp').read_text()
def function(name):
    start = source.rindex('\n', 0, source.index(name+'(')) + 1
    opening = source.index('{', start)
    depth, end = 1, opening + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]
# Declarations and capture functions; exclude prototypes resolved by the fixture.
capture = source[source.index('struct GevrScopeDraw {'):source.index("/* a hand's scope target")]
capture = capture.replace('#define GEVR_SCOPE_RES 512', '#define GEVR_SCOPE_RES 512')
code = capture + '\n' + function('gfx_vr_scope_render')
constants = sorted(set(re.findall(r'\bGL_[A-Z0-9_]+\b', code)))
glcalls = set(re.findall(r'\b(gl[A-Z]\w*)\(', code))
custom = {'glBindVertexArray','glGetIntegerv','glGetFloatv','glGetBooleanv','glIsEnabled'}
prelude = '''#include <algorithm>
#include <vector>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <cmath>
#include <cassert>
#include "gfx_rendering_api.h"
#include "gevr_mesh_visibility.h"
#include "gevr_scope.h"
using GLuint=unsigned; using GLint=int; using GLsizei=int; using GLenum=unsigned;
using GLfloat=float; using GLboolean=bool;
struct ShaderProgram { int scopeLocation=0,scopeVPLocation=1,scopeHeadPLocation=2;
 int three_point_filter_locations[2]={3,4}; bool used_textures[2]={true,true};
 int num_floats=4; GLuint opengl_program_id=1; };
struct GevrProfileSection { GevrProfileSection(int) {} }; enum {GEVR_TIME_SCOPE,LOG_NOTE};
GevrScopeState gevrScope[2]; int gevrScopeOn;
static unsigned char *s_pmPtr; static bool gVrFlatPass,gForceFlatShaderForMenu,vr_dl_is_pause_or_menu;
static ShaderProgram *s_curPrg; static GLuint s_boundTex[2]; static bool current_textures_linear_filter[2];
static bool s_depthArgs[4],s_alphaArgs[2]; static uint16_t s_depthZmode;
static float s_drawDecalPull; static bool current_depth_mask,s_isDecal,s_decalZ,s_opaqueDepthWrite,s_uniCacheValid;
static GLuint opengl_vao=7, opengl_vbo=8; static int s_activeTexUnit,gevrZDebugMode;
static bool s_drawElements; static GLuint boundVao; static int cull; static GfxMeshDrawParams savedParams;
struct Draw {GLuint vao; int first,count,cull; bool indexed; GfxMeshDrawParams params;};
static std::vector<Draw> issued;
static int get_debug_VisCVG_flag() {return 0;}
static bool gevr_decal_writes_depth() {return false;}
static void gevr_scope_make_target(int) {}
static void gevr_mesh_uniforms(const ShaderProgram*,const GfxMeshDrawParams*p) {savedParams=*p;}
static void gevr_mesh_clip(bool) {}
static void gevr_mesh_cull(int v) {cull=v;}
static void gfx_opengl_load_shader(ShaderProgram*p) {s_curPrg=p;}
static void gfx_opengl_unload_shader(ShaderProgram*) {}
static void gfx_opengl_set_depth_mode(bool,bool,bool,bool,uint16_t) {}
static void gfx_opengl_set_use_alpha(bool,bool) {}
static void gevr_zdebug_apply(ShaderProgram*,bool) {}
static void sysLogPrintf(int,const char*,...) {}
static void gevr_issue_draw(int f,int n,bool,bool,float,bool) {issued.push_back({boundVao,f,n,cull,s_drawElements,savedParams});}
static void glBindVertexArray(GLuint v) {boundVao=v;}
static void glGetIntegerv(GLenum,GLint *v) {v[0]=0;}
static void glGetFloatv(GLenum,GLfloat *v) {v[0]=0;}
static void glGetBooleanv(GLenum,GLboolean *v) {*v=false;}
static GLboolean glIsEnabled(GLenum) {return false;}
'''
prelude += 'enum {' + ','.join(f'{c}={i+1}' for i,c in enumerate(constants)) + '};\n'
prelude += '\n'.join(f'template<class...T> static void {f}(T...) {{}}' for f in sorted(glcalls-custom)) + '\n'
main = r'''
int main() {
 ShaderProgram mesh,cpu; cpu.opengl_program_id=2; s_curPrg=&mesh;
 float mp[16]={2,0,0,0, 0,-3,0,0, 0,0,1,-1, 0,0,0,0};
 float lo[3]={-1,-1,-5},hi[3]={1,1,-5};
 for(int h=0;h<2;h++) {
  s_scopeTaken[h]=true; s_scopeHeadP[h][0]=2; s_scopeHeadP[h][1]=-3;
  s_scopeVP[h][0]=s_scopeVP[h][5]=1; s_scopeVP[h][10]=-1; s_scopeVP[h][11]=-1;
 }
 s_scopeRec=true;
 // Mask calculation must match the existing CPU clip test for a complete box.
 for(int x=-12;x<=12;x++) {
  float a[3]={float(x),-1,-5},b[3]={float(x+1),1,-4},vertices[8*4];
  for(int i=0;i<8;i++) {
   float p[3]={i&1?b[0]:a[0],i&2?b[1]:a[1],i&4?b[2]:a[2]};
   for(int r=0;r<4;r++) vertices[i*4+r]=mp[r]*p[0]+mp[4+r]*p[1]+mp[8+r]*p[2]+mp[12+r];
  }
  // gevr_scope_sees consumes triangles: pad a ninth repeated corner.
  float nine[9*4]; memcpy(nine,vertices,sizeof(vertices)); memcpy(nine+32,vertices,16);
  bool sees=gevr_scope_sees(0,nine,36,3);
  assert(bool(gevr_scope_mesh_mask(mp,a,b)&1)==sees);
 }
 GfxMeshDrawParams skin={}; memcpy(skin.mp,mp,sizeof(mp)); skin.skinned=true;
 skin.cull=-1; skin.invert_y=true; skin.palette.valid[0]=1; skin.palette.rows[0][3]=42;
 skin.authored_color=true; skin.tex_rotation[0][1]=1; skin.fog_linear=true; skin.fog_mul=.125f; skin.fog_offset=2; skin.fog[0]=.85f;
 gevr_scope_keep_mesh(20,3,6,&skin);
 skin.authored_color=false; skin.tex_rotation[0][1]=0; skin.fog_linear=false; skin.fog_mul=9; skin.fog[0]=0;
 skin.palette.rows[0][3]=900; // The captured instance owns its palette.
 s_curPrg=&cpu;
 float tri[12]={0,0,0,5, .5f,0,0,5, 0,.5f,0,5};
 gevr_scope_keep(30,tri,12,1);
 s_curPrg=&mesh;
 GfxMeshDrawParams room={}; memcpy(room.mp,mp,sizeof(mp));
 room.has_bounds=true; memcpy(room.lo,lo,sizeof(lo)); memcpy(room.hi,hi,sizeof(hi));
 s_scopeVP[1][12]=100; // Only the right scope sees this static chunk.
 gevr_scope_keep_mesh(21,12,9,&room);
 assert(s_scopeDraws.size()==3 && s_scopeMeshes.size()==2);
 s_scopeFbo[0]=10; s_scopeFbo[1]=11;
 gfx_vr_scope_render();
 assert(issued.size()==5);
 for(int i : {0,3}) {
  const Draw&d=issued[i]; assert(d.vao==20 && d.indexed && d.first==3 && d.count==6 && d.cull==1);
  assert(d.params.fog_linear && d.params.fog_mul==.125f && d.params.fog_offset==2 && d.params.fog[0]==.85f);
  assert(d.params.authored_color && d.params.tex_rotation[0][1]==1);
  assert(d.params.palette.rows[0][3]==42 && d.params.palette.valid[0]==1);
 }
 for(int i : {1,4}) {const Draw&d=issued[i];assert(d.vao==7 && !d.indexed && d.first==30 && d.cull==0);}
 assert(issued[2].vao==21 && issued[2].indexed && issued[2].first==12 && issued[2].cull==0);
 assert(s_scopeDraws.empty() && s_scopeMeshes.empty() && !s_drawElements && cull==0);
 puts("Scope mesh capture/replay: CPU and indexed VAOs, saved palettes, two hand masks and winding passed");
}
'''
with tempfile.TemporaryDirectory(prefix='gevr-scope-mesh-') as temp:
    cpp=Path(temp)/'scope.cpp'; cpp.write_text(prelude+code+main)
    exe=Path(temp)/'scope.exe'
    sanitize=['-fsanitize=address,undefined','-fno-omit-frame-pointer','-no-pie'] if os.environ.get('BEAN_SANITIZE') else []
    subprocess.run([shutil.which('g++') or 'c++','-std=c++17','-O2',*sanitize,
                    '-I'+str(ROOT/'port/include'),'-I'+str(ROOT/'port/fast3d'),str(cpp),'-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True)
