#ifndef GEVR_MODGRIP_H
#define GEVR_MODGRIP_H
/*
 * A pack body's grip point (mod.weapon.model's grip) as the hand's gun offset
 * (bondview2.c gevrStereoGunMatrix): cm right, up and back of the controller's
 * grip, the form Gun fit's own trim takes. The gun's root sits at the grip
 * plus (right * x + up * y + back * (gripToOrigin + z)) cm, and a model point
 * p (its units, viewCm cm each) is -p.x right, p.y up and -p.z back of the
 * root, so the point is in the grip when x = p.x * viewCm, y = -p.y * viewCm
 * and z = p.z * viewCm - gripToOrigin. The player's own trim, how far their
 * Gun fit moved the gun from the game's default place, then adds on top.
 */
static inline void gevrModGripOff(const float grip[3], const float player[3], const float game[3],
                                  float viewCm, float gripToOrigin, float out[3])
{
    out[0] = grip[0] * viewCm + (player[0] - game[0]);
    out[1] = -grip[1] * viewCm + (player[1] - game[1]);
    out[2] = grip[2] * viewCm - gripToOrigin + (player[2] - game[2]);
}
#endif
