/*
 * GoldenEye VR launcher, inside VR.
 *
 * Shown on the virtual screen before the game boots (port/src/main.c calls
 * gevrLauncherRun after inputInit and before romdataInit, so a ROM chosen here
 * is the one the game loads). The model is VirtualBoyGo's: a vr_only app keeps
 * its options inside VR; a 2D launcher activity leaves Quest's shell waiting in
 * the loading space for an XR session (HANDOFF 52).
 *
 * What it offers, and where it lands:
 *  - the ROM in use (the first of gevr_romload.c's names that exists under the
 *    data directory) with its header check, and any other GoldenEye ROMs found
 *    in the data directory, /sdcard/GEVR and /sdcard/Download; picking one
 *    copies it over data/ge.z64;
 *  - Stereo VR or the flat screen (VrPlayMode), right-stick turning, smooth or
 *    snap 30/45/90 (VrUseSnapTurn), the comfort vignette (VrComfortVignette);
 *    saved to goldeneye-vr.ini with the game's own vrSettingsSave.
 *
 * Drawn with the vendored Dear ImGui (port/vr/imgui, OpenGL3 backend on GLES 3)
 * into a 2D texture that vr_screen_present_tex2d hands to the screen quad; the
 * XR frames come from the engine shim's own pump (gevrVrPumpBegin/End), which
 * also brings the session up and loads the ini. Either thumbstick moves between
 * items (ImGui gamepad navigation), A or a trigger selects, B backs out; Start
 * has the focus from the first frame, so A alone starts the game.
 */
#include <cmath>
#include <cstdio>
#include <climits>
#include <cstdlib>
#include <cfloat>
#include <cstring>
#include <string>
#include <vector>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

#include <SDL.h>
#include <SDL_system.h>
#include <jni.h>
#include <GLES3/gl3.h>

#include "imgui/imgui.h"
#include "imgui/imgui_impl_opengl3.h"
#include "vr_input.h"
#include "vr_log.h"
#include "vr_screen.h"
#include "vr_settings.h"

extern "C" {
int gevrVrPumpBegin(void);            // port/src/gevr_engine_shim.c
void gevrVrPumpEnd(void);
const char *fsFullPath(const char *relPath);  // port/src/fs.c
extern const char gevrBuildId[];              // generated, port/cmake/buildid.cmake
void vrSettingsSave(void);            // vr_settings.cpp
}
bool vr_begin_eye_render();           // vr_openxr.cpp
void vr_end_eye_render();

