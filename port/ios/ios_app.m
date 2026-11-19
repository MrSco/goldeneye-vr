/*
 * iOS: the app around SDL's UIKit app (port/src/main.c calls
 * SDL_UIKitRunApp).
 *
 * Two traps found by rebelancap's perfect-dark-ios (MIT,
 * port/licenses/perfect-dark-ios.txt), whose fixes these are:
 *
 * - An app linked against the iOS 27 SDK must declare a UIScene manifest, or
 *   UIKit kills it at launch. SDL 2 has no scene support: it builds its
 *   UIWindow without a scene, and with a manifest declared such a window is
 *   never composited (a black screen at full frame rate). The scene delegate
 *   below (Info.plist names it) moves every sceneless window onto the scene.
 * - SDL's HIDAPI Steam backend opens CoreBluetooth when the pads start, which
 *   iOS answers by killing the app unless it asked first. Pads come through
 *   GameController (SDL's MFi backend) instead.
 */
#import <UIKit/UIKit.h>
#import <GameController/GameController.h>

#include <TargetConditionals.h>
#include <SDL.h>

#include "ios_app.h"

@interface GevrSceneDelegate : UIResponder <UIWindowSceneDelegate>
@property (nonatomic, strong) UIWindow *window;
@end

/* The connecting or active UIWindowScene captured by GevrSceneDelegate, so
 * gevrForegroundScene() finds it reliably even before activation finishes. */
static __weak UIWindowScene *s_activeScene;

static UIWindowScene *gevrForegroundScene(void)
{
    UIWindowScene *any = nil;
    for (UIScene *sc in UIApplication.sharedApplication.connectedScenes) {
        if (![sc isKindOfClass:UIWindowScene.class]) continue;
        if (sc.activationState == UISceneActivationStateForegroundActive) return (UIWindowScene *)sc;
        if (!any) any = (UIWindowScene *)sc;
    }
    return any ?: s_activeScene;
}

void sysLogPrintf(int level, const char *fmt, ...);

/* Adds the GCEventInteraction to a view once: controller events under it go
 * to GameController (SDL) instead of becoming UI input. On visionOS they go to
 * "the view the user is gazing at" and up the responder chain, so the window
 * (the root of everything the game draws) and its root view get it as well as
 * the game's own view (gevrIosGraftWindows, gevrIosSetHostView). */
static void gevrIosClaimControllers(UIView *v)
{
    if (!v) return;
#if TARGET_OS_TV
    // Apple TV: SDL's view controller is a GCEventViewController that already
    // keeps controller events from UIKit (SDL_HINT_APPLE_TV_CONTROLLER_UI_EVENTS)
    (void)v;
#else
    if (@available(iOS 18.0, *)) {
        for (id<UIInteraction> i in v.interactions) {
            if ([i isKindOfClass:GCEventInteraction.class]) return;
        }
        GCEventInteraction *only = [[GCEventInteraction alloc] init];
        only.handledEventTypes = GCUIEventTypeGamepad;   // exclusively to GameController (receivesEventsInView NO)
        [v addInteraction:only];
        sysLogPrintf(0, "ios: game controllers to the game: GCEventInteraction on %s (in a window %d, on a scene %d)",
                     NSStringFromClass(v.class).UTF8String, v.window != nil, v.window.windowScene != nil);
    }
#endif
}

/* The game's view (ios_angle.mm): a sceneless window is in no list UIKit
 * keeps, so the view is the one way to it. */
static UIView *s_hostView;
void gevrIosSetHostView(void *view)
{
    UIView *prev = s_hostView;
    s_hostView = (__bridge UIView *)view;
    if (!s_hostView) {
        if (prev.window) {
            UIWindowScene *sc = prev.window.windowScene ?: s_activeScene;
            if ([sc.delegate isKindOfClass:GevrSceneDelegate.class]) {
                GevrSceneDelegate *del = (GevrSceneDelegate *)sc.delegate;
                if (del.window == prev.window) {
                    del.window = nil;
                }
            }
        }
        return;
    }
    /*
     * Game controllers to the game alone (SDL reads them through
     * GameController). Without this, visionOS (the iPad app on Vision Pro,
     * 2026-11-14) also turns a pad into UI input for the view under the
     * viewer's gaze: its sticks arrived as arrow keys (the stick binds: a full
     * turn from a nudge of the walking stick) and A as Return (Start, and a
     * flood of new lines in a text box). Apple's answer is GCEventInteraction.
     */
    UIView *v = s_hostView;
    dispatch_async(dispatch_get_main_queue(), ^{ gevrIosClaimControllers(v); });
    // the controllers there already (the connect note below runs before the log opens)
    for (GCController *c in GCController.controllers) {
        sysLogPrintf(0, "ios: controller: %s (%s), extended gamepad %d, micro gamepad %d",
                     c.vendorName.UTF8String ?: "?", c.productCategory.UTF8String ?: "?",
                     c.extendedGamepad != nil, c.microGamepad != nil);
    }
}

