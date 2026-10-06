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
#include "gevr_watch_status.h"
#include "vr_settings.h"

/* --- Comfort and control options ---------------------------------------- */

int  VrManualReloading    = 0;      /* reload by gesture rather than automatically (int: game code reads it) */
int  VrMotionThrowing     = 1;      /* throw grenades, knives and mines by arm motion (int: game code reads it) */
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
int VrMpMaxPlayers = 4;
int VrDetailedGuns = 1;     /* guards and other players hold the first-person gun models (gevr_heldgun.c) */
int VrGexGuns = 0;          /* GoldenEye X's first-person guns (gevr_gexmodel.c); off, as the original */
int VrGexArms = 1;          /* stereo: GoldenEye X's arms for every hand, the left with the watch (gun.c) */
int VrMpScenario = 0, VrMpLength = 2, VrMpHealth = 5, VrMpDual = 0, VrMpLoadouts = 0, VrMpNextRound = 0;
int VrMpCustom[4] = { 6, 7, 8, 25 };    /* ITEM_TT33, ITEM_SKORPION, ITEM_AK47, ITEM_ROCKETLAUNCH */
int VrMpLoadout[4] = { 4, 8, 15, 26 };  /* ITEM_WPPK, ITEM_AK47, ITEM_SHOTGUN, ITEM_GRENADE */
unsigned VrMpFavStages = 0, VrMpFavSets = 0;
int VrMpMode = 0, VrMpMission = 33, VrMpDifficulty = 0;   /* deathmatch; co-op: Dam, Agent */
int  vr_invert_hands      = 0;      /* swap which hand holds the weapon */

/* 0 turns snap turning off and uses smooth turning; otherwise the snap angle. */
float VrUseSnapTurn = 0.0f;
/* Smooth turning speed in degrees per second (GEVR PC vr443's turn speed;
 * 120 is PD VR's fixed VR_JOY_TURN_SPEED). */
int VrSmoothTurnSpeed = 120;

/* GEVR PC's grip gestures (CONTROLS.md, vr450..vr453), one toggle each. A
 * gesture claims a fresh grip press only with the hand in its zone; any other
 * press aims as before (port/src/input.c gevrGripArbiter). */
int VrGestureHolster  = 1;   /* grip at the hip holsters the gun, again draws it (vr451) */
int VrGestureGripUse  = 1;   /* grip with the hand at a door or switch uses it (vr450, #90) */
int VrGesturePickup   = 0;   /* Grip to hand: grip at a gun on the floor puts it in that hand (vr451); off by default (user) */
int VrGestureMineGrab = 1;   /* grip at your own stuck mine takes it back (vr450.2) */
/* PD VR's per-weapon recoil table instead of the one generic kick. */
int VrPerWeaponRecoil = 0;
/* Game rules GEVR PC changed; off keeps the original game's rule. */
int VrMinesStickToGuards = 0; /* thrown mines stick to guards (vr450.2) */
int VrBodiesStay = 0;         /* keep this many bodies (0, 12, 24 or 48) instead of fading them */
int VrFastReinforcements = 0; /* independent difficulty option; normal reinforcement timing by default */
int VrCoopFastReinforcements = 0; /* host-controlled co-op rule; clients follow the session, not their own preference */

/* GoldenEye: 1 = true stereo in first-person play, 0 = everything on the virtual
 * screen. See vr_settings.h. */
int VrPlayMode = 1;
int VrWatchFaceStatus = GEVR_WATCH_FACE_ON;
int VrWatchGesturePause = 1;

/* GoldenEye: comfort vignette while moving in stereo, 0 = off .. 1. */
float VrComfortVignette = 0.0f;
/* Being hit in stereo (issue #95): no push (on: motion you didn't make),
 * hitstun kept, the red flash kept. */
