#ifndef GEVR_ODDJOBHAT_H
#define GEVR_ODDJOBHAT_H

#include <PR/ultratypes.h>

/* Mods: Oddjob's hat in place of the throwing knife (held, thrown and lying
 * about), in the cartridge's, Bean's or GoldenEye X's hand. The launcher's
 * Mods page downloads it (the oddjob-hat pack); it is not in the APK.
 * Cosmetic and local to each headset. docs/oddjob-hat.md. */

#ifdef __cplusplus
extern "C" {
#endif

struct ModelFileHeader;
struct Model;
struct ModelNode;

extern int VrOddjobHat;   /* vr_settings: Mods, saved as OddjobHat */

/* the hat file is on the headset */
int gevrHatAvailable(void);

/* load_object_fill_header: notes the knife models' display list nodes */
void gevrHatModelLoaded(struct ModelFileHeader *header, const char *name);

/* generate_player_thrown_knife: this PchrknifeZ instance is a thrown knife
 * (the hunting knife shares the model and stays a knife) */
void gevrHatThrown(struct Model *model);

/* the model renderer: 1 when the hat decides what this node draws, with the
 * list in *gdl (NULL: nothing) */
int gevrHatNodeGdl(struct ModelNode *node, struct Model *model, void **gdl);

/* Bean leaves the held knife and the pickup alone while the hat is shown */
int gevrHatOwnsNode(const void *node);

/* fast3d: the hat's pictures, at the stand-in addresses its lists load */
int gevrHatTexture(const void *addr, const u8 **rgba, int *w, int *h);

#ifdef __cplusplus
}
#endif

#endif