namespace {

const int kTexW = 1280;
const int kTexH = 960;
const long kRomSize = 12582912;       // every retail GoldenEye cartridge

struct RomInfo {
    std::string path;
    std::string status;
    bool good = false;
};

// The same checks as gevr_romload.c: size, header name, NTSC-U cartridge id.
// .z64 byte order is checked; .v64/.n64 are accepted by the loader and
// reported here by size and extension alone.
RomInfo probeRom(const std::string &path)
{
    RomInfo r;
    r.path = path;
    FILE *f = fopen(path.c_str(), "rb");
    if (!f) {
        r.status = "cannot be read";
        return r;
    }
    unsigned char hdr[0x40] = {};
    size_t n = fread(hdr, 1, sizeof(hdr), f);
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fclose(f);

    if (size != kRomSize) {
        r.status = "not a 12 MB GoldenEye ROM (" + std::to_string(size / 1024) + " KB)";
        return r;
    }
    if (n == sizeof(hdr) && hdr[0] == 0x80 && hdr[1] == 0x37) {
        if (memcmp(hdr + 0x20, "GOLDENEYE", 9) != 0) {
            r.status = "header name is not GOLDENEYE";
            return r;
        }
        if (memcmp(hdr + 0x3b, "NGEE", 4) != 0) {
            r.status = "not the USA (NGEE) cartridge";
            return r;
        }
        r.status = "GoldenEye 007 (USA) - OK";
        r.good = true;
        return r;
    }
    r.status = "12 MB, byte-swapped dump (the loader converts it)";
    r.good = true;
    return r;
}

// The data directory is the working directory (fsFullPath gives "./ge.z64");
// show and compare absolute paths.
std::string absPath(const std::string &p)
{
    char buf[PATH_MAX];
    return realpath(p.c_str(), buf) ? std::string(buf) : p;
}

std::string dataDir()
{
    return absPath(fsFullPath("$B"));
}

bool endsWithRomExt(const char *name)
{
    size_t l = strlen(name);
    if (l < 4) return false;
    const char *e = name + l - 4;
    return strcasecmp(e, ".z64") == 0 || strcasecmp(e, ".v64") == 0 || strcasecmp(e, ".n64") == 0;
}

std::string activeRomPath()
{
    static const char *const names[] = {
        "ge.z64", "data/ge.z64", "goldeneye.z64", "007 - GoldenEye.z64", "GoldenEye 007 (U) [!].z64",
    };
    for (const char *nm : names) {
        std::string p = fsFullPath(nm);
        struct stat st;
        if (stat(p.c_str(), &st) == 0 && st.st_size > 0) return absPath(p);
    }
    return std::string();
}

std::vector<RomInfo> findRoms(const std::string &active)
{
    std::vector<RomInfo> out;
    std::string dirs[] = {dataDir(), "/sdcard/GEVR", "/sdcard/Download"};
    for (const std::string &d : dirs) {
        DIR *dir = opendir(d.c_str());
        if (!dir) continue;
        while (struct dirent *e = readdir(dir)) {
            if (!endsWithRomExt(e->d_name)) continue;
            std::string p = d;
            if (!p.empty() && p.back() != '/') p += '/';
            p += e->d_name;
            if (absPath(p) == active) continue;
            RomInfo r = probeRom(p);
            if (r.good) out.push_back(r);
        }
        closedir(dir);
    }
    return out;
}

bool copyFile(const std::string &from, const std::string &to)
{
    FILE *in = fopen(from.c_str(), "rb");
    if (!in) return false;
    std::string tmp = to + ".part";
    FILE *o = fopen(tmp.c_str(), "wb");
    if (!o) {
        fclose(in);
        return false;
    }
    std::vector<char> buf(1 << 16);
    size_t n;
    bool ok = true;
    while ((n = fread(buf.data(), 1, buf.size(), in)) > 0) {
        if (fwrite(buf.data(), 1, n, o) != n) {
            ok = false;
            break;
        }
    }
    fclose(in);
    fclose(o);
    if (!ok || rename(tmp.c_str(), to.c_str()) != 0) {
        remove(tmp.c_str());
        return false;
    }
    return true;
}

// The PC test hook (port/src/libultra.c gevrPollInjectedInput) reaches the
// launcher too: 8000 = A, 4000 = B, a stick y of +-80 moves the focus.
struct Injected {
    unsigned mask = 0;
    int x = 0, y = 0, frames = 0;
};

void pollInjected(Injected &in)
{
    static unsigned tick;
    if (in.frames > 0) {
        in.frames--;
        return;
    }
    in.mask = 0;
    in.x = in.y = 0;
    if ((++tick % 15) != 0) return;
    const char *path = "/sdcard/Android/data/com.gevr.port/files/gevr_input.txt";
    FILE *f = fopen(path, "r");
    if (!f) return;
    int frames = 4;
    if (fscanf(f, "%x %d %d %d", &in.mask, &in.x, &in.y, &frames) >= 1) {
        in.frames = frames < 1 ? 1 : frames > 600 ? 600 : frames;
        vr_log("launcher: injecting buttons %04x stick %d,%d", in.mask, in.x, in.y);
    }
    fclose(f);
    unlink(path);
}

// The system file picker, opened by MainActivity.openRomPicker; it copies the
// chosen file to data/picked.z64, which scan() checks and adopts.
bool gevrOpenRomPicker()
{
    JNIEnv *env = (JNIEnv *)SDL_AndroidGetJNIEnv();
    jobject activity = (jobject)SDL_AndroidGetActivity();
    if (env == nullptr || activity == nullptr) {
        return false;
    }
    jclass cls = env->GetObjectClass(activity);
    jmethodID m = env->GetMethodID(cls, "openRomPicker", "()V");
    bool ok = false;
    if (m != nullptr) {
        env->CallVoidMethod(activity, m);
        ok = !env->ExceptionCheck();
    }
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
    }
    env->DeleteLocalRef(cls);
    env->DeleteLocalRef(activity);
    vr_log("launcher: file picker %s", ok ? "opened" : "failed");
    return ok;
}

