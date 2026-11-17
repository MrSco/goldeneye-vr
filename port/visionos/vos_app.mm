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
#include "vos_bridging.h"

extern "C" void sysLogPrintf(int level, const char *fmt, ...);
extern "C" int gevrVosGameMain(int argc, const char **argv);   // port/src/main.c
extern "C" void pcReportForeground(int active);                 // port/pc/pc_reports.cpp

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
 * The launcher's file pickers (port/pc/pc_launcher.cpp, as on iOS): a window
 * beside the space with the system's picker (VosApp.swift), as Klepton's
 * "kimport" window. The game's thread waits here, and since it is also the
 * compositor's frame loop, the space keeps getting frames (black) meanwhile.
 * Returns 1 with the copy's path in out (the caller copies or reads it), 0
 * cancelled, -1 the picker could not be shown.
 */
static pthread_mutex_t s_pickLock = PTHREAD_MUTEX_INITIALIZER;
static int s_pickStatus;   // 2 waiting
static char s_pickPath[1024];

extern "C" void vosPickerDone(int status, const char *path)
{
    pthread_mutex_lock(&s_pickLock);
    snprintf(s_pickPath, sizeof(s_pickPath), "%s", path ? path : "");
    s_pickStatus = status;
    pthread_mutex_unlock(&s_pickLock);
}

extern "C" int gevrIosChooseFile(const char *title, char *out, int outLen)
{
    pthread_mutex_lock(&s_pickLock);
    s_pickStatus = 2;
    s_pickPath[0] = 0;
    pthread_mutex_unlock(&s_pickLock);
    vosLog("visionos: file picker: %s", title ? title : "");
    vosPickerOpen(title);
    for (;;) {
        pthread_mutex_lock(&s_pickLock);
        const int st = s_pickStatus;
        pthread_mutex_unlock(&s_pickLock);
        if (st != 2) break;
        vosKeepAlive();
    }
    pthread_mutex_lock(&s_pickLock);
    const int st = s_pickStatus;
    if (st == 1 && out && outLen > 0) snprintf(out, (size_t)outLen, "%s", s_pickPath);
    pthread_mutex_unlock(&s_pickLock);
    vosLog("visionos: file picker: %s", st == 1 ? "chosen" : st == 0 ? "cancelled" : "could not be shown");
    return st == 1 && out && out[0] ? 1 : st;
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
    // main.c's atexit (cleanup) saves the binds, the config and the VR settings;
    // a closed space is a clean close, not a crash to offer a report for
    pcReportForeground(0);
    vosLog("visionos: closing");
    exit(0);
}
