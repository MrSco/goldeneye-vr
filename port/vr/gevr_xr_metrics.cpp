#include "gevr_xr_metrics.h"
#include "gevr_frame_timing.h"
#include "vr_log.h"
#include <vector>
#include <string>

static XrInstance instance;
static XrSession session;
static PFN_xrGetPerformanceMetricsStateMETA getState;
static PFN_xrSetPerformanceMetricsStateMETA setState;
static PFN_xrQueryPerformanceMetricsCounterMETA query;
static std::vector<std::pair<XrPath,std::string>> counters;
static bool changed;
static uint64_t last;
void gevrXrMetricsReset() {
    if(changed && session && setState) {
        XrPerformanceMetricsStateMETA state{XR_TYPE_PERFORMANCE_METRICS_STATE_META};
        state.enabled=XR_FALSE;setState(session,&state);
    }
    changed=false;session=XR_NULL_HANDLE;instance=XR_NULL_HANDLE;
    getState=nullptr;setState=nullptr;query=nullptr;counters.clear();last=0;
}
void gevrXrMetricsInit(XrInstance inst,XrSession sess) {
    gevrXrMetricsReset();instance=inst;session=sess;
    PFN_xrEnumeratePerformanceMetricsCounterPathsMETA enumerate=nullptr;
    xrGetInstanceProcAddr(inst,"xrEnumeratePerformanceMetricsCounterPathsMETA",(PFN_xrVoidFunction*)&enumerate);
    xrGetInstanceProcAddr(inst,"xrGetPerformanceMetricsStateMETA",(PFN_xrVoidFunction*)&getState);
    xrGetInstanceProcAddr(inst,"xrSetPerformanceMetricsStateMETA",(PFN_xrVoidFunction*)&setState);
    xrGetInstanceProcAddr(inst,"xrQueryPerformanceMetricsCounterMETA",(PFN_xrVoidFunction*)&query);
    if(!enumerate||!getState||!setState||!query) { vr_log("xr-metrics: unavailable APIs");return; }
    uint32_t count=0;
    if(XR_FAILED(enumerate(inst,0,&count,nullptr))||count>256) { vr_log("xr-metrics: unavailable enumeration");return; }
    std::vector<XrPath> paths(count);
    if(XR_FAILED(enumerate(inst,count,&count,paths.data()))) { vr_log("xr-metrics: unavailable enumeration data");return; }
    for(XrPath path:paths) {
        char name[256];uint32_t length=0;
        if(XR_SUCCEEDED(xrPathToString(inst,path,sizeof(name),&length,name))) {
            counters.emplace_back(path,name);vr_log("xr-metrics-counter: %s",name);
        }
    }
    vr_log("xr-metrics: discovered %u counters",(unsigned)counters.size());
}
void gevrXrMetricsPoll(bool active) {
    if(!active||!getState||!setState||!query||counters.empty()) return;
    const uint64_t now=gevrFrameTimingNow();
    if(!now||now-last<1000000000ull) return;
    last=now;
    const uint64_t pollStart=now;
    XrPerformanceMetricsStateMETA state{XR_TYPE_PERFORMANCE_METRICS_STATE_META};
    if(XR_FAILED(getState(session,&state))) { vr_log("xr-metrics: unavailable state");return; }
    if(!state.enabled) {
        state.enabled=XR_TRUE;
        if(XR_FAILED(setState(session,&state))) { vr_log("xr-metrics: unavailable enable");return; }
        changed=true;
    }
    for(const auto &counter:counters) {
        if(counter.second.find("/app_")==std::string::npos && counter.second.find("/compositor_")==std::string::npos) continue;
        XrPerformanceMetricsCounterMETA value{XR_TYPE_PERFORMANCE_METRICS_COUNTER_META};
        const XrResult result=query(session,counter.first,&value);
        if(XR_FAILED(result)||!(value.counterFlags&XR_PERFORMANCE_METRICS_COUNTER_ANY_VALUE_VALID_BIT_META)) {
            vr_log("xr-metric: time=%llu path=%s unavailable result=%d",(unsigned long long)now,counter.second.c_str(),(int)result);
        } else if(value.counterFlags&XR_PERFORMANCE_METRICS_COUNTER_UINT_VALUE_VALID_BIT_META) {
            vr_log("xr-metric: time=%llu path=%s unit=%d uint=%u",(unsigned long long)now,counter.second.c_str(),value.counterUnit,value.uintValue);
        } else if(value.counterFlags&XR_PERFORMANCE_METRICS_COUNTER_FLOAT_VALUE_VALID_BIT_META) {
            vr_log("xr-metric: time=%llu path=%s unit=%d float=%.6f",(unsigned long long)now,counter.second.c_str(),value.counterUnit,value.floatValue);
        }
    }
    gevrFrameTimingOutside(GEVR_TIME_METRICS,gevrFrameTimingNow()-pollStart);
}
