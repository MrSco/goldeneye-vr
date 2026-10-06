"""Run the real GE-X converter on a synthetic gun, optionally a local GE-X ROM.

Usage: python port/tests/test_gex_muzzle.py [path/to/gex.z64]
No ROM bytes or converted assets are saved outside the temporary directory.
"""
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]


def u32(data, offset):
    return struct.unpack_from(">I", data, offset)[0]


def pointer(data, offset):
    return u32(data, offset) & 0xFFFFFF


def nodes(data, root):
    while root:
        yield root
        child = pointer(data, root + 20)
        if child:
            yield from nodes(data, child)
        root = pointer(data, root + 12)


def commands(data, offset):
    while offset:
        w0, w1 = struct.unpack_from(">II", data, offset)
        yield w0, w1
        if w0 >> 24 == 0xB8:
            return
        offset += 8


def fixture():
    data = bytearray(0x200)
    seg = lambda value: 0x05000000 | value if value else 0
    struct.pack_into(">IIIhhfhhI", data, 0, seg(0x40), 0, seg(0x20), 1, 41, 1.0, 0, 0, 0)
    struct.pack_into(">Ih", data, 0x20, seg(0x58), 90)
    for offset, kind, record, parent, nxt, prev, child in (
        (0x40, 2, 0xC0, 0, 0, 0, 0x58),
        (0x58, 0x12, 0xDC, 0x40, 0xA0, 0, 0x70),
        (0x70, 4, 0xE4, 0x58, 0x88, 0, 0),
        (0x88, 0x16, 0xF8, 0x58, 0, 0x70, 0),
        (0xA0, 4, 0x108, 0x40, 0, 0x58, 0),
    ):
        struct.pack_into(">HxxIIIII", data, offset, kind, seg(record),
                         seg(parent), seg(nxt), seg(prev), seg(child))
    struct.pack_into(">fffhhhhf", data, 0xC0, 0, 0, 0, 0, 0, -1, -1, 0)
    struct.pack_into(">II", data, 0xDC, seg(0x70), 0)
    struct.pack_into(">IIIIhh", data, 0xE4, seg(0x1B0), 0, 0, seg(0x120), 4, 0)
    struct.pack_into(">IIII", data, 0xF8, 1, seg(0x150), seg(0x1D0), 0)
    struct.pack_into(">IIIIhh", data, 0x108, seg(0x1E0), 0, 0, seg(0x180), 4, 3)
    for start, z in ((0x120, 0), (0x150, 8), (0x180, 100)):
        for index, (x, y) in enumerate(((-106, -106), (106, -106), (106, 106), (-106, 106))):
            struct.pack_into(">hhhHhh", data, start + index * 12, x, y, z, 0, 0, 0)
    for offset, words in (
        (0x1B0, (0x01020040, 0x03000940, 0x04300030, seg(0x120),
                  0x01020040, 0x03000900, 0xB8000000, 0)),
        (0x1D0, (0x04300030, 0x04000000, 0xB8000000, 0)),
        (0x1E0, (0x01020040, 0x03000040, 0x04300030, seg(0x180), 0xB8000000, 0)),
    ):
        struct.pack_into(">" + "I" * len(words), data, offset, *words)
    return data


HARNESS = r'''
#include <stdio.h>
#include <stdlib.h>
#include "gevr_gexmodel.h"
static const char *input;
void sysLogPrintf(s32 level, const char *fmt, ...) {}
const u8 *gevrGexTextureData(s32 id, u32 *len) { return NULL; }
u8 *gevrGexFileLoad(const char *name, u32 *len) {
    FILE *f = fopen(input, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    *len = ftell(f);
    rewind(f);
    u8 *bytes = malloc(*len);
    if (fread(bytes, 1, *len, f) != *len) abort();
    fclose(f);
    return bytes;
}
int main(int argc, char **argv) {
    u32 len = 0;
    u16 matrices = 0, textures = 0;
    s32 parts[2] = {-1, atoi(argv[3])};
    input = argv[1];
    u8 *out = gevrGexBuildModel("test", 2, parts, NULL, &len, &matrices, &textures);
    if (!out) return 1;
    FILE *f = fopen(argv[2], "wb");
    if (!f || fwrite(out, 1, len, f) != len) return 2;
    fclose(f);
    free(out);
    printf("%u\n", matrices);
    return 0;
}
'''


