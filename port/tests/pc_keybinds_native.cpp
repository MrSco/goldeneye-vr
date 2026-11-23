// Native test for Phase 2 (PC keybinds, mouse settings, Previous weapon on wheel up):
// - Bind capture flow (keys, mouse buttons, wheel up/down, pad buttons; Esc cancels; Del/Bksp clears)
// - Save / load round-trip and empty-config default preservation (wheel up -> prev weapon, wheel dn -> next weapon)
// - Duplicate bind detection across actions
// - Campaign-specific action labels across all 5 campaigns
// - Layout fit (zero scrolling, zero horizontal clipping) of Keyboard & mouse and Gamepad pages
//   at PC window (1280x720), VR panel (1280x960), and in-game pause overlay (1244x612) sizes.
#define GEVR_KEYBINDS_NO_LIVE
#include "gevr_keybinds_ui.h"
#include "imgui/imgui_internal.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <sstream>
#include <string>
#include <vector>

static void check(bool ok, const char *what) {
    if (!ok) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        std::exit(1);
    }
}

namespace {

struct FakeBackend {
    unsigned binds[40][4] = {};   /* input.h CK_TOTAL_COUNT: the pad's 32 and CK_RESTART */
    int lastKey = 0;
    bool capture = false;
    int saveCount = 0;
    float mouseX = 2.5f, mouseY = 2.5f;
    int rawInput = 1;
    int mouseLockMode = 2;
    int swapSticks = 1;
    float moveDz = 0.125f, moveSc = 1.0f;
    float lookSpd = 1.0f, lookDz = 0.125f;
    int lookInv = 0;
    float padVib = 0.3f;
    int trigFb = 100;

    static bool isPad(unsigned vk) { return vk >= GEVR_VK_JOY_BEGIN && vk < GEVR_VK_VR_BEGIN; }
    static bool isKbm(unsigned vk) { return vk > 0 && vk < GEVR_VK_JOY_BEGIN; }

    void setDefaults() {
        std::memset(binds, 0, sizeof(binds));
        // Mirror inputSetDefaultKeyBinds (GEVR_PC || GEVR_MOBILE)
        binds[GEVR_CK_A][0] = 20; /* Q */ binds[GEVR_CK_A][1] = GEVR_VK_MOUSE_WHEEL_DN;
        binds[GEVR_CK_A][2] = GEVR_VK_JOY_BEGIN + 0; /* Pad A */ binds[GEVR_CK_A][3] = GEVR_VK_JOY_BEGIN + 2; /* Pad X */
        binds[GEVR_CK_PREV_WEAPON][0] = GEVR_VK_MOUSE_WHEEL_UP;
        binds[GEVR_CK_B][0] = 8; /* E */ binds[GEVR_CK_B][1] = 21; /* R */
        binds[GEVR_CK_B][2] = GEVR_VK_JOY_BEGIN + 1; /* Pad B */ binds[GEVR_CK_B][3] = GEVR_VK_JOY_BEGIN + 3; /* Pad Y */
        binds[GEVR_CK_ZTRIG][0] = GEVR_VK_MOUSE_BEGIN; /* L-Click */ binds[GEVR_CK_ZTRIG][1] = 44; /* Space */
        binds[GEVR_CK_ZTRIG][2] = GEVR_VK_JOY_BEGIN + 31; /* RT */
        binds[GEVR_CK_RTRIG][0] = GEVR_VK_MOUSE_BEGIN + 2; /* R-Click */ binds[GEVR_CK_RTRIG][1] = 29; /* Z */
        binds[GEVR_CK_RTRIG][2] = GEVR_VK_JOY_BEGIN + 30; /* LT */ binds[GEVR_CK_RTRIG][3] = GEVR_VK_JOY_BEGIN + 10; /* RB */
        binds[GEVR_CK_LTRIG][0] = 9; /* F */ binds[GEVR_CK_LTRIG][1] = 27; /* X */
        binds[GEVR_CK_LTRIG][2] = GEVR_VK_JOY_BEGIN + 9; /* LB */
        binds[GEVR_CK_START][0] = 40; /* Enter */ binds[GEVR_CK_START][1] = 43; /* Tab */
        binds[GEVR_CK_START][2] = GEVR_VK_JOY_BEGIN + 6; /* Start */
        binds[GEVR_CK_C_U][0] = 26; /* W */ binds[GEVR_CK_C_U][1] = GEVR_VK_JOY_BEGIN + 11;
        binds[GEVR_CK_C_D][0] = 22; /* S */ binds[GEVR_CK_C_D][1] = GEVR_VK_JOY_BEGIN + 12;
        binds[GEVR_CK_C_L][0] = 4;  /* A */ binds[GEVR_CK_C_L][1] = GEVR_VK_JOY_BEGIN + 13;
        binds[GEVR_CK_C_R][0] = 7;  /* D */ binds[GEVR_CK_C_R][1] = GEVR_VK_JOY_BEGIN + 14;
        binds[GEVR_CK_STICK_XNEG][0] = 80; /* Left */
        binds[GEVR_CK_STICK_XPOS][0] = 79; /* Right */
        binds[GEVR_CK_STICK_YNEG][0] = 81; /* Down */
        binds[GEVR_CK_STICK_YPOS][0] = 82; /* Up */
        binds[GEVR_CK_ACCEPT][0] = GEVR_VK_JOY_BEGIN + 0;
        binds[GEVR_CK_RESTART][0] = GEVR_VK_BACKSPACE;
    }

