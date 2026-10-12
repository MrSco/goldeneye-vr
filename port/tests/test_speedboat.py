"""The Dam speedboat: 1.75x retail, the north jetty, the crossing, the landing.

With GEVR_GE_ROM set to a GoldenEye 007 (USA) .z64 (the player's own), the pad
choice also runs on Dam's real pads: the island is the separate patch of floor
out in the reservoir (pads 79-81), the jetty the unused waterline pad 238.
"""
import os
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile
import zlib

ROOT = Path(__file__).resolve().parents[2]
V = 0x80020D90
# propdef record sizes in words, by type (setup.c sizepropdef)
WORDS = [1, 64, 2, 32, 33, 32, 0x3b, 0x21, 0x22, 7, 0x40, 0x95, 32, 0x36, 3, 32, 32, 32, 3, 4, 0x2d, 0x22,
         4, 4, 1, 2, 2, 2, 2, 2, 4, 1, 4, 5, 1, 4, 32, 10, 4, 0x2c, 0x2d, 32, 32, 32, 5, 0x38, 7, 37, 1]
# gevr_dam_boat.c gevrDamBoatReadsPad: door, prop, key, alarm, magazine, collectable, vehicle, tank, ammo, armour
READS_PAD = {1, 3, 4, 5, 7, 8, 39, 45, 20, 21}


def build(temp):
    exe = temp / "test.exe"
    subprocess.run(
        [shutil.which("gcc") or "gcc", "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror",
         "-I" + str(ROOT / "port/include"),
         str(ROOT / "port/tests/speedboat_native.c"),
         str(ROOT / "port/src/gevr_speedboat.c"),
         "-lm", "-o", str(exe)],
        check=True)
    return exe


def inflate(blob):
    return zlib.decompressobj(-15).decompress(blob[2:]) if blob[:2] == b"\x11\x72" else blob


def dam_pads(rom):
    """Dam's pads in world units, as gevrDamBoatPrepare sees them, and which lie on a floor tile."""
    d = zlib.decompressobj(-15).decompress(rom[0x21992:])

    def cstr(p):
        o = p - V
        return d[o:d.index(b"\0", o)].decode()

    rows = 0
    while struct.unpack_from(">I", d, 0x252C4 + 12 * rows)[0] == rows:
        rows += 1
    files = {}
    for k in range(1, rows - 1):
        idx, name, addr = struct.unpack_from(">III", d, 0x252C4 + 12 * k)
        files[cstr(name)] = rom[addr:struct.unpack_from(">I", d, 0x252C4 + 12 * (k + 1) + 8)[0]]
    for i in range(38):
        lid, bg, stan, scale = struct.unpack_from(">IIIf", d, 0x8004448C - V + 24 * i)
        if lid == 33:
            break
    stan = inflate(files[cstr(stan)])
    setup = inflate(files["UsetupdamZ"])

    first = struct.unpack_from(">I", stan, 4)[0]
    tiles = []
    t = first
    while struct.unpack_from(">I", stan, t)[0]:
        n = struct.unpack_from(">H", stan, t + 6)[0] >> 12
        tiles.append([struct.unpack_from(">hhh", stan, t + 8 + 8 * j) for j in range(n)])
        t += 8 + 8 * max(3, n)

    def on_floor(x, y, z):
        for pts in tiles:
            if len(pts) < 3 or abs(sum(p[1] for p in pts) / len(pts) - y) > 400:
                continue
            inside = False
            for a, b in zip(pts, pts[1:] + pts[:1]):
                if (a[2] > z) != (b[2] > z) and x < a[0] + (z - a[2]) * (b[0] - a[0]) / (b[2] - a[2]):
                    inside = not inside
            if inside:
                return True
        return False

    hdr = struct.unpack_from(">10I", setup, 0)
    pads = []
    o = hdr[6]
    while struct.unpack_from(">I", setup, o + 0x24)[0]:
        x, y, z = struct.unpack_from(">3f", setup, o)
        pads.append([x / scale, y / scale, z / scale, 0, int(on_floor(x, y, z))])
        o += 44
    o = hdr[3]
    while setup[o + 3] != 48:
        kind = setup[o + 3]
        if kind in READS_PAD:
            pad = struct.unpack_from(">h", setup, o + 6)[0]
            if 0 <= pad < len(pads):
                pads[pad][3] = 1
        o += 4 * WORDS[kind]
    return pads


def choose(exe, temp, pads):
    path = temp / "pads.txt"
    path.write_text("".join("%.3f %.3f %.3f %d %d\n" % tuple(p) for p in pads))
    out = subprocess.run([str(exe), str(path)], check=True, capture_output=True, text=True).stdout.split()
    return int(out[1]), int(out[3])


def test_dam_speedboat():
    with tempfile.TemporaryDirectory(prefix="gevr-speedboat-") as temp:
        temp = Path(temp)
        exe = build(temp)
        subprocess.run([str(exe)], check=True)
        print("PASS: speedboat scale, pad choice, crossing, landing and reservoir clamp (synthetic pads)")

        rom = os.environ.get("GEVR_GE_ROM")
        if not rom or not Path(rom).is_file():
            print("SKIP: set GEVR_GE_ROM to a GoldenEye 007 (USA) .z64 to check the choice on Dam's pads")
            return
        pads = dam_pads(open(rom, "rb").read())

        def on_island(i):
            # 79-81 stand on a floor patch with no tile link to the rest of Dam, 290 m out;
            # 259-268 (no tile names) are the island's hut
            return ((pads[i][0] - pads[81][0]) ** 2 + (pads[i][2] - pads[81][2]) ** 2) ** 0.5 < 2000

        # the game finds a floor near a pad its tile misses, so the choice must not hang on those
        for label, chosen in (("on the floor", pads), ("all", [p[:4] + [1] for p in pads])):
            jetty, island = choose(exe, temp, chosen)
            assert on_island(island), "%s pads: island pad %d is not on the reservoir island" % (label, island)
            assert jetty == 238, "%s pads: jetty pad %d, not the unused waterline pad 238" % (label, jetty)
        crossing = ((pads[238][0] - pads[island][0]) ** 2 + (pads[238][2] - pads[island][2]) ** 2) ** 0.5
        print("PASS: Dam's %d pads: jetty 238, island %d, %.0f m across" % (len(pads), island, crossing / 100))


test_dam_speedboat()
