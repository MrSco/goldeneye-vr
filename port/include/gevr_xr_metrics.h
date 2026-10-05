#ifndef GEVR_XR_METRICS_H
#define GEVR_XR_METRICS_H
#include <openxr/openxr.h>
void gevrXrMetricsInit(XrInstance instance, XrSession session);
void gevrXrMetricsPoll(bool active);
void gevrXrMetricsReset();
#endif
