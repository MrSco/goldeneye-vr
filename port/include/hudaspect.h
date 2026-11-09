#ifndef _HUDASPECT_H_
#define _HUDASPECT_H_

/*
 * Screen shape 16:9 (VrScreenShape): the HUD and the front end on a wide 2D
 * screen. Ported from gepc-ref's port/include/hudaspect.h (D335).
 *
 * The world is projected at the screen's aspect (bondview2.c, D334), but HUD
 * and front-end draws are laid out in the game's 4:3 canvas, which fast3d
 * stretches across the screen. Wrapped in an aspect mode, a draw keeps the
 * canvas's proportions, anchored to the left edge, the right edge or the
 * centre (fast3d gfx_adjust_x_for_aspect_ratio, the PD port's
 * G_ASPECT_*_EXT extra-geometry modes in gepc-ref's encoding,
 * port/fast3d/gfx_pc.h).
 *
 * PORT_HUD_ASPECT emits only when this frame goes to a 2D screen wider than
 * 4:3 (portNativeAspect, fast3d gfx_pc.cpp); a 4:3 screen and stereo frames
 * get exactly the display list they had before.
 */

#define GE_HUD_ASPECT_NONE     0x00u
#define GE_HUD_ASPECT_LEFT     0x10u   /* G_ASPECT_LEFT_EXT   (port/fast3d/gfx_pc.h) */
#define GE_HUD_ASPECT_RIGHT    0x20u   /* G_ASPECT_RIGHT_EXT  */
#define GE_HUD_ASPECT_CENTER   0x30u   /* G_ASPECT_CENTER_EXT */
#define GE_HUD_ASPECT_MASK     0x70u   /* G_ASPECT_MODE_EXT   */
#define GE_EXTRAGEOMETRYMODE_EXT 0x71  /* G_EXTRAGEOMETRYMODE_EXT */

/* fast3d does extra_mode = (extra_mode & ~clear) | set with clear =
 * ~(w0 & 0xFFFFFF): ~MASK in the low 24 bits clears the aspect bits. */
#define gSPHudAspectEXT(pkt, mode)                                                       \
    do {                                                                                 \
        Gfx *_g = (Gfx *)(pkt);                                                          \
        _g->words.w0 = ((u32)GE_EXTRAGEOMETRYMODE_EXT << 24) | ((~GE_HUD_ASPECT_MASK) & 0xFFFFFFu); \
        _g->words.w1 = (mode);                                                           \
    } while (0)

extern float portNativeAspect(void);   /* port/fast3d/gfx_pc.cpp */

/* 1.3334 so a 4:3 screen emits nothing */
#define PORT_HUD_ASPECT(gdl, mode) \
    do { if (portNativeAspect() > 1.3334f) { gSPHudAspectEXT((gdl)++, (mode)); } } while (0)

#endif
