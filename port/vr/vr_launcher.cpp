#include "gevr_launcher_ui.h"
#include "gevr_settings_ui.h"
#include "gevr_romcheck.h"
#include "gevr_romlayout.h"
#include "gevr_bean.h"
#include "gevr_bean_ce.h"
#include "gevr_oddjobhat.h"
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
 *    copies it over data/ge.z64. Goldfinger 64 and Tomorrow Never Dies sit
 *    beside it (data/goldfinger64.z64, data/tnd64.z64). The campaign radio
 *    chooses which file Start loads, and the choice is saved;
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
#include <utility>
#include <algorithm>
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
#include "vr_friends.h"
#include "gevr_friends_ui.h"

extern "C" {
int gevrVrPumpBegin(void);            // port/src/gevr_engine_shim.c
void gevrVrPumpEnd(void);
const char *fsFullPath(const char *relPath);  // port/src/fs.c
int fsFileSize(const char *name);             // port/src/fs.c: -1 when absent
extern const char gevrBuildId[];              // generated, port/cmake/buildid.cmake
void vrSettingsSave(void);            // vr_settings.cpp
void vrEnsurePlayerName(void);        // vr_settings.cpp: make up a name if there is none
extern char g_ActiveExtTexPack[];     // port/src/ext_tex.c: the texture pack in use ("" = none), saved in the ini
void gevrTexpackStartEarly(void);     // fast3d/gfx_pc.cpp: index that pack in the background
void vr_apply_refresh_rate(void);     // vr_openxr.cpp
int vr_get_supported_refresh_rates(int *rates, int capacity);
int gevrVrSessionRunning(void);       // vr_openxr.cpp: includes the unfocused Quest menu
unsigned long long sysGetMicroseconds(void);  // port/src/system.c: wall clock, runs on through a headset's sleep
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

// The same checks as gevr_romload.c: size, byte order, header name, NTSC-U
// cartridge id. The loader converts .v64/.n64 dumps to .z64 order before it
// checks them, so the header is converted here too: a byte-swapped PAL dump
// used to pass on its size alone, enable START and then abort the game.
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
    if (n != sizeof(hdr)) {
        r.status = "cannot be read";
        return r;
    }

    const char *order;
    if (hdr[0] == 0x80 && hdr[1] == 0x37 && hdr[2] == 0x12 && hdr[3] == 0x40) {
        order = "";
    } else if (hdr[0] == 0x37 && hdr[1] == 0x80 && hdr[2] == 0x40 && hdr[3] == 0x12) {
        for (size_t i = 0; i + 1 < sizeof(hdr); i += 2) {     // .v64
            std::swap(hdr[i], hdr[i + 1]);
        }
        order = ", byte-swapped";
    } else if (hdr[0] == 0x40 && hdr[1] == 0x12 && hdr[2] == 0x37 && hdr[3] == 0x80) {
        for (size_t i = 0; i + 3 < sizeof(hdr); i += 4) {     // .n64
            std::swap(hdr[i], hdr[i + 3]);
            std::swap(hdr[i + 1], hdr[i + 2]);
        }
        order = ", byte-swapped";
    } else {
        r.status = "not an N64 ROM image";
        return r;
    }

    if (memcmp(hdr + 0x20, "GOLDENEYE", 9) != 0) {
        r.status = "header name is not GOLDENEYE";
        return r;
    }
    if (memcmp(hdr + 0x3b, "NGEE", 4) != 0) {
        const char *region = hdr[0x3e] == 'P' ? "PAL" : hdr[0x3e] == 'J' ? "Japanese" : "non-USA";
        r.status = std::string(region) + " cartridge (" + std::string((const char *)hdr + 0x3b, 4)
            + ") - needs the USA one (NGEE)";
        return r;
    }
    // A correct cartridge header alone does not establish usable game data.
    // Report 109c2b77 reached langInit with a non-1172 language bank.
    std::vector<unsigned char> rom(kRomSize);
    f = fopen(path.c_str(), "rb");
    if (!f) {
        r.status = "cannot be read";
        return r;
    }
    n = fread(rom.data(), 1, rom.size(), f);
    fclose(f);
    if (n != rom.size() || !gevrRomNormaliseOrder(rom.data(), (unsigned int)n)) {
        r.status = "cannot read cartridge data";
        return r;
    }
    char error[192];
    if (!gevrRomCheckFiles(rom.data(), (unsigned int)n, error, sizeof(error))) {
        r.status = error;
        return r;
    }
    r.status = std::string("GoldenEye 007 (USA)") + order + " - OK";
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

// MainActivity.openRomPicker / openGexPicker / openGoldfingerPicker /
// openTndPicker: the system file picker, copying the pick to data/picked*.z64
static bool gevrOpenPicker(const char *method)
{
    JNIEnv *env = (JNIEnv *)SDL_AndroidGetJNIEnv();
    jobject activity = (jobject)SDL_AndroidGetActivity();
    if (env == nullptr || activity == nullptr) {
        return false;
    }
    jclass cls = env->GetObjectClass(activity);
    jmethodID m = env->GetMethodID(cls, method, "()V");
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
    vr_log("launcher: file picker (%s) %s", method, ok ? "opened" : "failed");
    return ok;
}

bool gevrOpenRomPicker()
{
    return gevrOpenPicker("openRomPicker");
}

static bool hackHeaderOk(const std::string &path, const char *tag)
{
    FILE *f = fopen(path.c_str(), "rb");
    unsigned char hdr[0x40] = {};
    size_t n;
    long size;
    const struct gevrRomLayout *layout;
    if (!f) return false;
    n = fread(hdr, 1, sizeof(hdr), f);
    fseek(f, 0, SEEK_END);
    size = ftell(f);
    fclose(f);
    if (n != sizeof(hdr) || size <= 0) return false;
    if (!gevrRomNormaliseOrder(hdr, (unsigned int)sizeof(hdr))) return false;
    layout = gevrRomLayoutMatchHeader(hdr, (uint32_t)size);
    return layout && std::strcmp(layout->tag, tag) == 0;
}

static bool adoptHackFile(const char *pickedName, const char *finalName, const char *tag,
                          const char *okText, const char *badText, std::string &message)
{
    const std::string picked = dataDir() + "/" + pickedName;
    struct stat st;
    if (stat(picked.c_str(), &st) != 0) return false;
    if (hackHeaderOk(picked, tag) && rename(picked.c_str(), (dataDir() + "/" + finalName).c_str()) == 0) {
        message = okText;
        vr_log("launcher: %s adopted", finalName);
        return true;
    }
    message = badText;
    remove(picked.c_str());
    return false;
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

static void gevrHapticsPage(const ImVec4 &gold, const ImVec4 &good, const ImVec4 &bad)
{
    static int activeTab = HAPTIC_CAT_PISTOLS;

    ImGui::TextDisabled("Vibration strength (0-10) and length (ms) for each weapon and action; each change buzzes.");

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

/*
 * The main page's START row, with the campaign beside it rather than above:
 * the page is laid out to its last pixel and must not scroll, so choosing
 * a hack campaign costs no height. A hack whose ROM is
 * not installed is listed but disabled; the last three entries open the file
 * picker for it.
 */
enum {
    GEVR_START_ROW_NONE,
    GEVR_START_ROW_START,
    GEVR_START_ROW_CAMPAIGN,    // *campaign changed
    GEVR_START_ROW_PICK_GF,
    GEVR_START_ROW_PICK_TND,
    GEVR_START_ROW_PICK_MIRROR,
};

static int gevrLauncherStartRow(int *campaign, bool gfGood, bool tndGood, bool mirrorGood, bool ready, bool focus,
                                const char *startLabel = "START")
{
    static const char *const names[] = { "GoldenEye", "Goldfinger 64", "Tomorrow Never Dies", "Mirror Mode (Wreck)" };
    const bool installed[] = { true, gfGood, tndGood, mirrorGood };
    const float startH = ImGui::GetFrameHeight() * 1.6f;
    const float rowY = ImGui::GetCursorPosY();
    int action = GEVR_START_ROW_NONE;
    const int shown = *campaign >= 0 && *campaign < GEVR_CAMPAIGN_COUNT ? *campaign : 0;

    ImGui::SetCursorPosY(rowY + (startH - ImGui::GetFrameHeight()) * 0.5f);
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.38f);
    if (ImGui::BeginCombo("##campaign", names[shown])) {
        for (int c = 0; c < GEVR_CAMPAIGN_COUNT; c++) {
            char label[64];

            snprintf(label, sizeof(label), installed[c] ? "%s" : "%s (not installed)", names[c]);
            ImGui::BeginDisabled(!installed[c]);
            if (ImGui::Selectable(label, shown == c) && c != shown) {
                *campaign = c;
                action = GEVR_START_ROW_CAMPAIGN;
            }
            ImGui::EndDisabled();
        }
        ImGui::Separator();
        if (ImGui::Selectable("Add Goldfinger 64 ROM...")) action = GEVR_START_ROW_PICK_GF;
        if (ImGui::Selectable("Add Tomorrow Never Dies ROM...")) action = GEVR_START_ROW_PICK_TND;
        if (ImGui::Selectable("Add Mirror Mode ROM...")) action = GEVR_START_ROW_PICK_MIRROR;
        ImGui::EndCombo();
    }
    if (ImGui::IsItemHovered())
        gevrTooltip("Choose GoldenEye, Goldfinger 64, Tomorrow Never Dies or Wreck's Mirror Mode. Add a patched ROM with this menu.");
    ImGui::SameLine();
    ImGui::SetCursorPosY(rowY);
    ImGui::BeginDisabled(!ready);
    if (ImGui::Button(startLabel, ImVec2(-1, startH)))
        action = GEVR_START_ROW_START;
    if (focus) {
        ImGui::SetItemDefaultFocus();
        ImGui::SetKeyboardFocusHere(-1);
    }
    ImGui::EndDisabled();
    return action;
}

static void gevrModsPage(Uint32 now, const ImVec4 &gold, const ImVec4 &good, const ImVec4 &bad)
{
    static std::vector<ModPack> packs;
    static Uint32 lastPoll = 0;
    static bool checked = false;
    static std::string beanMessage;   // the picker would not open
    if (!checked) {
        checked = true;
        gevrJavaCommand("modsCommand", "check");   // newer pack releases on GitHub (ModManager.checkLatest)
    }
    if (now - lastPoll > 250 || lastPoll == 0) {
        lastPoll = now;
        gevrBeanInit();   // an imported or downloaded pack becomes available without reopening the launcher
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

    if (ImGui::RadioButton("Original", !gevrBeanIsEnabled() && !g_ActiveExtTexPack[0])) {
        g_ActiveExtTexPack[0] = '\0';
        gevrBeanSetEnabled(0);
    }
    if (ImGui::IsItemHovered()) gevrTooltip("The game's own look. The packs below change it from your next game.");
    bool beanListed = false;
    if (ImGui::BeginTable("##visual-packs", 3, ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("Look", ImGuiTableColumnFlags_WidthStretch, 1.4f);
        ImGui::TableSetupColumn("Status", ImGuiTableColumnFlags_WidthStretch, 1.0f);
        ImGui::TableSetupColumn("Actions", ImGuiTableColumnFlags_WidthStretch, 1.2f);
        for (const ModPack &p : packs) {
            if (p.id == "oddjob-hat") continue;   // a mod, not a look: the row above Done
            ImGui::PushID(p.id.c_str());
            const bool bean = p.id == "ge007-bean";
            const bool installed = p.state == "installed";
            const bool busy = p.state == "downloading" || p.state == "installing" || p.state == "importing" || p.state == "patching";
            const bool localBean = bean && gevrBeanIsAvailable();
            const bool usable = installed || localBean;
            const bool active = bean ? gevrBeanIsEnabled() : !gevrBeanIsEnabled() && p.id == g_ActiveExtTexPack;
            const char *title = bean ? "XBLA / Bean" : p.id == "ge007-hd-ai" ? "HD + AI" : "HD";
            beanListed |= bean && usable;
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            // Bean's meshes are GoldenEye's models remade: off in a hack campaign
            // but Mirror Mode, which keeps GoldenEye's (and Bean flips them with it)
            ImGui::BeginDisabled(!usable || (bean && !gevrCampaignRetailArt(VrCampaign)));
            if (ImGui::RadioButton(title, active)) {
                snprintf(g_ActiveExtTexPack, 256, "%s", p.id.c_str());
                gevrBeanSetEnabled(bean);
            }
            ImGui::EndDisabled();
            // one line a pack: under the tab rows and above START the tab does not scroll
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                if (bean) gevrTooltip("%s: meshes + textures\nby %s\nFrom your own copy of the game\nIn game: VR options, or hold one grip + right-stick click for 1 second",
                                            p.title.c_str(), p.by.c_str());
                else gevrTooltip("%s: textures\nby %s\n%s\nVersion %s", p.title.c_str(), p.by.c_str(), p.site.c_str(), p.version.c_str());
            }
            ImGui::TableNextColumn();
            const std::string &message = bean && p.message.empty() ? beanMessage : p.message;
            if (busy) {
                const char *verb = p.state == "downloading" ? "Downloading" : p.state == "importing" ? "Importing"
                                   : p.state == "patching" ? "Applying CE" : "Unpacking";
                if (p.progress >= 0) ImGui::TextColored(gold, "%s %d%%", verb, p.progress);
                else ImGui::TextColored(gold, "%s...", verb);
            } else if (!message.empty()) {
                // the failure in the status's place, cut to the column; hovering shows it whole
                ImGui::PushStyleColor(ImGuiCol_Text, bad);
                gevrLauncherStatus(message.c_str());
                ImGui::PopStyleColor();
            } else {
                ImGui::TextColored(p.state == "error" ? bad : usable ? good : ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled),
                                   "%s%s", active ? "Selected" : installed ? "Installed" : localBean ? "Local files"
                                   : p.state == "error" ? (bean ? "Import failed" : "Download failed") : "Not installed",
                                   p.update.empty() ? "" : ", update");
            }
            ImGui::TableNextColumn();
            if (busy) {
                // the Community Edition's patch runs to its end once started
                if (p.state != "patching" && ImGui::Button("Cancel")) gevrJavaCommand("modsCommand", "cancel");
            } else if (bean && !installed) {
                // Never downloaded: the player picks their own copy of the
                // release and ModManager imports Bean's files from it
                // (port/src/gevr_bean_import.c)
                const char *label = "Choose copy...";
                if (ImGui::Button(label))
                    beanMessage = gevrOpenPicker("openXblaPicker") ? "" : "Could not open the file picker.";
            } else if (!installed || !p.update.empty()) {
                char label[64];
                snprintf(label, sizeof(label), "%s (%d MB)", installed ? "Update" : p.state == "error" ? "Retry" : "Download", p.mb);
                if (ImGui::Button(label)) gevrJavaCommand("modsCommand", ("install:" + p.id).c_str());
            }
            if (installed) {
                if (!p.update.empty()) ImGui::SameLine();
                if (ImGui::SmallButton("Remove")) {
                    if (bean) gevrBeanSetEnabled(0);
                    if (p.id == g_ActiveExtTexPack) g_ActiveExtTexPack[0] = '\0';
                    vrSettingsSave();
                    gevrJavaCommand("modsCommand", ("remove:" + p.id).c_str());
                }
                // The Community Edition over the imported copy (port/src/gevr_bean_ce.c):
                // the player picks its updater zip once, then switches it here
                if (bean) {
                    ImGui::SameLine();
                    if (gevrBeanCeApplied()) {
                        bool ce = VrBeanCommunityEdition != 0;
                        if (ImGui::Checkbox("CE", &ce)) VrBeanCommunityEdition = ce ? 1 : 0;
                        if (ImGui::IsItemHovered())
                            gevrTooltip("GoldenEye XBLA Community Edition: new skies, Frigate's water,\nprops and fixes over your copy. From the next level you start.");
                    } else {
                        if (ImGui::SmallButton("CE..."))
                            beanMessage = gevrOpenPicker("openCePicker") ? "" : "Could not open the file picker.";
                        if (ImGui::IsItemHovered())
                            gevrTooltip("Apply the GoldenEye XBLA Community Edition to your copy:\nchoose its updater, CommunityEditionUpdaterV6.zip.");
                    }
                }
            }
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    if (!beanListed && gevrBeanIsAvailable() && ImGui::RadioButton("XBLA / Bean (local files)", gevrBeanIsEnabled())) {
        gevrBeanSetEnabled(1);
    }
    if (!gevrCampaignRetailArt(VrCampaign) && gevrBeanIsAvailable())
        ImGui::TextWrapped("XBLA / Bean is off while the Goldfinger 64 or Tomorrow Never Dies campaign is chosen.");
    else if (gevrBeanIsEnabled()) {
        // How to toggle Bean in game is the Bean row's tooltip: with the HD fill
        // row and Done's blood patch, its two lines ran the page past the panel.
        // the pictures Bean leaves at the cartridge's, from an installed HD pack (gfx_pc.cpp tp_find)
        const char *fill = nullptr;
        for (const char *id : { "ge007-hd-ai", "ge007-hd" })
            for (const ModPack &p : packs)
                if (!fill && p.id == id && p.state == "installed") fill = p.id == "ge007-hd-ai" ? "HD + AI" : "HD";
        bool on = VrBeanFillTextures != 0;
        char label[96];
        snprintf(label, sizeof(label), "Fill Bean's gaps with %s textures", fill ? fill : "HD");
        ImGui::BeginDisabled(fill == nullptr);
        if (ImGui::Checkbox(label, &on)) VrBeanFillTextures = on ? 1 : 0;
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            gevrTooltip(fill ? "Pictures the XBLA release left as the cartridge's (the stage pictures and the like)\ncome from the %s pack."
                                   : "Install the HD or HD + AI pack above to fill the pictures\nthe XBLA release left as the cartridge's.", fill);
    }
    else if (!gevrBeanIsAvailable())
        ImGui::TextWrapped("XBLA / Bean reads your own GoldenEye XBLA: its .7z, .zip or Xbox 360 package. Nothing is downloaded.");
    ImGui::Separator();

    // GoldenEye X's first-person guns, read from the player's own patched ROM
    // (port/src/gevr_gex.c, docs/gex-weapons.md); nothing of it ships. The
    // picker copies the file to data/picked-gex.z64, adopted as gex.z64 when
    // its header names GoldenEye X.
    static int s_gexRom = -1;
    static bool s_gexPicking = false;
    static std::string s_gexMessage;
    if (s_gexRom < 0) s_gexRom = fsFileSize("data/gex.z64") > 0 ? 1 : 0;
    if (s_gexPicking) {
        const std::string r = gevrTakePickResult();
        if (!r.empty()) {
            s_gexMessage = r;
            s_gexPicking = false;
        }
    }
    {
        const std::string picked = dataDir() + "/picked-gex.z64";
        struct stat st;
        if (stat(picked.c_str(), &st) == 0) {
            char head[0x40] = {0};
            FILE *f = fopen(picked.c_str(), "rb");
            const bool read = f != nullptr && fread(head, 1, sizeof(head), f) == sizeof(head);
            if (f) fclose(f);
            const bool isGex = read && st.st_size == 0x2000000 && (unsigned char)head[0] == 0x80
                && (unsigned char)head[1] == 0x37 && memcmp(head + 0x20, "GoldenEye X", 11) == 0;
            if (isGex && rename(picked.c_str(), (dataDir() + "/gex.z64").c_str()) == 0) {
                s_gexMessage = "GoldenEye X ROM chosen.";
                s_gexRom = 1;
                vr_log("launcher: GoldenEye X ROM adopted");
            } else {
                s_gexMessage = "That is not a GoldenEye X .z64 ROM (Perfect Dark 1.1 with the GE-X 6a patch).";
                remove(picked.c_str());
            }
            s_gexPicking = false;
        }
    }
    ImGui::TextColored(gold, "GOLDENEYE X");
    bool gex = VrGexGuns != 0;
    // Off for a Goldfinger 64 or Tomorrow Never Dies run (gevr_romload.c): its
    // models would replace the hack's guns and its textures share Goldfinger's ids.
    ImGui::BeginDisabled(VrCampaign != GEVR_CAMPAIGN_GE);
    if (ImGui::Checkbox("GE-X guns, hands and watch", &gex)) VrGexGuns = gex ? 1 : 0;
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::SmallButton(s_gexRom ? "Change ROM..." : "Choose ROM...")) {
        s_gexPicking = gevrOpenPicker("openGexPicker");
        s_gexMessage = s_gexPicking ? "Pick the GoldenEye X ROM in the window that opened."
                                    : "Could not open the file picker.";
    }
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    ImGui::TextWrapped("%s", !s_gexMessage.empty() ? s_gexMessage.c_str()
                       : VrCampaign != GEVR_CAMPAIGN_GE ? "Off while a hack campaign is chosen."
                       : s_gexRom ? "Uses your GoldenEye X ROM (data/gex.z64)."
                                  : "Needs your own GoldenEye X ROM: Perfect Dark 1.1 with the GE-X 6a patch.");
    ImGui::PopStyleColor();
    if (gex && gevrBeanIsEnabled()) ImGui::TextDisabled("GE-X supplies first-person gear; Bean supplies the world.");
    // Wreck's blood patch (chr.c chrCreateHitPuffs): a cosmetic table, so it goes
    // with every look and campaign, and online each headset its own.
    ImGui::Separator();
    bool blood = VrBloodPatch != 0;
    if (ImGui::Checkbox("Blood patch (by Wreck)", &blood)) VrBloodPatch = blood ? 1 : 0;
    if (ImGui::IsItemHovered())
        gevrTooltip("Shot guards spray the red blood sparks Rare left in the game,\ninstead of smoke and sparks.");
    // Oddjob's hat shares the row: a mod pack (ModManager "oddjob-hat") the
    // player downloads here, then turns on and off.
    for (const ModPack &p : packs) {
        if (p.id != "oddjob-hat") continue;
        ImGui::PushID("oddjob-hat");
        ImGui::SameLine();
        const bool busy = p.state == "downloading" || p.state == "installing";
        if (busy) {
            ImGui::TextColored(gold, "Oddjob's hat %d%%", p.progress < 0 ? 0 : p.progress);
            ImGui::SameLine();
            if (ImGui::SmallButton("Cancel")) gevrJavaCommand("modsCommand", "cancel");
        } else if (p.state == "installed" || gevrHatAvailable()) {
            bool hat = VrOddjobHat != 0;
            if (ImGui::Checkbox("Oddjob's hat", &hat)) VrOddjobHat = hat ? 1 : 0;
            if (ImGui::IsItemHovered())
                gevrTooltip("Throwing knives are Oddjob's hat (007 Nightfire): in your hand,\nin flight and lying about. Only the look changes.");
            if (!p.update.empty()) {
                ImGui::SameLine();
                if (ImGui::SmallButton("Update")) gevrJavaCommand("modsCommand", "install:oddjob-hat");
            }
            if (p.state == "installed") {
                ImGui::SameLine();
                if (ImGui::SmallButton("Remove")) gevrJavaCommand("modsCommand", "remove:oddjob-hat");
            }
        } else {
            if (ImGui::SmallButton(p.state == "error" ? "Retry Oddjob's hat" : "Get Oddjob's hat"))
                gevrJavaCommand("modsCommand", "install:oddjob-hat");
            if (ImGui::IsItemHovered())
                gevrTooltip(p.state == "error" ? "%s" : "Throwing knives become Oddjob's hat (007 Nightfire).\nA small download.",
                                  p.message.c_str());
        }
        ImGui::PopID();
    }
}

// Names: printable ASCII, which the game's font can draw over a player's head,
// less '|', the lobby service's field separator.
int nameCharFilter(ImGuiInputTextCallbackData *data)
{
    return data->EventChar < 0x20 || data->EventChar > 0x7e || data->EventChar == '|';
}

// GoldenEye's cheats (issue #1's idea: the tiny guns as a cheat), Rules >
// Cheats. Ticked cheats are switched on as each mission starts, the way the
// game's own cheat menu does (front.c init_menu0B_runstage).
static void gevrCheatsPage(const GevrSettingsApi &settings, const ImVec4 &gold)
{
    // excl: a cheat ticking this one unticks (2x and 10x Health both
    // set the health ceiling; cheat.c applies only the first)
    struct CheatRow { const char *name; int id; bool cosmetic; int excl; };
    static const CheatRow fun[] = {
        { "DK mode (big heads)", 12, true }, { "Paintball mode", 15, true }, { "Line mode", 7, true },
        { "Tiny Bond", 14, false }, { "Turbo mode", 24, false }, { "Invisibility", 10, false },
        { "Fast animation", 26, false }, { "Slow animation", 27, false }, { "Enemy rockets", 28, false },
    };
    // with the retail game's hidden cheats (#95): 2x/10x Health,
    // 2x Armor, Max ammo, Extra weapons are in cheat.c but have no
    // unlock in the game's Cheat Options
    static const CheatRow health[] = {
        { "Invincibility", 2, false }, { "2x Health", 8, false, 16 }, { "10x Health", 16, false, 8 },
        { "2x Armor", 9, false }, { "Infinite ammo", 11, false }, { "Max ammo", 4, false },
    };
    static const CheatRow arms[] = {
        { "All guns", 3, false }, { "Extra weapons", 13, false },
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
            if (on && c.excl) VrCheatMask &= ~(1ULL << c.excl);
            vrSettingsSave();
        }
    };
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    // a hack campaign skips this list (front.c gevrApplyLauncherCheats)
    const bool hackCampaign = VrCampaign != GEVR_CAMPAIGN_GE;
    ImGui::TextWrapped(hackCampaign ? "Off with a hack campaign chosen: use the game's own Cheat Options."
                                    : "On from the next mission. * stops missions saving, as in the original game.");
    ImGui::PopStyleColor();
    // three even columns of eleven, closer set than the other pages: under the
    // two tab rows and above START the page does not scroll
    const ImGuiStyle &style = ImGui::GetStyle();
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(style.FramePadding.x, style.FramePadding.y * 0.4f));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(style.ItemSpacing.x, style.ItemSpacing.y * 0.3f));
    if (ImGui::BeginTable("cheats", 3, ImGuiTableFlags_SizingStretchSame)) {
        ImGui::TableNextColumn();
        // the VR gun size, a settings row (gevr_settings.c)
        gevrSettingsSection(settings, GEVR_LSEC_CHEATS, GEVR_SET_LAUNCHER);
        ImGui::BeginDisabled(hackCampaign);
        ImGui::TextColored(gold, "FUN");
        for (int i = 0; i < 7; i++) row(fun[i]);
        ImGui::EndDisabled();
        if (ImGui::Button("All off")) {
            VrCheatMask = 0;
            VrGunSizeCheat = 0;
            vrSettingsSave();
        }
        // Issue #54, as gepc-ref D257 Game.AllUnlocked: the game's own mission
        // select and Cheat Options, fully open (port/src/main.c, front.c).
        // Nothing is written to the saves.
        bool unlock = VrUnlockAll != 0;
        if (ImGui::Checkbox("Unlock all", &unlock)) {
            VrUnlockAll = unlock ? 1 : 0;
            vrSettingsSave();
        }
        if (ImGui::IsItemHovered()) {
            gevrTooltip("Every mission at every difficulty, 007 mode, and every cheat\n"
                        "in the game's own Cheat Options. Your save files are left as\n"
                        "they are; missions you finish still count as usual.");
        }
        ImGui::BeginDisabled(hackCampaign);
        ImGui::TableNextColumn();
        for (int i = 7; i < 9; i++) row(fun[i]);
        ImGui::TextColored(gold, "HEALTH & AMMO");
        for (const CheatRow& c : health) row(c);
        ImGui::TextColored(gold, "WEAPONS");
        for (int i = 0; i < 2; i++) row(arms[i]);
        ImGui::TableNextColumn();
        for (int i = 2; i < (int)(sizeof(arms) / sizeof(arms[0])); i++) row(arms[i]);
        ImGui::EndDisabled();
        ImGui::EndTable();
    }
    ImGui::PopStyleVar(2);
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

// The lobby service's visibility: the host page's choice, or private for a
// lobby the Friends page started to invite into (VrMpVisibility stays as chosen).
static const char *gevrVisibilityName(int visibility)
{
    return visibility == 2 ? "friends" : visibility == 1 ? "private" : "public";
}
static const char *s_hostVisibility = nullptr;
extern "C" const char *gevrHostedLobbyVisibility(void)
{
    return !netIsHost() ? "" : s_hostVisibility ? s_hostVisibility : gevrVisibilityName(VrMpVisibility);
}

static std::string gevrLobbyCreateCommand(const std::string &name, int stage, int set, int max)
{
    return std::string("create|") + (s_hostVisibility ? s_hostVisibility : gevrVisibilityName(VrMpVisibility)) + "|" + name + "|" +
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

// The host's player count, 2..8 on any stage (VrMpMaxPlayers), after its
// "Players:" label: the number alone, so the stage chooser beside it keeps
// room for every stage's name (user: "Caverns" was cut off).
static const char *playerCountName(int idx) {
    static const char *const names[] = { "2", "3", "4", "5", "6", "7", "8" };
    static_assert(sizeof(names) / sizeof(names[0]) == GEVR_MAX_PLAYERS - 1, "a name for every count");
    return idx >= 0 && idx < GEVR_MAX_PLAYERS - 1 ? names[idx] : names[2];
}

static const char *gunNameAt(int idx) { return netItem(idx)->name; }
static const char *gevrBotCountName(int idx) {
    static const char *const names[] = { "1 bot", "2 bots", "3 bots", "4 bots", "5 bots", "6 bots", "7 bots" };
    static_assert(sizeof(names) / sizeof(names[0]) == GEVR_MAX_PLAYERS - 1, "a name for every count");
    return idx >= 0 && idx < GEVR_MAX_PLAYERS - 1 ? names[idx] : names[2];
}
static const char *stageNameById(int levelId) { return netStageName(netStageIndexOf((uint8_t)levelId)); }

// A game list's entry (the lobby service, a LAN beacon): a co-op game lists
// 0x80 | where the party is (its menus or a mission's level id), and its
// difficulty in the weapons' place (net_core.c netGetLobbyStage).
static std::string listedGameLabel(int stage, int weapons) {
    // Horde lists its map in the weapons' place (net_core.c netGetLobbyWeaponSet)
    if (stage == NET_LOBBY_HORDE_STAGE)
        return netHordeStageValid((unsigned)weapons) ? std::string("Horde: ") + netHordeStageName((unsigned)weapons) : "Horde";
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

// The host's match config from the saved choices (net_core.c, shared with the dedicated server).
static NetMatchConfig gevrLauncherConfig() { return netMatchConfigFromSettings(); }

// Co-op carries the host's deathmatch scenario along unused: never teams.
static bool gevrConfigHasTeams(int mode, int scenario) {
    return mode == NET_MODE_DEATHMATCH && netScenarioHasTeams(scenario);
}
static bool gevrLobbyHasTeams(const NetMatchConfig *c) { return gevrConfigHasTeams(c->mode, c->scenario); }

static void gevrTeamChoiceRow(const char *id) {
    if (!netIsActive() || !gevrLobbyHasTeams(netGetMatchConfig()))
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
    if (accepted->mode == NET_MODE_DEATHMATCH)
        VrMpStage = accepted->stage;
    if (accepted->mode != NET_MODE_HORDE) {
    VrMpScenario = accepted->scenario;
    VrMpWeaponSet = accepted->weapon_set;
    VrMpLength = accepted->game_length;
    VrMpHealth = accepted->health;
    VrMpDual = accepted->dual_wield;
    VrMpLoadouts = accepted->loadouts;
    VrMpNextRound = accepted->next_round;
    }
    VrMpVoiceMode = accepted->voice_mode;
    if (accepted->mode == NET_MODE_HORDE) {
        VrHordeFriendlyFire = accepted->friendly_fire;
        VrHordeStage = accepted->stage;
        VrHordeStartWave = accepted->horde_start_wave;
    }
    else VrMpFriendlyFire = accepted->friendly_fire;
    VrMpFunFlags = accepted->fun_flags & NET_FUN_MASK;
    if (accepted->mode == NET_MODE_COOP)
        VrCoopFastReinforcements = (accepted->fun_flags & NET_COOP_FAST_REINFORCEMENTS) != 0;
    VrMpGunSize = accepted->gun_size;
    VrMpMovementSpeed = accepted->movement_speed;
    if (accepted->mode == NET_MODE_DEATHMATCH) // co-op's four is not the deathmatch count
        VrMpMaxPlayers = accepted->max_players;
    if (accepted->mode != NET_MODE_HORDE) {
    VrMpBotMode = accepted->bot_mode;
    VrMpBotCount = accepted->bot_count;
    VrMpBotDifficulty = accepted->bot_difficulty;
    }
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
template <typename Mask>
static bool favoriteRow(const char *label, int count, const char *(*name)(int), Mask *mask, int perRow) {
    bool changed = false;
    ImGui::TextUnformatted(label);
    for (int i = 0; i < count && i < (int)(8 * sizeof(Mask)); i++) {
        bool on = ((*mask >> i) & 1u) != 0;
        if (i % perRow != 0)
            ImGui::SameLine();
        char id[48];
        snprintf(id, sizeof(id), "%s##%s%d", name(i), label, i);
        if (ImGui::Checkbox(id, &on)) {
            *mask = on ? (*mask | (Mask(1) << i)) : (*mask & ~(Mask(1) << i));
            changed = true;
        }
    }
    return changed;
}

// The host's match options: a section of the Host tab, before and while hosting.
static void gevrMatchLiveOptions();
static void gevrMatchOptions(bool favorites = false) {
    bool changed = false;
    if (!favorites && ImGui::BeginTable("match-columns", 2, ImGuiTableFlags_SizingStretchSame)) {
        ImGui::TableNextColumn();
        ImGui::TextColored(ImVec4(1.0f, 0.84f, 0.47f, 1.0f), "MATCH SETUP");
        ImGui::Text("Scenario:");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
        changed |= namedCombo("##scenario", netScenarioCount(), netScenarioName, &VrMpScenario);
        if (VrMpScenario == SCENARIO_YOLT) {
            ImGui::TextDisabled("Length: last one standing");
        } else {
            ImGui::Text("Length:");
            ImGui::SameLine();
            ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
            // The Living Daylights takes the time limits only, as the game's own menu has it
            changed |= namedCombo("##length", VrMpScenario == SCENARIO_TLD ? 4 : 7, netGameLengthName, &VrMpLength);
        }
        ImGui::Text("Health:");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
        changed |= namedCombo("##health", netHealthCount(), netHealthName, &VrMpHealth);
        ImGui::Text("Dual wield:");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
        changed |= namedCombo("##dual", 3, netDualWieldName, &VrMpDual);
        if (ImGui::IsItemHovered()) gevrTooltip("%s", VrMpDual == NET_DUAL_DOUBLES ? "a second copy of your gun makes a pair"
                            : VrMpDual == NET_DUAL_ANY   ? "hold X for the left hand's panel"
                                                         : "");
        ImGui::Text("Next round:");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
        changed |= namedCombo("##nextround", 3, netNextRoundName, &VrMpNextRound);
        if (ImGui::IsItemHovered()) gevrTooltip("%s", VrMpNextRound == NET_NEXT_SHUFFLE    ? "a random favorite map and set"
                                  : VrMpNextRound == NET_NEXT_PLAYLIST ? "your favorites in order"
                                                                       : "the players vote in the pause menu");
        ImGui::Spacing();
        bool loadouts = VrMpLoadouts != 0;
        if (ImGui::Checkbox("Use player loadouts", &loadouts)) {
            VrMpLoadouts = loadouts ? 1 : 0;
            changed = true;
        }
        if (ImGui::IsItemHovered()) gevrTooltip("Players spawn with their own four selected guns.");
        ImGui::TableNextColumn();
        ImGui::TextColored(ImVec4(1.0f, 0.84f, 0.47f, 1.0f), "BOTS AND LIVE SETTINGS");
        // Bots: player slots the host runs (gevr_bot.c); they change between rounds only
        if (VrMpMode == NET_MODE_DEATHMATCH) {
            ImGui::BeginDisabled(netIsActive() && !gevrNetBotRowsEditable());
            ImGui::Text("Bots:");
            ImGui::SameLine();
            ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * (VrMpBotMode == NET_BOT_FIXED ? 0.65f : 1.0f));
            changed |= namedCombo("##botmode", NET_BOT_MODE_COUNT, netBotModeName, &VrMpBotMode);
            if (VrMpBotMode == NET_BOT_FIXED) {
                ImGui::SameLine();
                ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
                int count = VrMpBotCount - 1;
                if (namedCombo("##botcount", GEVR_MAX_PLAYERS - 1, gevrBotCountName, &count)) {
                    VrMpBotCount = count + 1;
                    changed = true;
                }
                if (ImGui::IsItemHovered()) gevrTooltip("Number of bots");
            }
            if (VrMpBotMode != NET_BOT_OFF) {
                ImGui::Text("Bot difficulty:");
                ImGui::SameLine();
                ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
                changed |= namedCombo("##botdiff", NET_BOT_DIFF_COUNT, netBotDifficultyName, &VrMpBotDifficulty);
            }
            ImGui::EndDisabled();
        }
        gevrMatchLiveOptions();
        ImGui::EndTable();
    }

    if (favorites) {
        changed |= favoriteRow("Favorite maps:", netStageCount(), netStageName, &VrMpFavStages, 6);
        changed |= favoriteRow("Favorite sets:", netWeaponSetCount(), netWeaponSetName, &VrMpFavSets, 5);
    }

    if (changed)
        gevrHostChoiceChanged();
}

static void gevrMovementSpeedOptions(bool hostPage) {
    bool online = netIsActive();
    int mode = online ? netGetMatchConfig()->movement_speed
                      : hostPage ? VrMpMovementSpeed : VrMovementSpeed;
    int percent = netMovementSpeedPercent(mode);
    ImGui::BeginDisabled(online && !netIsHost());
    ImGui::TextUnformatted("Movement speed");
    const float resetWidth = ImGui::CalcTextSize("Reset").x + ImGui::GetStyle().FramePadding.x * 2;
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - resetWidth - ImGui::GetStyle().ItemSpacing.x);
    bool changed = ImGui::SliderInt("##MovementSpeed", &percent, 50, 200,
                                   percent == 100 ? "%d%% (default)" : "%d%%", ImGuiSliderFlags_AlwaysClamp);
    bool hovered = ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled);
    ImGui::SameLine();
    if (ImGui::Button("Reset##MovementSpeed")) { percent = 100; changed = true; }
    hovered |= ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled);
    if (changed) {
        mode = netMovementSpeedMode(percent);
        if (online) gevrNetConfigSet(CFG_MOVEMENT_SPEED, mode);
        else {
            (hostPage ? VrMpMovementSpeed : VrMovementSpeed) = mode;
            vrSettingsSave();
        }
    }
    ImGui::EndDisabled();
    if (hovered) gevrTooltip("%s", online || hostPage
        ? "100% is the default. Reset restores it for everyone. Host changes apply immediately. Room-scale walking keeps its normal speed."
        : "Solo stick movement, 100% is normal. Room-scale walking keeps its normal speed.");
}

