#include "gevr_launcher_ui.h"
#include "imgui/imgui_internal.h"
#include <cassert>
#include <cstdio>
#include <cstring>

int VrAlly, VrAllyWeapon, VrAllyAggression=1, VrAllyHealth=100, VrAllyDamage=100, VrAllyInvincible;
int VrCampaign, VrMinesStickToGuards, VrBodiesStay, VrRagdolls, VrDismemberment, VrDismemberChance;
int VrFastReinforcements;
float VrDismemberDamage=1;
static bool online, host=true;
static int saves, configChanges;
static bool rulesPage=true, allyPage;
static ImVec2 allyButton, openButton, doneButton;
static bool netModePve(int mode) { return mode==1 || mode==2; }
enum { NET_MODE_COOP=1, NET_COOP_FAST_REINFORCEMENTS=1, CFG_FAST_REINFORCEMENTS=1 };
static struct { int mode=NET_MODE_COOP, fun_flags=0; } config;
static bool netIsActive() { return online; }
static bool netIsHost() { return host; }
static auto netGetMatchConfig() { return &config; }
static void gevrNetConfigSet(int, int) { configChanges++; }
static void vrSettingsSave() { saves++; }

static ImVec2 centre() { return ImRect(ImGui::GetItemRectMin(),ImGui::GetItemRectMax()).GetCenter(); }
static bool allyCheckbox(const char *label, bool *value) {
    bool changed=ImGui::Checkbox(label,value); allyButton=centre(); return changed;
}
static bool openAlly(const char *label) { bool clicked=ImGui::Button(label); openButton=centre(); return clicked; }
static bool done(const char *label, ImVec2 size) { bool clicked=ImGui::Button(label,size); doneButton=centre(); return clicked; }
/* INSERT_PAGES */
/* INSERT_SETTINGS */

static void settingsRoundTrip() {
    VrAlly=VrAllyInvincible=1; VrAllyWeapon=7; VrAllyAggression=2;
    VrAllyHealth=333; VrAllyDamage=250;
    FILE *f=tmpfile(); assert(f); saveAlly(f); rewind(f);
    VrAlly=VrAllyInvincible=VrAllyWeapon=VrAllyAggression=VrAllyHealth=VrAllyDamage=0;
    char line[256]; while (fgets(line,sizeof(line),f)) loadAlly(line); fclose(f);
    assert(VrAlly==1 && VrAllyInvincible==1 && VrAllyWeapon==7 && VrAllyAggression==2
           && VrAllyHealth==333 && VrAllyDamage==250);
    loadAlly("AllyWeapon=-1"); loadAlly("AllyAggression=99");
    loadAlly("AllyHealth=99999"); loadAlly("AllyDamage=-50");
    assert(VrAllyWeapon==0 && VrAllyAggression==2 && VrAllyHealth==1000 && VrAllyDamage==25);
    VrAlly=VrAllyInvincible=VrAllyWeapon=0; VrAllyAggression=1; VrAllyHealth=VrAllyDamage=100;
}

/* The launcher as drawn: header, ROM row, the page, the footer hint. Neither
 * page may scroll or run past the window (no launcher page scrolls). */
static void frame() {
    auto &io=ImGui::GetIO();
    ImGui::NewFrame();
    ImGui::SetNextWindowPos(ImVec2(0,0)); ImGui::SetNextWindowSize(io.DisplaySize);
    ImGui::Begin("Launcher",nullptr,ImGuiWindowFlags_NoTitleBar|ImGuiWindowFlags_NoSavedSettings);
    ImGui::Dummy(ImVec2(0,ImGui::GetTextLineHeight()*2.2f+ImGui::GetStyle().ItemSpacing.y));
    ImGui::Separator(); ImGui::Button("ROM: ok",ImVec2(-1,0)); ImGui::Separator();
    if (allyPage) drawAlly(); else if (rulesPage) drawRules();
    ImGui::TextDisabled("Point and pull the trigger, or use the stick and A.");
    auto *win=ImGui::GetCurrentWindow();
    if (win->DC.CursorMaxPos.y>win->ClipRect.Max.y || win->DC.CursorMaxPos.x>win->ClipRect.Max.x) {
        std::fprintf(stderr,"%s page overflow %.1f %.1f, limit %.1f %.1f\n",allyPage?"ally":"rules",
                     win->DC.CursorMaxPos.x,win->DC.CursorMaxPos.y,win->ClipRect.Max.x,win->ClipRect.Max.y);
        assert(!"page fits without scrolling");
    }
    /* nor scroll inside: v0.5.4 put the rules in a scrolling child that passed the check above */
    for (ImGuiWindow *w : ImGui::GetCurrentContext()->Windows)
        if (w->WasActive && w->ScrollMax.y > 0.5f) {
            std::fprintf(stderr,"%s scrolls by %.1f\n",w->Name,w->ScrollMax.y);
            assert(!"no part of the page scrolls");
        }
    ImGui::End(); ImGui::Render();
}
static void click(ImVec2 p) {
    auto &io=ImGui::GetIO();
    io.AddMousePosEvent(p.x,p.y); frame();
    io.AddMouseButtonEvent(0,true); frame();
    io.AddMouseButtonEvent(0,false); frame(); frame();
}
int main() {
    settingsRoundTrip();
    ImGui::CreateContext(); auto &io=ImGui::GetIO();
    io.DisplaySize=ImVec2(1280,960); io.DeltaTime=1.0f/72; io.FontGlobalScale=2.2f;
    ImGui::GetStyle().ScaleAllSizes(2.2f); io.IniFilename=io.LogFilename=nullptr;
    unsigned char *pixels; int w,h; io.Fonts->GetTexDataAsRGBA32(&pixels,&w,&h);
    /* Game rules, every ragdoll/body choice: one row opens the ally's page */
    for (int r=0;r<2;r++) { VrRagdolls=VrDismemberment=r; VrBodiesStay=48; frame(); frame(); frame(); }
    click(openButton); assert(allyPage && rulesPage);
    /* the ally's page, every state, fits too */
    for (int a=0;a<2;a++) for (int i=0;i<2;i++) { VrAlly=a; VrAllyInvincible=i; frame(); frame(); }
    VrAlly=VrAllyInvincible=0; saves=0;
    click(allyButton); assert(VrAlly==1 && saves==1);
    online=true; host=false; click(allyButton); assert(VrAlly==1 && saves==1);
    host=true; config.mode=0; click(allyButton); assert(VrAlly==1 && saves==1);
    config.mode=NET_MODE_COOP; click(allyButton); assert(VrAlly==0 && saves==2);
    click(allyButton); assert(VrAlly==1 && saves==3);
    online=false; VrCampaign=1; click(allyButton); assert(VrAlly==1 && saves==3);
    VrCampaign=0;
    /* Done goes back to Game rules, whose button says the ally is on */
    click(doneButton); assert(!allyPage && rulesPage && saves==4 && configChanges==0);
    click(openButton); assert(allyPage);
    click(doneButton); assert(!allyPage && rulesPage);
    click(doneButton); assert(!rulesPage);
    ImGui::DestroyContext();
    puts("PASS: companion settings round trip/bounds; its own page opens from Game rules, saves and disables, and both pages fit without scrolling at launcher font scale");
}