int VrNoKnockback = 1;
int VrNoHitstun = 0;
int VrDamageFlash = 1;
#ifdef ANDROID
int VrRefreshRate = 0;     /* Auto: no app preference; runtime / external profile manages the rate. */
#else
int VrRefreshRate = 90;    /* Preserve the desktop default. Saved DisplayHz wins on either platform. */
#endif

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
/* GoldenEye X's models (launcher MODS) sit differently in the hand, so Gun
 * fit keeps theirs apart (user). The gun's is the user's, fitted to the KF7
 * in the headset (2026-10-04); the grips start from GoldenEye's. */
float VrGexGunOff[3] = {3.580500f,1.361500f,-19.116900f};
float VrGexGripTrim[2][6] = {
    { 0.21f, -5.14f, 3.36f, 22.0f, 0.0f, -11.3f },
    { 2.45f, -3.24f, 7.60f, 92.9f, 0.0f, 83.1f },    /* the user's, on the GE-X KF7 (2026-10-04) */
};
/*
 * Hand reload's places, set with Gun fit's reload mode (user):
 *  - where the off hand takes a magazine, cm right, up and back from the
 *    gun hand's grip pose along its own axes, GoldenEye's guns [0] and
 *    GoldenEye X's [1] (8 ahead and 7 below was the guess for both);
 *  - the belt, cm below the eye, out to the hand's own side and ahead
 *    (the user's reaches, 2026-10-04: 45-75 below, 17-22 out, -10-0 ahead);
 *  - where GoldenEye X's off hand has its palm, empty or holding a
 *    magazine, cm right, up and back from the off hand's grip pose.
 * GoldenEye X's, the belt and the off hand are the user's, set in the
 * headset with Gun fit (2026-10-04).
 */
float VrReloadGrab[2][3] = { {0.0000f,-7.0000f,-8.0000f}, {-8.2100f,-11.7900f,-11.2800f} };
float VrReloadBelt[3] = { 68.34f, 20.61f, -13.15f };
float VrGexHeldMag[3] = {3.260000f,2.830000f,-5.930000f};
/* GoldenEye's watch on GE-X's left wrist (gun.c): cm ahead of the end of the
 * sleeve, up and out from its axis there, and its size (Gun fit's off hand
 * mode, holding the right grip). Over the wrist and a fifth larger, to go
 * round GE-X's sleeve (user; measured offline on the KF7). */
float VrGexWatch[4] = {5.180000f,1.380000f,0.240000f,0.940000f};
/* where GE-X's left hand holds a gun with both hands, cm forward, up and out
 * along the gun from where its animation has it (Gun fit's grip mode) */
float VrGexForeHold[3] = { 0.0f, 0.0f, 0.0f };
/* PP7 and PP7 silenced share a model fit, separate from legacy KF7 values.
 * Grab is a cm delta from the actual magazine, not a guessed controller point. */
float VrGexPp7Grab[3] = {-9.690000f,-9.140000f,3.940000f};
float VrGexPp7Support[3] = {1.540000f,-0.110000f,6.130000f};
float VrGexPp7SupportRot[3] = {-14.300000f,-36.700000f,8.800000f};
/* Fire-frame-zero palm aligned with the already fitted KF7 shooting palm. */
float VrGexPp7GunOff[3] = { 0.478802f, 4.372890f, -9.395315f };
/* Held-magazine fit moves the mesh independently of the off hand and watch. */
float VrGexKf7MagOff[3] = { 0.0f, 0.0f, 0.0f };
float VrGexPp7MagOff[3] = {1.230000f,5.270000f,1.690000f};
float VrGexKf7WellOff[3] = { 0.0f, 0.0f, 0.0f };
float VrGexPp7WellOff[3] = { 0.0f, 0.0f, 0.0f };

/* Gun fit's scope trims (port/include/gevr_scope.h): GoldenEye X's KF7 sight
 * as the user fitted it (2026-10-04), the others none */
