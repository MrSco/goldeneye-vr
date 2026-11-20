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

#ifdef __cplusplus
}
#endif

#endif /* GEVR_IOS_APP_H */
