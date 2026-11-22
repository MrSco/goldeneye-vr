/*
 * The PC game's window (docs/pc-platforms.md, P1): SDL2 with a desktop
 * OpenGL context, ported from Dab's Mod's port/fast3d/gfx_sdl2.cpp (itself
 * from fgsfds' Perfect Dark port, MIT) without its Vulkan window.
 *
 * port/fast3d/gfx_sdl2.cpp is the Quest's window plus Perfect Dark VR's PCVR
 * mirror window; this one is the plain desktop window the flat game draws
 * into. Pacing: the frame pump's 60 Hz clock (port/flat/flat_headset.c), so
 * target_fps starts at 0 here and the swap only waits for vsync if it is on.
 */

#include <stdio.h>
#include <string.h>
#include <SDL.h>
#include <time.h>

#include "platform.h"
#include "system.h"
#ifdef ANDROID
#include <EGL/egl.h>
#include <jni.h>
#include <SDL_syswm.h>   // the window's EGL surface (flatSwapWindow)
// SDL's own handler for SDLSurface.surfaceChanged: makes the window's EGL
// surface again when it has none (libSDL2, SDL_android.c)
extern "C" JNIEXPORT void JNICALL Java_org_libsdl_app_SDLActivity_onNativeSurfaceChanged(JNIEnv *env, jclass cls);
#endif

#include "gfx_window_manager_api.h"
#include "gfx_screen_config.h"
#ifdef GEVR_PC
#include "../../pc/pc_testinput.h"
#endif

// fast3d (gfx_opengl.cpp) reaches both by name, as with the Quest window
SDL_Window* wnd;
SDL_GLContext ctx;

/*
 * Swap the window, on Android after making sure the context draws to the
 * window's current surface.
 *
 * When the activity goes behind a file picker (or the home screen), SDL
 * destroys the window's EGL surface once this thread has unbound the context
 * in its event pump, waiting at most 500 ms. This thread can be stuck that
 * long in a swap on the dying surface, so SDL destroyed a surface that was
 * still current; the window stayed connected to it, SDL could not make the
 * new one on return, and every swap failed (EGL_BAD_SURFACE, a black screen).
 * Here: unbind the dead surface, have SDL make the window's surface again
 * (its own surfaceChanged handler), and bind the context to it.
 */
#ifdef GEVR_PC
/* The window's icon (the taskbar, Alt-Tab, a Linux dock): gevr_icon.rgba beside
 * the exe, 128 x 128 raw RGBA (tools/make_icons.py). */
static void gevrSetWindowIcon(SDL_Window *w)
{
    static unsigned char px[128 * 128 * 4];
    SDL_RWops *rw = SDL_RWFromFile("gevr_icon.rgba", "rb");
    if (!rw) return;
    const bool ok = SDL_RWread(rw, px, 1, sizeof(px)) == sizeof(px);
    SDL_RWclose(rw);
    if (!ok) return;
    SDL_Surface *s = SDL_CreateRGBSurfaceWithFormatFrom(px, 128, 128, 32, 128 * 4, SDL_PIXELFORMAT_RGBA32);
    if (s) {
        SDL_SetWindowIcon(w, s);
        SDL_FreeSurface(s);
    }
}
#endif

extern "C" void flatSwapWindow(void)
{
#ifdef ANDROID
    SDL_SysWMinfo wm;
    SDL_VERSION(&wm.version);
    if (SDL_GetWindowWMInfo(wnd, &wm) && wm.info.android.window != nullptr) {
        if (wm.info.android.surface == EGL_NO_SURFACE) {
            static Uint32 lastTry;
            if (SDL_GetTicks() - lastTry < 250) return;   // no surface to draw to yet
            lastTry = SDL_GetTicks();
            SDL_GL_MakeCurrent(wnd, nullptr);   // the dead surface lets go of the window
            Java_org_libsdl_app_SDLActivity_onNativeSurfaceChanged((JNIEnv *)SDL_AndroidGetJNIEnv(), nullptr);
            SDL_VERSION(&wm.version);
            const bool made = SDL_GetWindowWMInfo(wnd, &wm) && wm.info.android.surface != EGL_NO_SURFACE;
            sysLogPrintf(LOG_NOTE, "window: the surface was lost; made again: %s", made ? "yes" : "not yet");
            if (!made) return;
        }
        if (eglGetCurrentSurface(EGL_DRAW) != (EGLSurface)wm.info.android.surface) {
            const int bound = SDL_GL_MakeCurrent(wnd, ctx);
            sysLogPrintf(LOG_NOTE, "window: a new surface; context bound: %s", bound == 0 ? "yes" : SDL_GetError());
        }
    }
#endif
    SDL_GL_SwapWindow(wnd);
}
static bool vsync_enabled = false;

static int window_width = 1280;
static int window_height = 720;
static uint32_t fullscreen_flag = SDL_WINDOW_FULLSCREEN_DESKTOP;
static bool fullscreen_state;
static bool maximized_state;
static bool is_running = true;
static void (*on_fullscreen_changed_callback)(bool is_now_fullscreen);

