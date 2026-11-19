#ifndef GEVR_BEANMAG_H
#define GEVR_BEANMAG_H

/*
 * Bean guns' removable magazines (user, 2026-11-18: as GoldenEye X's, on the
 * release's meshes). The release's guns are one skinned mesh each with no
 * magazine bone (their skeletons are SKEL_TOP, _MUZZLE, _FLASH, _CARTRIDGE and
 * a pistol's _SLIDE and _FINGER), and its dropped-magazine props are a
 * 28-triangle wedge, so a magazine is made from the gun itself when the gun is
 * laid out (gevr_bean.c):
 *  - the foot: the gun's own triangles below a plane across the grip (the
 *    PP7's floor plate and finger rest), cut off along it;
 *  - the body: a plain prism up the well from that plane, the grip's own
 *    outline there drawn in;
 *  - the gun without it: the rest of the gun, and a dark floor a little way up
 *    the empty well.
 * That is a pistol's, whose magazine is inside its grip. A rifle's or a
 * submachine gun's hangs out of the gun and is a piece of the mesh on its own
 * (an island: it shares no corner with the rest), so there the magazine is
 * that piece whole, with a lid on its top, and the gun without it is the rest
 * as it is: where the magazine went in the release's gun may be open, shut or
 * not there at all (the AR33's hangs off the receiver's front), so no floor is
 * guessed for it. Nothing is cut and no body is made.
 * Everything here is in the release mesh's own units (gevrBeanTri.p), which
 * tools/gevr_bean_mag_preview.py draws.
 */
#include "gevr_bean_int.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct GevrBeanMagDef {
	const char *source;   /* gevr_bean_fits.h: "gun/ppk" */
	/* plane[0..2] . p + plane[3] >= 0 stays on the gun; the unit normal points up the well */
	float plane[4];
	float lo[3], hi[3];   /* the grip: only its triangles are cut, and its outline is the well's */
	float floorDepth;     /* the empty well's dark floor, this far up the well */
	float bodyLength;     /* the body, this far up the well from the plane */
	float bodyDraw[2];    /* its outline: the grip's, drawn in across (x) and along the gun */
	float axisSpan;       /* the well runs through the grip's outlines at the plane and this far up */
	unsigned char bodyRgb[3], floorRgb[3];
	/* An island magazine: every piece of the gun wholly inside lo..hi. plane's
	 * normal then only says which way is into the gun (its offset is not read);
	 * the mouth is `inside` below the magazine's top, where it meets the gun. */
	int island;
	float inside;
	int straight;         /* it goes in along the normal; else along its own top third */
	int pullUp;           /* it lifts off the top of the gun (the RC-P90's) */
} GevrBeanMagDef;

typedef struct GevrBeanMagBuilt {
	/* the gun with its magazine out: every triangle of the mesh that stays, and the floor */
	struct gevrBeanTri *open;
	int *openSrc;         /* the mesh triangle each came from; -1 the floor's */
	int nopen;
	/* the magazine, where it sits in the gun: the foot, then the body */
	struct gevrBeanTri *mag;
	int nmag, nfoot;
	float axis[3];        /* up the well, unit */
	float mouth[3];       /* the well's mouth: its outline's middle on the plane */
	float top[3];         /* the seated magazine's top */
	float foot[3];        /* the foot's middle: where a hand takes it */
	float hold[3];        /* where the off hand's palm has it */
	/* its silhouette, plain and one colour, for the see-through placeholders in
	 * the well and at the belt (gevr_bean.c tints it): the magazine's own
	 * triangles, or its box when it has very many (the RC-P90's rounds) */
	struct gevrBeanTri *ghost;
	int nghost;
} GevrBeanMagBuilt;

const GevrBeanMagDef *gevrBeanMagFor(const char *source);
/* the n-th gun that has one, NULL past the last */
const GevrBeanMagDef *gevrBeanMagAt(int n);
/* eligible: per triangle, 1 the gun's own (not its hand, flash or slide). 0 when
 * the mesh has nothing to cut there or memory ran out; built is then empty. */
int gevrBeanMagBuild(const GevrBeanMagDef *def, const struct gevrBeanTri *tris, int ntris,
                     const unsigned char *eligible, GevrBeanMagBuilt *built);
void gevrBeanMagFree(GevrBeanMagBuilt *built);

#ifdef __cplusplus
}
#endif
#endif
