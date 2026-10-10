/*
 * Meta Quest Touch controllers for the watch's Controls page: the data
 * tools/gevr_touch_model_gen.py writes to port/src/gevr_touch_model_data.c.
 * src/game/gevr_touch.c draws them.
 *
 * Each kind is a pair (left, right) laid out side by side in the watch
 * controller's model space (options.c draw_watch_controller). A vertex
 * belongs to one part; a part other than the body moves as the controller's
 * input does: the trigger turns about its pivot round axis, the stick tilts
 * about its pivot round axis (forward and back) and axis2 (side to side),
 * and the grip and the buttons go in along axis.
 */

#ifndef GEVR_TOUCH_MODEL_H
#define GEVR_TOUCH_MODEL_H

#include <PR/ultratypes.h>

enum {
    GEVR_TOUCH_QUEST1,   /* Oculus Touch for Quest and Rift S */
    GEVR_TOUCH_QUEST2,   /* Quest 2 */
    GEVR_TOUCH_PLUS,     /* Touch Plus: Quest 3 and 3S */
    GEVR_TOUCH_PRO,      /* Touch Pro */
    GEVR_TOUCH_KINDS
};

enum {
    GEVR_TOUCH_BODY,
    GEVR_TOUCH_TRIGGER,
    GEVR_TOUCH_GRIP,
    GEVR_TOUCH_STICK,
    GEVR_TOUCH_LOWER,    /* X on the left controller, A on the right */
    GEVR_TOUCH_UPPER,    /* Y, B */
    GEVR_TOUCH_MENU,     /* the left one's Menu, the right one's Meta button */
    GEVR_TOUCH_PARTS
};

typedef struct {
    s16 x, y, z;
    s8 nx, ny, nz;
    u8 part;
    u8 r, g, b;
} GevrTouchVert;

typedef struct {
    f32 pivot[3];
    f32 axis[3];
    f32 axis2[3];
} GevrTouchPart;

/*
 * batches: for each 16-vertex load, the vertex count, the triangle count,
 * the vertex indices, then the triangles, a | b << 4 | c << 8 in the load.
 */
typedef struct {
    const GevrTouchVert *verts;
    const u16 *batches;
    u16 nverts;
    u16 nbatches;
    u16 nstream;   /* the loads' vertices, all told */
    u16 ntris;
    GevrTouchPart parts[GEVR_TOUCH_PARTS];
} GevrTouchMesh;

typedef struct {
    const char *name;
    GevrTouchMesh side[2];   /* [0] left, [1] right */
} GevrTouchModel;

extern const GevrTouchModel gevrTouchModels[GEVR_TOUCH_KINDS];
extern const f32 gevrTouchTriggerAngle;   /* radians, at full pull */
extern const f32 gevrTouchStickAngle;     /* radians, at full push */
extern const f32 gevrTouchButtonTravel;   /* model units */
extern const f32 gevrTouchGripTravel;
extern const f32 gevrTouchStickTravel;

#endif
