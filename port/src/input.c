#ifdef GEVR
#include "net_game.h"
#include "net_coop.h"
#include "../vr/gevr_pause_menu.h"
#include "../vr/gevr_pause_input.h"
#include "gevr_reload_input.h"
#include "gevr_watch_status.h"
#include "gevr_scope.h"
#include "gevr_gexweapon.h"
#endif
#include <string.h>
#include <stddef.h>
#include <ctype.h>
#include <SDL.h>
#include <PR/ultratypes.h>
#include <PR/os.h>
#include "platform.h"
#include "input.h"
#include "video.h"
#include "config.h"
#include "utils.h"
#include "system.h"
#include "fs.h"

#include "../vr/vr_openxr.h"
#include "../vr/vr_input.h"
#include "../vr/vr_screen.h"
#include "../vr/vr_haptics.h"
#include <math.h>

#include <gun.h>
#include <player.h>
#include <options.h>
#include <boss.h>
#include <lv.h>
#include "net/net_core.h"

#ifdef ANDROID
#include <android/log.h>
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  "GoldenEye-VR", __VA_ARGS__)
#else
#define LOGI(...) printf(__VA_ARGS__)
#endif

extern void gevrPlayerLayout(u32 *size, u32 *pausestate); // bondview2.c

/* Crouch toggle for the left stick click; bondview2.c applies it. */
static s32 gevrCrouchToggle = 0;

/*
 * Stereo gameplay (bondview2.c gevrStereoFrame): the right stick turns the
 * body instead of reaching the game (Perfect Dark VR's joy_for_vr), and both
 * stick clicks recentre (GEVR PC's GETV_XR_RECENTER_CHORD).
 */
extern s32 g_gevrStereo;          /* bondview2.c: this frame is stereo */
extern s32 gevrScopeZoomStick(void);  /* bondview2.c: aiming the sniper, this stick zooms */
extern s32 gevrStereoWatchItem(s32 item);  /* bondview2.c: the watch laser, the detonator, the watch gadgets (#31) */
extern int gevrVrScreenMode;      /* gfx_pc.cpp: this frame is on the virtual screen */
extern int VrPlayMode;            /* vr_settings: 1 = stereo gameplay */
extern void vrSettingsSave(void);
extern int gevrVrWatchGesture(void); /* vr_input.cpp */
extern s32 g_gevrWatchGesturePending; /* bondview2.c */
extern s32 gevrStereoWatchGrip(void);    /* bondview2.c: the gun hand holds the watch (#31) */
extern s32 gevrStereoTwoHandGrip(void);  /* bondview2.c: the off hand holds the gun (#35) */
extern int VrGunFitArmed;                 /* vr_input.cpp: launcher "Gun fit..." */
extern float VrGunOffX, VrGunOffY, VrGunOffZ;   /* vr_settings_defaults.c: the gun's trim in the hand, cm */
extern s32 gevrGunFitAvailable(void);     /* bondview2.c: a gun in hand, in a level, in stereo */
extern s32 gevrGadgetFitItem(void);       /* bondview2.c: the gadget in the gun hand, or -1 */
extern void gevrGadgetFitBegin(void);
extern void gevrGadgetFitNudge(s32 item, f32 left, f32 up, f32 fwd, f32 rx, f32 ry, f32 rz, f32 dscale);
extern void gevrGadgetFitEnd(s32 keep);
int gevrGunFitActive;                     /* bondview2.c draws the readout while it is set */
extern float VrGripTrim[2][6];            /* vr_settings_defaults.c: the two-handed hold's hand (#35) */
extern float VrGexGunOff[3];              /* vr_settings_defaults.c: the two for GoldenEye X's models */
extern float VrGexGripTrim[2][6];
extern s32 gevrGexHeld(s32 hand);         /* gun.c: the hand's gun is a GoldenEye X model */
extern s32 gevrScopeFitIndex(void);       /* bondview2.c: the gun hand's scope in VrScopeFit, or -1 */
int gevrScopeFitting;                     /* Gun fit is moving the gun's scope (X), for bondview2.c */
int gevrReloadFitting;                    /* Gun fit is setting Hand reload's places (X), for bondview2.c */
int gevrOffHandFitting;                   /* Gun fit is moving GE-X's off hand (X), for bondview2.c */
int gevrHeldMagFitting;                   /* Gun fit moves the magazine independently of the hand */
int gevrInstalledMagFitting;                /* visible magazine in the gun, independently of reload targets */
int gevrWellFitting;                      /* Gun fit moves the magazine-well insertion target */
extern int gevrMuzzleFitting;                 /* Gun fit is moving barrel tip (X), for bondview2.c */
extern int gevrGunHandFitting;                /* Gun fit is moving GE-X's own gun hand on the gun (X) */
extern s32 gevrReloadFitAvailable(void);  /* bondview2.c */
extern void gevrReloadFitSetGrab(void);
extern void gevrReloadFitSetBelt(void);
extern s32 gevrGexMagState(s32 hand, f32 off[3]);
extern float VrReloadGrab[2][3], VrReloadBelt[3], VrGexHeldMag[3], VrGexWatch[4], VrGexForeHold[3];   /* vr_settings_defaults.c */
extern float VrMuzzleTrim[2][GEVR_MAX_WEAPONS][3];
extern float VrGexPp7Grab[3], VrGexPp7Support[3];
extern float VrGexPp7SupportRot[3];
extern float *gevrGexSupportFit(s32 item);
extern float VrGexPp7GunOff[3];
extern float *gevrGexGunFit(s32 item);
extern float VrGexKf7MagOff[3], VrGexPp7MagOff[3];
extern float *gevrGexHeldMagFit(s32 item);
extern float VrGexKf7WellOff[3], VrGexPp7WellOff[3];
extern float *gevrGexWellFit(s32 item);
extern void gevrReloadFitSetWell(void);
extern int VrGexGuns;                     /* vr_settings_defaults.c: GoldenEye X's models */
extern s32 gevrGexMineDetonates(void);    /* gun.c: GE-X's remote mines, detonated from the watch */
extern void gevrGexDetonateRequest(void);

/* Gun fit's values as last saved, which B goes back to: both models' sets */
static struct {
    float gun[3], gexGun[3], grip[2][6], gexGrip[2][6], scope[2][GEVR_SCOPE_FITS][4];
    float reloadGrab[2][3], reloadBelt[3], gexHeld[3], gexWatch[4], gexFore[3];
    float muzzle[2][GEVR_MAX_WEAPONS][3];
    float pp7Grab[3], pp7Support[3], pp7Gun[3];
    float kf7Mag[3], pp7Mag[3];
    float pp7SupportRot[3];
    float kf7Well[3], pp7Well[3];
    float weaponFits[64][10][3];   /* GEVR_GEX_FIT_COMPONENTS, checked below */
} s_gunFitSaved;
_Static_assert(sizeof(s_gunFitSaved.weaponFits) == sizeof(VrGexWeaponFits), "fit snapshot matches the fits");

static void gevrGunFitSaved(bool restore)
{
    if (restore) {
        memcpy(VrGexWeaponFits, s_gunFitSaved.weaponFits, sizeof(VrGexWeaponFits));
        VrGunOffX = s_gunFitSaved.gun[0];
        VrGunOffY = s_gunFitSaved.gun[1];
        VrGunOffZ = s_gunFitSaved.gun[2];
        memcpy(VrGexGunOff, s_gunFitSaved.gexGun, sizeof(VrGexGunOff));
        memcpy(VrGripTrim, s_gunFitSaved.grip, sizeof(VrGripTrim));
        memcpy(VrGexGripTrim, s_gunFitSaved.gexGrip, sizeof(VrGexGripTrim));
        memcpy(VrScopeFit, s_gunFitSaved.scope, sizeof(VrScopeFit));
        memcpy(VrReloadGrab, s_gunFitSaved.reloadGrab, sizeof(VrReloadGrab));
        memcpy(VrReloadBelt, s_gunFitSaved.reloadBelt, sizeof(VrReloadBelt));
        memcpy(VrGexHeldMag, s_gunFitSaved.gexHeld, sizeof(VrGexHeldMag));
        memcpy(VrGexWatch, s_gunFitSaved.gexWatch, sizeof(VrGexWatch));
        memcpy(VrGexForeHold, s_gunFitSaved.gexFore, sizeof(VrGexForeHold));
        memcpy(VrMuzzleTrim, s_gunFitSaved.muzzle, sizeof(VrMuzzleTrim));
        memcpy(VrGexPp7Grab, s_gunFitSaved.pp7Grab, sizeof(VrGexPp7Grab));
        memcpy(VrGexPp7Support, s_gunFitSaved.pp7Support, sizeof(VrGexPp7Support));
        memcpy(VrGexPp7GunOff, s_gunFitSaved.pp7Gun, sizeof(VrGexPp7GunOff));
        memcpy(VrGexKf7MagOff, s_gunFitSaved.kf7Mag, sizeof(VrGexKf7MagOff));
        memcpy(VrGexPp7MagOff, s_gunFitSaved.pp7Mag, sizeof(VrGexPp7MagOff));
        memcpy(VrGexPp7SupportRot, s_gunFitSaved.pp7SupportRot, sizeof(VrGexPp7SupportRot));
        memcpy(VrGexKf7WellOff, s_gunFitSaved.kf7Well, sizeof(VrGexKf7WellOff));
        memcpy(VrGexPp7WellOff, s_gunFitSaved.pp7Well, sizeof(VrGexPp7WellOff));
    } else {
        memcpy(s_gunFitSaved.weaponFits, VrGexWeaponFits, sizeof(VrGexWeaponFits));
        s_gunFitSaved.gun[0] = VrGunOffX;
        s_gunFitSaved.gun[1] = VrGunOffY;
        s_gunFitSaved.gun[2] = VrGunOffZ;
        memcpy(s_gunFitSaved.gexGun, VrGexGunOff, sizeof(VrGexGunOff));
        memcpy(s_gunFitSaved.grip, VrGripTrim, sizeof(VrGripTrim));
        memcpy(s_gunFitSaved.gexGrip, VrGexGripTrim, sizeof(VrGexGripTrim));
        memcpy(s_gunFitSaved.scope, VrScopeFit, sizeof(VrScopeFit));
        memcpy(s_gunFitSaved.reloadGrab, VrReloadGrab, sizeof(VrReloadGrab));
        memcpy(s_gunFitSaved.reloadBelt, VrReloadBelt, sizeof(VrReloadBelt));
        memcpy(s_gunFitSaved.gexHeld, VrGexHeldMag, sizeof(VrGexHeldMag));
        memcpy(s_gunFitSaved.gexWatch, VrGexWatch, sizeof(VrGexWatch));
        memcpy(s_gunFitSaved.gexFore, VrGexForeHold, sizeof(VrGexForeHold));
        memcpy(s_gunFitSaved.muzzle, VrMuzzleTrim, sizeof(VrMuzzleTrim));
        memcpy(s_gunFitSaved.pp7Grab, VrGexPp7Grab, sizeof(VrGexPp7Grab));
        memcpy(s_gunFitSaved.pp7Support, VrGexPp7Support, sizeof(VrGexPp7Support));
        memcpy(s_gunFitSaved.pp7Gun, VrGexPp7GunOff, sizeof(VrGexPp7GunOff));
        memcpy(s_gunFitSaved.kf7Mag, VrGexKf7MagOff, sizeof(VrGexKf7MagOff));
        memcpy(s_gunFitSaved.pp7Mag, VrGexPp7MagOff, sizeof(VrGexPp7MagOff));
        memcpy(s_gunFitSaved.pp7SupportRot, VrGexPp7SupportRot, sizeof(VrGexPp7SupportRot));
        memcpy(s_gunFitSaved.kf7Well, VrGexKf7WellOff, sizeof(VrGexKf7WellOff));
        memcpy(s_gunFitSaved.pp7Well, VrGexPp7WellOff, sizeof(VrGexPp7WellOff));
    }
}
extern s32 gevrStereoTwoHandClass(void);  /* bondview2.c: 0 handgun, 1 long gun */
static float gevrTurnAxis = 0.0f;
static s32 gevrRecenterPending = 0;
extern float gevrInjectTurn;   /* libultra.c: the PC input hook's turn */
float gevrVrTurnAxis(void) { return gevrInjectTurn != 0.0f ? gevrInjectTurn : gevrTurnAxis; }
s32 gevrVrTakeRecenter(void)
{
    s32 pending = gevrRecenterPending;
    gevrRecenterPending = 0;
    return pending;
}
s32 gevrCrouchToggled(void)
{
    return gevrCrouchToggle;
}
extern bool vr_leftHasWeapon;
int vr_button_R_grip = false;
int vr_button_L_grip = false;
extern int vr_invert_hands;
extern bool VrTwoHandsGun(s32 weaponnum);   // bondgun.c: weapon is held with both hands
int vr_right_gun_fire;
int vr_left_gun_fire;
extern bool vr_grip_for_unarmed;
extern int VrLeftHandedMode;
extern int VrSwapJoysticks;
extern int bossGetStageNum(void);
extern bool netIsActive(void);
extern void netVoiceToggleMuted(void);

#ifndef HAND_RIGHT
#define HAND_RIGHT 1
#define HAND_LEFT  0
#endif

#ifndef WEAPON_LASER
#define WEAPON_LASER ITEM_LASER
#endif

int gevrVrTriggerDown[2];   /* by gun hand (0 right, 1 left); gunfire.c gunTickGameplay */
static GevrReloadInput s_gevrReloadInput;
int gevrVrReloadPressedMask(void) { return s_gevrReloadInput.pending; }
int gevrVrReloadHeldMask(void) { return s_gevrReloadInput.held; }
int gevrVrTakeReloadMask(void) { return gevrReloadInputTake(&s_gevrReloadInput); }
int gevrReturnPrompt;       /* menu held: "back to the launcher?" is up (bondview2.c draws it) */
extern int gevrTexpackToggle(void);        /* gfx_pc.cpp: 1 on now, 0 off now, -1 no pack */
extern int gevrTexpackState(void);         /* gfx_pc.cpp: 1 on, 0 off, -1 no pack */
s32 gevrTexpackToggleMsg;                  /* bondview2.c says it in a level: 2 off, 3 on */
s32 gevrMicToggleMsg;                      /* bondview2.c says it in a level: 1 muted, 2 on */
static bool s_menuHeld;                    /* the Menu button is down (the microphone chord's other half) */
extern int VrLeftHandedMode;               /* vr_settings.h: the physical controllers swap roles */
extern int netVoiceIsMuted(void);
extern void netVoiceToggleMuted(void);
extern void mpwatchPlayBeep(void);         /* mpmenu.c */
static bool gevrSwallowX;                  /* X answered the prompt: no weapon change until let go */
static bool gevrSwallowA;                  /* A of Menu + A (gun fit): no weapon change until let go */
extern s32 gevrWeaponPanelOpen, gevrWeaponPanelRelease;   /* bondview2.c, issue #10 */
extern f32 gevrWeaponPanelStickX, gevrWeaponPanelStickY;
extern s32 gevrWeaponPanelStep;            /* bondview2.c: trigger steps through the wheel's category */
extern s32 gevrWeaponPanelLeft;            /* bondview2.c, issue #56: the left hand's panel */
extern s32 gevrLeftPanelAvailable(void);
extern void gevrCycleHandWeapon(s32 hand, s32 dir);
#define GEVR_WEAPON_PANEL_HOLD_MS 350
extern void gevrRestartToLauncher(void);   /* vr_launcher.cpp */
extern void gevrLobbySessionStopped(void); /* vr_launcher.cpp: leave the online game */
extern bool netIsActive(void);
extern s32 gevrDualWielding(void);
extern int VrMotionThrowing;
extern int VrPerWeaponRecoil;   /* launcher "Per-gun recoil" */
#include "gevr_recoil.h"
/* bondview2.c: GEVR PC's grip gestures (src/game/gevr_grip_gesture.h) */
extern void gevrGripGestureInput(int ctrl, int pressed, int held);
extern int gevrGripGestureTaken(int ctrl);
extern ITEM_IDS getCurrentPlayerWeaponId(GUNHAND hand);

s32 gevrIsThrowable(s32 item)
{
    switch (item)
    {
        case ITEM_THROWKNIFE:
        case ITEM_GRENADE:
        case ITEM_TIMEDMINE:
        case ITEM_PROXIMITYMINE:
        case ITEM_REMOTEMINE:
        case ITEM_PLASTIQUE:
        case ITEM_BOMBCASE:
        case ITEM_BUG:
        case ITEM_MICROCAMERA:
        case ITEM_GOLDENEYEKEY:
            return 1;
        default:
            return 0;
    }
}

