"""Execute production scope capture/replay against a GL state recorder.

The real shader projection also runs under GLES in test_skin_shader.py.
"""
from pathlib import Path
import os, re, shutil, subprocess, tempfile
ROOT = Path(__file__).resolve().parents[2]
source = (ROOT / 'port/fast3d/gfx_opengl.cpp').read_text()
def function(name):
    match = re.search(r'^[^\n]*\b' + re.escape(name) + r'\([^;{}]*\)\s*\{', source, re.M)
    assert match, name
    start = match.start()
    opening = source.index('{', start)
    depth, end = 1, opening + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]
# Declarations and capture functions; exclude prototypes resolved by the fixture.
capture = source[source.index('struct GevrScopeDraw {'):source.index("/* a hand's scope target")]
capture = capture.replace('#define GEVR_SCOPE_RES 512', '#define GEVR_SCOPE_RES 512')
code = capture + '\n' + function('gevr_replay_vao') + '\n' + function('gevr_replay_mesh') + '\n' + function('gfx_vr_scope_render')
constants = sorted(set(re.findall(r'\bGL_[A-Z0-9_]+\b', code)))
glcalls = set(re.findall(r'\b(gl[A-Z]\w*)\(', code))
custom = {'glBindVertexArray','glGetIntegerv','glGetFloatv','glGetBooleanv','glIsEnabled',
          'glActiveTexture','glBindTexture','glUniform1i','glEnableVertexAttribArray'}
