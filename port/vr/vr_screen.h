#pragma once
/*
 * Virtual screen: the game's frame is rendered off-screen at its own aspect
 * and shown on a world-locked quad in front of the player, like a cinema
 * screen. Head tracking comes from the compositor placing the quad in play
 * space; the game itself keeps its N64 camera and projection.
 *
 * vr_screen_target_*  (vr_screen.cpp)  the off-screen multiview render target
 * vr_screen_present   (vr_openxr.cpp)  hands the finished frame to a quad layer
 * vr_screen_recenter  (vr_openxr.cpp)  puts the screen in front of the head again
 */
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

bool vr_screen_target_begin(int w, int h);
void vr_screen_target_end(void);
unsigned int vr_screen_target_tex(void);

bool vr_screen_present(unsigned int srcArrayTex, int w, int h);
void vr_screen_recenter(void);

/* Tunables, metres and degrees. */
extern float VrScreenDistance;
extern float VrScreenFov;

#ifdef __cplusplus
}
#endif