static inline bool bgunIsFiring(s32 hand) {
    return get_button_state(hand, "trigger");
}

#if !SDL_VERSION_ATLEAST(2, 0, 14)
// this was added in 2.0.14
#define SDL_CONTROLLER_TYPE_VIRTUAL SDL_CONTROLLER_TYPE_UNKNOWN
#endif

#define CONTROLLERDB_FNAME "gamecontrollerdb.txt"

#define MAX_BIND_STR 256

#define TRIG_THRESHOLD (30 * 256)
#define DEFAULT_DEADZONE 4096
#define DEFAULT_DEADZONE_RY 6144

#define WHEEL_UP_MASK SDL_BUTTON(VK_MOUSE_WHEEL_UP - VK_MOUSE_BEGIN + 1)
#define WHEEL_DN_MASK SDL_BUTTON(VK_MOUSE_WHEEL_DN - VK_MOUSE_BEGIN + 1)

#define CURSOR_HIDE_THRESHOLD 1
#define CURSOR_HIDE_TIME 3000000 // us

static SDL_GameController *pads[INPUT_MAX_CONTROLLERS];

#define CONTROLLERCFG_DEFAULT { \
	.rumbleOn = 0, \
	.rumbleScale = 0.5f, \
	.axisMap = { \
		{ SDL_CONTROLLER_AXIS_LEFTX,  SDL_CONTROLLER_AXIS_LEFTY  }, \
		{ SDL_CONTROLLER_AXIS_RIGHTX, SDL_CONTROLLER_AXIS_RIGHTY }, \
	}, \
	.sens = { 1.f, 1.f, 1.f, 1.f }, \
	.deadzone = { DEFAULT_DEADZONE, DEFAULT_DEADZONE, DEFAULT_DEADZONE, DEFAULT_DEADZONE_RY }, \
	.stickCButtons = 0, \
	.swapSticks = 1, \
	.deviceIndex = -1, \
	.cancelCButtons = 0, \
}

static struct controllercfg {
    s32 rumbleOn;
    f32 rumbleScale;
    u32 axisMap[2][2];
    f32 sens[4];
    s32 deadzone[4];
    s32 stickCButtons;
    s32 swapSticks;
    s32 deviceIndex;
    s32 cancelCButtons;
} padsCfg[INPUT_MAX_CONTROLLERS] = {
        CONTROLLERCFG_DEFAULT,
        CONTROLLERCFG_DEFAULT,
        CONTROLLERCFG_DEFAULT,
        CONTROLLERCFG_DEFAULT
};

static u32 binds[MAXCONTROLLERS][CK_TOTAL_COUNT][INPUT_MAX_BINDS];
static char bindStrs[MAXCONTROLLERS][CK_TOTAL_COUNT][MAX_BIND_STR];

static s32 fakeControllers = 0;
static s32 firstController = 0;
static s32 connectedMask = 0;

static s32 numJoysticks = 0;

#ifdef ANDROID
static s32 useHIDAPI = 0; // Disable HIDAPI on Android due to receiver registration issues
#else
static s32 useHIDAPI = 1;
#endif
static s32 useRawInput = 1;

static s32 mouseEnabled = 0; // Disabled for VR
static s32 mouseX, mouseY;
static s32 mouseDX, mouseDY;
static u32 mouseButtons;
static s32 mouseWheel = 0;

static s32 mouseLocked = 0;
static s32 mouseLockMode = MLOCK_AUTO;
static u64 mouseCursorTime = 0;
static s32 mouseShowCursor = 1;

static f32 mouseSensX = 2.5f;
static f32 mouseSensY = 2.5f;

static s32 lastKey = 0;
static char lastChar = 0;
static s32 textInput = 0;

static char *clipboardText = NULL;

static const char *ckNames[CK_TOTAL_COUNT] = {
        "R_CBUTTONS",
        "L_CBUTTONS",
        "D_CBUTTONS",
        "U_CBUTTONS",
        "R_TRIG",
        "L_TRIG",
        "X_BUTTON",
        "Y_BUTTON",
        "R_JPAD",
        "L_JPAD",
        "D_JPAD",
        "U_JPAD",
        "START_BUTTON",
        "Z_TRIG",
        "B_BUTTON",
        "A_BUTTON",
        "STICK_XNEG",
        "STICK_XPOS",
        "STICK_YNEG",
        "STICK_YPOS",
        "ACCEPT_BUTTON",
        "CANCEL_BUTTON",
        "CK_0040",
        "CK_0080",
        "CK_0100",
        "CK_0200",
        "CK_0400",
        "CK_0800",
        "CK_1000",
        "CK_2000",
        "CK_4000",
        "CK_8000"
};

static const char *vkPunctNames[] = {
        "MINUS", "EQUALS", "LEFTBRACKET", "RIGHTBRACKET", "BACKSLASH",
        "HASH", "SEMICOLON", "APOSTROPHE", "GRAVE", "COMMA", "PERIOD", "SLASH"
};

static const char *vkMouseNames[] = {
        "MOUSE_LEFT",
        "MOUSE_MIDDLE",
        "MOUSE_RIGHT",
        "MOUSE_X1",
        "MOUSE_X2",
        "MOUSE_WHEEL_UP",
        "MOUSE_WHEEL_DN",
};

static const char *vkJoyNames[] = {
        "JOY1_A",
        "JOY1_B",
        "JOY1_X",
        "JOY1_Y",
        "JOY1_BACK",
        "JOY1_GUIDE",
        "JOY1_START",
        "JOY1_LSTICK",
        "JOY1_RSTICK",
        "JOY1_LSHOULDER",
        "JOY1_RSHOULDER",
        "JOY1_DPAD_UP",
        "JOY1_DPAD_DOWN",
        "JOY1_DPAD_LEFT",
        "JOY1_DPAD_RIGHT",
        "JOY1_BUTTON_15",
        "JOY1_BUTTON_16",
        "JOY1_BUTTON_17",
        "JOY1_BUTTON_18",
        "JOY1_BUTTON_19",
        "JOY1_TOUCHPAD",
        "JOY1_BUTTON_21",
        "JOY1_BUTTON_22",
        "JOY1_BUTTON_23",
        "JOY1_BUTTON_24",
        "JOY1_BUTTON_25",
        "JOY1_BUTTON_26",
        "JOY1_BUTTON_27",
        "JOY1_BUTTON_28",
        "JOY1_BUTTON_29",
        "JOY1_LTRIGGER",
        "JOY1_RTRIGGER",
};

static const char *vkVRNames[] = {
        "VR_LEFT_TRIGGER",
        "VR_LEFT_GRIP",
        "VR_LEFT_X",
        "VR_LEFT_Y",
        "VR_LEFT_MENU",
        "VR_LEFT_THUMBSTICK",
        "VR_RIGHT_TRIGGER",
        "VR_RIGHT_GRIP",
        "VR_RIGHT_A",
        "VR_RIGHT_B",
        "VR_RIGHT_THUMBSTICK",
};


static char vkNames[VK_TOTAL_COUNT][64];

static s8 vkPrevState[VK_TOTAL_COUNT];




void inputSetDefaultKeyBinds(s32 cidx, s32 n64mode)
{
    // TODO: make VK constants for all these
    static const u32 pckbbinds[][3] = {
            { CK_B,             SDL_SCANCODE_E,      0                   },
            { CK_X,             SDL_SCANCODE_R,      0                   },
            { CK_RTRIG,         VK_MOUSE_RIGHT,      SDL_SCANCODE_Z      },
            { CK_LTRIG,         SDL_SCANCODE_F,      SDL_SCANCODE_X      },
            { CK_ZTRIG,         VK_MOUSE_LEFT,       SDL_SCANCODE_SPACE  },
            { CK_START,         SDL_SCANCODE_RETURN, SDL_SCANCODE_TAB    },
            { CK_DPAD_D,        SDL_SCANCODE_Q,      VK_MOUSE_MIDDLE     },
            { CK_DPAD_U,        0,                   0                   },
            { CK_Y,             VK_MOUSE_WHEEL_DN,   0                   },
            { CK_DPAD_L,        VK_MOUSE_WHEEL_UP,   0                   },
            { CK_C_D,           SDL_SCANCODE_S,      0                   },
            { CK_C_U,           SDL_SCANCODE_W,      0                   },
            { CK_C_R,           SDL_SCANCODE_D,      0                   },
            { CK_C_L,           SDL_SCANCODE_A,      0                   },
            { CK_STICK_XNEG,    SDL_SCANCODE_LEFT,   0                   },
            { CK_STICK_XPOS,    SDL_SCANCODE_RIGHT,  0                   },
            { CK_STICK_YNEG,    SDL_SCANCODE_DOWN,   0                   },
            { CK_STICK_YPOS,    SDL_SCANCODE_UP,     0                   },
            { CK_4000,          SDL_SCANCODE_LSHIFT, 0                   },
            { CK_2000,          SDL_SCANCODE_LCTRL,  0                   }
    };

    static const u32 pcjoybinds[][2] = {
            { CK_A,      SDL_CONTROLLER_BUTTON_A             },
            { CK_X,      SDL_CONTROLLER_BUTTON_X             },
            { CK_Y,      SDL_CONTROLLER_BUTTON_Y             },
            { CK_DPAD_L, SDL_CONTROLLER_BUTTON_B,            },
            { CK_DPAD_D, SDL_CONTROLLER_BUTTON_LEFTSHOULDER  },
            { CK_LTRIG,  SDL_CONTROLLER_BUTTON_RIGHTSHOULDER },
            { CK_RTRIG,  VK_JOY1_LTRIG - VK_JOY1_BEGIN       },
            { CK_ZTRIG,  VK_JOY1_RTRIG - VK_JOY1_BEGIN       },
            { CK_START,  SDL_CONTROLLER_BUTTON_START         },
            { CK_C_D,    SDL_CONTROLLER_BUTTON_DPAD_DOWN     },
            { CK_C_U,    SDL_CONTROLLER_BUTTON_DPAD_UP       },
            { CK_C_R,    SDL_CONTROLLER_BUTTON_DPAD_RIGHT    },
            { CK_C_L,    SDL_CONTROLLER_BUTTON_DPAD_LEFT     },
            { CK_ACCEPT, SDL_CONTROLLER_BUTTON_A             },
            { CK_CANCEL, SDL_CONTROLLER_BUTTON_B             },
            { CK_8000,   SDL_CONTROLLER_BUTTON_LEFTSTICK     },
    };

    static const u32 n64kbbinds[][3] = {
            { CK_A,          SDL_SCANCODE_Q,      0                  },
            { CK_B,          SDL_SCANCODE_E,      0                  },
            { CK_RTRIG,      VK_MOUSE_RIGHT,      SDL_SCANCODE_LALT  },
            { CK_LTRIG,      SDL_SCANCODE_F,      0                  },
            { CK_ZTRIG,      VK_MOUSE_LEFT,       SDL_SCANCODE_SPACE },
            { CK_START,      SDL_SCANCODE_RETURN, 0                  },
            { CK_C_D,        SDL_SCANCODE_S,      0                  },
            { CK_C_U,        SDL_SCANCODE_W,      0                  },
            { CK_C_R,        SDL_SCANCODE_D,      0                  },
            { CK_C_L,        SDL_SCANCODE_A,      0                  },
            { CK_DPAD_L,     SDL_SCANCODE_LEFT,   0                  },
            { CK_DPAD_R,     SDL_SCANCODE_RIGHT,  0                  },
            { CK_DPAD_D,     SDL_SCANCODE_DOWN,   0                  },
            { CK_DPAD_U,     SDL_SCANCODE_UP,     0                  },
            { CK_STICK_YNEG, SDL_SCANCODE_K,      0                  },
            { CK_STICK_YPOS, SDL_SCANCODE_I,      0                  },
            { CK_STICK_XNEG, SDL_SCANCODE_J,      0                  },
            { CK_STICK_XPOS, SDL_SCANCODE_L,      0                  },
    };

    static const u32 n64joybinds[][2] = {
            { CK_A,      SDL_CONTROLLER_BUTTON_A             },
            { CK_B,      SDL_CONTROLLER_BUTTON_B             },
            { CK_LTRIG,  SDL_CONTROLLER_BUTTON_LEFTSHOULDER  },
            { CK_RTRIG,  SDL_CONTROLLER_BUTTON_RIGHTSHOULDER },
            { CK_ZTRIG,  VK_JOY1_RTRIG - VK_JOY1_BEGIN       },
            { CK_START,  SDL_CONTROLLER_BUTTON_START         },
            { CK_DPAD_D, SDL_CONTROLLER_BUTTON_DPAD_DOWN     },
            { CK_DPAD_U, SDL_CONTROLLER_BUTTON_DPAD_UP       },
            { CK_DPAD_L, SDL_CONTROLLER_BUTTON_DPAD_LEFT     },
            { CK_DPAD_R, SDL_CONTROLLER_BUTTON_DPAD_RIGHT    },
    };

    memset(binds[cidx], 0, sizeof(binds[cidx]));

    const u32 (*kbbinds)[3];
    const u32 (*joybinds)[2];
    u32 numkbbinds;
    u32 numjoybinds;
    if (n64mode) {
        kbbinds = n64kbbinds;
        joybinds = n64joybinds;
        numkbbinds = sizeof(n64kbbinds) / sizeof(n64kbbinds[0]);
        numjoybinds = sizeof(n64joybinds) / sizeof(n64joybinds[0]);
    } else {
        kbbinds = pckbbinds;
        joybinds = pcjoybinds;
        numkbbinds = sizeof(pckbbinds) / sizeof(pckbbinds[0]);
        numjoybinds = sizeof(pcjoybinds) / sizeof(pcjoybinds[0]);
    }

    if (cidx == 0) {
        for (u32 i = 0; i < numkbbinds; ++i) {
            for (s32 j = 1; j < 3; ++j) {
                if (kbbinds[i][j]) {
                    inputKeyBind(cidx, kbbinds[i][0], j - 1, kbbinds[i][j]);
                }
            }
        }
    }

    for (u32 i = 0; i < numjoybinds; ++i) {
        inputKeyBind(cidx, joybinds[i][0], -1, VK_JOY_BEGIN + cidx * INPUT_MAX_CONTROLLER_BUTTONS + joybinds[i][1]);
    }
}

static inline s32 inputDeviceIndexFromId(const SDL_JoystickID id) {
    for (s32 jidx = 0; jidx < numJoysticks; ++jidx) {
        if (SDL_JoystickGetDeviceInstanceID(jidx) == id) {
            return jidx;
        }
    }
    return -1;
}

static inline SDL_JoystickID inputControllerGetId(SDL_GameController *ctrl)
{
    return SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(ctrl));
}

static inline void inputInitController(const s32 cidx, const s32 jidx)
{
#if SDL_VERSION_ATLEAST(2, 0, 18)
    // SDL_GameControllerHasRumble() appeared in 2.0.18 even though SDL_GameControllerRumble() is in 2.0.9
    padsCfg[cidx].rumbleOn = SDL_GameControllerHasRumble(pads[cidx]);
#else
    // assume that all joysticks with haptic feedback support will support rumble
    padsCfg[cidx].rumbleOn = SDL_JoystickIsHaptic(SDL_GameControllerGetJoystick(pads[cidx]));
    if (!padsCfg[cidx].rumbleOn) {
        // at least on Windows some controllers will report no haptics, but rumble will still function
        // just assume it's supported if the controller is of known type
        const SDL_GameControllerType ctype = SDL_GameControllerGetType(pads[cidx]);
        padsCfg[cidx].rumbleOn = ctype && (ctype != SDL_CONTROLLER_TYPE_VIRTUAL);
    }
#endif

    // make the LEDs on the controller indicate which player it's for
    SDL_GameControllerSetPlayerIndex(pads[cidx], cidx);

    // remember the joystick index
    padsCfg[cidx].deviceIndex = jidx;

    connectedMask |= (1 << cidx);

    sysLogPrintf(LOG_NOTE, "input: assigned controller '%d: (%s)' (id %d) to player %d",
                 jidx, SDL_GameControllerName(pads[cidx]), inputControllerGetId(pads[cidx]), cidx);

    SDL_Joystick* joy = SDL_GameControllerGetJoystick(pads[cidx]);
    if (joy) {
        char guidStr[1024] = "";
        SDL_JoystickGUID guid = SDL_JoystickGetGUID(joy);
        SDL_JoystickGetGUIDString(guid, guidStr, sizeof(guidStr));
        sysLogPrintf(LOG_NOTE, "input: GUID for controller %d: %s", jidx, guidStr);
    }
}

