#include "gevr_frame_timing.h"
#include "glad/glad.h"
#include <cassert>
#include <cstring>
#include <cstdio>
extern "C" { int VrShowStats = 1; }
static bool extension = true, resultsReady, disjointBefore, disjointAfter;
static int disjointCalls, bits = 64, generated, began, ended, readResults, destroyed;
static GLuint activeQuery;
static bool ready[64];
static int ticks;
static void APIENTRY mockGet(GLenum p, GLint *v) {
    if (p == GL_NUM_EXTENSIONS) *v = extension ? 1 : 0;
    else {
        assert(p == 0x8fbb);
        *v = (++disjointCalls % 2) ? disjointBefore : disjointAfter;
    }
}
static const GLubyte *APIENTRY mockString(GLenum, GLuint) {
    return (const GLubyte*)"GL_EXT_disjoint_timer_query";
}
PFNGLGETINTEGERVPROC glad_glGetIntegerv = mockGet;
PFNGLGETSTRINGIPROC glad_glGetStringi = mockString;
static void APIENTRY gen(GLsizei n, GLuint *ids) { for (int i=0; i<n; i++) ids[i]=++generated; }
static void APIENTRY begin(GLenum t, GLuint id) { assert(t==0x88bf && !activeQuery); activeQuery=id; began++; }
static void APIENTRY end(GLenum t) { assert(t==0x88bf && activeQuery); activeQuery=0; ended++; }
static void APIENTRY getiv(GLuint id, GLenum p, GLint *v) {
    assert(p==0x8867 && id!=activeQuery); *v=resultsReady; ready[id]=resultsReady;
}
static void APIENTRY get64(GLuint id, GLenum p, GLuint64 *v) {
    assert(p==0x8866 && ready[id] && id!=activeQuery);
    readResults++; *v=(++ticks % 2 ? 4000000 : 6000000);
}
static void APIENTRY targetiv(GLenum, GLenum p, GLint *v) { assert(p==0x8864); *v=bits; }
static void APIENTRY destroy(GLsizei n, const GLuint *) { destroyed+=n; }
void *SDL_GL_GetProcAddress(const char *name) {
    if (!std::strcmp(name,"glGenQueriesEXT")) return (void*)gen;
    if (!std::strcmp(name,"glBeginQueryEXT")) return (void*)begin;
    if (!std::strcmp(name,"glEndQueryEXT")) return (void*)end;
    if (!std::strcmp(name,"glGetQueryObjectivEXT")) return (void*)getiv;
    if (!std::strcmp(name,"glGetQueryObjectui64vEXT")) return (void*)get64;
    if (!std::strcmp(name,"glGetQueryivEXT")) return (void*)targetiv;
    if (!std::strcmp(name,"glDeleteQueriesEXT")) return (void*)destroy;
    assert(false); return nullptr;
}
int main() {
    gevrFrameTimingEnable(1);
    GevrFrameTimingWindow w;
    extension=false; gfx_vr_gpu_begin(0); gfx_vr_gpu_end(); assert(generated==0);
    gfx_vr_gpu_reset(); extension=true; bits=0;
    gfx_vr_gpu_begin(0); gfx_vr_gpu_end(); assert(generated==0);
    gfx_vr_gpu_reset(); bits=64;
    for (int i=0;i<8;i++) { gfx_vr_gpu_begin(i%2); gfx_vr_gpu_end(); }
    assert(generated==8 && began==8 && ended==8 && readResults==0);
    gfx_vr_gpu_begin(0); gfx_vr_gpu_end();
    gevrFrameTimingTake(&w); assert(w.gpuBusy==1 && !w.gpuCount[0]);
    resultsReady=true; gfx_vr_gpu_begin(0); gfx_vr_gpu_end();
    gevrFrameTimingTake(&w);
    assert(readResults==8 && w.gpuCount[0]==4 && w.gpuCount[1]==4);
    assert(w.gpuPeak[0]==4 && w.gpuPeak[1]==6);
    disjointBefore=true; gfx_vr_gpu_begin(1); gfx_vr_gpu_end();
    gevrFrameTimingTake(&w); assert(w.gpuDisjoint==1 && !w.gpuCount[0]);
    disjointBefore=false; disjointAfter=true;
    gfx_vr_gpu_begin(0); gfx_vr_gpu_end();
    gevrFrameTimingTake(&w); assert(w.gpuDisjoint==1 && !w.gpuCount[1]);
    disjointAfter=false;
    VrShowStats=0; gfx_vr_gpu_begin(0); gfx_vr_gpu_end();
    VrShowStats=1; gfx_vr_gpu_begin(0); gfx_vr_gpu_end();
    gevrFrameTimingTake(&w); assert(!w.gpuCount[0]);
    gfx_vr_gpu_begin(0); /* reset also balances a currently active query */
    gfx_vr_gpu_reset(); assert(destroyed==8 && !activeQuery && began==ended);
    std::puts("PASS: real GPU timer never reads unavailable results, full pool skips, disjoint/toggle results discarded, queries cleaned up");
}
