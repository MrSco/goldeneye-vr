/*
 * The dedicated server's headset and renderer (docs/dedicated-server.md).
 *
 * port/vr and port/fast3d are left out of the server build: there is no
 * headset to read and nothing to draw. The game still calls into them, so
 * this file answers each call the way a headset with no tracked hands and an
 * empty screen would. The one thing it does do is pace the game: the frame
 * pump (gevr_engine_shim.c gevrSchedBlockedRecv) waits in "xrWaitFrame" for
 * the next display frame, and here that wait is a 60 Hz clock.
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

/* --- Settings and state the VR layer defines (vr_openxr.cpp, vr_input.cpp,
 * vr_screen.cpp, vr_launcher.cpp), at their defaults ------------------- */

float RENDER_SCALE = 1.0f;
int VrAimSteady = 2;
unsigned long long VrCheatMask = 0;
int VrGunFitArmed = 0;
int VrGunSizeCheat = 0;
float VrHudDistance = 0.8f;
int VrLeftHandedMode = 0;
int VrPauseHub = false;
uint32_t VrRecommendedH = 0;
uint32_t VrRecommendedW = 0;
int VrScreenCurved = 0;
float VrScreenDistance = 2.5f;
float VrScreenFov = 60.0f;
float VrScreenHeight = 0.0f;
int VrScreenPassthrough = 0;
int VrShowStats = 0;
float VrStereoCrosshair = 0.70f;
int VrSwapJoysticks = 0;
int VrUnlockAll = 0;
bool VrWeaponRecoil = true;
float XrAspect = 1.0f;
float gCtrlPos[2][3] = { {0,0,0}, {0,0,0} };
float gCtrlQuat[2][4] = { {0,0,0,1}, {0,0,0,1} };
int32_t g_internalRenderHeight = 0;
int32_t g_internalRenderWidth = 0;
float vr_ctrl_quat_play[2][4] = { {1,0,0,0}, {1,0,0,0} };
float vr_ctrl_velocity_play[2][3];
float vr_head_velocity_play[3];
int gevrVrScreenMode = 1;   /* no stereo view: the game draws (nothing) as on the virtual screen */
int gevrZDebugMode = 0;

/* --- The headset: a session that is always up, with nothing tracked ------ */

static long long serverNowNs(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (long long)ts.tv_sec * 1000000000ll + ts.tv_nsec;
}

#define SERVER_FRAME_NS 16666667ll   /* the game's 60 Hz retrace */
#define SERVER_IDLE_FRAME_NS 200000000ll   /* 5 a second with nobody connected (server_host.c) */
int gevrServerIdle(void);

void vr_initialize(void) { }
bool vr_is_initialized(void) { return true; }
void vr_poll_events(void) { }

/*
 * xrWaitFrame: sleep to the next 1/60 s. No frame is begun, so the pump never
 * ends or redraws one; it hands the game its retrace when one is due.
 */
bool vr_begin_frame_and_update_poses(void)
{
	static long long next;
	const long long now = serverNowNs();

	if (next == 0 || now - next > 100000000ll) {
		next = now;   /* the first wait, or the game stalled: start the clock again */
	}
	if (gevrServerIdle()) {
		next = now + SERVER_IDLE_FRAME_NS;   /* one game frame each wait, so the world barely moves */
	} else {
		next += SERVER_FRAME_NS;
	}
	if (next > now) {
		struct timespec ts = { (time_t)(next / 1000000000ll), (long)(next % 1000000000ll) };
		clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &ts, NULL);
	}
	return false;
}

bool vr_end_frame_and_submit(void) { return false; }
bool vr_apply_pending_scale(void) { return false; }
int vr_prepare_next_frame(void) { return 0; }
void vr_cancel_next_frame(void) { }
long long vr_display_period_ns(void) { return SERVER_FRAME_NS; }
int vr_eyes_hold_stereo(void) { return 0; }
unsigned vr_early_poses_missed(void) { return 0; }
void vr_request_scale(float scale) { (void)scale; }
void vr_apply_refresh_rate(void) { }
int vr_get_supported_refresh_rates(int *rates, int capacity) { (void)rates; (void)capacity; return 0; }
int vr_get_internal_render_width(void) { return g_internalRenderWidth; }
int vr_get_internal_render_height(void) { return g_internalRenderHeight; }
void vr_align_with_game_angle(float target_game_angle) { (void)target_game_angle; }
int vr_passthrough_supported(void) { return 0; }
int vr_screen_curve_supported(void) { return 0; }
void vr_screen_grab(int active) { (void)active; }
void vr_screen_recenter(void) { }
void vr_screen_resize(float dist, float fov) { (void)dist; (void)fov; }
int vr_spacewarp_setting_on(void) { return 0; }
int vr_spacewarp_supported(void) { return 0; }

