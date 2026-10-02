/**
 * Definitions for the VR settings and runtime flags.
 *
 * port/vr/vr_settings.h declares these and port/vr/vr_settings.cpp reads and
 * writes them through goldeneye-vr.ini, but nothing in this tree defined them: in the
 * Perfect Dark VR port they live in Perfect Dark's own game sources (the header
 * still says "Defined in bondgun.c"), and those files have no counterpart here.
 * Without definitions the library does not link, so they are defined once, here,
 * on the GoldenEye side.
 *
 * The values are the neutral ones - no comfort mode on, world scale at 1:1,
 * player height mid-range - so that behaviour is whatever goldeneye-vr.ini says and
 * nothing is silently enabled. Ranges are the ones documented in vr_settings.h.
 */

#include <stdbool.h>
#include <PR/ultratypes.h>

/* --- Comfort and control options ---------------------------------------- */

bool VrManualReloading    = false;  /* reload by gesture rather than automatically */
bool VrMotionThrowing     = true;   /* throw grenades, knives and mines by arm motion */
float VrMotionThrowPitch  = 0.0f;   /* vertical pitch offset in degrees for motion throws */
float VrMotionThrowGazeAssist = 0.50f; /* 0..1 gaze direction assist for overhand throws */
float VrMotionThrowStrength = 1.0f;   /* throw speed / strength multiplier (0.5 .. 2.0) */
bool VrSeatedMode         = false;  /* play seated; height is taken from the ini */
bool VrlaserDotForALL     = false;  /* laser dot on every weapon, not just the ones that have one */
bool VrTwoHandAim         = false;  /* two-handed weapons aim along the line between both controllers */
bool vr_grip_for_unarmed  = false;  /* the grip button still does something with no weapon held */

int  VrHideArms           = 0;      /* draw no arm models */
int  VrStickClickToCrouch = 0;      /* crouch on stick click instead of physically ducking */
int  VrAimNoLean          = 0;      /* aiming keeps the move stick moving: no lean, no duck (issue #81) */
int  VrMicMuted           = 0;      /* persist multiplayer microphone mute */
float VrMusicVolume       = 1.0f;   /* music volume (0..1) */
float VrVoiceVolume       = 1.0f;   /* multiplayer voice chat volume (0..1) */
float VrSfxVolume         = 1.0f;   /* multiplayer sound effects volume (0..1) */
char VrPlayerName[16]     = "";     /* multiplayer name; the launcher makes one up when empty */
/* The multiplayer page's choices (vr_settings.h): Bunker II, Power Weapons,
 * Bond, a public game, Normal, 10 minutes, normal health, no dual wielding,
 * no loadouts, votes; the Rockets set's guns as the custom set, a PP7, a
 * KF7, a shotgun and grenades as the loadout; no favorites. */
int VrMpStage = 27, VrMpWeaponSet = 4, VrMpChr = 0, VrMpVisibility = 0;
int VrMpVoiceMode = 0;
int VrMpFriendlyFire = 1;
int VrHostEqualization = 1, VrHostLatencyCapMs = 50;
int VrMpFunFlags = 0, VrMpGunSize = 0;
int VrMpScenario = 0, VrMpLength = 2, VrMpHealth = 5, VrMpDual = 0, VrMpLoadouts = 0, VrMpNextRound = 0;
int VrMpCustom[4] = { 6, 7, 8, 25 };    /* ITEM_TT33, ITEM_SKORPION, ITEM_AK47, ITEM_ROCKETLAUNCH */
int VrMpLoadout[4] = { 4, 8, 15, 26 };  /* ITEM_WPPK, ITEM_AK47, ITEM_SHOTGUN, ITEM_GRENADE */
unsigned VrMpFavStages = 0, VrMpFavSets = 0;
int  vr_invert_hands      = 0;      /* swap which hand holds the weapon */

/* 0 turns snap turning off and uses smooth turning; otherwise the snap angle. */
float VrUseSnapTurn = 0.0f;

/* GoldenEye: 1 = true stereo in first-person play, 0 = everything on the virtual
 * screen. See vr_settings.h. */
int VrPlayMode = 1;

/* GoldenEye: comfort vignette while moving in stereo, 0 = off .. 1. */
float VrComfortVignette = 0.0f;
int VrRefreshRate = 90;    /* Hz (launcher: 72, 90, 120), 0 = the runtime's default (vr_openxr.cpp vr_request_refresh_rate) */