/* Moves SDL's sceneless windows onto the app's scene; returns how many. */
int gevrIosGraftWindows(void)
{
    UIWindowScene *scene = gevrForegroundScene();
    if (!scene) return 0;
    GevrSceneDelegate *sceneDel = [scene.delegate isKindOfClass:GevrSceneDelegate.class]
        ? (GevrSceneDelegate *)scene.delegate
        : nil;
    int moved = 0;
    NSMutableArray<UIWindow *> *windows = [NSMutableArray array];
    if (s_hostView.window) [windows addObject:s_hostView.window];
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
    for (UIWindow *w in UIApplication.sharedApplication.windows) {
        if (![windows containsObject:w]) [windows addObject:w];
    }
#pragma clang diagnostic pop
    const CGRect sceneBounds = scene.coordinateSpace.bounds;
    for (UIWindow *w in windows) {
        gevrIosClaimControllers(w);
        gevrIosClaimControllers(w.rootViewController.view);
        if (w.windowScene) {
            if (sceneDel && !sceneDel.window && w.windowScene == scene) {
                sceneDel.window = w;
            }
            continue;
        }
        w.windowScene = scene;
        if (!CGRectIsEmpty(sceneBounds) && !CGRectEqualToRect(w.frame, sceneBounds)) {
            w.frame = sceneBounds;
        }
        if (sceneDel && (!sceneDel.window || w == s_hostView.window)) {
            sceneDel.window = w;
        }
        [w makeKeyAndVisible];
        moved++;
        NSLog(@"GoldenEyeVR: moved SDL's window %@ onto the scene", NSStringFromCGRect(w.bounds));
    }
    return moved;
}

@implementation GevrSceneDelegate
- (void)scene:(UIScene *)scene willConnectToSession:(UISceneSession *)session
      options:(UISceneConnectionOptions *)options
{
    (void)session;
    (void)options;
    if ([scene isKindOfClass:UIWindowScene.class]) {
        s_activeScene = (UIWindowScene *)scene;
    }
    gevrIosGraftWindows();
}

- (void)sceneDidBecomeActive:(UIScene *)scene
{
    if ([scene isKindOfClass:UIWindowScene.class]) {
        s_activeScene = (UIWindowScene *)scene;
    }
    gevrIosGraftWindows();
}

- (void)sceneDidDisconnect:(UIScene *)scene
{
    if (s_activeScene == scene) {
        s_activeScene = nil;
    }
    if (self.window.windowScene == scene) {
        self.window = nil;
    }
}
@end

/*
 * Fatal error presentation for iOS/tvOS (port/src/system.c sysFatalError).
 *
 * SDL_ShowSimpleMessageBox(..., NULL) creates a sceneless UIWindow that UIKit
 * never composites under UIApplicationSceneManifest, hanging forever in
 * UIKit_WaitUntilMessageBoxClosed on an empty launch screen. Attach an alert
 * window directly to the active UIWindowScene instead, and bail out immediately
 * if no scene is connected or if called off the main thread.
 */