static int target_fps = 0;   // the frame pump paces the game (flat_headset.c)
static uint64_t previous_time;
static uint64_t qpc_freq;
#define FRAME_INTERVAL_US_NUMERATOR 1000000
#define FRAME_INTERVAL_US_DENOMINATOR (flatTimerFps())

/*
 * Frame rate (#269, #274):
 * - Auto (VrFrameRate == 0): matches the display's refresh rate (e.g. 60, 90,
 *   120, 144, 165, 180, 240 Hz). In levels, after a 3-second warmup, if the
 *   device can't sustain 92% of its current target for 5 consecutive seconds,
 *   it steps down to a steady lower tier (display Hz -> 60 fps -> 30/25 fps)
 *   and probes back up periodically (15 s, 30 s, 60 s ... 4 min). Session-only,
 *   tracked separately for N64 vs XBLA/Bean graphics. Video.AutoHalfRate=0
 *   disables automatic step-down.
 * - Fixed cap (VrFrameRate >= 25): 30 (half rate in levels), 60, 90, 120, 144,
 *   165, 180, 240, or the monitor's refresh rate. Uses hardware VSync swap
 *   intervals when the cap evenly divides the display refresh rate under VSync,
 *   and the high-resolution frame timer otherwise.
 * - Uncapped (VrFrameRate == -1): disables VSync and the frame timer in levels
 *   while high-refresh redraws are active; menus/cutscenes/aiming stay paced
 *   on the 60 Hz clock.
 */
extern "C" int gevrBeanIsEnabled(void);
extern "C" s32 bossGetStageNum(void);
extern "C" const char *fsFullPath(const char *relPath);
extern "C" s32 vidAutoHalfRate;
extern "C" int VrFrameRate;       // vr_settings.cpp: 0 Auto, -1 Uncapped, >= 25 explicit cap
extern "C" int flatLookRedrawable(void);   // flat_headset.c: first-person look redraw available
static int s_baseInterval = -1;   // the VSync setting, as gfx_sdl_set_swap_interval last had it
static int s_activeInterval = -999;   // what SDL_GL_SetSwapInterval currently holds
static bool s_halfOn;
static bool s_autoHighOn = true;  // Auto: currently targeting > 61 Hz (tier 0 on a fast screen)
static bool s_uncappedNow;        // Uncapped active in a level
static int s_capTimerFps;         // timer-paced cap (half rate or non-divisor high-refresh cap)
static int s_autoTier[2];         // Auto's verdict this session: [0] N64, [1] XBLA / Bean (0=hz, 1=60, 2=half)
static int s_screenHz;

static int halfRateScreenHz(void)
{
    SDL_DisplayMode m;
    if (wnd && SDL_GetCurrentDisplayMode(SDL_GetWindowDisplayIndex(wnd), &m) == 0 && m.refresh_rate > 0) {
        return m.refresh_rate;
    }
    return 60;
}

extern "C" int flatScreenHz(void)
{
    return s_screenHz ? s_screenHz : (wnd ? halfRateScreenHz() : 60);
}

/* The game's own full simulation/render rate on this screen: 60 (or 50 on a 50 Hz TV). */
extern "C" int flatFullFps(void)
{
    const int hz = flatScreenHz();
    return hz > 61 ? 60 : hz;
}

/* Whether in-between camera-look redraws (> 60 fps) should be recorded and presented (#274). */
extern "C" int flatHighRefreshWanted(void)
{
    if (s_halfOn) return 0;
    if (VrFrameRate == -1) return 1;
    if (VrFrameRate > 61) return 1;
    if (VrFrameRate == 0) return flatScreenHz() > 61 && s_autoHighOn;
    return 0;
}

/* the frame pump (gevr_engine_shim.c): each frame is two of the screen's refreshes */
extern "C" int flatHalfRateActive(void)
{
    return s_halfOn ? 1 : 0;
}

static int flatAutoTargetFps(int hz, int tier)
{
    const int halfFps = (hz > 61 ? 60 : hz) / 2;
    if (tier <= 0) return hz;
    if (tier == 1 && hz > 61) return 60;
    return halfFps;
}

static int flatAutoMaxTier(int hz)
{
    return hz > 61 ? 2 : 1;   // > 61 Hz: 0=hz, 1=60, 2=30; <= 61 Hz: 0=hz, 1=half
}

static void flatApplySwapInterval(int wantInterval, int fallbackTimerFps)
{
    if (wantInterval == s_activeInterval) return;
    if (SDL_GL_SetSwapInterval(wantInterval) >= 0) {
        s_activeInterval = wantInterval;
    } else {
        if (s_baseInterval >= 0 && s_activeInterval != s_baseInterval) {
            SDL_GL_SetSwapInterval(s_baseInterval);
            s_activeInterval = s_baseInterval;
        }
        if (fallbackTimerFps > 0) s_capTimerFps = fallbackTimerFps;
    }
}

