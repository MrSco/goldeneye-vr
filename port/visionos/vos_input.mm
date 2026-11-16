/*
 * Vision Pro, native: the controllers the OpenXR runtime reports
 * (docs/visionos-native.md, vos_xr.mm's actions).
 *
 * The game binds Touch (vr_input.cpp: /interaction_profiles/oculus/
 * touch_controller), so this answers a Touch pair:
 *   - PlayStation VR2 Sense controllers (VosControllers.swift, Klepton's
 *     port): tracked grips, sticks, triggers, grips, A/B (X/Y on the left),
 *     the left menu button, rumble. A hand without a tracked pose this frame
 *     keeps its last one;
 *   - otherwise a game pad through GameController: left stick and trigger,
 *     X/Y and the left shoulder as the left Touch, the right ones and A/B as
 *     the right, the hands held in front of the head and turned with it, so
 *     the pad aims where the player looks.
 */
#import <Foundation/Foundation.h>
#import <GameController/GameController.h>

#include "vos.h"
#include "vos_bridging.h"

static VosHand s_hands[2];
static bool s_saidPad, s_saidSense[2];
static int s_senseCount = -1;
static bool s_senseActive;

static simd_float4x4 vosHandAtHead(const simd_float4x4 &head, int hand)
{
    // the head's facing without its roll: the hands follow the look up and
    // down but do not tip with a tilted head
    simd_float3 fwd = -head.columns[2].xyz;
    if (simd_length(fwd) < 1e-4f) fwd = simd_make_float3(0.0f, 0.0f, -1.0f);
    fwd = simd_normalize(fwd);
    simd_float3 right = simd_cross(fwd, simd_make_float3(0.0f, 1.0f, 0.0f));
    if (simd_length(right) < 1e-4f) right = head.columns[0].xyz;
    right = simd_normalize(right);
    const simd_float3 up = simd_normalize(simd_cross(right, fwd));
    const simd_float3 pos = head.columns[3].xyz + right * (hand == 0 ? -0.18f : 0.18f) - up * 0.30f + fwd * 0.30f;
    simd_float4x4 m;
    m.columns[0] = simd_make_float4(right, 0.0f);
    m.columns[1] = simd_make_float4(up, 0.0f);
    m.columns[2] = simd_make_float4(-fwd, 0.0f);   // the grip's -Z points ahead
    m.columns[3] = simd_make_float4(pos, 1.0f);
    return m;
}

/* the Sense pair; false when none is connected */
static bool vosSyncSense(double t)
{
    VosSenseHand sense[2];
    const int n = vosSenseSync(t, sense);
    if (n != s_senseCount) {
        s_senseCount = n;
        vosLog("visionos: %d PlayStation VR2 Sense controller(s)", n);
    }
    if (n <= 0) return false;
    for (int h = 0; h < 2; h++) {
        const VosSenseHand &s = sense[h];
        VosHand &v = s_hands[h];
        const simd_float4x4 lastGrip = v.grip;
        const bool hadPose = v.poseValid && v.active;
        v = VosHand{};
        v.active = s.connected != 0;
        if (!v.active) continue;
        if (s.tracked) {
            memcpy(&v.grip, s.grip, sizeof(s.grip));
            v.poseValid = true;
            if (!s_saidSense[h]) {
                s_saidSense[h] = true;
                vosLog("visionos: %s Sense controller tracked at (%.2f %.2f %.2f) m", h == 0 ? "left" : "right",
                       v.grip.columns[3].x, v.grip.columns[3].y, v.grip.columns[3].z);
            }
        } else if (hadPose) {
            v.grip = lastGrip;   // lost for a moment: where it last was
            v.poseValid = true;
        }
        v.stickX = s.stickX;
        v.stickY = s.stickY;
        v.trigger = s.trigger;
        v.squeeze = s.squeeze;
        v.a = s.a;
        v.b = s.b;
        v.menu = h == 0 && s.menu;   // Touch's menu button is the left's
        v.stickClick = s.stickClick;
        v.triggerTouch = s.triggerTouch;
    }
    return true;
}

extern "C" void vosInputSync(double t, const simd_float4x4 *head)
{
    s_senseActive = vosSyncSense(t);
    if (s_senseActive) return;

    GCController *pad = nil;
    for (GCController *c in GCController.controllers) {
        if (c.extendedGamepad && ![c.productCategory isEqualToString:GCProductCategorySpatialController]) {
            pad = c;
            break;
        }
    }
    for (int h = 0; h < 2; h++) {
        VosHand &v = s_hands[h];
        v = VosHand{};
        v.grip = vosHandAtHead(*head, h);
        v.poseValid = pad != nil;
        v.active = pad != nil;
    }
    if (!pad) {
        s_saidPad = false;
        return;
    }
    if (!s_saidPad) {
        s_saidPad = true;
        vosLog("visionos: pad %s (%s) as a Touch pair", pad.vendorName.UTF8String ?: "?",
               pad.productCategory.UTF8String ?: "?");
    }
    GCExtendedGamepad *g = pad.extendedGamepad;
    VosHand &l = s_hands[0], &r = s_hands[1];
    l.stickX = g.leftThumbstick.xAxis.value;
    l.stickY = g.leftThumbstick.yAxis.value;
    l.trigger = g.leftTrigger.value;
    l.squeeze = g.leftShoulder.value;
    l.a = g.buttonX.pressed;
    l.b = g.buttonY.pressed;
    l.menu = g.buttonMenu.pressed;
    l.stickClick = g.leftThumbstickButton.pressed;
    r.stickX = g.rightThumbstick.xAxis.value;
    r.stickY = g.rightThumbstick.yAxis.value;
    r.trigger = g.rightTrigger.value;
    r.squeeze = g.rightShoulder.value;
    r.a = g.buttonA.pressed;
    r.b = g.buttonB.pressed;
    r.stickClick = g.rightThumbstickButton.pressed;
}

extern "C" const VosHand *vosInputHand(int hand)
{
    return hand >= 0 && hand < 2 ? &s_hands[hand] : nullptr;
}

extern "C" void vosInputHaptic(int hand, float amplitude, double seconds)
{
    if (s_senseActive) vosSenseHaptic(hand, amplitude, seconds);
}
