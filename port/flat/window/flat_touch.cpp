/*
 * On-screen touch controls for a flat game: the Android phone/tablet APK, and
 * a PC with a touch screen (docs/mobile-platforms.md, "Touch layout").
 *
 * Ported, not designed:
 *  - The mechanics come from Mr4xd3mn/perfect-dark-android port/src/touch.c
 *    (MIT, Ryan Dwyer and contributors): SDL finger events tracked by touch
 *    and finger id, each finger's role fixed when it lands, a floating stick
 *    on the left, relative look on the right fed into the port's mouse look,
 *    buttons that become the N64 pad bits the game already reads.
 *  - The layout and feel come from rebelancap/Project-Midas-ios
 *    app/ios/GETouchOverlay.m and GETouchLayout.m (MIT, murkantor and the
 *    Project Midas contributors), tuned for GoldenEye on a phone: the button
 *    table below is theirs to the digit, in fractions of the screen with radii
 *    in points (an Android dp is the same 1/160 inch); a floating stick on the
 *    left 45 % with 62 pt of travel, a 0.15 radial dead zone and a 0.4 linear
 *    + 0.6 cubic response; look at 0.25 degrees a point; AIM and FIRE drag the
 *    view as well; a mirrored FIRE for the left thumb while AIM is held;
 *    glyphs, not words; no resting stick drawn.
 *
 * port/src/input.c reads the result (flatTouchPoll) into the pad as the
 * game's own controller: FIRE is Z, AIM is R, USE is B, WEAPON is A, START is
 * START, and the stick walks and strafes on the C buttons as a pad's left
 * stick does (1.2 Solitaire). In menus the stick moves GoldenEye's crosshair
 * cursor, FIRE selects (A) and USE goes back (B). The look goes in through
 * input.c's mouse look (inputMouseGetLookDeg, bondview2.c), in degrees.
 *
 * The overlay hides itself when a pad button or a key is pressed and comes
 * back with the next touch; on a PC it stays hidden until a finger lands.
 * Drawn with ImGui's draw lists, in its own context, over the game's frame
 * in the window (flat_render.cpp vr_screen_present).
 */

#ifdef GEVR_FLAT

#include <SDL.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#if defined(ANDROID) || defined(GEVR_GLES)   // GLES 3: Android, iOS (ANGLE)
#include <GLES3/gl3.h>
#else
#include "../../fast3d/glad/glad.h"
#endif
#include "../../vr/imgui/imgui.h"
#include "../../vr/imgui/imgui_impl_opengl3.h"
#include <PR/ultratypes.h>
#include "system.h"

extern SDL_Window *wnd;   // flat_window.cpp
extern "C" int gevrNativePauseOpen(void);   // port/src/input.c: the in-game panel or the online pause is up
extern "C" int VrTouchHaptics, VrGyroAim;   // vr_settings.cpp
extern "C" float VrGyroSpeed;
#ifdef GEVR_IOS
extern "C" void gevrIosHaptic(void);   // port/ios/ios_app.m: the Taptic Engine's light tap
extern "C" int gevrIosTouchIsLive(long long fingerId);   // ios_app.m: UIKit still has it down
#endif

/* What a button does; port/src/input.c turns these into pad bits. */
enum {
    TOUCH_FIRE = 1 << 0,
    TOUCH_AIM = 1 << 1,
    TOUCH_USE = 1 << 2,
    TOUCH_RELOAD = 1 << 3,
    TOUCH_CROUCH = 1 << 4,
    TOUCH_WEAPON = 1 << 5,
    TOUCH_START = 1 << 6,
};

enum Glyph { G_FIRE, G_AIM, G_USE, G_RELOAD, G_CROUCH, G_WEAPON, G_START };

struct TouchButton {
    const char *name;
    unsigned action;
    float unitX, unitY;   // centre, as a fraction of the window
    float radius;         // points
    bool inMenu;          // shown while a menu is up (FIRE, USE, START)
    Glyph glyph;
};

/* Midas's GETouchLayout.m kBaked (from the GoldenEye recomp port's shipped
 * layout), less GADGET: GoldenEye has no next-gadget button (the watch
 * reaches every item). FIRE stays first: the left FIRE mirrors it. */
static const TouchButton kButtons[] = {
    { "FIRE",   TOUCH_FIRE,   0.8609f, 0.7548f, 46.0f, true,  G_FIRE },
    { "AIM",    TOUCH_AIM,    0.9315f, 0.5484f, 28.0f, false, G_AIM },
    { "USE",    TOUCH_USE,    0.8030f, 0.9278f, 28.0f, true,  G_USE },
    { "RELOAD", TOUCH_RELOAD, 0.8521f, 0.5119f, 28.0f, false, G_RELOAD },
    { "CROUCH", TOUCH_CROUCH, 0.9443f, 0.9175f, 28.0f, false, G_CROUCH },
    { "WEAPON", TOUCH_WEAPON, 0.9554f, 0.7214f, 28.0f, false, G_WEAPON },
    { "START",  TOUCH_START,  0.9400f, 0.0992f, 20.0f, true,  G_START },
};
static const int kCount = (int)(sizeof(kButtons) / sizeof(kButtons[0]));
static const int kLeftFire = kCount;   // the aim-mode left FIRE's index

static const float kStickRegionFrac = 0.45f;  // the left share of the screen
static const float kStickRadius = 62.0f;      // points of travel to full deflection
static const float kStickKnobRadius = 26.0f;
static const float kStickDeadzone = 0.15f;
static const float kStickCurve = 0.6f;        // the cubic share of the response
static const float kLookDegPerPoint = 0.25f;
static const float kLookSlopPoints = 1.0f;    // a still finger must not creep the view
static const float kHitSlop = 1.25f;          // fingers land short of what they aim at
static const float kOpacity = 0.28f;
static const float kPi = 3.14159265f;

enum Role { R_NONE, R_STICK, R_LOOK, R_BUTTON };