/* --- Physical setup ------------------------------------------------------ */

/*
 * false: your own height carries into the game. true: you take the height of
 * the character being played. See vr_settings.h.
 */
bool VrMatchCharacterHeight = false;

/* Standing eye height in cm, PLAYERHEIGHT_MIN..PLAYERHEIGHT_MAX (130..200). */
float VrPlayerHeight = 170.0f;

/* WORLDSCALE_MIN..WORLDSCALE_MAX (0.50..1.50); 1.0 is life size. */
float VrSetWorldScale = 1.0f;

/* --- Hand and gun placement ---------------------------------------------- */

/*
 * vr_settings.h says these are "Defined in bondgun.c" - Perfect Dark's weapon
 * code. There is no GoldenEye counterpart yet, so they live here. There is
 * deliberately no menu UI for them; vrSettingsSave() writes them to goldeneye-vr.ini
 * with a comment each. The gun's trim is set in game with the launcher's
 * "Gun fit..." (port/src/input.c).
 *
 * The defaults were set with it in the headset (2026-09-26, user): the model's
 * hand sat about 12 cm behind the real one and its trigger missed the finger.
 * Z is back toward the player, so -12.35 is 12.35 cm forward. An ini holding
 * 0, 0, 0 - the old default, which every install wrote - keeps these
 * (vr_settings.cpp).
 */
float VrGunOffX = 2.74f;     /* grip fit trim in the controller frame, cm: right */
float VrGunOffY = 1.94f;     /* up */
float VrGunOffZ = -12.35f;   /* back */
/* Issue #35: the holding hand's trim for a two-handed hold, per class (0
 * handguns, 1 long guns): cm along the off hand's side, up and forward, then
 * degrees about the hand model's X, Y, Z (bondview2.c gevrStereoTwoHandMatrix).
 * Gun fit sets them while holding with both hands (port/src/input.c). The
 * defaults are the user's, set that way in the headset (2026-09-26). */
float VrGripTrim[2][6] = {
    { 0.21f, -5.14f, 3.36f, 22.0f, 0.0f, -11.3f },   /* handguns: under the gun hand */
    { 0.02f, -4.72f, 5.23f, 90.1f, 0.0f, 83.1f },    /* long guns: underhand below the fore-end */
};
/* Issue #60: the gun hand holding the watch for the watch laser, pinned to the
 * left wrist: cm along the arm toward the fingers, out of the watch face,
 * along the thumb, then degrees about the hand model's X, Y, Z
 * (bondview2.c gevrStereoWatchGripMatrix). Gun fit sets it while holding the
 * watch. The default grips the forearm's inner side, the grip along the arm
 * (the long guns' 90 degrees), clear of the face the beam leaves. */
float VrWatchGripTrim[6] = { 0.0f, -5.0f, 0.0f, 90.0f, 0.0f, 0.0f };
float VrArmElbowTuck  = 0.0f;  /* 0..1, how tightly the elbow is pinned to the body */
float VrArmBodyFollow = 0.0f;  /* how fast the smoothed torso yaw chases the head */
int   VrFistClench    = 0;     /* close the off hand while the left grip is squeezed */

/* --- Runtime state ------------------------------------------------------- */

bool vr_init_done      = false;  /* set once the VR subsystem has come up */
bool vr_leftHasWeapon  = false;  /* the off hand is currently holding the weapon */
int  weaponnum         = 0;      /* weapon currently held, as the VR code sees it */

/*
 * Perfect Dark's extended options menu tracks which player's settings are being
 * edited. vr_settings.cpp reads it; the menu itself (port/src/optionsmenu.c) is
 * Perfect Dark's and is excluded from the build, so this stays at player one.
 */
int g_ExtMenuPlayer = 0;

/*
 * Whether the weapon is held with both hands.
 *
 * Perfect Dark's port answers this from its own weapon ids. GoldenEye has a
 * different weapon set, so the mapping has to be written against GoldenEye's
 * ids rather than renamed - the same work the recoil table in vr_input.cpp is
 * waiting on. Until then no weapon is two-handed, which is the behaviour the
 * one-handed path already handles.
 */
bool VrTwoHandsGun(int weaponnum)
{
	(void)weaponnum;
	return false;
}

/*
 * Entry point for Perfect Dark's extended options menu, called from video.c.
 * That menu is built on Perfect Dark's menu system and is excluded from the
 * build; GoldenEye needs its own before there is anything to initialise.
 */
void optionsMenuInit(void)
{
}
