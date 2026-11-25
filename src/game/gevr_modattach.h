#ifndef GEVR_MODATTACH_H
#define GEVR_MODATTACH_H

#include <bondtypes.h>

/* The gun-view draw owns these frames. A fresh draw supersedes any earlier view. */
void gevrModAttachBeginGunDraw(void);
/* The attachment's actual view-space transform from the latest gun view.
 * Also valid for the next tick before a new view starts. False for a part
 * that was not drawn, became hidden, was removed, or was replaced. */
int gevrModAttachGunFrame(int id, Mtxf *pose, s32 *mirrored);

#endif
