#include "gevr_pause_widgets.h"
#include "gevr_pause_input.h"
#include "imgui/imgui_internal.h"
#include <cstdio>
#include <cstdlib>
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
    for(const ImVec4 color:{ImVec4(.95f,.25f,.08f,1),ImVec4(.2f,.4f,.68f,1)}) {
        int vertices=0;
        for(const auto& vertex:window->DrawList->VtxBuffer)if(vertex.col==ImGui::ColorConvertFloat4ToU32(color)) {
            check(vertex.pos.y>=60 && vertex.pos.y<=74,"vital bars stay in the header, with no Player footer copy");
            check(vertex.pos.x>=352 && vertex.pos.x<=712,"vital bars fit between branding and session");vertices++;
        }
        check(vertices>0,"both vital bars are rendered on every tab");
    }
}
int main(){
    GevrPauseInputState gate{};
    check(!gevrPauseBlocksFire(&gate,0,1),"normal trigger fires");
    check(gevrPauseBlocksFire(&gate,1,1),"UI owns trigger");
    check(gevrPauseBlocksFire(&gate,0,1),"resume click cannot fire");
    check(gevrPauseBlocksFire(&gate,0,1),"held trigger remains blocked");
    check(!gevrPauseBlocksFire(&gate,0,0),"release clears latch");
    check(!gevrPauseBlocksFire(&gate,0,1),"new squeeze can fire");
    ImGui::CreateContext();auto&io=ImGui::GetIO();io.IniFilename=nullptr;io.DisplaySize=ImVec2(1280,960);io.DeltaTime=1.f/60;io.FontGlobalScale=2.2f;
    ImGui::StyleColorsDark();ImGui::GetStyle().ScaleAllSizes(2.2f);unsigned char*px;int w,h;io.Fonts->GetTexDataAsRGBA32(&px,&w,&h);
    view.count=8;view.host=true;view.canStart=true;view.health=75;view.armour=40;
    for(int i=0;i<8;i++){snprintf(view.players[i].name,64,"Player %d",i+1);snprintf(view.players[i].character,64,"James Bond");}
    frame();frame();
    auto*window=ImGui::FindWindowByName("##match-window");auto*table=ImGui::GetCurrentContext()->Tables.GetByKey(window->GetID("players"));
    check(table && table->CurrentRow==8,"all eight deathmatch rows rendered");
    check(table->RowPosY2<=table->InnerClipRect.Max.y,"last player fits without scrolling");
    for(int tab=0;tab<4;tab++){ui.tab=tab;frame();frame();checkHeaderVitals();}ui.tab=0;frame();frame();
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
    ImGui::DestroyContext();puts("PASS: native pointer tabs, checkbox, dropdown, host gating, 8-player scores, 4-player co-op, confirmation and trigger release");
}
