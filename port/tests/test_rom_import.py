"""Several ROMs from one pick, loose or zipped (gevr_romimport.c, e-ee's request).

Every cartridge here is synthetic: a header the harness's layout table names
(port/tests/rom_import_native.c) over zeros. Checks that each ROM lands under
its picker's name in .z64 order, that a .zip is read through its central
directory (stored and deflated, CRC checked), that a lone pick of a bad
GoldenEye is left for the launcher's own message, and that nothing is left
behind to sort again.
"""
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile
import zipfile

ROOT = Path(__file__).resolve().parents[2]
MB = 1 << 20


def rom(size, title, code, crc, swap=None):
    data = bytearray(size)
    data[0:4] = b"\x80\x37\x12\x40"
    data[0x10:0x18] = bytes([crc]) * 8
    data[0x20:0x20 + len(title)] = title.encode()
    data[0x3b:0x3f] = code.encode()
    data[0x1000:0x1010] = bytes(range(16))   # something for the byte order to move
    if swap == "v64":
        data[0::2], data[1::2] = data[1::2], data[0::2]
    return bytes(data)


GE = rom(12 * MB, "GOLDENEYE", "NGEE", 1)
GE_PAL = rom(12 * MB, "GOLDENEYE", "NGEP", 9)
GF = rom(24 * MB, "GOLDFINGER", "NGFE", 2)
TND = rom(32 * MB, "GOLDENEYE", "NGEE", 3)
MIRROR = rom(12 * MB, "GOLDENEYE", "NGEE", 4)
MARIO = rom(16 * MB, "GOLDENEYE", "NGEE", 5)
GEX = rom(32 * MB, "GoldenEye X", "NPDE", 7)
OTHER = rom(12 * MB, "SUPER MARIO 64", "NSME", 8)


def build(out):
    exe = out / "rom_import_native.exe"
    subprocess.run([shutil.which("gcc") or "gcc", "-std=c11", "-O1", "-Wall", "-Wextra", "-Werror",
                    "-I" + str(ROOT / "port/include"), str(ROOT / "port/tests/rom_import_native.c"),
                    str(ROOT / "port/src/gevr_romimport.c"), str(ROOT / "port/src/gevr_romcheck.c"),
                    str(ROOT / "port/src/gevr_rom_manifest.c"), "-lz", "-o", str(exe)], check=True)
    return exe


def run(exe, data):
    out = subprocess.run([str(exe), str(data)], check=True, capture_output=True, text=True).stdout
    return dict(line.split(" ", 1) for line in out.strip().splitlines())


def fresh(base, name):
    d = base / name
    d.mkdir()
    return d


def main():
    base = Path(tempfile.mkdtemp(prefix="gevr-romimport-"))
    try:
        exe = build(base)

        # one .zip of everything, picked alone: it arrives as picked.z64
        d = fresh(base, "zip")
        with zipfile.ZipFile(d / "picked.z64", "w") as z:
            z.writestr("roms/GoldenEye 007 (USA).z64", GE, zipfile.ZIP_DEFLATED)
            z.writestr("roms/Goldfinger.v64", rom(24 * MB, "GOLDFINGER", "NGFE", 2, "v64"), zipfile.ZIP_STORED)
            z.writestr("roms/gex.z64", GEX, zipfile.ZIP_DEFLATED)
            z.writestr("roms/tnd.z64", TND, zipfile.ZIP_DEFLATED)
            z.writestr("readme.txt", b"hello", zipfile.ZIP_DEFLATED)
            z.writestr("__MACOSX/roms/._gex.z64", b"\0" * 4096, zipfile.ZIP_STORED)
            z.writestr("sm64.z64", OTHER, zipfile.ZIP_DEFLATED)
        r = run(exe, d)
        assert r["pending"] == "1", r
        assert r["filed"] == "4", r
        assert r["gotGe"] == "1", r
        assert r["after"] == "0", r
        assert r["summary"] == ("Added GoldenEye 007 (US), Goldfinger 64, GoldenEye X, Tomorrow Never Dies 64. "
                                "Not recognised: sm64.z64."), r
        assert (d / "picked.z64").read_bytes() == GE
        assert (d / "goldfinger64.z64").read_bytes() == GF, "a .v64 is put in .z64 order"
        assert (d / "picked-gex.z64").read_bytes() == GEX
        assert (d / "tnd64.z64").read_bytes() == TND
        assert sorted(p.name for p in d.iterdir()) == ["goldfinger64.z64", "picked-gex.z64", "picked.z64", "tnd64.z64"]

        # several loose files and a zip, picked together (data/picked-import/)
        d = fresh(base, "several")
        imp = d / "picked-import"
        imp.mkdir()
        (imp / "0-mirror.z64").write_bytes(MIRROR)
        (imp / "1-pal.z64").write_bytes(GE_PAL)
        (imp / "2-ge.z64").write_bytes(GE)
        (imp / "3-notes.txt").write_bytes(b"notes")
        (imp / "4-half.z64.part").write_bytes(b"still copying")
        with zipfile.ZipFile(imp / "5-mario.zip", "w") as z:
            z.writestr("mario.z64", MARIO, zipfile.ZIP_DEFLATED)
        r = run(exe, d)
        assert r["pending"] == "1" and r["filed"] == "3" and r["gotGe"] == "1", r
        assert "Mirror Mode (Wreck)" in r["summary"] and "Mario Characters" in r["summary"], r
        assert "3-notes.txt" in r["summary"] and "pal" not in r["summary"], r
        assert (d / "mirror.z64").read_bytes() == MIRROR
        assert (d / "mario.z64").read_bytes() == MARIO
        assert (d / "picked.z64").read_bytes() == GE, "a good GoldenEye beats a PAL one in the same pick"
        assert sorted(p.name for p in imp.iterdir()) == ["4-half.z64.part"], "a copy still going is left alone"
        assert r["after"] == "0", r

        # one PAL GoldenEye alone: the launcher's probe says what is wrong with it
        d = fresh(base, "pal")
        (d / "picked.z64").write_bytes(GE_PAL)
        r = run(exe, d)
        assert r["pending"] == "0" and r["filed"] == "0" and r["summary"] == "", r
        assert (d / "picked.z64").read_bytes() == GE_PAL

        # one campaign ROM through the main picker goes to its own file
        d = fresh(base, "single-hack")
        (d / "picked.z64").write_bytes(TND)
        r = run(exe, d)
        assert r["pending"] == "1" and r["filed"] == "1" and r["gotGe"] == "0", r
        assert r["summary"] == "Added Tomorrow Never Dies 64.", r
        assert (d / "tnd64.z64").read_bytes() == TND and not (d / "picked.z64").exists()

        # a zip entry that does not check out is not filed
        d = fresh(base, "damaged")
        with zipfile.ZipFile(d / "picked.z64", "w") as z:
            z.writestr("ge.z64", GE, zipfile.ZIP_STORED)
        raw = bytearray((d / "picked.z64").read_bytes())
        raw[30 + len("ge.z64") + 0x100] ^= 0xff   # inside the stored ROM
        (d / "picked.z64").write_bytes(bytes(raw))
        r = run(exe, d)
        assert r["filed"] == "0" and r["gotGe"] == "0", r
        assert r["summary"] == "No ROM this knows in ge.z64.", r
        assert not (d / "picked.z64").exists()
        print("rom import: OK")
    finally:
        shutil.rmtree(base, ignore_errors=True)


if __name__ == "__main__":
    main()