struct Finger {
    SDL_TouchID touch;
    SDL_FingerID id;
    Role role;
    int button;        // R_BUTTON: the index, or kLeftFire
    bool dragsLook;    // AIM / FIRE: the finger on the button also turns the view
    float x, y;        // pixels
    float lastX, lastY;   // where the look last took its delta from
    bool active;
    Uint32 downAt;     // SDL_GetTicks when it landed (START's long press)
    bool fired;        // START: the long press has asked for the panel
};

static Finger s_fingers[10];
static float s_stickOX, s_stickOY, s_stickX, s_stickY;   // the floating stick, pixels
static int s_stickFinger = -1;
static float s_yawDeg, s_pitchDeg;   // look since the game last took it
static bool s_looked;                // a finger turned the view since the last poll
static int s_crouchTaps;
/* Presses since the game's last poll: a tap goes down and up between two
 * polls, and the game must still see it once (PD Android's tap button). */
static unsigned s_tapped;
/* START held this long asks for the in-game panel (port/src/input.c); a
 * shorter press is START, sent when the finger lifts. */
static const Uint32 kPanelHoldMs = 700;
static bool s_panelRequest;
static bool s_menu;                  // the game's last word: a menu is up
#if defined(GEVR_MOBILE) || defined(GEVR_IOS)
static bool s_shown = true;          // drawn and taking touches: from the start on a phone
#else
static bool s_shown;                 // a PC waits for a finger
#endif
static bool s_seen;                  // a finger has landed since start
static int s_winW, s_winH;           // the window's drawable size, pixels
static float s_ptPx = 2.0f;          // pixels a point
static ImGuiContext *s_imgui;

static bool aimHeld(void);

/* ---- layout ------------------------------------------------------------- */

static void touchMeasure(void)
{
    int w = 0, h = 0;
    if (!wnd) return;
    SDL_GL_GetDrawableSize(wnd, &w, &h);
    if (w <= 0 || h <= 0) return;
    s_winW = w;
    s_winH = h;
    float ddpi = 0.0f, hdpi = 0.0f, vdpi = 0.0f;
    float pt = 0.0f;
    if (SDL_GetDisplayDPI(SDL_GetWindowDisplayIndex(wnd), &ddpi, &hdpi, &vdpi) == 0 && vdpi > 0.0f) {
        pt = vdpi / 160.0f;   // a point is an Android dp, 1/160 inch
    }
    // A phone held sideways is about 390 points tall (Midas's layout was made
    // on one); keep any odd DPI within reach of that.
    const float phone = (float)h / 390.0f;
    if (pt <= 0.0f) pt = phone;
    if (pt < phone * 0.6f) pt = phone * 0.6f;
    if (pt > phone * 1.6f) pt = phone * 1.6f;
    s_ptPx = pt;
}

static bool buttonVisible(int i)
{
    if (i == kLeftFire) return !s_menu && aimHeld();
    return s_menu ? kButtons[i].inMenu : true;
}

/*
 * The touch layout editor's changes (Midas's GETouchLayout overrides): a
 * button's centre as fractions of the window and its radius in points, NAN
 * where it keeps the built-in value. FIRE_L placed of its own stops
 * mirroring FIRE. Kept in VrTouchLayout as "NAME x y radius;...".
 */
extern "C" char VrTouchLayout[1024];   // vr_settings.cpp
extern "C" void vrSettingsSave(void);
static float s_ovX[kCount + 1], s_ovY[kCount + 1], s_ovR[kCount + 1];
static bool s_ovLoaded;

static const char *buttonName(int i) { return i == kLeftFire ? "FIRE_L" : kButtons[i].name; }
static float clampf(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }

static void layoutLoad(void)
{
    s_ovLoaded = true;
    for (int i = 0; i <= kCount; i++) s_ovX[i] = s_ovY[i] = s_ovR[i] = NAN;
    const char *p = VrTouchLayout;
    while (*p) {
        char name[16];
        float x, y, r;
        int used = 0;
        if (sscanf(p, " %15[A-Z_] %f %f %f%n", name, &x, &y, &r, &used) == 4) {
            for (int i = 0; i <= kCount; i++) {
                if (strcmp(name, buttonName(i)) == 0) {
                    s_ovX[i] = clampf(x, 0.02f, 0.98f);
                    s_ovY[i] = clampf(y, 0.02f, 0.98f);
                    s_ovR[i] = clampf(r, 12.0f, 80.0f);
                }
            }
            p += used;
        }
        const char *semi = strchr(p, ';');
        if (!semi) break;
        p = semi + 1;
    }
}

static void layoutSave(void)
{
    char out[sizeof(VrTouchLayout)] = "";
    size_t n = 0;
    for (int i = 0; i <= kCount; i++) {
        if (isnan(s_ovX[i])) continue;
        n += snprintf(out + n, sizeof(out) - n, "%s%s %.4f %.4f %.1f", n ? ";" : "", buttonName(i), s_ovX[i], s_ovY[i],
                      isnan(s_ovR[i]) ? kButtons[i == kLeftFire ? 0 : i].radius : s_ovR[i]);
        if (n >= sizeof(out)) break;
    }
    snprintf(VrTouchLayout, sizeof(VrTouchLayout), "%s", out);
    vrSettingsSave();
    sysLogPrintf(LOG_NOTE, "touch: layout saved \"%s\"", VrTouchLayout);
}

