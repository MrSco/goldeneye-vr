#include "gevr_romcheck.h"
#include "gevr_rom_manifest.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

int gevrRomNormaliseOrder(unsigned char *rom, unsigned int size)
{
    unsigned int stride;
    if (!rom || size < 4) return 0;
    if (!memcmp(rom, "\x80\x37\x12\x40", 4)) return 1;
    if (!memcmp(rom, "\x37\x80\x40\x12", 4)) stride = 2;
    else if (!memcmp(rom, "\x40\x12\x37\x80", 4)) stride = 4;
    else return 0;
    if (size % stride) return 0;
    for (unsigned int i = 0; i < size; i += stride)
        for (unsigned int j = 0; j < stride / 2; j++) {
            unsigned char tmp = rom[i + j];
            rom[i + j] = rom[i + stride - 1 - j];
            rom[i + stride - 1 - j] = tmp;
        }
    return 1;
}

int gevrInflate1172(const unsigned char *src, unsigned int srcSize,
                    unsigned char *dst, unsigned int capacity,
                    unsigned int *written)
{
    unsigned char scratch[16384];
    unsigned char *copy = NULL;
    z_stream stream = {0};
    int result;
    if (written) *written = 0;
    if (!src || srcSize < 3 || src[0] != 0x11 || src[1] != 0x72)
        return Z_DATA_ERROR;

    /* load_resource parks its input at the end of the output allocation.
     * zlib requires the unread input to survive expansion. */
    if (dst && (uintptr_t)src >= (uintptr_t)dst &&
        (uintptr_t)src - (uintptr_t)dst < capacity) {
        copy = malloc(srcSize);
        if (!copy) return Z_MEM_ERROR;
        memcpy(copy, src, srcSize);
        src = copy;
    }
    stream.next_in = (Bytef *)src + 2;
    stream.avail_in = srcSize - 2;
    result = inflateInit2(&stream, -MAX_WBITS);
    if (result != Z_OK) {
        free(copy);
        return result;
    }
    do {
        unsigned int remaining = capacity - (unsigned int)stream.total_out;
        /* One scratch byte allows an exact-capacity stream to consume its end
         * marker; any extra output is rejected without writing past dst. */
        stream.next_out = dst && remaining ? dst + stream.total_out : scratch;
        stream.avail_out = remaining ? (dst ? remaining :
            (remaining < sizeof(scratch) ? remaining : sizeof(scratch))) : 1;
        result = inflate(&stream, Z_NO_FLUSH);
        if (stream.total_out > capacity) {
            result = Z_BUF_ERROR;
            break;
        }
    } while (result == Z_OK);
    if (result == Z_STREAM_END && written) *written = (unsigned int)stream.total_out;
    inflateEnd(&stream);
    free(copy);
    return result == Z_STREAM_END ? Z_OK : result;
}

int gevrRomCheckFiles(const unsigned char *rom, unsigned int size,
                     char *error, size_t errorSize)
{
    if (errorSize) error[0] = '\0';
    for (unsigned int i = 0; i < g_GevrRomFileCount; i++) {
        const struct gevrRomFile *file = &g_GevrRomFiles[i];
        if (!file->offset || !file->size) continue;
        if (!rom || file->offset > size || file->size > size - file->offset) {
            snprintf(error, errorSize, "ROM file %u is outside the cartridge", i);
            return 0;
        }
        if (file->compressed && gevrInflate1172(rom + file->offset, file->size,
                NULL, 16u * 1024u * 1024u, NULL) != Z_OK) {
            snprintf(error, errorSize,
                "ROM game data is damaged or incompatible (file %u at 0x%08X); use an original USA dump",
                i, file->offset);
            return 0;
        }
    }
    return 1;
}