// MainActivity.pickResult: a message about a failed or cancelled pick (a
// successful one shows up as data/picked.z64 instead), read and cleared.
std::string gevrTakePickResult()
{
    std::string out;
    JNIEnv *env = (JNIEnv *)SDL_AndroidGetJNIEnv();
    jobject activity = (jobject)SDL_AndroidGetActivity();
    if (env == nullptr || activity == nullptr) {
        return out;
    }
    jclass cls = env->GetObjectClass(activity);
    jfieldID f = env->GetStaticFieldID(cls, "pickResult", "Ljava/lang/String;");
    if (f != nullptr) {
        jstring s = (jstring)env->GetStaticObjectField(cls, f);
        if (s != nullptr) {
            const char *c = env->GetStringUTFChars(s, nullptr);
            if (c) {
                out = c;
                env->ReleaseStringUTFChars(s, c);
            }
            env->SetStaticObjectField(cls, f, nullptr);
            env->DeleteLocalRef(s);
        }
    }
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
    }
    env->DeleteLocalRef(cls);
    env->DeleteLocalRef(activity);
    return out;
}

// Laser pointer (vr_openxr.cpp gevrVrScreenPointer): a controller pointed at
// the screen is the mouse, and a trigger clicks. Returns whether it points.
//
// The pointer takes over only when it really moves (over 1% of the screen) or
// a trigger is pulled, and hands back as soon as the stick or a button is
// used: ImGui hides the gamepad focus on every mouse move, and a controller
// lying still still jitters, which left A doing nothing.
bool feedPointer(ImGuiIO &io, bool navUsed)
{
    static bool owns = false;
    static float lu = -1.0f, lv = -1.0f;
    float u, v;
    const bool on = gevrVrScreenPointer(&u, &v) != 0;
    const bool trig = get_button_state(1, "trigger") || get_button_state(0, "trigger");

    if (!on) {
        if (owns) {
            io.AddMousePosEvent(-FLT_MAX, -FLT_MAX);
        }
        owns = false;
        io.AddMouseButtonEvent(0, false);
        return false;
    }
    if (fabsf(u - lu) + fabsf(v - lv) > 0.01f || (trig && !navUsed)) {
        owns = true;
        lu = u;
        lv = v;
    }
    if (navUsed && owns) {
        owns = false;
        io.AddMousePosEvent(-FLT_MAX, -FLT_MAX);
    }
    if (owns) {
        io.AddMousePosEvent(u * kTexW, v * kTexH);
    }
    io.AddMouseButtonEvent(0, owns && trig);
    return owns;
}

// Returns whether the stick or a button drove the focus this frame.
// The test hook's START (1000) presses the launcher's Start directly.
bool s_injectStart = false;

