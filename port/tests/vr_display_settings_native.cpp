#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "vr_settings.h"
#include "gevr_scope.h"
#include "vr_screen.h"
#include "vr_haptics.h"

// Other subsystems' settings symbols and side effects are irrelevant to the round trip.
float XrFov = 90, VrStereoCrosshair = 0.7f, VrHudDistance = 1.0f, VrFistClenchAmt = 0;
bool VrWeaponRecoil = true;
int VrPauseHub = 0, VrLeftHandedMode = 0, VrSwapJoysticks = 0, VrAimSteady = 2, VrShowStats = 0,
    VrGunSizeCheat = 0, VrUnlockAll = 0, VrGunFitArmed = 0;
unsigned long long VrCheatMask = 0;
extern "C" {
float VrScreenDistance = 2, VrScreenFov = 70, VrScreenHeight = 0;
int VrScreenCurved = 1;
int VrScreenPassthrough = 0;
int vr_passthrough_supported(void) { return 1; }
float inputRumbleGetStrength(int) { return 1; }
void inputRumbleSetStrength(int, int) {}
void extTexSetPack(const char *) {}
void videoSetExternalTextures(bool) {}
void set_mTrack2Vol(unsigned short) {}
void musicTrack1ApplySeqpVol(unsigned short) {}
void musicTrack3ApplySeqpVol(unsigned short) {}
void gevrSndApplySfxVolume(unsigned short) {}
void vrHapticsInit() {}
void vrHapticsSaveIni(void *) {}
int vrHapticsLoadLine(const char *, const char *) { return 0; }
void vrHapticsDumpCTable() {}
void vr_log(const char *, ...) {}
void vrSettingsLoad();
void vrSettingsSave();
}
char g_ActiveExtTexPack[256] = {};
unsigned int arc4random_uniform(unsigned int) { return 1; }
/* FIT_SNAPSHOT */

static void newFamilyFits(float base, bool check) {
    const int items[]={14,15,16,17,18,19,22,24,25};
    for(int item:items) for(int component=0;component<10;component++) for(int axis=0;axis<3;axis++) {
        const float value=base+item+component*0.25f+axis*0.125f;
        if(check) assert(VrGexWeaponFits[item][component][axis]==value);
        else VrGexWeaponFits[item][component][axis]=value;
    }
}