    unsigned getClassBind(unsigned ck, bool pad, int slot) const {
        if (ck >= 40 || slot < 0 || slot >= 2) return 0;
        int seen = 0;
        for (int i = 0; i < 4; i++) {
            const unsigned vk = binds[ck][i];
            if (pad ? isPad(vk) : isKbm(vk)) {
                if (seen == slot) return vk;
                seen++;
            }
        }
        return 0;
    }

    void setClassBind(unsigned ck, bool pad, int slot, unsigned vk) {
        if (ck >= 40 || slot < 0 || slot >= 2) return;
        unsigned mine[2] = {0, 0}; int nm = 0;
        unsigned other[4] = {0}; int no = 0;
        for (int i = 0; i < 4; i++) {
            const unsigned cur = binds[ck][i];
            if (!cur) continue;
            if (pad ? isPad(cur) : isKbm(cur)) { if (nm < 2) mine[nm++] = cur; }
            else { if (no < 4) other[no++] = cur; }
        }
        if (vk == 0) {
            if (slot < nm) {
                if (slot == 0) { mine[0] = nm > 1 ? mine[1] : 0; mine[1] = 0; }
                else mine[1] = 0;
            }
        } else {
            if (1 - slot < nm && mine[1 - slot] == vk) mine[1 - slot] = 0;
            mine[slot] = vk;
            if (mine[0] == 0 && mine[1] != 0) { mine[0] = mine[1]; mine[1] = 0; }
        }
        std::memset(binds[ck], 0, sizeof(binds[ck]));
        int out = 0;
        for (int i = 0; i < 2 && out < 4; i++) if (mine[i]) binds[ck][out++] = mine[i];
        for (int i = 0; i < no && out < 4; i++) if (other[i]) binds[ck][out++] = other[i];
    }

    int countBinds(unsigned vk, bool pad, const unsigned *acts, int n) const {
        int c = 0;
        for (int i = 0; i < n; i++) {
            if (getClassBind(acts[i], pad, 0) == vk || getClassBind(acts[i], pad, 1) == vk) c++;
        }
        return c;
    }

    // Serialize/deserialize like inputSaveBinds/inputLoadBinds
    std::map<unsigned, std::string> saveStrings() const {
        std::map<unsigned, std::string> out;
        for (unsigned ck = 0; ck < 40; ck++) {
            std::string s;
            for (int b = 0; b < 4; b++) {
                if (binds[ck][b]) {
                    if (!s.empty()) s += ", ";
                    s += std::to_string(binds[ck][b]);
                }
            }
            out[ck] = s.empty() ? "NONE" : s;
        }
        return out;
    }

