"""Synthetic headers for Goldfinger 64 and Tomorrow Never Dies.

No copyrighted ROM is built or read. The layout rows are matched on size,
title, code and the eight CRC bytes, and the file table, image word and
Expansion Pak window are exercised with made-up bytes.
"""
import ctypes as C
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
DATA_VRAM = 0x80020D90
FILES_AT = 0x252C4


def be32(n):
    return bytes([(n >> 24) & 255, (n >> 16) & 255, (n >> 8) & 255, n & 255])


def header(title, code, crc, size):
    blob = bytearray(64)
    blob[0:4] = b"\x80\x37\x12\x40"
    blob[0x10:0x18] = bytes(crc)
    blob[0x20:0x20 + len(title)] = title
    blob[0x3B:0x3F] = code
    return bytes(blob), size


def build():
    directory = Path(tempfile.mkdtemp(prefix="gevr-romlayout-"))
    compiler = shutil.which("gcc") or "gcc"
    library = directory / "romlayout.so"
    subprocess.run([compiler, "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror",
                    "-shared", "-fPIC",
                    "-I" + str(ROOT / "port/include"),
                    str(ROOT / "port/src/gevr_romlayout.c"),
                    "-o", str(library)], check=True)
    lib = C.CDLL(str(library))
    lib.gevrRomLayoutMatchHeader.argtypes = [C.c_void_p, C.c_uint32]
    lib.gevrRomLayoutMatchHeader.restype = C.c_void_p
    lib.gevrRomCampaignSet.argtypes = [C.c_int]
    lib.gevrRomCampaign.restype = C.c_int
    lib.gevrRomIsVariant.restype = C.c_int
    lib.gevrRomActiveLayout.restype = C.c_void_p
    lib.gevrRomDataMount.argtypes = [C.c_void_p, C.c_void_p, C.c_uint32,
                                     C.c_void_p, C.c_uint32, C.c_void_p]
    lib.gevrRomDataMount.restype = C.c_int
    lib.gevrRomDataBytes.argtypes = [C.c_void_p, C.c_uint32, C.c_uint32]
    lib.gevrRomDataBytes.restype = C.c_void_p
    lib.gevrRomDataFree.argtypes = [C.c_void_p]
    lib.gevrRomParseFiles.argtypes = [C.c_void_p, C.c_uint32, C.c_void_p, C.c_int]
    lib.gevrRomParseFiles.restype = C.c_int
    lib.gevrRomDecodeImage.argtypes = [C.c_uint32, C.c_uint32, C.c_uint32,
                                       C.POINTER(C.c_uint32), C.POINTER(C.c_uint32),
                                       C.POINTER(C.c_uint32)]
    lib.gevrRomBuildItemMap.argtypes = [C.POINTER(C.c_char_p), C.c_int]
    lib.gevrRomItemHost.argtypes = [C.c_int]
    lib.gevrRomItemHost.restype = C.c_int
    lib.gevrRomHeldProp.argtypes = [C.c_int]
    lib.gevrRomHeldProp.restype = C.c_int
    lib.gevrRomClearItemMap.argtypes = []
    lib.gevrRomAiPrepare.argtypes = [C.c_void_p, C.c_uint32, C.c_int]
    lib.gevrRomAiPrepare.restype = C.c_uint32
    lib.gevrRomCreditsAfter.restype = C.c_int
    lib.gevrRomAdvanceBelow.restype = C.c_int
    lib.gevrRomMissionLimit.restype = C.c_int
    return lib


class FileRow(C.Structure):
    _fields_ = [("index", C.c_uint32), ("addr", C.c_uint32),
                ("size", C.c_uint32), ("name", C.c_char_p)]


class DataWindow(C.Structure):
    _fields_ = [("bytes", C.c_void_p), ("length", C.c_uint32)]


def tag_of(lib, ptr):
    # struct starts with name, tag (two pointers)
    if not ptr:
        return None
    name = C.cast(ptr, C.POINTER(C.c_char_p))
    return name[1].decode()