int main(int argc, char **argv) {
    const int initial = VrRefreshRate;
#ifdef ANDROID
    assert(initial == 0);
#else
    assert(initial == 90);
#endif
    vrSettingsLoad();
    if (argc > 1 && std::strcmp(argv[1], "rules_write") == 0) {
        VrFastReinforcements = std::atoi(argv[2]);
        VrBodiesStay = std::atoi(argv[3]);
        VrCoopFastReinforcements = std::atoi(argv[4]);
        vrSettingsSave();
        return 0;
    }
    if (argc > 1 && std::strcmp(argv[1], "rules_read") == 0) {
        assert(VrFastReinforcements == std::atoi(argv[2]));
        assert(VrBodiesStay == std::atoi(argv[3]));
        assert(VrCoopFastReinforcements == std::atoi(argv[4]));
        return 0;
    }
    // Gun fit: GoldenEye X's models' own trims and the scopes'
    if (argc > 1 && std::strcmp(argv[1], "fit_invalid") == 0) {
        assert(VrGexWeaponFits[6][3][2] == 8.8f);
        assert(VrGexWeaponFits[1][0][0] == 0);           // unregistered: no default
        assert(VrGexWeaponFits[2][0][0] == 3.4710f);     // the knives keep their default
        assert(VrAimSight == 1);                         // missing key: the crosshair stays on
        assert(VrGexWeaponFits[18][0][0] == 0.9652f);    // the Cougar keeps its default
        return 0;
    }
    if (argc > 1 && std::strcmp(argv[1], "fit_write") == 0) {
        VrAimSight = 0;
        VrGexGunOff[0] = 1.5f;
        VrGexGripTrim[1][3] = 45.0f;
        VrScopeFit[0][0][0] = 0.5f;
        VrScopeFit[1][2][3] = -1.25f;
        VrReloadGrab[1][2] = -9.5f;
        VrReloadBelt[0] = 55.0f;
        VrGexHeldMag[1] = 2.25f;
        VrGexWatch[3] = 1.5f;
        VrGexForeHold[0] = 3.5f;
        VrGexPp7Grab[1] = -1.25f;
        VrGexPp7Support[2] = 2.5f;
        VrGexPp7GunOff[0] = 0.75f;
        VrGexKf7MagOff[0] = -0.5f;
        VrGexPp7MagOff[0] = 1.25f;
        VrGexPp7MagOff[1] = -2.5f;
        VrGexPp7MagOff[2] = 3.75f;
        VrGexPp7SupportRot[0] = 30;
        VrGexPp7SupportRot[1] = -45;
        VrGexPp7SupportRot[2] = 90;
        VrGexKf7WellOff[0] = -0.75f;
        VrGexPp7WellOff[0] = 1.5f;
        VrGexPp7WellOff[1] = -2.25f;
        VrGexPp7WellOff[2] = 3.0f;
        VrMuzzleTrim[0][8][0] = 0.5f;
        VrMuzzleTrim[0][8][2] = 2.0f;
        VrMuzzleTrim[1][8][1] = -1.5f;
        VrMuzzleTrim[1][8][2] = 4.25f;
        VrGexWeaponFits[6][3][2] = 47.25f;
        VrGexWeaponFits[12][6][0] = -3.25f;
        newFamilyFits(-5,false);
        gevrGunFitSaved(false);
        newFamilyFits(99,false);
        VrGexPp7MagOff[0] = 99;
        VrGexKf7MagOff[0] = 99;
        VrGexHeldMag[1] = 99;
        VrGexPp7SupportRot[2] = 99;
        VrGexKf7WellOff[0] = 99;
        VrGexPp7WellOff[1] = 99;
        VrGexWeaponFits[6][3][2] = 99;
        VrGexWeaponFits[12][6][0] = 99;
        gevrGunFitSaved(true);
        newFamilyFits(-5,true);
        assert(VrGexWeaponFits[6][3][2] == 47.25f);
        assert(VrGexWeaponFits[12][6][0] == -3.25f);
        assert(VrGexPp7MagOff[0] == 1.25f && VrGexKf7MagOff[0] == -0.5f && VrGexHeldMag[1] == 2.25f);
        assert(VrGexPp7SupportRot[2] == 90);
        assert(VrGexKf7WellOff[0] == -0.75f && VrGexPp7WellOff[1] == -2.25f);
        vrSettingsSave();
        return 0;
    }
    if (argc > 1 && std::strcmp(argv[1], "fit_read") == 0) {
        newFamilyFits(-5,true);
        assert(VrAimSight == 0);   // "Aim: crosshair" off round trips
        assert(VrGexWeaponFits[6][3][2] == 47.25f);
        assert(VrGexWeaponFits[12][6][0] == -3.25f);
        assert(VrGexGunOff[0] == 1.5f && VrGexGunOff[2] == -19.1169f && VrGunOffX == 2.74f);
        assert(VrGexGripTrim[1][3] == 45.0f && VrGripTrim[1][3] == 86.3f);
        assert(VrScopeFit[0][0][0] == 0.5f && VrScopeFit[1][2][3] == -1.25f && VrScopeFit[0][2][3] == 0.0f);
        assert(VrGexGuns == 0);   // GexGuns is not read as a Gex-prefixed fit
        assert(VrReloadGrab[1][2] == -9.5f && VrReloadGrab[0][2] == -8.0f);
        assert(VrReloadBelt[0] == 55.0f && VrReloadBelt[1] == 19.86f && VrGexHeldMag[1] == 2.25f);
        assert(VrGexWatch[3] == 1.5f && VrGexWatch[0] == 5.18f && VrGexForeHold[0] == 3.5f);
        assert(VrGexPp7Grab[1] == -1.25f && VrGexPp7Support[2] == 2.5f);
        assert(VrGexPp7GunOff[0] == 0.75f);
        assert(VrGexKf7MagOff[0] == -0.5f && VrGexKf7MagOff[1] == 0);
        assert(VrGexPp7MagOff[0] == 1.25f && VrGexPp7MagOff[1] == -2.5f && VrGexPp7MagOff[2] == 3.75f);
        assert(VrGexPp7SupportRot[0] == 30 && VrGexPp7SupportRot[1] == -45 && VrGexPp7SupportRot[2] == 90);
        assert(VrGexKf7WellOff[0] == -0.75f && VrGexKf7WellOff[1] == 0);
        assert(VrGexPp7WellOff[0] == 1.5f && VrGexPp7WellOff[1] == -2.25f && VrGexPp7WellOff[2] == 3.0f);
        assert(VrMuzzleTrim[0][8][0] == 0.5f && VrMuzzleTrim[0][8][2] == 2.0f);
        assert(VrMuzzleTrim[1][8][1] == -1.5f && VrMuzzleTrim[1][8][2] == 4.25f);
        assert(VrMuzzleTrim[0][4][0] == 0.0f);
        return 0;
    }
    /* The fun rules, No radar (16) among them, survive a restart; anything else is off. */
    if (argc > 1 && std::strcmp(argv[1], "fun_write") == 0) {
        VrMpFunFlags = std::atoi(argv[2]);
        vrSettingsSave();
        return 0;
    }
    if (argc > 1 && std::strcmp(argv[1], "fun_read") == 0) {
        assert(VrMpFunFlags == std::atoi(argv[2]));
        return 0;
    }
    if (argc > 1 && std::strcmp(argv[1], "watch_write") == 0) {
        VrWatchFaceStatus = std::atoi(argv[2]);
        VrWatchGesturePause = std::atoi(argv[3]);
        vrSettingsSave();
        return 0;
    }
    if (argc > 1 && std::strcmp(argv[1], "watch_read") == 0) {
        assert(VrWatchFaceStatus == std::atoi(argv[2]));
        assert(VrWatchGesturePause == std::atoi(argv[3]));
        VrWatchFaceStatus = GEVR_WATCH_FACE_ONLY;
        VrWatchGesturePause = 0;
        vrSettingsLoad();
        assert(VrWatchFaceStatus == GEVR_WATCH_FACE_ONLY && !VrWatchGesturePause);
        return 0;
    }
    if (argc == 1) {
        assert(VrFastReinforcements == 0);
        assert(VrCoopFastReinforcements == 0);
        assert(VrRefreshRate == initial); // No DisplayHz, including the obsolete RefreshRate key.
        assert(VrWatchFaceStatus == GEVR_WATCH_FACE_ON && VrWatchGesturePause == 1);
        assert(VrScreenPassthrough == 0);
    } else if (std::strcmp(argv[1], "write") == 0) {
        VrRefreshRate = std::atoi(argv[2]);
        vrSettingsSave();
    } else if (std::strcmp(argv[1], "read") == 0) {
        assert(VrRefreshRate == std::atoi(argv[2]));
        FILE *file = std::fopen(VR_INI_PATH, "w");
        assert(file); std::fputs("DisplayHz=120\n", file); std::fclose(file);
        vrSettingsLoad(); // Repeated frame initialization cannot reload over the current preference.
        assert(VrRefreshRate == std::atoi(argv[2]));
    }
    std::puts("PASS: production display setting load/save and platform default");
}
