/*
 * Vision Pro: the game's OpenGL ES 3 context, from ANGLE on Metal, with no
 * window (docs/visionos-native.md).
 *
 * The game's SDL_GL_* calls land here (port/ios/ios_gl.h, as on iOS). There
 * is no CAMetalLayer to draw on: every picture the headset shows goes through
 * an OpenXR swapchain (vos_xr.mm), so the context has a small pbuffer for a
 * default framebuffer nothing looks at, and the swap does nothing.
 *
 * Metal textures become GL textures the way rebelancap's Project-Midas-ios
 * (MIT, port/licenses/project-midas-ios.txt) found works, app/vision3d/
 * GEEyeTargets.mm: eglCreateImageKHR(EGL_METAL_TEXTURE_ANGLE) and
 * glEGLImageTargetTexture2DOES, on ANGLE's own MTLDevice, shared storage. And
 * GL's work reaches Metal's queue through the MTLSharedEvent ANGLE signals for
 * an EGL_SYNC_METAL_SHARED_EVENT_ANGLE sync (its geVisionEyePublish).
 */
#import <Metal/Metal.h>
#import <Foundation/Foundation.h>

#define EGL_EGLEXT_PROTOTYPES
#define GL_GLEXT_PROTOTYPES
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <EGL/eglext_angle.h>
#include <GLES3/gl3.h>
#include <GLES2/gl2ext.h>

#include <SDL.h>
#include <map>
#include <stdarg.h>

#include "vos.h"

static EGLDisplay s_dpy = EGL_NO_DISPLAY;
static EGLConfig s_cfg;
static EGLContext s_ctx = EGL_NO_CONTEXT;
static EGLSurface s_surf = EGL_NO_SURFACE;
static id<MTLDevice> s_dev;
static int s_interval;

static bool vosEglFail(const char *what)
{
    vosLog("visionos: ANGLE: %s (EGL 0x%x)", what, eglGetError());
    SDL_SetError("ANGLE: %s", what);
    return false;
}

extern "C" SDL_GLContext gevrIosGLCreateContext(SDL_Window *)
{
    if (s_ctx != EGL_NO_CONTEXT) {
        return (SDL_GLContext)s_ctx;   // one context: the game's thread's
    }
    const EGLint dattr[] = { EGL_PLATFORM_ANGLE_TYPE_ANGLE, EGL_PLATFORM_ANGLE_TYPE_METAL_ANGLE, EGL_NONE };
    s_dpy = eglGetPlatformDisplayEXT(EGL_PLATFORM_ANGLE_ANGLE, (void *)EGL_DEFAULT_DISPLAY, dattr);
    EGLint maj = 0, min = 0;
    if (s_dpy == EGL_NO_DISPLAY || !eglInitialize(s_dpy, &maj, &min)) {
        vosEglFail("no Metal display");
        return nullptr;
    }
    // depth 24 + stencil 8, as every other build's window has
    const EGLint cattr[] = {
        EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT,
        EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8,
        EGL_DEPTH_SIZE, 24, EGL_STENCIL_SIZE, 8,
        EGL_NONE
    };
    EGLint n = 0;
    if (!eglChooseConfig(s_dpy, cattr, &s_cfg, 1, &n) || n < 1) {
        vosEglFail("no ES 3 pbuffer config with depth 24 and stencil 8");
        return nullptr;
    }
    const EGLint xattr[] = { EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE };
    s_ctx = eglCreateContext(s_dpy, s_cfg, EGL_NO_CONTEXT, xattr);
    if (s_ctx == EGL_NO_CONTEXT) {
        vosEglFail("no ES 3.0 context");
        return nullptr;
    }
    const EGLint sattr[] = { EGL_WIDTH, 64, EGL_HEIGHT, 64, EGL_NONE };
    s_surf = eglCreatePbufferSurface(s_dpy, s_cfg, sattr);
    if (s_surf == EGL_NO_SURFACE) {
        vosEglFail("no pbuffer");
        return nullptr;
    }
    if (!eglMakeCurrent(s_dpy, s_surf, s_surf, s_ctx)) {
        vosEglFail("eglMakeCurrent");
        return nullptr;
    }
    // ANGLE's own device: a Metal texture made on another is refused by its
    // EGL images (Midas's devMatch). On visionOS it is the system's, which is
    // the compositor's too.
    EGLAttrib devAttr = 0, mtl = 0;
    if (eglQueryDisplayAttribEXT(s_dpy, EGL_DEVICE_EXT, &devAttr) && devAttr &&
        eglQueryDeviceAttribEXT((EGLDeviceEXT)devAttr, EGL_METAL_DEVICE_ANGLE, &mtl) && mtl) {
        s_dev = (__bridge id<MTLDevice>)(void *)mtl;
    } else {
        s_dev = MTLCreateSystemDefaultDevice();
        vosLog("visionos: ANGLE named no MTLDevice; the system's (%s)", s_dev.name.UTF8String);
    }
    vosLog("visionos: ANGLE EGL %d.%d, %s, %s, device %s", maj, min, (const char *)glGetString(GL_RENDERER),
           (const char *)glGetString(GL_VERSION), s_dev.name.UTF8String);
    return (SDL_GLContext)s_ctx;
}

