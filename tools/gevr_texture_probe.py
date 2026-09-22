"""Exercise production native texture import with synthetic TMEM, no ROM/GPU."""
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
source = (ROOT / "port/fast3d/gfx_pc.cpp").read_text()
production = source[source.index("static void gfx_native_texture_dimensions("):
                    source.index("static void import_texture(int i,")]
header = (ROOT / "port/fast3d/gfx_pc.h").read_text()
key = header[header.index("struct TextureCacheKey {"):header.index("typedef std::unordered_map")]
stub = r"""
#include <cstdint>
#include <cstddef>
#include <cassert>
#include <cstdlib>
#include <vector>
#include <cstdio>
#define G_IM_FMT_RGBA 0
#define G_IM_FMT_CI 2
#define G_IM_FMT_IA 3
#define G_IM_FMT_I 4
#define G_IM_SIZ_4b 0
#define G_IM_SIZ_8b 1
#define G_IM_SIZ_16b 2
#define G_IM_SIZ_32b 3
#define G_TT_NONE 0
#define G_TT_IA16 49152
#define SCALE_3_8(x) ((x)*255/7)
#define SCALE_4_8(x) ((x)*17)
#define SCALE_5_8(x) ((x)*255/31)
#define PD_BE32(x) __builtin_bswap32(x)
static void sysFatalError(const char*, ...) { abort(); }
struct LoadedTexture {
    const uint8_t* addr;
    uint32_t orig_size_bytes, size_bytes, full_image_line_size_bytes;
    bool loaded_by_tile = false, tmem_swizzled = false;
};
struct Tile { uint32_t width, height, line_size_bytes, siz, fmt, palette; };
static struct { Tile texture_tile[8]; uint16_t palette[256]; uint32_t palette_fmt; bool tex_lod; } rdp;
static uint8_t tex_upload_buffer[65536];
static uint32_t upload_w, upload_h;
static std::vector<uint8_t> pixels;
struct API { void upload_texture(const uint8_t* p, uint32_t w, uint32_t h) {
    upload_w=w; upload_h=h; pixels.assign(p,p+w*h*4);
} } api;
static API* gfx_rapi=&api;
"""
tests = r"""
int main() {
    for (unsigned i=0;i<256;++i) rdp.palette[i]=(i<<8)|255;
    rdp.palette_fmt=G_TT_IA16;
    // CI4 HUD: 16x16 base plus poisoned mip bytes must never become rows.
    rdp.texture_tile[0]={16,16,8,0,G_IM_FMT_CI,0}; rdp.tex_lod=true;
    std::vector<uint8_t> data(176,0xee);
    for (unsigned i=0;i<128;++i) data[i]=0x12;
    LoadedTexture load{data.data(),176,176,176};
    importTextureNative(0,load,true);
    assert(upload_w==16 && upload_h==16 && pixels[0]==1 && pixels[4]==2);
    auto rect=pixels; importTextureNative(0,load,false); assert(rect==pixels);
    // Non-LOD SETTILESIZE 1x1 is a window; retain the 16x64 loaded image.
    rdp.tex_lod=false; rdp.texture_tile[0]={1,1,16,1,G_IM_FMT_CI,0};
    data.assign(1024,7); load={data.data(),1024,1024,1024};
    importTextureNative(0,load,false);
    assert(upload_w==16 && upload_h==64 && pixels.back()==255);
    // LoadTile rows are 16 bytes apart, eight useful pixels per row.
    rdp.texture_tile[0]={8,2,8,1,G_IM_FMT_CI,0};
    data.assign(32,99); for(unsigned i=0;i<8;++i) { data[i]=3; data[16+i]=5; }
    load={data.data(),16,16,16,true,false}; importTextureNative(0,load,false);
    assert(upload_w==8 && upload_h==2 && pixels[0]==3 && pixels[32]==5);
    // Odd TMEM row's two words are reversed; unwrap exactly once.
    for(unsigned i=0;i<8;++i) { data[i]=i; data[8+(i^4)]=i+8; }
    load={data.data(),16,16,16,false,true}; importTextureNative(0,load,false);
    for(unsigned i=0;i<16;++i) assert(pixels[i*4]==i);
    // CI with no TLUT is intensity, regardless of stale palette entries.
    rdp.palette_fmt=G_TT_NONE; importTextureNative(0,load,false);
    for(unsigned i=0;i<16;++i) assert(pixels[i*4]==i && pixels[i*4+3]==i);
    TextureCacheKey a{}, b{};
    b.width=16; assert(!(a==b)); b=a; b.palette_hash=1; assert(!(a==b));
    b=a; b.palette_fmt=G_TT_IA16; assert(!(a==b));
    b=a; b.source_pitch=16; assert(!(a==b)); b=a; b.swizzled=true; assert(!(a==b));
    puts("PASS: CI4 HUD/mips, subtile image, pitched rows, TMEM swizzle, CI/no-TLUT, cache variants");
}
"""
with tempfile.TemporaryDirectory(prefix="gevr-textures-") as tmp:
    tmp = Path(tmp)
    code = tmp / "texture.cpp"
    code.write_text(stub + key + production + tests)
    exe = tmp / ("texture.exe" if os.name == "nt" else "texture")
    compiler = r"C:\Strawberry\c\bin\g++.exe" if os.name == "nt" else "c++"
    subprocess.run([compiler, "-std=c++20", "-O2", str(code), "-o", str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
