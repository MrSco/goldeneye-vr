#ifndef GEVR_PAUSE_WIDGETS_H
#define GEVR_PAUSE_WIDGETS_H
#include "gevr_pause_menu.h"
#include "imgui/imgui.h"
#include <cstring>
#include <cstdio>
#include <initializer_list>

// This view contains no engine pointers, XR calls or GL state. The browser
// starter uses the same 1280x960 window and stable field labels/bindings.
struct GevrPausePlayerView {
    char name[64], character[64];
    int points=0,kills=0,losses=0,ping=0;
    bool host=false,ready=false,down=false;
};
struct GevrPauseView {
    bool coop=false,host=false,canStart=false,canReturn=false,localReady=false;
    int count=0,capacity=8,countdown=0,health=100,armour=0;
    GevrPausePlayerView players[8];
    char session[160]="", status[128]="";
};
struct GevrPauseUi {
    int tab=0;
    bool focusResume=true,confirmLeave=false,confirmAbort=false;
};
enum { GEVR_PAUSE_NONE,GEVR_PAUSE_RESUME,GEVR_PAUSE_LEAVE,GEVR_PAUSE_ABORT };
inline void gevrPauseFieldWidget(const GevrPauseField& f,float x,float y,float width) {
    ImGui::PushID(f.id);
    ImGui::SetCursorPos(ImVec2(x,y+7));ImGui::TextUnformatted(f.label);
    const float labelWidth=ImGui::CalcTextSize(f.label).x+18;
    ImGui::SetCursorPos(ImVec2(x+labelWidth,y));ImGui::SetNextItemWidth(width-labelWidth);
    ImGui::BeginDisabled(!f.editable);
    const ImVec4 gold(.88f,.69f,.25f,1);
    if(f.kind==GEVR_PAUSE_CHOICE) {
        ImGui::PushStyleColor(ImGuiCol_Text,gold);
        if(ImGui::BeginCombo("##value",f.value)) {
            for(int i=0;i<f.count;i++) {
                const bool chosen=i==f.selected;
                if(ImGui::Selectable(gevrPauseChoice(f.id,i),chosen))gevrPauseSelect(f.id,i);
                if(chosen)ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
        ImGui::PopStyleColor();
    } else if(f.kind==GEVR_PAUSE_SLIDER) {
        ImGui::PushStyleColor(ImGuiCol_Text,gold);
        int value=f.selected;
        if(ImGui::SliderInt("##volume",&value,0,100,"%d%%"))gevrPauseVolume(f.id,value);ImGui::PopStyleColor();
    } else if(f.kind==GEVR_PAUSE_TOGGLE) {
        ImGui::PushStyleColor(ImGuiCol_Text,gold);
        bool on=f.selected!=0;
        if(ImGui::Checkbox(f.value,&on))gevrPauseStep(f.id,on?1:-1);ImGui::PopStyleColor();
    } else {
        if(ImGui::Button("-",ImVec2(44,0)))gevrPauseStep(f.id,-1);
        ImGui::SameLine();ImGui::TextColored(gold,"%s",f.value);
        ImGui::SameLine();if(ImGui::Button("+",ImVec2(44,0)))gevrPauseStep(f.id,1);
    }
    ImGui::EndDisabled();ImGui::PopID();
}
inline int gevrDrawPauseWindow(const GevrPauseView& model,GevrPauseUi& ui,ImTextureID icon=0) {
    int action=GEVR_PAUSE_NONE;
    const ImVec4 gold(.88f,.69f,.25f,1),good(.5f,.9f,.5f,1),bad(.95f,.5f,.4f,1);
    ImGui::SetNextWindowPos(ImVec2(0,0));ImGui::SetNextWindowSize(ImVec2(1280,960));
    ImGui::SetNextWindowBgAlpha(1.0f);
    ImGui::Begin("##match-window",nullptr,ImGuiWindowFlags_NoTitleBar|ImGuiWindowFlags_NoMove|ImGuiWindowFlags_NoResize|ImGuiWindowFlags_NoCollapse|ImGuiWindowFlags_NoSavedSettings|ImGuiWindowFlags_NoScrollbar);
    if(icon){ImGui::SetCursorPos(ImVec2(18,18));ImGui::Image(icon,ImVec2(64,64));}
    ImGui::SetCursorPos(ImVec2(100,32));ImGui::TextColored(ImVec4(1,.84f,.47f,1),"GOLDENEYE VR");
    ImGui::SetWindowFontScale(.75f);
    for(int i=0;i<2;i++) {
        const int value=i?model.armour:model.health;
        ImGui::SetCursorPos(ImVec2(352+i*190,22));ImGui::TextColored(gold,"%s  %d%%",i?"ARMOUR":"HEALTH",value);
        ImGui::SetCursorPos(ImVec2(352+i*190,60));
        ImGui::PushStyleColor(ImGuiCol_PlotHistogram,i?ImVec4(.2f,.4f,.68f,1):ImVec4(.95f,.25f,.08f,1));
        ImGui::ProgressBar(value/100.f,ImVec2(170,14),"");ImGui::PopStyleColor();
    }
    ImGui::SetWindowFontScale(1.f);
    ImGui::SetCursorPos(ImVec2(742,22));ImGui::TextColored(gold,"%s",model.session);
    ImGui::SetCursorPos(ImVec2(742,60));ImGui::TextDisabled("%s",model.status);
    ImGui::SetCursorPos(ImVec2(18,98));ImGui::Separator();
    const char*names[]={"Match","Rules","Player","Audio"};
    for(int i=0;i<4;i++) {
        const bool selected=ui.tab==i;
        ImGui::SetCursorPos(ImVec2(18+i*198,122));
        if(selected)ImGui::PushStyleColor(ImGuiCol_Button,ImGui::GetStyleColorVec4(ImGuiCol_TabSelected));
        if(ImGui::Button(names[i],ImVec2(184,48)))ui.tab=i;
        if(selected)ImGui::PopStyleColor();
    }
    if(ui.tab==GEVR_PAUSE_MATCH) {
        if(model.coop) {
            ImGui::SetCursorPos(ImVec2(18,206));ImGui::TextColored(gold,"CO-OP MISSION");
            ImGui::SameLine();ImGui::TextUnformatted(model.session);
        } else {
            GevrPauseField field;
            for(int i=0;gevrPauseReadField(GEVR_PAUSE_MATCH,i,&field);i++) {
                if(!strcmp(field.label,"MAP"))gevrPauseFieldWidget(field,18,206,398);
                else if(!strcmp(field.label,"WEAPONS"))gevrPauseFieldWidget(field,440,206,398);
                else if(!strcmp(field.label,"SCENARIO"))gevrPauseFieldWidget(field,862,206,398);
                else if(!strcmp(field.label,"NEXT ROUND"))gevrPauseFieldWidget(field,742,716,518);
                else if(!strcmp(field.label,"NEXT MAP"))gevrPauseFieldWidget(field,18,772,604);
                else if(!strcmp(field.label,"NEXT WEAPONS"))gevrPauseFieldWidget(field,660,772,602);
            }
            ImGui::SetCursorPos(ImVec2(18,722));ImGui::TextColored(gold,"NEXT ROUND");
            ImGui::SetCursorPos(ImVec2(18,832));ImGui::TextDisabled("Votes are independent; the host starts the next round.");
        }
        ImGui::SetCursorPos(ImVec2(18,276));ImGui::TextColored(gold,"%s  (%d / %d)",model.coop?"PARTY":"PLAYERS & SCORES",model.count,model.capacity);
        ImGui::SetCursorPos(ImVec2(18,320));
        if(ImGui::BeginTable("players",model.coop?4:7,ImGuiTableFlags_RowBg|ImGuiTableFlags_BordersInnerH|ImGuiTableFlags_SizingStretchProp|ImGuiTableFlags_ScrollY,ImVec2(1244,model.coop?224:382))) {
            ImGui::TableSetupColumn("PLAYER",ImGuiTableColumnFlags_WidthStretch,2.2f);
            ImGui::TableSetupColumn("CHARACTER",ImGuiTableColumnFlags_WidthStretch,2.5f);
            if(model.coop) {ImGui::TableSetupColumn("STATUS");ImGui::TableSetupColumn("PING MS");}
            else {for(const char*name:{"PTS","KILLS","LOSSES","PING MS","READY"})ImGui::TableSetupColumn(name);}
            ImGui::TableHeadersRow();
            for(int i=0;i<model.count;i++) {
                const auto&p=model.players[i];ImGui::TableNextRow(0,40);
                ImGui::TableNextColumn();ImGui::Text("%.14s%s",p.name,p.host?" *":"");
                if(ImGui::IsItemHovered())ImGui::SetTooltip("%s%s",p.name,p.host?" (Host)":"");
                ImGui::TableNextColumn();ImGui::TextUnformatted(p.character);
                if(model.coop) {ImGui::TableNextColumn();ImGui::TextColored(p.down?bad:good,"%s",p.down?"Down / revive":"Active");ImGui::TableNextColumn();ImGui::Text("%d",p.ping);}
                else {
                    ImGui::TableNextColumn();ImGui::TextColored(gold,"%d",p.points);
                    ImGui::TableNextColumn();ImGui::Text("%d",p.kills);
                    ImGui::TableNextColumn();ImGui::Text("%d",p.losses);
                    ImGui::TableNextColumn();ImGui::Text("%d",p.ping);
                    ImGui::TableNextColumn();ImGui::TextColored(p.ready?good:ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled),"%s",p.ready?"Ready":"Waiting");
                }
            }
            ImGui::EndTable();
        }
        if(model.coop) {
            ImGui::SetCursorPos(ImVec2(18,574));ImGui::TextColored(gold,"MISSION OBJECTIVES");
            ImGui::SetCursorPos(ImVec2(18,618));ImGui::BeginChild("objectives",ImVec2(1244,222));
            int shown=0;
            for(int i=0;i<10;i++) {char text[256];int status=gevrPauseObjective(i,text,sizeof(text));if(status<0)continue;
                ImGui::PushTextWrapPos(1000);
                ImGui::Text("%c. %s",'A'+shown++,text);ImGui::PopTextWrapPos();
                ImGui::SameLine(1030);ImGui::TextColored(status==1?good:status==2?bad:gold,"%s",status==1?"Complete":status==2?"Failed":"Incomplete");
            }
            if(!shown)ImGui::TextDisabled("No mission objectives available.");
            ImGui::EndChild();
        }
    } else {
        GevrPauseField field;
        for(int i=0;gevrPauseReadField(ui.tab,i,&field);i++) {
            const bool audio=ui.tab==GEVR_PAUSE_AUDIO;
            gevrPauseFieldWidget(field,audio?18:18+(i%2)*642,212+(audio?i:i/2)*76,audio?1100:602);
        }
        ImGui::SetCursorPos(ImVec2(18,816));
        ImGui::TextDisabled("%s",ui.tab==GEVR_PAUSE_AUDIO?"Volumes and microphone are yours. The host chooses voice mode.":ui.tab==GEVR_PAUSE_RULES?"Host settings. Round rules and fun options apply on the next load.":model.coop?"Your character and status. Teammates continue while this window is open.":"Your character, team and loadout. The match continues while this window is open.");
    }
    ImGui::SetCursorPos(ImVec2(18,864));ImGui::Separator();
    ImGui::SetCursorPos(ImVec2(18,886));
    if(ImGui::Button("Resume",ImVec2(244,52)))action=GEVR_PAUSE_RESUME;
    if(ui.focusResume){ImGui::SetItemDefaultFocus();ImGui::SetKeyboardFocusHere(-1);ui.focusResume=false;}
    if(!model.coop) {
        ImGui::SetCursorPos(ImVec2(282,886));
        if(model.host) {char start[64];snprintf(start,sizeof(start),model.countdown>0?"Starting in %d":"Start match",model.countdown);ImGui::BeginDisabled(!model.canStart || model.countdown>0);if(ImGui::Button(start,ImVec2(300,52)))gevrPauseAction("START MATCH");ImGui::EndDisabled();}
        else if(ImGui::Button(model.localReady?"Unready":"Ready up",ImVec2(300,52))) {
            GevrPauseField f;for(int i=0;gevrPauseReadField(GEVR_PAUSE_PLAYER,i,&f);i++)if(!strcmp(f.label,"NEXT ROUND READY"))gevrPauseStep(f.id,1);
        }
        ImGui::SetCursorPos(ImVec2(600,886));ImGui::BeginDisabled(!model.canReturn);
        if(ImGui::Button(model.countdown>0?"Cancel start":"Return to lobby",ImVec2(330,52)))gevrPauseAction("RETURN TO LOBBY");ImGui::EndDisabled();
    } else if(model.host) {ImGui::SetCursorPos(ImVec2(600,886));if(ImGui::Button("End mission...",ImVec2(330,52))) {ui.confirmAbort=true;ImGui::OpenPopup("End mission?");}}
    ImGui::SetCursorPos(ImVec2(948,886));ImGui::PushStyleColor(ImGuiCol_Text,bad);
    if(ImGui::Button(model.coop?"Leave party...":"Leave match...",ImVec2(314,52))) {ui.confirmLeave=true;ImGui::OpenPopup("Leave session?");}
    ImGui::PopStyleColor();
    for(int i=0;i<2;i++) {
        const char*title=i?"End mission?":"Leave session?";
        if(ImGui::BeginPopupModal(title,nullptr,ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::TextUnformatted(i?"End this mission for the whole party?":"Disconnect this headset and return to the launcher?");
            if(ImGui::Button("Cancel",ImVec2(220,52))){ImGui::CloseCurrentPopup();ui.confirmLeave=ui.confirmAbort=false;}
            ImGui::SameLine();if(ImGui::Button(i?"End mission":"Leave",ImVec2(220,52))){action=i?GEVR_PAUSE_ABORT:GEVR_PAUSE_LEAVE;ImGui::CloseCurrentPopup();ui.confirmLeave=ui.confirmAbort=false;}
            ImGui::EndPopup();
        }
    }
    ImGui::End();return action;
}
#endif
