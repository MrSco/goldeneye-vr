/*
 * iOS: the game's OpenGL ES 3 context, from ANGLE on Metal.
 *
 * The shape is rebelancap's perfect-dark-ios app/gfx/gfx_angle_egl.mm (MIT,
 * port/licenses/perfect-dark-ios.txt): SDL makes a Metal view on the window,
 * ANGLE's EGL makes a window surface on its CAMetalLayer and an ES 3.0
 * context. The game's SDL_GL_* calls land here (port/ios/ios_gl.h).
 *
 * The app must not touch the GPU while it is in the background (iOS kills
 * it), so a swap waits there, pumping SDL's events, until the app is back.
 */
#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>
#import <UIKit/UIKit.h>

#include <stdio.h>
#include <string.h>
#include <SDL.h>
#include <SDL_metal.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl3.h>

#include "ios_app.h"

extern "C" int gevrBeanIsEnabled(void);   // port/src/gevr_bean.c
extern "C" int bossGetStageNum(void);     // src/boss.c (LEVELID_TITLE 90: the menus)

#ifndef EGL_PLATFORM_ANGLE_ANGLE
#define EGL_PLATFORM_ANGLE_ANGLE 0x3202
#endif
#ifndef EGL_PLATFORM_ANGLE_TYPE_ANGLE
#define EGL_PLATFORM_ANGLE_TYPE_ANGLE 0x3203
#endif
#ifndef EGL_PLATFORM_ANGLE_TYPE_METAL_ANGLE
#define EGL_PLATFORM_ANGLE_TYPE_METAL_ANGLE 0x3489
#endif
#ifndef EGL_FEATURE_OVERRIDES_DISABLED_ANGLE
#define EGL_FEATURE_OVERRIDES_DISABLED_ANGLE 0x3467
#endif

extern "C" void sysLogPrintf(int level, const char *fmt, ...);
#define IOS_LOG(...) sysLogPrintf(1, __VA_ARGS__)

static EGLDisplay s_dpy = EGL_NO_DISPLAY;
static EGLConfig s_cfg;
static EGLContext s_ctx = EGL_NO_CONTEXT;
static EGLSurface s_surf = EGL_NO_SURFACE;
static SDL_MetalView s_view;
static CAMetalLayer *s_layer;
static int s_interval;
static int s_appliedInterval = -1;   // what eglSwapInterval last got (gevrIosGLSwapWindow)
static volatile bool s_background;

/* SDL's app events, as they happen (an event watch runs inside the pump) */
static int SDLCALL iosAppEvents(void *, SDL_Event *e)
{
    switch (e->type) {
    case SDL_APP_WILLENTERBACKGROUND:
    case SDL_APP_DIDENTERBACKGROUND:
        if (!s_background) {
            s_background = true;
            if (s_dpy != EGL_NO_DISPLAY) {
                glFinish();   // nothing of ours left queued on the GPU
            }
        }
        break;
    case SDL_APP_WILLENTERFOREGROUND:
    case SDL_APP_DIDENTERFOREGROUND:
        s_background = false;
        break;
    default:
        break;
    }
    return 1;
}

static bool iosFail(const char *what)
{
    IOS_LOG("ios: ANGLE: %s (EGL 0x%x)", what, eglGetError());
    SDL_SetError("ANGLE: %s", what);
    return false;
}

static void iosSyncDrawable(void)
{
    if (!s_layer) return;
    const CGSize want = CGSizeMake(s_layer.bounds.size.width * s_layer.contentsScale,
                                   s_layer.bounds.size.height * s_layer.contentsScale);
    if (want.width > 0 && want.height > 0 && !CGSizeEqualToSize(want, s_layer.drawableSize)) {
        s_layer.drawableSize = want;
    }
}

