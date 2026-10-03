#include "gevr_pause_widgets.h"
#include "gevr_pause_input.h"
#include "imgui/imgui_internal.h"
#include <cstdio>
#include <cstdlib>
#include <cmath>
extern "C" void testPauseGauges(float health,float armour,int slot);
extern "C" int testPauseGaugeVisibility(int open,int menu,int mode,int gameOver,int healthTime);
static GevrPauseView view;
static GevrPauseUi ui;
static int selected=0,changed=0,action=0,startCount=0;
static void check(bool ok,const char*message){if(!ok){fprintf(stderr,"FAIL: %s\n",message);exit(1);}}
extern "C" int gevrPauseReadField(int tab,int index,GevrPauseField*f) {
    if(tab!=GEVR_PAUSE_AUDIO || index>1)return 0;
    *f=GevrPauseField{};f->id=index;f->editable=view.host;f->kind=index?GEVR_PAUSE_TOGGLE:GEVR_PAUSE_CHOICE;f->selected=selected;f->count=3;
    snprintf(f->label,sizeof(f->label),"%s",index?"MIC":"VOICE MODE");snprintf(f->value,sizeof(f->value),"%s",index?"OFF":"Couch");return 1;
}
extern "C" const char*gevrPauseChoice(int,int n){return n==0?"Couch":n==1?"Proximity":"Other";}
extern "C" void gevrPauseSelect(int,int n){selected=n;changed++;}
extern "C" void gevrPauseStep(int,int){changed++;}
extern "C" void gevrPauseVolume(int,int){changed++;}
extern "C" void gevrPauseAction(const char*){startCount++;}
extern "C" int gevrPauseObjective(int i,char*b,unsigned size){if(i>1)return -1;snprintf(b,size,"%s",i?"Bungee jump from platform":"Neutralize all alarms");return i;}
static void frame(){ImGui::NewFrame();int a=gevrDrawPauseWindow(view,ui);if(a)action=a;ImGui::Render();}
static void click(float x,float y){auto&io=ImGui::GetIO();io.AddMousePosEvent(x,y);frame();io.AddMouseButtonEvent(0,true);frame();io.AddMouseButtonEvent(0,false);frame();frame();}
static void checkHeaderVitals() {
    auto* window=ImGui::FindWindowByName("##match-window");
    int blipVertices=0;
    for(const auto& v:window->DrawList->VtxBuffer)if(v.col==IM_COL32(255,255,0,160)) {
        check(v.pos.x>=466 && v.pos.x<=534 && v.pos.y>=18 && v.pos.y<=86,"live radar blips stay between header arcs");blipVertices++;
    }
    check(blipVertices==4,"live radar is drawn once on every menu tab");
    for(int side=0;side<2;side++) {
        const auto* gauges=view.gauges+side*46;
        int base=-1,matches=0;
        for(int i=0;i<window->DrawList->VtxBuffer.Size;i++) {
            const auto& vertex=window->DrawList->VtxBuffer[i];
            const auto& g=gauges[0];
            if(fabsf(vertex.pos.x-(500+(g.x-1)*42/520.f))<.001f && fabsf(vertex.pos.y-(52+g.y*42/520.f))<.001f && vertex.col==IM_COL32(g.r,g.g,g.b,g.a)){base=i;matches++;}
        }
        check(matches==1,"each original arc is rendered once in the header on every tab");
        for(int i=0;i<46;i++) {
            const auto& vertex=window->DrawList->VtxBuffer[base+i];const auto& g=gauges[i];
            check(fabsf(vertex.pos.x-(500+(g.x-1)*42/520.f))<.001f && fabsf(vertex.pos.y-(52+g.y*42/520.f))<.001f,"header preserves original curved vertex positions");
            check(vertex.col==IM_COL32(g.r,g.g,g.b,g.a),"header preserves original gradient and partial-fill alpha");
            check(vertex.pos.x>=440 && vertex.pos.x<=560 && vertex.pos.y>=8 && vertex.pos.y<98,"arcs fit beside branding above the divider");
        }
        int indices=0;
        for(int index:window->DrawList->IdxBuffer)if(index>=base && index<base+46)indices++;
        check(indices==84,"original segment gaps produce 28 triangles per arc");
    }
}
int main(){
    check(!testPauseGaugeVisibility(1,1,0,0,60),"native pause owns arcs, with no copy behind the panel");
    check(testPauseGaugeVisibility(0,1,0,0,60),"legacy status menu keeps its original gauges");
    check(testPauseGaugeVisibility(0,0,0,0,60),"ordinary timed HUD gauges remain available after closing");
    check(!testPauseGaugeVisibility(0,0,0,0,0),"ordinary HUD visibility remains unchanged");
    GevrPauseInputState gate{};
    check(!gevrPauseBlocksFire(&gate,0,1),"normal trigger fires");
    check(gevrPauseBlocksFire(&gate,1,1),"UI owns trigger");
    check(gevrPauseBlocksFire(&gate,0,1),"resume click cannot fire");
    check(gevrPauseBlocksFire(&gate,0,1),"held trigger remains blocked");
    check(!gevrPauseBlocksFire(&gate,0,0),"release clears latch");
    check(!gevrPauseBlocksFire(&gate,0,1),"new squeeze can fire");
    ImGui::CreateContext();auto&io=ImGui::GetIO();io.IniFilename=nullptr;io.DisplaySize=ImVec2(1280,960);io.DeltaTime=1.f/60;io.FontGlobalScale=2.2f;
    ImGui::StyleColorsDark();ImGui::GetStyle().ScaleAllSizes(2.2f);unsigned char*px;int w,h;io.Fonts->GetTexDataAsRGBA32(&px,&w,&h);
    view.count=8;view.host=true;view.canStart=true;
    testPauseGauges(.75f,.4f,7);gevrPauseLocalGauges(view.gauges);
    view.radar.visible=1;view.radar.count=2;
    view.radar.blips[0]={0,0,255,255,255,160};view.radar.blips[1]={.25f,-.5f,255,255,0,160};
    check(view.gauges[0].x<0 && view.gauges[46].x>0,"local slot seven has original left health and right armour geometry");
    check(view.gauges[0].r==255 && view.gauges[46].b==255,"original red health and blue armour colors");
    for(int i=0;i<8;i++){snprintf(view.players[i].name,64,"Player %d",i+1);snprintf(view.players[i].character,64,"James Bond");view.players[i].slot=i;view.players[i].host=i==0;view.players[i].canKick=i!=0;}
    view.players[6].spectator=true;view.players[7].loaded=false;
    frame();frame();
    auto*window=ImGui::FindWindowByName("##match-window");auto*table=ImGui::GetCurrentContext()->Tables.GetByKey(window->GetID("players"));
    check(table && table->CurrentRow==8 && table->ColumnsCount==8,"host sees eight connected rows, including spectators/loading, plus kick column");
    check(table->RowPosY2<=table->InnerClipRect.Max.y,"last player fits without scrolling");
    check(table->Columns[6].WorkMaxX-table->Columns[6].WorkMinX>=ImGui::CalcTextSize("Spectator").x,"ready/loading/spectator status fits at headset font scale");
    check(table->Columns[7].WorkMaxX-table->Columns[7].WorkMinX>=88,"host kick buttons fit inside their column");
    float kickX=table->Columns[7].WorkMinX+40,kickY=table->RowPosY1+16;
    click(kickX,kickY);frame();
    auto*kickModal=ImGui::FindWindowByName("Kick player?");
    check(kickModal && kickModal->Active && ui.kickSlot==7 && action==0,"host kick requires confirmation and retains the loading player's slot");
    click(kickModal->Pos.x+100,kickModal->Pos.y+kickModal->Size.y-40);
    check(action==0 && ui.kickSlot<0,"cancel preserves connected player");
    click(kickX,kickY);frame();
    click(kickModal->Pos.x+kickModal->Size.x-100,kickModal->Pos.y+kickModal->Size.y-40);
    check(action==GEVR_PAUSE_KICK_BASE+7,"confirmed kick targets the correct slot");action=0;
    view.host=false;frame();frame();table=ImGui::GetCurrentContext()->Tables.GetByKey(window->GetID("players"));
    check(table->ColumnsCount==7,"clients have no kick controls");view.host=true;
    view.voting=true;frame();frame();click(500,736);
    check(action==GEVR_PAUSE_REQUEST_VOTES,"host can request votes without opening players' menus");action=0;view.voting=false;
    for(bool coop:{false,true})for(float health:{0.f,.125f,.35f,.75f,1.f}) {
        view.coop=coop;testPauseGauges(health,1-health,coop?3:7);gevrPauseLocalGauges(view.gauges);
        for(int tab=0;tab<4;tab++){ui.tab=tab;frame();frame();checkHeaderVitals();}
    }
    testPauseGauges(1,1,-1);gevrPauseLocalGauges(view.gauges);
    for(const auto& g:view.gauges)check(g.a==48,"absent local player produces original empty arcs");
    view.coop=false;ui.tab=0;frame();frame();
    click(400,912);check(startCount==1,"pointer starts match");view.canStart=false;click(400,912);check(startCount==1,"disabled start ignores pointer");
    click(700,146);check(ui.tab==GEVR_PAUSE_AUDIO,"direct Audio tab");
    click(120,307);check(changed==1,"pointer toggles checkbox");view.host=false;click(120,307);check(changed==1,"disabled control cannot change settings");view.host=true;
    click(400,230);frame();check(ImGui::IsPopupOpen(nullptr,ImGuiPopupFlags_AnyPopupId|ImGuiPopupFlags_AnyPopupLevel),"pointer opens actual dropdown");
    click(400,285);frame(); // select a visible named choice
    check(changed>1,"pointer chooses a named option");
    click(100,146);view.coop=true;view.count=4;frame();frame();table=ImGui::GetCurrentContext()->Tables.GetByKey(window->GetID("players"));
    check(table && table->CurrentRow==4 && table->ColumnsCount==4,"co-op party uses four rows and status columns");
    click(1100,912);check(action==0,"leave requires confirmation");frame();
    auto*modal=ImGui::FindWindowByName("Leave session?");check(modal && modal->Active,"leave confirmation is visible");
    // Cancel and verify that no disconnect action is produced.
    click(modal->Pos.x+100,modal->Pos.y+modal->Size.y-40);check(action==0,"cancel keeps session");
    click(100,912);check(action==GEVR_PAUSE_RESUME,"pointer resumes gameplay");
    ImGui::DestroyContext();puts("PASS: original header arcs and fill on all tabs, pause-only HUD visibility, pointer controls, scores, co-op and trigger release");
}
