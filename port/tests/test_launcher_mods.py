"""Measure the actual Mods page at the Quest launcher size and font scale."""
from pathlib import Path
import shutil, subprocess, tempfile
root = Path(__file__).resolve().parents[2]
source = (root / 'port/vr/vr_launcher.cpp').read_text()
start = source.index('struct ModPack {')
end = source.index('// Names: printable ASCII', start)
page = source[start:end]
split_start = source.index('static std::vector<std::string> gevrSplit(')
split_end = source.index('// Issue #25: the Mods page.', split_start)
fixture = r'''
#include "gevr_launcher_ui.h"
#include "imgui/imgui_internal.h"
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <vector>
#include <sys/stat.h>
using Uint32 = unsigned;
static char g_ActiveExtTexPack[256];
static int VrGexGuns=1, bean;
static bool available;
static std::string raw;
static unsigned clockMs;
static void gevrJavaCommand(const char*,const char*) {}
static std::string gevrJavaString(const char*) {return raw;}
static void gevrBeanInit() {}
static int gevrBeanIsEnabled() {return bean;}
static int gevrBeanIsAvailable() {return available;}
static void gevrBeanSetEnabled(int v) {bean=v;}
static void vrSettingsSave() {}
static std::string gevrTakePickResult() {return {};}
static int fsFileSize(const char*) {return 0;}
static std::string dataDir() {return "this-fixture-has-no-ROM";}
static bool gevrOpenPicker(const char*) {return false;}
static void vr_log(const char*,...) {}
/* PRODUCTION */
int main(){
    ImGui::CreateContext();auto &io=ImGui::GetIO();io.DisplaySize=ImVec2(1280,960);
    io.DeltaTime=1.f/72;io.IniFilename=nullptr;io.LogFilename=nullptr;
    io.FontGlobalScale=2.2f;ImGui::GetStyle().ScaleAllSizes(2.2f);
    unsigned char *pixels;int w,h;io.Fonts->GetTexDataAsRGBA32(&pixels,&w,&h);
    for(const char* state : {"none","installed","downloading","installing","error"})
    for(int update=0;update<2;update++)for(bean=0;bean<2;bean++){
        available=bean;raw.clear();
        for(const char *id : {"ge007-hd","ge007-hd-ai","ge007-bean"})
            raw+=std::string(id)+"\tPack\tCredit\tgithub.com/credit\t2026.10.11.1\t374\t"+state+"\t100\t"+
                (!strcmp(state,"error")?"A long download failure message that exceeds the column width and must stay on one line.":"")+"\t"+(update?"2026.10.11.2":"")+"\n";
        for(int pass=0;pass<3;pass++){
            ImGui::NewFrame();ImGui::SetNextWindowPos(ImVec2(0,0));ImGui::SetNextWindowSize(io.DisplaySize);
            ImGui::Begin("Launcher",nullptr,ImGuiWindowFlags_NoTitleBar|ImGuiWindowFlags_NoSavedSettings);
            ImGui::Dummy(ImVec2(0,ImGui::GetTextLineHeight()*2.2f+ImGui::GetStyle().ItemSpacing.y));
            ImGui::Separator();ImGui::Button("ROM: ok",ImVec2(-1,0));ImGui::Separator();
            bool open=true;clockMs+=300;
            gevrModsPage(open,clockMs,ImVec4(1,.8f,.3f,1),ImVec4(.5f,1,.5f,1),ImVec4(1,.5f,.4f,1));
            auto *win=ImGui::GetCurrentWindow();
            if(pass==2 && (win->DC.CursorMaxPos.y>win->ClipRect.Max.y || win->DC.CursorMaxPos.x>win->ClipRect.Max.x)){
                std::fprintf(stderr,"Mods overflow: %s update%d bean%d extent %.1f %.1f limit %.1f %.1f\n",state,update,bean,win->DC.CursorMaxPos.x,win->DC.CursorMaxPos.y,win->ClipRect.Max.x,win->ClipRect.Max.y);return 1;
            }
            ImGui::End();ImGui::Render();
        }
    }
    ImGui::DestroyContext();puts("PASS: production Mods page, including Bean, progress, errors and updates, fits at Quest font size without scrolling");
}
'''
with tempfile.TemporaryDirectory(prefix='gevr-mods-ui-') as d:
    generated=Path(d)/'mods.cpp';out=Path(d)/'mods.exe'
    generated.write_text(fixture.replace('/* PRODUCTION */',source[split_start:split_end]+page))
    subprocess.run([shutil.which('g++') or 'g++','-std=c++17','-O2','-Iport/vr',str(generated),
        *['port/vr/imgui/'+f for f in ('imgui.cpp','imgui_draw.cpp','imgui_tables.cpp','imgui_widgets.cpp')],'-o',str(out)],cwd=root,check=True)
    subprocess.run([str(out)],cwd=root,check=True)