static inline void inputCloseController(const s32 cidx)
{
    sysLogPrintf(LOG_NOTE, "input: removed controller '%d: (%s)' (id %d) from player %d",
                 padsCfg[cidx].deviceIndex, SDL_GameControllerName(pads[cidx]), inputControllerGetId(pads[cidx]), cidx);

    // reset player LEDs
    SDL_GameControllerSetPlayerIndex(pads[cidx], -1);

    SDL_GameControllerClose(pads[cidx]);

    pads[cidx] = NULL;
    padsCfg[cidx].rumbleOn = 0;

    if (cidx) {
        connectedMask &= ~(1 << cidx);
    }
}

static inline s32 inputControllerGetIndex(SDL_GameController *ctrl)
{
    if (ctrl) {
        for (s32 i = 0; i < INPUT_MAX_CONTROLLERS; ++i) {
            if (pads[i] == ctrl) {
                return i;
            }
        }
    }
    return -1;
}

static inline s32 inputControllerGetIndexByDeviceIndex(const s32 jidx)
{
    for (s32 cidx = 0; cidx < INPUT_MAX_CONTROLLERS; ++cidx) {
        if (pads[cidx] && padsCfg[cidx].deviceIndex == jidx) {
            return cidx;
        }
    }
    return -1;
}

static inline s32 inputControllerGetIndexById(const SDL_JoystickID jid)
{
    for (s32 cidx = 0; cidx < INPUT_MAX_CONTROLLERS; ++cidx) {
        if (pads[cidx]) {
            if (inputControllerGetId(pads[cidx]) == jid) {
                return cidx;
            }
        }
    }
    return -1;
}

static inline void inputCloseAllControllers(void)
{
    for (s32 cidx = 0; cidx < INPUT_MAX_CONTROLLERS; ++cidx) {
        if (pads[cidx]) {
            inputCloseController(cidx);
            pads[cidx] = NULL;
        }
    }

    connectedMask = 1; // always report first controller as connected
}

static inline s32 inputTryController(const s32 cidx, const s32 jidx)
{
    if (!pads[cidx]) {
        pads[cidx] = SDL_GameControllerOpen(jidx);
        if (pads[cidx]) {
            inputInitController(cidx, jidx);
            return 1;
        }
    }
    return 0;
}

static inline void inputInitAllControllers(void)
{
    SDL_GameControllerUpdate();

    numJoysticks = SDL_NumJoysticks();

    connectedMask = 1; // always report first controller as connected

    // first try to assign the controllers that we had last time
    // we're still free to check by device index before any controller device events fire
    for (s32 cidx = 0; cidx < INPUT_MAX_CONTROLLERS; ++cidx) {
        const s32 jidx = padsCfg[cidx].deviceIndex;
        if (jidx >= 0 && jidx < numJoysticks) {
            if (SDL_IsGameController(jidx) && inputControllerGetIndexByDeviceIndex(jidx) < 0) {
                // using the full assign function in case user sets same index for several players
                if (inputTryController(cidx, jidx)) {
                    // success
                    continue;
                }
            }
            // nothing was there, forget it
            padsCfg[cidx].deviceIndex = -1;
        }
    }

    // now try autofilling the rest, starting with firstController
    for (s32 jidx = 0; jidx < numJoysticks; ++jidx) {
        if (SDL_IsGameController(jidx) && inputControllerGetIndexByDeviceIndex(jidx) < 0) {
            for (s32 cidx = firstController; cidx < INPUT_MAX_CONTROLLERS; ++cidx) {
                if (inputTryController(cidx, jidx)) {
                    break;
                }
            }
        }
    }

    const s32 overrideMask = (1 << fakeControllers) - 1;
    if (overrideMask) {
        connectedMask = overrideMask;
    }
}

static int inputEventFilter(void *data, SDL_Event *event)
{
    extern volatile int g_gevrAppBackgrounded;

    switch (event->type) {
        /*
         * Android pauses the game thread inside SDL's event pump (not the
         * frame wait) while the app is in the background, e.g. a sleeping
         * headset. SDL sends these on that thread just before it blocks and
         * just after it resumes; the frame watchdog leaves a pause alone.
         */
        case SDL_APP_WILLENTERBACKGROUND:
        case SDL_APP_DIDENTERBACKGROUND:
            g_gevrAppBackgrounded = 1;
            break;
        case SDL_APP_WILLENTERFOREGROUND:
        case SDL_APP_DIDENTERFOREGROUND:
            g_gevrAppBackgrounded = 0;
            break;

        case SDL_CONTROLLERDEVICEADDED:
            for (s32 i = firstController; i < INPUT_MAX_CONTROLLERS; ++i) {
                if (!pads[i]) {
                    pads[i] = SDL_GameControllerOpen(event->cdevice.which);
                    if (pads[i]) {
                        inputInitController(i, event->cdevice.which);
                    }
                    break;
                }
            }
            break;

        case SDL_CONTROLLERDEVICEREMOVED: {
            SDL_GameController *ctrl = SDL_GameControllerFromInstanceID(event->cdevice.which);
            const s32 idx = inputControllerGetIndex(ctrl);
            if (idx >= 0) {
                inputCloseController(idx);
                padsCfg[idx].deviceIndex = -1;
            }
            break;
        }

        case SDL_JOYDEVICEADDED:
        case SDL_JOYDEVICEREMOVED:
            numJoysticks = SDL_NumJoysticks(); // joystick count has changed
            break;

        case SDL_MOUSEWHEEL:
            mouseWheel = event->wheel.y;
            if (!lastKey && mouseWheel) {
                lastKey = (mouseWheel < 0) + VK_MOUSE_WHEEL_UP;
            }
            break;

        case SDL_MOUSEBUTTONDOWN:
            if (!lastKey) {
                lastKey = VK_MOUSE_BEGIN - 1 + event->button.button;
            }
            break;

        case SDL_KEYDOWN:
            if (!lastKey) {
                lastKey = VK_KEYBOARD_BEGIN + event->key.keysym.scancode;
            }
            break;

        case SDL_CONTROLLERBUTTONDOWN:
            if (!lastKey) {
                lastKey = VK_JOY1_BEGIN + event->cbutton.button;
                SDL_GameController *ctrl = SDL_GameControllerFromInstanceID(event->cdevice.which);
                const s32 idx = inputControllerGetIndex(ctrl);
                if (idx >= 0) {
                    lastKey += idx * INPUT_MAX_CONTROLLER_BUTTONS;
                }
            }
            break;

        case SDL_CONTROLLERAXISMOTION:
            if (!lastKey) {
                if (event->caxis.axis >= SDL_CONTROLLER_AXIS_TRIGGERLEFT && event->caxis.value > TRIG_THRESHOLD) {
                    lastKey = VK_JOY1_LTRIG + (event->caxis.axis - SDL_CONTROLLER_AXIS_TRIGGERLEFT);
                    SDL_GameController *ctrl = SDL_GameControllerFromInstanceID(event->cdevice.which);
                    const s32 idx = inputControllerGetIndex(ctrl);
                    if (idx >= 0) {
                        lastKey += idx * INPUT_MAX_CONTROLLER_BUTTONS;
                    }
                }
            }
            break;

        case SDL_TEXTINPUT:
            if (!lastChar && event->text.text[0] && (u8)event->text.text[0] < 0x80) {
                lastChar = event->text.text[0];
            }
            break;

        default:
            break;
    }

    return 0;
}

static inline void inputGetScancodeName(const SDL_Scancode sc, char *out, size_t len)
{
    const char *scname = SDL_GetScancodeName(sc);
    if (scname) {
        strncpy(out, scname, len - 1);
        for (u32 i = 0; i < len && out[i]; ++i) {
            if (out[i] == ' ') {
                out[i] = '_';
            } else {
                out[i] = toupper(out[i]);
            }
        }
    } else {
        snprintf(out, len, "KEY%d", (s32)sc);
    }
}

static inline void inputInitKeyNames(void)
{

    for (SDL_Scancode key = SDL_SCANCODE_A; key <= SDL_SCANCODE_SPACE; ++key) {
        inputGetScancodeName(key, vkNames[key], sizeof(vkNames[key]));
    }

    // special characters
    for (SDL_Scancode key = SDL_SCANCODE_MINUS; key < SDL_SCANCODE_CAPSLOCK; ++key) {
        strcpy(vkNames[key], vkPunctNames[key - SDL_SCANCODE_MINUS]);
    }

    for (SDL_Scancode key = SDL_SCANCODE_CAPSLOCK; key <= SDL_SCANCODE_NUMLOCKCLEAR; ++key) {
        inputGetScancodeName(key, vkNames[key], sizeof(vkNames[key]));
    }

    // keypad names
    strcpy(vkNames[SDL_SCANCODE_KP_DIVIDE], "KP_DIVIDE");
    strcpy(vkNames[SDL_SCANCODE_KP_MULTIPLY], "KP_MULTIPLY");
    strcpy(vkNames[SDL_SCANCODE_KP_MINUS], "KP_MINUS");
    strcpy(vkNames[SDL_SCANCODE_KP_PLUS], "KP_PLUS");
    strcpy(vkNames[SDL_SCANCODE_KP_ENTER], "KP_ENTER");
    strcpy(vkNames[SDL_SCANCODE_KP_PERIOD], "KP_PERIOD");
    strcpy(vkNames[SDL_SCANCODE_KP_EQUALS], "KP_EQUALS");
    for (SDL_Scancode key = SDL_SCANCODE_KP_1; key < SDL_SCANCODE_KP_0; ++key) {
        char tmp[8] = "KP_1";
        tmp[3] = '1' + (key - SDL_SCANCODE_KP_1);
        strcpy(vkNames[key], tmp);
    }

    for (SDL_Scancode key = SDL_SCANCODE_LCTRL; key <= SDL_SCANCODE_RGUI; ++key) {
        inputGetScancodeName(key, vkNames[key], sizeof(vkNames[key]));
    }

    // mouse names
    for (u32 vk = VK_MOUSE_BEGIN; vk < VK_JOY1_BEGIN; ++vk) {
        strcpy(vkNames[vk], vkMouseNames[vk - VK_MOUSE_BEGIN]);
    }



    // joystick names
    for (u32 vk = VK_JOY1_BEGIN; vk < VK_TOTAL_COUNT; ++vk) {
        const u32 jidx = (vk - VK_JOY1_BEGIN) / INPUT_MAX_CONTROLLER_BUTTONS;
        const u32 jbtn = (vk - VK_JOY1_BEGIN) % INPUT_MAX_CONTROLLER_BUTTONS;
        strcpy(vkNames[vk], vkJoyNames[jbtn]);
        vkNames[vk][3] = '1' + jidx;
    }



}

void inputSaveBinds(void)
{
    char *bindstr;

    for (s32 i = 0; i < MAXCONTROLLERS; ++i) {
        for (u32 ck = 0; ck < CK_TOTAL_COUNT; ++ck) {
            bindstr = bindStrs[i][ck];
            bindstr[0] = '\0';
            for (s32 b = 0; b < INPUT_MAX_BINDS; ++b) {
                if (binds[i][ck][b]) {
                    if (b) {
                        strncat(bindstr, ", ", MAX_BIND_STR - 1);
                    }
                    strncat(bindstr, inputGetKeyName(binds[i][ck][b]), MAX_BIND_STR - 1);
                }
            }
            if (!bindstr[0]) {
                strcpy(bindstr, "NONE");
            }
        }
    }
}

static inline void inputParseBindString(const s32 ctrl, const u32 ck, char *bindstr)
{
    if (!bindstr[0]) {
        // empty string, keep defaults
        return;
    }

    // unbind all first
    memset(binds[ctrl][ck], 0, sizeof(binds[ctrl][ck]));

    if (!strcasecmp(bindstr, "NONE")) {
        // explicitly nothing bound
        return;
    }

    const char *tok = strtok(bindstr, ", ");
    while (tok) {
        if (tok[0]) {
            const s32 vk = inputGetKeyByName(tok);
            if (vk > 0) {
                inputKeyBind(ctrl, ck, -1, vk);
            }
        }
        tok = strtok(NULL, ", ");
    }
}

static inline void inputLoadBinds(void)
{
    for (s32 i = 0; i < MAXCONTROLLERS; ++i) {
        for (u32 ck = 0; ck < CK_TOTAL_COUNT; ++ck) {
            inputParseBindString(i, ck, bindStrs[i][ck]);
        }
    }
}

s32 inputInit(void)
{
    // Set SDL hints before initializing the controller subsystem.
    /*   if (useHIDAPI) {
   #if SDL_VERSION_ATLEAST(2, 0, 12)
           SDL_SetHint(SDL_HINT_JOYSTICK_HIDAPI_GAMECUBE, "1");
   #endif
   #if SDL_VERSION_ATLEAST(2, 0, 14)
           SDL_SetHint(SDL_HINT_JOYSTICK_HIDAPI_PS5, "1");
   #endif
   #if SDL_VERSION_ATLEAST(2, 0, 22)
           SDL_SetHint(SDL_HINT_JOYSTICK_HIDAPI, "1");
           SDL_SetHint(SDL_HINT_JOYSTICK_HIDAPI_PS4, "1");
           SDL_SetHint(SDL_HINT_JOYSTICK_HIDAPI_SWITCH, "1");
           SDL_SetHint(SDL_HINT_JOYSTICK_HIDAPI_JOY_CONS, "1");
           // the two hints below enable Rumble and Motion Sensor for PS4/5 pads connected via bluetooth
           SDL_SetHint(SDL_HINT_JOYSTICK_HIDAPI_PS4_RUMBLE, "1");
           SDL_SetHint(SDL_HINT_JOYSTICK_HIDAPI_PS5_RUMBLE, "1");
           SDL_SetHint(SDL_HINT_JOYSTICK_HIDAPI_STEAM, "1");
   #endif
   #if SDL_VERSION_ATLEAST(2, 23, 2)
           SDL_SetHint(SDL_HINT_JOYSTICK_HIDAPI_COMBINE_JOY_CONS, "1");
   #endif
   #if SDL_VERSION_ATLEAST(2, 25, 1)
           SDL_SetHint(SDL_HINT_JOYSTICK_HIDAPI_PS3, "1");
   #endif
   #if SDL_VERSION_ATLEAST(2, 26, 0)
           SDL_SetHint(SDL_HINT_JOYSTICK_HIDAPI_WII, "1");
   #endif
   #if SDL_VERSION_ATLEAST(2, 30, 0)
           SDL_SetHint(SDL_HINT_JOYSTICK_HIDAPI_STEAMDECK, "1");
   #endif
       }
       if (useRawInput) {
           SDL_SetHint(SDL_HINT_JOYSTICK_RAWINPUT, "1");
           SDL_SetHint(SDL_HINT_JOYSTICK_RAWINPUT_CORRELATE_XINPUT, "1");
       }

       if (!SDL_WasInit(SDL_INIT_GAMECONTROLLER | SDL_INIT_HAPTIC)) {
           SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER | SDL_INIT_HAPTIC);
       }

       // try to load controller db from an external file in the save folder
       if (fsFileSize("$S/" CONTROLLERDB_FNAME)) {
           const char *dbpath = fsFullPath("$S/" CONTROLLERDB_FNAME);
           const s32 dbcount = SDL_GameControllerAddMappingsFromFile(dbpath);
           if (dbcount >= 0) {
               sysLogPrintf(LOG_NOTE, "input: added %d controller mappings from %s", dbcount, dbpath);
           }
       }
   */

    inputInitAllControllers();

    // since the main event loop is elsewhere, we can receive some events we need using a watcher
    SDL_AddEventWatch(inputEventFilter, NULL);

    inputInitKeyNames();

    for (s32 i = 0; i < INPUT_MAX_CONTROLLERS; ++i) {
        inputSetDefaultKeyBinds(i, 0);
    }

    // Setup VR bindings
    inputSetupVRBindings(0);

    if (mouseLockMode != MLOCK_AUTO) {
        inputLockMouse(mouseLockMode);
    }

    // update the axis maps
    // NOTE: by default sticks get swapped for 1.2: "right stick" here means left stick on your controller
    for (s32 i = 0; i < INPUT_MAX_CONTROLLERS; ++i) {
        inputControllerSetSticksSwapped(i, padsCfg[i].swapSticks);
    }

    // inputLoadBinds(); Removed for VR



    return connectedMask;
}

