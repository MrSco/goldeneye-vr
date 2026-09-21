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

    texInitPool(&ptr_texture_alloc_start, mempAllocBytesInBank(bytes, MEMPOOL_STAGE), bytes);
}
