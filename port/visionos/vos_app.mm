/*
 * Vision Pro, native: the app's start (docs/visionos-native.md).
 *
 * VosApp.swift opens one full-immersion space at launch; its CompositorLayer
 * hands the layer renderer here. The game's main (port/src/main.c,
 * gevrVosGameMain) then runs on a thread of its own, as it runs on SDL's
 * thread on the Quest: it never returns while the game runs, and the
 * compositor's frame loop is its xrWaitFrame (vos_xr.mm).
 */
#import <Foundation/Foundation.h>
#import <CompositorServices/CompositorServices.h>

#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

#include "vos.h"

extern "C" void sysLogPrintf(int level, const char *fmt, ...);
extern "C" int gevrVosGameMain(int argc, const char **argv);   // port/src/main.c

static void *s_layer;   // cp_layer_renderer_t, CFBridgingRetain'd
static bool s_started;

extern "C" void vosLog(const char *fmt, ...)
{
    char line[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(line, sizeof(line), fmt, ap);
    va_end(ap);
    NSLog(@"GoldenEyeVR: %s", line);
    sysLogPrintf(0, "%s", line);
}

extern "C" void *vosLayerRenderer(void)
{
    return s_layer;
}

static void *vosGameThread(void *)
{
    pthread_setname_np("gevr-game");
    // the process's arguments (xcrun simctl launch ... --boot-stage 33), as a
    // desktop build takes them
    NSArray<NSString *> *args = NSProcessInfo.processInfo.arguments;
    static const char *argv[64];
    int argc = 0;
    for (NSString *a in args) {
        if (argc >= 63) break;
        argv[argc++] = strdup(a.UTF8String);
    }
    argv[argc] = nullptr;
    vosLog("visionos: the game starts (%d arguments)", argc);
    const int rc = gevrVosGameMain(argc, argv);
    vosLog("visionos: the game returned %d", rc);
    exit(rc);
    return nullptr;
}

extern "C" void gevrVosLayerReady(void *layerRenderer)
{
    if (s_layer) {
        // a second space (it was closed and opened again): the game went with
        // the first (vosSpaceClosed), so this is a new process's, never ours
        vosLog("visionos: another layer renderer; the game keeps the first");
        return;
    }
    s_layer = (void *)CFBridgingRetain((__bridge id)layerRenderer);
    if (s_started) return;
    s_started = true;
    // the game's thread: the decomp's deep recursion wants a main thread's stack
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, 16u << 20);
    pthread_t t;
    if (pthread_create(&t, &attr, vosGameThread, nullptr) != 0) {
        vosLog("visionos: could not start the game's thread");
        abort();
    }
    pthread_attr_destroy(&attr);
    pthread_detach(t);
}

/*
 * The launcher's file pickers (port/pc/pc_launcher.cpp, as on iOS): none yet.
 * A full space has no window to show the system's picker in; the next step is
 * a window opened for it beside the space, as Klepton's "kimport" does. Until
 * then the ROM goes into the app's folder in the Files app (UIFileSharingEnabled),
 * where the launcher finds it. -1: could not be shown.
 */
extern "C" int gevrIosChooseFile(const char *title, char *out, int outLen)
{
    (void)out;
    (void)outLen;
    vosLog("visionos: no file picker yet (%s): put the file in GoldenEye VR's folder in the Files app", title ? title : "");
    return -1;
}

/* Online voice (port/src/net/net_voice.c): Apple's echo cancellation and noise
 * suppression on the mic, as port/ios/ios_app.m's */
#import <AVFoundation/AVFoundation.h>

extern "C" void gevrIosVoiceChatMode(void)
{
    AVAudioSession *s = AVAudioSession.sharedInstance;
    NSError *err = nil;
    if (![s setCategory:s.category mode:AVAudioSessionModeVoiceChat options:s.categoryOptions error:&err]) {
        vosLog("visionos: voice chat mode refused: %s", err.localizedDescription.UTF8String);
    }
}

extern "C" void vosSpaceClosed(void)
{
    // main.c's atexit (cleanup) saves the binds, the config and the VR settings
    vosLog("visionos: closing");
    exit(0);
}
