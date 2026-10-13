"""The hack campaign binder (port/src/gevr_romcampaign.c) on a real ROM.

romcampaign_native.c runs the production binder against stand-ins for the
engine tables; this script decodes the same ROM independently, from the N64
layouts, and compares every table the binder writes. No ROM is in the repo:
set GEVR_GF64_ROM to a Goldfinger 64 1.0 .z64 (the player's own) to run it;
without it the test says SKIP.
"""
import os
import re
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
    campaign=1, images=3915, imagerows=(0x80049300, 3915), chrs=(0x80700FC0, 126), props=(0x8070B400, 416), blast=0x8070DD28,
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


def advance(seed):
    """src/random.c's randomAdvance, in 64 bits."""
    m = (1 << 64) - 1
    v = (((seed << 63) & m) >> 31) | (((seed << 31) & m) >> 32)
    v ^= ((seed << 44) & m) >> 32
    v ^= (v >> 20) & 0xFFF
    return v & m


def head_picks(rom):
    """Goldfinger's bodyChooseHead() and level init (7F0235AC, 7F000F88), from its own tables."""
    code = rom[0x34B30:]
    at = lambda va, n: code[va - 0x7F000000:va - 0x7F000000 + n]
    sizes = at(0x7F023668, 8)
    ptrs = struct.unpack(">4I", at(0x7F023670, 16))
    lists = {}
    for i in range(8):
        pool, male = 3 - (i & 3), i >> 2
        if sizes[i] and pool <= 1:
            lists[i] = at(ptrs[2 * pool + male], sizes[i])
    seed = 0x0123456789ABCDEF
    seed = advance(seed)
    r = seed & 0xFFFFFFFF
    start = []
    for i in range(8):
        start.append(r % sizes[i] if sizes[i] else 0)
        r >>= 1
    picks = []
    for n in range(24):
        male, head = n & 1, (-2 if n & 2 else -1)
        i = 4 * male + 4 + head
        if not sizes[i]:
            i = 4 * male + 3
        a = start[i] + (((seed >> 16) & 3) if male else 0)
        picks.append(lists[i][a % sizes[i]])
        seed = advance(seed)
    return picks


def anim_tables(folder):
    """GoldenEye's animation_table_ptrs1/2 from initanitable.c, for the harness's animation check."""
    src = (ROOT / "src/game/initanitable.c").read_text(encoding="utf-8")
    out = []
    for name, c in (("animation_table_ptrs1[] = {", "s_geAnim1"), ("animation_table_ptrs2[] = {", "s_geAnim2")):
        body = src[src.index(name) + len(name):src.index("};", src.index(name))]
        rows = re.findall(r"PTR_ANIM_\w+", body)
        out.append("static const u32 %s[] = { %s, 0 };" % (c, ", ".join(rows)))
    (folder / "anim_tables.h").write_text("\n".join(out) + "\n", encoding="utf-8")


def build():
    out = Path(tempfile.mkdtemp(prefix="gevr-romcampaign-")) / "romcampaign_native.exe"
    anim_tables(out.parent)
    args = [shutil.which("gcc") or "gcc", "-O1", "-fms-extensions", "-Wno-builtin-declaration-mismatch", "-static"]
    args += ["-I" + str(out.parent)]
    args += ["-D" + v for v in ("GEVR=1", "PLATFORM_64BIT=1", "_LANGUAGE_C=1", "VERSION=2", "VERSION_US=1", "LANG_US=1")]
    args += ["-I" + str(ROOT / v) for v in (".", "port/include", "include", "src", "src/game", "port/src")]
    args += [str(ROOT / p) for p in ("port/tests/romcampaign_native.c", "port/src/gevr_romcampaign.c",
                                      "port/src/gevr_romlayout.c", "port/src/gevr_romcheck.c", "src/random.c",
                                      "port/src/gevr_rom_manifest.c", "port/src/gevr_romswap.c")]
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
    # random_tracks: up to GoldenEye's 43, ended by M_NONE (0)
    rnd = []
    while len(rnd) < 43:
        t = struct.unpack_from(">h", d, o)[0]
        o += 2
        if t == 0:
            break
        rnd.append(t)
    rnd += [0] * (44 - len(rnd))
    e["music"], e["random"] = music, rnd
    e["times"] = [struct.unpack_from(">hhh", d, 0x8002B564 - V + 6 * m) for m in range(20)]
    # boss.c memallocstringtable: {id, string} rows ended by the id 0 default
    mem, o = [], 0x800241BC - V
    while True:
        lid, p = struct.unpack_from(">II", d, o)
        mem.append((lid, " ".join(cstr(d, p).split())))
        o += 8
        if lid == 0:
            break
    e["mem"] = mem
    # check_if_imageID_is_light: bit 23 of each 4-byte image row
    iv, n = L["imagerows"]
    e["lamps"] = [i for i in range(n) if struct.unpack_from(">I", d, iv - V + 4 * i)[0] >> 23 & 1]
    return e


