#include "gevr_launcher_ui.h"
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
#include "vr_haptics.h"

extern "C" {
int gevrVrPumpBegin(void);            // port/src/gevr_engine_shim.c
void gevrVrPumpEnd(void);
const char *fsFullPath(const char *relPath);  // port/src/fs.c
extern const char gevrBuildId[];              // generated, port/cmake/buildid.cmake
void vrSettingsSave(void);            // vr_settings.cpp
void vrEnsurePlayerName(void);        // vr_settings.cpp: make up a name if there is none
extern char g_ActiveExtTexPack[];     // port/src/ext_tex.c: the texture pack in use ("" = none), saved in the ini
void gevrTexpackStartEarly(void);     // fast3d/gfx_pc.cpp: index that pack in the background
void vr_apply_refresh_rate(void);     // vr_openxr.cpp
int vr_get_supported_refresh_rates(int *rates, int capacity);
int gevrVrSessionRunning(void);       // vr_openxr.cpp: includes the unfocused Quest menu
extern int selected_num_players;      // src/game/front.c
extern int gamemode;                  // src/game/front.c
extern int g_StageNum;                 // port/src/main.c
void bossSetLoadedStage(int stage);
void init_mp_options_for_scenario(int numplayers);
void reset_mp_options_for_scenario(int scenarioid);
void setMPWeaponSet(int setNUM);
int getMPWeaponSet(void);
extern int player_char[];
extern int player_handicap[];
}
#include "net_core.h"
#include "net_game.h"
#include "net_voice.h"
#include "net_discovery.h"
#include "net_ice.h"
#include "juice/juice.h"
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
// Issue #16: back to the launcher from a level (port/src/input.c menu hold).
// The launcher runs before the ROM loads, so the app restarts into it.
extern "C" void gevrRestartToLauncher(void)
{
    JNIEnv *env = (JNIEnv *)SDL_AndroidGetJNIEnv();
    jobject activity = (jobject)SDL_AndroidGetActivity();
    if (env == nullptr || activity == nullptr) {
        vr_log("launcher: restart failed (no activity)");
        return;
    }
    jclass cls = env->GetObjectClass(activity);
    jmethodID m = env->GetMethodID(cls, "restartToLauncher", "()V");
    if (m != nullptr) {
        vr_log("launcher: restarting into the launcher");
        env->CallVoidMethod(activity, m);
    }
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
    }
    env->DeleteLocalRef(cls);
    env->DeleteLocalRef(activity);
}

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

// Update check against the GitHub releases (android UpdateChecker.java, through
// MainActivity.updaterStatus / updaterCommand). Java does the network and the
// install; the launcher shows its state and forwards the buttons.
struct UpdateStatus {
    std::string state;      // idle checking uptodate available downloading permission installing error
    std::string offered;    // version the Update button installs
    std::string message;
    std::string installed;
    int progress = -1;
    bool testBuilds = false;
};

UpdateStatus gevrUpdaterStatus()
{
    UpdateStatus st;
    JNIEnv *env = (JNIEnv *)SDL_AndroidGetJNIEnv();
    jobject activity = (jobject)SDL_AndroidGetActivity();
    if (env == nullptr || activity == nullptr) {
        return st;
    }
    jclass cls = env->GetObjectClass(activity);
    jmethodID m = env->GetMethodID(cls, "updaterStatus", "()Ljava/lang/String;");
    std::string raw;
    if (m != nullptr) {
        jstring s = (jstring)env->CallObjectMethod(activity, m);
        if (!env->ExceptionCheck() && s != nullptr) {
            const char *c = env->GetStringUTFChars(s, nullptr);
            if (c) {
                raw = c;
                env->ReleaseStringUTFChars(s, c);
            }
        }
        if (s != nullptr) env->DeleteLocalRef(s);
    }
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
    }
    env->DeleteLocalRef(cls);
    env->DeleteLocalRef(activity);

    // state \t offered \t progress \t message \t testBuilds \t installed
    std::vector<std::string> f;
    size_t pos = 0;
    while (true) {
        size_t tab = raw.find('\t', pos);
        f.push_back(raw.substr(pos, tab == std::string::npos ? std::string::npos : tab - pos));
        if (tab == std::string::npos) break;
        pos = tab + 1;
    }
    if (f.size() >= 6) {
        st.state = f[0];
        st.offered = f[1];
        st.progress = atoi(f[2].c_str());
        st.message = f[3];
        st.testBuilds = f[4] == "1";
        st.installed = f[5];
    }
    return st;
}

void gevrUpdaterCommand(const char *cmd)
{
    JNIEnv *env = (JNIEnv *)SDL_AndroidGetJNIEnv();
    jobject activity = (jobject)SDL_AndroidGetActivity();
    if (env == nullptr || activity == nullptr) {
        return;
    }
    jclass cls = env->GetObjectClass(activity);
    jmethodID m = env->GetMethodID(cls, "updaterCommand", "(Ljava/lang/String;)V");
    if (m != nullptr) {
        jstring s = env->NewStringUTF(cmd);
        env->CallVoidMethod(activity, m, s);
        env->DeleteLocalRef(s);
    }
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
    }
    env->DeleteLocalRef(cls);
    env->DeleteLocalRef(activity);
    vr_log("launcher: updater %s", cmd);
}

// Java calls for the Mods page (MainActivity.modsStatus / modsCommand).
static std::string gevrJavaString(const char *method)
{
    std::string out;
    JNIEnv *env = (JNIEnv *)SDL_AndroidGetJNIEnv();
    jobject activity = (jobject)SDL_AndroidGetActivity();
    if (env == nullptr || activity == nullptr) {
        return out;
    }
    jclass cls = env->GetObjectClass(activity);
    jmethodID m = env->GetMethodID(cls, method, "()Ljava/lang/String;");
    if (m != nullptr) {
        jstring s = (jstring)env->CallObjectMethod(activity, m);
        if (!env->ExceptionCheck() && s != nullptr) {
            const char *c = env->GetStringUTFChars(s, nullptr);
            if (c) {
                out = c;
                env->ReleaseStringUTFChars(s, c);
            }
        }
        if (s != nullptr) env->DeleteLocalRef(s);
    }
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
    }
    env->DeleteLocalRef(cls);
    env->DeleteLocalRef(activity);
    return out;
}

static void gevrJavaCommand(const char *method, const char *arg)
{
    JNIEnv *env = (JNIEnv *)SDL_AndroidGetJNIEnv();
    jobject activity = (jobject)SDL_AndroidGetActivity();
    if (env == nullptr || activity == nullptr) {
        return;
    }
    jclass cls = env->GetObjectClass(activity);
    jmethodID m = env->GetMethodID(cls, method, "(Ljava/lang/String;)V");
    if (m != nullptr) {
        jstring s = env->NewStringUTF(arg);
        env->CallVoidMethod(activity, m, s);
        env->DeleteLocalRef(s);
    }
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
    }
    env->DeleteLocalRef(cls);
    env->DeleteLocalRef(activity);
    // Report commands contain the optional private note and player name.
    if (strcmp(method, "reportCommand") == 0) vr_log("launcher: reportCommand");
    else vr_log("launcher: %s %s", method, arg);
}

static std::string gevrEncodeUrl64(const char *input)
{
    static const char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
    std::string out;
    const unsigned char *p = (const unsigned char *)input;
    const size_t n = strlen(input);
    for (size_t i = 0; i < n; i += 3) {
        unsigned value = (unsigned)p[i] << 16;
        if (i + 1 < n) value |= (unsigned)p[i + 1] << 8;
        if (i + 2 < n) value |= p[i + 2];
        out.push_back(alphabet[(value >> 18) & 63]);
        out.push_back(alphabet[(value >> 12) & 63]);
        if (i + 1 < n) out.push_back(alphabet[(value >> 6) & 63]);
        if (i + 2 < n) out.push_back(alphabet[value & 63]);
    }
    return out;
}

static void gevrHapticsPage(bool &open, const ImVec4 &gold, const ImVec4 &good, const ImVec4 &bad)
{
    static int activeTab = HAPTIC_CAT_PISTOLS;

    ImGui::TextColored(gold, "HAPTIC FEEDBACK SETTINGS");
    ImGui::TextDisabled("Tune vibration intensity (0-10) and pulse duration (ms) for each weapon and action. Feel real-time vibrations while testing.");
    ImGui::Spacing();

    // Category Tabs (6 categories, keeping every category to <= 8 items so nothing is cut off)
    const char *catNames[] = {
        "Pistols",
        "Automatics",
        "Rifles & Heavy",
        "Melee & Thrown",
        "Gadgets",
        "Damage & Actions"
    };

    for (int i = 0; i < HAPTIC_CAT_COUNT; ++i) {
        if (i > 0) ImGui::SameLine();
        bool isSelected = (activeTab == i);
        if (isSelected) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.35f, 0.30f, 0.15f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_Text, gold);
        }
        if (ImGui::Button(catNames[i])) {
            activeTab = i;
        }
        if (isSelected) {
            ImGui::PopStyleColor(2);
        }
    }

    ImGui::Separator();

    // Scrollable region for table so the footer is always pinned visible and nothing is ever clipped
    const float footerH = ImGui::GetFrameHeightWithSpacing() * 2.0f;
    ImGui::BeginChild("##haptics_scroll", ImVec2(0, -footerH), false, ImGuiWindowFlags_None);
    ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(ImGui::GetStyle().CellPadding.x, 3.0f));

    // Table of items for activeTab
    if (ImGui::BeginTable("haptics_table", 4, ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH)) {
        ImGui::TableSetupColumn("Item / Action", ImGuiTableColumnFlags_WidthFixed, 270.0f);
        ImGui::TableSetupColumn("Intensity", ImGuiTableColumnFlags_WidthFixed, 330.0f);
        ImGui::TableSetupColumn("Duration", ImGuiTableColumnFlags_WidthFixed, 390.0f);
        ImGui::TableSetupColumn("Test Feel", ImGuiTableColumnFlags_WidthFixed, 110.0f);
        ImGui::TableHeadersRow();

        int count = vrHapticsGetCount();
        for (int i = 0; i < count; ++i) {
            HapticProfile *p = vrHapticsGetProfileByIndex(i);
            if (!p || p->category != (HapticCategory)activeTab) continue;

            ImGui::PushID(p->iniKey);

            // Column 0: Name
            ImGui::TableNextColumn();
            ImGui::AlignTextToFramePadding();
            ImGui::Text("%s", p->name);

            // Column 1: Intensity Stepper & Slider
            ImGui::TableNextColumn();
            bool changed = false;
            if (ImGui::Button("-##int", ImVec2(34, 0))) {
                if (p->intensity > 0) {
                    p->intensity--;
                    changed = true;
                }
            }
            ImGui::SameLine();
            ImGui::SetNextItemWidth(170.0f);
            if (ImGui::SliderInt("##int_sl", &p->intensity, 0, 10, "%d")) {
                changed = true;
            }
            ImGui::SameLine();
            if (ImGui::Button("+##int", ImVec2(34, 0))) {
                if (p->intensity < 10) {
                    p->intensity++;
                    changed = true;
                }
            }

            // Column 2: Duration Stepper & Slider
            ImGui::TableNextColumn();
            if (ImGui::Button("-##dur", ImVec2(34, 0))) {
                if (p->durationMs > 10) {
                    p->durationMs = std::max(10, p->durationMs - 10);
                    changed = true;
                } else if (p->durationMs > 0 && p->intensity == 0) {
                    p->durationMs = 0;
                    changed = true;
                }
            }
            ImGui::SameLine();
            ImGui::SetNextItemWidth(220.0f);
            if (ImGui::SliderInt("##dur_sl", &p->durationMs, 10, 500, "%d ms")) {
                changed = true;
            }
            ImGui::SameLine();
            if (ImGui::Button("+##dur", ImVec2(34, 0))) {
                if (p->durationMs < 500) {
                    p->durationMs = std::min(500, p->durationMs + 10);
                    changed = true;
                }
            }

            // Column 3: Test Button
            ImGui::TableNextColumn();
            if (ImGui::Button("Test", ImVec2(100, 0)) || changed) {
                vrHapticsTriggerTest(p->id);
            }

            ImGui::PopID();
        }

        ImGui::EndTable();
    }

    ImGui::PopStyleVar();
    ImGui::EndChild();

    ImGui::Spacing();
    ImGui::Separator();

    // Footer actions
    if (ImGui::Button("Reset Category to Defaults")) {
        vrHapticsResetCategory((HapticCategory)activeTab);
    }
    ImGui::SameLine();
    if (ImGui::Button("Reset All to Defaults")) {
        vrHapticsResetAll();
    }
    ImGui::SameLine();
    if (ImGui::Button("Back", ImVec2(-1, 0))) {
        vrSettingsSave();
        open = false;
    }
}