/* before each swap */
static void halfRateFrame(void)
{
    static int lastStage = -1, lastPref = -999, lastMode = -1, slowSeconds, goodSeconds, frames;
    static Uint64 stageSince, secondSince;
    static bool probing;              // Auto: trying the next higher tier again
    static Uint64 probeAt;            // Auto: when the next try is due
    static unsigned probeWaitMs = 15000;
    static bool cleaned;
    if (!cleaned) {
        cleaned = true;
        remove(fsFullPath("$S/gevr_halfrate.txt"));   // an older build's lasting verdict (#269)
    }
    if (!s_screenHz) s_screenHz = halfRateScreenHz();
    const int hz = s_screenHz;
    const int stage = bossGetStageNum();
    const bool level = stage > 0 && stage != 90;   // 90: the title stage, the menus
    const int modeIdx = gevrBeanIsEnabled() ? 1 : 0;
    const bool isAuto = VrFrameRate == 0;
    const bool vsync = s_baseInterval > 0;
    const bool autoAble = isAuto && vidAutoHalfRate && (hz >= 48);
    const int halfFps = flatFullFps() / 2;
    const Uint64 now = SDL_GetTicks64();

    if (stage != lastStage || !autoAble || VrFrameRate != lastPref || modeIdx != lastMode) {
        lastStage = stage;
        lastPref = VrFrameRate;
        lastMode = modeIdx;
        stageSince = now;
        secondSince = 0;
        slowSeconds = goodSeconds = 0;
        probing = false;
        probeWaitMs = 15000;
        probeAt = now + probeWaitMs;
    }

    const int maxTier = flatAutoMaxTier(hz);
    if (s_autoTier[modeIdx] > maxTier) s_autoTier[modeIdx] = maxTier;
    const int curTier = autoAble ? (probing && s_autoTier[modeIdx] > 0 ? s_autoTier[modeIdx] - 1 : s_autoTier[modeIdx]) : 0;
    const int curAutoFps = flatAutoTargetFps(hz, curTier);

    if (autoAble && level && now - stageSince >= 3000) {
        const bool atFloor = s_autoTier[modeIdx] >= maxTier && !probing;
        // High-refresh Auto only redraws when first-person look redraws are permitted
        // (not while aiming, in cutscenes, third person, or split screen). Don't penalize those.
        if (curAutoFps > 61 && !flatLookRedrawable()) {
            secondSince = 0;
            frames = 0;
            slowSeconds = 0;
        } else if (atFloor) {
            secondSince = 0;
            if (now >= probeAt) {
                probing = true;
                goodSeconds = slowSeconds = 0;
                sysLogPrintf(LOG_NOTE, "video: trying %d fps again", flatAutoTargetFps(hz, s_autoTier[modeIdx] - 1));
            }
        } else {
            if (!probing && s_autoTier[modeIdx] > 0 && now >= probeAt) {
                probing = true;
                goodSeconds = slowSeconds = 0;
                secondSince = 0;
                frames = 0;
                sysLogPrintf(LOG_NOTE, "video: trying %d fps again", flatAutoTargetFps(hz, s_autoTier[modeIdx] - 1));
            } else {
                if (!secondSince) {
                    secondSince = now;
                    frames = 0;
                }
                frames++;
                if (now - secondSince >= 1000) {
                    const double fps = frames * 1000.0 / (double)(now - secondSince);
                    const bool slow = fps < curAutoFps * 0.92;
                    slowSeconds = slow ? slowSeconds + 1 : 0;
                    goodSeconds = slow ? 0 : goodSeconds + 1;
                    secondSince = now;
                    frames = 0;
                    if (probing && slow) {
                        probing = false;
                        probeWaitMs = probeWaitMs >= 120000 ? 240000 : probeWaitMs * 2;
                        probeAt = now + probeWaitMs;
                        sysLogPrintf(LOG_NOTE, "video: %d fps didn't hold (%.1f fps): staying at %d, next try in %u s",
                                     curAutoFps, fps, flatAutoTargetFps(hz, s_autoTier[modeIdx]), probeWaitMs / 1000);
                    } else if (probing && goodSeconds >= 2) {
                        probing = false;
                        if (s_autoTier[modeIdx] > 0) s_autoTier[modeIdx]--;
                        probeWaitMs = 15000;
                        probeAt = now + probeWaitMs;
                        sysLogPrintf(LOG_NOTE, "video: %d fps holds again (%.1f fps)", curAutoFps, fps);
                    } else if (!probing && slowSeconds >= 5 && s_autoTier[modeIdx] < maxTier) {
                        s_autoTier[modeIdx]++;
                        probeWaitMs = 15000;
                        probeAt = now + probeWaitMs;
                        sysLogPrintf(LOG_NOTE, "video: %s levels can't hold %d fps here (%.1f for 5 s): stepping down to %d",
                                     modeIdx ? "XBLA / Bean" : "N64", curAutoFps, fps,
                                     flatAutoTargetFps(hz, s_autoTier[modeIdx]));
                    }
                }
            }
        }
    }

    const int effTier = autoAble ? (probing && s_autoTier[modeIdx] > 0 ? s_autoTier[modeIdx] - 1 : s_autoTier[modeIdx]) : 0;
    const int effTarget = VrFrameRate == -1 ? -1
                        : VrFrameRate >= 25 ? VrFrameRate
                        : flatAutoTargetFps(hz, effTier);
    s_autoHighOn = isAuto && effTarget > 61;

    const bool half = level && effTarget > 0 && effTarget <= halfFps;
    const bool high = level && (effTarget == -1 || effTarget > 61);
    const bool halfChanged = half != s_halfOn;
    s_halfOn = half;
    s_uncappedNow = level && effTarget == -1;

    int wantInterval = s_baseInterval >= 0 ? s_baseInterval : 0;
    int wantTimerFps = 0;
    int fallbackTimerFps = 0;

    if (half) {
#ifdef ANDROID
        wantTimerFps = halfFps;   // an Android surface swaps every refresh at most
#else
        // the refreshes each frame is shown for: 2 at 60 and 50 Hz, 4 at 120, 6 at 180. The nearest
        // where none fits: 5 at 144 Hz, a steady 28.8 (measured 2026-11-22)
        const int every = vsync && halfFps > 0 ? (hz + halfFps / 2) / halfFps : 0;
        if (every > 0) {
            wantInterval = every;
            fallbackTimerFps = halfFps;
        } else {
            wantTimerFps = halfFps;
        }
#endif
    } else if (high) {
#if defined(ANDROID) || defined(GEVR_IOS)
        wantTimerFps = (effTarget > 0 && effTarget < hz) ? effTarget : 0;
#else
        if (effTarget == -1) {
            wantInterval = 0;   // Uncapped: no swap wait, no timer cap
            wantTimerFps = 0;
        } else if (vsync && effTarget <= hz && hz % effTarget == 0) {
            wantInterval = hz / effTarget;   // exact hardware VSync divisor (1 at native Hz, 2 at half, etc.)
            wantTimerFps = 0;
            fallbackTimerFps = effTarget;
        } else {
            wantInterval = 0;   // non-divisor cap, cap above monitor Hz, or VSync off: high-res timer paces it
            wantTimerFps = flatLookRedrawable() ? effTarget : 0;   // 60 Hz clock paces cutscenes/menus/aim
        }
#endif
    } else if (level && effTarget >= 25 && effTarget < 60 && !(vsync && hz == effTarget)) {
        wantTimerFps = effTarget;
    }

    if (wantTimerFps != s_capTimerFps) {
        s_capTimerFps = wantTimerFps;
        previous_time = 0;
    }
    flatApplySwapInterval(wantInterval, fallbackTimerFps);

    if (halfChanged) {
        sysLogPrintf(LOG_NOTE, "video: %s", half ? (s_capTimerFps ? "half rate in this level (frame timer)"
                                                                  : "half rate in this level (every other refresh)")
                                                 : "full rate");
    }
}

