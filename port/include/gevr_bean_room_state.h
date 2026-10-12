#ifndef GEVR_BEAN_ROOM_STATE_H
#define GEVR_BEAN_ROOM_STATE_H
#include <stdint.h>
/* Host-only room material: select fog at draw time, without rewriting lists. */
#define G_GEVR_ROOM_STATE 0x6c
static inline uint32_t gevrBeanRoomMode(uint32_t mode, int fog)
{
    return (mode & ~0xcccc0000u) | (fog ? G_RM_FOG_SHADE_A : G_RM_PASS);
}
#endif