def close(a, b):
    return abs(float(a) - float(b)) <= 1e-3 * max(1.0, abs(float(b)))


def tnd(exe):
    """TND64 Expanded 06-22 rebuilt its animation segment (every record but the first moved,
    DAB geconvert.c animRecord()): the binder reads its tables, and every one of GoldenEye's
    records in the rebuilt segment is the hack's own (user: report 3020ba2c, it crashed starting)."""
    path = os.environ.get("GEVR_TND64_ROM")
    if not path or not Path(path).is_file():
        print("SKIP: set GEVR_TND64_ROM to a Tomorrow Never Dies 64 Expanded 06-22 .z64 to check its animations")
        return
    out = subprocess.run([str(exe), path, "2"], capture_output=True, text=True, check=True).stdout
    got = {}
    for line in out.splitlines():
        f = line.split()
        got.setdefault(f[0], []).append(f[1:])
    bad = []
    if got.get("match") != [["yes"]] or got.get("apply") != [["1"]] or got.get("rebind") != [["1"]]:
        bad.append("TND binds: %s %s %s" % (got.get("match"), got.get("apply"), got.get("rebind")))
    tables = got.get("animtables", [[]])[0]
    if len(tables) != 6 or tables[0] != tables[2] or tables[1] != tables[3] or int(tables[5]) == 0:
        bad.append("TND animation tables: %s" % tables)
    anim = got.get("anim", [["0", "1"]])[0]
    if int(anim[1]) != 0 or int(anim[0]) < 170:
        bad.append("TND animation records: %s ok, %s bad; %s" % (anim[0], anim[1], got.get("animbad", [])[:5]))
    if bad:
        print("\n".join(bad))
        print("FAIL: Tomorrow Never Dies")
        sys.exit(1)
    print("PASS: Tomorrow Never Dies 64: binds, and its %s moved animation records read at GoldenEye's offsets (%s ok)"
          % (tables[5], anim[0]))


def main():
    path = os.environ.get("GEVR_GF64_ROM")
    exe = None
    if os.environ.get("GEVR_TND64_ROM"):
        exe = build()
        tnd(exe)
    if not path or not Path(path).is_file():
        print("SKIP: set GEVR_GF64_ROM to a Goldfinger 64 1.0 .z64 to run the binder on it")
        return
    rom = open(path, "rb").read()
    exe = exe or build()
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
    check(got.get("canaries") == [["1"]], "a table write ran past GoldenEye's rows")
    lamps = [int(r[0]) for r in got.get("lamp", []) if r[1] == "1"]
    check(lamps == e["lamps"] == [121, 1211, 1345, 1823, 2004, 2056, 3406, 3729],
          "lamps %s, image rows %s" % (lamps, e["lamps"]))
    check(all(r[1] in ("0", "1") for r in got.get("lamp", [])), "lamp test fell back to GoldenEye's list")

    def memfor(stage, multi):
        rows = [s for lid, s in e["mem"] if multi and lid == stage + 400] + [s for lid, s in e["mem"] if lid == stage]
        return rows[0] if rows else e["mem"][-1][1]
    check(memfor(33, 0) == "-ml0 -me0 -mgfx200 -mvtx200 -mt800 -ma475", "the Cartel's memory split: %s" % memfor(33, 0))
    check(len(got.get("memstr", [])) == 42, "memory string lookups")
    for r in got.get("memstr", []):
        want = memfor(int(r[0]), int(r[1]))
        check(" ".join(r[2:]) == want, "memory string %s/%s: %s != %s" % (r[0], r[1], " ".join(r[2:]), want))
    tables = got.get("animtables", [[]])[0]
    check(len(tables) == 6 and tables[5] == "0" and tables[0] == tables[2],
          "Goldfinger's animation records are GoldenEye's: %s" % tables)
    check(got.get("poolinit") == [["1"]], "head pool level init")
    check(got.get("hats") == [["0", "126"]], "hat fits cover heads 0-125: %s" % got.get("hats"))
    hats = struct.unpack_from(">%df" % (126 * 6 * 6), rom, 0x17F8800)
    for r in got.get("hat", []):
        i = int(r[0])
        check(all(close(a, b) for a, b in zip(r[1:], hats[6 * i:6 * i + 6])), "hat fit %d" % i)
    picks = [int(r[3]) for r in got.get("pick", [])]
    check(picks == head_picks(rom), "head picks %s != %s" % (picks, head_picks(rom)))

    if bad:
        print("\n".join(bad[:40]))
        print("FAIL: %d mismatches" % len(bad))
        sys.exit(1)
    print("PASS: Goldfinger 64 binder: %d files, %d images, %d characters, %d props and their explosions, "
          "the item host map, gun headers and stats, head pools, hat fits, level scales, %d fog and %d fogless rows, music, target times, %d stage memory splits"
          % (len(files), e["images"], len(chrs), len(props), len(e["fog"]), len(e["fogless"]), len(e["mem"])))


if __name__ == "__main__":
    main()
