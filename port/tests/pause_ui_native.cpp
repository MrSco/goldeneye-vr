#include "gevr_pause_widgets.h"
#include "gevr_pause_input.h"
#include "imgui/imgui_internal.h"
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <cstring>
extern "C" void testPauseGauges(float health,float armour,int slot);
extern "C" int testPauseGaugeVisibility(int open,int menu,int mode,int gameOver,int healthTime);
static GevrPauseView view;
static GevrPauseUi ui;
static int selected=0,changed=0,action=0,startCount=0;
static void check(bool ok,const char*message){if(!ok){fprintf(stderr,"FAIL: %s\n",message);exit(1);}}
/* mpmenu.c's fields, faked: two of Rules' (Audio is the settings table's own rows now) */
extern "C" int gevrPauseReadField(int tab,int index,GevrPauseField*f) {
    if(tab!=GEVR_PAUSE_RULES || index>1)return 0;
    *f=GevrPauseField{};f->id=index;f->editable=view.host;f->kind=index?GEVR_PAUSE_TOGGLE:GEVR_PAUSE_CHOICE;f->selected=selected;f->count=3;
    snprintf(f->label,sizeof(f->label),"%s",index?"LOADOUTS":"HEALTH");snprintf(f->value,sizeof(f->value),"%s",index?"OFF":"Normal");return 1;
}
extern "C" const char*gevrPauseChoice(int,int n){return n==0?"Normal":n==1?"Double":"Half";}
extern "C" void gevrPauseSelect(int,int n){selected=n;changed++;}
extern "C" void gevrPauseStep(int,int){changed++;}
extern "C" void gevrPauseVolume(int,int){changed++;}
extern "C" void gevrPauseAction(const char*){startCount++;}
extern "C" int gevrPauseObjective(int i,char*b,unsigned size){if(i>1)return -1;snprintf(b,size,"%s",i?"Bungee jump from platform":"Neutralize all alarms");return i;}
// The Settings tab's rows (the settings table's, faked): seven sections, the fullest thirteen
static int fakeIndex[16*16];
static int fakeSectionCount(){return 7;}
static const char*fakeSectionName(int s){static const char*n[]={"Comfort","Controls","Gestures","Weapons","Display","Game rules","Mods and fun"};return n[s];}
static int fakeSectionRows(int s,unsigned,int*ids,int max){const int n=s==4?13:7;for(int i=0;i<n&&i<max;i++)ids[i]=s*16+i;return n;}
static int fakeInfo(int id,GevrSettingInfo*out){out->label=id%3?"Fast reinforcements":"Screen dist";out->tooltip=nullptr;
    out->kind=id%3;out->section=id/16;out->launcherSection=0;out->where=GEVR_SET_BOTH;return 1;}
