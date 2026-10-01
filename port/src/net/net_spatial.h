#ifndef GEVR_NET_SPATIAL_H
#define GEVR_NET_SPATIAL_H
int netSpatialInit(void);
int netSpatialLastError(void);
void netSpatialShutdown(void);
void netSpatialResetSlot(unsigned slot);
/* One sample through a persistent 256-frame adapter. direction is head-relative,
 * +X right, +Y up, -Z forward. Centered sources bypass the HRTF. */
void netSpatialSample(unsigned slot, float sample, const float direction[3], int positioned, float *left, float *right);
#endif
