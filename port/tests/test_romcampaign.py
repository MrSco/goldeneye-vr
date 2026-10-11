"""The hack campaign binder (port/src/gevr_romcampaign.c) on a real ROM.

romcampaign_native.c runs the production binder against stand-ins for the
engine tables; this script decodes the same ROM independently, from the N64
layouts, and compares every table the binder writes. No ROM is in the repo:
set GEVR_GF64_ROM to a Goldfinger 64 1.0 .z64 (the player's own) to run it;
without it the test says SKIP.
"""
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile
import zlib

ROOT = Path(__file__).resolve().parents[2]
V = 0x80020D90
ITEMS_AT = 0x12B94
GF = dict(
    campaign=1, images=3915, chrs=(0x80700FC0, 126), props=(0x8070B400, 416), blast=0x8070DD28,
    pieces=[(0x80700000, 0x17FD000, 0x3000), (0x80703000, 0x17F8800, 0x4800), (0x80708000, 0x17F0000, 0x8800)],
)
SKELETONS = {
    0x8003A05C: "cctv", 0x8003A070: "console_one_screen", 0x8003A084: "console_four_screen",
    0x8003A0B0: "tv_holder", 0x8003A0E0: "rotating_stuff", 0x8003A100: "eyelid_door", 0x8003A15C: "iris_door",
    0x8003A170: "walletbond", 0x8003A19C: "car", 0x8003A1C8: "flying", 0x8003A1DC: "door", 0x8003A208: "tank",
    0x8003A21C: "hat", 0x8003C4D8: "standard_object", 0x8003C4FC: "prop_weapon", 0x8003C6E4: "suit_lf_hand",
    0x8003C714: "standard_gun", 0x8003C76C: "gun_revolver", 0x8003C7AC: "gun_kf7", 0x8003D390: "g_weapon",
    0x8003D400: "guard", 0: "none",
}
GUN_FILES = [None, "GfistZ", "GknifeZ", "GthrowknifeZ", "GwppkZ", "GwppksilZ", "Gtt33Z", "GskorpionZ",
             "Gak47Z", "GuziZ", "Gmp5kZ", "Gmp5ksilZ", "GspectreZ", "Gm16Z", "Gfnp90Z", "GshotgunZ",
             "GautoshotZ", "GsniperrifleZ", "GrugerZ", "GgoldengunZ", "GsilverwppkZ", "GgoldwppkZ", "GlaserZ",
             "GwatchlaserZ", "GgrenadelaunchZ", "GrocketlaunchZ", "GgrenadeZ", "GtimedmineZ", "GproximitymineZ",
             "GremotemineZ", "GtriggerZ", "GtaserZ"]
CASINGS = {0x8003CB60: "cartridge", 0x8003CB80: "cartrifle", 0x8003CBA0: "cartblue", 0x8003CBC0: "cartshell"}
FOG = ">IffffffIiiBBBBfhHfffB3xfhHffff"
FOGLESS = ">IBBBBfhHfffB3xfhHffff"


def window(rom, layout):
    data = bytearray(zlib.decompressobj(-15).decompress(rom[0x21992:]))
    for vram, at, length in layout["pieces"]:
        off = vram - V
        if len(data) < off + length:
            data.extend(b"\0" * (off + length - len(data)))
        data[off:off + length] = rom[at:at + length]
    return bytes(data)


def word(d, vram):
    return struct.unpack_from(">I", d, vram - V)[0]


def cstr(d, ptr):
    if ptr < V or ptr - V >= len(d):
        return ""
    o = ptr - V
    return d[o:d.index(b"\0", o)].decode("ascii")


def header(d, vram):
    skel, = struct.unpack_from(">I", d, vram - V + 4)
    nsw, nmtx, _, nrec, ntex = struct.unpack_from(">hhfhh", d, vram - V + 12)
    return [SKELETONS[skel], nsw, nmtx, nrec, ntex]


def build():
    out = Path(tempfile.mkdtemp(prefix="gevr-romcampaign-")) / "romcampaign_native.exe"
    args = [shutil.which("gcc") or "gcc", "-O1", "-fms-extensions", "-Wno-builtin-declaration-mismatch", "-static"]
    args += ["-D" + v for v in ("GEVR=1", "PLATFORM_64BIT=1", "_LANGUAGE_C=1", "VERSION=2", "VERSION_US=1", "LANG_US=1")]
    args += ["-I" + str(ROOT / v) for v in (".", "port/include", "include", "src", "src/game", "port/src")]
    args += [str(ROOT / p) for p in ("port/tests/romcampaign_native.c", "port/src/gevr_romcampaign.c",
                                      "port/src/gevr_romlayout.c", "port/src/gevr_romcheck.c",
                                      "port/src/gevr_rom_manifest.c")]
    args += ["-lz", "-o", str(out)]
    subprocess.run(args, check=True, cwd=ROOT)
    return out


