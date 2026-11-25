#include "gevr_gexweapon.h"
#include "gevr_romlayout.h"
#include "gevr_icloud.h"
#include "gevr_sync.h"

extern "C" int VrICloudSync, VrICloudPacks;   // gevr_settings.c
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <cmath>
#include <condition_variable>
#include <cstdarg>
#include <mutex>
#include <string>
#include <thread>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <io.h>
#else
#include <sys/stat.h>
#include <unistd.h>
#endif

#include "vr_log.h"
#include "vr_settings.h"
#include "gevr_scope.h"
#include "vr_screen.h"
#include "vr_haptics.h"
#include "gevr_settings.h"
#include "../src/net/net_match.h"
#include "../src/net/net_rules.h" /* NET_FUN_MASK: the deathmatch fun rules */

/* A phone or tablet's touch controls (port/flat/window/flat_touch.cpp): a
 * buzz on an on-screen button, and turning the device to look and aim. */
int VrTouchHaptics = 1;
/* A pad's triggers (input.c inputTriggerShot): a shot buzzes the trigger that
 * fired it, an Xbox pad's trigger motors or a DualSense's adaptive triggers.
 * 0 to 100%, its own strength (#269); an ini's 1 is the old toggle's on. */
int VrTriggerFeedback = 100;
/* A DualSense's adaptive triggers (input.c inputTriggerTick): 1 = a feel for
 * what the hand holds (a gun's bump that fires past it, fists, a knife, a
 * throw, the gadgets) and the shot's buzz in the trigger; 0 = the triggers
 * left free, the pad's rumble only. */
int VrAdaptiveTriggers = 1;
/* The walk's head bob in a flat game (src/game/bondhead.c, #269); stereo has
 * none either way. */
int VrHeadBob = 1;
/* A flat game's frame rate in levels (flat_window.cpp, #269, #274):
 * 0 Auto (display refresh rate, stepping down if the device can't hold it),
 * -1 Uncapped, or an explicit FPS cap (>= 25). */
int VrFrameRate = 0;
/* Save folders already checked for the old accidental Reverse look, a bit
 * each (src/game/file2.c fileLoadSettingsForFolder). */
extern "C" { int VrLookUprightFolders = 0; }
/* A pad's look (input.c inputPadLookSet, #269), written to every pad when the
 * ini has them; until then the engine ini's own (1x, 12.5%, not inverted). */
static float s_padLookSpeed = -1.0f, s_padLookDeadzone = -1.0f;
static int s_padLookInvert = -1;
/* the ini was written by a build that knew Vibration's 30% default (#269) */
static int s_padDefaults;
/* 0: no on-screen controls at all, for a mouse and keyboard whose clicks
 * arrive as touches (the iOS app on a Mac through PlayCover, #254). Only in
 * the ini: a phone with it off would have no way back to the panel. */
int VrTouchControls = 1;
int VrGyroAim = 0;   // 0 off, 1 while aiming, 2 always
float VrGyroSpeed = 1.5f;   // view degrees a degree the device turns
/* The on-screen buttons moved or resized in the touch layout editor
 * (flat_touch.cpp): "NAME x y radius;..." in screen fractions and points,
 * empty for the built-in layout. */
char VrTouchLayout[1024] = "";

#if defined(GEVR_FLAT)
int VrScreenShape = 1;   // a phone or a PC window: 16:9
#else
int VrScreenShape = 0;   // the headset's virtual screen: 4:3 until its cost is measured
#endif

extern "C" float inputRumbleGetStrength(int playernum);
extern "C" void inputRumbleSetStrength(int playernum, float strength);   // input.c takes an f32
extern "C" void inputRumbleSetStrengthAll(float strength);
extern "C" float inputPadLookSpeed(void), inputPadLookDeadzone(void);
extern "C" int inputPadLookInverted(void);
extern "C" void inputPadLookSet(float speed, float deadzone, int inverted);

#define FS_MAXPATH 256

// goldeneye-vr.ini's names for VrScopeFit's guns, in its order (gevr_scope.h)
static const char *const kScopeFitNames[GEVR_SCOPE_FITS] = { "Sniper", "Laser", "KF7", "AR33" };
/* Gun fit's keys for each look (gevr_scope.h): GoldenEye's, GoldenEye X's, Bean's */
static const char *const kFitLookPrefix[3] = { "", "Gex", "Bean" };
// BodySlot<name>= lines, in gevr_bodyslot.h's order
static const char *const kBodySlotKeys[GEVR_BODY_SLOTS] = { "HipGun", "HipOff", "BackGun", "BackOff", "Chest", "Belt" };

struct MuzzleWeaponEntry {
    int item;
    const char *name;
};
static const MuzzleWeaponEntry kMuzzleWeapons[] = {
    { 1, "Fist" },   /* the unarmed hand: Gun fit moves it as a gun (user, 2026-11-22: its fit was not saved) */
    { 2, "Knife" },
    { 3, "ThrowKnife" },
    { 4, "PP7" },
    { 5, "PP7Sil" },
    { 6, "DD44" },
    { 7, "Klobb" },
    { 8, "KF7" },
    { 9, "ZMG" },
    { 10, "D5K" },
    { 11, "D5KSil" },
    { 12, "Phantom" },
    { 13, "AR33" },
    { 14, "RCP90" },
    { 15, "Shotgun" },
    { 16, "AutoShot" },
    { 17, "Sniper" },
    { 18, "Cougar" },
    { 19, "GoldenGun" },
    { 20, "SilverPP7" },
    { 21, "GoldPP7" },
    { 22, "Laser" },
    { 23, "WatchLaser" },
    { 24, "GrenadeLaunch" },
    { 25, "RocketLaunch" },
    { 26, "Grenade" },
    { 27, "TimedMine" },
    { 28, "ProxMine" },
    { 29, "RemoteMine" },
    { 31, "Taser" },
    { 32, "TankShells" },
};
extern char g_ActiveExtTexPack[FS_MAXPATH];
extern "C" void extTexSetPack(const char *newPackName);
extern "C" void videoSetExternalTextures(bool enable);
extern "C" void set_mTrack2Vol(unsigned short);
extern "C" void musicTrack1ApplySeqpVol(unsigned short);
extern "C" void musicTrack3ApplySeqpVol(unsigned short);
extern "C" void gevrSndApplySfxVolume(unsigned short);

// Set once vrSettingsLoad has run (the first VR frame, gevr_engine_shim.c).
// A save before it writes every default over the player's file: the launcher's
// first name did, at launcher start, and reset everything it held.
static bool s_settingsLoaded = false;

#ifdef GEVR_FLAT
// The ini's PlayMode, kept for PC VR: the flat game shares goldeneye-vr.ini
// with GoldenEyeVR-VR.exe and plays on the screen whatever it says, so it
// writes back what it read (stereo when the ini has none), never its own 0.
// Saving 0 left PC VR on the big screen after a flat run (rivas_10, Quest 3
// over Virtual Desktop, 2026-11-12).
static int s_iniPlayMode = VR_PLAYMODE_STEREO;
#endif
static int gevrIniPlayMode(int ival)
{
    const int mode = ival != 0 ? VR_PLAYMODE_STEREO : VR_PLAYMODE_SCREEN;
#ifdef GEVR_FLAT
    s_iniPlayMode = mode;
#endif
    return mode;
}

static void gevrEnsureWritable(const char *path)
{
#ifdef _WIN32
    DWORD attrs = GetFileAttributesA(path);
    if (attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_READONLY)) {
        SetFileAttributesA(path, attrs & ~FILE_ATTRIBUTE_READONLY);
    }
#else
    chmod(path, 0666);
#endif
}

/*
 * The ini's text is made here, on the game's thread, and written to the file
 * by a thread of its own once the game turns that on (vrSettingsAsyncSaves).
 * The menus save on every change, and on the Apple TV HD writing the file took
 * 120-140 ms of a frame: longer than the ~100 ms of sound queued, so folding
 * the Friends panel or changing a setting broke the sound up (#269 #7).
 * vrSettingsFlush waits for the write: before the file is read again, when
 * the app leaves the foreground, and at exit.
 */
#if defined(__GNUC__) || defined(__clang__)
__attribute__((format(printf, 2, 3)))
#endif
static void iniPrintf(std::string *out, const char *format, ...)
{
    char line[1024];
    va_list args;
    va_start(args, format);
    const int n = vsnprintf(line, sizeof(line), format, args);
    va_end(args);
    if (n < 0) return;
    if ((size_t)n < sizeof(line)) {
        out->append(line, (size_t)n);
        return;
    }
    std::string big((size_t)n + 1, '\0');
    va_start(args, format);
    vsnprintf(&big[0], big.size(), format, args);
    va_end(args);
    out->append(big.data(), (size_t)n);
}

extern "C" void vrSettingsSave(void);

static void vrSettingsWrite(const std::string &text)
{
    const char *tmpPath = "goldeneye-vr.ini.tmp";
    gevrEnsureWritable(tmpPath);
    gevrEnsureWritable(VR_INI_PATH);
    remove(tmpPath);

    bool useTmp = true;
    FILE *f = fopen(tmpPath, "w");
    if (!f) {
        useTmp = false;
        f = fopen(VR_INI_PATH, "w");
        if (!f) {
            remove(VR_INI_PATH);
            f = fopen(VR_INI_PATH, "w");
        }
    }
    if (!f) {
        vr_log("vrSettingsSave: failed to open %s for writing: %s", VR_INI_PATH, strerror(errno));
        return;
    }

    fwrite(text.data(), 1, text.size(), f);
    fflush(f);
    fclose(f);

    if (useTmp) {
#ifdef _WIN32
        gevrEnsureWritable(VR_INI_PATH);
        remove(VR_INI_PATH);
        if (rename(tmpPath, VR_INI_PATH) != 0) {
            vr_log("vrSettingsSave: rename failed: %s", strerror(errno));
        }
#else
        gevrEnsureWritable(tmpPath);
        if (rename(tmpPath, VR_INI_PATH) != 0) {
            gevrEnsureWritable(VR_INI_PATH);
            remove(VR_INI_PATH);
            if (rename(tmpPath, VR_INI_PATH) != 0) {
                vr_log("vrSettingsSave: rename failed: %s", strerror(errno));
            }
        }
#endif
    }
    gevrCloudFileSaved(VR_INI_PATH);   // iCloud on the Apple builds (port/ios/ios_icloud.mm)
    gevrSyncFileSaved(VR_INI_PATH);    // the cross-platform sync (port/pc/gevr_sync.cpp)
}

// A multiplayer name for a player who hasn't chosen one. The Meta account name
// isn't open to a sideloaded app (the Platform SDK needs a store app and an
// entitled user), so a number keeps the default apart from everyone else's.
extern "C" void vrEnsurePlayerName(void)
{
    if (VrPlayerName[0] != '\0') return;
    snprintf(VrPlayerName, sizeof(VrPlayerName), "Agent %u", 1000u + arc4random_uniform(9000u));
    vrSettingsSave();
}

namespace {
struct GevrSaveQueue {
    std::mutex lock;
    std::condition_variable changed;
    std::string pending;
    bool waiting = false, writing = false, started = false;
};
// never destroyed: the writer thread may still wait on it while the process exits
GevrSaveQueue &gevrSaveQueue() { static GevrSaveQueue *q = new GevrSaveQueue; return *q; }
bool s_asyncSaves = false;

void gevrSaveWriter()
{
    GevrSaveQueue &q = gevrSaveQueue();
    std::unique_lock<std::mutex> hold(q.lock);
    for (;;) {
        q.changed.wait(hold, [&q] { return q.waiting; });
        std::string text;
        text.swap(q.pending);   // the newest save only: older ones it replaced are skipped
        q.waiting = false;
        q.writing = true;
        hold.unlock();
        vrSettingsWrite(text);
        hold.lock();
        q.writing = false;
        q.changed.notify_all();
    }
}
}

extern "C" void vrSettingsFlush(void)
{
    GevrSaveQueue &q = gevrSaveQueue();
    std::unique_lock<std::mutex> hold(q.lock);
    if (!q.started) return;
    q.changed.wait(hold, [&q] { return !q.waiting && !q.writing; });
}

/* The game's builds (main.c): saves go to the writer thread. Tests and the
 * dedicated server keep writing at once. */
extern "C" void vrSettingsAsyncSaves(int on)
{
    GevrSaveQueue &q = gevrSaveQueue();
    {
        std::lock_guard<std::mutex> hold(q.lock);
        if (on && !q.started) {
            q.started = true;
            std::thread(gevrSaveWriter).detach();
            atexit(vrSettingsFlush);
        }
    }
    if (!on) vrSettingsFlush();
    s_asyncSaves = on != 0;
}

