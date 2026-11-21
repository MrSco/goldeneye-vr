/*
 * The loading screen (#269): the GoldenEye VR logo and a gold bar, shown on
 * every platform while a level loads (src/boss.c gevrStageFrontLoad) and
 * while the game boots after the launcher's START (port/src/main.c). It fades
 * in, follows the loading steps and fades out, even when there is little to
 * load, so every level starts the same way.
 *
 * The picture is drawn into a texture with plain GL (scissored clears for the
 * bar, blits for the logo, which is pre-darkened in steps for the fades: no
 * shaders), then shown as the launcher shows its own: a flat game's window
 * (port/flat/window/flat_launcher.cpp vr_screen_present_tex2d), or a headset's
 * screen quad in XR frames of its own (gevr_engine_shim.c
 * gevrVrLoadingFrameBegin/End, which first close the frame the game's tick had
 * open). At most a picture per 33 ms between the steps; the window's events are
 * pumped as it draws, so no system takes the game for hung.
 */
#include <SDL.h>
#include <vector>

#if defined(ANDROID) || defined(GEVR_GLES)   // GLES 3: Android (Quest, phones), iOS/tvOS (ANGLE)
#include <GLES3/gl3.h>
#else
#include "../fast3d/glad/glad.h"
#endif

extern "C" {
bool vr_screen_present_tex2d(unsigned int srcTex, int w, int h);   // the window, or the headset's screen quad
void vr_screen_set_visible(int visible);
void gevrBeanSetLoadingHook(void (*hook)(float progress));         // gevr_bean_load.c
#ifndef GEVR_FLAT
int gevrVrLoadingFrameBegin(void);                                 // gevr_engine_shim.c
void gevrVrLoadingFrameEnd(void);
#endif
#ifdef GEVR_PCVR
void gfx_pc_mirror_frame(void);                                    // gfx_pc.cpp: the desktop mirror
#endif
}
bool vr_begin_eye_render();
void vr_end_eye_render();

namespace {

#ifdef GEVR_FLAT
const int kW = 1280, kH = 720;     // the window's shape: no letterbox on a TV or monitor
#else
const int kW = 1280, kH = 960;     // the headset's screen quad, as the launcher's
#endif
const int kLogoW = 384, kLogoH = 128, kFadeSteps = 8;
const Uint32 kFadeMs = 220, kFrameMs = 33;

GLuint s_tex, s_fbo;
GLuint s_logoTex[kFadeSteps + 1], s_logoFbo[kFadeSteps + 1];
bool s_ready, s_tried, s_active;
float s_shown;
Uint32 s_start, s_last;

void makeLogos()
{
    SDL_RWops *rw = SDL_RWFromFile("gevr_logo.rgba", "rb");
    if (!rw) return;
    std::vector<unsigned char> px((size_t)kLogoW * kLogoH * 4), dim(px.size());
    const bool read = SDL_RWread(rw, px.data(), 1, px.size()) == px.size();
    SDL_RWclose(rw);
    if (!read) return;
    for (int s = 1; s <= kFadeSteps; s++) {
        // the stored rows are top first: stood up here, so blits need no flip
        for (int y = 0; y < kLogoH; y++)
            for (int x = 0; x < kLogoW * 4; x++) {
                const unsigned char v = px[(size_t)(kLogoH - 1 - y) * kLogoW * 4 + x];
                dim[(size_t)y * kLogoW * 4 + x] = (x & 3) == 3 ? v : (unsigned char)(v * s / kFadeSteps);
            }
        glGenTextures(1, &s_logoTex[s]);
        glBindTexture(GL_TEXTURE_2D, s_logoTex[s]);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, kLogoW, kLogoH, 0, GL_RGBA, GL_UNSIGNED_BYTE, dim.data());
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glGenFramebuffers(1, &s_logoFbo[s]);
        glBindFramebuffer(GL_READ_FRAMEBUFFER, s_logoFbo[s]);
        glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, s_logoTex[s], 0);
    }
}

bool ensure()
{
    if (s_tried) return s_ready;
    s_tried = true;
    GLint tex = 0, draw = 0, read = 0;
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &tex);
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &draw);
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &read);
    glGenTextures(1, &s_tex);
    glBindTexture(GL_TEXTURE_2D, s_tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, kW, kH, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glGenFramebuffers(1, &s_fbo);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, s_fbo);
    glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, s_tex, 0);
    s_ready = glCheckFramebufferStatus(GL_DRAW_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    makeLogos();
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, (GLuint)draw);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, (GLuint)read);
    glBindTexture(GL_TEXTURE_2D, (GLuint)tex);
    return s_ready;
}