static void gevrMatchLiveOptions() {
    gevrMovementSpeedOptions(true);
    ImGui::BeginDisabled(netIsActive() && !netIsHost());
    unsigned cap; bool equalized = netGetHostEqualization(&cap) != 0;
    if (ImGui::Checkbox("Host hit equalization", &equalized)) netSetHostEqualization(equalized, cap);
    if (ImGui::IsItemHovered()) gevrTooltip("Delay host hits to balance latency with guests.");
    int limit = (int)cap;
    ImGui::TextUnformatted("Host delay cap (ms)");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::SliderInt("##HostDelayCap", &limit, 0, 80)) netSetHostEqualization(equalized, (unsigned)limit);
    ImGui::EndDisabled();
    char delays[128]; netHostEqualizationText(delays, sizeof(delays));
    if (delays[0] && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) gevrTooltip("%s", delays);
    if (VrMpMode == NET_MODE_DEATHMATCH && netScenarioHasTeams(VrMpScenario)) {
        bool ff = VrMpFriendlyFire != 0;
        if (ImGui::Checkbox("Friendly fire", &ff)) {
            VrMpFriendlyFire = ff;
            gevrHostChoiceChanged();
        }
    }
}

static void gevrLobbyRoster(const ImVec4 &gold, bool showCount = true) {
    const NetMsgLobbyState *lobby = netGetLobbyState();
    static int kickSlot = -1;
    static char kickName[GEVR_MAX_NAME_LEN] = "";
    const bool canKick = netIsHost();
    if (showCount) ImGui::TextColored(gold, "PLAYERS (%d/%d)", netGetConnectedPlayerCount(), netGetMaxPlayers());
    ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(ImGui::GetStyle().CellPadding.x, 2.0f));
    if (ImGui::BeginTable("##lobbyroster", canKick ? 6 : 5, ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_BordersInnerH)) {
        float font = ImGui::GetFontSize();
        ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Character", ImGuiTableColumnFlags_WidthFixed, font * 9);
        ImGui::TableSetupColumn("Team", ImGuiTableColumnFlags_WidthFixed, font * 5);
        ImGui::TableSetupColumn("State", ImGuiTableColumnFlags_WidthFixed, font * 6);
        ImGui::TableSetupColumn("Ping (ms)", ImGuiTableColumnFlags_WidthFixed, font * 6);
        if (canKick) ImGui::TableSetupColumn("Host", ImGuiTableColumnFlags_WidthFixed, font * 3.2f);
        ImGui::TableHeadersRow();
        for (int i = 0; i < GEVR_MAX_PLAYERS; i++) {
            if (!lobby->slots[i].connected)
                continue;
            const auto &slot = lobby->slots[i];
            bool host = i == netGetHostSlot();
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(slot.name);
            if (ImGui::IsItemHovered() && slot.is_bot)
                gevrTooltip("%s\nA bot the host runs (%s)\nSlot: %d", slot.name,
                                  netBotDifficultyName(lobby->config.bot_difficulty), i + 1);
            else if (ImGui::IsItemHovered())
                gevrTooltip("%s\nBuild: %s\nProtocol: %d\nSlot: %d\nPing is the round trip to the current host.",
                                  slot.name, netGetSlotAppVersion(i), GEVR_NET_VERSION, i + 1);
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(netCharacterName(slot.chr_id));
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(gevrLobbyHasTeams(&lobby->config) ? netTeamName(slot.team) : "—");
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(host ? "Host" : slot.is_bot ? "Bot" : slot.ready ? "Ready" : "Waiting");
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(slot.is_bot ? "—" : gevrPingText(i).c_str());
            if (canKick) {
                ImGui::TableNextColumn();ImGui::PushID(i);
                if (!host) {
                    const bool supported=netHostCanKickPlayer(i)!=0;
                    ImGui::BeginDisabled(!supported);
                    if (ImGui::SmallButton("Kick")) {
                        kickSlot = i;snprintf(kickName,sizeof(kickName),"%s",slot.name);
                    }
                    ImGui::EndDisabled();
                    if (!supported && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                        gevrTooltip("This player needs the updated build for host removal.");
                }
                ImGui::PopID();
            }
        }
        ImGui::EndTable();
    }
    ImGui::PopStyleVar();
    if (kickSlot >= 0 && !ImGui::IsPopupOpen("Kick from lobby?")) ImGui::OpenPopup("Kick from lobby?");
    if (ImGui::BeginPopupModal("Kick from lobby?",nullptr,ImGuiWindowFlags_AlwaysAutoResize)) {
        const bool present = netHostCanKickPlayer(kickSlot) &&
            !strcmp(netGetSlotName(kickSlot),kickName);
        ImGui::Text("Remove %s from this session?",kickName);
        if (ImGui::Button("Cancel",ImVec2(220,52))) { kickSlot=-1;ImGui::CloseCurrentPopup(); }
        ImGui::SameLine();ImGui::BeginDisabled(!canKick || !present);
        if (ImGui::Button("Kick player",ImVec2(220,52))) {
            netHostKickPlayer(kickSlot);kickSlot=-1;ImGui::CloseCurrentPopup();
        }
        ImGui::EndDisabled();ImGui::EndPopup();
    }
}
/*
 * A host's launcher idle (gevrMultiplayerPage): idle since, on the wall clock, so
 * a sleeping headset's time counts. The page runs only while it is on screen;
 * launcher frames it missed (another page) or a match (gevrLobbyGameTick) start
 * the clock again. A sleep draws no frames at all, so it counts.
 */
