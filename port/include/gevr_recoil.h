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
    GEVR_RECOIL_PISTOL,    /* PD's Falcon 2: the PP7 family, the DD44 */
    GEVR_RECOIL_MAGNUM,    /* PD's DY357: the Cougar, the Golden Gun */
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
void vrRecoilKick(int gunhand, int recoilClass);   /* gunhand: GUNRIGHT 0, GUNLEFT 1 */
#ifdef __cplusplus
}
#endif

#endif
