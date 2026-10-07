#include "gevr_collision.h"
#include "gevr_hud_geometry.h"
#include "gevr_line_geometry.h"
#include <string.h>
#include "net/net_protocol.h"
#include "net/net_spatial.h"
#define EXPORT __declspec(dllexport)
void sysLogPrintf(s32 level, const char *fmt, ...) { (void)level; (void)fmt; }
EXPORT float test_gain(int mode,float distance) { return netVoiceDistanceGain(mode,distance); }
EXPORT int test_group(int running,int mode,int a_spec,int b_spec,int a_team,int b_team) { return netVoiceGroupsMatch(running,mode,a_spec,b_spec,a_team,b_team); }
EXPORT int test_roster(int mode,const uint8_t *connected,const uint8_t *teams) { return netTeamRosterComplete(mode,GEVR_MAX_PLAYERS,connected,teams); }
EXPORT int test_max_players(void) { return GEVR_MAX_PLAYERS; }
EXPORT int test_game_scenario(int mode) { return netGameScenario(mode); }
EXPORT int test_capacity(int mode,int team) { return netTeamCapacity(mode,team); }
EXPORT int test_points(int a,int b,int kills) { return netTeamKillPoints(a,b,kills); }
EXPORT int test_damage(int mode,int enabled,int self,int a,int b) { return netTeamDamageAllowed(mode,enabled,self,a,b); }
EXPORT int test_ammo_protocol(void) {
    u8 raw[NET_AMMO_STATE_BYTES];struct netbuf b={.data=raw,.size=sizeof(raw)};
    NetAmmoState a={0},r;
    a.index=29;a.enabled=a.moving=1;a.regen=0;a.pos.x=123;a.pos.y=-45;a.pos.z=987;
    a.speed.x=3;a.speed.z=-7;a.mtx.m[0][0]=a.mtx.m[1][1]=a.mtx.m[2][2]=.75f;
    a.rotation.m[0][0]=a.rotation.m[1][1]=a.rotation.m[2][2]=1;
    a.mtx.m[3][3]=a.rotation.m[3][3]=1;
    netbufStartWrite(&b);netbufWriteAmmoState(&b,&a);
    if(b.error || b.wp!=NET_AMMO_STATE_BYTES) return 1;
    netbufStartReadData(&b,raw,b.wp);
    if(!netbufReadAmmoState(&b,&r) || netbufReadLeft(&b) || memcmp(&a,&r,sizeof(a))) return 2;
    for(int size=0;size<NET_AMMO_STATE_BYTES;size++) {
        netbufStartReadData(&b,raw,size);if(netbufReadAmmoState(&b,&r)) return 3;
    }
    a.pos.y=NAN;if(netAmmoStateValid(&a)) return 4;a.pos.y=0;
    a.index=0x9000;if(netAmmoStateValid(&a)) return 5;a.index=29;
    a.regen=1201;if(netAmmoStateValid(&a)) return 6;a.regen=1200;
    a.enabled=2;if(netAmmoStateValid(&a)) return 7;a.enabled=1;
    a.speed.z=INFINITY;if(netAmmoStateValid(&a)) return 8;
    return 0;
}
EXPORT int test_protocol(void) {
    uint8_t raw[32]; struct netbuf b={.data=raw,.size=sizeof(raw)};
    NetMatchConfig original={0},received={0};
    original.stage=34;original.scenario=7;original.weapon_set=14;original.game_length=6;
    original.fun_flags=7;original.gun_size=2;
    original.health=10;original.dual_wield=2;original.loadouts=1;original.next_round=2;original.voice_mode=1;original.friendly_fire=1;
    for (int i=0;i<4;i++) original.custom_set[i]=(uint8_t)(10+i);
    original.max_players=6;original.mode=1;original.difficulty=2;
    original.bot_mode=1;original.bot_count=3;original.bot_difficulty=2;
    netbufStartWrite(&b); netbufWriteMatchConfig(&b,&original);
    if (b.error || b.wp != 22 || GEVR_NET_VERSION != 18 || GEVR_MAX_PLAYERS != 8) return 1;
    netbufStartReadData(&b,raw,b.wp); netbufReadMatchConfig(&b,&received);
    if (b.error || netbufReadLeft(&b) || memcmp(&original,&received,sizeof(original))) return 2;
    for(int size=0;size<22;size++) {
        netbufStartReadData(&b,raw,size); netbufReadMatchConfig(&b,&received); if(!b.error) return 3;
    }
    netbufStartReadData(&b,raw,22); netbufReadMatchConfig(&b,&received);
    netbufStartWrite(&b); b.size=21; netbufWriteMatchConfig(&b,&original); if(!b.error) return 4;
    return 0;
}
EXPORT int test_spatial_init(void) { return netSpatialInit(); }
EXPORT void test_spatial_reset(unsigned slot) { netSpatialResetSlot(slot); }
EXPORT void test_spatial_shutdown(void) { netSpatialShutdown(); }
EXPORT void test_spatial_render(unsigned slot,const float *input,unsigned frames,const float *direction,int positioned,float *output) {
    for(unsigned i=0;i<frames;i++) netSpatialSample(slot,input[i],direction,positioned,&output[2*i],&output[2*i+1]);
}

