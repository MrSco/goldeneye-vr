/*
 * iOS / tvOS app shell helpers (port/ios/ios_app.m).
 */
#ifndef GEVR_IOS_APP_H
#define GEVR_IOS_APP_H

#ifdef __cplusplus
extern "C" {
#endif

void gevrIosPrepare(void);
void gevrIosSetHostView(void *view);
int gevrIosGraftWindows(void);
void gevrIosWatchTouches(void *view);
int gevrIosTouchIsLive(long long fingerId);
void gevrIosHaptic(void);
void gevrIosVoiceChatMode(void);
int gevrIosMicAvailable(void);   /* Apple TV: a microphone is connected */
void gevrIosFatalError(const char *msg);
void gevrTvRemoteLogPoll(void);   /* Apple TV: the Siri Remote's raw input to the log (#268) */
int gevrTvRemoteMenu(int *step, int *select);   /* Apple TV: a 2nd generation remote's clicks */
int gevrTvRemoteHeld(int *dx, int *dy, int *centre);   /* the same remote's ring and centre, held */
/* A DualSense trigger's feel (input.c inputDualSenseFeel): mode 0 off, 1 a
 * weapon's bump from start to end, 2 a push from start on, 3 a slope from
 * start to end; forces 0 to 1 */
void gevrIosDualSenseFeel(int right, int mode, float start, float end, float force0, float force1);
void gevrIosDualSenseBuzz(int right, float start, float amp, float freq);   /* a DualSense trigger's buzz from start on */

#ifdef __cplusplus
}
#endif

#endif /* GEVR_IOS_APP_H */
