/* Diagnostic-only asynchronous eye-pass timer. Never waits for a query or GPU
 * completion. Disjoint results and full-pool samples are discarded. */
#include "gevr_frame_timing.h"
#include "glad/glad.h"
#include <SDL.h>
#include <cstring>
extern "C" int VrShowStats;

namespace {
constexpr GLenum elapsedTarget = 0x88bf, disjointParam = 0x8fbb;
constexpr GLenum availableParam = 0x8867, resultParam = 0x8866, bitsParam = 0x8864;
constexpr int capacity = 8;
struct Slot { GLuint id; bool pending, discard, redraw; } slots[capacity];
int active = -1;
bool attempted, supported;
void (APIENTRY *gen)(GLsizei, GLuint*);
void (APIENTRY *begin)(GLenum, GLuint);
void (APIENTRY *end)(GLenum);
void (APIENTRY *getiv)(GLuint, GLenum, GLint*);
void (APIENTRY *get64)(GLuint, GLenum, GLuint64*);
void (APIENTRY *targetiv)(GLenum, GLenum, GLint*);
void (APIENTRY *destroy)(GLsizei, const GLuint*);

bool initialize()
{
    if (attempted) return supported;
    attempted = true;
    if (!glGetStringi || !glGetIntegerv) return false;
    GLint extensions = 0;
    glGetIntegerv(GL_NUM_EXTENSIONS, &extensions);
    bool found = false;
    for (int i = 0; i < extensions; i++) {
        const char *s = (const char*)glGetStringi(GL_EXTENSIONS, i);
        if (s && !std::strcmp(s, "GL_EXT_disjoint_timer_query")) found = true;
    }
    if (!found) return false;
    gen = (decltype(gen))SDL_GL_GetProcAddress("glGenQueriesEXT");
    begin = (decltype(begin))SDL_GL_GetProcAddress("glBeginQueryEXT");
    end = (decltype(end))SDL_GL_GetProcAddress("glEndQueryEXT");
    getiv = (decltype(getiv))SDL_GL_GetProcAddress("glGetQueryObjectivEXT");
    get64 = (decltype(get64))SDL_GL_GetProcAddress("glGetQueryObjectui64vEXT");
    targetiv = (decltype(targetiv))SDL_GL_GetProcAddress("glGetQueryivEXT");
    destroy = (decltype(destroy))SDL_GL_GetProcAddress("glDeleteQueriesEXT");
    if (!gen || !begin || !end || !getiv || !get64 || !targetiv || !destroy) return false;
    GLint bits = 0;
    targetiv(elapsedTarget, bitsParam, &bits);
    if (!bits) return false;
    GLuint ids[capacity];
    gen(capacity, ids);
    for (int i = 0; i < capacity; i++) slots[i].id = ids[i];
    supported = true;
    return true;
}
void poll()
{
    GLint disjoint = 0;
    glGetIntegerv(disjointParam, &disjoint);
    if (disjoint) {
        for (auto &s : slots) if (s.pending) s.discard = true;
        gevrFrameTimingGpuDiscard(0);
    }
    GLuint64 results[capacity] = {};
    bool ready[capacity] = {};
    for (int i = 0; i < capacity; i++) {
        if (!slots[i].pending) continue;
        GLint available = 0;
        getiv(slots[i].id, availableParam, &available);
        if (!available) continue;
        ready[i] = true;
        get64(slots[i].id, resultParam, &results[i]);
    }
    /* A clock discontinuity during collection also invalidates every in-flight
     * result. Unavailable queries retain their slot until they become ready. */
    glGetIntegerv(disjointParam, &disjoint);
    if (disjoint) {
        for (auto &s : slots) if (s.pending) s.discard = true;
        gevrFrameTimingGpuDiscard(0);
    }
    for (int i = 0; i < capacity; i++) if (ready[i]) {
        if (!slots[i].discard) gevrFrameTimingGpu(results[i] / 1e6, slots[i].redraw);
        slots[i].pending = false;
    }
}
}
extern "C" void gfx_vr_gpu_begin(int redraw)
{
    if (!VrShowStats) {
        for (auto &s : slots) if (s.pending) s.discard = true;
        return;
    }
    if (active >= 0 || !initialize()) return;
    poll();
    for (int i = 0; i < capacity; i++) if (!slots[i].pending) {
        slots[i].discard = false;
        slots[i].redraw = redraw != 0;
        active = i;
        begin(elapsedTarget, slots[i].id);
        return;
    }
    gevrFrameTimingGpuDiscard(1);
}
extern "C" void gfx_vr_gpu_end(void)
{
    if (active < 0) return;
    end(elapsedTarget);
    slots[active].pending = true;
    active = -1;
}
extern "C" void gfx_vr_gpu_reset(void)
{
    gfx_vr_gpu_end();
    if (supported) for (auto &s : slots) destroy(1, &s.id);
    for (auto &s : slots) s = {};
    attempted = supported = false;
}
