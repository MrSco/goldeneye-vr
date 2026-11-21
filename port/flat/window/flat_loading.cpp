/*
 * A Bean level's loading screen on a flat screen (PC, phones, Apple TV; #269):
 * the GoldenEye VR logo and a gold bar, drawn between the steps of the stage's
 * front-load (src/boss.c gevrBeanFrontLoad, gevr_bean_load.c's hook). Without
 * it the last picture froze for the seconds Bean took to build its level and
 * send its pictures to the GPU, and the fly-in stalled after.
 *
 * Plain GL, no shaders: the bar is scissored clears, the logo a blit from its
 * own framebuffer. At most one picture per 33 ms; the window's events are
 * pumped as it draws, so the system does not take the game for hung.
 */
#ifdef GEVR_FLAT

#include <SDL.h>
#include <vector>

#if defined(ANDROID) || defined(GEVR_GLES)   // GLES 3: Android, iOS (ANGLE)
#include <GLES3/gl3.h>
#else
#include "../../fast3d/glad/glad.h"
#endif

extern SDL_Window *wnd;                                   // flat_window.cpp
extern "C" void flatSwapWindow(void);                     // flat_window.cpp
extern "C" void gevrBeanSetLoadingHook(void (*hook)(float progress));   // gevr_bean_load.c

namespace {

GLuint s_logoTex, s_logoFbo;
bool s_logoTried;
float s_shown;
Uint32 s_start, s_last;

// "GoldenEye VR", 384 x 128 RGBA, beside the game (the launcher's header)
void loadLogo()
{
    s_logoTried = true;
    SDL_RWops *rw = SDL_RWFromFile("gevr_logo.rgba", "rb");
    if (!rw) return;
    std::vector<unsigned char> px(384 * 128 * 4);
    const bool read = SDL_RWread(rw, px.data(), 1, px.size()) == px.size();
    SDL_RWclose(rw);
    if (!read) return;
    GLint tex = 0, fbo = 0;
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &tex);
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &fbo);
    glGenTextures(1, &s_logoTex);
    glBindTexture(GL_TEXTURE_2D, s_logoTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 384, 128, 0, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glGenFramebuffers(1, &s_logoFbo);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, s_logoFbo);
    glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, s_logoTex, 0);
    if (glCheckFramebufferStatus(GL_READ_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        glDeleteFramebuffers(1, &s_logoFbo);
        s_logoFbo = 0;
    }
    glBindFramebuffer(GL_READ_FRAMEBUFFER, (GLuint)fbo);
    glBindTexture(GL_TEXTURE_2D, (GLuint)tex);
}

void box(int x, int y, int w, int h, float r, float g, float b)
{
    if (w <= 0 || h <= 0) return;
    glScissor(x, y, w, h);
    glClearColor(r, g, b, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
}

// progress 0..1 moves the bar, < 0 keeps it moving, > 1 ends the screen
void flatLoadingScreen(float progress)
{
    const Uint32 now = SDL_GetTicks();
    if (progress > 1.5f) {
        s_shown = 0.0f;
        s_start = s_last = 0;
        return;
    }
    if (progress > s_shown) s_shown = progress > 1.0f ? 1.0f : progress;
    if (!s_start) s_start = now;
    if (s_last && now - s_last < 33) return;
    s_last = now;
    if (!wnd) return;
    SDL_PumpEvents();
    if (!s_logoTried) loadLogo();

    GLint draw = 0, read = 0, viewport[4], scissor[4];
    GLfloat clear[4];
    const GLboolean scissorOn = glIsEnabled(GL_SCISSOR_TEST);
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &draw);
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &read);
    glGetIntegerv(GL_VIEWPORT, viewport);
    glGetIntegerv(GL_SCISSOR_BOX, scissor);
    glGetFloatv(GL_COLOR_CLEAR_VALUE, clear);

    int w = 0, h = 0;
    SDL_GL_GetDrawableSize(wnd, &w, &h);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, w, h);
    glDisable(GL_SCISSOR_TEST);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    // the logo, a third of the width (at most 3x its size), above the middle
    int lw = w / 3;
    if (lw > 384 * 3) lw = 384 * 3;
    const int lh = lw / 3;
    const int lx = (w - lw) / 2, ly = h / 2;
    if (s_logoFbo) {
        glBindFramebuffer(GL_READ_FRAMEBUFFER, s_logoFbo);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
        // the rows are stored top first: read bottom to top to stand it up
        glBlitFramebuffer(0, 128, 384, 0, lx, ly, lx + lw, ly + lh, GL_COLOR_BUFFER_BIT, GL_LINEAR);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }

    // the bar under it: an outline, the part done, and a glint that keeps moving
    const int bw = lw, bh = h / 90 > 6 ? h / 90 : 6;
    const int bx = lx, by = ly - bh * 5;
    const float gold[3] = { 1.0f, 0.84f, 0.47f };
    glEnable(GL_SCISSOR_TEST);
    box(bx - 2, by - 2, bw + 4, bh + 4, gold[0] * 0.55f, gold[1] * 0.55f, gold[2] * 0.55f);
    box(bx, by, bw, bh, 0.08f, 0.07f, 0.05f);
    const int done = (int)(bw * s_shown);
    box(bx, by, done, bh, gold[0], gold[1], gold[2]);
    const int glintW = bw / 10;
    const int glintX = bx + (int)((float)(bw - glintW) * (float)((now - s_start) % 1400) / 1400.0f);
    const bool over = glintX + glintW <= bx + done;
    box(glintX, by, glintW, bh, over ? 1.0f : 0.35f, over ? 0.95f : 0.3f, over ? 0.8f : 0.2f);
    glDisable(GL_SCISSOR_TEST);

    flatSwapWindow();

    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, (GLuint)draw);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, (GLuint)read);
    glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);
    glScissor(scissor[0], scissor[1], scissor[2], scissor[3]);
    if (scissorOn) glEnable(GL_SCISSOR_TEST);
    glClearColor(clear[0], clear[1], clear[2], clear[3]);
}

struct Register {
    Register() { gevrBeanSetLoadingHook(flatLoadingScreen); }
} s_register;

}   // namespace

#endif