static void gevrReportPage(bool &open, bool crash, const ImVec4 &gold, const ImVec4 &good, const ImVec4 &bad)
{
    static char note[501] = {};
    static Uint32 lastPoll = 0;
    static std::string status;
    Uint32 now = SDL_GetTicks();
    if (now - lastPoll > 250 || !lastPoll) {
        status = gevrJavaString("reportStatus");
        lastPoll = now;
    }
    if (crash && status == "offer") {
        gevrJavaCommand("reportCommand", "offered");
        status = "idle";
    }
    ImGui::TextColored(gold, crash ? "SEND CRASH REPORT" : "SEND DEBUG LOG");
    ImGui::TextWrapped("This sends the game log, available crash data, your player name and headset model. Network addresses and connection details are removed from text logs.");
    ImGui::Spacing();
    ImGui::TextWrapped("Optional note (what happened just before the problem):");
    ImGui::InputTextMultiline("##reportnote", note, sizeof(note), ImVec2(-1, ImGui::GetTextLineHeight() * 4));
    ImGui::Spacing();
    if (status != "sending" && ImGui::Button("Send", ImVec2(-1, 0))) {
        vrHapticsDumpCTable();
        std::string command = "send|" + gevrEncodeUrl64(note) + "|"
            + gevrEncodeUrl64(VrPlayerName) + "|" + gevrBuildId;
        gevrJavaCommand("reportCommand", command.c_str());
        status = "sending";
    }
    if (status == "sending") ImGui::TextColored(gold, "Sending...");
    else if (status == "sent") ImGui::TextColored(good, "Sent. Thank you.");
    else if (status.rfind("error:", 0) == 0) ImGui::TextColored(bad, "%s", status.c_str() + 6);
    ImGui::Spacing();
    if (ImGui::Button("Back", ImVec2(-1, 0))) open = false;
}

static std::string gevrDecodeUrl64(const std::string &input)
{
    std::string out;
    unsigned value = 0;
    int bits = -8;
    for (unsigned char c : input) {
        int digit = c >= 'A' && c <= 'Z' ? c - 'A' :
                    c >= 'a' && c <= 'z' ? c - 'a' + 26 :
                    c >= '0' && c <= '9' ? c - '0' + 52 :
                    c == '-' ? 62 : c == '_' ? 63 : -1;
        if (digit < 0) break;
        value = (value << 6) | (unsigned)digit;
        bits += 6;
        if (bits >= 0) {
            out.push_back((char)((value >> bits) & 255));
            bits -= 8;
        }
    }
    return out;
}

static std::vector<std::string> gevrSplitLobbyEvent(const std::string &raw)
{
    std::vector<std::string> fields;
    size_t pos = 0;
    while (true) {
        size_t sep = raw.find('|', pos);
        fields.push_back(raw.substr(pos, sep == std::string::npos ? std::string::npos : sep - pos));
        if (sep == std::string::npos) break;
        pos = sep + 1;
    }
    return fields;
}

static std::vector<std::string> gevrSplit(const std::string &s, char sep)
{
    std::vector<std::string> f;
    size_t pos = 0;
    while (true) {
        size_t at = s.find(sep, pos);
        f.push_back(s.substr(pos, at == std::string::npos ? std::string::npos : at - pos));
        if (at == std::string::npos) break;
        pos = at + 1;
    }
    return f;
}

// Issue #25: the Mods page. Fan-made texture packs that ModManager.java
// downloads from their authors' sites and unpacks into files/texture-packs;
// the renderer (port/fast3d/gevr_texpack.cpp) uses the one picked here from
// the next START. Nothing of theirs ships with the app.
struct ModPack {
    std::string id, title, by, site, version, state, message, update;   // update: a newer release's version
    int mb = 0, progress = -1;
};

static void gevrModsPage(bool &open, Uint32 now, const ImVec4 &gold, const ImVec4 &good, const ImVec4 &bad)
{
    static std::vector<ModPack> packs;
    static Uint32 lastPoll = 0;
    static bool checked = false;
    if (!checked) {
        checked = true;
        gevrJavaCommand("modsCommand", "check");   // newer pack releases on GitHub (ModManager.checkLatest)
    }
    if (now - lastPoll > 250 || lastPoll == 0) {
        lastPoll = now;
        packs.clear();
        std::string raw = gevrJavaString("modsStatus");
        if (!raw.empty()) {
            for (const std::string &line : gevrSplit(raw, '\n')) {
                std::vector<std::string> f = gevrSplit(line, '\t');
                if (f.size() < 9) continue;
                ModPack p;
                p.id = f[0];
                p.title = f[1];
                p.by = f[2];
                p.site = f[3];
                p.version = f[4];
                p.mb = atoi(f[5].c_str());
                p.state = f[6];
                p.progress = atoi(f[7].c_str());
                p.message = f[8];
                if (f.size() > 9) p.update = f[9];
                packs.push_back(p);
            }
        }
    }

    ImGui::TextColored(gold, "MODS");
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    ImGui::TextWrapped("Texture packs made by fans replace the game's textures with sharper ones. They download "
                       "when you ask; nothing is included with GoldenEye VR. A pack is used from the "
                       "next START.");
    ImGui::PopStyleColor();
    ImGui::Separator();

    for (const ModPack &p : packs) {
        ImGui::PushID(p.id.c_str());
        ImGui::Text("%s  %s", p.title.c_str(), p.version.c_str());
        // wrapped: the AI pack's credit ran off the page's right edge (user)
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
        ImGui::TextWrapped("by %s  -  %s  -  %d MB", p.by.c_str(), p.site.c_str(), p.mb);
        ImGui::PopStyleColor();
        if (p.state == "downloading" || p.state == "installing") {
            if (ImGui::SmallButton("Cancel")) gevrJavaCommand("modsCommand", "cancel");
            ImGui::SameLine();
            const char *what = p.state == "downloading" ? "Downloading" : "Unpacking";
            if (p.progress >= 0) {
                ImGui::TextColored(gold, "%s... %d%%", what, p.progress);
            } else {
                ImGui::TextColored(gold, "%s...", what);
            }
        } else if (p.state == "installed") {
            bool inuse = p.id == g_ActiveExtTexPack;
            ImGui::TextColored(good, inuse ? "Installed, in use." : "Installed.");
            ImGui::SameLine();
            if (ImGui::SmallButton("Remove")) {
                if (inuse) g_ActiveExtTexPack[0] = '\0';
                gevrJavaCommand("modsCommand", ("remove:" + p.id).c_str());
            }
            if (!p.update.empty()) {
                // a newer release of the pack (ModManager.checkLatest): it replaces this one
                char label[64];
                snprintf(label, sizeof(label), "Update to %s (%d MB)", p.update.c_str(), p.mb);
                const float w = ImGui::CalcTextSize(label).x + ImGui::GetStyle().FramePadding.x * 2;
                ImGui::SameLine();
                if (ImGui::GetContentRegionAvail().x < w) ImGui::NewLine();
                if (ImGui::SmallButton(label)) gevrJavaCommand("modsCommand", ("install:" + p.id).c_str());
            }
            if (!p.message.empty()) {   // an update that failed; the installed pack stays
                ImGui::PushStyleColor(ImGuiCol_Text, bad);
                ImGui::TextWrapped("%s", p.message.c_str());
                ImGui::PopStyleColor();
            }
        } else {
            if (p.state == "error") {
                ImGui::PushStyleColor(ImGuiCol_Text, bad);
                ImGui::TextWrapped("%s", p.message.c_str());
                ImGui::PopStyleColor();
            }
            char label[64];
            snprintf(label, sizeof(label), p.state == "error" ? "Try again (%d MB)" : "Download and install (%d MB)", p.mb);
            if (ImGui::Button(label)) gevrJavaCommand("modsCommand", ("install:" + p.id).c_str());
        }
        ImGui::PopID();
        ImGui::Spacing();
    }
    ImGui::Separator();

    // which textures the game uses
    ImGui::TextColored(gold, "TEXTURES");
    if (ImGui::RadioButton("Original", g_ActiveExtTexPack[0] == '\0')) {
        g_ActiveExtTexPack[0] = '\0';
    }
    for (const ModPack &p : packs) {
        if (p.state != "installed") continue;
        // beside the last choice while it fits, else on the next line
        const float w = ImGui::GetFrameHeight() + ImGui::GetStyle().ItemInnerSpacing.x + ImGui::CalcTextSize(p.title.c_str()).x;
        ImGui::SameLine();
        if (ImGui::GetContentRegionAvail().x < w) ImGui::NewLine();
        if (ImGui::RadioButton(p.title.c_str(), p.id == g_ActiveExtTexPack)) {
            snprintf(g_ActiveExtTexPack, 256, "%s", p.id.c_str());
        }
    }
    ImGui::Spacing();
    if (ImGui::Button("Done", ImVec2(-1, 0))) {
        open = false;
    }
}

// Names: printable ASCII, which the game's font can draw over a player's head,
// less '|', the lobby service's field separator.
int nameCharFilter(ImGuiInputTextCallbackData *data)
{
    return data->EventChar < 0x20 || data->EventChar > 0x7e || data->EventChar == '|';
}

extern "C" uint16_t get_mTrack2Vol(void);
extern "C" void set_mTrack2Vol(uint16_t);
extern "C" void musicTrack1ApplySeqpVol(uint16_t);
extern "C" void musicTrack3ApplySeqpVol(uint16_t);
extern "C" void gevrSndApplySfxVolume(uint16_t);

// How this headset reached its game: through the internet lobby (ICE) or the
// LAN. A host migration rejoins the same way (gevrLobbyGameTick).
static bool g_joinedViaInternet = false;

static std::string gevrLobbyName()
{
    char name[GEVR_MAX_NAME_LEN];
    snprintf(name, sizeof(name), "%s's game", VrPlayerName);
    return name;
}

static std::string gevrLobbyCreateCommand(const std::string &name, int stage, int set, int max)
{
    return std::string("create|") + (VrMpVisibility ? "private" : "public") + "|" + name + "|" +
        std::to_string(GEVR_NET_VERSION) + "|" + std::to_string(stage) + "|" +
        std::to_string(set) + "|" + std::to_string(max);
}

// The multiplayer page's shared widgets. net_match.c is the one list of the
// stages, sets, scenarios, characters and guns, and the choices live in the
// settings (VrMp*, goldeneye-vr.ini), so they survive the restart into the
// launcher.
//
// A combo over a named list, tall enough to show it whole: the stick steps
// rows, and a popup that scrolled hid them (user). A long list (the 64
// characters, the guns) keeps ImGui's larger popup and follows the focus.
static bool namedCombo(const char *id, int count, const char *(*name)(int), int *sel, bool longList = false) {
    return gevrNamedCombo(id, count, name, sel, longList, [](int) { return false; });
}

// The host's player count, 2..8 on any stage (VrMpMaxPlayers).
static const char *playerCountName(int idx) {
    static const char *const names[] = { "2 players", "3 players", "4 players", "5 players",
                                         "6 players", "7 players", "8 players" };
    static_assert(sizeof(names) / sizeof(names[0]) == GEVR_MAX_PLAYERS - 1, "a name for every count");
    return idx >= 0 && idx < GEVR_MAX_PLAYERS - 1 ? names[idx] : names[2];
}

static const char *gunNameAt(int idx) { return netItem(idx)->name; }
static const char *stageNameById(int levelId) { return netStageName(netStageIndexOf((uint8_t)levelId)); }

// A game list's entry (the lobby service, a LAN beacon): a co-op game lists
// 0x80 | where the party is (its menus or a mission's level id), and its
// difficulty in the weapons' place (net_core.c netGetLobbyStage).
static std::string listedGameLabel(int stage, int weapons) {
    if (stage & NET_LOBBY_COOP_STAGE) {
        const uint8_t where = (uint8_t)(stage & 0x7F);
        if (where == NET_COOP_FRONT_STAGE)
            return "Co-op campaign, in the menus";
        return std::string("Co-op campaign: ") + netCoopStageName(where) + ", " + netDifficultyName(weapons);
    }
    return std::string(stageNameById(stage)) + ", " + netWeaponSetName(weapons);
}

// A gun chooser: the value is an ITEM_IDS, the list works in positions.
static bool gunCombo(const char *id, int *item) {
    int idx = netItemIndexOf(*item);
    if (idx < 0)
        idx = 0;
    const bool changed = namedCombo(id, netItemCount(), gunNameAt, &idx, true);
    if (changed)
        *item = netItem(idx)->item;
    return changed;
}

// The host's match config from the saved choices, clamped to the lists.
static NetMatchConfig gevrLauncherConfig() {
    NetMatchConfig c = {};
    auto clampi = [](int v, int n, int dflt) { return v >= 0 && v < n ? v : dflt; };
    c.stage = (uint8_t)(netStageIndexOf((uint8_t)VrMpStage) >= 0 ? VrMpStage : 27);
    c.scenario = (uint8_t)clampi(VrMpScenario, netScenarioCount(), 0);
    c.weapon_set = (uint8_t)clampi(VrMpWeaponSet, netWeaponSetCount(), 4);
    c.game_length = (uint8_t)clampi(VrMpLength, 7, 2);
    c.health = (uint8_t)clampi(VrMpHealth, netHealthCount(), 5);
    c.dual_wield = (uint8_t)clampi(VrMpDual, 3, 0);
    c.loadouts = VrMpLoadouts ? 1 : 0;
    c.next_round = (uint8_t)clampi(VrMpNextRound, 3, 0);
    c.friendly_fire = VrMpFriendlyFire != 0;
    c.voice_mode = (uint8_t)clampi(VrMpVoiceMode, 2, 0);
    c.fun_flags = (uint8_t)clampi(VrMpFunFlags, 8, 0);
    c.gun_size = (uint8_t)clampi(VrMpGunSize, 3, 0);
    for (int i = 0; i < 4; i++)
        c.custom_set[i] = (uint8_t)(netItemIndexOf(VrMpCustom[i]) >= 0 ? VrMpCustom[i] : netItem(0)->item);
    c.max_players = (uint8_t)(VrMpMaxPlayers >= 2 && VrMpMaxPlayers <= GEVR_MAX_PLAYERS ? VrMpMaxPlayers : 4);
    if (VrMpMode == NET_MODE_COOP) {
        // the solo campaign for the party: it starts in the game's menus, where the
        // host picks each mission and difficulty; four players at most (the deathmatch
        // fields ride along unused)
        c.mode = NET_MODE_COOP;
        c.stage = NET_COOP_FRONT_STAGE;
        c.difficulty = 0;
        c.max_players = NET_COOP_MAX_PLAYERS;
        return c;
    }
    return c;
}

