/* Production gevr_pm_next_segment (inserted by test_frame_timing.py) against a
 * mock GPU: a segment is rewritten only after its fence signaled or the GPU was
 * drained, and a fence is never deleted while it is still being waited on. */
#include "gevr_vertex_ownership.h"
#include <cassert>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
typedef struct Fence *GLsync;
typedef intptr_t GLsizeiptr;
enum { GL_SYNC_GPU_COMMANDS_COMPLETE = 0x9117, GL_SYNC_FLUSH_COMMANDS_BIT = 1 };
enum { LOG_WARNING = 2, GEVR_TIME_VERTEX_WAIT = 8 };
struct Fence { int id; bool live; };
static std::vector<std::string> events;
static Fence fences[64];
static int nextFence;
static bool failCreate;
static unsigned waitResult = GEVR_FENCE_ALREADY;
static int warnings, finishes;
static GLsync glFenceSync(unsigned condition, unsigned flags) {
    assert(condition == GL_SYNC_GPU_COMMANDS_COMPLETE && flags == 0);
    if (failCreate) { events.push_back("create-failed"); return nullptr; }
    Fence *f = &fences[nextFence++];
    f->id = nextFence; f->live = true;
    events.push_back("create" + std::to_string(f->id));
    return f;
}
static void glDeleteSync(GLsync f) {
    assert(f && f->live);
    f->live = false;
    events.push_back("delete" + std::to_string(f->id));
}
static unsigned glClientWaitSync(GLsync f, unsigned flags, uint64_t timeout) {
    assert(f && f->live && flags == GL_SYNC_FLUSH_COMMANDS_BIT && timeout == GEVR_VERTEX_FENCE_TIMEOUT_NS);
    events.push_back("wait" + std::to_string(f->id));
    return waitResult;
}
static void glFinish(void) { finishes++; events.push_back("finish"); }
static void sysLogPrintf(int level, const char *, ...) { assert(level == LOG_WARNING); warnings++; }
static uint64_t gevrFrameTimingNow(void) { return 0; }
static void gevrFrameTimingAdd(int section, uint64_t) { assert(section == GEVR_TIME_VERTEX_WAIT); }
#define GEVR_PM_SEGMENTS 3
static GLsizeiptr s_pmSegSize = 100, s_pmOff;
static int s_pmSeg = -1;
static GLsync s_pmFence[GEVR_PM_SEGMENTS];
/* INSERT_RING */
static std::string take() {
    std::string all;
    for (auto &e : events) all += (all.empty() ? "" : " ") + e;
    events.clear();
    return all;
}
int main() {
    gevr_pm_next_segment();                    /* first frame: nothing to close or wait on */
    assert(s_pmSeg == 0 && s_pmOff == 0 && take().empty());
    gevr_pm_next_segment();
    gevr_pm_next_segment();
    assert(take() == "create1 create2" && s_pmSeg == 2 && s_pmOff == 200);
    gevr_pm_next_segment();                    /* wrap: wait for segment 0 before reuse */
    assert(take() == "create3 wait1 delete1" && s_pmSeg == 0 && s_pmFence[0] == nullptr);
    assert(finishes == 0 && warnings == 0);

    waitResult = GEVR_FENCE_TIMEOUT;           /* GPU wedged: drain, then free */
    gevr_pm_next_segment();
    assert(take() == "create4 wait2 finish delete2" && finishes == 1 && warnings == 1);
    waitResult = GEVR_FENCE_FAILED;
    gevr_pm_next_segment();
    assert(take() == "create5 wait3 finish delete3" && finishes == 2 && warnings == 2);
    waitResult = GEVR_FENCE_SATISFIED;

    failCreate = true;                         /* no fence: drain before leaving the segment */
    gevr_pm_next_segment();
    assert(take() == "create-failed finish wait4 delete4" && finishes == 3);
    assert(warnings == 2);                     /* third fallback is rate limited (logs 1,2,4,8...) */
    failCreate = false;
    gevr_pm_next_segment();
    gevr_pm_next_segment();                    /* the unfenced segment needs no wait now */
    assert(take() == "create6 wait5 delete5 create7" && s_pmSeg == 2);
    gevr_pm_next_segment();
    assert(take() == "create8 wait6 delete6");
    for (auto &f : fences) assert(!f.live || f.id >= 7);
    puts("PASS: vertex ring reuses segments only after their fence or a full GPU drain");
    return 0;
}
