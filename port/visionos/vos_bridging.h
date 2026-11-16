// What port/visionos's Swift (the app target) and its C/Objective-C++ (the game
// library) share. Swift reads this as its bridging header.
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/* vos_app.mm: the immersive space's layer renderer, unretained */
void gevrVosLayerReady(void *layerRenderer);

/* One PlayStation VR2 Sense controller as a Touch controller (VosControllers.swift) */
typedef struct VosSenseHand {
    int connected;
    int tracked;            /* grip below is this frame's */
    float grip[16];         /* origin from grip, column-major, metres */
    float stickX, stickY;
    float trigger, squeeze;
    int a, b, menu, stickClick, triggerTouch;
} VosSenseHand;

/* VosControllers.swift: both hands (out[0] left, out[1] right) at a display
 * time (seconds, CACurrentMediaTime's clock); returns the controllers connected */
int vosSenseSync(double displaySeconds, VosSenseHand *out);
void vosSenseHaptic(int hand, float amplitude, double seconds);

/* The file picker: VosApp.swift opens a window for it (vosPickerOpen); the
 * window answers vos_app.mm (vosPickerDone): 1 with the copy's path, 0
 * cancelled, -1 it could not be shown. */
void vosPickerOpen(const char *title);
void vosPickerDone(int status, const char *path);

#ifdef __cplusplus
}
#endif