int gevrVrReady(void) { return 0; }
int gevrVrSessionFocused(void) { return 1; }
const char *gevrHmdName(void) { return "server"; }
float gevrVrAspect(void) { return XrAspect; }
float gevrVrFov(void) { return 90.0f; }
void gevrVrHeadPosCm(float out[3]) { out[0] = out[1] = out[2] = 0.0f; }
void gevrVrHeadQuat(float out[4]) { out[0] = 1.0f; out[1] = out[2] = out[3] = 0.0f; }
void gevrVrSetWorldScale(float unitsPerMetre) { (void)unitsPerMetre; }
void gevrVrSnapshotCameraPose(void) { }
unsigned gevrVrGripSnapshotId(void) { return 0; }
int gevrVrGripTracked(int hand) { (void)hand; return 0; }
int gevrVrGripUsable(int hand) { (void)hand; return 0; }
int gevrVrGripPoseCamera(int hand, float pos[3], float q[4]) { (void)hand; (void)pos; (void)q; return 0; }
int gevrVrGripPosePlay(int hand, float pos[3], float quat[4]) { (void)hand; (void)pos; (void)quat; return 0; }
int gevrVrWatchGesture(void) { return 0; }
int gevrVrScreenPointer(float *u, float *v) { (void)u; (void)v; return 0; }
void gevrVrCameraWorld(const float position[3], const float look[3], const float up[3]) { (void)position; (void)look; (void)up; }
void gevrVrLocomotionSnapshot(const float position[3], const float tracking[3], float yaw, uint64_t sequence)
{
	(void)position; (void)tracking; (void)yaw; (void)sequence;
}
void gevrVrLocomotionCollision(const float step[3], const float requested[3], const float actual[3])
{
	(void)step; (void)requested; (void)actual;
}
void gevrVrLocomotionResetReason(GevrLocomotionResetReason reason) { (void)reason; }
void gevrVrStatsSimulation(unsigned ticks) { (void)ticks; }
const char *gevrVrStatsText(void) { return ""; }

/* --- Controllers: none ---------------------------------------------------- */

bool get_button_state(int hand_index, const char *button_name) { (void)hand_index; (void)button_name; return false; }
float get_analog_value(int hand_index, const char *input_name) { (void)hand_index; (void)input_name; return 0.0f; }
bool get_2d_input(int hand_index, const char *input_name, XrVector2f *value)
{
	(void)hand_index; (void)input_name;
	if (value) value->x = value->y = 0.0f;
	return false;
}
const char *gevrTouchProfileName(void) { return ""; }
int gevrTouchPadState(int phys, float *trigger, float *grip, float *stickx, float *sticky, int *buttons)
{
	(void)phys; (void)trigger; (void)grip; (void)stickx; (void)sticky; (void)buttons;
	return 0;
}
int gevrNativePauseOpen(void) { return 0; }

/* --- Haptics: none -------------------------------------------------------- */

XrResult trigger_haptic_vibration_c(int hand_index, float amplitude, float duration)
{
	(void)hand_index; (void)amplitude; (void)duration;
	return XR_SUCCESS;
}
XrResult trigger_haptic_vibration_freq_c(int hand_index, float amplitude, float duration, float frequency)
{
	(void)hand_index; (void)amplitude; (void)duration; (void)frequency;
	return XR_SUCCESS;
}
XrResult stop_haptic_vibration_c(int hand_index) { (void)hand_index; return XR_SUCCESS; }
int vr_haptics_ready(void) { return 0; }
void vrHapticsInit(void) { }
void vrHapticsDumpCTable(void) { }
int vrHapticsLoadLine(const char *key, const char *val) { (void)key; (void)val; return 0; }
void vrHapticsSaveIni(void *file_handle) { (void)file_handle; }
void vrHapticsGetRumble(int id, float *out_amplitude, float *out_duration, float *out_frequency)
{
	(void)id;
	if (out_amplitude) *out_amplitude = 0.0f;
	if (out_duration) *out_duration = 0.0f;
	if (out_frequency) *out_frequency = 0.0f;
}
void vrRecoilKick(int gunhand, int recoilClass, float strength) { (void)gunhand; (void)recoilClass; (void)strength; }

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
static double wmGetTime(void) { return (double)serverNowNs() / 1e9; }
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
