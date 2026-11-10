/*
 * The Quest launcher (port/vr/vr_launcher.cpp) in a flat game's window: the
 * PC's flat game and the Android phone/tablet APK (docs/pc-platforms.md,
 * docs/mobile-platforms.md).
 *
 * The launcher draws itself with ImGui into a texture and hands it to the
 * headset's screen quad (vr_screen_present_tex2d); here that texture goes into
 * the window. Its input on the Quest is the Touch pointer and sticks; here it
 * is the mouse (mapped from the window onto the launcher's canvas; on Android
 * a touch is SDL's mouse), the keyboard (arrows, Return, Escape, Tab) and a
 * pad. The file pickers, restart and services are each platform's own
 * (port/pc/pc_launcher.cpp on PC, Java on Android).
 */
#ifdef GEVR_FLAT

#include <cfloat>
#include <cmath>
#include <SDL.h>

#ifdef ANDROID
#include <GLES3/gl3.h>
#else
#include "../../fast3d/glad/glad.h"
#endif
#include "../../vr/imgui/imgui.h"
#include <PR/ultratypes.h>

#ifdef GEVR_PC
#include "../../pc/pc_testinput.h"
extern "C" void pc_write_shot(const char *name, int w, int h);   // flat_render.cpp
#else
/* no test scripts off PC */
static inline int pcTestInputActive(void) { return 0; }
static inline void pcTestInputFrame(s32 *, s32 *, u32 *, s32 *) { }
static inline int pcTestInputPointer(s32 *, s32 *) { return 0; }
static inline int pcTestInputLeftDown(void) { return 0; }
static inline int pcTestInputKey(int) { return 0; }
#endif

extern SDL_Window *wnd;   // flat_window.cpp

/* Where the canvas was last drawn in the window: the mouse maps through it. */
static int s_rectX, s_rectY, s_rectW = 1, s_rectH = 1;
static float s_wheel;
/* Button presses as they happen: a tap on a touch screen (SDL's mouse on
 * Android) goes down and up between two frames, which a once-a-frame look at
 * the button never sees. ImGui's input queue takes both in order. */
static struct { Uint8 button; bool down; int x, y; } s_clicks[16];
static int s_clickCount;

static int SDLCALL flatLauncherWheelWatch(void *, SDL_Event *e)
{
    if (e->type == SDL_MOUSEWHEEL) {
        s_wheel += (float)e->wheel.y;
    } else if ((e->type == SDL_MOUSEBUTTONDOWN || e->type == SDL_MOUSEBUTTONUP) && s_clickCount < 16
               && (e->button.button == SDL_BUTTON_LEFT || e->button.button == SDL_BUTTON_RIGHT)) {
        s_clicks[s_clickCount].button = e->button.button;
        s_clicks[s_clickCount].down = e->type == SDL_MOUSEBUTTONDOWN;
        s_clicks[s_clickCount].x = e->button.x;
        s_clicks[s_clickCount].y = e->button.y;
        s_clickCount++;
    }
    return 1;
}

extern "C" void flatSwapWindow(void);   // flat_window.cpp: a swap that survives a new surface

/* Set while the in-game panel is drawn inside the game's frame (flat_render.cpp):
 * the game swaps the window itself. */
bool g_flatPresentNoSwap = false;

/* The launcher's texture (w x h, its canvas) into the window, letterboxed, and shown. */
extern "C" bool vr_screen_present_tex2d(unsigned int srcTex, int w, int h)
{
    static GLuint readFbo;
    int winW = 0, winH = 0;

    if (!wnd || !srcTex || w <= 0 || h <= 0) return false;
    SDL_GL_GetDrawableSize(wnd, &winW, &winH);
    if (winW <= 0 || winH <= 0) return false;
    int dw = winW, dh = (int)((long long)winW * h / w);
    if (dh > winH) {
        dh = winH;
        dw = (int)((long long)winH * w / h);
    }
    const int dx = (winW - dw) / 2, dy = (winH - dh) / 2;
    s_rectX = dx;
    s_rectY = dy;
    s_rectW = dw > 0 ? dw : 1;
    s_rectH = dh > 0 ? dh : 1;

    if (!readFbo) glGenFramebuffers(1, &readFbo);
    glDisable(GL_SCISSOR_TEST);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, readFbo);
    glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, srcTex, 0);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
    glViewport(0, 0, winW, winH);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glBlitFramebuffer(0, 0, w, h, dx, dy, dx + dw, dy + dh, GL_COLOR_BUFFER_BIT, GL_LINEAR);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
#ifdef GEVR_PC
    if (const char *shot = pcTestInputTakeShot()) {
        pc_write_shot(shot, winW, winH);   // flat_render.cpp
    }
#endif
    if (!g_flatPresentNoSwap) flatSwapWindow();
    return true;
}

/*
 * The in-game panel's input (vr_pause_menu.cpp): the launcher's own, the
 * mouse or a finger on the canvas, the keyboard and a pad. The headset's
 * version points the controllers instead (vr_launcher.cpp).
 */