extern "C" int gevrIosGLMakeCurrent(SDL_Window *, SDL_GLContext context)
{
    if (s_dpy == EGL_NO_DISPLAY) return -1;
    const bool ok = context ? eglMakeCurrent(s_dpy, s_surf, s_surf, (EGLContext)context)
                            : eglMakeCurrent(s_dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    return ok ? 0 : SDL_SetError("eglMakeCurrent: 0x%x", eglGetError());
}

/* the headset's frames are OpenXR's (xrEndFrame); the pbuffer is never shown */
extern "C" void gevrIosGLSwapWindow(SDL_Window *) {}

extern "C" void *gevrIosGLGetProcAddress(const char *proc)
{
    return (void *)eglGetProcAddress(proc);
}

extern "C" void gevrIosGLGetDrawableSize(SDL_Window *, int *w, int *h)
{
    if (w) *w = 64;
    if (h) *h = 64;
}

extern "C" int gevrIosGLSetSwapInterval(int interval)
{
    s_interval = interval;
    return 0;
}

extern "C" int gevrIosGLGetSwapInterval(void)
{
    return s_interval;
}

extern "C" void *vosMetalDevice(void)
{
    return (__bridge void *)s_dev;
}

struct VosImported {
    EGLImageKHR img;
    void *mtl;   // CFBridgingRetain'd: the texture lives as long as its GL name
};
static std::map<unsigned, VosImported> s_imported;

extern "C" unsigned vosGLTextureFromMetal(void *mtlTexture)
{
    id<MTLTexture> t = (__bridge id<MTLTexture>)mtlTexture;
    if (!t || s_dpy == EGL_NO_DISPLAY) return 0;
    EGLImageKHR img = eglCreateImageKHR(s_dpy, EGL_NO_CONTEXT, EGL_METAL_TEXTURE_ANGLE,
                                        (EGLClientBuffer)mtlTexture, nullptr);
    if (img == EGL_NO_IMAGE_KHR) {
        vosLog("visionos: eglCreateImageKHR(EGL_METAL_TEXTURE_ANGLE) %lux%lu format %lu failed 0x%x",
               (unsigned long)t.width, (unsigned long)t.height, (unsigned long)t.pixelFormat, eglGetError());
        return 0;
    }
    // the game's own earlier errors would read as this import's
    while (glGetError() != GL_NO_ERROR) {}
    GLint prev = 0;
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &prev);
    GLuint tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glEGLImageTargetTexture2DOES(GL_TEXTURE_2D, (GLeglImageOES)img);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, (GLuint)prev);
    const GLenum err = glGetError();
    if (err != GL_NO_ERROR) {
        vosLog("visionos: glEGLImageTargetTexture2DOES: GL error 0x%x", err);
        glDeleteTextures(1, &tex);
        eglDestroyImageKHR(s_dpy, img);
        return 0;
    }
    s_imported[tex] = { img, (void *)CFBridgingRetain(t) };
    return tex;
}

extern "C" void vosGLTextureRelease(unsigned tex)
{
    auto it = s_imported.find(tex);
    if (it == s_imported.end()) return;
    GLuint name = tex;
    glDeleteTextures(1, &name);
    if (s_dpy != EGL_NO_DISPLAY) eglDestroyImageKHR(s_dpy, it->second.img);
    CFRelease(it->second.mtl);
    s_imported.erase(it);
}

extern "C" int vosGLFence(void **event, uint64_t *value)
{
    *event = nullptr;
    *value = 0;
    EGLSync sync = s_dpy != EGL_NO_DISPLAY ? eglCreateSync(s_dpy, EGL_SYNC_METAL_SHARED_EVENT_ANGLE, nullptr)
                                           : EGL_NO_SYNC;
    if (sync == EGL_NO_SYNC) {
        static bool said;
        if (!said) {
            said = true;
            vosLog("visionos: no EGL_SYNC_METAL_SHARED_EVENT_ANGLE (0x%x): glFinish every frame", eglGetError());
        }
        glFinish();
        return 0;
    }
    // Midas's order: the sync is encoded into the command buffer that carries
    // the frame's draws, so flush between making it and reading it
    glFlush();
    void *evt = eglCopyMetalSharedEventANGLE(s_dpy, sync);
    EGLAttrib lo = 0, hi = 0;
    eglGetSyncAttrib(s_dpy, sync, EGL_SYNC_METAL_SHARED_EVENT_SIGNAL_VALUE_LO_ANGLE, &lo);
    eglGetSyncAttrib(s_dpy, sync, EGL_SYNC_METAL_SHARED_EVENT_SIGNAL_VALUE_HI_ANGLE, &hi);
    eglDestroySync(s_dpy, sync);
    if (!evt) {
        glFinish();
        return 0;
    }
    *event = evt;   // +1 from the copy
    *value = ((uint64_t)(uint32_t)hi << 32) | (uint32_t)lo;
    return 1;
}
