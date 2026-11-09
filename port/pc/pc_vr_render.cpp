/*
 * The headset's half of the renderer, for the flat PC game (docs/pc-platforms.md).
 *
 * fast3d (gfx_pc.cpp, gfx_opengl.cpp) calls into vr_openxr.cpp, vr_screen.cpp
 * and vr_hub.cpp for eye matrices, the virtual screen and the hub room. It
 * only takes those paths while a stereo session runs with multiview, which
 * the flat game never has (gfx_pc.cpp gevr_pc_flat), so each answer here is
 * "no eyes, nothing to replay". The virtual screen is real: the flat frame is
 * drawn into the Quest's screen target and shown in the window. PDVR's mirror
 * window lives in port/fast3d/gfx_sdl2.cpp, which the PC build leaves out
 * until PCVR (P2): its globals say "no mirror".
 */

#include <stdint.h>
#include <SDL.h>
#include "../fast3d/glad/glad.h"
#include <stdio.h>
#include <stdlib.h>
#include <direct.h>
#include "pc_testinput.h"
#include <PR/ultratypes.h>
#include "system.h"

extern SDL_Window *wnd;   // pc_window.cpp

/* --- vr_openxr.cpp: eyes and their matrices -------------------------------- */

float vr_world_scale = 100.0f;      // game units per metre, unused without eyes
bool is_meta_runtime = false;
float g_eyeTanHalfFov[2] = { 1.0f, 1.0f };

bool vr_begin_eye_render() { return false; }
void vr_end_eye_render() { }
float *vr_get_eye_proj_mtx(int eye)
{
    static float identity[16] = { 1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1 };
    (void)eye;
    return identity;
}
void vr_get_eye_view_offset(int eye, float *out_tx, float *out_ty, float *out_tz, float *out_tx_HUD)
{
    (void)eye;
    if (out_tx) *out_tx = 0.0f;
    if (out_ty) *out_ty = 0.0f;
    if (out_tz) *out_tz = 0.0f;
    if (out_tx_HUD) *out_tx_HUD = 0.0f;
}
float vr_get_horizontal_fov_offset_ratio(int eye) { (void)eye; return 0.0f; }