def expect(rom, L):
    d = window(rom, L)
    e = {"images": L["images"]}
    cv, cn = L["chrs"]
    e["chr"] = [header(d, word(d, cv + 20 * i)) for i in range(cn)]
    e["chrname"] = [cstr(d, word(d, cv + 20 * i + 4)) for i in range(cn)]
    pv, pn = L["props"]
    e["prop"] = [header(d, word(d, pv + 12 * i)) for i in range(pn)]
    e["propname"] = [cstr(d, word(d, pv + 12 * i + 4)) for i in range(pn)]
    e["blast"] = [list(struct.unpack_from(">hhhhhhh", d, L["blast"] - V + 14 * i))[:2] +
                  [struct.unpack_from(">h", d, L["blast"] - V + 14 * i + 12)[0]] for i in range(pn)]
    items = {}
    for k in range(1, 89):
        hp, np_ = struct.unpack_from(">II", d, ITEMS_AT + 56 * k)
        if hp and np_ and cstr(d, np_):
            items.setdefault(cstr(d, np_), []).append(header(d, hp))
    e["items"] = items
    # item k fires with its own row's stats, on the US item whose gun file it uses
    stats = {}
    for k in range(1, 32):
        sp = struct.unpack_from(">I", d, ITEMS_AT + 56 * k + 12)[0]
        name = cstr(d, struct.unpack_from(">I", d, ITEMS_AT + 56 * k + 4)[0])
        if sp and name in GUN_FILES:
            s = struct.unpack_from(">7fihBbBBHI6fi9fI", d, sp - V)
            stats[GUN_FILES.index(name)] = [s[7], s[8], s[9], s[10], s[11], s[13], CASINGS.get(s[14], "none"),
                                            s[15], s[16], s[17], s[21], s[22], s[30], s[31]]
    e["stats"] = stats
    e["host"] = {k: GUN_FILES.index(cstr(d, struct.unpack_from(">I", d, ITEMS_AT + 56 * k + 4)[0]))
                 for k in range(1, 32)}
    rows = 0
    while struct.unpack_from(">I", d, 0x252C4 + 12 * rows)[0] == rows:
        rows += 1
    files = {}
    for k in range(1, rows - 1):
        idx, nptr, addr = struct.unpack_from(">III", d, 0x252C4 + 12 * k)
        nxt = struct.unpack_from(">I", d, 0x252C4 + 12 * (k + 1) + 8)[0]
        if nxt > addr:
            files[idx] = (cstr(d, nptr), addr, nxt - addr)
    e["files"] = files
    e["levels"] = [struct.unpack_from(">IIIfff", d, 0x8004448C - V + 24 * i) for i in range(38)]
    e["levelnames"] = [(cstr(d, r[1]), cstr(d, r[2])) for r in e["levels"]]
    fog, o = [], 0x24080
    while True:
        r = struct.unpack_from(FOG, d, o)
        fog.append(r)
        o += 92
        if r[0] == 0 and len(fog) > 1:
            break
    fogless = []
    while True:
        r = struct.unpack_from(FOGLESS, d, o)
        fogless.append(r)
        o += 56
        if r[0] == 0 and len(fogless) > 1:
            break
    e["fog"], e["fogless"] = fog, fogless
    music, o = [], 0x2DD80
    while True:
        r = struct.unpack_from(">hhhh", d, o)
        music.append(r)
        o += 8
        if r[0] == 0:
            break
    rnd = []
    while len(rnd) < 52:
        t = struct.unpack_from(">h", d, o)[0]
        rnd.append(t)
        o += 2
        if t == -1:
            break
    e["music"], e["random"] = music, rnd
    e["times"] = [struct.unpack_from(">hhh", d, 0x8002B564 - V + 6 * m) for m in range(20)]
    return e


def close(a, b):
    return abs(float(a) - float(b)) <= 1e-3 * max(1.0, abs(float(b)))


