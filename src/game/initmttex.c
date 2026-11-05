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
         * backing, came out as garbage ("texLoad: pool ... full"). Solo too
         * since third person (#230): Bond's own body and head load here on
         * top of the stage's, and on Dam the bullet holes (2168, 2170) and
         * Bond's face came out as garbage (headset log, 2026-11-05). The
         * stage bank has megabytes to spare (7 MB left after Dam's load);
         * twice the room, offline as online.
         */
        s32 size = bytes * 2;

        texInitPool(&ptr_texture_alloc_start, mempAllocBytesInBank(size, MEMPOOL_STAGE), size);
        {
            /* Bean's folder repaints are bound by this pool's addresses */
            extern void gevrBeanRepaintForget(void);
            gevrBeanRepaintForget();
        }
    }
#else
    texInitPool(&ptr_texture_alloc_start, mempAllocBytesInBank(bytes, MEMPOOL_STAGE), bytes);
#endif
}
