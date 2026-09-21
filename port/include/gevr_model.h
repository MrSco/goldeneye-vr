#ifndef _GEVR_MODEL_H_
#define _GEVR_MODEL_H_

#include <PR/ultratypes.h>

/*
 * Model files (the C*Z, G*Z and P*Z cartridge files): cartridge layout to
 * host layout.
 *
 * A model file is a tree of ModelNode records, each with a rodata record of
 * its opcode's type, plus vertex arrays, embedded texture data and display
 * lists. Every reference between them is a 4-byte segment-5 address
 * (0x05000000 | offset into the file) that the game turns into a pointer at
 * load, and every field is big-endian. On this host a pointer slot is 8
 * bytes, a display list command is 16 rather than 8, and the fields need
 * swapping, so the file cannot be used where it was decompressed.
 *
 * gevrModelConvert() walks the tree from the root the way the game's own
 * modelPromoteNodeOffsetsToPointers() does, finds every block, and rebuilds
 * the file in host layout in place: records widened, fields swapped, display
 * lists doubled, and every segment-5 reference rewritten to the block's new
 * offset. References stay segment-5 offsets - the game adds the base itself,
 * and keeps display list references as offsets for the renderer to resolve
 * through segment 5. Blocks keep their order, so the display lists stay at
 * the tail of the file where the game's post-load texture pass expects them.
 *
 * tools/gevr_model_probe.py performs the same walk on a cartridge file on
 * the host and prints what it finds; the record sizes here were checked
 * against it for a prop, a character and a gun.
 */

struct ModelFileHeader;

/*
 * Set by load_object_fill_header() around its file load and consumed by
 * load_resource(), which is where the decompressed bytes and the room to
 * grow them are both in hand. It carries the switch and texture counts the
 * file itself does not store.
 */
extern struct ModelFileHeader *gevrModelPendingHeader;

/*
 * Convert the decompressed model at data (size bytes, in a buffer of capacity
 * bytes) in place. Returns the host-layout size, or 0 if the file could not
 * be converted, in which case data is untouched.
 */
u32 gevrModelConvert(u8 *data, u32 size, u32 capacity, s32 numSwitches, s32 numTextures, const char *name);

#endif
