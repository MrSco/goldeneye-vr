#include "system.h"
#include <ultra64.h>
#include <ramrom.h>
#include <memp.h>
#include "image_bank.h"
#include "gevr_rom_segments.h"
/* The image table symbols, declared with their real types: the local externs
   this replaced called the arrays pointers, so reading one fetched its first
   entry as an address. */
#include <assets/oddtextures.h>

// bss
//8008D0A0
u8* img_curpos;
//8008D0A4
u32 img_curdatatable;
//8008D0A8
s32 img_bitcount;
//8008D0AC
s32 dword_CODE_bss_8008D0AC;
//8008D0B0;
s32 globalbank_rdram_offset;
//8008D0B4;
s32 *pGlobalimagetable;
//8008D0B8;
struct sImageTableEntry *genericimage;
//8008D0BC
struct sImageTableEntry *impactimages;
//8008D0C0
struct sImageTableEntry *explosion_smokeimages;
//8008D0C4
struct sImageTableEntry *scattered_explosions;
//8008D0C8
struct sImageTableEntry *flareimage1;
//8008D0CC
struct sImageTableEntry *flareimage2;
//8008D0D0
struct sImageTableEntry *flareimage3;
//8008D0D4
struct sImageTableEntry *flareimage4;
//8008D0D8
struct sImageTableEntry *flareimage5;
//8008D0DC
struct sImageTableEntry *ammo9mmimage;
//8008D0E0
struct sImageTableEntry *rifleammoimage;
//8008D0E4
struct sImageTableEntry *shotgunammoimage;
//8008D0E8
struct sImageTableEntry *knifeammoimage;
//8008D0EC
struct sImageTableEntry *glaunchammoimage;
//8008D0F0
struct sImageTableEntry *rocketammoimage;
//8008D0F4
struct sImageTableEntry *genericmineammoimage;
//8008D0F8
struct sImageTableEntry *grenadeammoimage;
//8008D0FC
struct sImageTableEntry *magnumammoimage;
//8008D100
struct sImageTableEntry *goldengunammoimage;
//8008D104
struct sImageTableEntry *remotemineammoimage;
//8008D108
struct sImageTableEntry *timedmineammoimage;
//8008D10C
struct sImageTableEntry *proxmineammoimage;
//8008D110
struct sImageTableEntry *tankammoimage;
//8008D114;
struct sImageTableEntry *crosshairimage;
//8008D118
struct sImageTableEntry *betacrosshairimage;
//8008D11C
struct sImageTableEntry *glassoverlayimage;
//8008D120
struct sImageTableEntry *monitorimages;
//8008D124
struct sImageTableEntry *skywaterimages;
//8008D128
struct sImageTableEntry *mainfolderimages;
//8008D12C
struct sImageTableEntry *mpradarimages;
//8008D130
struct sImageTableEntry *mpcharselimages;
//8008D134
struct sImageTableEntry *mpstageselimages;




/* pos is an address; as s32 it lost its top half. */
void texSetBitstring(u8 *pos) {
    img_curpos = pos;
    img_curdatatable = 0;
    img_bitcount = 0;
}



u32 texReadBits(s32 bitCount)
{
    if (img_bitcount < bitCount)
    {
        do
        {
            img_curdatatable = (*img_curpos | (img_curdatatable << 8));
            img_curpos++;
            img_bitcount = img_bitcount + 8;
        } while (img_bitcount < bitCount);
    }
    
    img_bitcount -= bitCount;
    return (img_curdatatable >> img_bitcount) & ((1 << bitCount) - 1);
}





/*
 * PORT: the global image table is the compiled-in data in assets/oddtextures.c,
 * not a copy of the cartridge segment.
 *
 * On the N64 this function copied the Globalimagetable segment into RAM and
 * pointed each of the variables below at an offset inside it, with the copy's
 * base folded into globalbank_rdram_offset so that a segment-0x02 address
 * plus that offset gave a RAM pointer. The segment's contents - seventeen
 * display lists and the image entry arrays - also exist as C in
 * oddtextures.c, already in the layout and byte order the host wants and
 * with pointer-width entries, so they are used directly and no bytes are
 * copied. globalbank_rdram_offset is therefore 0, and pGlobalimagetable
 * points at the first display list only so that the gSPSegment calls that
 * publish it still have an address.
 *
 * Cartridge ammo-icon addresses are resolved by texGetAmmoIcon below.
 */
