#ifndef GEVR_VRPAD_H
#define GEVR_VRPAD_H

/*
 * Player 1's game pad in the headset, answered as a Touch pair
 * (port/vr/vr_input.cpp gevrPadAsTouch, from port/src/input.c
 * inputPadVrState), as the Vision Pro build answers one
 * (port/visionos/vos_input.mm).
 */
#define INPUT_VRPAD_A     0x001
#define INPUT_VRPAD_B     0x002
#define INPUT_VRPAD_X     0x004
#define INPUT_VRPAD_Y     0x008
#define INPUT_VRPAD_LB    0x010
#define INPUT_VRPAD_RB    0x020
#define INPUT_VRPAD_START 0x040
#define INPUT_VRPAD_BACK  0x080
#define INPUT_VRPAD_LS    0x100
#define INPUT_VRPAD_RS    0x200

#ifdef __cplusplus
extern "C" {
#endif

/* sticks: the left's x and y, the right's x and y (up positive); triggers:
 * left, right; buttons: INPUT_VRPAD_*. 0 with no pad. */
int inputPadVrState(float sticks[4], float triggers[2], unsigned int *buttons);

#ifdef __cplusplus
}
#endif

#endif