/* the frame timer's rate: the Frame rate cap, or Video.FramerateLimit if lower */
static int flatTimerFps(void)
{
    if (s_capTimerFps > 0) return s_capTimerFps;
    if (s_uncappedNow) return 0;
    return target_fps;
}

static int32_t gfx_sdl_get_maximized_state(void) {
    return (int32_t)maximized_state;
}

static int32_t gfx_sdl_get_fullscreen_state(void) {
    return (int32_t)fullscreen_state;
}

static int32_t gfx_sdl_get_fullscreen_flag_mode(void) {
    return fullscreen_flag == SDL_WINDOW_FULLSCREEN_DESKTOP ? 0 : 1;
}

static void gfx_sdl_set_fullscreen_flag(int32_t mode) {
    switch (mode) {
        case 0: {
            fullscreen_flag = SDL_WINDOW_FULLSCREEN_DESKTOP;
        } break;
        case 1: {
            fullscreen_flag = SDL_WINDOW_FULLSCREEN;
        } break;
    }
}

static void set_fullscreen(bool on, bool call_callback) {
    fullscreen_state = on;
    SDL_SetWindowFullscreen(wnd, on ? fullscreen_flag : 0);
    if (call_callback && on_fullscreen_changed_callback) {
        on_fullscreen_changed_callback(on);
    }
}

static void set_maximize_window(bool on) {
	maximized_state = on;
	if (on) {
		SDL_MaximizeWindow(wnd);
	} else {
		SDL_RestoreWindow (wnd);
	}
}

static void gfx_sdl_get_active_window_refresh_rate(uint32_t* refresh_rate) {
    int display_in_use = SDL_GetWindowDisplayIndex(wnd);

    SDL_DisplayMode mode;
    SDL_GetCurrentDisplayMode(display_in_use, &mode);
    *refresh_rate = mode.refresh_rate;
}

static void get_drawable_size(int *w, int *h) {
    SDL_GL_GetDrawableSize(wnd, w, h);
}