bool feedGamepad(ImGuiIO &io, bool pointing)
{
    static Injected inj;
    pollInjected(inj);
    if (inj.mask & 0x1000) {
        s_injectStart = true;
    }
    static bool pickerHookDone = false;
    if ((inj.mask & 0x0020) && !pickerHookDone) {   // test hook: L opens the file picker
        pickerHookDone = true;
        gevrOpenRomPicker();
    }
    if (!(inj.mask & 0x0020)) {
        pickerHookDone = false;
    }

    XrVector2f l = {0, 0}, r = {0, 0};
    get_2d_input(0, "thumbstick", &l);
    get_2d_input(1, "thumbstick", &r);
    // whichever stick is pushed further
    XrVector2f s = (l.x * l.x + l.y * l.y > r.x * r.x + r.y * r.y) ? l : r;
    if (inj.x || inj.y) s = {inj.x / 80.0f, inj.y / 80.0f};
    const float t = 0.5f;
    io.AddKeyEvent(ImGuiKey_GamepadDpadUp, s.y > t);
    io.AddKeyEvent(ImGuiKey_GamepadDpadDown, s.y < -t);
    io.AddKeyEvent(ImGuiKey_GamepadDpadLeft, s.x < -t);
    io.AddKeyEvent(ImGuiKey_GamepadDpadRight, s.x > t);
    io.AddKeyEvent(ImGuiKey_GamepadFaceDown,
                   get_button_state(1, "a") || get_button_state(0, "x")
                   || (!pointing && (get_button_state(1, "trigger") || get_button_state(0, "trigger")))
                   || (inj.mask & 0x8000));
    const bool back = get_button_state(1, "b") || get_button_state(0, "y") || (inj.mask & 0x4000);
    const bool select = get_button_state(1, "a") || get_button_state(0, "x") || (inj.mask & 0x8000);
    io.AddKeyEvent(ImGuiKey_GamepadFaceRight, back);
    return fabsf(s.x) > t || fabsf(s.y) > t || back || select;
}

}  // namespace

