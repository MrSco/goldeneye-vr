"""Exercise the actual briefing conversion and padded 4-bit texture importers."""
from pathlib import Path
import os
import subprocess
import tempfile
from gevr_menu_probe import function

ROOT = Path(__file__).resolve().parents[1]


def main():
    gfx = (ROOT / 'port/fast3d/gfx_pc.cpp').read_text()
    swap = (ROOT / 'port/src/gevr_romswap.c').read_text()
    code = r'''
#include <stdint.h>
#include <string.h>
#include <assert.h>
#include <stdio.h>
typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
#define PD_BE16(v) __builtin_bswap16(v)
#define SCALE_4_8(v) ((v) * 17)
#define SCALE_3_8(v) ((v) * 255 / 7)
static uint8_t tex_upload_buffer[128 * 65 * 4 + 16];
struct LoadedTexture { const uint8_t *addr; };
struct { struct { uint32_t width, height; } texture_tile[1]; } rdp;
static void upload(const uint8_t*, uint32_t w, uint32_t h) {
    assert(w == rdp.texture_tile[0].width && h == rdp.texture_tile[0].height);
}
struct { void (*upload_texture)(const uint8_t*, uint32_t, uint32_t); } api = {upload}, *gfx_rapi = &api;
FUNCTIONS
int main() {
    // IDs from the Dam bank, including the Agent objective and zero sentinel.
    u8 brief[] = {0x2c,0x00, 0x2c,0x07, 0,2, 0,0, 0xee};
    gevrRomSwapBriefing(brief, 8);
    u16 fields[4]; memcpy(fields, brief, 8);
    assert(fields[0] == 0x2c00 && fields[1] == 0x2c07 && fields[2] == 2 && fields[3] == 0);
    assert(brief[8] == 0xee);
    for (uint32_t w : {65u, 95u, 128u}) {
        const uint32_t h = 65, packed = (w + 1) / 2, stride = (packed + 7) & ~7u;
        uint8_t tmem[64*65], linear[64*65];
        memset(tmem, 0xee, sizeof(tmem));
        for (uint32_t y=0; y<h; ++y) for (uint32_t x=0; x<packed; ++x) {
            uint8_t value = (((x*2+y)%16)<<4) | ((x*2+1+y)%16);
            tmem[y*stride + ((y&1) ? (x^4) : x)] = value;
        }
        gfx_unpack_tmem_rows(linear,tmem,stride,packed,h,true);
        rdp.texture_tile[0] = {w,h};
        for (int ia=0; ia<2; ++ia) {
            memset(tex_upload_buffer,0xee,sizeof(tex_upload_buffer));
            if (ia) import_texture_ia4(0,{linear},false);
            else import_texture_i4(0,{linear},false);
            for (uint32_t y=0; y<h; ++y) for (uint32_t x=0; x<w; ++x) {
                unsigned part=(x+y)%16, p=(y*w+x)*4;
                unsigned intensity=ia ? SCALE_3_8(part>>1) : SCALE_4_8(part);
                assert(tex_upload_buffer[p]==intensity && tex_upload_buffer[p+1]==intensity && tex_upload_buffer[p+2]==intensity);
                assert(tex_upload_buffer[p+3]==(ia ? ((part&1)?255:0) : intensity));
            }
            for (uint32_t i=w*h*4; i<sizeof(tex_upload_buffer); ++i) assert(tex_upload_buffer[i]==0xee);
        }
    }
    // Select File: IA8, 122 visible pixels in 128-byte linear rows.
    uint8_t padded[128*18], compact[122*18];
    for (unsigned y=0; y<18; ++y) for (unsigned x=0; x<128; ++x)
        padded[y*128+x] = x<122 ? (x+y)%256 : 0xee;
    gfx_unpack_tmem_rows(compact,padded,128,122,18,false);
    for (unsigned y=0; y<18; ++y) for (unsigned x=0; x<122; ++x)
        assert(compact[y*122+x] == (x+y)%256);
    puts("PASS: briefing u16 conversion; padded/swizzled I4 and IA4 rows at widths 65, 95, 128; output canaries");
}
'''
    extracted = function(swap, 'gevrRomSwapBriefing') + '\n' + '\n'.join(
        function(gfx, name) for name in ('gfx_unpack_tmem_rows', 'import_texture_i4', 'import_texture_ia4'))
    code = '#include <initializer_list>\n' + code.replace('FUNCTIONS', extracted)
    with tempfile.TemporaryDirectory(prefix='gevr-front-') as td:
        src = Path(td) / 'probe.cpp'
        exe = Path(td) / 'probe.exe'
        src.write_text(code)
        compiler = r'C:\Strawberry\c\bin\g++.exe' if os.name == 'nt' else 'g++'
        subprocess.run([compiler, '-std=c++11', str(src), '-o', str(exe)], check=True)
        subprocess.run([str(exe)], check=True, timeout=10)


if __name__ == '__main__':
    main()