static inline s32 inputBindPressed(const s32 idx, const u32 ck)
{
    for (s32 i = 0; i < INPUT_MAX_BINDS; ++i) {
        if (binds[idx][ck][i]) {
            if (inputKeyPressed(binds[idx][ck][i])) {
                return 1;
            }
        }
    }
    return 0;
}

static inline s32 inputAxisScale(s32 x, const s32 deadzone, const f32 scale)
{
    if (abs(x) < deadzone) {
        return 0;
    } else {
// rescale to fit the non-deadzone range
        if (x < 0) {
            x += deadzone;
        } else {
            x -= deadzone;
        }
        x = x * 32768 / (32768 - deadzone);
// scale with sensitivity
        x *= scale;
        return (x > 32767) ? 32767 : ((x < -32768) ? -32768 : x);
    }
}

s32 inputReadController(s32 idx, OSContPad *npad)
{
    extern bool netIsActive(void);
    /* An online-only slot (4..7, src/joy.c gevrReadSlotPads) has no port, binds
     * or settings of its own; the online branches below fill its pad. */
    const bool slotPad = idx >= INPUT_MAX_CONTROLLERS && idx < INPUT_MAX_SLOT_PADS && netIsActive();

    if (idx < 0 || (idx >= INPUT_MAX_CONTROLLERS && !slotPad) || !npad) {
        return -1;
    }

    npad->button = 0;

    if (textInput) {
        s_gevrReloadInput.context = s_gevrReloadInput.pending = 0;
        npad->stick_x = 0;
        npad->stick_y = 0;
        npad->rstick_x = 0;
        npad->rstick_y = 0;
        return 0;
    }

    if (!slotPad) {
        for (u32 i = 0; i < CONT_NUM_BUTTONS; ++i) {
            if (inputBindPressed(idx, i)) {
                npad->button |= 1U << i;
            }
        }

        const s32 xdiff = (inputBindPressed(idx, CK_STICK_XPOS) - inputBindPressed(idx, CK_STICK_XNEG));
        const s32 ydiff = (inputBindPressed(idx, CK_STICK_YPOS) - inputBindPressed(idx, CK_STICK_YNEG));
        npad->stick_x = xdiff < 0 ? -0x80 : (xdiff > 0 ? 0x7F : 0);
        npad->stick_y = ydiff < 0 ? -0x80 : (ydiff > 0 ? 0x7F : 0);
    }

    /* the first port's settings for the headset's own slot when it has no port */
    const struct controllercfg *cfg = &padsCfg[slotPad ? 0 : idx];


    extern int netGetLocalSlot(void);
    extern int bossGetStageNum(void);
    extern int gevrCoopMenuFollowing(void);
    /*
     * Co-op (#94): the party's menus are the title stage, which has no player
     * slots and reads the first controller: this headset's, wherever its slot.
     * A headset following the host's menus takes no input of its own there.
     */
    const int inMenus = bossGetStageNum() == 90; /* LEVELID_TITLE */
    const int localSlot = netIsActive() && !inMenus ? netGetLocalSlot() : 0;
    bool reloadGameplay = false;

    if (inMenus && netIsActive() && gevrCoopMenuFollowing()) {
        s_gevrReloadInput.context = s_gevrReloadInput.pending = 0;
        memset(npad, 0, sizeof(*npad));
        return 0;
    }

    if (netIsActive() && idx != localSlot) {
        memset(npad, 0, sizeof(*npad));
        if (netIsRemotePlayerActive(idx)) {
            const struct netplayermove *m = netGetRemotePlayerMove(idx);
            if (m) {
                /* Remote positions are authoritative. Feeding their movement
                 * back through the local walk simulation moves them twice and
                 * makes the character bounce between ticks. */
                if ((m->ucmd & UCMD_FIRE) && g_playerPointers[idx] &&
                    !g_playerPointers[idx]->bonddead) {
                    npad->button |= Z_TRIG;
                }
                /* The crouch arrives with the state (net_player_sync.c
                 * crouchpos). As a C-down press it also stepped the copy
                 * back, or turned its look up, by control style. */
            }
        }
        return 0;
    }

    /* Quest screen mode: ordinary GoldenEye 1.2 controls, no tracked-hand
     * weapon logic or Perfect Dark extended buttons. */
    if (idx == localSlot) {
        memset(npad, 0, sizeof(*npad));
        /*
         * This file reads struct player directly but sees <stdbool.h>'s bool,
         * not the game's s32 one; bondtypes.h explains why struct members may
         * not be bool. Check once that both sides agree on the layout.
         */
        {
            static s32 layoutchecked = 0;
            if (!layoutchecked) {
                u32 size, pausestate;
                layoutchecked = 1;
                gevrPlayerLayout(&size, &pausestate);
                if (size != sizeof(struct player) || pausestate != offsetof(struct player, pause_state)) {
                    sysLogPrintf(LOG_ERROR, "input: struct player layout differs from the game's "
                            "(size %u vs %u, pause_state at %u vs %u)", (u32)sizeof(struct player), size,
                            (u32)offsetof(struct player, pause_state), pausestate);
                }
            }
        }
        /*
         * gepc-ref D194/D238: hold the player on 1.2 Solitaire every poll.
         * The mapping below only makes sense under that style, and the watch
         * Controls page (options.c, sub_GAME_7F0A611C) or a save load can
         * change it; after the watch the left stick became look and the right
         * one did nothing. Re-asserting is plain field writes, as the setter is.
         */
        if (g_CurrentPlayer != NULL && cur_player_get_control_type() != CONTROLLER_CONFIG_SOLITARE) {
            static s32 lastlogged = -1;
            if (cur_player_get_control_type() != lastlogged) {
                lastlogged = cur_player_get_control_type();
                LOGI("input: control style %d -> Solitaire\n", lastlogged);
            }
            cur_player_set_control_type(CONTROLLER_CONFIG_SOLITARE);
        }
#ifdef ANDROID
        const bool nativePause=gevrNativePauseOpen()!=0;
#else
        const bool nativePause=false;
#endif
        const bool paused = g_CurrentPlayer && g_CurrentPlayer->pause_state != 0;
        const bool menu = nativePause || bossGetStageNum() == LEVELID_TITLE || paused ||
                          (netIsActive() && g_CurrentPlayer && g_CurrentPlayer->mpmenuon);
        XrVector2f left = {0}, right = {0};
        get_2d_input(0, "thumbstick", &left);
        get_2d_input(1, "thumbstick", &right);
        // Gun fit (launcher "Gun fit...", user): in a level with a gun in hand,
        // the sticks move the gun on the hand - VrGunOffX/Y/Z, which
        // bondview2.c gevrStereoGunMatrix reads every frame - instead of the
        // player: the move stick forward/back and sideways, the turn stick up
        // and down, about 3 cm a second. A keeps it (goldeneye-vr.ini), B puts
        // it back; Menu + A starts and ends it in play. The game gets neither
        // stick nor A/B. GoldenEye X's models keep their own fit (user), and X
        // switches a scoped gun's fit to its scope's lens and back (user).
        bool fitting = false;
        {
            static bool fitWas = false, aHeld = true, bHeld = true, xHeld = true, ltHeld = true, yHeld = true;
            static u32 fitLast;
            const u32 now = SDL_GetTicks();
            fitting = VrGunFitArmed && !menu && g_gevrStereo && gevrGunFitAvailable();
            const int gex = fitting && gevrGexHeld(GUNRIGHT) ? 1 : 0;
            float *gunOff[3] = { &VrGunOffX, &VrGunOffY, &VrGunOffZ };
            float (*gripTrim)[6] = gex ? VrGexGripTrim : VrGripTrim;
            const s32 scope = fitting ? gevrScopeFitIndex() : -1;
            const s32 fitItem = fitting ? (s32)getCurrentPlayerWeaponId(GUNRIGHT) : -1;
            if (gex) {
                float *fit = gevrGexGunFit(fitItem);
                gunOff[0] = &fit[0];
                gunOff[1] = &fit[1];
                gunOff[2] = &fit[2];
            }
            if (scope < 0) gevrScopeFitting = 0;
            if (!fitting || !gevrReloadFitAvailable()) gevrReloadFitting = 0;
            if (!gex) gevrOffHandFitting = 0;
            if (!gex || !gevrGexHasAmmo(gevrGexWeaponForHand(GUNRIGHT))) gevrHeldMagFitting = 0;
            if (!gex || !gevrGexHasAmmo(gevrGexWeaponForHand(GUNRIGHT))) gevrWellFitting = 0;
            if (!gex || !gevrGexHasMagazine(gevrGexWeaponForHand(GUNRIGHT))) gevrInstalledMagFitting = 0;
            if (!fitting || !gevrMuzzleFitAvailable()) gevrMuzzleFitting = 0;
            if (!gex) gevrGunHandFitting = 0;
            if (fitting && !fitWas) {
                gevrGunFitSaved(false);
                gevrGadgetFitBegin();
                aHeld = bHeld = xHeld = ltHeld = yHeld = true;   /* a button already down does not answer */
                fitLast = now;
                LOGI("input: gun fit on (%.1f %.1f %.1f%s)\n", *gunOff[0], *gunOff[1], *gunOff[2], gex ? ", GoldenEye X" : "");
            }
            if (fitting) {
                const float dz = 0.15f, rate = 3.0f;
                float dt = (now - fitLast) / 1000.0f;
                if (dt > 0.1f) dt = 0.1f;
                fitLast = now;
                float mx = fabsf(left.x) < dz ? 0.0f : left.x;
                float my = fabsf(left.y) < dz ? 0.0f : left.y;
                float ry = fabsf(right.y) < dz ? 0.0f : right.y;
                float rx = fabsf(right.x) < dz ? 0.0f : right.x;
                const s32 gadget = gevrGadgetFitItem();
                /* X: gun, scope, reload places, off hand, held magazine, well, installed magazine, barrel tip,
                 * GE-X's gun hand (bondview2.c gevrFitNextLine says which is next) */
                const bool x = get_button_state(0, "x");
                if (x && !xHeld && gadget < 0) {
                    static const char *const names[9] = { "gun", "scope", "reload", "off hand", "held magazine", "magazine well", "installed magazine", "barrel tip", "gun hand" };
                    const bool can[9] = { true, scope >= 0, gevrReloadFitAvailable() != 0, gex != 0,
                        gevrGexHasAmmo(gevrGexWeaponForHand(GUNRIGHT)), gevrGexHasAmmo(gevrGexWeaponForHand(GUNRIGHT)), gevrGexHasMagazine(gevrGexWeaponForHand(GUNRIGHT)), gevrMuzzleFitAvailable() != 0,
                        gex != 0 };
                    int mode = gevrScopeFitting ? 1 : gevrReloadFitting ? 2 : gevrOffHandFitting ? 3
                        : gevrHeldMagFitting ? 4 : gevrWellFitting ? 5 : gevrInstalledMagFitting ? 6 : gevrMuzzleFitting ? 7
                        : gevrGunHandFitting ? 8 : 0;
                    do {
                        mode = (mode + 1) % 9;
                    } while (!can[mode]);
                    gevrScopeFitting = mode == 1;
                    gevrReloadFitting = mode == 2;
                    gevrOffHandFitting = mode == 3;
                    gevrHeldMagFitting = mode == 4;
                    gevrWellFitting = mode == 5;
                    gevrInstalledMagFitting = mode == 6;
                    gevrMuzzleFitting = mode == 7;
                    gevrGunHandFitting = mode == 8;
                    LOGI("input: gun fit on the %s\n", names[mode]);
                }
                xHeld = x;
                /* the reload mode: the off hand where the magazine is taken (the left
                 * trigger) and where the belt is (Y), shown, not steered */
                const bool lt = get_button_state(0, "trigger"), yb = get_button_state(0, "y");
                if (gevrReloadFitting && lt && !ltHeld) gevrReloadFitSetGrab();
                if (gevrReloadFitting && yb && !yHeld) gevrReloadFitSetBelt();
                if (gevrWellFitting && lt && !ltHeld) gevrReloadFitSetWell();
                ltHeld = lt;
                yHeld = yb;
                if (gadget >= 0) {
                    /* A gadget in the gun hand: its own pose (bondview2.c s_gevrItemPoses) -
                     * the move stick forward and sideways, the turn stick up and its
                     * size; holding the right grip, the sticks turn it instead, about
                     * 45 degrees a second (move stick: X forward, Z sideways; turn stick: Y). */
                    const float side = VrLeftHandedMode ? 1.0f : -1.0f;   /* the model's left is the holder's right negated */
                    if (get_button_state(1, "grip")) {
                        gevrGadgetFitNudge(gadget, 0, 0, 0, my * 45.0f * dt, rx * 45.0f * dt, mx * 45.0f * dt * -side, 0);
                    } else {
                        gevrGadgetFitNudge(gadget, mx * rate * dt * side, ry * rate * dt, my * rate * dt, 0, 0, 0, rx * 0.5f * dt);
                    }
                } else if (gevrReloadFitting) {
                    /* the places are shown with the off hand (above): the sticks rest */
                } else if (gevrGunHandFitting) {
                    /* GE-X's own gun hand on the gun (user: the grenade launcher's hand
                     * missed its grip): the sticks move it, as the gun on its hand;
                     * holding the gun hand's grip they turn it about its palm */
                    if (get_button_state(1, "grip")) {
                        float *r = gevrGexHandRotFit(fitItem);
                        r[0] += my * 45.0f * dt;
                        r[1] += ry * 45.0f * dt;
                        r[2] += mx * 45.0f * dt;
                    } else {
                        extern s32 gevrGexHandHeld(s32 hand);   /* gun.c: a knife, grenade or mine */
                        float *h = gevrGexHandFit(fitItem);
                        h[0] += mx * rate * dt * (VrLeftHandedMode ? -1.0f : 1.0f);
                        h[2] -= my * rate * dt;
                        h[1] += ry * rate * dt;
                        if (gevrGexHandHeld(GUNRIGHT)) {
                            /* a hand-held item: the turn stick's sideways sizes it (user) */
                            float *size = gevrGexItemSizeFit(fitItem);
                            size[0] += rx * 0.5f * dt;
                            if (size[0] < -0.8f) size[0] = -0.8f;
                            if (size[0] > 2.0f) size[0] = 2.0f;
                        }
                    }
                } else if (gevrInstalledMagFitting) {
                    float *fit = gevrGexInstalledMagFit(fitItem);
                    fit[0] += mx * rate * dt * (VrLeftHandedMode ? -1.0f : 1.0f);
                    fit[2] -= my * rate * dt;
                    fit[1] += ry * rate * dt;
                } else if (gevrWellFitting) {
                    float *wellFit = gevrGexWellFit(fitItem);
                    wellFit[0] += mx * rate * dt * (VrLeftHandedMode ? -1.0f : 1.0f);
                    wellFit[2] -= my * rate * dt;
                    wellFit[1] += ry * rate * dt;
                } else if (gevrHeldMagFitting) {
                    float *magFit = gevrGexHeldMagFit(fitItem);
                    magFit[0] += mx * rate * dt * (VrLeftHandedMode ? -1.0f : 1.0f);
                    magFit[2] -= my * rate * dt;
                    magFit[1] += ry * rate * dt;
                } else if (gevrOffHandFitting && get_button_state(1, "grip")) {
                    /* holding the right grip, the watch on GE-X's left wrist (user: over
                     * the wrist, sized to the arm): forward and sideways, up and down,
                     * and the turn stick's sideways its size */
                    VrGexWatch[0] += my * rate * dt;
                    VrGexWatch[2] += mx * rate * dt;
                    VrGexWatch[1] += ry * rate * dt;
                    VrGexWatch[3] += rx * 0.5f * dt;
                    if (VrGexWatch[3] < 0.5f) VrGexWatch[3] = 0.5f;
                    if (VrGexWatch[3] > 3.0f) VrGexWatch[3] = 3.0f;
                } else if (gevrOffHandFitting) {
                    /* GE-X's off hand, empty or holding a magazine (user: it sat
                     * ahead of the hand): the sticks move it, as the gun on its hand */
                    VrGexHeldMag[0] += mx * rate * dt * (VrLeftHandedMode ? -1.0f : 1.0f);
                    VrGexHeldMag[2] -= my * rate * dt;
                    VrGexHeldMag[1] += ry * rate * dt;
                } else if (gevrScopeFitting) {
                    /* the scope's lens, as the gun moves, and the turn stick's
                     * sideways makes it wider or narrower */
                    float *s = VrScopeFit[gex][scope];
                    s[0] += mx * rate * dt * (VrLeftHandedMode ? -1.0f : 1.0f);
                    s[2] -= my * rate * dt;
                    s[1] += ry * rate * dt;
                    s[3] += rx * rate * dt;
                } else if (gevrMuzzleFitting && fitItem > 0 && fitItem < GEVR_MAX_WEAPONS) {
                    /* Muzzle / barrel tip fit:
                     * Left stick: Forward/Back (Z), Side (X)
                     * Right stick: Up/Down (Y)
                     */
                    float *mtrim = VrMuzzleTrim[gex][fitItem];
                    mtrim[0] += mx * rate * dt * (VrLeftHandedMode ? -1.0f : 1.0f);
                    mtrim[2] += my * rate * dt;
                    mtrim[1] += ry * rate * dt;
                } else if (gevrStereoTwoHandGrip() && gex && VrGexGuns) {
                    /* GoldenEye X's own left hand holds it (gun.c): where, cm forward,
                     * up and out along the gun (user: the hold was taken too near the
                     * magazine); the hold is taken there too */
                    if (gevrGexWeaponGet(fitItem) != NULL && get_button_state(1, "grip")) {
                        gevrGexSupportRotFit(fitItem)[0] += my * 45.0f * dt;
                        gevrGexSupportRotFit(fitItem)[1] += ry * 45.0f * dt;
                        gevrGexSupportRotFit(fitItem)[2] += mx * 45.0f * dt;
                    } else {
                        float *supportFit = gevrGexSupportFit(fitItem);
                        supportFit[0] += my * rate * dt;
                        supportFit[2] += mx * rate * dt;
                        supportFit[1] += ry * rate * dt;
                    }
                } else if (gevrStereoTwoHandGrip()) {
                    /* Holding with both hands (#35, user): the holding hand instead, for
                     * this class of gun - [0] out to the off hand's side (so the move
                     * stick's right is inward), [1] up, [2] forward, [3] its tilt, about
                     * 45 degrees a second on the turn stick's sideways - or, with the
                     * right grip held, [5] its roll (user: to turn it more underhand). */
                    float *t = gripTrim[gevrStereoTwoHandClass()];
                    t[0] -= mx * rate * dt;
                    t[2] += my * rate * dt;
                    t[1] += ry * rate * dt;
                    t[get_button_state(1, "grip") ? 5 : 3] += rx * 45.0f * dt;
                } else {
                    /* +X is the holder's right (mirrored when left-handed), +Z back toward you */
                    *gunOff[0] += mx * rate * dt * (VrLeftHandedMode ? -1.0f : 1.0f);
                    *gunOff[2] -= my * rate * dt;
                    *gunOff[1] += ry * rate * dt;
                }
                /* A saves and B undoes back to the last save; the fit goes on, for the
                 * next gun or gadget, until Menu + A ends it (below, user) */
                const bool a = get_button_state(1, "a"), b = get_button_state(1, "b");
                if (a && !aHeld) {
                    vrSettingsSave();
                    gevrGadgetFitEnd(true);
                    gevrGadgetFitBegin();
                    gevrGunFitSaved(false);
                    LOGI("input: gun fit kept (%.1f %.1f %.1f%s)\n", *gunOff[0], *gunOff[1], *gunOff[2], gex ? ", GoldenEye X" : "");
                    if (scope >= 0) {
                        const float *s = VrScopeFit[gex][scope];
                        LOGI("input: scope %d fit %.1f %.1f %.1f, %.1f wider\n", scope, s[0], s[1], s[2], s[3]);
                    }
                    if (fitItem > 0 && fitItem < GEVR_MAX_WEAPONS) {
                        const float *m = VrMuzzleTrim[gex][fitItem];
                        LOGI("input: muzzle %d fit %.1f %.1f %.1f%s\n", fitItem, m[0], m[1], m[2], gex ? ", GoldenEye X" : "");
                    }
                    LOGI("input: reload fit, magazine %.1f %.1f %.1f, belt %.0f %.0f %.0f, held %.1f %.1f %.1f, watch %.1f %.1f %.1f x%.2f\n",
                         VrReloadGrab[gex][0], VrReloadGrab[gex][1], VrReloadGrab[gex][2],
                         VrReloadBelt[0], VrReloadBelt[1], VrReloadBelt[2], VrGexHeldMag[0], VrGexHeldMag[1], VrGexHeldMag[2],
                         VrGexWatch[0], VrGexWatch[1], VrGexWatch[2], VrGexWatch[3]);
                } else if (b && !bHeld) {
                    gevrGunFitSaved(true);
                    gevrGadgetFitEnd(false);
                    gevrGadgetFitBegin();
                    LOGI("input: gun fit undone\n");
                }
                aHeld = a;
                bHeld = b;
                left.x = left.y = right.x = right.y = 0.0f;
            }
            if (fitWas && !fitting) gevrGadgetFitEnd(-1);   /* paused, put away or switched off: the edits stay, unsaved */
            if (!fitting) gevrScopeFitting = gevrOffHandFitting = gevrHeldMagFitting = gevrWellFitting = gevrInstalledMagFitting = gevrMuzzleFitting = gevrGunHandFitting = 0;
            fitWas = fitting;
            /* 1 fitting; 2 on in a level with nothing that fits in hand (bondview2.c says so) */
            gevrGunFitActive = fitting ? 1 : (VrGunFitArmed && !menu && g_gevrStereo) ? 2 : 0;
        }
        // Watch: grips are the N64 L/R triggers, which turn its pages.
        if (paused && get_button_state(0, "grip")) npad->button |= L_TRIG;
        if (paused && get_button_state(1, "grip")) npad->button |= R_TRIG;
        // Screen mode: either trigger fires (the left one was unmapped).
        // Stereo: each hand's trigger is its own gun, as in Perfect Dark VR and
        // GEVR PC - the right trigger is GoldenEye's Z (the right gun) and the
        // left one its R, which fires the left gun when dual-wielding and aims
        // (zooms) otherwise. The left grip then has no job; the right grip
        // keeps aim/zoom.
        const bool stereoplay = g_gevrStereo && !menu;
        // Dual-wielding (issue #15): GoldenEye's R only aims - both guns take
        // turns on Z - so the left trigger fires the left gun instead, as in
        // Perfect Dark VR: it presses Z too, and gunfire.c gunTickGameplay
        // gives each gun its own trigger (gevrVrTriggerDown).
        gevrVrTriggerDown[0] = get_button_state(1, "trigger");   /* GUNRIGHT */
        gevrVrTriggerDown[1] = get_button_state(0, "trigger");   /* GUNLEFT */
        /* GE-X's remote mines (gun.c): the off hand's trigger detonates them, on
         * the screen and in the headset, instead of aiming or firing */
        {
            static bool detonateWas;
            const bool detonate = !menu && gevrGexMineDetonates() && get_button_state(0, "trigger");

            if (detonate && !detonateWas) gevrGexDetonateRequest();
            detonateWas = detonate;
            if (detonate || gevrGexMineDetonates()) gevrVrTriggerDown[1] = 0;
        }
        if (stereoplay && gevrDualWielding()) {
            if (gevrVrTriggerDown[0] || gevrVrTriggerDown[1]) npad->button |= Z_TRIG;
        } else if (stereoplay) {
            if (get_button_state(1, "trigger")) npad->button |= Z_TRIG;
            if (get_button_state(0, "trigger") && !gevrGexMineDetonates()) npad->button |= R_TRIG;
        } else if (get_button_state(1, "trigger") || (get_button_state(0, "trigger") && !gevrGexMineDetonates())) {
            npad->button |= Z_TRIG;
        }
        if (get_button_state(1, "a")) npad->button |= A_BUTTON;
        if (get_button_state(1, "b")) npad->button |= B_BUTTON;
        // Menu button (issue #16): a tap is START, sent on release, and a 1.5 s
        // hold asks whether to go back to the launcher (A yes, B no; bondview2.c
        // gevrDrawReturnPrompt) - in play, on the watch and in the menus alike.
        // The app restarts into it, so the mission in progress is lost.
        // With a texture pack in use the prompt also offers X: switch the pack
        // off, or on again, for the session (gfx_pc.cpp gevrTexpackToggle).
        {
            static u32 downat = 0, startuntil = 0;
            static bool consumed = false, aWas = true, bWas = true, xWas = true;
            const u32 t = SDL_GetTicks();
            const bool held = get_button_state(0, "menu");
            s_menuHeld = held;
            if (gevrReturnPrompt) {
                const bool a = get_button_state(1, "a"), b = get_button_state(1, "b");
                const bool x = get_button_state(0, "x");
                if (a && !aWas) {
                    LOGI("input: menu hold -> back to the launcher\n");
                    if (netIsActive()) {
                        gevrLobbySessionStopped();   /* the goodbye: the host frees the slot at once */
                    }
                    gevrRestartToLauncher();
                }
                if (b && !bWas) {
                    LOGI("input: menu hold -> cancelled\n");
                    gevrReturnPrompt = 0;
                }
                if (x && !xWas && gevrTexpackState() >= 0) {
                    gevrTexpackToggleMsg = gevrTexpackToggle() + 2;
                    gevrReturnPrompt = 0;
                    gevrSwallowX = true;
                    LOGI("input: menu hold -> texture pack %s\n", gevrTexpackToggleMsg == 3 ? "on" : "off");
                }
                aWas = a;
                bWas = b;
                xWas = x;
                downat = 0;
            } else if (held) {
                if (!downat) {
                    downat = t ? t : 1;
                    consumed = false;
                }
                /* Menu + the other hand's B (the physical right controller's,
                 * whichever hand is the gun hand): the multiplayer microphone.
                 * Two hands, nothing else bound; Menu is consumed (no START on
                 * release, no prompt) and B held back while Menu is down, so
                 * nothing leaks into the game (the old X+Y chord sent a reload
                 * or a weapon change and gave no sign it had worked). */
                {
                    static bool micWas = false;
                    const bool mic = VrLeftHandedMode ? get_button_state(0, "y") : get_button_state(1, "b");
                    if (mic && !micWas && netIsActive()) {
                        netVoiceToggleMuted();
                        gevrMicToggleMsg = netVoiceIsMuted() ? 1 : 2;
                        mpwatchPlayBeep();
                        consumed = true;
                        LOGI("input: menu + B -> microphone %s\n", netVoiceIsMuted() ? "muted" : "on");
                    }
                    micWas = mic;
                }
                if (!VrLeftHandedMode) npad->button &= ~B_BUTTON;
                /* Menu + the gun hand's A: Gun fit on or off in play (user), so
                 * guns and gadgets can be fitted one after another without the
                 * launcher; A saves (above), so ending it keeps what was set. */
                {
                    extern s32 getPlayerCount(void);
                    static bool fitChordWas = true;
                    const bool fitChord = get_button_state(1, "a");
                    /* single player only: not online, not split screen (user) */
                    if (fitChord && !fitChordWas && g_gevrStereo && !menu && !netIsActive() && getPlayerCount() == 1) {
                        VrGunFitArmed = !VrGunFitArmed;
                        gevrSwallowA = true;
                        consumed = true;
                        LOGI("input: menu + A -> gun fit %s\n", VrGunFitArmed ? "on" : "off");
                    }
                    fitChordWas = fitChord;
                }
                if (!consumed && t - downat >= 1500) {
                    consumed = true;
                    gevrReturnPrompt = 1;
                    aWas = bWas = xWas = true;   /* a button already down does not answer */
                    LOGI("input: menu hold -> return prompt\n");
                }
            } else {
                if (downat && !consumed) startuntil = t + 100;
                downat = 0;
            }
            if (t < startuntil) npad->button |= START_BUTTON;
        }
        const bool rightGrip = get_button_state(1, "grip");
        const bool leftGrip = get_button_state(0, "grip");
        const bool rightThrowable = g_CurrentPlayer && VrMotionThrowing && gevrIsThrowable(getCurrentPlayerWeaponId(GUNRIGHT));
        const bool leftThrowable = g_CurrentPlayer && VrMotionThrowing && gevrIsThrowable(getCurrentPlayerWeaponId(GUNLEFT));
        // GEVR PC's grip gestures (bondview2.c gevrGripGestureTry): a fresh
        // press waits one game tick while it decides whether the hand is at
        // the hip, a mine, a pickup or a door; one it takes neither aims nor
        // shows the sight until let go. Not in Gun fit, where the grip turns
        // the gadget.
        bool gripTaken[2] = { false, false };
        {
            static bool gripWas[2];
            const bool grips[2] = { leftGrip, rightGrip };
            for (int c = 0; c < 2; c++) {
                const bool on = grips[c] && stereoplay && !fitting;
                gevrGripGestureInput(c, on && !gripWas[c], on);
                gripWas[c] = on;
                gripTaken[c] = on && gevrGripGestureTaken(c);
            }
        }
        if (!menu && ((rightGrip && !rightThrowable && !gripTaken[1]) || (!stereoplay && leftGrip && !leftThrowable)))
            npad->button |= R_TRIG;
        // Issue #37: dual-wielding, the left grip shows the left gun's sight,
        // as Perfect Dark VR's (sight.c sightDrawLeftHand, on vr_button_L_grip).
        // Not R as well: here R aims and zooms.
        vr_button_L_grip = stereoplay && gevrDualWielding() && leftGrip && !leftThrowable && !gripTaken[0];
        // Scope sight diagnosis (sniper/laser headset reports): every source of
        // the aim request, logged only when one of them changes.
        {
            static int aimWas = -1;
            const int aim = (stereoplay ? 1 : 0) | (menu ? 2 : 0) | (fitting ? 4 : 0)
                | (rightGrip ? 8 : 0) | (leftGrip ? 16 : 0) | (get_button_state(0, "trigger") ? 32 : 0)
                | (gripTaken[0] ? 64 : 0) | (gripTaken[1] ? 128 : 0)
                | (gevrStereoTwoHandGrip() ? 256 : 0) | ((npad->button & R_TRIG) ? 512 : 0)
                | ((npad->button & L_TRIG) ? 1024 : 0);
            if (aim != aimWas && g_gevrStereo) {
                LOGI("aimsrc: play %d menu %d fit %d gunGrip %d offGrip %d offTrig %d taken %d/%d twoHand %d R %d L %d item %d\n",
                     aim & 1, !!(aim & 2), !!(aim & 4), !!(aim & 8), !!(aim & 16), !!(aim & 32),
                     !!(aim & 64), !!(aim & 128), !!(aim & 256), !!(aim & 512), !!(aim & 1024),
                     g_CurrentPlayer ? (int)getCurrentPlayerWeaponId(GUNRIGHT) : -1);
            }
            aimWas = aim;
        }
        // The off hand's buttons do what the gun hand's in the same place do, as
        // in the launcher (user): X (lower) is A, the weapons, and Y (upper) is B,
        // use/reload. (Not the X that just switched the texture pack in the
        // prompt, until let go.)
        if (gevrSwallowX && !get_button_state(0, "x")) gevrSwallowX = false;
        if (get_button_state(0, "x") && !gevrSwallowX) npad->button |= A_BUTTON;
        /* the off hand's upper button is B, unless Menu is held on the other
         * hand: left-handed, that is the microphone chord's B */
        if (get_button_state(0, "y") && !(VrLeftHandedMode && s_menuHeld)) npad->button |= B_BUTTON;
        // Logical dominant/off-hand buttons cycle only that hand. Grip reverses;
        // holding opens that hand's selector. get_button_state handles handedness.
        {
            static u32 adown = 0, xdown = 0;
            static bool apanel = false, aspoilt = false, xpanel = false, xspoilt = false;
            static bool aback = false, xback = false;
            const u32 t = SDL_GetTicks();
            if (gevrSwallowA && !get_button_state(1, "a")) gevrSwallowA = false;
            if (stereoplay && !gevrReturnPrompt && !fitting && !gevrSpectating()) {
                const bool a = !gevrSwallowA && get_button_state(1, "a");
                const bool x = !gevrSwallowX && get_button_state(0, "x");
                if (x && !xdown) {
                    xdown = t ? t : 1;
                    xpanel = false;
                    xspoilt = false;
                    xback = get_button_state(0, "grip");
                }
                npad->button &= ~A_BUTTON;
                if (a) {
                    if (!adown) {
                        adown = t ? t : 1;
                        apanel = false;
                        aspoilt = false;
                        aback = get_button_state(1, "grip");
                    }
                    if (get_button_state(1, "grip")) aback = true;
                    if (gevrWeaponPanelOpen && !apanel) aspoilt = true;
                    if (!apanel && !aspoilt && t - adown >= GEVR_WEAPON_PANEL_HOLD_MS) {
                        apanel = true;
                        gevrWeaponPanelLeft = 0;
                        gevrWeaponPanelOpen = 1;
                        LOGI("input: weapon panel open\n");
                    }
                } else if (adown) {
                    if (apanel) {
                        gevrWeaponPanelRelease = 1;
                        gevrWeaponPanelOpen = 0;
                    } else if (!aspoilt) {
                        if (get_button_state(1, "grip")) aback = true;
                        gevrCycleHandWeapon(0, aback ? -1 : 1);
                    }
                    adown = 0;
                }
                if (x) {
                    if (get_button_state(0, "grip")) xback = true;
                    if (gevrWeaponPanelOpen && !xpanel) xspoilt = true;
                    if (gevrLeftPanelAvailable() && !xpanel && !xspoilt && t - xdown >= GEVR_WEAPON_PANEL_HOLD_MS) {
                        xpanel = true;
                        gevrWeaponPanelLeft = 1;
                        gevrWeaponPanelOpen = 1;
                        LOGI("input: left hand panel open\n");
                    }
                } else if (xdown) {
                    if (xpanel) {
                        gevrWeaponPanelRelease = 1;
                        gevrWeaponPanelOpen = 0;
                    } else if (!xspoilt) {
                        if (get_button_state(0, "grip")) xback = true;
                        gevrCycleHandWeapon(1, xback ? -1 : 1);
                    }
                    xdown = 0;
                }
                /* While a wheel is up the triggers step through its category, as
                 * GTA's d-pad does - the button hand's forward, the other's back -
                 * and fire nothing until both are let go after it closes. */
                {
                    static bool trig[2], wheelfire;
                    const bool t0 = get_button_state(0, "trigger"), t1 = get_button_state(1, "trigger");
                    if (gevrWeaponPanelOpen) {
                        const bool fwd = gevrWeaponPanelLeft ? t0 : t1, back = gevrWeaponPanelLeft ? t1 : t0;
                        const bool fwdWas = gevrWeaponPanelLeft ? trig[0] : trig[1], backWas = gevrWeaponPanelLeft ? trig[1] : trig[0];
                        if (fwd && !fwdWas) gevrWeaponPanelStep++;
                        if (back && !backWas) gevrWeaponPanelStep--;
                        wheelfire = true;
                    } else if (!t0 && !t1) {
                        wheelfire = false;
                    }
                    trig[0] = t0;
                    trig[1] = t1;
                    if (wheelfire) {
                        npad->button &= ~(Z_TRIG | (rightGrip ? 0 : R_TRIG));   /* the right grip's R still aims */
                        gevrVrTriggerDown[0] = gevrVrTriggerDown[1] = 0;
                    }
                }
            } else {
                adown = xdown = 0;
                gevrWeaponPanelOpen = 0;
            }
        }
        if (gevrReturnPrompt) npad->button &= ~(A_BUTTON | B_BUTTON | START_BUTTON);
        if (fitting) npad->button &= ~(A_BUTTON | B_BUTTON | R_TRIG);   /* gun fit keeps them (the right grip rolls the grip hand) */
        reloadGameplay = !menu && !fitting && !gevrReturnPrompt && !gevrWeaponPanelOpen
            && !lvlGetControlsLockedFlag();
        const bool lclick = get_button_state(0, "thumbstick_click");
        const bool rclick = get_button_state(1, "thumbstick_click");
        const u32 now = SDL_GetTicks();
        // Left stick click toggles crouch (bondview2.c reads gevrCrouchToggled). It acts on
        // release so that a click of both sticks (recentre) or a long hold (bring the
        // screen back, gevr_engine_shim.c) does not crouch as well.
        {
            static bool wasclicked = false;
            static bool spoilt = false;
            static u32 pressedat = 0;
            static s32 crouchstage = -1;
            if (bossGetStageNum() != crouchstage) {
                crouchstage = bossGetStageNum();
                gevrCrouchToggle = 0;
            }
            if (lclick && !wasclicked) {
                pressedat = now;
                spoilt = false;
            }
            if (lclick && rclick) spoilt = true;
            if (!lclick && wasclicked && !spoilt && !menu && now - pressedat < 700) {
                gevrCrouchToggle = !gevrCrouchToggle;
            }
            wasclicked = lclick;
        }
        // Both stick clicks: recentre. Stereo faces the body the way you look; the
        // screen comes back in front of you.
        {
            static bool both = false;
            if (lclick && rclick && !both) {
                gevrRecenterPending = 1;
                if (gevrVrScreenMode || nativePause) vr_screen_recenter();
            }
            both = lclick && rclick;
        }
        // Raise the left wrist to your face like reading a watch: open the watch.
        // The pose must hold briefly, presses START once, and re-arms only after
        // the arm comes down again (vr_input.cpp gevrVrWatchGesture).
        {
            static GevrWatchGestureState gesture = {0, 0, 1};
            // Issue #31 (user): not while the gun hand holds the watch for the
            // watch laser or the detonator - aiming them raises the wrist too.
            // It re-arms only once the arm comes down, as after a press.
            const bool reading = gevrVrWatchGesture() != 0;
            const bool blocked = menu || !g_gevrStereo || gevrStereoWatchGrip() || gevrStereoTwoHandGrip();
            if (!VrWatchGesturePause) g_gevrWatchGesturePending = 0;
            if (gevrWatchGestureTick(&gesture, now, VrWatchGesturePause, reading, blocked)) {
                LOGI("input: watch gesture -> pause\n");
                g_gevrWatchGesturePending = 1; /* bondview2.c: skip the raise on screen */
            }
            if (gesture.pressUntil && (s32)(gesture.pressUntil - now) > 0) npad->button |= START_BUTTON;
        }
        // Hold the right stick click for a second: switch stereo gameplay and the
        // virtual screen, and remember the choice in goldeneye-vr.ini.
        {
            static u32 downat = 0;
            static bool fired = false, spoilt = false;
            if (rclick) {
                if (!downat) {
                    downat = now ? now : 1;
                    fired = spoilt = false;
                }
                if (lclick) spoilt = true;
                if (!fired && !spoilt && now - downat >= 1000) {
                    VrPlayMode = VrPlayMode ? 0 : 1; /* VR_PLAYMODE_SCREEN : VR_PLAYMODE_STEREO */
                    vrSettingsSave();
                    fired = true;
                    LOGI("input: play mode -> %s\n", VrPlayMode ? "stereo" : "screen");
                }
            } else {
                downat = 0;
            }
        }
        // While the virtual screen is up (menus, cutscenes, the watch, screen play), both
        // grips take hold of it: it follows your hands (vr_screen_grab), and the right
        // stick moves it away/nearer at the same physical size and makes it bigger/smaller
        // with left/right. Saved on release.
        bool adjusting = false;
        {
            static bool changed = false;
            static u32 last = 0;
            float dt = last ? (float)(now - last) / 1000.0f : 0.0f;
            if (dt > 0.1f) dt = 0.1f;
            last = now;
            adjusting = (gevrVrScreenMode || nativePause) && get_button_state(0, "grip") && get_button_state(1, "grip");
            vr_screen_grab(adjusting);
            if (adjusting) {
                changed = true;
                const float dy = fabsf(right.y) > 0.2f ? right.y : 0.0f;
                const float dx = fabsf(right.x) > 0.2f ? right.x : 0.0f;
                if (dy != 0.0f || dx != 0.0f) {
                    const float halftan = tanf(VrScreenFov * 0.5f * 3.14159265f / 180.0f);
                    const float width = 2.0f * VrScreenDistance * halftan;
                    float dist = VrScreenDistance + dy * 1.5f * dt;
                    if (dist < VR_SCREEN_DISTANCE_MIN) dist = VR_SCREEN_DISTANCE_MIN;
                    if (dist > VR_SCREEN_DISTANCE_MAX) dist = VR_SCREEN_DISTANCE_MAX;
                    float fov = 2.0f * atanf(width / (2.0f * dist)) * 180.0f / 3.14159265f;
                    fov += dx * 25.0f * dt;
                    if (fov < VR_SCREEN_FOV_MIN) fov = VR_SCREEN_FOV_MIN;
                    if (fov > VR_SCREEN_FOV_MAX) fov = VR_SCREEN_FOV_MAX;
                    vr_screen_resize(dist, fov);
                }
                npad->button &= ~(L_TRIG | R_TRIG);
            } else if (changed) {
                vrSettingsSave();
                changed = false;
            }
        }
        // In menus either stick navigates (whichever is pushed further).
        XrVector2f look = right;
        if (menu && left.x * left.x + left.y * left.y >= right.x * right.x + right.y * right.y)
            look = left;
        if (netIsActive() && g_CurrentPlayer && g_CurrentPlayer->mpmenuon) {
            /* Keep page navigation on the left stick and volume on the right.
             * A diagonal adjustment must not also change pages. The left
             * stick's up and down move the LOBBY page's cursor (mpmenu.c), so
             * a mostly vertical push flips no page either. */
            look.x = fabsf(left.y) > fabsf(left.x) ? 0.0f : left.x;
            look.y = right.y;
        }
        npad->stick_x = inputAxisScale((s32)(look.x * 32767.0f),
                cfg->deadzone[cfg->axisMap[0][0]], cfg->sens[cfg->axisMap[0][0]]) / 256;
        npad->stick_y = inputAxisScale((s32)(look.y * 32767.0f),
                cfg->deadzone[cfg->axisMap[0][1]], cfg->sens[cfg->axisMap[0][1]]) / 256;
        // Stereo: the head looks and the right stick turns the body (bondview2.c), so
        // the game's own stick turn and look stay idle. Dead zone as PD's joy_for_vr.
        gevrTurnAxis = 0.0f;
        if (adjusting || (g_gevrStereo && !menu)) {
            npad->stick_x = 0;
            npad->stick_y = 0;
        }
        // The weapon wheels point with the stick on the other hand from their
        // button - A's (issue #10) the off hand's, X's (#56) the gun hand's - and
        // that stick neither moves nor turns while one is up. "left" is the move
        // stick and "right" the turn stick, on whichever hands Swap sticks puts them.
        const bool panelOnMoveStick = gevrWeaponPanelOpen && ((gevrWeaponPanelLeft != 0) == (VrSwapJoysticks != 0));
        const bool panelOnTurnStick = gevrWeaponPanelOpen && !panelOnMoveStick;
        gevrWeaponPanelStickX = panelOnMoveStick ? left.x : panelOnTurnStick ? right.x : 0.0f;
        gevrWeaponPanelStickY = panelOnMoveStick ? left.y : panelOnTurnStick ? right.y : 0.0f;
        if (g_gevrStereo && !menu && !adjusting && !panelOnTurnStick) {
            const float dz = 0.15f;
            float x = right.x;
            if (fabsf(x) < dz) x = 0.0f;
            else x = (x - (x > 0.0f ? dz : -dz)) / (1.0f - dz);
            gevrTurnAxis = x;
        }
        if (!menu && !panelOnMoveStick) {
            // Solitaire: C directions move; while aiming down/up crouches/stands,
            // or with the sniper zooms - and then side to side does not strafe
            // the scope off its target (issue #58, user).
            const bool zoomStick = gevrScopeZoomStick() != 0;
            if (!zoomStick && left.x < -0.25f) npad->button |= L_CBUTTONS;
            if (!zoomStick && left.x >  0.25f) npad->button |= R_CBUTTONS;
            if (left.y < -0.25f) npad->button |= D_CBUTTONS;
            if (left.y >  0.25f) npad->button |= U_CBUTTONS;
        }
    }

#ifdef ANDROID
    /* Menu owns A/B, triggers and sticks. Co-op uses this panel without
     * entering the campaign watch or pausing the other three headsets. */
    {
        static int startHeld;
        static GevrPauseInputState pauseInput;
        if(netIsActive() && !inMenus && idx==localSlot) {
            const int start=(npad->button & START_BUTTON)!=0;
            if(start && !startHeld && g_playerPointers[localSlot]) {
                g_playerPointers[localSlot]->mpmenuon=!g_playerPointers[localSlot]->mpmenuon;
                g_gevrWatchGesturePending=0;
            }
            startHeld=start;
            /* Own the whole synthesized pulse, including after closing. A
             * later poll must not send its remaining START to the old menu. */
            npad->button&=~START_BUTTON;
        } else startHeld=0;
        const int open=gevrNativePauseOpen();
        const int fire=get_button_state(0,"trigger") || get_button_state(1,"trigger");
        const int blockFire=gevrPauseBlocksFire(&pauseInput,open,fire);
        if(open) {
            npad->button&=START_BUTTON;
            npad->stick_x=npad->stick_y=npad->rstick_x=npad->rstick_y=0;
            gevrTurnAxis=0;gevrVrTriggerDown[0]=gevrVrTriggerDown[1]=0;
        } else if(blockFire) {npad->button&=~(Z_TRIG|R_TRIG);gevrVrTriggerDown[0]=gevrVrTriggerDown[1]=0;}
    }
#endif
    /* a spectator, or (co-op, #94) a downed player waiting for a teammate: looks, no more */
    if ((gevrSpectating() || gevrCoopLocalDowned()) && g_CurrentPlayer && !g_CurrentPlayer->mpmenuon) {
        npad->stick_x = npad->stick_y = npad->rstick_x = npad->rstick_y = 0;
        npad->button &= ~(Z_TRIG | A_BUTTON | B_BUTTON | L_CBUTTONS | R_CBUTTONS | U_CBUTTONS | D_CBUTTONS);
        gevrTurnAxis = 0;
        if (gevrCoopLocalDowned()) gevrVrTriggerDown[0] = gevrVrTriggerDown[1] = 0;
    }
    if (npad->button != 0 || npad->stick_x != 0 || npad->stick_y != 0 || npad->rstick_x != 0 || npad->rstick_y != 0) {
        netTouchLocalActivity();
    }
    if (idx == localSlot) {
        const unsigned held = (get_button_state(1, "b") ? 1u : 0u)
                            | (get_button_state(0, "y") ? 2u : 0u);
        unsigned allowed = reloadGameplay
            && g_playerPointers[localSlot] && !g_playerPointers[localSlot]->bonddead
            && !gevrSpectating() && !gevrCoopLocalDowned() ? 3u : 0u;
#ifdef ANDROID
        if (gevrNativePauseOpen()) allowed = 0;
#endif
        /* Menu + physical B belongs to the microphone, in either handedness. */
        if (s_menuHeld) allowed &= ~(VrLeftHandedMode ? 2u : 1u);
        const unsigned context = 1u + (unsigned)bossGetStageNum() * 32u
            + (unsigned)localSlot * 4u + (VrLeftHandedMode ? 2u : 0u) + (VrPlayMode ? 1u : 0u);
        gevrReloadInputUpdate(&s_gevrReloadInput, held, allowed, context);
    }
    return 0;
}

