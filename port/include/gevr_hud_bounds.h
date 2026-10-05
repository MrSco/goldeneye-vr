#ifndef GEVR_HUD_BOUNDS_H
#define GEVR_HUD_BOUNDS_H
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Asynchronous alpha bounds of the right HUD capture. No CPU pixel read or
 * map until a zero-timeout fence poll confirms completion. */
void gfx_vr_hud_bounds_update(unsigned framebuffer, int width, int height, int aim);
bool gfx_vr_hud_bounds_box(float out[4]);
void gfx_vr_hud_bounds_reset(void);
#ifdef __cplusplus
}
#endif
#endif