def main():
    path = os.environ.get("GEVR_GF64_ROM")
    if not path or not Path(path).is_file():
        print("SKIP: set GEVR_GF64_ROM to a Goldfinger 64 1.0 .z64 to run the binder on it")
        return
    rom = open(path, "rb").read()
    exe = build()
    out = subprocess.run([str(exe), path, str(GF["campaign"])], capture_output=True, text=True, check=True).stdout
    got = {}
    for line in out.splitlines():
        f = line.split()
        got.setdefault(f[0], []).append(f[1:])
    e = expect(rom, GF)
    bad = []

    def check(cond, what):
        if not cond:
            bad.append(what)

    check(got["match"] == [["yes"]], "header match")
    check(got["apply"] == [["1"]], "apply: " + " | ".join(" ".join(x) for x in got.get("log", [])[-3:]))
    check(got["rebind"] == [["1"]], "obInit rebind keeps the hack table")
    check(int(got["images"][0][0]) == e["images"], "image count")
    chrs = {int(r[0]): r[1:] for r in got.get("chr", [])}
    names = {int(r[0]): r[1] if len(r) > 1 else "" for r in got.get("chrname", [])}
    check(len(chrs) == len(e["chr"]), "character rows %d" % len(chrs))
    for i, want in enumerate(e["chr"]):
        check(chrs.get(i) == [str(x) for x in want], "chr %d %s: %s != %s" % (i, e["chrname"][i], chrs.get(i), want))
        check(names.get(i) == e["chrname"][i], "chr %d name" % i)
    check(int(got["props"][0][0]) == len(e["prop"]), "prop count")
    props = {int(r[0]): r[1:] for r in got.get("prop", [])}
    blast = {int(r[0]): [int(x) for x in r[1:]] for r in got.get("blast", [])}
    pnames = {int(r[0]): r[1] if len(r) > 1 else "" for r in got.get("propname", [])}
    for i, want in enumerate(e["prop"]):
        check(props.get(i) == [str(x) for x in want], "prop %d: %s != %s" % (i, props.get(i), want))
        check(blast.get(i) == e["blast"][i], "prop %d explosion: %s != %s" % (i, blast.get(i), e["blast"][i]))
        check(pnames.get(i) == e["propname"][i], "prop %d name" % i)
    items = {r[-1]: None for r in got.get("itemname", [])}
    for r, n in zip(got.get("item", []), got.get("itemname", [])):
        want = e["items"].get(n[1])
        check(want is not None, "item %s in the hack" % n[1])
        if want:
            check(all(w == want[0] for w in want), "item %s rows disagree: %s" % (n[1], want))
            check(r[1:] == [str(x) for x in want[-1]], "item %s: %s != %s" % (n[1], r[1:], want[-1]))
    host = {int(r[0]): int(r[1]) for r in got.get("host", [])}
    check(host == e["host"], "item host map: %s != %s" % (host, e["host"]))
    stats = {int(r[0]): r[1:] for r in got.get("stats", [])}
    check(len(e["stats"]) == 31, "31 guns with stats in the hack, got %d" % len(e["stats"]))
    for host, want in e["stats"].items():
        row = stats.get(host, [])
        check(len(row) == len(want) and all((a == b) if isinstance(b, str) else close(a, b) for a, b in zip(row, want)),
              "stats of US item %d (%s): %s != %s" % (host, GUN_FILES[host], row, want))
    files = {int(r[0]): (r[1], int(r[2]), int(r[3])) for r in got.get("file", [])}
    check(len(files) == len(e["files"]), "bound files %d of %d" % (len(files), len(e["files"])))
    for idx, want in e["files"].items():
        check(files.get(idx) == want, "file %d: %s != %s" % (idx, files.get(idx), want))
    for r, want, wn in zip(got["level"], e["levels"], e["levelnames"]):
        check(int(r[0]) == want[0] and (r[1], r[2]) == wn and all(close(a, b) for a, b in zip(r[3:], want[3:])),
              "level %s: %s != %s %s" % (r[0], r, wn, want[3:]))
    fog = {int(r[0]): r[1:] for r in got["fog"]}
    for i, want in enumerate(e["fog"]):
        w = [want[0], want[1], want[2], want[3], want[6], want[7], want[9], want[10], want[11], want[12], want[13],
             want[14], want[15], want[20], want[21], want[22], want[27]]
        check(len(fog[i]) == len(w) and all(close(a, b) for a, b in zip(fog[i], w)), "fog %d: %s != %s" % (i, fog[i], w))
    fogless = {int(r[0]): r[1:] for r in got["fogless"]}
    for i, want in enumerate(e["fogless"]):
        w = [want[0], want[1], want[2], want[3], want[4], want[5], want[11], want[12]]
        check(all(close(a, b) for a, b in zip(fogless[i], w)), "fogless %d: %s != %s" % (i, fogless[i], w))
    music = {int(r[0]): [int(x) for x in r[1:]] for r in got["music"]}
    for i, want in enumerate(e["music"]):
        check(music[i] == list(want), "music %d: %s != %s" % (i, music[i], want))
    rnd = {int(r[0]): int(r[1]) for r in got["random"]}
    for i, want in enumerate(e["random"]):
        check(rnd[i] == want, "random track %d" % i)
    times = {int(r[0]): [int(x) for x in r[1:]] for r in got["times"]}
    for i, want in enumerate(e["times"]):
        check(times[i] == list(want), "target times %d" % i)
    check(len(got.get("folder", [])) >= 20, "mission folder rows")

    if bad:
        print("\n".join(bad[:40]))
        print("FAIL: %d mismatches" % len(bad))
        sys.exit(1)
    print("PASS: Goldfinger 64 binder: %d files, %d images, %d characters, %d props and their explosions, "
          "the item host map, gun headers and stats, level scales, %d fog and %d fogless rows, music, target times"
          % (len(files), e["images"], len(chrs), len(props), len(e["fog"]), len(e["fogless"])))


if __name__ == "__main__":
    main()
