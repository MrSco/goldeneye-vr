"""Check production mesh uniform uploads and owned palette state with mocked GL."""
from pathlib import Path
import os
import re
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
s = (ROOT / 'port/fast3d/gfx_opengl.cpp').read_text(encoding='utf-8')
a = s.index('static void gevr_mesh_uniforms(const struct ShaderProgram* prg, const struct GfxMeshDrawParams* p)\n{')
b = s.index('\n/*', a)
code = s[a:b]
fields = sorted(set(re.findall(r'prg->(\w+)', code)))
arrays = {'meshTexLocation': 2, 'meshTexRotationLocation': 2,
          'meshInSelLocation': 7, 'meshInConstLocation': 7}
pre = '''#include <map>
#include <cstring>
#include <cassert>
#include <cstdio>
#include "gfx_rendering_api.h"
using GLuint=unsigned;
enum {GL_UNIFORM_BUFFER=1,GL_STREAM_DRAW=2,GL_FALSE=0};
struct ShaderProgram {
'''
pre += '\n'.join('int ' + f + (f'[{arrays[f]}]' if f in arrays else '') + ';' for f in fields)
pre += '''
};
struct GevrMeshDrawUniforms {bool valid=false; GfxMeshDrawParams params={};};
static std::map<const ShaderProgram*,GevrMeshDrawUniforms> s_meshDrawUniforms;
static GLuint s_skinPaletteBuffer;
static bool s_skinPaletteUploaded;
static float s_skinPaletteRows[GEVR_SKIN_BONES][12];
static int uploads,uniforms; static std::map<int,int> calls;
static float uploaded[GEVR_SKIN_BONES][12];
static void glGenBuffers(int,GLuint *p) {*p=1;}
static void glBindBuffer(int,GLuint) {}
static void glBindBufferBase(int,int,GLuint) {}
static void glBufferData(int,size_t n,const void *p,int) {assert(n==sizeof(uploaded));memcpy(uploaded,p,n);uploads++;}
template<class...T> void glUniform2uiv(int l,T...) {calls[l]++;uniforms++;}
template<class...T> void glUniform1i(int l,T...) {calls[l]++;uniforms++;}
template<class...T> void glUniform3fv(int l,T...) {calls[l]++;uniforms++;}
template<class...T> void glUniform4fv(int l,T...) {calls[l]++;uniforms++;}
template<class...T> void glUniform4f(int l,T...) {calls[l]++;uniforms++;}
template<class...T> void glUniform2f(int l,T...) {calls[l]++;uniforms++;}
template<class...T> void glUniformMatrix4fv(int l,T...) {calls[l]++;uniforms++;}
'''
main = 'int main() {\n ShaderProgram a={},b={}; int id=1;\n'
main += '\n'.join((f' for(int j=0;j<{arrays[f]};j++) a.{f}[j]=id++;'
                   if f in arrays else f' a.{f}=id++;') for f in fields)
main += '''
 b=a;
 GfxMeshDrawParams p={}; p.skinned=true; p.palette.valid[0]=1; p.palette.rows[0][3]=42;
 p.lit=true; p.authored_color=true; p.num_inputs=2; p.num_lights=1;
 p.tex_rotation[0][0]=1;
 gevr_mesh_uniforms(&a,&p); const int first=uniforms;
 assert(uploads==1 && uploaded[0][3]==42);
 for(int j=0;j<100;j++) gevr_mesh_uniforms(&a,&p);
 assert(uploads==1 && uniforms==first);
 // Uniform values belong to each program; the palette buffer is shared.
 gevr_mesh_uniforms(&b,&p); assert(uniforms==first*2 && uploads==1);
 gevr_mesh_uniforms(&a,&p); assert(uniforms==first*2);
 p.tex_rotation[0][1]=1;
 gevr_mesh_uniforms(&a,&p); assert(uniforms==first*2+1);
 p.palette.rows[0][3]=900;
 gevr_mesh_uniforms(&a,&p); assert(uploads==2 && uploaded[0][3]==900);
 assert(uniforms==first*2+1);
 // Saved parameters restore the original instance, rather than an address.
 GfxMeshDrawParams replay=p; replay.palette.rows[0][3]=42;
 gevr_mesh_uniforms(&a,&replay); assert(uploads==3 && uploaded[0][3]==42);
 p.skinned=false; gevr_mesh_uniforms(&a,&p); assert(uploads==3);
 p.skinned=true; gevr_mesh_uniforms(&a,&p); assert(uploads==4);
 p.num_lights=0; gevr_mesh_uniforms(&a,&p); int light=calls[a.meshLightDirLocation];
 p.num_lights=1; gevr_mesh_uniforms(&a,&p); assert(calls[a.meshLightDirLocation]==light+1);
 // Context/program destruction invalidates caches even with identical data.
 s_skinPaletteBuffer=0; s_skinPaletteUploaded=false; s_meshDrawUniforms.clear();
 int before=uniforms; gevr_mesh_uniforms(&a,&p);
 assert(uploads==5 && uniforms>before);
 puts("Mesh uniform uploads and saved palette/program/context checks passed");
}
'''
with tempfile.TemporaryDirectory(prefix='gevr-mesh-uniforms-') as tmp:
    cpp = Path(tmp) / 'check.cpp'
    cpp.write_text(pre + code + main, encoding='utf-8')
    exe = Path(tmp) / 'check.exe'
    san = ['-fsanitize=address,undefined', '-fno-omit-frame-pointer', '-no-pie'] if os.environ.get('BEAN_SANITIZE') else []
    subprocess.run([shutil.which('g++') or 'c++', '-std=c++17', '-O2', *san,
                    '-I' + str(ROOT / 'port/include'), '-I' + str(ROOT / 'port/fast3d'),
                    str(cpp), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
