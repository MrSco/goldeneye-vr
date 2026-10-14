"""Production checks for texture ownership, live toggles and multiplayer hit HUD."""
from pathlib import Path
import shutil
import re
import os
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]


def function(source, signature):
    match = re.search(re.escape(signature) + r"[^;{]*\{", source)
    assert match, signature
    start, end = match.start(), match.end()
    depth = 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]


pack = (ROOT / "port/fast3d/gevr_texpack.cpp").read_text()
index = pack[pack.index("static bool parseHex("):pack.index("// halve (2x2 box)")]
if os.name == "nt":
    # MinGW dirent lacks d_type; exercise the production stat fallback.
    index = "#define DT_UNKNOWN 0\n#define DT_DIR 4\n" + index.replace("de->d_type", "DT_UNKNOWN")
find = function(pack, "int find(")
gfx = (ROOT / "port/fast3d/gfx_pc.cpp").read_text()
tp_find = function(gfx, "static int tp_find(")
mode = gfx[gfx.index("    const int bean = gevrBeanIsEnabled();"):gfx.index("    // tools/texai texture dump", gfx.index("    const int bean = gevrBeanIsEnabled();"))]
options = (ROOT / "src/game/options.c").read_text()
step = function(options, "static void gevrVrStep(")
bean_step = step[step.index("    if (row == GEVR_VR_BEAN)"):step.index("    if (row == GEVR_VR_FASTREINF")]
value = function(options, "static void gevrVrValueText(")
bean_value = value[value.index("        case GEVR_VR_BEAN:"):value.index("        case GEVR_VR_MOVESPEED:")]
bond = (ROOT / "src/game/bondview2.c").read_text()
hud_start = bond.index("    if (", bond.index("/* VR_HUD_CAPTURE_BEGIN_H */", bond.index("Stereo: the health and armour arcs")))
hud_end = bond.index("#ifdef GEVR\n    if (g_gevrStereo)", hud_start)
hud = bond[hud_start:hud_end]
damage_start = bond.index("                if (g_CurrentPlayer->damageshowtime < ZERO_7F08991C")
damage = bond[damage_start:bond.index("#undef ZERO_7F08991C", damage_start)]
health = function(bond, "int bondviewGetIfCurrentPlayerHealthShowTime(void)")
update_start = bond.index("    if (g_CurrentPlayer->healthshowtime >= 0)")
update = function(bond[update_start:], "    if (g_CurrentPlayer->healthshowtime >= 0)")
hook_source = (ROOT / "src/game/objecthandler_2.c").read_text()
hook = hook_source[hook_source.index("    if (!gevrGexPendingFile) gevrBeanModelApply"):].split("\n", 1)[0]
input_source = (ROOT / "port/src/input.c").read_text()
shortcut_start = input_source.index("        {", input_source.index("/* One grip (not both:"))
# This block shares the same state machine each tick; extract its balanced braces.
end, depth = shortcut_start + 1 + input_source[shortcut_start:].index("{"), 1
while depth:
    depth += (input_source[end] == "{") - (input_source[end] == "}")
    end += 1
shortcut = input_source[shortcut_start:end]

