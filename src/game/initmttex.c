#include <ultra64.h>
#include <memp.h>
#include "image.h"
#include "initmttex.h"
#include "token.h" /* tokenFind returns a pointer; without the prototype C assumed int and truncated it */

void set_mt_tex_alloc(void)
{  
    g_TexCacheCount = 0;

    if (tokenFind(1, "-mt"))
    {
        bytes = strtol(tokenFind(1, "-mt"), 0x0, 0) * 1024; //get KB
    }

#ifdef GEVR
    {
        /*
         * Online every player's own character loads its textures here, up to
         * eight (#88; bots fill all eight), where the cartridge sized it for
         * four: the eighth player's skin, then bullet holes and the radar's
         * backing, came out as garbage ("texLoad: pool ... full"). The stage
         * bank has megabytes to spare online; give it twice the room.
         */
        extern bool netIsActive(void);
        s32 size = netIsActive() ? bytes * 2 : bytes;

        texInitPool(&ptr_texture_alloc_start, mempAllocBytesInBank(size, MEMPOOL_STAGE), size);
    }
#else
    texInitPool(&ptr_texture_alloc_start, mempAllocBytesInBank(bytes, MEMPOOL_STAGE), bytes);
#endif
}