void flatLauncherInit(ImGuiIO &io);
void flatLauncherFeedInput(ImGuiIO &io);
extern "C" void gevrFeedPauseInput(int opening)
{
    static bool watching = false;
    ImGuiIO &io = ImGui::GetIO();
    if (!watching) {   // the launcher's click queue: a tap's down and up can share a frame
        flatLauncherInit(io);
        watching = true;
    }
    if (opening) s_clickCount = 0;   // presses from before it opened
    flatLauncherFeedInput(io);
}

/* The launcher's canvas is wider than the game's screen when the friends
 * sidebar shows; the headset widens its quad. The window letterboxes anyway. */
extern "C" void vr_screen_set_width_scale(float scale) { (void)scale; }

void flatLauncherInit(ImGuiIO &io)
{
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.MouseDrawCursor = false;
    SDL_AddEventWatch(flatLauncherWheelWatch, nullptr);
}

void flatLauncherShutdown(void)
{
    SDL_DelEventWatch(flatLauncherWheelWatch, nullptr);
}

/* This frame's mouse, keys and pad for ImGui. */
void flatLauncherFeedInput(ImGuiIO &io)
{
    // Drain the queue (the launcher reads its input through event watches and
    // state): on Android SDL completes a pause, unbinding the context, only once
    // the app has taken its SDL_APP_DIDENTERBACKGROUND. A quit is the window's own.
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        if (e.type == SDL_QUIT) exit(0);
    }
    const bool scripted = pcTestInputActive() != 0;
    if (scripted) {
        // the game's input poll (input.c) isn't running yet: tick the script here
        s32 dx = 0, dy = 0, lock = -1;
        u32 b = 0;
        pcTestInputFrame(&dx, &dy, &b, &lock);
    }

    // the mouse, from the window onto the canvas
    int mx = 0, my = 0;
    const Uint32 buttons = SDL_GetMouseState(&mx, &my);
    int winW = 0, winH = 0, drawW = 0, drawH = 0;
    SDL_GetWindowSize(wnd, &winW, &winH);
    SDL_GL_GetDrawableSize(wnd, &drawW, &drawH);
    if (winW > 0 && winH > 0) {   // high-DPI: the drawable may be bigger than the window
        mx = mx * drawW / winW;
        my = my * drawH / winH;
    }
    const bool inside = mx >= s_rectX && mx < s_rectX + s_rectW && my >= s_rectY && my < s_rectY + s_rectH;
    const bool focused = SDL_GetMouseFocus() == wnd && !scripted;
    s32 px, py;
    if (scripted && pcTestInputPointer(&px, &py)) {
        io.AddMousePosEvent((float)px, (float)py);
        io.AddMouseButtonEvent(0, pcTestInputLeftDown() != 0);
    } else if (focused && inside) {
        io.AddMousePosEvent((float)(mx - s_rectX) * io.DisplaySize.x / (float)s_rectW,
                            (float)(my - s_rectY) * io.DisplaySize.y / (float)s_rectH);
    } else {
        io.AddMousePosEvent(-FLT_MAX, -FLT_MAX);
    }
    if (!scripted) {
        // the presses in order, each where it happened (a tap's down and up can
        // share a frame); then the button's state, for a hold or a drag
        for (int i = 0; i < s_clickCount; i++) {
            int cx = s_clicks[i].x, cy = s_clicks[i].y;
            if (winW > 0 && winH > 0) {
                cx = cx * drawW / winW;
                cy = cy * drawH / winH;
            }
            if (cx >= s_rectX && cx < s_rectX + s_rectW && cy >= s_rectY && cy < s_rectY + s_rectH) {
                io.AddMousePosEvent((float)(cx - s_rectX) * io.DisplaySize.x / (float)s_rectW,
                                    (float)(cy - s_rectY) * io.DisplaySize.y / (float)s_rectH);
            }
            io.AddMouseButtonEvent(s_clicks[i].button == SDL_BUTTON_LEFT ? 0 : 1, s_clicks[i].down);
        }
        s_clickCount = 0;
        io.AddMouseButtonEvent(0, focused && (buttons & SDL_BUTTON_LMASK) != 0);
        io.AddMouseButtonEvent(1, focused && (buttons & SDL_BUTTON_RMASK) != 0);
    }
    if (s_wheel != 0.0f) {
        io.AddMouseWheelEvent(0.0f, s_wheel);
        s_wheel = 0.0f;
    }

    // the keyboard: ImGui's own navigation keys (text goes through the launcher's keyboardWatch)
    const Uint8 *keys = SDL_GetKeyboardState(nullptr);
    auto key = [&](SDL_Scancode sc) -> bool {
        return scripted ? pcTestInputKey(sc) != 0 : (keys && keys[sc] && SDL_GetKeyboardFocus() == wnd);
    };
    io.AddKeyEvent(ImGuiKey_UpArrow, key(SDL_SCANCODE_UP));
    io.AddKeyEvent(ImGuiKey_DownArrow, key(SDL_SCANCODE_DOWN));
    io.AddKeyEvent(ImGuiKey_LeftArrow, key(SDL_SCANCODE_LEFT));
    io.AddKeyEvent(ImGuiKey_RightArrow, key(SDL_SCANCODE_RIGHT));
    io.AddKeyEvent(ImGuiKey_Tab, key(SDL_SCANCODE_TAB));
    io.AddKeyEvent(ImGuiKey_Space, key(SDL_SCANCODE_SPACE));
    io.AddKeyEvent(ImGuiKey_Escape, key(SDL_SCANCODE_ESCAPE));
    io.AddKeyEvent(ImGuiKey_LeftShift, key(SDL_SCANCODE_LSHIFT));
    io.AddKeyEvent(ImGuiKey_LeftCtrl, key(SDL_SCANCODE_LCTRL));
    if (!io.WantTextInput) {
        // Return activates (in a text box the launcher's queue types it)
        io.AddKeyEvent(ImGuiKey_Enter, key(SDL_SCANCODE_RETURN));
    }
    bool navKey = key(SDL_SCANCODE_UP) || key(SDL_SCANCODE_DOWN) || key(SDL_SCANCODE_LEFT) || key(SDL_SCANCODE_RIGHT)
               || key(SDL_SCANCODE_TAB) || key(SDL_SCANCODE_SPACE) || key(SDL_SCANCODE_RETURN);

    // the first pad, as the Quest's sticks and A/B drive the launcher
    SDL_GameController *pad = nullptr;
    for (int i = 0; i < SDL_NumJoysticks() && !pad; i++) {
        if (SDL_IsGameController(i)) pad = SDL_GameControllerOpen(i);   // the one input.c holds, by reference
    }
    if (pad) {
        const float t = 0.5f;
        const float lx = SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_LEFTX) / 32767.0f;
        const float ly = SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_LEFTY) / 32767.0f;
        io.AddKeyEvent(ImGuiKey_GamepadDpadUp, SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_DPAD_UP) || ly < -t);
        io.AddKeyEvent(ImGuiKey_GamepadDpadDown, SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_DPAD_DOWN) || ly > t);
        io.AddKeyEvent(ImGuiKey_GamepadDpadLeft, SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_DPAD_LEFT) || lx < -t);
        io.AddKeyEvent(ImGuiKey_GamepadDpadRight, SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_DPAD_RIGHT) || lx > t);
        io.AddKeyEvent(ImGuiKey_GamepadFaceDown, SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_A));
        io.AddKeyEvent(ImGuiKey_GamepadFaceRight, SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_B));
        io.AddKeyEvent(ImGuiKey_GamepadL1, SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_LEFTSHOULDER));
        io.AddKeyEvent(ImGuiKey_GamepadR1, SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_RIGHTSHOULDER));
        navKey = navKey || fabsf(lx) > t || fabsf(ly) > t || SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_A)
              || SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_DPAD_UP) || SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_DPAD_DOWN);
        SDL_GameControllerClose(pad);   // drop this reference; input.c keeps its own
    }
    // A key or the pad shows ImGui's navigation cursor, so the same press that
    // reaches START activates it (with the cursor hidden, ImGui spends the first
    // press on showing it); the launcher puts the focus on START at open.
    if (navKey) {
        ImGui::SetNavCursorVisible(true);
    }
}

