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
#include <string.h>
#ifdef ANDROID
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
#ifdef GEVR_MOBILE
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

static void buttonCentre(int i, float *x, float *y, float *r)
{
    const TouchButton &b = kButtons[i == kLeftFire ? 0 : i];
    *x = b.unitX * (float)s_winW;
    *y = b.unitY * (float)s_winH;
    *r = b.radius * s_ptPx;
    if (i == kLeftFire) *x = (float)s_winW - *x;   // FIRE, mirrored for the left thumb
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

static void fingerDown(const SDL_TouchFingerEvent &e)
{
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

/* Called for each SDL event the window drains (flat_window.cpp), on the game's thread. */
extern "C" void flatTouchEvent(const SDL_Event *e)
{
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
        if (s_shown) fingerUp(e->tfinger);
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
    if (s_menu != (menu != 0)) {
        s_menu = menu != 0;
        releaseAll();   // the buttons under the fingers changed
        s_tapped = 0;
    }
    *sx = *sy = 0.0f;
    if (!s_shown || gevrNativePauseOpen()) return 0;
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
    const float deg = 57.29578f * dt;
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
    case G_FIRE: {   // a crosshair
        dl->AddCircle(c, r * 0.36f, col, 32, t);
        const float in = r * 0.14f, out = r * 0.56f;
        dl->AddLine(ImVec2(c.x - out, c.y), ImVec2(c.x - in, c.y), col, t);
        dl->AddLine(ImVec2(c.x + in, c.y), ImVec2(c.x + out, c.y), col, t);
        dl->AddLine(ImVec2(c.x, c.y - out), ImVec2(c.x, c.y - in), col, t);
        dl->AddLine(ImVec2(c.x, c.y + in), ImVec2(c.x, c.y + out), col, t);
        break;
    }
    case G_AIM:   // a target
        dl->AddCircle(c, r * 0.46f, col, 32, t);
        dl->AddCircle(c, r * 0.24f, col, 24, t);
        dl->AddCircleFilled(c, r * 0.07f + t * 0.5f, col);
        break;
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
extern "C" void flatTouchDraw(int winW, int winH)
{
    if (!s_shown || gevrNativePauseOpen() || winW <= 0 || winH <= 0) return;
    if (winW != s_winW || winH != s_winH) touchMeasure();

    ImGuiContext *prev = ImGui::GetCurrentContext();
    if (!s_imgui) {
        s_imgui = ImGui::CreateContext();
        ImGui::SetCurrentContext(s_imgui);
        ImGui::GetIO().IniFilename = nullptr;
#ifdef ANDROID
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