extern "C" void vrSettingsSave(void)
{
    if (!s_settingsLoaded) return;

    std::string text;
    text.reserve(32768);
    std::string *f = &text;
    iniPrintf(f, "[VR]\n");
    iniPrintf(f, "ManualReloading=%d\n", VrManualReloading ? 1 : 0);
    iniPrintf(f, "; Hand reload was switched on once for everyone in v0.5.26, its default since.\n");
    iniPrintf(f, "HandReloadDefaulted=1\n");
    iniPrintf(f, "LaserDotForAll=%d\n", VrlaserDotForALL ? 1 : 0);
    iniPrintf(f, "SeatedMode=%d\n", VrSeatedMode ? 1 : 0);
    iniPrintf(f, "MotionThrowing=%d\n", VrMotionThrowing ? 1 : 0);
    iniPrintf(f, "MotionThrowPitch=%.1f\n", VrMotionThrowPitch);
    iniPrintf(f, "MotionThrowGazeAssist=%.2f\n", VrMotionThrowGazeAssist);
    iniPrintf(f, "MotionThrowStrength=%.2f\n", VrMotionThrowStrength);
    iniPrintf(f, "Vibration=%.4f\n", inputRumbleGetStrength(g_ExtMenuPlayer));
    iniPrintf(f, "PadDefaults=2\n");   // #269: Vibration's default is 30%; an older ini's 0.5 was the old default
    iniPrintf(f, "PadLookSpeed=%.2f\nPadLookDeadzone=%.4f\nPadLookInvert=%d\n", inputPadLookSpeed(),
            inputPadLookDeadzone(), inputPadLookInverted());
    iniPrintf(f, "HeadBob=%d\n", VrHeadBob);
    iniPrintf(f, "; Movement in the headset: 0 = the move stick walks, 1 = teleport: push it forward to\n");
    iniPrintf(f, "; aim an arc, let go to jump there; flick it back or sideways for a short hop. A jump\n");
    iniPrintf(f, "; goes only where you could walk in a straight line, and is paid for from a stamina\n");
    iniPrintf(f, "; that fills a little slower than running.\n");
    iniPrintf(f, "Teleport=%d\n", VrTeleport ? 1 : 0);
    iniPrintf(f, "; A flat game's levels: 0 = Auto (half the screen's rate while it can't hold the full one), 1 = Full, 2 = Half.\n");
    iniPrintf(f, "FrameRate=%d\n", VrFrameRate);
    iniPrintf(f, "LookUprightFolders=%d\n", VrLookUprightFolders);
    iniPrintf(f, "StereoCrosshair=%.4f\n", VrStereoCrosshair);
    iniPrintf(f, "HudDistance=%.4f\n", VrHudDistance);
    iniPrintf(f, "WeaponRecoil=%d\n", VrWeaponRecoil ? 1 : 0);
    iniPrintf(f, "WorldScale=%.4f\n", VrSetWorldScale);
    iniPrintf(f, "PauseHub=%d\n", VrPauseHub ? 1 : 0);
    iniPrintf(f, "StickClickToCrouch=%d\n", VrStickClickToCrouch ? 1 : 0);
    iniPrintf(f, "; 1 = holding the aim trigger no longer leans or ducks: the move stick keeps moving (issue #81).\n");
    iniPrintf(f, "AimNoLean=%d\n", VrAimNoLean ? 1 : 0);
    iniPrintf(f, "; 0 = no crosshair (nor scope sight) while holding a grip to aim, in stereo.\n");
    iniPrintf(f, "AimSight=%d\n", VrAimSight ? 1 : 0);
    iniPrintf(f, "SnapTurn=%.1f\n", VrUseSnapTurn);
    iniPrintf(f, "; Smooth turning speed in degrees per second, %d..%d.\n", SMOOTHTURN_MIN, SMOOTHTURN_MAX);
    iniPrintf(f, "SmoothTurnSpeed=%d\n", VrSmoothTurnSpeed);
    iniPrintf(f, "MovementSpeed=%d\nMpMovementSpeed=%d\n",
            netMovementSpeedPercent(VrMovementSpeed), netMovementSpeedPercent(VrMpMovementSpeed));
    iniPrintf(f, "; Grip gestures (stereo), 1 = on: use a door or switch at the hand, take back your own mine.\n");
    iniPrintf(f, "GestureGripUse=%d\nGestureMineGrab=%d\n",
            VrGestureGripUse, VrGestureMineGrab);
    iniPrintf(f, "; Force pull (stereo), 1 = on: point an empty hand at something out of reach, squeeze to lock, flick to pull, grip to catch.\n");
    iniPrintf(f, "; It takes a guard's gun too, and shows the target's halo and the pull line.\n");
    // ForcePull, not test.11-16's GestureForcePull: every ini those builds wrote
    // holds their default on, so the new default (off) reaches them once
    iniPrintf(f, "ForcePull=%d\n", VrForcePull);
    iniPrintf(f, "; Body slots (stereo), 1 = on: pistols at the hips, long guns over the shoulders,\n");
    iniPrintf(f, "; thrown on the chest, gadgets at the belt. Show: 1 = draw what they hold.\n");
    iniPrintf(f, "; Size: zones 0 small, 1 normal, 2 large.\n");
    iniPrintf(f, "BodySlots=%d\nBodySlotShow=%d\nBodySlotSize=%d\n", VrBodySlots, VrBodySlotShow, VrBodySlotSize);
    iniPrintf(f, "; Each slot's centre, set with Gun fit's Slots mode: cm below the eye, out to its\n");
    iniPrintf(f, "; side and ahead, in the torso's frame. 0 0 0 = the default for your height.\n");
    for (int s = 0; s < GEVR_BODY_SLOTS; s++) {
        iniPrintf(f, "BodySlot%s=%.2f %.2f %.2f\n", kBodySlotKeys[s],
                VrBodySlotFit[s][0], VrBodySlotFit[s][1], VrBodySlotFit[s][2]);
    }
    iniPrintf(f, "; 1 = each gun kicks with its own recoil (Perfect Dark VR's table), 0 = one generic kick.\n");
    iniPrintf(f, "PerWeaponRecoil=%d\n", VrPerWeaponRecoil);
    iniPrintf(f, "; Game rules (single player). 1 = thrown mines stick to guards.\n");
    iniPrintf(f, "MinesStickToGuards=%d\n", VrMinesStickToGuards);
    iniPrintf(f, "; Bodies kept on the floor: 0 = they fade as in the original game, or 12, 24.\n");
    iniPrintf(f, "BodiesStay=%d\n", VrBodiesStay);
    iniPrintf(f, "; 1 = alerted guards can call multiple reinforcements (solo, default off; any body count).\n");
    iniPrintf(f, "; GoldenEye solo/co-op/Horde AI ally; host chooses, next mission or retry.\n");
    iniPrintf(f, "Ally=%d\nAllyWeapon=%d\nAllyAggression=%d\nAllyHealth=%d\nAllyDamage=%d\nAllyInvincible=%d\n",
            VrAlly != 0, VrAllyWeapon, VrAllyAggression, VrAllyHealth, VrAllyDamage, VrAllyInvincible != 0);
    iniPrintf(f, "FastReinforcements=%d\n", VrFastReinforcements ? 1 : 0);
    iniPrintf(f, "; Host's co-op Fast reinforcements preference, independent of solo; default off.\n");
    iniPrintf(f, "CoopFastReinforcements=%d\n", VrCoopFastReinforcements ? 1 : 0);
    iniPrintf(f, "TwoHandedAiming=%d\n", VrTwoHandAim ? 1 : 0);
    iniPrintf(f, "LeftHandedMode=%d\n", VrLeftHandedMode ? 1 : 0);
    iniPrintf(f, "; Watch face: 0 Off, 1 On (also standard HUD), 2 Only (gameplay).\n");
    iniPrintf(f, "WatchFaceStatus=%d\n", gevrWatchStatusChoice(VrWatchFaceStatus));
    iniPrintf(f, "WatchGesturePause=%d\n", VrWatchGesturePause ? 1 : 0);
    iniPrintf(f, "SwapJoysticks=%d\n", VrSwapJoysticks ? 1 : 0);
    iniPrintf(f, "AimSteadying=%d\n", VrAimSteady);
    iniPrintf(f, "; The watch's Controls page: 0 = this headset's controllers, 1 = Quest 1, 2 = Quest 2,\n");
    iniPrintf(f, "; 3 = Touch Plus (Quest 3 and 3S), 4 = Touch Pro.\n");
    iniPrintf(f, "TouchControllers=%d\n", VrTouchModel);
    iniPrintf(f, "ShowStats=%d\n", VrShowStats ? 1 : 0);
    iniPrintf(f, "; Apple builds: iCloud sync of settings, saves and ROMs; and the texture packs\n");
    iniPrintf(f, "ICloudSync=%d\nICloudPacks=%d\n", VrICloudSync ? 1 : 0, VrICloudPacks ? 1 : 0);
    iniPrintf(f, "; 1 = the mission time in the lower left, to the hundredth (as the debrief counts it).\n");
    iniPrintf(f, "SpeedrunTimer=%d\n", VrSpeedrunTimer ? 1 : 0);
    iniPrintf(f, "; 1 = race your best run of each mission as a see-through Bond (files/ghosts).\n");
    iniPrintf(f, "GhostRace=%d\n", VrGhostRace ? 1 : 0);
    iniPrintf(f, "; 1 = the mission clock to LiveSplit's TCP server (LiveSplit: Control > Start TCP Server),\n");
    iniPrintf(f, "; at LiveSplitHost (empty: this computer; on a headset, the PC's address).\n");
    iniPrintf(f, "LiveSplit=%d\n", VrLiveSplit ? 1 : 0);
    iniPrintf(f, "LiveSplitHost=%s\n", VrLiveSplitHost);
    iniPrintf(f, "; 1 = Application SpaceWarp: the game renders at half the display rate and the\n");
    iniPrintf(f, "; headset makes the frames between from motion vectors. 0 = off, -1 = on with Bean.\n");
    iniPrintf(f, "SpaceWarp=%d\n", VrSpaceWarp < 0 ? -1 : VrSpaceWarp ? 1 : 0);
    iniPrintf(f, "; Anti-aliasing of the eye images: 1 = off, 2, 4 (8 on PC VR) samples per pixel.\n");
    iniPrintf(f, "EyeMsaa=%d\n", VrEyeMsaa);
    iniPrintf(f, "; Resolution: the eye images' size over the headset's recommendation, from the next\n");
    iniPrintf(f, "; start (above 1 is supersampling). 0 = Auto: this headset's (Quest 3: 1.25 at 90 Hz or less).\n");
    iniPrintf(f, "EyeResolution=%.2f\n", VrRenderScale);
    iniPrintf(f, "; Anisotropic filtering of HD pack and Bean textures: 0 = Auto (this headset's), 1 = off, 2, 4, 8 or 16.\n");
    iniPrintf(f, "TextureAniso=%d\n", VrTextureAniso);
    iniPrintf(f, "; 1 = 3D volumetric smoke and explosions (all modes), 0 = classic flat sprites.\n");
    iniPrintf(f, "VolumetricExplosions=%d\n", VrVolumetricExplosions ? 1 : 0);
    iniPrintf(f, "; CPU performance level asked of the headset at start: -1 = none, 0 = power savings,\n");
    iniPrintf(f, "; 1 = sustained low, 2 = sustained high (lifts the CPU cap on battery).\n");
    iniPrintf(f, "CpuLevel=%d\n", VrCpuLevel);
    iniPrintf(f, "; The rifle bat with both hands (Bean's, GoldenEye's own): 0 = the gun wrist swings it with\n");
    iniPrintf(f, "; the hands together and the hands' line apart, 1 = the hands' line always.\n");
    iniPrintf(f, "BatTwoHand=%d\n", VrBatTwoHand);
    iniPrintf(f, "Cheats=%llx\n", (unsigned long long)VrCheatMask);
    iniPrintf(f, "GunSizeCheat=%d\n", VrGunSizeCheat);
    iniPrintf(f, "; 1 = every mission at every difficulty, 007 mode and every cheat unlocked.\n");
    iniPrintf(f, "UnlockAll=%d\n", VrUnlockAll ? 1 : 0);
    iniPrintf(f, "HideArms=%d\n", VrHideArms ? 1 : 0);
    iniPrintf(f, "ActiveTexturePack=%s\n", g_ActiveExtTexPack);
    iniPrintf(f, "; Your standing EYE height in cm -- where your eyes are off the floor, which is\n");
    iniPrintf(f, "; what the headset reports, roughly 13 cm below the top of your head. Set it from\n");
    iniPrintf(f, "; the live reading beside the menu slider rather than from your stature.\n");
    iniPrintf(f, "PlayerHeight=%.1f\n", VrPlayerHeight);
    iniPrintf(f, "; 1 = stand at the height of the character you are playing instead of your own,\n");
    iniPrintf(f, "; so Elvis is short and Mr Blonde towers. 0 = you are your own height throughout.\n");
    iniPrintf(f, "MatchCharacterHeight=%d\n", VrMatchCharacterHeight ? 1 : 0);

    // --- GoldenEye presentation -------------------------------------------------------------------
    iniPrintf(f, "\n");
    iniPrintf(f, "; 1 = true stereo in first-person play (menus, cutscenes and the watch stay on the\n");
    iniPrintf(f, "; virtual screen), 0 = everything on the virtual screen. Hold the right stick click\n");
    iniPrintf(f, "; in game to switch.\n");
#ifdef GEVR_FLAT
    iniPrintf(f, "PlayMode=%d\n", s_iniPlayMode);   // PC VR's: the flat game plays on the screen
#else
    iniPrintf(f, "PlayMode=%d\n", VrPlayMode);
#endif
    iniPrintf(f, "; 1 = Project Bean characters, guns, rooms and skies, when the files are in\n");
    iniPrintf(f, "; added-content/xbla. Hold one grip and the right stick click to switch.\n");
    iniPrintf(f, "XblaVisualsMode=%d\n", VrBeanModels);
    iniPrintf(f, "; 1 = with Project Bean on, the pictures it leaves as the cartridge's (the stage\n");
    iniPrintf(f, "; pictures and the like) come from the HD + AI or HD texture pack, if one is installed.\n");
    iniPrintf(f, "BeanFillTextures=%d\n", VrBeanFillTextures ? 1 : 0);
    iniPrintf(f, "; 1 = with Project Bean on, the Community Edition's files, once its updater is applied.\n");
    iniPrintf(f, "BeanCommunityEdition=%d\n", VrBeanCommunityEdition ? 1 : 0);
    iniPrintf(f, "; 1 = Wreck's blood patch: shot guards spray the red sparks Rare left in the game.\n");
    iniPrintf(f, "BloodPatch=%d\n", VrBloodPatch ? 1 : 0);
    iniPrintf(f, "; 1 = Oddjob's hat in place of the throwing knife, from files/mods/oddjob_hat.bin.\n");
    iniPrintf(f, "OddjobHat=%d\n", VrOddjobHat ? 1 : 0);
    iniPrintf(f, "Ragdolls=%d\n", VrRagdolls ? 1 : 0);
    iniPrintf(f, "Dismemberment=%d\n", VrDismemberment ? 1 : 0);
    iniPrintf(f, "DismemberDamage=%.1f\n", VrDismemberDamage);
    iniPrintf(f, "DismemberChance=%d\n", VrDismemberChance);
    iniPrintf(f, "MicMuted=%d\n", VrMicMuted ? 1 : 0);
    iniPrintf(f, "MasterVolume=%.2f\n", VrMasterVolume);
    iniPrintf(f, "MusicVolume=%.2f\n", VrMusicVolume);
    iniPrintf(f, "VoiceVolume=%.2f\n", VrVoiceVolume);
    iniPrintf(f, "SfxVolume=%.2f\n", VrSfxVolume);
    iniPrintf(f, "; Your name in multiplayer, up to 15 characters.\n");
    iniPrintf(f, "PlayerName=%s\n", VrPlayerName);
    for (int g = 0; g < 3; g++) {
        iniPrintf(f, "Guest%dName=%s\n", g + 2, VrGuestName[g]);
        iniPrintf(f, "Guest%dChr=%d\n", g + 2, VrGuestChr[g]);
        for (int k = 0; k < 4; k++) iniPrintf(f, "Guest%dLoadout%d=%d\n", g + 2, k + 1, VrGuestLoadout[g][k]);
    }
    iniPrintf(f, "; The multiplayer page's last choices. MpStage is the level id; the sets,\n");
    iniPrintf(f, "; scenario, length and health are the launcher's list positions; the guns are\n");
    iniPrintf(f, "; item ids; the favorites are bitmasks over the stage and weapon-set lists.\n");
    iniPrintf(f, "MpStage=%d\nMpWeaponSet=%d\nMpChr=%d\nMpVisibility=%d\nMpVrOnly=%d\nMpHideFlat=%d\n", VrMpStage, VrMpWeaponSet, VrMpChr, VrMpVisibility, VrMpVrOnly, VrMpHideFlat);
    iniPrintf(f, "; The host's \"Hand reload only\", and the Join list's \"Hide games with button reload\".\n");
    iniPrintf(f, "MpHandReloadOnly=%d\nMpHideButton=%d\n", VrMpHandReloadOnly ? 1 : 0, VrMpHideButton ? 1 : 0);
    iniPrintf(f, "MpServerFilter=%d\nMpCustomServers=%s\n", VrMpServerFilter, VrMpCustomServers);
    iniPrintf(f, "MpScenario=%d\nMpLength=%d\nMpHealth=%d\nMpDual=%d\nMpLoadouts=%d\nMpNextRound=%d\n",
            VrMpScenario, VrMpLength, VrMpHealth, VrMpDual, VrMpLoadouts, VrMpNextRound);
    iniPrintf(f, "MpVoiceMode=%d\n", VrMpVoiceMode);
    iniPrintf(f, "; 1 = the host's Handicaps rule: each player picks their own health.\nMpHandicaps=%d\n", VrMpHandicaps ? 1 : 0);
    iniPrintf(f, "; The host's Lobby rule: 1 gathers everyone in the pre-match lobby before each match, 0 drops them into the warmup.\n"
            "; MpWarmup: the practice after the lobby's START, 0 off, 1 30 s, 2 60 s, 3 2 min.\nMpLobby=%d\nMpWarmup=%d\n",
            VrMpLobby ? 1 : 0, VrMpWarmup);
    iniPrintf(f, "FriendsShowDetail=%d\nFriendsAppearOffline=%d\nFriendsRestoreNoted=%d\n",
            VrFriendsShowDetail, VrFriendsAppearOffline, VrFriendsRestoreNoted);
    iniPrintf(f, "FriendsSidebar=%d\nFriendsSearchable=%d\n", VrFriendsSidebar, VrFriendsSearchable);
    iniPrintf(f, "MpFriendlyFire=%d\n", VrMpFriendlyFire);
    iniPrintf(f, "HordeFriendlyFire=%d\n", VrHordeFriendlyFire);
    iniPrintf(f, "HordeStage=%d\n", VrHordeStage);
    iniPrintf(f, "HordeStartWave=%d\n", VrHordeStartWave);
    iniPrintf(f, "HostEqualization=%d\nHostLatencyCapMs=%d\n", VrHostEqualization, VrHostLatencyCapMs);
    iniPrintf(f, "MpFunFlags=%d\nMpGunSize=%d\nMpMaxPlayers=%d\n", VrMpFunFlags, VrMpGunSize, VrMpMaxPlayers);
    iniPrintf(f, "MpBotMode=%d\nMpBotCount=%d\nMpBotDifficulty=%d\n", VrMpBotMode, VrMpBotCount, VrMpBotDifficulty);
    iniPrintf(f, "; 1 = guards and other players hold the detailed first-person gun models, 0 = the game's own.\n");
    iniPrintf(f, "DetailedGuns=%d\n", VrDetailedGuns ? 1 : 0);
    iniPrintf(f, "; 1 = GoldenEye X's first-person guns from data/gex.z64 (experimental, docs/gex-weapons.md).\n");
    /* a hack campaign turns the models off for its run only (gevr_romload.c) */
    iniPrintf(f, "GexGuns=%d\n", (VrGexGuns || gevrGexCampaignOff) ? 1 : 0);
    iniPrintf(f, "; Campaign at Start: 0 GoldenEye, 1 Goldfinger 64, 2 Tomorrow Never Dies, 3 Mirror Mode.\n");
    iniPrintf(f, "; The four ROMs stay installed together (docs/rom-hacks.md).\n");
    iniPrintf(f, "Campaign=%d\n", VrCampaign);
    iniPrintf(f, "; 1 = in the headset GoldenEye X's arms are every arm, the left wearing the watch; 0 = the watch arm.\n");
    for (int i = 0; i < 4; i++) iniPrintf(f, "MpCustom%d=%d\n", i + 1, VrMpCustom[i]);
    for (int i = 0; i < 4; i++) iniPrintf(f, "MpLoadout%d=%d\n", i + 1, VrMpLoadout[i]);
    iniPrintf(f, "MpFavStages=%llu\nMpFavSets=%u\n", VrMpFavStages, VrMpFavSets);
    iniPrintf(f, "; MpMode 2 = Horde (Archives). Co-op: MpMode 1, the mission's level id and the difficulty (0 Agent .. 3 007).\n");
    iniPrintf(f, "MpMode=%d\nMpMission=%d\nMpDifficulty=%d\n", VrMpMode, VrMpMission, VrMpDifficulty);
    iniPrintf(f, "; The virtual screen: metres in front of you, and the degrees of view it spans.\n");
    iniPrintf(f, "; Hold both grips and use the right stick while the screen is up to change them.\n");
    iniPrintf(f, "ScreenDistance=%.2f\n", VrScreenDistance);
    iniPrintf(f, "ScreenFov=%.1f\n", VrScreenFov);
    iniPrintf(f, "; 1 = a curved screen (a section of a cylinder around you), 0 = flat.\n");
    iniPrintf(f, "ScreenCurved=%d\n", VrScreenCurved);
    iniPrintf(f, "; 1 = passthrough background behind the 2D screen, 0 = black void.\n");
    iniPrintf(f, "ScreenPassthrough=%d\n", VrScreenPassthrough);
    iniPrintf(f, "; The 2D screen's shape (a phone, a PC window, the virtual screen): 0 = 4:3, 1 = 16:9.\n");
    iniPrintf(f, "ScreenShape=%d\n", VrScreenShape);
    iniPrintf(f, "; A phone or tablet: 1 = a buzz on an on-screen button; 1 = turn the device to look and aim.\n");
    iniPrintf(f, "TouchHaptics=%d\nGyroAim=%d\nGyroSpeed=%.2f\n", VrTouchHaptics, VrGyroAim, VrGyroSpeed);
    iniPrintf(f, "; 0 = no on-screen touch controls, for a mouse and keyboard whose clicks arrive\n");
    iniPrintf(f, "; as touches (the iOS app on a Mac through PlayCover). 1 = shown (the default).\n");
    iniPrintf(f, "TouchControls=%d\n", VrTouchControls);
    iniPrintf(f, "; A pad's triggers: a shot buzzes the trigger that fired it (an Xbox pad's\n");
    iniPrintf(f, "; trigger motors, a DualSense's adaptive triggers), 0 to 100 (%%), 0 = off.\n");
    iniPrintf(f, "TriggerFeedback=%d\n", VrTriggerFeedback);
    iniPrintf(f, "; A DualSense: 1 = adaptive triggers (a gun's bump to pull past, a feel for each weapon),\n");
    iniPrintf(f, "; 0 = the triggers left free and the pad's rumble only.\n");
    iniPrintf(f, "AdaptiveTriggers=%d\n", VrAdaptiveTriggers);
    iniPrintf(f, "; The touch layout editor's buttons (NAME x y radius;...), empty for the built-in layout.\n");
    iniPrintf(f, "TouchLayout=%s\n", VrTouchLayout);
    iniPrintf(f, "; Metres above (+) or below (-) eye level; set by grabbing the screen with both grips.\n");
    iniPrintf(f, "ScreenHeight=%.2f\n", VrScreenHeight);
    iniPrintf(f, "; Preferred display refresh rate in Hz. 0 = Auto (Quest 3: 90, Quest 2 and Pro: 72,\n");
    iniPrintf(f, "; others: no app preference), the Quest default. The launcher offers this headset's supported\n");
    iniPrintf(f, "; rates. Saved choices are retained; the runtime decides the actual rate.\n");
    iniPrintf(f, "DisplayHz=%d\n", VrRefreshRate);
    iniPrintf(f, "; Stereo: darken the edges of the view while moving or smooth-turning, to\n");
    iniPrintf(f, "; ease motion sickness. 0 = off, up to 1 = strongest.\n");
    iniPrintf(f, "ComfortVignette=%.2f\n", VrComfortVignette);
    iniPrintf(f, "; Stereo, when you're hit: 1 = the hit doesn't push you, 1 = the trigger still\n");
    iniPrintf(f, "; fires while the hit shows (no hitstun), 0 = no red flash.\n");
    iniPrintf(f, "NoKnockback=%d\nNoHitstun=%d\nDamageFlash=%d\n", VrNoKnockback ? 1 : 0, VrNoHitstun ? 1 : 0,
            VrDamageFlash ? 1 : 0);
    iniPrintf(f, "; Flat screen: 1 = third person (the camera behind Bond). Its offset from\n");
    iniPrintf(f, "; the eye in world units (about cm): back, up, right shoulder (- = left).\n");
    iniPrintf(f, "ThirdPerson=%d\nThirdPersonDistance=%d\nThirdPersonHeight=%d\nThirdPersonSideways=%d\n",
            VrThirdPerson ? 1 : 0, VrThirdPersonDist, VrThirdPersonHeight, VrThirdPersonSide);

    // --- VR hand placement (no menu UI; edit here) --------------------------------------------
    iniPrintf(f, "\n");
    iniPrintf(f, "; Where the gun sits in your hand, in the CONTROLLER's own frame (cm). Set them\n");
    iniPrintf(f, "; in game with the launcher's Gun fit..., or here. X = right, Y = up, Z = back\n");
    iniPrintf(f, "; toward you (negative is forward). All three 0 means the fitted defaults.\n");
    iniPrintf(f, "GunOffX=%.4f\n", VrGunOffX);
    iniPrintf(f, "GunOffY=%.4f\n", VrGunOffY);
    iniPrintf(f, "GunOffZ=%.4f\n", VrGunOffZ);
    iniPrintf(f, "; The holding hand of a two-handed hold (issue #35), set with Gun fit while\n");
    iniPrintf(f, "; holding a gun with both hands: cm outward, up, forward, then degrees of turn\n");
    iniPrintf(f, "; about the hand's X, Y, Z. GripPistol for handguns, GripRifle for long guns.\n");
    iniPrintf(f, "GripPistol=%.2f %.2f %.2f %.1f %.1f %.1f\n", VrGripTrim[0][0], VrGripTrim[0][1],
            VrGripTrim[0][2], VrGripTrim[0][3], VrGripTrim[0][4], VrGripTrim[0][5]);
    iniPrintf(f, "GripRifle=%.2f %.2f %.2f %.1f %.1f %.1f\n", VrGripTrim[1][0], VrGripTrim[1][1],
            VrGripTrim[1][2], VrGripTrim[1][3], VrGripTrim[1][4], VrGripTrim[1][5]);
    iniPrintf(f, "; GoldenEye X's models (MODS) sit differently in the hand, so Gun fit keeps\n");
    iniPrintf(f, "; theirs apart: GexGunOff is GunOffX, Y and Z; GexGrip* as above.\n");
    iniPrintf(f, "GexGunOff=%.4f %.4f %.4f\n", VrGexGunOff[0], VrGexGunOff[1], VrGexGunOff[2]);
    iniPrintf(f, "GexGripPistol=%.2f %.2f %.2f %.1f %.1f %.1f\n", VrGexGripTrim[0][0], VrGexGripTrim[0][1],
            VrGexGripTrim[0][2], VrGexGripTrim[0][3], VrGexGripTrim[0][4], VrGexGripTrim[0][5]);
    iniPrintf(f, "GexGripRifle=%.2f %.2f %.2f %.1f %.1f %.1f\n", VrGexGripTrim[1][0], VrGexGripTrim[1][1],
            VrGexGripTrim[1][2], VrGexGripTrim[1][3], VrGexGripTrim[1][4], VrGexGripTrim[1][5]);
    iniPrintf(f, "; Bean's models (MODS) keep theirs apart too: BeanGunOff, BeanGrip*, and BeanReloadGrab,\n");
    iniPrintf(f, "; BeanScope* and BeanMuzzle* below.\n");
    iniPrintf(f, "BeanGunOff=%.4f %.4f %.4f\n", VrBeanGunOff[0], VrBeanGunOff[1], VrBeanGunOff[2]);
    iniPrintf(f, "BeanGripPistol=%.2f %.2f %.2f %.1f %.1f %.1f\n", VrBeanGripTrim[0][0], VrBeanGripTrim[0][1],
            VrBeanGripTrim[0][2], VrBeanGripTrim[0][3], VrBeanGripTrim[0][4], VrBeanGripTrim[0][5]);
    iniPrintf(f, "BeanGripRifle=%.2f %.2f %.2f %.1f %.1f %.1f\n", VrBeanGripTrim[1][0], VrBeanGripTrim[1][1],
            VrBeanGripTrim[1][2], VrBeanGripTrim[1][3], VrBeanGripTrim[1][4], VrBeanGripTrim[1][5]);
    iniPrintf(f, "; Each gun's own, once Gun fit has moved it (the models hold the hand their own\n");
    iniPrintf(f, "; way): GunFit<gun> as GunOffX, Y and Z, GripFit<gun> as GripPistol; Bean* for\n");
    iniPrintf(f, "; Bean's models. A gun without its own takes the ones above.\n");
    for (int m = 0; m < 3; m++) {
        const int set = gevrPerGunSet(m);
        if (set < 0) continue;
        for (const auto &w : kMuzzleWeapons) {
            if (VrGunOffPerSet[set][w.item]) {
                const float *t = VrGunOffPer[set][w.item];
                iniPrintf(f, "%sGunFit%s=%.4f %.4f %.4f\n", kFitLookPrefix[m], w.name, t[0], t[1], t[2]);
            }
            if (VrGripTrimPerSet[set][w.item]) {
                const float *t = VrGripTrimPer[set][w.item];
                iniPrintf(f, "%sGripFit%s=%.2f %.2f %.2f %.1f %.1f %.1f\n", kFitLookPrefix[m], w.name,
                        t[0], t[1], t[2], t[3], t[4], t[5]);
            }
        }
    }
    iniPrintf(f, "; Hand reload, set with Gun fit's reload mode (X): where the off hand takes the\n");
    iniPrintf(f, "; magazine, cm right, up and back from the gun hand's grip (Gex* for GoldenEye X);\n");
    iniPrintf(f, "; the belt, cm below the eye, out to the side and ahead; and where a GoldenEye X\n");
    iniPrintf(f, "; magazine sits in the off hand, cm right, up and back from its grip.\n");
    iniPrintf(f, "ReloadGrab=%.2f %.2f %.2f\n", VrReloadGrab[0][0], VrReloadGrab[0][1], VrReloadGrab[0][2]);
    iniPrintf(f, "GexReloadGrab=%.2f %.2f %.2f\n", VrReloadGrab[1][0], VrReloadGrab[1][1], VrReloadGrab[1][2]);
    iniPrintf(f, "BeanReloadGrab=%.2f %.2f %.2f\n", VrBeanReloadGrab[0], VrBeanReloadGrab[1], VrBeanReloadGrab[2]);
    iniPrintf(f, "ReloadBelt=%.2f %.2f %.2f\n", VrReloadBelt[0], VrReloadBelt[1], VrReloadBelt[2]);
    iniPrintf(f, "GexHeldMag=%.2f %.2f %.2f\n", VrGexHeldMag[0], VrGexHeldMag[1], VrGexHeldMag[2]);
    iniPrintf(f, "; The watch on GoldenEye X's left wrist (Gun fit's off hand mode, right grip):\n");
    iniPrintf(f, "; cm ahead of the sleeve's end, up and out, and its size.\n");
    iniPrintf(f, "GexWatch=%.2f %.2f %.2f %.2f\n", VrGexWatch[0], VrGexWatch[1], VrGexWatch[2], VrGexWatch[3]);
    iniPrintf(f, "; GoldenEye X's off hand (and its watch arm) turned about its palm, degrees\n");
    iniPrintf(f, "; (Gun fit's off hand mode, left grip).\n");
    iniPrintf(f, "GexOffRot=%.1f %.1f %.1f\n", VrGexOffRot[0], VrGexOffRot[1], VrGexOffRot[2]);
    iniPrintf(f, "; Where GoldenEye X's left hand holds a gun with both hands (Gun fit, both hands):\n");
    iniPrintf(f, "; cm forward, up and out along the gun.\n");
    iniPrintf(f, "GexForeHold=%.2f %.2f %.2f\n", VrGexForeHold[0], VrGexForeHold[1], VrGexForeHold[2]);
    iniPrintf(f, "; The sniper rifle's butt held as a bat (Gun fit with it out): GexBat the rifle on\n");
    iniPrintf(f, "; the fist, cm right, up and back (down slides the fist toward the butt);\n");
    iniPrintf(f, "; GexBatHand and GexBatHandRot the gun hand on it (all 0: the rifle's own).\n");
    iniPrintf(f, "GexBat=%.4f %.4f %.4f\n", VrGexBatFit[0][0], VrGexBatFit[0][1], VrGexBatFit[0][2]);
    iniPrintf(f, "GexBatHand=%.4f %.4f %.4f\n", VrGexBatFit[1][0], VrGexBatFit[1][1], VrGexBatFit[1][2]);
    iniPrintf(f, "GexBatHandRot=%.4f %.4f %.4f\n", VrGexBatFit[2][0], VrGexBatFit[2][1], VrGexBatFit[2][2]);
    iniPrintf(f, "; GexBatOff and GexBatOffRot: the off hand where it holds the rifle (both hands).\n");
    iniPrintf(f, "GexBatOff=%.4f %.4f %.4f\n", VrGexBatFit[3][0], VrGexBatFit[3][1], VrGexBatFit[3][2]);
    iniPrintf(f, "GexBatOffRot=%.4f %.4f %.4f\n", VrGexBatFit[4][0], VrGexBatFit[4][1], VrGexBatFit[4][2]);
    iniPrintf(f, "; BeanBat: Bean's sniper rifle as the bat, the rifle on the fist as GexBat.\n");
    iniPrintf(f, "BeanBat=%.4f %.4f %.4f\n", VrGexBatFit[5][0], VrGexBatFit[5][1], VrGexBatFit[5][2]);
    iniPrintf(f, "; Bat: the same for GoldenEye's own sniper rifle.\n");
    iniPrintf(f, "Bat=%.4f %.4f %.4f\n", VrGexBatFit[6][0], VrGexBatFit[6][1], VrGexBatFit[6][2]);
    iniPrintf(f, "; BeanBatOff and BatOff: with both hands on, the rifle's end in the off hand, cm right,\n");
    iniPrintf(f, "; up and back of the hand's middle.\n");
    iniPrintf(f, "BeanBatOff=%.4f %.4f %.4f\n", VrGexBatFit[7][0], VrGexBatFit[7][1], VrGexBatFit[7][2]);
    iniPrintf(f, "BatOff=%.4f %.4f %.4f\n", VrGexBatFit[8][0], VrGexBatFit[8][1], VrGexBatFit[8][2]);
    iniPrintf(f, "; Bean guns' magazines: item number / vector (0 held magazine, 1 magazine well, 2 held\n");
    iniPrintf(f, "; magazine in the primary hand, reloading a gun in the other); fitted ones only.\n");
    for (int item=0; item<64; item++)
        for (int which=0; which<3; which++) {
            const float *v=VrBeanMagFits[item][which];
            /* the fitted ones: an unfitted gun takes a later build's default (owner,
             * 2026-11-22: the shipped place, written out, outlived a better default) */
            if (memcmp(v, VrBeanMagFitsDefault[item][which], sizeof(float) * 3) != 0)
                iniPrintf(f, "BeanMagFit%d_%d=%.4f %.4f %.4f\n", item, which, v[0], v[1], v[2]);
        }
    iniPrintf(f, "; Per-weapon fits: item number / vector (0 gun, 1 grab, 2 support, 3 rotation, 4 held mag, 5 well, 6 installed mesh).\n");
    for (int item=0; item<64; item++) {
        /* Do not write zero overrides for models implemented by future builds. */
        /* 5, 20 and 21 share the PP7's (item 4's) fits; 11 the D5K's (10); 3 the knife's (2) */
        if (!gevrGexWeaponGet(item) || item == 3 || item == 5 || item == 11 || item == 20 || item == 21) continue;
        /* item 4's 0..5 are the legacy GexPP7* keys; 7 and 8 the gun hand (or item), 9 item size */
        for (int component=(item == 4 ? 6 : 0); component<10; component++) {
            const float *v=VrGexWeaponFits[item][component];
            iniPrintf(f, "GexFit%d_%d=%.4f %.4f %.4f\n", item, component, v[0], v[1], v[2]);
        }
    }
    iniPrintf(f, "GexPP7Grab=%.2f %.2f %.2f\n", VrGexPp7Grab[0], VrGexPp7Grab[1], VrGexPp7Grab[2]);
    iniPrintf(f, "GexPP7Support=%.2f %.2f %.2f\n", VrGexPp7Support[0], VrGexPp7Support[1], VrGexPp7Support[2]);
    iniPrintf(f, "GexPP7SupportRot=%.1f %.1f %.1f\n", VrGexPp7SupportRot[0], VrGexPp7SupportRot[1], VrGexPp7SupportRot[2]);
    iniPrintf(f, "GexPP7GunOff=%.4f %.4f %.4f\n", VrGexPp7GunOff[0], VrGexPp7GunOff[1], VrGexPp7GunOff[2]);
    iniPrintf(f, "; Held magazine only (Gun fit, X): cm right, up and back on the off controller.\n");
    iniPrintf(f, "GexKF7MagOff=%.2f %.2f %.2f\n", VrGexKf7MagOff[0], VrGexKf7MagOff[1], VrGexKf7MagOff[2]);
    iniPrintf(f, "GexPP7MagOff=%.2f %.2f %.2f\n", VrGexPp7MagOff[0], VrGexPp7MagOff[1], VrGexPp7MagOff[2]);
    iniPrintf(f, "; Magazine-well entrance (Gun fit, X): cm right, up and back on the gun.\n");
    iniPrintf(f, "GexKF7WellOff=%.2f %.2f %.2f\n", VrGexKf7WellOff[0], VrGexKf7WellOff[1], VrGexKf7WellOff[2]);
    iniPrintf(f, "GexPP7WellOff=%.2f %.2f %.2f\n", VrGexPp7WellOff[0], VrGexPp7WellOff[1], VrGexPp7WellOff[2]);
    iniPrintf(f, "; A scope's lens, set with Gun fit holding the gun (X switches to its scope):\n");
    iniPrintf(f, "; cm right, up and back from the eyepiece, then cm wider. GexScope* for\n");
    iniPrintf(f, "; GoldenEye X's models.\n");
    for (int m = 0; m < 3; m++) {
        for (int s = 0; s < GEVR_SCOPE_FITS; s++) {
            const float *t = gevrScopeFitOf(m, s);
            iniPrintf(f, "%sScope%s=%.2f %.2f %.2f %.2f\n", kFitLookPrefix[m], kScopeFitNames[s], t[0], t[1], t[2], t[3]);
        }
    }
    iniPrintf(f, "\n");
    iniPrintf(f, "; Barrel tip / muzzle fit, set with Gun fit holding the gun (X switches to barrel tip):\n");
    iniPrintf(f, "; cm right (side), up and forward from the default barrel tip. GexMuzzle* for\n");
    iniPrintf(f, "; GoldenEye X's models.\n");
    for (int m = 0; m < 3; m++) {
        for (const auto &w : kMuzzleWeapons) {
            const float *t = gevrMuzzleTrimOf(m, w.item);
            if (t[0] != 0.0f || t[1] != 0.0f || t[2] != 0.0f) {
                iniPrintf(f, "%sMuzzle%s=%.2f %.2f %.2f\n", kFitLookPrefix[m], w.name, t[0], t[1], t[2]);
            }
        }
    }
    iniPrintf(f, "\n");
    iniPrintf(f, "; Hand reload's rack, set with Gun fit's reload mode (X) by the left grip: where the off\n");
    iniPrintf(f, "; hand takes the slide, the bolt handle or the pump, cm right, up and back of the gun\n");
    iniPrintf(f, "; hand's grip. A gun with no line uses a guess. GexRack* and BeanRack* for those models.\n");
    for (int m = 0; m < 3; m++) {
        for (const auto &w : kMuzzleWeapons) {
            const float *t = VrRackFit[m][w.item];
            if (t[0] != 0.0f || t[1] != 0.0f || t[2] != 0.0f) {
                iniPrintf(f, "%sRack%s=%.2f %.2f %.2f\n", kFitLookPrefix[m], w.name, t[0], t[1], t[2]);
            }
        }
    }



    iniPrintf(f, "\n");
    iniPrintf(f, "; 0..1. How tightly the elbows are pulled in toward your body. 0 leaves them at the\n");
    iniPrintf(f, "; animation's rest pose (they splay outward), 1 pins them hard against the torso.\n");
    iniPrintf(f, "ArmElbowTuck=%.4f\n", VrArmElbowTuck);
    iniPrintf(f, "\n");
    iniPrintf(f, "; How fast the virtual torso turns to follow your head, per tick. The elbow anchor\n");
    iniPrintf(f, "; is held steady relative to that torso, so this trades two things off: too LOW and\n");
    iniPrintf(f, "; the elbows lag behind when you physically turn your whole body; too HIGH and they\n");
    iniPrintf(f, "; drift when you merely glance around. ~0.02 suits most people. Body slots ride\n");
    iniPrintf(f, "; on the same torso.\n");
    iniPrintf(f, "ArmBodyFollow=%.4f\n", VrArmBodyFollow);
    iniPrintf(f, "\n");
    iniPrintf(f, "; 1 = with GE-X's arms, a free or unarmed hand closes into a fist while you\n");
    iniPrintf(f, "; squeeze its grip, 0 = it stays open. (FistClench, before it did this, saved 0.)\n");
    iniPrintf(f, "GexFistClench=%d\n", VrFistClench);
    iniPrintf(f, "\n");
    iniPrintf(f, "; Milliseconds after a gun runs dry and switches itself during which the first\n");
    iniPrintf(f, "; weapon-cycle tap keeps the gun it switched to instead of stepping past it.\n");
    iniPrintf(f, "; 0 = off (every tap cycles).\n");
    iniPrintf(f, "AutoSwitchKeepMs=%d\n", VrAutoSwitchKeepMs);
    vrHapticsSaveIni(f);
    // no vrHapticsDumpCTable here: ~60 log lines, a file open each, took
    // ~115 ms of every save on the Apple TV HD (#269 #7); a debug log's Send has it

    if (!s_asyncSaves) {
        vrSettingsWrite(text);
        return;
    }
    GevrSaveQueue &q = gevrSaveQueue();
    std::lock_guard<std::mutex> hold(q.lock);
    q.pending.swap(text);
    q.waiting = true;
    q.changed.notify_all();
}