static inline void inputUpdateMouse(void)
{
    s32 mx, my;
    mouseButtons = SDL_GetMouseState(&mx, &my);

    if (mouseWheel > 0) {
        mouseButtons |= WHEEL_UP_MASK;
    } else if (mouseWheel < 0) {
        mouseButtons |= WHEEL_DN_MASK;
    }

    mouseWheel = 0;

    s32 mdx = 0;
    s32 mdy = 0;
    SDL_GetRelativeMouseState(&mdx, &mdy);
    if (mouseLocked) {
        mouseDX = mdx;
        mouseDY = mdy;
    } else {
        mouseDX = mx - mouseX;
        mouseDY = my - mouseY;
    }

    mouseX = mx;
    mouseY = my;

    // if MLOCK_AUTO is enabled, disable cursor if mouse is unlocked
    // and we haven't moved it for a few seconds
    if (mouseLockMode == MLOCK_AUTO && !mouseLocked) {
        if (abs(mouseDX) > CURSOR_HIDE_THRESHOLD || abs(mouseDY) > CURSOR_HIDE_THRESHOLD) {
            if (!mouseShowCursor) {
                inputMouseShowCursor(1);
            }
        } else if (sysGetMicroseconds() > mouseCursorTime) {
            if (mouseShowCursor) {
                inputMouseShowCursor(0);
            }
        }
    }
}



