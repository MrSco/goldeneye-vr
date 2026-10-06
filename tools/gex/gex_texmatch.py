"""Match GE-X weapon textures by decoded RGBA pixels, not compressed bytes.

Usage: python tools/gex/gex_texmatch.py GEX.z64 GE.z64 slot [--ge-model NAME]
Both ROMs remain local. The non-zlib codecs are the production decoder;
zlib palette data is decoded with Python zlib. No image assets are emitted.
Only unambiguous exact matches become C pairs. Other matches are reported.
"""
import argparse
import atexit
import ctypes
from pathlib import Path
import re
import shutil
import struct
import subprocess
import tempfile
import zlib
import sys

from gex_extract import inspect
from pdrom import PdRom

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT/"tools"))
from gevr_tex_decode import Bits, colour, texture_offsets, images_segment
from gevr_model_export import header_counts
from gevr_model_probe import load_rom, manifest_lookup, decompress_1172


def function(source, name):
    match = re.search(r"^\w+ " + name + r"\([^;]*?\)\s*\{", source, re.M)
    if not match: raise ValueError(name)
    end, depth = match.end(), 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[match.start():end]


class Decoder:
    def __init__(self):
        self.temp = tempfile.TemporaryDirectory(prefix="gex-tex-", ignore_cleanup_errors=True)
        folder = Path(self.temp.name)
        source = (ROOT/"src/game/image.c").read_text()
        source = source[:source.index("void texLoad(")]
        for name in ("texInflateZlib", "texShrinkPaletted", "texLoadFromDisplayList"):
            source = source.replace(function(source,name), "")
        bank = (ROOT/"src/game/image_bank.c").read_text()
        source += "\nu8 *img_curpos; s32 img_bitcount, img_curdatatable;\n"
        source += function(bank,"texSetBitstring") + "\n" + function(bank,"texReadBits")
        source += '''
#ifdef _WIN32
#define EXPORT __declspec(dllexport)
#else
#define EXPORT __attribute__((visibility("default")))
#endif
EXPORT int decodeTexture(u8 *src, u8 *dst, int *meta) {
    struct tex tex = {0}; struct texpool pool = {0};
    pool.rightpos=&tex; g_TexCacheCount=0;
    int size=texInflateNonZlib(src+1,dst,0,0,&pool);
    meta[0]=tex.width; meta[1]=tex.height; meta[2]=src[1]>>4;
    return size;
}
'''
        file = folder/"decoder.c"
        file.write_text(source)
        output = folder/("decoder.dll" if __import__('os').name == 'nt' else "decoder.so")
        result = subprocess.run([shutil.which("gcc") or "cc", "-shared", "-O2", "-fPIC",
                        "-ffunction-sections", "-fdata-sections", "-Wl,--gc-sections",
                        "-fvisibility=hidden", "-D_LANGUAGE_C", "-DGEVR", "-DVERSION_US",
                        "-I"+str(ROOT), "-I"+str(ROOT/"include"), "-I"+str(ROOT/"src"),
                        "-I"+str(ROOT/"src/game"), "-I"+str(ROOT/"port/include"),
                        str(file), "-lm", "-o", str(output)],
                       stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        if result.returncode: raise RuntimeError(result.stderr.decode(errors='replace'))
        self.lib = ctypes.CDLL(str(output))
        self.lib.decodeTexture.argtypes = [ctypes.c_void_p]*3
        atexit.register(self.close)

    def close(self):
        if self.lib is not None:
            if __import__('os').name == 'nt':
                from _ctypes import FreeLibrary
                FreeLibrary(self.lib._handle)
            else:
                from _ctypes import dlclose
                dlclose(self.lib._handle)
            self.lib=None
        self.temp.cleanup()

    def decode(self, blob):
        if blob[0] & 64:
            bits=Bits(blob,1); fmt=bits.read(8); count=bits.read(8)+1
            palette=[bits.read(16) for _ in range(count)]
            w,h=bits.read(8),bits.read(8)
            kind="IA16" if fmt in (11,12) else "RGBA16"
            start=bits.pos
            skip=5 if blob[start:start+2] == b'\x11\x73' else 2
            raw=zlib.decompress(bytes(blob[start+skip:]),-15)
            indices=list(raw) if fmt in (9,11) else [n for b in raw for n in (b>>4,b&15)]
            if len(indices)<w*h: raise ValueError("Truncated palette indices")
            return w,h,bytes(c for n in indices[:w*h] for c in colour(kind,palette[n]))
        meta=(ctypes.c_int*3)(); out=ctypes.create_string_buffer(65536)
        size=self.lib.decodeTexture(ctypes.create_string_buffer(bytes(blob)+bytes(16)),out,meta)
        w,h,fmt=meta
        if size<=0 or fmt>8: raise ValueError("Unsupported/invalid non-zlib texture")
        bpp=(32,16,32,16,16,8,4,8,4)[fmt]
        stride=((w*bpp+7)//8+7)&~7
        pixels=[]
        for y in range(h):
            for x in range(w):
                offset=y*stride+x*bpp//8
                if fmt in (0,2):
                    n=struct.unpack_from('<I',out.raw,offset)[0]
                    pixel=(n>>24,(n>>16)&255,(n>>8)&255,n&255)
                elif fmt in (1,3,4):
                    n=struct.unpack_from('>H',out.raw,offset)[0]
                    pixel=colour("IA16" if fmt==4 else "RGBA16",n)
                else:
                    n=out.raw[offset]
                    if bpp==4: n=(n>>4) if x%2==0 else n&15
                    if fmt==5: pixel=((n>>4)*17,)*3+((n&15)*17,)
                    elif fmt==6: pixel=((n>>1)*255//7,)*3+(255 if n&1 else 0,)
                    else:
                        intensity=n if fmt==7 else n*17
                        pixel=(intensity,)*4
                pixels.extend(pixel)
        return w,h,bytes(pixels)


def gex_texture(rom, number):
    offset=struct.unpack_from('>I',rom,0x1e77400+8*number)[0]&0xffffff
    following=struct.unpack_from('>I',rom,0x1e77400+8*(number+1))[0]&0xffffff
    end=following if following>offset else len(rom)-0x1b449a2
    return rom[0x1b449a2+offset:0x1b449a2+end]


def dimensions(blob):
    bits=Bits(blob,1)
    if blob[0]&64:
        fmt=bits.read(8); count=bits.read(8)+1
        if fmt not in (9,10,11,12): return None
        for _ in range(count): bits.read(16)
        return bits.read(8),bits.read(8)
    fmt=bits.read(4); w,h=bits.read(8),bits.read(8); method=bits.read(4)
    return (w,h) if fmt<=8 and method<=9 else None


if __name__ == '__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('gex_rom'); parser.add_argument('ge_rom'); parser.add_argument('slot',type=int)
    parser.add_argument('--ge-model')
    args=parser.parse_args()
    gex=PdRom(args.gex_rom); report=inspect(gex,args.slot); ge=load_rom(args.ge_rom)
    model=args.ge_model or ('GwppksilZ' if args.slot==4 else report['model'])
    ofs,length=manifest_lookup(model); ns,nm,nt=header_counts(model)
    data=decompress_1172(ge[ofs:ofs+length])
    candidates=sorted({struct.unpack_from('>I',data,4*ns+12*i)[0]&0xfff for i in range(nt)})
    offsets=texture_offsets(); base,_=images_segment(); decoder=Decoder(); decoded={}
    targets={n:decoder.decode(gex_texture(gex.rom,n)) for n in sorted(set(report['textures']))}
    shapes={v[:2] for v in targets.values()}
    for n in range(len(offsets)-1):
        blob=ge[base+offsets[n]:base+offsets[n+1]]
        if dimensions(blob) in shapes:
            try: decoded[n]=decoder.decode(blob)
            except (ValueError,IndexError,zlib.error): continue
    pairs=[]
    for n,target in targets.items():
        all_matches=[k for k,v in decoded.items() if target==v]
        preferred=[k for k in all_matches if k in candidates]
        matches=preferred if len(preferred)==1 else all_matches
        if len(matches)==1:
            pairs.extend((n,matches[0])); print(f'/* exact: {n} -> {matches[0]} */')
        else: print(f'/* {n}: {"ambiguous " + str(matches) if matches else "unmatched; keep GE-X texture"} */')
    print('static const u16 texturePairs[] = { '+', '.join(str(n) for n in pairs)+(', ' if pairs else '')+'0 };')
    decoder.close()
