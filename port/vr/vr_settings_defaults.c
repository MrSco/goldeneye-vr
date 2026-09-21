/**
 * Definitions for the VR settings and runtime flags.
 *
 * port/vr/vr_settings.h declares these and port/vr/vr_settings.cpp reads and
 * writes them through pd-vr.ini, but nothing in this tree defined them: in the
 * Perfect Dark VR port they live in Perfect Dark's own game sources (the header
 * still says "Defined in bondgun.c"), and those files have no counterpart here.
 * Without definitions the library does not link, so they are defined once, here,
 * on the GoldenEye side.
 *
 * The values are the neutral ones - no comfort mode on, world scale at 1:1,
 * player height mid-range - so that behaviour is whatever pd-vr.ini says and
 * nothing is silently enabled. Ranges are the ones documented in vr_settings.h.
 */

#include <stdbool.h>
#include <PR/ultratypes.h>

/* --- Comfort and control options ---------------------------------------- */

bool VrManualReloading    = false;  /* reload by gesture rather than automatically */
bool VrMotionThrowing     = false;  /* throw grenades by arm motion */
bool VrSeatedMode         = false;  /* play seated; height is taken from the ini */
bool VrlaserDotForALL     = false;  /* laser dot on every weapon, not just the ones that have one */
bool VrTwoHandAim         = false;  /* two-handed weapons aim along the line between both controllers */
bool vr_grip_for_unarmed  = false;  /* the grip button still does something with no weapon held */

int  VrHideArms           = 0;      /* draw no arm models */
int  VrStickClickToCrouch = 0;      /* crouch on stick click instead of physically ducking */
int  vr_invert_hands      = 0;      /* swap which hand holds the weapon */

/* 0 turns snap turning off and uses smooth turning; otherwise the snap angle. */
float VrUseSnapTurn = 0.0f;

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
 * deliberately no menu UI for them; vrSettingsSave() writes them to pd-vr.ini
 * with a comment each. Zero means no trim, which is the unadjusted fit.
 */
float VrGunOffX = 0.0f;      /* grip fit trim in the controller frame, game units */
float VrGunOffY = 0.0f;
float VrGunOffZ = 0.0f;
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