static void buttonCentre(int i, float *x, float *y, float *r)
{
    if (!s_ovLoaded) layoutLoad();
    const int src = i == kLeftFire ? 0 : i;
    const TouchButton &b = kButtons[src];
    *x = (isnan(s_ovX[src]) ? b.unitX : s_ovX[src]) * (float)s_winW;
    *y = (isnan(s_ovY[src]) ? b.unitY : s_ovY[src]) * (float)s_winH;
    *r = (isnan(s_ovR[src]) ? b.radius : s_ovR[src]) * s_ptPx;
    if (i == kLeftFire) {
        if (!isnan(s_ovX[kLeftFire])) {   // placed in the editor
            *x = s_ovX[kLeftFire] * (float)s_winW;
            *y = s_ovY[kLeftFire] * (float)s_winH;
            if (!isnan(s_ovR[kLeftFire])) *r = s_ovR[kLeftFire] * s_ptPx;
        } else {
            *x = (float)s_winW - *x;   // FIRE, mirrored for the left thumb
        }
    }
}

/* The button under a point: the nearest within its hit radius, or -1. The
 * left FIRE first: while it is up it shares the stick's side. */
static float hitDistance(int i, float px, float py)
{
    float x, y, r;
    buttonCentre(i, &x, &y, &r);
    const float d = sqrtf((px - x) * (px - x) + (py - y) * (py - y));
    return d <= r * kHitSlop ? d : -1.0f;
}

static int buttonAt(float px, float py)
{
    if (buttonVisible(kLeftFire) && hitDistance(kLeftFire, px, py) >= 0.0f) return kLeftFire;
    int best = -1;
    float bestD = 1e30f;
    for (int i = 0; i < kCount; i++) {
        if (!buttonVisible(i)) continue;
        const float d = hitDistance(i, px, py);
        if (d >= 0.0f && d < bestD) {
            best = i;
            bestD = d;
        }
    }
    return best;
}

static bool buttonHeld(int i)
{
    for (const Finger &f : s_fingers) {
        if (f.active && f.role == R_BUTTON && f.button == i) return true;
    }
    return false;
}

/* Held by a finger other than `except` (fingerDown: is this a new press). */
static bool buttonHeldBy(int i, int except)
{
    for (int k = 0; k < 10; k++) {
        const Finger &f = s_fingers[k];
        if (k != except && f.active && f.role == R_BUTTON && f.button == i) return true;
    }
    return false;
}

/*
 * A short buzz on a press (Midas: UIImpactFeedbackGenerator on the press).
 * SDL lists the phone's own vibrator as a haptic device on Android
 * (SDLHapticHandler, VIBRATE in the manifest); a pad's rumble is not used.
 */
static void touchBuzz(void)
{
#ifdef ANDROID
    static SDL_Haptic *haptic;
    static bool tried;
    if (!VrTouchHaptics) return;
    if (!tried) {
        tried = true;
        if (SDL_InitSubSystem(SDL_INIT_HAPTIC) == 0) {
            for (int i = 0; i < SDL_NumHaptics() && !haptic; i++) {
                const char *name = SDL_HapticName(i);
                sysLogPrintf(LOG_NOTE, "touch: haptic %d \"%s\"", i, name ? name : "");
                if (name && strstr(name, "VIBRATOR")) haptic = SDL_HapticOpen(i);
            }
            if (haptic && SDL_HapticRumbleInit(haptic) != 0) {
                SDL_HapticClose(haptic);
                haptic = nullptr;
            }
        }
        sysLogPrintf(LOG_NOTE, "touch: vibration %s", haptic ? "on" : "unavailable");
    }
    if (haptic) SDL_HapticRumblePlay(haptic, 0.35f, 12);
#elif defined(GEVR_IOS)
    // iOS: SDL lists no haptic device for the phone itself
    if (VrTouchHaptics) gevrIosHaptic();
#endif
}

static bool aimHeld(void)
{
    return buttonHeld(1);   // kButtons[1] is AIM
}

/* ---- the stick ---------------------------------------------------------- */

/* Midas geStickResponse: radial dead zone, rescale, (1-c)n + cn^3, the
 * direction taken before the clamp. Up-positive out. */
static void stickResponse(float *outX, float *outY)
{
    *outX = *outY = 0.0f;
    if (s_stickFinger < 0) return;
    const float dx = s_stickX - s_stickOX, dy = s_stickY - s_stickOY;
    const float len = sqrtf(dx * dx + dy * dy);
    const float radius = kStickRadius * s_ptPx;
    if (radius <= 0.0f || len <= 0.0f) return;
    float mag = len / radius;
    if (mag <= kStickDeadzone) return;
    if (mag > 1.0f) mag = 1.0f;
    mag = (mag - kStickDeadzone) / (1.0f - kStickDeadzone);
    const float curved = (1.0f - kStickCurve) * mag + kStickCurve * mag * mag * mag;
    *outX = dx / len * curved;
    *outY = -dy / len * curved;
}

/* ---- events ------------------------------------------------------------- */

static int findFinger(SDL_TouchID touch, SDL_FingerID id)
{
    for (int i = 0; i < 10; i++) {
        if (s_fingers[i].active && s_fingers[i].touch == touch && s_fingers[i].id == id) return i;
    }
    return -1;
}

static void releaseAll(void)
{
    memset(s_fingers, 0, sizeof(s_fingers));
    s_stickFinger = -1;
}

static void fingerUp(const SDL_TouchFingerEvent &e);

/*
 * Lets go of every finger SDL no longer has down. A lift we never saw (a
 * finger that slid off the screen's edge into a system gesture, user test
 * 2026-11-09) left the stick held: Bond kept walking, and the next thumb on
 * the left could not take the stick back.
 */
static void sweepLostFingers(void)
{
    for (int i = 0; i < 10; i++) {
        Finger &f = s_fingers[i];
        if (!f.active) continue;
        bool down = false;
        const int n = SDL_GetNumTouchFingers(f.touch);
        for (int k = 0; k < n && !down; k++) {
            const SDL_Finger *sf = SDL_GetTouchFinger(f.touch, k);
            down = sf && sf->id == f.id;
        }
#ifdef GEVR_IOS
        // iOS can lose a lift before SDL hears of it (a finger slid off an
        // edge: user, AIM stuck held): UIKit's own list of the touches down
        if (down && gevrIosTouchIsLive((long long)f.id) == 0) down = false;
#endif
        if (!down) {
            sysLogPrintf(LOG_NOTE, "touch: let go of a finger the system lost (%s)",
                         f.role == R_STICK ? "the stick" : f.role == R_LOOK ? "a look"
                         : f.role == R_BUTTON ? kButtons[f.button == kLeftFire ? 0 : f.button].name : "none");
            SDL_TouchFingerEvent up = {};
            up.touchId = f.touch;
            up.fingerId = f.id;
            fingerUp(up);
        }
    }
}

