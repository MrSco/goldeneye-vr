#include "gevr_frame_timing.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
uint64_t gevrTimingTestClock;
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
    FILE *f=fopen("profile.marker","w");fputs("record test 0 1 1",f);fclose(f);
    gevrTimingTestClock=1000000000ull;
    for(int i=0;i<60;i++) gevrFrameTimingTracePoll("profile.marker");
    assert(gevrFrameTimingTracing());
    for(int i=0;i<8194;i++) {
        gevrTimingTestClock=1000000000ull+i*1000ull;
        gevrFrameTimingBegin(gevrTimingTestClock);
        gevrFrameTimingSubmit(gevrTimingTestClock+1,1);
    }
    gevrTimingTestClock=2000000000ull;gevrFrameTimingTracePoll("profile.marker");
    assert(!gevrFrameTimingTracing());
    f=fopen("profile.marker.test.csv","r");assert(f);
    char text[512];assert(fgets(text,sizeof(text),f));fclose(f);
    assert(strstr(text,"rows=8192") && strstr(text,"overflow=2"));
    remove("profile.marker.test.csv");
    gevrFrameTimingEnable(0);assert(!gevrFrameTimingEnter(GEVR_TIME_GAME));
    puts("PASS: production nested inclusive/exclusive timing, 1/16 sampling, pre-frame work/counters and bounded CSV export");
}
