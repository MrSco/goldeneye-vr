#ifndef GEVR_FRAME_TIMING_H
#define GEVR_FRAME_TIMING_H
#include <stdint.h>
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    GEVR_TIME_WAIT, GEVR_TIME_BEGIN, GEVR_TIME_POSES,
    GEVR_TIME_ACQUIRE, GEVR_TIME_IMAGE_WAIT, GEVR_TIME_EYE_SETUP,
    GEVR_TIME_FRESH, GEVR_TIME_REDRAW, GEVR_TIME_VERTEX_WAIT,
    GEVR_TIME_LAYERS, GEVR_TIME_RELEASE, GEVR_TIME_SUBMIT,
    GEVR_TIME_THROTTLE, GEVR_TIME_DRAW_BATCH, GEVR_TIME_DRAW_ISSUE,
    GEVR_TIME_SHADER_BIND, GEVR_TIME_SHADER_COMPILE, GEVR_TIME_TEXTURE_UPLOAD,
    GEVR_TIME_GPU_POLL, GEVR_TIME_HUD_READBACK,
    GEVR_TIME_FRAME, GEVR_TIME_GAME, GEVR_TIME_INPUT, GEVR_TIME_AUDIO,
    GEVR_TIME_FINALIZE, GEVR_TIME_METRICS, GEVR_TIME_DL, GEVR_TIME_VERTEX, GEVR_TIME_CLIP,
    GEVR_TIME_TEX_LOOKUP, GEVR_TIME_TEX_CONVERT, GEVR_TIME_CAPTURE,
    GEVR_TIME_SCOPE, GEVR_TIME_TEX_READY,
    GEVR_TIME_COUNT
} GevrFrameTimingSection;

typedef enum {
    GEVR_MOVE_SIMPLE, GEVR_MOVE_FRACTION, GEVR_MOVE_EDGE, GEVR_MOVE_END, GEVR_MOVE_PRECISION
} GevrMoveAttempt;
typedef struct {
    int64_t display;
    float requested[2], actual[2], edge[2]; /* horizontal game centimetres; edge is a unit tangent */
    unsigned attempted, accepted, calls, scoot;
} GevrCollisionTick;

/* Durations are CPU wall time. FRESH/REDRAW include their nested eye/driver
 * sections; they must not be summed with those sections. No clock conversion
 * between OpenXR time and the CPU monotonic clock is assumed. */
typedef struct {
    uint64_t start, submit, cpu[GEVR_TIME_COUNT], self[GEVR_TIME_COUNT];
    uint64_t pre[GEVR_TIME_COUNT];
    /* allocations counts texture-cache node allocations only (one per miss),
     * not heap activity in general; the CSV calls it tex_cache_allocs. */
    uint64_t draws, vertices, allocations, uploadBytes, cacheHits, cacheMisses;
    unsigned detailed, timingErrors;
    /* Test-hook input (libultra.c gevr_input.txt) active at submit; zero otherwise. */
    unsigned inputButtons;
    int inputX, inputY, inputTurn;
    uint64_t imageLifetime;
    int64_t display, period, predictedStep;
    uint64_t between, work;
    unsigned kind; /* 0 = no game eye pass, 1 = fresh, 2 = redraw */
    /* Game centimetres, paired with this submitted frame. Delta validity
     * excludes the first sample after a tracking/context discontinuity. */
    float physical[3], requested[3], actual[3], bodyStep[3], rootStep[3], cameraStep[3];
    unsigned collision, physicalReset, bodyValid, cameraValid, resetReason;
    unsigned moveCalls, scootCalls, moveAttempted, moveAccepted;
    float collisionEdge[2];
} GevrFrameTimingSample;
typedef struct {
    unsigned frames, predictedSkips, gpuCount[2], gpuDisjoint, gpuBusy;
    uint64_t peak[GEVR_TIME_COUNT], worstGap;
    double gpuPeak[2];
    GevrFrameTimingSample gapFrame, gapPrevious, workFrame, motionFrame;
    unsigned moveTicks, stoppedTicks, clippedTicks, collisionCount, collisionOverflow;
    GevrCollisionTick collisionTicks[64];
} GevrFrameTimingWindow;

void gevrFrameTimingEnable(int enabled);
uint64_t gevrFrameTimingNow(void); /* zero when disabled */
void gevrFrameTimingBegin(uint64_t now);
void gevrFrameTimingPredicted(int64_t display, int64_t period);
void gevrFrameTimingAdd(GevrFrameTimingSection section, uint64_t start);
void gevrFrameTimingDuration(GevrFrameTimingSection section, uint64_t ns);
void gevrFrameTimingSubmit(uint64_t now, int success);
void gevrFrameTimingGpu(double ms, int redraw);
void gevrFrameTimingGpuDiscard(int busy);
void gevrFrameTimingTake(GevrFrameTimingWindow *out);
void gevrFrameTimingFormatSample(const GevrFrameTimingSample *s, char *out, size_t size);
void gevrFrameTimingCollision(const float physical[3], const float requested[3], const float actual[3], int reset);
void gevrFrameTimingSnapshot(const float body[3], const float root[3]);
void gevrFrameTimingCamera(const float position[3]);
void gevrFrameTimingResetMotion(unsigned reason, int discontinuity);
void gevrFrameTimingFormatMotion(const GevrFrameTimingSample *s, char *out, size_t size);
void gevrFrameTimingMoveBegin(int allowScoot);
void gevrFrameTimingMoveResult(GevrMoveAttempt kind, int result, const float edge0[3], const float edge1[3]);
void gevrFrameTimingFormatCollision(const GevrFrameTimingWindow *w, unsigned first, char *out, size_t size);
uint64_t gevrFrameTimingEnter(GevrFrameTimingSection section);
void gevrFrameTimingLeave(uint64_t token);
void gevrFrameTimingImageLifetime(uint64_t ns);
void gevrFrameTimingOutside(GevrFrameTimingSection section, uint64_t ns);
void gevrFrameTimingCounters(uint64_t draws, uint64_t vertices, uint64_t allocations,
    uint64_t uploadBytes, uint64_t cacheHits, uint64_t cacheMisses);
/* A redraw span marks the frame as a redraw; report a redraw that drew nothing. */
void gevrFrameTimingRedrawResult(int redrawn);
void gevrFrameTimingInput(unsigned buttons, int x, int y, int turn);
/* record LABEL WARMUP_SECONDS MEASURE_SECONDS DETAIL(0/1) in an opt-in marker.
 * The completed CSV is written next to the marker, after the measured interval.
 * <marker>.status holds "LABEL START END" while recording, then
 * "done LABEL ROWS OVERFLOW" or "failed LABEL REASON" after export. */
void gevrFrameTimingTracePoll(const char *marker);
int gevrFrameTimingTracing(void);
const char *gevrFrameTimingSectionName(unsigned section);
void gfx_vr_gpu_begin(int redraw);
void gfx_vr_gpu_end(void);
void gfx_vr_gpu_reset(void);
#ifdef __cplusplus
}
class GevrProfileSection {
    uint64_t token;
public:
    explicit GevrProfileSection(GevrFrameTimingSection section) : token(gevrFrameTimingEnter(section)) {}
    ~GevrProfileSection() { gevrFrameTimingLeave(token); }
    GevrProfileSection(const GevrProfileSection&) = delete;
    GevrProfileSection& operator=(const GevrProfileSection&) = delete;
};
#endif
#endif
