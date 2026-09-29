#include "vr_haptics.h"
#include "vr_input.h"
#include "vr_log.h"
#include "vr_settings.h"

#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <algorithm>

#ifndef ITEM_UNARMED
#define ITEM_UNARMED        0
#define ITEM_FIST           1
#define ITEM_KNIFE          2
#define ITEM_THROWKNIFE     3
#define ITEM_WPPK           4
#define ITEM_WPPKSIL        5
#define ITEM_TT33           6
#define ITEM_SKORPION       7
#define ITEM_AK47           8
#define ITEM_UZI            9
#define ITEM_MP5K           10
#define ITEM_MP5KSIL        11
#define ITEM_SPECTRE        12
#define ITEM_M16            13
#define ITEM_FNP90          14
#define ITEM_SHOTGUN        15
#define ITEM_AUTOSHOT       16
#define ITEM_SNIPERRIFLE    17
#define ITEM_RUGER          18
#define ITEM_GOLDENGUN      19
#define ITEM_SILVERWPPK     20
#define ITEM_GOLDWPPK       21
#define ITEM_LASER          22
#define ITEM_WATCHLASER     23
#define ITEM_GRENADELAUNCH  24
#define ITEM_ROCKETLAUNCH   25
#define ITEM_GRENADE        26
#define ITEM_TIMEDMINE      27
#define ITEM_PROXIMITYMINE  28
#define ITEM_REMOTEMINE     29
#define ITEM_TRIGGER        30
#define ITEM_TASER          31
#define ITEM_TANKSHELLS     32
#endif

