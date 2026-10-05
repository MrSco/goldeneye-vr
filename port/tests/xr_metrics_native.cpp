/* Production gevr_xr_metrics.cpp against a mocked XR_META_performance_metrics
 * runtime: missing APIs, refused enumeration, discovery, once-a-second
 * polling only while timing is enabled, and restoring the runtime's state. */
#include "gevr_xr_metrics.h"
#include "gevr_frame_timing.h"
#include <cassert>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

static std::vector<std::string> logs;
extern "C" void vr_log(const char *format, ...) {
    char text[2048];
    va_list args; va_start(args, format); vsnprintf(text, sizeof(text), format, args); va_end(args);
    logs.push_back(text);
}
static uint64_t clockNs;
static uint64_t outside;
extern "C" uint64_t gevrFrameTimingNow(void) { return clockNs; }
extern "C" void gevrFrameTimingOutside(GevrFrameTimingSection section, uint64_t ns) {
    assert(section == GEVR_TIME_METRICS); outside += ns + 1;
}

static const char *names[] = {"/perfmetrics_meta/app/cpu_frametime", "/perfmetrics_meta/app/gpu_frametime",
    "/perfmetrics_meta/compositor/dropped_frame_count", "/perfmetrics_meta/device/gpu_utilization"};
static bool haveApis = true;
static uint32_t reportedCount = 4;
static XrBool32 runtimeEnabled;
static int sets, queries;
static const XrSession theSession = (XrSession)(uintptr_t)0x5e55;
static const XrInstance theInstance = (XrInstance)(uintptr_t)0x1257;

static XRAPI_ATTR XrResult XRAPI_CALL enumeratePaths(XrInstance instance, uint32_t capacity, uint32_t *count, XrPath *paths) {
    assert(instance == theInstance);
    *count = reportedCount;
    if (capacity) { assert(capacity >= reportedCount); for (uint32_t i = 0; i < reportedCount; i++) paths[i] = 100 + i; }
    return XR_SUCCESS;
}
static XRAPI_ATTR XrResult XRAPI_CALL getState(XrSession session, XrPerformanceMetricsStateMETA *state) {
    assert(session == theSession && state->type == XR_TYPE_PERFORMANCE_METRICS_STATE_META);
    state->enabled = runtimeEnabled; return XR_SUCCESS;
}
static XRAPI_ATTR XrResult XRAPI_CALL setState(XrSession session, const XrPerformanceMetricsStateMETA *state) {
    assert(session == theSession); runtimeEnabled = state->enabled; sets++; return XR_SUCCESS;
}
static XRAPI_ATTR XrResult XRAPI_CALL queryCounter(XrSession session, XrPath path, XrPerformanceMetricsCounterMETA *value) {
    assert(session == theSession && runtimeEnabled); queries++;
    switch (path) {
    case 100: value->counterFlags = XR_PERFORMANCE_METRICS_COUNTER_ANY_VALUE_VALID_BIT_META | XR_PERFORMANCE_METRICS_COUNTER_FLOAT_VALUE_VALID_BIT_META;
        value->counterUnit = XR_PERFORMANCE_METRICS_COUNTER_UNIT_MILLISECONDS_META; value->floatValue = 4.25f; return XR_SUCCESS;
    case 101: value->counterFlags = 0; return XR_SUCCESS;
    case 102: value->counterFlags = XR_PERFORMANCE_METRICS_COUNTER_ANY_VALUE_VALID_BIT_META | XR_PERFORMANCE_METRICS_COUNTER_UINT_VALUE_VALID_BIT_META;
        value->counterUnit = XR_PERFORMANCE_METRICS_COUNTER_UNIT_GENERIC_META; value->uintValue = 3; return XR_SUCCESS;
    default: return XR_ERROR_VALIDATION_FAILURE;
    }
}
extern "C" XRAPI_ATTR XrResult XRAPI_CALL xrGetInstanceProcAddr(XrInstance instance, const char *name, PFN_xrVoidFunction *function) {
    assert(instance == theInstance);
    *function = nullptr;
    if (!haveApis) return XR_ERROR_FUNCTION_UNSUPPORTED;
    if (!strcmp(name, "xrEnumeratePerformanceMetricsCounterPathsMETA")) *function = (PFN_xrVoidFunction)enumeratePaths;
    else if (!strcmp(name, "xrGetPerformanceMetricsStateMETA")) *function = (PFN_xrVoidFunction)getState;
    else if (!strcmp(name, "xrSetPerformanceMetricsStateMETA")) *function = (PFN_xrVoidFunction)setState;
    else if (!strcmp(name, "xrQueryPerformanceMetricsCounterMETA")) *function = (PFN_xrVoidFunction)queryCounter;
    return *function ? XR_SUCCESS : XR_ERROR_FUNCTION_UNSUPPORTED;
}
extern "C" XRAPI_ATTR XrResult XRAPI_CALL xrPathToString(XrInstance instance, XrPath path, uint32_t capacity, uint32_t *count, char *buffer) {
    assert(instance == theInstance && path >= 100 && path < 104);
    const char *name = names[path - 100];
    *count = (uint32_t)strlen(name) + 1;
    assert(capacity >= *count);
    memcpy(buffer, name, *count);
    return XR_SUCCESS;
}
static bool logged(const char *text) {
    for (auto &line : logs) if (line.find(text) != std::string::npos) return true;
    return false;
}

