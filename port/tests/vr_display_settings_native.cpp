#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "vr_settings.h"
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
void vrSettingsLoad();
void vrSettingsSave();
}
char g_ActiveExtTexPack[256] = {};
unsigned int arc4random_uniform(unsigned int) { return 1; }

int main(int argc, char **argv) {
    const int initial = VrRefreshRate;
#ifdef ANDROID
    assert(initial == 0);
#else
    assert(initial == 90);
#endif
    vrSettingsLoad();
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
