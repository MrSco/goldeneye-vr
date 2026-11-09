/*
 * The Quest launcher (port/vr/vr_launcher.cpp) in the PC window
 * (docs/pc-platforms.md, P1).
 *
 * The launcher draws itself with ImGui into a texture and hands it to the
 * headset's screen quad (vr_screen_present_tex2d); here that texture goes into
 * the window. Its input on the Quest is the Touch pointer and sticks; here it
 * is the mouse (mapped from the window onto the launcher's canvas), the
 * keyboard (arrows, Return, Escape, Tab) and a pad. What the Quest asks of
 * Java goes through pc_services.cpp, and the file pickers are Windows' own.
 */

#include <string>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <SDL.h>

#include "../fast3d/glad/glad.h"
#include "../vr/imgui/imgui.h"
#include "pc_testinput.h"

#ifdef _WIN32
#include <windows.h>
#include <commdlg.h>
#else
#include <filesystem>
#include <string>
#include <vector>
#include <spawn.h>      // pcRestartSelf
#include <sys/wait.h>   // pclose's status
extern char **environ;
#endif

extern SDL_Window *wnd;   // pc_window.cpp

extern "C" {
const char *fsFullPath(const char *relPath);   // port/src/fs.c
void sysLogPrintf(int level, const char *fmt, ...);
}

#ifdef GEVR_PC_FLAT   /* the window is the launcher's screen; PCVR shows it on the headset's quad */
/* Where the canvas was last drawn in the window: the mouse maps through it. */
static int s_rectX, s_rectY, s_rectW = 1, s_rectH = 1;
static float s_wheel;

static int SDLCALL pcLauncherWheelWatch(void *, SDL_Event *e)
{
    if (e->type == SDL_MOUSEWHEEL) {
        s_wheel += (float)e->wheel.y;
    }
    return 1;
}

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
    if (const char *shot = pcTestInputTakeShot()) {
        extern void pc_write_shot(const char *name, int w, int h);   // pc_vr_render.cpp
        pc_write_shot(shot, winW, winH);
    }
    SDL_GL_SwapWindow(wnd);
    return true;
}

/* The launcher's canvas is wider than the game's screen when the friends
 * sidebar shows; the headset widens its quad. The window letterboxes anyway. */
extern "C" void vr_screen_set_width_scale(float scale) { (void)scale; }

void pcLauncherInit(ImGuiIO &io)
{
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.MouseDrawCursor = false;
    SDL_AddEventWatch(pcLauncherWheelWatch, nullptr);
}

void pcLauncherShutdown(void)
{
    SDL_DelEventWatch(pcLauncherWheelWatch, nullptr);
}

/* This frame's mouse, keys and pad for ImGui. */
void pcLauncherFeedInput(ImGuiIO &io)
{
    SDL_PumpEvents();
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
bool pcLauncherStickClick(int hand)
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

#endif   /* GEVR_PC_FLAT */

#ifdef _WIN32
enum PickResult { PICK_CHOSEN, PICK_CANCELLED, PICK_FAILED };

/*
 * Windows' Open dialog: the file chosen, in *file. A test script
 * (pc_testinput.c "pick") answers instead, so a scripted run never leaves a
 * modal dialog on the desktop; with no answer waiting the pick is cancelled.
 */
static PickResult pcChooseFile(const wchar_t *filter, const wchar_t *title, std::wstring *file)
{
    if (pcTestInputActive()) {
        char scripted[400];
        if (!pcTestInputTakePick(scripted, (s32)sizeof(scripted)) || !strcmp(scripted, "-")) {
            sysLogPrintf(0, "launcher: scripted file picker: cancelled");
            return PICK_CANCELLED;
        }
        wchar_t w[400];
        MultiByteToWideChar(CP_UTF8, 0, scripted, -1, w, 400);
        *file = w;
        sysLogPrintf(0, "launcher: scripted file picker: %s", scripted);
        return PICK_CHOSEN;
    }
    wchar_t buf[MAX_PATH * 4] = L"";
    OPENFILENAMEW ofn;
    memset(&ofn, 0, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = GetActiveWindow();
    ofn.lpstrFilter = filter;
    ofn.lpstrFile = buf;
    ofn.nMaxFile = MAX_PATH * 4;
    ofn.lpstrTitle = title;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (!GetOpenFileNameW(&ofn)) {
        // a closed dialog is no error; one that could not open is (CommDlgExtendedError)
        return CommDlgExtendedError() ? PICK_FAILED : PICK_CANCELLED;
    }
    *file = buf;
    return PICK_CHOSEN;
}

static std::string pcNarrow(const std::wstring &w)
{
    const int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s((size_t)(n > 0 ? n : 0), '\0');
    if (n > 0) WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), &s[0], n, nullptr, nullptr);
    return s;
}
#else
enum PickResult { PICK_CHOSEN, PICK_CANCELLED, PICK_FAILED };

