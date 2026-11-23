/* The settings table (port/src/gevr_settings.c) against stubbed globals:
 * every row's positions read back as set, Reset lands on the default, rows
 * that depend on another hide and block with it, online rows go to the host,
 * and a section's rows fit the in-game screen's two columns. */
#include "gevr_settings.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "settings_table_stubs.h"
#include "net/net_rules.h"

static void check(int ok, const char *message) { if (!ok) { fprintf(stderr, "FAIL: %s\n", message); exit(1); } }

static int find(const char *label)
{
    for (int id = 0; id < gevrSettingCount(); id++) {
        GevrSettingInfo info;
        if (gevrSettingInfo(id, &info) && !strcmp(info.label, label)) return id;
    }
    fprintf(stderr, "FAIL: no row \"%s\"\n", label);
    exit(1);
}

static void text(int id, char *buf) { gevrSettingText(id, -1, buf, 64); }

int main(void)
{
    char t[64];
    /* every row shown and open: flat screen, third person, the ally, the switches others need */
    VrRagdolls = VrDismemberment = VrThirdPerson = 1;
    VrPlayMode = 0;
    VrMotionThrowing = VrForcePull = VrBodySlots = VrAlly = 1;

    /* Every row: each position reads back as set, and Reset lands on the default. */
    for (int id = 0; id < gevrSettingCount(); id++) {
        GevrSettingInfo info;
        check(gevrSettingInfo(id, &info) && info.label && info.tooltip, "every row has a label and a tooltip");
        check(info.section >= 0 && info.section < GEVR_SEC_COUNT, "every row is in a section");
        if (gevrSettingBlocked(id)) continue;
        const int n = gevrSettingPositions(id);
        check(n >= 2, "every row has at least two positions");
        if (info.kind == GEVR_SET_TOGGLE) check(n == 2, "a toggle has two positions");
        check(info.launcherSection >= 0 && info.launcherSection < GEVR_LSEC_COUNT, "every row has a launcher place");
        const int was = gevrSettingIndex(id);
        for (int i = 0; i < n; i++) {
            gevrSettingSet(id, i);
            if (gevrSettingIndex(id) != i) {
                fprintf(stderr, "FAIL: %s position %d reads back %d\n", info.label, i, gevrSettingIndex(id));
                exit(1);
            }
            gevrSettingText(id, i, t, sizeof(t));
            check(t[0] != '\0', "every position has text");
        }
        const int def = gevrSettingDefault(id);
        if (def >= 0) {
            gevrSettingSet(id, def);
            check(gevrSettingIndex(id) == def, "Reset lands on the default");
        }
        gevrSettingSet(id, was);   /* as it was, so the rows after it stay open */
    }
    check(s_saves > 0, "setting a row saves goldeneye-vr.ini");

    /* Values as the launcher and the watch had them. */
    int id = find("Turn speed");
    check(gevrSettingDefault(id) == 5 && gevrSettingPositions(id) == 14, "turn speed: 45..240 by 15, default 120");
    gevrSettingSet(id, gevrSettingDefault(id));
    check(VrSmoothTurnSpeed == 120, "turn speed's Reset is 120");
    id = find("Throw gaze");
    gevrSettingSet(id, 3); text(id, t);
    check(!strcmp(t, "30%"), "throw gaze reads as a percentage");
    id = find("Vignette");
    gevrSettingSet(id, 0); text(id, t);
    check(!strcmp(t, "Off (default)"), "vignette's zero reads Off, its default");
    id = find("Turning");
    gevrSettingSet(id, 2); text(id, t);
    check(VrUseSnapTurn == 45 && !strcmp(t, "Snap 45"), "turning snaps by 45");
    VrScreenDistance = 3;
    id = find("Turn speed");
    VrUseSnapTurn = 45;
    check(gevrSettingBlocked(id) != NULL, "turn speed is smooth turning's");
    VrUseSnapTurn = 0;
    id = find("Screen size");
    gevrSettingSet(id, gevrSettingPositions(id) - 1);
    check(VrScreenFov == 110 && VrScreenDistance == 3, "screen size resizes the screen, keeping its distance");
    id = find("Refresh");
    check(gevrSettingPositions(id) == 4, "refresh: Auto and the headset's three rates");
    gevrSettingSet(id, 3); text(id, t);
    check(VrRefreshRate == 120 && s_applied && !strcmp(t, "120 Hz"), "refresh 120 applies the rate");
    id = find("SpaceWarp");
    VrSpaceWarp = -1; text(id, t);
    check(!strcmp(t, "On (auto)"), "SpaceWarp's default reads On (auto) at 120 Hz");
    id = find("Anti-aliasing");
    check(gevrSettingBlocked(id) && !strcmp(gevrSettingBlocked(id), "Not on this headset"), "anti-aliasing blocks where unsupported");
    /* 4x back on Quest, warned as "(slower)" (24a8d14a dropped it for its cost) */
    VrEyeMsaa = 4; text(id, t);
    check(!strcmp(t, "4x (slower)") && gevrSettingWarning(id) != NULL, "anti-aliasing 4x reads (slower) and warns on a headset");
    VrEyeMsaa = 2; text(id, t);
    check(!strcmp(t, "2x") && !gevrSettingWarning(id), "anti-aliasing 2x has no warning");
    VrEyeMsaa = 1;
    /* Resolution: Auto (this headset's, else the scale in use), then the scales, made as VR
     * starts; above 1x warns */
    id = find("Resolution");
    VrRenderScale = 0.0f; s_liveRenderScale = 1.0f; text(id, t);
    check(gevrSettingIndex(id) == 0 && !strcmp(t, "Auto (1x)") && !gevrSettingWarning(id),
          "resolution's Auto reads the scale in use on an unknown headset, no warning at 1x");
    check(gevrSettingDefault(id) == 0, "resolution's default is Auto");
    gevrSettingSet(id, 4); text(id, t);
    check(VrRenderScale == 1.5f && !strcmp(t, "1.5x (next start)"), "resolution 1.5x waits for the next start");
    check(gevrSettingWarning(id) != NULL, "resolution above 1x warns of the cost");
    s_liveRenderScale = 1.5f; text(id, t);
    check(!strcmp(t, "1.5x"), "resolution in use drops (next start)");
    gevrSettingSet(id, 1);
    check(VrRenderScale == 0.8f && !gevrSettingWarning(id), "resolution below 1x has no warning");
    gevrSettingSet(id, 0);
    check(VrRenderScale == 0.0f, "resolution back to Auto");
    /* each headset offers what its GPU holds: Quest 3 up to 1.5x, others 1.25x, PC VR 2x */
    check(gevrSettingPositions(id) == 5, "resolution on a Quest 3: Auto, then up to 1.5x");
    s_scaleMax = 1.25f;
    check(gevrSettingPositions(id) == 4, "resolution on a lesser standalone headset stops at 1.25x");
    VrRenderScale = 2.0f; text(id, t);
    check(gevrSettingIndex(id) == 3 && !strncmp(t, "1.25x", 5), "a saved 2x reads as the headset's most");
    s_scaleMax = 2.0f;
    check(gevrSettingPositions(id) == 7, "resolution on PC VR reaches 2x");
    s_scaleMax = 1.5f;
    /* at more than 90 Hz without SpaceWarp it says what holds it */
    VrRenderScale = 1.25f; VrRefreshRate = 120; VrSpaceWarp = 0;
    check(gevrSettingWarning(id) && strstr(gevrSettingWarning(id), "90 Hz"), "resolution at 120 Hz without SpaceWarp says 90 Hz or SpaceWarp");
    VrSpaceWarp = 1;
    check(gevrSettingWarning(id) && !strstr(gevrSettingWarning(id), "90 Hz"), "resolution with SpaceWarp warns only of the cost");
    VrSpaceWarp = 0; VrRefreshRate = 90;
    check(gevrSettingWarning(id) && !strstr(gevrSettingWarning(id), "90 Hz"), "resolution at 90 Hz warns only of the cost");
    VrRefreshRate = 0; g_gevrDisplayHz = 120.0f;
    check(gevrSettingWarning(id) && strstr(gevrSettingWarning(id), "90 Hz"), "Auto reads the display's rate");
    VrRefreshRate = 120; VrSpaceWarp = -1; g_gevrDisplayHz = 0.0f;
    VrRenderScale = 0.0f; s_liveRenderScale = 1.0f;
    /* each headset's defaults (Auto), measured on Quest 3 (2026-11-23): 90 Hz, 1.25x at
     * 90 Hz or less, 4x anti-aliasing "(slower)" only above 90 Hz without SpaceWarp */
    s_hmdClass = GEVR_HMD_QUEST3;
    VrRefreshRate = 0; VrSpaceWarp = -1;
    text(find("Refresh"), t);
    check(!strcmp(t, "Auto (90 Hz)") && gevrAutoRefreshHz() == 90, "Quest 3's Auto refresh is 90 Hz");
    text(id, t);
    check(!strcmp(t, "Auto (1.25x)") && !gevrSettingWarning(id), "Quest 3's Auto resolution is 1.25x at 90 Hz, no warning");
    VrRenderScale = 1.5f; VrRefreshRate = 0; g_gevrDisplayHz = 120.0f;
    check(gevrSettingWarning(id) && !strstr(gevrSettingWarning(id), "90 Hz"), "Quest 3 at Auto refresh: 1.5x warns only of the cost");
    VrRenderScale = 0.0f; g_gevrDisplayHz = 0.0f;
    VrRefreshRate = 120; VrSpaceWarp = 0; text(id, t);
    check(!strcmp(t, "Auto (1x)") && !gevrSettingWarning(id), "Quest 3's Auto resolution is 1x at 120 Hz without SpaceWarp");
    VrSpaceWarp = 1; text(id, t);
    check(!strcmp(t, "Auto (1.25x)"), "Quest 3's Auto resolution is 1.25x at 120 Hz with SpaceWarp");
    {
        const int aa = find("Anti-aliasing");
        VrEyeMsaa = 4; VrRefreshRate = 0; VrSpaceWarp = 0; VrRenderScale = 1.0f; text(aa, t);
        check(!strcmp(t, "4x") && !gevrSettingWarning(aa), "Quest 3: 4x anti-aliasing at 90 Hz and 1x is no slower");
        VrRenderScale = 0.0f; text(aa, t);
        check(!strcmp(t, "4x (slower)") && gevrSettingWarning(aa) && strstr(gevrSettingWarning(aa), "Resolution"),
              "Quest 3: 4x anti-aliasing with Auto's 1.25x is near the limit");
        VrRenderScale = 1.0f; VrRefreshRate = 120; text(aa, t);
        check(!strcmp(t, "4x (slower)") && gevrSettingWarning(aa) && strstr(gevrSettingWarning(aa), "90 Hz"),
              "Quest 3: 4x anti-aliasing at 120 Hz says 90 Hz or SpaceWarp");
        VrEyeMsaa = 1; VrRenderScale = 0.0f;
    }
    /* Quest 2 and Pro: 72 Hz and the scale in use (not measured: scaled from Quest 3's) */
    s_hmdClass = GEVR_HMD_QUEST2; VrRefreshRate = 0;
    text(find("Refresh"), t);
    check(!strcmp(t, "Auto (72 Hz)"), "Quest 2's Auto refresh is 72 Hz");
    text(id, t);
    check(!strcmp(t, "Auto (1x)"), "Quest 2's Auto resolution is the scale in use");
    s_hmdClass = GEVR_HMD_OTHER; VrRefreshRate = 120; VrSpaceWarp = -1;
    /* Anisotropic filtering: Auto (this headset's), then Off to 16x, live */
    id = find("Anisotropic filtering");
    text(id, t);
    check(gevrSettingIndex(id) == 0 && !strcmp(t, "Auto (2x)") && !gevrSettingWarning(id),
          "anisotropic filtering is Auto by default, 2x on an unknown headset, no warning");
    check(gevrSettingDefault(id) == 0, "anisotropic filtering's default is Auto");
    gevrSettingSet(id, 5); text(id, t);
    check(VrTextureAniso == 16 && !strcmp(t, "16x (slower)"), "anisotropic filtering reaches 16x");
    check(gevrSettingWarning(id) != NULL, "anisotropic filtering above Auto warns on an unknown headset");
    s_hmdClass = GEVR_HMD_QUEST3; text(id, t);
    check(!strcmp(t, "16x") && !gevrSettingWarning(id), "Quest 3: 16x is its Auto, no warning");
    gevrSettingSet(id, 0); text(id, t);
    check(VrTextureAniso == 0 && !strcmp(t, "Auto (16x)") && gevrAutoAniso() == 16, "Quest 3's Auto anisotropic filtering is 16x");
    s_hmdClass = GEVR_HMD_QUEST2; text(id, t);
    check(!strcmp(t, "Auto (4x)"), "Quest 2's Auto anisotropic filtering is 4x");
    s_hmdClass = GEVR_HMD_OTHER;
    gevrSettingSet(id, 1);
    check(VrTextureAniso == 1 && !gevrSettingWarning(id), "anisotropic filtering off");
    VrTextureAniso = 0;
    /* Texture filtering: the N64 pictures, live, saved to the game config */
    id = find("Texture filtering");
    text(id, t);
    check(gevrSettingIndex(id) == 1 && !strcmp(t, "Smooth (default)") && !gevrSettingWarning(id),
          "texture filtering is Smooth by default, no warning");
    gevrSettingSet(id, 0); text(id, t);
    check(s_texFilter == 0 && !strcmp(t, "Pixelated") && !gevrSettingWarning(id) && s_configSaves > 0,
          "pixelated is the game's FILTER_NONE, saved to the game config");
    gevrSettingSet(id, 2); text(id, t);
    check(s_texFilter == 2 && !strcmp(t, "N64 accurate (slower)") && gevrSettingWarning(id) != NULL,
          "N64 accurate is the three-point filter and warns on a headset");
    gevrSettingSet(id, 1);
    check(s_texFilter == 1 && gevrSettingDefault(id) == 1, "texture filtering back to Smooth, its default");
    id = find("Movement speed");
    gevrSettingSet(id, 2); text(id, t);
    check(!strcmp(t, "100% (default)") && VrMovementSpeed == 0, "movement speed 100% is the normal mode");
    id = find("Frame rate");
    check(gevrSettingDefault(id) == 0 && gevrSettingPositions(id) == 10,
          "frame rate: Auto, standard caps (30..240), and Uncapped");
    gevrSettingSet(id, 0); text(id, t);
    check(VrFrameRate == 0 && !strcmp(t, "Auto (default)"), "frame rate 0 is Auto");
    gevrSettingSet(id, 2); text(id, t);
    check(VrFrameRate == 60 && !strcmp(t, "60 fps"), "frame rate cap 60");
    gevrSettingSet(id, 5); text(id, t);
    check(VrFrameRate == 144 && !strcmp(t, "144 fps"), "frame rate cap 144");
    gevrSettingSet(id, gevrSettingPositions(id) - 1); text(id, t);
    check(VrFrameRate == -1 && !strcmp(t, "Uncapped"), "frame rate last choice is Uncapped");
    gevrSettingSet(id, 0);

    /* The launcher's places: Play holds the play mode, third person, stats, the speedrun timer, the ghost race and the volumes;
     * the watch face is on Hands; the ally has its own sub-tab; gun size is a cheat. */
    {
        static const char *const play[] = { "Play mode", "Third person", "  Distance", "  Height", "  Shoulder", "Show stats",
                                            "Speedrun timer", "Ghost race", "Music", "Sound effects" };
        int ids[48], n = gevrSettingSectionRows(GEVR_LSEC_PLAY, GEVR_SET_LAUNCHER, ids, 48);
        check(n == 10, "Play: the play mode, third person and its camera, stats, the speedrun timer and ghost race, music and effects");
        for (int i = 0; i < n; i++) {
            GevrSettingInfo info;
            gevrSettingInfo(ids[i], &info);
            check(!strcmp(info.label, play[i]), "Play's rows in order");
        }
        GevrSettingInfo info;
        gevrSettingInfo(find("Watch face"), &info);
        check(info.launcherSection == GEVR_LSEC_HANDS && info.section == GEVR_SEC_DISPLAY, "watch face: Hands, in game Display");
        gevrSettingInfo(find("Gun size"), &info);
        check(info.launcherSection == GEVR_LSEC_CHEATS, "gun size is with the cheats");
        check(gevrSettingSectionRows(GEVR_LSEC_ALLY, GEVR_SET_LAUNCHER, ids, 48) == 6, "the ally's six rows");
        /* (an LSEC past the in-game sections: Cheats was, until Graphics made nine) */
        check(GEVR_LSEC_MP_FUN >= GEVR_SEC_COUNT && gevrSettingSectionRows(GEVR_LSEC_MP_FUN, GEVR_SET_INGAME, ids, 48) == 0,
              "an LSEC asked of the in-game screen is its own section number");
        n = gevrSettingSectionRows(GEVR_SEC_AUDIO, GEVR_SET_INGAME, ids, 48);
        check(n == 2 && ids[0] == find("Music") && ids[1] == find("Sound effects"), "in game, Audio: the Play tab's music and effects (solo too)");
        n = gevrSettingSectionRows(GEVR_SEC_RULES, GEVR_SET_INGAME, ids, 48);
        for (int i = 0; i < n; i++) {
            gevrSettingInfo(ids[i], &info);
            check(info.launcherSection != GEVR_LSEC_ALLY, "the ally is the launcher's only");
        }
        check(gevrSettingSectionRows(GEVR_LSEC_MODS, GEVR_SET_LAUNCHER, ids, 48) == 0, "the launcher's Mods draws its own rows, with their packs");
        /* Context visibility table: each context shows only what applies */
        gevrSettingsSetContextOverride(GEVR_CTX_PC_FLAT);
        check(gevrSettingShown(find("Frame rate")) && gevrSettingShown(find("Fullscreen")) &&
              !gevrSettingShown(find("Turning")) && !gevrSettingShown(find("Gyro aim")),
              "PC flat shows frame rate and fullscreen, hides VR turning and phone gyro");
        gevrSettingsSetContextOverride(GEVR_CTX_MOBILE);
        check(gevrSettingShown(find("Frame rate")) && gevrSettingShown(find("Gyro aim")) &&
              !gevrSettingShown(find("Fullscreen")) && !gevrSettingShown(find("SpaceWarp")),
              "Mobile shows frame rate and gyro, hides PC fullscreen and VR SpaceWarp");
        gevrSettingsSetContextOverride(GEVR_CTX_APPLE_TV);
        check(gevrSettingShown(find("Pad vibration")) && !gevrSettingShown(find("Gyro aim")) && !gevrSettingShown(find("Fullscreen")),
              "Apple TV shows pad rows, hides touch/gyro and PC fullscreen");
        gevrSettingsSetContextOverride(GEVR_CTX_QUEST);
        check(gevrSettingShown(find("Turning")) && gevrSettingShown(find("SpaceWarp")) &&
              !gevrSettingShown(find("Frame rate")) && !gevrSettingShown(find("Fullscreen")),
              "Quest VR shows VR turning and SpaceWarp, hides flat frame rate and PC fullscreen");
        gevrSettingsSetContextOverride(0);
    }
    VrPlayMode = 1;
    check(gevrSettingBlocked(find("Third person")) != NULL && !gevrSettingShown(find("  Distance")),
          "third person is the flat screen's");
    VrPlayMode = 0;

    /* The ally: GoldenEye, co-op or Horde, the host's; its parts need it on. */
    VrAlly = 0;
    check(gevrSettingBlocked(find("Weapon")) != NULL, "the ally's weapon needs the ally on");
    VrAlly = 1; VrAllyInvincible = 1;
    check(gevrSettingBlocked(find("Health")) != NULL && !gevrSettingBlocked(find("Damage")), "an invincible ally has no health to set");
    VrAllyInvincible = 0;
    VrCampaign = 1;
    check(gevrSettingBlocked(find("AI ally")) != NULL, "the ally is GoldenEye's");
    VrCampaign = 0;
    s_net = 1; s_host = 1; s_coop = 0; s_horde = 0;
    check(gevrSettingBlocked(find("AI ally")) != NULL, "no ally in a deathmatch");
    s_horde = 1;
    check(!gevrSettingBlocked(find("AI ally")), "the Horde host picks the ally");
    s_host = 0;
    check(gevrSettingBlocked(find("AI ally")) != NULL, "a joiner can't");
    s_net = s_host = s_horde = 0;

    /* Hidden and blocked with the row they depend on. */
    int ids[48], before = gevrSettingSectionRows(GEVR_SEC_DISPLAY, GEVR_SET_INGAME, ids, 48);
    VrThirdPerson = 0;
    check(gevrSettingSectionRows(GEVR_SEC_DISPLAY, GEVR_SET_INGAME, ids, 48) == before - 3,
          "third person's three sliders hide while it is off");
    VrDismemberment = 0;
    check(!gevrSettingShown(find("  Chance")), "dismemberment's thresholds hide while it is off");
    VrRagdolls = 0;
    check(gevrSettingBlocked(find("Dismemberment")) != NULL, "dismemberment needs ragdolls");
    /* hand reload has magazines to take with every model set of GoldenEye's own art; a ROM hack's
     * own guns under GoldenEye's names have none (gevr_n64mag.h) */
    VrGexGuns = 0;
    check(gevrSettingBlocked(find("Hand reload")) == NULL, "hand reload works on GoldenEye's own guns");
    s_campaign = 1;   /* GEVR_CAMPAIGN_GF64 */
    check(gevrSettingBlocked(find("Hand reload")) != NULL, "hand reload is not for a hack's own guns");
    /* ... and GoldenEye X's guns are not for a hack either: its row cannot be switched on there */
    check(gevrSettingBlocked(find("GoldenEye X")) != NULL, "GoldenEye X's guns are blocked in a hack campaign");
    s_campaign = 0;
    check(gevrSettingBlocked(find("GoldenEye X")) == NULL, "GoldenEye X's guns are offered on GoldenEye's own ROM");
    /* hand reload holds grip use on: blocked, and shown on whatever the ini says */
    VrGexGuns = 1;
    VrManualReloading = 1;
    VrGestureGripUse = 0;
    check(gevrSettingBlocked(find("Grip use")) != NULL && gevrSettingIndex(find("Grip use")) == 1,
          "grip use shows on while hand reload needs it");
    VrManualReloading = 0;
    check(gevrSettingBlocked(find("Grip use")) == NULL && gevrSettingIndex(find("Grip use")) == 0,
          "grip use is its own again without hand reload");
    VrGexGuns = 0;
    VrBodySlots = 0;
    check(gevrSettingBlocked(find("Show slots")) != NULL && gevrSettingBlocked(find("Slot size")) != NULL,
          "the slots' size and display need body slots");
    VrForcePull = 0;
    check(gevrSettingBlocked(find("Pull disarm")) != NULL, "disarm needs force pull");
    VrMotionThrowing = 0;
    check(gevrSettingBlocked(find("Throw power")) != NULL, "throw power needs motion throw");

    /* Online: movement speed and fast reinforcements are the host's. */
    s_net = 1; s_host = 0; s_coop = 1; s_netSpeed = 4;
    id = find("Movement speed"); text(id, t);
    check(!strcmp(t, "Host: 150%"), "a joiner sees the host's movement speed");
    check(gevrSettingBlocked(id) && !strcmp(gevrSettingBlocked(id), "Host's choice"), "a joiner can't change it");
    gevrSettingSet(id, 0);
    check(s_netSpeed == 4 && VrMovementSpeed == 0, "a joiner's set changes nothing");
    s_host = 1;
    gevrSettingSet(id, 6);
    check(s_netSpeed == 6 && VrMovementSpeed == 0, "the host's set changes the match's rule, not the solo preference");
    id = find("Fast reinforcements");
    gevrSettingSet(id, 1); text(id, t);
    check(s_netFast == 1 && VrFastReinforcements == 0 && !strcmp(t, "Host: on"), "the co-op host sets fast reinforcements for the match");
    s_coop = 0;
    check(gevrSettingBlocked(id) && !strcmp(gevrSettingBlocked(id), "Co-op only"), "fast reinforcements is co-op only online");
    s_net = 0;

    /* Every section has rows, none more than two columns of seven. */
    VrThirdPerson = VrRagdolls = VrDismemberment = 1;
    for (int s = 0; s < gevrSettingSectionCount(); s++) {
        const int n = gevrSettingSectionRows(s, GEVR_SET_INGAME, ids, 48);
        check(n > 0 && n <= 14, "a section fills one or two columns of at most seven rows");
        check(gevrSettingSectionName(s) != NULL, "every section has a name");
    }
    /* Multiplayer's Fun: the host's preference offline; online the match's, the host's only */
    {
        int ids[48];
        check(gevrSettingSectionRows(GEVR_LSEC_MP_FUN, GEVR_SET_LAUNCHER, ids, 48) == 6, "Fun: four switches, hit immunity, gun size");
        check(gevrSettingSectionRows(GEVR_LSEC_MP_AUDIO, GEVR_SET_LAUNCHER, ids, 48) == 5, "Audio: the mic, three volumes, the voice mode (music and effects on Play too)");
        int dk = -1, immunity = -1, gun = -1, music = -1, sfx = -1;
        for (int id = 0; id < gevrSettingCount(); id++) {
            GevrSettingInfo info;
            gevrSettingInfo(id, &info);
            if (info.launcherSection == GEVR_LSEC_MP_FUN && !strcmp(info.label, "DK mode")) dk = id;
            if (info.launcherSection == GEVR_LSEC_MP_FUN && !strcmp(info.label, "Hit immunity")) immunity = id;
            if (info.launcherSection == GEVR_LSEC_MP_FUN && !strcmp(info.label, "Gun size")) gun = id;
            if (!strcmp(info.label, "Music")) music = id;
            if (!strcmp(info.label, "Sound effects")) sfx = id;
        }
        s_net = 0; VrMpFunFlags = 0;
        gevrSettingSet(dk, 1);
        check(VrMpFunFlags == NET_FUN_DK, "offline, DK mode is the host's saved preference");
        gevrSettingSet(immunity, 0);
        check(netHitImmunity(VrMpFunFlags) == 0 && (VrMpFunFlags & NET_FUN_DK), "hit immunity keeps the other switches");
        s_net = 1; s_host = 0; s_cfg[CFG_FUN_FLAGS] = NET_FUN_PAINTBALL;
        check(gevrSettingIndex(dk) == 0 && gevrSettingBlocked(dk) != NULL, "a joiner sees the match's switches, and can't change them");
        gevrSettingSet(dk, 1);
        check(s_cfg[CFG_FUN_FLAGS] == NET_FUN_PAINTBALL, "a joiner's set changes nothing");
        s_host = 1;
        gevrSettingSet(dk, 1);
        check(s_cfg[CFG_FUN_FLAGS] == (NET_FUN_PAINTBALL | NET_FUN_DK), "the host's set goes to the match, live");
        gevrSettingSet(gun, 2);
        check(s_cfg[CFG_GUN_SIZE] == 2 && VrMpGunSize == 2, "the host's gun size goes to the match");
        s_net = s_host = 0;
        gevrSettingSet(music, 10);
        check(s_music == (unsigned short)((50 * 32767 + 50) / 100) && VrMusicVolume == 0.5f && gevrSettingIndex(music) == 10,
              "music at 50% sets the game's music volume");
        gevrSettingSet(sfx, 10);
        check(VrSfxVolume > 0.49f && VrSfxVolume < 0.51f && s_sfxApplied == (50 * 32767 + 50) / 100, "sound effects apply at once");
    }
    {
        /* #269 #10: Reset all settings puts every row back, in one save: a
         * slider to its Reset, a toggle or choice to its value before the ini */
        int before[256], moved = 0;
        const int rows = gevrSettingCount();
        check(rows <= 256, "rows fit the test's table");
        gevrSettingsReadBuiltIn();
        for (int id = 0; id < rows; id++) before[id] = gevrSettingIndex(id);
        for (int id = 0; id < rows; id++) {
            if (gevrSettingBlocked(id) || gevrSettingPositions(id) < 2) continue;
            const int def = gevrSettingDefault(id) >= 0 ? gevrSettingDefault(id) : before[id];
            gevrSettingSet(id, def == 0 ? 1 : 0);
            moved += gevrSettingIndex(id) != def;
        }
        check(moved > 30, "rows moved off their defaults, toggles too");
        const int saves = s_saves;
        check(gevrSettingResetAll() > 0, "the moved rows reset (rows sharing a value count once)");
        check(s_saves == saves + 1, "Reset all settings saves once");
        for (int id = 0; id < rows; id++) {
            if (gevrSettingBlocked(id)) continue;
            const int def = gevrSettingDefault(id) >= 0 ? gevrSettingDefault(id) : before[id];
            check(gevrSettingIndex(id) == def, "every row at its default");
        }
        check(gevrSettingResetAll() == 0, "a second reset changes nothing");
    }
    for (int s = 0; s < GEVR_LSEC_COUNT; s++)
        check(gevrSettingSectionRows(s, GEVR_SET_LAUNCHER, ids, 48) <= 16, "a launcher place fills at most two columns of eight");
    puts("PASS: settings table: every row round-trips, Reset, hidden and blocked rows, the host's online rules, "
         "the launcher's places and the ally, each section fits two columns, Reset all settings");
    return 0;
}
