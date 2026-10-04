#ifndef GEVR_RECOIL_H
#define GEVR_RECOIL_H

/*
 * Per-gun recoil (launcher "Per-gun recoil", VrPerWeaponRecoil): Perfect Dark
 * VR's recoil profiles (port/vr/vr_input.cpp) by the nearest PD gun. The game
 * side (port/src/input.c gevrRumbleGunfire) names the class of the gun that
 * fired; the VR side kicks that hand's grip pose.
 */
enum GevrRecoilClass {
    GEVR_RECOIL_NONE,      /* knives, throwables, gadgets */
    GEVR_RECOIL_PISTOL,    /* PD's Falcon 2: the PP7 family (silenced a little weaker) */
    GEVR_RECOIL_MAGNUM,    /* PD's DY357: the Cougar, the Golden Gun; the DD44 at half */
    GEVR_RECOIL_SMG,       /* PD's light machine guns: the SMGs */
    GEVR_RECOIL_RIFLE,     /* PD's AR34: the KF7, the AR33 */
    GEVR_RECOIL_SHOTGUN,   /* the shotguns */
    GEVR_RECOIL_SNIPER,    /* the sniper rifle */
    GEVR_RECOIL_LAUNCHER,  /* PD's Devastator and rocket launcher: the launchers */
    GEVR_RECOIL_LASER,     /* PD's laser: the Moonraker laser */
    GEVR_RECOIL_TASER      /* PD's tranquilizer: the taser */
};

#ifdef __cplusplus
extern "C" {
#endif
/* gunhand: GUNRIGHT 0, GUNLEFT 1; strength scales the class's kick (1 = PD's) */
void vrRecoilKick(int gunhand, int recoilClass, float strength);
#ifdef __cplusplus
}
#endif

#endif