float VrScopeFit[2][4][4] = {
    { { 0 } },
    { { 0 }, { 0 }, { -2.86f, 4.01f, 2.76f, 0.59f }, { 0 } },
};
float VrArmElbowTuck  = 0.0f;  /* 0..1, how tightly the elbow is pinned to the body */
float VrArmBodyFollow = 0.0f;  /* how fast the smoothed torso yaw chases the head */
int   VrFistClench    = 0;     /* close the off hand while the left grip is squeezed */
/*
 * Muzzle / barrel tip trim in cm, set with Gun fit's barrel tip mode (X):
 * cm right (side), up, forward along the gun from the default barrel tip.
 * [0] GoldenEye's guns, [1] GoldenEye X's. The KF7 default is the user's
 * headset calibration (2026-10-05). Saved INI fits override these defaults.
 */
float VrMuzzleTrim[2][GEVR_MAX_WEAPONS][3] = {
    [1] = { [8] = { 4.07f, 2.87f, -7.23f }, [5] = { 0.08f, 0.32f, -0.32f } }, /* GE-X ITEM_AK47 / KF7 */
};
int gevrMuzzleFitting = 0;

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

/* Model-specific fits; D5K silenced shares item 10. Missing INI keys keep these defaults. */
float VrGexWeaponFits[64][7][3] = {
    [6] = {{0.4788f,4.3729f,-9.3953f},{-8.8699f,-13.2398f,7.5154f},{1.5400f,-0.1100f,6.1300f},{-14.3000f,-36.7000f,8.8000f},{1.2300f,5.2700f,1.6900f},{0.0000f,0.0000f,0.0000f}},
    [7] = {{2.4745f,2.6738f,-8.4976f},{-9.0799f,-9.3980f,2.1436f},{1.5400f,-0.1100f,6.1300f},{-14.3000f,-36.7000f,8.8000f},{0.0000f,0.0000f,0.0000f},{0.0000f,0.0000f,0.0000f}},
    [8] = {{0.0000f,0.0000f,0.0000f},{0.0000f,0.0000f,0.0000f},{0.0000f,0.0000f,0.0000f},{0.0000f,0.0000f,0.0000f},{0.0000f,0.0000f,0.0000f},{0.0000f,0.0000f,0.0000f}},
    [9] = {{2.3327f,2.3827f,-8.4851f},{-8.3530f,-10.2353f,6.7583f},{1.5400f,-0.1100f,6.1300f},{-14.3000f,-36.7000f,8.8000f},{0.0000f,0.0000f,0.0000f},{0.0000f,0.0000f,0.0000f}},
    [10] = {{3.0861f,1.3847f,-19.4238f},{-8.9007f,-9.7985f,4.6520f},{0.0000f,0.0000f,0.0000f},{0.0000f,0.0000f,0.0000f},{0.0000f,0.0000f,0.0000f},{0.0000f,0.0000f,0.0000f}},
    [12] = {{3.6020f,1.2565f,-19.2300f},{-9.4191f,-10.0058f,6.3455f},{0.0000f,0.0000f,0.0000f},{0.0000f,0.0000f,0.0000f},{0.0000f,0.0000f,0.0000f},{-2.0765f,1.9985f,0.5825f}},
    [13] = {{3.6147f,1.3828f,-19.1331f},{-9.4418f,-9.0044f,6.4789f},{4.4486f,-0.1748f,-0.1474f},{0.0000f,0.0000f,0.0000f},{-5.6976f,4.9249f,6.2176f},{0.6024f,-0.1363f,0.6389f},{0.0000f,0.0000f,0.0000f}},
    [14] = {{2.426499f,-0.191122f,-17.002609f}},
    [17] = {{6.170935f,2.538541f,-15.521610f}},
    [22] = {{0.161102f,3.607290f,-8.564715f}},
    [15] = {{2.307075f,2.768407f,-20.009436f}},
    [16] = {{2.307075f,2.768407f,-20.009436f}},
    [25] = {{1.359098f,-0.305644f,-17.812867f},{0,0,0},{30,7,0}},
    [19] = {{0.161102f,3.607290f,-8.564715f}},
};
