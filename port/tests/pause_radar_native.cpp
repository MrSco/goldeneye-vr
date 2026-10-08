#include "gevr_pause_menu.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
using f32=float;
#define MAX_PLAYER_COUNT 8
#define M_PI_F 3.14159265358979323846f
#define NET_RADAR_BRIGHT_RANGE 4000.f
#define CHEAT_NO_RADAR_MP 1
enum MPSCENARIOS {SCENARIO_NORMAL,SCENARIO_2v2,SCENARIO_3v1,SCENARIO_2v1,SCENARIO_TLD,SCENARIO_MWTGG};
struct Prop {struct {float f[3];} pos;};
struct player {Prop* prop;int bonddead,mpmenuon;float vv_theta;};
static Prop props[8];
static player players[8];
static player* g_playerPointers[8];
static struct {int have_token_or_goldengun;} g_playerPlayerData[8];
static int localSlot=7,count=8,spectator=-1;
static bool connected=true,noRadar=false,occupied[8],active[8];
static MPSCENARIOS scenario=SCENARIO_NORMAL;
int netGetLocalSlot(){return localSlot;}
int netSpectatorTarget(){return spectator;}
bool netIsActive(){return connected;}
bool netSlotOccupied(int i){return occupied[i];}
bool netIsRemotePlayerActive(int i){return active[i];}
int gevrNetOwnsSlot(int i){return i==localSlot;}   /* the local player, or the host's bot (net_core.c netSlotOwned) */
int getPlayerCount(){return count;}
int cheatIsActive(int){return noRadar;}
MPSCENARIOS get_scenario(){return scenario;}
/* INSERT_PAUSE_RADAR */
static void check(bool ok,const char* message){if(!ok){fprintf(stderr,"FAIL: %s\n",message);exit(1);}}
static bool near(float a,float b){return fabsf(a-b)<.001f;}
int main(){
    for(int i=0;i<8;i++){g_playerPointers[i]=&players[i];players[i].prop=&props[i];occupied[i]=active[i]=true;}
    players[7].mpmenuon=1;props[0].pos.f[2]=2000;
    GevrPauseRadarView radar{};gevrPauseLocalRadar(&radar);
    check(radar.visible && radar.count==8,"all eight radar slots remain live while pause is open");
    check(near(radar.blips[1].x,0) && near(radar.blips[1].y,-.5f),"forward opponent is above local center at half radar range");
    props[0].pos.f[0]=2000;props[0].pos.f[2]=0;gevrPauseLocalRadar(&radar);
    check(near(radar.blips[1].x,-.5f) && near(radar.blips[1].y,0),"moving remote position updates on the next menu frame");
    players[7].vv_theta=90;gevrPauseLocalRadar(&radar);
    check(near(radar.blips[1].x,0) && near(radar.blips[1].y,.5f),"radar rotates with local heading");
    props[0].pos.f[0]=8000;gevrPauseLocalRadar(&radar);
    check(near(std::hypot(radar.blips[1].x,radar.blips[1].y),1) && radar.blips[1].a==96,"distant opponent clamps to rim and dims");
    scenario=SCENARIO_2v2;g_playerPlayerData[7].have_token_or_goldengun=1;g_playerPlayerData[0].have_token_or_goldengun=1;
    gevrPauseLocalRadar(&radar);
    check(radar.blips[0].r==136 && radar.blips[0].b==255,"local team color matches gameplay radar");
    check(radar.blips[1].r==40 && radar.blips[1].b==255 && radar.blips[1].a==176,"far blue team/token colors match gameplay radar");
    check(radar.blips[2].r==255 && radar.blips[2].g==0 && radar.blips[2].a==160,"red team/token colors match gameplay radar");
    occupied[0]=false;active[1]=false;players[2].bonddead=1;players[3].prop=nullptr;spectator=4;g_playerPointers[5]=nullptr;
    gevrPauseLocalRadar(&radar);check(radar.count==2,"absent, inactive, dead, missing-prop and spectator-target players are hidden");
    noRadar=true;gevrPauseLocalRadar(&radar);check(!radar.visible && !radar.count,"no-radar rule is respected");noRadar=false;
    players[7].bonddead=1;gevrPauseLocalRadar(&radar);check(!radar.visible,"dead local player cannot gain radar information");players[7].bonddead=0;
    connected=false;gevrPauseLocalRadar(&radar);check(!radar.visible,"disconnected menu has no stale radar");connected=true;
    localSlot=-1;gevrPauseLocalRadar(&radar);check(!radar.visible,"invalid local slot is safe");
    localSlot=3;count=4;players[3].prop=&props[3];gevrPauseLocalRadar(&radar);
    check(radar.visible && radar.count==1,"co-op local slot three reads its own live center");
    puts("PASS: live pause radar movement, heading, range, colors, eight slots, co-op and visibility rules");
}
