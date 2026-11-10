#ifndef GEVR_PAUSE_WIDGETS_H
#define GEVR_PAUSE_WIDGETS_H
#include "gevr_pause_menu.h"
#include "imgui/imgui.h"
#include "gevr_tooltip.h"
#include "gevr_settings_ui.h"
#include <cstring>
#include <cstdio>
#include <initializer_list>

// This view contains no engine pointers, XR calls or GL state. The browser
// starter uses the same 1280x960 window and stable field labels/bindings.
struct GevrPausePlayerView {
    char name[64], character[64];
    int slot=-1,points=0,kills=0,losses=0,ping=0;
    bool host=false,ready=false,down=false,loaded=true,spectator=false,canKick=false,bot=false;
};
struct GevrPauseView {
    bool horde=false,hordeDefeated=false;
    bool coop=false,host=false,canStart=false,canReturn=false,localReady=false,soloWarmup=false;
    bool startRequested=false,rosterReady=false,voting=false,warmup=true;
    int count=0,capacity=8,countdown=0;
    GevrPauseGaugeVertex gauges[92]{};
    GevrPauseRadarView radar{};
    GevrPausePlayerView players[8];
    char session[160]="", status[128]="", hordeStats[160]="";
    char clock[32]="";   /* time left or played, while a round runs; empty otherwise */
    bool clockEnding=false;   /* the last minute of a timed match */
    GevrSettingsApi settings;   /* the Settings tab: the settings table (gevr_settings.c), or a test's rows */
    int texpack=-1;             /* the Settings tab's HD textures: 1 on, 0 off, -1 no pack */
};
struct GevrPauseUi {
    int tab=0;
    int shownTab=-1;      /* the tab the tab row shows; another tab asked for is brought up */
    int openSection=-1;   /* the Settings tab's section to bring up (the layout tests) */
    bool focusResume=true,confirmLeave=false,confirmAbort=false;
    int kickSlot=-1;
    char kickName[64]="";
};
enum { GEVR_PAUSE_NONE,GEVR_PAUSE_RESUME,GEVR_PAUSE_LEAVE,GEVR_PAUSE_ABORT,GEVR_PAUSE_REQUEST_VOTES,GEVR_PAUSE_HORDE_RETRY,
       GEVR_PAUSE_TEXPACK,GEVR_PAUSE_RESET_SOUND,GEVR_PAUSE_KICK_BASE=100 };
inline void gevrPauseRadarWidget(const GevrPauseRadarView& radar) {
    if(!radar.visible)return;
    auto* draw=ImGui::GetWindowDrawList();const ImVec2 origin=ImGui::GetWindowPos();
    const ImVec2 center(origin.x+500,origin.y+52);
    draw->AddCircleFilled(center,34,IM_COL32(0,0,0,160),48);
    for(int i=0;i<radar.count;i++) {
        const auto& b=radar.blips[i];const ImVec2 p(center.x+b.x*32,center.y+b.y*32);
        draw->AddRectFilled(ImVec2(p.x-4,p.y-4),ImVec2(p.x+4,p.y+4),IM_COL32(0,0,0,64));
        draw->AddRectFilled(ImVec2(p.x-2,p.y-2),ImVec2(p.x+2,p.y+2),IM_COL32(b.r,b.g,b.b,b.a));
    }
}
inline void gevrPauseGaugeWidget(const GevrPauseGaugeVertex* vertices) {
    auto* draw=ImGui::GetWindowDrawList();
    const ImVec2 origin=ImGui::GetWindowPos(),uv=ImGui::GetFontTexUvWhitePixel();
    draw->PrimReserve(84,46);
    const unsigned base=draw->_VtxCurrentIdx;
    for(int i=0;i<46;i++) {
        const auto& v=vertices[i];
        draw->PrimWriteVtx(ImVec2(origin.x+500+(v.x-1)*42/520.f,origin.y+52+v.y*42/520.f),uv,IM_COL32(v.r,v.g,v.b,v.a));
    }
    // Same pairs and gaps as buildGaugeBarDL; preserve the original gradients.
    for(int i=0;i<22;i++)if(i<9?(i&1)==0:(i+3)%4!=0) {
        for(unsigned index:{0u,1u,2u,1u,2u,3u})draw->PrimWriteIdx((ImDrawIdx)(base+i*2+index));
    }
}
/*
 * The online tabs' rows through the settings renderer (gevr_settings_ui.h), as
 * the launcher and the Settings tab draw theirs (docs/launcher-unify.md):
 * mpmenu.c's live match fields (gevrPauseReadField: the map, votes, rules,
 * bots, your team and loadout) become dropdowns, checkboxes and sliders, one
 * not yours to change greyed as the host's choice; their behaviour stays
 * mpmenu.c's (gevrPauseSelect, gevrPauseStep, gevrPauseVolume). Rules' fun
 * switches and the whole Audio tab are the settings table's own rows
 * (GEVR_LSEC_MP_FUN, GEVR_LSEC_MP_AUDIO), the launcher's Multiplayer ones,
 * which set the same host config; mpmenu.c's copies of them are left out.
 */
