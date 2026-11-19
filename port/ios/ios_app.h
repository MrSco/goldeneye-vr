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
void gevrIosFatalError(const char *msg);

#ifdef __cplusplus
}
#endif

#endif /* GEVR_IOS_APP_H */
