#ifndef GEVR_BEAN_ROOM_STATE_H
#define GEVR_BEAN_ROOM_STATE_H
#include <stdint.h>
/* Host-only room material: select fog at draw time, without rewriting lists. */
#define G_GEVR_ROOM_STATE 0x6c
/* A fading chr's Bean draws (chrRenderProp's PropType 8 env alpha, which the
 * game's render mode carries for its own lists): w1 the alpha, 255 ends it. */
#define G_GEVR_BEAN_FADE 0x6D
/* Bean model lists under the game's node state (DAB xblaMeshApplyNodeMode,
 * xblaMeshSetSpanMode): the game sets a node's render mode for the list it
 * replaces (room shade, prop and gun tints, fades); Bean's list keeps it.
 * w0's low byte is the op. ON/OFF are put around the list by the game
 * (model.c, objecthandler.c), SAVE/APPLY/END by the list itself, so a list
 * drawn without ON keeps its own state. An APPLY's w0 bits 8+ are flags and
 * w1 is the list's own render mode (the G_GEVR_ROOM_STATE word). */
#define G_GEVR_NODE_STATE 0x68
enum { GEVR_NODE_OFF, GEVR_NODE_ON, GEVR_NODE_SAVE, GEVR_NODE_APPLY, GEVR_NODE_END };
#define GEVR_NODE_COMBINE 0x100  /* the node's combiner */
#define GEVR_NODE_SPAN 0x200     /* the node's first cycle, the list's second */
#define GEVR_NODE_CUT 0x400      /* a cut-out: prim alpha 0 (props' PropType 9 adds it) */
#define GEVR_NODE_XLU 0x800      /* translucent: spans a fading node too */
static inline uint32_t gevrBeanRoomMode(uint32_t mode, int fog)
{
    /* G_RM_FOG_SHADE_A shifts signed 3 into bit 30 in the SDK macro.
     * Use unsigned operands so the host command construction is defined. */
    const uint32_t fogMode = GBL_c1((uint32_t)G_BL_CLR_FOG, (uint32_t)G_BL_A_SHADE,
                                   (uint32_t)G_BL_CLR_IN, (uint32_t)G_BL_1MA);
    return (mode & ~0xcccc0000u) | (fog ? fogMode : G_RM_PASS);
}
#endif
