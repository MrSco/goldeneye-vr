#define _POSIX_C_SOURCE 200809L
#include "gevr_frame_timing.h"
#include <string.h>
#include <stdio.h>
#include <time.h>
#include <math.h>

static int enabled;
static GevrFrameTimingSample frame, previous;
static GevrFrameTimingWindow window;
static float lastBody[3], lastRoot[3], lastCamera[3];
static int haveBody, haveCamera, sampledBody, sampledCamera;
static uint64_t pending[GEVR_TIME_COUNT], serial;
static unsigned depth, frameIndex;
static struct { GevrFrameTimingSection section; uint64_t start, child, token; } spans[32];
#define GEVR_TRACE_CAPACITY 8192
static GevrFrameTimingSample trace[GEVR_TRACE_CAPACITY];
static struct { uint64_t ready, ns; unsigned kind; } gpuTrace[GEVR_TRACE_CAPACITY];
static unsigned gpuRows, gpuOverflow;
static struct {
    unsigned active, detail, rows, overflow;
    uint64_t measureStart, measureEnd;
    char path[768], label[64];
} recording;
static struct { unsigned buttons; int x, y, turn; } injected;
static uint64_t rawNow(void) {
#ifdef GEVR_TIMING_TEST_CLOCK
    extern uint64_t gevrTimingTestClock;
    return gevrTimingTestClock;
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec*1000000000ull + (uint64_t)ts.tv_nsec;
#endif
}
const char *gevrFrameTimingSectionName(unsigned i) {
    static const char *names[] = {"wait", "begin", "poses", "acquire", "image_wait", "eye_setup",
        "fresh", "redraw", "vertex_wait", "layers", "release", "submit", "throttle", "draw_batch",
        "draw_issue", "shader_bind", "shader_compile", "texture_upload", "gpu_poll", "hud_readback",
        "frame", "game", "input", "audio", "finalize", "metrics", "dl", "vertex", "clip", "tex_lookup",
        "tex_convert", "capture", "scope", "tex_ready"};
    return i < GEVR_TIME_COUNT ? names[i] : "invalid";
}
int gevrFrameTimingTracing(void) { return recording.active != 0; }
static void traceStatus(const char *marker, const char *text) {
    char status[800];snprintf(status,sizeof(status),"%s.status",marker);
    FILE *f=fopen(status,"w");
    if(f) { fputs(text,f);fclose(f); }
}
void gevrFrameTimingTracePoll(const char *marker) {
    static unsigned polls;
    if(recording.active==1 && rawNow()>=recording.measureEnd) recording.active=2;
    if (recording.active == 2) {
        /* Exported once, after measuring; the status line tells the runner the
         * outcome so a failed export is never mistaken for a missing run. */
        char result[160];
        int ok=1;
        FILE *out = fopen(recording.path, "w");
        if (!out) ok=0;
        else {
            fprintf(out, "# gevr-profile-v2 label=%s rows=%u overflow=%u detail=%u\n", recording.label, recording.rows, recording.overflow, recording.detail);
            fprintf(out, "start_ns,submit_ns,display_ns,period_ns,kind,body_valid,camera_valid,reset,detailed,errors,work_ns,pre_ns,draws,vertices,tex_cache_allocs,upload_bytes,cache_hits,cache_misses,image_lifetime_ns,input_buttons,input_x,input_y,input_turn,collision,move_attempted,move_accepted,requested_x,requested_z,actual_x,actual_z");
            for (unsigned j=0;j<GEVR_TIME_COUNT;j++) fprintf(out, ",%s_ns,%s_self_ns,%s_pre_ns",gevrFrameTimingSectionName(j),gevrFrameTimingSectionName(j),gevrFrameTimingSectionName(j));
            fputc('\n',out);
            for (unsigned i=0;i<recording.rows;i++) {
                const GevrFrameTimingSample *s=&trace[i];
                uint64_t pre=0;for(unsigned j=0;j<GEVR_TIME_COUNT;j++) pre+=s->pre[j];
                fprintf(out,"%llu,%llu,%lld,%lld,%u,%u,%u,%u,%u,%u,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%x,%d,%d,%d,%u,%x,%x,%.4f,%.4f,%.4f,%.4f",
                    (unsigned long long)s->start,(unsigned long long)s->submit,(long long)s->display,(long long)s->period,
                    s->kind,s->bodyValid,s->cameraValid,s->resetReason,s->detailed,s->timingErrors,
                    (unsigned long long)s->work,(unsigned long long)pre,(unsigned long long)s->draws,(unsigned long long)s->vertices,
                    (unsigned long long)s->allocations,(unsigned long long)s->uploadBytes,(unsigned long long)s->cacheHits,(unsigned long long)s->cacheMisses,(unsigned long long)s->imageLifetime,
                    s->inputButtons,s->inputX,s->inputY,s->inputTurn,
                    s->collision,s->moveAttempted,s->moveAccepted,s->requested[0],s->requested[2],s->actual[0],s->actual[2]);
                for(unsigned j=0;j<GEVR_TIME_COUNT;j++) fprintf(out,",%llu,%llu,%llu",(unsigned long long)s->cpu[j],(unsigned long long)s->self[j],(unsigned long long)s->pre[j]);
                fputc('\n',out);
            }
            ok=!ferror(out);
            ok&=fclose(out)==0;
        }
        char gpuPath[800];snprintf(gpuPath,sizeof(gpuPath),"%s.gpu.csv",recording.path);
        FILE *gpu=ok ? fopen(gpuPath,"w") : NULL;
        if(gpu) {
            fprintf(gpu,"# gpu async arrival-time samples; overflow=%u\nready_ns,kind,gpu_ns\n",gpuOverflow);
            for(unsigned i=0;i<gpuRows;i++) fprintf(gpu,"%llu,%u,%llu\n",(unsigned long long)gpuTrace[i].ready,gpuTrace[i].kind,(unsigned long long)gpuTrace[i].ns);
            ok&=!ferror(gpu);
            ok&=fclose(gpu)==0;
        } else if(ok) ok=0;
        if(ok) snprintf(result,sizeof(result),"done %s %u %u",recording.label,recording.rows,recording.overflow);
        else snprintf(result,sizeof(result),"failed %s csv-write",recording.label);
        traceStatus(marker,result);
        recording.active=0;
        return;
    }
    if (recording.active || (++polls % 60)) return;
    FILE *in=fopen(marker,"r");
    if(!in) return;
    char command[16], label[64]; unsigned warm=15, seconds=60, detail=1;
    const int fields=fscanf(in,"%15s %63s %u %u %u",command,label,&warm,&seconds,&detail);
    fclose(in);remove(marker);
    if(fields!=5 || strcmp(command,"record") || warm>120 || seconds<1 || seconds>60 || detail>1) return;
    for(unsigned i=0;label[i];i++) if(!((label[i]>='a'&&label[i]<='z')||(label[i]>='A'&&label[i]<='Z')||(label[i]>='0'&&label[i]<='9')||label[i]=='-'||label[i]=='_')) return;
    memset(&recording,0,sizeof(recording));
    if(snprintf(recording.path,sizeof(recording.path),"%s.%s.csv",marker,label)>=(int)sizeof(recording.path)) return;
    memcpy(recording.label,label,strlen(label)+1);
    recording.detail=detail;recording.active=1;
    recording.measureStart=rawNow()+(uint64_t)warm*1000000000ull;
    recording.measureEnd=recording.measureStart+(uint64_t)seconds*1000000000ull;
    gpuRows=gpuOverflow=0;
    char ack[160];
    snprintf(ack,sizeof(ack),"%s %llu %llu",label,(unsigned long long)recording.measureStart,(unsigned long long)recording.measureEnd);
    traceStatus(marker,ack);
}
void gevrFrameTimingInput(unsigned buttons, int x, int y, int turn) {
    injected.buttons=buttons;injected.x=x;injected.y=y;injected.turn=turn;
}
void gevrFrameTimingRedrawResult(int redrawn) {
    if(!redrawn && frame.kind==2) frame.kind=0;
}

