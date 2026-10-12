#ifndef GEVR_SCOPE_H
#define GEVR_SCOPE_H
/*
 * The VR scope (issue #40), one per hand: bondview2.c gevrScopeBegin fills a
 * slot each game frame for a hand holding a scoped gun, gfx_opengl.cpp draws
 * the world again through its camera, and vr_openxr.cpp shows that image as
 * a lens on the gun. Indexed by the game's hand (GUNRIGHT 0, GUNLEFT 1), not
 * the controller: the gun hand is controller 1 and the off hand 0.
 */
#ifdef __cplusplus
extern "C" {
#endif

typedef struct GevrScopeState
{
    float vp[16];          /* head camera space to the scope's clip space, column-major */
    float headP[2];        /* the head projection's x and y scales */
    float lens[4];         /* the lens from the hand's grip: right, up, back, diameter (m) */
    float origin[3];       /* the scope camera, camera space (gunfire.c sizes its sight) */
    float fovDeg;          /* the angle across the lens */
    float gunOrigin[3];    /* game-frame grip pos in view space (m); lens submission applies the eye replay delta */
    float gunAxes[3][3];   /* game-frame gun right/up/back axes, including two-hand aim */
    int   gunValid;        /* whether gunOrigin/gunAxes are valid */
    /*
     * Issue #58: the lens's radius over its distance from the eyes, as last
     * shown (tan of half the angle it fills). vr_openxr.cpp writes it;
     * gevrScopeBegin sizes the scope's view to it, so it magnifies as the
     * N64's zoom did.
     */
    float lensTan;
} GevrScopeState;

extern GevrScopeState gevrScope[2];
extern int gevrScopeOn;   /* this frame's scopes: bit (1 << hand) */

/*
 * Gun fit's scope trims (user), one per scoped gun in bondview2.c
 * s_gevrScopes' order (sniper rifle, Moonraker laser, KF7, AR33), for
 * GoldenEye's models [0] and GoldenEye X's [1]: cm right, up and back from
 * the eyepiece, then cm wider (goldeneye-vr.ini Scope*, GexScope*).
 */
#define GEVR_SCOPE_FITS 4
extern float VrScopeFit[2][GEVR_SCOPE_FITS][4];

#ifndef GEVR_MAX_WEAPONS
#define GEVR_MAX_WEAPONS 64
#endif
extern float VrMuzzleTrim[2][GEVR_MAX_WEAPONS][3];
extern int gevrMuzzleFitting;
int gevrMuzzleFitAvailable(void);

/*
 * Bean's models keep their own (vr_settings_defaults.c VrBean*). Gun fit's
 * look for a hand's gun is bondview2.c gevrFitLook(): 0 GoldenEye's models,
 * 1 GoldenEye X's, 2 Bean's.
 */
extern float VrBeanScopeFit[GEVR_SCOPE_FITS][4];
extern float VrBeanMuzzleTrim[GEVR_MAX_WEAPONS][3];
int gevrFitLook(int hand);
static inline float *gevrScopeFitOf(int look, int scope)
{
    return look == 2 ? VrBeanScopeFit[scope] : VrScopeFit[look ? 1 : 0][scope];
}
static inline float *gevrMuzzleTrimOf(int look, int item)
{
    return look == 2 ? VrBeanMuzzleTrim[item] : VrMuzzleTrim[look ? 1 : 0][item];
}

/*
 * Per gun (user, 2026-10-12: one pistol's model and another's, and the
 * automatics', put the hand in different places, so a class's fit set on one
 * gun spoiled another): each gun of GoldenEye's models [0] and Bean's [1]
 * keeps its own place in the hand and its own two-handed hold once Gun fit
 * moves it (goldeneye-vr.ini GunFit<gun>, GripFit<gun>, Bean* for Bean's). A
 * gun without its own takes the look's GunOff and Grip<class>. GoldenEye X's
 * guns have theirs (VrGexWeaponFits).
 */
extern float VrGunOffPer[2][GEVR_MAX_WEAPONS][3];
extern float VrGripTrimPer[2][GEVR_MAX_WEAPONS][6];
extern unsigned char VrGunOffPerSet[2][GEVR_MAX_WEAPONS], VrGripTrimPerSet[2][GEVR_MAX_WEAPONS];

/* a look's per-gun set: 0 GoldenEye's, 1 Bean's, -1 none (GoldenEye X's) */
static inline int gevrPerGunSet(int look)
{
    return look == 0 ? 0 : look == 2 ? 1 : -1;
}

/* the gun whose fit this one shares, as GoldenEye X's do (vr_settings.cpp
 * GexFit): the silenced, gold and silver PP7 the PP7's, the silenced D5K the
 * D5K's, the throwing knife the knife's */
static inline int gevrPerGunItem(int item)
{
    return item == 3 ? 2 : item == 5 || item == 20 || item == 21 ? 4 : item == 11 ? 10 : item;
}

/* the gun's own place in the hand, or NULL: it takes the look's shared one */
static inline const float *gevrGunOffOwnOf(int look, int item)
{
    const int s = gevrPerGunSet(look);

    item = gevrPerGunItem(item);
    return s >= 0 && item > 0 && item < GEVR_MAX_WEAPONS && VrGunOffPerSet[s][item] ? VrGunOffPer[s][item] : 0;
}

/* the gun's own two-handed hold, or NULL: it takes its class's */
static inline const float *gevrGripTrimOwnOf(int look, int item)
{
    const int s = gevrPerGunSet(look);

    item = gevrPerGunItem(item);
    return s >= 0 && item > 0 && item < GEVR_MAX_WEAPONS && VrGripTrimPerSet[s][item] ? VrGripTrimPer[s][item] : 0;
}

/* Gun fit moving the gun's own: made from what it shows now (`shared`) the first time */
static inline float *gevrGunOffOwnFit(int look, int item, const float shared[3])
{
    const int s = gevrPerGunSet(look);
    int i;

    item = gevrPerGunItem(item);
    if (s < 0 || item <= 0 || item >= GEVR_MAX_WEAPONS)
    {
        return 0;
    }
    if (!VrGunOffPerSet[s][item])
    {
        for (i = 0; i < 3; i++)
        {
            VrGunOffPer[s][item][i] = shared[i];
        }
        VrGunOffPerSet[s][item] = 1;
    }
    return VrGunOffPer[s][item];
}

static inline float *gevrGripTrimOwnFit(int look, int item, const float shared[6])
{
    const int s = gevrPerGunSet(look);
    int i;

    item = gevrPerGunItem(item);
    if (s < 0 || item <= 0 || item >= GEVR_MAX_WEAPONS)
    {
        return 0;
    }
    if (!VrGripTrimPerSet[s][item])
    {
        for (i = 0; i < 6; i++)
        {
            VrGripTrimPer[s][item][i] = shared[i];
        }
        VrGripTrimPerSet[s][item] = 1;
    }
    return VrGripTrimPer[s][item];
}

#ifdef __cplusplus
}
#endif
#endif
