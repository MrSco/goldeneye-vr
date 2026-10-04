#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "vr_settings.h"
#include "vr_screen.h"
#include "vr_haptics.h"

extern "C" float inputRumbleGetStrength(int playernum);
extern "C" void inputRumbleSetStrength(int playernum, int strength);

#define FS_MAXPATH 256
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

extern "C" void vrSettingsSave(void)
{
    if (!s_settingsLoaded) return;
    FILE *f = fopen(VR_INI_PATH, "w");
    if (!f) return;

    fprintf(f, "[VR]\n");
    fprintf(f, "ManualReloading=%d\n", VrManualReloading ? 1 : 0);
    fprintf(f, "LaserDotForAll=%d\n", VrlaserDotForALL ? 1 : 0);
    fprintf(f, "SeatedMode=%d\n", VrSeatedMode ? 1 : 0);
    fprintf(f, "MotionThrowing=%d\n", VrMotionThrowing ? 1 : 0);
    fprintf(f, "MotionThrowPitch=%.1f\n", VrMotionThrowPitch);
    fprintf(f, "MotionThrowGazeAssist=%.2f\n", VrMotionThrowGazeAssist);
    fprintf(f, "MotionThrowStrength=%.2f\n", VrMotionThrowStrength);
    fprintf(f, "Vibration=%.4f\n", inputRumbleGetStrength(g_ExtMenuPlayer));
    fprintf(f, "StereoCrosshair=%.4f\n", VrStereoCrosshair);
    fprintf(f, "HudDistance=%.4f\n", VrHudDistance);
    fprintf(f, "WeaponRecoil=%d\n", VrWeaponRecoil ? 1 : 0);
    fprintf(f, "WorldScale=%.4f\n", VrSetWorldScale);
    fprintf(f, "PauseHub=%d\n", VrPauseHub ? 1 : 0);
    fprintf(f, "StickClickToCrouch=%d\n", VrStickClickToCrouch ? 1 : 0);
    fprintf(f, "; 1 = holding the aim trigger no longer leans or ducks: the move stick keeps moving (issue #81).\n");
    fprintf(f, "AimNoLean=%d\n", VrAimNoLean ? 1 : 0);
    fprintf(f, "SnapTurn=%.1f\n", VrUseSnapTurn);
    fprintf(f, "TwoHandedAiming=%d\n", VrTwoHandAim ? 1 : 0);
    fprintf(f, "LeftHandedMode=%d\n", VrLeftHandedMode ? 1 : 0);
    fprintf(f, "; Watch face: 0 Off, 1 On (also standard HUD), 2 Only (gameplay).\n");
    fprintf(f, "WatchFaceStatus=%d\n", gevrWatchStatusChoice(VrWatchFaceStatus));
    fprintf(f, "WatchGesturePause=%d\n", VrWatchGesturePause ? 1 : 0);
    fprintf(f, "SwapJoysticks=%d\n", VrSwapJoysticks ? 1 : 0);
    fprintf(f, "AimSteadying=%d\n", VrAimSteady);
    fprintf(f, "ShowStats=%d\n", VrShowStats ? 1 : 0);
    fprintf(f, "Cheats=%llx\n", (unsigned long long)VrCheatMask);
    fprintf(f, "GunSizeCheat=%d\n", VrGunSizeCheat);
    fprintf(f, "; 1 = every mission at every difficulty, 007 mode and every cheat unlocked.\n");
    fprintf(f, "UnlockAll=%d\n", VrUnlockAll ? 1 : 0);
    fprintf(f, "HideArms=%d\n", VrHideArms ? 1 : 0);
    fprintf(f, "ActiveTexturePack=%s\n", g_ActiveExtTexPack);
    fprintf(f, "; Your standing EYE height in cm -- where your eyes are off the floor, which is\n");
    fprintf(f, "; what the headset reports, roughly 13 cm below the top of your head. Set it from\n");
    fprintf(f, "; the live reading beside the menu slider rather than from your stature.\n");
    fprintf(f, "PlayerHeight=%.1f\n", VrPlayerHeight);
    fprintf(f, "; 1 = stand at the height of the character you are playing instead of your own,\n");
    fprintf(f, "; so Elvis is short and Mr Blonde towers. 0 = you are your own height throughout.\n");
    fprintf(f, "MatchCharacterHeight=%d\n", VrMatchCharacterHeight ? 1 : 0);

    // --- GoldenEye presentation -------------------------------------------------------------------
    fprintf(f, "\n");
    fprintf(f, "; 1 = true stereo in first-person play (menus, cutscenes and the watch stay on the\n");
    fprintf(f, "; virtual screen), 0 = everything on the virtual screen. Hold the right stick click\n");
    fprintf(f, "; in game to switch.\n");
    fprintf(f, "PlayMode=%d\n", VrPlayMode);
    fprintf(f, "MicMuted=%d\n", VrMicMuted ? 1 : 0);
    fprintf(f, "MusicVolume=%.2f\n", VrMusicVolume);
    fprintf(f, "VoiceVolume=%.2f\n", VrVoiceVolume);
    fprintf(f, "SfxVolume=%.2f\n", VrSfxVolume);
    fprintf(f, "; Your name in multiplayer, up to 15 characters.\n");
    fprintf(f, "PlayerName=%s\n", VrPlayerName);
    fprintf(f, "; The multiplayer page's last choices. MpStage is the level id; the sets,\n");
    fprintf(f, "; scenario, length and health are the launcher's list positions; the guns are\n");
    fprintf(f, "; item ids; the favorites are bitmasks over the stage and weapon-set lists.\n");
    fprintf(f, "MpStage=%d\nMpWeaponSet=%d\nMpChr=%d\nMpVisibility=%d\n", VrMpStage, VrMpWeaponSet, VrMpChr, VrMpVisibility);
    fprintf(f, "MpScenario=%d\nMpLength=%d\nMpHealth=%d\nMpDual=%d\nMpLoadouts=%d\nMpNextRound=%d\n",
            VrMpScenario, VrMpLength, VrMpHealth, VrMpDual, VrMpLoadouts, VrMpNextRound);
    fprintf(f, "MpVoiceMode=%d\n", VrMpVoiceMode);
    fprintf(f, "MpFriendlyFire=%d\n", VrMpFriendlyFire);
    fprintf(f, "HostEqualization=%d\nHostLatencyCapMs=%d\n", VrHostEqualization, VrHostLatencyCapMs);
    fprintf(f, "MpFunFlags=%d\nMpGunSize=%d\nMpMaxPlayers=%d\n", VrMpFunFlags, VrMpGunSize, VrMpMaxPlayers);
    fprintf(f, "; 1 = other players hold the detailed first-person gun models, 0 = the game's own.\n");
    fprintf(f, "MpDetailedGuns=%d\n", VrMpDetailedGuns ? 1 : 0);
    for (int i = 0; i < 4; i++) fprintf(f, "MpCustom%d=%d\n", i + 1, VrMpCustom[i]);
    for (int i = 0; i < 4; i++) fprintf(f, "MpLoadout%d=%d\n", i + 1, VrMpLoadout[i]);
    fprintf(f, "MpFavStages=%u\nMpFavSets=%u\n", VrMpFavStages, VrMpFavSets);
    fprintf(f, "; Co-op: MpMode 1, the mission's level id and the difficulty (0 Agent .. 3 007).\n");
    fprintf(f, "MpMode=%d\nMpMission=%d\nMpDifficulty=%d\n", VrMpMode, VrMpMission, VrMpDifficulty);
    fprintf(f, "; The virtual screen: metres in front of you, and the degrees of view it spans.\n");
    fprintf(f, "; Hold both grips and use the right stick while the screen is up to change them.\n");
    fprintf(f, "ScreenDistance=%.2f\n", VrScreenDistance);
    fprintf(f, "ScreenFov=%.1f\n", VrScreenFov);
    fprintf(f, "; 1 = a curved screen (a section of a cylinder around you), 0 = flat.\n");
    fprintf(f, "ScreenCurved=%d\n", VrScreenCurved);
    fprintf(f, "; 1 = passthrough background behind the 2D screen, 0 = black void.\n");
    fprintf(f, "ScreenPassthrough=%d\n", VrScreenPassthrough);
    fprintf(f, "; Metres above (+) or below (-) eye level; set by grabbing the screen with both grips.\n");
    fprintf(f, "ScreenHeight=%.2f\n", VrScreenHeight);
    fprintf(f, "; Preferred display refresh rate in Hz. 0 = Auto (no app preference), the\n");
    fprintf(f, "; Quest default for new settings. The launcher offers this headset's supported\n");
    fprintf(f, "; rates. Saved choices are retained; the runtime decides the actual rate.\n");
    fprintf(f, "DisplayHz=%d\n", VrRefreshRate);
    fprintf(f, "; Stereo: darken the edges of the view while moving or smooth-turning, to\n");
    fprintf(f, "; ease motion sickness. 0 = off, up to 1 = strongest.\n");
    fprintf(f, "ComfortVignette=%.2f\n", VrComfortVignette);
    fprintf(f, "; Stereo, when you're hit: 1 = the hit doesn't push you, 1 = the trigger still\n");
    fprintf(f, "; fires while the hit shows (no hitstun), 0 = no red flash.\n");
    fprintf(f, "NoKnockback=%d\nNoHitstun=%d\nDamageFlash=%d\n", VrNoKnockback ? 1 : 0, VrNoHitstun ? 1 : 0,
            VrDamageFlash ? 1 : 0);

    // --- VR hand placement (no menu UI; edit here) --------------------------------------------
    fprintf(f, "\n");
    fprintf(f, "; Where the gun sits in your hand, in the CONTROLLER's own frame (cm). Set them\n");
    fprintf(f, "; in game with the launcher's Gun fit..., or here. X = right, Y = up, Z = back\n");
    fprintf(f, "; toward you (negative is forward). All three 0 means the fitted defaults.\n");
    fprintf(f, "GunOffX=%.4f\n", VrGunOffX);
    fprintf(f, "GunOffY=%.4f\n", VrGunOffY);
    fprintf(f, "GunOffZ=%.4f\n", VrGunOffZ);
    fprintf(f, "; The holding hand of a two-handed hold (issue #35), set with Gun fit while\n");
    fprintf(f, "; holding a gun with both hands: cm outward, up, forward, then degrees of turn\n");
    fprintf(f, "; about the hand's X, Y, Z. GripPistol for handguns, GripRifle for long guns.\n");
    fprintf(f, "GripPistol=%.2f %.2f %.2f %.1f %.1f %.1f\n", VrGripTrim[0][0], VrGripTrim[0][1],
            VrGripTrim[0][2], VrGripTrim[0][3], VrGripTrim[0][4], VrGripTrim[0][5]);
    fprintf(f, "GripRifle=%.2f %.2f %.2f %.1f %.1f %.1f\n", VrGripTrim[1][0], VrGripTrim[1][1],
            VrGripTrim[1][2], VrGripTrim[1][3], VrGripTrim[1][4], VrGripTrim[1][5]);
    fprintf(f, "\n");
    fprintf(f, "; 0..1. How tightly the elbows are pulled in toward your body. 0 leaves them at the\n");
    fprintf(f, "; animation's rest pose (they splay outward), 1 pins them hard against the torso.\n");
    fprintf(f, "ArmElbowTuck=%.4f\n", VrArmElbowTuck);
    fprintf(f, "\n");
    fprintf(f, "; How fast the virtual torso turns to follow your head, per tick. The elbow anchor\n");
    fprintf(f, "; is held steady relative to that torso, so this trades two things off: too LOW and\n");
    fprintf(f, "; the elbows lag behind when you physically turn your whole body; too HIGH and they\n");
    fprintf(f, "; drift when you merely glance around. ~0.02 suits most people.\n");
    fprintf(f, "ArmBodyFollow=%.4f\n", VrArmBodyFollow);
    fprintf(f, "\n");
    fprintf(f, "; 1 = the empty off-hand closes into a fist while you squeeze the left grip,\n");
    fprintf(f, "; 0 = it stays open. Single-handed weapons only, since on two-handers that grip\n");
    fprintf(f, "; already means 'take the two-handed hold'.\n");
    fprintf(f, "FistClench=%d\n", VrFistClench);
    vrHapticsSaveIni(f);
    fclose(f);
    vrHapticsDumpCTable();
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

extern "C" void vrSettingsLoad(void)
{
    if (s_settingsLoaded) return;
    s_settingsLoaded = true;   // with no file yet, the defaults are the settings
    vrHapticsInit();
    FILE *f = fopen(VR_INI_PATH, "r");
    if (!f) return;

    char line[128];
    char key[64];
    float fval;
    int ival;
    char sval[256];

    // The gun's trim (GunOffX/Y/Z): kept aside, see below.
    float gunOff[3] = { 0.0f, 0.0f, 0.0f };
    bool gunOffRead = false;
    while (fgets(line, sizeof(line), f)) {
        if (line[0] == '[' || line[0] == '\n' || line[0] == ';' || line[0] == '#') continue;
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
        if (strncmp(line, "GripPistol=", 11) == 0 || strncmp(line, "GripRifle=", 10) == 0) {
            const int cls = line[4] == 'P' ? 0 : 1;
            float t[6];
            if (sscanf(strchr(line, '=') + 1, "%f %f %f %f %f %f", &t[0], &t[1], &t[2], &t[3], &t[4], &t[5]) == 6) {
                for (int i = 0; i < 6; i++) VrGripTrim[cls][i] = t[i];
            }
            continue;
        }
        if (strncmp(line, "Cheats=", 7) == 0) {        // hex bitmask of CHEAT_IDS
            VrCheatMask = strtoull(line + 7, NULL, 16);
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
            else if (strcmp(key, "LaserDotForAll") == 0) VrlaserDotForALL = ival != 0;
            else if (strcmp(key, "SeatedMode") == 0) VrSeatedMode = ival != 0;
            else if (strcmp(key, "MotionThrowing") == 0) VrMotionThrowing = ival != 0;
            else if (strcmp(key, "WeaponRecoil") == 0) VrWeaponRecoil = (ival != 0);
            else if (strcmp(key, "StickClickToCrouch") == 0) VrStickClickToCrouch = (ival != 0);
            else if (strcmp(key, "AimNoLean") == 0) VrAimNoLean = (ival != 0);
            else if (strcmp(key, "NoKnockback") == 0) VrNoKnockback = (ival != 0);
            else if (strcmp(key, "NoHitstun") == 0) VrNoHitstun = (ival != 0);
            else if (strcmp(key, "DamageFlash") == 0) VrDamageFlash = (ival != 0);
            else if (strcmp(key, "PauseHub") == 0) VrPauseHub = (ival != 0);
            else if (strcmp(key, "TwoHandedAiming") == 0) VrTwoHandAim = (ival != 0);
            else if (strcmp(key, "LeftHandedMode") == 0) VrLeftHandedMode = (ival != 0);
            else if (strcmp(key, "WatchGesturePause") == 0) VrWatchGesturePause = (ival != 0);
            else if (strcmp(key, "SwapJoysticks") == 0) VrSwapJoysticks = (ival != 0);
            else if (strcmp(key, "AimSteadying") == 0) VrAimSteady = ival < 0 ? 0 : ival > 2 ? 2 : ival;
            else if (strcmp(key, "ShowStats") == 0) VrShowStats = (ival != 0);
            else if (strcmp(key, "GunSizeCheat") == 0) VrGunSizeCheat = ival < 0 ? 0 : ival > 2 ? 2 : ival;
            else if (strcmp(key, "UnlockAll") == 0) VrUnlockAll = ival != 0;
            else if (strcmp(key, "HideArms") == 0) VrHideArms = (ival != 0);
            else if (strcmp(key, "MatchCharacterHeight") == 0) VrMatchCharacterHeight = (ival != 0);
            else if (strcmp(key, "FistClench") == 0) VrFistClench = ival;
            else if (strcmp(key, "PlayMode") == 0) VrPlayMode = ival != 0 ? VR_PLAYMODE_STEREO : VR_PLAYMODE_SCREEN;
            else if (strcmp(key, "MicMuted") == 0) VrMicMuted = ival != 0;
            else if (strcmp(key, "MpStage") == 0) VrMpStage = ival;
            else if (strcmp(key, "MpWeaponSet") == 0) VrMpWeaponSet = ival;
            else if (strcmp(key, "MpChr") == 0) VrMpChr = ival;
            else if (strcmp(key, "MpVisibility") == 0) VrMpVisibility = ival != 0;
            else if (strcmp(key, "HostEqualization") == 0) VrHostEqualization = ival != 0;
            else if (strcmp(key, "HostLatencyCapMs") == 0) VrHostLatencyCapMs = ival < 0 ? 0 : ival > 80 ? 80 : ival;
            else if (strcmp(key, "MpFriendlyFire") == 0) VrMpFriendlyFire = ival != 0;
            else if (strcmp(key, "MpFunFlags") == 0) VrMpFunFlags = ival >= 0 && ival <= 7 ? ival : 0;
            else if (strcmp(key, "MpGunSize") == 0) VrMpGunSize = ival >= 0 && ival <= 2 ? ival : 0;
            else if (strcmp(key, "MpMaxPlayers") == 0) VrMpMaxPlayers = ival >= 2 && ival <= 8 ? ival : 4;
            else if (strcmp(key, "MpDetailedGuns") == 0) VrMpDetailedGuns = ival != 0;
            else if (strcmp(key, "MpVoiceMode") == 0) VrMpVoiceMode = ival == 1 ? 1 : 0;
            else if (strcmp(key, "MpScenario") == 0) VrMpScenario = ival;
            else if (strcmp(key, "MpLength") == 0) VrMpLength = ival;
            else if (strcmp(key, "MpHealth") == 0) VrMpHealth = ival;
            else if (strcmp(key, "MpDual") == 0) VrMpDual = ival;
            else if (strcmp(key, "MpLoadouts") == 0) VrMpLoadouts = ival != 0;
            else if (strcmp(key, "MpNextRound") == 0) VrMpNextRound = ival;
            else if (strcmp(key, "MpFavStages") == 0) VrMpFavStages = (unsigned)ival;
            else if (strcmp(key, "MpFavSets") == 0) VrMpFavSets = (unsigned)ival;
            else if (strcmp(key, "MpMode") == 0) VrMpMode = ival == 1 ? 1 : 0;
            else if (strcmp(key, "MpMission") == 0) VrMpMission = ival;
            else if (strcmp(key, "MpDifficulty") == 0) VrMpDifficulty = ival >= 0 && ival <= 3 ? ival : 0;
            else if (strncmp(key, "MpCustom", 8) == 0 && key[8] >= '1' && key[8] <= '4') VrMpCustom[key[8] - '1'] = ival;
            else if (strncmp(key, "MpLoadout", 9) == 0 && key[9] >= '1' && key[9] <= '4') VrMpLoadout[key[9] - '1'] = ival;
            else if (strcmp(key, "ScreenCurved") == 0) VrScreenCurved = ival != 0;
            else if (strcmp(key, "ScreenPassthrough") == 0) VrScreenPassthrough = ival != 0;
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
            else if (strcmp(key, "ArmBodyFollow") == 0) VrArmBodyFollow = fval;
            else if (strcmp(key, "GunOffX") == 0) { gunOff[0] = fval; gunOffRead = true; }
            else if (strcmp(key, "GunOffY") == 0) { gunOff[1] = fval; gunOffRead = true; }
            else if (strcmp(key, "GunOffZ") == 0) { gunOff[2] = fval; gunOffRead = true; }
            else if (strcmp(key, "ScreenDistance") == 0) {
                if (fval < VR_SCREEN_DISTANCE_MIN) fval = VR_SCREEN_DISTANCE_MIN;
                if (fval > VR_SCREEN_DISTANCE_MAX) fval = VR_SCREEN_DISTANCE_MAX;
                VrScreenDistance = fval;
            }
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

    // Tell the engine to use external textures.
    // Initialization will be safely handled by the game later via extTexInit().
    if (g_ActiveExtTexPack[0] != '\0') {
        extTexSetPack(g_ActiveExtTexPack);
        videoSetExternalTextures(true);
    }
}
