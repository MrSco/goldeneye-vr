#ifndef _GEVR_TOUCH_H_
#define _GEVR_TOUCH_H_

#include <ultra64.h>

/* The kind of Touch controllers the watch's Controls page draws (gevr_touch_model.h). */
s32 gevrTouchKind(void);

/*
 * The pair of Touch controllers in place of the N64 controller, under the
 * page's model matrix (options.c draw_watch_controller: lookat, spin and
 * pitch); alpha below 0xff fades them in with the page.
 */
Gfx *gevrTouchRender(Gfx *gdl, Mtxf *basemtx, s32 alpha);

/* Under each controller, its controls and what they do, lit while held. */
Gfx *gevrTouchDrawLabels(Gfx *gdl);

#endif
