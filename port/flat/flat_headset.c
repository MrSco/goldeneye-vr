/*
 * A headset that isn't there: the VR layer's answers for the builds without
 * one (docs/pc-platforms.md). The dedicated server (port/server) and the flat
 * PC game (port/pc) both leave port/vr out, and the game still asks it for
 * poses, buttons and haptics; this file answers each call the way a headset
 * with no tracked hands would.
 *
 * It also paces the game: the frame pump (gevr_engine_shim.c
 * gevrSchedBlockedRecv) waits in "xrWaitFrame" for the next display frame,
 * and here that wait is a 60 Hz clock, the N64's retrace.
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#ifdef _WIN32
#include <windows.h>
#endif
#include <PR/ultratypes.h>
#include <PR/gbi.h>
#include <openxr/openxr.h>

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
#ifdef GEVR_HEADLESS   /* the PC build has fast3d, which defines it */
int VrPauseHub = false;
#endif
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
#ifdef GEVR_HEADLESS
int gevrVrScreenMode = 1;   /* no stereo view: the game draws (nothing) as on the virtual screen */
#endif   /* the PC build has fast3d, which defines it (gfx_pc.cpp) */
#ifdef GEVR_HEADLESS   /* the PC build has fast3d, which defines it */
int gevrZDebugMode = 0;
#endif

/* --- The headset: a session that is always up, with nothing tracked ------ */

long long gevrFlatNowNs(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (long long)ts.tv_sec * 1000000000ll + ts.tv_nsec;
}

/* Sleep until gevrFlatNowNs() reaches t. Windows' Sleep, and the winpthreads
 * clock_nanosleep built on it, wake on the 15.6 ms system tick, a whole frame
 * late; a high-resolution waitable timer wakes within a fraction of 1 ms. */
static void flatSleepUntil(long long t)
{
#ifdef _WIN32
	static HANDLE timer;
	const long long now = gevrFlatNowNs();
	if (t <= now) return;
	if (!timer) timer = CreateWaitableTimerExW(NULL, NULL, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
	if (timer) {
		LARGE_INTEGER due;
		due.QuadPart = -(t - now) / 100;   /* relative, in 100 ns units */
		if (SetWaitableTimer(timer, &due, 0, NULL, NULL, FALSE)) {
			WaitForSingleObject(timer, INFINITE);
			return;
		}
	}
	Sleep((DWORD)((t - now) / 1000000));
#elif defined(__APPLE__)
	/* no clock_nanosleep on Apple: the time left, relative */
	const long long left = t - gevrFlatNowNs();
	if (left > 0) {
		struct timespec ts = { (time_t)(left / 1000000000ll), (long)(left % 1000000000ll) };
		nanosleep(&ts, NULL);
	}
#else
	struct timespec ts = { (time_t)(t / 1000000000ll), (long)(t % 1000000000ll) };
	clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &ts, NULL);
#endif
}

#define FLAT_FRAME_NS 16666667ll   /* the game's 60 Hz retrace */
#define FLAT_IDLE_FRAME_NS 200000000ll   /* 5 a second on an empty dedicated server (server_host.c) */
int gevrServerIdle(void);   /* the PC build answers it in port/pc: never idle */

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
	const long long now = gevrFlatNowNs();

	if (next == 0 || now - next > 100000000ll) {
		next = now;   /* the first wait, or the game stalled: start the clock again */
	}
	if (gevrServerIdle()) {
		next = now + FLAT_IDLE_FRAME_NS;   /* one game frame each wait, so the world barely moves */
	} else {
		next += FLAT_FRAME_NS;
	}
	if (next > now) {
		flatSleepUntil(next);
	}
	return false;
}

