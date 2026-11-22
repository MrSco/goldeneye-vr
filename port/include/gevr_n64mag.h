#ifndef GEVR_N64MAG_H
#define GEVR_N64MAG_H

/*
 * GoldenEye's own guns' removable magazines (owner, 2026-11-21: the hand
 * reload "also original mode guns ... you'll probably have to cut some meshes
 * to make magazines removable"), as GoldenEye X's and Bean's
 * (gevr_beanmag.h). The cartridge's guns are a handful of display lists each,
 * all on the gun's root matrix but the slide and the trigger finger:
 *  - a rifle's or a submachine gun's magazine hangs out of the gun and is its
 *    own triangles, none shared with the gun: every triangle wholly inside a
 *    box round it. The gun without it is its lists with those triangles
 *    struck out (a copy of each list, the commands that drew them emptied),
 *    the magazine alone is the same lists with every other triangle struck
 *    out. Nothing is cut and no vertex is made.
 *  - a pistol's is inside its grip, with nothing of it modelled, and the
 *    sniper rifle has none: one is made, a plain dark box up the grip (its
 *    foot showing under it) or under the receiver. The gun stays as it is.
 *  - a shotgun's is one of the shells it carries (the cartridge shows as many
 *    as the reserve has, up to five): the round is that shell's own list, the
 *    gun is never opened, and it goes in at a port under the receiver.
 * The Cougar, the grenade launcher, the rocket launcher and the Golden Gun
 * have nothing here: gestures run the game's own reload for them.
 *
 * Coordinates are the model's as drawn: the root joint's space, 0.085 cm a
 * unit (x the gun's left, y up, z along the barrel), which is the space of
 * the gun's matrix. port/tests/test_n64_magazine.py runs it on the ROM's own
 * models.
 */
#ifdef __cplusplus
extern "C" {
#endif

struct ModelFileHeader;
struct gevrBeanMagInfo;

typedef struct GevrN64MagDef {
	const char *name;      /* the model's file: "Gak47Z" */
	/* a magazine of the model's own triangles: those wholly inside lo..hi */
	float lo[3], hi[3];
	float into[3];         /* unit: the way it goes into the gun */
	float inside;          /* this much of its top is in the gun: the mouth is that far short of its top */
	int pullUp;            /* it lifts off the top of the gun (the RC-P90's) */
	int single;            /* a loose round (a shotgun's shell): the gun is never opened, the round is a copy */
	int switched;          /* only lists under one of the shell switches (18-27) */
	float port[3];         /* a loose round goes in here */
	/* a magazine made here: a box from foot along into, `length` long, half[0]
	 * wide across the gun and half[1] deep, `inside` of its top in the gun */
	int made;
	float foot[3], length, half[2];
	unsigned char rgb[3];
} GevrN64MagDef;

const GevrN64MagDef *gevrN64MagFor(const char *name);
const GevrN64MagDef *gevrN64MagAt(int n);

/* objecthandler_2.c, once a model file is loaded: lays its magazine out */
void gevrN64MagApply(struct ModelFileHeader *header, const char *name);
void gevrN64MagForget(struct ModelFileHeader *header);
void gevrN64MagForgetStorage(const void *base, unsigned size);
/* gun.c: the model has a magazine to take out, and where (as Bean's: gevr_bean.h) */
int gevrN64GunMagazine(const void *header, struct gevrBeanMagInfo *out);
/* model.c: the list to draw in place of a node's primary (secondary: 1) for the
 * magazine's state (gevr_bean.c gevrBeanSetMagazine); current when it has none */
void *gevrN64MagGdl(void *node, void *current, int secondary);
/* tests: how many triangles the magazine took, and how many lists were opened */
int gevrN64MagStats(const void *header, int *triangles, int *lists);

#ifdef __cplusplus
}
#endif
#endif