void inputUpdate(void)
{
    SDL_GameControllerUpdate();


    if (mouseEnabled) {
        // inputUpdateMouse(); Removed for VR
    }
}

s32 inputControllerConnected(s32 idx)
{
    if (idx < 0 || idx >= INPUT_MAX_CONTROLLERS) {
        return 0;
    }
    if (netIsActive()) {
        /* co-op (#94): the party's menus read this headset's controller as the first */
        if (bossGetStageNum() == 90 /* LEVELID_TITLE */) return idx == 0;
        return (idx == netGetLocalSlot() || netIsRemotePlayerActive(idx)) ? 1 : 0;
    }
    return pads[idx] || (connectedMask & (1 << idx));
}


s32 inputRumbleSupported(s32 idx) {
    if (idx < 0 || idx >= INPUT_MAX_CONTROLLERS) {
        return 0;
    }

    // Player 1 VR haptics
    if (idx == 0) {
        return 1;
    }

    return padsCfg[idx].rumbleOn;
}

void inputRumble(s32 idx, f32 strength, f32 time) {
    if (idx < 0 || idx >= INPUT_MAX_CONTROLLERS) {
        return;
    }

    if (padsCfg[idx].rumbleScale <= 0.f) {
        return;
    }

    // === VR HAPTICS Player 1 ===
    /* vr_init_done belongs to pdmain.c's VR loop, which screen mode does not
     * run, so it stayed false and no rumble ever reached the controllers. */
    if (vr_haptics_ready() && idx == 0) {
        strength *= padsCfg[idx].rumbleScale;

        /*
         * The rumble pak shim asks for 5 s at full strength on every motor
         * start ("hope someone turns it off"), and the branch below sends it to
         * both controllers. The left controller has been dropping off the
         * headset until its battery is pulled; haptics are the only thing sent
         * to it. Keep each pulse short and never repeat an identical command
         * within 200 ms.
         */
        {
            static f32 lastStrength = -1.f;
            static u64 lastUs = 0;
            const u64 nowUs = sysGetMicroseconds();

            if (strength == lastStrength && nowUs - lastUs < 200000) {
                return;
            }
            lastStrength = strength;
            lastUs = nowUs;
            if (time > 0.3f) {
                time = 0.3f;
            }
        }

        s32 handRight = HAND_RIGHT;
        s32 handLeft  = HAND_LEFT;

        s32 cur_weapon = g_CurrentPlayer ? g_CurrentPlayer->equipcuritem : 0;
        // Inversion des mains si arme = LASER
        if (cur_weapon == WEAPON_LASER) {
            handRight = HAND_LEFT;
            handLeft  = HAND_RIGHT;
        }

        // Support hand for a two-handed grip, same mapping vrBuildGunRotation uses: the
        // controller that is not the trigger hand. Only meaningful while it is actually
        // gripping -- an idle off hand is not touching the weapon and should stay quiet.
        const s32  supportHand    = vr_invert_hands ? 1 : 0;
        const bool supportOnWeapon = gevrStereoTwoHandGrip() != 0;   /* issue #35: the off hand at the gun */

        if (strength > 0.f) {
            if (strength > 1.f) strength = 1.f;

            if (bgunIsFiring(handRight)) {
                vr_right_gun_fire = 5;
            }
            if (bgunIsFiring(handLeft)) {
                vr_left_gun_fire = 5;
            }

            if (bgunIsFiring(handRight) && vr_right_gun_fire > 0) {
                trigger_haptic_vibration_c(1, strength, time);
                // A two-handed weapon lives in a single hand slot, so only the controller
                // holding it ever pulsed. A gripping support hand is on the same weapon
                // taking the same recoil, so mirror the pulse onto it -- driven off the
                // firing hand's own decay, so a burst reads as one weapon and not two.
                if (supportOnWeapon) {
                    trigger_haptic_vibration_c(supportHand, strength, time);
                }
            }
            if (vr_right_gun_fire > 0) {
                vr_right_gun_fire--;
            }

            if (bgunIsFiring(handLeft) && vr_left_gun_fire > 0) {
                trigger_haptic_vibration_c(0, strength, time);
            }
            if (vr_left_gun_fire > 0) {
                vr_left_gun_fire--;
            }

            if (!bgunIsFiring(handRight) && !bgunIsFiring(handLeft) && vr_right_gun_fire == 0 && vr_left_gun_fire == 0) {
                trigger_haptic_vibration_c(1, strength, time);
                trigger_haptic_vibration_c(0, strength, time);
            }
        } else {
            stop_haptic_vibration_c(0);
            stop_haptic_vibration_c(1);
        }
        return;
    }

    if (!pads[idx]) {
        return;
    }

    if (padsCfg[idx].rumbleOn) {
        strength *= padsCfg[idx].rumbleScale;
        if (strength > 0.f) {
            strength *= 65535.f;
            time *= 1000.f;
        } else {
            strength = 0.f;
            time = 0.f;
        }
        SDL_GameControllerRumble(pads[idx], (u16)strength, (u16)strength, (u32)time);
    }
}