static void gevrTeamChoiceRow(const char *id) {
    if (!netIsActive() || netGetMatchConfig()->mode == NET_MODE_COOP || !netScenarioHasTeams(netGetMatchConfig()->scenario))
        return;
    int team = netGetSlotTeam(netGetLocalSlot());
    ImGui::TextUnformatted("Your team:");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 9.0f);
    if (namedCombo(id, 3, netTeamName, &team))
        netLobbySetTeam((uint8_t)team);
    if (netGetPhase() == NET_PHASE_IN_PROGRESS)
        ImGui::SameLine(), ImGui::TextDisabled("next match");
}
static std::string gevrPingText(int slot) {
    int ping = netGetSlotPing(slot);
    return ping < 0 ? "—" : std::to_string(ping);
}

// A host choice changed: saved, and told to the lobby when one is up.
static void gevrHostChoiceChanged() {
    if (!netIsHost()) {
        vrSettingsSave();
        return;
    }
    const NetMatchConfig c = gevrLauncherConfig();
    netLobbySetConfig(&c);
    const NetMatchConfig *accepted = netGetMatchConfig();
    VrMpMode = accepted->mode;
    if (accepted->mode != NET_MODE_COOP)
        VrMpStage = accepted->stage;
    VrMpScenario = accepted->scenario;
    VrMpWeaponSet = accepted->weapon_set;
    VrMpLength = accepted->game_length;
    VrMpHealth = accepted->health;
    VrMpDual = accepted->dual_wield;
    VrMpLoadouts = accepted->loadouts;
    VrMpNextRound = accepted->next_round;
    VrMpVoiceMode = accepted->voice_mode;
    VrMpFriendlyFire = accepted->friendly_fire;
    VrMpFunFlags = accepted->fun_flags;
    VrMpGunSize = accepted->gun_size;
    if (accepted->mode != NET_MODE_COOP) // co-op's four is not the deathmatch count
        VrMpMaxPlayers = accepted->max_players;
    for (int k = 0; k < 4; k++)
        VrMpCustom[k] = accepted->custom_set[k];
    vrSettingsSave();
}

static void gevrSendLoadout() {
    uint8_t items[4];
    for (int i = 0; i < 4; i++)
        items[i] = (uint8_t)(netItemIndexOf(VrMpLoadout[i]) >= 0 ? VrMpLoadout[i] : netItem(0)->item);
    netLobbySetLoadout(items);
}

// This player's character: the one combo for the host, a joiner and a connected client.
static void characterRow(const char *label, const char *id) {
    ImGui::TextUnformatted(label);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 12.0f);
    if (namedCombo(id, netCharacterCount(), netCharacterName, &VrMpChr, true)) {
        vrSettingsSave();
        if (netIsActive())
            netLobbySetCharacter((uint8_t)VrMpChr);
    }
    netSetPreferredCharacter((uint8_t)VrMpChr);
}

// This player's four spawn guns, for a match with loadouts on.
static void loadoutRows(const char *idprefix) {
    ImGui::TextUnformatted("Your loadout, when the host turns loadouts on:");
    bool changed = false;
    for (int i = 0; i < 4; i++) {
        char id[32];
        snprintf(id, sizeof(id), "##%sloadout%d", idprefix, i);
        ImGui::Text("Gun %d:", i + 1);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(ImGui::GetFontSize() * 12.0f);
        changed |= gunCombo(id, &VrMpLoadout[i]);
    }
    if (changed) {
        vrSettingsSave();
        if (netIsActive())
            gevrSendLoadout();
    }
}

// A row of favorite toggles over a bitmask: the shuffle and the playlist draw from them.
static bool favoriteRow(const char *label, int count, const char *(*name)(int), unsigned *mask, int perRow) {
    bool changed = false;
    ImGui::TextUnformatted(label);
    for (int i = 0; i < count; i++) {
        bool on = ((*mask >> i) & 1u) != 0;
        if (i % perRow != 0)
            ImGui::SameLine();
        char id[48];
        snprintf(id, sizeof(id), "%s##%s%d", name(i), label, i);
        if (ImGui::Checkbox(id, &on)) {
            *mask = on ? (*mask | (1u << i)) : (*mask & ~(1u << i));
            changed = true;
        }
    }
    return changed;
}

// The host's match options: a section of the Host tab, before and while hosting.
static void gevrMatchOptions(bool favorites = false) {
    bool changed = false;
    if (!favorites) {
        ImGui::Text("Scenario:");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(ImGui::GetFontSize() * 14.0f);
        changed |= namedCombo("##scenario", netScenarioCount(), netScenarioName, &VrMpScenario);
        if (VrMpScenario == SCENARIO_YOLT) {
            ImGui::TextDisabled("Length: last one standing (the scenario's own)");
        } else {
            ImGui::Text("Length:");
            ImGui::SameLine();
            ImGui::SetNextItemWidth(ImGui::GetFontSize() * 10.0f);
            // The Living Daylights takes the time limits only, as the game's own menu has it
            changed |= namedCombo("##length", VrMpScenario == SCENARIO_TLD ? 4 : 7, netGameLengthName, &VrMpLength);
        }
        ImGui::Text("Health:");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(ImGui::GetFontSize() * 10.0f);
        changed |= namedCombo("##health", netHealthCount(), netHealthName, &VrMpHealth);
        ImGui::Text("Dual wield:");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(ImGui::GetFontSize() * 10.0f);
        changed |= namedCombo("##dual", 3, netDualWieldName, &VrMpDual);
        ImGui::SameLine();
        ImGui::TextDisabled(VrMpDual == NET_DUAL_DOUBLES ? "a second copy of your gun makes a pair"
                            : VrMpDual == NET_DUAL_ANY   ? "hold X for the left hand's panel"
                                                         : "");
        bool loadouts = VrMpLoadouts != 0;
        if (ImGui::Checkbox("Players spawn with their own four guns (loadouts)", &loadouts)) {
            VrMpLoadouts = loadouts ? 1 : 0;
            changed = true;
        }
        ImGui::Text("Next round:");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(ImGui::GetFontSize() * 8.0f);
        changed |= namedCombo("##nextround", 3, netNextRoundName, &VrMpNextRound);
        ImGui::SameLine();
        ImGui::TextDisabled("%s", VrMpNextRound == NET_NEXT_SHUFFLE    ? "a random favorite map and set"
                                  : VrMpNextRound == NET_NEXT_PLAYLIST ? "your favorites in order"
                                                                       : "the players vote in the pause menu");
    }

    if (favorites) {
        changed |= favoriteRow("Favorite maps:", netStageCount(), netStageName, &VrMpFavStages, 6);
        changed |= favoriteRow("Favorite sets:", netWeaponSetCount(), netWeaponSetName, &VrMpFavSets, 5);
    }

    if (changed)
        gevrHostChoiceChanged();
}