EXPORT int test_config_validation(void) {
    NetMatchConfig c={0}; c.stage=34; c.health=5; c.max_players=4;
    for(int i=0;i<4;i++) c.custom_set[i]=netItem(0)->item;
    if(!netMatchConfigValid(&c)) return 1;
    c.voice_mode=2; if(netMatchConfigValid(&c)) return 2; c.voice_mode=0;
    /* the host's count, two to eight, on any stage: eight on Egypt (the game's two) */
    c.stage=32; c.max_players=8; if(!netMatchConfigValid(&c) || netConfigMaxPlayers(&c)!=8) return 3;
    c.max_players=1; if(netMatchConfigValid(&c)) return 4;
    c.max_players=9; if(netMatchConfigValid(&c)) return 5;
    /* a team scenario takes its own size whatever the count */
    c.max_players=2; c.scenario=9; if(!netMatchConfigValid(&c) || netConfigMaxPlayers(&c)!=8) return 14;
    c.stage=27; c.scenario=8; if(!netMatchConfigValid(&c) || netConfigMaxPlayers(&c)!=6) return 15;
    c.stage=34; c.max_players=4; c.scenario=10; if(netMatchConfigValid(&c)) return 6;
    c.scenario=0; c.custom_set[0]=255; if(netMatchConfigValid(&c)) return 7;
    c.custom_set[0]=netItem(0)->item; c.loadouts=2; if(netMatchConfigValid(&c)) return 8;
    c.loadouts=0;c.friendly_fire=2;if(netMatchConfigValid(&c)) return 9;
    c.friendly_fire=0;c.fun_flags=8;if(netMatchConfigValid(&c)) return 10;
    c.fun_flags=255;if(netMatchConfigValid(&c)) return 11;
    c.fun_flags=7;c.gun_size=3;if(netMatchConfigValid(&c)) return 12;
    c.gun_size=2;if(!netMatchConfigValid(&c)) return 13;
    /* Statue and Cradle (#95): the ROM's cut MP setups, eight players, last in the list */
    c.stage=22;c.max_players=8;if(!netMatchConfigValid(&c) || netStageIndexOf(22)!=11) return 16;
    c.stage=41;if(!netMatchConfigValid(&c) || netStageIndexOf(41)!=12 || netStageIndexOf(34)!=0) return 17;
    return 0;
}

EXPORT int test_spatial_error(void) { return netSpatialLastError(); }

EXPORT int test_ping(uint32_t rtt,uint32_t last,uint32_t now) { return netLatencyValue(rtt,last,now); }