extern "C" void vrSettingsLoad(void)
{
    if (s_settingsLoaded) return;
    vrSettingsFlush();   // a save still being written
    s_settingsLoaded = true;   // with no file yet, the defaults are the settings
    gevrSettingsReadBuiltIn();   // gevr_settings.c: the defaults Reset all settings returns to
    vrHapticsInit();
    FILE *f = fopen(VR_INI_PATH, "r");
    if (!f) return;

    char line[1100];   // TouchLayout's value is up to 1 KB
    char key[64];
    float fval;
    bool handReloadDefaulted = false;   // HandReloadDefaulted=1: v0.5.26 switched Hand reload on once
    int ival;
    char sval[256];

    // The gun's trim (GunOffX/Y/Z): kept aside, see below.
    float gunOff[3] = { 0.0f, 0.0f, 0.0f };
    bool gunOffRead = false;
    // Bean's own fits (Bean*): until one is saved they are the player's GoldenEye ones
    bool beanFitsRead = false;
    while (fgets(line, sizeof(line), f)) {
        if (line[0] == '[' || line[0] == '\n' || line[0] == ';' || line[0] == '#') continue;
        if (strncmp(line, "TouchLayout=", 12) == 0) {   // a string: before the number parsers
            snprintf(VrTouchLayout, sizeof(VrTouchLayout), "%s", line + 12);
            VrTouchLayout[strcspn(VrTouchLayout, "\r\n")] = 0;
            continue;
        }
        if (strncmp(line, "MpCustomServers=", 16) == 0) {   // a string too
            snprintf(VrMpCustomServers, sizeof(VrMpCustomServers), "%s", line + 16);
            VrMpCustomServers[strcspn(VrMpCustomServers, "\r\n")] = 0;
            continue;
        }
        if (strncmp(line, "WatchFaceStatus=", 16) == 0) {
            char *end;
            const char *value = line + 16;
            const long choice = strtol(value, &end, 10);
            const bool haveValue = end != value;
            while (*end == ' ' || *end == '\t' || *end == '\r' || *end == '\n') end++;
            VrWatchFaceStatus = haveValue && !*end && choice >= GEVR_WATCH_FACE_OFF && choice <= GEVR_WATCH_FACE_ONLY
                ? (int)choice : GEVR_WATCH_FACE_ON;
            continue;
        }
        // Gun fit: Gex* are GoldenEye X's models' own, Bean* Bean's (gevr_scope.h's looks)
        const bool gexFit = strncmp(line, "Gex", 3) == 0;
        const bool beanFit = strncmp(line, "Bean", 4) == 0;
        const int look = gexFit ? 1 : beanFit ? 2 : 0;
        const char *fit = gexFit ? line + 3 : beanFit ? line + 4 : line;
        if (beanFit && strncmp(fit, "GunOff=", 7) == 0) {
            float t[3];
            if (sscanf(fit + 7, "%f %f %f", &t[0], &t[1], &t[2]) == 3
                && std::isfinite(t[0]) && std::isfinite(t[1]) && std::isfinite(t[2])) {
                for (int i = 0; i < 3; i++) VrBeanGunOff[i] = t[i];
                beanFitsRead = true;
            }
            continue;
        }
        if ((strncmp(fit, "GunFit", 6) == 0 || strncmp(fit, "GripFit", 7) == 0) && gevrPerGunSet(look) >= 0) {
            const bool grip = fit[1] == 'r';
            const char *name = fit + (grip ? 7 : 6);
            bool known = false;
            for (const auto &w : kMuzzleWeapons) {
                const size_t nameLen = strlen(w.name);
                if (strncmp(name, w.name, nameLen) != 0 || name[nameLen] != '=') continue;
                known = true;
                float t[6];
                const int n = sscanf(name + nameLen + 1, "%f %f %f %f %f %f", &t[0], &t[1], &t[2], &t[3], &t[4], &t[5]);
                bool finite = true;
                for (int i = 0; i < (grip ? 6 : 3); i++) finite = finite && std::isfinite(t[i]);
                const int set = gevrPerGunSet(look);
                if (finite && grip && n == 6) {
                    for (int i = 0; i < 6; i++) VrGripTrimPer[set][w.item][i] = t[i];
                    VrGripTrimPerSet[set][w.item] = 1;
                    beanFitsRead |= beanFit;
                } else if (finite && !grip && n >= 3) {
                    for (int i = 0; i < 3; i++) VrGunOffPer[set][w.item][i] = t[i];
                    VrGunOffPerSet[set][w.item] = 1;
                    beanFitsRead |= beanFit;
                }
                break;
            }
            if (known) continue;
        }
        if (strncmp(fit, "GripPistol=", 11) == 0 || strncmp(fit, "GripRifle=", 10) == 0) {
            const int cls = fit[4] == 'P' ? 0 : 1;
            float t[6];
            if (sscanf(strchr(fit, '=') + 1, "%f %f %f %f %f %f", &t[0], &t[1], &t[2], &t[3], &t[4], &t[5]) == 6) {
                for (int i = 0; i < 6; i++) (look == 1 ? VrGexGripTrim : look == 2 ? VrBeanGripTrim : VrGripTrim)[cls][i] = t[i];
                beanFitsRead |= beanFit;
            }
            continue;
        }
        if (strncmp(fit, "ReloadGrab=", 11) == 0 || strncmp(line, "ReloadBelt=", 11) == 0
            || strncmp(line, "GexHeldMag=", 11) == 0) {
            float t[3];
            float *to = strncmp(fit, "ReloadGrab=", 11) == 0 ? (look == 2 ? VrBeanReloadGrab : VrReloadGrab[look])
                      : line[0] == 'R' ? VrReloadBelt : VrGexHeldMag;
            beanFitsRead |= beanFit;
            if (sscanf(strchr(line, '=') + 1, "%f %f %f", &t[0], &t[1], &t[2]) == 3) {
                for (int i = 0; i < 3; i++) to[i] = t[i];
            }
            continue;
        }
        if (strncmp(line, "BodySlot", 8) == 0) {
            int s = 0;
            while (s < GEVR_BODY_SLOTS && !(strncmp(line + 8, kBodySlotKeys[s], strlen(kBodySlotKeys[s])) == 0
                                            && line[8 + strlen(kBodySlotKeys[s])] == '=')) {
                s++;
            }
            if (s < GEVR_BODY_SLOTS) {
                float t[3];
                if (sscanf(strchr(line, '=') + 1, "%f %f %f", &t[0], &t[1], &t[2]) == 3
                    && std::isfinite(t[0]) && std::isfinite(t[1]) && std::isfinite(t[2])) {
                    for (int i = 0; i < 3; i++) VrBodySlotFit[s][i] = t[i];
                }
                continue;
            }
            // BodySlots=, BodySlotShow=, BodySlotSize=: the integer keys below
        }
        if (strncmp(line, "BeanMagFit", 10) == 0) {
            int item, which, consumed=0; float t[3];
            if (sscanf(line, "BeanMagFit%d_%d=%n", &item, &which, &consumed) == 2 && consumed > 0
                && item >= 0 && item < 64 && which >= 0 && which < 3
                && sscanf(line+consumed, "%f %f %f", &t[0], &t[1], &t[2]) == 3
                && std::isfinite(t[0]) && std::isfinite(t[1]) && std::isfinite(t[2]))
                memcpy(VrBeanMagFits[item][which], t, sizeof(t));
            continue;
        }
        if (strncmp(line, "GexFit", 6) == 0) {
            int item, component, consumed=0; float t[3];
            if (sscanf(line, "GexFit%d_%d=%n", &item, &component, &consumed) == 2 && consumed > 0
                && item >= 0 && item < 64 && component >= 0 && component < 10
                && sscanf(line+consumed, "%f %f %f", &t[0], &t[1], &t[2]) == 3
                && std::isfinite(t[0]) && std::isfinite(t[1]) && std::isfinite(t[2])
                /* every known model's rows are saved, fitted or not: an all-zero row is a
                 * fit never made, and a default added since (the watch items') stands */
                && (t[0] != 0.0f || t[1] != 0.0f || t[2] != 0.0f))
                memcpy(VrGexWeaponFits[item][component], t, sizeof(t));
            continue;
        }
        if (strncmp(line, "GexPP7Grab=", 11) == 0 || strncmp(line, "GexPP7Support=", 14) == 0
            || strncmp(line, "GexPP7GunOff=", 13) == 0
            || strncmp(line, "GexPP7MagOff=", 13) == 0 || strncmp(line, "GexKF7MagOff=", 13) == 0
            || strncmp(line, "GexPP7SupportRot=", 17) == 0
            || strncmp(line, "GexKF7WellOff=", 14) == 0 || strncmp(line, "GexPP7WellOff=", 14) == 0) {
            float t[3];
            float *to = strncmp(line, "GexPP7Grab=", 11) == 0 ? VrGexPp7Grab
                : strncmp(line, "GexPP7GunOff=", 13) == 0 ? VrGexPp7GunOff
                : strncmp(line, "GexPP7MagOff=", 13) == 0 ? VrGexPp7MagOff
                : strncmp(line, "GexKF7MagOff=", 13) == 0 ? VrGexKf7MagOff
                : strncmp(line, "GexPP7SupportRot=", 17) == 0 ? VrGexPp7SupportRot
                : strncmp(line, "GexKF7WellOff=", 14) == 0 ? VrGexKf7WellOff
                : strncmp(line, "GexPP7WellOff=", 14) == 0 ? VrGexPp7WellOff : VrGexPp7Support;
            if (sscanf(strchr(line, '=') + 1, "%f %f %f", &t[0], &t[1], &t[2]) == 3
                && std::isfinite(t[0]) && std::isfinite(t[1]) && std::isfinite(t[2])) {
                for (int i=0; i<3; i++) to[i] = t[i];
            }
            continue;
        }
        if (strncmp(line, "BeanBat=", 8) == 0 || strncmp(line, "Bat=", 4) == 0
            || strncmp(line, "BeanBatOff=", 11) == 0 || strncmp(line, "BatOff=", 7) == 0) {
            const bool off = strstr(line, "BatOff=") != nullptr;
            const int row = (line[0] == 'B' && line[1] == 'e' ? 5 : 6) + (off ? 2 : 0);
            float t[3];
            if (sscanf(strchr(line, '=') + 1, "%f %f %f", &t[0], &t[1], &t[2]) == 3
                && std::isfinite(t[0]) && std::isfinite(t[1]) && std::isfinite(t[2])) {
                for (int i = 0; i < 3; i++) VrGexBatFit[row][i] = t[i];
            }
            continue;
        }
        if (strncmp(line, "GexBat", 6) == 0) {
            /* GexBat=, GexBatHand=, GexBatHandRot= (the rifle butt as a bat) */
            const int which = strncmp(line, "GexBat=", 7) == 0 ? 0 : strncmp(line, "GexBatHand=", 11) == 0 ? 1
                : strncmp(line, "GexBatHandRot=", 14) == 0 ? 2 : strncmp(line, "GexBatOff=", 10) == 0 ? 3
                : strncmp(line, "GexBatOffRot=", 13) == 0 ? 4 : -1;
            float t[3];
            if (which >= 0 && sscanf(strchr(line, '=') + 1, "%f %f %f", &t[0], &t[1], &t[2]) == 3
                && std::isfinite(t[0]) && std::isfinite(t[1]) && std::isfinite(t[2])) {
                for (int i = 0; i < 3; i++) VrGexBatFit[which][i] = t[i];
            }
            continue;
        }
        if (strncmp(line, "GexForeHold=", 12) == 0) {
            float t[3];
            if (sscanf(line + 12, "%f %f %f", &t[0], &t[1], &t[2]) == 3) {
                for (int i = 0; i < 3; i++) VrGexForeHold[i] = t[i];
            }
            continue;
        }
        if (strncmp(line, "GexOffRot=", 10) == 0) {
            float t[3];
            if (sscanf(line + 10, "%f %f %f", &t[0], &t[1], &t[2]) == 3
                && std::isfinite(t[0]) && std::isfinite(t[1]) && std::isfinite(t[2])) {
                for (int i = 0; i < 3; i++) VrGexOffRot[i] = t[i];
            }
            continue;
        }
        if (strncmp(line, "GexWatch=", 9) == 0) {
            float t[4];
            if (sscanf(line + 9, "%f %f %f %f", &t[0], &t[1], &t[2], &t[3]) == 4 && t[3] > 0.1f) {
                for (int i = 0; i < 4; i++) VrGexWatch[i] = t[i];
            }
            continue;
        }
        if (strncmp(line, "GexGunOff=", 10) == 0) {
            float t[3];
            if (sscanf(line + 10, "%f %f %f", &t[0], &t[1], &t[2]) == 3) {
                for (int i = 0; i < 3; i++) VrGexGunOff[i] = t[i];
            }
            continue;
        }
        if (strncmp(fit, "Scope", 5) == 0) {
            int s = 0;
            while (s < GEVR_SCOPE_FITS && !(strncmp(fit + 5, kScopeFitNames[s], strlen(kScopeFitNames[s])) == 0
                                            && fit[5 + strlen(kScopeFitNames[s])] == '=')) {
                s++;
            }
            if (s < GEVR_SCOPE_FITS) {
                float t[4];
                if (sscanf(strchr(fit, '=') + 1, "%f %f %f %f", &t[0], &t[1], &t[2], &t[3]) == 4) {
                    for (int i = 0; i < 4; i++) gevrScopeFitOf(look, s)[i] = t[i];
                    beanFitsRead |= beanFit;
                }
                continue;
            }
        }
        if (strncmp(fit, "Rack", 4) == 0) {
            for (const auto &w : kMuzzleWeapons) {
                const size_t nameLen = strlen(w.name);
                if (strncmp(fit + 4, w.name, nameLen) == 0 && fit[4 + nameLen] == '=') {
                    float t[3];
                    if (sscanf(fit + 4 + nameLen + 1, "%f %f %f", &t[0], &t[1], &t[2]) == 3) {
                        for (int i = 0; i < 3; i++) VrRackFit[look][w.item][i] = t[i];
                        beanFitsRead |= beanFit;
                    }
                    break;
                }
            }
            continue;
        }
        if (strncmp(fit, "Muzzle", 6) == 0) {
            for (const auto &w : kMuzzleWeapons) {
                const size_t nameLen = strlen(w.name);
                if (strncmp(fit + 6, w.name, nameLen) == 0 && fit[6 + nameLen] == '=') {
                    float t[3];
                    if (sscanf(fit + 6 + nameLen + 1, "%f %f %f", &t[0], &t[1], &t[2]) == 3) {
                        for (int i = 0; i < 3; i++) gevrMuzzleTrimOf(look, w.item)[i] = t[i];
                        beanFitsRead |= beanFit;
                    }
                    break;
                }
            }
            continue;
        }
        if (strncmp(line, "Cheats=", 7) == 0) {        // hex bitmask of CHEAT_IDS
            VrCheatMask = strtoull(line + 7, NULL, 16);
            continue;
        }
        if (strncmp(line, "LiveSplitHost=", 14) == 0) {   // text: an address or a name
            strncpy(VrLiveSplitHost, line + 14, sizeof(VrLiveSplitHost) - 1);
            VrLiveSplitHost[sizeof(VrLiveSplitHost) - 1] = '\0';
            VrLiveSplitHost[strcspn(VrLiveSplitHost, "\r\n \t")] = '\0';
            continue;
        }
        if (strncmp(line, "PlayerName=", 11) == 0) {   // text, even when it reads as a number ("007")
            strncpy(VrPlayerName, line + 11, sizeof(VrPlayerName) - 1);
            VrPlayerName[sizeof(VrPlayerName) - 1] = '\0';
            VrPlayerName[strcspn(VrPlayerName, "\r\n")] = '\0';
            continue;
        }

        // Custom Haptics line (Intensity,Duration)
        char kbuf[64] = {}, vbuf[128] = {};
        if (sscanf(line, "%63[^=]=%127[^\r\n]", kbuf, vbuf) == 2) {
            if (vrHapticsLoadLine(kbuf, vbuf)) {
                continue;
            }
        }

        // 1. Is it an integer (%d)? Not if the value has a decimal point: "%d"
        // happily reads the 30 of "SnapTurn=30.0", and the float keys were
        // then swallowed by this branch and never loaded (GoldenEye fix).
        const char *eq = strchr(line, '=');
        const bool looksFloat = eq != NULL && strchr(eq, '.') != NULL;
        if (!looksFloat && sscanf(line, "%63[^=]=%d", key, &ival) == 2) {
            if (strcmp(key, "ManualReloading") == 0) VrManualReloading = ival != 0;
            else if (strcmp(key, "HandReloadDefaulted") == 0) handReloadDefaulted = true;
            else if (strcmp(key, "LaserDotForAll") == 0) VrlaserDotForALL = ival != 0;
            else if (strcmp(key, "SeatedMode") == 0) VrSeatedMode = ival != 0;
            else if (strcmp(key, "MotionThrowing") == 0) VrMotionThrowing = ival != 0;
            else if (strcmp(key, "WeaponRecoil") == 0) VrWeaponRecoil = (ival != 0);
            else if (strcmp(key, "StickClickToCrouch") == 0) VrStickClickToCrouch = (ival != 0);
            else if (strcmp(key, "AimNoLean") == 0) VrAimNoLean = (ival != 0);
            else if (strcmp(key, "AimSight") == 0) VrAimSight = (ival != 0);
            else if (strcmp(key, "NoKnockback") == 0) VrNoKnockback = (ival != 0);
            else if (strcmp(key, "NoHitstun") == 0) VrNoHitstun = (ival != 0);
            else if (strcmp(key, "DamageFlash") == 0) VrDamageFlash = (ival != 0);
            else if (strcmp(key, "ThirdPerson") == 0) VrThirdPerson = (ival != 0);
            else if (strcmp(key, "ThirdPersonDistance") == 0)
                VrThirdPersonDist = ival < THIRDPERSON_DIST_MIN ? THIRDPERSON_DIST_MIN : ival > THIRDPERSON_DIST_MAX ? THIRDPERSON_DIST_MAX : ival;
            else if (strcmp(key, "ThirdPersonHeight") == 0)
                VrThirdPersonHeight = ival < THIRDPERSON_HEIGHT_MIN ? THIRDPERSON_HEIGHT_MIN : ival > THIRDPERSON_HEIGHT_MAX ? THIRDPERSON_HEIGHT_MAX : ival;
            else if (strcmp(key, "ThirdPersonSideways") == 0)
                VrThirdPersonSide = ival < THIRDPERSON_SIDE_MIN ? THIRDPERSON_SIDE_MIN : ival > THIRDPERSON_SIDE_MAX ? THIRDPERSON_SIDE_MAX : ival;
            else if (strcmp(key, "PauseHub") == 0) VrPauseHub = (ival != 0);
            else if (strcmp(key, "TwoHandedAiming") == 0) VrTwoHandAim = (ival != 0);
            else if (strcmp(key, "LeftHandedMode") == 0) VrLeftHandedMode = (ival != 0);
            else if (strcmp(key, "WatchGesturePause") == 0) VrWatchGesturePause = (ival != 0);
            else if (strcmp(key, "SwapJoysticks") == 0) VrSwapJoysticks = (ival != 0);
            else if (strcmp(key, "AimSteadying") == 0) VrAimSteady = ival < 0 ? 0 : ival > 2 ? 2 : ival;
            else if (strcmp(key, "TouchControllers") == 0) VrTouchModel = ival < 0 ? 0 : ival > 4 ? 4 : ival;
            else if (strcmp(key, "MovementSpeed") == 0) VrMovementSpeed = netMovementSpeedMode(ival);
            else if (strcmp(key, "MpMovementSpeed") == 0) VrMpMovementSpeed = netMovementSpeedMode(ival);
            else if (strcmp(key, "SmoothTurnSpeed") == 0)
                VrSmoothTurnSpeed = ival < SMOOTHTURN_MIN ? SMOOTHTURN_MIN : ival > SMOOTHTURN_MAX ? SMOOTHTURN_MAX : ival;
            else if (strcmp(key, "GestureGripUse") == 0) VrGestureGripUse = ival != 0;
            else if (strcmp(key, "GestureMineGrab") == 0) VrGestureMineGrab = ival != 0;
            else if (strcmp(key, "ForcePull") == 0) VrForcePull = ival != 0;
            else if (strcmp(key, "BodySlots") == 0) VrBodySlots = ival != 0;
            else if (strcmp(key, "BodySlotShow") == 0) VrBodySlotShow = ival != 0;
            else if (strcmp(key, "BodySlotSize") == 0) VrBodySlotSize = ival < 0 ? 0 : ival > 2 ? 2 : ival;
            else if (strcmp(key, "PerWeaponRecoil") == 0) VrPerWeaponRecoil = ival != 0;
            else if (strcmp(key, "MinesStickToGuards") == 0) VrMinesStickToGuards = ival != 0;
            else if (strcmp(key, "Ally") == 0) VrAlly = ival != 0;
            else if (strcmp(key, "AllyWeapon") == 0) VrAllyWeapon = ival < 0 ? 0 : ival > 9 ? 9 : ival;
            else if (strcmp(key, "AllyAggression") == 0) VrAllyAggression = ival < 0 ? 0 : ival > 2 ? 2 : ival;
            else if (strcmp(key, "AllyHealth") == 0) VrAllyHealth = ival < 25 ? 25 : ival > 1000 ? 1000 : ival;
            else if (strcmp(key, "AllyDamage") == 0) VrAllyDamage = ival < 25 ? 25 : ival > 400 ? 400 : ival;
            else if (strcmp(key, "AllyInvincible") == 0) VrAllyInvincible = ival != 0;
            else if (strcmp(key, "FastReinforcements") == 0) VrFastReinforcements = ival != 0;
            else if (strcmp(key, "CoopFastReinforcements") == 0) VrCoopFastReinforcements = ival != 0;
            else if (strcmp(key, "BodiesStay") == 0)
                /* 48 was an option until a pile of 41 Bean ragdolls held the GPU
                 * near its limit (2026-10-30): a saved 48 keeps 24 */
                VrBodiesStay = ival >= 24 ? 24 : ival >= 12 ? 12 : 0;
            else if (strcmp(key, "ShowStats") == 0) VrShowStats = (ival != 0);
            else if (strcmp(key, "ICloudSync") == 0) VrICloudSync = (ival != 0);
            else if (strcmp(key, "ICloudPacks") == 0) VrICloudPacks = (ival != 0);
            else if (strcmp(key, "SpeedrunTimer") == 0) VrSpeedrunTimer = (ival != 0);
            else if (strcmp(key, "GhostRace") == 0) VrGhostRace = (ival != 0);
            else if (strcmp(key, "LiveSplit") == 0) VrLiveSplit = (ival != 0);
            else if (strcmp(key, "SpaceWarp") == 0) VrSpaceWarp = ival < 0 ? -1 : ival != 0;
#ifdef GEVR_PCVR
            else if (strcmp(key, "EyeMsaa") == 0) VrEyeMsaa = ival >= 8 ? 8 : ival >= 4 ? 4 : ival >= 2 ? 2 : 1;
#else
            else if (strcmp(key, "EyeMsaa") == 0) VrEyeMsaa = ival >= 4 ? 4 : ival >= 2 ? 2 : 1;   // 4x is back, warned
#endif
            else if (strcmp(key, "TextureAniso") == 0) VrTextureAniso = ival <= 0 ? 0 : ival >= 16 ? 16 : ival >= 8 ? 8 : ival >= 4 ? 4 : ival >= 2 ? 2 : 1;
            else if (strcmp(key, "VolumetricExplosions") == 0) VrVolumetricExplosions = ival != 0;
            else if (strcmp(key, "CpuLevel") == 0) VrCpuLevel = ival < 0 ? -1 : ival > 2 ? 2 : ival;
            else if (strcmp(key, "BatTwoHand") == 0) VrBatTwoHand = ival ? 1 : 0;
            else if (strcmp(key, "RenderScale") == 0) VrResetRenderScale = 1;
            else if (strcmp(key, "GunSizeCheat") == 0) VrGunSizeCheat = ival < 0 ? 0 : ival > 2 ? 2 : ival;
            else if (strcmp(key, "UnlockAll") == 0) VrUnlockAll = ival != 0;
            else if (strcmp(key, "HideArms") == 0) VrHideArms = (ival != 0);
            else if (strcmp(key, "MatchCharacterHeight") == 0) VrMatchCharacterHeight = (ival != 0);
            else if (strcmp(key, "GexFistClench") == 0) VrFistClench = ival != 0;
            else if (strcmp(key, "AutoSwitchKeepMs") == 0) VrAutoSwitchKeepMs = ival < 0 ? 0 : ival > 5000 ? 5000 : ival;
            else if (strcmp(key, "PlayMode") == 0) VrPlayMode = gevrIniPlayMode(ival);
            else if (strcmp(key, "XblaVisualsMode") == 0) VrBeanModels = ival != 0;
            else if (strcmp(key, "BeanFillTextures") == 0) VrBeanFillTextures = ival != 0;
            else if (strcmp(key, "BeanCommunityEdition") == 0) VrBeanCommunityEdition = ival != 0;
            else if (strcmp(key, "BloodPatch") == 0) VrBloodPatch = ival != 0;
            else if (strcmp(key, "OddjobHat") == 0) VrOddjobHat = ival != 0;
            else if (strcmp(key, "Ragdolls") == 0) VrRagdolls = ival != 0;
            else if (strcmp(key, "Dismemberment") == 0) VrDismemberment = ival != 0;
            else if (strcmp(key, "DismemberChance") == 0) VrDismemberChance = ival < 0 ? 0 : ival > 100 ? 100 : ival;
            else if (strcmp(key, "DismemberDamage") == 0) VrDismemberDamage = ival < 1 ? 0.5f : ival > 10 ? 10.0f : (float)ival;
            else if (strcmp(key, "MicMuted") == 0) VrMicMuted = ival != 0;
            else if (strcmp(key, "MpStage") == 0) VrMpStage = ival;
            else if (strcmp(key, "MpWeaponSet") == 0) VrMpWeaponSet = ival;
            else if (strcmp(key, "MpChr") == 0) VrMpChr = ival;
            else if (strcmp(key, "MpVisibility") == 0) VrMpVisibility = ival < 0 ? 0 : ival > 2 ? 2 : ival;
            else if (strcmp(key, "MpVrOnly") == 0) VrMpVrOnly = ival != 0;
            else if (strcmp(key, "MpHideFlat") == 0) VrMpHideFlat = ival != 0;
            else if (strcmp(key, "MpHandReloadOnly") == 0) VrMpHandReloadOnly = ival != 0;
            else if (strcmp(key, "MpHideButton") == 0) VrMpHideButton = ival != 0;
            else if (strcmp(key, "MpServerFilter") == 0) VrMpServerFilter = ival < 0 || ival > 3 ? 0 : ival;
            else if (strcmp(key, "FriendsShowDetail") == 0) VrFriendsShowDetail = ival != 0;
            else if (strcmp(key, "FriendsAppearOffline") == 0) VrFriendsAppearOffline = ival != 0;
            else if (strcmp(key, "FriendsRestoreNoted") == 0) VrFriendsRestoreNoted = ival != 0;
            else if (strcmp(key, "FriendsSidebar") == 0) VrFriendsSidebar = ival != 0;
            else if (strcmp(key, "FriendsSearchable") == 0) VrFriendsSearchable = ival != 0;
            else if (strcmp(key, "HostEqualization") == 0) VrHostEqualization = ival != 0;
            else if (strcmp(key, "HostLatencyCapMs") == 0) VrHostLatencyCapMs = ival < 0 ? 0 : ival > 80 ? 80 : ival;
            else if (strcmp(key, "MpFriendlyFire") == 0) VrMpFriendlyFire = ival != 0;
            else if (strcmp(key, "HordeFriendlyFire") == 0) VrHordeFriendlyFire = ival != 0;
            else if (strcmp(key, "HordeStage") == 0) VrHordeStage = netHordeStageValid((unsigned)ival) ? ival : NET_HORDE_STAGE;
            else if (strcmp(key, "HordeStartWave") == 0) VrHordeStartWave = ival < 1 ? 1 : ival > 50 ? 50 : ival;
            else if (strcmp(key, "MpFunFlags") == 0) VrMpFunFlags = ival >= 0 && !(ival & ~NET_FUN_MASK) ? ival : 0;
            else if (strcmp(key, "MpGunSize") == 0) VrMpGunSize = ival >= 0 && ival <= 2 ? ival : 0;
            else if (strcmp(key, "MpMaxPlayers") == 0) VrMpMaxPlayers = ival >= 2 && ival <= 8 ? ival : 4;
            else if (strcmp(key, "MpBotMode") == 0) VrMpBotMode = ival >= 0 && ival <= 2 ? ival : 0;
            else if (strcmp(key, "MpBotCount") == 0) VrMpBotCount = ival >= 1 && ival <= 7 ? ival : 3;
            else if (strcmp(key, "MpBotDifficulty") == 0) VrMpBotDifficulty = ival >= 0 && ival <= 5 ? ival : 2;
            else if (strcmp(key, "DetailedGuns") == 0) VrDetailedGuns = ival != 0;
            else if (strcmp(key, "GexGuns") == 0) VrGexGuns = ival != 0;
            else if (strcmp(key, "Campaign") == 0) VrCampaign = ival >= 0 && ival < GEVR_CAMPAIGN_COUNT ? ival : 0;
            /* GexArms (before one toggle): on by default, nothing without the guns; GexGuns decides */
            else if (strcmp(key, "MpVoiceMode") == 0) VrMpVoiceMode = ival == 1 ? 1 : 0;
            else if (strcmp(key, "MpScenario") == 0) VrMpScenario = ival;
            else if (strcmp(key, "MpLength") == 0) VrMpLength = ival;
            else if (strcmp(key, "MpHealth") == 0) VrMpHealth = ival;
            else if (strcmp(key, "MpDual") == 0) VrMpDual = ival;
            else if (strcmp(key, "MpLoadouts") == 0) VrMpLoadouts = ival != 0;
            else if (strcmp(key, "MpHandicaps") == 0) VrMpHandicaps = ival != 0;
            else if (strcmp(key, "MpLobby") == 0) VrMpLobby = ival != 0;
            else if (strcmp(key, "MpWarmup") == 0) VrMpWarmup = ival >= 0 && ival <= 3 ? ival : 0;
            else if (strcmp(key, "MpNextRound") == 0) VrMpNextRound = ival;
            else if (strcmp(key, "MpFavStages") == 0) VrMpFavStages = strtoull(eq + 1, NULL, 10); /* past 32 bits */
            else if (strcmp(key, "MpFavSets") == 0) VrMpFavSets = (unsigned)ival;
            else if (strcmp(key, "MpMode") == 0) VrMpMode = ival >= 0 && ival <= 2 ? ival : 0;
            else if (strcmp(key, "MpMission") == 0) VrMpMission = ival;
            else if (strcmp(key, "MpDifficulty") == 0) VrMpDifficulty = ival >= 0 && ival <= 3 ? ival : 0;
            else if (strncmp(key, "MpCustom", 8) == 0 && key[8] >= '1' && key[8] <= '4') VrMpCustom[key[8] - '1'] = ival;
            else if (strncmp(key, "MpLoadout", 9) == 0 && key[9] >= '1' && key[9] <= '4') VrMpLoadout[key[9] - '1'] = ival;
            else if (strncmp(key, "Guest", 5) == 0 && key[5] >= '2' && key[5] <= '4') {
                int g = key[5] - '2';
                const char *rest = key + 6;
                if (strcmp(rest, "Name") == 0) {
                    strncpy(VrGuestName[g], eq + 1, sizeof(VrGuestName[g]) - 1);
                    VrGuestName[g][sizeof(VrGuestName[g]) - 1] = '\0';
                    VrGuestName[g][strcspn(VrGuestName[g], "\r\n")] = '\0';
                } else if (strcmp(rest, "Chr") == 0) {
                    VrGuestChr[g] = ival >= 0 ? ival : 0;
                } else if (strncmp(rest, "Loadout", 7) == 0 && rest[7] >= '1' && rest[7] <= '4') {
                    VrGuestLoadout[g][rest[7] - '1'] = ival;
                }
            }
            else if (strcmp(key, "ScreenCurved") == 0) VrScreenCurved = ival != 0;
            else if (strcmp(key, "ScreenPassthrough") == 0) VrScreenPassthrough = ival != 0;
            else if (strcmp(key, "ScreenShape") == 0) VrScreenShape = ival == 1 ? 1 : 0;
            else if (strcmp(key, "TouchHaptics") == 0) VrTouchHaptics = ival != 0;
            else if (strcmp(key, "TouchControls") == 0) VrTouchControls = ival != 0;
            else if (strcmp(key, "TriggerFeedback") == 0) VrTriggerFeedback = ival == 1 ? 100 : ival < 0 ? 0 : ival > 100 ? 100 : ival;
            else if (strcmp(key, "AdaptiveTriggers") == 0) VrAdaptiveTriggers = ival != 0;
            else if (strcmp(key, "PadDefaults") == 0) s_padDefaults = ival;
            else if (strcmp(key, "PadLookInvert") == 0) s_padLookInvert = ival != 0;
            else if (strcmp(key, "HeadBob") == 0) VrHeadBob = ival != 0;
            else if (strcmp(key, "Teleport") == 0) VrTeleport = ival != 0;
            else if (strcmp(key, "FrameRate") == 0) {
                /* 0 Auto, -1 Uncapped, >= 25 explicit cap; legacy 1/2/3 map to 60/30/Auto */
                if (ival == 0 || ival == 3) VrFrameRate = 0;
                else if (ival == 1) VrFrameRate = 60;
                else if (ival == 2) VrFrameRate = 30;
                else if (ival == -1) VrFrameRate = -1;
                else if (ival >= 25 && ival <= 1000) VrFrameRate = ival;
                else VrFrameRate = 0;
            }
            else if (strcmp(key, "LookUprightFolders") == 0) VrLookUprightFolders = ival;
            else if (strcmp(key, "GyroAim") == 0) VrGyroAim = ival < 0 ? 0 : ival > 2 ? 2 : ival;
            /* Keep saved DisplayHz preferences. The obsolete RefreshRate key
             * was never user-selectable and remains ignored. */
            else if (strcmp(key, "DisplayHz") == 0) VrRefreshRate = ival < 0 ? 0 : ival;
            else if (strcmp(key, "MusicVolume") == 0) VrMusicVolume = ival <= 0 ? 0.0f : (ival >= 1 ? 1.0f : (float)ival);
            else if (strcmp(key, "VoiceVolume") == 0) VrVoiceVolume = ival <= 0 ? 0.0f : (ival >= 1 ? 1.0f : (float)ival);
            else if (strcmp(key, "SfxVolume") == 0) VrSfxVolume = ival <= 0 ? 0.0f : 1.0f;
        }
            // 2. OTHERWISE, is it a floating-point number (%f)?
        else if (sscanf(line, "%63[^=]=%f", key, &fval) == 2) {
            if (strcmp(key, "Vibration") == 0) inputRumbleSetStrength(0, fval);
            else if (strcmp(key, "PadLookSpeed") == 0) s_padLookSpeed = fval;
            else if (strcmp(key, "PadLookDeadzone") == 0) s_padLookDeadzone = fval;
            else if (strcmp(key, "StereoCrosshair") == 0) {
                if (fval < HUD_STEREO_DEPTH_MIN) fval = HUD_STEREO_DEPTH_MIN;
                if (fval > HUD_STEREO_DEPTH_MAX) fval = HUD_STEREO_DEPTH_MAX;
                VrStereoCrosshair = fval;
            }
            else if (strcmp(key, "HudDistance") == 0) {
                if (fval < HUD_DISTANCE_MIN) fval = HUD_DISTANCE_MIN;
                if (fval > HUD_DISTANCE_MAX) fval = HUD_DISTANCE_MAX;
                VrHudDistance = fval;
            }
            else if (strcmp(key, "WorldScale") == 0) {
                if (fval < WORLDSCALE_MIN) fval = WORLDSCALE_MIN;
                if (fval > WORLDSCALE_MAX) fval = WORLDSCALE_MAX;
                VrSetWorldScale = fval;
            }
            else if (strcmp(key, "PlayerHeight") == 0) {
                if (fval < PLAYERHEIGHT_MIN) fval = PLAYERHEIGHT_MIN;
                if (fval > PLAYERHEIGHT_MAX) fval = PLAYERHEIGHT_MAX;
                VrPlayerHeight = fval;
            }
            else if (strcmp(key, "SnapTurn") == 0) {
                if (fval < 0.0f) fval = 0.0f;
                if (fval > 90.0f) fval = 90.0f;
                VrUseSnapTurn = fval;
            }
            else if (strcmp(key, "ArmElbowTuck") == 0) VrArmElbowTuck = fval;
            /* nothing read it before body slots, and every file saved 0: the
             * torso has to follow, so 0 or less is Perfect Dark VR's 0.02 */
            else if (strcmp(key, "ArmBodyFollow") == 0) VrArmBodyFollow = fval > 0.0f && fval <= 1.0f ? fval : 0.02f;
            else if (strcmp(key, "GunOffX") == 0) { gunOff[0] = fval; gunOffRead = true; }
            else if (strcmp(key, "GunOffY") == 0) { gunOff[1] = fval; gunOffRead = true; }
            else if (strcmp(key, "GunOffZ") == 0) { gunOff[2] = fval; gunOffRead = true; }
            else if (strcmp(key, "RenderScale") == 0) VrResetRenderScale = 1;
            /* 0 (or nonsense) leaves the game config's scale; else the eye images' range */
            else if (strcmp(key, "EyeResolution") == 0) VrRenderScale = fval >= 0.5f && fval <= 2.0f ? fval : 0.0f;
            else if (strcmp(key, "ScreenDistance") == 0) {
                if (fval < VR_SCREEN_DISTANCE_MIN) fval = VR_SCREEN_DISTANCE_MIN;
                if (fval > VR_SCREEN_DISTANCE_MAX) fval = VR_SCREEN_DISTANCE_MAX;
                VrScreenDistance = fval;
            }
            else if (strcmp(key, "GyroSpeed") == 0) VrGyroSpeed = fminf(4.0f, fmaxf(0.5f, fval));
            else if (strcmp(key, "ComfortVignette") == 0) {
                if (fval < 0.0f) fval = 0.0f;
                if (fval > 1.0f) fval = 1.0f;
                VrComfortVignette = fval;
            }
            else if (strcmp(key, "MotionThrowPitch") == 0) {
                if (fval < -45.0f) fval = -45.0f;
                if (fval > 45.0f) fval = 45.0f;
                VrMotionThrowPitch = fval;
            }
            else if (strcmp(key, "MotionThrowGazeAssist") == 0) {
                if (fval < 0.0f) fval = 0.0f;
                if (fval > 1.0f) fval = 1.0f;
                VrMotionThrowGazeAssist = fval;
            }
            else if (strcmp(key, "DismemberDamage") == 0) VrDismemberDamage = fval < 0.5f ? 0.5f : fval > 10.0f ? 10.0f : fval;
            else if (strcmp(key, "MotionThrowStrength") == 0) {
                if (fval < 0.2f) fval = 0.2f;
                if (fval > 3.0f) fval = 3.0f;
                VrMotionThrowStrength = fval;
            }
            else if (strcmp(key, "ScreenHeight") == 0) {
                if (fval < -VR_SCREEN_HEIGHT_MAX) fval = -VR_SCREEN_HEIGHT_MAX;
                if (fval > VR_SCREEN_HEIGHT_MAX) fval = VR_SCREEN_HEIGHT_MAX;
                VrScreenHeight = fval;
            }
            else if (strcmp(key, "ScreenFov") == 0) {
                if (fval < VR_SCREEN_FOV_MIN) fval = VR_SCREEN_FOV_MIN;
                if (fval > VR_SCREEN_FOV_MAX) fval = VR_SCREEN_FOV_MAX;
                VrScreenFov = fval;
            }
            else if (strcmp(key, "MasterVolume") == 0) {
                if (fval < 0.0f) fval = 0.0f;
                if (fval > 1.0f) fval = 1.0f;
                VrMasterVolume = fval;
            }
            else if (strcmp(key, "MusicVolume") == 0) {
                if (fval < 0.0f) fval = 0.0f;
                if (fval > 1.0f) fval = 1.0f;
                VrMusicVolume = fval;
            }
            else if (strcmp(key, "VoiceVolume") == 0) {
                if (fval < 0.0f) fval = 0.0f;
                if (fval > 1.0f) fval = 1.0f;
                VrVoiceVolume = fval;
            }
            else if (strcmp(key, "SfxVolume") == 0) {
                if (fval < 0.0f) fval = 0.0f;
                if (fval > 1.0f) fval = 1.0f;
                VrSfxVolume = fval;
            }
        }
            // 3. OTHERWISE, is it text (%s)?
        else if (sscanf(line, "%63[^=]=%255[^\n]", key, sval) == 2) {
            if (strcmp(key, "ActiveTexturePack") == 0) {
                strncpy(g_ActiveExtTexPack, sval, FS_MAXPATH - 1);
                g_ActiveExtTexPack[FS_MAXPATH - 1] = '\0';
            }
        }
    }

    fclose(f);

    // Hand reload works on every gun set since v0.5.26 and is on by default: a file from
    // before then had it off only by the old default, so it is switched on once (owner,
    // 2026-11-23); turned off again afterwards, it stays off.
    if (!handReloadDefaulted) VrManualReloading = 1;

    unsigned short mVol = (unsigned short)(VrMusicVolume * 32767.0f);
    set_mTrack2Vol(mVol);
    musicTrack1ApplySeqpVol(mVol);
    musicTrack3ApplySeqpVol(mVol);
    gevrSndApplySfxVolume((unsigned short)(VrSfxVolume * 32767.0f));

    // GunOffX/Y/Z of 0, 0, 0 is the old default, which every install wrote
    // before the launcher's Gun fit existed: it keeps the fitted defaults
    // (vr_settings_defaults.c). Any other trim is the player's own.
    if (gunOffRead && (gunOff[0] != 0.0f || gunOff[1] != 0.0f || gunOff[2] != 0.0f)) {
        VrGunOffX = gunOff[0];
        VrGunOffY = gunOff[1];
        VrGunOffZ = gunOff[2];
    }
    // Bean's models (MODS) stand where GoldenEye's would (gevr_bean.c), so an
    // ini from before they had a set of their own starts their reload grab,
    // scopes and barrel tips from the player's GoldenEye fits. Not the gun
    // or the holds: Bean's are fitted on its own holding arm
    // (vr_settings_defaults.c), GoldenEye's on the taser hand.
    if (!beanFitsRead) {
        memcpy(VrBeanReloadGrab, VrReloadGrab[0], sizeof(VrBeanReloadGrab));
        memcpy(VrBeanScopeFit, VrScopeFit[0], sizeof(VrBeanScopeFit));
        memcpy(VrBeanMuzzleTrim, VrMuzzleTrim[0], sizeof(VrBeanMuzzleTrim));
    }

    // Pads (#269). Every install wrote Vibration=0.5000, the old default, so an
    // ini from before PadDefaults keeps 0.5 only as that default: 30% now.
    // Then one strength for every pad, a split screen's too, and the look rows'.
    {
        float vib = inputRumbleGetStrength(0);
        if (s_padDefaults < 2 && std::fabs(vib - 0.5f) < 0.001f) vib = 0.3f;
        inputRumbleSetStrengthAll(vib);
        if (s_padLookSpeed > 0.0f || s_padLookDeadzone >= 0.0f || s_padLookInvert >= 0) {
            inputPadLookSet(s_padLookSpeed > 0.0f ? s_padLookSpeed : inputPadLookSpeed(),
                            s_padLookDeadzone >= 0.0f ? s_padLookDeadzone : inputPadLookDeadzone(),
                            s_padLookInvert >= 0 ? s_padLookInvert : inputPadLookInverted());
        }
    }

    // Tell the engine to use external textures.
    // Initialization will be safely handled by the game later via extTexInit().
    if (g_ActiveExtTexPack[0] != '\0') {
        extTexSetPack(g_ActiveExtTexPack);
        videoSetExternalTextures(true);
    }
#ifdef GEVR_FLAT
    // The flat PC game has no stereo view (PCVR, docs/pc-platforms.md P2, will
    // choose it with its session): the screen, so flat-only rows like third
    // person are open whatever an ini from elsewhere says.
    VrPlayMode = 0;
#endif
    vrSyncGuestSeatConfigs();
}

extern "C" void netSetLocalSeatConfig(int seat, const char *name, uint8_t chr_id, uint8_t input_class, const uint8_t loadout[4]);

void vrSyncGuestSeatConfigs(void)
{
    for (int g = 0; g < 3; g++) {
        uint8_t kit[4];
        for (int k = 0; k < 4; k++) kit[k] = (uint8_t)VrGuestLoadout[g][k];
        netSetLocalSeatConfig(g + 1, VrGuestName[g], (uint8_t)VrGuestChr[g], 2 /* NET_INPUT_PAD */, kit);
    }
}
