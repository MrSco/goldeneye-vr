/*
 * The dedicated server's renderer and window (docs/dedicated-server.md).
 *
 * port/fast3d is left out of the server build: there is nothing to draw. The
 * game still builds its display lists and hands them over, so this file takes
 * each and drops it, and stands in for SDL's window. The headset the server
 * hasn't got is port/flat/flat_headset.c, shared with the PC build.
 */

#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <time.h>
#include <PR/ultratypes.h>
#include <PR/gbi.h>
#include <openxr/openxr.h>

#include "../fast3d/gfx_api.h"
#include "gevr_locomotion.h"
#include "gevr_static_mesh.h"

long long gevrFlatNowNs(void);   /* port/flat/flat_headset.c */

/* --- The renderer: display lists are built by the game and dropped here ---- */

struct GfxDimensions gfx_current_window_dimensions;
struct GfxDimensions gfx_current_dimensions;
struct XYWidthHeight gfx_current_game_window_viewport;
struct XYWidthHeight gfx_current_native_viewport;
float gfx_current_native_aspect = 320.f / 220.f;
uint32_t gfx_msaa_level = 1;
bool gfx_framebuffers_enabled;
bool gfx_detail_textures_enabled;
bool gfx_external_textures_enabled;
uint32_t gevr_perf_draws, gevr_perf_tris, gevr_perf_verts, gevr_perf_tri_calls;
uint32_t gevr_perf_clipped, gevr_perf_room_tris, gevr_perf_present_passes, gevr_perf_passes;

struct GfxRenderingAPI gfx_opengl_api;   /* never called: gfx_init below keeps nothing */

void gfx_init(const struct GfxInitSettings *settings) { (void)settings; }
void gfx_start_frame(void) { }
void gfx_run(Gfx *commands) { (void)commands; }
void gfx_end_frame(void) { }
int gfx_vr_redraw_frame(void) { return 0; }
void gfx_set_texture_filter(enum FilteringMode mode) { (void)mode; }
void gfx_texture_cache_clear(void) { }
void gfx_texture_cache_delete(const uint8_t *orig_addr) { (void)orig_addr; }
int gfx_create_framebuffer(uint32_t width, uint32_t height, int upscale, int autoresize)
{
	(void)width; (void)height; (void)upscale; (void)autoresize;
	return 0;
}
void gfx_resize_framebuffer(int fb, uint32_t width, uint32_t height, int upscale, int autoresize)
{
	(void)fb; (void)width; (void)height; (void)upscale; (void)autoresize;
}
void gfx_set_framebuffer(int fb, float noise_scale) { (void)fb; (void)noise_scale; }
void gfx_reset_framebuffer(void) { }
void gfx_copy_framebuffer(int fb_dst, int fb_src, int left, int top, int use_back)
{
	(void)fb_dst; (void)fb_src; (void)left; (void)top; (void)use_back;
}
void gfx_static_mesh_release(GevrStaticMesh *mesh) { (void)mesh; }

int gevrTexpackState(void) { return -1; }
/* Friends (port/vr/vr_friends.cpp): a server has no player of its own. */
const char *gevrFriendsNotice(void) { return 0; }
int gevrTexpackToggle(void) { return -1; }
int gevrTexpackFillAvailable(void) { return 0; }
int gevrTexpackWarmDl(const void *dl, const void *seg5) { (void)dl; (void)seg5; return 0; }
void gevrTexpackPreloadBackground(const unsigned char *data, unsigned int size) { (void)data; (void)size; }
void gevrTexpackPreloadFont(const unsigned char *data, unsigned int size) { (void)data; (void)size; }

/* --- The window: none (Dab's Mod's videoInitNull does the same) ------------ */

static void wmInit(const struct GfxWindowInitSettings *settings) { (void)settings; }
static void wmVoid(void) { }
static int wmGetDisplayMode(int modenum, int *out_w, int *out_h)
{
	(void)modenum;
	*out_w = 320;
	*out_h = 220;
	return 1;
}
static int wmGetCurrentDisplayMode(int *out_w, int *out_h) { return wmGetDisplayMode(0, out_w, out_h); }
static int wmGetNumDisplayModes(void) { return 1; }
static int32_t wmZero(void) { return 0; }
static void wmSetFullscreenChangedCallback(void (*cb)(bool)) { (void)cb; }
static void wmSetBool(bool b) { (void)b; }
static void wmSetFlag(int32_t mode) { (void)mode; }
static void wmGetRefreshRate(uint32_t *rate) { *rate = 60; }
static void wmSetClosestResolution(int32_t w, int32_t h, bool center) { (void)w; (void)h; (void)center; }
static void wmSetDimensions(uint32_t w, uint32_t h, int32_t x, int32_t y) { (void)w; (void)h; (void)x; (void)y; }
static void wmGetDimensions(uint32_t *w, uint32_t *h, int32_t *x, int32_t *y)
{
	*w = 320;
	*h = 220;
	*x = *y = 0;
}
static void wmGetCenteredPositions(int32_t w, int32_t h, int32_t *x, int32_t *y) { (void)w; (void)h; *x = *y = 0; }
static bool wmTrue(void) { return true; }
static bool wmFalse(void) { return false; }
static double wmGetTime(void) { return (double)gevrFlatNowNs() / 1e9; }
static int32_t wmGetTargetFps(void) { return 60; }
static void wmSetTargetFps(int fps) { (void)fps; }
static void *wmGetWindowHandle(void) { return NULL; }
static void wmSetWindowTitle(const char *title) { (void)title; }
static int wmGetSwapInterval(void) { return 0; }
static bool wmSetSwapInterval(int interval) { (void)interval; return true; }

struct GfxWindowManagerAPI gfx_sdl = {
	wmInit, wmVoid, wmGetDisplayMode, wmGetCurrentDisplayMode, wmGetNumDisplayModes,
	wmZero, wmSetFullscreenChangedCallback, wmSetBool, wmSetBool, wmSetFlag, wmZero,
	wmZero, wmSetBool, wmGetRefreshRate, wmSetBool, wmSetClosestResolution, wmSetDimensions,
	wmGetDimensions, wmGetCenteredPositions, wmVoid, wmTrue, wmVoid, wmVoid, wmGetTime,
	wmGetTargetFps, wmSetTargetFps, wmFalse, wmGetWindowHandle, wmSetWindowTitle,
	wmGetSwapInterval, wmSetSwapInterval,
};