/* Resolve cartridge identities to typed host entries; never add a ROM offset
 * to a compiled table, whose entries and linker placement have changed. */
struct sImageTableEntry *texGetAmmoIcon(u32 address)
{
    switch (address)
    {
        case 0x02000C84: return s_ammo9mmimage;
        case 0x02000C90: return s_rifleammoimage;
        case 0x02000C9C: return s_shotgunammoimage;
        case 0x02000CA8: return s_knifeammoimage;
        case 0x02000CB4: return s_glammoimage;
        case 0x02000CC0: return s_rocketammoimage;
        case 0x02000CCC: return s_genericmineammoimage;
        case 0x02000CD8: return s_grenadeammoimage;
        case 0x02000CE4: return s_magnumammoimage;
        case 0x02000CF0: return s_goldengunammoimage;
        case 0x02000CFC: return s_remotemineammoimage;
        case 0x02000D08: return s_timedmineammoimage;
        case 0x02000D14: return s_proxmineammoimage;
        case 0x02000D20: return s_tankammoimage;
        default: return NULL;
    }
}

void texReset(void)
{
    s32 i;

    gevrResetStaticTextureIds();

    globalbank_rdram_offset = 0;
    pGlobalimagetable = (s32 *)&globalDL_0x000;

    genericimage = s_genericimage;
    impactimages = s_impactimages;
    explosion_smokeimages = s_explosion_smokeimages;
    scattered_explosions = s_scattered_explosions;
    flareimage1 = s_flareimage1;
    flareimage2 = s_flareimage2;
    flareimage3 = s_flareimage3;
    flareimage4 = s_flareimage4;
    flareimage5 = s_flareimage5;
    ammo9mmimage = s_ammo9mmimage;
    rifleammoimage = s_rifleammoimage;
    shotgunammoimage = s_shotgunammoimage;
    knifeammoimage = s_knifeammoimage;
    glaunchammoimage = s_glammoimage;
    rocketammoimage = s_rocketammoimage;
    genericmineammoimage = s_genericmineammoimage;
    grenadeammoimage = s_grenadeammoimage;
    magnumammoimage = s_magnumammoimage;
    goldengunammoimage = s_goldengunammoimage;
    remotemineammoimage = s_remotemineammoimage;
    timedmineammoimage = s_timedmineammoimage;
    proxmineammoimage = s_proxmineammoimage;
    tankammoimage = s_tankammoimage;
    crosshairimage = s_crosshairimage;
    betacrosshairimage = s_betacrosshairimage;
    glassoverlayimage = s_glassoverlayimage;
    monitorimages = s_monitorimages;
    skywaterimages = s_skywaterimages;
    mainfolderimages = s_mainfolderimages;
    mpradarimages = s_mpradarimages;
    mpcharselimages = s_mpcharselimages;
    mpstageselimages = s_mpstageselimages;

    texLoadFromDisplayList(&globalDL_0x000, 0);
    texLoadFromDisplayList(&globalDL_0x078, 0);
    texLoadFromDisplayList(&globalDL_0x120, 0);
    texLoadFromDisplayList(&globalDL_0x1c8, 0);
    texLoadFromDisplayList(&globalDL_0x270, 0);
    texLoadFromDisplayList(&globalDL_0x318, 0);
    texLoadFromDisplayList(&globalDL_0x3c0, 0);
    texLoadFromDisplayList(&globalDL_0x468, 0);
    texLoadFromDisplayList(&globalDL_0x510, 0);
    texLoadFromDisplayList(&globalDL_0x5b8, 0);
    texLoadFromDisplayList(&globalDL_0x660, 0);
    texLoadFromDisplayList(&globalDL_0x708, 0);
    texLoadFromDisplayList(&globalDL_0x7b0, 0);
    texLoadFromDisplayList(&globalDL_0x858, 0);
    texLoadFromDisplayList(&globalDL_0x900, 0);
    texLoadFromDisplayList(&globalDL_0x9a8, 0);
    texLoadFromDisplayList(&globalDL_0xa50, 0);

    texLoad(&genericimage->index, 0);

    for (i=0; i < 6; i++)
    {
        texLoad(&explosion_smokeimages[i].index, 0);
    }

    for (i=0; i < 5; i++)
    {
        texLoad(&scattered_explosions[i].index, 0);
    }
}