static std::string pcShellQuote(const std::string &s)
{
    std::string q = "'";
    for (char c : s) q += c == '\'' ? std::string("'\\''") : std::string(1, c);
    return q + "'";
}

/*
 * Linux: the desktop's own open dialog, zenity (GNOME and most others) or
 * kdialog (KDE), whichever is installed. patterns: "*.z64 *.v64 *.n64", or
 * "" for any file. A test script answers as on Windows.
 */
static PickResult pcChooseFile(const char *title, const char *patterns, std::string *file)
{
    if (pcTestInputActive()) {
        char scripted[400];
        if (!pcTestInputTakePick(scripted, (s32)sizeof(scripted)) || !strcmp(scripted, "-")) {
            sysLogPrintf(0, "launcher: scripted file picker: cancelled");
            return PICK_CANCELLED;
        }
        *file = scripted;
        sysLogPrintf(0, "launcher: scripted file picker: %s", scripted);
        return PICK_CHOSEN;
    }
    const std::string zenity = "zenity --file-selection --title=" + pcShellQuote(title)
        + (patterns[0] ? " --file-filter=" + pcShellQuote(patterns) : std::string()) + " 2>/dev/null";
    const std::string kdialog = "kdialog --title " + pcShellQuote(title) + " --getopenfilename . "
        + pcShellQuote(patterns[0] ? patterns : "*") + " 2>/dev/null";
    for (const std::string &cmd : { zenity, kdialog }) {
        FILE *p = popen(cmd.c_str(), "r");
        if (!p) continue;
        char line[4096] = "";
        const bool read = fgets(line, sizeof(line), p) != nullptr;
        const int status = pclose(p);
        if (WIFEXITED(status) && WEXITSTATUS(status) == 127) continue;   // not installed
        std::string chosen = read ? line : "";
        while (!chosen.empty() && (chosen.back() == '\n' || chosen.back() == '\r')) chosen.pop_back();
        if (WIFEXITED(status) && WEXITSTATUS(status) == 0 && !chosen.empty()) {
            *file = chosen;
            return PICK_CHOSEN;
        }
        return PICK_CANCELLED;
    }
    sysLogPrintf(1, "launcher: no file dialog (install zenity or kdialog)");
    return PICK_FAILED;
}
#endif

void pcModsImportOwn(const char *id, const std::string &path);   // pc_mods.cpp
void pcModsApplyCe(const std::string &path);

/*
 * The Quest's file pickers (MainActivity.openRomPicker and friends) copy the
 * chosen file to data/picked*.z64, which the launcher adopts. Here Windows'
 * Open dialog does the same, at once.
 *
 * The Mods page's XBLA / Bean (MainActivity.openXblaPicker / openCePicker):
 * nothing is copied; the chosen file goes to the Mods page's import
 * (ModManager.importOwn / applyCe, pc_mods.cpp), whose progress the Bean row
 * shows. As on the Quest, true means the picker opened, chosen or not.
 */
