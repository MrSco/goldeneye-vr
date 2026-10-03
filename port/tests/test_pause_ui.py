"""Headless checks against production pause widgets, input gate and bundled ImGui."""
from pathlib import Path
import shutil, subprocess, tempfile
root=Path(__file__).resolve().parents[2]
source=(root/'port/src/input.c').read_text(encoding='utf-8')
start=source.index('    {',source.index('    /* Menu owns A/B, triggers and sticks.'))
end=source.index('\n#endif',start)
fixture=(root/'port/tests/pause_input_native.cpp').read_text(encoding='utf-8')
with tempfile.TemporaryDirectory(prefix='gevr-pause-input-') as temp:
    cpp=Path(temp)/'pause_input_native.cpp'
    exe=Path(temp)/'pause_input_native.exe'
    cpp.write_text(fixture.replace('/* INSERT_PAUSE_INPUT */',source[start:end]),encoding='utf-8')
    subprocess.run([shutil.which('g++') or 'g++','-std=c++17','-O2','-I'+str(root/'port/vr'),str(cpp),'-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True)
radar=(root/'src/game/radar.c').read_text(encoding='utf-8')
radar_start=radar.index('void gevrPauseLocalRadar(')
radar_end=radar.index('\n#endif',radar_start)
fixture=(root/'port/tests/pause_radar_native.cpp').read_text(encoding='utf-8')
with tempfile.TemporaryDirectory(prefix='gevr-pause-radar-') as temp:
    cpp=Path(temp)/'pause_radar_native.cpp'
    exe=Path(temp)/'pause_radar_native.exe'
    cpp.write_text(fixture.replace('/* INSERT_PAUSE_RADAR */',radar[radar_start:radar_end]),encoding='utf-8')
    subprocess.run([shutil.which('g++') or 'g++','-std=c++17','-O2','-I'+str(root/'port/vr'),str(cpp),'-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True)
glass=(root/'src/game/glass2.c').read_text(encoding='utf-8')
gauge_start=glass.index('void hudMakeDamageSegments',glass.index('#if !defined(LEFTOVERDEBUG)'))
gauge_end=glass.index('\n#endif',gauge_start)
menu=(root/'src/game/mpmenu.c').read_text(encoding='utf-8')
bridge_start=menu.index('void gevrPauseLocalGauges(')
bridge_end=menu.index('\nvoid gevrPausePlayerStats',bridge_start)
visibility_start=menu.index('s32 mpwatchShouldDisplayGauges(void)')
visibility_end=menu.index('\ns32 checkGamePaused',visibility_start)
with tempfile.TemporaryDirectory(prefix='gevr-pause-ui-') as temp:
    output=Path(temp)/'pause_ui_native.exe'
    gauges=Path(temp)/'pause_gauges.cpp'
    gauges.write_text('''#include "gevr_pause_menu.h"
#include <cmath>
using s32=int;using s16=short;using s8=signed char;using f32=float;using f64=double;
#define M_PI_F 3.14159265358979323846f
#define MAX_PLAYER_COUNT 8
#define GEVR
#define ANDROID
#define FALSE 0
#define MENU_STATUS 0
struct damage_display_val {
    struct {short x,y,z;} pos,normal;
    struct {unsigned char r,g,b,a;} colour;
};
struct Player {float bondhealth,bondarmour;int mpmenuon,mpmenumode,healthdisplaytime;};
static Player localPlayer;
static Player* g_CurrentPlayer=&localPlayer;
static Player* g_playerPointers[8];
static int localSlot,nativeOpen,g_gameOverFlag;
extern "C" int gevrNativePauseOpen(){return nativeOpen;}
extern "C" int testPauseGaugeVisibility(int open,int menu,int mode,int gameOver,int healthTime);
int netGetLocalSlot(){return localSlot;}
extern "C" void testPauseGauges(float health,float armour,int slot){
    for(auto& p:g_playerPointers)p=nullptr;
    localSlot=slot;localPlayer={health,armour};
    if(slot>=0 && slot<8)g_playerPointers[slot]=&localPlayer;
}
''' + glass[gauge_start:gauge_end]+'\n'+menu[bridge_start:bridge_end]+'\n'+menu[visibility_start:visibility_end]+'''
extern "C" int testPauseGaugeVisibility(int open,int menu,int mode,int gameOver,int healthTime){
    nativeOpen=open;localPlayer.mpmenuon=menu;localPlayer.mpmenumode=mode;
    g_gameOverFlag=gameOver;localPlayer.healthdisplaytime=healthTime;
    return mpwatchShouldDisplayGauges();
}
''',encoding='utf-8')
    args=[shutil.which('g++') or 'g++','-std=c++17','-O2','-Iport/vr','port/tests/pause_ui_native.cpp',str(gauges)]
    args += ['port/vr/imgui/'+s for s in ('imgui.cpp','imgui_draw.cpp','imgui_tables.cpp','imgui_widgets.cpp')]
    subprocess.run(args+['-o',str(output)],cwd=root,check=True)
    subprocess.run([str(output)],cwd=root,check=True)
