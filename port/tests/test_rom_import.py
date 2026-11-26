"""Several ROMs from one pick, loose or zipped (gevr_romimport.c, e-ee's request).

Every cartridge here is synthetic: a header the harness's layout table names
(port/tests/rom_import_native.c) over zeros. Checks that each ROM lands under
its picker's name in .z64 order, that a .zip is read through its central
directory (stored and deflated, CRC checked), that a lone pick of a bad
GoldenEye is left for the launcher's own message, and that nothing is left
behind to sort again.
"""
from pathlib import Path
import io
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
        assert "Not recognised: notes.txt." in r["summary"] and "pal" not in r["summary"], r
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

        # the XBLA copy is the Mods page's (the pickers pass it on; one that gets
        # here anyway is named, not taken for a bad ROM)
        d = fresh(base, "xbla")
        imp = d / "picked-import"
        imp.mkdir()
        (imp / "0-ge.z64").write_bytes(GE)
        (imp / "1-GoldenEye XBLA.7z").write_bytes(b"7z\xbc\xaf\x27\x1c" + b"\0" * 64)
        (imp / "2-30BA9271").write_bytes(b"LIVE" + b"\0" * 64)
        r = run(exe, d)
        assert r["filed"] == "1", r
        assert r["summary"].startswith("Added GoldenEye 007 (US). "), r
        assert r["summary"].endswith(" is GoldenEye XBLA: import it under Mods, XBLA / Bean."), r
        assert " GoldenEye XBLA.7z, 30BA9271 is GoldenEye XBLA" in r["summary"], r
        assert "Not recognised" not in r["summary"], r

        d = fresh(base, "xbla-alone")
        (d / "picked.z64").write_bytes(b"PIRS" + b"\0" * 64)
        r = run(exe, d)
        assert r["pending"] == "1" and r["filed"] == "0", r
        assert r["summary"] == "The file chosen is GoldenEye XBLA: import it under Mods, XBLA / Bean.", r
        # the pickers' take: a zip of everything, read in place
        d = fresh(base, "take")
        src = fresh(base, "take-src")
        roms_zip = io.BytesIO()
        with zipfile.ZipFile(roms_zip, "w") as z:
            z.writestr("Mirror.z64", MIRROR, zipfile.ZIP_DEFLATED)
            z.writestr("Mario.z64", MARIO, zipfile.ZIP_DEFLATED)
        ce_zip = io.BytesIO()
        with zipfile.ZipFile(ce_zip, "w") as z:
            z.writestr("Bean/CEUpdate/files.diff", b"hdiff" * 100, zipfile.ZIP_DEFLATED)
            z.writestr("Bean/CEUpdate/hpatchz.exe", b"MZ" * 100, zipfile.ZIP_DEFLATED)
        # the release's own zip inside: larger than any zip of ROMs, stored as it comes
        release_zip = io.BytesIO()
        with zipfile.ZipFile(release_zip, "w") as z:
            z.writestr("30BA9271F0A3A12F", b"LIVE" + bytes(200 * MB), zipfile.ZIP_STORED)
        with zipfile.ZipFile(src / "everything.zip", "w") as z:
            z.writestr("GoldenEye 007 (USA).z64", GE, zipfile.ZIP_DEFLATED)
            z.writestr("hacks/roms.zip", roms_zip.getvalue(), zipfile.ZIP_STORED)
            z.writestr("CommunityEditionUpdaterV6.zip", ce_zip.getvalue(), zipfile.ZIP_STORED)
            z.writestr("GoldenEye_007_XBLA_Retail.zip", release_zip.getvalue(), zipfile.ZIP_STORED)
            z.writestr("notes.txt", b"my roms", zipfile.ZIP_DEFLATED)
        del release_zip
        # the updater as it comes, and the unpacked release with a ROM beside it
        (src / "CommunityEditionUpdaterV6.zip").write_bytes(ce_zip.getvalue())
        with zipfile.ZipFile(src / "unpacked.zip", "w") as z:
            z.writestr("GoldenEye XBLA/default.xex", b"XEX2" + bytes(1000), zipfile.ZIP_DEFLATED)
            z.writestr("GoldenEye XBLA/files/new/char/boilerbond/default.bin", b"bond", zipfile.ZIP_DEFLATED)
            z.writestr("gex.z64", GEX, zipfile.ZIP_DEFLATED)
        (src / "readme.txt").write_bytes(b"hello")
        out = subprocess.run([str(exe), "take", str(d), str(src / "everything.zip"),
                              str(src / "CommunityEditionUpdaterV6.zip"), str(src / "unpacked.zip"),
                              str(src / "readme.txt")], check=True, capture_output=True, text=True).stdout
        t = dict(line.split(" ", 1) for line in out.strip().splitlines())
        ce_path = (d / "picked-ce.zip")
        # xbla ce staged cePath
        assert t["file0"].startswith("1 0 3 ") and t["file0"].endswith("picked-ce.zip"), t
        assert ce_path.read_bytes() == ce_zip.getvalue(), "the updater inside the zip, written out whole"
        assert t["file1"] == "0 1 0 -", t
        assert t["file2"] == "1 0 1 -", t
        assert t["file3"] == "0 0 1 -", t
        assert t["moved"] == "5", t
        assert not (d / "picked-import.part").exists() or not list((d / "picked-import.part").iterdir())
        staged = sorted(p.name for p in (d / "picked-import").iterdir())
        assert staged == ["0-GoldenEye 007 (USA).z64", "1-Mirror.z64", "2-Mario.z64", "3-gex.z64", "4-readme.txt"], staged
        r = run(exe, d)
        assert r["filed"] == "4" and r["gotGe"] == "1", r
        assert r["summary"] == ("Added GoldenEye 007 (US), Mirror Mode (Wreck), Mario Characters, GoldenEye X. "
                                "Not recognised: readme.txt."), r
        assert (d / "picked.z64").read_bytes() == GE and (d / "picked-gex.z64").read_bytes() == GEX
        assert (d / "mirror.z64").read_bytes() == MIRROR and (d / "mario.z64").read_bytes() == MARIO
        print("rom import: OK")
    finally:
        shutil.rmtree(base, ignore_errors=True)


if __name__ == "__main__":
    main()
