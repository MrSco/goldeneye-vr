"""Bean import from the player's own XBLA copy (port/src/gevr_bean_import.c).

Usage:
  python port/tests/test_bean_import.py
  python port/tests/test_bean_import.py --files <xbla>/files --copy <copy> [--copy <copy> ...]

The first form builds made-up copies in every form the importer takes (a zip of
the unpacked release, an Xbox 360 package bare and zipped, and with 7-Zip on
hand a .7z of each) and checks what comes out. The second imports real copies
and compares every file against an unpacked release byte for byte. Game files
stay outside the repository and are never copied into artifacts.
"""
import argparse
import os
import shutil
import struct
import subprocess
import sys
import tempfile
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
LZMA = ["7zAlloc", "7zBuf", "7zCrc", "7zCrcOpt", "7zDec", "7zFile", "7zIn", "7zStream", "Alloc",
        "Bcj2", "Bra", "Bra86", "BraIA64", "CpuArch", "Delta", "Lzma2Dec", "LzmaDec"]
WINDOWS_7Z = Path("/mnt/c/Program Files/7-Zip/7z.exe")


def build(directory):
    exe = Path(directory) / "import.exe"
    sanitizers = ["-fsanitize=address,undefined", "-fno-omit-frame-pointer"] if os.environ.get("BEAN_SANITIZE") else []
    subprocess.run([
        shutil.which("gcc") or "cc", "-std=c11", "-O2", "-Wall", "-Wextra", *sanitizers,
        "-I" + str(ROOT / "port/include"),
        str(ROOT / "port/tests/gevr_bean_import_test.c"), str(ROOT / "port/src/gevr_bean_import.c"),
        *[str(ROOT / "port/external/lzma" / (name + ".c")) for name in LZMA],
        "-lz", "-o", str(exe),
    ], check=True)
    return exe


def run_import(exe, copy, dest):
    result = subprocess.run([str(exe), "import", str(copy), str(dest)], capture_output=True, text=True)
    count, _, message = result.stdout.strip().partition(" ")
    return int(count), message


def tree(folder):
    return {p.relative_to(folder).as_posix(): p.read_bytes() for p in Path(folder).rglob("*") if p.is_file()}


def caff(tag):
    return b"CAFF" + tag.encode() * 300


# A made-up release: Bean's own files, and the things around them that stay behind
RELEASE = {
    "default.xex": b"XEX2" + b"\0" * 5000,
    "music.xwb": b"WBND" + b"\1" * 9000,
    "files/new/char/boilerbond/default.bin": caff("bond"),
    "files/new/background/dam/default.bin": caff("dam") * 4,
    "files/new/background/dam_hits/default.bin": caff("hits"),
    "files/new/gun/ak47/default.bin": caff("ak"),
    "files/new/skydome/sky1/default.bin": caff("sky"),
    "files/original/char/boilerbond/default.bin": caff("n64"),
    "files/misc/alps3/default.dt": caff("alps"),
    "files/misc/doc0/default.dt": caff("doc"),
    "files/misc/other/default.dt": caff("other"),
    "files/texture/sfx/hit5/default.rba": caff("hit"),
    "files/texture/bg/ammoicon9mm/default.rba": caff("ammo"),
    "files/new/texture/monitors/map/default.rba": caff("map"),
    "files/new/texture/sfx/firebomb0/default.rba": caff("fire"),
    "files/loc/english/default.str": b"STR",
}
WANTED = {name for name in RELEASE if name.startswith("files/") and "_hits" not in name
          and "/original/" not in name and "/other/" not in name and "/loc/" not in name}


def write_tree(folder, files):
    for name, data in files.items():
        path = Path(folder) / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)


