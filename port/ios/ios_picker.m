/*
 * iOS: the launcher's file pickers (port/pc/pc_launcher.cpp pcChooseFile):
 * the system's document picker, as a copy, waited for on the game's thread
 * (the main thread) while SDL's event pump keeps UIKit's run loop turning.
 */
#import <UIKit/UIKit.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>

#include <SDL.h>
#include <string.h>

@interface GevrPickerDelegate : NSObject <UIDocumentPickerDelegate>
@property (nonatomic) int state;   // 0 open, 1 chosen, 2 cancelled
@property (nonatomic, copy) NSString *path;
@end

@implementation GevrPickerDelegate
- (void)documentPicker:(UIDocumentPickerViewController *)controller didPickDocumentsAtURLs:(NSArray<NSURL *> *)urls
{
    self.path = urls.firstObject.path;
    self.state = self.path ? 1 : 2;
}

- (void)documentPickerWasCancelled:(UIDocumentPickerViewController *)controller
{
    self.state = 2;
}
@end

static UIViewController *gevrTopController(void)
{
    UIWindow *key = nil;
    for (UIScene *sc in UIApplication.sharedApplication.connectedScenes) {
        if (![sc isKindOfClass:UIWindowScene.class]) continue;
        for (UIWindow *w in ((UIWindowScene *)sc).windows) {
            if (w.isKeyWindow || !key) key = w;
        }
    }
    UIViewController *vc = key.rootViewController;
    while (vc.presentedViewController) vc = vc.presentedViewController;
    return vc;
}

/*
 * Shows the picker and waits. Returns 1 with the copy's path in out (in the
 * app's temporary folder, the caller copies or reads it), 0 when cancelled,
 * -1 when it could not be shown.
 */
int gevrIosChooseFile(const char *title, char *out, int outLen)
{
    @autoreleasepool {
        UIViewController *top = gevrTopController();
        if (!top) return -1;
        // any file: ROMs (.z64 .v64 .n64) and the XBLA copy have no system types
        UIDocumentPickerViewController *picker =
            [[UIDocumentPickerViewController alloc] initForOpeningContentTypes:@[ UTTypeItem ] asCopy:YES];
        GevrPickerDelegate *del = [GevrPickerDelegate new];
        picker.delegate = del;
        picker.allowsMultipleSelection = NO;
        picker.title = [NSString stringWithUTF8String:title];
        [top presentViewController:picker animated:YES completion:nil];
        while (del.state == 0) {
            SDL_PumpEvents();
            SDL_Delay(16);
        }
        if (del.state != 1) return 0;
        snprintf(out, outLen, "%s", del.path.fileSystemRepresentation);
        return 1;
    }
}