static HapticProfile s_profiles[] = {
    // === Category: Pistols ===
    { ITEM_WPPK,        "PP7 Special Issue",           "PP7",             HAPTIC_CAT_PISTOLS,        2, 2,  50,  50, 240.0f },
    { ITEM_WPPKSIL,     "PP7 (Silenced)",              "PP7_Silenced",    HAPTIC_CAT_PISTOLS,        1, 1,  40,  40, 320.0f },
    { ITEM_SILVERWPPK,  "Silver PP7",                  "Silver_PP7",      HAPTIC_CAT_PISTOLS,        2, 2,  50,  50, 240.0f },
    { ITEM_GOLDWPPK,    "Gold PP7",                    "Gold_PP7",        HAPTIC_CAT_PISTOLS,        2, 2,  50,  50, 240.0f },
    { ITEM_TT33,        "DD44 Dostovei",               "DD44",            HAPTIC_CAT_PISTOLS,        5, 5,  60,  60, 190.0f },
    { ITEM_RUGER,       "Cougar Magnum",               "Cougar_Magnum",   HAPTIC_CAT_PISTOLS,        8, 8, 120, 120, 110.0f },
    { ITEM_GOLDENGUN,   "Golden Gun",                  "Golden_Gun",      HAPTIC_CAT_PISTOLS,        7, 7,  90,  90, 130.0f },

    // === Category: Automatics ===
    { ITEM_SKORPION,    "Klobb",                       "Klobb",           HAPTIC_CAT_AUTOMATICS,     3, 3,  40,  40, 260.0f },
    { ITEM_AK47,        "KF7 Soviet",                  "KF7_Soviet",      HAPTIC_CAT_AUTOMATICS,     6, 6,  70,  70, 160.0f },
    { ITEM_UZI,         "ZMG (9mm)",                   "ZMG_9mm",         HAPTIC_CAT_AUTOMATICS,     3, 3,  50,  50, 210.0f },
    { ITEM_MP5K,        "D5K Deutsche",                "D5K",             HAPTIC_CAT_AUTOMATICS,     3, 3,  50,  50, 210.0f },
    { ITEM_MP5KSIL,     "D5K (Silenced)",              "D5K_Silenced",    HAPTIC_CAT_AUTOMATICS,     2, 2,  50,  50, 280.0f },
    { ITEM_SPECTRE,     "Phantom",                     "Phantom",         HAPTIC_CAT_AUTOMATICS,     3, 3,  50,  50, 200.0f },
    { ITEM_FNP90,       "RCP-90",                      "RCP90",           HAPTIC_CAT_AUTOMATICS,     3, 3,  40,  40, 230.0f },
    { ITEM_M16,         "US AR33 Assault Rifle",       "AR33",            HAPTIC_CAT_AUTOMATICS,     6, 6,  70,  70, 160.0f },

    // === Category: Rifles & Heavy ===
    { ITEM_SNIPERRIFLE, "Sniper Rifle",                "Sniper_Rifle",    HAPTIC_CAT_HEAVY,          7, 7,  90,  90, 130.0f },
    { ITEM_SHOTGUN,     "Shotgun",                     "Shotgun",         HAPTIC_CAT_HEAVY,          9, 9, 120, 120, 110.0f },
    { ITEM_AUTOSHOT,    "Automatic Shotgun",           "Auto_Shotgun",    HAPTIC_CAT_HEAVY,          8, 8, 100, 100, 120.0f },
    { ITEM_GRENADELAUNCH, "Grenade Launcher",          "Grenade_Launcher",HAPTIC_CAT_HEAVY,          8, 8, 140, 140, 100.0f },
    { ITEM_ROCKETLAUNCH,  "Rocket Launcher",           "Rocket_Launcher", HAPTIC_CAT_HEAVY,          8, 8, 150, 150,  90.0f },
    { ITEM_TANKSHELLS,  "Tank Shells",                 "Tank_Shells",     HAPTIC_CAT_HEAVY,         10, 10, 220, 220,  80.0f },

    // === Category: Melee & Thrown ===
    { ITEM_UNARMED,     "Unarmed / Fist",              "Unarmed",         HAPTIC_CAT_MELEE_THROWN,   1, 1,  40,  40, 250.0f },
    { ITEM_KNIFE,       "Hunting Knife",               "Hunting_Knife",   HAPTIC_CAT_MELEE_THROWN,   1, 1,  40,  40, 250.0f },
    { ITEM_THROWKNIFE,  "Throwing Knife",              "Throwing_Knife",  HAPTIC_CAT_MELEE_THROWN,   1, 1,  40,  40, 250.0f },
    { ITEM_GRENADE,     "Hand Grenade",                "Grenade",         HAPTIC_CAT_MELEE_THROWN,   0, 0,   0,   0, 150.0f },
    { ITEM_TIMEDMINE,   "Timed Mine",                  "Timed_Mine",      HAPTIC_CAT_MELEE_THROWN,   0, 0,   0,   0, 150.0f },
    { ITEM_PROXIMITYMINE, "Proximity Mine",            "Proximity_Mine",  HAPTIC_CAT_MELEE_THROWN,   0, 0,   0,   0, 150.0f },
    { ITEM_REMOTEMINE,  "Remote Mine",                 "Remote_Mine",     HAPTIC_CAT_MELEE_THROWN,   0, 0,   0,   0, 150.0f },

    // === Category: Gadgets ===
    { ITEM_TRIGGER,     "Watch Detonator",             "Detonator",       HAPTIC_CAT_GADGETS,        1, 1,  30,  30, 300.0f },
    { ITEM_TASER,       "Taser",                       "Taser",           HAPTIC_CAT_GADGETS,        2, 2,  80,  80, 300.0f },
    { ITEM_LASER,       "Military Laser",              "Military_Laser",  HAPTIC_CAT_GADGETS,        1, 1,  50,  50, 350.0f },
    { ITEM_WATCHLASER,  "Watch Laser",                 "Watch_Laser",     HAPTIC_CAT_GADGETS,        1, 1,  50,  50, 350.0f },

    // === Category: Damage & Actions ===
    { GEVR_ACTION_DAMAGE_BULLET,    "Damage (Bullet Hit)", "Damage_Bullet",   HAPTIC_CAT_DAMAGE,     4, 4, 100, 100, 150.0f },
    { GEVR_ACTION_DAMAGE_EXPLOSION, "Damage (Explosion)",  "Damage_Explosion",HAPTIC_CAT_DAMAGE,     8, 8, 250, 250,  90.0f },
};

static const int kNumProfiles = sizeof(s_profiles) / sizeof(s_profiles[0]);
static bool s_initialized = false;

extern "C" void vrHapticsInit(void) {
    if (s_initialized) return;
    s_initialized = true;
    for (int i = 0; i < kNumProfiles; ++i) {
        s_profiles[i].intensity = s_profiles[i].defaultIntensity;
        s_profiles[i].durationMs = s_profiles[i].defaultDurationMs;
    }
}

extern "C" void vrHapticsResetAll(void) {
    for (int i = 0; i < kNumProfiles; ++i) {
        s_profiles[i].intensity = s_profiles[i].defaultIntensity;
        s_profiles[i].durationMs = s_profiles[i].defaultDurationMs;
    }
}

extern "C" void vrHapticsResetCategory(HapticCategory cat) {
    for (int i = 0; i < kNumProfiles; ++i) {
        if (s_profiles[i].category == cat) {
            s_profiles[i].intensity = s_profiles[i].defaultIntensity;
            s_profiles[i].durationMs = s_profiles[i].defaultDurationMs;
        }
    }
}