static void fingerDown(const SDL_TouchFingerEvent &e)
{
    sweepLostFingers();
    int idx = findFinger(e.touchId, e.fingerId);
    for (int i = 0; idx < 0 && i < 10; i++) {
        if (!s_fingers[i].active) idx = i;
    }
    if (idx < 0) return;
    Finger &f = s_fingers[idx];
    memset(&f, 0, sizeof(f));
    f.active = true;
    f.touch = e.touchId;
    f.id = e.fingerId;
    f.x = f.lastX = e.x * (float)s_winW;
    f.y = f.lastY = e.y * (float)s_winH;
    // The role is fixed now and kept until the finger lifts: a drag that
    // wanders onto a button must not press it.
    const int b = buttonAt(f.x, f.y);
    if (b >= 0) {
        f.role = R_BUTTON;
        f.button = b;
        const unsigned act = kButtons[b == kLeftFire ? 0 : b].action;
        f.dragsLook = !s_menu && (act == TOUCH_AIM || act == TOUCH_FIRE);
        if (act == TOUCH_CROUCH) s_crouchTaps++;
        f.downAt = SDL_GetTicks();
        if (act != TOUCH_START) s_tapped |= act;   // START waits: a tap, or a long press
        if (!buttonHeldBy(b, idx)) touchBuzz();   // a second finger on a held button isn't a press
        return;
    }
    if (s_stickFinger < 0 && e.x < kStickRegionFrac) {
        // the stick appears wherever the thumb lands and stays there
        f.role = R_STICK;
        s_stickFinger = idx;
        s_stickOX = s_stickX = f.x;
        s_stickOY = s_stickY = f.y;
        return;
    }
    f.role = s_menu ? R_NONE : R_LOOK;
}

static void fingerMotion(const SDL_TouchFingerEvent &e)
{
    const int idx = findFinger(e.touchId, e.fingerId);
    if (idx < 0) return;
    Finger &f = s_fingers[idx];
    f.x = e.x * (float)s_winW;
    f.y = e.y * (float)s_winH;
    if (idx == s_stickFinger) {
        s_stickX = f.x;
        s_stickY = f.y;
        return;
    }
    // A finger that slides off its button keeps holding it, as a real button
    // does; AIM and FIRE turn the view meanwhile.
    if (f.role == R_LOOK || (f.role == R_BUTTON && f.dragsLook)) {
        const float dx = (f.x - f.lastX) / s_ptPx, dy = (f.y - f.lastY) / s_ptPx;
        if (fabsf(dx) >= kLookSlopPoints || fabsf(dy) >= kLookSlopPoints) {
            s_yawDeg += dx * kLookDegPerPoint;
            s_pitchDeg += dy * kLookDegPerPoint;
            s_looked = true;
            f.lastX = f.x;
            f.lastY = f.y;
        }
    }
}

static void fingerUp(const SDL_TouchFingerEvent &e)
{
    const int idx = findFinger(e.touchId, e.fingerId);
    if (idx < 0) return;
    if (idx == s_stickFinger) s_stickFinger = -1;
    {
        const Finger &f = s_fingers[idx];
        if (f.role == R_BUTTON && f.button != kLeftFire && kButtons[f.button].action == TOUCH_START && !f.fired) {
            s_tapped |= TOUCH_START;   // a tap of START
        }
    }
    s_fingers[idx].active = false;
    // An invisible button must not fire: letting go of AIM lets go of the left FIRE.
    if (!aimHeld()) {
        for (Finger &f : s_fingers) {
            if (f.active && f.role == R_BUTTON && f.button == kLeftFire) f.role = R_NONE;
        }
    }
}

/*
 * The touch layout editor (Midas's GETouchOverlay editing mode), opened from
 * the in-game panel over the paused game: drag a button to move it, pinch
 * with a second finger to resize it; Cancel, Reset (the built-in layout) and
 * Done along the top. Done saves and goes back to the panel.
 */
static bool s_editing;
static float s_snapX[kCount + 1], s_snapY[kCount + 1], s_snapR[kCount + 1];
static int s_editCount;                 // fingers down, at most two
static SDL_FingerID s_editId[2];
static SDL_TouchID s_editTouch[2];
static float s_editPX[2], s_editPY[2];
static int s_editBtn = -1;              // the button under the first finger
static float s_grabX, s_grabY;          // its centre less the finger
static float s_pinchDist, s_pinchR;     // when the second finger landed
enum { EB_CANCEL, EB_RESET, EB_DONE, EB_COUNT };
static const char *const kEditBar[EB_COUNT] = { "Cancel", "Reset", "Done" };

static void editBarRect(int k, float *x, float *y, float *w, float *h)
{
    const float bw = 120.0f * s_ptPx, bh = 40.0f * s_ptPx, gap = 16.0f * s_ptPx;
    *x = ((float)s_winW - (3.0f * bw + 2.0f * gap)) * 0.5f + k * (bw + gap);
    *y = 12.0f * s_ptPx;
    *w = bw;
    *h = bh;
}

static int editButtonAt(float px, float py)
{
    int best = -1;
    float bestD = 1e30f;
    for (int i = 0; i <= kCount; i++) {   // every button, the left FIRE too
        const float d = hitDistance(i, px, py);
        if (d >= 0.0f && d < bestD) {
            best = i;
            bestD = d;
        }
    }
    return best;
}

