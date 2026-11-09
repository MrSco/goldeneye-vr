/*
 * What the PC game answers in place of the headset's host (docs/pc-platforms.md).
 */

#include <stdbool.h>
#include <stdlib.h>
#include <PR/ultratypes.h>

#include "system.h"

/* port/flat/flat_headset.c: a dedicated server slows down while empty; a player's game never does */
int gevrServerIdle(void) { return 0; }

/* The launcher is the Quest's (port/vr/vr_launcher.cpp, drawn in the window by
 * port/pc/pc_launcher.cpp), with its lobby tick and restart. */
