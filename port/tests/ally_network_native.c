/* Production companion transport; one simulated endpoint at a time. */
#include "net_core.h"
#include "net_ally.h"
#include "game/gevr_ally.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static bool hosting=true, active=true;
static uint32_t epoch=7;
static u64 now=1000000;
static GevrAllyState state={1,7,2,250,200,1,1,6001};
static unsigned char packet[64];
static unsigned size, sends, applied;
void sysLogPrintf(int level, const char *format, ...) {}
bool netIsHost(void) { return hosting; }
int netCoopActive(void) { return active; }
int netGetLocalSlot(void) { return 0; }
uint32_t netGetWorldEpoch(void) { return epoch; }
u64 sysGetMicroseconds(void) { return now; }
void gevrAllyGetState(GevrAllyState *out) { *out=state; }
void gevrAllyApplyState(const GevrAllyState *in) { state=*in; applied++; }
/* INSERT_VALIDATION */
void netCoopBroadcast(const unsigned char *data,unsigned bytes,int reliable) {
    assert(hosting && reliable && bytes<=32); memcpy(packet,data,bytes); size=bytes;sends++;
}
void netCoopSendTo(int slot,const unsigned char *data,unsigned bytes) {
    assert(slot==2);netCoopBroadcast(data,bytes,1);
}
static void receive(void) {
    struct netbuf b;netbufStartReadData(&b,packet,size);
    assert(netbufReadU32(&b)==GEVR_NET_MAGIC && netbufReadU16(&b)==GEVR_NET_VERSION);
    assert(netbufReadU8(&b)==NET_MSG_COOP_ALLY);netbufReadU8(&b);
    netAllyReceive(&b);
}
int main(void) {
    netAllyReset();netAllyHostTick();assert(sends==1 && size==27);
    netAllyHostTick();assert(sends==1);now+=1000000;netAllyHostTick();assert(sends==2);
    GevrAllyState expected=state;
    unsigned char original[64];memcpy(original,packet,size);
    hosting=false;state=(GevrAllyState){0,0,0,25,25,0,0,-1};netAllyReset();receive();
    assert(applied==1 && !memcmp(&state,&expected,sizeof(state)));
    /* Invalid config, extra/truncated bytes and an old retry epoch are atomic. */
    packet[17]=99;receive();assert(applied==1);memcpy(packet,original,size);
    size--;receive();assert(applied==1);size++;
    packet[size++]=0;receive();assert(applied==1);size--;
    epoch++;receive();assert(applied==1);epoch--;
    active=false;receive();assert(applied==1);active=true;
    hosting=true;receive();assert(applied==1); // clients cannot reconfigure a host
    /* Takeover preserves the former host's settings and advances revisions. */
    netAllyHostTick();assert(sends==3 && !memcmp(&state,&expected,sizeof(state)));
    unsigned char newhost[64];memcpy(newhost,packet,size);
    hosting=false;receive();assert(applied==2);
    memcpy(packet,original,size);receive();assert(applied==2);
    memcpy(packet,newhost,size);receive();assert(applied==3);
    /* Late join after the body has gone still knows that it already spawned;
     * an elected client cannot create a second life from its local preference. */
    hosting=true;netAllySend(2);hosting=false;netAllyReset();receive();
    assert(applied==4 && state.spawned && state.identity==6001);
    hosting=true;state.spawned=0;state.identity=-1;netAllySend(-1);
    hosting=false;netAllyReset();receive();assert(applied==5 && state.enabled && !state.spawned);
    hosting=true;state.enabled=0;netAllySend(-1);
    hosting=false;netAllyReset();receive();assert(applied==6 && !state.enabled);
    puts("PASS: companion host snapshots, bounded wire config, malformed/stale/epoch rejection, late join, lifecycle and host takeover revisions");
}
