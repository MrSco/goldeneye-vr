/* In-match ImGui surface. Reuses the launcher's style, input and XR quad. */
#ifdef ANDROID
#include "gevr_pause_widgets.h"
#include "imgui/imgui_impl_opengl3.h"
#include "vr_screen.h"
#include "vr_log.h"
#include <GLES3/gl3.h>
#include <SDL.h>
#include <cmath>
#include "net_core.h"
#include "net_game.h"
#include "net_coop.h"
extern "C" { void gevrLobbySessionStopped(void);void gevrRestartToLauncher(void);int bossGetStageNum(void); }
namespace {
ImGuiContext* context=nullptr;
GLuint texture=0,framebuffer=0,icon=0;
Uint64 last=0;
bool wasOpen=false;
GevrPauseUi ui;
void initialize() {
    context=ImGui::CreateContext();auto&io=ImGui::GetIO();io.IniFilename=nullptr;
    io.ConfigFlags|=ImGuiConfigFlags_NavEnableGamepad;io.BackendFlags|=ImGuiBackendFlags_HasGamepad;
    io.DisplaySize=ImVec2(1280,960);io.FontGlobalScale=2.2f;
    ImGui::StyleColorsDark();ImGui::GetStyle().ScaleAllSizes(2.2f);ImGui::GetStyle().WindowRounding=0;
    ImGui_ImplOpenGL3_Init("#version 300 es");
    glGenTextures(1,&texture);glBindTexture(GL_TEXTURE_2D,texture);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,1280,960,0,GL_RGBA,GL_UNSIGNED_BYTE,nullptr);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
    glGenFramebuffers(1,&framebuffer);glBindFramebuffer(GL_FRAMEBUFFER,framebuffer);
    glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,texture,0);
    SDL_RWops*rw=SDL_RWFromFile("launcher_icon.rgba","rb");
    if(rw){unsigned char px[128*128*4];if(SDL_RWread(rw,px,1,sizeof(px))==sizeof(px)) {
        glGenTextures(1,&icon);glBindTexture(GL_TEXTURE_2D,icon);glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,128,128,0,GL_RGBA,GL_UNSIGNED_BYTE,px);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
    }SDL_RWclose(rw);}
}
}
extern "C" void gevrNativePauseRender(void) {
    const bool open=gevrNativePauseOpen()!=0;
    vr_screen_set_overlay(open);
    if(!open){wasOpen=false;return;}
    GLint drawFbo,readFbo,viewport[4],scissor[4],boundTexture;GLfloat clear[4];
    const bool scissorOn=glIsEnabled(GL_SCISSOR_TEST);
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING,&drawFbo);glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING,&readFbo);
    glGetIntegerv(GL_VIEWPORT,viewport);glGetIntegerv(GL_SCISSOR_BOX,scissor);glGetFloatv(GL_COLOR_CLEAR_VALUE,clear);
    glGetIntegerv(GL_TEXTURE_BINDING_2D,&boundTexture);
    ImGuiContext*previous=ImGui::GetCurrentContext();
    if(!context)initialize();else ImGui::SetCurrentContext(context);
    auto&io=ImGui::GetIO();const Uint64 now=SDL_GetPerformanceCounter();
    io.DeltaTime=last?fmaxf(.001f,fminf(.1f,(float)(now-last)/SDL_GetPerformanceFrequency())):1.f/72;last=now;
    vr_screen_set_visible(1);
    if(!wasOpen){ui=GevrPauseUi();vr_screen_recenter();io.ClearInputKeys();}
    gevrFeedPauseInput(!wasOpen);wasOpen=true;
    ImGui_ImplOpenGL3_NewFrame();ImGui::NewFrame();
    GevrPauseView view;view.coop=netCoopActive()!=0;view.host=netIsHost();view.capacity=view.coop?4:netGetMaxPlayers();
    view.canStart=!view.coop&&gevrPauseActionAvailable("START MATCH")&&netRoundRosterReady();
    view.canReturn=!view.coop&&gevrPauseActionAvailable("RETURN TO LOBBY");view.countdown=netCountdownSecondsLeft();view.localReady=netLocalReady()!=0;
    snprintf(view.session,sizeof(view.session),"%s",view.coop?netCoopStageName(bossGetStageNum()):netStageName(netStageIndexOf((uint8_t)netGetLobbyStage())));
    snprintf(view.status,sizeof(view.status),"%s",view.coop?netDifficultyName(netGetMatchConfig()->difficulty):view.countdown>0?"Match is starting":netGetPhase()==NET_PHASE_IN_PROGRESS?"Match continues while this menu is open":"Warmup / next round");
    gevrPauseLocalVitals(&view.health,&view.armour);
    const NetMsgLobbyState*lobby=netGetLobbyState();
    for(int slot=0;slot<(view.coop?4:8);slot++)if(netSlotOccupied(slot)) {
        auto&p=view.players[view.count++];snprintf(p.name,sizeof(p.name),"%s",netGetSlotName(slot)?netGetSlotName(slot):"Player");
        snprintf(p.character,sizeof(p.character),"%s",netCharacterName(gevrNetSlotChr(slot)));p.host=slot==netGetHostSlot();
        p.ready=lobby->slots[slot].ready!=0;p.down=view.coop&&gevrCoopDowned(slot);p.ping=netGetSlotPing(slot);
        if(!view.coop)gevrPausePlayerStats(slot,&p.points,&p.kills,&p.losses);
    }
    const bool popupWasOpen=ImGui::IsPopupOpen(nullptr,ImGuiPopupFlags_AnyPopupId|ImGuiPopupFlags_AnyPopupLevel);
    int action=gevrDrawPauseWindow(view,ui,(ImTextureID)(intptr_t)icon);
    if(!popupWasOpen&&ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight))action=GEVR_PAUSE_RESUME;
    if(io.MousePos.x>=0&&io.MousePos.y>=0) {
        ImGui::GetForegroundDrawList()->AddCircleFilled(io.MousePos,8,IM_COL32(255,214,120,255));
        ImGui::GetForegroundDrawList()->AddCircle(io.MousePos,11,IM_COL32(255,255,255,255));
    }
    ImGui::Render();glBindFramebuffer(GL_FRAMEBUFFER,framebuffer);glViewport(0,0,1280,960);glDisable(GL_SCISSOR_TEST);
    glClearColor(.06f,.06f,.06f,1);glClear(GL_COLOR_BUFFER_BIT);ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    vr_screen_present_tex2d(texture,1280,960);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER,(GLuint)drawFbo);glBindFramebuffer(GL_READ_FRAMEBUFFER,(GLuint)readFbo);
    glViewport(viewport[0],viewport[1],viewport[2],viewport[3]);glScissor(scissor[0],scissor[1],scissor[2],scissor[3]);
    if(scissorOn)glEnable(GL_SCISSOR_TEST);else glDisable(GL_SCISSOR_TEST);
    glClearColor(clear[0],clear[1],clear[2],clear[3]);glBindTexture(GL_TEXTURE_2D,(GLuint)boundTexture);
    vr_pointer_draw();ImGui::SetCurrentContext(previous);
    if(action==GEVR_PAUSE_RESUME)gevrNativePauseResume();
    if(action==GEVR_PAUSE_LEAVE){gevrNativePauseResume();gevrLobbySessionStopped();gevrRestartToLauncher();}
    if(action==GEVR_PAUSE_ABORT&&netIsHost()&&netCoopActive()){gevrNativePauseResume();netCoopMissionEnded(NET_COOP_RESULT_ABORTED);}
}
#endif
