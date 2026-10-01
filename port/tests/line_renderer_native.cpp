#include <vector>
#include <cstdint>
#include <cstring>
#include "gevr_line_geometry.h"
using GLint=int;using GLsizei=int;using GLuint=unsigned;using GLfloat=float;using GLboolean=unsigned char;
constexpr int GL_ELEMENT_ARRAY_BUFFER_BINDING=1,GL_POLYGON_OFFSET_FILL=2,GL_POLYGON_OFFSET_FACTOR=3,GL_POLYGON_OFFSET_UNITS=4,GL_TRIANGLES=5,GL_ELEMENT_ARRAY_BUFFER=6,GL_STREAM_DRAW=7,GL_LINES=8,GL_UNSIGNED_INT=9;
constexpr GLboolean GL_FALSE=0,GL_TRUE=1;
static bool current_depth_mask,s_opaqueDepthWrite;
struct ShaderProgram { int opaque_depth_pass_location; } program={7};
static ShaderProgram *s_curPrg=&program;
static GLint elementBinding;
static bool offsetEnabled,depthMask,colorMask;
static float factor,units;
static int uniform,filledDraws,edgeDraws,error;
static std::vector<uint32_t> uploaded;
void glGenBuffers(int n,GLuint *p) {*p=17;}
void glGetIntegerv(int p,GLint *out) {*out=elementBinding;}
GLboolean glIsEnabled(int p) {return offsetEnabled;}
void glGetFloatv(int p,GLfloat *out) {*out=p==GL_POLYGON_OFFSET_FACTOR ? factor : units;}
void glEnable(int p) {offsetEnabled=true;}
void glDisable(int p) {offsetEnabled=false;}
void glPolygonOffset(float f,float u) {factor=f;units=u;}
void glColorMask(GLboolean a,GLboolean b,GLboolean c,GLboolean d) {if(a!=b||a!=c||a!=d)error=1;colorMask=a;}
void glDepthMask(GLboolean d) {depthMask=d;}
void glUniform1i(int location,int value) {if(location!=7)error=2;uniform=value;}
void glDrawArrays(int mode,int first,int count) {
    filledDraws++;
    if(mode!=GL_TRIANGLES || first!=300000 || count!=6 || colorMask || !depthMask || !offsetEnabled || factor!=1 || units!=1 || uniform!=(s_opaqueDepthWrite ? 1 : 0))error=3;
}
void glBindBuffer(int target,GLuint id) {elementBinding=id;}
void glBufferData(int target,size_t bytes,const void *data,int usage) {uploaded.assign((const uint32_t*)data,(const uint32_t*)data+bytes/4);}
void glDrawElements(int mode,int count,int type,const void *offset) {edgeDraws++;if(mode!=GL_LINES||count!=12||type!=GL_UNSIGNED_INT||offset||elementBinding!=17||!colorMask||uniform)error=4;}
/* INSERT_LINE_RENDERER */
extern "C" __declspec(dllexport) int test_line_renderer(void) {
    for(int mask=0;mask<2;mask++)for(int opaque=0;opaque<2;opaque++)for(int offset=0;offset<2;offset++) {
        current_depth_mask=mask;s_opaqueDepthWrite=opaque;elementBinding=9;
        offsetEnabled=offset;depthMask=mask;colorMask=true;factor=2.5f;units=-7;uniform=0;filledDraws=edgeDraws=error=0;
        gevr_draw_world_lines(300000,6);
        if(error)return error;
        if(filledDraws!=(mask||opaque) || edgeDraws!=1 || elementBinding!=9 || offsetEnabled!=bool(offset) || factor!=2.5f || units!=-7 || depthMask!=bool(mask) || !colorMask || uniform) return 5;
        uint32_t expected[12]={300000,300001,300001,300002,300002,300000,300003,300004,300004,300005,300005,300003};
        if(uploaded.size()!=12 || std::memcmp(expected,uploaded.data(),sizeof(expected)))return 6;
    }
    return 0;
}