static const char *gevrGunSizeName(int n) {
    const char *names[] = {"Normal", "Tiny", "Big"};
    return names[n];
}
static void gevrFunOptions(bool hostPage) {
    int flags = netIsActive() ? netGetMatchConfig()->fun_flags : VrMpFunFlags;
    int size = netIsActive() ? netGetMatchConfig()->gun_size : VrMpGunSize;
    ImGui::TextDisabled(netIsActive() && netGetPhase() == NET_PHASE_IN_PROGRESS ? "Pending: applies next round"
                                                                                : "Applies when the round loads");
    ImGui::BeginDisabled(netIsActive() ? !netIsHost() : !hostPage);
    const char *labels[] = {"DK mode", "Paintball", "Line mode"};
    bool changed = false;
    for (int n = 0; n < 3; n++) {
        bool on = (flags & (1 << n)) != 0;
        if (ImGui::Checkbox(labels[n], &on)) {
            flags ^= 1 << n;
            changed = true;
        }
    }
    ImGui::TextUnformatted("Gun size:");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 9);
    changed |= namedCombo("##mpgunsize", 3, gevrGunSizeName, &size);
    ImGui::EndDisabled();
    if (changed) {
        VrMpFunFlags = flags;
        VrMpGunSize = size;
        if (netIsHost()) {
            gevrNetConfigSet(CFG_FUN_FLAGS, flags);
            gevrNetConfigSet(CFG_GUN_SIZE, size);
        } else
            vrSettingsSave();
    }
}
static void gevrLobbyRoster(const ImVec4 &gold) {
    const NetMsgLobbyState *lobby = netGetLobbyState();
    ImGui::TextColored(gold, "PLAYERS (%d/%d)", netGetConnectedPlayerCount(), netGetMaxPlayers());
    if (ImGui::BeginTable("##lobbyroster", 5, ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_BordersInnerH)) {
        float font = ImGui::GetFontSize();
        ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Character", ImGuiTableColumnFlags_WidthFixed, font * 9);
        ImGui::TableSetupColumn("Team", ImGuiTableColumnFlags_WidthFixed, font * 5);
        ImGui::TableSetupColumn("State", ImGuiTableColumnFlags_WidthFixed, font * 6);
        ImGui::TableSetupColumn("Ping (ms)", ImGuiTableColumnFlags_WidthFixed, font * 6);
        ImGui::TableHeadersRow();
        for (int i = 0; i < GEVR_MAX_PLAYERS; i++) {
            if (!lobby->slots[i].connected)
                continue;
            const auto &slot = lobby->slots[i];
            bool host = i == netGetHostSlot();
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(slot.name);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("%s\nBuild: %s\nProtocol: %d\nSlot: %d\nPing is the round trip to the current host.",
                                  slot.name, netGetSlotAppVersion(i), GEVR_NET_VERSION, i + 1);
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(netCharacterName(slot.chr_id));
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(netScenarioHasTeams(lobby->config.scenario) ? netTeamName(slot.team) : "—");
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(host ? "Host" : slot.ready ? "Ready" : "Waiting");
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(gevrPingText(i).c_str());
        }
        ImGui::EndTable();
    }
}
void gevrMultiplayerPage(bool &open, bool &startMatch, const ImVec4 &gold, const ImVec4 &good, const ImVec4 &bad) {
    static int subTab = 0;     // 0 = Host, 1 = Join
    static int joinMethod = 0; // 0 = Public Internet, 1 = Private Code, 2 = LAN Games, 3 = Direct IP
    static char directIp[64] = "192.168.1.";
    static char privateCode[16] = "";
    static std::string hostedCode;
    static std::string onlineMessage;
    static std::string clientJoinId;
    static std::vector<std::string> hostJoinIds;
    struct OnlineLobby {
        std::string code, name, phase;
        int stage, weapons, players, maxPlayers;
    };
    static std::vector<OnlineLobby> onlineLobbies;
    static bool listLoading = false;
    static uint32_t lastListMs = 0;
    static uint32_t lastHeartbeatMs = 0;

    vrEnsurePlayerName(); // here, not at launcher start: the settings load on the first frame
    for (int i = 0; i < 32; ++i) {
        const std::string raw = gevrJavaString("lobbyEvent");
        if (raw.empty())
            break;
        const auto f = gevrSplitLobbyEvent(raw);
        if (f[0] == "CREATED" && f.size() >= 2 && netIsHost()) {
            hostedCode = f[1];
            onlineMessage = "Lobby online";
            // the clients keep the lobby and its token: whoever is elected host
            // if this one leaves resumes the same lobby (net_core.c netHostLost)
            netSetLobbyHandoff(f[1].c_str(), f.size() >= 3 ? f[2].c_str() : "");
        } else if (f[0] == "LOBBY_LOST" && f.size() >= 3 && netIsHost()) {
            vr_log("launcher: lobby %s lost (%s); registering again", f[1].c_str(), f[2].c_str());
            hostedCode.clear();
            hostJoinIds.clear();
            onlineMessage = "Registering online lobby...";
            lastHeartbeatMs = 0;
            gevrJavaCommand("lobbyCommand", gevrLobbyCreateCommand(gevrLobbyName(), netGetLobbyStage(),
                                                                   netGetLobbyWeaponSet(), netGetMaxPlayers())
                                                .c_str());
        } else if (f[0] == "LIST_BEGIN") {
            onlineLobbies.clear();
            listLoading = true;
        } else if (f[0] == "LIST_END") {
            listLoading = false;
        } else if (f[0] == "LOBBY" && f.size() >= 8) {
            onlineLobbies.push_back(
                {f[1], f[2], f[7], atoi(f[3].c_str()), atoi(f[4].c_str()), atoi(f[5].c_str()), atoi(f[6].c_str())});
        } else if (f[0] == "JOINED" && f.size() >= 4) {
            clientJoinId = f[1];
            g_joinedViaInternet = true;
            if (!netIceStartClient(f[1].c_str(), f[2].c_str(), f[3].c_str()))
                onlineMessage = "Could not start internet connection";
            else
                onlineMessage = "Finding a connection to host...";
        } else if (f[0] == "HOST_PEER" && f.size() >= 5 && netIsHost()) {
            if (netIcePeerCount() < netIceMaxPeers() &&
                netIceAddHostPeer(f[1].c_str(), gevrDecodeUrl64(f[2]).c_str(), f[3].c_str(), f[4].c_str()))
                hostJoinIds.push_back(f[1]);
        } else if (f[0] == "ANSWER" && f.size() >= 3) {
            if (!netIceApplyAnswer(f[1].c_str(), gevrDecodeUrl64(f[2]).c_str()))
                onlineMessage = "Internet connection failed";
        } else if (f[0] == "ERROR" && f.size() >= 2) {
            listLoading = false;
            onlineMessage = f[1];
        }
    }
    char iceSdp[JUICE_MAX_SDP_STRING_LEN];
    if (!clientJoinId.empty() && netIceTakeDescription(clientJoinId.c_str(), iceSdp, sizeof(iceSdp)))
        gevrJavaCommand("lobbyCommand", ("offer|" + gevrEncodeUrl64(iceSdp)).c_str());
    if (!clientJoinId.empty()) {
        const char *status = netIceStatus(clientJoinId.c_str());
        if (status)
            onlineMessage = status;
    }
    for (const std::string &id : hostJoinIds) {
        if (netIceTakeDescription(id.c_str(), iceSdp, sizeof(iceSdp)))
            gevrJavaCommand("lobbyCommand", ("answer|" + id + "|" + gevrEncodeUrl64(iceSdp)).c_str());
        const char *status = netIceStatus(id.c_str());
        if (status && strcmp(status, "Internet connection ended") != 0 && netIsHost())
            onlineMessage = status;
    }
    netIcePoll();

    if (netIsHost()) {
        // Launcher idle kick: if host is inactive for 10 minutes, stop hosting
        static uint32_t s_launcher_host_act_ms = 0;
        static float s_last_px = 0.0f, s_last_py = 0.0f;
        const uint32_t actNow = SDL_GetTicks();
        if (s_launcher_host_act_ms == 0)
            s_launcher_host_act_ms = actNow;
        ImGuiIO &io = ImGui::GetIO();
        bool act =
            io.MouseDown[0] || fabsf(io.MousePos.x - s_last_px) > 2.0f || fabsf(io.MousePos.y - s_last_py) > 2.0f;
        s_last_px = io.MousePos.x;
        s_last_py = io.MousePos.y;
        if (get_button_state(0, "thumbstick_click") || get_button_state(1, "thumbstick_click") ||
            get_button_state(0, "a") || get_button_state(0, "b") || get_button_state(0, "x") ||
            get_button_state(0, "y") || get_button_state(1, "a") || get_button_state(1, "b") ||
            get_button_state(1, "x") || get_button_state(1, "y") || get_button_state(0, "trigger") ||
            get_button_state(1, "trigger") || get_button_state(0, "grip") || get_button_state(1, "grip")) {
            act = true;
        }
        if (act) {
            s_launcher_host_act_ms = actNow;
        } else if (actNow - s_launcher_host_act_ms >= 10 * 60 * 1000) {
            gevrJavaCommand("lobbyCommand", "stop");
            netDiscoveryStopBroadcasting();
            netDisconnect();
            netIceStop();
            hostedCode.clear();
            hostJoinIds.clear();
            onlineMessage = "Hosting stopped due to inactivity (10 min idle)";
            s_launcher_host_act_ms = 0;
        }

        int pCount = netGetConnectedPlayerCount();
        int maxP = netGetMaxPlayers();
        const uint32_t heartbeatNow = SDL_GetTicks();
        if (netIsHost() && (heartbeatNow - lastHeartbeatMs > 5000 || lastHeartbeatMs == 0)) {
            lastHeartbeatMs = heartbeatNow;
            // the count too: the host may change it after registering the lobby
            const std::string refresh = "refresh|" + std::to_string(pCount) + "|" + (pCount < maxP ? "1" : "0") +
                                        "|" + std::to_string(maxP);
            gevrJavaCommand("lobbyCommand", refresh.c_str());
        }
    }
    if (netIsActive() && !netIsHost() && netGetState() == NET_STATE_INGAME) {
        netApplyMatchConfig();
        // the boot loads g_StageNum (main.c); a co-op party's menus loaded twice so,
        // and the Rare logo and folder music played twice over (#94)
        if (netGetMatchConfig()->mode != NET_MODE_COOP)
            bossSetLoadedStage(g_StageNum);
        startMatch = true;
        open = false;
    }
    static int sentSlot = -1;
    if (!netIsActive())
        sentSlot = -1;
    if (netIsActive() && !netIsHost() && netGetLocalSlot() >= 0) {
        if (sentSlot != netGetLocalSlot()) {
            sentSlot = netGetLocalSlot();
            gevrSendLoadout();
        }
    }
    auto disconnect = [&]() {
        gevrJavaCommand("lobbyCommand", "stop");
        netDiscoveryStopBroadcasting();
        netDisconnect();
        netIceStop();
        hostedCode.clear();
        hostJoinIds.clear();
        clientJoinId.clear();
    };
    auto playerOptions = [&]() {
        ImGui::TextUnformatted("Your name:");
        ImGui::SameLine();
        ImGui::BeginDisabled(netIsActive());
        ImGui::SetNextItemWidth(ImGui::GetFontSize() * 16);
        ImGui::InputText("##playername", VrPlayerName, sizeof(VrPlayerName), ImGuiInputTextFlags_CallbackCharFilter,
                         nameCharFilter);
        if (ImGui::IsItemDeactivatedAfterEdit()) {
            size_t n = strlen(VrPlayerName);
            while (n > 0 && VrPlayerName[n - 1] == ' ')
                VrPlayerName[--n] = '\0';
            if (VrPlayerName[0] == ' ')
                memmove(VrPlayerName, VrPlayerName + strspn(VrPlayerName, " "), n + 1);
            vrEnsurePlayerName();
            vrSettingsSave();
        }
        ImGui::EndDisabled();
        characterRow("Character:", "##mpcharacter");
        loadoutRows("mpplayer");
        gevrTeamChoiceRow("##playerteam");
    };
    auto audioOptions = [&]() {
        bool micMuted = netVoiceIsMuted() != 0;
        if (ImGui::Checkbox("Mute microphone", &micMuted))
            netVoiceSetMuted(micMuted);
        ImGui::TextDisabled("%s", micMuted                   ? "Mic muted"
                                  : !netVoiceHasPermission() ? "No mic access (listen only)"
                                  : netVoiceCaptureFailed()  ? "Mic unavailable (listen only)"
                                                             : "Mic active");
        // Persistent Audio Volume controls
        int musicPct = (int)(((s32)get_mTrack2Vol() * 100 + 16383) / 32767);
        int voicePct = (int)(VrVoiceVolume * 100.0f + 0.5f);
        int sfxPct = (int)(VrSfxVolume * 100.0f + 0.5f);
        ImGui::TextUnformatted("Music Vol:");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(ImGui::GetFontSize() * 6.0f);
        if (ImGui::SliderInt("##mpmusicvol", &musicPct, 0, 100, "%d%%")) {
            uint16_t vol = (uint16_t)((musicPct * 32767 + 50) / 100);
            set_mTrack2Vol(vol);
            musicTrack1ApplySeqpVol(vol);
            musicTrack3ApplySeqpVol(vol);
            VrMusicVolume = (float)musicPct / 100.0f;
            vrSettingsSave();
        }
        ImGui::SameLine(0.0f, ImGui::GetFontSize() * 1.5f);
        ImGui::TextUnformatted("SFX Vol:");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(ImGui::GetFontSize() * 6.0f);
        if (ImGui::SliderInt("##mpsfxvol", &sfxPct, 0, 100, "%d%%")) {
            VrSfxVolume = (float)sfxPct / 100.0f;
            gevrSndApplySfxVolume((uint16_t)((sfxPct * 32767 + 50) / 100));
            vrSettingsSave();
        }
        ImGui::SameLine(0.0f, ImGui::GetFontSize() * 1.5f);
        ImGui::TextUnformatted("Voice Vol:");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(ImGui::GetFontSize() * 6.0f);
        if (ImGui::SliderInt("##mpvoicevol", &voicePct, 0, 100, "%d%%")) {
            VrVoiceVolume = (float)voicePct / 100.0f;
            vrSettingsSave();
        }
        ImGui::Separator();

        ImGui::TextUnformatted("Voice mode:");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(ImGui::GetFontSize() * 10.0f);
        int voiceMode = netIsActive() ? netGetMatchConfig()->voice_mode : VrMpVoiceMode;
        ImGui::BeginDisabled(netIsActive() ? !netIsHost() : subTab != 0);
        if (namedCombo("##mpvoicemode", 2, netVoiceModeName, &voiceMode)) {
            VrMpVoiceMode = voiceMode;
            if (netIsHost())
                gevrNetConfigSet(CFG_VOICE_MODE, voiceMode);
            else
                vrSettingsSave();
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::TextDisabled("%s", netIsActive() && !netIsHost() ? "Host chooses"
                                                                : "Proximity: 10% floor / Couch: full volume");

        if (netScenarioHasTeams(netIsActive() ? netGetMatchConfig()->scenario : VrMpScenario)) {
            ImGui::TextDisabled("Teams: teammates full volume; opponents proximity.");
        }
    };
    ImGui::TextColored(gold, "ONLINE MULTIPLAYER");
    gevrLauncherBeginBody("##mpbody");
    if (ImGui::BeginTabBar("##mprole")) {
        for (int role = 0; role < 2; role++) {
            if (!ImGui::BeginTabItem(role == 0 ? "Host" : "Join"))
                continue;
            subTab = role;
            if (role == 0) {
                if (netIsActive() && !netIsHost())
                    ImGui::TextWrapped("Disconnect before hosting another game.");
                else if (ImGui::BeginTabBar("##hostpages")) {
                    if (ImGui::BeginTabItem("Lobby")) {
                        const bool hosting = netIsHost();
                        if (!hosting) {
                            if (ImGui::RadioButton("Public game", VrMpVisibility == 0)) {
                                VrMpVisibility = 0;
                                vrSettingsSave();
                            }
                            ImGui::SameLine();
                            if (ImGui::RadioButton("Private game", VrMpVisibility == 1)) {
                                VrMpVisibility = 1;
                                vrSettingsSave();
                            }
                        }
                        // Deathmatch or co-op: a solo mission played by the party (#94).
                        ImGui::Text("Mode:");
                        ImGui::SameLine();
                        if (ImGui::RadioButton("Deathmatch", VrMpMode != NET_MODE_COOP)) {
                            VrMpMode = NET_MODE_DEATHMATCH;
                            gevrHostChoiceChanged();
                        }
                        ImGui::SameLine();
                        if (ImGui::RadioButton("Co-op mission", VrMpMode == NET_MODE_COOP)) {
                            VrMpMode = NET_MODE_COOP;
                            gevrHostChoiceChanged();
                        }
                        if (VrMpMode == NET_MODE_COOP) {
                            // the game's own menus decide the rest: each player's folder, then
                            // the host's mission, difficulty and briefing (net_coop_menu.c)
                            ImGui::TextDisabled("The campaign, up to %d players. Each player picks a folder;", NET_COOP_MAX_PLAYERS);
                            ImGui::TextDisabled("the host picks missions. Players can join or leave any time.");
                        }
                        // The stage and the weapons, before hosting and in the lobby too,
                        // where a change reaches everyone (gevrHostChoiceChanged).
                        int stageIdx = netStageIndexOf((uint8_t)VrMpStage);
                        if (stageIdx < 0)
                            stageIdx = 9;
                        if (VrMpMode != NET_MODE_COOP) {
                            ImGui::Text("Stage:");
                            ImGui::SameLine();
                            ImGui::SetNextItemWidth(ImGui::GetFontSize() * 10.0f);
                            if (namedCombo("##stagecombo", netStageCount(), netStageName, &stageIdx)) {
                                VrMpStage = netStage(stageIdx)->level_id;
                                gevrHostChoiceChanged();
                            }
                            // The player count beside it: any stage takes 2..8, the host's
                            // call; a team scenario takes its own size.
                            ImGui::SameLine();
                            if (netScenarioHasTeams(VrMpScenario)) {
                                ImGui::TextDisabled("Players: %d (teams)", netTeamRequiredPlayers(VrMpScenario));
                            } else {
                                ImGui::Text("Players:");
                                ImGui::SameLine();
                                ImGui::SetNextItemWidth(ImGui::GetFontSize() * 6.0f);
                                int countIdx = VrMpMaxPlayers - 2;
                                const int minPlayers = hosting ? netLobbyMinPlayers() : 2;
                                if (gevrNamedCombo("##playerscombo", GEVR_MAX_PLAYERS - 1, playerCountName, &countIdx, false,
                                                   [&](int n) { return n + 2 < minPlayers; })) {
                                    VrMpMaxPlayers = countIdx + 2;
                                    gevrHostChoiceChanged();
                                }
                            }
                            if (VrMpScenario == SCENARIO_MWTGG) {
                                ImGui::TextDisabled("Weapons: Golden Gun (the scenario's own set)");
                            } else {
                                ImGui::Text("Weapons:");
                                ImGui::SameLine();
                                ImGui::SetNextItemWidth(ImGui::GetFontSize() * 10.0f);
                                if (namedCombo("##weaponcombo", netWeaponSetCount(), netWeaponSetName, &VrMpWeaponSet))
                                    gevrHostChoiceChanged();
                                if (VrMpWeaponSet == NET_WEAPON_SET_CUSTOM) {
                                    bool changed = false;
                                    for (int i = 0; i < 4; i++) {
                                        char id[24];
                                        snprintf(id, sizeof(id), "##custom%d", i);
                                        if (i)
                                            ImGui::SameLine();
                                        ImGui::SetNextItemWidth(ImGui::GetFontSize() * 8.0f);
                                        changed |= gunCombo(id, &VrMpCustom[i]);
                                    }
                                    if (changed)
                                        gevrHostChoiceChanged();
                                }
                            }
                        } // deathmatch: stage, players and weapons

                        if (hosting) {
                            if (!hostedCode.empty())
                                ImGui::TextColored(gold, "%s CODE: %s", VrMpVisibility ? "PRIVATE" : "PUBLIC",
                                                   hostedCode.c_str());
                            gevrTeamChoiceRow("##hostteam");
                            gevrLobbyRoster(gold);
                            if (netScenarioHasTeams(netGetMatchConfig()->scenario) && !netTeamRosterReady())
                                ImGui::TextDisabled("Complete teams before countdown; warmup remains available.");
                        }
                        ImGui::EndTabItem();
                    }
                    if (ImGui::BeginTabItem("Match")) {
                        if (VrMpMode == NET_MODE_COOP) {
                            // co-op: the mission and difficulty decide the rest (Lobby tab)
                            bool ff = VrMpFriendlyFire != 0;
                            if (ImGui::Checkbox("Friendly fire", &ff)) {
                                VrMpFriendlyFire = ff;
                                gevrHostChoiceChanged();
                            }
                        } else
                        gevrMatchOptions();
                        ImGui::BeginDisabled(netIsActive() && !netIsHost());
                        unsigned cap; bool equalized=netGetHostEqualization(&cap)!=0;
                        if (ImGui::Checkbox("Host hit equalization", &equalized)) netSetHostEqualization(equalized,cap);
                        int limit=(int)cap;
                        if (ImGui::SliderInt("Host delay cap (ms)",&limit,0,80)) netSetHostEqualization(equalized,(unsigned)limit);
                        ImGui::EndDisabled();
                        char delays[128];netHostEqualizationText(delays,sizeof(delays));
                        if (delays[0]) ImGui::TextDisabled("%s",delays);

                        if (VrMpMode != NET_MODE_COOP && netScenarioHasTeams(VrMpScenario)) {
                            bool ff = VrMpFriendlyFire != 0;
                            if (ImGui::Checkbox("Friendly fire", &ff)) {
                                VrMpFriendlyFire = ff;
                                gevrHostChoiceChanged();
                            }
                        }
                        ImGui::EndTabItem();
                    }
                    if (ImGui::BeginTabItem("Fun")) {
                        gevrFunOptions(true);
                        ImGui::EndTabItem();
                    }
                    if (ImGui::BeginTabItem("Player")) {
                        playerOptions();
                        ImGui::EndTabItem();
                    }
                    if (ImGui::BeginTabItem("Audio")) {
                        audioOptions();
                        ImGui::EndTabItem();
                    }
                    if (ImGui::BeginTabItem("Favorites")) {
                        gevrMatchOptions(true);
                        ImGui::EndTabItem();
                    }
                    ImGui::EndTabBar();
                }
            } else {
                const bool connected = netIsActive() && !netIsHost() && netGetState() != NET_STATE_CONNECTING;
                if (ImGui::BeginTabBar("##joinpages")) {
                    const char *titles[] = {connected ? "Lobby###publicpane" : "Public###publicpane",
                                            "Private",
                                            "LAN",
                                            "Direct IP",
                                            "Player",
                                            "Audio"};
                    for (int pane = 0; pane < 6; pane++) {
                        if (!ImGui::BeginTabItem(titles[pane]))
                            continue;
                        joinMethod = pane;
                        if (pane == 4)
                            playerOptions();
                        else if (pane == 5) {
                            audioOptions();
                            if (connected)
                                gevrFunOptions(false);
                        } else if (netIsHost())
                            ImGui::TextWrapped("Stop hosting before joining another game.");
                        else if (netGetState() == NET_STATE_CONNECTING)
                            ImGui::TextUnformatted("Connecting to host...");
                        else if (connected) {
                            const NetMatchConfig *cfg = netGetMatchConfig();
                            if (cfg->mode == NET_MODE_COOP)
                                ImGui::Text("Co-op campaign: the host picks the missions");
                            else
                                ImGui::Text("%s / %s / %s", stageNameById(cfg->stage), netWeaponSetName(cfg->weapon_set),
                                            netScenarioName(cfg->scenario));
                            gevrTeamChoiceRow("##clientteam");
                            gevrLobbyRoster(gold);
                            ImGui::TextDisabled("Waiting for the host to launch.");
                        } else {
                            const bool joiningOnline = !clientJoinId.empty();
                            if (!clientJoinId.empty()) {
                                if (ImGui::SmallButton("Cancel internet join")) {
                                    gevrJavaCommand("lobbyCommand", "stop");
                                    netIceStop();
                                    clientJoinId.clear();
                                    onlineMessage.clear();
                                }
                            }
                            if (!onlineMessage.empty())
                                ImGui::TextWrapped("%s", onlineMessage.c_str());

                            if (joiningOnline)
                                ImGui::BeginDisabled();

                            // 1. Public Internet Games
                            if (joinMethod == 0) {
                                const uint32_t listNow = SDL_GetTicks();
                                if (listNow - lastListMs > 8000 || lastListMs == 0) {
                                    lastListMs = listNow;
                                    listLoading = true;
                                    gevrJavaCommand("lobbyCommand",
                                                    ("list|" + std::to_string(GEVR_NET_VERSION)).c_str());
                                }
                                if (ImGui::SmallButton("Refresh list")) {
                                    listLoading = true;
                                    gevrJavaCommand("lobbyCommand",
                                                    ("list|" + std::to_string(GEVR_NET_VERSION)).c_str());
                                }
                                if (listLoading)
                                    ImGui::TextDisabled("Loading internet games...");
                                else if (onlineLobbies.empty())
                                    ImGui::TextDisabled("No open internet games found.");
                                for (const OnlineLobby &game : onlineLobbies) {
                                    char label[180];
                                    snprintf(label, sizeof(label), "%s  -  %s  -  %d/%d players%s##online%s",
                                             game.name.c_str(), listedGameLabel(game.stage, game.weapons).c_str(),
                                             game.players, game.maxPlayers,
                                             game.phase == "warmup"        ? " (warmup)"
                                             : game.phase == "in_progress" ? " (in progress)"
                                                                           : "",
                                             game.code.c_str());
                                    if (ImGui::Button(label, ImVec2(-1, 0))) {
                                        gevrJavaCommand("requestVoicePermission", "");
                                        netDisconnect();
                                        netIceStop();
                                        clientJoinId.clear();
                                        onlineMessage = "Joining " + game.name + "...";
                                        gevrJavaCommand(
                                            "lobbyCommand",
                                            ("join|" + game.code + "|" + std::to_string(GEVR_NET_VERSION)).c_str());
                                    }
                                    if (ImGui::IsItemHovered()) {
                                        ImGui::SetTooltip(
                                            "Game: %s\nRoom Code: %s\nPhase: %s\nPlayers: %d/%d\nProtocol: %d",
                                            game.name.c_str(), game.code.c_str(), game.phase.c_str(), game.players,
                                            game.maxPlayers, GEVR_NET_VERSION);
                                    }
                                }
                            }

                            // 2. Private Room Code
                            if (joinMethod == 1) {
                                ImGui::Text("Enter host's private code:");
                                ImGui::SetNextItemWidth(-1);
                                ImGui::InputText("##privatecode", privateCode, sizeof(privateCode));
                                if (ImGui::Button("Join by code", ImVec2(-1, 0))) {
                                    gevrJavaCommand("requestVoicePermission", "");
                                    netDisconnect();
                                    netIceStop();
                                    clientJoinId.clear();
                                    onlineMessage = "Looking up private game...";
                                    gevrJavaCommand("lobbyCommand", ("join|" + std::string(privateCode) + "|" +
                                                                     std::to_string(GEVR_NET_VERSION))
                                                                        .c_str());
                                }
                            }

                            // 3. LAN Games Discovered
                            netDiscoveryInit();
                            int count = netDiscoveryGetServerCount();
                            char lanLabel[80];
                            snprintf(lanLabel, sizeof(lanLabel), "LAN Games Discovered (%d)###acc_lan", count);
                            if (joinMethod == 2) {
                                if (count == 0) {
                                    ImGui::TextDisabled("Searching your Wi-Fi network for GoldenEye VR hosts...");
                                } else {
                                    for (int i = 0; i < count; i++) {
                                        const NetDiscoveredServer *srv = netDiscoveryGetServer(i);
                                        const int cap = srv->max_players;
                                        const bool full = !srv->joinable;
                                        char label[160];
                                        snprintf(label, sizeof(label), "%s  -  %s  -  %d/%d players%s##srv%d",
                                                 srv->server_name,
                                                 listedGameLabel(srv->stage_num, srv->weapon_set).c_str(),
                                                 srv->player_count, cap,
                                                 full                                  ? " (full)"
                                                 : srv->phase == NET_PHASE_WARMUP      ? " (warmup)"
                                                 : srv->phase == NET_PHASE_IN_PROGRESS ? " (in progress)"
                                                                                       : "",
                                                 i);
                                        if (full)
                                            ImGui::BeginDisabled();
                                        if (ImGui::Button(label, ImVec2(-1, 0))) {
                                            gevrJavaCommand("requestVoicePermission", "");
                                            gevrJavaCommand("lobbyCommand", "stop");
                                            netIceStop();
                                            clientJoinId.clear();
                                            g_joinedViaInternet = false;
                                            netConnect(srv->host_ip, srv->port);
                                        }
                                        if (full)
                                            ImGui::EndDisabled();
                                    }
                                }
                            }

                            // 4. Connect Directly via IP
                            if (joinMethod == 3) {
                                ImGui::Text("Enter host IP address:");
                                ImGui::SetNextItemWidth(-1);
                                ImGui::InputText("##directip", directIp, sizeof(directIp));
                                if (ImGui::Button("Connect", ImVec2(-1, 0))) {
                                    gevrJavaCommand("requestVoicePermission", "");
                                    gevrJavaCommand("lobbyCommand", "stop");
                                    netIceStop();
                                    clientJoinId.clear();
                                    g_joinedViaInternet = false;
                                    netConnect(directIp, GEVR_DEFAULT_PORT);
                                }
                            }
                            if (joiningOnline)
                                ImGui::EndDisabled();
                        }
                        ImGui::EndTabItem();
                    }
                    ImGui::EndTabBar();
                }
            }
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
    ImGui::EndChild();
    ImGui::Separator();
    // Fixed footer: long rosters, popups and status messages cannot push it away.
    gevrLauncherStatus(onlineMessage.empty()
                           ? (netIsHost() ? (netLobbyCanLaunch() ? "Ready to launch warmup."
                                                                 : "Waiting for joined players to ready up.")
                                          : "Choose a game or connection method.")
                           : onlineMessage.c_str());
    int pCount = netGetConnectedPlayerCount();
    float h = ImGui::GetFrameHeight() * 1.5f;
    if (netIsHost()) {
        ImGui::BeginDisabled(!netLobbyCanLaunch());
        if (ImGui::Button("Launch", ImVec2(0, h))) {

            if (netLobbyHostLaunchMatch()) {
                // the lobby service lists one player as warming up (it refuses "in progress" alone)
                gevrJavaCommand("lobbyCommand", (std::string("phase|") + (pCount > 1 ? "in_progress" : "warmup") + "|" +
                                                 std::to_string(pCount))
                                                    .c_str());
                // the game's globals from the lobby's config, as every headset sets them before a load
                netApplyMatchConfig();
                if (netGetMatchConfig()->mode != NET_MODE_COOP) // the boot loads it (above)
                    bossSetLoadedStage(g_StageNum);
                startMatch = true;
                open = false;
            }
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Stop Hosting", ImVec2(0, h))) {

            gevrJavaCommand("lobbyCommand", "stop");
            netDiscoveryStopBroadcasting();
            netDisconnect();
            netIceStop();
            hostedCode.clear();
            hostJoinIds.clear();
        }
        ImGui::SameLine();
    } else if (netIsActive()) {
        int slot = netGetLocalSlot();
        if (slot >= 0 && netGetState() != NET_STATE_CONNECTING) {
            bool ready = netGetLobbyState()->slots[slot].ready != 0;
            ImGui::BeginDisabled(netScenarioHasTeams(netGetMatchConfig()->scenario) &&
                                 netGetSlotTeam(slot) == NET_TEAM_NONE);
            if (ImGui::Checkbox("Ready", &ready))
                netLobbySetReady(ready);
            ImGui::EndDisabled();
            ImGui::SameLine();
        }
        if (ImGui::Button("Disconnect", ImVec2(0, h)))
            disconnect();
        ImGui::SameLine();
    } else if (!clientJoinId.empty()) {
        if (ImGui::Button("Cancel Join", ImVec2(0, h))) { disconnect(); onlineMessage.clear(); }
        ImGui::SameLine();
    } else if (subTab == 0) {
        if (ImGui::Button("Start Hosting", ImVec2(0, h))) {
            // the game's name in the LAN and internet lists: the host's
            const std::string gameName = gevrLobbyName();
            if (netHostStart(GEVR_DEFAULT_PORT)) {
                netSetGameName(gameName.c_str()); // the clients keep it: the LAN beacon of a migrated host
                g_joinedViaInternet = false;
                gevrJavaCommand("requestVoicePermission", "");
                netIceStartHost();
                netDiscoveryInit();
                netDiscoveryStartBroadcasting(gameName.c_str(), GEVR_DEFAULT_PORT);
                netLobbySetCharacter((uint8_t)VrMpChr);
                gevrSendLoadout();
                {
                    const NetMatchConfig c = gevrLauncherConfig();
                    netLobbySetConfig(&c);
                }
                hostedCode.clear();
                hostJoinIds.clear();
                onlineMessage = "Registering online lobby...";
                const std::string command =
                    gevrLobbyCreateCommand(gameName, netGetLobbyStage(), netGetLobbyWeaponSet(), netGetMaxPlayers());
                gevrJavaCommand("lobbyCommand", command.c_str());
            } else
                onlineMessage = "Could not start the local game host";
        }
        ImGui::SameLine();
    }
    if (ImGui::Button("Back", ImVec2(0, h))) {
        if (netIsHost())
            disconnect();
        open = false;
    }
}

/* The launcher is no longer pumping signaling after the stage starts. Keep the
 * tiny JNI event drain on the game thread; all HTTP work stays on LobbyClient's
 * background executor. */
extern "C" void gevrLobbyGameTick(void)
{
    static Uint32 lastTick = 0;
    static Uint32 lastRefresh = 0;
    static NetPhase lastPhase = NET_PHASE_WAITING;
    static bool keepalivePaused = false;
    static std::vector<std::string> pendingAnswers;
    const Uint32 now = SDL_GetTicks();
    if (now - lastTick < 250) return;
    lastTick = now;

    // Host migration (net_core.c netHostLost): elected, this headset serves
    // the same match. The ICE transport first when there is an internet lobby
    // to resume (netSetVirtualTransport reaches the ENet host it then makes),
    // the lobby under its owner token, and the LAN beacon under the old name.
    if (netTakeHostTakeover()) {
        const bool internet = netGetLobbyCode()[0] != '\0';
        netIceStop();
        if (internet) netIceStartHost();
        if (netHostTakeOver(GEVR_DEFAULT_PORT)) {
            if (internet) {
                gevrJavaCommand("lobbyCommand", (std::string("resume|") + netGetLobbyCode() + "|" + netGetLobbyToken() + "|" +
                    std::to_string(netGetMaxPlayers()) + "|" +
                    (netGetPhase() == NET_PHASE_IN_PROGRESS ? "in_progress" : "warmup") + "|1|" + gevrLobbyName()).c_str());
            }
            netDiscoveryInit();
            netDiscoveryStartBroadcasting(netGetGameName(), GEVR_DEFAULT_PORT);
            vr_log("launcher: took the match over%s, beacon '%s'", internet ? " with the internet lobby" : "", netGetGameName());
            lastPhase = NET_PHASE_WAITING;
            lastRefresh = 0;
        }
    }

    if (netIsHost()) {
        for (int i = 0; i < 32; ++i) {
            const std::string event = gevrJavaString("lobbyEvent");
            if (event.empty()) break;
            const auto fields = gevrSplitLobbyEvent(event);
            if (fields[0] == "CREATED" && fields.size() >= 3) {
                netSetLobbyHandoff(fields[1].c_str(), fields[2].c_str());
                lastPhase = NET_PHASE_WAITING;
                lastRefresh = 0;
            } else if (fields[0] == "LOBBY_LOST" && fields.size() >= 3) {
                pendingAnswers.clear();
                const int players = netGetLivePlayerCount();
                if (players < netGetMaxPlayers() &&
                    (netGetPhase() != NET_PHASE_IN_PROGRESS || players >= 2)) {
                    vr_log("launcher: lobby %s lost (%s); registering again", fields[1].c_str(), fields[2].c_str());
                    gevrJavaCommand("lobbyCommand", gevrLobbyCreateCommand(gevrLobbyName(),
                        netGetLobbyStage(), netGetLobbyWeaponSet(), netGetMaxPlayers()).c_str());
                } else {
                    vr_log("launcher: lobby %s lost (%s); match is not joinable", fields[1].c_str(), fields[2].c_str());
                }
            } else if (fields[0] == "HOST_PEER" && fields.size() >= 5 && netIcePeerCount() < netIceMaxPeers() &&
                netIceAddHostPeer(fields[1].c_str(), gevrDecodeUrl64(fields[2]).c_str(),
                                  fields[3].c_str(), fields[4].c_str()))
                pendingAnswers.push_back(fields[1]);
        }
        for (auto it = pendingAnswers.begin(); it != pendingAnswers.end();) {
            char sdp[JUICE_MAX_SDP_STRING_LEN];
            if (netIceTakeDescription(it->c_str(), sdp, sizeof(sdp))) {
                gevrJavaCommand("lobbyCommand", ("answer|" + *it + "|" + gevrEncodeUrl64(sdp)).c_str());
                it = pendingAnswers.erase(it);
            } else if (netIceStatus(it->c_str())) {
                it = pendingAnswers.erase(it);
            } else ++it;
        }
        const NetPhase phase = netGetPhase();
        const bool running = gevrVrSessionRunning() != 0;
        if (!running && !keepalivePaused) {
            vr_log("launcher: lobby keepalive paused (XR session not running)");
            keepalivePaused = true;
        } else if (running && keepalivePaused) {
            vr_log("launcher: lobby keepalive resumed (XR session running)");
            keepalivePaused = false;
            lastRefresh = 0;
        }
        if (running && (phase != lastPhase || now - lastRefresh >= 5000 || lastRefresh == 0)) {
            lastRefresh = now;
            const char *phaseName = phase == NET_PHASE_IN_PROGRESS ? "in_progress" : "warmup";
            if (phase != lastPhase) {
                lastPhase = phase;
                gevrJavaCommand("lobbyCommand", (std::string("phase|") + phaseName + "|" +
                                std::to_string(netGetLivePlayerCount())).c_str());
            } else {
                // live connections: a player yet to come back after a host
                // change must find the lobby open
                const int players = netGetLivePlayerCount();
                gevrJavaCommand("lobbyCommand", (std::string("refresh|") + std::to_string(players) +
                                "|" + (players < netGetMaxPlayers() ? "1" : "0") +
                                "|" + std::to_string(netGetMaxPlayers())).c_str());
            }
        }
    } else {
        keepalivePaused = false;
        lastPhase = NET_PHASE_WAITING;
        lastRefresh = 0;
        pendingAnswers.clear();

        // A client whose host left: to the elected one, the way this headset
        // came in - the internet lobby (the new host resumes it; a join before
        // its first heartbeat is refused and asked again) or the LAN beacon
        // under the game's name, skipping the old host's while it lingers.
        // The ICE peer, once connected, connects ENet itself (net_ice.cpp).
        static Uint32 attemptMs = 0;
        static bool wasConnecting = false;
        static std::string rejoinId;
        const char *oldIp = "";
        if (netMigrationWantsRejoin(&oldIp)) {
            const bool internet = g_joinedViaInternet && netGetLobbyCode()[0] != '\0';
            const bool connecting = netMigrationConnecting();
            if (wasConnecting && !connecting) {
                // the attempt failed: start over
                rejoinId.clear();
                attemptMs = 0;
                if (internet) netIceStop();
            }
            wasConnecting = connecting;
            for (int i = 0; i < 32; ++i) {
                const std::string event = gevrJavaString("lobbyEvent");
                if (event.empty()) break;
                const auto f = gevrSplitLobbyEvent(event);
                if (f[0] == "JOINED" && f.size() >= 4) {
                    rejoinId = f[1];
                    if (!netIceStartClient(f[1].c_str(), f[2].c_str(), f[3].c_str())) rejoinId.clear();
                } else if (f[0] == "ANSWER" && f.size() >= 3 && !rejoinId.empty()) {
                    netIceApplyAnswer(f[1].c_str(), gevrDecodeUrl64(f[2]).c_str());
                } else if (f[0] == "ERROR" && f.size() >= 2) {
                    vr_log("launcher: rejoin: %s", f[1].c_str());
                    rejoinId.clear();
                }
            }
            char sdp[JUICE_MAX_SDP_STRING_LEN];
            if (!rejoinId.empty() && netIceTakeDescription(rejoinId.c_str(), sdp, sizeof(sdp)))
                gevrJavaCommand("lobbyCommand", ("offer|" + gevrEncodeUrl64(sdp)).c_str());
            if (!connecting && now - attemptMs >= 4000) {
                if (internet) {
                    if (rejoinId.empty()) {
                        attemptMs = now;
                        netIceStop();
                        gevrJavaCommand("lobbyCommand", (std::string("join|") + netGetLobbyCode() + "|" +
                                        std::to_string(GEVR_NET_VERSION)).c_str());
                        vr_log("launcher: rejoining lobby %s after the host change", netGetLobbyCode());
                    }
                } else {
                    netDiscoveryInit();
                    for (int i = 0; i < netDiscoveryGetServerCount(); ++i) {
                        const NetDiscoveredServer *srv = netDiscoveryGetServer(i);
                        if (strcmp(srv->server_name, netGetGameName()) != 0 || strcmp(srv->host_ip, oldIp) == 0) continue;
                        attemptMs = now;
                        vr_log("launcher: rejoining '%s' at %s after the host change", srv->server_name, srv->host_ip);
                        netConnect(srv->host_ip, srv->port);
                        break;
                    }
                }
            }
        } else {
            rejoinId.clear();
            attemptMs = 0;
            wasConnecting = false;
        }
    }
    netIcePoll();
}

extern "C" void gevrLobbySessionStopped(void)
{
    // A host leaving players behind hands the internet lobby to the one they
    // elect (net_core.c netHostLost): left, not deleted.
    const bool handover = netIsHost() && netGetState() == NET_STATE_INGAME && netGetLivePlayerCount() > 1;
    // ENet's goodbye first: over the internet it travels through the ICE
    // peers, and with those torn down first it was dropped, so the host kept
    // a stale player until the timeout (the quit-and-rejoin hang, user).
    netDisconnect();
    netIceStop();
    netDiscoveryShutdown();
    gevrJavaCommand("lobbyCommand", handover ? "leave" : "stop");
}

// Laser pointer (vr_openxr.cpp gevrVrScreenPointer): a controller pointed at
// the screen is the mouse, and a trigger clicks. Returns whether it points.
//
// The pointer takes over only when it really moves (over 1% of the screen) or
// a trigger is pulled, and hands back as soon as the stick or a button is
// used: ImGui hides the gamepad focus on every mouse move, and a controller
// lying still still jitters, which left A doing nothing.
bool feedPointer(ImGuiIO &io, bool navUsed, bool blockClick=false) {
    static GevrPointerOwner owner;
    float u = 0, v = 0;
    const bool on = gevrVrScreenPointer(&u, &v) != 0;
    const bool trig = !blockClick && (get_button_state(1, "trigger") || get_button_state(0, "trigger"));
    bool owns = owner.update(on, u, v, trig, navUsed);
    io.AddMousePosEvent(owns ? u * kTexW : -FLT_MAX, owns ? v * kTexH : -FLT_MAX);
    io.AddMouseButtonEvent(0, owns && trig);
    return owns;
}

// Returns whether the stick or a button drove the focus this frame.
// The test hook's START (1000) presses the launcher's Start directly.
bool s_injectStart = false;

bool feedGamepad(ImGuiIO &io) {
    static Injected inj;
    pollInjected(inj);
    if (inj.mask & 0x1000) {
        s_injectStart = true;
    }
    static bool pickerHookDone = false;
    if ((inj.mask & 0x0020) && !pickerHookDone) { // test hook: L opens the file picker
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
    if (inj.x || inj.y)
        s = {inj.x / 80.0f, inj.y / 80.0f};
    const float t = 0.5f;
    const bool back = get_button_state(1, "b") || get_button_state(0, "y") || (inj.mask & 0x4000);
    const bool select = get_button_state(1, "a") || get_button_state(0, "x") || (inj.mask & 0x8000);
    const bool navUsed = fabsf(s.x) > t || fabsf(s.y) > t || back || select;
    const bool pointing = feedPointer(io, navUsed);
    io.AddKeyEvent(ImGuiKey_GamepadDpadUp, s.y > t);
    io.AddKeyEvent(ImGuiKey_GamepadDpadDown, s.y < -t);
    io.AddKeyEvent(ImGuiKey_GamepadDpadLeft, s.x < -t);
    io.AddKeyEvent(ImGuiKey_GamepadDpadRight, s.x > t);
    io.AddKeyEvent(
        ImGuiKey_GamepadFaceDown,
        gevrGamepadActivate(pointing, select, get_button_state(1, "trigger") || get_button_state(0, "trigger")));
    io.AddKeyEvent(ImGuiKey_GamepadFaceRight, back);
    return navUsed;
}

// The Quest system keyboard (AndroidManifest: oculus.software.overlay_keyboard).
// SDL_StartTextInput shows it over the launcher (SDLActivity's DummyEdit), and
// it types into SDL: text as SDL_TEXTINPUT, Backspace and Return as key
// presses. Those come on Android's UI thread, so they wait here, '\b' and '\r'
// standing for the two keys, and go to ImGui with the frame's other input.
SDL_mutex *s_kbdLock;
std::string s_kbdQueue;

int SDLCALL keyboardWatch(void *, SDL_Event *e)
{
    const bool text = e->type == SDL_TEXTINPUT;
    const bool key = e->type == SDL_KEYDOWN
        && (e->key.keysym.sym == SDLK_BACKSPACE || e->key.keysym.sym == SDLK_RETURN);
    if (!text && !key) return 1;
    SDL_LockMutex(s_kbdLock);
    if (key) s_kbdQueue += e->key.keysym.sym == SDLK_BACKSPACE ? '\b' : '\r';
    else for (const char *c = e->text.text; *c; c++) {
        if ((unsigned char)*c >= 0x20) s_kbdQueue += *c;   // Return comes as a key too
    }
    SDL_UnlockMutex(s_kbdLock);
    return 1;
}

void feedKeyboard(ImGuiIO &io)
{
    std::string q;
    SDL_LockMutex(s_kbdLock);
    q.swap(s_kbdQueue);
    SDL_UnlockMutex(s_kbdLock);
    if (!io.WantTextInput) return;   // no text box to type into
    size_t run = 0;
    for (size_t i = 0; i <= q.size(); i++) {
        if (i < q.size() && q[i] != '\b' && q[i] != '\r') continue;
        if (i > run) io.AddInputCharactersUTF8(q.substr(run, i - run).c_str());
        if (i < q.size()) {
            const ImGuiKey k = q[i] == '\b' ? ImGuiKey_Backspace : ImGuiKey_Enter;
            io.AddKeyEvent(k, true);
            io.AddKeyEvent(k, false);
        }
        run = i + 1;
    }
}

// Up while a text box has the focus: shown when one takes it, put away when it
// lets go (Return, or pointing elsewhere). Closed with its own button, it
// comes back on pointing at the box again.
void showKeyboard(const ImGuiIO &io)
{
    static bool shown = false;
    if (io.WantTextInput != shown) {
        shown = io.WantTextInput;
        if (shown) SDL_StartTextInput();
        else SDL_StopTextInput();
        vr_log("launcher: system keyboard %s", shown ? "up" : "down");
    } else if (shown && io.MouseClicked[0]) {
        SDL_StartTextInput();
    }
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
    s_kbdLock = SDL_CreateMutex();
    SDL_StopTextInput();         // no keyboard until a text box asks for it
    SDL_AddEventWatch(keyboardWatch, nullptr);
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
    bool romError = false;
    GevrLauncherSession launcherSession;

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
                romError = r.compare(0, 9, "Could not") == 0;
                launcherSession.romSelectionFinished();
                pickerPending = false;
            }
        }
        {
            const std::string picked = dataDir() + "/picked.z64";
            struct stat st;
            if (stat(picked.c_str(), &st) == 0) {
                RomInfo r = probeRom(picked);
                launcherSession.romSelectionFinished();
                if (r.good && rename(picked.c_str(), (dataDir() + "/ge.z64").c_str()) == 0) {
                    message = "ROM chosen.";
                    romError = false;
                    vr_log("launcher: picked ROM adopted");
                } else {
                    message = r.good ? "Could not replace the ROM in the data folder." :
                        "That file is not a GoldenEye 007 (USA) ROM: " + r.status + ".";
                    romError = true;
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
                    romError = false;
                    launcherSession.romSelectionFinished();
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

    // The app's versionName (android build.gradle), for the header beside the build.
    const std::string appVersion = gevrUpdaterStatus().installed;
    {
        char localVerStr[32];
        snprintf(localVerStr, sizeof(localVerStr), "v%s (%.7s)", appVersion.c_str(), gevrBuildId);
        netSetLocalAppVersion(localVerStr);
    }

    vr_log("launcher: open, v%s build %s (rom %s)", appVersion.c_str(), gevrBuildId,
           active.empty() ? "none" : active.c_str());

    // One look at the GitHub releases per launch (the answer arrives in the
    // background; the line under the ROM shows it). Polled a few times a
    // second rather than every frame: it crosses into Java.
    gevrUpdaterCommand("check");
    UpdateStatus upd;
    Uint32 lastUpdPoll = 0;
    bool reportPage = false;
    bool reportCrash = false;
    bool cheatPage = false, modsPage = false, mpPage = false, hapticsPage = false, throwingPage = false;
    Uint32 lastReportPoll = 0;
    while (!start) {
        // SDL's native thread may reach the launcher before MainActivity has
        // finished constructing the reporter. Poll after startup too.
        Uint32 reportNow = SDL_GetTicks();
        if (!lastReportPoll || reportNow - lastReportPoll >= 500) {
            lastReportPoll = reportNow;
            if (gevrJavaString("reportStatus") == "offer") {
                reportPage = true;
                reportCrash = true;
            }
        }
        SDL_PumpEvents();

        netPoll();
        netDiscoveryUpdate(SDL_GetTicks());

        if (!gevrVrPumpBegin()) {
            // The Quest menu can suppress rendering without stopping XR.
            // Keep the hosted lobby alive while that session still runs.
            static Uint32 lastNoFrameRefresh = 0;
            const Uint32 now = SDL_GetTicks();
            if (netIsHost() && gevrVrSessionRunning() &&
                (lastNoFrameRefresh == 0 || now - lastNoFrameRefresh >= 5000)) {
                lastNoFrameRefresh = now;
                const int players = netGetConnectedPlayerCount();
                gevrJavaCommand("lobbyCommand", (std::string("refresh|") + std::to_string(players) +
                    "|" + (players < netGetMaxPlayers() ? "1" : "0") +
                    "|" + std::to_string(netGetMaxPlayers())).c_str());
            }
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
        launcherSession.updateDebugCombo(get_button_state(0, "thumbstick_click"),
                                         get_button_state(1, "thumbstick_click"));
        io.DeltaTime = (now > last) ? (now - last) / 1000.0f : 1.0f / 72.0f;
        last = now;
        // Look for a ROM about once a second while there is none or the file
        // picker is out (copied over USB, or picked).
        {
            static Uint32 lastScan = 0;
            if ((!activeInfo.good || pickerPending) && now - lastScan > 1000) {
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
            // no stick navigation while the stick is resizing the screen
            if (!grabbing) feedGamepad(io);
            if (grabbing) {
                io.AddKeyEvent(ImGuiKey_GamepadDpadUp, false);
                io.AddKeyEvent(ImGuiKey_GamepadDpadDown, false);
                io.AddKeyEvent(ImGuiKey_GamepadDpadLeft, false);
                io.AddKeyEvent(ImGuiKey_GamepadDpadRight, false);
            }
            if (grabbing) feedPointer(io, false);
            feedKeyboard(io);
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

        // header: icon and title left, build stamp and report button stacked right
        const float headerY = ImGui::GetCursorPosY();
        const float headerH = ImGui::GetTextLineHeight() * 2.2f;
        if (iconTex) {
            ImGui::Image((ImTextureID)(intptr_t)iconTex, ImVec2(headerH, headerH));
            ImGui::SameLine();
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() + (headerH - ImGui::GetTextLineHeight()) * 0.5f);
        }
        ImGui::TextColored(ImVec4(1.0f, 0.84f, 0.47f, 1.0f), "GOLDENEYE VR");
        {
            char build[128];
            if (!appVersion.empty()) {
                snprintf(build, sizeof(build), "v%s  Build %s", appVersion.c_str(), gevrBuildId);
            } else {
                snprintf(build, sizeof(build), "Build %s", gevrBuildId);
            }
            const float w = ImGui::CalcTextSize(build).x;
            const float buttonW = ImGui::CalcTextSize("Send debug log").x + ImGui::GetStyle().FramePadding.x * 2;
            const float right = ImGui::GetWindowWidth() - ImGui::GetStyle().WindowPadding.x;
            ImGui::SetCursorPos(ImVec2(right - w, headerY));
            ImGui::TextDisabled("%s", build);
            ImGui::SetCursorPos(ImVec2(right - buttonW,
                headerY + ImGui::GetTextLineHeight() + ImGui::GetStyle().ItemSpacing.y));
            if (gevrLauncherDebugButton(launcherSession)) {
                reportPage = true;
                reportCrash = false;
            }
        }
        const float iconBottom = headerY + headerH + ImGui::GetStyle().ItemSpacing.y;
        if (ImGui::GetCursorPosY() < iconBottom) ImGui::SetCursorPosY(iconBottom);
        ImGui::Separator();

        // A good ROM starts collapsed. A failed pick opens it even if the old ROM is valid.
        const bool romReady = !active.empty() && activeInfo.good;
        const std::string romLabel = romError ? "ROM: selection error###rom" :
            active.empty() ? "ROM: choose a file###rom" : "ROM: " + activeInfo.status + "###rom";
        if (gevrLauncherRomHeader(launcherSession, romReady, romError, romLabel.c_str())) {
            gevrLauncherBeginRomDetails();
            if (active.empty()) {
                ImGui::TextColored(bad, "No ROM yet.");
                ImGui::SameLine();
                ImGui::TextWrapped("Choose your GoldenEye 007 (USA) ROM, or copy it over USB into "
                                   "Android/data/com.gevr.port/files/data (any name).");
                if (ImGui::Button("Choose ROM file...")) {
                    pickerPending = gevrOpenRomPicker();
                    romError = !pickerPending;
                    if (romError) launcherSession.romSelectionFinished();
                    message = pickerPending ? "Pick the ROM in the window that opened." : "Could not open the file picker.";
                }
                ImGui::SameLine();
                if (ImGui::Button("Look again")) {
                    romError = false;
                    scan();
                    if (active.empty()) message = "Still no ROM in that folder.";
                }
            } else {
                ImGui::TextColored(activeInfo.good ? good : bad, "ROM: %s", activeInfo.status.c_str());
                ImGui::SameLine();
                if (ImGui::SmallButton("Change...")) {
                    pickerPending = gevrOpenRomPicker();
                    romError = !pickerPending;
                    if (romError) launcherSession.romSelectionFinished();
                    message = pickerPending ? "Pick the ROM in the window that opened." : "Could not open the file picker.";
                }
                ImGui::TextDisabled("%s", active.c_str());
            }
            for (size_t i = 0; i < others.size() && i < 2; i++) {
                ImGui::PushID((int)i);
                if (ImGui::SmallButton("Use")) {
                    launcherSession.romSelectionFinished();
                    std::string dst = dataDir();
                    if (!dst.empty() && dst.back() != '/') dst += '/';
                    dst += "ge.z64";
                    if (copyFile(others[i].path, dst)) {
                        romError = false;
                        scan();
                        message = "ROM copied in.";
                    } else {
                        romError = true;
                        message = "Could not copy that ROM.";
                    }
                    ImGui::PopID();
                    break;
                }
                ImGui::SameLine();
                ImGui::TextDisabled("%s", others[i].path.c_str());
                ImGui::PopID();
            }
            if (!message.empty()) ImGui::TextWrapped("%s", message.c_str());
            ImGui::EndChild();
        }

        // Poll updater status for the scrollable settings area above START.
        if (now - lastUpdPoll > 250 || lastUpdPoll == 0) {
            lastUpdPoll = now;
            upd = gevrUpdaterStatus();
        }
        ImGui::Separator();

        // GoldenEye cheats (issue #1's idea: the tiny guns as a cheat): their
        // own page. Ticked cheats are switched on as each mission starts, the
        // way the game's own cheat menu does (front.c init_menu0B_runstage).
        if (reportPage) {
            gevrReportPage(reportPage, reportCrash, gold, good, bad);
        } else if (hapticsPage) {
            gevrHapticsPage(hapticsPage, gold, good, bad);
        } else if (mpPage) {
            gevrMultiplayerPage(mpPage, start, gold, good, bad);
        } else if (modsPage) {
            gevrModsPage(modsPage, now, gold, good, bad);
        } else if (throwingPage) {
            ImGui::TextColored(gold, "MOTION THROWING SETTINGS");
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
            ImGui::TextWrapped("Hold Grip on throwables (grenades, knives, mines), swing arm, and release Grip to throw.\n"
                               "Grip + Trigger cooks grenades. Releases instantly at 90%% grip squeeze.");
            ImGui::PopStyleColor();
            ImGui::Spacing();

            bool motionThrow = VrMotionThrowing;
            if (ImGui::Checkbox("Enable motion throwing", &motionThrow)) {
                VrMotionThrowing = motionThrow;
            }

            ImGui::BeginDisabled(!VrMotionThrowing);

            ImGui::Spacing();
            ImGui::TextColored(gold, "THROW STRENGTH & VELOCITY");
            ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.70f);
            ImGui::SliderFloat("##ThrowStrength", &VrMotionThrowStrength, 0.5f, 2.0f, "Strength %.2fx");
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Scales throw speed with physical swing speed.\n1.0x = natural realism, higher = longer throws.");
            }

            ImGui::Spacing();
            ImGui::TextColored(gold, "TRAJECTORY CALIBRATION");
            ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.70f);
            ImGui::SliderFloat("##ThrowPitch", &VrMotionThrowPitch, -20.0f, 20.0f, "Pitch %+.0f°");
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Vertical pitch trim: adjust upward (+) or downward (-) if throws fly too low/high.");
            }

            ImGui::Spacing();
            ImGui::TextColored(gold, "GAZE ASSIST (overhand throws)");
            ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.70f);
            float gazePct = VrMotionThrowGazeAssist * 100.0f;
            if (ImGui::SliderFloat("##ThrowGaze", &gazePct, 0.0f, 100.0f, "Gaze %.0f%%")) {
                VrMotionThrowGazeAssist = gazePct / 100.0f;
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Blends overhand throw direction toward where you are looking (0%% = pure hand, 100%% = max gaze pull).\n"
                                  "Underhand rolls, bowling, and throws behind your back remain 100%% pure hand physics.");
            }

            ImGui::EndDisabled();

            ImGui::Spacing();
            ImGui::Separator();
            if (ImGui::Button("Reset to Defaults")) {
                VrMotionThrowing = true;
                VrMotionThrowStrength = 1.0f;
                VrMotionThrowPitch = 0.0f;
                VrMotionThrowGazeAssist = 0.50f;
            }
            ImGui::SameLine();
            if (ImGui::Button("Done", ImVec2(-1, 0))) {
                throwingPage = false;
            }
        } else if (cheatPage) {
            struct CheatRow { const char *name; int id; bool cosmetic; };
            static const CheatRow fun[] = {
                { "DK mode (big heads)", 12, true }, { "Paintball mode", 15, true }, { "Line mode", 7, true },
                { "Tiny Bond", 14, false }, { "Turbo mode", 24, false }, { "Invisibility", 10, false },
                { "Fast animation", 26, false }, { "Slow animation", 27, false }, { "Enemy rockets", 28, false },
            };
            static const CheatRow arms[] = {
                { "Invincibility", 2, false }, { "All guns", 3, false }, { "Infinite ammo", 11, false },
                { "Golden Gun", 19, false }, { "Silver PP7", 20, false }, { "Gold PP7", 21, false },
                { "Magnum", 17, false }, { "Laser", 18, false }, { "2x Rocket launcher", 29, false },
                { "2x Grenade launcher", 30, false }, { "2x RC-P90", 31, false }, { "2x Throwing knife", 32, false },
                { "2x Hunting knife", 33, false }, { "2x Laser", 34, false },
            };
            auto row = [](const CheatRow& c) {
                bool on = (VrCheatMask >> c.id) & 1ULL;
                char label[64];
                snprintf(label, sizeof(label), "%s%s", c.name, c.cosmetic ? "" : " *");
                if (ImGui::Checkbox(label, &on)) {
                    if (on) VrCheatMask |= 1ULL << c.id;
                    else VrCheatMask &= ~(1ULL << c.id);
                }
            };
            ImGui::TextColored(gold, "CHEATS");
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
            ImGui::TextWrapped("On from the next mission. * stops missions saving, as in the original game.");
            ImGui::PopStyleColor();
            if (ImGui::BeginTable("cheats", 3, ImGuiTableFlags_SizingStretchSame)) {
                ImGui::TableNextColumn();
                ImGui::TextColored(gold, "VR");
                ImGui::RadioButton("Normal guns", &VrGunSizeCheat, 0);
                ImGui::RadioButton("Tiny guns", &VrGunSizeCheat, 1);
                ImGui::RadioButton("Big guns", &VrGunSizeCheat, 2);
                // three even columns: the launcher page does not scroll
                ImGui::TextColored(gold, "FUN");
                for (int i = 0; i < 5; i++) row(fun[i]);
                ImGui::TableNextColumn();
                for (int i = 5; i < 9; i++) row(fun[i]);
                ImGui::TextColored(gold, "WEAPONS");
                for (int i = 0; i < 5; i++) row(arms[i]);
                ImGui::TableNextColumn();
                for (int i = 5; i < (int)(sizeof(arms) / sizeof(arms[0])); i++) row(arms[i]);
                ImGui::EndTable();
            }
            if (ImGui::Button("All off")) {
                VrCheatMask = 0;
                VrGunSizeCheat = 0;
            }
            ImGui::SameLine();
            // Issue #54, as gepc-ref D257 Game.AllUnlocked: the game's own mission
            // select and Cheat Options, fully open (port/src/main.c, front.c).
            // Nothing is written to the saves.
            {
                bool unlock = VrUnlockAll != 0;
                if (ImGui::Checkbox("Unlock all missions and cheats", &unlock)) {
                    VrUnlockAll = unlock ? 1 : 0;
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Every mission at every difficulty, 007 mode, and every cheat\n"
                                      "in the game's own Cheat Options. Your save files are left as\n"
                                      "they are; missions you finish still count as usual.");
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("Done", ImVec2(-1, 0))) {
                cheatPage = false;
            }
        } else {
        gevrLauncherBeginBody("main-options");
        gevrLauncherMainTabs(
        [&]() {
            ImGui::TextColored(gold, "PLAY MODE");
            ImGui::RadioButton("Stereo VR (3D play)", &mode, 1);
            ImGui::RadioButton("Flat screen", &mode, 0);
            ImGui::Spacing();
            if (ImGui::Button("Multiplayer...")) mpPage = true;
            ImGui::Spacing();
            ImGui::TextColored(gold, "MODS & CHEATS");
            if (ImGui::Button(g_ActiveExtTexPack[0] ? "Mods... (HD textures on)" : "Mods..."))
                modsPage = true;
            int n = (VrGunSizeCheat ? 1 : 0) + (VrUnlockAll ? 1 : 0);
            for (int b = 0; b < 64; b++) n += (int)((VrCheatMask >> b) & 1ULL);
            char label[48];
            snprintf(label, sizeof(label), n ? "Cheats... (%d on)" : "Cheats...", n);
            if (ImGui::Button(label)) cheatPage = true;
            ImGui::Spacing();
            ImGui::TextColored(gold, "DIAGNOSTICS & UPDATES");
            bool stats = VrShowStats != 0;
            if (ImGui::Checkbox("Show stats", &stats)) VrShowStats = stats ? 1 : 0;
            bool tests = upd.testBuilds;
            if (ImGui::Checkbox("Offer test builds", &tests)) {
                upd.testBuilds = tests;
                gevrUpdaterCommand(tests ? "testbuilds:1" : "testbuilds:0");
            }
        },
        [&]() {
            ImGui::TextColored(gold, "HANDS & STICKS");
            bool lefty = VrLeftHandedMode != 0;
            if (ImGui::Checkbox("Left-handed", &lefty)) VrLeftHandedMode = lefty ? 1 : 0;
            bool swap = VrSwapJoysticks != 0;
            if (ImGui::Checkbox("Swap sticks", &swap)) VrSwapJoysticks = swap ? 1 : 0;
            bool nolean = VrAimNoLean != 0;
            if (ImGui::Checkbox("Aim: no lean", &nolean)) VrAimNoLean = nolean ? 1 : 0;
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Stereo: keep moving while holding the aim trigger.\nClick the left stick to crouch.");
            ImGui::Spacing();
            ImGui::TextColored(gold, "AIM STEADYING (stereo)");
            int steady = VrAimSteady < 0 ? 0 : VrAimSteady > 2 ? 2 : VrAimSteady;
            ImGui::RadioButton("Off##steady", &steady, 0);
            ImGui::SameLine();
            ImGui::RadioButton("Low##steady", &steady, 1);
            ImGui::SameLine();
            ImGui::RadioButton("High##steady", &steady, 2);
            VrAimSteady = steady;
            ImGui::Spacing();
            if (ImGui::Button(VrGunFitArmed ? "Gun fit: on" : "Gun fit..."))
                VrGunFitArmed = !VrGunFitArmed;
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("In the next level, with a gun in hand (stereo): the sticks move\n"
                                  "the gun on your hand. A keeps it, B puts it back.");
            char throwLabel[64];
            snprintf(throwLabel, sizeof(throwLabel), "Motion Throwing%s...", VrMotionThrowing ? "" : " (Off)");
            if (ImGui::Button(throwLabel)) throwingPage = true;
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Configure motion throwing, throw strength, pitch trim, and gaze assist.");
            if (ImGui::Button("Haptics...")) hapticsPage = true;
        },
        [&]() {
            ImGui::TextColored(gold, "TURNING (stereo)");
            ImGui::RadioButton("Smooth", &turn, 0);
            ImGui::SameLine();
            ImGui::RadioButton("Snap 30", &turn, 1);
            ImGui::RadioButton("Snap 45", &turn, 2);
            ImGui::SameLine();
            ImGui::RadioButton("Snap 90", &turn, 3);
            ImGui::Spacing();
            ImGui::TextColored(gold, "MOVEMENT COMFORT (stereo)");
            ImGui::Checkbox("Darken edges when moving", &vignetteOn);
            ImGui::BeginDisabled(!vignetteOn);
            ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.62f);
            ImGui::SliderFloat("Strength", &vignette, 0.1f, 1.0f, "%.1f");
            ImGui::EndDisabled();
        },
        [&]() {
            ImGui::TextColored(gold, "SCREEN");
            ImGui::TextWrapped("Both grips grab the screen; right stick: distance / size. "
                               "Hold the left stick click to recentre it.");
            int curved = VrScreenCurved;
            ImGui::RadioButton("Flat", &curved, 0);
            ImGui::SameLine();
            ImGui::BeginDisabled(!vr_screen_curve_supported());
            ImGui::RadioButton("Curved", &curved, 1);
            ImGui::EndDisabled();
            VrScreenCurved = curved;
            float size = VrScreenFov, dist = VrScreenDistance;
            ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.62f);
            if (ImGui::SliderFloat("Size", &size, VR_SCREEN_FOV_MIN, VR_SCREEN_FOV_MAX, "%.0f deg"))
                vr_screen_resize(VrScreenDistance, size);
            ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.62f);
            if (ImGui::SliderFloat("Distance", &dist, VR_SCREEN_DISTANCE_MIN, VR_SCREEN_DISTANCE_MAX, "%.1f m"))
                vr_screen_resize(dist, VrScreenFov);
            ImGui::Spacing();
            ImGui::TextColored(gold, "DISPLAY RATE");
            std::vector<int> rates(vr_get_supported_refresh_rates(nullptr, 0));
            const int count = vr_get_supported_refresh_rates(rates.data(), (int)rates.size());
            gevrDisplayRateControls(&VrRefreshRate, rates.data(), count);
        });
        ImGui::Separator();
        // Update line: only when there is something to say, so an
        // up-to-date launcher looks as it always has.
        bool updLine = upd.state == "available" || upd.state == "downloading"
            || upd.state == "permission" || upd.state == "installing"
            || upd.state == "error" || !upd.message.empty();
        if (updLine) {
            if (upd.state == "available") {
                ImGui::TextColored(gold, "Update available: v%s", upd.offered.c_str());
                ImGui::SameLine();
                if (ImGui::SmallButton("Update")) gevrUpdaterCommand("update");
                if (!upd.message.empty()) {
                    // wrapped: the row ends at the panel's edge
                    ImGui::SameLine();
                    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
                    ImGui::TextWrapped("%s", upd.message.c_str());
                    ImGui::PopStyleColor();
                }
            } else if (upd.state == "downloading") {
                // Cancel on the left of every busy line: a prompt closed from the
                // shell may never report back, and this is the way out of it.
                if (ImGui::SmallButton("Cancel##update")) gevrUpdaterCommand("cancel");
                ImGui::SameLine();
                if (upd.progress >= 0) {
                    ImGui::TextColored(gold, "Downloading v%s... %d%%", upd.offered.c_str(), upd.progress);
                } else {
                    ImGui::TextColored(gold, "Downloading v%s...", upd.offered.c_str());
                }
            } else if (upd.state == "permission" || upd.state == "installing") {
                if (ImGui::SmallButton("Cancel##update")) gevrUpdaterCommand("cancel");
                ImGui::SameLine();
                ImGui::PushStyleColor(ImGuiCol_Text, gold);
                ImGui::TextWrapped("%s", upd.message.c_str());
                ImGui::PopStyleColor();
            } else if (upd.state == "error") {
                if (ImGui::SmallButton("Retry##update")) gevrUpdaterCommand("retry");
                ImGui::SameLine();
                ImGui::PushStyleColor(ImGuiCol_Text, bad);
                ImGui::TextWrapped("%s", upd.message.c_str());
                ImGui::PopStyleColor();
            } else if (!upd.message.empty()) {
                ImGui::PushStyleColor(ImGuiCol_Text, good);
                ImGui::TextWrapped("%s", upd.message.c_str());   // "Updated to v0.1.13."
                ImGui::PopStyleColor();
            }
        }
        ImGui::EndChild();
        ImGui::Separator();
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
        }   // cheatPage
        ImGui::TextDisabled("Point and pull the trigger, or use the stick and A.");
        ImGui::End();
        ImGui::Render();
        showKeyboard(io);

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
    vr_apply_refresh_rate();
    vr_log("launcher: start (%s, snap %.0f, vignette %.2f)", VrPlayMode ? "stereo" : "screen",
           VrUseSnapTurn, VrComfortVignette);
    gevrTexpackStartEarly();   // index the chosen pack while the game boots

    SDL_StopTextInput();
    SDL_DelEventWatch(keyboardWatch, nullptr);
    SDL_DestroyMutex(s_kbdLock);
    s_kbdLock = nullptr;
    s_kbdQueue.clear();
    ImGui_ImplOpenGL3_Shutdown();
    ImGui::DestroyContext();
    if (iconTex) glDeleteTextures(1, &iconTex);
    glDeleteFramebuffers(1, &fbo);
    glDeleteTextures(1, &tex);
}

