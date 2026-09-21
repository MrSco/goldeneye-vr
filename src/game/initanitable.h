#ifndef _INITANITABLE_H_
#define _INITANITABLE_H_
#include <ultra64.h>
#include <bondtypes.h>
#include <assets/animationtable_data.h>



/**
 * Struct to hold animation data. This is never instantiated.
 * Instead, only a pointer to this will exist.
 */
struct animation_table_data {
    /**
     * Array length is arbitrary and shouldn't matter. The largest offset
     * into this is for the last animation pointer 0xE7C0, so just choosing
     * a value bigger than that, like u16_max_value.
    */
    u8 data[0xffff];
};

/**
 * Data holder for animations.
 */
extern struct animation_table_data* ptr_animation_table;

/*
 * The two cartridge offsets inside ModelAnimation, resolved against the loaded
 * animation segment.
 *
 * Both slots arrive from the cartridge holding a byte offset into this table.
 * On the N64 the loader rewrote them in place as absolute addresses, because a
 * pointer was the same four bytes the cartridge had reserved. That is not true
 * here, so the offsets are left as they came and the base is added on each
 * read instead - which also means an animation is usable whether or not the
 * relocation pass ever walked it.
 */
static inline ModelAnimBitField *modelAnimBitDescriptors(const ModelAnimation *anim)
{
	return (ModelAnimBitField *)(ptr_animation_table->data + anim->bitDescriptors);
}

static inline u8 *modelAnimBitStream(const ModelAnimation *anim)
{
	return (u8 *)(ptr_animation_table->data + anim->bitStream);
}

/**
 * Contains offsets into ptr_animation_table for player and guard animations.
 * The index of each value corresponds to `enum ANIMATION`.
 * The value corresponds to (e.g. index=0) PTR_ANIM_idle (same as ANIM_DATA_idle)
*/
/*
 * Holds offsets into the animation table until expand_ani_table_entries()
 * relocates them, after which every slot is an address - chr.c and
 * chraction.c cast them straight to ModelAnimation *. That makes them
 * pointer width; as s32 the relocated address was cut in half, and walking
 * the array as s32** (4 bytes on the N64, 8 here) also read two offsets as
 * one pointer and strode twice as far.
 */
extern uintptr_t animation_table_ptrs1[];

/**
 * Contains offsets into ptr_animation_table for object/vehicle animations.
 * The index of each value corresponds to `enum AIRCRAFT_ANIMATION`.
 * The value corresponds to (e.g. index=0) PTR_ANIM_helicopter_cradle (same as ANIM_DATA_helicopter_cradle)
*/
extern struct ModelAnimation * animation_table_ptrs2[];

#endif