extern "C" int flatTouchEditing(void) { return s_editing ? 1 : 0; }

extern "C" void flatTouchEditBegin(void)
{
    if (!s_ovLoaded) layoutLoad();
    memcpy(s_snapX, s_ovX, sizeof(s_ovX));
    memcpy(s_snapY, s_ovY, sizeof(s_ovY));
    memcpy(s_snapR, s_ovR, sizeof(s_ovR));
    s_editing = true;
    s_editCount = 0;
    s_editBtn = -1;
    releaseAll();
    touchMeasure();
}

static void editEnd(bool save)
{
    if (save) {
        layoutSave();
    } else {
        memcpy(s_ovX, s_snapX, sizeof(s_ovX));
        memcpy(s_ovY, s_snapY, sizeof(s_ovY));
        memcpy(s_ovR, s_snapR, sizeof(s_ovR));
    }
    s_editing = false;
    s_editCount = 0;
    s_editBtn = -1;
    releaseAll();
}

static void editEvent(const SDL_Event *e)
{
    const float x = e->tfinger.x * (float)s_winW, y = e->tfinger.y * (float)s_winH;
    int k = -1;
    for (int j = 0; j < s_editCount; j++) {
        if (s_editId[j] == e->tfinger.fingerId && s_editTouch[j] == e->tfinger.touchId) k = j;
    }
    switch (e->type) {
    case SDL_FINGERDOWN:
        if (s_editCount == 0) {
            for (int b = 0; b < EB_COUNT; b++) {
                float bx, by, bw, bh;
                editBarRect(b, &bx, &by, &bw, &bh);
                if (x >= bx && x <= bx + bw && y >= by && y <= by + bh) {
                    if (b == EB_RESET) {
                        for (int i = 0; i <= kCount; i++) s_ovX[i] = s_ovY[i] = s_ovR[i] = NAN;
                    } else {
                        editEnd(b == EB_DONE);
                    }
                    return;
                }
            }
            s_editBtn = editButtonAt(x, y);
            if (s_editBtn >= 0) {
                float bx, by, br;
                buttonCentre(s_editBtn, &bx, &by, &br);
                s_grabX = bx - x;
                s_grabY = by - y;
            }
            s_editId[0] = e->tfinger.fingerId;
            s_editTouch[0] = e->tfinger.touchId;
            s_editPX[0] = x;
            s_editPY[0] = y;
            s_editCount = 1;
        } else if (s_editCount == 1) {
            if (s_editBtn < 0) {
                // a pinch that started beside the button: the one nearest the
                // two fingers' midpoint, within twice its size (Midas's editControlNear)
                const float mx = (x + s_editPX[0]) * 0.5f, my = (y + s_editPY[0]) * 0.5f;
                float bestD = 1e30f;
                for (int i = 0; i <= kCount; i++) {
                    float bx, by, br;
                    buttonCentre(i, &bx, &by, &br);
                    const float d = hypotf(mx - bx, my - by);
                    if (d <= br * 2.0f && d < bestD) {
                        bestD = d;
                        s_editBtn = i;
                    }
                }
                if (s_editBtn < 0) break;
            }
            s_editId[1] = e->tfinger.fingerId;
            s_editTouch[1] = e->tfinger.touchId;
            s_editPX[1] = x;
            s_editPY[1] = y;
            s_editCount = 2;
            s_pinchDist = fmaxf(hypotf(x - s_editPX[0], y - s_editPY[0]), 1.0f);
            float bx, by, br;
            buttonCentre(s_editBtn, &bx, &by, &br);
            s_pinchR = br / s_ptPx;
        }
        break;
    case SDL_FINGERMOTION:
        if (k < 0) break;
        s_editPX[k] = x;
        s_editPY[k] = y;
        if (s_editBtn < 0) break;
        if (s_editCount == 1 && k == 0) {
            float bx, by, br;
            buttonCentre(s_editBtn, &bx, &by, &br);
            s_ovX[s_editBtn] = clampf((x + s_grabX) / (float)s_winW, 0.02f, 0.98f);
            s_ovY[s_editBtn] = clampf((y + s_grabY) / (float)s_winH, 0.02f, 0.98f);
            if (isnan(s_ovR[s_editBtn])) s_ovR[s_editBtn] = br / s_ptPx;
        } else if (s_editCount == 2) {
            const float d = hypotf(s_editPX[1] - s_editPX[0], s_editPY[1] - s_editPY[0]);
            float bx, by, br;
            buttonCentre(s_editBtn, &bx, &by, &br);
            if (isnan(s_ovX[s_editBtn])) {   // resized in place: keep where it is
                s_ovX[s_editBtn] = bx / (float)s_winW;
                s_ovY[s_editBtn] = by / (float)s_winH;
            }
            s_ovR[s_editBtn] = clampf(s_pinchR * d / s_pinchDist, 12.0f, 80.0f);
        }
        break;
    case SDL_FINGERUP:
        if (k == 0) {
            s_editCount = 0;
            s_editBtn = -1;
        } else if (k == 1) {
            s_editCount = 1;   // back to dragging, from where the button is now
            float bx, by, br;
            buttonCentre(s_editBtn, &bx, &by, &br);
            s_grabX = bx - s_editPX[0];
            s_grabY = by - s_editPY[0];
        }
        break;
    default:
        break;
    }
}

extern "C" int flatTouchSeenAny(void) { return s_seen ? 1 : 0; }

