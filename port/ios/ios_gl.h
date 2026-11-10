/*
 * iOS: SDL's GL calls answered by ANGLE (port/ios/ios_angle.mm).
 *
 * iOS has no desktop GL, and Apple's own GLES (EAGL) is deprecated, so the
 * game draws with OpenGL ES 3 through ANGLE on Metal, as rebelancap's
 * perfect-dark-ios does. SDL cannot make that context, so the iOS build
 * force-includes this header (CMakeLists.txt, GEVR_IOS) and every
 * SDL_GL_CreateContext, SDL_GL_MakeCurrent, SDL_GL_SwapWindow,
 * SDL_GL_GetProcAddress, SDL_GL_GetDrawableSize and swap-interval call in the
 * game's sources goes to the EGL versions instead. The window, the events and
 * everything else stay SDL's.
 *
 * SDL's own declarations are renamed with the calls; the signatures match.
 * port/ios's own files (GEVR_IOS_NO_GL_SHIM) and Objective-C see SDL's names.
 */
#ifndef GEVR_IOS_GL_H
#define GEVR_IOS_GL_H

#if defined(GEVR_IOS) && !defined(GEVR_IOS_NO_GL_SHIM) && !defined(__OBJC__)

typedef struct SDL_Window SDL_Window;
typedef void *SDL_GLContext;

#ifdef __cplusplus
extern "C" {
#endif
SDL_GLContext gevrIosGLCreateContext(SDL_Window *window);
int gevrIosGLMakeCurrent(SDL_Window *window, SDL_GLContext context);
void gevrIosGLSwapWindow(SDL_Window *window);
void *gevrIosGLGetProcAddress(const char *proc);
void gevrIosGLGetDrawableSize(SDL_Window *window, int *w, int *h);
int gevrIosGLSetSwapInterval(int interval);
int gevrIosGLGetSwapInterval(void);
#ifdef __cplusplus
}
#endif

#define SDL_GL_CreateContext gevrIosGLCreateContext
#define SDL_GL_MakeCurrent gevrIosGLMakeCurrent
#define SDL_GL_SwapWindow gevrIosGLSwapWindow
#define SDL_GL_GetProcAddress gevrIosGLGetProcAddress
#define SDL_GL_GetDrawableSize gevrIosGLGetDrawableSize
#define SDL_GL_SetSwapInterval gevrIosGLSetSwapInterval
#define SDL_GL_GetSwapInterval gevrIosGLGetSwapInterval

#endif
#endif /* GEVR_IOS_GL_H */