with tempfile.TemporaryDirectory(prefix="gevr-gex-muzzle-") as directory:
    temp = Path(directory)
    source = temp / "converter.c"
    source.write_text(HARNESS, encoding="utf-8")
    exe = temp / "converter.exe"
    subprocess.run([shutil.which("gcc") or "cc", "-std=c11", "-O2", "-D_LANGUAGE_C",
                    "-I" + str(ROOT / "include"), "-I" + str(ROOT / "port/include"),
                    str(source), str(ROOT / "port/src/gevr_gexmodel.c"), "-o", str(exe)], check=True)
    samples = [("synthetic", fixture())]
    if len(sys.argv) > 1:
        sys.path.insert(0, str(ROOT / "tools/gex"))
        from pdrom import PdRom
        rom = PdRom(sys.argv[1])
        samples.append(("GE-X KF7", rom.load("Gak47Z")))
        samples.append(("GE-X PP7", rom.load("GwppkZ")))
    for label, original in samples:
        original = bytes(original)
        input_file = temp / "input.bin"
        output_file = temp / "output.bin"
        input_file.write_bytes(original)
        original_nodes = list(nodes(original, pointer(original, 0)))
        matrices = struct.unpack_from(">H", original, 14)[0]
        for flash_part in (90, -1):
            result = subprocess.run([str(exe), str(input_file), str(output_file), str(flash_part)],
                                    check=True, capture_output=True, text=True)
            assert int(result.stdout) == matrices + (flash_part == 90)
            output = output_file.read_bytes()
            # With two empty/no-texture switch slots the root starts at byte 8;
            # with textures it follows their 12-byte configurations.
            tex_count = struct.unpack_from(">H", original, 22)[0]
            output_nodes = list(nodes(output, 8 + 12 * tex_count))
            assert len(original_nodes) == len(output_nodes)
            toggle = pointer(output, 4) if flash_part == 90 else 0
            flash_nodes = set(nodes(output, pointer(output, toggle + 20))) if toggle else set()
            for old, new in zip(original_nodes, output_nodes):
                kind = original[old + 1]
                old_record, new_record = pointer(original, old + 4), pointer(output, new + 4)
                if kind == 2:
                    assert original[old_record:old_record + 20] == output[new_record:new_record + 20]
                if kind not in (4, 0x16):
                    continue
                vertex_field = 12 if kind == 4 else 4
                dl_field = 0 if kind == 4 else 8
                old_v, new_v = pointer(original, old_record + vertex_field), pointer(output, new_record + vertex_field)
                count = struct.unpack_from(">H", original, old_record + 16)[0] if kind == 4 else u32(original, old_record) * 4
                for index in range(count):
                    assert original[old_v + index * 12:old_v + index * 12 + 6] == output[new_v + index * 16:new_v + index * 16 + 6]
                old_dl, new_dl = pointer(original, old_record + dl_field), pointer(output, new_record + dl_field)
                before, after = list(commands(original, old_dl)), list(commands(output, new_dl))
                if new in flash_nodes:
                    assert after[0] == (0x01020040, 0x03000000 | (matrices * 64))
                    loads = [w1 for w0, w1 in after if w0 >> 24 == 1 and w1 >> 24 == 3]
                    assert loads and all(load == 0x03000000 | (matrices * 64) for load in loads)
                else:
                    assert [c for c in before if c[0] >> 24 == 1] == [c for c in after if c[0] >> 24 == 1]
                assert sum(w0 >> 24 == 4 for w0, _ in before) == sum(w0 >> 24 == 4 for w0, _ in after)
                assert all(w0 & 0xFFFF == 16 * ((old0 & 0xFFFF) // 12)
                           for (old0, _), (w0, _) in zip([c for c in before if c[0] >> 24 == 4],
                                                        [c for c in after if c[0] >> 24 == 4]))
        print(f"PASS: {label}: flash lists share an appended matrix; skeleton, other matrix loads and vertex positions preserved")