EXPORT int test_door_escape(void) {
    float p[8]={-50,-10,50,-10,50,10,-50,10};
    float reversed[8]={-50,10,50,10,50,-10,-50,-10};
    for(int i=0;i<2;i++) {
        float *q=i?reversed:p;
        if(!gevrDoorEscape(q,4,30,0,0,0,5)) return 1;
        if(!gevrDoorEscape(q,4,30,0,15,0,25)) return 2;
        if(gevrDoorEscape(q,4,30,0,25,0,15)) return 3;
        if(gevrDoorEscape(q,4,30,0,50,0,0)) return 4;
        if(gevrDoorEscape(q,4,30,0,50,0,60)) return 5;
        if(gevrDoorEscape(q,4,30,0,0,0,0)) return 6;
    }
    return 0;
}

EXPORT int test_gauge_geometry(void) {
    for(int scale=1;scale<=8;scale++) for(int point=0;point<23;point++) for(int pair=0;pair<2;pair++) {
        float a=(142.5f-5*point)*3.14159265358979323846f/180;
        float r=520*(6-pair)/5.0f, x=sin(a)*r, y=-cos(a)*r;
        float hx,hy,ax,ay;
        gevrRadarGaugePoint(1-x,y,16*scale,17.5f,&hx,&hy);
        gevrRadarGaugePoint(1+x,y,16*scale,-17.5f,&ax,&ay);
        if(fabsf(hx+ax)>.0001f || fabsf(hy-ay)>.0001f) return 1;
        if(fabsf(hypotf(hx,hy-2*scale)-19*scale*(pair ? 1 : 1.2f))>.0001f) return 2;
        if(hypotf(roundf(hx),roundf(hy)) <= 16*scale) return 3;
    }
    if(netGunSizeFactor(0)!=1 || netGunSizeFactor(1)!=.2f || netGunSizeFactor(2)!=2) return 4;
    return 0;
}

EXPORT int test_line_indices(void) {
    uint32_t out[14];out[12]=0xaabbccdd;out[13]=0x11223344;
    uint32_t expected[12]={300000,300001,300001,300002,300002,300000,300003,300004,300004,300005,300005,300003};
    gevrTriangleEdgeIndices(300000,6,out);
    if(memcmp(out,expected,sizeof(expected)) || out[12]!=0xaabbccdd || out[13]!=0x11223344) return 1;
    gevrTriangleEdgeIndices(0,0,out);if(out[0]!=300000) return 2;
    return 0;
}

