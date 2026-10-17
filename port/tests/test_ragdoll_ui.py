"""Measure the Game rules page, ragdoll toggles included, at the Quest font scale."""
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
launcher = (ROOT / "port/vr/vr_launcher.cpp").read_text()


def page(name):
    start = launcher.index("} else if (" + name + ") {") + len("} else if (" + name + ") ")
    depth, end = 1, start + 1
    while depth:
        depth += (launcher[end] == "{") - (launcher[end] == "}")
        end += 1
    return "void draw_" + name + "() " + launcher[start:end]


fixture = r'''
#include "imgui/imgui_internal.h"
#include "gevr_tooltip.h"
#include <cassert>
#include <cstdio>
static int VrRagdolls,VrDismemberment,VrBodiesStay,VrMinesStickToGuards,VrFastReinforcements,VrDismemberChance=40;
static float VrDismemberDamage=1.0f;
static bool rulesPage,online;
static ImVec4 gold(1,.8f,.3f,1);
enum { NET_MODE_COOP=1,NET_COOP_FAST_REINFORCEMENTS=1,CFG_FAST_REINFORCEMENTS=1 };
struct Match {int mode,fun_flags;} match;
bool netIsActive(){return online;} bool netIsHost(){return true;}
const Match *netGetMatchConfig(){return &match;}
void gevrNetConfigSet(int,int){} void vrSettingsSave(){}
/* PAGES */
int main(){
 ImGui::CreateContext();auto &io=ImGui::GetIO();io.DisplaySize=ImVec2(1280,960);
 io.DeltaTime=1.f/72;io.IniFilename=nullptr;io.LogFilename=nullptr;
 io.FontGlobalScale=2.2f;ImGui::GetStyle().ScaleAllSizes(2.2f);
 unsigned char *pixels;int w,h;io.Fonts->GetTexDataAsRGBA32(&pixels,&w,&h);
 for(int enabled=0;enabled<2;enabled++)for(int net=0;net<2;net++){
  VrRagdolls=VrDismemberment=enabled;VrBodiesStay=48;online=net;match.mode=NET_MODE_COOP;
  for(int pass=0;pass<3;pass++){
   ImGui::NewFrame();ImGui::SetNextWindowPos(ImVec2(0,0));ImGui::SetNextWindowSize(io.DisplaySize);
   ImGui::Begin("Launcher",nullptr,ImGuiWindowFlags_NoTitleBar|ImGuiWindowFlags_NoSavedSettings);
   ImGui::Dummy(ImVec2(0,ImGui::GetTextLineHeight()*2.2f+ImGui::GetStyle().ItemSpacing.y));
   ImGui::Separator();ImGui::Button("ROM: ok",ImVec2(-1,0));ImGui::Separator();
   draw_rulesPage();
   ImGui::TextDisabled("Point and pull the trigger, or use the stick and A.");
   auto *win=ImGui::GetCurrentWindow();
   if(pass==2){
    if(win->DC.CursorMaxPos.y>win->ClipRect.Max.y || win->DC.CursorMaxPos.x>win->ClipRect.Max.x){
     std::fprintf(stderr,"rules page overflow %.1f %.1f, limit %.1f %.1f\n",win->DC.CursorMaxPos.x,win->DC.CursorMaxPos.y,win->ClipRect.Max.x,win->ClipRect.Max.y);return 1;
    }
   }
   ImGui::End();ImGui::Render();
  }
 }
 ImGui::DestroyContext();puts("PASS: the Game rules page fits the Quest launcher with the ragdoll toggles and all body-count controls");
}
'''

with tempfile.TemporaryDirectory(prefix="gevr-ragdoll-ui-") as directory:
    source, executable = Path(directory) / "test.cpp", Path(directory) / "test"
    source.write_text(fixture.replace("/* PAGES */", page("rulesPage")))
    subprocess.run([shutil.which("g++") or "g++", "-std=c++17", "-O2", "-Iport/vr", str(source),
                    *["port/vr/imgui/" + name for name in
                      ("imgui.cpp", "imgui_draw.cpp", "imgui_tables.cpp", "imgui_widgets.cpp")],
                    "-o", str(executable)], cwd=ROOT, check=True)
    subprocess.run([str(executable)], check=True)