/* Called for each SDL event the window drains (flat_window.cpp), on the game's thread. */
extern "C" void flatTouchEvent(const SDL_Event *e)
{
    if (s_editing) {
        if (e->type == SDL_FINGERDOWN || e->type == SDL_FINGERMOTION || e->type == SDL_FINGERUP) {
            touchMeasure();
            editEvent(e);
        }
        return;
    }
    switch (e->type) {
    case SDL_FINGERDOWN:
        touchMeasure();
        if (!s_seen) sysLogPrintf(LOG_NOTE, "touch: first finger, %dx%d px, %.2f px a point", s_winW, s_winH, s_ptPx);
        s_seen = true;
        if (!s_shown) {
            s_shown = true;
            releaseAll();
        }
        if (gevrNativePauseOpen()) return;   // the panel takes the touch as its mouse
        fingerDown(e->tfinger);
        break;
    case SDL_FINGERMOTION:
        if (s_shown) fingerMotion(e->tfinger);
        break;
    case SDL_FINGERUP:
        fingerUp(e->tfinger);   // shown or not: a lift always lets go
        break;
    case SDL_CONTROLLERBUTTONDOWN:
    case SDL_KEYDOWN:
        // A pad (or on a PC a keyboard) takes over; the next touch brings the
        // overlay back. A phone's own keys (back, volume) arrive as keys too.
#ifdef GEVR_MOBILE
        if (e->type == SDL_KEYDOWN) break;
#endif
        if (s_shown) {
            s_shown = false;
            releaseAll();
            s_crouchTaps = 0;
        }
        break;
    default:
        break;
    }
}

/* ---- the game's side (port/src/input.c) --------------------------------- */

/*
 * This poll's touch controls: the held actions (TOUCH_*), and the stick,
 * -1..1 with up positive. `menu`: the game is showing a menu (the title
 * stage, the watch, a multiplayer menu), so only FIRE, USE and START are up
 * and the stick moves the cursor. Nothing while hidden.
 */
extern "C" unsigned flatTouchPoll(int menu, float *sx, float *sy)
{
    if (!s_editing) sweepLostFingers();
    if (s_menu != (menu != 0)) {
        s_menu = menu != 0;
        releaseAll();   // the buttons under the fingers changed
        s_tapped = 0;
    }
    *sx = *sy = 0.0f;
    // The panel drains the window's events (flat_launcher.cpp): fingers lifted
    // while it was up were never seen, so it starts and ends with none down.
    static bool panelWas;
    const bool panel = gevrNativePauseOpen() != 0;
    if (panel != panelWas) {
        panelWas = panel;
        if (!panel && s_editing) editEnd(false);   // the panel closed under it (F10, Back)
        releaseAll();
        s_tapped = 0;
    }
    if (!s_shown || panel) return 0;
    unsigned held = s_tapped;
    s_tapped = 0;
    const Uint32 now = SDL_GetTicks();
    for (Finger &f : s_fingers) {
        if (!f.active || f.role != R_BUTTON) continue;
        const unsigned act = kButtons[f.button == kLeftFire ? 0 : f.button].action;
        if (act == TOUCH_START) {
            if (!f.fired && now - f.downAt >= kPanelHoldMs) {
                f.fired = true;
                s_panelRequest = true;
            }
            continue;
        }
        held |= act;
    }
    // START from a tap is sent when the finger lifts (a long press is the
    // panel), so it was one poll long, and the briefing could miss it
    // (user, 2026-11-09): held a tenth of a second, as the panel's own START is
    static Uint32 startUntil;
    if (held & TOUCH_START) startUntil = now + 100;
    if ((Sint32)(startUntil - now) > 0) held |= TOUCH_START;
    stickResponse(sx, sy);
    return held;
}

/* START held down long enough: open the in-game panel (once a press). */
extern "C" int flatTouchTakePanel(void)
{
    const bool r = s_panelRequest;
    s_panelRequest = false;
    return r ? 1 : 0;
}

/* Taps of CROUCH since the last call (port/src/input.c flips the crouch toggle). */
extern "C" int flatTouchTakeCrouch(void)
{
    const int n = s_crouchTaps;
    s_crouchTaps = 0;
    return n;
}

/* The view turn since the last call, in degrees (right and down positive, as
 * the mouse's), and whether a finger turned it. */
/*
 * Gyro aim (VrGyroAim): the device's turn rate since the last call, as view
 * degrees (right and down positive). Android reports it about the device's
 * natural axes (portrait: x right, y up, z out of the screen); held sideways,
 * the screen's up is +x (LANDSCAPE, the right side up) or -x (FLIPPED), and
 * its right is -y or +y. No reference port has gyro aim (docs/mobile-platforms.md).
 */
static void gyroLook(float *yaw, float *pitch)
{
    static SDL_Sensor *gyro;
    static bool tried;
    static Uint32 last;
    *yaw = *pitch = 0.0f;
    const Uint32 now = SDL_GetTicks();
    const float dt = last ? (now - last) / 1000.0f : 0.0f;
    last = now;
    if (!VrGyroAim || s_menu || !s_shown) return;
    // While aiming: only with AIM held (the user found always-on gyro weak for turning)
    if (VrGyroAim == 1 && !aimHeld()) return;
    if (!tried) {
        tried = true;
        if (SDL_InitSubSystem(SDL_INIT_SENSOR) == 0) {
            for (int i = 0; i < SDL_NumSensors() && !gyro; i++) {
                if (SDL_SensorGetDeviceType(i) == SDL_SENSOR_GYRO) gyro = SDL_SensorOpen(i);
            }
        }
        sysLogPrintf(LOG_NOTE, "touch: gyro %s", gyro ? "on" : "unavailable");
    }
    if (!gyro || dt <= 0.0f || dt > 0.1f) return;
    float w[3] = { 0.0f, 0.0f, 0.0f };
    if (SDL_SensorGetData(gyro, w, 3) != 0) return;
    const bool flipped = SDL_GetDisplayOrientation(0) == SDL_ORIENTATION_LANDSCAPE_FLIPPED;
    const float up = flipped ? -w[0] : w[0];      // about the screen's up axis
    const float right = flipped ? w[1] : -w[1];   // about the screen's right axis
    const float deg = 57.29578f * dt * VrGyroSpeed;
    *yaw = -up * deg;      // a positive turn about up turns the view left
    *pitch = -right * deg; // a positive turn about right looks up
}

