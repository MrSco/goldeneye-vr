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
static bool rulesPage=true;
static ImVec2 allyButton, doneButton;
static float doneBottom, scrollMax;
enum { NET_MODE_COOP=1, NET_COOP_FAST_REINFORCEMENTS=1, CFG_FAST_REINFORCEMENTS=1 };
static struct { int mode=NET_MODE_COOP, fun_flags=0; } config;
static bool netIsActive() { return online; }
static bool netIsHost() { return host; }
static auto netGetMatchConfig() { return &config; }
static void gevrNetConfigSet(int, int) { configChanges++; }
static void vrSettingsSave() { saves++; }

static bool allyCheckbox(const char *label, bool *value) {
    bool changed=ImGui::Checkbox(label,value);
    allyButton=ImRect(ImGui::GetItemRectMin(),ImGui::GetItemRectMax()).GetCenter();
    return changed;
}
static bool done(const char *label, ImVec2 size) {
    bool clicked=ImGui::Button(label,size);
    doneButton=ImRect(ImGui::GetItemRectMin(),ImGui::GetItemRectMax()).GetCenter();
    doneBottom=ImGui::GetItemRectMax().y;
    return clicked;
}
/* INSERT_RULES */
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

static void frame() {
    ImGui::NewFrame();
    ImGui::SetNextWindowPos(ImVec2(0,0));
    ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);
    ImGui::Begin("Rules",nullptr,ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoSavedSettings
                              | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::Dummy(ImVec2(0,100)); // main launcher header and ROM summary
    drawRules();
    assert(doneBottom <= ImGui::GetIO().DisplaySize.y - gevrLauncherFooterHeight());
    ImGui::End(); ImGui::Render();
}
static void click(ImVec2 p) {
    auto &io=ImGui::GetIO();
    io.AddMousePosEvent(p.x,p.y); frame();
    io.AddMouseButtonEvent(0,true); frame();
    io.AddMouseButtonEvent(0,false); frame(); frame();
}
static void setup(int width,int height,float scale) {
    ImGui::CreateContext(); auto &io=ImGui::GetIO();
    io.DisplaySize=ImVec2(width,height); io.DeltaTime=1.0f/60; io.FontGlobalScale=scale;
    io.IniFilename=io.LogFilename=nullptr;
    unsigned char *pixels; int w,h; io.Fonts->GetTexDataAsRGBA32(&pixels,&w,&h);
    frame(); frame(); frame();
}
int main() {
    settingsRoundTrip();
    setup(800,600,2.2f);
    assert(scrollMax>0); // content scrolls while Done remains visible
    click(allyButton); assert(VrAlly==1 && saves==1);
    online=true; click(allyButton); assert(VrAlly==1 && saves==1);
    online=false; VrCampaign=1; click(allyButton); assert(VrAlly==1 && saves==1);
    VrCampaign=0; click(doneButton); assert(!rulesPage && saves==2 && configChanges==0);
    ImGui::DestroyContext();
    rulesPage=true; setup(1280,720,2.2f); assert(scrollMax>0);
    click(doneButton); assert(!rulesPage);
    ImGui::DestroyContext();
    puts("PASS: companion settings round trip/bounds, controls save/disable, scroll and Done fit at launcher font scale");
}