static void gfx_sdl_init(const struct GfxWindowInitSettings *set) {
    window_width = set->width;
    window_height = set->height;

#ifdef SDL_HINT_VIDEO_HIGHDPI_DISABLED
    if (!set->allow_hidpi) {
        // HiDPI control, if available
        SDL_SetHint(SDL_HINT_VIDEO_HIGHDPI_DISABLED, "1");
#if defined(PLATFORM_WIN32) && defined(SDL_HINT_WINDOWS_DPI_AWARENESS)
        // if HiDPI is disabled, declare ourselves DPI aware to get 1:1 window size on Windows
        SDL_SetHint(SDL_HINT_WINDOWS_DPI_AWARENESS, "permonitor");
#endif
    }
#endif

    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        sysFatalError("Could not init SDL:\n%s", SDL_GetError());
    }

    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    if (sysArgCheck("--debug-gl")) {
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_DEBUG_FLAG);
    }

    int posX = set->x;
    int posY = set->y;
    int display_in_use = SDL_GetWindowDisplayIndex(wnd);
    if (display_in_use < 0) { // Fallback to default if out of bounds
        posX = SDL_WINDOWPOS_UNDEFINED;
        posY = SDL_WINDOWPOS_UNDEFINED;
    }

    if (set->centered) {
        SDL_DisplayMode mode = {};
        SDL_GetCurrentDisplayMode(0, &mode);
        posX = mode.w / 2 - window_width / 2;
        posY = mode.h / 2 - window_height / 2;
    }

    if (set->fullscreen_is_exclusive) {
        fullscreen_flag = SDL_WINDOW_FULLSCREEN;
    }

    // we will unhide the window once the GL context is successfully created
    Uint32 flags = SDL_WINDOW_HIDDEN | SDL_WINDOW_RESIZABLE;
#if defined(ANDROID) || defined(GEVR_IOS)
    // The phone/tablet APK: landscape, either way up, full screen. SDL's Android
    // window sets the activity's orientation itself, and a resizable window
    // otherwise left it to the sensor (the game ran upright, letterboxed).
    SDL_SetHint(SDL_HINT_ORIENTATIONS, "LandscapeLeft LandscapeRight");
    flags |= SDL_WINDOW_FULLSCREEN;
#endif

    // if fullscreen was requested, start the window in fullscreen right away
    if (set->fullscreen) {
        flags |= fullscreen_flag;
        fullscreen_state = true;
    }

    if (set->maximized) {
        flags |= SDL_WINDOW_MAXIMIZED;
        maximized_state = true;
    }

#ifdef SDL_WINDOW_ALLOW_HIGHDPI
    if (set->allow_hidpi) {
        flags |= SDL_WINDOW_ALLOW_HIGHDPI;
    }
#endif

#ifdef GEVR_IOS
    // iOS: a Metal view; ANGLE makes the GLES 3 context on its layer
    // (port/ios/ios_angle.mm, through ios_gl.h's SDL_GL_CreateContext)
    // (ALLOW_HIGHDPI: the panel's pixels, not its points)
    flags |= SDL_WINDOW_METAL | SDL_WINDOW_BORDERLESS | SDL_WINDOW_ALLOW_HIGHDPI;
#else
    flags |= SDL_WINDOW_OPENGL;
#endif

    // ideally we need 3.0 compat
    // if that doesn't work, try 3.2 core in case we're on mac, 2.1 compat as a last resort
    static u32 glver[][3] = {
        { 0, 0, 0                                    }, // for command line override
#if defined(ANDROID) || defined(GEVR_IOS)
        { 3, 0, SDL_GL_CONTEXT_PROFILE_ES            }, // Android (the mobile APK): GLES 3, as the Quest's; iOS: ANGLE's
#endif
        { 3, 0, SDL_GL_CONTEXT_PROFILE_COMPATIBILITY }, // 3.0: default, has all the features required
        { 4, 1, SDL_GL_CONTEXT_PROFILE_CORE          }, // 4.1core: macs only have core profile and this is the latest
        { 3, 2, SDL_GL_CONTEXT_PROFILE_CORE          }, // 3.2core: older macs will only have this at best
        { 3, 0, SDL_GL_CONTEXT_PROFILE_ES            }, // es3: don't really support ES properly, but we can try
        { 2, 1, SDL_GL_CONTEXT_PROFILE_COMPATIBILITY }, // 2.1: absolute last resort, will still require GLSL130 as an extension
    };

    u32 verstart = 1;
    const u32 verend = sizeof(glver) / sizeof(*glver);
    const char *verstr = sysArgGetString("--gl-version");
    if (verstr && *verstr) {
        // user override
        glver[0][2] = strstr(verstr, "core") ? SDL_GL_CONTEXT_PROFILE_CORE :
            (strstr(verstr, "es") ? SDL_GL_CONTEXT_PROFILE_ES :
            SDL_GL_CONTEXT_PROFILE_COMPATIBILITY);
        sscanf(verstr, "%d.%d", &glver[0][0], &glver[0][1]);
        if (glver[0][0] >= 1 && glver[0][0] <= 4 && glver[0][1] < 9) {
            verstart = 0;
        }
    }

    ctx = NULL;
    u32 vmin = 0, vmaj = 0, vprof = SDL_GL_CONTEXT_PROFILE_COMPATIBILITY;
    const char *vprofstr = "";
    for (u32 i = verstart; i < verend && !ctx; ++i) {
        vmaj = glver[i][0];
        vmin = glver[i][1];
        vprof = glver[i][2];
        vprofstr = (vprof == SDL_GL_CONTEXT_PROFILE_CORE ? "core" :
            (vprof == SDL_GL_CONTEXT_PROFILE_ES ? "es" : ""));

        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, vmaj);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, vmin);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, vprof);

        wnd = SDL_CreateWindow(set->title, posX, posY, window_width, window_height, flags);
