/* Scripted test input for the PC game (pc_testinput.c). */
#ifndef GEVR_PC_TESTINPUT_H
#define GEVR_PC_TESTINPUT_H

#include <PR/ultratypes.h>

#ifdef __cplusplus
extern "C" {
#endif

s32 pcTestInputActive(void);
void pcTestInputFrame(s32 *mdx, s32 *mdy, u32 *buttons, s32 *lock);
s32 pcTestInputKey(s32 scancode);
const char *pcTestInputTakeShot(void);

#ifdef __cplusplus
}
#endif

#endif
