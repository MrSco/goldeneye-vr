#include "gevr_launcher_ui.h"
#include "imgui/imgui_internal.h"
#include "net_match.h"
#include "net_rules.h"
#include <cstdlib>
#include <cstring>
#include <vector>
static ImVec4 gold(1, .84f, .47f, 1);
static int VrMpScenario, VrMpLength, VrMpHealth=5, VrMpDual, VrMpLoadouts, VrMpNextRound;
static int VrAlly, VrHordeFriendlyFire, VrHordeStage = 24, VrHordeStartWave = 1;
static int VrMpMode, VrMpBotMode, VrMpBotCount=7, VrMpBotDifficulty=5, VrMpFriendlyFire;
static int VrMpStage=34,VrMpMaxPlayers=8,VrMpVisibility=1,VrMpWeaponSet=4,VrMpCustom[4];
static int VrMovementSpeed, VrMpMovementSpeed, VrSmoothTurnSpeed=120;
static int VrNoKnockback=1, VrNoHitstun=1, VrDamageFlash=1, VrCoopFastReinforcements;
static unsigned long long VrMpFavStages;
static unsigned VrMpFavSets;
static float VrUseSnapTurn, VrComfortVignette=.5f, vignette=.5f;
static bool vignetteOn=true;
static int turn, online, host;
enum {SCENARIO_YOLT=1, SCENARIO_TLD=2, SCENARIO_MWTGG=3, CFG_MOVEMENT_SPEED};
#define GEVR_MAX_PLAYERS 8
#define GEVR_MAX_NAME_LEN 25
#define GEVR_NET_VERSION 25
#define NET_COOP_MAX_PLAYERS 4
#define SMOOTHTURN_MIN 45
#define SMOOTHTURN_MAX 240
#define SMOOTHTURN_STEP 15
struct FixtureConfig {int movement_speed,fun_flags,mode,scenario,bot_difficulty;};
struct NetMsgLobbyState {
    FixtureConfig config;
    struct {int connected,is_bot,chr_id,team,ready;char name[GEVR_MAX_NAME_LEN];} slots[8];
};
static NetMsgLobbyState lobby;
static FixtureConfig &config=lobby.config;
static const NetMsgLobbyState *netGetLobbyState(){return &lobby;}
static int netGetConnectedPlayerCount(){return netModePve(VrMpMode)?4:8;}
static int netGetMaxPlayers(){return netGetConnectedPlayerCount();}
static int netGetHostSlot(){return 0;}
static const char *netGetSlotAppVersion(int){return "0.4.16";}
static int netHostCanKickPlayer(int slot){return host && slot>0;}
static const char *netGetSlotName(int slot){return lobby.slots[slot].name;}
static void netHostKickPlayer(int){}
static bool gevrLobbyHasTeams(const FixtureConfig *c){return c->mode!=NET_MODE_COOP && netScenarioHasTeams(c->scenario);}
static bool netTeamRosterReady(){return false;}
static int netLobbyMinPlayers(){return 2;}
static std::string gevrPingText(int){return "999";}
static bool netIsActive(){return online;}
static bool netIsHost(){return host;}
static bool gevrNetBotRowsEditable(){return !online || host;}
static auto netGetMatchConfig(){return &config;}
static void vrSettingsSave(){}
static void gevrHostChoiceChanged(){}
static void gevrNetConfigSet(int,int value){if(host)config.movement_speed=VrMpMovementSpeed=value;}
static int netGetHostEqualization(unsigned *cap){*cap=50;return 1;}
static void netSetHostEqualization(bool,unsigned){}
static void netHostEqualizationText(char *out,size_t n){std::snprintf(out,n,"Host delay: 50 ms");}
template <typename Mask> static bool favoriteRow(const char*,int,const char*(*)(int),Mask*,int){return false;}
static const char* gevrBotCountName(int n){static const char*names[]={"1","2","3","4","5","6","7"};return names[n];}
/* the host's stage chooser as drawn: every stage's name must fit it (user: "Caverns" was cut off) */
static float stageComboWidth=-1;
static bool namedCombo(const char*id,int count,const char*(*name)(int),int*value){
    const bool changed=gevrNamedCombo(id,count,name,value,false,[](int){return false;});
    if(std::strcmp(id,"##stagecombo")==0)stageComboWidth=ImGui::GetItemRectSize().x;
    return changed;
}
static bool gunCombo(const char*id,int*value){return namedCombo(id,netItemCount(),[](int i){return netItem(i)->name;},value);}
static void gevrTeamChoiceRow(const char*) {
    if(!gevrLobbyHasTeams(&config))return;
    ImGui::TextUnformatted("Your team:");ImGui::SameLine();int team=0;ImGui::SetNextItemWidth(ImGui::GetFontSize()*5);
    namedCombo("##fixtureTeam",3,netTeamName,&team);
}
static void gevrMatchLiveOptions();
/* the Screen tab: the virtual screen, the display rate, SpaceWarp, anti-aliasing and resolution */
static ImVec4 bad(1, .4f, .4f, 1);
static int VrScreenCurved, VrScreenPassthrough, VrSpaceWarp, VrRefreshRate=120, VrEyeMsaa=1;
static float VrScreenFov=60, VrScreenDistance=3;
static bool beanOn;
#define VR_SCREEN_DISTANCE_MIN 1.0f
#define VR_SCREEN_DISTANCE_MAX 10.0f
#define VR_SCREEN_FOV_MIN 25.0f
#define VR_SCREEN_FOV_MAX 120.0f
static int vr_screen_curve_supported(){return 1;}
static int vr_passthrough_supported(){return 1;}
static int vr_spacewarp_supported(){return 1;}
static int vr_spacewarp_setting_on(){return VrSpaceWarp>=0?VrSpaceWarp!=0:beanOn;}
static int vr_eye_msaa_supported(){return 1;}
static void vr_screen_resize(float,float){}
static bool gevrBeanIsEnabled(){return beanOn;}
static int vr_get_supported_refresh_rates(int *rates,int capacity){
    static const int all[]={72,80,90,120};
    if(rates)for(int i=0;i<4&&i<capacity;i++)rates[i]=all[i];
    return 4;
}
/* INSERT_OPTIONS */
static void comfort()
/* INSERT_COMFORT */
static void coop()
/* INSERT_COOP */
static void screen()
/* INSERT_SCREEN */
static void check(bool yes,const char *msg){if(!yes){std::fprintf(stderr,"FAIL: %s\n",msg);std::exit(1);}}
static void layout(int page) {
    for(int pass=0;pass<3;pass++) {
        ImGui::NewFrame();
        ImGui::SetNextWindowPos(ImVec2(0,0));ImGui::SetNextWindowSize(ImVec2(1280,960));
        ImGui::Begin("Launcher",nullptr,ImGuiWindowFlags_NoTitleBar|ImGuiWindowFlags_NoSavedSettings);
        ImGui::Dummy(ImVec2(0,ImGui::GetTextLineHeight()*2.2f+ImGui::GetStyle().ItemSpacing.y));
        ImGui::Separator();ImGui::Button("ROM: ok",ImVec2(-1,0));ImGui::Separator();
        if(page==0 || page==3) {ImGui::TextUnformatted("ONLINE MULTIPLAYER");}
        gevrLauncherBeginBody("body");
        if(page!=1) {ImGui::Dummy(ImVec2(0,ImGui::GetFrameHeight()*(page==4?1:2)));}
        config.mode=VrMpMode;config.scenario=VrMpScenario;
        for(int i=0;i<8;i++) {
            lobby.slots[i].connected=i<netGetMaxPlayers();lobby.slots[i].is_bot=i%2;
            lobby.slots[i].chr_id=i;std::snprintf(lobby.slots[i].name,GEVR_MAX_NAME_LEN,"abcdefghijklmnopqrstuvwx");
        }
        stageComboWidth=-1;
        if(page==0)gevrMatchOptions();else if(page==1)comfort();else if(page==2)coop();else if(page==4)screen();
        else gevrHostLobbyOptions(gold,true,"TBGKYV43");
        if(pass==2) {
            auto *w=ImGui::GetCurrentWindow();
            if(w->DC.CursorMaxPos.y>w->ClipRect.Max.y) std::fprintf(stderr,"page=%d scenario=%d bots=%d content=%f limit=%f\n",page,VrMpScenario,VrMpBotMode,w->DC.CursorMaxPos.y,w->ClipRect.Max.y);
            check(w->DC.CursorMaxPos.y<=w->ClipRect.Max.y,"production tab fits vertically without scrolling");
            check(w->DC.CursorMaxPos.x<=w->ClipRect.Max.x,"production tab fits horizontally");
            if(page==3 && !netModePve(VrMpMode)) {
                check(stageComboWidth>0,"the host's stage chooser is drawn");
                for(int i=0;i<netStageCount();i++) {
                    const float need=ImGui::CalcTextSize(netStageName(i)).x+ImGui::GetFrameHeight()
                        +ImGui::GetStyle().FramePadding.x*2.0f;
                    if(stageComboWidth<need) std::fprintf(stderr,"stage %s needs %.0f, chooser %.0f (scenario %d)\n",
                                                          netStageName(i),need,stageComboWidth,VrMpScenario);
                    check(stageComboWidth>=need,"every stage's name fits the host's stage chooser");
                }
            }
        }
        ImGui::EndChild();ImGui::End();ImGui::Render();
    }
}
static ImVec2 resetButton;
static void speedFrame(bool hostPage) {
    ImGui::NewFrame();ImGui::SetNextWindowPos(ImVec2(0,0));ImGui::SetNextWindowSize(ImVec2(1280,960));
    ImGui::Begin("Reset test",nullptr,ImGuiWindowFlags_NoTitleBar|ImGuiWindowFlags_NoSavedSettings);
    gevrMovementSpeedOptions(hostPage);
    resetButton=ImRect(ImGui::GetItemRectMin(),ImGui::GetItemRectMax()).GetCenter();
    ImGui::End();ImGui::Render();
}
static void resetCheck(bool hostPage) {
    VrMovementSpeed=NET_MOVE_50;VrMpMovementSpeed=NET_MOVE_200;config.movement_speed=NET_MOVE_175;
    speedFrame(hostPage);speedFrame(hostPage);
    auto &io=ImGui::GetIO();io.AddMousePosEvent(resetButton.x,resetButton.y);speedFrame(hostPage);
    io.AddMouseButtonEvent(0,true);speedFrame(hostPage);io.AddMouseButtonEvent(0,false);speedFrame(hostPage);
    if(online) {
        check(config.movement_speed==(host?NET_MOVE_NORMAL:NET_MOVE_175),"only host can reset live multiplayer speed");
        check(VrMovementSpeed==NET_MOVE_50,"multiplayer reset preserves solo speed");
    } else {
        check(VrMovementSpeed==(hostPage?NET_MOVE_50:NET_MOVE_NORMAL),"solo reset restores 100% independently");
        check(VrMpMovementSpeed==(hostPage?NET_MOVE_NORMAL:NET_MOVE_200),"host preference reset restores 100% independently");
    }
    io.AddMousePosEvent(-1000,-1000);speedFrame(hostPage);
}
int main() {
    ImGui::CreateContext();auto &io=ImGui::GetIO();io.DisplaySize=ImVec2(1280,960);
    io.DeltaTime=1.f/72;io.IniFilename=nullptr;io.LogFilename=nullptr;
    io.FontGlobalScale=2.2f;ImGui::GetStyle().ScaleAllSizes(2.2f);
    unsigned char *pixels;int w,h;io.Fonts->GetTexDataAsRGBA32(&pixels,&w,&h);
    for(online=0;online<2;online++)for(host=0;host<2;host++) {
        VrMpMode=0;
        for(VrMpScenario=0;VrMpScenario<10;VrMpScenario++)
            for(VrMpBotMode=0;VrMpBotMode<3;VrMpBotMode++)layout(0);
        for(turn=0;turn<4;turn++)layout(1);
        VrMpMode=NET_MODE_COOP;layout(2);
        VrMpMode=NET_MODE_HORDE;layout(2);
        resetCheck(false);resetCheck(true);
    }
    online=host=1;
    for(VrMpMode=0;VrMpMode<3;VrMpMode++)for(VrMpScenario=0;VrMpScenario<10;VrMpScenario++)
        for(int set: {4,NET_WEAPON_SET_CUSTOM}) {VrMpWeaponSet=set;layout(3);}
    // the Screen tab with every row it can show: Bean's 90 Hz warning,
    // SpaceWarp on and off, each anti-aliasing choice
    for(beanOn=false;;beanOn=true) {
        for(VrSpaceWarp=-1;VrSpaceWarp<2;VrSpaceWarp++)for(int s:{1,2}) {
            VrEyeMsaa=s;layout(4);
        }
        if(beanOn)break;
    }
    // a long tooltip wraps and stays on the screen, wherever it is hovered
    for(float x:{20.0f,640.0f,1260.0f}) {
        io.AddMousePosEvent(x,480);
        for(int pass=0;pass<3;pass++) {
            ImGui::NewFrame();
            gevrTooltip("Supersampling: the game draws each eye at this multiple of the size the headset recommends "
                        "and the headset scales it down, which is sharper. 1.5x draws 2.25 times the pixels, at that "
                        "GPU cost. It multiplies a Quest Games Optimizer or SideQuest resolution. Takes effect the "
                        "next time the game starts.");
            ImGui::EndFrame();
        }
        ImGuiWindow *tip=nullptr;
        for(ImGuiWindow *w:ImGui::GetCurrentContext()->Windows)
            if(w->Flags&ImGuiWindowFlags_Tooltip)tip=w;
        check(tip!=nullptr,"the tooltip is drawn");
        check(tip->Pos.x>=0 && tip->Pos.x+tip->Size.x<=1280,"a long tooltip fits the launcher's width");
        check(tip->Pos.y>=0 && tip->Pos.y+tip->Size.y<=960,"a long tooltip fits the launcher's height");
    }
    ImGui::DestroyContext();
    std::puts("PASS: production Match/co-op/Comfort/Screen and eight-player Lobby fit at Quest font size, including teams, custom weapons and maximum names; solo/host Reset and client restrictions; long tooltips wrap on screen");
}