fixture = r'''
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <vector>
#include <string>
#include <unordered_map>
#include <dirent.h>
#include <sys/stat.h>
#include <cerrno>
#ifdef _WIN32
#define strcasecmp _stricmp
#endif
#define TPLOG(...) ((void)0)
#define GEVR
#define VERSION_US
#define TRUE 1
#define FALSE 0
#define ZERO_7F08991C 0
#define CHEAT_NO_RADAR_MP 1
#define LOGI(...) ((void)0)
using s32=int; using u32=unsigned; using f32=float;
struct Entry {std::string path;uint8_t fmt,siz;bool bean=false;};
static std::vector<Entry> s_entries;
static std::unordered_map<uint64_t,std::vector<int>> s_index;
static bool s_ready=true;
/* INDEX */
/* FIND */
static bool enabled=false, available=true;
int gevrBeanIsEnabled(){return enabled;}
int gevrBeanIsAvailable(){return available;}
void gevrBeanSetEnabled(int on){enabled=on;}
void gevrBeanToggle(){enabled=!enabled;}
static int clears=0,lastBean=-1,lastFill=-1,s_tpUserOff=0,VrBeanFillTextures=1;
static bool s_tpFillOnly=false;
namespace gevrtp { int find(uint64_t k,uint8_t f,uint8_t s,bool b){return ::find(k,f,s,b);} }
/* TPFIND */
static bool s_tpActive=true,s_tpIndexed=true;
static std::vector<int> s_tpUploads,s_tpPending;
void gfx_texture_cache_clear(){clears++;}
void sysLogPrintf(int,const char*,...){}
#define LOG_NOTE 0
void tick(){ /* MODE */ }
enum {GEVR_VR_BEAN};
void watchStep(int dir){int row=GEVR_VR_BEAN; /* STEP */ }
void watchValue(char* buf){switch(GEVR_VR_BEAN){ /* VALUE */ }}
static int loads=0;static void* gevrGexPendingFile=nullptr;
void gevrBeanModelApply(void*,const char*){loads++;}
void apply(){void* objheader=nullptr;const char* name="Gak47Z"; /* HOOK */ }
static bool grips[2]; bool get_button_state(int hand,const char*){return grips[hand];}
void vrSettingsSave(){}
void shortcutTick(unsigned now,bool rclick){ /* SHORTCUT */ }
struct Player {int healthshowtime,damageshowtime,healthdamagetype,bonddead,watch_animation_state,mpmenuon,healthdisplaytime;
    float bondhealth,bondarmour,oldhealth,oldarmour,apparenthealth,apparentarmour;struct {float x,z;} bondshotspeed;} player;
static Player* g_CurrentPlayer=&player;
static bool network=true,radar=true,standard=true;
static int VrNoKnockback,VrNoHitstun,VrDamageFlash,VrWatchFaceStatus,g_ClockTimer=1,g_gevrStereo=1,gauges=0;
static struct {int validStartFrame,updateToRealHealthFrame,hideHealthFrame;} g_HealthDisplayDurations[8];
float currentPlayerGetHealth(){return player.bondhealth;}
bool netIsActive(){return network;}
bool cheatIsActive(int){return !radar;}
bool mpwatchShouldDisplayGauges(){return false;}
bool gevrWatchShowsStandard(int,bool,int){return standard;}
int bondviewRenderGaugeBars(int gdl){gauges++;return gdl;}
/* HEALTH */
void hit(){float vectorx=1,vectorz=1; /* DAMAGE */ }
void update(){ /* UPDATE */ }
void draw(){int gdl=0; /* HUD */ }
int main(int argc,char** argv){
    scan(argv[1],s_entries,s_index,false);scan(argv[2],s_entries,s_index,true);
    assert(s_entries.size()==4);
    assert(find(0x12345678,0,2,false)>=0 && find(0x12345678,0,2,true)>=0);
    assert(find(0x12345678,0,2,false)!=find(0x12345678,0,2,true));
    assert(find(0x11111111,0,2,true)==-1); // Bean never borrows an HD-only entry
    assert(find(0x22222222,0,2,false)==-1);
    tick();int baseline=clears;s_tpUploads={1};s_tpPending={2};enabled=true;tick();
    assert(clears==baseline+1 && s_tpUploads.empty() && s_tpPending.empty());tick();assert(clears==baseline+1);
    enabled=false;tick();assert(clears==baseline+2);
    /* the fill: an HD pack indexed for Bean's look answers only with Bean on and the
       option on, for what Bean has no picture of; a chosen HD pack as before */
    s_tpFillOnly=true;assert(tp_find(0x11111111,0,2)==-1);
    enabled=true;assert(tp_find(0x11111111,0,2)>=0);
    assert(tp_find(0x12345678,0,2)==find(0x12345678,0,2,true));   // Bean's own entry first
    VrBeanFillTextures=0;assert(tp_find(0x11111111,0,2)==-1);
    tick();assert(clears==baseline+3);   // the option switched: every texture looks again
    s_tpFillOnly=false;enabled=false;VrBeanFillTextures=1;tick();
    assert(tp_find(0x11111111,0,2)>=0);
    baseline=clears;
    char value[64];available=false;watchStep(1);assert(!enabled);watchValue(value);assert(!strcmp(value,"NEEDS FILES"));
    available=true;watchStep(1);assert(enabled);watchValue(value);assert(!strcmp(value,"ON"));watchStep(2);assert(!enabled);
    apply();assert(loads==1);gevrGexPendingFile=&player;apply();assert(loads==1);
    grips[0]=true;shortcutTick(100,true);shortcutTick(1099,true);assert(!enabled);shortcutTick(1100,true);assert(enabled);
    shortcutTick(4000,true);assert(enabled);shortcutTick(4001,false);grips[1]=true;shortcutTick(5000,true);shortcutTick(7000,true);assert(enabled);
    for(auto& d:g_HealthDisplayDurations)d={0,5,20};
    for(int bits=0;bits<8;bits++)for(int face=0;face<3;face++)for(int r=0;r<2;r++){
        VrNoKnockback=bits&1;VrNoHitstun=(bits>>1)&1;VrDamageFlash=(bits>>2)&1;
        VrWatchFaceStatus=face;standard=face!=2;radar=r;
        player={};player.bondhealth=.5f;player.oldhealth=.75f;player.oldarmour=.5f;player.bondarmour=.25f;
        player.healthshowtime=player.damageshowtime=-1;hit();assert(player.healthshowtime==0);
        assert(player.bondshotspeed.x==(VrNoKnockback?0:2));update();gauges=0;draw();assert(gauges==1);
        assert(player.apparenthealth==.75f && player.apparentarmour==.5f);
        for(int i=0;i<8;i++)update();assert(player.apparenthealth==.5f && player.apparentarmour==.25f);
        for(int i=0;i<30;i++)update();gauges=0;draw();assert(gauges==0);
    }
    puts("PASS: Bean ownership, cache invalidation, watch/shortcut, GE-X priority and all hit comfort/HUD combinations");
}
'''
for marker, source in {"INDEX": index, "FIND": find, "TPFIND": tp_find, "MODE": mode, "STEP": bean_step,
                       "VALUE": bean_value, "HUD": hud, "DAMAGE": damage,
                       "HEALTH": health, "UPDATE": update, "HOOK": hook, "SHORTCUT": shortcut}.items():
    fixture = fixture.replace(f"/* {marker} */", source)
with tempfile.TemporaryDirectory(prefix="gevr-bean-modes-") as temp:
    temp = Path(temp)
    for owner, crcs in (("hd", (0x12345678, 0x11111111)), ("bean", (0x12345678, 0x22222222))):
        folder = temp / owner
        folder.mkdir()
        for crc in crcs:
            (folder / f"GOLDENEYE#{crc:08X}#0#2_all.png").touch()
    cpp, exe = temp / "modes.cpp", temp / "modes.exe"
    cpp.write_text(fixture)
    subprocess.run([shutil.which("g++") or "g++", "-std=c++17", "-O2", str(cpp), "-o", str(exe)], check=True)
    subprocess.run([str(exe), str(temp / "hd"), str(temp / "bean")], check=True)