    void loadStrings(const std::map<unsigned, std::string> &saved) {
        for (const auto &kv : saved) {
            const unsigned ck = kv.first;
            const std::string &str = kv.second;
            if (str.empty()) continue; // empty keeps defaults!
            std::memset(binds[ck], 0, sizeof(binds[ck]));
            if (str == "NONE") continue;
            std::istringstream iss(str);
            std::string tok;
            int idx = 0;
            while (std::getline(iss, tok, ',') && idx < 4) {
                unsigned v = (unsigned)std::strtoul(tok.c_str(), nullptr, 10);
                if (v) binds[ck][idx++] = v;
            }
        }
    }
};

static FakeBackend g_b;

static const char *fakeKeyName(int vk) {
    if (vk <= 0) return "—";
    if (vk == GEVR_VK_MOUSE_BEGIN) return "L-Click";
    if (vk == GEVR_VK_MOUSE_BEGIN + 2) return "R-Click";
    if (vk == GEVR_VK_MOUSE_WHEEL_UP) return "Wheel Up";
    if (vk == GEVR_VK_MOUSE_WHEEL_DN) return "Wheel Dn";
    if (vk == 20) return "Q";
    if (vk == 8) return "E";
    if (vk == 21) return "R";
    if (vk == 26) return "W";
    if (vk == 22) return "S";
    if (vk == 4) return "A";
    if (vk == 7) return "D";
    if (vk == 44) return "Space";
    if (vk == 40) return "Enter";
    if (vk == 43) return "Tab";
    if (vk == GEVR_VK_JOY_BEGIN + 0) return "Pad A";
    if (vk == GEVR_VK_JOY_BEGIN + 1) return "Pad B";
    if (vk == GEVR_VK_JOY_BEGIN + 2) return "Pad X";
    if (vk == GEVR_VK_JOY_BEGIN + 3) return "Pad Y";
    if (vk == GEVR_VK_JOY_BEGIN + 9) return "LB";
    if (vk == GEVR_VK_JOY_BEGIN + 10) return "RB";
    if (vk == GEVR_VK_JOY_BEGIN + 30) return "LT";
    if (vk == GEVR_VK_JOY_BEGIN + 31) return "RT";
    static char buf[24];
    std::snprintf(buf, sizeof(buf), "Key%d", vk);
    return buf;
}

static GevrKeybindApi makeApi() {
    GevrKeybindApi a;
    a.getBind = [](unsigned ck, bool pad, int slot) { return g_b.getClassBind(ck, pad, slot); };
    a.setBind = [](unsigned ck, bool pad, int slot, unsigned vk) { g_b.setClassBind(ck, pad, slot, vk); };
    a.resetBinds = [](bool pad) {
        FakeBackend def; def.setDefaults();
        for (unsigned ck = 0; ck < 40; ck++) {
            g_b.setClassBind(ck, pad, 0, 0); g_b.setClassBind(ck, pad, 0, 0);
            if (unsigned v0 = def.getClassBind(ck, pad, 0)) g_b.setClassBind(ck, pad, 0, v0);
            if (unsigned v1 = def.getClassBind(ck, pad, 1)) g_b.setClassBind(ck, pad, 1, v1);
        }
    };
    a.countBinds = [](unsigned vk, bool pad, const unsigned *acts, int n) { return g_b.countBinds(vk, pad, acts, n); };
    a.keyDisplayName = fakeKeyName;
    a.getLastKey = []() { return g_b.lastKey; };
    a.clearLastKey = []() { g_b.lastKey = 0; };
    a.setCapture = [](bool on) { g_b.capture = on; };
    a.getMouseSpeed = [](float *x, float *y) { *x = g_b.mouseX; *y = g_b.mouseY; };
    a.setMouseSpeed = [](float x, float y) { g_b.mouseX = x; g_b.mouseY = y; };
    a.getRawInput = []() { return g_b.rawInput; };
    a.setRawInput = [](int on) { g_b.rawInput = on; };
    a.getMouseLockMode = []() { return g_b.mouseLockMode; };
    a.setMouseLockMode = [](int m) { g_b.mouseLockMode = m; };
    a.getSwapSticks = []() { return g_b.swapSticks; };
    a.setSwapSticks = [](int v) { g_b.swapSticks = v; };
    a.getMoveDeadzone = []() { return g_b.moveDz; };
    a.setMoveDeadzone = [](float d) { g_b.moveDz = d; };
    a.getMoveScale = []() { return g_b.moveSc; };
    a.setMoveScale = [](float s) { g_b.moveSc = s; };
    a.getLookSpeed = []() { return g_b.lookSpd; };
    a.getLookDeadzone = []() { return g_b.lookDz; };
    a.getLookInverted = []() { return g_b.lookInv; };
    a.setLook = [](float s, float d, int inv) { g_b.lookSpd = s; g_b.lookDz = d; g_b.lookInv = inv; };
    a.getPadVibration = []() { return g_b.padVib; };
    a.setPadVibration = [](float v) { g_b.padVib = v; };
    a.getTriggerFeedback = []() { return g_b.trigFb; };
    a.setTriggerFeedback = [](int p) { g_b.trigFb = p; };
    a.save = []() { g_b.saveCount++; };
    return a;
}

static void checkLayout(const char *label, float winW, float winH, float scale, float boxW, float boxH, bool separateXY) {
    ImGui::CreateContext();
    auto &io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.DisplaySize = ImVec2(winW, winH);
    io.DeltaTime = 1.f / 60.f;
    io.FontGlobalScale = scale;
    ImGui::StyleColorsDark();
    ImGui::GetStyle().ScaleAllSizes(scale);
    ImGui::GetStyle().WindowRounding = 0.0f;
    unsigned char *px; int tw, th;
    io.Fonts->GetTexDataAsRGBA32(&px, &tw, &th);

    const GevrKeybindApi api = makeApi();
    GevrKeybindUiState ui;
    ui.separateXY = separateXY;
    ui.separateInit = true;

    for (int page = 0; page < 2; page++) {
        ImGuiWindow *box = nullptr;
        for (int pass = 0; pass < 3; pass++) {
            ImGui::NewFrame();
            ImGui::SetNextWindowPos(ImVec2(0, 0));
            ImGui::SetNextWindowSize(ImVec2(winW, winH));
            ImGui::Begin("##host", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoScrollbar);
            ImGui::BeginChild("##kb-box", ImVec2(boxW, boxH), false, ImGuiWindowFlags_NoScrollbar);
            if (page == 0) gevrDrawKbmPage(api, ui, 0);
            else gevrDrawGamepadPage(api, ui, 0);
            ImGui::EndChild();
            ImGui::End();
            ImGui::Render();
        }
        for (ImGuiWindow *w : ImGui::GetCurrentContext()->Windows) {
            if (strstr(w->Name, "##kb-box") && w->WasActive) box = w;
        }
        char msg[220];
        std::snprintf(msg, sizeof(msg), "%s (%s%s): pane exists", label, page == 0 ? "KBM" : "Gamepad",
                      separateXY ? ", sep X/Y" : "");
        check(box != nullptr, msg);
        std::snprintf(msg, sizeof(msg), "%s (%s%s): fits vertically without scrolling (content %.0f, box %.0f)",
                      label, page == 0 ? "KBM" : "Gamepad", separateXY ? ", sep X/Y" : "",
                      box->ContentSize.y, box->Size.y);
        check(box->ContentSize.y <= box->Size.y + 1.0f && box->ScrollMax.y <= 0.5f, msg);
        std::snprintf(msg, sizeof(msg), "%s (%s%s): fits horizontally without clipping (content %.0f, box %.0f)",
                      label, page == 0 ? "KBM" : "Gamepad", separateXY ? ", sep X/Y" : "",
                      box->ContentSize.x + box->WindowPadding.x * 2.0f, box->Size.x);
        check(box->ContentSize.x + box->WindowPadding.x * 2.0f <= box->Size.x + 1.0f, msg);
    }

    ImGui::DestroyContext();
}

} // namespace