#include "net/net_timing.h"
#define TIMING_CHECK(x) do {if(!(x))return __LINE__;}while(0)
EXPORT int test_clock_math(void) {
    NetClockSync c={0};uint64_t host;unsigned uncertainty;
    // Host at 10 s, client at 110 s, 30 ms each way and 2 ms response processing.
    TIMING_CHECK(netClockSample(&c,10000000,110030000,110032000,10062000));
    TIMING_CHECK(netClockMap(&c,110020000,10062000,&host,&uncertainty));
    TIMING_CHECK(host==10020000 && uncertainty==30000);
    TIMING_CHECK(!netClockSample(&c,0,1,1,1));TIMING_CHECK(!netClockSample(&c,100,200,100,300));
    TIMING_CHECK(!netClockSample(&c,100,200,1000001,300));
    TIMING_CHECK(!netClockSample(&c,100,200,200,2000000));
    TIMING_CHECK(!netClockMap(&c,UINT64_MAX,10062000,&host,&uncertainty));
    TIMING_CHECK(!netClockMap(&c,110020000,20062001,&host,&uncertainty));
    c=(NetClockSync){0};TIMING_CHECK(netClockSample(&c,100000000,3030000,3030000,100060000));
    TIMING_CHECK(netClockMap(&c,3020000,100060000,&host,&uncertainty));TIMING_CHECK(host==100020000);
    TIMING_CHECK(netShotTimeValid(9500000,10000000,0));TIMING_CHECK(!netShotTimeValid(9499999,10000000,0));
    TIMING_CHECK(netShotTimeValid(10050000,10000000,0));TIMING_CHECK(!netShotTimeValid(10050001,10000000,0));
    TIMING_CHECK(!netShotTimeValid(UINT64_MAX,10000000,50000));
    TIMING_CHECK(netShotTradeValid(9850000,10000000,0));TIMING_CHECK(!netShotTradeValid(9849999,10000000,0));
    TIMING_CHECK(!netShotTradeValid(10000001,10000000,0));TIMING_CHECK(netShotTradeValid(10025000,10000000,99999));
    return 0;
}
EXPORT int test_timing_protocol(void) {
    u8 raw[256];struct netbuf b={.data=raw,.size=sizeof(raw)};
    NetHitReport h={0xabcdef123456789ull,900000000000ull,23,87,4,12,2,ITEM_WPPK,3,1,-2,3,.5f},r={0};
    netbufStartWrite(&b);netbufWriteHitReport(&b,&h);TIMING_CHECK(!b.error && b.wp==NET_HIT_REPORT_BYTES);
    netbufStartReadData(&b,raw,b.wp);TIMING_CHECK(netbufReadHitReport(&b,&r) && !netbufReadLeft(&b));
    TIMING_CHECK(h.epoch==r.epoch && h.shot_us==r.shot_us && h.shooter_life==r.shooter_life && h.target_life==r.target_life);
    TIMING_CHECK(h.shot_id==r.shot_id && h.hit_id==r.hit_id && h.damage==r.damage && h.hy==r.hy);
    for(int size=0;size<NET_HIT_REPORT_BYTES;size++) {netbufStartReadData(&b,raw,size);TIMING_CHECK(!netbufReadHitReport(&b,&r));}
    h.damage=NAN;b.size=sizeof(raw);netbufStartWrite(&b);netbufWriteHitReport(&b,&h);netbufStartReadData(&b,raw,b.wp);TIMING_CHECK(!netbufReadHitReport(&b,&r));
    b.size=sizeof(raw);NetClockExchange c={.epoch=1,.t0=100,.t1=150,.t2=160,.nonce=9,.reply=1},cr;
    netbufStartWrite(&b);netbufWriteClock(&b,&c);TIMING_CHECK(!b.error && b.wp==NET_CLOCK_EXCHANGE_BYTES);
    netbufStartReadData(&b,raw,b.wp);TIMING_CHECK(netbufReadClock(&b,&cr) && !netbufReadLeft(&b) && cr.t2==160 && cr.nonce==9);
    for(int size=0;size<NET_CLOCK_EXCHANGE_BYTES;size++) {netbufStartReadData(&b,raw,size);TIMING_CHECK(!netbufReadClock(&b,&cr));}
    c.reply=2;netbufStartWrite(&b);netbufWriteClock(&b,&c);netbufStartReadData(&b,raw,b.wp);TIMING_CHECK(!netbufReadClock(&b,&cr));
    struct netplayermove m={0},mr={0};m.tick=9;m.epoch=123;m.life_id=45;m.clock_us=678;m.death_us=650;m.dead=1;
    b.size=sizeof(raw);netbufStartWrite(&b);netbufWritePlayerMove(&b,&m);unsigned len=b.wp;TIMING_CHECK(!b.error);
    netbufStartReadData(&b,raw,len);netbufReadPlayerMove(&b,&mr);TIMING_CHECK(!b.error && !netbufReadLeft(&b));
    TIMING_CHECK(mr.epoch==123 && mr.life_id==45 && mr.clock_us==678 && mr.death_us==650);
    for(unsigned n=0;n<len;n++){netbufStartReadData(&b,raw,n);netbufReadPlayerMove(&b,&mr);TIMING_CHECK(b.error);}
    return 0;
}