static uint64_t s_launcherHostActMs = 0;
static uint64_t s_launcherFrame = 0;       // gevrLauncherRun: one per launcher frame
// The START footer's height as drawn last frame (the update line and its note
// can wrap): the open page takes the rest, so neither scrolls nor clips.
static float s_launcherFooterH = 0.0f;
static uint64_t s_launcherHostFrame = 0;   // the launcher frame the page last ran in, 0 never
#define GEVR_LAUNCHER_HOST_IDLE_MS (10ull * 60 * 1000)

/* One page frame at nowMs (wall clock, ms): true when the host's 10 minutes ran out.
 * The idle time so far is judged before this frame's activity: waking the headset
 * moves the pointer, and that must not wipe out the sleep before it. */
static bool gevrLauncherHostIdleTick(uint64_t nowMs, bool hosting, bool act)
{
    if (!hosting || s_launcherHostFrame + 1 != s_launcherFrame)
        s_launcherHostActMs = 0;   // a new hosting session, or back from another page or a match
    s_launcherHostFrame = s_launcherFrame;
    if (!hosting) return false;
    if (s_launcherHostActMs == 0) s_launcherHostActMs = nowMs;
    if (nowMs - s_launcherHostActMs >= GEVR_LAUNCHER_HOST_IDLE_MS) {
        s_launcherHostActMs = 0;
        return true;
    }
    if (act) s_launcherHostActMs = nowMs;
    return false;
}