prelude = '''#include <algorithm>
#include <vector>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <cmath>
#include <cassert>
#include <map>
#include "gfx_rendering_api.h"
#include "gevr_mesh_visibility.h"
#include "gevr_scope.h"
using GLuint=unsigned; using GLint=int; using GLsizei=int; using GLenum=unsigned;
using GLfloat=float; using GLboolean=bool;
struct ShaderProgram { int scopeLocation=0,scopeVPLocation=1,scopeHeadPLocation=2;
 int three_point_filter_locations[2]={3,4}; bool used_textures[2]={true,true};
 bool is_mesh=false; int num_floats=4; GLuint opengl_program_id=1; };
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
static int vaoCalls,attribCalls,textureCalls,filterCalls,activeTexture;
static GLuint boundTex[2]; static std::map<ShaderProgram*,std::vector<int>> filters;
static std::map<GLuint,std::vector<bool>> attributes;
struct GevrScopeDraw; static std::vector<const GevrScopeDraw*> expected;
static void verify_draw();
static int get_debug_VisCVG_flag() {return 0;}
static bool gevr_decal_writes_depth() {return false;}
static void gevr_scope_make_target(int) {}
static void gevr_mesh_uniforms(const ShaderProgram*,const GfxMeshDrawParams*p,bool *bound) {savedParams=*p;*bound=true;}
static void gevr_mesh_clip(bool) {}
static void gevr_mesh_cull(int v) {cull=v;}
static void gevr_mesh_settle(void) {}   // gfx_opengl.cpp: no mesh run is left bound here
static void gfx_opengl_load_shader(ShaderProgram*p) {s_curPrg=p;filters[p]={-9,-9};}
static void gfx_opengl_unload_shader(ShaderProgram*p) {
 if(p && !p->is_mesh) {attributes[boundVao].resize(16);for(int i=0;i<3;i++)attributes[boundVao][i]=false;}
}
static void gfx_opengl_set_depth_mode(bool,bool,bool,bool,uint16_t) {}
static void gfx_opengl_set_use_alpha(bool,bool) {}
static void gevr_zdebug_apply(ShaderProgram*,bool) {}
static void sysLogPrintf(int,const char*,...) {}
static void gevr_issue_draw(int f,int n,bool,bool,float,bool) {verify_draw();issued.push_back({boundVao,f,n,cull,s_drawElements,savedParams});}
static void glBindVertexArray(GLuint v) {boundVao=v;vaoCalls++;}
static void glGetIntegerv(GLenum,GLint *v) {v[0]=0;}
static void glGetFloatv(GLenum,GLfloat *v) {v[0]=0;}
static void glGetBooleanv(GLenum,GLboolean *v) {*v=false;}
static GLboolean glIsEnabled(GLenum) {return false;}
'''
prelude += 'enum {' + ','.join(f'{c}={i+1}' for i,c in enumerate(constants)) + '};\n'
prelude += r'''
static void glActiveTexture(GLenum t) {activeTexture=t-GL_TEXTURE0;}
static void glBindTexture(GLenum,GLuint t) {assert(activeTexture>=0 && activeTexture<2);boundTex[activeTexture]=t;textureCalls++;}
static void glUniform1i(GLint loc,GLint v) {
 for(int i=0;i<2;i++)if(loc==s_curPrg->three_point_filter_locations[i]) {filters[s_curPrg][i]=v;filterCalls++;}
}
static void glEnableVertexAttribArray(GLuint a) {attributes[boundVao].resize(16);attributes[boundVao][a]=true;attribCalls++;}
'''
prelude += '\n'.join(f'template<class...T> static void {f}(T...) {{}}' for f in sorted(glcalls-custom)) + '\n'
main = r'''
static void verify_draw() {
 const auto &d=*expected.at(issued.size());
 assert(s_drawElements==bool(d.mesh));
 for(int i=0;i<2;i++)if(d.prg->used_textures[i]) {
  assert(boundTex[i]==d.tex[i]);assert(filters[d.prg][i]==int(d.linear[i]));
 }
 if(d.mesh) {
  const auto &m=s_scopeMeshes[d.mesh-1];assert(boundVao==m.vao);
  for(int i=0;i<(m.params.skinned?15:3);i++)assert(attributes[boundVao][i]);
 } else assert(boundVao==opengl_vao && cull==0);
}
static void render() {
 expected.clear();issued.clear();
 for(int h=0;h<2;h++)if(s_scopeTaken[h])for(const auto &d:s_scopeDraws)
  if(d.mask&(1<<h))expected.push_back(&d);
 gfx_vr_scope_render();assert(issued.size()==expected.size());
 assert(boundTex[0]==s_boundTex[0] && boundTex[1]==s_boundTex[1] && activeTexture==s_activeTexUnit);
}
int main() {
 ShaderProgram mesh,cpu;mesh.is_mesh=true; cpu.opengl_program_id=2; s_curPrg=&mesh;
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
 skin.normal_tex1=true;skin.normal_axis[0][0]=.75f;
 skin.authored_color=true; skin.tex_rotation[0][1]=1; skin.fog_linear=true; skin.fog_mul=.125f; skin.fog_offset=2; skin.fog[0]=.85f;
 gevr_scope_keep_mesh(20,3,6,&skin);
 skin.normal_tex1=false;skin.normal_axis[0][0]=0;
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
 render();
 assert(issued.size()==5);
 for(int i : {0,3}) {
  const Draw&d=issued[i]; assert(d.vao==20 && d.indexed && d.first==3 && d.count==6 && d.cull==1);
  assert(d.params.fog_linear && d.params.fog_mul==.125f && d.params.fog_offset==2 && d.params.fog[0]==.85f);
  assert(d.params.normal_tex1 && d.params.normal_axis[0][0]==.75f);
  assert(d.params.authored_color && d.params.tex_rotation[0][1]==1);
  assert(d.params.palette.rows[0][3]==42 && d.params.palette.valid[0]==1);
 }
 for(int i : {1,4}) {const Draw&d=issued[i];assert(d.vao==7 && !d.indexed && d.first==30 && d.cull==0);}
 assert(issued[2].vao==21 && issued[2].indexed && issued[2].first==12 && issued[2].cull==0);
 assert(s_scopeDraws.empty() && s_scopeMeshes.empty() && !s_drawElements && cull==0);
 // Identical consecutive draws: two passes each establish local bindings.
 // The end-of-pass frontend restore adds two texture binds and one VAO bind.
 for(bool skin : {false,true}) {
  GevrScopeMesh m={};m.vao=20;m.params.skinned=skin;m.params.cull=-1;
  s_scopeMeshes.push_back(m);GevrScopeDraw d={};d.prg=&mesh;d.mesh=1;d.mask=3;
  d.tex[0]=42;d.tex[1]=43;d.linear[0]=true;
  for(int i=0;i<100;i++)s_scopeDraws.push_back(d);
  s_scopeTaken[0]=s_scopeTaken[1]=true;
  vaoCalls=attribCalls=textureCalls=filterCalls=0;
  render();assert(issued.size()==200);
  assert(attribCalls==2*(skin?15:3));
  assert(textureCalls==6 && filterCalls==4 && vaoCalls==5);
 }
 // Shader A -> B -> A, changed filtering/texture, one unused sampler and
 // CPU/mesh transitions. Test the left scope alone after previous two-hand passes.
 ShaderProgram other=mesh;other.opengl_program_id=3;other.used_textures[1]=false;
 GevrScopeMesh m={};m.vao=21;m.params.skinned=true;s_scopeMeshes.push_back(m);
 GevrScopeDraw d={};d.prg=&mesh;d.mesh=1;d.mask=2;d.tex[0]=5;d.tex[1]=6;
 s_scopeDraws.push_back(d);d.prg=&other;s_scopeDraws.push_back(d);
 d.prg=&mesh;s_scopeDraws.push_back(d);
 d.tex[1]=7;d.linear[0]=true;s_scopeDraws.push_back(d);
 d.prg=&cpu;d.mesh=0;s_scopeDraws.push_back(d);
 d.prg=&mesh;d.mesh=1;s_scopeDraws.push_back(d);
 s_scopeTaken[1]=true;render();assert(issued.size()==6);
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