void gevrIosFatalError(const char *msg)
{
    @autoreleasepool {
        if (![NSThread isMainThread]) {
            NSLog(@"GoldenEyeVR: fatal error (off main thread): %s", msg ? msg : "");
            return;
        }

        UIWindowScene *scene = gevrForegroundScene();
        for (int i = 0; !scene && i < 20; i++) {
            [[NSRunLoop currentRunLoop] runMode:NSDefaultRunLoopMode
                                     beforeDate:[NSDate dateWithTimeIntervalSinceNow:0.01]];
            scene = gevrForegroundScene();
        }
        if (!scene) {
            NSLog(@"GoldenEyeVR: fatal error (no UIWindowScene): %s", msg ? msg : "");
            return;
        }

#if TARGET_OS_TV
        // Let the Siri Remote / game pad drive the alert (Select or Back closes it).
        SDL_SetHintWithPriority(SDL_HINT_APPLE_TV_CONTROLLER_UI_EVENTS, "1", SDL_HINT_OVERRIDE);
#endif

        NSString *text = msg ? [NSString stringWithUTF8String:msg] : nil;
        if (!text && msg) {
            text = [NSString stringWithCString:msg encoding:NSISOLatin1StringEncoding];
        }
        if (!text || text.length == 0) {
            text = @"An unrecoverable error occurred.";
        }

        UIWindow *alertWindow = [[UIWindow alloc] initWithWindowScene:scene];
        const CGRect sceneBounds = scene.coordinateSpace.bounds;
        if (!CGRectIsEmpty(sceneBounds)) {
            alertWindow.frame = sceneBounds;
        }
        alertWindow.windowLevel = UIWindowLevelAlert + 1;
        UIViewController *host = [[UIViewController alloc] init];
        host.view.backgroundColor = [UIColor blackColor];
        alertWindow.rootViewController = host;
        if ([scene.delegate isKindOfClass:GevrSceneDelegate.class]) {
            ((GevrSceneDelegate *)scene.delegate).window = alertWindow;
        }
        [alertWindow makeKeyAndVisible];
        [alertWindow layoutIfNeeded];
        [[NSRunLoop currentRunLoop] runMode:NSDefaultRunLoopMode
                                 beforeDate:[NSDate dateWithTimeIntervalSinceNow:0.05]];

        UIAlertController *alert = [UIAlertController alertControllerWithTitle:@"GoldenEye VR - Fatal Error"
                                                                       message:text
                                                                preferredStyle:UIAlertControllerStyleAlert];
        __block BOOL dismissed = NO;
        __block BOOL presented = NO;
        UIAlertAction *closeAction = [UIAlertAction actionWithTitle:@"Close"
                                                              style:UIAlertActionStyleCancel
                                                            handler:^(UIAlertAction *action) {
            (void)action;
            dismissed = YES;
        }];
        [alert addAction:closeAction];
        alert.preferredAction = closeAction;

        [host presentViewController:alert animated:YES completion:^{
            presented = YES;
        }];

        NSDate *presentDeadline = [NSDate dateWithTimeIntervalSinceNow:2.0];
        while (!dismissed) {
            [[NSRunLoop currentRunLoop] runMode:NSDefaultRunLoopMode
                                     beforeDate:[NSDate dateWithTimeIntervalSinceNow:0.05]];
            if (![UIApplication.sharedApplication.connectedScenes containsObject:scene] && s_activeScene != scene) {
                break;
            }
            if (presented) {
                if (host.presentedViewController == nil) {
                    break;
                }
            } else if (host.presentedViewController == alert) {
                presented = YES;
            } else if ([presentDeadline timeIntervalSinceNow] <= 0.0) {
                NSLog(@"GoldenEyeVR: fatal error alert failed to present: %@", text);
                break;
            }
        }

        alertWindow.hidden = YES;
    }
}

/* Before SDL starts (port/src/main.c) */
void gevrIosPrepare(void)
{
    SDL_SetHintWithPriority(SDL_HINT_JOYSTICK_HIDAPI, "0", SDL_HINT_OVERRIDE);
    SDL_SetHintWithPriority(SDL_HINT_JOYSTICK_HIDAPI_STEAM, "0", SDL_HINT_OVERRIDE);
    // the keyboard (a lobby name, a friend code) goes away on Return
    SDL_SetHintWithPriority(SDL_HINT_RETURN_KEY_HIDES_IME, "1", SDL_HINT_OVERRIDE);
    // landscape only, so SDL never lays its view out portrait (Info.plist agrees)
    SDL_SetHintWithPriority(SDL_HINT_ORIENTATIONS, "LandscapeLeft LandscapeRight", SDL_HINT_OVERRIDE);
#if TARGET_OS_TV
    // Apple TV: the Siri Remote is a pad, read through GameController (its
    // touch surface or ring, the click as A, Back as B, Play/Pause as X), and
    // it drives the launcher. Every controller's presses are the game's alone,
    // so Back never leaves the app (the TV button still does). As keys
    // instead, its presses never reached SDL's view (simulator, 2026-11-18:
    // the system's Settings took the same presses). tvos_upload.mm hands
    // controllers to UIKit while its dialog is up.
    SDL_SetHintWithPriority(SDL_HINT_TV_REMOTE_AS_JOYSTICK, "1", SDL_HINT_OVERRIDE);
    SDL_SetHintWithPriority(SDL_HINT_APPLE_TV_CONTROLLER_UI_EVENTS, "0", SDL_HINT_OVERRIDE);
#endif
    // which controllers the system hands the app (the iPad app on Vision Pro)
    [NSNotificationCenter.defaultCenter addObserverForName:GCControllerDidConnectNotification object:nil queue:nil
                                                usingBlock:^(NSNotification *n) {
        GCController *c = n.object;
        sysLogPrintf(0, "ios: controller connected: %s (%s), extended gamepad %d", c.vendorName.UTF8String ?: "?",
                     c.productCategory.UTF8String ?: "?", c.extendedGamepad != nil);
    }];
}