void gevrFrameTimingEnable(int value)
{
    value = value != 0;
    if (value == enabled) return;
    enabled = value;
    memset(&frame, 0, sizeof(frame));
    memset(&previous, 0, sizeof(previous));
    memset(&window, 0, sizeof(window));
    haveBody = haveCamera = sampledBody = sampledCamera = 0;
    depth=0;memset(pending,0,sizeof(pending));
}
uint64_t gevrFrameTimingNow(void)
{
    if (!enabled) return 0;
    return rawNow();
}
void gevrFrameTimingBegin(uint64_t now)
{
    memset(&frame, 0, sizeof(frame));
    sampledBody = sampledCamera = 0;
    depth=0;
    if (enabled) {
        frame.start = now;
        memcpy(frame.pre,pending,sizeof(pending));memset(pending,0,sizeof(pending));
        frame.detailed = ((frameIndex++ % 16)==0) && (!recording.active || recording.detail);
        spans[depth].section=GEVR_TIME_FRAME;spans[depth].start=now;spans[depth].child=0;spans[depth++].token=++serial;
    }
}
uint64_t gevrFrameTimingEnter(GevrFrameTimingSection section) {
    if(!enabled || !frame.start || section<0 || section>=GEVR_TIME_COUNT) return 0;
    if(section>=GEVR_TIME_DL && !frame.detailed) return 0;
    if(depth==32) { frame.timingErrors++;return 0; }
    spans[depth].section=section;spans[depth].start=rawNow();spans[depth].child=0;
    spans[depth].token=++serial;
    return spans[depth++].token;
}
void gevrFrameTimingLeave(uint64_t token) {
    if(!token || !enabled || !frame.start) return;
    if(!depth || spans[depth-1].token!=token) { frame.timingErrors++;return; }
    const unsigned i=--depth;
    const uint64_t now=rawNow(), ns=now>=spans[i].start ? now-spans[i].start : 0;
    frame.cpu[spans[i].section]+=ns;
    if(spans[i].section==GEVR_TIME_FRESH) frame.kind=1;
    if(spans[i].section==GEVR_TIME_REDRAW) frame.kind=2;
    frame.self[spans[i].section]+=ns>spans[i].child ? ns-spans[i].child : 0;
    if(depth) spans[depth-1].child+=ns;
}
void gevrFrameTimingImageLifetime(uint64_t ns) { if(enabled && frame.start) frame.imageLifetime=ns; }
void gevrFrameTimingOutside(GevrFrameTimingSection section,uint64_t ns) {
    if(enabled && section>=0 && section<GEVR_TIME_COUNT) pending[section]+=ns;
}
void gevrFrameTimingCounters(uint64_t draws,uint64_t vertices,uint64_t allocations,uint64_t bytes,uint64_t hits,uint64_t misses) {
    if(!enabled || !frame.start) return;
    frame.draws+=draws;frame.vertices+=vertices;frame.allocations+=allocations;
    frame.uploadBytes+=bytes;frame.cacheHits+=hits;frame.cacheMisses+=misses;
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
    frame.self[section] += ns;
    if(depth) spans[depth-1].child+=ns;
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
        haveBody = haveCamera = 0;
        frame.start = 0;
        depth=0;
        return;
    }
    frame.submit = now;
    frame.inputButtons=injected.buttons;frame.inputX=injected.x;frame.inputY=injected.y;frame.inputTurn=injected.turn;
    if(depth!=1) frame.timingErrors++;
    if(depth) {
        frame.cpu[GEVR_TIME_FRAME]=now-frame.start;
        frame.self[GEVR_TIME_FRAME]=now-frame.start>spans[0].child ? now-frame.start-spans[0].child : 0;
    }
    depth=0;
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
    if (frame.cameraValid) {
        float step = 0, peak = 0;
        for (int i = 0; i < 3; i++) {
            step += frame.cameraStep[i] * frame.cameraStep[i];
            peak += window.motionFrame.cameraStep[i] * window.motionFrame.cameraStep[i];
        }
        if (!window.motionFrame.cameraValid || step > peak) window.motionFrame = frame;
    }
    for (int i = 0; i < GEVR_TIME_COUNT; i++)
        if (frame.cpu[i] > window.peak[i]) window.peak[i] = frame.cpu[i];
    if (frame.collision) {
        const float rx = frame.requested[0], rz = frame.requested[2];
        const float ax = frame.actual[0], az = frame.actual[2];
        if (rx*rx + rz*rz >= 1.0f) {
            window.moveTicks++;
            if (ax*ax + az*az <= 0.0001f) window.stoppedTicks++;
            else if ((rx-ax)*(rx-ax) + (rz-az)*(rz-az) > 0.0001f) window.clippedTicks++;
        }
        if (window.collisionCount < 64) {
            GevrCollisionTick *tick = &window.collisionTicks[window.collisionCount++];
            tick->display = frame.display;
            tick->requested[0] = rx; tick->requested[1] = rz;
            tick->actual[0] = ax; tick->actual[1] = az;
            memcpy(tick->edge, frame.collisionEdge, sizeof(tick->edge));
            tick->attempted = frame.moveAttempted; tick->accepted = frame.moveAccepted;
            tick->calls = frame.moveCalls; tick->scoot = frame.scootCalls;
        } else window.collisionOverflow++;
    }
    window.frames++;
    if(recording.active==1 && now>=recording.measureStart) {
        if(now>=recording.measureEnd) recording.active=2;
        else if(recording.rows<GEVR_TRACE_CAPACITY) trace[recording.rows++]=frame;
        else recording.overflow++;
    }
    previous = frame;
    frame.start = 0;
}
void gevrFrameTimingGpu(double ms, int redraw)
{
    if (!enabled || !isfinite(ms) || ms < 0) return;
    const int kind = redraw != 0;
    const uint64_t now=rawNow();
    if(recording.active==1 && now>=recording.measureStart && now<recording.measureEnd) {
        if(gpuRows<GEVR_TRACE_CAPACITY) { gpuTrace[gpuRows].ready=now;gpuTrace[gpuRows].ns=(uint64_t)(ms*1e6);gpuTrace[gpuRows++].kind=kind?2:1; }
        else gpuOverflow++;
    }
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
        "kind=%s work=%.3f between=%.3f predict_step=%.3f wait=%.3f begin=%.3f poses=%.3f acquire=%.3f image_wait=%.3f setup=%.3f fresh=%.3f redraw=%.3f vertex_wait=%.3f layers=%.3f release=%.3f submit=%.3f throttle=%.3f draw_batch=%.3f draw_issue=%.3f shader_bind=%.3f shader_compile=%.3f texture_upload=%.3f gpu_poll=%.3f hud_readback=%.3f",
        s->kind == 1 ? "fresh" : s->kind == 2 ? "redraw" : "other",
        s->work / 1e6, s->between / 1e6, s->predictedStep / 1e6,
        s->cpu[GEVR_TIME_WAIT] / 1e6, s->cpu[GEVR_TIME_BEGIN] / 1e6, s->cpu[GEVR_TIME_POSES] / 1e6,
        s->cpu[GEVR_TIME_ACQUIRE] / 1e6, s->cpu[GEVR_TIME_IMAGE_WAIT] / 1e6, s->cpu[GEVR_TIME_EYE_SETUP] / 1e6,
        s->cpu[GEVR_TIME_FRESH] / 1e6, s->cpu[GEVR_TIME_REDRAW] / 1e6, s->cpu[GEVR_TIME_VERTEX_WAIT] / 1e6,
        s->cpu[GEVR_TIME_LAYERS] / 1e6, s->cpu[GEVR_TIME_RELEASE] / 1e6, s->cpu[GEVR_TIME_SUBMIT] / 1e6,
        s->cpu[GEVR_TIME_THROTTLE] / 1e6, s->cpu[GEVR_TIME_DRAW_BATCH] / 1e6,
        s->cpu[GEVR_TIME_DRAW_ISSUE] / 1e6, s->cpu[GEVR_TIME_SHADER_BIND] / 1e6,
        s->cpu[GEVR_TIME_SHADER_COMPILE] / 1e6, s->cpu[GEVR_TIME_TEXTURE_UPLOAD] / 1e6,
        s->cpu[GEVR_TIME_GPU_POLL] / 1e6, s->cpu[GEVR_TIME_HUD_READBACK] / 1e6);
}