#define GEVR_PAUSE_TABLE_ID 1000       /* a table row here: 1000 + its id in the table */
struct GevrPauseRowsState {
    GevrSettingsApi table;             /* the settings table, or a test's rows */
    GevrPauseField cache[64];          /* the tab's fields, read once a frame by sectionRows */
    int cached=0;
    char labels[256][64];              /* "NEXT ROUND READY" as "Next round ready", by id */
};
inline GevrPauseRowsState &gevrPauseRows() { static GevrPauseRowsState state; return state; }
inline bool gevrPauseFunCopy(const GevrPauseField &f) {
    /* mpmenu.c's fun switches (ids 200 on), but its movement speed: the table's are drawn */
    static const char *const fun[]={"DK MODE","PAINTBALL","LINE MODE","NO RADAR","HIT IMMUNITY","GUN SIZE"};
    if(f.id<200)return false;
    for(const char*name:fun)if(!strcmp(f.label,name))return true;
    return false;
}
inline const GevrPauseField *gevrPauseFieldOf(int id) {
    GevrPauseRowsState &s=gevrPauseRows();
    for(int i=0;i<s.cached;i++)if(s.cache[i].id==id)return &s.cache[i];
    /* not on the tab read last (the Match tab's own places): look it up */
    for(int tab=GEVR_PAUSE_MATCH;tab<=GEVR_PAUSE_AUDIO;tab++) {
        GevrPauseField f;
        for(int i=0;gevrPauseReadField(tab,i,&f);i++)
            if(f.id==id && s.cached<64){s.cache[s.cached]=f;return &s.cache[s.cached++];}
    }
    return nullptr;
}
inline int gevrPauseRowsCount(){return 4;}
inline const char *gevrPauseRowsName(int tab){static const char*n[]={"Match","Rules","Player","Audio"};return tab>=0&&tab<4?n[tab]:"";}
inline int gevrPauseRowsList(int tab,unsigned,int *ids,int max) {
    GevrPauseRowsState &s=gevrPauseRows();
    int n=0;
    s.cached=0;
    if(tab!=GEVR_PAUSE_AUDIO) {
        GevrPauseField f;
        for(int i=0;s.cached<64 && gevrPauseReadField(tab,i,&f);i++) {
            s.cache[s.cached++]=f;
            if(tab==GEVR_PAUSE_RULES && gevrPauseFunCopy(f))continue;
            if(n<max)ids[n]=f.id;
            n++;
        }
    }
    const int section=tab==GEVR_PAUSE_RULES?GEVR_LSEC_MP_FUN:tab==GEVR_PAUSE_AUDIO?GEVR_LSEC_MP_AUDIO:-1;
    if(section>=0 && s.table.sectionRows) {
        int rows[32];
        const int got=s.table.sectionRows(section,GEVR_SET_LAUNCHER,rows,32);
        for(int i=0;i<got && i<32;i++){if(n<max)ids[n]=GEVR_PAUSE_TABLE_ID+rows[i];n++;}
    }
    return n;
}
inline int gevrPauseRowsInfo(int id,GevrSettingInfo *out) {
    GevrPauseRowsState &s=gevrPauseRows();
    if(id>=GEVR_PAUSE_TABLE_ID)return s.table.info?s.table.info(id-GEVR_PAUSE_TABLE_ID,out):0;
    const GevrPauseField *f=gevrPauseFieldOf(id);
    if(!f || id<0 || id>=256)return 0;
    char *label=s.labels[id];
    snprintf(label,64,"%s",f->label);
    for(int i=1;label[i];i++)if(label[i]>='A'&&label[i]<='Z')label[i]=(char)(label[i]-'A'+'a');
    out->label=label;out->tooltip=nullptr;
    out->kind=f->kind==GEVR_PAUSE_TOGGLE?GEVR_SET_TOGGLE:f->kind==GEVR_PAUSE_CHOICE?GEVR_SET_CHOICE:GEVR_SET_RANGE;
    out->section=out->launcherSection=0;out->where=GEVR_SET_INGAME;
    return 1;
}
inline int gevrPauseRowsPositions(int id) {
    if(id>=GEVR_PAUSE_TABLE_ID)return gevrPauseRows().table.positions(id-GEVR_PAUSE_TABLE_ID);
    const GevrPauseField *f=gevrPauseFieldOf(id);
    if(!f)return 0;
    /* a stepper (no list of its own) is three: back, as it is, on */
    return f->kind==GEVR_PAUSE_CHOICE?f->count:f->kind==GEVR_PAUSE_TOGGLE?2:f->kind==GEVR_PAUSE_SLIDER?21:3;
}
inline int gevrPauseRowsIndex(int id) {
    if(id>=GEVR_PAUSE_TABLE_ID)return gevrPauseRows().table.index(id-GEVR_PAUSE_TABLE_ID);
    const GevrPauseField *f=gevrPauseFieldOf(id);
    if(!f)return 0;
    return f->kind==GEVR_PAUSE_SLIDER?(f->selected+2)/5:f->kind==GEVR_PAUSE_STEP?1:f->selected;
}
inline int gevrPauseRowsDefault(int id) {
    return id>=GEVR_PAUSE_TABLE_ID && gevrPauseRows().table.defaultIndex?gevrPauseRows().table.defaultIndex(id-GEVR_PAUSE_TABLE_ID):-1;
}
inline void gevrPauseRowsSet(int id,int index) {
    if(id>=GEVR_PAUSE_TABLE_ID){gevrPauseRows().table.set(id-GEVR_PAUSE_TABLE_ID,index);return;}
    const GevrPauseField *f=gevrPauseFieldOf(id);
    if(!f)return;
    if(f->kind==GEVR_PAUSE_CHOICE)gevrPauseSelect(id,index);
    else if(f->kind==GEVR_PAUSE_SLIDER)gevrPauseVolume(id,index*5);
    else if(f->kind==GEVR_PAUSE_TOGGLE){if((index!=0)!=(f->selected!=0))gevrPauseStep(id,index?1:-1);}
    else if(index!=1)gevrPauseStep(id,index<1?-1:1);
    gevrPauseRows().cached=0;   /* read again: a choice can change what the tab shows */
}
inline void gevrPauseRowsText(int id,int index,char *buf,int size) {
    if(id>=GEVR_PAUSE_TABLE_ID){gevrPauseRows().table.text(id-GEVR_PAUSE_TABLE_ID,index,buf,size);return;}
    const GevrPauseField *f=gevrPauseFieldOf(id);
    if(!f){snprintf(buf,size,"%s","");return;}
    if(f->kind==GEVR_PAUSE_CHOICE)snprintf(buf,size,"%s",gevrPauseChoice(id,index<0?f->selected:index));
    else if(f->kind==GEVR_PAUSE_TOGGLE)snprintf(buf,size,"%s",(index<0?f->selected:index)?"On":"Off");
    else if(f->kind==GEVR_PAUSE_SLIDER)snprintf(buf,size,"%d%%",(index<0?(f->selected+2)/5:index)*5);
    else snprintf(buf,size,"%s",f->value);
}
inline const char *gevrPauseRowsBlocked(int id) {
    if(id>=GEVR_PAUSE_TABLE_ID)return gevrPauseRows().table.blocked?gevrPauseRows().table.blocked(id-GEVR_PAUSE_TABLE_ID):nullptr;
    const GevrPauseField *f=gevrPauseFieldOf(id);
    return f && !f->editable?"Host's choice":nullptr;
}
inline GevrSettingsApi gevrPauseRowsApi(const GevrSettingsApi &table) {
    gevrPauseRows().table=table;
    GevrSettingsApi a;
    a.sectionCount=gevrPauseRowsCount;a.sectionName=gevrPauseRowsName;a.sectionRows=gevrPauseRowsList;
    a.info=gevrPauseRowsInfo;a.positions=gevrPauseRowsPositions;a.index=gevrPauseRowsIndex;
    a.defaultIndex=gevrPauseRowsDefault;a.set=gevrPauseRowsSet;a.text=gevrPauseRowsText;a.blocked=gevrPauseRowsBlocked;
    return a;
}
/* One field in its own place (the Match tab's map and votes): its label, then its control */
inline void gevrPauseRowAt(const GevrSettingsApi &api,int id,float x,float y,float width) {
    GevrSettingInfo info{};
    if(!api.info(id,&info))return;
    ImGui::SetCursorPos(ImVec2(x,y+7));ImGui::TextUnformatted(info.label);
    if(ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)&&api.blocked(id))gevrTooltip("%s.",api.blocked(id));
    const float labelWidth=ImGui::CalcTextSize(info.label).x+18;
    ImGui::SetCursorPos(ImVec2(x+labelWidth,y));
    gevrSettingControl(api,id,width-labelWidth);
}
// The header both in-game windows share (the online pause, the hold-Menu
// panel): icon and name, radar, health and armour arcs, where you are, a
// clock, a status line, then the divider at y 98.
inline void gevrPauseHeader(ImTextureID icon,const GevrPauseRadarView& radar,const GevrPauseGaugeVertex* gauges,
                            const char* session,const char* status,const char* clock="",bool clockEnding=false) {
    const ImVec4 gold(.88f,.69f,.25f,1),bad(.95f,.5f,.4f,1);
    /* the logo (3:1), or the title in text without it */
    if(icon){ImGui::SetCursorPos(ImVec2(18,18));ImGui::Image(icon,ImVec2(192,64));}
    else{ImGui::SetCursorPos(ImVec2(18,32));ImGui::TextColored(ImVec4(1,.84f,.47f,1),"GOLDENEYE VR");}
    gevrPauseRadarWidget(radar);
    gevrPauseGaugeWidget(gauges);
    gevrPauseGaugeWidget(gauges+46);
    ImGui::SetCursorPos(ImVec2(742,22));ImGui::TextColored(gold,"%s",session);
    /* the match clock at the right of the map's name */
    if(clock[0]) {
        ImGui::SetCursorPos(ImVec2(1262-ImGui::CalcTextSize(clock).x,22));
        ImGui::TextColored(clockEnding?bad:ImVec4(1,1,1,1),"%s",clock);
    }
    ImGui::SetCursorPos(ImVec2(742,60));ImGui::TextDisabled("%s",status);
    ImGui::SetCursorPos(ImVec2(18,98));ImGui::Separator();
}
inline int gevrDrawPauseWindow(const GevrPauseView& model,GevrPauseUi& ui,ImTextureID icon=0) {
    int action=GEVR_PAUSE_NONE;
    const ImVec4 gold(.88f,.69f,.25f,1),good(.5f,.9f,.5f,1),bad(.95f,.5f,.4f,1);
    ImGui::SetNextWindowPos(ImVec2(0,0));ImGui::SetNextWindowSize(ImVec2(1280,960));
    ImGui::SetNextWindowBgAlpha(1.0f);
    ImGui::Begin("##match-window",nullptr,ImGuiWindowFlags_NoTitleBar|ImGuiWindowFlags_NoMove|ImGuiWindowFlags_NoResize|ImGuiWindowFlags_NoCollapse|ImGuiWindowFlags_NoSavedSettings|ImGuiWindowFlags_NoScrollbar);
    gevrPauseHeader(icon,model.radar,model.gauges,model.session,model.status,model.clock,model.clockEnding);
    /* the tab row, as the launcher's and the panel's (Settings: the panel's sections, online) */
    const char*names[]={"Match","Rules","Player","Audio","Settings"};
    const GevrSettingsApi rows=gevrPauseRowsApi(model.settings);
    ImGui::SetCursorPos(ImVec2(18,112));
    if(ImGui::BeginTabBar("##match-tabs",ImGuiTabBarFlags_FittingPolicyResizeDown)) {
        const bool asked=ui.tab!=ui.shownTab;   /* a tab asked for (hold Menu: Settings) */
        for(int i=0;i<5;i++) {
            if(!ImGui::BeginTabItem(names[i],nullptr,asked&&ui.tab==i?ImGuiTabItemFlags_SetSelected:0))continue;
            if(!asked||i==ui.tab){ui.tab=i;ui.shownTab=i;}
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
    if(ui.tab==GEVR_PAUSE_MATCH) {
        if(model.horde) {
            ImGui::SetCursorPos(ImVec2(18,590));ImGui::TextColored(gold,"%s",model.hordeStats);
            ImGui::SetCursorPos(ImVec2(18,652));
            ImGui::TextWrapped("%s",model.hordeDefeated?"Everyone is down. The host can retry, or leave to choose another mode.":"Clear every guard to finish the wave. Ammo is supplied between waves. Stay beside a downed teammate to revive them.");
        }
        if(model.coop) {
            ImGui::SetCursorPos(ImVec2(18,206));ImGui::TextColored(gold,model.horde?"HORDE":"CO-OP MISSION");
            ImGui::SameLine();ImGui::TextUnformatted(model.session);
        } else {
            int ids[16];
            const int n=rows.sectionRows(GEVR_PAUSE_MATCH,GEVR_SET_INGAME,ids,16);
            for(int i=0;i<n && i<16;i++) {
                const GevrPauseField *field=gevrPauseFieldOf(ids[i]);
                if(!field)continue;
                if(!strcmp(field->label,"MAP"))gevrPauseRowAt(rows,ids[i],18,206,398);
                else if(!strcmp(field->label,"WEAPONS"))gevrPauseRowAt(rows,ids[i],440,206,398);
                else if(!strcmp(field->label,"SCENARIO"))gevrPauseRowAt(rows,ids[i],862,206,398);
                else if(!strcmp(field->label,"NEXT ROUND"))gevrPauseRowAt(rows,ids[i],742,716,518);
                else if(!strcmp(field->label,"NEXT MAP"))gevrPauseRowAt(rows,ids[i],18,772,604);
                else if(!strcmp(field->label,"NEXT WEAPONS"))gevrPauseRowAt(rows,ids[i],660,772,602);
            }
            ImGui::SetCursorPos(ImVec2(18,722));ImGui::TextColored(gold,"NEXT ROUND");
            if(model.host && model.voting) {
                ImGui::SetCursorPos(ImVec2(340,712));ImGui::BeginDisabled(model.countdown>0 || !model.canStart);
                if(ImGui::Button("Request votes",ImVec2(350,48)))action=GEVR_PAUSE_REQUEST_VOTES;
                ImGui::EndDisabled();
            }
            ImGui::SetCursorPos(ImVec2(18,832));ImGui::TextDisabled("Ready players start after warmup; the host can start early.");
        }
        ImGui::SetCursorPos(ImVec2(18,276));ImGui::TextColored(gold,"%s  (%d / %d)",model.coop?"PARTY":"PLAYERS & SCORES",model.count,model.capacity);
        ImGui::SetCursorPos(ImVec2(18,320));
        const bool kicks=model.host && !model.coop;
        if(ImGui::BeginTable("players",model.coop?4:kicks?8:7,ImGuiTableFlags_RowBg|ImGuiTableFlags_BordersInnerH|ImGuiTableFlags_SizingStretchProp|ImGuiTableFlags_ScrollY,ImVec2(1244,model.coop?224:382))) {
            ImGui::TableSetupColumn("PLAYER",ImGuiTableColumnFlags_WidthStretch,2.2f);
            ImGui::TableSetupColumn("CHARACTER",ImGuiTableColumnFlags_WidthStretch,1.8f);
            if(model.coop) {ImGui::TableSetupColumn("STATUS");ImGui::TableSetupColumn("PING MS");}
            else {
                ImGui::TableSetupColumn("PTS",ImGuiTableColumnFlags_WidthFixed,68);
                ImGui::TableSetupColumn("KILLS",ImGuiTableColumnFlags_WidthFixed,94);
                ImGui::TableSetupColumn("LOSSES",ImGuiTableColumnFlags_WidthFixed,110);
                ImGui::TableSetupColumn("PING MS",ImGuiTableColumnFlags_WidthFixed,128);
                ImGui::TableSetupColumn("STATUS",ImGuiTableColumnFlags_WidthStretch,1.7f);
            }
            if(kicks)ImGui::TableSetupColumn("HOST",ImGuiTableColumnFlags_WidthFixed,104);
            ImGui::TableHeadersRow();
            for(int i=0;i<model.count;i++) {
                const auto&p=model.players[i];ImGui::TableNextRow(0,40);
                ImGui::TableNextColumn();ImGui::Text("%.14s%s",p.name,p.host?" *":"");
                if(ImGui::IsItemHovered())gevrTooltip("%s%s",p.name,p.host?" (Host)":"");
                ImGui::TableNextColumn();ImGui::TextUnformatted(p.character);
                if(model.coop) {ImGui::TableNextColumn();ImGui::TextColored(p.down?bad:good,"%s",p.down?"Down / revive":"Active");ImGui::TableNextColumn();ImGui::Text("%d",p.ping);}
                else {
                    ImGui::TableNextColumn();ImGui::TextColored(gold,"%d",p.points);
                    ImGui::TableNextColumn();ImGui::Text("%d",p.kills);
                    ImGui::TableNextColumn();ImGui::Text("%d",p.losses);
                    ImGui::TableNextColumn();if(p.bot)ImGui::TextUnformatted("—");else ImGui::Text("%d",p.ping);
                    ImGui::TableNextColumn();ImGui::TextColored(p.ready?good:ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled),"%s",!p.loaded?"Loading":p.host?"Host":p.bot?"Bot":p.ready?(p.spectator?"Ready (S)":"Ready"):(p.spectator?"Spectator":"Waiting"));
                }
                if(kicks) {
                    ImGui::TableNextColumn();ImGui::PushID(p.slot);
                    if(!p.host && p.slot>=0) {
                        ImGui::BeginDisabled(!p.canKick);
                        if(ImGui::Button("Kick",ImVec2(88,32))) {
                            ui.kickSlot=p.slot;snprintf(ui.kickName,sizeof(ui.kickName),"%s",p.name);
                        }
                        ImGui::EndDisabled();
                        if(!p.canKick && !p.bot && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))gevrTooltip("This player needs the updated build for host removal.");
                    }
                    ImGui::PopID();
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
    } else if(ui.tab==GEVR_PAUSE_SETTINGS) {
        ImGui::SetCursorPos(ImVec2(18,184));
        ImGui::BeginChild("##pause-settings",ImVec2(1244,612),false,ImGuiWindowFlags_NoScrollbar);
        gevrSettingsTabs(model.settings,ui.openSection);
        ImGui::EndChild();
        if(model.texpack>=0) {
            ImGui::SetCursorPos(ImVec2(18,806));
            if(ImGui::Button(model.texpack?"HD textures off":"HD textures on",ImVec2(300,48)))action=GEVR_PAUSE_TEXPACK;
            if(ImGui::IsItemHovered())gevrTooltip("Switches the texture pack for this session.");
        }
        /* #229: sound gone bit-crushed with an echo comes back with a new output stream */
        ImGui::SetCursorPos(ImVec2(336,806));
        if(ImGui::Button("Reset sound",ImVec2(330,48)))action=GEVR_PAUSE_RESET_SOUND;
        if(ImGui::IsItemHovered())gevrTooltip("If the sound turns crunchy or echoes, this restarts it.");
    } else {
        /* Rules, Player, Audio: the rows in one or two columns, as every settings page; none scrolls */
        ImGui::SetCursorPos(ImVec2(18,184));
        ImGui::BeginChild("##pause-rows",ImVec2(1244,620),false,ImGuiWindowFlags_NoScrollbar);
        gevrSettingsSection(rows,ui.tab,GEVR_SET_INGAME);
        ImGui::EndChild();
        ImGui::SetCursorPos(ImVec2(18,816));
        ImGui::TextDisabled("%s",ui.tab==GEVR_PAUSE_AUDIO?"Volumes and microphone are yours. The host chooses voice mode.":ui.tab==GEVR_PAUSE_RULES?"Movement speed applies immediately. Other round rules and fun options apply on the next load.":model.coop?"Your character and status. Teammates continue while this window is open.":"Your character, team and loadout. The match continues while this window is open.");
    }
    ImGui::SetCursorPos(ImVec2(18,864));ImGui::Separator();
    ImGui::SetCursorPos(ImVec2(18,886));
    if(ImGui::Button("Resume",ImVec2(244,52)))action=GEVR_PAUSE_RESUME;
    if(ui.focusResume){ImGui::SetItemDefaultFocus();ImGui::SetKeyboardFocusHere(-1);ui.focusResume=false;}
    if(!model.coop) {
        ImGui::SetCursorPos(ImVec2(282,886));
        if(model.host) {char start[64];snprintf(start,sizeof(start),model.countdown>0?"Starting in %d":model.soloWarmup?"Restart warmup":model.startRequested?"Waiting for ready":!model.rosterReady?"Request ready":model.warmup?"Start match":"Next warmup",model.countdown);ImGui::BeginDisabled(!model.canStart || model.countdown>0);if(ImGui::Button(start,ImVec2(300,52)))gevrPauseAction("START MATCH");ImGui::EndDisabled();}
        else if(ImGui::Button(model.localReady?"Unready":"Ready up",ImVec2(300,52))) {
            GevrPauseField f;for(int i=0;gevrPauseReadField(GEVR_PAUSE_PLAYER,i,&f);i++)if(!strcmp(f.label,"NEXT ROUND READY"))gevrPauseStep(f.id,1);
        }
        ImGui::SetCursorPos(ImVec2(600,886));ImGui::BeginDisabled(!model.canReturn);
        if(ImGui::Button(model.countdown>0 || model.startRequested?"Cancel start":"Return to lobby",ImVec2(330,52)))gevrPauseAction("RETURN TO LOBBY");ImGui::EndDisabled();
    } else if(model.horde && model.host) {
        ImGui::SetCursorPos(ImVec2(600,886));
        ImGui::BeginDisabled(!model.hordeDefeated);
        if(ImGui::Button("Retry Horde",ImVec2(330,52)))action=GEVR_PAUSE_HORDE_RETRY;
        ImGui::EndDisabled();
    } else if(model.host) {ImGui::SetCursorPos(ImVec2(600,886));if(ImGui::Button("End mission...",ImVec2(330,52))) {ui.confirmAbort=true;ImGui::OpenPopup("End mission?");}}
    ImGui::SetCursorPos(ImVec2(948,886));ImGui::PushStyleColor(ImGuiCol_Text,bad);
    if(ImGui::Button(model.coop?"Leave party...":"Leave match...",ImVec2(314,52))) {ui.confirmLeave=true;ImGui::OpenPopup("Leave session?");}
    ImGui::PopStyleColor();
    if(ui.kickSlot>=0 && !ImGui::IsPopupOpen("Kick player?"))ImGui::OpenPopup("Kick player?");
    if(ImGui::BeginPopupModal("Kick player?",nullptr,ImGuiWindowFlags_AlwaysAutoResize)) {
        bool present=false;
        for(int i=0;i<model.count;i++)if(model.players[i].slot==ui.kickSlot && !model.players[i].host && model.players[i].canKick &&
            !strcmp(model.players[i].name,ui.kickName))present=true;
        ImGui::Text("Remove %s from this session?",ui.kickName);
        if(ImGui::Button("Cancel",ImVec2(220,52))){ui.kickSlot=-1;ImGui::CloseCurrentPopup();}
        ImGui::SameLine();ImGui::BeginDisabled(!model.host || !present);
        if(ImGui::Button("Kick player",ImVec2(220,52))){action=GEVR_PAUSE_KICK_BASE+ui.kickSlot;ui.kickSlot=-1;ImGui::CloseCurrentPopup();}
        ImGui::EndDisabled();ImGui::EndPopup();
    }
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