#ifdef GEVR_PC
        if (wnd) gevrSetWindowIcon(wnd);
#endif
        if (!wnd) {
            sysLogPrintf(LOG_WARNING, "SDL: could not open SDL window for GL%d.%d%s:\n%s", vmaj, vmin, vprofstr, SDL_GetError());
            continue;
        }

        ctx = SDL_GL_CreateContext(wnd);
        if (!ctx) {
            sysLogPrintf(LOG_WARNING, "SDL: could not create GL%d.%d%s context: %s", vmaj, vmin, vprofstr, SDL_GetError());
            SDL_DestroyWindow(wnd);
            wnd = nullptr;
        }
    }

    if (!wnd || !ctx) {
        sysFatalError("Could not open SDL window with an OpenGL context of any supported version:\n%s", SDL_GetError());
    } else {
        sysLogPrintf(LOG_NOTE, "SDL: created GL%d.%d%s context", vmaj, vmin, vprofstr);
    }

    SDL_GL_MakeCurrent(wnd, ctx);
    SDL_GL_SetSwapInterval(0);

#ifdef GEVR_PC
    if (pcTestInputActive()) {
        // a test script runs while someone uses the desktop: shown, never raised over their windows
        SDL_SetHint(SDL_HINT_WINDOW_NO_ACTIVATION_WHEN_SHOWN, "1");
        // ...so the window never has focus, and SDL would drop its (virtual) pad's input
        SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
    }
#endif
    SDL_ShowWindow(wnd);

    qpc_freq = SDL_GetPerformanceFrequency();
}

static void gfx_sdl_close(void) {
    is_running = false;

    // Exclusive fullscreen changes the display's mode, and nothing but the game
    // puts it back: X11 keeps whatever mode the process died in, so quitting
    // from 640x480 left the desktop at 640x480. Leaving fullscreen is what makes
    // SDL restore the desktop's mode. fullscreen_state is left alone on purpose,
    // so that anything asking afterwards still hears what the player had chosen.
    //
    // Not on Wayland: a client cannot change the display's mode there (SDL
    // scales the window instead), so there is nothing to restore, and leaving
    // fullscreen brings back libdecor's GTK title bar, whose icons GTK loads
    // through gdk-pixbuf. From this atexit() handler that load aborted inside
    // glycin on Fedora (crash 20261001-032848), killing the exit with SIGABRT.
    const char *drv = SDL_GetCurrentVideoDriver();
    const bool wayland = drv && strcmp(drv, "wayland") == 0;

    if (wnd && fullscreen_state && fullscreen_flag == SDL_WINDOW_FULLSCREEN && !wayland) {
        SDL_SetWindowFullscreen(wnd, 0);
    }
}

static void gfx_sdl_set_fullscreen_changed_callback(void (*on_fullscreen_changed)(bool is_now_fullscreen)) {
    on_fullscreen_changed_callback = on_fullscreen_changed;
}

static void gfx_sdl_set_fullscreen(bool enable) {
    set_fullscreen(enable, true);
}

static void gfx_sdl_set_fullscreen_exclusive(bool enable) {
    const uint32_t newflag = enable ? SDL_WINDOW_FULLSCREEN : SDL_WINDOW_FULLSCREEN_DESKTOP;
    if (fullscreen_flag != newflag) {
        fullscreen_flag = newflag;
        // reset fullscreen to take new value into account if it already is in fullscreen
        if (fullscreen_state) {
            fullscreen_state = false;
            set_fullscreen(enable, true);
        }
    }
}

static void gfx_sdl_set_maximize_window(bool enable) {
    set_maximize_window(enable);
}

static void gfx_sdl_set_cursor_visibility(bool visible) {
    if (visible) {
        SDL_ShowCursor(SDL_ENABLE);
    } else {
        SDL_ShowCursor(SDL_DISABLE);
    }
}

static void get_centered_positions_native(int32_t width, int32_t height, int32_t *posX, int32_t *posY) {
    const int disp_idx = SDL_GetWindowDisplayIndex(wnd);
    SDL_DisplayMode mode = {};
    SDL_GetDesktopDisplayMode(disp_idx, &mode);
    *posX = mode.w / 2 - width / 2;
    *posY = mode.h / 2 - height / 2;
}

