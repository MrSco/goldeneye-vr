#ifndef GEVR_NET_VOICE_H
#define GEVR_NET_VOICE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void netVoicePermissionResult(int granted);
int netVoiceHasPermission(void);
int netVoiceCaptureReady(void);
int netVoiceCaptureFailed(void);
int netVoiceIsMuted(void);
void netVoiceSetMuted(int muted);
void netVoiceToggleMuted(void);
void netVoicePause(void);
void netVoiceResume(void);
void netVoiceReset(void);
void netVoiceForgetSlot(uint8_t slot);
void netVoiceTick(void);
void netVoiceReceive(uint8_t slot, uint32_t sequence, const uint8_t *packet, uint16_t size);
void netVoiceMix(int16_t *stereo, size_t frames);

#ifdef __cplusplus
}
#endif
#endif