f32 inputRumbleGetStrength(s32 cidx)
{
    return padsCfg[cidx].rumbleScale;
}

void inputRumbleSetStrength(s32 cidx, f32 val)
{
    padsCfg[cidx].rumbleScale = val;
}

struct WeaponRumbleProfile {
    f32 amplitude; // Base amplitude 0..1 (value / 10.0f)
    f32 duration;  // Seconds
    f32 frequency; // Hz
};

/* Perfect Dark VR's recoil profile for a GoldenEye gun, by the nearest PD gun
 * (gevr_recoil.h), and its share of that kick: the DD44 takes the magnum's at
 * half strength, and a silenced gun kicks a little less than its plain one
 * (user). The watch's items fire from the other arm and do not kick. */
#define GEVR_RECOIL_SILENCED 0.85f

static int gevrRecoilFor(s32 item, f32 *strength)
{
    *strength = 1.0f;
    switch (item) {
        case ITEM_TT33:
            *strength = 0.5f;
            return GEVR_RECOIL_MAGNUM;
        case ITEM_WPPKSIL:
            *strength = GEVR_RECOIL_SILENCED;
            return GEVR_RECOIL_PISTOL;
        case ITEM_MP5KSIL:
            *strength = GEVR_RECOIL_SILENCED;
            return GEVR_RECOIL_SMG;
        case ITEM_WPPK: case ITEM_SILVERWPPK: case ITEM_GOLDWPPK:
            return GEVR_RECOIL_PISTOL;
        case ITEM_RUGER: case ITEM_GOLDENGUN:
            return GEVR_RECOIL_MAGNUM;
        case ITEM_SKORPION: case ITEM_UZI: case ITEM_MP5K:
        case ITEM_SPECTRE: case ITEM_FNP90:
            return GEVR_RECOIL_SMG;
        case ITEM_AK47: case ITEM_M16:
            return GEVR_RECOIL_RIFLE;
        case ITEM_SHOTGUN: case ITEM_AUTOSHOT:
            return GEVR_RECOIL_SHOTGUN;
        case ITEM_SNIPERRIFLE:
            return GEVR_RECOIL_SNIPER;
        case ITEM_ROCKETLAUNCH: case ITEM_GRENADELAUNCH:
            return GEVR_RECOIL_LAUNCHER;
        case ITEM_LASER:
            return GEVR_RECOIL_LASER;
        case ITEM_TASER:
            return GEVR_RECOIL_TASER;
        default:
            return GEVR_RECOIL_NONE;
    }
}

static struct WeaponRumbleProfile getWeaponRumbleProfile(s32 item_id) {
    struct WeaponRumbleProfile p;
    vrHapticsGetRumble(item_id, &p.amplitude, &p.duration, &p.frequency);
    return p;
}

void gevrRumbleGunfire(s32 hand, s32 item_id) {
    extern int netGetLocalSlot(void);
    extern s32 get_cur_playernum(void);
    /* Another player's copy fires on this headset too: only the local
     * player's own gun reaches the controllers. */
    if (netIsActive() && get_cur_playernum() != netGetLocalSlot()) {
        return;
    }
    /* per-gun recoil: the shot kicks this hand's grip pose (vr_input.cpp vrRecoilKick) */
    if (g_gevrStereo && VrPerWeaponRecoil) {
        f32 strength;
        const int cls = gevrRecoilFor(item_id, &strength);
        vrRecoilKick(hand, cls, strength);
    }
    struct WeaponRumbleProfile p = getWeaponRumbleProfile(item_id);
    f32 amp = p.amplitude;
    if (amp <= 0.001f || p.duration <= 0.001f) {
        return;
    }

    // VR Haptics
    if (vr_haptics_ready()) {
        // GUNRIGHT = 0 -> OpenXR 1 (right hand)
        // GUNLEFT  = 1 -> OpenXR 0 (left hand)
        s32 targetHand = (hand == 1) ? 0 : 1;
        if (vr_invert_hands) {
            targetHand = 1 - targetHand;
        }
        // Issue #64 (tester): the watch laser and the detonator are the
        // watch's, on the other wrist (bondview2.c gevrStereoWatchItem), so
        // their rumble goes to the arm that wears it, not the gun hand.
        if (hand == 0 && gevrStereoWatchItem(item_id)) {
            targetHand = 1 - targetHand;
        }

        trigger_haptic_vibration_freq_c(targetHand, amp, p.duration, p.frequency);

        // Two-handed grip support: if gripping with off hand, mirror recoil with 60% strength
        if (gevrStereoTwoHandGrip() != 0) {
            s32 supportHand = 1 - targetHand;
            trigger_haptic_vibration_freq_c(supportHand, amp * 0.6f, p.duration, p.frequency);
        }
    }

    // Gamepad controller rumble
    if (pads[0] && padsCfg[0].rumbleOn && padsCfg[0].rumbleScale > 0.f) {
        f32 padAmp = amp * padsCfg[0].rumbleScale;
        SDL_GameControllerRumble(pads[0], (u16)(padAmp * 65535.f), (u16)(padAmp * 65535.f), (u32)(p.duration * 1000.f));
    }
}

s32 s_gevrExplosionDamage = 0;

void gevrRumbleDamage(f32 damage_amount, s32 is_explosion) {
    (void)damage_amount;
    f32 base_amp = 0.0f, base_dur = 0.0f, freq = 0.0f;
    s32 action_id = is_explosion ? GEVR_ACTION_DAMAGE_EXPLOSION : GEVR_ACTION_DAMAGE_BULLET;
    vrHapticsGetRumble(action_id, &base_amp, &base_dur, &freq);

    if (base_amp <= 0.001f || base_dur <= 0.001f) {
        return;
    }

    // VR Haptics: pulse both controllers simultaneously for full-body impact
    if (vr_haptics_ready()) {
        trigger_haptic_vibration_freq_c(0, base_amp, base_dur, freq);
        trigger_haptic_vibration_freq_c(1, base_amp, base_dur, freq);
    }

    // Gamepad controller rumble
    if (pads[0] && padsCfg[0].rumbleOn && padsCfg[0].rumbleScale > 0.f) {
        f32 padAmp = base_amp * padsCfg[0].rumbleScale;
        SDL_GameControllerRumble(pads[0], (u16)(padAmp * 65535.f), (u16)(padAmp * 65535.f), (u32)(base_dur * 1000.f));
    }
}

s32 inputControllerMask(void)
{
    if (netIsActive()) {
        s32 mask = 0;
        if (bossGetStageNum() == 90 /* LEVELID_TITLE: the party's menus */) return 1;
        for (int i = 0; i < INPUT_MAX_CONTROLLERS; ++i) {
            if (i == netGetLocalSlot() || netIsRemotePlayerActive(i)) {
                mask |= (1 << i);
            }
        }
        return mask ? mask : 1;
    }
    return connectedMask;
}

s32 inputControllerGetSticksSwapped(s32 cidx)
{
    return padsCfg[cidx].swapSticks;
}

void inputControllerSetSticksSwapped(s32 cidx, s32 swapped)
{
    padsCfg[cidx].swapSticks = swapped;
    if (swapped) {
        padsCfg[cidx].axisMap[0][0] = SDL_CONTROLLER_AXIS_RIGHTX;
        padsCfg[cidx].axisMap[0][1] = SDL_CONTROLLER_AXIS_RIGHTY;
        padsCfg[cidx].axisMap[1][0] = SDL_CONTROLLER_AXIS_LEFTX;
        padsCfg[cidx].axisMap[1][1] = SDL_CONTROLLER_AXIS_LEFTY;
    } else {
        padsCfg[cidx].axisMap[0][0] = SDL_CONTROLLER_AXIS_LEFTX;
        padsCfg[cidx].axisMap[0][1] = SDL_CONTROLLER_AXIS_LEFTY;
        padsCfg[cidx].axisMap[1][0] = SDL_CONTROLLER_AXIS_RIGHTX;
        padsCfg[cidx].axisMap[1][1] = SDL_CONTROLLER_AXIS_RIGHTY;
    }
}

s32 inputControllerGetDualAnalog(s32 cidx)
{
    return !padsCfg[cidx].stickCButtons;
}

void inputControllerSetDualAnalog(s32 cidx, s32 enable)
{
    padsCfg[cidx].stickCButtons = !enable;
}

s32 inputControllerGetCancelCButtons(s32 cidx)
{
    return padsCfg[cidx].cancelCButtons;
}

void inputControllerSetCancelCButtons(s32 cidx, s32 cancel)
{
    padsCfg[cidx].cancelCButtons = cancel;
}

f32 inputControllerGetAxisScale(s32 cidx, s32 stick, s32 axis)
{
    return padsCfg[cidx].sens[stick * 2 + axis];
}

void inputControllerSetAxisScale(s32 cidx, s32 stick, s32 axis, f32 value)
{
    padsCfg[cidx].sens[stick * 2 + axis] = value;
}

f32 inputControllerGetAxisDeadzone(s32 cidx, s32 stick, s32 axis)
{
    return (f32)padsCfg[cidx].deadzone[stick * 2 + axis] / 32767.f;
}

void inputControllerSetAxisDeadzone(s32 cidx, s32 stick, s32 axis, f32 value)
{
    padsCfg[cidx].deadzone[stick * 2 + axis] = value * 32767.f;
}

s32 inputGetConnectedControllers(s32 *out)
{
    s32 count = 0;

    for (s32 jidx = 0; jidx < numJoysticks; ++jidx) {
        if (SDL_IsGameController(jidx)) {
            if (out && count < INPUT_MAX_CONNECTED_CONTROLLERS) {
                out[count] = SDL_JoystickGetDeviceInstanceID(jidx);
            }
            ++count;
        }
    }

    return count;
}

s32 inputGetAssignedControllerId(s32 cidx)
{
    if (cidx < 0 || cidx >= INPUT_MAX_CONTROLLERS) {
        return -1;
    }

    if (pads[cidx] == NULL) {
        return -1;
    }

    return inputControllerGetId(pads[cidx]);
}

const char *inputGetConnectedControllerName(s32 id)
{
    static char fullName[256];

    if (id < 0) {
        return "Invalid";
    }

    const s32 jidx = inputDeviceIndexFromId(id);
    if (jidx < 0) {
        return "Invalid";
    }

    const char *name = SDL_GameControllerNameForIndex(jidx);
    if (!name || !name[0]) {
        name = "Unnamed Controller";
    }

    snprintf(fullName, sizeof(fullName), "%d: %s", jidx, name);

    for (char *p = fullName; *p; ++p) {
        if ((u32)*p >= 0x7f) {
            *p = ' ';
        }
    }

    return fullName;
}

s32 inputAssignController(s32 cidx, s32 id)
{
    if (cidx < 0 || cidx >= INPUT_MAX_CONTROLLERS) {
        return 0;
    }

    if (id < 0) {
        // close current controller, if any
        if (pads[cidx]) {
            inputCloseController(cidx);
            return 1;
        }
        return 0;
    }

    const s32 jidx = inputDeviceIndexFromId(id);
    if (jidx < 0 || jidx >= SDL_NumJoysticks() || !SDL_IsGameController(jidx)) {
        return 0;
    }

    // try to unassign any other instances of this controller
    for (s32 i = 0; i < INPUT_MAX_CONTROLLERS; ++i) {
        if (pads[i] && inputControllerGetId(pads[i]) == id) {
            inputCloseController(i);
            pads[i] = NULL;
            padsCfg[i].deviceIndex = -1;
        }
    }

    SDL_GameController *newpad = SDL_GameControllerOpen(jidx);
    if (!newpad) {
        return 0;
    }

    if (pads[cidx]) {
        inputCloseController(cidx);
    }

    pads[cidx] = newpad;
    inputInitController(cidx, id);

    return 1;
}