static void gfx_sdl_get_centered_positions(int32_t width, int32_t height, int32_t *posX, int32_t *posY) {
    const int disp_idx = SDL_GetWindowDisplayIndex(wnd);
    SDL_DisplayMode mode = {};
    SDL_GetCurrentDisplayMode(disp_idx, &mode);
    *posX = mode.w / 2 - width / 2;
    *posY = mode.h / 2 - height / 2;
}

static void gfx_sdl_set_closest_resolution(int32_t width, int32_t height, bool should_center) {
    const SDL_DisplayMode mode = {.w = width, .h = height};
    const int disp_idx = SDL_GetWindowDisplayIndex(wnd);
    SDL_DisplayMode closest = {};
    if (SDL_GetClosestDisplayMode(disp_idx, &mode, &closest)) {
        SDL_SetWindowDisplayMode(wnd, &closest);
        SDL_SetWindowSize(wnd, closest.w, closest.h);
        if (should_center) {
            int32_t posX = 0;
            int32_t posY = 0;
            get_centered_positions_native(closest.w, closest.h, &posX, &posY);
            SDL_SetWindowPosition(wnd, posX, posY);
        }
    }
}

static void gfx_sdl_set_dimensions(uint32_t width, uint32_t height, int32_t posX, int32_t posY) {
    SDL_SetWindowSize(wnd, width, height);
    SDL_SetWindowPosition(wnd, posX, posY);
}

static void gfx_sdl_get_dimensions(uint32_t* width, uint32_t* height, int32_t* posX, int32_t* posY) {
    get_drawable_size(static_cast<int*>((void*)width), static_cast<int*>((void*)height));
    SDL_GetWindowPosition(wnd, static_cast<int*>(posX), static_cast<int*>(posY));
}

#ifdef GEVR_FLAT
extern "C" void flatTouchEvent(const SDL_Event *e);   // flat_touch.cpp: the on-screen controls
#endif

static void gfx_sdl_handle_events(void) {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
#ifdef GEVR_FLAT
        flatTouchEvent(&event);
#endif
        switch (event.type) {
            case SDL_KEYDOWN:
                if (event.key.keysym.sym == SDLK_RETURN && (event.key.keysym.mod & KMOD_ALT)) {
                    // alt-enter received, switch fullscreen state
                    set_fullscreen(!fullscreen_state, true);
                }
                break;
            case SDL_WINDOWEVENT:
                if (event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED) {
                    get_drawable_size(&window_width, &window_height);
                    s_screenHz = halfRateScreenHz();
                    if (!fullscreen_state) {
                        maximized_state = SDL_GetWindowFlags(wnd) & SDL_WINDOW_MAXIMIZED ? true : false;
                    }
                } else if (event.window.event == SDL_WINDOWEVENT_MOVED
#ifdef SDL_WINDOWEVENT_DISPLAY_CHANGED
                           || event.window.event == SDL_WINDOWEVENT_DISPLAY_CHANGED
#endif
                ) {
                    s_screenHz = halfRateScreenHz();
                } else if (event.window.event == SDL_WINDOWEVENT_CLOSE &&
                           event.window.windowID == SDL_GetWindowID(wnd)) {
                    // We listen specifically for main window close because closing main window
                    // on macOS does not trigger SDL_Quit.
                    exit(0);
                }
                break;
            case SDL_QUIT:
                exit(0);
                break;
        }
    }
}

static bool gfx_sdl_start_frame(void) {
    return true;
}

static uint64_t qpc_to_100ns(uint64_t qpc) {
    return qpc / qpc_freq * 10000000 + qpc % qpc_freq * 10000000 / qpc_freq;
}

static inline void sync_framerate_with_timer(void) {
    const int fps = FRAME_INTERVAL_US_DENOMINATOR;
    if (fps <= 0) return;
    const int64_t period = (10LL * FRAME_INTERVAL_US_NUMERATOR) / fps;   // 100 ns units
    uint64_t t = qpc_to_100ns(SDL_GetPerformanceCounter());

    // Keep a phase-locked timeline across each 1/60 s game tick so a heavier game
    // frame followed by fast in-between redraws lands evenly on the cap grid without
    // overshooting the next tick or throwing away redraw slots; clamp lag to one
    // 1/60 s tick so stalls/loads never cause a catch-up burst.
    if (previous_time == 0 || (int64_t)(t - previous_time) > 166667LL + period) {
        previous_time = t;
        return;   // first frame or returning from a stall/menu: present immediately, start the grid here
    }
    const int64_t next = (int64_t)previous_time + period;
    int64_t left = next - (int64_t)t;
    if (left > 0) {
        // Exit a bit early to spin the last 1.5 ms for sub-millisecond accuracy
        if (left > 15000LL) {
            sysSleep(left - 15000LL);
        }
        do {
            sysCpuRelax();
            t = qpc_to_100ns(SDL_GetPerformanceCounter());
        } while ((int64_t)t < next);
        previous_time = (uint64_t)next;
    } else {
        // Late (e.g. heavy game frame): advance one grid step so fast redraws can
        // stay on cadence, but never let debt exceed one 1/60 s tick.
        previous_time = (uint64_t)next;
        if ((int64_t)(t - previous_time) > 166667LL) {
            previous_time = t - 166667ULL;
        }
    }
}

