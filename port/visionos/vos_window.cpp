/*
 * Vision Pro: fast3d's window manager (gfx_window_manager_api.h) where there
 * is no window (docs/visionos-native.md).
 *
 * gfx_sdl2.cpp is the Quest's and PC VR's: an SDL window holding the GL
 * context (and on PC the mirror window). Here SDL has no video at all
 * (CMakeLists.txt: SDL 2.32's UIKit video does not build for visionOS), the
 * headset shows only what goes through OpenXR (port/visionos/vos_xr.mm), and
 * the context is ANGLE's with a pbuffer (vos_egl.mm). What the game asks of a
 * window is answered from the eye size; events are SDL's pump, which still
 * reads the pads.
 */
#include <SDL.h>
#include <stdio.h>

#include "platform.h"
#include "system.h"
#include "gfx_window_manager_api.h"
#include "gfx_screen_config.h"
#include "../vr/vr_log.h"

extern uint32_t VrRecommendedW;
extern uint32_t VrRecommendedH;
extern uint32_t g_internalRenderWidth;
extern uint32_t g_internalRenderHeight;

// vr_openxr.cpp's PC branch makes these current and sizes them (a no-op here)
SDL_Window *wnd;
SDL_GLContext ctx;

static int s_targetFps = 0;   // the compositor paces the frames (xrWaitFrame)

static void vos_init(const struct GfxWindowInitSettings *)
{
    ctx = SDL_GL_CreateContext(nullptr);   // port/visionos/vos_egl.mm (ios_gl.h)
    if (!ctx) {
        sysFatalError("Could not make ANGLE's OpenGL ES 3 context:\n%s", SDL_GetError());
    }
    SDL_GL_MakeCurrent(nullptr, ctx);
    vr_log("visionos: GL context ready, eyes %ux%u", VrRecommendedW, VrRecommendedH);
}

static void vos_close(void) {}

static int vos_get_display_mode(int modenum, int *w, int *h)
{
    if (modenum != 0) return 0;
    *w = (int)g_internalRenderWidth;
    *h = (int)g_internalRenderHeight;
    return 1;
}

static int vos_get_current_display_mode(int *w, int *h) { return vos_get_display_mode(0, w, h); }
static int vos_get_num_display_modes(void) { return 1; }
static int32_t vos_get_fullscreen_state(void) { return 1; }
static void vos_set_fullscreen_changed_callback(void (*)(bool)) {}
static void vos_set_fullscreen(bool) {}
static void vos_set_fullscreen_exclusive(bool) {}
static void vos_set_fullscreen_flag(int32_t) {}
static int32_t vos_get_fullscreen_flag_mode(void) { return 0; }
static int32_t vos_get_maximized_state(void) { return 0; }
static void vos_set_maximize(bool) {}
static void vos_get_active_window_refresh_rate(uint32_t *rate) { *rate = 90; }
static void vos_set_cursor_visibility(bool) {}
static void vos_set_closest_resolution(int32_t, int32_t, bool) {}
static void vos_set_dimensions(uint32_t, uint32_t, int32_t, int32_t) {}

static void vos_get_dimensions(uint32_t *w, uint32_t *h, int32_t *x, int32_t *y)
{
    *w = g_internalRenderWidth;
    *h = g_internalRenderHeight;
    if (x) *x = 0;
    if (y) *y = 0;
}

static void vos_get_centered_positions(int32_t, int32_t, int32_t *x, int32_t *y)
{
    *x = 0;
    *y = 0;
}

static void vos_handle_events(void)
{
    // the pads (SDL's MFi joysticks) and SDL's own queue; the game's input
    // layer reads the rest (port/src/input.c)
    SDL_PumpEvents();
}

static bool vos_start_frame(void) { return true; }
static void vos_swap_buffers_begin(void) {}
static void vos_swap_buffers_end(void) {}
static double vos_get_time(void) { return (double)SDL_GetPerformanceCounter() / (double)SDL_GetPerformanceFrequency(); }
static int32_t vos_get_target_fps(void) { return s_targetFps; }
static void vos_set_target_fps(int fps) { s_targetFps = fps; }
static bool vos_can_disable_vsync(void) { return false; }
static void *vos_get_window_handle(void) { return nullptr; }
static void vos_set_window_title(const char *) {}
static int vos_get_swap_interval(void) { return 0; }
static bool vos_set_swap_interval(int) { return true; }

// the names gfx_sdl2.cpp gives them (the launcher and the settings ask)
int gfx_sdl_get_display_mode(int modenum, int *w, int *h) { return vos_get_display_mode(modenum, w, h); }
int gfx_sdl_get_current_display_mode(int *w, int *h) { return vos_get_current_display_mode(w, h); }
int gfx_sdl_get_num_display_modes(void) { return 1; }

struct GfxWindowManagerAPI gfx_sdl = {
    vos_init,
    vos_close,
    vos_get_display_mode,
    vos_get_current_display_mode,
    vos_get_num_display_modes,
    vos_get_fullscreen_state,
    vos_set_fullscreen_changed_callback,
    vos_set_fullscreen,
    vos_set_fullscreen_exclusive,
    vos_set_fullscreen_flag,
    vos_get_fullscreen_flag_mode,
    vos_get_maximized_state,
    vos_set_maximize,
    vos_get_active_window_refresh_rate,
    vos_set_cursor_visibility,
    vos_set_closest_resolution,
    vos_set_dimensions,
    vos_get_dimensions,
    vos_get_centered_positions,
    vos_handle_events,
    vos_start_frame,
    vos_swap_buffers_begin,
    vos_swap_buffers_end,
    vos_get_time,
    vos_get_target_fps,
    vos_set_target_fps,
    vos_can_disable_vsync,
    vos_get_window_handle,
    vos_set_window_title,
    vos_get_swap_interval,
    vos_set_swap_interval,
};

/* gfx_sdl2.cpp's PC VR mirror window, asked after by gfx_opengl.cpp and
 * vr_openxr.cpp: there is none */
extern "C" int gfx_sdl_get_mirror_eye(void) { return 0; }
extern "C" bool gfx_sdl_is_mirror_enabled(void) { return false; }
extern "C" bool gfx_sdl_is_mirror_sbs(void) { return false; }
extern "C" void gfx_sdl_get_mirror_dimensions(int *w, int *h) { *w = 0; *h = 0; }
extern "C" void gfx_sdl_mirror_present(void) {}
extern "C" void mirror_apply_size(bool) {}
bool mirror_enabled = false;
