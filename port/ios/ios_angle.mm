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
#import <QuartzCore/CAMetalLayer.h>
#import <UIKit/UIKit.h>

#include <stdio.h>
#include <string.h>
#include <SDL.h>
#include <SDL_metal.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl3.h>

#ifndef EGL_PLATFORM_ANGLE_ANGLE
#define EGL_PLATFORM_ANGLE_ANGLE 0x3202
#endif
#ifndef EGL_PLATFORM_ANGLE_TYPE_ANGLE
#define EGL_PLATFORM_ANGLE_TYPE_ANGLE 0x3203
#endif
#ifndef EGL_PLATFORM_ANGLE_TYPE_METAL_ANGLE
#define EGL_PLATFORM_ANGLE_TYPE_METAL_ANGLE 0x3489
#endif

extern "C" void sysLogPrintf(int level, const char *fmt, ...);
extern "C" int gevrIosGraftWindows(void);   // ios_app.m: SDL's window onto the scene
extern "C" void gevrIosSetHostView(void *view);
#define IOS_LOG(...) sysLogPrintf(1, __VA_ARGS__)

static EGLDisplay s_dpy = EGL_NO_DISPLAY;
static EGLConfig s_cfg;
static EGLContext s_ctx = EGL_NO_CONTEXT;
static EGLSurface s_surf = EGL_NO_SURFACE;
static SDL_MetalView s_view;
static CAMetalLayer *s_layer;
static int s_interval;
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

extern "C" SDL_GLContext gevrIosGLCreateContext(SDL_Window *window)
{
    if (s_ctx != EGL_NO_CONTEXT) {
        return (SDL_GLContext)s_ctx;   // one window, one context
    }
    s_view = SDL_Metal_CreateView(window);
    if (!s_view) {
        iosFail("SDL_Metal_CreateView");
        return nullptr;
    }
    s_layer = (__bridge CAMetalLayer *)SDL_Metal_GetLayer(s_view);
    if (!s_layer) {
        iosFail("no CAMetalLayer");
        return nullptr;
    }
    gevrIosSetHostView((void *)s_view);
    gevrIosGraftWindows();
    // the panel's pixels: SDL's sceneless window left the layer at 1 pixel a point
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
    UIScreen *screen = ((__bridge UIView *)s_view).window.screen ?: UIScreen.mainScreen;
#pragma clang diagnostic pop
    s_layer.contentsScale = screen.nativeScale;
    iosSyncDrawable();

    PFNEGLGETPLATFORMDISPLAYEXTPROC getPlatformDisplay =
        (PFNEGLGETPLATFORMDISPLAYEXTPROC)eglGetProcAddress("eglGetPlatformDisplayEXT");
    if (!getPlatformDisplay) {
        iosFail("no eglGetPlatformDisplayEXT");
        return nullptr;
    }
    const EGLint dattr[] = { EGL_PLATFORM_ANGLE_TYPE_ANGLE, EGL_PLATFORM_ANGLE_TYPE_METAL_ANGLE, EGL_NONE };
    s_dpy = getPlatformDisplay(EGL_PLATFORM_ANGLE_ANGLE, (void *)EGL_DEFAULT_DISPLAY, dattr);
    EGLint maj = 0, min = 0;
    if (s_dpy == EGL_NO_DISPLAY || !eglInitialize(s_dpy, &maj, &min)) {
        iosFail("no Metal display");
        return nullptr;
    }
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
        return nullptr;
    }
    const EGLint xattr[] = { EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE };
    s_ctx = eglCreateContext(s_dpy, s_cfg, EGL_NO_CONTEXT, xattr);
    if (s_ctx == EGL_NO_CONTEXT) {
        iosFail("no ES 3.0 context");
        return nullptr;
    }
    s_surf = eglCreateWindowSurface(s_dpy, s_cfg, (EGLNativeWindowType)(__bridge void *)s_layer, nullptr);
    if (s_surf == EGL_NO_SURFACE) {
        iosFail("no window surface on the layer");
        return nullptr;
    }
    if (!eglMakeCurrent(s_dpy, s_surf, s_surf, s_ctx)) {
        iosFail("eglMakeCurrent");
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

extern "C" void gevrIosGLSwapWindow(SDL_Window *)
{
    if (s_dpy == EGL_NO_DISPLAY) return;
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
    }
    return 0;
}

extern "C" int gevrIosGLGetSwapInterval(void)
{
    return s_interval;
}