// visibility: the lobby's own when it differs from the setting (one hosted to invite into)
static void gevrHostLobbyOptions(const ImVec4 &gold, bool hosting, const std::string &code, const char *visibility = nullptr) {
    if (ImGui::BeginTable("host-lobby-options", 2, ImGuiTableFlags_SizingStretchSame)) {
        ImGui::TableNextColumn();
        if (!hosting) {
            if (ImGui::RadioButton("Public", VrMpVisibility == 0)) { VrMpVisibility=0;vrSettingsSave(); }
            if (ImGui::IsItemHovered()) gevrTooltip("Listed for everyone under Join > Public.");
            ImGui::SameLine();
            if (ImGui::RadioButton("Friends", VrMpVisibility == 2)) { VrMpVisibility=2;vrSettingsSave(); }
            if (ImGui::IsItemHovered()) gevrTooltip("Not listed. Your friends can join from their Friends page; anyone else needs the code.");
            ImGui::SameLine();
            if (ImGui::RadioButton("Private", VrMpVisibility == 1)) { VrMpVisibility=1;vrSettingsSave(); }
            if (ImGui::IsItemHovered()) gevrTooltip("Not listed. Players join with the code or an invite.");
        }
        ImGui::TextUnformatted("Mode:");ImGui::SameLine();
        if (ImGui::RadioButton("Deathmatch", VrMpMode == NET_MODE_DEATHMATCH)) { VrMpMode=NET_MODE_DEATHMATCH;gevrHostChoiceChanged(); }
        ImGui::SameLine();
        if (ImGui::RadioButton("Co-op", VrMpMode == NET_MODE_COOP)) { VrMpMode=NET_MODE_COOP;gevrHostChoiceChanged(); }
        ImGui::SameLine();
        if (ImGui::RadioButton("Horde", VrMpMode == NET_MODE_HORDE)) { VrMpMode=NET_MODE_HORDE;gevrHostChoiceChanged(); }
        if (VrMpMode == NET_MODE_DEATHMATCH) {
            int stage=netStageIndexOf((uint8_t)VrMpStage);if (stage<0)stage=9;
            // no "Stage:" label: "Bunker II (Full)" needs the room beside the players
            const bool teams=netScenarioHasTeams(VrMpScenario);
            const float playersWidth=ImGui::CalcTextSize("8").x + ImGui::GetFrameHeight()
                + ImGui::GetStyle().FramePadding.x*2.0f;
            const float reserve=teams ? ImGui::CalcTextSize("Players: 8").x
                : ImGui::CalcTextSize("Players:").x+playersWidth+ImGui::GetStyle().ItemSpacing.x;
            ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x-reserve-ImGui::GetStyle().ItemSpacing.x);
            if (namedCombo("##stagecombo",netStageCount(),netStageName,&stage)) { VrMpStage=netStage(stage)->level_id;gevrHostChoiceChanged(); }
            if (ImGui::IsItemHovered()) gevrTooltip("Stage. \"(Full)\" is the whole mission level, not the multiplayer map.");
            ImGui::SameLine();
            if (teams) {
                ImGui::TextDisabled("Players: %d",netTeamRequiredPlayers(VrMpScenario));
                if (ImGui::IsItemHovered()) gevrTooltip("The team scenario sets the player count.");
            } else {
                ImGui::TextUnformatted("Players:");ImGui::SameLine();ImGui::SetNextItemWidth(playersWidth);
                int count=VrMpMaxPlayers-2;const int minimum=hosting?netLobbyMinPlayers():2;
                if (gevrNamedCombo("##playerscombo",GEVR_MAX_PLAYERS-1,playerCountName,&count,false,
                                  [&](int n){return n+2<minimum;})) { VrMpMaxPlayers=count+2;gevrHostChoiceChanged(); }
            }
        } else if (VrMpMode == NET_MODE_HORDE) {
            ImGui::TextDisabled("%s: solo or up to 4 players",netHordeStageName((unsigned)VrHordeStage));
            ImGui::TextWrapped("Endless waves of armed guards, with Oddjob swarms and bosses. Ammo drops from enemies. Everyone down ends the run.");
        } else {
            ImGui::TextDisabled("Campaign: up to %d players",NET_COOP_MAX_PLAYERS);
            if (ImGui::IsItemHovered()) gevrTooltip("Each player picks a folder. The host picks missions. Players can join or leave any time.");
        }
        ImGui::TableNextColumn();
        if (VrMpMode == NET_MODE_DEATHMATCH) {
            if (VrMpScenario == SCENARIO_MWTGG) ImGui::TextDisabled("Weapons: Golden Gun");
            else {
                ImGui::TextUnformatted("Weapons:");ImGui::SameLine();
                const bool custom=VrMpWeaponSet==NET_WEAPON_SET_CUSTOM;
                const float editWidth=ImGui::CalcTextSize("Edit").x+ImGui::GetStyle().FramePadding.x*2;
                ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x-(custom?editWidth+ImGui::GetStyle().ItemSpacing.x:0));
                if (namedCombo("##weaponcombo",netWeaponSetCount(),netWeaponSetName,&VrMpWeaponSet))gevrHostChoiceChanged();
                if (VrMpWeaponSet==NET_WEAPON_SET_CUSTOM) {
                    ImGui::SameLine();if (ImGui::Button("Edit##customset"))ImGui::OpenPopup("Custom weapons");
                    if (ImGui::BeginPopup("Custom weapons")) {
                        bool changed=false;
                        for (int i=0;i<4;i++) {
                            ImGui::PushID(i);ImGui::Text("Gun %d:",i+1);ImGui::SameLine();
                            ImGui::SetNextItemWidth(ImGui::GetFontSize()*12);changed|=gunCombo("##custom",&VrMpCustom[i]);ImGui::PopID();
                        }
                        if (changed)gevrHostChoiceChanged();ImGui::EndPopup();
                    }
                }
            }
        }
        if (hosting) {
            if (!code.empty()) {
                const char *shown=visibility?visibility:VrMpVisibility==2?"friends":VrMpVisibility?"private":"public";
                ImGui::TextColored(gold,"%s: %s",!strcmp(shown,"friends")?"FRIENDS":!strcmp(shown,"private")?"PRIVATE":"PUBLIC",code.c_str());ImGui::SameLine();
            }
            ImGui::TextColored(gold,"PLAYERS (%d/%d)",netGetConnectedPlayerCount(),netGetMaxPlayers());
        }
        ImGui::EndTable();
    }
    if (hosting) {
        gevrTeamChoiceRow("##hostteam");
        gevrLobbyRoster(gold,false);
        if (gevrLobbyHasTeams(netGetMatchConfig()) && !netTeamRosterReady()) {
            if (ImGui::IsItemHovered())gevrTooltip("Complete teams before countdown; warmup remains available.");
        }
    }
}

