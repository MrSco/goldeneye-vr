#ifndef _IN_AUDIO_H
#define _IN_AUDIO_H

#include <PR/ultratypes.h>

s32 audioInit(void);
s32 audioGetBytesBuffered(void);
s32 audioGetSamplesBuffered(void);
void audioSetNextBuffer(const s16 *buf, u32 len);
void audioEndFrame(void);
#ifdef GEVR
/* Quest/home-button: stop the SDL queue immediately (GEVR's XR pump never
 * waits on SDL's Android_PauseSem, so PauseAudioDevice must be explicit). */
void audioPause(void);
void audioResume(void);
void audioShutdown(void);
s32 audioIsPaused(void);
#endif

#endif