static int fakePositions(int id){return id%3==2?29:id%3==1?3:2;}
static int fakeIndexOf(int id){return fakeIndex[id];}
static int fakeDefault(int id){return id%3==2?0:-1;}
static void fakeSet(int id,int i){fakeIndex[id]=i;}
static void fakeText(int id,int,char*b,int n){snprintf(b,n,"%s",id%3==2?"8.00 m":id%3==1?"Host: on":"On");}
static const char*fakeBlocked(int){return nullptr;}
static void frame(){ImGui::NewFrame();int a=gevrDrawPauseWindow(view,ui);if(a)action=a;ImGui::Render();}
/* the tab row's tabs, and a row's control in the rows box, where the layout put them */
static ImVec2 tabCentre(int i){
    auto*window=ImGui::FindWindowByName("##match-window");
    ImGuiTabBar*bar=ImGui::GetCurrentContext()->TabBars.GetByKey(window->GetID("##match-tabs"));
    if(!bar||i>=bar->Tabs.Size)return ImVec2(-100,-100);
    return ImVec2(bar->BarRect.Min.x+bar->Tabs[i].Offset+bar->Tabs[i].Width*.5f,(bar->BarRect.Min.y+bar->BarRect.Max.y)*.5f);
}
static ImVec2 rowControl(int row,int rows){
    ImGuiWindow*box=nullptr;
    for(ImGuiWindow*x:ImGui::GetCurrentContext()->Windows)if(strstr(x->Name,"##pause-rows")&&x->Active)box=x;
    if(!box)return ImVec2(-100,-100);
    ImGuiTable*t=ImGui::GetCurrentContext()->Tables.GetByKey(box->GetID("##settings"));
    if(!t)return ImVec2(-100,-100);
    const float rowH=(t->RowPosY2-t->OuterRect.Min.y)/rows;
    return ImVec2(t->Columns[1].MinX+24,t->OuterRect.Min.y+rowH*(row+.5f));
}
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
        for(int tab=0;tab<5;tab++){ui.tab=tab;frame();frame();checkHeaderVitals();}
    }
    testPauseGauges(1,1,-1);gevrPauseLocalGauges(view.gauges);
    for(const auto& g:view.gauges)check(g.a==48,"absent local player produces original empty arcs");
    view.coop=false;ui.tab=0;frame();frame();
    click(400,912);check(startCount==1,"pointer starts match");view.canStart=false;click(400,912);check(startCount==1,"disabled start ignores pointer");
    { const ImVec2 t=tabCentre(GEVR_PAUSE_RULES);click(t.x,t.y);check(ui.tab==GEVR_PAUSE_RULES,"direct Rules tab"); }
    /* mpmenu.c's fields through the settings renderer: a checkbox, a dropdown, the host's choice greyed */
    { const ImVec2 box=rowControl(1,2);click(box.x,box.y);check(changed==1,"pointer toggles checkbox");
      view.host=false;frame();frame();click(box.x,box.y);check(changed==1,"disabled control cannot change settings");view.host=true;frame();frame(); }
    { const ImVec2 combo=rowControl(0,2);click(combo.x+60,combo.y);frame();
      check(ImGui::IsPopupOpen(nullptr,ImGuiPopupFlags_AnyPopupId|ImGuiPopupFlags_AnyPopupLevel),"pointer opens actual dropdown");
      ImGuiWindow*pop=nullptr;for(ImGuiWindow*x:ImGui::GetCurrentContext()->Windows)if((x->Flags&ImGuiWindowFlags_Popup)&&x->Active)pop=x;
      check(pop!=nullptr,"the dropdown's list is drawn");
      const float itemH=pop->ContentSize.y/3;
      click(pop->Pos.x+60,pop->Pos.y+pop->WindowPadding.y+itemH*1.5f);frame(); // select a visible named choice
      check(changed>1 && selected==1,"pointer chooses a named option"); }
    { const ImVec2 t=tabCentre(GEVR_PAUSE_AUDIO);click(t.x,t.y);check(ui.tab==GEVR_PAUSE_AUDIO,"direct Audio tab"); }
    // Settings (hold Menu online): the panel's sections, each fitting without scrolling, and Reset sound
    view.settings.sectionCount=fakeSectionCount;view.settings.sectionName=fakeSectionName;view.settings.sectionRows=fakeSectionRows;
    view.settings.info=fakeInfo;view.settings.positions=fakePositions;view.settings.index=fakeIndexOf;
    view.settings.defaultIndex=fakeDefault;view.settings.set=fakeSet;view.settings.text=fakeText;view.settings.blocked=fakeBlocked;
    view.texpack=1;
    { const ImVec2 t=tabCentre(GEVR_PAUSE_SETTINGS);click(t.x,t.y);check(ui.tab==GEVR_PAUSE_SETTINGS,"direct Settings tab"); }
    for(int s=0;s<7;s++) {
        ui.openSection=s;frame();frame();
        ImGuiWindow*rows=nullptr;
        for(ImGuiWindow*x:ImGui::GetCurrentContext()->Windows)
            if(strstr(x->Name,"##panel-rows")&&x->Active&&x->LastFrameActive==ImGui::GetFrameCount())rows=x;
        check(rows!=nullptr,"each settings section is drawn on the Settings tab");
        check(rows->ContentSize.y<=rows->Size.y,"a section's rows, thirteen at the fullest, fit the Settings tab without scrolling");
        check(rows->ContentSize.x+rows->WindowPadding.x*2<=rows->Size.x+1,"the rows fit the Settings tab's width");
        check(rows->Pos.y+rows->Size.y<=806,"the rows end above HD textures and Reset sound");
    }
    click(336+165,830);check(action==GEVR_PAUSE_RESET_SOUND,"Reset sound on the Settings tab");action=0;
    click(18+150,830);check(action==GEVR_PAUSE_TEXPACK,"HD textures on the Settings tab");action=0;
    { const ImVec2 t=tabCentre(GEVR_PAUSE_MATCH);click(t.x,t.y); }
    view.coop=true;view.count=4;frame();frame();table=ImGui::GetCurrentContext()->Tables.GetByKey(window->GetID("players"));
    check(table && table->CurrentRow==4 && table->ColumnsCount==4,"co-op party uses four rows and status columns");
    click(1100,912);check(action==0,"leave requires confirmation");frame();
    auto*modal=ImGui::FindWindowByName("Leave session?");check(modal && modal->Active,"leave confirmation is visible");
    // Cancel and verify that no disconnect action is produced.
    click(modal->Pos.x+100,modal->Pos.y+modal->Size.y-40);check(action==0,"cancel keeps session");
    click(100,912);check(action==GEVR_PAUSE_RESUME,"pointer resumes gameplay");
    ImGui::DestroyContext();puts("PASS: original header arcs and fill on all tabs, pause-only HUD visibility, pointer controls, scores, co-op and trigger release");
}