bool pcOpenPicker(const char *method)
{
#ifdef _WIN32
    const bool xbla = !strcmp(method, "openXblaPicker"), ce = !strcmp(method, "openCePicker");
    if (xbla || ce) {
        std::wstring file;
        const PickResult r = xbla
            ? pcChooseFile(L"GoldenEye XBLA (.7z, .zip or the Xbox 360 package)\0*.*\0", L"Choose your copy of GoldenEye XBLA", &file)
            : pcChooseFile(L"Community Edition updater (CommunityEditionUpdaterV6.zip)\0*.zip\0All files\0*.*\0",
                           L"Choose the GoldenEye XBLA Community Edition updater", &file);
        if (r == PICK_CHOSEN) {
            sysLogPrintf(0, "launcher: %s: a file chosen", xbla ? "XBLA copy" : "Community Edition");
            if (xbla) pcModsImportOwn("ge007-bean", pcNarrow(file));
            else pcModsApplyCe(pcNarrow(file));
        }
        return r != PICK_FAILED;
    }
#else
    const bool xbla = !strcmp(method, "openXblaPicker"), ce = !strcmp(method, "openCePicker");
    if (xbla || ce) {
        std::string file;
        const PickResult r = xbla
            ? pcChooseFile("Choose your copy of GoldenEye XBLA", "", &file)
            : pcChooseFile("Choose the GoldenEye XBLA Community Edition updater", "*.zip", &file);
        if (r == PICK_CHOSEN) {
            sysLogPrintf(0, "launcher: %s: a file chosen", xbla ? "XBLA copy" : "Community Edition");
            if (xbla) pcModsImportOwn("ge007-bean", file);
            else pcModsApplyCe(file);
        }
        return r != PICK_FAILED;
    }
#endif
    const char *dest = !strcmp(method, "openRomPicker") ? "picked.z64"
                     : !strcmp(method, "openGexPicker") ? "picked-gex.z64"
                     : !strcmp(method, "openGoldfingerPicker") ? "picked-goldfinger.z64"
                     : !strcmp(method, "openTndPicker") ? "picked-tnd.z64"
                     : !strcmp(method, "openMirrorPicker") ? "picked-mirror.z64" : nullptr;
    if (!dest) return false;
#ifdef _WIN32
    std::wstring file;
    if (pcChooseFile(L"N64 ROMs (*.z64;*.v64;*.n64)\0*.z64;*.v64;*.n64\0All files\0*.*\0", L"Choose the ROM", &file) != PICK_CHOSEN) {
        return false;
    }
    char to[MAX_PATH * 2];
    snprintf(to, sizeof(to), "%s", fsFullPath(dest));
    wchar_t wto[MAX_PATH * 2];
    MultiByteToWideChar(CP_UTF8, 0, to, -1, wto, MAX_PATH * 2);
    const bool ok = CopyFileW(file.c_str(), wto, FALSE) != 0;
    sysLogPrintf(ok ? 0 : 1, "launcher: picked a file for %s (%s)", dest, ok ? "copied" : "copy failed");
    return ok;
#else
    std::string file;
    if (pcChooseFile("Choose the ROM", "*.z64 *.v64 *.n64", &file) != PICK_CHOSEN) {
        return false;
    }
    std::error_code ec;
    std::filesystem::copy_file(file, fsFullPath(dest), std::filesystem::copy_options::overwrite_existing, ec);
    sysLogPrintf(ec ? 1 : 0, "launcher: picked a file for %s (%s)", dest, ec ? "copy failed" : "copied");
    return !ec;
#endif
}

void pcReportCleanExit(void);         // pc_reports.cpp
extern "C" void gevrLogClose(void);   // port/src/system.c

/* The Quest restarts its activity into the launcher; the PC runs itself again. */
void pcRestartSelf(void)
{
    pcReportCleanExit();   // before the new run looks for this one's marker (MainActivity.restartToLauncher)
    gevrLogClose();        // and before it rotates gevr.log to gevr.prev.log
#ifdef _WIN32
    STARTUPINFOW si;
    PROCESS_INFORMATION pi;
    memset(&si, 0, sizeof(si));
    si.cb = sizeof(si);
    memset(&pi, 0, sizeof(pi));
    if (CreateProcessW(nullptr, GetCommandLineW(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi)) {
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
    }
#else
    // the same command line again (/proc/self/cmdline), started before this one exits
    std::string cmdline;
    if (FILE *f = fopen("/proc/self/cmdline", "rb")) {
        char buf[4096];
        size_t n;
        while ((n = fread(buf, 1, sizeof(buf), f)) > 0) cmdline.append(buf, n);
        fclose(f);
    }
    std::vector<char *> argv;
    for (size_t i = 0; i < cmdline.size(); i += strlen(&cmdline[i]) + 1) argv.push_back(&cmdline[i]);
    argv.push_back(nullptr);
    pid_t pid;
    if (argv.size() > 1) posix_spawn(&pid, "/proc/self/exe", nullptr, nullptr, argv.data(), environ);
#endif
    exit(0);
}
