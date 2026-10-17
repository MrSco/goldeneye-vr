#include "net_core.h"
#include "net_horde.h"
#include <assert.h>
#include <string.h>

static HordeState state;
static uint32_t epoch=7;
static bool hosting=true;
static unsigned char packet[128];
static unsigned bytes,applied;
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
    if (action == HORDE_ACT_BOX) return hordeUseBox(&state, (unsigned)slot) != 0;
    if (action == HORDE_ACT_WALL_BUY || action == HORDE_ACT_WALL_AMMO)
        return hordeBuyWall(&state, (unsigned)slot, item, action == HORDE_ACT_WALL_AMMO);
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
    assert(hordeUseBox(&state,1)==1);
    HordeState saved=state;
    netHordeSend(-1);memset(&state,0,sizeof(state));hosting=false;receive();
    assert(applied==1 && !memcmp(&state,&saved,sizeof(state)));
    /* Client sends a Wall Buy request to the host; host validates and broadcasts updated state. */
    netHordeRequestAction(HORDE_ACT_WALL_BUY,0);
    hosting=true;receive_client(0);
    assert(state.credits[0]==0 && state.grant_seq[0]==1 && state.grant_kind[0]==HORDE_GRANT_WALL_WEAPON);
    saved=state;memset(&state,0,sizeof(state));hosting=false;receive();
    assert(applied==2 && !memcmp(&state,&saved,sizeof(state)));
    /* A migrated host can continue the same seeded director. */
    HordeState adopted=state;
    assert(hordeRandom(&adopted)==hordeRandom(&saved));
    epoch++;receive();assert(applied==2);epoch--;
    bytes--;receive();assert(applied==2);bytes++;
    packet[bytes++]=0;receive();assert(applied==2);bytes--;
    packet[bytes-2]=99;receive();assert(applied==2);
    assert(gevrHordeGuardToken((struct ChrRecord *)1)==(((uint64_t)7<<32)|123));
    assert(gevrHordeTokenCurrent(((uint64_t)7<<32)|123));
    assert(!gevrHordeTokenCurrent(((uint64_t)6<<32)|123));
    assert(!gevrHordeTokenCurrent((uint64_t)7<<32));
    NetMatchConfig c={0};c.mode=NET_MODE_HORDE;c.stage=NET_HORDE_STAGE;c.max_players=4;
    assert(netMatchConfigValid(&c) && netConfigMaxPlayers(&c)==4);
    c.stage=27;assert(!netMatchConfigValid(&c));c.stage=NET_HORDE_STAGE;
    c.bot_mode=NET_BOT_FILL;assert(!netMatchConfigValid(&c));
    NetChrState actor={0},copy;actor.generation=((uint64_t)7<<32)|123;
    unsigned char raw[NET_CHR_STATE_BYTES];struct netbuf b={.data=raw,.size=sizeof(raw)};
    netbufStartWrite(&b);netbufWriteChrState(&b,&actor);assert(!b.error && b.wp==sizeof(raw));
    netbufStartReadData(&b,raw,sizeof(raw));assert(netbufReadChrState(&b,&copy));
    assert(!netbufReadLeft(&b) && copy.generation==actor.generation);
    puts("PASS: Horde snapshot round trips, client Mystery Box / Wall Buy requests, migration seed, epoch/malformed rejection, mode validation and guard identities");
    return 0;
}