/* A light tap from the Taptic Engine (port/flat/window/flat_touch.cpp's
 * Touch vibration): SDL lists no haptic device for the phone itself. */
void gevrIosHaptic(void)
{
#if TARGET_OS_TV
    return;   // no touch controls and no Taptic Engine on a TV
#else
    static UIImpactFeedbackGenerator *tap;
    if (!tap) {
        tap = [[UIImpactFeedbackGenerator alloc] initWithStyle:UIImpactFeedbackStyleLight];
    }
    [tap impactOccurred];
    [tap prepare];   // keeps the engine warm for the next press
#endif
}

/*
 * Every touch on the game's view, held, so the touch controls can ask its
 * phase: a finger whose end never reached SDL (user, iPhone, 2026-11-09: the
 * stick and AIM stuck held, ten lifts lost in a fight) is let go of within a
 * poll. rebelancap's perfect-dark-ios found the same on device (its D-034
 * stuck-stick watchdog, PDTouchOverlay.m): a touch's phase says ended or
 * cancelled though its callback never came, and a UITouch must be held
 * strongly to be asked. SDL's finger ids are the UITouch addresses
 * (SDL_uikitview.m). A gesture recognizer that never recognises sees every
 * touch first and changes nothing for the view.
 */
#if !TARGET_OS_TV
#import <UIKit/UIGestureRecognizerSubclass.h>

static NSMutableDictionary<NSNumber *, UITouch *> *s_touches;   /* by address */

static void gevrHoldTouches(NSSet<UITouch *> *touches)
{
    if (!s_touches) s_touches = [NSMutableDictionary dictionary];
    for (UITouch *t in touches) s_touches[@((uintptr_t)(__bridge void *)t)] = t;
}

static void gevrForgetEnded(void)
{
    NSMutableArray<NSNumber *> *gone = nil;
    for (NSNumber *k in s_touches) {
        const UITouchPhase p = s_touches[k].phase;
        if (p == UITouchPhaseEnded || p == UITouchPhaseCancelled) {
            if (!gone) gone = [NSMutableArray array];
            [gone addObject:k];
        }
    }
    if (gone) [s_touches removeObjectsForKeys:gone];
}

@interface GevrTouchWatcher : UIGestureRecognizer
@end

@implementation GevrTouchWatcher
- (void)touchesBegan:(NSSet<UITouch *> *)touches withEvent:(UIEvent *)event
{
    (void)event;
    if (s_touches.count > 32) gevrForgetEnded();   // ended ones SDL never asked about
    gevrHoldTouches(touches);
}
@end
#endif /* !TARGET_OS_TV */

/* 1: the touch is down; 0: its phase says the finger left; -1: not one we hold */
int gevrIosTouchIsLive(long long fingerId)
{
#if TARGET_OS_TV
    (void)fingerId;
    return -1;
#else
    NSNumber *k = @((uintptr_t)fingerId);
    UITouch *t = s_touches[k];
    if (!t) return -1;
    if (t.phase == UITouchPhaseEnded || t.phase == UITouchPhaseCancelled) {
        [s_touches removeObjectForKey:k];
        return 0;
    }
    return 1;
#endif
}

void gevrIosWatchTouches(void *view)
{
#if !TARGET_OS_TV
    UIView *v = (__bridge UIView *)view;
    GevrTouchWatcher *w = [[GevrTouchWatcher alloc] initWithTarget:nil action:nil];
    w.cancelsTouchesInView = NO;
    w.delaysTouchesBegan = NO;
    w.delaysTouchesEnded = NO;
    [v addGestureRecognizer:w];
#else
    (void)view;
#endif
}

/*
 * Online voice from a phone (port/src/net/net_voice.c): SDL opens the mic in
 * the audio session's default mode, which records the phone's own speaker
 * too - the game and the other players' voices - and the Quest heard
 * "absolute muddled garbage" (user, 2026-11-09). The voice chat mode turns on
 * Apple's echo cancellation and noise suppression on the built-in mic. SDL's
 * category and options stay as they are, so it leaves the mode alone, and
 * puts its own back (playback, default mode) when the mic closes.
 */
#import <AVFoundation/AVFoundation.h>

void gevrIosVoiceChatMode(void)
{
    AVAudioSession *s = AVAudioSession.sharedInstance;
    NSError *err = nil;
    if (![s setCategory:s.category mode:AVAudioSessionModeVoiceChat options:s.categoryOptions error:&err]) {
        NSLog(@"GoldenEyeVR: voice chat mode refused: %@", err);
    }
}