extern "C" int vrHapticsGetCount(void) {
    return kNumProfiles;
}

extern "C" HapticProfile *vrHapticsGetProfileByIndex(int index) {
    if (index >= 0 && index < kNumProfiles) {
        return &s_profiles[index];
    }
    return nullptr;
}

extern "C" HapticProfile *vrHapticsGetProfile(int id) {
    if (id == ITEM_FIST) id = ITEM_UNARMED;
    for (int i = 0; i < kNumProfiles; ++i) {
        if (s_profiles[i].id == id) {
            return &s_profiles[i];
        }
    }
    return nullptr;
}

extern "C" void vrHapticsGetRumble(int id, float *out_amplitude, float *out_duration, float *out_frequency) {
    HapticProfile *p = vrHapticsGetProfile(id);
    if (!p) {
        if (out_amplitude) *out_amplitude = 0.40f;
        if (out_duration)  *out_duration  = 0.06f;
        if (out_frequency) *out_frequency = 180.0f;
        return;
    }

    if (out_amplitude) *out_amplitude = (float)p->intensity / 10.0f;
    if (out_duration)  *out_duration  = (float)p->durationMs / 1000.0f;
    if (out_frequency) *out_frequency = p->frequencyHz;
}

extern "C" void vrHapticsSaveIni(void *file_handle) {
    FILE *f = (FILE *)file_handle;
    if (!f) return;

    fprintf(f, "\n[Haptics]\n");
    fprintf(f, "; GoldenEye VR custom haptics: Intensity (0..10), Duration (ms)\n");
    for (int i = 0; i < kNumProfiles; ++i) {
        fprintf(f, "%s=%d,%d\n", s_profiles[i].iniKey, s_profiles[i].intensity, s_profiles[i].durationMs);
    }
}

extern "C" int vrHapticsLoadLine(const char *key, const char *val) {
    if (!key || !val) return 0;

    for (int i = 0; i < kNumProfiles; ++i) {
        if (strcmp(s_profiles[i].iniKey, key) == 0) {
            int intensity = 0;
            int durationMs = 0;
            if (sscanf(val, "%d,%d", &intensity, &durationMs) == 2) {
                s_profiles[i].intensity = std::max(0, std::min(10, intensity));
                s_profiles[i].durationMs = std::max(0, std::min(500, durationMs));
                return 1;
            } else if (sscanf(val, "%d", &intensity) == 1) {
                s_profiles[i].intensity = std::max(0, std::min(10, intensity));
                return 1;
            }
        }
    }
    return 0;
}

extern "C" void vrHapticsTriggerTest(int id) {
    if (!vr_haptics_ready()) {
        return;
    }

    HapticProfile *p = vrHapticsGetProfile(id);
    if (!p) return;

    float amp = (float)p->intensity / 10.0f;
    float dur = (float)p->durationMs / 1000.0f;
    float freq = p->frequencyHz;

    if (amp <= 0.001f || dur <= 0.001f) {
        return;
    }

    if (id == GEVR_ACTION_DAMAGE_BULLET || id == GEVR_ACTION_DAMAGE_EXPLOSION) {
        // Full body impact feel: pulse both hands simultaneously
        trigger_haptic_vibration_freq_c(0, amp, dur, freq);
        trigger_haptic_vibration_freq_c(1, amp, dur, freq);
    } else {
        // Weapon pulse in dominant hand
        int targetHand = VrLeftHandedMode ? 0 : 1;
        trigger_haptic_vibration_freq_c(targetHand, amp, dur, freq);
    }
}

extern "C" void vrHapticsDumpCTable(void) {
    vr_log("=== GoldenEye VR Haptic Settings Dump (C Defaults Format) ===");
    vr_log("/* Copy-paste directly into s_profiles in vr_haptics.cpp */");
    for (int i = 0; i < kNumProfiles; ++i) {
        const HapticProfile &p = s_profiles[i];
        vr_log("    { %-20s \"%-28s\", \"%-16s\", %d, %2d, %2d, %3d, %3d, %5.1ff },",
               p.id == GEVR_ACTION_DAMAGE_BULLET ? "GEVR_ACTION_DAMAGE_BULLET," :
               p.id == GEVR_ACTION_DAMAGE_EXPLOSION ? "GEVR_ACTION_DAMAGE_EXPLOSION," : "ITEM_ID,",
               p.name, p.iniKey, (int)p.category,
               p.intensity, p.defaultIntensity,
               p.durationMs, p.defaultDurationMs,
               p.frequencyHz);
    }
    vr_log("=============================================================");
}
