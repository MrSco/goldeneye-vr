/*
 * Vision Pro, native (docs/visionos-native.md): what port/visionos's files
 * share.
 *
 * The game is the PC VR build (port/vr, OpenXR) with OpenGL ES through ANGLE
 * on Metal. Apple has no OpenXR, so these files are an OpenXR runtime of the
 * game's own, the subset port/vr calls, over Compositor Services (the frame
 * clock, the drawables), ARKit (the head) and GameController (the
 * controllers). There is no loader: vos_xr.mm defines xrCreateInstance and the
 * rest, and the game links to them.
 *
 *   vos_app.mm        the app's start: the data folder, the game's thread,
 *                     ARKit's world tracking (the head at a time)
 *   vos_egl.mm        ANGLE's EGL context (no window), Metal textures as GL
 *                     textures, the GL -> Metal fence
 *   vos_window.cpp    fast3d's window manager, which has no window here
 *   vos_xr.mm         the OpenXR runtime: instance, session, spaces,
 *                     swapchains, the frame loop and the compositing
 *   vos_input.mm      the OpenXR actions, answered from GameController
 *   VosApp.swift      the SwiftUI app: one full-immersion space
 */
#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <simd/simd.h>

#ifdef __cplusplus
extern "C" {
#endif

/* the game's log (sysLogPrintf) and Apple's (NSLog), one line */
void vosLog(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

/* ---- vos_app.mm ---- */

/* VosApp.swift's CompositorLayer: the layer renderer is ready (unretained,
 * SwiftUI owns it). The first call starts ARKit and the game. */
void gevrVosLayerReady(void *layerRenderer);
/* the layer renderer (cp_layer_renderer_t), NULL before the space opened */
void *vosLayerRenderer(void);
/* (vos_xr.mm) The head (ARKit's device anchor, origin from device) at a time
 * in seconds on CACurrentMediaTime's clock. 1 tracked, 0 not (out is then the
 * last good one, or 1.6 m up at the origin). */
int vosHeadAt(double seconds, simd_float4x4 *out);
/* (vos_xr.mm) a compositor frame presented black, for a wait on the game's
 * thread (the file picker): the space must keep getting frames */
void vosKeepAlive(void);
/* (vos_app.mm) the immersive space closed: save and quit */
void vosSpaceClosed(void);
/* seconds on CACurrentMediaTime's clock (the compositor's and ARKit's) */
double vosNow(void);

/* ---- vos_egl.mm ---- */

/* ANGLE's MTLDevice (id<MTLDevice>, unretained), once the context exists */
void *vosMetalDevice(void);
/* a Metal texture (id<MTLTexture>) as a GL texture name of the current
 * context (GL_TEXTURE_2D); 0 on failure. The EGL image is kept with it. */
unsigned vosGLTextureFromMetal(void *mtlTexture);
void vosGLTextureRelease(unsigned tex);
/* glFlush, and the (MTLSharedEvent, value) ANGLE signals once the GL work so
 * far has retired on the GPU. *event is +1 (CFBridgingRetain'd): release it.
 * 0 if the extension is missing (then glFinish has run instead). */
int vosGLFence(void **event, uint64_t *value);

/* ---- vos_input.mm ---- */

/* The controllers' state, as Touch reports it (vos_input.mm fills it at
 * xrSyncActions; vos_xr.mm's action spaces read the poses). */
typedef struct VosHand {
    bool active;              /* a controller in this hand */
    bool poseValid;           /* tracked, or placed beside the head */
    simd_float4x4 grip;       /* origin from grip (ARKit's origin, metres) */
    float stickX, stickY;
    float trigger, squeeze;
    bool a, b, menu, stickClick, triggerTouch;
} VosHand;
void vosInputSync(double displaySeconds, const simd_float4x4 *head);
const VosHand *vosInputHand(int hand);   /* 0 left, 1 right */
void vosInputHaptic(int hand, float amplitude, double seconds);

#ifdef __cplusplus
}
#endif