void box(int x, int y, int w, int h, float r, float g, float b)
{
    if (w <= 0 || h <= 0) return;
    glScissor(x, y, w, h);
    glClearColor(r, g, b, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
}

// The picture at brightness fade (0..1), into s_tex
void paint(float fade, Uint32 now)
{
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, s_fbo);
    glViewport(0, 0, kW, kH);
    glDisable(GL_SCISSOR_TEST);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    const int lw = kW * 2 / 5, lh = lw * kLogoH / kLogoW;
    const int lx = (kW - lw) / 2, ly = kH / 2;
    const int step = (int)(fade * kFadeSteps + 0.5f);
    if (step > 0 && s_logoFbo[step]) {
        glBindFramebuffer(GL_READ_FRAMEBUFFER, s_logoFbo[step]);
        glBlitFramebuffer(0, 0, kLogoW, kLogoH, lx, ly, lx + lw, ly + lh, GL_COLOR_BUFFER_BIT, GL_LINEAR);
    }
    const int bw = lw, bh = kH / 90 > 6 ? kH / 90 : 6, bx = lx, by = ly - bh * 5;
    const float gold[3] = { 1.0f * fade, 0.84f * fade, 0.47f * fade };
    glEnable(GL_SCISSOR_TEST);
    box(bx - 2, by - 2, bw + 4, bh + 4, gold[0] * 0.55f, gold[1] * 0.55f, gold[2] * 0.55f);
    box(bx, by, bw, bh, 0.08f * fade, 0.07f * fade, 0.05f * fade);
    const int done = (int)(bw * s_shown);
    box(bx, by, done, bh, gold[0], gold[1], gold[2]);
    const int glintW = bw / 10;
    const int glintX = bx + (int)((float)(bw - glintW) * (float)((now - s_start) % 1400) / 1400.0f);
    const bool over = glintX + glintW <= bx + done;
    box(glintX, by, glintW, bh, (over ? 1.0f : 0.35f) * fade, (over ? 0.95f : 0.3f) * fade, (over ? 0.8f : 0.2f) * fade);
    glDisable(GL_SCISSOR_TEST);
}

void present()
{
#ifdef GEVR_FLAT
    vr_screen_present_tex2d(s_tex, kW, kH);   // the window, letterboxed, and swapped
#else
    if (gevrVrLoadingFrameBegin()) {
        if (vr_begin_eye_render()) vr_end_eye_render();   // the eyes black behind the screen
        vr_screen_set_visible(1);
        vr_screen_present_tex2d(s_tex, kW, kH);
#ifdef GEVR_PCVR
        gfx_pc_mirror_frame();
#endif
        gevrVrLoadingFrameEnd();
    }
#endif
}

// One picture, keeping the GL state the renderer expects
void frame(float fade)
{
    if (!ensure()) return;
    SDL_PumpEvents();
    GLint draw = 0, read = 0, viewport[4], scissor[4];
    GLfloat clear[4];
    const GLboolean scissorOn = glIsEnabled(GL_SCISSOR_TEST);
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &draw);
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &read);
    glGetIntegerv(GL_VIEWPORT, viewport);
    glGetIntegerv(GL_SCISSOR_BOX, scissor);
    glGetFloatv(GL_COLOR_CLEAR_VALUE, clear);
    const Uint32 now = SDL_GetTicks();
    paint(fade, now);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    present();
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, (GLuint)draw);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, (GLuint)read);
    glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);
    glScissor(scissor[0], scissor[1], scissor[2], scissor[3]);
    if (scissorOn) glEnable(GL_SCISSOR_TEST);
    else glDisable(GL_SCISSOR_TEST);
    glClearColor(clear[0], clear[1], clear[2], clear[3]);
    s_last = now;
}

void fadeTo(bool in)
{
    const Uint32 t0 = SDL_GetTicks();
    for (;;) {
        const Uint32 t = SDL_GetTicks() - t0;
        const float k = t >= kFadeMs ? 1.0f : (float)t / (float)kFadeMs;
        frame(in ? k : 1.0f - k);
        if (t >= kFadeMs) break;
#ifdef GEVR_FLAT
        SDL_Delay(kFrameMs / 2);   // a window without VSync would spin; a headset's frame wait paces it
#endif
    }
}

void progressHook(float progress)
{
    if (!s_active) return;
    if (progress > s_shown) s_shown = progress > 1.0f ? 1.0f : progress;
    if (SDL_GetTicks() - s_last < kFrameMs) return;
    frame(1.0f);
}

struct Register {
    Register() { gevrBeanSetLoadingHook(progressHook); }
} s_register;

}   // namespace

extern "C" void gevrLoadingBegin(void)
{
    s_active = true;
    s_shown = 0.0f;
    s_start = SDL_GetTicks();
    s_last = 0;
    fadeTo(true);
}

/* 0..1: the bar; the picture again if a frame's time has passed (any step may call it) */
extern "C" void gevrLoadingProgress(float progress)
{
    progressHook(progress);
}

extern "C" void gevrLoadingEnd(void)
{
    if (!s_active) return;
    s_shown = 1.0f;
    frame(1.0f);
    fadeTo(false);
    s_active = false;
}
