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
int VrFriendsShowDetail = 1, VrFriendsAppearOffline = 0, VrFriendsRestoreNoted = 0, VrFriendsSidebar = 1, VrFriendsSearchable = 1;
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
void gevrFriendsSendSettings(void) {}
void gevrFriendsSetName(const char *) {}
static int discordState = GEVR_DISCORD_CONNECTED;
int gevrDiscordState(void) { return discordState; }
const char *gevrDiscordName(void) { return "A very long Discord display name"; }
void gevrDiscordLink(void) {}
void gevrDiscordUnlink(void) {}
static std::vector<GevrDiscordFriend> discordFriends;
static std::vector<GevrDiscordInvite> discordInvites;
const std::vector<GevrDiscordFriend> &gevrDiscordFriends(void) { return discordFriends; }
const std::vector<GevrDiscordInvite> &gevrDiscordInvites(void) { return discordInvites; }
void gevrDiscordInvite(uint64_t) {}
void gevrDiscordAnswer(int, bool) {}

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
static void drawSidePanel() {
    gevrFriendsSidePanel(ui, ctx, ImVec4(1, .8f, .3f, 1), ImVec4(.5f, .9f, .5f, 1), ImVec4(1, .5f, .4f, 1));
}

static float widestItem(ImGuiWindow *w) {
    float right = 0;
    for (ImGuiWindow *x : ImGui::GetCurrentContext()->Windows)
        if (x->Active && (x == w || x->ParentWindow == w || (x->ParentWindow && x->ParentWindow->ParentWindow == w)))
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
    model.searchQuery = "Natalya";
    for (int i = 0; i < 20; i++) model.search.push_back({"s" + std::to_string(i), "Natalya Simonov" + std::to_string(i % 10), "SSSSSS",
                                                         i % 4 == 0 ? "none" : i % 4 == 1 ? "sent" : i % 4 == 2 ? "request" : "friends"});
    model.friends.clear();   // no friends yet: Find a friend opens by itself, and is measured
    for (int i = 0; i < 3; i++) discordInvites.push_back({i + 1, "A Discord display name that is long"});
    for (int i = 0; i < 40; i++) discordFriends.push_back({100u + i, "Discord friend with a long name " + std::to_string(i), i < 10, i == 3});
    ctx.canJoin = ctx.canInvite = true; ctx.netVersion = 25; ctx.lobby = "";
    ctx.message = "That player is not taking friend requests";

    // The side panel, as every surface draws it: open, then folded to a strip.
    auto sideFrame = [&]() {
        io.DisplaySize = ImVec2((float)gevrSurfaceCanvasW(), 960);
        io.MousePos = ImVec2(-FLT_MAX, -FLT_MAX);
        ImGui::NewFrame();
        drawSidePanel();
        ImGui::Render();
    };
    sideFrame(); sideFrame();
    ImGuiWindow *sidebar = ImGui::FindWindowByName("##friends-sidebar");
    check(gevrSurfaceCanvasW() == 1780, "open, the canvas is the 1280 surface plus 500");
    check(sidebar && sidebar->Pos.x == 1280 && sidebar->Size.x == 500, "the side panel sits right of the 1280 surface");
    check(widestItem(sidebar) <= 500 + 1, "every friend, invite and request row fits the 500-pixel side panel");
    VrFriendsSidebar = 0;
    sideFrame(); sideFrame();
    check(gevrSurfaceCanvasW() == 1356 && sidebar->Size.x == 76, "folded, the side panel is a 76-pixel strip");
    check(widestItem(sidebar) <= 76 + 1, "the folded strip's label fits it");
    VrFriendsSidebar = 1;

    // An invite's Join joins its lobby.
    gevrFriendsAnswerInvite(model.invites[0], true);
    check(joined == "ABCDEFGH", "an invite's Join joins its lobby");

    // The in-game panel: the shared header, the VR settings across the surface, the footer.
    io.DisplaySize = ImVec2((float)gevrSurfaceCanvasW(), 960);
    GevrPanelView v; GevrPanelUi pui;
    v.sectionCount = sectionCount; v.sectionName = sectionName; v.rowCount = rowCount; v.rowRead = rowRead; v.rowStep = rowStep;
    v.texpack = 1;
    snprintf(v.session, sizeof(v.session), "Bunker II");
    snprintf(v.status, sizeof(v.status), "00 Agent");
    snprintf(v.note, sizeof(v.note), "Going back to the launcher does not save this mission.");
    int action = 0;
    auto panelFrame = [&]() {
        ImGui::NewFrame();
        action = gevrDrawPanelWindow(v, pui);
        drawSidePanel();
        ImGui::Render();
    };
    panelFrame(); panelFrame();
    ImGuiWindow *rows = nullptr;
    for (ImGuiWindow *x : ImGui::GetCurrentContext()->Windows)
        if (strstr(x->Name, "##panel-rows") && x->Active) rows = x;
    check(rows != nullptr, "a section's rows are drawn");
    check(rows->ContentSize.y <= rows->Size.y, "a section's eight rows fit without scrolling");
    check(rows->ContentSize.x + rows->WindowPadding.x * 2 <= rows->Size.x + 1, "labels and < VALUE > fit the settings column");
    ImGuiWindow *panel = ImGui::FindWindowByName("##gevr-panel");
    ImGuiTabBar *tabs = nullptr;
    for (int t = 0; t < ImGui::GetCurrentContext()->TabBars.GetMapSize(); t++)
        if (ImGuiTabBar *bar = ImGui::GetCurrentContext()->TabBars.TryGetMapData(t)) tabs = bar;
    check(tabs && tabs->Tabs.Size == 7, "the seven VR sections are tabs");
    check(tabs->WidthAllTabsIdeal <= tabs->BarRect.GetWidth() + 1, "all seven section tabs fit without scrolling or shrinking");
    check(ImGui::FindWindowByName("##friends-sidebar")->Pos.x == 1280, "the panel has the same Friends side panel as the launcher");
    check(panel->ContentSize.y + panel->WindowPadding.y <= 960, "the footer (Resume, HD textures, Back to launcher) stays on the canvas");
    ImGui::DestroyContext();
    puts("PASS: the Friends side panel fits open (500) and folded (76) beside the 1280 surface; the in-game panel's tabs, VR settings and footer fit at Quest font size");
}
