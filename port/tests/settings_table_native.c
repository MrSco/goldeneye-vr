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
    id = find("Movement speed");
    gevrSettingSet(id, 2); text(id, t);
    check(!strcmp(t, "100% (default)") && VrMovementSpeed == 0, "movement speed 100% is the normal mode");

    /* The launcher's places: Play holds the play mode, third person and stats;
     * the watch face is on Hands; the ally has its own sub-tab; gun size is a cheat. */
    {
        static const char *const play[] = { "Play mode", "Third person", "  Distance", "  Height", "  Shoulder", "Show stats" };
        int ids[48], n = gevrSettingSectionRows(GEVR_LSEC_PLAY, GEVR_SET_LAUNCHER, ids, 48);
        check(n == 6, "Play: the play mode, third person and its camera, stats");
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
        check(gevrSettingSectionRows(GEVR_LSEC_ALLY, GEVR_SET_INGAME, ids, 48) == 0, "an LSEC asked of the in-game screen is its own section number");
        n = gevrSettingSectionRows(GEVR_SEC_RULES, GEVR_SET_INGAME, ids, 48);
        for (int i = 0; i < n; i++) {
            gevrSettingInfo(ids[i], &info);
            check(info.launcherSection != GEVR_LSEC_ALLY, "the ally is the launcher's only");
        }
        check(gevrSettingSectionRows(GEVR_LSEC_MODS, GEVR_SET_LAUNCHER, ids, 48) == 0, "the launcher's Mods draws its own rows, with their packs");
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
    VrGexGuns = 0;
    check(gevrSettingBlocked(find("Hand reload WIP")) != NULL, "hand reload needs GoldenEye X");
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
        check(gevrSettingSectionRows(GEVR_LSEC_MP_AUDIO, GEVR_SET_LAUNCHER, ids, 48) == 5, "Audio: the mic, three volumes, the voice mode");
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
    for (int s = 0; s < GEVR_LSEC_COUNT; s++)
        check(gevrSettingSectionRows(s, GEVR_SET_LAUNCHER, ids, 48) <= 14, "a launcher place fills at most two columns of seven");
    puts("PASS: settings table: every row round-trips, Reset, hidden and blocked rows, the host's online rules, "
         "the launcher's places and the ally, each section fits two columns");
    return 0;
}
