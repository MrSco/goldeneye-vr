#include <ultra64.h>
#include "chrobjdata.h"
#include "gevr_model.h"
#include "system.h"
#include "image.h"
#include "math_asinfacosf.h"
#include "math_ceil.h"
#include "math_floor.h"
#include "math_unk_05A9E0.h"
#include "model.h"
#include "ob.h"
#include "objecthandler.h"
#include "quaternion.h"
#include "tex.h"


/***
 * Perfect Dark:
 * void modeldef0f1a7560(struct modeldef *modeldef, u16 filenum, u32 arg2, struct modeldef *modeldef2, struct texpool *texpool, bool arg5)
 * 
 * NTSC address 0x7F0762E0.
*/
/*
 * PORT: the same pass in pointer-width arithmetic.
 *
 * After a model file is loaded its display lists are rewritten: the texture
 * pseudo-commands the files carry are expanded into real texture loads by
 * texLoadFromGdl(), which makes each list longer. The original does this in
 * place, using the room the allocation still has past the file. It first
 * moves the tail of the file - every display list; the files keep them last -
 * up to the far end of that room, then writes each expanded list back
 * starting where the first list used to be, and finally trims the allocation
 * to the new end. Display list references are segment-5 offsets throughout,
 * before and after; only the offsets change.
 *
 * The original computed all of this in s32 with pointers folded into the
 * arithmetic, which cannot hold a host address. This keeps the offsets as
 * offsets and the pointers as pointers.
 */
void sub_GAME_7F0762E0(ModelFileHeader *objheader, u8 *name, u8 *dst, struct texpool *buffer)
{
    ModelNode *node;
    ModelNode *curnode;
    Gfx *gdl;          /* a segment-5 reference (0x05000000 | offset), never dereferenced here */
    Gfx *curgdl;
    u8 *filedata = (u8 *)objheader->Switches;
    s32 filenum = fileGetIndex((char *)name);
    s32 romremaining = get_rom_remaining_buffer_for_index(filenum);   /* bytes in the allocation */
    s32 pcremaining = get_pc_remaining_buffer_for_index(filenum);     /* bytes in the file */
    u32 firstofs;      /* where the first display list sits; the expanded ones start there too */
    u32 tailsize;      /* bytes from the first display list to the end of the file */
    u8 *moved;         /* the tail's temporary home, at the end of the allocation */
    u32 replacementofs;
    u32 cursize;

    node = NULL;
    modelIterateDisplayLists(objheader, &node, &gdl);

    if (gdl == NULL)
    {
        return;
    }

    firstofs = (u32)((uintptr_t)gdl & 0x00ffffff);
    tailsize = (u32)pcremaining - firstofs;
    moved = filedata + romremaining - tailsize;

    if (firstofs > (u32)pcremaining || moved < filedata + pcremaining)
    {
        sysLogPrintf(LOG_ERROR, "model %s: no room to rewrite its display lists (file %d bytes, allocation %d)",
                (char *)name, pcremaining, romremaining);
        return;
    }

    texCopyGdls((Gfx *)(filedata + firstofs), (Gfx *)moved, (s32)tailsize);
    texLoadFromModelFileHeader(objheader, buffer);

    replacementofs = firstofs;

    do
    {
        curnode = node;
        curgdl = gdl;
        modelIterateDisplayLists(objheader, &node, &gdl);

        if (gdl != NULL)
        {
            cursize = (u32)((uintptr_t)gdl & 0x00ffffff) - (u32)((uintptr_t)curgdl & 0x00ffffff);
        }
        else
        {
            cursize = (u32)pcremaining - (u32)((uintptr_t)curgdl & 0x00ffffff);
        }

        modelNodeReplaceGdl(0, curnode, curgdl, (Gfx *)(uintptr_t)(0x05000000u | replacementofs));

        replacementofs += texLoadFromGdl((Gfx *)(moved + (((uintptr_t)curgdl & 0x00ffffff) - firstofs)),
                (s32)cursize, (Gfx *)(filedata + replacementofs), buffer);
    }
    while (node != NULL);

    fileSetSize(filenum, filedata, (replacementofs + 0xf) & ~0xfu, dst == NULL);
}


/***
 * NTSC addres 0x7F0764A4.
*/
void load_object_fill_header(struct ModelFileHeader *objheader, u8 *name, u8* dst, s32 size, struct texpool * buffer)
{
    void *filedata;

    /*
     * PORT: load_resource() rebuilds the file in host layout as it lands, and
     * needs the switch and texture counts only this header knows.
     */
    gevrModelPendingHeader = objheader;

    if (dst != 0)
    {
        filedata = _fileNameLoadToAddr(name, 0, dst, size);
    }
    else
    {
        filedata = _fileNameLoadToBank(name, 0, 0x100, 4);
    }

    gevrModelPendingHeader = NULL;

    objheader->Switches = (struct ModelNode **)filedata;

    /* the switch entries are pointer width in the host layout */
    objheader->Textures = (struct ModelFileTextures *)&((uintptr_t *)filedata)[objheader->numSwitches];

    objheader->RootNode = (struct ModelNode *)&objheader->Textures[objheader->numtextures];

    sub_GAME_7F075A90(objheader, 0x5000000, (uintptr_t)filedata);
    sub_GAME_7F0762E0(objheader, name, dst, buffer);
}




void fileLoad(struct ModelFileHeader *header,char *name)
{
   load_object_fill_header(header,name,0,0,0);
   return;
}


void load_object_into_memory_unused_maybe(struct ModelFileHeader *header,int *recallstring,int *targetloc,int sizeleft)
{
   load_object_fill_header(header,recallstring,targetloc,sizeleft,0);
   return;
}