void gevrFrameTimingCollision(const float physical[3], const float requested[3], const float actual[3], int reset)
{
    if (!enabled || !frame.start) return;
    memcpy(frame.physical, physical, sizeof(frame.physical));
    memcpy(frame.requested, requested, sizeof(frame.requested));
    memcpy(frame.actual, actual, sizeof(frame.actual));
    frame.collision = 1;
    frame.physicalReset = reset != 0;
}
void gevrFrameTimingMoveBegin(int allowScoot)
{
    if (!enabled || !frame.start) return;
    frame.moveCalls++;
    frame.scootCalls += allowScoot != 0;
}
void gevrFrameTimingMoveResult(GevrMoveAttempt kind, int result, const float edge0[3], const float edge1[3])
{
    if (!enabled || !frame.start || kind < GEVR_MOVE_SIMPLE || kind > GEVR_MOVE_PRECISION) return;
    const unsigned bit = 1u << kind;
    /* A successful simple move never initialized its output collision edge. */
    if (kind == GEVR_MOVE_SIMPLE && result == 0 &&
        frame.collisionEdge[0] == 0 && frame.collisionEdge[1] == 0) {
        const float x = edge1[0] - edge0[0], z = edge1[2] - edge0[2];
        const float length = sqrtf(x*x + z*z);
        if (isfinite(length) && length > 0.0001f) {
            frame.collisionEdge[0] = x / length;
            frame.collisionEdge[1] = z / length;
        }
    }
    frame.moveAttempted |= bit;
    if (result > 0) frame.moveAccepted |= bit;
}
void gevrFrameTimingFormatCollision(const GevrFrameTimingWindow *w, unsigned first, char *out, size_t size)
{
    if (!size) return;
    size_t used = 0;
    out[0] = 0;
    for (unsigned i = first; i < w->collisionCount && i < first + 8; i++) {
        const GevrCollisionTick *t = &w->collisionTicks[i];
        const int n = snprintf(out + used, size - used,
            "%s[%u %.2f r %.3f,%.3f a %.3f,%.3f e %.3f,%.3f p %x/%x c %u/%u]",
            used ? " " : "", i, (t->display - w->collisionTicks[0].display) / 1e6,
            t->requested[0], t->requested[1], t->actual[0], t->actual[1],
            t->edge[0], t->edge[1], t->attempted, t->accepted, t->calls, t->scoot);
        if (n < 0 || (size_t)n >= size - used) return;
        used += (size_t)n;
    }
}
void gevrFrameTimingSnapshot(const float body[3], const float root[3])
{
    if (!enabled || !frame.start || sampledBody) return;
    sampledBody = 1;
    frame.bodyValid = haveBody;
    for (int i = 0; i < 3; i++) {
        if (haveBody) {
            frame.bodyStep[i] = body[i] - lastBody[i];
            frame.rootStep[i] = root[i] - lastRoot[i];
        }
        lastBody[i] = body[i]; lastRoot[i] = root[i];
    }
    haveBody = 1;
}
void gevrFrameTimingCamera(const float position[3])
{
    if (!enabled || !frame.start || sampledCamera) return;
    sampledCamera = 1;
    frame.cameraValid = haveCamera;
    for (int i = 0; i < 3; i++) {
        if (haveCamera) frame.cameraStep[i] = position[i] - lastCamera[i];
        lastCamera[i] = position[i];
    }
    haveCamera = 1;
}
void gevrFrameTimingResetMotion(unsigned reason, int discontinuity)
{
    if (!enabled) return;
    if (frame.start) frame.resetReason = reason;
    if (discontinuity) haveBody = haveCamera = 0;
}
void gevrFrameTimingFormatMotion(const GevrFrameTimingSample *s, char *out, size_t size)
{
    snprintf(out, size,
        "display=%lld collision=%u physical_reset=%u reset_reason=%u body_valid=%u camera_valid=%u physical_cm=%.4f,%.4f,%.4f requested_cm=%.4f,%.4f,%.4f actual_cm=%.4f,%.4f,%.4f body_step_cm=%.4f,%.4f,%.4f root_step_cm=%.4f,%.4f,%.4f camera_step_cm=%.4f,%.4f,%.4f",
        (long long)s->display, s->collision, s->physicalReset, s->resetReason, s->bodyValid, s->cameraValid,
        s->physical[0], s->physical[1], s->physical[2], s->requested[0], s->requested[1], s->requested[2],
        s->actual[0], s->actual[1], s->actual[2], s->bodyStep[0], s->bodyStep[1], s->bodyStep[2],
        s->rootStep[0], s->rootStep[1], s->rootStep[2], s->cameraStep[0], s->cameraStep[1], s->cameraStep[2]);
}
