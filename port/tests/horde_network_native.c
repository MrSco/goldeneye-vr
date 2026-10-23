#include "net_core.h"
#include "net_horde.h"
#include <assert.h>
#include <string.h>
#include <math.h>

s32 gevrAllyIs(struct ChrRecord *chr) { return FALSE; }
static HordeState state;
static uint32_t epoch=7;
static bool hosting=true;
static unsigned char packet[1024];
static unsigned bytes,applied,observed_item;
void sysLogPrintf(int level,const char *format,...) {(void)level;(void)format;}
bool netIsHost(void) {return hosting;}
int netGetLocalSlot(void) {return 0;}
int netHordeActive(void) {return 1;}
int netHordeRunning(void) {return 1;}
uint32_t netGetWorldEpoch(void) {return epoch;}
u64 sysGetMicroseconds(void) {return 1000000;}
const HordeState *gevrHordeState(void) {return &state;}
void gevrHordeApplyState(const HordeState *s) {state=*s;applied++;}
unsigned gevrHordeGuardId(const struct ChrRecord *chr) {return chr?123:0;}
int gevrHordeHandleAction(int slot, unsigned action, unsigned item) {
    observed_item=item;
    if (action == HORDE_ACT_BOX) return hordeUseBox(&state, (unsigned)slot, 20, 0) != 0;
    if (action == HORDE_ACT_WALL_BUY || action == HORDE_ACT_WALL_AMMO)
        return hordeBuyWall(&state, (unsigned)slot, item & 255, action == HORDE_ACT_WALL_AMMO, 10, 0);
    return 0;
}
void netCoopBroadcast(const unsigned char *p,unsigned size,int reliable) {
    assert(reliable && size<=sizeof(packet));memcpy(packet,p,size);bytes=size;
}
void netCoopSendTo(int slot,const unsigned char *p,unsigned size) {
    assert(slot==2);netCoopBroadcast(p,size,1);
}
static void receive(void) {
    struct netbuf b;netbufStartReadData(&b,packet,bytes);
    assert(netbufReadU32(&b)==GEVR_NET_MAGIC && netbufReadU16(&b)==GEVR_NET_VERSION);
    assert(netbufReadU8(&b)==NET_MSG_HORDE_STATE);netbufReadU8(&b);
    netHordeReceive(&b);
}
static void receive_client(int slot) {
    struct netbuf b;netbufStartReadData(&b,packet,bytes);
    assert(netbufReadU32(&b)==GEVR_NET_MAGIC && netbufReadU16(&b)==GEVR_NET_VERSION);
    assert(netbufReadU8(&b)==NET_MSG_HORDE_STATE);netbufReadU8(&b);
    netHordeReceiveClient(slot,&b);
}
int main(void)
{
    hordeInit(&state,456);
    for(int i=0;i<60;i++)hordeAdvance(&state,15,2);
    hordeSpawned(&state);
    hordeAddCredits(&state,1,600);
    assert(hordeUseBox(&state,1,20,0)==1);
    state.ammo_grants[2]=17;state.guns[3][0]=2;state.guns[3][1]=33;state.grant_replace[1]=10;
    for(unsigned i=0;i<HORDE_AMMO_DROPS;i++)state.ammo[i]=(HordeAmmoDrop){1000+i,1200,{i*10.0f,-20.0f,3.0f}};
    state.instakill_ticks=1700;state.max_ammo_seq=3;state.power_seq=7;state.power_kind=HORDE_POWER_INSTAKILL;
    state.explosives[2][0]=19;state.explosives[3][1]=31;
    for (unsigned i=0;i<HORDE_POWER_DROPS;i++)state.powers[i]=(HordePowerDrop){2000+i,3500,{i*13.0f,1,3},i%HORDE_POWER_COUNT};
    HordeState saved=state;
    netHordeSend(-1);memset(&state,0,sizeof(state));hosting=false;receive();
    assert(applied==1 && !memcmp(&state,&saved,sizeof(state)));
    /* Client sends a Wall Buy request to the host; host validates and broadcasts updated state. */
    netHordeRequestAction(HORDE_ACT_WALL_BUY,0);
    hosting=true;receive_client(0);
    assert(state.credits[0]==0 && state.grant_seq[0]==1 && state.grant_kind[0]==HORDE_GRANT_WALL_WEAPON);
    saved=state;memset(&state,0,sizeof(state));hosting=false;receive();
    assert(applied==2 && !memcmp(&state,&saved,sizeof(state)));
    hosting=false;netHordeRequestAction(HORDE_ACT_AMMO_PICKUP,0x12345678u);
    hosting=true;receive_client(0);assert(observed_item==0x12345678u);
    netHordeSend(-1);hosting=false;
    /* A migrated host can continue the same seeded director. */
    HordeState adopted=state;
    assert(hordeRandom(&adopted)==hordeRandom(&saved));
    epoch++;receive();assert(applied==2);epoch--;
    bytes--;receive();assert(applied==2);bytes++;
    packet[bytes++]=0;receive();assert(applied==2);bytes--;
    packet[bytes-2]=99;receive();assert(applied==2);
    /* Reject malformed loot while preserving the last accepted snapshot. */
    HordeState valid=state;
    state.ammo[0].pos[0]=NAN;hosting=true;netHordeSend(-1);hosting=false;receive();assert(applied==2);state=valid;
    state.ammo[1].id=state.ammo[0].id;hosting=true;netHordeSend(-1);hosting=false;receive();assert(applied==2);state=valid;
    state.guns[0][1]=200;hosting=true;netHordeSend(-1);hosting=false;receive();assert(applied==2);state=valid;
    state.powers[0].pos[2]=INFINITY;hosting=true;netHordeSend(-1);hosting=false;receive();assert(applied==2);state=valid;
    state.powers[1].id=state.powers[0].id;hosting=true;netHordeSend(-1);hosting=false;receive();assert(applied==2);state=valid;
    state.powers[0].kind=HORDE_POWER_COUNT;hosting=true;netHordeSend(-1);hosting=false;receive();assert(applied==2);state=valid;
    state.instakill_ticks=HORDE_INSTAKILL_TICKS+1;hosting=true;netHordeSend(-1);hosting=false;receive();assert(applied==2);state=valid;
    assert(gevrHordeGuardToken((struct ChrRecord *)1)==(((uint64_t)7<<32)|123));
    assert(gevrHordeTokenCurrent(((uint64_t)7<<32)|123));
    assert(!gevrHordeTokenCurrent(((uint64_t)6<<32)|123));
    assert(!gevrHordeTokenCurrent((uint64_t)7<<32));
    NetMatchConfig c={0};c.mode=NET_MODE_HORDE;c.stage=NET_HORDE_STAGE;c.max_players=4;c.horde_start_wave=1;
    assert(netMatchConfigValid(&c) && netConfigMaxPlayers(&c)==4);
    /* Every mission and arena, including Statue and MP-only Complex. The
     * wire round trip must preserve IDs and reject menus/unknown stages. */
    assert(netStageCount()==26);
    for(int i=0;i<netStageCount();i++) {
        c.stage=netStage(i)->level_id;
        assert(netMatchConfigValid(&c) && netConfigMaxPlayers(&c)==4);
        assert(*netHordeStageName(c.stage));
        unsigned char config_raw[64];struct netbuf config_buf={.data=config_raw,.size=sizeof(config_raw)};
        NetMatchConfig decoded;
        netbufStartWrite(&config_buf);netbufWriteMatchConfig(&config_buf,&c);
        unsigned written=config_buf.wp;netbufStartReadData(&config_buf,config_raw,written);
        assert(!netbufReadMatchConfig(&config_buf,&decoded) && !netbufReadLeft(&config_buf) && netMatchConfigValid(&decoded));
        assert(decoded.stage==c.stage && decoded.mode==NET_MODE_HORDE);
    }
    for(int i=0;i<netCoopMissionCount();i++)assert(netHordeStageValid(netCoopMission(i)->level_id));
    const unsigned invalid[]={0,21,40,54,90,127,255,256+24,(unsigned)-1};
    for(unsigned i=0;i<sizeof(invalid)/sizeof(invalid[0]);i++)assert(!netHordeStageValid(invalid[i]));
    c.stage=90;assert(!netMatchConfigValid(&c) && netConfigMaxPlayers(&c)==0);c.stage=NET_HORDE_STAGE;
    c.horde_start_wave=0;assert(!netMatchConfigValid(&c));
    c.horde_start_wave=NET_HORDE_MAX_START_WAVE+1;assert(!netMatchConfigValid(&c));
    c.horde_start_wave=NET_HORDE_MAX_START_WAVE;assert(netMatchConfigValid(&c));c.horde_start_wave=1;
    c.bot_mode=NET_BOT_FILL;assert(!netMatchConfigValid(&c));
    NetChrState actor={0},copy;actor.generation=((uint64_t)7<<32)|123;
    unsigned char raw[NET_CHR_STATE_BYTES];struct netbuf b={.data=raw,.size=sizeof(raw)};
    netbufStartWrite(&b);netbufWriteChrState(&b,&actor);assert(!b.error && b.wp==sizeof(raw));
    netbufStartReadData(&b,raw,sizeof(raw));assert(netbufReadChrState(&b,&copy));
    assert(!netbufReadLeft(&b) && copy.generation==actor.generation);
    puts("PASS: Horde snapshot round trips, client Mystery Box / Wall Buy requests, migration seed, epoch/malformed rejection, mode validation and guard identities");
    return 0;
}