extern "C" {

void vr_get_eye_view_proj_gl(int eye, float outVP[16])
{
    (void)eye;
    for (int i = 0; i < 16; i++) outVP[i] = (i % 5 == 0) ? 1.0f : 0.0f;
}
unsigned int vr_get_current_multiview_swapchain_tex(void) { return 0; }
int vr_eye_multisampled(void) { return 0; }
void vr_relocate_on_begin(void) { }
int vr_spacewarp_begin(void) { return 0; }
void vr_spacewarp_end(void) { }

/* the redraw replay and motion vectors (issue #53, SpaceWarp): never drawn again */
int gevrVrRedrawDelta(float out[16]) { (void)out; return 0; }
int gevrVrRedrawHandDelta(int hand, float out[16]) { (void)hand; (void)out; return 0; }
int gevrVrRedrawBodyDelta(float out[16]) { (void)out; return 0; }
int gevrVrPresentationDelta(float out[16]) { (void)out; return 0; }
void gevrVrMotionRemember(void) { }
int gevrVrMotionPrevDelta(float out[16]) { (void)out; return 0; }
int gevrVrMotionPrevHandDelta(int hand, float out[16]) { (void)hand; (void)out; return 0; }
void gevrVrMarkRedrawn(void) { }
void gevrVrMarkEyesRendered(int stereo) { (void)stereo; }

/* vr_openxr.cpp: the menu mirror's matrices (gfx_opengl.cpp VrMirrorMenuEntry) */
struct VrMirrorMenuEntry;
int vrGetMenuMirrorMVPList(int eyeIndex, struct VrMirrorMenuEntry *outEntries) { (void)eyeIndex; (void)outEntries; return 0; }

/* --- vr_screen.cpp / vr_hub.cpp: the virtual screen and the hub room ------- */

/* A test script's shot (pc_testinput.c): the window's back buffer as a 24-bit BMP in shots/. */
void pc_write_shot(const char *name, int w, int h)
{
    const int stride = (w * 3 + 3) & ~3;
    unsigned char *px = (unsigned char *)malloc((size_t)stride * h);
    char path[96];
    FILE *f;

    if (!px) return;
    _mkdir("shots");
    snprintf(path, sizeof(path), "shots/%s.bmp", name);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
    glReadBuffer(GL_BACK);
    glPixelStorei(GL_PACK_ALIGNMENT, 4);
    glReadPixels(0, 0, w, h, GL_BGR, GL_UNSIGNED_BYTE, px);   // bottom-up rows, as BMP stores them
    f = fopen(path, "wb");
    if (f) {
        const unsigned int size = 54u + (unsigned)stride * (unsigned)h;
        unsigned char hdr[54] = { 'B', 'M' };
        hdr[2] = size; hdr[3] = size >> 8; hdr[4] = size >> 16; hdr[5] = size >> 24;
        hdr[10] = 54; hdr[14] = 40;
        hdr[18] = w; hdr[19] = w >> 8; hdr[20] = w >> 16; hdr[21] = w >> 24;
        hdr[22] = h; hdr[23] = h >> 8; hdr[24] = h >> 16; hdr[25] = h >> 24;
        hdr[26] = 1; hdr[28] = 24;
        fwrite(hdr, 1, sizeof(hdr), f);
        fwrite(px, 1, (size_t)stride * h, f);
        fclose(f);
        sysLogPrintf(LOG_NOTE, "input script: wrote %s", path);
    }
    free(px);
}

/*
 * The screen target itself is the Quest's (port/vr/vr_screen.cpp). Where the
 * headset hands its layer 0 to the compositor as a quad, the PC copies it
 * into the window, letterboxed to the game's aspect.
 */
bool vr_screen_present(unsigned int srcArrayTex, int w, int h)
{
    static GLuint readFbo;
    int winW = 0, winH = 0;

    if (!srcArrayTex || w <= 0 || h <= 0 || !wnd) return false;
    SDL_GL_GetDrawableSize(wnd, &winW, &winH);
    if (winW <= 0 || winH <= 0) return false;

    // the largest rectangle of the game's aspect that fits, centred
    int dw = winW, dh = (int)((long long)winW * h / w);
    if (dh > winH) {
        dh = winH;
        dw = (int)((long long)winH * w / h);
    }
    const int dx = (winW - dw) / 2, dy = (winH - dh) / 2;

    if (!readFbo) glGenFramebuffers(1, &readFbo);
    const GLboolean scissor = glIsEnabled(GL_SCISSOR_TEST);
    glDisable(GL_SCISSOR_TEST);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, readFbo);
    glFramebufferTextureLayer(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, srcArrayTex, 0, 0);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
    glViewport(0, 0, winW, winH);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glBlitFramebuffer(0, 0, w, h, dx, dy, dx + dw, dy + dh, GL_COLOR_BUFFER_BIT, GL_LINEAR);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    if (const char *shot = pcTestInputTakeShot()) {
        pc_write_shot(shot, winW, winH);
    }
    if (scissor) glEnable(GL_SCISSOR_TEST);
    return true;
}
void vr_screen_set_visible(int visible) { (void)visible; }
void vr_pointer_draw(void) { }
void vr_hub_render(const float eyeViewProj[2][16]) { (void)eyeViewProj; }

/* --- gfx_sdl2.cpp: PDVR's PCVR mirror window, not opened ------------------- */

bool mirror_enabled = false;
void mirror_apply_size(bool enabled) { (void)enabled; }
bool gfx_sdl_is_mirror_sbs(void) { return false; }
bool gfx_sdl_is_mirror_enabled(void) { return false; }
int gfx_sdl_get_mirror_eye(void) { return 0; }
void gfx_sdl_get_mirror_dimensions(int *w, int *h) { if (w) *w = 0; if (h) *h = 0; }

/* --- vr_openxr.cpp: the session the launcher asks about ---------------------- */

int gevrVrSessionRunning(void) { return 1; }   // the window is always "up"

}   // extern "C"

/* --- vr_haptics.cpp: the Touch controllers' rumble table, which the launcher's
 * Haptics page tunes. No tracked controllers on the flat PC: an empty table. */
#include "../vr/vr_haptics.h"
extern "C" {
void vrHapticsResetAll(void) { }
void vrHapticsResetCategory(HapticCategory cat) { (void)cat; }
int vrHapticsGetCount(void) { return 0; }
HapticProfile *vrHapticsGetProfileByIndex(int index) { (void)index; return nullptr; }
void vrHapticsTriggerTest(int id) { (void)id; }
}

GLuint g_multiviewFBO = 0;   // vr_openxr.cpp's eye buffer; vr_screen.cpp hands fast3d back to it (none: the window)

SDL_Window *mirror_wnd = nullptr;
SDL_GLContext mirror_ctx = nullptr;