int main() {
    g_b.setDefaults();
    const GevrKeybindApi api = makeApi();
    GevrKeybindUiState ui;

    // 1. Defaults: mouse wheel up -> Previous weapon, wheel down -> Next weapon
    check(api.getBind(GEVR_CK_PREV_WEAPON, false, 0) == GEVR_VK_MOUSE_WHEEL_UP, "Previous weapon defaults to Wheel Up");
    check(api.getBind(GEVR_CK_A, false, 0) == 20 && api.getBind(GEVR_CK_A, false, 1) == GEVR_VK_MOUSE_WHEEL_DN,
          "Next weapon defaults to Q and Wheel Down");
    check(api.getBind(GEVR_CK_A, true, 0) == GEVR_VK_JOY_BEGIN + 0, "Gamepad A stays intact alongside K&M binds");
    check(api.getBind(GEVR_CK_RESTART, false, 0) == GEVR_VK_BACKSPACE, "Restart (#280) defaults to Backspace");
    check(api.getBind(GEVR_CK_RESTART, true, 0) == 0, "Restart has no pad default");
    check(std::strcmp(gevrActionLabel(0, GEVR_CK_RESTART), "Restart") == 0, "Restart label");

    // 2. Bind capture: key, mouse button, wheel, Esc cancel, Del/Backspace clear
    ui.capturing = true; ui.captureGamepad = false; ui.captureAction = GEVR_CK_PREV_WEAPON; ui.captureSlot = 1;
    api.setCapture(true);
    g_b.lastKey = GEVR_VK_ESCAPE;
    check(gevrKeybindPollCapture(api, ui), "Esc ends capture");
    check(!ui.capturing && !g_b.capture, "Esc clears capture flags");
    check(api.getBind(GEVR_CK_PREV_WEAPON, false, 1) == 0, "Esc leaves slot unchanged");

    // Capture a keyboard key into secondary slot of Previous weapon
    ui.capturing = true; ui.captureGamepad = false; ui.captureAction = GEVR_CK_PREV_WEAPON; ui.captureSlot = 1;
    api.setCapture(true);
    g_b.lastKey = 23; // T
    int savesBefore = g_b.saveCount;
    check(gevrKeybindPollCapture(api, ui), "key press ends capture");
    check(api.getBind(GEVR_CK_PREV_WEAPON, false, 1) == 23, "bound key stored in secondary slot");
    check(g_b.saveCount == savesBefore + 1, "bind change triggers save");

    // Capture Delete clears the slot
    ui.capturing = true; ui.captureGamepad = false; ui.captureAction = GEVR_CK_PREV_WEAPON; ui.captureSlot = 1;
    g_b.lastKey = GEVR_VK_DELETE;
    check(gevrKeybindPollCapture(api, ui), "Delete ends capture");
    check(api.getBind(GEVR_CK_PREV_WEAPON, false, 1) == 0, "Delete clears bound slot");

    // Wrong device class does not clobber K&M bind
    ui.capturing = true; ui.captureGamepad = false; ui.captureAction = GEVR_CK_PREV_WEAPON; ui.captureSlot = 1;
    g_b.lastKey = GEVR_VK_JOY_BEGIN + 1;
    check(!gevrKeybindPollCapture(api, ui), "pad button ignored while capturing K&M slot");
    check(ui.capturing, "still capturing after wrong device class");
    gevrKeybindCancelCapture(api, ui);

    // 3. Duplicate detection across actions
    int kbmCount = 0;
    const unsigned *kbmActs = gevrKbmActions(kbmCount);
    check(api.countBinds(20 /* Q */, false, kbmActs, kbmCount) == 1, "Q initially bound once");
    api.setBind(GEVR_CK_PREV_WEAPON, false, 1, 20 /* Q */);
    check(api.countBinds(20 /* Q */, false, kbmActs, kbmCount) == 2, "duplicate Q detected across two actions");
    api.setBind(GEVR_CK_PREV_WEAPON, false, 1, 0);
    check(api.countBinds(20 /* Q */, false, kbmActs, kbmCount) == 1, "clearing duplicate drops count back to 1");
    // Restart sits outside the table (on the mouse row): its duplicates count across every bind
    int allCount = 0;
    const unsigned *allActs = gevrKbmBindActions(allCount);
    check(allCount == kbmCount + 1 && allActs[allCount - 1] == GEVR_CK_RESTART, "every K&M bind: the table and Restart");
    api.setBind(GEVR_CK_RESTART, false, 0, 20 /* Q */);
    check(api.countBinds(20 /* Q */, false, allActs, allCount) == 2, "Restart on Q is a duplicate of Next weapon");
    api.setBind(GEVR_CK_RESTART, false, 0, GEVR_VK_BACKSPACE);
    check(api.getBind(GEVR_CK_RESTART, false, 0) == GEVR_VK_BACKSPACE, "Restart back on Backspace");

    // 4. Save / load round-trip + empty config preserves defaults
    api.setBind(GEVR_CK_ZTRIG, false, 1, 15 /* L */);
    const auto saved = g_b.saveStrings();
    FakeBackend restored;
    restored.setDefaults();
    restored.loadStrings(saved);
    check(restored.getClassBind(GEVR_CK_ZTRIG, false, 1) == 15, "saved custom bind round-trips");
    check(restored.getClassBind(GEVR_CK_PREV_WEAPON, false, 0) == GEVR_VK_MOUSE_WHEEL_UP, "saved wheel-up round-trips");
    check(restored.getClassBind(GEVR_CK_A, true, 0) == GEVR_VK_JOY_BEGIN + 0, "saved gamepad binds round-trip");

    // Empty config entry keeps defaults (e.g. upgrading older goldeneye.ini without PREV_WEAPON)
    std::map<unsigned, std::string> emptyCfg;
    emptyCfg[GEVR_CK_PREV_WEAPON] = "";
    FakeBackend upgraded;
    upgraded.setDefaults();
    upgraded.loadStrings(emptyCfg);
    check(upgraded.getClassBind(GEVR_CK_PREV_WEAPON, false, 0) == GEVR_VK_MOUSE_WHEEL_UP,
          "empty config preserves Wheel Up default for Previous weapon");

    // 5. Campaign-specific action labels across all 5 campaigns
    check(std::strcmp(gevrActionLabel(0, GEVR_CK_C_L), "Strafe left") == 0, "GE strafe left label");
    check(std::strcmp(gevrActionLabel(3, GEVR_CK_C_L), "Strafe right") == 0, "Mirror Mode flips strafe label");
    for (int camp = 0; camp < 5; camp++) {
        for (int i = 0; i < kbmCount; i++) {
            const char *lbl = gevrActionLabel(camp, kbmActs[i]);
            check(lbl && lbl[0] && std::strncmp(lbl, "CK_", 3) != 0, "all action labels are human game terms");
        }
    }

    // 6. Multi-size layout verification: PC window, VR panel, and Pause overlay (no scroll, no clipping)
    g_b.setDefaults();
    checkLayout("PC window (1280x720)",     1280.0f, 720.0f, 1.5f, 1244.0f, 540.0f, false);
    checkLayout("PC window (1280x720)",     1280.0f, 720.0f, 1.5f, 1244.0f, 540.0f, true);
    checkLayout("VR panel (1280x960)",      1280.0f, 960.0f, 2.2f, 1244.0f, 670.0f, false);
    checkLayout("VR panel (1280x960)",      1280.0f, 960.0f, 2.2f, 1244.0f, 670.0f, true);
    checkLayout("Pause overlay (1244x560)", 1280.0f, 960.0f, 2.2f, 1244.0f, 560.0f, false);
    checkLayout("Pause overlay (1244x560)", 1280.0f, 960.0f, 2.2f, 1244.0f, 560.0f, true);
    checkLayout("Pause Controls sub-tab (1244x510)", 1280.0f, 960.0f, 2.2f, 1244.0f, 510.0f, false);
    checkLayout("Pause Controls sub-tab (1244x510)", 1280.0f, 960.0f, 2.2f, 1244.0f, 510.0f, true);

    std::printf("PASS: PC keybinds (capture, Esc cancel, Del clear, duplicates, wheel up/down defaults, "
                "save/load round-trip, 5 campaign labels, and no-scroll layout at PC/VR/pause sizes)\n");
    return 0;
}
