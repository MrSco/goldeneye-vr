"""Headless interaction with the production Mods page and bundled ImGui."""
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
source = (ROOT / "port/vr/vr_launcher.cpp").read_text()
start = source.index("static std::vector<std::string> gevrSplit(")
end = source.index("// Names: printable ASCII", start)
page = source[start:end].replace("ImGui::RadioButton(", "radio(").replace("ImGui::Button(label)", "download(label)")
fixture = r'''
#include "gevr_launcher_ui.h"
#include "imgui/imgui_internal.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <sys/stat.h>
using Uint32=unsigned;
static char g_ActiveExtTexPack[256]={};
static bool enabled=false,have=true,installed=true;
static int VrGexGuns=1; static int VrCampaign=0; /* GoldenEye; hack campaigns grey out Bean and GE-X */
static int VrBeanFillTextures=1, VrBloodPatch=0;
static int VrOddjobHat=0;          /* Mods: Oddjob's hat (PR #196), drawn when its pack is listed */
static int gevrHatAvailable() {return 1;}
#define GEVR_CAMPAIGN_GE 0
#define GEVR_CAMPAIGN_MIRROR 3
#define GEVR_CAMPAIGN_COUNT 4
static int gevrCampaignRetailArt(int c){return c==GEVR_CAMPAIGN_GE||c==GEVR_CAMPAIGN_MIRROR;}
static std::vector<std::string> commands;
static ImVec2 bean,hd,original,chooseBean;
int gevrBeanIsEnabled(){return enabled&&have;}
int gevrBeanIsAvailable(){return have;}
void gevrBeanInit(){}
void gevrBeanSetEnabled(int on){enabled=on;}
void vrSettingsSave(){}
int fsFileSize(const char*){return 0;}
bool gevrOpenPicker(const char* method){commands.emplace_back(method);return true;}
std::string gevrTakePickResult(){return "";}
std::string dataDir(){return "missing-test-data";}
void vr_log(const char*,...){}
void gevrJavaCommand(const char*,const char* cmd){commands.emplace_back(cmd);}
std::string gevrJavaString(const char*){
 std::string s;
 for(const char* id:{"ge007-hd","ge007-hd-ai","ge007-bean"}){
  if(!s.empty())s+='\n';
  std::string title=std::string(id)=="ge007-bean"?"GoldenEye 007 XBLA / Project Bean":id;
  s+=std::string(id)+"\t"+title+"\tCredit\tgithub.com\t2026.10.11.1\t"+(std::string(id)=="ge007-bean"?"173":"100")+"\t"+(installed?"installed":"none")+"\t-1\t\t";
 }
 return s;
}
bool radio(const char* label,bool selected){
 bool clicked=ImGui::RadioButton(label,selected);
 ImVec2 point=(ImGui::GetItemRectMin()+ImGui::GetItemRectMax())*.5f;
 if(!strcmp(label,"XBLA / Bean"))bean=point; /* the page shows short names */
 if(!strcmp(label,"HD + AI"))hd=point;
 if(!strcmp(label,"Original"))original=point;
 return clicked;
}
bool download(const char* label){
 bool clicked=ImGui::Button(label);
 if(strstr(label,"copy"))chooseBean=(ImGui::GetItemRectMin()+ImGui::GetItemRectMax())*.5f;
 return clicked;
}
/* PAGE */
static unsigned now=1000;
void frame(){
 ImGui::NewFrame();
 ImGui::SetNextWindowPos(ImVec2(0,0));ImGui::SetNextWindowSize(ImVec2(1280,1800));
 ImGui::Begin("Mods test",nullptr,ImGuiWindowFlags_NoSavedSettings|ImGuiWindowFlags_NoTitleBar);
 bool open=true;gevrModsPage(open,now+=300,ImVec4(1,1,0,1),ImVec4(0,1,0,1),ImVec4(1,0,0,1));
 ImGui::End();ImGui::Render();
}
void click(ImVec2 at){
 auto& io=ImGui::GetIO();io.AddMousePosEvent(at.x,at.y);frame();
 io.AddMouseButtonEvent(0,true);frame();io.AddMouseButtonEvent(0,false);frame();
}
int main(){
 ImGui::CreateContext();auto& io=ImGui::GetIO();io.IniFilename=nullptr;io.DisplaySize=ImVec2(1280,1800);io.DeltaTime=1.f/60;
 unsigned char* pixels;int w,h;io.Fonts->GetTexDataAsRGBA32(&pixels,&w,&h);
 io.FontGlobalScale=2.2f;ImGui::GetStyle().ScaleAllSizes(2.2f);
 frame();frame();assert(bean.y>0 && bean.x<1280 && bean.y<1800);
 click(bean);assert(enabled && !strcmp(g_ActiveExtTexPack,"ge007-bean") && VrGexGuns==1);
 click(hd);assert(!enabled && !strcmp(g_ActiveExtTexPack,"ge007-hd-ai"));
 click(bean);click(original);assert(!enabled && !*g_ActiveExtTexPack);
 installed=false;have=false;frame();click(chooseBean);
 assert(commands.back()=="openXblaPicker"); /* never a download: the player's own copy */
 for(const std::string& c:commands)assert(c!="install:ge007-bean");
 ImGui::DestroyContext();puts("PASS: Mods offers Bean from the player's own copy, exclusive selection and preserves GE-X choice at Quest font scale");
}
'''.replace("/* PAGE */", page)
with tempfile.TemporaryDirectory(prefix="gevr-bean-launcher-") as temp:
    cpp, exe = Path(temp) / "mods.cpp", Path(temp) / "mods.exe"
    cpp.write_text("#define IMGUI_DEFINE_MATH_OPERATORS\n" + fixture)
    subprocess.run([shutil.which("g++") or "g++", "-std=c++17", "-O2", "-I" + str(ROOT / "port/vr"), str(cpp),
                    *[str(ROOT / "port/vr/imgui" / file) for file in ("imgui.cpp", "imgui_draw.cpp", "imgui_tables.cpp", "imgui_widgets.cpp")],
                    "-o", str(exe)], check=True)
    subprocess.run([str(exe)], cwd=temp, check=True)
