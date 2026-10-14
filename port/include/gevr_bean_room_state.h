#ifndef GEVR_BEAN_ROOM_STATE_H
#define GEVR_BEAN_ROOM_STATE_H
#include <stdint.h>
/* Host-only room material: select fog at draw time, without rewriting lists. */
#define G_GEVR_ROOM_STATE 0x6c
/* A fading chr's Bean draws (chrRenderProp's PropType 8 env alpha, which the
 * game's render mode carries for its own lists): w1 the alpha, 255 ends it. */
#define G_GEVR_BEAN_FADE 0x6D
static inline uint32_t gevrBeanRoomMode(uint32_t mode, int fog)
{
    /* G_RM_FOG_SHADE_A shifts signed 3 into bit 30 in the SDK macro.
     * Use unsigned operands so the host command construction is defined. */
    const uint32_t fogMode = GBL_c1((uint32_t)G_BL_CLR_FOG, (uint32_t)G_BL_A_SHADE,
                                   (uint32_t)G_BL_CLR_IN, (uint32_t)G_BL_1MA);
    return (mode & ~0xcccc0000u) | (fog ? fogMode : G_RM_PASS);
}
#endif