static void iosCleanupFailedCreate(bool dpyInitialized)
{
    if (s_dpy != EGL_NO_DISPLAY) {
        if (dpyInitialized) {
            eglMakeCurrent(s_dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        }
        if (s_surf != EGL_NO_SURFACE) {
            eglDestroySurface(s_dpy, s_surf);
            s_surf = EGL_NO_SURFACE;
        }
        if (s_ctx != EGL_NO_CONTEXT) {
            eglDestroyContext(s_dpy, s_ctx);
            s_ctx = EGL_NO_CONTEXT;
        }
        if (dpyInitialized) {
            eglTerminate(s_dpy);
        }
        s_dpy = EGL_NO_DISPLAY;
    }
    // Clear host-view registration before destroying s_view so ios_app.m can
    // still inspect prev.window and detach GevrSceneDelegate.window if needed.
    gevrIosSetHostView(nullptr);
    s_layer = nil;
    if (s_view) {
        SDL_Metal_DestroyView(s_view);
        s_view = nullptr;
    }
}

static bool iosShouldDisableShaderStencilOutput(void)
{
    // Metal [[stencil]] shader output (ANGLE hasShaderStencilOutput) requires
    // Apple GPU Family 5 (A12+); disable it defensively on older GPUs such as
    // Apple TV 4K Gen 1's A10X Fusion (Apple GPU Family 3).
    if (@available(iOS 13.0, tvOS 13.0, *)) {
        id<MTLDevice> dev = MTLCreateSystemDefaultDevice();
        if (dev && ![dev supportsFamily:MTLGPUFamilyApple5]) {
            return true;
        }
    }
    return false;
}

extern "C" SDL_GLContext gevrIosGLCreateContext(SDL_Window *window)
{
    if (s_ctx != EGL_NO_CONTEXT) {
        return (SDL_GLContext)s_ctx;   // one window, one context
    }
    bool dpyInitialized = false;
    s_view = SDL_Metal_CreateView(window);
    if (!s_view) {
        iosFail("SDL_Metal_CreateView");
        return nullptr;
    }
    s_layer = (__bridge CAMetalLayer *)SDL_Metal_GetLayer(s_view);
    if (!s_layer) {
        iosFail("no CAMetalLayer");
        iosCleanupFailedCreate(false);
        return nullptr;
    }
    gevrIosSetHostView((void *)s_view);
    gevrIosWatchTouches((void *)s_view);
    gevrIosGraftWindows();
    // the panel's pixels: SDL's sceneless window left the layer at 1 pixel a point
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
    UIScreen *screen = ((__bridge UIView *)s_view).window.screen ?: UIScreen.mainScreen;
#pragma clang diagnostic pop
    s_layer.contentsScale = screen.nativeScale;
    iosSyncDrawable();

    static const char *const kDisableLegacyMtlFeatures[] = {
        "hasShaderStencilOutput",
        nullptr,
    };
    const bool disableStencilOutput = iosShouldDisableShaderStencilOutput();
    s_dpy = EGL_NO_DISPLAY;
    if (disableStencilOutput) {
        const EGLAttrib dattr[] = {
            EGL_PLATFORM_ANGLE_TYPE_ANGLE, EGL_PLATFORM_ANGLE_TYPE_METAL_ANGLE,
            EGL_FEATURE_OVERRIDES_DISABLED_ANGLE, reinterpret_cast<EGLAttrib>(kDisableLegacyMtlFeatures),
            EGL_NONE
        };
        s_dpy = eglGetPlatformDisplay(EGL_PLATFORM_ANGLE_ANGLE, (void *)EGL_DEFAULT_DISPLAY, dattr);
        if (s_dpy != EGL_NO_DISPLAY) {
            IOS_LOG("ios: ANGLE: < Apple5 GPU, disabled hasShaderStencilOutput");
        }
    }
    if (s_dpy == EGL_NO_DISPLAY) {
        const EGLAttrib dattr[] = {
            EGL_PLATFORM_ANGLE_TYPE_ANGLE, EGL_PLATFORM_ANGLE_TYPE_METAL_ANGLE,
            EGL_NONE
        };
        s_dpy = eglGetPlatformDisplay(EGL_PLATFORM_ANGLE_ANGLE, (void *)EGL_DEFAULT_DISPLAY, dattr);
    }
    if (s_dpy == EGL_NO_DISPLAY) {
        PFNEGLGETPLATFORMDISPLAYEXTPROC getPlatformDisplay =
            (PFNEGLGETPLATFORMDISPLAYEXTPROC)eglGetProcAddress("eglGetPlatformDisplayEXT");
        if (!getPlatformDisplay) {
            iosFail("no eglGetPlatformDisplayEXT");
            iosCleanupFailedCreate(false);
            return nullptr;
        }
        const EGLint dattr[] = { EGL_PLATFORM_ANGLE_TYPE_ANGLE, EGL_PLATFORM_ANGLE_TYPE_METAL_ANGLE, EGL_NONE };
        s_dpy = getPlatformDisplay(EGL_PLATFORM_ANGLE_ANGLE, (void *)EGL_DEFAULT_DISPLAY, dattr);
    }
    EGLint maj = 0, min = 0;
    if (s_dpy == EGL_NO_DISPLAY || !eglInitialize(s_dpy, &maj, &min)) {
        iosFail("no Metal display");
        iosCleanupFailedCreate(false);
        return nullptr;
    }
    dpyInitialized = true;
    // depth 24 + stencil 8, as the desktop window asks SDL for
    const EGLint cattr[] = {
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT,
        EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8,
        EGL_DEPTH_SIZE, 24, EGL_STENCIL_SIZE, 8,
        EGL_NONE
    };
    EGLint n = 0;
    if (!eglChooseConfig(s_dpy, cattr, &s_cfg, 1, &n) || n < 1) {
        iosFail("no ES 3 config with depth 24 and stencil 8");
        iosCleanupFailedCreate(dpyInitialized);
        return nullptr;
    }
    const EGLint xattr[] = { EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE };
    s_ctx = eglCreateContext(s_dpy, s_cfg, EGL_NO_CONTEXT, xattr);
    if (s_ctx == EGL_NO_CONTEXT) {
        iosFail("no ES 3.0 context");
        iosCleanupFailedCreate(dpyInitialized);
        return nullptr;
    }
    s_surf = eglCreateWindowSurface(s_dpy, s_cfg, (EGLNativeWindowType)(__bridge void *)s_layer, nullptr);
    if (s_surf == EGL_NO_SURFACE) {
        iosFail("no window surface on the layer");
        iosCleanupFailedCreate(dpyInitialized);
        return nullptr;
    }
    if (!eglMakeCurrent(s_dpy, s_surf, s_surf, s_ctx)) {
        iosFail("eglMakeCurrent");
        iosCleanupFailedCreate(dpyInitialized);
        return nullptr;
    }
    eglSwapInterval(s_dpy, s_interval);
    SDL_AddEventWatch(iosAppEvents, nullptr);
    IOS_LOG("ios: ANGLE EGL %d.%d, %s, drawable %.0fx%.0f", maj, min,
            (const char *)glGetString(GL_RENDERER), s_layer.drawableSize.width, s_layer.drawableSize.height);
    return (SDL_GLContext)s_ctx;
}

extern "C" int gevrIosGLMakeCurrent(SDL_Window *window, SDL_GLContext context)
{
    if (s_dpy == EGL_NO_DISPLAY) return -1;
    const bool ok = context ? eglMakeCurrent(s_dpy, s_surf, s_surf, (EGLContext)context)
                            : eglMakeCurrent(s_dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    return ok ? 0 : SDL_SetError("eglMakeCurrent: 0x%x", eglGetError());
}

/*
 * An A8 (Apple GPU family 2: the Apple TV HD, the A8 iPads) can't hold 60
 * with Project Bean's models: measured on an Apple TV HD, Dam ran at an
 * uneven 40, which judders on a 60 Hz screen (2026-11-20; the N64 graphics
 * held 60). Cached: Metal's answer doesn't change.
 */
extern "C" int gevrIosSlowGpu(void)
{
    static int slow = -1;
    if (slow < 0) {
        slow = 0;
        if (@available(iOS 13.0, tvOS 13.0, *)) {
            id<MTLDevice> dev = MTLCreateSystemDefaultDevice();
            slow = dev && ![dev supportsFamily:MTLGPUFamilyApple3];
        }
    }
    return slow;
}

extern "C" void gevrIosGLSwapWindow(SDL_Window *)
{
    if (s_dpy == EGL_NO_DISPLAY) return;
    {
        // There, a level with Bean on runs at a steady 30: each frame held two
        // refreshes (swap interval 2, ANGLE's frame pacing patch in
        // tools/build_angle_apple.sh). The launcher and the menus stay at 60.
        const int want = s_interval > 0 && gevrIosSlowGpu() && gevrBeanIsEnabled() && bossGetStageNum() != 90
                       ? 2 : s_interval;
        if (want != s_appliedInterval) {
            eglSwapInterval(s_dpy, want);
            if (want == 2 || s_appliedInterval == 2) {
                IOS_LOG("ios: %s", want == 2 ? "an A8 with Bean in a level: 30 fps, every other refresh"
                                             : "every refresh again");
            }
            s_appliedInterval = want;
        }
    }
    if (s_background) {
        // in the background: no GPU work until the app is back
        IOS_LOG("ios: in the background, drawing paused");
        while (s_background) {
            SDL_PumpEvents();
            SDL_Delay(50);
        }
        IOS_LOG("ios: back in the foreground");
        return;
    }
    eglSwapBuffers(s_dpy, s_surf);
    iosSyncDrawable();   // a rotation or a resize: the next frame's size
    static unsigned frames;
    if ((frames++ & 63) == 0) gevrIosGraftWindows();   // a window SDL made since (cheap)
}

extern "C" void *gevrIosGLGetProcAddress(const char *proc)
{
    return (void *)eglGetProcAddress(proc);
}

extern "C" void gevrIosGLGetDrawableSize(SDL_Window *window, int *w, int *h)
{
    if (s_layer) {
        iosSyncDrawable();
        if (w) *w = (int)s_layer.drawableSize.width;
        if (h) *h = (int)s_layer.drawableSize.height;
    } else {
        SDL_Metal_GetDrawableSize(window, w, h);
    }
}

extern "C" int gevrIosGLSetSwapInterval(int interval)
{
    s_interval = interval;
    if (s_dpy != EGL_NO_DISPLAY) {
        eglSwapInterval(s_dpy, interval);
        s_appliedInterval = interval;
    }
    return 0;
}

extern "C" int gevrIosGLGetSwapInterval(void)
{
    return s_interval;
}
