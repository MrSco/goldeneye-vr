"""Exercise the production retained VAO setup for rooms, legacy and skin vertices."""
from pathlib import Path
import os
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
source = (ROOT/'port/fast3d/gfx_opengl.cpp').read_text(encoding='utf-8')
start = source.index('static GevrMeshGpu* gevr_mesh_gpu(')
end = source.index('static void gfx_opengl_delete_mesh(',start)
code = source[start:end]
pre = r'''#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include "gevr_skin_mesh.h"
using GLuint=unsigned; using GLsizei=int; using GLsizeiptr=ptrdiff_t;
enum {GL_ARRAY_BUFFER=1,GL_ELEMENT_ARRAY_BUFFER=2,GL_STATIC_DRAW=3,
      GL_SHORT=4,GL_FLOAT=5,GL_UNSIGNED_BYTE=6,GL_TRUE=1,GL_FALSE=0};
struct GevrMeshGpu {GLuint vao,vbo,ibo; unsigned generation;};
static unsigned s_glGeneration=1,opengl_vao=1,opengl_vbo=2;
struct Attribute {int size,type,normalized,stride; size_t offset;};
static Attribute attr[16]; static size_t vertexBytes,indexBytes;
static void glGenVertexArrays(int,GLuint *p) {*p=10;}
static void glGenBuffers(int,GLuint *p) {*p=20;}
static void glBindVertexArray(GLuint) {}
static void glBindBuffer(int,GLuint) {}
static void glBufferData(int target,GLsizeiptr n,const void *,int) {
    if(target==GL_ARRAY_BUFFER) vertexBytes=n; else indexBytes=n;
}
static void glEnableVertexAttribArray(unsigned) {}
static void glVertexAttribPointer(unsigned index,int size,int type,int normalized,int stride,const void *offset) {
    attr[index]={size,type,normalized,stride,(size_t)offset};
}
'''
main = r'''int main() {
    static_assert(sizeof(GevrWorldVertex)==32);
    static_assert(offsetof(GevrWorldVertex,tc)==12 && offsetof(GevrWorldVertex,rgba)==16);
    void *gpu=nullptr; uint32_t indices[3]={0,1,2}; GevrWorldVertex vertices[3]={};
    vertices[0].p[0]=50000.25f; vertices[0].p[1]=0.125f;
    GevrMeshGpu *g=gevr_mesh_gpu(&gpu,vertices,3,indices,3,false,true);
    assert(g && vertexBytes==96 && indexBytes==12);
    assert(attr[0].type==GL_FLOAT && attr[0].stride==32);
    assert(attr[15].type==GL_FLOAT && attr[15].offset==20);
    assert(attr[1].type==GL_SHORT && attr[1].offset==12);
    assert(attr[2].type==GL_UNSIGNED_BYTE && attr[2].normalized && attr[2].offset==16);
    assert(gevr_mesh_gpu(&gpu,vertices,3,indices,3,false,true)==g);
    s_glGeneration++; gevr_mesh_gpu(&gpu,vertices,3,indices,3,false,true);
    assert(((GevrMeshGpu *)gpu)->generation==s_glGeneration && vertexBytes==96);
    delete (GevrMeshGpu *)gpu; gpu=nullptr;
    gevr_mesh_gpu(&gpu,vertices,3,indices,3,false,false);
    assert(vertexBytes==48 && attr[0].type==GL_SHORT && attr[1].offset==8 && attr[2].offset==12);
    delete (GevrMeshGpu *)gpu; gpu=nullptr;
    GevrSkinVertex skin[3]={}; gevr_mesh_gpu(&gpu,skin,3,indices,3,true,false);
    assert(vertexBytes==3*sizeof(GevrSkinVertex) && attr[0].type==GL_SHORT);
    assert(attr[13].offset==offsetof(GevrSkinVertex,tint));
    delete (GevrMeshGpu *)gpu;
    puts("Float room, legacy and skin VAO strides, attributes and context recreation passed");
}
'''
with tempfile.TemporaryDirectory(prefix='gevr-world-layout-') as tmp:
    cpp=Path(tmp)/'test.cpp'; cpp.write_text(pre+code+main,encoding='utf-8')
    exe=Path(tmp)/'test.exe'
    san=['-fsanitize=address,undefined','-fno-omit-frame-pointer','-no-pie'] if os.environ.get('BEAN_SANITIZE') else []
    subprocess.run([shutil.which('g++') or 'c++','-std=c++17','-O2',*san,
                    '-I'+str(ROOT/'port/include'),str(cpp),'-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True)
