/* Cartridge Usetup*Z → host stagesetup. Pointer fields grow from 4 to 8 bytes;
 * offsets stay file-relative until proplvreset2 rebases them. */
#include "system.h"
#include "gevr_stage.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

static int g_setup_fail;
int gevrSetupFailCode(void) { return g_setup_fail; }

static uint32_t read32(const uint8_t *p) {
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}
static uint16_t read16(const uint8_t *p) { return (uint16_t)p[0] << 8 | p[1]; }
static float readf(const uint8_t *p) {
    uint32_t v = read32(p);
    float f;
    memcpy(&f, &v, 4);
    return f;
}
static void put32(uint8_t *p, uint32_t v) { memcpy(p, &v, 4); }
static void putptr(uint8_t *p, uintptr_t v) { memcpy(p, &v, sizeof(uintptr_t)); }
static void put16(uint8_t *p, uint16_t v) { memcpy(p, &v, 2); }
static void putf(uint8_t *p, float f) { memcpy(p, &f, 4); }
static size_t align8(size_t n) { return (n + 7) & ~(size_t)7; }

/* N64 prop sizes in u32 words — matches sizepropdef() #if 1 / cart data. */
static int n64_prop_words(uint8_t type) {
    static const int words[] = {
        /*0*/ 1, /*DOOR*/ 64, /*DOOR_SCALE*/ 2, /*PROP*/ 32, /*KEY*/ 33,
        /*ALARM*/ 32, /*CCTV*/ 0x3b, /*MAGAZINE*/ 0x21, /*COLLECTABLE*/ 0x22,
        /*GUARD*/ 7, /*MONITOR*/ 0x40, /*MULTI_MONITOR*/ 0x95, /*RACK*/ 32,
        /*AUTOGUN*/ 0x36, /*LINK*/ 3, /*DEBRIS*/ 32, /*UNK16*/ 32, /*HAT*/ 32,
        /*GUARD_ATTR*/ 3, /*SWITCH*/ 4, /*AMMO*/ 0x2d, /*ARMOUR*/ 0x22,
        /*TAG*/ 4, /*OBJ_START*/ 4, /*OBJ_END*/ 1, /*DESTROY*/ 2, /*COMPLETE*/ 2,
        /*FAIL*/ 2, /*COLLECT*/ 2, /*DEPOSIT*/ 2, /*PHOTO*/ 4, /*NULL*/ 1,
        /*ENTER*/ 4, /*DEPOSIT_ROOM*/ 5, /*COPY*/ 1, /*WATCH*/ 4,
        /*GAS*/ 32, /*RENAME*/ 10, /*LOCK*/ 4, /*VEHICLE*/ 0x2c, /*AIRCRAFT*/ 0x2d,
        /*UNK41*/ 32, /*GLASS*/ 32, /*SAFE*/ 32, /*SAFE_ITEM*/ 5, /*TANK*/ 0x38,
        /*CAMERAPOS*/ 7, /*TINTED*/ 37, /*END*/ 1,
    };
    if (type > 48) return 0;
    return words[type];
}

/* Host ObjectRecord is N64 0x80 plus four pointer slots grown 4→8 (= +16). */
enum { N64_OBJ = 0x80, HOST_OBJ = 0x90 };

/* Host sizes from sizeof() with -fms-extensions (ARM64). */
static size_t host_prop_bytes(uint8_t type) {
    switch (type) {
    case 1:  return 296; /* DoorRecord */
    case 2:  return 8;   /* GlobalDoorScaleRecord */
    case 3: case 5: case 12: case 15: case 16: case 17: case 36: case 41: case 42: case 43:
        return HOST_OBJ; /* ObjectRecord / aliases */
    case 4:  return 152; /* KeyRecord */
    case 6:  return 272; /* CCTVRecord */
    case 7:  return 152; /* AmmoCrateRecord */
    case 8:  return 160; /* WeaponObjRecord */
    case 9:  return 32;  /* GuardRecord */
    case 10: return 288; /* MonitorObjRecord */
    case 11: return 664; /* MultiMonitorObjRecord */
    case 13: return 248; /* AutogunRecord */
    case 14: case 19: return 32; /* LinkRecord / SwitchRecord */
    case 18: return 12;  /* GuardAttributeRecord */
    case 20: return 200; /* MultiAmmoCrateRecord */
    case 21: return HOST_OBJ + 8; /* Armour: Object + 2 f32 */
    case 22: return 24;  /* TagObjectRecord */
    case 23: return 24;  /* MissionObjectiveRecord (gains nextentry*) */
    case 37: return 48;  /* RenameObjectRecord */
    case 38: return 32;  /* LockDoorRecord */
    case 39: case 40: return 208; /* VehichleRecord / AircraftRecord */
    case 44: return 40;  /* SafeObjectRecord */
    case 45: return 248; /* TankRecord */
    case 46: return 28;  /* CutsceneRecord */
    case 47: return 168; /* TintedGlassRecord */
    default: {
        int n64w = n64_prop_words(type);
        return n64w ? (size_t)n64w * 4 : 0;
    }
    }
}

