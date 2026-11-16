/*
 * Vision Pro, native: the controllers the OpenXR runtime reports
 * (docs/visionos-native.md, vos_xr.mm's actions).
 *
 * The game binds Touch (vr_input.cpp: /interaction_profiles/oculus/
 * touch_controller), so this answers a Touch pair. V0 (the spike) reads a
 * game pad through GameController: left stick and trigger, X/Y and the left
 * shoulder as the left Touch; the right ones and A/B as the right. With no
 * tracked controllers the hands are held in front of the head, turned with
 * it, so a pad aims where the player looks. PlayStation VR2 Sense
 * controllers with their tracked poses are V2 (accessory tracking).
 */
#import <Foundation/Foundation.h>
#import <GameController/GameController.h>

#include "vos.h"

static VosHand s_hands[2];
static bool s_saidPad;

static simd_float4x4 vosHandAtHead(const simd_float4x4 &head, int hand)
{
    // the head's facing without its roll: forward and level-ish, so the hands
    // follow the look up and down but do not tip with a tilted head
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

extern "C" void vosInputSync(double, const simd_float4x4 *head)
{
    GCController *pad = nil;
    for (GCController *c in GCController.controllers) {
        if (c.extendedGamepad) {
            pad = c;
            break;
        }
    }
    for (int h = 0; h < 2; h++) {
        VosHand &v = s_hands[h];
        const simd_float4x4 grip = vosHandAtHead(*head, h);
        v = VosHand{};
        v.grip = grip;
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

extern "C" void vosInputHaptic(int, float, double)
{
    // V2: the Sense controllers' haptics (GCController.haptics)
}