#ifdef ANDROID
// The same ownership rules and ray coordinates as the boot launcher, with
// no launch/ROM-picker test hooks in a running match.
extern "C" void gevrFeedPauseInput(int opening) {
    static bool waitRelease=false;
    const bool trigger=get_button_state(1,"trigger") || get_button_state(0,"trigger");
    const bool grabbing=get_button_state(1,"grip") && get_button_state(0,"grip");
    if(opening)waitRelease=trigger;
    if(!trigger)waitRelease=false;
    ImGuiIO&io=ImGui::GetIO();XrVector2f l={0,0},r={0,0};
    get_2d_input(0,"thumbstick",&l);get_2d_input(1,"thumbstick",&r);
    XrVector2f stick=l.x*l.x+l.y*l.y>r.x*r.x+r.y*r.y?l:r;
    const bool back=get_button_state(1,"b") || get_button_state(0,"y");
    const bool select=get_button_state(1,"a") || get_button_state(0,"x");
    const bool nav=fabsf(stick.x)>.5f || fabsf(stick.y)>.5f || back || select;
    if(grabbing){stick={0,0};}
    const bool pointing=feedPointer(io,nav,waitRelease || grabbing);
    io.AddKeyEvent(ImGuiKey_GamepadDpadUp,stick.y>.5f);io.AddKeyEvent(ImGuiKey_GamepadDpadDown,stick.y<-.5f);
    io.AddKeyEvent(ImGuiKey_GamepadDpadLeft,stick.x<-.5f);io.AddKeyEvent(ImGuiKey_GamepadDpadRight,stick.x>.5f);
    io.AddKeyEvent(ImGuiKey_GamepadFaceDown,gevrGamepadActivate(pointing,select,!waitRelease && !grabbing && trigger));
    io.AddKeyEvent(ImGuiKey_GamepadFaceRight,back);
}
#endif