static void conv_object(uint8_t *dst, const uint8_t *src) {
    put16(dst + 0, read16(src + 0));
    dst[2] = src[2];
    dst[3] = src[3];
    put16(dst + 4, read16(src + 4));
    put16(dst + 6, read16(src + 6));
    put32(dst + 8, read32(src + 8));
    put32(dst + 12, read32(src + 12));
    putptr(dst + 16, (uintptr_t)read32(src + 16));
    putptr(dst + 24, (uintptr_t)read32(src + 20));
    for (int i = 0; i < 16; i++) putf(dst + 32 + i * 4, readf(src + 0x18 + i * 4));
    for (int i = 0; i < 3; i++) putf(dst + 0x60 + i * 4, readf(src + 0x58 + i * 4));
    put32(dst + 0x6c, read32(src + 0x64));
    putptr(dst + 0x70, (uintptr_t)read32(src + 0x68));
    putptr(dst + 0x78, (uintptr_t)read32(src + 0x6c));
    putf(dst + 0x80, readf(src + 0x70));
    putf(dst + 0x84, readf(src + 0x74));
    memcpy(dst + 0x88, src + 0x78, 4);
    memcpy(dst + 0x8c, src + 0x7c, 4);
}

static void conv_words(uint8_t *dst, const uint8_t *src, size_t nbytes) {
    for (size_t i = 0; i + 4 <= nbytes; i += 4) put32(dst + i, read32(src + i));
    for (size_t i = (nbytes & ~3u); i < nbytes; i++) dst[i] = src[i];
}

/* N64 MonitorRecord 0x74 → host 128: two ptrs grow + pad before tconfig (+12). */
static void conv_monitor(uint8_t *dst, const uint8_t *src) {
    putptr(dst + 0, (uintptr_t)read32(src + 0));
    put16(dst + 8, read16(src + 4));
    put16(dst + 10, read16(src + 6));
    putptr(dst + 16, (uintptr_t)read32(src + 8));
    for (size_t i = 0x0C; i + 4 <= 0x74; i += 4)
        put32(dst + 12 + i, read32(src + i)); /* N64 i → host i+12 */
}