// Friends (docs/friends-and-invites.md): a sidebar to the right of the
// launcher, open from the start and folded to a strip on request (user,
// 2026-11-04). It is its own window beyond the launcher's 1280 pixels: the
// canvas and the screen quad grow by its width (vr_screen_set_width_scale), so
// no launcher page gets narrower. Joining and the lobby hosted for an invite go
// through the Multiplayer page (gevrLauncherRun opens it).
static void gevrFriendsSidebarWindow(bool onlineGame, const ImVec4 &gold, const ImVec4 &good, const ImVec4 &bad)
{
    static GevrFriendsUiState ui;
    const char *lobby = netIsActive() ? netGetLobbyCode() : "";
    GevrFriendsUiContext ctx;
    ctx.canJoin = onlineGame && !netIsActive();
    ctx.joinHint = !onlineGame ? "Online play needs the GoldenEye ROM (not a hack campaign)."
                               : "Leave your lobby to join another.";
    ctx.canInvite = onlineGame && (!netIsActive() || lobby[0]);
    ctx.inviteHint = !onlineGame ? "Online play needs the GoldenEye ROM (not a hack campaign)."
                     : lobby[0] ? "Invite them into your lobby."
                     : netIsActive() ? "This game has no internet lobby to invite into."
                     : "Starts a private lobby and invites them into it.";
    ctx.lobby = lobby;
    ctx.netVersion = GEVR_NET_VERSION;
    ctx.message = gevrFriendsFreshMessage();
    ctx.canRename = !netIsActive();   // as the Multiplayer page's name box
    gevrFriendsSidePanel(ui, ctx, gold, good, bad);
}

// The newest invite, above the launcher's page while the sidebar is folded.
static void gevrFriendsInviteBanner(bool canJoin, const ImVec4 &gold)
{
    const GevrFriendsModel &m = gevrFriends();
    if (m.invites.empty() || VrFriendsSidebar) return;
    GevrFriendsUiContext ctx;
    ctx.canJoin = canJoin;
    ctx.joinHint = "Leave your lobby, and choose the GoldenEye ROM, to join.";
    ctx.netVersion = GEVR_NET_VERSION;
    gevrFriendsInviteCard(m.invites.front(), ctx, gold);
    ImGui::Separator();
}