def stfs_package(files, scatter=None):
    """A read-only LIVE package of files, one hash table, blocks after the file table.
    scatter: a file whose blocks are laid out backwards, so only the hash chain finds them."""
    entries, index = [], {}
    for name in sorted(files):
        parts = name.split("/")
        for depth in range(1, len(parts)):
            folder = "/".join(parts[:depth])
            if folder not in index:
                index[folder] = len(entries)
                entries.append([parts[depth - 1], True, index.get("/".join(parts[:depth - 1]), 0xFFFF), 0, 0, 0, True])
        index[name] = len(entries)
        entries.append([parts[-1], False, index.get("/".join(parts[:-1]), 0xFFFF), 0, 0, 0, True])
    assert len(entries) <= 64
    blocks, next_of = {}, {}
    free = 1  # block 0 is the file table
    for entry in entries:
        if entry[1]:
            continue
        name = [n for n, i in index.items() if entries[i] is entry][0]
        data = files[name]
        count = max(1, (len(data) + 0xFFF) // 0x1000)
        order = list(range(free, free + count))
        if name == scatter:
            order.reverse()
            entry[6] = False
        for k, block in enumerate(order):
            blocks[block] = data[k * 0x1000:(k + 1) * 0x1000]
            if k + 1 < count:
                next_of[block] = order[k + 1]
        entry[3], entry[4], entry[5] = count, order[0], len(data)
        free += count
    assert free < 0xAA
    table = bytearray(0x1000)
    for i, (name, is_dir, parent, count, start, size, consecutive) in enumerate(entries):
        e = bytearray(0x40)
        e[:len(name)] = name.encode()
        e[0x28] = len(name) | (0x80 if is_dir else 0) | (0x40 if consecutive else 0)
        e[0x29:0x2C] = count.to_bytes(3, "little")
        e[0x2C:0x2F] = count.to_bytes(3, "little")
        e[0x2F:0x32] = start.to_bytes(3, "little")
        e[0x32:0x34] = parent.to_bytes(2, "big")
        e[0x34:0x38] = size.to_bytes(4, "big")
        table[i * 0x40:(i + 1) * 0x40] = e
    out = bytearray(0xC000 + free * 0x1000)
    out[0:4] = b"LIVE"
    out[0x340:0x344] = struct.pack(">I", 0xAD0E)
    out[0x379], out[0x37B] = 0x24, 1
    out[0x37C:0x37E] = (1).to_bytes(2, "little")
    out[0x37E:0x381] = (0).to_bytes(3, "little")
    out[0x395:0x399] = struct.pack(">I", free)
    for block in range(free):
        nxt = next_of.get(block, block + 1 if block + 1 < free else 0xFFFFFF)
        at = 0xB000 + block * 0x18
        out[at + 0x15:at + 0x18] = nxt.to_bytes(3, "big")
    out[0xC000:0xD000] = table
    for block, data in blocks.items():
        out[0xC000 + block * 0x1000:0xC000 + block * 0x1000 + len(data)] = data
    return bytes(out)


def seven_zip(source_folder, archive):
    """A solid LZMA2 .7z of source_folder's contents, as the release circulates. None without 7-Zip."""
    tool = shutil.which("7z") or shutil.which("7za")
    paths = (lambda p: str(p))
    if not tool and WINDOWS_7Z.exists() and shutil.which("wslpath"):
        tool = str(WINDOWS_7Z)
        paths = (lambda p: subprocess.run(["wslpath", "-w", str(p)], capture_output=True, text=True, check=True).stdout.strip())
    if not tool:
        return None
    subprocess.run([tool, "a", "-t7z", "-m0=lzma2", "-mx=3", "-ms=on", paths(archive), "."],
                   cwd=source_folder, check=True, capture_output=True)
    return archive


def check(label, ok, detail=""):
    print(("ok   " if ok else "FAIL ") + label + (": " + detail if detail and not ok else ""))
    if not ok:
        check.failed = True


check.failed = False


def synthetic(exe, work):
    names = [
        "files/new/char/boilerbond/default.bin", "GoldenEye 007/files/new/char/boilerbond/default.bin",
        "Files\\New\\Char\\BoilerBond\\default.bin", "../files/new/char/x/default.bin",
        "files/new/background/dam_hits/default.bin", "files/new/char/../default.bin", "files/new/char/a/b/default.bin",
        "files/texture/sfx/hit5/default.rba", "files/texture/bg/ammo.icon/default.rba", "files/texture/bg/sight/x.y/default.rba",
        "files/new/texture/monitors/map/default.rba", "files/misc/alps3/default.dt", "files/misc/x/default.dt",
        "myfiles/new/char/a/default.bin", "files/original/char/a/default.bin",
    ]
    expected = [
        "files/new/char/boilerbond/default.bin", "files/new/char/boilerbond/default.bin",
        "files/new/char/boilerbond/default.bin", "files/new/char/x/default.bin",
        "-", "-", "-",
        "files/texture/sfx/hit5/default.rba", "files/texture/bg/ammo.icon/default.rba", "-",
        "files/new/texture/monitors/map/default.rba", "files/misc/alps3/default.dt", "-",
        "-", "-",
    ]
    got = subprocess.run([str(exe), "wanted"], input="\n".join(names) + "\n", capture_output=True, text=True,
                         check=True).stdout.split("\n")[:len(names)]
    for name, want, have in zip(names, expected, got):
        check("wanted " + name, want == have, have)

    expect = {name: RELEASE[name] for name in WANTED}
    release = work / "release"
    write_tree(release / "GoldenEye 007", RELEASE)
    copies = {}

    copies["zip of the folder"] = work / "folder.zip"
    with zipfile.ZipFile(copies["zip of the folder"], "w", zipfile.ZIP_DEFLATED) as z:
        for name, data in RELEASE.items():
            z.writestr("GoldenEye 007/" + name, data)

    package = stfs_package(RELEASE, scatter="files/new/background/dam/default.bin")
    copies["package"] = work / "30BA92710985645EF623D4A6BA9E8EFFAEC62617"
    copies["package"].write_bytes(package)
    copies["zip of the package"] = work / "retail.zip"
    with zipfile.ZipFile(copies["zip of the package"], "w", zipfile.ZIP_DEFLATED) as z:
        z.writestr("0000000000000000/584108A9/000D0000/", b"")
        z.writestr("0000000000000000/584108A9/000D0000/30BA92710985645EF623D4A6BA9E8EFFAEC62617", package + b"\0" * (64 << 20))
        z.writestr("fyodorovna.nfo", b"nfo")

    if seven_zip(release, work / "folder.7z"):
        copies["7z of the folder"] = work / "folder.7z"
        holder = work / "holder"
        holder.mkdir()
        (holder / "30BA92710985645EF623D4A6BA9E8EFFAEC62617").write_bytes(package + b"\0" * (64 << 20))
        copies["7z of the package"] = seven_zip(holder, work / "package.7z")
    else:
        print("skip 7z: no 7-Zip (7z, 7za, or C:/Program Files/7-Zip/7z.exe)")

    for label, copy in copies.items():
        dest = work / ("out-" + label.replace(" ", "-"))
        count, message = run_import(exe, copy, dest)
        have = tree(dest)
        check(label + " imports", count == len(expect) and message == "", f"{count} {message}")
        check(label + " writes only Bean's files, whole", have == expect,
              f"extra {sorted(set(have) - set(expect))} missing {sorted(set(expect) - set(have))}")

    junk = work / "junk.bin"
    junk.write_bytes(b"\0" * 100000)
    count, message = run_import(exe, junk, work / "out-junk")
    check("a file that is no copy is refused", count == -1 and "Choose" in message, message)

    other = work / "other.zip"
    with zipfile.ZipFile(other, "w") as z:
        z.writestr("files/whatever.txt", b"x")
    count, message = run_import(exe, other, work / "out-other")
    check("a zip without Bean is refused", count == -1 and "not GoldenEye XBLA" in message, message)

    fake = work / "fake.zip"
    with zipfile.ZipFile(fake, "w") as z:
        z.writestr("files/new/char/boilerbond/default.bin", b"NOTCAFF" * 10)
    count, message = run_import(exe, fake, work / "out-fake")
    check("a file that is not CAFF is refused", count == -1 and "not GoldenEye XBLA" in message
          and not (work / "out-fake/files/new/char/boilerbond/default.bin").exists(), message)

    nobond = work / "nobond.zip"
    with zipfile.ZipFile(nobond, "w") as z:
        z.writestr("files/new/gun/ak47/default.bin", caff("ak"))
    count, message = run_import(exe, nobond, work / "out-nobond")
    check("a copy without Bond is refused", count == -1 and "no Bean characters" in message, message)

    cut = work / "cut"
    cut.write_bytes(package[:0xC000 + 0x3000])
    count, message = run_import(exe, cut, work / "out-cut")
    check("a cut-off package is refused", count == -1, message)


def real(exe, files, copy_paths, work):
    files = Path(files)
    release = files.parent
    names = sorted(p.relative_to(release).as_posix() for p in files.rglob("*") if p.is_file())
    mapped = subprocess.run([str(exe), "wanted"], input="\n".join(names) + "\n", capture_output=True, text=True,
                            check=True).stdout.split("\n")[:len(names)]
    expect = {have: name for name, have in zip(names, mapped) if have != "-"}
    print(f"{len(expect)} wanted files in {files}")
    for copy in copy_paths:
        dest = work / ("real-" + Path(copy).name)
        count, message = run_import(exe, copy, dest)
        have = {p.relative_to(dest).as_posix() for p in dest.rglob("*") if p.is_file()}
        check(f"{copy} imports", count == len(expect) and message == "", f"{count} {message}")
        check(f"{copy} writes the wanted set", have == set(expect),
              f"extra {sorted(have - set(expect))[:5]} missing {sorted(set(expect) - have)[:5]}")
        same = sum((dest / rel).read_bytes() == (release / src).read_bytes() for rel, src in expect.items() if rel in have)
        check(f"{copy} byte for byte", same == len(expect), f"{same} of {len(expect)} match")
        shutil.rmtree(dest, ignore_errors=True)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--files")
    parser.add_argument("--copy", action="append", default=[])
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="gevr-bean-import-") as directory:
        work = Path(directory)
        exe = build(directory)
        synthetic(exe, work)
        if args.files:
            real(exe, args.files, args.copy, work)
    if check.failed:
        sys.exit(1)
    print("bean import: all checks passed")


main()