extern "C" void gevrLauncherRun(void)
{
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO &io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
    io.BackendFlags |= ImGuiBackendFlags_HasGamepad;
    io.DisplaySize = ImVec2((float)kTexW, (float)kTexH);
    io.FontGlobalScale = 2.2f;
    io.MouseDrawCursor = false;  // the pointer's own spot is drawn in 3D (vr_pointer_draw)
    ImGui::StyleColorsDark();
    ImGuiStyle &style = ImGui::GetStyle();
    style.ScaleAllSizes(2.2f);
    style.WindowRounding = 0.0f;
    ImGui_ImplOpenGL3_Init("#version 300 es");

    // The header icon (android assets/launcher_icon.rgba, tools/make_icons.py).
    GLuint iconTex = 0;
    {
        SDL_RWops *rw = SDL_RWFromFile("launcher_icon.rgba", "rb");
        if (rw) {
            std::vector<unsigned char> px(128 * 128 * 4);
            if (SDL_RWread(rw, px.data(), 1, px.size()) == px.size()) {
                glGenTextures(1, &iconTex);
                glBindTexture(GL_TEXTURE_2D, iconTex);
                glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 128, 128, 0, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            }
            SDL_RWclose(rw);
        }
    }

    GLuint tex = 0, fbo = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, kTexW, kTexH, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    std::string active, message;
    RomInfo activeInfo;
    std::vector<RomInfo> others;
    bool pickerPending = false;

    // Find the ROM. A good dump copied into the data folder under any name
    // ("GoldenEye 007 (USA).z64") is renamed to ge.z64, the name the loader
    // looks for, so nobody has to rename files on a headset.
    auto scan = [&]() {
        // A ROM the file picker copied in (MainActivity.onActivityResult)
        // replaces the one in use if it checks out.
        if (pickerPending) {
            const std::string r = gevrTakePickResult();
            if (!r.empty()) {
                message = r;
                pickerPending = false;
            }
        }
        {
            const std::string picked = dataDir() + "/picked.z64";
            struct stat st;
            if (stat(picked.c_str(), &st) == 0) {
                RomInfo r = probeRom(picked);
                if (r.good && rename(picked.c_str(), (dataDir() + "/ge.z64").c_str()) == 0) {
                    message = "ROM chosen.";
                    vr_log("launcher: picked ROM adopted");
                } else {
                    message = "That file is not a GoldenEye 007 (USA) ROM: " + r.status + ".";
                    remove(picked.c_str());
                }
                pickerPending = false;
            }
        }
        active = activeRomPath();
        others = findRoms(active);
        if (active.empty()) {
            const std::string dir = dataDir() + "/";
            for (const RomInfo &r : others) {
                if (r.path.compare(0, dir.size(), dir) == 0 && r.path.find('/', dir.size()) == std::string::npos
                    && rename(r.path.c_str(), (dir + "ge.z64").c_str()) == 0) {
                    message = "Found " + r.path.substr(dir.size()) + " - using it.";
                    vr_log("launcher: renamed %s to ge.z64", r.path.c_str());
                    active = activeRomPath();
                    others = findRoms(active);
                    break;
                }
            }
        }
        activeInfo = active.empty() ? RomInfo() : probeRom(active);
    };
    scan();

    bool settingsRead = false;
    int mode = 1, turn = 0;
    bool vignetteOn = false;
    float vignette = 0.5f;
    bool focusStart = true;
    bool start = false;
    Uint32 last = SDL_GetTicks();

    vr_log("launcher: open, build %s (rom %s)", gevrBuildId, active.empty() ? "none" : active.c_str());

    while (!start) {
        SDL_PumpEvents();

        if (!gevrVrPumpBegin()) {
            SDL_Delay(5);
            continue;
        }

        // The pump's first begin loads goldeneye-vr.ini; take the choices then.
        if (!settingsRead) {
            settingsRead = true;
            mode = VrPlayMode ? 1 : 0;
            turn = VrUseSnapTurn >= 80.0f ? 3 : VrUseSnapTurn >= 40.0f ? 2 : VrUseSnapTurn > 0.0f ? 1 : 0;
            vignetteOn = VrComfortVignette > 0.0f;
            if (vignetteOn) vignette = VrComfortVignette;
        }

        Uint32 now = SDL_GetTicks();
        io.DeltaTime = (now > last) ? (now - last) / 1000.0f : 1.0f / 72.0f;
        last = now;
        // Look for a ROM about once a second while there is none or the file
        // picker is out (copied over USB, or picked).
        {
            static Uint32 lastScan = 0;
            if ((active.empty() || pickerPending) && now - lastScan > 1000) {
                lastScan = now;
                scan();
            }
        }
        // Both grips: the screen is in your hands, as in game (port/src/input.c):
        // it follows them, and the right stick moves it nearer/farther
        // (up/down) and makes it bigger/smaller (left/right).
        const bool grabbing = get_button_state(0, "grip") && get_button_state(1, "grip");
        vr_screen_grab(grabbing ? 1 : 0);
        if (grabbing) {
            XrVector2f r = {0, 0};
            get_2d_input(1, "thumbstick", &r);
            const float dt = io.DeltaTime > 0.1f ? 0.1f : io.DeltaTime;
            const float dy = fabsf(r.y) > 0.2f ? r.y : 0.0f;
            const float dx = fabsf(r.x) > 0.2f ? r.x : 0.0f;
            if (dy != 0.0f || dx != 0.0f) {
                const float width = 2.0f * VrScreenDistance * tanf(VrScreenFov * 0.5f * 3.14159265f / 180.0f);
                float dist = VrScreenDistance + dy * 1.5f * dt;
                if (dist < VR_SCREEN_DISTANCE_MIN) dist = VR_SCREEN_DISTANCE_MIN;
                if (dist > VR_SCREEN_DISTANCE_MAX) dist = VR_SCREEN_DISTANCE_MAX;
                float fov = 2.0f * (float)atan((double)(width / (2.0f * dist))) * 180.0f / 3.14159265f;
                fov += dx * 25.0f * dt;
                if (fov < VR_SCREEN_FOV_MIN) fov = VR_SCREEN_FOV_MIN;
                if (fov > VR_SCREEN_FOV_MAX) fov = VR_SCREEN_FOV_MAX;
                vr_screen_resize(dist, fov);
            }
        }
        {
            static bool pointing = false;
            // no stick navigation while the stick is resizing the screen
            const bool navUsed = grabbing ? false : feedGamepad(io, pointing);
            if (grabbing) {
                io.AddKeyEvent(ImGuiKey_GamepadDpadUp, false);
                io.AddKeyEvent(ImGuiKey_GamepadDpadDown, false);
                io.AddKeyEvent(ImGuiKey_GamepadDpadLeft, false);
                io.AddKeyEvent(ImGuiKey_GamepadDpadRight, false);
            }
            pointing = feedPointer(io, navUsed);
        }

        ImGui_ImplOpenGL3_NewFrame();
        ImGui::NewFrame();
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(io.DisplaySize);
        ImGui::Begin("##launcher", nullptr,
                     ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove
                     | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings
                     | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        const ImVec4 gold(0.88f, 0.69f, 0.25f, 1.0f);
        const ImVec4 good(0.5f, 0.9f, 0.5f, 1.0f);
        const ImVec4 bad(0.95f, 0.5f, 0.4f, 1.0f);

        // header: icon and title left, build right
        if (iconTex) {
            const float s = ImGui::GetTextLineHeight() * 2.2f;
            ImGui::Image((ImTextureID)(intptr_t)iconTex, ImVec2(s, s));
            ImGui::SameLine();
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() + (s - ImGui::GetTextLineHeight()) * 0.5f);
        }
        ImGui::TextColored(ImVec4(1.0f, 0.84f, 0.47f, 1.0f), "GOLDENEYE VR");
        {
            char build[96];
            snprintf(build, sizeof(build), "Build %s", gevrBuildId);
            const float w = ImGui::CalcTextSize(build).x;
            ImGui::SameLine(ImGui::GetWindowWidth() - w - ImGui::GetStyle().WindowPadding.x);
            ImGui::TextDisabled("%s", build);
        }
        ImGui::Separator();

        // ROM
        if (active.empty()) {
            ImGui::TextColored(bad, "No ROM yet.");
            ImGui::SameLine();
            ImGui::TextWrapped("Choose your GoldenEye 007 (USA) ROM, or copy it over USB into "
                               "Android/data/com.gevr.port/files/data (any name).");
            if (ImGui::Button("Choose ROM file...")) {
                pickerPending = gevrOpenRomPicker();
                message = pickerPending ? "Pick the ROM in the window that opened." : "Could not open the file picker.";
            }
            ImGui::SameLine();
            if (ImGui::Button("Look again")) {
                scan();
                if (active.empty()) message = "Still no ROM in that folder.";
            }
        } else {
            ImGui::TextColored(activeInfo.good ? good : bad, "ROM: %s", activeInfo.status.c_str());
            ImGui::SameLine();
            if (ImGui::SmallButton("Change...")) {
                pickerPending = gevrOpenRomPicker();
                message = pickerPending ? "Pick the ROM in the window that opened." : "Could not open the file picker.";
            }
            ImGui::TextDisabled("%s", active.c_str());
        }
        for (size_t i = 0; i < others.size() && i < 2; i++) {
            ImGui::PushID((int)i);
            if (ImGui::SmallButton("Use")) {
                std::string dst = dataDir();
                if (!dst.empty() && dst.back() != '/') dst += '/';
                dst += "ge.z64";
                if (copyFile(others[i].path, dst)) {
                    scan();
                    message = "ROM copied in.";
                } else {
                    message = "Could not copy that ROM.";
                }
                ImGui::PopID();
                break;
            }
            ImGui::SameLine();
            ImGui::TextDisabled("%s", others[i].path.c_str());
            ImGui::PopID();
        }
        if (!message.empty()) ImGui::TextDisabled("%s", message.c_str());
        ImGui::Separator();

        // options, two columns
        if (ImGui::BeginTable("opts", 2, ImGuiTableFlags_SizingStretchSame)) {
            ImGui::TableNextColumn();
            ImGui::TextColored(gold, "DISPLAY");
            ImGui::RadioButton("Stereo VR (3D play)", &mode, 1);
            ImGui::RadioButton("Flat screen", &mode, 0);
            ImGui::Spacing();
            ImGui::TextColored(gold, "SCREEN");
            {
                int curved = VrScreenCurved;
                ImGui::RadioButton("Flat", &curved, 0);
                ImGui::SameLine();
                ImGui::BeginDisabled(!vr_screen_curve_supported());
                ImGui::RadioButton("Curved", &curved, 1);
                ImGui::EndDisabled();
                VrScreenCurved = curved;
                // Live: this page is on the same screen.
                float size = VrScreenFov, dist = VrScreenDistance;
                ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.62f);
                if (ImGui::SliderFloat("Size", &size, VR_SCREEN_FOV_MIN, VR_SCREEN_FOV_MAX, "%.0f deg")) {
                    vr_screen_resize(VrScreenDistance, size);
                }
                ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.62f);
                if (ImGui::SliderFloat("Distance", &dist, VR_SCREEN_DISTANCE_MIN, VR_SCREEN_DISTANCE_MAX, "%.1f m")) {
                    vr_screen_resize(dist, VrScreenFov);
                }
            }

            ImGui::TableNextColumn();
            ImGui::TextColored(gold, "TURNING (stereo)");
            ImGui::RadioButton("Smooth", &turn, 0);
            ImGui::SameLine();
            ImGui::RadioButton("Snap 30", &turn, 1);
            ImGui::RadioButton("Snap 45", &turn, 2);
            ImGui::SameLine();
            ImGui::RadioButton("Snap 90", &turn, 3);
            ImGui::Spacing();
            ImGui::TextColored(gold, "COMFORT (stereo)");
            ImGui::Checkbox("Darken edges when moving", &vignetteOn);
            ImGui::BeginDisabled(!vignetteOn);
            ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.62f);
            ImGui::SliderFloat("Strength", &vignette, 0.1f, 1.0f, "%.1f");
            ImGui::EndDisabled();
            ImGui::EndTable();
        }
        ImGui::Separator();
        ImGui::TextDisabled("Both grips grab the screen (right stick: distance / size). Hold left stick click to recentre.");

        ImGui::BeginDisabled(active.empty() || !activeInfo.good);
        if (ImGui::Button("START", ImVec2(-1, ImGui::GetFrameHeight() * 1.6f))
            || (s_injectStart && !active.empty() && activeInfo.good)) {
            start = true;
        }
        s_injectStart = false;
        if (focusStart) {
            ImGui::SetItemDefaultFocus();
            ImGui::SetKeyboardFocusHere(-1);
            focusStart = false;
        }
        ImGui::EndDisabled();
        ImGui::TextDisabled("Point and pull the trigger, or use the stick and A.");
        ImGui::End();
        ImGui::Render();

        GLint prevFbo = 0;
        glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFbo);
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        glViewport(0, 0, kTexW, kTexH);
        glDisable(GL_SCISSOR_TEST);
        glClearColor(0.02f, 0.02f, 0.03f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)prevFbo);

        // Eye buffers black behind the screen, the launcher on the screen quad.
        if (vr_begin_eye_render()) {
            vr_pointer_draw();
            vr_end_eye_render();
        }
        vr_screen_set_visible(1);
        vr_screen_present_tex2d(tex, kTexW, kTexH);
        gevrVrPumpEnd();
    }

    VrPlayMode = mode ? VR_PLAYMODE_STEREO : VR_PLAYMODE_SCREEN;
    static const float snaps[] = {0.0f, 30.0f, 45.0f, 90.0f};
    VrUseSnapTurn = snaps[turn < 0 ? 0 : turn > 3 ? 3 : turn];
    VrComfortVignette = vignetteOn ? vignette : 0.0f;
    vrSettingsSave();
    vr_log("launcher: start (%s, snap %.0f, vignette %.2f)", VrPlayMode ? "stereo" : "screen",
           VrUseSnapTurn, VrComfortVignette);

    ImGui_ImplOpenGL3_Shutdown();
    ImGui::DestroyContext();
    if (iconTex) glDeleteTextures(1, &iconTex);
    glDeleteFramebuffers(1, &fbo);
    glDeleteTextures(1, &tex);
}