extern "C" int flatTouchTakeLookDeg(float *yaw, float *pitch)
{
    float gy, gp;
    gyroLook(&gy, &gp);
    s_yawDeg += gy;
    s_pitchDeg += gp;
    if (gy != 0.0f || gp != 0.0f) s_looked = true;
    const int looked = s_looked;
    *yaw = s_yawDeg;
    *pitch = s_pitchDeg;
    s_yawDeg = s_pitchDeg = 0.0f;
    s_looked = false;
    return looked;
}

/* ---- drawing ------------------------------------------------------------ */

static ImU32 white(float a)
{
    if (a > 1.0f) a = 1.0f;
    return IM_COL32(255, 255, 255, (int)(a * 255.0f));
}

static void drawGlyph(ImDrawList *dl, Glyph g, ImVec2 c, float r, ImU32 col)
{
    const float t = 0.09f * r > 2.0f ? 0.09f * r : 2.0f;   // stroke
    switch (g) {
    case G_FIRE: {   // a bullet, point up: shoot (user: the crosshair read as aim)
        const float hw = r * 0.15f;                       // half its width
        const float tip = c.y - r * 0.50f, shoulder = c.y - r * 0.12f;
        const float caseTop = shoulder + r * 0.05f, base = c.y + r * 0.44f;
        dl->PathLineTo(ImVec2(c.x - hw, shoulder));       // the head: an ogive to the point
        dl->PathBezierCubicCurveTo(ImVec2(c.x - hw, tip + r * 0.16f), ImVec2(c.x - hw * 0.35f, tip),
                                   ImVec2(c.x, tip), 12);
        dl->PathBezierCubicCurveTo(ImVec2(c.x + hw * 0.35f, tip), ImVec2(c.x + hw, tip + r * 0.16f),
                                   ImVec2(c.x + hw, shoulder), 12);
        dl->PathFillConvex(col);
        dl->AddRectFilled(ImVec2(c.x - hw, caseTop), ImVec2(c.x + hw, base - r * 0.07f), col);   // the case
        dl->AddRectFilled(ImVec2(c.x - hw * 1.25f, base - r * 0.05f), ImVec2(c.x + hw * 1.25f, base), col);   // its rim
        break;
    }
    case G_AIM: {   // a crosshair: aim
        dl->AddCircle(c, r * 0.36f, col, 32, t);
        const float in = r * 0.14f, out = r * 0.56f;
        dl->AddLine(ImVec2(c.x - out, c.y), ImVec2(c.x - in, c.y), col, t);
        dl->AddLine(ImVec2(c.x + in, c.y), ImVec2(c.x + out, c.y), col, t);
        dl->AddLine(ImVec2(c.x, c.y - out), ImVec2(c.x, c.y - in), col, t);
        dl->AddLine(ImVec2(c.x, c.y + in), ImVec2(c.x, c.y + out), col, t);
        break;
    }
    case G_USE: {   // a door with its handle: open, activate
        const float w = r * 0.30f, h = r * 0.46f;
        dl->AddRect(ImVec2(c.x - w, c.y - h), ImVec2(c.x + w, c.y + h), col, 0.0f, 0, t);
        dl->AddCircleFilled(ImVec2(c.x + w * 0.5f, c.y + h * 0.05f), t * 0.9f, col);
        break;
    }
    case G_RELOAD: {   // a circular arrow
        const float rr = r * 0.40f;
        dl->PathArcTo(c, rr, -kPi * 0.35f, kPi * 1.35f, 28);
        dl->PathStroke(col, 0, t);
        const float a = -kPi * 0.35f;
        const ImVec2 tip(c.x + cosf(a) * rr, c.y + sinf(a) * rr);
        const float s = r * 0.20f;
        dl->AddTriangleFilled(ImVec2(tip.x - s, tip.y - s * 0.55f), ImVec2(tip.x + s * 0.45f, tip.y - s * 0.75f),
                              ImVec2(tip.x + s * 0.05f, tip.y + s * 0.6f), col);
        break;
    }
    case G_CROUCH: {   // down to the floor
        const float h = r * 0.42f, w = r * 0.30f;
        dl->AddLine(ImVec2(c.x, c.y - h), ImVec2(c.x, c.y + h * 0.45f), col, t);
        dl->AddLine(ImVec2(c.x - w, c.y + h * 0.45f - w), ImVec2(c.x, c.y + h * 0.45f), col, t);
        dl->AddLine(ImVec2(c.x + w, c.y + h * 0.45f - w), ImVec2(c.x, c.y + h * 0.45f), col, t);
        dl->AddLine(ImVec2(c.x - w * 1.3f, c.y + h), ImVec2(c.x + w * 1.3f, c.y + h), col, t);
        break;
    }
    case G_WEAPON: {   // swap: an arrow each way
        const float w = r * 0.44f, y1 = c.y - r * 0.17f, y2 = c.y + r * 0.17f, s = r * 0.15f;
        dl->AddLine(ImVec2(c.x - w, y1), ImVec2(c.x + w, y1), col, t);
        dl->AddLine(ImVec2(c.x + w - s, y1 - s), ImVec2(c.x + w, y1), col, t);
        dl->AddLine(ImVec2(c.x + w - s, y1 + s), ImVec2(c.x + w, y1), col, t);
        dl->AddLine(ImVec2(c.x - w, y2), ImVec2(c.x + w, y2), col, t);
        dl->AddLine(ImVec2(c.x - w + s, y2 - s), ImVec2(c.x - w, y2), col, t);
        dl->AddLine(ImVec2(c.x - w + s, y2 + s), ImVec2(c.x - w, y2), col, t);
        break;
    }
    case G_START: {   // the menu's three lines
        const float w = r * 0.45f, s = r * 0.28f;
        for (int k = -1; k <= 1; k++) {
            dl->AddLine(ImVec2(c.x - w, c.y + k * s), ImVec2(c.x + w, c.y + k * s), col, t);
        }
        break;
    }
    }
}

