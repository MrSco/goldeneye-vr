#ifndef GEVR_MODATTACH_H
#define GEVR_MODATTACH_H

#include <bondtypes.h>

/* The gun-view draw owns these frames. A fresh draw supersedes any earlier view. */
void gevrModAttachBeginGunDraw(void);
/* The attachment's actual view-space transform, after it emits its housing.
 * False for unpublished, stale, hidden, removed or replaced attachments. */
int gevrModAttachGunFrame(int id, Mtxf *pose, s32 *mirrored);

#endif
