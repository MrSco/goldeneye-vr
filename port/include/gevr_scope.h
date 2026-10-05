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
    float gunOrigin[3];    /* gun grip pos in view space (m) */
    float gunAxes[3][3];   /* gun right, up, back axes */
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

#ifdef __cplusplus
}
#endif
#endif