// The Multiplayer tab (docs/launcher-unify.md, phase 3). The lobby service's
// events, the connections, the host's heartbeat and idle stop and a client's
// match starting run every launcher frame, whichever tab is open
// (gevrMultiplayerTick); the tab only draws (gevrMultiplayerPage).
struct GevrOnlineLobby {
    std::string code, name, phase;
    int stage, weapons, players, maxPlayers;
};
static struct {
    int joinMethod = 0;   // 0 = Public Internet, 1 = Private Code, 2 = LAN Games, 3 = Direct IP
    char directIp[64] = "192.168.1.";
    char privateCode[16] = "";
    std::string hostedCode, onlineMessage, clientJoinId;
    std::vector<std::string> hostJoinIds;
    std::vector<GevrOnlineLobby> onlineLobbies;
    bool listLoading = false;
    uint32_t lastListMs = 0, lastHeartbeatMs = 0;
    int sentSlot = -1;
} s_mp;

static void gevrMpDisconnect()
{
    gevrJavaCommand("lobbyCommand", "stop");
    netDiscoveryStopBroadcasting();
    netDisconnect();
    netIceStop();
    s_mp.hostedCode.clear();
    s_mp.hostJoinIds.clear();
    s_mp.clientJoinId.clear();
}

static void gevrMpStartHosting()
{
    auto &hostedCode = s_mp.hostedCode;
    auto &hostJoinIds = s_mp.hostJoinIds;
    auto &onlineMessage = s_mp.onlineMessage;
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

void gevrMultiplayerTick(GevrLauncherTabs &tabs, bool &startMatch, bool romReady) {
    auto &hostedCode = s_mp.hostedCode;
    auto &onlineMessage = s_mp.onlineMessage;
    auto &clientJoinId = s_mp.clientJoinId;
    auto &hostJoinIds = s_mp.hostJoinIds;
    auto &onlineLobbies = s_mp.onlineLobbies;
    auto &listLoading = s_mp.listLoading;
    auto &lastHeartbeatMs = s_mp.lastHeartbeatMs;
    if (VrCampaign != GEVR_CAMPAIGN_GE && !netIsActive()) {
        // the tab says why in a hack campaign
        return;
    }
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
    // From the Friends page: a friend's lobby or an invite's, joined by its code
    // as the Private tab does; or a private lobby to invite friends into. Either
    // brings up its Multiplayer page.
    if (gevrFriendsJoinPending()) {
        const std::string code = gevrFriendsTakeJoin();
        if (netIsActive()) {
            onlineMessage = netIsHost() ? "Stop hosting before joining a friend's game." : "Disconnect before joining a friend's game.";
        } else if (!romReady) {
            onlineMessage = "Choose a GoldenEye ROM before joining a friend's game.";
        } else {
            gevrJavaCommand("requestVoicePermission", "");
            netIceStop();
            clientJoinId.clear();
            onlineMessage = "Joining your friend's game...";
            gevrJavaCommand("lobbyCommand", ("join|" + code + "|" + std::to_string(GEVR_NET_VERSION)).c_str());
            tabs.open(GEVR_LTAB_MULTIPLAYER, GEVR_LSUB_JOIN);
        }
    }
    if (gevrFriendsTakeHostRequest() && !netIsActive() && clientJoinId.empty()) {
        // a lobby hosted to invite friends into is private unless friends-only was chosen
        s_hostVisibility = VrMpVisibility == 0 ? "private" : nullptr;
        gevrMpStartHosting();
        tabs.open(GEVR_LTAB_MULTIPLAYER, GEVR_LSUB_HOST);
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

    // Launcher idle kick: if host is inactive for 10 minutes, stop hosting.
    // Wall clock: SDL_GetTicks stops while the headset sleeps, so a host who slept
    // or left the headset kept the lobby listed forever. A sleep counts as idle.
    if (!netIsHost()) gevrLauncherHostIdleTick(0, false, false);
    if (netIsHost()) {
        static float s_last_px = 0.0f, s_last_py = 0.0f;
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
        if (gevrLauncherHostIdleTick(sysGetMicroseconds() / 1000, true, act)) {
            gevrJavaCommand("lobbyCommand", "stop");
            netDiscoveryStopBroadcasting();
            netDisconnect();
            netIceStop();
            hostedCode.clear();
            hostJoinIds.clear();
            onlineMessage = "Hosting stopped due to inactivity (10 min idle)";
        }

        int pCount = netGetHumanPlayerCount();   // a joiner takes a bot's place
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
    auto disconnect = [] { gevrMpDisconnect(); };
    if (const char *refusal = netTakeJoinRefusal()) {
        onlineMessage = refusal;   // a different ROM from the host's (net_core.c NET_CONTENT_*)
        disconnect();
    }
    if (netIsActive() && !netIsHost() && netGetState() == NET_STATE_INGAME) {
        if (!romReady) {
            onlineMessage = "Cannot join match: no valid GoldenEye ROM loaded.";
            disconnect();
        } else {
            netApplyMatchConfig();
            // the boot loads g_StageNum (main.c); a co-op party's menus loaded twice so,
            // and the Rare logo and folder music played twice over (#94)
            if (netGetMatchConfig()->mode != NET_MODE_COOP)
                bossSetLoadedStage(g_StageNum);
            startMatch = true;
        }
    }
    int &sentSlot = s_mp.sentSlot;
    if (!netIsActive())
        sentSlot = -1;
    if (netIsActive() && !netIsHost() && netGetLocalSlot() >= 0) {
        if (sentSlot != netGetLocalSlot()) {
            sentSlot = netGetLocalSlot();
            gevrSendLoadout();
        }
    }
}

void gevrMultiplayerPage(int sub, bool &startMatch, bool romReady, const ImVec4 &gold, const ImVec4 &good, const ImVec4 &bad) {
    auto &joinMethod = s_mp.joinMethod;
    auto &directIp = s_mp.directIp;
    auto &privateCode = s_mp.privateCode;
    auto &hostedCode = s_mp.hostedCode;
    auto &onlineMessage = s_mp.onlineMessage;
    auto &clientJoinId = s_mp.clientJoinId;
    auto &hostJoinIds = s_mp.hostJoinIds;
    auto &onlineLobbies = s_mp.onlineLobbies;
    auto &listLoading = s_mp.listLoading;
    auto &lastListMs = s_mp.lastListMs;
    auto &lastHeartbeatMs = s_mp.lastHeartbeatMs;
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
    // the mic, the volumes and the voice mode: the settings table's rows (gevr_settings.c)
    auto audioOptions = [&]() {
        gevrSettingsSection(gevrSettingsLive(), GEVR_LSEC_MP_AUDIO, GEVR_SET_LAUNCHER);
        const bool micMuted = netVoiceIsMuted() != 0;
        // Teammates are always full volume, so the mode only matters for
        // free-for-all opponents: Horde, co-op and team battles ignore it.
        const int voiceGameMode = netIsActive() ? netGetMatchConfig()->mode : VrMpMode;
        const bool voiceTeams = netIsActive() ? gevrLobbyHasTeams(netGetMatchConfig()) : gevrConfigHasTeams(VrMpMode, VrMpScenario);
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
        ImGui::TextWrapped("%s. %s", micMuted                   ? "Mic muted"
                                  : !netVoiceHasPermission() ? "No mic access (listen only)"
                                  : netVoiceCaptureFailed()  ? "Mic unavailable (listen only)"
                                                             : "Mic active",
                           netModePve(voiceGameMode) ? "Voice mode not used: everyone is a teammate, full volume."
                           : voiceTeams ? "Voice mode not used: teammates full volume, opponents proximity."
                           : "Proximity: 10% floor; couch: full volume.");
        ImGui::PopStyleColor();
    };
    gevrLauncherBeginBody("##mpbody");
    switch (sub) {
    case GEVR_LSUB_HOST:
        if (netIsActive() && !netIsHost())
            ImGui::TextWrapped("Disconnect before hosting another game.");
        else if (ImGui::BeginTabBar("##hostpages")) {
            if (ImGui::BeginTabItem("Lobby")) {
                const bool hosting = netIsHost();
                gevrHostLobbyOptions(gold, hosting, hostedCode, s_hostVisibility);
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Match")) {
                if (netModePve(VrMpMode)) {
                    if (ImGui::BeginTable("coop-match-columns", 2, ImGuiTableFlags_SizingStretchSame)) {
                        ImGui::TableNextColumn();
                        ImGui::TextColored(gold, VrMpMode == NET_MODE_HORDE ? "HORDE RULES" : "MISSION RULES");
                        // co-op: the mission and difficulty decide the rest (Lobby tab)
                        ImGui::BeginDisabled(netIsActive() && !netIsHost());
                        bool ff = (VrMpMode == NET_MODE_HORDE ? VrHordeFriendlyFire : VrMpFriendlyFire) != 0;
                        if (ImGui::Checkbox("Friendly fire", &ff)) {
                            if (VrMpMode == NET_MODE_HORDE) VrHordeFriendlyFire = ff;
                            else VrMpFriendlyFire = ff;
                            gevrHostChoiceChanged();
                        }
                        if (VrMpMode == NET_MODE_HORDE) {
                            // the map and the first wave (user, 2026-10-17); a change
                            // reaches the next launch or retry
                            int pick = netStageIndexOf((uint8_t)VrHordeStage);
                            if (pick < 0) pick = netStageIndexOf(NET_HORDE_STAGE);
                            ImGui::TextUnformatted("Map:");ImGui::SameLine();ImGui::SetNextItemWidth(-1);
                            if (namedCombo("##hordemap", netHordeStageCount(), netStageName, &pick)) {
                                VrHordeStage = netStage(pick)->level_id;
                                gevrHostChoiceChanged();
                            }
                            ImGui::TextUnformatted("Start at wave:");ImGui::SameLine();ImGui::SetNextItemWidth(-1);
                            if (ImGui::SliderInt("##hordewave", &VrHordeStartWave, 1, NET_HORDE_MAX_START_WAVE, "%d")) {
                                gevrHostChoiceChanged();
                            }
                            if (ImGui::IsItemHovered())
                                gevrTooltip("Later waves start with the points the skipped waves would have paid.");
                        }
                        if (VrMpMode == NET_MODE_COOP) {
                        bool fast = netIsActive() ? (netGetMatchConfig()->fun_flags & NET_COOP_FAST_REINFORCEMENTS) != 0
                                            : VrCoopFastReinforcements != 0;
                        if (ImGui::Checkbox("Fast reinforcements", &fast)) {
                            VrCoopFastReinforcements = fast ? 1 : 0;
                            gevrHostChoiceChanged();
                        }
                        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) gevrTooltip("Harder: guards can call more reinforcements while earlier ones are alive. Changes apply immediately.");
                        }
                        bool ally = VrAlly != 0;
                        if (ImGui::Checkbox("AI ally (Natalya)", &ally)) {
                            VrAlly = ally ? 1 : 0;
                            vrSettingsSave();
                        }
                        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                            gevrTooltip("One companion for the party, from the next mission or Horde retry. Weapon, aggression and durability: Play > Game rules > AI ally.");
                        ImGui::EndDisabled();
                        ImGui::TableNextColumn();
                        ImGui::TextColored(gold, "LIVE SETTINGS");
                        gevrMatchLiveOptions();
                        ImGui::EndTable();
                    }
                } else {
                    gevrMatchOptions();
                }
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Fun")) {
                // the host's; online the match's, live (the settings table's rows)
                ImGui::TextDisabled(netIsActive() && netGetPhase() == NET_PHASE_IN_PROGRESS
                                        ? "Pending: applies next round" : "Applies when the round loads");
                gevrSettingsSection(gevrSettingsLive(), GEVR_LSEC_MP_FUN, GEVR_SET_LAUNCHER);
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }
        break;
    case GEVR_LSUB_JOIN: {
        const bool connected = netIsActive() && !netIsHost() && netGetState() != NET_STATE_CONNECTING;
        if (ImGui::BeginTabBar("##joinpages")) {
            const char *titles[] = {connected ? "Lobby###publicpane" : "Public###publicpane",
                                    "Private",
                                    "LAN",
                                    "Direct IP"};
            for (int pane = 0; pane < 4; pane++) {
                if (!ImGui::BeginTabItem(titles[pane]))
                    continue;
                joinMethod = pane;
                if (netIsHost())
                    ImGui::TextWrapped("Stop hosting before joining another game.");
                else if (netGetState() == NET_STATE_CONNECTING)
                    ImGui::TextUnformatted("Connecting to host...");
                else if (connected) {
                    const NetMatchConfig *cfg = netGetMatchConfig();
                    if (cfg->mode == NET_MODE_HORDE) {
                        ImGui::TextDisabled("Horde: %s, starting at wave %d",netHordeStageName(cfg->stage),
                                            cfg->horde_start_wave ? cfg->horde_start_wave : 1);
                    } else if (cfg->mode == NET_MODE_COOP) {
                        ImGui::Text("Co-op campaign: the host picks the missions");
                        ImGui::TextDisabled("Fast reinforcements: %s (host chooses)",
                            cfg->fun_flags & NET_COOP_FAST_REINFORCEMENTS ? "On" : "Off");
                    } else
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
                        for (const GevrOnlineLobby &game : onlineLobbies) {
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
                                gevrTooltip(
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
        break;
    }
    case GEVR_LSUB_PLAYER:
        playerOptions();
        break;
    case GEVR_LSUB_AUDIO:
        audioOptions();
        if (netIsActive() && !netIsHost() && netGetState() != NET_STATE_CONNECTING) {
            // the host's Fun, to read
            ImGui::Separator();
            gevrSettingsSection(gevrSettingsLive(), GEVR_LSEC_MP_FUN, GEVR_SET_LAUNCHER);
        }
        break;
    default:
        gevrMatchOptions(true);   // Favorites
        break;
    }
    ImGui::EndChild();
    ImGui::Separator();
    // Fixed footer: long rosters, popups and status messages cannot push it away.
    gevrLauncherStatus(onlineMessage.empty()
                           ? (!romReady ? "Choose a GoldenEye ROM before launching a match."
                                        : (netIsHost() ? (netLobbyCanLaunch() ? "Ready to launch warmup."
                                                                              : "Waiting for joined players to ready up.")
                                                       : "Choose a game or connection method."))
                           : onlineMessage.c_str());
    int pCount = netGetHumanPlayerCount();   // the lobby service counts the people
    float h = ImGui::GetFrameHeight() * 1.5f;
    if (netIsHost()) {
        ImGui::BeginDisabled(!netLobbyCanLaunch() || !romReady);
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
            ImGui::BeginDisabled(gevrLobbyHasTeams(netGetMatchConfig()) && netGetSlotTeam(slot) == NET_TEAM_NONE);
            if (ImGui::Checkbox("Ready", &ready))
                netLobbySetReady(ready);
            ImGui::EndDisabled();
            ImGui::SameLine();
        }
        if (ImGui::Button("Disconnect", ImVec2(0, h)))
            gevrMpDisconnect();
        ImGui::SameLine();
    } else if (!clientJoinId.empty()) {
        if (ImGui::Button("Cancel Join", ImVec2(0, h))) { gevrMpDisconnect(); onlineMessage.clear(); }
        ImGui::SameLine();
    } else if (sub != GEVR_LSUB_JOIN) {
        if (ImGui::Button("Start Hosting", ImVec2(0, h))) {
            s_hostVisibility = nullptr;
            gevrMpStartHosting();
        }
        ImGui::SameLine();
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
    s_launcherHostFrame = 0;   // in a match: the launcher's idle clock starts again on return
    if (now - lastTick < 250) return;
    lastTick = now;
    gevrFriendsTick(1);   // presence from the running game, and invites to announce

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
    io.AddMousePosEvent(owns ? u * io.DisplaySize.x : -FLT_MAX, owns ? v * kTexH : -FLT_MAX);
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

// The same keyboard for the in-game surface (vr_pause_menu.cpp): the Friends
// panel's name, search and code boxes. Begin when it opens, End when it closes.
void gevrTextInputBegin()
{
    if (!s_kbdLock) s_kbdLock = SDL_CreateMutex();
    SDL_StopTextInput();
    SDL_AddEventWatch(keyboardWatch, nullptr);
}
void gevrTextInputFeed(ImGuiIO &io) { feedKeyboard(io); }
void gevrTextInputShow(const ImGuiIO &io) { showKeyboard(io); }
void gevrTextInputEnd()
{
    SDL_DelEventWatch(keyboardWatch, nullptr);
    SDL_StopTextInput();
    if (s_kbdLock) SDL_LockMutex(s_kbdLock);
    s_kbdQueue.clear();
    if (s_kbdLock) SDL_UnlockMutex(s_kbdLock);
}

/*
 * What the game starts with, on START (user, test.21: the old first page's
 * "Mods... (Bean on)" and "Cheats... (3 on)" showed it before launching; the
 * tabbed launcher had lost it). Cheats count as the old page did: the gun
 * size, Unlock all, and the ticked cheats with GoldenEye's campaign only.
 */
static void gevrLauncherLoadout(char *buf, size_t size)
{
    std::string parts;
    int cheats = (VrGunSizeCheat ? 1 : 0) + (VrUnlockAll ? 1 : 0);

    for (int b = 0; b < 64 && VrCampaign == GEVR_CAMPAIGN_GE; b++) cheats += (int)((VrCheatMask >> b) & 1ULL);
    if (gevrBeanIsEnabled()) parts = "Bean";
    else if (g_ActiveExtTexPack[0]) parts = "HD";
    if (VrGexGuns) parts += parts.empty() ? "GE-X" : ", GE-X";
    if (VrOddjobHat && gevrHatAvailable()) parts += parts.empty() ? "Oddjob's hat" : ", Oddjob's hat";
    if (cheats) {
        char n[32];
        snprintf(n, sizeof(n), "%d cheat%s", cheats, cheats == 1 ? "" : "s");
        parts += parts.empty() ? n : std::string(", ") + n;
    }
    snprintf(buf, size, parts.empty() ? "START###start" : "START  (%s)###start", parts.c_str());
}

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
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, kTexW + kGevrSidebarW, kTexH, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    std::string active, message, gfNote, tndNote, mirrorNote, campaignFail;
    std::string footerNote;    // the line under START (gevrLauncherStartRow), this frame
    bool footerBad = false;
    bool gfGood = false, tndGood = false, mirrorGood = false;   // the hack ROMs' header checks, refreshed about once a second
    Uint32 hackCheckMs = 0;
    {
        // gevr_romload.c leaves this when a chosen campaign did not load
        const std::string failed = dataDir() + "/" + GEVR_CAMPAIGN_FAILED_FILE;
        FILE *f = fopen(failed.c_str(), "rb");
        if (f) {
            char text[160] = {};
            fread(text, 1, sizeof(text) - 1, f);
            fclose(f);
            remove(failed.c_str());
            campaignFail = text;
        }
    }
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
    GevrLauncherTabs launcherTabs;   // Play, Multiplayer, Controls...: gevr_launcher_ui.h
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
                const int players = netGetHumanPlayerCount();
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
            gevrFriendsLoadPendingJoin();   // an invite accepted in a mission, before the restart
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

        // the launcher's 1280 pixels, and the friends sidebar beyond them
        const int canvasW = gevrSurfaceCanvasW();
        io.DisplaySize = ImVec2((float)canvasW, (float)kTexH);
        vr_screen_set_width_scale((float)canvasW / (float)kTexW);
        ImGui_ImplOpenGL3_NewFrame();
        ImGui::NewFrame();
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(ImVec2((float)kTexW, (float)kTexH));
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

        s_launcherFrame++;   // the host's idle clock (gevrMultiplayerPage) sees frames it missed
        // Friends: presence and the list, every frame (vr_friends.cpp sends at most once a second).
        gevrFriendsTick(0);
        const bool onlineGame = romReady && VrCampaign == GEVR_CAMPAIGN_GE;
        if (gevrFriendsJoinPending() && !onlineGame) {
            vr_log("launcher: friend's lobby %s not joined: no GoldenEye ROM in use", gevrFriendsTakeJoin().c_str());
        }
        // the lobby and its connections, whichever tab is open; a friend's lobby,
        // an invite, or a lobby to host for an invite brings up Multiplayer
        gevrMultiplayerTick(launcherTabs, start, romReady);
        if (!reportPage) gevrFriendsInviteBanner(onlineGame && !netIsActive(), gold);
        {
            // About once a second, not every frame: each check opens a 16-24 MB
            // file on the headset's shared storage. A pick lands as
            // data/picked-*.z64 and is adopted here.
            const Uint32 nowMs = SDL_GetTicks();
            if (hackCheckMs == 0 || nowMs - hackCheckMs > 1000) {
                hackCheckMs = nowMs ? nowMs : 1;
                if (adoptHackFile("picked-goldfinger.z64", GEVR_ROM_GF_FILE, "gf64",
                                  "Goldfinger 64 ROM chosen.",
                                  "That is not Goldfinger 64 1.0 (24 MB, title GOLDFINGER).",
                                  gfNote)) {
                    VrCampaign = GEVR_CAMPAIGN_GF64;
                    gevrRomCampaignSet(VrCampaign);
                    vrSettingsSave();
                }
                if (adoptHackFile("picked-tnd.z64", GEVR_ROM_TND_FILE, "tnd64",
                                  "Tomorrow Never Dies ROM chosen.",
                                  "That is not Tomorrow Never Dies 64 Expanded 06-22.",
                                  tndNote)) {
                    VrCampaign = GEVR_CAMPAIGN_TND64;
                    gevrRomCampaignSet(VrCampaign);
                    vrSettingsSave();
                }
                if (adoptHackFile("picked-mirror.z64", GEVR_ROM_MIRROR_FILE, "mirror",
                                  "Mirror Mode ROM chosen (by Wreck).",
                                  "That is not Wreck's Mirror Mode (August 2017).",
                                  mirrorNote)) {
                    VrCampaign = GEVR_CAMPAIGN_MIRROR;
                    gevrRomCampaignSet(VrCampaign);
                    vrSettingsSave();
                }
                mirrorGood = hackHeaderOk(dataDir() + "/" + GEVR_ROM_MIRROR_FILE, "mirror");
                gfGood = hackHeaderOk(dataDir() + "/" + GEVR_ROM_GF_FILE, "gf64");
                tndGood = hackHeaderOk(dataDir() + "/" + GEVR_ROM_TND_FILE, "tnd64");
                if (VrCampaign == GEVR_CAMPAIGN_GF64 && !gfGood) {
                    VrCampaign = GEVR_CAMPAIGN_GE;
                    gevrRomCampaignSet(VrCampaign);
                    vrSettingsSave();
                    if (gfNote.empty()) gfNote = "Goldfinger 64 is not installed. This start uses GoldenEye.";
                } else if (VrCampaign == GEVR_CAMPAIGN_TND64 && !tndGood) {
                    VrCampaign = GEVR_CAMPAIGN_GE;
                    gevrRomCampaignSet(VrCampaign);
                    vrSettingsSave();
                    if (tndNote.empty()) tndNote = "Tomorrow Never Dies is not installed. This start uses GoldenEye.";
                } else if (VrCampaign == GEVR_CAMPAIGN_MIRROR && !mirrorGood) {
                    VrCampaign = GEVR_CAMPAIGN_GE;
                    gevrRomCampaignSet(VrCampaign);
                    vrSettingsSave();
                    if (mirrorNote.empty()) mirrorNote = "Mirror Mode is not installed. This start uses GoldenEye.";
                }
            }
        }
        float footerTop = -1.0f;   // where the START footer starts, measured to the hint's end
        if (reportPage) {
            gevrReportPage(reportPage, reportCrash, gold, good, bad);
        } else {
        // The frame (docs/launcher-unify.md): the tab rows, the open page, and
        // under it the update line, START and the campaign. Multiplayer draws
        // its own buttons in START's place. Settings are the table's rows
        // (port/src/gevr_settings.c), the in-game screen's too.
        const bool updLine = upd.state == "available" || upd.state == "downloading"
            || upd.state == "permission" || upd.state == "installing"
            || upd.state == "error" || !upd.message.empty();
        const float footerH = s_launcherFooterH > 0.0f ? s_launcherFooterH
            : gevrLauncherFooterHeight() + (updLine ? ImGui::GetTextLineHeightWithSpacing() : 0.0f);
        const GevrSettingsApi settings = gevrSettingsLive();
        auto note = [](const char *text) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
            ImGui::TextWrapped("%s", text);
            ImGui::PopStyleColor();
        };
        gevrLauncherTabRows(launcherTabs, [&](int tab, int sub) {
            if (tab == GEVR_LTAB_MULTIPLAYER) {
                // Online play, co-op and bots: GoldenEye only. A hack's levels and
                // characters sit under GoldenEye's numbers and peers do not compare
                // campaigns, so the two sides would load different games.
                if (VrCampaign != GEVR_CAMPAIGN_GE && !netIsActive()) {
                    note("Online play, co-op and bots are GoldenEye's: choose GoldenEye beside START, on any other tab.");
                    return;
                }
                gevrMultiplayerPage(sub, start, romReady, gold, good, bad);
                return;
            }
            gevrLauncherBeginPage("launcher-page", footerH);
            switch (tab) {
            case GEVR_LTAB_PLAY: {
                gevrSettingsSection(settings, GEVR_LSEC_PLAY, GEVR_SET_LAUNCHER);
                ImGui::Spacing();
                bool tests = upd.testBuilds;
                if (ImGui::Checkbox("Offer test builds", &tests)) {
                    upd.testBuilds = tests;
                    gevrUpdaterCommand(tests ? "testbuilds:1" : "testbuilds:0");
                }
                if (ImGui::IsItemHovered()) gevrTooltip("Pre-release builds show up as updates too.");
                break;
            }
            case GEVR_LTAB_CONTROLS:
                if (sub == GEVR_LSUB_HANDS) {
                    gevrSettingsSection(settings, GEVR_LSEC_HANDS, GEVR_SET_LAUNCHER);
                    note("Gun fit: in the next level, with a gun in hand, the sticks move the gun on your hand; "
                         "A saves, B undoes. In single player, Menu + A starts and ends it during play.");
                } else if (sub == GEVR_LSUB_GESTURES) {
                    // GEVR PC's grip gestures (its CONTROLS.md, vr450..vr453), Perfect Dark VR's hand reload
                    note("Stereo. Squeeze the grip with the hand in place; anywhere else the grip aims as before.");
                    gevrSettingsSection(settings, GEVR_LSEC_GESTURES, GEVR_SET_LAUNCHER);
                } else if (sub == GEVR_LSUB_THROWING) {
                    note("Hold Grip on grenades, knives and mines, swing your arm and let go to throw. "
                         "Grip + Trigger cooks a grenade.");
                    gevrSettingsSection(settings, GEVR_LSEC_THROWING, GEVR_SET_LAUNCHER);
                } else {
                    gevrHapticsPage(gold, good, bad);
                }
                break;
            case GEVR_LTAB_COMFORT:
                gevrSettingsSection(settings, GEVR_LSEC_COMFORT, GEVR_SET_LAUNCHER);
                break;
            case GEVR_LTAB_SCREEN:
                note("Both grips grab the screen; the right stick sets its distance and size. "
                     "Hold the left stick click to recentre it.");
                gevrSettingsSection(settings, GEVR_LSEC_SCREEN, GEVR_SET_LAUNCHER);
                // XBLA / Bean's frames take longer than 120 Hz's 8.3 ms when turning
                // in open levels (Dam on Quest 3: 197 missed frames in three
                // minutes of turning at 120 Hz, 17 at 90).
                if (gevrBeanIsEnabled() && VrRefreshRate > 90 && !vr_spacewarp_setting_on()) {
                    ImGui::PushStyleColor(ImGuiCol_Text, bad);
                    ImGui::TextWrapped("Bean can stutter at 120 Hz while turning in open levels: "
                                       "90 Hz, or SpaceWarp, holds nearly every frame.");
                    ImGui::PopStyleColor();
                }
                break;
            case GEVR_LTAB_RULES:
                if (sub == GEVR_LSUB_GAME_RULES) {
                    // Game rules GEVR PC changed (vr450.2, its CORPSEKEEP); off keeps the original game's rule
                    note("Off keeps the original rules. Mines are solo; with ragdolls on, bodies stay in every mode. "
                         "Fast reinforcements works in co-op too, the host's.");
                    gevrSettingsSection(settings, GEVR_LSEC_RULES, GEVR_SET_LAUNCHER);
                    ImGui::Spacing();
                    if (ImGui::Button("Original rules")) {
                        VrAlly = 0;
                        VrMinesStickToGuards = 0;
                        VrBodiesStay = 0;
                        VrRagdolls = VrDismemberment = 0;
                        if (netIsActive()) gevrNetConfigSet(CFG_FAST_REINFORCEMENTS, 0);
                        else VrFastReinforcements = 0;
                        vrSettingsSave();
                    }
                } else if (sub == GEVR_LSUB_ALLY) {
                    // #194: Natalya as a companion (src/game/gevr_ally.c). The host's choice in co-op and Horde.
                    note("GoldenEye solo, campaign co-op and Horde: one companion for the party. The host chooses; "
                         "changes apply from the next mission or Horde retry.");
                    gevrSettingsSection(settings, GEVR_LSEC_ALLY, GEVR_SET_LAUNCHER);
                } else {
                    gevrCheatsPage(settings, gold);
                }
                break;
            case GEVR_LTAB_MODS:
                gevrModsPage(now, gold, good, bad);
                break;
            }
            ImGui::EndChild();
        });
        if (launcherTabs.top != GEVR_LTAB_MULTIPLAYER) footerTop = ImGui::GetCursorPosY();
        if (launcherTabs.top != GEVR_LTAB_MULTIPLAYER) {
        ImGui::Separator();
        // Update line: only when there is something to say, so an
        // up-to-date launcher looks as it always has.
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
        const bool campaignReady = VrCampaign == GEVR_CAMPAIGN_GE
            || (VrCampaign == GEVR_CAMPAIGN_GF64 && gfGood)
            || (VrCampaign == GEVR_CAMPAIGN_TND64 && tndGood)
            || (VrCampaign == GEVR_CAMPAIGN_MIRROR && mirrorGood);
        char startLabel[160];
        gevrLauncherLoadout(startLabel, sizeof(startLabel));   /* "START  (Bean, GE-X, 3 cheats)" */
        switch (gevrLauncherStartRow(&VrCampaign, gfGood, tndGood, mirrorGood,
                                     !active.empty() && activeInfo.good && campaignReady, focusStart, startLabel)) {
        case GEVR_START_ROW_START:
            start = true;
            break;
        case GEVR_START_ROW_CAMPAIGN:
            gevrRomCampaignSet(VrCampaign);
            vrSettingsSave();
            gfNote.clear();
            tndNote.clear();
            mirrorNote.clear();
            campaignFail.clear();
            break;
        case GEVR_START_ROW_PICK_GF:
            gfNote = gevrOpenPicker("openGoldfingerPicker") ? "Pick the Goldfinger 64 ROM in the window that opened."
                                                            : "Could not open the file picker.";
            break;
        case GEVR_START_ROW_PICK_TND:
            tndNote = gevrOpenPicker("openTndPicker") ? "Pick the Tomorrow Never Dies ROM in the window that opened."
                                                      : "Could not open the file picker.";
            break;
        case GEVR_START_ROW_PICK_MIRROR:
            mirrorNote = gevrOpenPicker("openMirrorPicker") ? "Pick Wreck's Mirror Mode ROM in the window that opened."
                                                            : "Could not open the file picker.";
            break;
        }
        if (s_injectStart && !active.empty() && activeInfo.good && campaignReady) {
            start = true;
        }
        s_injectStart = false;
        focusStart = false;
        // the line under START: a campaign's news if there is any, its terms if
        // a hack is chosen, the hint otherwise (one line, so the page keeps its height)
        footerNote = !campaignFail.empty() ? campaignFail : !gfNote.empty() ? gfNote : !tndNote.empty() ? tndNote : !mirrorNote.empty() ? mirrorNote
                   : VrCampaign == GEVR_CAMPAIGN_GE ? std::string()
                   : VrCampaign == GEVR_CAMPAIGN_MIRROR ? "Solo, with its own save. Online play and GoldenEye X are off."
                   : "Solo, with its own save. Online play, GoldenEye X, Bean and HD packs are off.";
        footerBad = !campaignFail.empty();
        }   // the START footer
        }   // reportPage
        if (footerNote.empty()) {
            ImGui::TextDisabled("Point and pull the trigger, or use the stick and A.");
        } else {
            ImGui::PushStyleColor(ImGuiCol_Text, footerBad ? bad : ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
            gevrLauncherStatus(footerNote.c_str());
            ImGui::PopStyleColor();
            footerNote.clear();
        }
        if (footerTop >= 0.0f) s_launcherFooterH = ImGui::GetCursorPosY() - footerTop;
        ImGui::End();
        gevrFriendsSidebarWindow(onlineGame, gold, good, bad);
        ImGui::Render();
        showKeyboard(io);

        GLint prevFbo = 0;
        glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFbo);
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        glViewport(0, 0, canvasW, kTexH);
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
        vr_screen_present_tex2d(tex, canvasW, kTexH);
        gevrVrPumpEnd();
    }

    vr_screen_set_width_scale(1.0f);   // the game's screen and the pause are 4:3 again
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
