#include "gevr_hud_bounds.h"
#include "glad/glad.h"
#include <cstdint>
#include <cstring>

namespace {
constexpr int size = 128, bytes = size * size * 4, capacity = 3;
struct Slot { GLuint buffer; GLsync fence; unsigned epoch; uint64_t serial; } slots[capacity];
GLuint fbo, texture;
unsigned epoch, interval;
uint64_t issued, applied;
int width, height, aim = -1, burst;
float boxes[2][4];
bool valid[2];
/* Per screen half: the off hand's gun counts at the left, the gun hand's at the right. */
float sides[2][2][4];
bool has[2][2];

struct State {
    GLint read, draw, pack, tex, alignment, rowLength, skipRows, skipPixels;
    GLboolean scissor;
    State() {
        glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &read);
        glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &draw);
        glGetIntegerv(GL_PIXEL_PACK_BUFFER_BINDING, &pack);
        glGetIntegerv(GL_TEXTURE_BINDING_2D, &tex);
        glGetIntegerv(GL_PACK_ALIGNMENT, &alignment);
        glGetIntegerv(GL_PACK_ROW_LENGTH, &rowLength);
        glGetIntegerv(GL_PACK_SKIP_ROWS, &skipRows);
        glGetIntegerv(GL_PACK_SKIP_PIXELS, &skipPixels);
        scissor = glIsEnabled(GL_SCISSOR_TEST);
    }
    ~State() {
        glBindFramebuffer(GL_READ_FRAMEBUFFER, read);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, draw);
        glBindBuffer(GL_PIXEL_PACK_BUFFER, pack);
        glBindTexture(GL_TEXTURE_2D, tex);
        glPixelStorei(GL_PACK_ALIGNMENT, alignment);
        glPixelStorei(GL_PACK_ROW_LENGTH, rowLength);
        glPixelStorei(GL_PACK_SKIP_ROWS, skipRows);
        glPixelStorei(GL_PACK_SKIP_PIXELS, skipPixels);
        if (scissor) glEnable(GL_SCISSOR_TEST); else glDisable(GL_SCISSOR_TEST);
    }
};
bool supported() {
    return glGenBuffers && glBindBuffer && glBufferData && glDeleteBuffers &&
        glMapBufferRange && glUnmapBuffer && glFenceSync && glClientWaitSync && glDeleteSync &&
        glGenFramebuffers && glBindFramebuffer && glFramebufferTexture2D && glCheckFramebufferStatus &&
        glDeleteFramebuffers && glGenTextures && glBindTexture && glTexImage2D && glTexParameteri &&
        glDeleteTextures && glReadPixels && glBlitFramebuffer && glGetIntegerv && glIsEnabled &&
        glPixelStorei && glEnable && glDisable;
}
bool initialize() {
    if (fbo) return true;
    glGenTextures(1, &texture);
    if (!texture) return false;
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, size, size, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glGenFramebuffers(1, &fbo);
    if (!fbo) { glDeleteTextures(1, &texture); texture=0; return false; }
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture, 0);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        glDeleteFramebuffers(1, &fbo); glDeleteTextures(1, &texture);
        fbo = texture = 0;
        return false;
    }
    for (auto &slot : slots) {
        glGenBuffers(1, &slot.buffer);
        if (!slot.buffer) { gfx_vr_hud_bounds_reset(); return false; }
        glBindBuffer(GL_PIXEL_PACK_BUFFER, slot.buffer);
        glBufferData(GL_PIXEL_PACK_BUFFER, bytes, nullptr, GL_STREAM_READ);
    }
    return true;
}
bool bounds(const unsigned char *pixels, float out[4], float half[2][4], bool found[2]) {
    int x0[3]={size,size,size}, y0[3]={size,size,size}, x1[3]={-1,-1,-1}, y1[3]={-1,-1,-1};
    for (int y=0;y<size;y++) for (int x=0;x<size;x++) if (pixels[(y*size+x)*4+3]>8) {
        const int into[2]={x<size/2 ? 0 : 1, 2};   /* its half's box and the whole one */
        for (int k : into) {
            if (x<x0[k]) x0[k]=x;
            if (x>x1[k]) x1[k]=x;
            if (y<y0[k]) y0[k]=y;
            if (y>y1[k]) y1[k]=y;
        }
    }
    for (int k=0;k<3;k++) {
        float *box=k<2 ? half[k] : out;
        if (k<2) found[k]=x1[k]>=x0[k];
        if (x1[k]<x0[k]) continue;
        box[0]=(float)x0[k]/size; box[1]=(float)y0[k]/size;
        box[2]=(float)(x1[k]+1)/size; box[3]=(float)(y1[k]+1)/size;
    }
    return x1[2]>=x0[2];
}
void poll() {
    for (auto &slot : slots) {
        if (!slot.fence) continue;
        const GLenum status=glClientWaitSync(slot.fence, 0, 0);
        if (status==GL_TIMEOUT_EXPIRED) continue;
        glDeleteSync(slot.fence); slot.fence=nullptr;
        if ((status!=GL_ALREADY_SIGNALED && status!=GL_CONDITION_SATISFIED) ||
            slot.epoch!=epoch || slot.serial<=applied) continue;
        glBindBuffer(GL_PIXEL_PACK_BUFFER, slot.buffer);
        const auto *pixels=(const unsigned char*)glMapBufferRange(GL_PIXEL_PACK_BUFFER, 0, bytes, GL_MAP_READ_BIT);
        if (!pixels) continue;
        float box[4], half[2][4];
        bool halves[2];
        const bool found=bounds(pixels, box, half, halves);
        const bool intact=glUnmapBuffer(GL_PIXEL_PACK_BUFFER)!=GL_FALSE;
        if (found && intact) {
            std::memcpy(boxes[aim], box, sizeof(box)); valid[aim]=true; applied=slot.serial;
            std::memcpy(sides[aim], half, sizeof(half)); has[aim][0]=halves[0]; has[aim][1]=halves[1];
        }
    }
}
}