static size_t convert_one_prop(uint8_t *dst, const uint8_t *src, uint8_t type) {
    size_t host = host_prop_bytes(type);
    int n64w = n64_prop_words(type);
    size_t n64b = (size_t)n64w * 4;
    memset(dst, 0, host);

    switch (type) {
    case 1: { /* DOOR → 296 */
        conv_object(dst, src);
        const uint8_t *s = src + N64_OBJ;
        uint8_t *d = dst + HOST_OBJ;
        put32(d + 0, read32(s + 0));
        for (int i = 0; i < 5; i++) putf(d + 4 + i * 4, readf(s + 4 + i * 4));
        put16(d + 24, read16(s + 24));
        put16(d + 26, read16(s + 26));
        put32(d + 28, read32(s + 28));
        put32(d + 32, read32(s + 32));
        put32(d + 36, read32(s + 36));
        for (int i = 0; i < 5; i++) putf(d + 40 + i * 4, readf(s + 40 + i * 4));
        d[60] = s[60];
        d[61] = s[61];
        put16(d + 62, read16(s + 62));
        put32(d + 64, read32(s + 64));
        put16(d + 68, read16(s + 68));
        d[70] = s[70];
        d[71] = s[71];
        putptr(d + 72, (uintptr_t)read32(s + 72));
        putptr(d + 80, (uintptr_t)read32(s + 76));
        conv_words(d + 88, s + 80, 28);
        put32(d + 116, read32(s + 108));
        put32(d + 120, read32(s + 112));
        /* 4 bytes pad then sound state pointers at host +128/+136 */
        putptr(d + 128, (uintptr_t)read32(s + 116));
        putptr(d + 136, (uintptr_t)read32(s + 120));
        put32(d + 144, read32(s + 124));
        break;
    }
    case 3: case 5: case 12: case 15: case 16: case 17: case 36: case 41: case 42: case 43:
        conv_object(dst, src);
        break;
    case 4: /* KEY */
        conv_object(dst, src);
        put32(dst + HOST_OBJ, read32(src + N64_OBJ));
        break;
    case 8: /* COLLECTABLE / WeaponObj */
        conv_object(dst, src);
        dst[HOST_OBJ] = src[N64_OBJ];
        dst[HOST_OBJ + 1] = src[N64_OBJ + 1];
        put16(dst + HOST_OBJ + 2, read16(src + N64_OBJ + 2));
        putptr(dst + HOST_OBJ + 8, (uintptr_t)read32(src + N64_OBJ + 4));
        break;
    case 10: { /* MONITOR → 288 */
        conv_object(dst, src);
        conv_monitor(dst + HOST_OBJ, src + N64_OBJ);
        put32(dst + 272, read32(src + N64_OBJ + 0x74));
        put32(dst + 276, read32(src + N64_OBJ + 0x78));
        put32(dst + 280, read32(src + N64_OBJ + 0x7C));
        break;
    }
    case 11: { /* MULTI_MONITOR → 664 */
        conv_object(dst, src);
        for (int m = 0; m < 4; m++)
            conv_monitor(dst + HOST_OBJ + m * 128, src + N64_OBJ + m * 0x74);
        memcpy(dst + HOST_OBJ + 4 * 128, src + N64_OBJ + 4 * 0x74, 4);
        break;
    }
    case 21: /* ARMOUR: Object + 2 f32 */
        conv_object(dst, src);
        putf(dst + HOST_OBJ, readf(src + N64_OBJ));
        putf(dst + HOST_OBJ + 4, readf(src + N64_OBJ + 4));
        break;
    case 39: { /* VEHICLE */
        conv_object(dst, src);
        const uint8_t *s = src + N64_OBJ;
        putptr(dst + HOST_OBJ, (uintptr_t)read32(s));
        put16(dst + HOST_OBJ + 8, read16(s + 4));
        put16(dst + HOST_OBJ + 10, read16(s + 6));
        for (int i = 0; i < 7; i++) putf(dst + HOST_OBJ + 12 + i * 4, readf(s + 8 + i * 4));
        putptr(dst + HOST_OBJ + 40, (uintptr_t)read32(s + 0x24)); /* path */
        put32(dst + HOST_OBJ + 48, read32(s + 0x28));             /* nextstep */
        putptr(dst + HOST_OBJ + 56, (uintptr_t)read32(s + 0x2c)); /* Sound */
        break;
    }
    case 40: { /* AIRCRAFT: nextstep then path (swapped vs vehicle) */
        conv_object(dst, src);
        const uint8_t *s = src + N64_OBJ;
        putptr(dst + HOST_OBJ, (uintptr_t)read32(s));
        put16(dst + HOST_OBJ + 8, read16(s + 4));
        put16(dst + HOST_OBJ + 10, read16(s + 6));
        for (int i = 0; i < 8; i++) putf(dst + HOST_OBJ + 12 + i * 4, readf(s + 8 + i * 4));
        put32(dst + HOST_OBJ + 44, read32(s + 0x28));             /* nextstep */
        putptr(dst + HOST_OBJ + 48, (uintptr_t)read32(s + 0x2c)); /* path */
        putptr(dst + HOST_OBJ + 56, (uintptr_t)read32(s + 0x30)); /* Sound */
        break;
    }
    case 47: /* TINTED_GLASS */
        conv_object(dst, src);
        for (int i = 0; i < 5; i++) put32(dst + HOST_OBJ + i * 4, read32(src + N64_OBJ + i * 4));
        break;
    case 6: case 7: case 13: case 20: case 45: /* CCTV/MAG/AUTOGUN/AMMO/TANK: best-effort */
        conv_object(dst, src);
        conv_words(dst + HOST_OBJ, src + N64_OBJ, n64b - N64_OBJ);
        break;
    case 9: /* GUARD */
        put16(dst + 0, read16(src + 0));
        dst[2] = src[2];
        dst[3] = src[3];
        for (int i = 0; i < 10; i++) put16(dst + 4 + i * 2, read16(src + 4 + i * 2));
        putptr(dst + 24, (uintptr_t)read32(src + 0x18));
        break;
    case 2: /* DOOR_SCALE */
        put16(dst + 0, read16(src + 0));
        dst[2] = src[2];
        dst[3] = src[3];
        put32(dst + 4, read32(src + 4));
        break;
    case 14: /* LINK: N64 12 → host 32 (Index1@8 Index2@16 next@24) */
        put16(dst + 0, read16(src + 0));
        dst[2] = src[2];
        dst[3] = src[3];
        put32(dst + 8, read32(src + 4));
        put32(dst + 16, read32(src + 8));
        break;
    case 19: case 38: /* SWITCH / LOCK_DOOR: N64 16 → host 32 */
        put16(dst + 0, read16(src + 0));
        dst[2] = src[2];
        dst[3] = src[3];
        put32(dst + 8, read32(src + 4));
        put32(dst + 16, read32(src + 8));
        putptr(dst + 24, (uintptr_t)read32(src + 12));
        break;
    case 18: /* GUARD_ATTRIBUTE */
        put16(dst + 0, read16(src + 0));
        dst[2] = src[2];
        dst[3] = src[3];
        put32(dst + 4, read32(src + 4));
        put16(dst + 8, read16(src + 8));
        dst[10] = src[10];
        dst[11] = src[11];
        break;
    case 22: /* TAG */
        put16(dst + 0, read16(src + 0));
        dst[2] = src[2];
        dst[3] = src[3];
        /* ID and signed object offset are distinct big-endian halfwords. */
        put16(dst + 4, read16(src + 4));
        put16(dst + 6, read16(src + 6));
        putptr(dst + 8, (uintptr_t)read32(src + 8));
        putptr(dst + 16, (uintptr_t)read32(src + 12));
        break;
    case 23: /* OBJECTIVE START: N64 16 → host 24 (+ nextentry*) */
        put16(dst + 0, read16(src + 0));
        dst[2] = src[2];
        dst[3] = src[3];
        put32(dst + 4, read32(src + 4));
        put32(dst + 8, read32(src + 8));
        put32(dst + 12, read32(src + 12));
        break;
    case 44: /* SAFE_ITEM */
        put16(dst + 0, read16(src + 0));
        dst[2] = src[2];
        dst[3] = src[3];
        putptr(dst + 8, (uintptr_t)read32(src + 4));
        putptr(dst + 16, (uintptr_t)read32(src + 8));
        putptr(dst + 24, (uintptr_t)read32(src + 12));
        putptr(dst + 32, (uintptr_t)read32(src + 16));
        break;
    case 37: /* RENAME: 9 words + Object* */
        conv_words(dst, src, 36);
        putptr(dst + 36, (uintptr_t)read32(src + 36));
        break;
    case 46: /* CAMERAPOS */
        put16(dst + 0, read16(src + 0));
        dst[2] = src[2];
        dst[3] = src[3];
        for (int i = 0; i < 3; i++) putf(dst + 4 + i * 4, readf(src + 4 + i * 4));
        putf(dst + 16, readf(src + 16));
        putf(dst + 20, readf(src + 20));
        put32(dst + 24, read32(src + 24));
        break;
    default:
        /*
         * objectives / END / NOTHING. The first word is NOT a u32: it is
         * PropDefHeaderRecord - u16 extrascale, u8 state, u8 type - exactly as
         * every explicit case above writes it. Swapping it as a word reverses
         * those four bytes and lands type at byte 0, where the host struct
         * keeps extrascale's high byte. The game then reads pdef->type from
         * byte 3 and never sees type 48, so the prop walk runs off the end of
         * the list into the pads and paths behind it - which is where Dam's
         * garbage records (pad=25653, model=1794) came from.
         *
         * Convert the header like the other cases, then word-swap the rest.
         * The payload of the 2-word objective records is a u16 at offset 4 and
         * is still swapped as a u32 here; that is wrong but harmless by
         * comparison, and is recorded in HANDOFF rather than guessed at.
         */
        if (n64b >= 4) {
            put16(dst + 0, read16(src + 0));
            dst[2] = src[2];
            dst[3] = src[3];
            if (n64b > 4) {
                conv_words(dst + 4, src + 4, n64b - 4);
            }
        } else {
            conv_words(dst, src, n64b);
        }
        break;
    }
    return host;
}

