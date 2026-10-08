#ifndef GEVR_NET_SPATIAL_H
#define GEVR_NET_SPATIAL_H
/* One binaural voice per player slot (net_protocol.h GEVR_MAX_PLAYERS), then
 * the placed game sounds' (gevr_sndpath.c) */
#define NET_SPATIAL_SFX_FIRST 8
#define NET_SPATIAL_SFX_SLOTS 16
#define NET_SPATIAL_SLOTS (NET_SPATIAL_SFX_FIRST + NET_SPATIAL_SFX_SLOTS)
int netSpatialInit(void);
int netSpatialLastError(void);
void netSpatialShutdown(void);
void netSpatialResetSlot(unsigned slot);
/* One sample through a persistent 256-frame adapter. direction is head-relative,
 * +X right, +Y up, -Z forward. Centered sources bypass the HRTF. */
void netSpatialSample(unsigned slot, float sample, const float direction[3], int positioned, float *left, float *right);
#endif
