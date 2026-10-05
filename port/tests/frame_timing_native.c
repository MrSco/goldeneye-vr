#include "gevr_frame_timing.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static void sample(uint64_t start, uint64_t submit, int64_t display, int64_t period,
                   uint64_t wait, uint64_t draw, int redraw)
{
    gevrFrameTimingBegin(start);
    gevrFrameTimingPredicted(display, period);
    gevrFrameTimingDuration(GEVR_TIME_WAIT, wait);
    gevrFrameTimingDuration(redraw ? GEVR_TIME_REDRAW : GEVR_TIME_FRESH, draw);
    gevrFrameTimingDuration(GEVR_TIME_IMAGE_WAIT, 1000000);
    gevrFrameTimingSubmit(submit, 1);
}
int main(void)
{
    GevrFrameTimingWindow w;
    assert(gevrFrameTimingNow() == 0);
    gevrFrameTimingEnable(1);
    assert(gevrFrameTimingNow() > 0);
    const int rates[] = {72, 80, 90, 120};
    for (unsigned i = 0; i < 4; i++) {
        gevrFrameTimingEnable(0); gevrFrameTimingEnable(1);
        const int64_t period = 1000000000ll / rates[i];
        sample(1000000000, 1005000000, period, period, 2000000, 2000000, 0);
        sample(1006000000, 1013000000, period*2, period, 3000000, 3000000, 1);
        gevrFrameTimingTake(&w);
        assert(w.frames == 2 && w.predictedSkips == 0 && w.worstGap == 8000000);
        assert(w.gapFrame.kind == 2 && w.gapPrevious.kind == 1);
        assert(w.gapFrame.between == 1000000 && w.gapFrame.work == 4000000);
        assert(w.gapFrame.predictedStep == period);
        assert(w.peak[GEVR_TIME_IMAGE_WAIT] == 1000000);
        /* A window rollover retains the previous submitted frame for pairing. */
        sample(1014000000, 1040000000, period*5, period, 1000000, 23000000, 0);
        gevrFrameTimingTake(&w);
        assert(w.frames == 1 && w.predictedSkips == 2 && w.worstGap == 27000000);
        assert(w.workFrame.work == 25000000 && w.gapPrevious.kind == 2);
        /* Long active-frame stalls are retained; explicit visibility aborts
         * clear the pair, and refresh changes aren't prediction skips. */
        sample(1300000000, 1305000000, period*30, period, 1000000, 2000000, 0);
        gevrFrameTimingTake(&w);
        assert(w.predictedSkips == 24 && w.worstGap == 265000000);
        gevrFrameTimingSubmit(0, 0);
        sample(1306000000, 1310000000, period*34, period+1000000, 1000000, 2000000, 1);
        gevrFrameTimingTake(&w);
        assert(w.predictedSkips == 0 && w.worstGap == 0);
    }
    gevrFrameTimingGpu(8.0, 0); gevrFrameTimingGpu(2.0, 1);
    gevrFrameTimingGpu(5.0, 0); gevrFrameTimingGpu(-1.0, 1);
    gevrFrameTimingGpuDiscard(0); gevrFrameTimingGpuDiscard(1);
    gevrFrameTimingTake(&w);
    assert(w.gpuCount[0] == 2 && w.gpuCount[1] == 1);
    assert(w.gpuPeak[0] == 8.0 && w.gpuPeak[1] == 2.0);
    assert(w.gpuDisjoint == 1 && w.gpuBusy == 1);
    gevrFrameTimingSubmit(0, 0);
    sample(1400000000, 1405000000, 1, 1, 0, 1000000, 0);
    gevrFrameTimingTake(&w);
    assert(w.worstGap == 0);
    char text[640];
    gevrFrameTimingFormatSample(&w.workFrame, text, sizeof(text));
    assert(strstr(text, "kind=fresh") && strstr(text, "image_wait=1.000"));
    gevrFrameTimingEnable(0);
    sample(1500000000, 1505000000, 1, 1, 0, 1000000, 0);
    gevrFrameTimingGpu(99, 0);
    gevrFrameTimingTake(&w);
    assert(!w.frames && !w.gpuCount[0]);
    puts("PASS: per-XR gap pairing, CPU sections, 72/80/90/120 predictions, refresh/session/window resets and GPU accounting");
}