/* The Quest's thumbstick click (hand 0 left, 1 right): a pad's stick clicks.
 * Both together unlock the launcher's "Send debug log", as on the Quest.
 * Without a pad, Ctrl+Shift+D held clicks both (unlabelled, like the Quest's
 * gesture): released first, then held, as the sticks are. */
bool flatLauncherStickClick(int hand)
{
    const Uint8 *keys = SDL_GetKeyboardState(nullptr);
    const bool scripted = pcTestInputActive() != 0;
    auto held = [&](SDL_Scancode sc) -> bool {
        return scripted ? pcTestInputKey(sc) != 0 : (keys && keys[sc] && SDL_GetKeyboardFocus() == wnd);
    };
    if ((held(SDL_SCANCODE_LCTRL) || held(SDL_SCANCODE_RCTRL)) && (held(SDL_SCANCODE_LSHIFT) || held(SDL_SCANCODE_RSHIFT))
        && held(SDL_SCANCODE_D)) {
        return true;
    }
    bool down = false;
    for (int i = 0; i < SDL_NumJoysticks() && !down; i++) {
        if (!SDL_IsGameController(i)) continue;
        SDL_GameController *pad = SDL_GameControllerOpen(i);
        if (!pad) continue;
        down = SDL_GameControllerGetButton(pad, hand ? SDL_CONTROLLER_BUTTON_RIGHTSTICK : SDL_CONTROLLER_BUTTON_LEFTSTICK) != 0;
        SDL_GameControllerClose(pad);   // input.c keeps its own reference
    }
    return down;
}

#endif   /* GEVR_FLAT */