bool vr_end_frame_and_submit(void) { return false; }
bool vr_apply_pending_scale(void) { return false; }
int vr_prepare_next_frame(void) { return 0; }
void vr_cancel_next_frame(void) { }
long long vr_display_period_ns(void) { return FLAT_FRAME_NS; }
int vr_eyes_hold_stereo(void) { return 0; }
unsigned vr_early_poses_missed(void) { return 0; }
void vr_request_scale(float scale) { (void)scale; }
void vr_apply_refresh_rate(void) { }
int vr_get_supported_refresh_rates(int *rates, int capacity) { (void)rates; (void)capacity; return 0; }
int vr_get_internal_render_width(void) { return g_internalRenderWidth; }
int vr_get_internal_render_height(void) { return g_internalRenderHeight; }
void vr_align_with_game_angle(float target_game_angle) { (void)target_game_angle; }
int vr_passthrough_supported(void) { return 0; }
int vr_eye_msaa_supported(void) { return 0; }   /* the settings table's anti-aliasing row (gevr_settings.c) */
int vr_screen_curve_supported(void) { return 0; }
void vr_screen_grab(int active) { (void)active; }
void vr_screen_recenter(void) { }
void vr_screen_resize(float dist, float fov) { (void)dist; (void)fov; }
int vr_spacewarp_setting_on(void) { return 0; }
int vr_spacewarp_supported(void) { return 0; }

int gevrVrReady(void) { return 0; }
int gevrVrSessionFocused(void) { return 1; }
#ifdef GEVR_HEADLESS
const char *gevrHmdName(void) { return "server"; }
#else
const char *gevrHmdName(void) { return "desktop"; }
#endif
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
#ifdef GEVR_PC_FLAT
/* the mouse over the window's picture, where the headset has its laser:
 * front.c's folder cursor follows it (port/src/input.c) */
extern s32 inputMenuPointer(f32 *u, f32 *v);
int gevrVrScreenPointer(float *u, float *v) { return inputMenuPointer(u, v); }
#else
int gevrVrScreenPointer(float *u, float *v) { (void)u; (void)v; return 0; }
#endif
void gevrVrScreenPointerOwnCursor(void) {}
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
/*
 * Show stats in a flat game (PC, phone, tablet): the headset's readout
 * (vr_openxr.cpp) without the XR lines - the build, the game's tick and frame
 * rates, the worst gap between frames and the CPU time per frame
 * (gevr_engine_shim.c). Called once a drawn frame while the readout is on;
 * the numbers change once a second. It was empty here, so a phone showed an
 * empty box (user, 2026-11-09).
 */
static unsigned s_flatStatSim;
void gevrVrStatsSimulation(unsigned ticks) { s_flatStatSim += ticks; }
extern const char gevrBuildId[];   /* generated, port/cmake/buildid.cmake */
extern const char *gevrPerfText(void);   /* gevr_engine_shim.c: CPU time per frame */
const char *gevrVrStatsText(void)
{
	static char text[512];
	static long long since, last;
	static unsigned frames;
	static double worstMs;
	const long long now = gevrFlatNowNs();
	if (last) {
		const double gap = (now - last) / 1e6;
		if (gap > worstMs && gap < 1000.0) worstMs = gap;   /* not a pause or the background */
	}
	last = now;
	frames++;
	if (!since) since = now;
	if (now - since >= 1000000000ll || !text[0]) {
		const double el = (now - since) / 1e9;
		char build[16];
		snprintf(build, sizeof(build), "%s", gevrBuildId);
		for (char *c = build; *c; c++) {
			if (*c == ' ') { *c = 0; break; }
		}
		const char *perf = gevrPerfText();
		snprintf(text, sizeof(text), "BUILD %s\nSIM %.0f HZ  RENDER %.0f FPS\nWORST RENDER GAP %.1f MS%s%s",
		         build, el > 0.0 ? s_flatStatSim / el : 0.0, el > 0.0 ? frames / el : 0.0, worstMs,
		         perf && perf[0] ? "\n" : "", perf ? perf : "");
		since = now;
		frames = 0;
		s_flatStatSim = 0;
		worstMs = 0.0;
	}
	return text;
}

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
#ifndef GEVR_FLAT   /* the flat game has the panel (port/src/input.c); the server has none */
int gevrNativePauseOpen(void) { return 0; }
#endif

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