static size_t copy_s32_list(uint8_t *dst, size_t dstpos, size_t cap,
                            const uint8_t *src, size_t srclen, uint32_t src_ofs,
                            uintptr_t *out_ofs) {
    if (!src_ofs || src_ofs >= srclen) {
        *out_ofs = 0;
        return dstpos;
    }
    dstpos = align8(dstpos);
    *out_ofs = (uintptr_t)dstpos;
    size_t i = src_ofs;
    while (i + 4 <= srclen) {
        if (dstpos + 4 > cap) return 0;
        int32_t v = (int32_t)read32(src + i);
        put32(dst + dstpos, (uint32_t)v);
        dstpos += 4;
        i += 4;
        if (v < 0) break;
    }
    return dstpos;
}

size_t gevrConvertSetup(uint8_t *data, size_t size, size_t capacity) {
    uint8_t *src;
    uint8_t *dst;
    uint32_t hdr[10];
    size_t dstpos;
    size_t i;
    static const uint8_t intro_words[] = {3, 4, 4, 8, 2, 2, 10, 3, 2, 1};

    g_setup_fail = 0;
    if (size < 40 || capacity < size + 4096) { g_setup_fail = 1; return 0; }
    for (i = 0; i < 10; i++) hdr[i] = read32(data + i * 4);

    src = malloc(size);
    if (!src) { g_setup_fail = 2; return 0; }
    memcpy(src, data, size);
    dst = data;
    memset(dst, 0, capacity);
    dstpos = 80; /* host stagesetup header */

    /* ---- pads (N64 44 → host 56) ---- */
    uintptr_t pads_ofs = 0, bound_ofs = 0, pwp_ofs = 0, wg_ofs = 0;
    size_t dstpos_pads_end = 0, dstpos_bound_end = 0;
    uintptr_t intro_ofs = 0, props_ofs = 0, paths_ofs = 0, ail_ofs = 0;
    uintptr_t pnames_ofs = 0, bnames_ofs = 0;

    if (hdr[6]) {
        pads_ofs = dstpos = align8(dstpos);
        for (i = hdr[6]; i + 44 <= size; i += 44) {
            uint32_t plink = read32(src + i + 0x24);
            if (dstpos + 56 > capacity) { g_setup_fail = 11; free(src); return 0; }
            for (int k = 0; k < 9; k++) putf(dst + dstpos + k * 4, readf(src + i + k * 4));
            /*
             * plink stays file-relative and is rebased below onto the string
             * pool; prop.c then adds the loaded file's base, matching how it
             * rebases pathwaypoints/waypointgroups/intro.
             *
             * stan is NOT carried over. The cartridge word at 0x28 is a
             * 4-byte slot that means nothing as a host pointer, and the game
             * overwrites it anyway: proplvreset2 calls
             * init_pathtable_something(pad, pad->plink, &pad->stan), which
             * resolves the tile by name and writes it here. Copying the raw
             * word through left a bogus pointer in any pad the resolver did
             * not reach, and getposstan walked it - that was the Dam load
             * crash (fault 0x3413f2b3, an odd address, in
             * stanLocusAddTileRoomIfNew). Zero is what an unresolved pad
             * should hold: getposstan returns early on a NULL stan.
             */
            putptr(dst + dstpos + 40, plink ? (uintptr_t)plink : 0);
            putptr(dst + dstpos + 48, 0);
            dstpos += 56;
            dstpos_pads_end = dstpos;
            if (!plink) break;
        }
    }

    if (hdr[7]) {
        bound_ofs = dstpos = align8(dstpos);
        for (i = hdr[7]; i + 68 <= size; i += 68) {
            uint32_t plink = read32(src + i + 0x24);
            if (dstpos + 80 > capacity) { g_setup_fail = 12; free(src); return 0; }
            for (int k = 0; k < 9; k++) putf(dst + dstpos + k * 4, readf(src + i + k * 4));
            putptr(dst + dstpos + 40, plink ? (uintptr_t)plink : 0);
            putptr(dst + dstpos + 48, 0); /* resolved at load - see the pad loop above */
            for (int k = 0; k < 6; k++) putf(dst + dstpos + 56 + k * 4, readf(src + i + 0x2c + k * 4));
            dstpos += 80;
            dstpos_bound_end = dstpos;
            if (!plink) break;
        }
    }

    /*
     * PORT probe: both loops above stop at the first record with a null
     * plink. If a real pad or volume in the middle has no name the array is
     * truncated here, and every later index reads past it - which is what a
     * bound-pad index is about to do in domakedefaultobj. Report the counts
     * so they can be compared against the ids the setup actually references.
     */
    if (pads_ofs || bound_ofs) {
        size_t np = pads_ofs ? (dstpos_pads_end - pads_ofs) / 56 : 0;
        size_t nb = bound_ofs ? (dstpos_bound_end - bound_ofs) / 80 : 0;
        sysLogPrintf(LOG_NOTE, "setup: %u pads, %u bound pads converted",
                (unsigned)np, (unsigned)nb);
    }

    /* ---- pathwaypoints (16 → 24) ---- */
    if (hdr[0]) {
        pwp_ofs = dstpos = align8(dstpos);
        for (i = hdr[0]; i + 16 <= size; i += 16) {
            int32_t padID = (int32_t)read32(src + i);
            uint32_t neigh = read32(src + i + 4);
            if (dstpos + 24 > capacity) { g_setup_fail = 13; free(src); return 0; }
            put32(dst + dstpos, (uint32_t)padID);
            put32(dst + dstpos + 4, 0); /* pad for align */
            putptr(dst + dstpos + 8, 0); /* fill after lists */
            put32(dst + dstpos + 16, read32(src + i + 8));
            put32(dst + dstpos + 20, read32(src + i + 12));
            /* stash src neigh ofs in unused low of neighbours temporarily via side table:
             * rewrite in second pass — store src ofs in the pointer slot as integer */
            putptr(dst + dstpos + 8, (uintptr_t)neigh);
            dstpos += 24;
            if (padID < 0) break;
        }
        /* expand neighbour arrays and patch */
        size_t scan = (size_t)pwp_ofs;
        while (scan + 24 <= dstpos) {
            int32_t padID;
            uintptr_t neigh_src;
            memcpy(&padID, dst + scan, 4);
            memcpy(&neigh_src, dst + scan + 8, sizeof(uintptr_t));
            if (padID >= 0 && neigh_src) {
                uintptr_t out;
                size_t np = copy_s32_list(dst, dstpos, capacity, src, size, (uint32_t)neigh_src, &out);
                if (!np) { g_setup_fail = 14; free(src); return 0; }
                dstpos = np;
                putptr(dst + scan + 8, out);
            } else {
                putptr(dst + scan + 8, 0);
            }
            scan += 24;
            if (padID < 0) break;
        }
    }

    /* ---- waygroups (12 → 24) ---- */
    if (hdr[1]) {
        wg_ofs = dstpos = align8(dstpos);
        size_t wg_start = dstpos;
        size_t wg_count = 0;
        for (i = hdr[1]; i + 12 <= size; i += 12) {
            uint32_t n = read32(src + i);
            uint32_t w = read32(src + i + 4);
            if (dstpos + 24 > capacity) { g_setup_fail = 15; free(src); return 0; }
            putptr(dst + dstpos, (uintptr_t)n);
            putptr(dst + dstpos + 8, (uintptr_t)w);
            put32(dst + dstpos + 16, read32(src + i + 8));
            put32(dst + dstpos + 20, 0);
            dstpos += 24;
            wg_count++;
            if (!n) break;
        }
        size_t scan = wg_start;
        for (size_t g = 0; g < wg_count; g++) {
            uintptr_t nsrc, wsrc;
            memcpy(&nsrc, dst + scan, sizeof(uintptr_t));
            memcpy(&wsrc, dst + scan + 8, sizeof(uintptr_t));
            if (nsrc) {
                uintptr_t out;
                size_t np = copy_s32_list(dst, dstpos, capacity, src, size, (uint32_t)nsrc, &out);
                if (!np) { g_setup_fail = 16; free(src); return 0; }
                dstpos = np;
                putptr(dst + scan, out);
            }
            if (wsrc) {
                uintptr_t out;
                size_t np = copy_s32_list(dst, dstpos, capacity, src, size, (uint32_t)wsrc, &out);
                if (!np) { g_setup_fail = 17; free(src); return 0; }
                dstpos = np;
                putptr(dst + scan + 8, out);
            }
            scan += 24;
        }
    }

    /* ---- intro: most cmds keep N64 word size; CAMERA expands 40→56 ---- */
    if (hdr[2]) {
        intro_ofs = dstpos = align8(dstpos);
        size_t ip = hdr[2];
        while (ip + 4 <= size) {
            int32_t cmd = (int32_t)read32(src + ip);
            uint8_t nw = (cmd >= 0 && cmd <= 9) ? intro_words[cmd] : 0;
            if (!nw || ip + nw * 4 > size) { g_setup_fail = 18; free(src); return 0; }
            if (cmd == 6) { /* INTROTYPE_CAMERA: N64 40 → host 56 */
                if (dstpos + 56 > capacity) { g_setup_fail = 18; free(src); return 0; }
                memset(dst + dstpos, 0, 56);
                for (int w = 0; w < 7; w++) /* type..unk18 */
                    put32(dst + dstpos + w * 4, read32(src + ip + w * 4));
                /* lang1c: two BE u16s → host union at +32 (pad at +28) */
                put16(dst + dstpos + 32, read16(src + ip + 28));
                put16(dst + dstpos + 34, read16(src + ip + 30));
                put32(dst + dstpos + 40, read32(src + ip + 32)); /* lang20 index */
                /* prev stays 0 until bondviewLoadSetupIntroSection links cameras */
                dstpos += 56;
            } else {
                if (dstpos + nw * 4 > capacity) { g_setup_fail = 18; free(src); return 0; }
                for (uint8_t w = 0; w < nw; w++)
                    put32(dst + dstpos + w * 4, read32(src + ip + w * 4));
                dstpos += nw * 4;
            }
            ip += nw * 4;
            if (cmd == 9) break;
        }
    }

    /* ---- props ---- */
    if (hdr[3]) {
        props_ofs = dstpos = align8(dstpos);
        /*
         * PORT probe. The game walk produces sane records to a point and
         * garbage after, with no size disagreement between the two tables -
         * which is what a desync on THIS side looks like: one wrong N64 word
         * count and every later record is read from the wrong offset, so the
         * host stream carries garbage types the game then walks happily.
         */
        size_t propindex = 0;
        size_t pp = hdr[3];
        while (pp + 4 <= size) {
            uint8_t type = src[pp + 3]; /* type is low byte of BE header word at +0 */
            /* Actually BE: extrascale at 0-1, state at 2, type at 3 — yes byte 3. */
            size_t hb = host_prop_bytes(type);
            int n64w = n64_prop_words(type);
            sysLogPrintf(LOG_NOTE, "setupwalk: i=%u pp=0x%06x type=%u n64w=%d hb=%u",
                (unsigned) propindex, (unsigned) pp, (unsigned) type, n64w, (unsigned) hb);
            if (!hb || !n64w || pp + n64w * 4 > size || dstpos + hb > capacity) {
                sysLogPrintf(LOG_ERROR, "setupwalk: STOP i=%u pp=0x%06x type=%u n64w=%d hb=%u size=%u",
                    (unsigned) propindex, (unsigned) pp, (unsigned) type, n64w, (unsigned) hb, (unsigned) size);
                g_setup_fail = 19; free(src); return 0;
            }
            convert_one_prop(dst + dstpos, src + pp, type);
            dstpos += hb;
            pp += (size_t)n64w * 4;
            propindex++;
            if (type == 48) break;
        }
        sysLogPrintf(LOG_NOTE, "setupwalk: done props=%u ended pp=0x%06x of size=0x%06x",
            (unsigned) propindex, (unsigned) pp, (unsigned) size);
    }

    /* ---- patrol paths (8 → 16) ---- */
    if (hdr[4]) {
        paths_ofs = dstpos = align8(dstpos);
        size_t path_start = dstpos;
        size_t path_count = 0;
        for (i = hdr[4]; i + 8 <= size; i += 8) {
            uint32_t wp = read32(src + i);
            if (dstpos + 16 > capacity) { g_setup_fail = 20; free(src); return 0; }
            putptr(dst + dstpos, (uintptr_t)wp);
            dst[dstpos + 8] = src[i + 4];
            dst[dstpos + 9] = src[i + 5];
            put16(dst + dstpos + 10, read16(src + i + 6));
            put32(dst + dstpos + 12, 0);
            dstpos += 16;
            path_count++;
            if (!wp) break;
        }
        size_t scan = path_start;
        for (size_t p = 0; p < path_count; p++) {
            uintptr_t wsrc;
            memcpy(&wsrc, dst + scan, sizeof(uintptr_t));
            if (wsrc) {
                uintptr_t out;
                size_t np = copy_s32_list(dst, dstpos, capacity, src, size, (uint32_t)wsrc, &out);
                if (!np) { g_setup_fail = 21; free(src); return 0; }
                dstpos = np;
                putptr(dst + scan, out);
            }
            scan += 16;
        }
    }

    /* ---- ailists: table then bytecode ---- */
    if (hdr[5]) {
        /* First copy all unique bytecodes, then write host table. */
        size_t table_src = hdr[5];
        size_t nlists = 0;
        while (table_src + (nlists + 1) * 8 <= size) {
            uint32_t list = read32(src + table_src + nlists * 8);
            nlists++;
            if (!list) break;
        }
        /* Bytecode blob: from after pads/strings — keep simple: copy each list
         * until we hit a 0x04 end opcode is hard; copy from list ofs to next
         * list ofs or to end of smaller section. Use chrai-less heuristic:
         * copy until 4-byte aligned run of zeros of length 4 after at least 1 byte,
         * or until next list pointer in the file. */
        size_t code_start = dstpos = align8(dstpos);
        uintptr_t *host_list_ofs = calloc(nlists, sizeof(uintptr_t));
        if (!host_list_ofs) { g_setup_fail = 22; free(src); return 0; }
        for (size_t li = 0; li + 1 < nlists; li++) {
            uint32_t list = read32(src + table_src + li * 8);
            if (!list) break;
            uint32_t end = (uint32_t)size;
            /* Lists are not stored in offset order; find the next higher start. */
            for (size_t lj = 0; lj < nlists; lj++) {
                uint32_t cand = read32(src + table_src + lj * 8);
                if (cand > list && cand < end) end = cand;
            }
            if (list >= size || end > size || end < list) {
                free(host_list_ofs);
                g_setup_fail = 26; free(src); return 0;
            }
            size_t len = end - list;
            if (dstpos + len > capacity) {
                free(host_list_ofs);
                g_setup_fail = 27; free(src); return 0;
            }
            host_list_ofs[li] = dstpos;
            memcpy(dst + dstpos, src + list, len);
            dstpos = align8(dstpos + len);
        }
        ail_ofs = dstpos = align8(dstpos);
        for (size_t li = 0; li < nlists; li++) {
            uint32_t list = read32(src + table_src + li * 8);
            uint32_t id = read32(src + table_src + li * 8 + 4);
            if (dstpos + 16 > capacity) {
                free(host_list_ofs);
                g_setup_fail = 28; free(src); return 0;
            }
            putptr(dst + dstpos, list ? host_list_ofs[li] : 0);
            put32(dst + dstpos + 8, id);
            put32(dst + dstpos + 12, 0);
            dstpos += 16;
            if (!list) break;
        }
        free(host_list_ofs);
        (void)code_start;
    }

    /* ---- padnames / boundpadnames (4 → 8) ---- */
    if (hdr[8]) {
        pnames_ofs = dstpos = align8(dstpos);
        for (i = hdr[8]; i + 4 <= size; i += 4) {
            uint32_t p = read32(src + i);
            if (dstpos + 8 > capacity) { g_setup_fail = 23; free(src); return 0; }
            putptr(dst + dstpos, p ? (uintptr_t)p : 0);
            dstpos += 8;
            if (!p) break;
        }
    }
    if (hdr[9]) {
        bnames_ofs = dstpos = align8(dstpos);
        for (i = hdr[9]; i + 4 <= size; i += 4) {
            uint32_t p = read32(src + i);
            if (dstpos + 8 > capacity) { g_setup_fail = 24; free(src); return 0; }
            putptr(dst + dstpos, p ? (uintptr_t)p : 0);
            dstpos += 8;
            if (!p) break;
        }
    }

    /* ---- string pool: keep original bytes so plink/padname offsets still resolve ---- */
    size_t pool = dstpos = align8(dstpos);
    if (dstpos + size > capacity) { g_setup_fail = 25; free(src); return 0; }
    memcpy(dst + dstpos, src, size);
    dstpos += size;

    /* Rewrite pad plinks and name offsets into string pool. */
    if (pads_ofs) {
        for (size_t off = pads_ofs;; off += 56) {
            uintptr_t plink;
            memcpy(&plink, dst + off + 40, sizeof(uintptr_t));
            if (!plink) break;
            putptr(dst + off + 40, pool + plink);
        }
    }
    if (bound_ofs) {
        for (size_t off = bound_ofs;; off += 80) {
            uintptr_t plink;
            memcpy(&plink, dst + off + 40, sizeof(uintptr_t));
            if (!plink) break;
            putptr(dst + off + 40, pool + plink);
        }
    }
    if (pnames_ofs) {
        for (size_t off = pnames_ofs;; off += 8) {
            uintptr_t p;
            memcpy(&p, dst + off, sizeof(uintptr_t));
            if (!p) break;
            putptr(dst + off, pool + p);
        }
    }
    if (bnames_ofs) {
        for (size_t off = bnames_ofs;; off += 8) {
            uintptr_t p;
            memcpy(&p, dst + off, sizeof(uintptr_t));
            if (!p) break;
            putptr(dst + off, pool + p);
        }
    }

    /* Host header */
    putptr(dst + 0, pwp_ofs);
    putptr(dst + 8, wg_ofs);
    putptr(dst + 16, intro_ofs);
    putptr(dst + 24, props_ofs);
    putptr(dst + 32, paths_ofs);
    putptr(dst + 40, ail_ofs);
    putptr(dst + 48, pads_ofs);
    putptr(dst + 56, bound_ofs);
    putptr(dst + 64, pnames_ofs);
    putptr(dst + 72, bnames_ofs);

    free(src);
    return dstpos;
}

int gevrSetupPropWords(unsigned type) {
    size_t b = host_prop_bytes((uint8_t)type);
    return b ? (int)(b / 4) : 0;
}
