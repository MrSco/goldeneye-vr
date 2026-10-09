#include <algorithm>
#include <vector>
#include <cstring>
#include <cstdint>
#include <cassert>
#include <cstdio>
using GLint=int; using GLuint=unsigned; using GLenum=unsigned;
using GLsizei=int; using GLboolean=bool; using GLfloat=float;
enum { GL_FALSE=0, GL_TRUE=1, GL_LEQUAL=2, GL_ONE=3, GL_ZERO=4,
    GL_VERTEX_ARRAY_BINDING=5, GL_DEPTH_FUNC, GL_BLEND_SRC_RGB, GL_BLEND_DST_RGB,
    GL_BLEND_SRC_ALPHA, GL_BLEND_DST_ALPHA, GL_POLYGON_OFFSET_FACTOR,
    GL_POLYGON_OFFSET_UNITS, GL_DEPTH_WRITEMASK, GL_DEPTH_TEST, GL_BLEND,
    GL_POLYGON_OFFSET_FILL, GL_ARRAY_BUFFER, GL_TEXTURE_2D, GL_TEXTURE0=100 };
struct ShaderProgram {
    GLuint opengl_program_id;
    GLint reprojLocation=20, scopeHeadPLocation=21, reprojVPLocation=22;
    bool used_textures[2]={true,true};
    GLint three_point_filter_locations[2]={30,31};
};
/* INSERT_DRAW_STRUCT */
static std::vector<GevrEyeDraw> s_eyeDraws;
static bool s_eyeReady=true, s_uniCacheValid=true;
static GLfloat s_eyeHeadP[2];
static ShaderProgram *s_curPrg;
static bool s_depthArgs[4], s_alphaArgs[2], current_depth_mask, s_isDecal, s_decalZ, s_opaqueDepthWrite;
static uint16_t s_depthZmode;
static GLint s_curViewport[4],s_curScissor[4];
static GLuint opengl_vao=7,opengl_vbo=9,s_boundTex[2]={777,888};
static int gCurIsMenuLoc=23,gevrZDebugMode=0,s_activeTexUnit=1;
static GLuint boundTex[2]={777,888},boundProgram;
static int activeTexture=1,textureCalls,filterCalls,issued;
static int filters[3][2];
static void gevr_eye_proj_times(const float *d,float m[16]) { if(d)std::memcpy(m,d,64); else std::memset(m,0,64); }
static void glGetIntegerv(GLenum,GLint *v){*v=0;}
static void glGetFloatv(GLenum,GLfloat *v){*v=0;}
static void glGetBooleanv(GLenum,GLboolean *v){*v=true;}
static GLboolean glIsEnabled(GLenum){return false;}
static void glBindVertexArray(GLuint){}
static void glBindBuffer(GLenum,GLuint){}
static void glUniform1i(GLint loc,GLint value){
    if(loc==30||loc==31){filters[boundProgram][loc-30]=value;filterCalls++;}
}
static void glUniform2f(GLint,GLfloat,GLfloat){}
static void glUniformMatrix4fv(GLint,GLsizei,GLboolean,const GLfloat*){}
static void glActiveTexture(GLenum t){activeTexture=t-GL_TEXTURE0;}
static void glBindTexture(GLenum,GLuint t){boundTex[activeTexture]=t;textureCalls++;}
static void glViewport(GLint,GLint,GLint,GLint){}
static void glScissor(GLint,GLint,GLint,GLint){}
static void glUseProgram(GLuint p){boundProgram=p;}
static void glEnable(GLenum){} static void glDisable(GLenum){}
static void glDepthMask(GLboolean){} static void glDepthFunc(GLenum){}
static void glPolygonOffset(GLfloat,GLfloat){}
static void glBlendFuncSeparate(GLenum,GLenum,GLenum,GLenum){}
static void gfx_opengl_unload_shader(ShaderProgram*){}
static void gfx_opengl_load_shader(ShaderProgram *p){
    s_curPrg=p;glUseProgram(p->opengl_program_id);
    /* Production program loading restores frontend uniforms before replay
     * overrides them. This forces reupload even on A -> B -> A switches. */
    filters[boundProgram][0]=filters[boundProgram][1]=-9;
}
static void gfx_opengl_set_depth_mode(bool,bool,bool,bool,uint16_t){}
static void gfx_opengl_set_use_alpha(bool,bool){}
static void gevr_zdebug_apply(ShaderProgram*,bool){}
static void gfx_opengl_set_viewport(GLint,GLint,GLint,GLint){}
static void gfx_opengl_set_scissor(GLint,GLint,GLint,GLint){}
static void gevr_issue_draw(GLint first,GLsizei count,bool,bool,float,bool){
    const GevrEyeDraw& d=s_eyeDraws[issued++];
    assert(first==d.first&&count==d.count);
    for(int t=0;t<2;t++)if(d.prg->used_textures[t]) {
        assert(boundTex[t]==d.tex[t]);
        assert(filters[boundProgram][t]==(int)d.linear[t]);
    }
}
/* INSERT_REPLAY */
int main(){
    ShaderProgram a,b;a.opengl_program_id=1;b.opengl_program_id=2;
    b.used_textures[1]=false;
    s_curPrg=&a;boundProgram=1;
    GevrEyeDraw d={};d.prg=&a;d.first=12;d.count=18;d.hand=-1;
    d.tex[0]=42;d.tex[1]=43;d.linear[0]=true;d.linear[1]=false;
    for(int i=0;i<100;i++)s_eyeDraws.push_back(d);
    d.prg=&b;s_eyeDraws.push_back(d);s_eyeDraws.push_back(d);
    d.prg=&a;s_eyeDraws.push_back(d);
    d.tex[1]=45;d.linear[0]=false;s_eyeDraws.push_back(d);
    float delta[16]={};delta[0]=delta[5]=delta[10]=delta[15]=1;
    gfx_vr_eye_replay(delta,nullptr,nullptr,nullptr);
    assert(issued==104);
    /* 3 changed texture bindings + 2 restores, rather than 208 bindings.
     * Program-local filtering is refreshed on switches and value changes. */
    assert(textureCalls==5 && filterCalls==6);
    assert(boundTex[0]==777&&boundTex[1]==888&&activeTexture==1);
    assert(s_curPrg==&a&&boundProgram==1&&!s_uniCacheValid);
    s_eyeReady=false;gfx_vr_eye_replay(delta,nullptr,nullptr,nullptr);assert(issued==104);
    std::puts("PASS: real redraw preserves draw order, shader-local filtering, texture changes, mixed texture usage and frontend restoration while removing redundant calls");
}