def test_headers(lib):
    us, us_size = header(b"GOLDENEYE", b"NGEE",
                         [0xdc, 0xbc, 0x50, 0xd1, 0x09, 0xfd, 0x1a, 0xa3], 0xC00000)
    got = lib.gevrRomLayoutMatchHeader(us, us_size)
    assert tag_of(lib, got) == "ge"
    lib.gevrRomCampaignSet(0)
    assert lib.gevrRomIsVariant() == 0
    assert tag_of(lib, lib.gevrRomActiveLayout()) == "ge"

    mirror, mirror_size = header(b"GOLDENEYE", b"NGEE",
                                 [0x94, 0xbd, 0x9a, 0x93, 0x4f, 0x82, 0x1a, 0xc8], 0xC00000)
    assert tag_of(lib, lib.gevrRomLayoutMatchHeader(mirror, mirror_size)) == "mirror"
    assert not lib.gevrRomLayoutMatchHeader(mirror, 0x1000000)
    damaged = bytearray(mirror)
    damaged[0x10] ^= 1
    assert not lib.gevrRomLayoutMatchHeader(bytes(damaged), mirror_size)
    lib.gevrRomCampaignSet(3)
    assert lib.gevrRomCampaign() == 3 and lib.gevrRomIsVariant() == 1
    assert tag_of(lib, lib.gevrRomActiveLayout()) == "mirror"
    assert lib.gevrRomMissionLimit() == 20
    assert lib.gevrRomCreditsAfter() == lib.gevrRomAdvanceBelow() == 0
    for invalid in (-1, 4, 99):
        lib.gevrRomCampaignSet(invalid)
        assert lib.gevrRomCampaign() == 0

    gf, gf_size = header(b"GOLDFINGER", b"NGFE",
                         [0xb2, 0x24, 0x27, 0x48, 0xff, 0xbd, 0x61, 0xda], 0x1800000)
    assert tag_of(lib, lib.gevrRomLayoutMatchHeader(gf, gf_size)) == "gf64"
    bad = bytearray(gf)
    bad[0x10] ^= 0xFF
    assert lib.gevrRomLayoutMatchHeader(bytes(bad), gf_size) is None or \
        tag_of(lib, lib.gevrRomLayoutMatchHeader(bytes(bad), gf_size)) is None
    assert not lib.gevrRomLayoutMatchHeader(bytes(bad), gf_size)

    tnd, tnd_size = header(b"GOLDENEYE", b"NGEE",
                           [0xd2, 0x24, 0x26, 0x04, 0xf7, 0xa5, 0x07, 0x84], 0x1000000)
    assert tag_of(lib, lib.gevrRomLayoutMatchHeader(tnd, tnd_size)) == "tnd64"
    random_tnd = bytearray(tnd)
    random_tnd[0x10:0x18] = b"\x11\x22\x33\x44\x55\x66\x77\x88"
    assert not lib.gevrRomLayoutMatchHeader(bytes(random_tnd), tnd_size)
    # A 12 MB GOLDENEYE / NGEE with the TND CRC is still retail-sized, so it is not TND.
    assert not lib.gevrRomLayoutMatchHeader(tnd, 0xC00000)

    lib.gevrRomCampaignSet(2)
    assert lib.gevrRomIsVariant() == 1
    assert lib.gevrRomMissionLimit() == 14
    assert lib.gevrRomCreditsAfter() == 9
    assert lib.gevrRomAdvanceBelow() == 13
    lib.gevrRomCampaignSet(1)
    assert lib.gevrRomMissionLimit() == 20
    # Goldfinger plays its Luger (taser file) and P38 (watch laser file) as the PPK
    assert [lib.gevrItemAs(i) for i in (31, 23, 17, 4)] == [4, 4, 17, 4]
    # its Cradle/Dam level cases are off and it uses items 49 and 57 on objects
    assert [lib.gevrRomBgCradle(), lib.gevrRomBgDam(), lib.gevrRomGadgetUse(0), lib.gevrRomGadgetUse(1)] == [37, 37, 49, 57]

    def patches():
        return [lib.gevrRomAutogunMask(), lib.gevrRomGlassPen(0), lib.gevrRomGlassPen(1), lib.gevrRomTimedCase(),
                lib.gevrRomCaseAmmo(), lib.gevrRomButtMelee(), lib.gevrRomCars()]
    # autoguns & 3, glass pierced by its Ruger- and Spectre-file guns, no Knockout Gas timer,
    # grenades from the weapon case, no rifle butt, its Aston Martin on tank records
    assert patches() == [3, 18, 12, 0, 5, 153, 1]
    lib.gevrRomCampaignSet(2)
    assert patches() == [1, 18, 20, 33, 10, 17, 0]
    lib.gevrRomCampaignSet(0)
    assert [lib.gevrItemAs(i) for i in (31, 23, 17, 4)] == [31, 23, 17, 4]
    assert [lib.gevrRomBgCradle(), lib.gevrRomBgDam(), lib.gevrRomGadgetUse(0), lib.gevrRomGadgetUse(1)] == [21, 13, 39, 55]
    assert patches() == [1, 18, 20, 33, 10, 17, 0]
    lib.gevrRomCampaignSet(1)
    assert lib.gevrRomCreditsAfter() == 0
    assert lib.gevrRomAdvanceBelow() == 0


