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
 * Frame rate (2026-11-20, reworked for #269): a device that can't hold the
 * screen's rate in a level plays it at a steady half instead. An uneven rate
 * judders on a TV: an Apple TV HD ran Dam with XBLA / Bean at 40 fps, each
 * frame shown for one refresh or two (the N64 graphics held 60 there).
 *
 * The settings' Frame rate row (VrFrameRate): Auto, Full (the screen's rate
 * always, 60 at most: flatFullFps) or Half (always, in levels; of the game's
 * 60 on a faster screen, and by the frame timer with VSync off). Auto measures in levels with VSync on,
 * after each level's first 3 seconds (loading, first draws): 5 seconds in a
 * row under 92% of the screen's rate (55 of 60) turn half rate on. It is no
 * longer a verdict for good (#269: Dam's mountains once locked every later
 * run, Bean or not, until a reinstall): after a stretch at half rate it tries
 * the full rate again, and stays there if 2 seconds hold it; if they don't,
 * the next try waits twice as long (15 s, then 30, 60 ... 4 minutes). A new
 * level of the same graphics (the N64's or Bean's) starts where the last one
 * ended, this session only: nothing is saved (an older build's
 * gevr_halfrate.txt is deleted). Menus and the launcher keep the full rate;
 * screens of 48 to 61 Hz only (a faster one is a fast device's).
 *
 * Every other refresh where the swap allows it (Apple through ANGLE's frame
 * pacing patch, tools/build_angle_apple.sh; the PC's WGL and GLX), the
 * game's frame timer otherwise (an Android surface swaps every refresh at
 * most). At half rate the frame pump steps the game two sixtieths a frame
 * (gevr_engine_shim.c, flatHalfRateActive), not by when the CPU started it.
 * Video.AutoHalfRate=0 makes Auto the full rate, as before.
 */
extern "C" int gevrBeanIsEnabled(void);
extern "C" s32 bossGetStageNum(void);
extern "C" const char *fsFullPath(const char *relPath);
extern "C" s32 vidAutoHalfRate;
extern "C" int VrFrameRate;       // vr_settings.cpp: 0 Auto, 1 Full, 2 Half
static int s_baseInterval = -1;   // the VSync setting, as gfx_sdl_set_swap_interval last had it
static bool s_halfOn;
static int s_halfTimerFps;        // half rate by the frame timer instead of the swap
static unsigned s_halfModes;      // Auto's verdict this session: 1 the N64 graphics, 2 XBLA / Bean
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

/*
 * The settings' Frame rate row, for its names (60 and 30, 50 and 25): the full rate a flat
 * game reaches on this screen. The game is GoldenEye's 60 Hz one (the frame pump's clock,
 * flat_headset.c), so a faster screen shows 60 too: the row read "180 fps" on a 180 Hz
 * monitor and the game ran at 60 (#274: "the game wont uncap the fps to 180").
 */
extern "C" int flatFullFps(void)
{
    const int hz = flatScreenHz();
    return hz > 61 ? 60 : hz;
}

/* the frame pump (gevr_engine_shim.c): each frame is two of the screen's refreshes */
extern "C" int flatHalfRateActive(void)
{
    return s_halfOn ? 1 : 0;
}

/* before each swap */
static void halfRateFrame(void)
{
    static int lastStage = -1, slowSeconds, goodSeconds, frames;
    static Uint64 stageSince, secondSince;
    static bool probing;              // Auto: trying the full rate again
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
    const unsigned mode = gevrBeanIsEnabled() ? 2u : 1u;
    const int pref = VrFrameRate == 1 || (VrFrameRate == 0 && !vidAutoHalfRate) ? 1 : VrFrameRate == 2 ? 2 : 0;
    const bool vsync = s_baseInterval > 0;
    const bool able = vsync && hz >= 48 && hz <= 61;   // Auto: a 60 or 50 Hz screen
    const Uint64 now = SDL_GetTicks64();
    if (stage != lastStage || !able || pref != 0) {
        lastStage = stage;
        stageSince = now;
        secondSince = 0;
        slowSeconds = goodSeconds = 0;
        probing = false;
        probeWaitMs = 15000;
        probeAt = now + probeWaitMs;
    }
    if (able && level && pref == 0 && now - stageSince >= 3000) {
        const bool atHalf = (s_halfModes & mode) && !probing;
        if (atHalf) {
            secondSince = 0;   // nothing to measure at half rate: the swap holds it at half
            if (now >= probeAt) {
                probing = true;
                goodSeconds = slowSeconds = 0;
                sysLogPrintf(LOG_NOTE, "video: trying the full %d fps again", hz);
            }
        } else {
            if (!secondSince) {
                secondSince = now;
                frames = 0;
            }
            frames++;
            if (now - secondSince >= 1000) {
                const double fps = frames * 1000.0 / (double)(now - secondSince);
                const bool slow = fps < hz * 0.92;
                slowSeconds = slow ? slowSeconds + 1 : 0;
                goodSeconds = slow ? 0 : goodSeconds + 1;
                secondSince = now;
                frames = 0;
                if (probing && slow) {
                    probing = false;
                    probeWaitMs = probeWaitMs >= 120000 ? 240000 : probeWaitMs * 2;
                    probeAt = now + probeWaitMs;
                    sysLogPrintf(LOG_NOTE, "video: the full rate didn't hold (%.1f fps): half rate, next try in %u s",
                                 fps, probeWaitMs / 1000);
                } else if (probing && goodSeconds >= 2) {
                    probing = false;
                    s_halfModes &= ~mode;
                    probeWaitMs = 15000;
                    sysLogPrintf(LOG_NOTE, "video: the full %d fps holds again (%.1f fps)", hz, fps);
                } else if (!probing && slowSeconds >= 5) {
                    s_halfModes |= mode;
                    probeWaitMs = 15000;
                    probeAt = now + probeWaitMs;
                    sysLogPrintf(LOG_NOTE, "video: %s levels can't hold %d fps here (%.1f for 5 s): half rate, a steady %d",
                                 mode == 2u ? "XBLA / Bean" : "N64", hz, fps, hz / 2);
                }
            }
        }
    }
    const bool half = level && (pref == 2 || (pref == 0 && able && (s_halfModes & mode) && !probing));
    if (half == s_halfOn) return;
    s_halfOn = half;
    s_halfTimerFps = 0;
    // half the game's rate, not the screen's: 30 on a 120 or 180 Hz screen too (#274)
    const int halfFps = flatFullFps() / 2;
#ifdef ANDROID
    if (half) s_halfTimerFps = halfFps;   // an Android surface swaps every refresh at most
#else
    // the refreshes each frame is shown for: 2 at 60 and 50 Hz, 4 at 120, 6 at 180. The nearest
    // where none fits: 5 at 144 Hz, a steady 28.8 (the frame timer under VSync there gave an
    // uneven 27.5, each frame held to the refresh after its time; measured 2026-11-22)
    const int every = vsync && halfFps > 0 ? (hz + halfFps / 2) / halfFps : 0;
    if (!half) {
        SDL_GL_SetSwapInterval(s_baseInterval);
    } else if (!every || SDL_GL_SetSwapInterval(every) < 0) {
        SDL_GL_SetSwapInterval(s_baseInterval);
        s_halfTimerFps = halfFps;   // no such interval here: the frame timer
    }
#endif
    sysLogPrintf(LOG_NOTE, "video: %s", half ? (s_halfTimerFps ? "half rate in this level (frame timer)"
                                                               : "half rate in this level (every other refresh)")
                                             : "full rate");
}

/* the frame timer's rate: the FramerateLimit setting's, or half rate's if lower */
static int flatTimerFps(void)
{
    if (s_halfTimerFps && (!target_fps || s_halfTimerFps < target_fps)) return s_halfTimerFps;
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
                    if (!fullscreen_state) {
                        maximized_state = SDL_GetWindowFlags(wnd) & SDL_WINDOW_MAXIMIZED ? true : false;
                    }
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
    uint64_t t;
    t = qpc_to_100ns(SDL_GetPerformanceCounter());

    const int64_t next = previous_time + 10 * FRAME_INTERVAL_US_NUMERATOR / FRAME_INTERVAL_US_DENOMINATOR;
    int64_t left = next - t;
    // We want to exit a bit early, so we can busy-wait the rest to never miss the deadline
    left -= 15000UL;
    if (left > 0) {
        sysSleep(left);
    }

    do {
        sysCpuRelax();
        t = qpc_to_100ns(SDL_GetPerformanceCounter());
    } while ((int64_t)t < next);

    t = qpc_to_100ns(SDL_GetPerformanceCounter());
    if (left > 0 && t - next < 10000) {
        // In case it takes some time for the application to wake up after sleep,
        // or inaccurate timer,
        // don't let that slow down the framerate.
        t = next;
    }
    previous_time = t;
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
    s_halfOn = false;   // half rate applies itself again over the new setting
    s_halfTimerFps = 0;
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