int main() {
    haveApis = false;                                  /* runtime without the extension */
    gevrXrMetricsInit(theInstance, theSession);
    assert(logged("xr-metrics: unavailable APIs"));
    clockNs = 5000000000ull; gevrXrMetricsPoll(true);
    assert(!queries && !sets);
    gevrXrMetricsReset(); logs.clear();

    haveApis = true; reportedCount = 300;              /* implausible enumeration is refused */
    gevrXrMetricsInit(theInstance, theSession);
    assert(logged("xr-metrics: unavailable enumeration"));
    gevrXrMetricsPoll(true); assert(!queries);
    gevrXrMetricsReset(); logs.clear();

    reportedCount = 4;
    gevrXrMetricsInit(theInstance, theSession);
    assert(logged("xr-metrics-counter: /perfmetrics_meta/compositor/dropped_frame_count"));
    assert(logged("xr-metrics: discovered 4 counters"));
    clockNs = 0; gevrXrMetricsPoll(true);              /* timing disabled: nothing queried */
    assert(!queries && !sets);
    clockNs = 7000000000ull; gevrXrMetricsPoll(false); /* not rendering: nothing queried */
    assert(!queries);
    gevrXrMetricsPoll(true);                           /* enables the runtime once, queries every path */
    assert(sets == 1 && runtimeEnabled && queries == 4 && outside);
    assert(logged("/perfmetrics_meta/app/cpu_frametime=4.2500/u2"));
    assert(logged("/perfmetrics_meta/app/gpu_frametime=unavailable(0)"));
    assert(logged("/perfmetrics_meta/compositor/dropped_frame_count=3/u0"));
    assert(logged("/perfmetrics_meta/device/gpu_utilization=unavailable("));
    clockNs += 500000000ull; gevrXrMetricsPoll(true);  /* at most once a second */
    assert(queries == 4);
    clockNs += 600000000ull; gevrXrMetricsPoll(true);
    assert(queries == 8 && sets == 1);
    gevrXrMetricsReset();                              /* restores the state it changed */
    assert(sets == 2 && !runtimeEnabled);
    gevrXrMetricsReset(); assert(sets == 2);

    runtimeEnabled = XR_TRUE; queries = sets = 0;      /* already enabled by someone else: leave it */
    gevrXrMetricsInit(theInstance, theSession);
    clockNs += 2000000000ull; gevrXrMetricsPoll(true);
    assert(queries == 4 && !sets);
    gevrXrMetricsReset(); assert(!sets && runtimeEnabled);
    puts("PASS: XR performance metrics discovery, unavailable runtimes, 1 Hz polling and state restore");
    return 0;
}
