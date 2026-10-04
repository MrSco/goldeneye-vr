
#define VR_INI_PATH "goldeneye-vr.ini"
#include "gevr_watch_status.h"
extern int VrManualReloading;
extern bool VrlaserDotForALL;
extern bool inputRumbleSupported(int playernum);
extern int g_ExtMenuPlayer;
extern float XrFov;
extern float VrStereoCrosshair;
#define HUD_STEREO_DEPTH_MIN 0.0f
#define HUD_STEREO_DEPTH_MAX 4.0f
extern bool VrSeatedMode;
extern int VrMotionThrowing;
extern float VrMotionThrowPitch;
extern float VrMotionThrowGazeAssist;
extern float VrMotionThrowStrength;
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
extern int VrMicMuted;
extern float VrMusicVolume;
extern float VrVoiceVolume;
extern float VrSfxVolume;
extern char VrPlayerName[16];  // multiplayer name, up to 15 characters (launcher "Your name")
// The multiplayer page's choices, kept between runs (the launcher restarts the app).
// Stage is a LEVELID; sets, scenario, length, health are net_match.c indices;
// the guns are ITEM_IDS values; the favorites are bitmasks over net_match.c's lists.
extern int VrMpStage, VrMpWeaponSet, VrMpChr, VrMpVisibility;
extern int VrMpVoiceMode;
extern int VrMpFriendlyFire;
extern int VrHostEqualization, VrHostLatencyCapMs;
extern int VrMpFunFlags, VrMpGunSize;
extern int VrMpMaxPlayers; // the host's player count, 2..8 on any stage
extern int VrDetailedGuns;   // guards and other players hold the first-person gun models (#95); 0 = the game's own
extern int VrGexGuns;        // GoldenEye X's first-person guns from the player's data/gex.z64 (experimental)
extern int VrMpScenario, VrMpLength, VrMpHealth, VrMpDual, VrMpLoadouts, VrMpNextRound;
extern int VrMpCustom[4];       // the host's custom set
// Co-op (NET_MODE_COOP): the mode, the mission's LEVELID and the solo difficulty (0..3).
extern int VrMpMode, VrMpMission, VrMpDifficulty;
extern int VrMpLoadout[4];      // this player's spawn guns
extern unsigned VrMpFavStages, VrMpFavSets;
// GoldenEye comfort vignette strength while moving in stereo, 0 = off .. 1.
extern float VrComfortVignette;
// Being hit in stereo (issue #95). VrNoKnockback: 1 = a hit doesn't push you
// (the default). VrNoHitstun: 1 = the trigger still fires while the hit
// shows. VrDamageFlash: 0 = no red flash.
extern int VrNoKnockback, VrNoHitstun, VrDamageFlash;
// Preferred display refresh rate in Hz; 0 = Auto (no app preference).
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
// Smooth turning, degrees per second (GEVR PC vr443's turn speed).
#define SMOOTHTURN_MIN  45
#define SMOOTHTURN_MAX  240
#define SMOOTHTURN_STEP 15
extern int VrSmoothTurnSpeed;
// GEVR PC's grip gestures, one toggle each (vr_settings_defaults.c).
extern int VrGestureHolster, VrGestureGripUse, VrGesturePickup, VrGestureMineGrab;
extern int VrPerWeaponRecoil;   // PD VR's per-weapon recoil table
extern int VrMinesStickToGuards; // thrown mines stick to guards (GEVR PC vr450.2)
extern int VrBodiesStay;        // bodies kept: 0 (original fade), 12, 24 or 48
extern bool VrTwoHandAim;       // two-handed weapons aim along the line between both controllers
extern int VrStickClickToCrouch;
extern int VrAimNoLean;         // aiming keeps the move stick moving: no lean, no duck (issue #81)
extern int VrLeftHandedMode;
extern int VrSwapJoysticks;
extern int VrAimSteady;         // gun-hand steadying: 0 off, 1 low, 2 high (issue #7)
extern int VrShowStats;         // troubleshooting readout in game
extern unsigned long long VrCheatMask; // launcher cheats: bit n = CHEAT_IDS n
extern int VrGunSizeCheat;      // VR fun cheat: 0 normal, 1 tiny, 2 big guns
extern int VrUnlockAll;         // every mission, 007 mode and every cheat unlocked (issue #54)
extern int VrGunFitArmed;       // launcher "Gun fit...": fit the gun in the next level (not saved)
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
extern float VrGripTrim[2][6];  // two-handed hold's hand trim: [handgun, long gun][dx dy dz rx ry rz]
extern float VrArmElbowTuck;    // 0..1, how tightly the elbow is pinned toward the body
extern float VrArmBodyFollow;   // how fast the smoothed torso yaw chases the head (spin comfort)
extern int   VrFistClench;      // close the off-hand while the left grip is squeezed
extern float VrFistClenchAmt;   // runtime 0..1 clench amount (not persisted)
