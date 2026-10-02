#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#define GEVR_ACTION_DAMAGE_BULLET    1001
#define GEVR_ACTION_DAMAGE_EXPLOSION 1002
#define GEVR_ACTION_GRENADE_COOK     1003   // each pulse while a grenade cooks (bondview2.c)

typedef enum {
    HAPTIC_CAT_PISTOLS = 0,
    HAPTIC_CAT_AUTOMATICS,
    HAPTIC_CAT_HEAVY,
    HAPTIC_CAT_MELEE_THROWN,
    HAPTIC_CAT_GADGETS,
    HAPTIC_CAT_DAMAGE,
    HAPTIC_CAT_COUNT
} HapticCategory;

typedef struct {
    int id;
    const char *name;
    const char *iniKey;
    HapticCategory category;
    int intensity;          // 0 to 10
    int defaultIntensity;   // 0 to 10
    int durationMs;         // 10 to 500 ms (or 0 if off)
    int defaultDurationMs;  // 10 to 500 ms
    float frequencyHz;      // Calibrated motor frequency
} HapticProfile;

void vrHapticsInit(void);
void vrHapticsResetAll(void);
void vrHapticsResetCategory(HapticCategory cat);
int vrHapticsGetCount(void);
HapticProfile *vrHapticsGetProfileByIndex(int index);
HapticProfile *vrHapticsGetProfile(int id);

// Fills out amplitude (0..1), duration (seconds), and frequency (Hz)
void vrHapticsGetRumble(int id, float *out_amplitude, float *out_duration, float *out_frequency);

// Persistence
void vrHapticsSaveIni(void *file_handle);
int vrHapticsLoadLine(const char *key, const char *val);

// Real-time test trigger inside launcher
void vrHapticsTriggerTest(int id);

// Formatted C-table dump to vr_log for developers to copy-paste into defaults
void vrHapticsDumpCTable(void);

#ifdef __cplusplus
}
#endif
