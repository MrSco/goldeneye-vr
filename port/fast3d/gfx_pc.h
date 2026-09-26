#ifndef GFX_PC_H
#define GFX_PC_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <unordered_map>
#include <list>
#include <cstddef>

#include <PR/gbi.h>

#include "system.h"

/*
 * PORT: depth is squashed by this factor in the vertex shader (gfx_opengl.cpp),
 * which reaches 1 / 0.3 = 3.3 times past the level's far fog distance before
 * anything is clamped. Perfect Dark VR has always run this way on Quest (its GL
 * loader never finds depth clamp, so it takes the squash path). With depth
 * clamp alone (v0.1.7 - v0.1.10), everything past the far distance collapsed
 * onto one depth and failed the depth test against itself: Statue's tree wall
 * and hills were cut off at an angle that moved with the head (issue: far
 * scenery pops). The far triangle reject here and the game's room far tests
 * (bg.c GEVR_FAR_EXTEND) reach the same distance, or they cut the scenery the
 * depth buffer now keeps.
 */
#define GEVR_FAR_DEPTH_SCALE 0.3f

#define SCREEN_WIDTH ((int32_t)gfx_current_native_viewport.width)
#define SCREEN_HEIGHT ((int32_t)gfx_current_native_viewport.height)

extern uintptr_t gfxFramebuffer;

struct GfxRenderingAPI;
struct GfxWindowManagerAPI;

struct TextureCacheKey {
    const uint8_t* texture_addr;
    const uint8_t* palette_addrs[2];
    uint8_t fmt, siz;
    uint8_t palette_index;
    uint64_t ext_key;
    uint16_t id_mask;
    uint32_t width = 0, height = 0, source_pitch = 0;
    uint32_t palette_hash = 0, palette_fmt = 0;
    uint32_t clamp = 0; /* a padded block's clamp window (clamp_w << 16 | clamp_h): the pad is filled from it */
    bool swizzled = false;

    bool operator==(const TextureCacheKey&) const noexcept = default;

    struct Hasher {
        size_t operator()(const TextureCacheKey& key) const noexcept {
            uintptr_t addr = (uintptr_t)key.texture_addr;
            return (size_t)(addr ^ (addr >> 5));
        }
    };
};

typedef std::unordered_map<TextureCacheKey, struct TextureCacheValue, TextureCacheKey::Hasher> TextureCacheMap;
typedef std::pair<const TextureCacheKey, struct TextureCacheValue> TextureCacheNode;

struct TextureCacheValue {
    uint32_t texture_id;
    uint8_t cms, cmt;
    bool linear_filter;

    std::list<struct TextureCacheMapIter>::iterator lru_location;
};

struct TextureCacheMapIter {
    TextureCacheMap::iterator it;
};

extern "C" {

#include "gfx_api.h"

}

#include "platform.h"

#ifndef G_NO_CLIPPING_EXT
#define G_NO_CLIPPING_EXT (1U << 29)
#endif
#ifndef G_MODULATE_EXT
#define G_MODULATE_EXT (1U << 30)
#endif
#ifndef G_ASPECT_MODE_EXT
#define G_ASPECT_MODE_EXT 0x3
#endif
#ifndef G_ASPECT_CENTER_EXT
#define G_ASPECT_CENTER_EXT 0
#endif
#ifndef G_ASPECT_LEFT_EXT
#define G_ASPECT_LEFT_EXT 1
#endif
#ifndef G_ASPECT_RIGHT_EXT
#define G_ASPECT_RIGHT_EXT 2
#endif
#ifndef G_ASPECT_WIDE_EXT
#define G_ASPECT_WIDE_EXT (1U << 2)
#endif
#ifndef G_TF_BLUR_EXT
#define G_TF_BLUR_EXT (1U << G_MDSFT_TEXTFILT)
#endif

#ifndef G_TRI4
#define G_TRI4 0xB1
#endif
#ifndef G_COL
#define G_COL 0x70
#endif
#ifndef G_EXTRAGEOMETRYMODE_EXT
#define G_EXTRAGEOMETRYMODE_EXT 0x71
#endif
#ifndef G_SETTEXINFO_EXT
#define G_SETTEXINFO_EXT 0x72
#endif
#ifndef G_SETTIMG_FB_EXT
#define G_SETTIMG_FB_EXT 0xD2
#endif
#ifndef G_SETGRAYSCALE_EXT
#define G_SETGRAYSCALE_EXT 0x73
#endif
#ifndef G_LOADTLUT2
#define G_LOADTLUT2 0x74
#endif
#ifndef G_SETINTENSITY_EXT
#define G_SETINTENSITY_EXT 0x75
#endif
#ifndef G_SETSUBPIXELOFFSET_EXT
#define G_SETSUBPIXELOFFSET_EXT 0x76
#endif
#ifndef G_FILLRECT_WIDE_EXT
#define G_FILLRECT_WIDE_EXT 0x77
#endif
#ifndef G_TEXRECT_WIDE_EXT
#define G_TEXRECT_WIDE_EXT 0x78
#endif
#ifndef G_IMAGERECT_EXT
#define G_IMAGERECT_EXT 0x79
#endif
#ifndef G_SETFB_EXT
#define G_SETFB_EXT 0x7A
#endif
#ifndef G_COPYFB_EXT
#define G_COPYFB_EXT 0x7B
#endif
#ifndef G_INVALTEXCACHE_EXT
#define G_INVALTEXCACHE_EXT 0x7C
#endif
#ifndef G_RDPFLUSH_EXT
#define G_RDPFLUSH_EXT 0x7D
#endif
#ifndef G_CLEAR_DEPTH_EXT
#define G_CLEAR_DEPTH_EXT 0x7E
#endif

#endif

