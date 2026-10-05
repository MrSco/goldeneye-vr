#define _POSIX_C_SOURCE 200809L
#include "gevr_frame_timing.h"
#include <string.h>
#include <stdio.h>
#include <time.h>
#include <math.h>

static int enabled;
static GevrFrameTimingSample frame, previous;
static GevrFrameTimingWindow window;

void gevrFrameTimingEnable(int value)
{
    value = value != 0;
    if (value == enabled) return;
    enabled = value;
    memset(&frame, 0, sizeof(frame));
    memset(&previous, 0, sizeof(previous));
    memset(&window, 0, sizeof(window));
}
uint64_t gevrFrameTimingNow(void)
{
    struct timespec ts;
    if (!enabled) return 0;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}
void gevrFrameTimingBegin(uint64_t now)
{
    memset(&frame, 0, sizeof(frame));
    if (enabled) frame.start = now;
}
void gevrFrameTimingPredicted(int64_t display, int64_t period)
{
    if (!enabled) return;
    frame.display = display;
    frame.period = period;
}
void gevrFrameTimingDuration(GevrFrameTimingSection section, uint64_t ns)
{
    if (!enabled || !frame.start || section < 0 || section >= GEVR_TIME_COUNT) return;
    frame.cpu[section] += ns;
    if (section == GEVR_TIME_FRESH) frame.kind = 1;
    if (section == GEVR_TIME_REDRAW) frame.kind = 2;
}
void gevrFrameTimingAdd(GevrFrameTimingSection section, uint64_t start)
{
    if (start) {
        const uint64_t now = gevrFrameTimingNow();
        if (now >= start) gevrFrameTimingDuration(section, now - start);
    }
}
void gevrFrameTimingSubmit(uint64_t now, int success)
{
    if (!enabled || !frame.start || !success || now < frame.start) {
        memset(&previous, 0, sizeof(previous));
        frame.start = 0;
        return;
    }
    frame.submit = now;
    const uint64_t total = now - frame.start;
    frame.work = total > frame.cpu[GEVR_TIME_WAIT] ? total - frame.cpu[GEVR_TIME_WAIT] : 0;
    /* Explicit session/visibility aborts clear previous; long active-frame
     * stalls still need a gap record. Refresh switches are excluded from
     * prediction-step counts. These aren't compositor dropped-frame counts. */
    if (previous.submit && now >= previous.submit) {
        const uint64_t gap = now - previous.submit;
        if (frame.start >= previous.submit) frame.between = frame.start - previous.submit;
        frame.predictedStep = frame.display - previous.display;
        if (frame.period > 0 && frame.period == previous.period && frame.predictedStep > frame.period * 3 / 2)
            window.predictedSkips += (unsigned)((frame.predictedStep + frame.period / 2) / frame.period - 1);
        if (gap > window.worstGap) {
            window.worstGap = gap;
            window.gapFrame = frame;
            window.gapPrevious = previous;
        }
    }
    if (frame.work > window.workFrame.work) window.workFrame = frame;
    for (int i = 0; i < GEVR_TIME_COUNT; i++)
        if (frame.cpu[i] > window.peak[i]) window.peak[i] = frame.cpu[i];
    window.frames++;
    previous = frame;
    frame.start = 0;
}
void gevrFrameTimingGpu(double ms, int redraw)
{
    if (!enabled || !isfinite(ms) || ms < 0) return;
    const int kind = redraw != 0;
    window.gpuCount[kind]++;
    if (ms > window.gpuPeak[kind]) window.gpuPeak[kind] = ms;
}
void gevrFrameTimingGpuDiscard(int busy)
{
    if (!enabled) return;
    if (busy) window.gpuBusy++; else window.gpuDisjoint++;
}
void gevrFrameTimingTake(GevrFrameTimingWindow *out)
{
    *out = window;
    memset(&window, 0, sizeof(window));
}
void gevrFrameTimingFormatSample(const GevrFrameTimingSample *s, char *out, size_t size)
{
    snprintf(out, size,
        "kind=%s work=%.3f between=%.3f predict_step=%.3f wait=%.3f begin=%.3f poses=%.3f acquire=%.3f image_wait=%.3f setup=%.3f fresh=%.3f redraw=%.3f vertex_wait=%.3f layers=%.3f release=%.3f submit=%.3f throttle=%.3f",
        s->kind == 1 ? "fresh" : s->kind == 2 ? "redraw" : "other",
        s->work / 1e6, s->between / 1e6, s->predictedStep / 1e6,
        s->cpu[GEVR_TIME_WAIT] / 1e6, s->cpu[GEVR_TIME_BEGIN] / 1e6, s->cpu[GEVR_TIME_POSES] / 1e6,
        s->cpu[GEVR_TIME_ACQUIRE] / 1e6, s->cpu[GEVR_TIME_IMAGE_WAIT] / 1e6, s->cpu[GEVR_TIME_EYE_SETUP] / 1e6,
        s->cpu[GEVR_TIME_FRESH] / 1e6, s->cpu[GEVR_TIME_REDRAW] / 1e6, s->cpu[GEVR_TIME_VERTEX_WAIT] / 1e6,
        s->cpu[GEVR_TIME_LAYERS] / 1e6, s->cpu[GEVR_TIME_RELEASE] / 1e6, s->cpu[GEVR_TIME_SUBMIT] / 1e6,
        s->cpu[GEVR_TIME_THROTTLE] / 1e6);
}