def test_files_and_image(lib):
    name_off = FILES_AT + 12 * 6
    data = bytearray(name_off + 32)
    data[name_off:name_off + 10] = b"CusgruntZ\0"
    name_ptr = DATA_VRAM + name_off

    def put_row(row, index, ptr, addr):
        at = FILES_AT + 12 * row
        data[at:at + 12] = be32(index) + be32(ptr) + be32(addr)

    put_row(0, 0, 0, 0)
    put_row(1, 1, name_ptr, 0x1000)
    put_row(2, 2, 0, 0x1800)
    put_row(3, 3, 0, 0x2000)
    put_row(4, 99, 0, 0x3000)

    rows = (FileRow * 8)()
    buf = (C.c_ubyte * len(data)).from_buffer_copy(data)
    count = lib.gevrRomParseFiles(buf, len(data), rows, 8)
    assert count == 2
    assert rows[0].index == 1
    assert rows[0].addr == 0x1000
    assert rows[0].size == 0x800
    assert rows[0].name == b"CusgruntZ"
    assert rows[1].index == 2
    assert rows[1].size == 0x800

    hit_s, hit_t, size = C.c_uint32(), C.c_uint32(), C.c_uint32()
    word = (0xA << 28) | (0x5 << 24) | 0x123456
    lib.gevrRomDecodeImage(word, 4, 0x7FFFFF, C.byref(hit_s), C.byref(hit_t), C.byref(size))
    assert hit_s.value == 0xA
    assert hit_t.value == 0x5
    assert size.value == 0x123456
    # Bit 23 is outside the 23-bit mask.
    lib.gevrRomDecodeImage(word | (1 << 23), 4, 0x7FFFFF, C.byref(hit_s), C.byref(hit_t), C.byref(size))
    assert size.value == 0x123456


def test_piece_and_items(lib):
    lib.gevrRomCampaignSet(1)
    layout = lib.gevrRomActiveLayout()
    rom_size = 0x1800000
    rom = bytearray(rom_size)
    rom[0x17FD000] = 0x5A
    rom[0x17FD000 + 0x3000 - 1] = 0xA5
    rom_buf = (C.c_ubyte * rom_size).from_buffer(rom)
    inflated = (C.c_ubyte * 32)()
    win = DataWindow()
    assert lib.gevrRomDataMount(C.byref(win), inflated, 32, rom_buf, rom_size, layout) == 1
    first = lib.gevrRomDataBytes(C.byref(win), 0x80700000, 1)
    last = lib.gevrRomDataBytes(C.byref(win), 0x80700000 + 0x3000 - 1, 1)
    before = lib.gevrRomDataBytes(C.byref(win), 0x80700000 - 1, 1)
    assert first and C.cast(first, C.POINTER(C.c_ubyte))[0] == 0x5A
    assert last and C.cast(last, C.POINTER(C.c_ubyte))[0] == 0xA5
    assert before and C.cast(before, C.POINTER(C.c_ubyte))[0] == 0
    lib.gevrRomDataFree(C.byref(win))

    names = (C.c_char_p * 32)()
    names[7] = b"GwppkZ"
    names[2] = b"GknifeZ"
    lib.gevrRomBuildItemMap(names, 32)
    assert lib.gevrRomItemHost(7) == 4
    assert lib.gevrRomHeldProp(4) == 191
    assert lib.gevrRomItemHost(2) == 2
    assert lib.gevrRomHeldProp(2) == 237
    assert lib.gevrRomHeldProp(9) == -2
    assert lib.gevrRomItemHost(9) == 9

    code = bytearray([0x2B, 0xFC, 0xAD, ord("h"), ord("i"), 0, 0xBF, 0, 0, 7, 0, 0, 0, 0, 0, 0x04])
    raw = (C.c_ubyte * len(code)).from_buffer(code)
    nlen = lib.gevrRomAiPrepare(raw, len(code), 1)
    assert nlen == 10
    assert list(code[:10]) == [0xBF, 0, 0, 4, 0, 0, 0, 0, 0, 0x04]
    # every command that names a hand item (geaitable.h), item 7 -> the PP7 host
    code = bytearray([0x1B, 0, 0, 7, 9,  0x57, 7, 9,  0x58, 7, 1, 9,  0x59, 7, 9,  0xE3, 7,  0xE4, 7])
    raw = (C.c_ubyte * len(code)).from_buffer(code)
    assert lib.gevrRomAiPrepare(raw, len(code), 0) == len(code)
    assert [code[i] for i in (3, 6, 9, 13, 16, 18)] == [4] * 6
    assert [code[i] for i in (4, 7, 10, 11, 14)] == [9, 9, 1, 9, 9]
    lib.gevrRomClearItemMap()
    assert lib.gevrRomItemHost(7) == 7
    assert lib.gevrRomHeldProp(4) == -2


def main():
    lib = build()
    test_headers(lib)
    test_files_and_image(lib)
    test_piece_and_items(lib)
    print("romlayout: ok")


if __name__ == "__main__":
    main()
