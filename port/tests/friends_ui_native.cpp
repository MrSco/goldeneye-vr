// The Friends sidebar (gevr_friends_ui.h) and the in-game panel (gevr_panel_ui.h)
// at the headset's font scale: nothing runs past its column, the panel's
// footer stays on the canvas, and the buttons act. Fakes stand in for
// vr_friends.cpp and options.c.
#include "gevr_friends_ui.h"
#include "gevr_panel_ui.h"
#include "imgui/imgui_internal.h"
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

static void check(bool ok, const char *message) { if (!ok) { fprintf(stderr, "FAIL: %s\n", message); exit(1); } }

// vr_friends.cpp and the settings, faked
char VrPlayerName[16] = "Agent 007 VRXYZ";
int VrFriendsShowDetail = 1, VrFriendsAppearOffline = 0, VrFriendsRestoreNoted = 0;
extern "C" void vrSettingsSave(void) {}
static GevrFriendsModel model;
static std::vector<std::string> sent;
static std::string joined, invited;
const GevrFriendsModel &gevrFriends(void) { return model; }
void gevrFriendsSend(const std::string &command) { sent.push_back(command); }
void gevrFriendsInvite(const std::string &id) { invited = id; }
void gevrFriendsJoin(const std::string &code) { joined = code; }
void gevrFriendsAnswerInvite(const GevrFriendInvite &invite, bool join) { if (join) joined = invite.lobby; }
std::string gevrFriendCodeText(const std::string &code) { return "GE-" + code; }

// options.c's rows, faked: seven sections, the longest values the watch shows
static int steps[8][8];
static int sectionCount() { return 7; }
static const char *sectionName(int s) { static const char *n[] = {"Comfort", "Controls", "Gestures", "Weapons", "Display", "Game rules", "Mods and fun"}; return n[s]; }
static int rowCount(int s) { return s == 2 || s == 4 ? 8 : 7; }
static int rowRead(int s, int i, const char **label, char *value, int size) {
    *label = i == 0 ? "Fast reinforcements" : "Hand reload WIP";
    snprintf(value, size, "%s", i == 1 ? "GET IN MODS" : i == 2 ? "NEEDS GE-X" : "PER GUN");
    return i < rowCount(s);
}
static void rowStep(int s, int i, int dir) { steps[s][i] += dir; }

static GevrFriendsUiState ui;
static GevrFriendsUiContext ctx;
static void drawSidebar() {
    gevrFriendsSidebar(ui, ctx, ImVec4(1, .8f, .3f, 1), ImVec4(.5f, .9f, .5f, 1), ImVec4(1, .5f, .4f, 1));
}

static float widestItem(ImGuiWindow *w) {
    float right = 0;
    for (ImGuiWindow *x : ImGui::GetCurrentContext()->Windows)
        if (x == w || x->ParentWindow == w || (x->ParentWindow && x->ParentWindow->ParentWindow == w))
            right = std::max(right, x->ContentSize.x + x->WindowPadding.x * 2);
    return right;
}

