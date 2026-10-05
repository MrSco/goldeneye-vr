#include "gevr_frame_timing.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
uint64_t gevrTimingTestClock;
#ifdef _WIN32
#include <direct.h>
static int mkdir_blocker(const char *path) { return _mkdir(path); }
#else
#include <sys/stat.h>
static int mkdir_blocker(const char *path) { return mkdir(path, 0700); }
#endif
int main(void) {
    gevrFrameTimingEnable(1);
    gevrTimingTestClock=100;
    gevrFrameTimingBegin(100);
    gevrFrameTimingDuration(GEVR_TIME_WAIT,20);
    gevrTimingTestClock=120;uint64_t game=gevrFrameTimingEnter(GEVR_TIME_GAME);
    gevrTimingTestClock=130;uint64_t render=gevrFrameTimingEnter(GEVR_TIME_FRESH);
    gevrTimingTestClock=150;uint64_t dl=gevrFrameTimingEnter(GEVR_TIME_DL);
    gevrTimingTestClock=160;uint64_t vertex=gevrFrameTimingEnter(GEVR_TIME_VERTEX);
    gevrTimingTestClock=180;gevrFrameTimingLeave(vertex);
    gevrTimingTestClock=190;gevrFrameTimingLeave(dl);
    gevrTimingTestClock=210;gevrFrameTimingLeave(render);
    gevrTimingTestClock=250;gevrFrameTimingLeave(game);
    gevrTimingTestClock=300;gevrFrameTimingSubmit(300,1);
    GevrFrameTimingWindow w;gevrFrameTimingTake(&w);
    assert(w.workFrame.cpu[GEVR_TIME_FRAME]==200 && w.workFrame.self[GEVR_TIME_FRAME]==50);
    assert(w.workFrame.cpu[GEVR_TIME_GAME]==130 && w.workFrame.self[GEVR_TIME_GAME]==50);
    assert(w.workFrame.cpu[GEVR_TIME_FRESH]==80 && w.workFrame.self[GEVR_TIME_FRESH]==40);
    assert(w.workFrame.cpu[GEVR_TIME_DL]==40 && w.workFrame.self[GEVR_TIME_DL]==20);
    assert(w.workFrame.self[GEVR_TIME_VERTEX]==20 && !w.workFrame.timingErrors);
    gevrFrameTimingOutside(GEVR_TIME_INPUT,70);
    gevrFrameTimingBegin(400);gevrTimingTestClock=500;
    gevrFrameTimingCounters(9,27,2,1024,3,1);
    uint64_t t=gevrFrameTimingEnter(GEVR_TIME_DL);assert(!t); /* sampled only every 16th frame */
    gevrFrameTimingSubmit(500,1);gevrFrameTimingTake(&w);
    assert(w.workFrame.pre[GEVR_TIME_INPUT]==70 && w.workFrame.draws==9);
    assert(w.workFrame.vertices==27 && w.workFrame.allocations==2 && w.workFrame.uploadBytes==1024);
    assert(w.workFrame.cacheHits==3 && w.workFrame.cacheMisses==1);
    /* A redraw span that drew nothing is not a redraw frame. */
    gevrFrameTimingBegin(600);gevrTimingTestClock=610;
    t=gevrFrameTimingEnter(GEVR_TIME_REDRAW);gevrTimingTestClock=620;gevrFrameTimingLeave(t);
    gevrFrameTimingRedrawResult(0);gevrFrameTimingSubmit(630,1);gevrFrameTimingTake(&w);
    assert(w.workFrame.kind==0 && w.workFrame.cpu[GEVR_TIME_REDRAW]==10);
    gevrFrameTimingBegin(700);gevrTimingTestClock=710;
    t=gevrFrameTimingEnter(GEVR_TIME_REDRAW);gevrTimingTestClock=720;gevrFrameTimingLeave(t);
    gevrFrameTimingRedrawResult(1);gevrFrameTimingSubmit(730,1);gevrFrameTimingTake(&w);
    assert(w.workFrame.kind==2);
    /* Injected test input is stamped on submitted frames until it ends. */
    gevrFrameTimingInput(0x8000,55,-80,25);
    gevrFrameTimingBegin(800);gevrFrameTimingSubmit(810,1);gevrFrameTimingTake(&w);
    assert(w.workFrame.inputButtons==0x8000 && w.workFrame.inputX==55 && w.workFrame.inputY==-80 && w.workFrame.inputTurn==25);
    gevrFrameTimingInput(0,0,0,0);
    gevrFrameTimingBegin(900);gevrFrameTimingSubmit(910,1);gevrFrameTimingTake(&w);
    assert(!w.workFrame.inputButtons && !w.workFrame.inputX && !w.workFrame.inputY && !w.workFrame.inputTurn);
    FILE *f=fopen("profile.marker","w");fputs("record test 0 1 1",f);fclose(f);
    gevrTimingTestClock=1000000000ull;
    for(int i=0;i<60;i++) gevrFrameTimingTracePoll("profile.marker");
    assert(gevrFrameTimingTracing());
    char text[512];
    f=fopen("profile.marker.status","r");assert(f);assert(fgets(text,sizeof(text),f));fclose(f);
    assert(!strcmp(text,"test 1000000000 2000000000"));
    gevrFrameTimingInput(0,0,-80,0);
    for(int i=0;i<8194;i++) {
        gevrTimingTestClock=1000000000ull+i*1000ull;
        gevrFrameTimingBegin(gevrTimingTestClock);
        gevrFrameTimingSubmit(gevrTimingTestClock+1,1);
    }
    gevrTimingTestClock=2000000000ull;gevrFrameTimingTracePoll("profile.marker");
    assert(!gevrFrameTimingTracing());
    f=fopen("profile.marker.test.csv","r");assert(f);
    assert(fgets(text,sizeof(text),f));
    assert(strstr(text,"gevr-profile-v2") && strstr(text,"rows=8192") && strstr(text,"overflow=2"));
    static char header[8192], row[8192];
    assert(fgets(header,sizeof(header),f) && fgets(row,sizeof(row),f));fclose(f);
    assert(strstr(header,",tex_cache_allocs,") && !strstr(header,",allocations,"));
    assert(strstr(header,"image_lifetime_ns,input_buttons,input_x,input_y,input_turn,collision,move_attempted,move_accepted,requested_x,requested_z,actual_x,actual_z,wait_ns,"));
    assert(strstr(row,",0,0,-80,0,0,0,0,0.0000,0.0000,0.0000,0.0000,"));   /* input, then collision columns */
    f=fopen("profile.marker.status","r");assert(f);assert(fgets(text,sizeof(text),f));fclose(f);
    assert(!strcmp(text,"done test 8192 2"));
    remove("profile.marker.test.csv");remove("profile.marker.test.csv.gpu.csv");
    /* An export that cannot be written reports failure instead of vanishing. */
    gevrFrameTimingInput(0,0,0,0);
    f=fopen("profile.marker","w");fputs("record blocked 0 1 0",f);fclose(f);
    assert(!mkdir_blocker("profile.marker.blocked.csv"));
    gevrTimingTestClock=3000000000ull;
    for(int i=0;i<60;i++) gevrFrameTimingTracePoll("profile.marker");
    assert(gevrFrameTimingTracing());
    gevrTimingTestClock=5000000000ull;gevrFrameTimingTracePoll("profile.marker");
    assert(!gevrFrameTimingTracing());
    f=fopen("profile.marker.status","r");assert(f);assert(fgets(text,sizeof(text),f));fclose(f);
    assert(!strcmp(text,"failed blocked csv-write"));
    remove("profile.marker.status");
    gevrFrameTimingEnable(0);assert(!gevrFrameTimingEnter(GEVR_TIME_GAME));
    puts("PASS: production nested inclusive/exclusive timing, 1/16 sampling, pre-frame work/counters and bounded CSV export");
}