extern "C" bool gfx_vr_hud_bounds_box(float out[4]) {
    if (aim>=0 && valid[aim]) { std::memcpy(out, boxes[aim], sizeof(boxes[aim])); return true; }
    /* Never crop digits using an obsolete aim/layout measurement. The initial
     * capture remains fully visible until a fenced measurement arrives. */
    out[0]=out[1]=0; out[2]=out[3]=1;
    return false;
}
extern "C" void gfx_vr_hud_bounds_update(unsigned source, int w, int h, int aiming) {
    if (w<=0 || h<=0 || !supported()) return;
    State saved;
    aiming=aiming!=0;
    if (w!=width || h!=height) {
        width=w; height=h; valid[0]=valid[1]=false; epoch++; burst=4;
    }
    if (aiming!=aim) { aim=aiming; epoch++; burst=4; }
    if (!initialize()) return;
    poll();
    if (burst>0) burst--; else if ((interval++%30)!=0) return;
    Slot *free=nullptr;
    for (auto &slot : slots) if (!slot.fence && slot.buffer) { free=&slot; break; }
    if (!free) return; /* Full ring: keep last completed bounds, never wait. */
    glDisable(GL_SCISSOR_TEST);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, source);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, fbo);
    glBlitFramebuffer(0,0,w,h,0,0,size,size,GL_COLOR_BUFFER_BIT,GL_LINEAR);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, fbo);
    glBindBuffer(GL_PIXEL_PACK_BUFFER, free->buffer);
    glPixelStorei(GL_PACK_ALIGNMENT, 4);
    glPixelStorei(GL_PACK_ROW_LENGTH, 0);
    glPixelStorei(GL_PACK_SKIP_ROWS, 0);
    glPixelStorei(GL_PACK_SKIP_PIXELS, 0);
    glReadPixels(0,0,size,size,GL_RGBA,GL_UNSIGNED_BYTE,nullptr);
    free->fence=glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
    free->epoch=epoch; free->serial=++issued;
    if (!free->fence) {
        /* An unfenced transfer cannot be safely mapped/reused on the CPU. */
        glDeleteBuffers(1, &free->buffer); free->buffer=0;
        glGenBuffers(1, &free->buffer);
        glBindBuffer(GL_PIXEL_PACK_BUFFER, free->buffer);
        glBufferData(GL_PIXEL_PACK_BUFFER, bytes, nullptr, GL_STREAM_READ);
    }
}
extern "C" bool gfx_vr_hud_bounds_side(int side, float out[4]) {
    if (side<0 || side>1 || aim<0 || !valid[aim] || !has[aim][side]) return false;
    std::memcpy(out, sides[aim][side], sizeof(sides[aim][side]));
    return true;
}
extern "C" void gfx_vr_hud_bounds_reset() {
    for (auto &slot : slots) {
        if (slot.fence) glDeleteSync(slot.fence);
        if (slot.buffer) glDeleteBuffers(1, &slot.buffer);
        slot={};
    }
    if (fbo) glDeleteFramebuffers(1, &fbo);
    if (texture) glDeleteTextures(1, &texture);
    fbo=texture=0; width=height=0; aim=-1; epoch=interval=0; issued=applied=0; burst=0;
    valid[0]=valid[1]=false;
    std::memset(has, 0, sizeof(has));
}