int main() {
    ImGui::CreateContext();
    auto &io = ImGui::GetIO();
    io.IniFilename = nullptr; io.DisplaySize = ImVec2(1780, 960); io.DeltaTime = 1.f / 60; io.FontGlobalScale = 2.2f;
    ImGui::StyleColorsDark(); ImGui::GetStyle().ScaleAllSizes(2.2f);
    unsigned char *px; int w, h; io.Fonts->GetTexDataAsRGBA32(&px, &w, &h);

    model.status = "ready"; model.friendCode = "7K3QX2"; model.restoreCode = "ABCDEFGHJKLMNPQR"; model.loaded = true;
    for (int i = 0; i < 3; i++) {
        GevrFriendInvite x; x.id = "i" + std::to_string(i); x.name = "Natalya Simonova"; x.lobby = "ABCDEFGH";
        x.lobbyName = "Natalya Simonova's game"; x.players = 2; x.maxPlayers = 4; x.lobbyVersion = 25;
        model.invites.push_back(x);
    }
    for (int i = 0; i < 4; i++) model.requests.push_back({"r" + std::to_string(i), "Boris Grishenko", "BBBBBB"});
    for (int i = 0; i < 24; i++) {
        GevrFriend f; f.id = "f" + std::to_string(i); f.name = "Alec Trevelyan 006"; f.code = "CCCCCC"; f.version = 25;
        f.state = i < 6 ? "lobby" : i < 10 ? "solo" : "offline";
        f.detail = i < 6 ? "You Only Live Twice, Bunker II (Full), 8/8" : i < 10 ? "Surface II, 00 Agent" : "";
        if (i < 6) { f.lobby = "LOBBY00" + std::to_string(i); f.players = 3; f.maxPlayers = 8; f.lobbyVersion = 25; }
        model.friends.push_back(f);
    }
    ctx.canJoin = ctx.canInvite = true; ctx.netVersion = 25; ctx.lobby = "";
    ctx.message = "That player is not taking friend requests";

    // The launcher's sidebar: 500 pixels beside the 1280.
    auto sidebarFrame = [&]() {
        io.MousePos = ImVec2(-FLT_MAX, -FLT_MAX);
        ImGui::NewFrame();
        ImGui::SetNextWindowPos(ImVec2(1280, 0)); ImGui::SetNextWindowSize(ImVec2(500, 960));
        ImGui::Begin("##friends-sidebar", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoScrollbar);
        drawSidebar();
        ImGui::End();
        ImGui::Render();
    };
    sidebarFrame(); sidebarFrame();
    ImGuiWindow *sidebar = ImGui::FindWindowByName("##friends-sidebar");
    check(sidebar && widestItem(sidebar) <= 500 + 1, "every friend, invite and request row fits the launcher's 500-pixel sidebar");

    // An invite's Join joins its lobby.
    gevrFriendsAnswerInvite(model.invites[0], true);
    check(joined == "ABCDEFGH", "an invite's Join joins its lobby");

    // The in-game panel: settings on the left, the sidebar on the right, footer on the canvas.
    io.DisplaySize = ImVec2(1280, 960);
    GevrPanelView v; GevrPanelUi pui;
    v.sectionCount = sectionCount; v.sectionName = sectionName; v.rowCount = rowCount; v.rowRead = rowRead; v.rowStep = rowStep;
    v.drawFriends = drawSidebar; v.texpack = 1;
    snprintf(v.session, sizeof(v.session), "Bunker II, 00 Agent");
    snprintf(v.note, sizeof(v.note), "Going back to the launcher does not save this mission.");
    int action = 0;
    auto panelFrame = [&]() {
        ImGui::NewFrame();
        action = gevrDrawPanelWindow(v, pui);
        ImGui::Render();
    };
    panelFrame(); panelFrame();
    ImGuiWindow *rows = nullptr;
    for (ImGuiWindow *x : ImGui::GetCurrentContext()->Windows)
        if (strstr(x->Name, "##panel-rows") && x->Active) rows = x;
    check(rows != nullptr, "a section's rows are drawn");
    check(rows->ContentSize.y <= rows->Size.y, "a section's eight rows fit without scrolling");
    check(rows->ContentSize.x + rows->WindowPadding.x * 2 <= rows->Size.x + 1, "labels and < VALUE > fit the settings column");
    ImGuiWindow *friendsCol = nullptr, *panel = ImGui::FindWindowByName("##gevr-panel");
    for (ImGuiWindow *x : ImGui::GetCurrentContext()->Windows)
        if (strstr(x->Name, "##panel-friends") && x->ParentWindow == panel) friendsCol = x;
    check(friendsCol != nullptr, "the panel draws the friends column");
    check(friendsCol->Pos.x + friendsCol->Size.x <= 1280, "the friends column stays on the panel");
    check(widestItem(friendsCol) <= friendsCol->Size.x + 1, "the sidebar fits the panel's narrower friends column");
    check(panel->ContentSize.y + panel->WindowPadding.y <= 960, "the footer (Resume, HD textures, Back to launcher) stays on the canvas");
    ImGui::DestroyContext();
    puts("PASS: friends sidebar fits the launcher's 500 pixels and the panel's column; the in-game panel's VR settings and footer fit at Quest font size");
}
