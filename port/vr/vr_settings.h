
#define VR_INI_PATH "goldeneye-vr.ini"
extern bool VrManualReloading;
extern bool VrlaserDotForALL;
extern bool inputRumbleSupported(int playernum);
extern int g_ExtMenuPlayer;
extern float XrFov;
extern float VrStereoCrosshair;
#define HUD_STEREO_DEPTH_MIN 0.0f
#define HUD_STEREO_DEPTH_MAX 4.0f
extern bool VrSeatedMode;
extern bool VrMotionThrowing;
extern bool VrWeaponRecoil;
#define WORLDSCALE_MIN    0.50f
#define WORLDSCALE_MAX    1.50f
extern float VrSetWorldScale;
extern int VrPauseHub;

// GoldenEye: how gameplay is shown. 0 = always on the virtual screen (flat),
// 1 = true stereo in first-person play; menus, cutscenes and the watch stay on
// the screen either way. Hold the right stick click to switch in game.
#define VR_PLAYMODE_SCREEN 0
#define VR_PLAYMODE_STEREO 1
extern int VrPlayMode;
// GoldenEye comfort vignette strength while moving in stereo, 0 = off .. 1.
extern float VrComfortVignette;
// Display refresh rate in Hz, 0 = the runtime's default (vr_openxr.cpp).
extern int VrRefreshRate;

// Your standing EYE height in cm -- where your eyes are off the floor, roughly
// 13 cm below the top of your head, not your stature. That is what the headset
// reports and what the game's own vv_eyeheight means. It is the reference
// physical crouching is measured against, and in character-height mode it maps
// your stand onto the character's stand.
#define PLAYERHEIGHT_MIN  130.0f
#define PLAYERHEIGHT_MAX  200.0f
extern float VrPlayerHeight;

// false: your real height carries into the game -- your eyes sit where your own
// eyes are whatever body you are wearing. true: you take the height of the
// character you are playing, so Elvis is short and Mr Blonde towers.
extern bool VrMatchCharacterHeight;
extern float VrUseSnapTurn;
extern bool VrTwoHandAim;       // two-handed weapons aim along the line between both controllers
extern int VrStickClickToCrouch;
extern int VrLeftHandedMode;
extern int VrSwapJoysticks;
extern int VrAimSteady;         // gun-hand steadying: 0 off, 1 low, 2 high (issue #7)
extern int VrShowStats;         // troubleshooting readout in game
extern unsigned long long VrCheatMask; // launcher cheats: bit n = CHEAT_IDS n
extern int VrGunSizeCheat;      // VR fun cheat: 0 normal, 1 tiny, 2 big guns
extern int VrUnlockAll;         // every mission, 007 mode and every cheat unlocked (issue #54)
extern int VrHideArms;

extern float VrHudDistance;
#define HUD_DISTANCE_MIN 0.30f
#define HUD_DISTANCE_MAX 2.0f

// VR hand/gun placement. Defined in bondgun.c. These are the only values still worth varying per
// player -- everything else about the placement is measured and baked. There is deliberately no
// menu UI for them; they live in goldeneye-vr.ini and vrSettingsSave() documents each one.
extern float VrGunOffX;         // grip fit trim in the controller frame, game units
extern float VrGunOffY;
extern float VrGunOffZ;
extern float VrArmElbowTuck;    // 0..1, how tightly the elbow is pinned toward the body
extern float VrArmBodyFollow;   // how fast the smoothed torso yaw chases the head (spin comfort)
extern int   VrFistClench;      // close the off-hand while the left grip is squeezed
extern float VrFistClenchAmt;   // runtime 0..1 clench amount (not persisted)