void inputKeyBind(s32 idx, u32 ck, s32 bind, u32 vk)
{
    if (idx < 0 || idx >= INPUT_MAX_CONTROLLERS || bind >= INPUT_MAX_BINDS || ck >= CK_TOTAL_COUNT) {
        return;
    }

    if (bind < 0) {
        for (s32 i = 0; i < INPUT_MAX_BINDS; ++i) {
            if (binds[idx][ck][i] == 0) {
                bind = i;
                break;
            }
        }
        if (bind < 0) {
            bind = INPUT_MAX_BINDS - 1; // just overwrite last
        }
    }

    binds[idx][ck][bind] = vk;
}

const u32 *inputKeyGetBinds(s32 idx, u32 ck)
{
    if (idx < 0 || idx >= INPUT_MAX_CONTROLLERS || ck >= CK_TOTAL_COUNT) {
        return NULL;
    }
    return binds[idx][ck];
}

s32 inputKeyPressed(u32 vk)
{
    if (vk >= VK_KEYBOARD_BEGIN && vk < VK_MOUSE_BEGIN) {
        const u8 *state = SDL_GetKeyboardState(NULL);
        return state[vk - VK_KEYBOARD_BEGIN];
    }

    if (vk >= VK_MOUSE_BEGIN && vk < VK_JOY_BEGIN) {
        return (mouseButtons & SDL_BUTTON(vk - VK_MOUSE_BEGIN + 1)) != 0;
    }


    // VR
    if (vk >= VK_VR_BEGIN && vk < VK_VR_END) {
        switch (vk) {
            case VK_VR_LEFT_TRIGGER: return get_button_state(0, "trigger");
            case VK_VR_LEFT_GRIP: return get_button_state(0, "grip");
            case VK_VR_LEFT_X: return get_button_state(0, "x");
            case VK_VR_LEFT_Y: return get_button_state(0, "y");
            case VK_VR_LEFT_MENU: return get_button_state(0, "menu");
            case VK_VR_LEFT_THUMBSTICK_CLICK: return get_button_state(0, "thumbstick_click");

            case VK_VR_RIGHT_TRIGGER: return get_button_state(1, "trigger");
            case VK_VR_RIGHT_GRIP: return get_button_state(1, "grip");
            case VK_VR_RIGHT_A: return get_button_state(1, "a");
            case VK_VR_RIGHT_B: return get_button_state(1, "b");
            case VK_VR_RIGHT_THUMBSTICK_CLICK: return get_button_state(1, "thumbstick_click");

            default: return 0;
        }
    }


    return 0;
}

s32 inputKeyJustPressed(u32 vk)
{
    const s8 pressed = inputKeyPressed(vk);
    const s32 result = pressed && !vkPrevState[vk];
    vkPrevState[vk] = pressed;
    return result;
}

static inline u32 inputContToContKey(const u32 cont)
{
    if (cont == 0) {
        return 0;
    }
    // just a log2 to convert CONT_* to their indices
    return 32 - __builtin_clz(cont - 1);
}

s32 inputButtonPressed(s32 idx, u32 contbtn)
{
    if (idx < 0 || idx >= INPUT_MAX_CONTROLLERS) {
        return 0;
    }

    return inputBindPressed(idx, inputContToContKey(contbtn));
}

void inputLockMouse(s32 lock)
{
    mouseLocked = !!lock;
    SDL_SetRelativeMouseMode(mouseLocked);
}

s32 inputMouseIsLocked(void)
{
    return mouseLocked;
}

s32 inputMouseGetPosition(s32 *x, s32 *y)
{
    if (x) *x = mouseX * videoGetNativeWidth() / videoGetWidth();
    if (y) *y = mouseY * videoGetNativeHeight() / videoGetHeight();
    return (mouseDX != 0 || mouseDY != 0);
}

void inputMouseGetRawDelta(s32 *dx, s32 *dy)
{
    if (dx) *dx = mouseDX;
    if (dy) *dy = mouseDY;
}

void inputMouseGetScaledDelta(f32* dx, f32* dy)
{
    f32 mdx = 0.f, mdy = 0.f;
    if (mouseLocked) {
        mdx = mouseDX * (0.022f / 3.5f) * mouseSensX;
        mdy = mouseDY * (0.022f / 3.5f) * mouseSensY;
    }
    if (dx) *dx = mdx;
    if (dy) *dy = mdy;
}

void inputMouseGetAbsScaledDelta(f32* dx, f32* dy)
{
    f32 mdx = 0.f, mdy = 0.f;
    if (mouseLocked) {
        mdx = mouseDX * (0.022f / 3.5f) * fabsf(mouseSensX);
        mdy = mouseDY * (0.022f / 3.5f) * fabsf(mouseSensY);
    }
    if (dx) *dx = mdx;
    if (dy) *dy = mdy;
}

void inputMouseGetSpeed(f32 *x, f32 *y)
{
    *x = mouseSensX;
    *y = mouseSensY;
}

void inputMouseSetSpeed(f32 x, f32 y)
{
    mouseSensX = x;
    mouseSensY = y;
}

s32 inputMouseIsEnabled(void)
{
//    return mouseEnabled;
    return 0;
}

void inputMouseEnable(s32 enabled)
{
//    mouseEnabled = !!enabled;
//    if (!mouseEnabled && mouseLockMode != MLOCK_ON && mouseLocked) {
//        inputLockMouse(0);
//    }
}

s32 inputAutoLockMouse(s32 wantlock)
{
//    if (mouseEnabled && mouseLockMode == MLOCK_AUTO) {
//        inputLockMouse(wantlock);
//        return 1;
//    }
    return 0;
}

void inputMouseShowCursor(s32 show)
{
    mouseShowCursor = !!show;
    SDL_ShowCursor(mouseShowCursor);
    if (show) {
        mouseCursorTime = sysGetMicroseconds() + CURSOR_HIDE_TIME;
    }
}

s32 inputGetMouseLockMode(void)
{
    return mouseLockMode;
}

void inputSetMouseLockMode(s32 lockmode)
{
    mouseLockMode = lockmode;
    if (lockmode == MLOCK_ON) {
        inputLockMouse(1);
    } else {
        inputLockMouse(0);
    }
}

const char *inputGetContKeyName(u32 ck)
{
    if (ck >= CK_TOTAL_COUNT) {
        return "";
    }
    return ckNames[ck];
}

s32 inputGetContKeyByName(const char *name)
{
    for (u32 i = 0; i < CK_TOTAL_COUNT; ++i) {
        if (!strcmp(name, ckNames[i])) {
            return i;
        }
    }
    sysLogPrintf(LOG_WARNING, "unknown bind name: `%s`", name);
    return -1;
}

const char *inputGetKeyName(s32 vk)
{
    if (vk < 0 || vk >= VK_TOTAL_COUNT) {
        vk = 0;
    }
    if (!vkNames[vk][0]) {
        snprintf(vkNames[vk], sizeof(vkNames[vk]), "UNKNOWN%d", vk);
    }
    return vkNames[vk];
}

s32 inputGetKeyByName(const char *name)
{
    s32 start = 0;
    s32 end = 0;

    if (!strncmp(name, "JOY", 3) && isdigit(name[3])) {
        const s32 idx = name[3] - '1';
        if (idx >= 0 && idx < INPUT_MAX_CONTROLLERS) {
            start = VK_JOY1_BEGIN + idx * INPUT_MAX_CONTROLLER_BUTTONS;
            end = start + INPUT_MAX_CONTROLLER_BUTTONS;
        }
    } else if (!strncmp(name, "MOUSE", 5)) {
        start = VK_MOUSE_BEGIN;
        end = VK_JOY1_BEGIN;
    } else if (!strncmp(name, "UNKNOWN", 7) && isdigit(name[7])) {
        const s32 key = atoi(name + 7);
        if (key >= 0 && key < VK_TOTAL_COUNT) {
            return key;
        }
    } else {
        end = VK_MOUSE_BEGIN;
    }

    for (s32 i = start; i < end; ++i) {
        if (!strcmp(vkNames[i], name)) {
            return i;
        }
    }

    sysLogPrintf(LOG_WARNING, "unknown key name: `%s`", name);

    return -1;
}

void inputClearLastKey(void)
{
    lastKey = 0;
}

s32 inputGetLastKey(void)
{
    return lastKey;
}

void inputStartTextInput(void)
{
    lastChar = 0;
    lastKey = 0;
    textInput = 1;
    SDL_StartTextInput();
}

void inputClearLastTextChar(void)
{
    lastChar = 0;
}

char inputGetLastTextChar(void)
{
    return lastChar;
}

static inline s32 filterChar(const char ch)
{
    return isalnum(ch) || ch == ' ' || ch == '?' || ch == '!' || ch == '.';
}

s32 inputTextHandler(char *out, const u32 outSize, s32 *curCol, s32 oskCharsOnly)
{
    const s32 ctrlHeld = inputGetKeyModState() & KM_CTRL;

    if (!ctrlHeld) {
        const char chr = inputGetLastTextChar();
        inputClearLastTextChar();
        const s32 valid = chr && (oskCharsOnly ? filterChar(chr) : isprint(chr));
        if (valid) {
            if (*curCol < outSize - 1) {
                out[(*curCol)++] = chr;
                out[*curCol] = '\0';
            }
        }
    }

    const s32 key = inputGetLastKey();
    inputClearLastKey();
    if (ctrlHeld && (key == VK_A + ('v' - 'a'))) {
        // CTRL+V; paste from clipboard
        const char *clip = inputGetClipboard();
        if (clip) {
            const s32 remain = outSize - *curCol - 1;
            inputClearClipboard();
            *curCol += snprintf(out + *curCol, remain, "%s", clip);
            if (*curCol > outSize) {
                *curCol = outSize;
            }
        }
    } else if (key == VK_BACKSPACE) {
        if (*curCol) {
            out[--*curCol] = '\0';
        } else {
            out[0] = '\0';
        }
    } else if (key == VK_RETURN) {
        if (out[0] && *curCol) {
            return 1;
        }
    } else if (key == VK_ESCAPE) {
        return -1;
    }

    return 0;
}

void inputClearClipboard(void)
{
    if (clipboardText) {
        SDL_free(clipboardText);
        clipboardText = NULL;
    }
}

const char *inputGetClipboard(void)
{
    if (!clipboardText) {
        char *text = SDL_GetClipboardText();
        if (text) {
            clipboardText = text;
            // remove non-printable and multibyte chars
            for (; *text; ++text) {
                if ((u8)*text < 0x20 || (u8)*text >= 0x7F) {
                    *text = '?';
                }
            }
        }
    }
    return clipboardText;
}

void inputStopTextInput(void)
{
    SDL_StopTextInput();
    textInput = 0;
}

s32 inputIsTextInputActive(void)
{
    return textInput;
}

u32 inputGetKeyModState(void)
{
    return SDL_GetModState();
}



void inputSetupVRBindings(s32 cidx) {  // VR
    if (cidx < 0 || cidx >= INPUT_MAX_CONTROLLERS) return;

    inputKeyBind(cidx, CK_A, 0, VK_VR_RIGHT_A);
    inputKeyBind(cidx, CK_B, 0, VK_VR_RIGHT_B);
    //inputKeyBind(cidx, CK_X, 0, VK_VR_RIGHT_B);
    // inputKeyBind(cidx, CK_ZTRIG, 0, VK_VR_RIGHT_TRIGGER);
    inputKeyBind(cidx, CK_START, 0, VK_VR_LEFT_MENU);
    inputKeyBind(cidx, CK_Y, 0, VK_VR_RIGHT_A);
    inputKeyBind(cidx, CK_LTRIG, 0, VK_VR_RIGHT_THUMBSTICK_CLICK);
    inputKeyBind(cidx, CK_DPAD_D, 0, VK_VR_LEFT_THUMBSTICK_CLICK);
}





PD_CONSTRUCTOR static void inputConfigInit(void)
{
    configRegisterInt("Input.MouseEnabled", &mouseEnabled, 0, 1);
    configRegisterInt("Input.MouseLockMode", &mouseLockMode, MLOCK_OFF, MLOCK_AUTO);
    configRegisterFloat("Input.MouseSpeedX", &mouseSensX, -30.f, 30.f);
    configRegisterFloat("Input.MouseSpeedY", &mouseSensY, -30.f, 30.f);
    configRegisterInt("Input.FakeGamepads", &fakeControllers, 0, 4);
    configRegisterInt("Input.FirstGamepadNum", &firstController, 0, 3);
    configRegisterInt("Input.UseHIDAPI", &useHIDAPI, 0, 1);
    configRegisterInt("Input.UseRawInput", &useRawInput, 0, 1);

    char secname[] = "Input.Player1.Binds";
    char keyname[256] = { 0 };
    for (s32 c = 0; c < MAXCONTROLLERS; ++c) {
        secname[12] = '1' + c;
        secname[13] = '\0';
        configRegisterFloat(strFmt("%s.RumbleScale", secname), &padsCfg[c].rumbleScale, 0.f, 1.f);
        configRegisterInt(strFmt("%s.LStickDeadzoneX", secname), &padsCfg[c].deadzone[0], 0, 32767);
        configRegisterInt(strFmt("%s.LStickDeadzoneY", secname), &padsCfg[c].deadzone[1], 0, 32767);
        configRegisterInt(strFmt("%s.RStickDeadzoneX", secname), &padsCfg[c].deadzone[2], 0, 32767);
        configRegisterInt(strFmt("%s.RStickDeadzoneY", secname), &padsCfg[c].deadzone[3], 0, 32767);
        configRegisterFloat(strFmt("%s.LStickScaleX", secname), &padsCfg[c].sens[0], -10.f, 10.f);
        configRegisterFloat(strFmt("%s.LStickScaleY", secname), &padsCfg[c].sens[1], -10.f, 10.f);
        configRegisterFloat(strFmt("%s.RStickScaleX", secname), &padsCfg[c].sens[2], -10.f, 10.f);
        configRegisterFloat(strFmt("%s.RStickScaleY", secname), &padsCfg[c].sens[3], -10.f, 10.f);
        configRegisterInt(strFmt("%s.StickCButtons", secname), &padsCfg[c].stickCButtons, 0, 1);
        configRegisterInt(strFmt("%s.CancelCButtons", secname), &padsCfg[c].cancelCButtons, 0, 1);
        configRegisterInt(strFmt("%s.SwapSticks", secname), &padsCfg[c].swapSticks, 0, 1);
        configRegisterInt(strFmt("%s.ControllerIndex", secname), &padsCfg[c].deviceIndex, -1, 0x7FFFFFFF);
        secname[13] = '.';
        for (u32 ck = 0; ck < CK_TOTAL_COUNT; ++ck) {
            snprintf(keyname, sizeof(keyname), "%s.%s", secname, inputGetContKeyName(ck));
            configRegisterString(keyname, bindStrs[c][ck], MAX_BIND_STR);
        }
    }
}

/* vr_input.cpp controller_pose: the multiplayer pause menu is up, hold the hands still */
int gevrMpMenuOpen(void)
{
    /*
     * The local player's menu, by slot: g_CurrentPlayer rotates through the
     * other slots' copies during their passes, and read there the hold
     * flickered on and off every frame (user, 2026-09-30).
     */
    extern bool netIsActive(void);
    extern int netGetLocalSlot(void);
    int slot = netIsActive() ? netGetLocalSlot() : -1;
    return slot >= 0 && slot < INPUT_MAX_SLOT_PADS && g_playerPointers[slot] != NULL && g_playerPointers[slot]->mpmenuon;
}

#ifdef ANDROID
int gevrNativePauseOpen(void) {
    int slot=netIsActive()?netGetLocalSlot():-1;
    return bossGetStageNum()!=90 && slot>=0 && slot<INPUT_MAX_SLOT_PADS && g_playerPointers[slot] && g_playerPointers[slot]->mpmenuon;
}
void gevrNativePauseResume(void) {
    int slot=netIsActive()?netGetLocalSlot():-1;
    if(slot>=0 && slot<INPUT_MAX_SLOT_PADS && g_playerPointers[slot])g_playerPointers[slot]->mpmenuon=0;
}
#endif
