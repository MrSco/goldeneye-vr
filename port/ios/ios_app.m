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

#include <SDL.h>

static UIWindowScene *gevrForegroundScene(void)
{
    UIWindowScene *any = nil;
    for (UIScene *sc in UIApplication.sharedApplication.connectedScenes) {
        if (![sc isKindOfClass:UIWindowScene.class]) continue;
        if (sc.activationState == UISceneActivationStateForegroundActive) return (UIWindowScene *)sc;
        if (!any) any = (UIWindowScene *)sc;
    }
    return any;
}

/* The game's view (ios_angle.mm): a sceneless window is in no list UIKit
 * keeps, so the view is the one way to it. */
static UIView *s_hostView;
void gevrIosSetHostView(void *view)
{
    s_hostView = (__bridge UIView *)view;
}

/* Moves SDL's sceneless windows onto the app's scene; returns how many. */
int gevrIosGraftWindows(void)
{
    UIWindowScene *scene = gevrForegroundScene();
    if (!scene) return 0;
    int moved = 0;
    NSMutableArray<UIWindow *> *windows = [NSMutableArray array];
    if (s_hostView.window) [windows addObject:s_hostView.window];
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
    for (UIWindow *w in UIApplication.sharedApplication.windows) {
        if (![windows containsObject:w]) [windows addObject:w];
    }
#pragma clang diagnostic pop
    for (UIWindow *w in windows) {
        if (w.windowScene) continue;
        w.windowScene = scene;
        [w makeKeyAndVisible];
        moved++;
        NSLog(@"GoldenEyeVR: moved SDL's window %@ onto the scene", NSStringFromCGRect(w.bounds));
    }
    return moved;
}

@interface GevrSceneDelegate : UIResponder <UIWindowSceneDelegate>
@property (nonatomic, strong) UIWindow *window;
@end

@implementation GevrSceneDelegate
- (void)scene:(UIScene *)scene willConnectToSession:(UISceneSession *)session
      options:(UISceneConnectionOptions *)options
{
    gevrIosGraftWindows();
}

- (void)sceneDidBecomeActive:(UIScene *)scene
{
    gevrIosGraftWindows();
}
@end

/* Before SDL starts (port/src/main.c) */
void gevrIosPrepare(void)
{
    SDL_SetHintWithPriority(SDL_HINT_JOYSTICK_HIDAPI, "0", SDL_HINT_OVERRIDE);
    SDL_SetHintWithPriority(SDL_HINT_JOYSTICK_HIDAPI_STEAM, "0", SDL_HINT_OVERRIDE);
    // the keyboard (a lobby name, a friend code) goes away on Return
    SDL_SetHintWithPriority(SDL_HINT_RETURN_KEY_HIDES_IME, "1", SDL_HINT_OVERRIDE);
    // landscape only, so SDL never lays its view out portrait (Info.plist agrees)
    SDL_SetHintWithPriority(SDL_HINT_ORIENTATIONS, "LandscapeLeft LandscapeRight", SDL_HINT_OVERRIDE);
}

/* A light tap from the Taptic Engine (port/flat/window/flat_touch.cpp's
 * Touch vibration): SDL lists no haptic device for the phone itself. */
void gevrIosHaptic(void)
{
    static UIImpactFeedbackGenerator *tap;
    if (!tap) {
        tap = [[UIImpactFeedbackGenerator alloc] initWithStyle:UIImpactFeedbackStyleLight];
    }
    [tap impactOccurred];
    [tap prepare];   // keeps the engine warm for the next press
}
