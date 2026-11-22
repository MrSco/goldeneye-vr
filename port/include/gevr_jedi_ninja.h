#ifndef GEVR_JEDI_NINJA_H
#define GEVR_JEDI_NINJA_H

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Jedi and Ninja modes (#270): pure C11 math, gesture, projectile and physics
 * core, kept free of game-state headers and atan2f/acosf/asinf overrides
 * (see port/include/gevr_pull.h). Units are Perfect Dark / GoldenEye world
 * units (roughly centimetres), Y up.
 */

enum {
    GEVR_JN_OFF = 0,
    GEVR_JN_JEDI = 1,
    GEVR_JN_NINJA = 2
};

enum {
    GEVR_JN_COLOR_BLUE = 0,
    GEVR_JN_COLOR_GREEN,
    GEVR_JN_COLOR_RED,
    GEVR_JN_COLOR_PURPLE,
    GEVR_JN_COLOR_YELLOW,
    GEVR_JN_COLOR_WHITE,
    GEVR_JN_COLOR_ORANGE,
    GEVR_JN_COLOR_GOLD,
    GEVR_JN_COLOR_COUNT
};

enum {
    GEVR_JN_SWORD_KATANA = 0,
    GEVR_JN_SWORD_GOLD_KATANA,
    GEVR_JN_SWORD_SHORT,
    GEVR_JN_SWORD_GOLD_SHORT,
    GEVR_JN_SWORD_DUAL_SHORT,
    GEVR_JN_SWORD_DUAL_GOLD_SHORT,
    GEVR_JN_SWORD_COUNT
};

enum {
    GEVR_JN_BOOM_SHURIKEN = 0,
    GEVR_JN_BOOM_WING,
    GEVR_JN_BOOM_GOLD_SHURIKEN,
    GEVR_JN_BOOM_GOLD_WING,
    GEVR_JN_BOOM_COUNT
};

/* Weapon-wheel slots in Jedi mode */
enum {
    GEVR_JN_FORCE_TK = 0,        /* Force Grab / Choke / Pull / Push */
    GEVR_JN_FORCE_LIGHTNING,     /* Force Lightning */
    GEVR_JN_FORCE_THROW,         /* Lightsaber Throw focus */
    GEVR_JN_FORCE_GUNS,          /* Normal GoldenEye guns */
    GEVR_JN_FORCE_COUNT
};

enum {
    GEVR_JN_STYLE_SINGLE = 0,
    GEVR_JN_STYLE_DOUBLE,
    GEVR_JN_STYLE_DUAL,
    GEVR_JN_STYLE_COUNT
};

/* Ninja weapon wheel slots */
enum {
    GEVR_JN_NINJA_SLOT_KATANA = 0,
    GEVR_JN_NINJA_SLOT_SHORT,
    GEVR_JN_NINJA_SLOT_DUAL,
    GEVR_JN_NINJA_SLOT_SHIELD,
    GEVR_JN_NINJA_SLOT_BOOMERANG,
    GEVR_JN_NINJA_SLOT_SHADOW,
    GEVR_JN_NINJA_SLOT_COUNT
};

enum {
    GEVR_JN_SCALE_NORMAL = 0,
    GEVR_JN_SCALE_RANDOM,
    GEVR_JN_SCALE_GIANT,
    GEVR_JN_SCALE_TINY,
    GEVR_JN_SCALE_COUNT
};

#define GEVR_JN_MAX_LIMBS          24
#define GEVR_JN_BODY_FADE_SEC      24.0f
#define GEVR_JN_DEFAULT_BODIES_STAY 24
#define GEVR_JN_IGNITE_SEC         0.42f
#define GEVR_JN_CHOKE_DIST_CM      92.0f
#define GEVR_JN_PULL_RANGE_CM      1400.0f
#define GEVR_JN_GRAB_RANGE_CM      380.0f

/* Palette, weapon metrics and NPC scaling */
void gevrJnSaberPalette(int color, unsigned int *coreRgba, unsigned int *glowRgba);
float gevrJnWeaponReachCm(int mode, int sword);
int gevrJnIsDualSword(int mode, int sword);
int gevrJnIsGoldSword(int sword);
float gevrJnSithScaleFactor(int setting, unsigned int seed);

/* Flat controller & mouse trigger gesture state machine */
enum {
    GEVR_JN_TRIG_NONE = 0,
    GEVR_JN_TRIG_TAP,
    GEVR_JN_TRIG_DOUBLE_TAP,
    GEVR_JN_TRIG_HOLD_START,
    GEVR_JN_TRIG_HOLD_ACTIVE,
    GEVR_JN_TRIG_HOLD_RELEASE
};

typedef struct GevrJnTrigger {
    int down;
    float heldSec;
    int holdFired;   /* 0 none, 1 hold active, 2 suppressed after double-tap */
    float tapTimer;
    int pendingTap;
} GevrJnTrigger;

void gevrJnTriggerReset(GevrJnTrigger *t);
int gevrJnTriggerUpdate(GevrJnTrigger *t, int pressed, float dt);

/* Alternating right-to-left / left-to-right Outcast melee slash */
typedef struct GevrJnSlash {
    int nextSide;    /* 0: right-to-left, 1: left-to-right */
    int active;
    int dir;
    float t;
    float duration;
} GevrJnSlash;

int gevrJnSlashStart(GevrJnSlash *s, int blocking);
float gevrJnSlashStep(GevrJnSlash *s, float dt);

/* VR wrist-snap gesture detection (units: cm/s, unit aimFwd) */
int gevrJnVrWristThrow(const float handVelCmS[3], const float aimFwd[3], int releasedFromHold);
int gevrJnVrWristPull(const float handVelCmS[3], const float aimFwd[3], int triggerHeld);

/* Lightsaber ignition curve */
typedef struct GevrJnIgnition {
    float extend;    /* 0..1 */
    int ignitedOnce;
    int active;
} GevrJnIgnition;

float gevrJnIgnitionUpdate(GevrJnIgnition *ig, int drawn, int isFirstPerson, float dt);

/* JK2 Jedi Outcast Lightsaber Throw & Ninja Boomerang flight */
typedef struct GevrJnThrown {
    int active;
    int isBoomerang;
    int returning;
    float pos[3];
    float vel[3];
    float dir[3];
    float startPos[3];
    float distTraveled;
    float maxRange;
    float speed;
    float spinAngle;
    float spinRate;
    float age;
    float maxAge;
} GevrJnThrown;

void gevrJnThrownLaunch(GevrJnThrown *th, int isBoomerang, const float origin[3], const float aimDir[3]);
int gevrJnThrownStep(GevrJnThrown *th, const float ownerPos[3], int hitWall, float dt);

/* Force Choke, 24-limb orbiting ball, and thrown-body energy/momentum */
void gevrJnChokeTargetPos(const float origin[3], const float fwd[3], float outPos[3]);
void gevrJnLimbOrbitPos(const float center[3], int slot, int count, float timeSec, float outPos[3]);
float gevrJnLimbBlastDamage(float distCm);

enum {
    GEVR_JN_IMPACT_NONE = 0,
    GEVR_JN_IMPACT_KNOCKDOWN,
    GEVR_JN_IMPACT_RAGDOLL_KILL,
    GEVR_JN_IMPACT_DISMEMBER
};

typedef struct GevrJnThrownBody {
    float massKg;
    float speedCmS;
    float energyJ;
    float travelCm;
    int dismemberedCount;
    int hitCount;
} GevrJnThrownBody;

void gevrJnThrownBodyInit(GevrJnThrownBody *tb, float massKg, float speedCmS);
int gevrJnThrownBodyImpact(GevrJnThrownBody *tb, float travelCm, float *outKnockSpeedCmS);

/* Bullet block & deflection cone check */
enum {
    GEVR_JN_BLOCK_NONE = 0,
    GEVR_JN_BLOCK_ABSORB,
    GEVR_JN_BLOCK_DEFLECT
};

int gevrJnCheckBlock(int mode, int blocking, int hasNinjaShield,
                     const float playerFwd[3], const float toShooterDir[3],
                     float outReflectDir[3]);

/* Flat trigger input feed from port/src/input.c */
void gevrJnInputFeedFlat(int rtDown, int ltDown, int isMouse);
void gevrJnInputReadFlat(int *rtDown, int *ltDown, int *isMouse);

/* Vector reflection for corridor boomerang ricochets & directional deflection */
void gevrJnReflectVec(const float v[3], const float n[3], float out[3]);

/* Procedural DSP audio synth (22050 Hz stereo PCM mixer, zero external assets) */
enum {
    GEVR_JN_SFX_IGNITE = 1,
    GEVR_JN_SFX_SLASH_SABER,
    GEVR_JN_SFX_SLASH_STEEL,
    GEVR_JN_SFX_DEFLECT,
    GEVR_JN_SFX_CLASH_SABER,
    GEVR_JN_SFX_CLASH_METAL,
    GEVR_JN_SFX_FORCE_PUSH,
    GEVR_JN_SFX_SHADOW_STEP
};

void gevrJnAudioReset(void);
void gevrJnAudioSetState(int active, int isJedi, float saberExtend, float swingSpeed01,
                         int forceChokeOrPull, int forceLightning,
                         int thrownSpinning, int thrownIsBoomerang);
void gevrJnAudioTrigger(int eventId, float intensity);
void gevrJnAudioMix(short *pcmStereo, int frames, int sampleRate);

#ifdef __cplusplus
}
#endif

#endif