extern "C" int gevrLoadingHoldFrame(void);   // port/vr/gevr_loading.cpp

static void gfx_sdl_swap_buffers_begin(void) {
    // the loading screen held over a level's slow first frames, in their place (#269)
    if (gevrLoadingHoldFrame()) return;
    halfRateFrame();
    if (flatTimerFps()) {
        sync_framerate_with_timer();
    }
    flatSwapWindow();
}

static void gfx_sdl_swap_buffers_end(void) {

}

static double gfx_sdl_get_time(void) {
    return SDL_GetPerformanceCounter() / (double)qpc_freq;
}

static int32_t gfx_sdl_get_target_fps(void) {
    return target_fps;
}

static void gfx_sdl_set_target_fps(int fps) {
    target_fps = fps;
}

static bool gfx_sdl_can_disable_vsync(void) {
    return true;
}

static void *gfx_sdl_get_window_handle(void) {
    return (void *)wnd;
}

static void gfx_sdl_set_window_title(const char *title) {
    SDL_SetWindowTitle(wnd, title);
}

static int gfx_sdl_get_swap_interval(void) {
    // the VSync setting, not half rate's 2 (gevr's settings save it)
    return s_baseInterval >= 0 ? s_baseInterval : SDL_GL_GetSwapInterval();
}

static bool gfx_sdl_set_swap_interval(int interval) {
    const bool success = SDL_GL_SetSwapInterval(interval) >= 0;
    s_baseInterval = success ? interval : 0;
    s_activeInterval = s_baseInterval;
    s_halfOn = false;   // frame pacing applies itself again over the new setting
    s_capTimerFps = 0;
    s_uncappedNow = false;
    vsync_enabled = success && (interval != 0);
    if (!success) {
        sysLogPrintf(LOG_WARNING, "SDL: failed to set vsync %d: %s", interval, SDL_GetError());
    }
    return success;
}

int gfx_sdl_get_display_mode(int modenum, int *out_w, int *out_h) {
    const int display_in_use = SDL_GetWindowDisplayIndex(wnd);
    SDL_DisplayMode sdlmode;
    if (SDL_GetDisplayMode(display_in_use, modenum, &sdlmode) == 0) {
        *out_w = sdlmode.w;
        *out_h = sdlmode.h;
        return 1;
    }
    return 0;
}

int gfx_sdl_get_current_display_mode(int *out_w, int *out_h) {
    const int display_in_use = SDL_GetWindowDisplayIndex(wnd);
    SDL_DisplayMode sdlmode;
    if (SDL_GetCurrentDisplayMode(display_in_use, &sdlmode) == 0) {
        *out_w = sdlmode.w;
        *out_h = sdlmode.h;
        return 1;
    }
    return 0;
}

int gfx_sdl_get_num_display_modes(void) {
    const int display_in_use = SDL_GetWindowDisplayIndex(wnd);
    return SDL_GetNumDisplayModes(display_in_use);
}

SDL_Window *gfx_sdl_window(void) {
    return wnd;
}

struct GfxWindowManagerAPI gfx_sdl = {
    gfx_sdl_init,
    gfx_sdl_close,
    gfx_sdl_get_display_mode,
    gfx_sdl_get_current_display_mode,
    gfx_sdl_get_num_display_modes,
    gfx_sdl_get_fullscreen_state,
    gfx_sdl_set_fullscreen_changed_callback,
    gfx_sdl_set_fullscreen,
    gfx_sdl_set_fullscreen_exclusive,
    gfx_sdl_set_fullscreen_flag,
    gfx_sdl_get_fullscreen_flag_mode,
    gfx_sdl_get_maximized_state,
    gfx_sdl_set_maximize_window,
    gfx_sdl_get_active_window_refresh_rate,
    gfx_sdl_set_cursor_visibility,
    gfx_sdl_set_closest_resolution,
    gfx_sdl_set_dimensions,
    gfx_sdl_get_dimensions,
    gfx_sdl_get_centered_positions,
    gfx_sdl_handle_events,
    gfx_sdl_start_frame,
    gfx_sdl_swap_buffers_begin,
    gfx_sdl_swap_buffers_end,
    gfx_sdl_get_time,
    gfx_sdl_get_target_fps,
    gfx_sdl_set_target_fps,
    gfx_sdl_can_disable_vsync,
    gfx_sdl_get_window_handle,
    gfx_sdl_set_window_title,
    gfx_sdl_get_swap_interval,
    gfx_sdl_set_swap_interval,
};
