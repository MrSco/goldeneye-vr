/* In-game ImGui surface. Reuses the launcher's style, input and XR quad, and
 * its layout: a 1280 x 960 window with the Friends side panel to its right
 * (gevr_friends_ui.h gevrFriendsSidePanel, the launcher's own). Online, one
 * screen: the match's (gevr_pause_widgets.h), on Match for a tap of Menu and
 * on Settings for a hold. Solo, a hold opens the panel (gevr_panel_ui.h),
 * the same sections without the match's tabs; a tap opens GoldenEye's watch. */
#ifdef ANDROID
#include "gevr_pause_widgets.h"
#include "gevr_panel_ui.h"
#include "gevr_friends_ui.h"
#include "imgui/imgui_impl_opengl3.h"
#include "vr_screen.h"
#include "vr_log.h"
#include <GLES3/gl3.h>
#include <SDL.h>
#include <cmath>
#include "net_core.h"
#include "net_game.h"
#include "net_coop.h"
#include "net_horde.h"
extern "C" {
void gevrLobbySessionStopped(void);void gevrRestartToLauncher(void);int bossGetStageNum(void);
int gevrPanelIsOpen(void);                       /* input.c: hold Menu */
int gevrTexpackState(void);int gevrTexpackToggle(void);   /* gfx_pc.cpp */
void audioReset(void);                           /* audio.c: a new output stream (#229) */
extern int gevrTexpackToggleMsg;                 /* input.c: bondview2.c says it in play */
int lvlGetSelectedDifficulty(void);
}
extern int VrCampaign;
// vr_launcher.cpp: the headset keyboard for text boxes, as in the launcher
void gevrTextInputBegin();void gevrTextInputFeed(ImGuiIO&);void gevrTextInputShow(const ImGuiIO&);void gevrTextInputEnd();
namespace {
ImGuiContext* context=nullptr;
GLuint texture=0,framebuffer=0,icon=0;
Uint64 last=0;
bool wasOpen=false,wasPanel=false,wasHold=false;
GevrPauseUi ui;
GevrPanelUi panelUi;
GevrFriendsUiState friendsUi;
// The Friends side panel here: joining leaves the mission (the launcher joins),
// and inviting needs this match's lobby to be open.
void drawFriends() {
    const char*lobby=netIsActive()?netGetLobbyCode():"";
    const bool open=lobby[0]&&netGetLivePlayerCount()<netGetMaxPlayers();
    GevrFriendsUiContext ctx;
    ctx.canJoin=VrCampaign==0;   /* GEVR_CAMPAIGN_GE: online play is GoldenEye's */
    ctx.joinHint="Online play needs the GoldenEye campaign.";
    ctx.canInvite=open;
    ctx.inviteHint=!netIsActive()?"Invite from a lobby: host one on the launcher's Multiplayer page."
        :!lobby[0]?"This game has no internet lobby to invite into.":!open?"This match is full.":"Invite them into this match.";
    ctx.lobby=lobby;ctx.netVersion=GEVR_NET_VERSION;ctx.message=gevrFriendsFreshMessage();
    ctx.canRename=!netIsActive();
    gevrFriendsSidePanel(friendsUi,ctx,ImVec4(.88f,.69f,.25f,1),ImVec4(.5f,.9f,.5f,1),ImVec4(.95f,.5f,.4f,1));
}
// A join asked for in the sidebar: confirmed, it leaves for the launcher, which joins.
bool confirmJoin() {
    if(!gevrFriendsJoinPending())return false;
    if(!ImGui::IsPopupOpen("Join friend?"))ImGui::OpenPopup("Join friend?");
    bool leave=false;
    if(ImGui::BeginPopupModal("Join friend?",nullptr,ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted(netIsActive()?"Leave this match for your friend's lobby?":"Leave this mission for your friend's lobby?");
        if(!netIsActive())ImGui::TextDisabled("This mission will not be saved.");
        if(ImGui::Button("Cancel",ImVec2(220,52))){gevrFriendsTakeJoin();ImGui::CloseCurrentPopup();}
        ImGui::SameLine();
        if(ImGui::Button("Join",ImVec2(220,52))){leave=gevrFriendsSavePendingJoin(gevrFriendsJoinCode());ImGui::CloseCurrentPopup();}
        ImGui::EndPopup();
    }
    return leave;
}
void leaveToLauncher() {
    gevrNativePauseResume();
    if(netIsActive())gevrLobbySessionStopped();   /* the goodbye: the host frees the slot at once */
    gevrRestartToLauncher();
}
void initialize() {
    context=ImGui::CreateContext();auto&io=ImGui::GetIO();io.IniFilename=nullptr;
    io.ConfigFlags|=ImGuiConfigFlags_NavEnableGamepad;io.BackendFlags|=ImGuiBackendFlags_HasGamepad;
    io.DisplaySize=ImVec2((float)gevrSurfaceCanvasW(),(float)kGevrSurfaceH);io.FontGlobalScale=2.2f;
    ImGui::StyleColorsDark();ImGui::GetStyle().ScaleAllSizes(2.2f);ImGui::GetStyle().WindowRounding=0;
    ImGui_ImplOpenGL3_Init("#version 300 es");
    glGenTextures(1,&texture);glBindTexture(GL_TEXTURE_2D,texture);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,kGevrSurfaceW+kGevrSidebarW,kGevrSurfaceH,0,GL_RGBA,GL_UNSIGNED_BYTE,nullptr);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
    glGenFramebuffers(1,&framebuffer);glBindFramebuffer(GL_FRAMEBUFFER,framebuffer);
    glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,texture,0);
    SDL_RWops*rw=SDL_RWFromFile("launcher_icon.rgba","rb");
    if(rw){unsigned char px[128*128*4];if(SDL_RWread(rw,px,1,sizeof(px))==sizeof(px)) {
        glGenTextures(1,&icon);glBindTexture(GL_TEXTURE_2D,icon);glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,128,128,0,GL_RGBA,GL_UNSIGNED_BYTE,px);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
    }SDL_RWclose(rw);}
}
// The in-game panel's frame. Returns a GEVR_PANEL_ action.
int panelFrame() {
    GevrPanelView v;
    v.online=netIsActive();v.texpack=gevrTexpackState();
    v.settings=gevrSettingsLive();   /* the settings table (gevr_settings.c) */
    gevrPanelGauges(v.gauges);   /* the online pause's health and armour, solo too */
    gevrPauseLocalRadar(&v.radar);
    const int stage=bossGetStageNum();
    if(v.online) {
        snprintf(v.session,sizeof(v.session),"%s",netCoopActive()?netCoopStageName((uint8_t)stage):netStageName(netStageIndexOf((uint8_t)netGetLobbyStage())));
        snprintf(v.status,sizeof(v.status),"%s",netCoopActive()?netDifficultyName(netGetMatchConfig()->difficulty):"Match continues while this menu is open");
    } else if(stage==90)snprintf(v.session,sizeof(v.session),"In the menus");   /* LEVELID_TITLE */
    else {
        const char*mission=netCoopStageName((uint8_t)stage);
        if(VrCampaign==0&&mission&&mission[0])snprintf(v.session,sizeof(v.session),"%s",mission);
        snprintf(v.status,sizeof(v.status),"%s",netDifficultyName(lvlGetSelectedDifficulty()));
    }
    snprintf(v.note,sizeof(v.note),"%s",v.online?"Leaving disconnects you from this match.":stage==90?"":"Going back to the launcher does not save this mission.");
    return gevrDrawPanelWindow(v,panelUi,(ImTextureID)(intptr_t)icon);
}
}
extern "C" void gevrNativePauseRender(void) {
    const bool open=gevrNativePauseOpen()!=0;
    vr_screen_set_overlay(open);
    if(!open){
        if(wasOpen){vr_screen_set_width_scale(1.0f);gevrTextInputEnd();}   /* the game's own screen is 4:3 */
        wasOpen=false;return;
    }
    /* hold Menu: solo, the panel; online, the match screen on its Settings tab (one screen) */
    const bool hold=gevrPanelIsOpen()!=0;
    const bool panel=hold&&!netIsActive();
    GLint drawFbo,readFbo,viewport[4],scissor[4],boundTexture;GLfloat clear[4];
    const bool scissorOn=glIsEnabled(GL_SCISSOR_TEST);
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING,&drawFbo);glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING,&readFbo);
    glGetIntegerv(GL_VIEWPORT,viewport);glGetIntegerv(GL_SCISSOR_BOX,scissor);glGetFloatv(GL_COLOR_CLEAR_VALUE,clear);
    glGetIntegerv(GL_TEXTURE_BINDING_2D,&boundTexture);
    ImGuiContext*previous=ImGui::GetCurrentContext();
    if(!context)initialize();else ImGui::SetCurrentContext(context);
    auto&io=ImGui::GetIO();const Uint64 now=SDL_GetPerformanceCounter();
    /* the surface and the Friends side panel, open or folded */
    const int canvasW=gevrSurfaceCanvasW();
    io.DisplaySize=ImVec2((float)canvasW,(float)kGevrSurfaceH);
    vr_screen_set_width_scale((float)canvasW/(float)kGevrSurfaceW);
    io.DeltaTime=last?fmaxf(.001f,fminf(.1f,(float)(now-last)/SDL_GetPerformanceFrequency())):1.f/72;last=now;
    vr_screen_set_visible(1);
    if(!wasOpen || panel!=wasPanel || hold!=wasHold){
        ui=GevrPauseUi();panelUi=GevrPanelUi();if(hold)ui.tab=GEVR_PAUSE_SETTINGS;
        if(!wasOpen){vr_screen_recenter();gevrTextInputBegin();}io.ClearInputKeys();
    }
    gevrTextInputFeed(io);
    gevrFeedPauseInput(!wasOpen || panel!=wasPanel || hold!=wasHold);wasOpen=true;wasPanel=panel;wasHold=hold;
    ImGui_ImplOpenGL3_NewFrame();ImGui::NewFrame();
    const bool popupWasOpen=ImGui::IsPopupOpen(nullptr,ImGuiPopupFlags_AnyPopupId|ImGuiPopupFlags_AnyPopupLevel);
    int action=GEVR_PAUSE_NONE,panelAction=GEVR_PANEL_NONE;
    bool joinFriend=false;
    if(panel) {
        panelAction=panelFrame();
        drawFriends();
        joinFriend=confirmJoin();
        if(!popupWasOpen&&ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight))panelAction=GEVR_PANEL_RESUME;
    } else {
    GevrPauseView view;view.horde=netHordeActive()!=0;view.hordeDefeated=gevrHordeDefeated()!=0;view.coop=netCoopActive()!=0;view.host=netIsHost();view.capacity=view.coop?4:netGetMaxPlayers();
    view.canStart=!view.coop&&gevrPauseActionAvailable("START MATCH");
    view.soloWarmup=!view.coop&&netGetConnectedPlayerCount()==1;
    view.startRequested=netHostStartRequested()!=0;view.rosterReady=netRoundRosterReady()!=0;
    view.voting=gevrNetConfigGet(CFG_NEXT_ROUND)==NET_NEXT_VOTE;view.warmup=netGetPhase()==NET_PHASE_WARMUP;
    view.canReturn=!view.coop&&gevrPauseActionAvailable("RETURN TO LOBBY");view.countdown=netCountdownSecondsLeft();view.localReady=netLocalReady()!=0;
    view.settings=gevrSettingsLive();view.texpack=gevrTexpackState();
    snprintf(view.session,sizeof(view.session),"%s",view.coop?netCoopStageName(bossGetStageNum()):netStageName(netStageIndexOf((uint8_t)netGetLobbyStage())));
    snprintf(view.status,sizeof(view.status),"%s",view.coop?netDifficultyName(netGetMatchConfig()->difficulty):view.countdown>0?"Match is starting":netGetPhase()==NET_PHASE_IN_PROGRESS?"Match continues while this menu is open":"Warmup / next round");
    if(view.horde) {
        snprintf(view.session,sizeof(view.session),"Horde: %s",netHordeStageName((unsigned)bossGetStageNum()));
        gevrHordeText(view.status,sizeof(view.status));
        const HordeState *s=gevrHordeState();
        int me=netGetLocalSlot();
        uint32_t pts=(me>=0 && me<4)?s->credits[me]:s->credits[0];
        snprintf(view.hordeStats,sizeof(view.hordeStats),"Points: %u   Waves: %u   Time: %u:%02u   Kills: %u   Revives: %u",
            pts,s->completed,s->elapsed/3600,(s->elapsed/60)%60,s->kills,s->revives);
    }
    if(!view.coop && !view.horde && netGetPhase()==NET_PHASE_IN_PROGRESS) {
        const int left=gevrPauseMatchClock(view.clock,sizeof(view.clock));
        view.clockEnding=left>=0&&left<=60;
    }
    if(!view.coop && !view.countdown) {
        int seconds=netWarmupSecondsLeft();
        if(view.startRequested)snprintf(view.status,sizeof(view.status),"Waiting for ready / loading players");
        else if(seconds>0)snprintf(view.status,sizeof(view.status),"Warmup %d:%02d - host can start early",seconds/60,seconds%60);
    }
    gevrPauseLocalGauges(view.gauges);
    gevrPauseLocalRadar(&view.radar);
    const NetMsgLobbyState*lobby=netGetLobbyState();
    for(int slot=0;slot<(view.coop?4:8);slot++)if(netLobbySlotConnected(slot)) {
        auto&p=view.players[view.count++];snprintf(p.name,sizeof(p.name),"%s",netGetSlotName(slot)?netGetSlotName(slot):"Player");
        p.slot=slot;p.loaded=lobby->slots[slot].loaded!=0;p.spectator=lobby->slots[slot].spectator!=0;p.canKick=netHostCanKickPlayer(slot)!=0;p.bot=lobby->slots[slot].is_bot!=0;
        snprintf(p.character,sizeof(p.character),"%s",netCharacterName(gevrNetSlotChr(slot)));p.host=slot==netGetHostSlot();
        p.ready=lobby->slots[slot].ready!=0;p.down=view.coop&&gevrCoopDowned(slot);p.ping=netGetSlotPing(slot);
        if(!view.coop)gevrPausePlayerStats(slot,&p.points,&p.kills,&p.losses);
    }
    action=gevrDrawPauseWindow(view,ui,(ImTextureID)(intptr_t)icon);
    drawFriends();
    joinFriend=confirmJoin();
    if(!popupWasOpen&&ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight))action=GEVR_PAUSE_RESUME;
    }
    if(io.MousePos.x>=0&&io.MousePos.y>=0) {
        ImGui::GetForegroundDrawList()->AddCircleFilled(io.MousePos,8,IM_COL32(255,214,120,255));
        ImGui::GetForegroundDrawList()->AddCircle(io.MousePos,11,IM_COL32(255,255,255,255));
    }
    ImGui::Render();gevrTextInputShow(io);glBindFramebuffer(GL_FRAMEBUFFER,framebuffer);glViewport(0,0,canvasW,kGevrSurfaceH);glDisable(GL_SCISSOR_TEST);
    glClearColor(.06f,.06f,.06f,1);glClear(GL_COLOR_BUFFER_BIT);ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    vr_screen_present_tex2d(texture,canvasW,kGevrSurfaceH);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER,(GLuint)drawFbo);glBindFramebuffer(GL_READ_FRAMEBUFFER,(GLuint)readFbo);
    glViewport(viewport[0],viewport[1],viewport[2],viewport[3]);glScissor(scissor[0],scissor[1],scissor[2],scissor[3]);
    if(scissorOn)glEnable(GL_SCISSOR_TEST);else glDisable(GL_SCISSOR_TEST);
    glClearColor(clear[0],clear[1],clear[2],clear[3]);glBindTexture(GL_TEXTURE_2D,(GLuint)boundTexture);
    vr_pointer_draw();ImGui::SetCurrentContext(previous);
    if(joinFriend){leaveToLauncher();return;}
    if(panelAction==GEVR_PANEL_RESUME)gevrNativePauseResume();
    if(panelAction==GEVR_PANEL_LAUNCHER){vr_log("panel: back to the launcher");leaveToLauncher();return;}
    if(panelAction==GEVR_PANEL_RESET_SOUND){audioReset();gevrFriendsMessage("Sound reset");vr_log("panel: sound reset");}
    if(panelAction==GEVR_PANEL_TEXPACK && gevrTexpackState()>=0){
        gevrTexpackToggleMsg=gevrTexpackToggle()+2;   /* 2 off, 3 on */
        vr_log("panel: texture pack %s",gevrTexpackToggleMsg==3?"on":"off");
    }
    if(action==GEVR_PAUSE_RESET_SOUND){audioReset();gevrFriendsMessage("Sound reset");vr_log("pause: sound reset");}
    if(action==GEVR_PAUSE_TEXPACK && gevrTexpackState()>=0){
        gevrTexpackToggleMsg=gevrTexpackToggle()+2;   /* 2 off, 3 on */
        vr_log("pause: texture pack %s",gevrTexpackToggleMsg==3?"on":"off");
    }
    if(action==GEVR_PAUSE_HORDE_RETRY){gevrNativePauseResume();netHordeRetry();}
    if(action==GEVR_PAUSE_RESUME)gevrNativePauseResume();
    if(action==GEVR_PAUSE_LEAVE)leaveToLauncher();
    if(action==GEVR_PAUSE_ABORT&&netIsHost()&&netCoopActive()){gevrNativePauseResume();netCoopMissionEnded(NET_COOP_RESULT_ABORTED);}
    if(action==GEVR_PAUSE_REQUEST_VOTES)netHostRequestVotes();
    if(action>=GEVR_PAUSE_KICK_BASE && action<GEVR_PAUSE_KICK_BASE+8)netHostKickPlayer(action-GEVR_PAUSE_KICK_BASE);
}
#endif