static void drawButton(ImDrawList *dl, int i, float a)
{
    float x, y, r;
    buttonCentre(i, &x, &y, &r);
    const bool held = buttonHeld(i);
    const ImVec2 c(x, y);
    const float line = 2.5f * s_ptPx;
    if (held) dl->AddCircleFilled(c, r, white(a * 0.8f), 48);
    dl->AddCircle(c, r, white(held ? a * 2.0f : a), 48, line);
    drawGlyph(dl, kButtons[i == kLeftFire ? 0 : i].glyph, c, r, white(a * 2.2f));
}

/* Over the game's frame in the window, after flat_render.cpp's blit. */
static ImU32 yellow(float a) { return IM_COL32(255, 217, 102, (int)(fminf(a, 1.0f) * 255.0f)); }

/* The editor: a scrim over the paused game, every button in yellow (the one
 * held thicker), and the bar. Midas: what is grabbable must be unmistakable,
 * whatever the buttons' own opacity. */
static void drawEditor(ImDrawList *dl)
{
    dl->AddRectFilled(ImVec2(0, 0), ImVec2((float)s_winW, (float)s_winH), IM_COL32(0, 0, 0, 140));
    for (int i = 0; i <= kCount; i++) {
        float x, y, r;
        buttonCentre(i, &x, &y, &r);
        const bool sel = i == s_editBtn;
        const float a = i == kLeftFire ? 0.5f : 0.95f;   // only there while aiming
        if (sel) dl->AddCircleFilled(ImVec2(x, y), r, yellow(0.25f), 48);
        dl->AddCircle(ImVec2(x, y), r, yellow(a), 48, (sel ? 4.0f : 2.5f) * s_ptPx);
        drawGlyph(dl, kButtons[i == kLeftFire ? 0 : i].glyph, ImVec2(x, y), r, white(a));
    }
    ImFont *font = ImGui::GetFont();
    const float size = 20.0f * s_ptPx;
    for (int b = 0; b < EB_COUNT; b++) {
        float x, y, w, h;
        editBarRect(b, &x, &y, &w, &h);
        dl->AddRectFilled(ImVec2(x, y), ImVec2(x + w, y + h), b == EB_DONE ? IM_COL32(60, 120, 200, 230) : IM_COL32(50, 50, 50, 230),
                          h * 0.5f);
        const ImVec2 ts = font->CalcTextSizeA(size, 1e9f, 0.0f, kEditBar[b]);
        dl->AddText(font, size, ImVec2(x + (w - ts.x) * 0.5f, y + (h - ts.y) * 0.5f), white(1.0f), kEditBar[b]);
    }
    const char *hint = "Drag a button to move it. Pinch to resize it.";
    float bx, by, bw, bh;
    editBarRect(0, &bx, &by, &bw, &bh);
    const float hs = 15.0f * s_ptPx;
    const ImVec2 ts = font->CalcTextSizeA(hs, 1e9f, 0.0f, hint);
    dl->AddText(font, hs, ImVec2(((float)s_winW - ts.x) * 0.5f, by + bh + 8.0f * s_ptPx), white(0.85f), hint);
}

extern "C" void flatTouchDraw(int winW, int winH)
{
    if (winW <= 0 || winH <= 0) return;
    if (!s_editing && (!s_shown || gevrNativePauseOpen())) return;
    if (winW != s_winW || winH != s_winH) touchMeasure();

    ImGuiContext *prev = ImGui::GetCurrentContext();
    if (!s_imgui) {
        s_imgui = ImGui::CreateContext();
        ImGui::SetCurrentContext(s_imgui);
        ImGui::GetIO().IniFilename = nullptr;
#if defined(ANDROID) || defined(GEVR_GLES)
        ImGui_ImplOpenGL3_Init("#version 300 es");
#else
        ImGui_ImplOpenGL3_Init("#version 330");
#endif
        sysLogPrintf(LOG_NOTE, "touch: overlay up, %dx%d px, %.2f px a point", winW, winH, s_ptPx);
    }
    ImGui::SetCurrentContext(s_imgui);
    ImGuiIO &io = ImGui::GetIO();
    io.DisplaySize = ImVec2((float)winW, (float)winH);
    io.DeltaTime = 1.0f / 60.0f;
    ImGui_ImplOpenGL3_NewFrame();
    ImGui::NewFrame();
    ImDrawList *dl = ImGui::GetForegroundDrawList();

    if (s_editing) {
        drawEditor(dl);
        ImGui::Render();
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glViewport(0, 0, winW, winH);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        ImGui::SetCurrentContext(prev);
        return;
    }
    for (int i = 0; i < kCount; i++) {
        if (buttonVisible(i)) drawButton(dl, i, kOpacity);
    }
    if (buttonVisible(kLeftFire)) drawButton(dl, kLeftFire, kOpacity);

    // No resting stick: it appears where the thumb lands.
    if (s_stickFinger >= 0) {
        const float r = kStickRadius * s_ptPx;
        const ImVec2 o(s_stickOX, s_stickOY);
        dl->AddCircle(o, r, white(kOpacity * 1.6f), 48, 2.5f * s_ptPx);
        float dx = s_stickX - s_stickOX, dy = s_stickY - s_stickOY;
        const float len = sqrtf(dx * dx + dy * dy);
        if (len > r) {
            dx *= r / len;
            dy *= r / len;
        }
        const ImVec2 k(s_stickOX + dx, s_stickOY + dy);
        dl->AddCircleFilled(k, kStickKnobRadius * s_ptPx, white(kOpacity * 0.8f), 32);
        dl->AddCircle(k, kStickKnobRadius * s_ptPx, white(kOpacity * 2.0f), 32, 2.5f * s_ptPx);
    }

    ImGui::Render();
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, winW, winH);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    ImGui::SetCurrentContext(prev);
}

#endif   // GEVR_FLAT
