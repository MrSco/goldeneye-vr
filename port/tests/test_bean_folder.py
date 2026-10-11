"""The briefing folder repaint (gevr_bean_folder.c, DAB's gefolder.c) on real files.

usage: python3 port/tests/test_bean_folder.py ROM XBLA_FILES
(or GEVR_GE_ROM and GEVR_BEAN_FILES). XBLA_FILES holds new/prop/walletbond/default.bin.
Both are the player's own; without them this says SKIP.

The folder's 84 textures are read from PwalletbondZ and decoded from the ROM
(tools/texai). The repaint runs on them in a native build, and each picture it
binds is checked against the cartridge's own: the briefing photographs and the
mission slides must be the same picture the right way up (correlation, with the
upside-down picture as the control); the release's photograph of Bond is another
shot, so each of its quarters need only match its own quarter of the ROM's best.
The cutouts are white, the intensity tiles grey at the ROM tile's brightness,
the crest opaque.
"""
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
sys.path.insert(0, str(ROOT / "tools" / "texai"))

SWITCHES, TEXTURES = 43, 84          # PwalletbondZ: the switch table, then the texture table
BRIEFS, SLIDES = range(24, 64), range(64, 84)
BOND = (5, 6, 7, 8)
MASKS = (4, 17, 9, 18)
GREY = (10, 11)                      # the cover and the paper: I4 tiles the shade colours
CREST = 12                           # printed on the paper tile: opaque


def padded(width, depth):
    return (width + 3) & ~3 if depth >= 2 else (width + 7) & ~7 if depth == 1 else (width + 15) & ~15


def native(rom):
    import gevr_model_probe as probe
    import gevr_tex_decode as dec
    import rareimg
    import romkeys

    off, size = probe.manifest_lookup("PwalletbondZ")
    model = probe.decompress_1172(rom[off:off + size])
    seg, _ = dec.images_segment()
    offs = dec.texture_offsets()
    out = []
    for i in range(TEXTURES):
        tid, w, h, _mip, _kind, depth = struct.unpack_from(">IBBBBB", model, SWITCHES * 4 + 12 * i)
        got = romkeys.named(rom, tid, seg, offs)
        rgba = got[5] if got and got[3] == w and got[4] == h else None
        # pool bytes for the intensity tiles: rows padded to whole 64-bit lines
        pool = bytes(padded(w, depth) * h * (4 << depth) // 8)
        src = rom[seg + offs[tid]: seg + offs[tid + 1] + 16]
        if not (src[0] >> 6) & 1:
            img = rareimg.decode(bytes(src[1:]), src[0])
            if img is not None and img.w == w and img.h == h:
                row = padded(w, depth) * (4 << depth) // 8
                pool = b"".join(bytes(img.data[y * img.stride: y * img.stride + row]).ljust(row, b"\0")
                                for y in range(h))
        out.append(dict(tid=tid, w=w, h=h, depth=depth, rgba=rgba, pool=pool))
    return out


def lum(px, w, h):
    return [(px[4 * i] * 299 + px[4 * i + 1] * 587 + px[4 * i + 2] * 114) / 1000 for i in range(w * h)]


def shrink(rgba, w, h, cw, tw, th):
    """the picture's first cw columns, box-averaged to tw x th, as luminance"""
    full = lum(rgba, w, h)
    out = []
    for y in range(th):
        y0, y1 = y * h // th, max(y * h // th + 1, (y + 1) * h // th)
        for x in range(tw):
            x0, x1 = x * cw // tw, max(x * cw // tw + 1, (x + 1) * cw // tw)
            vals = [full[yy * w + xx] for yy in range(y0, y1) for xx in range(x0, x1)]
            out.append(sum(vals) / len(vals))
    return out


def corr(a, b):
    n = len(a)
    ma, mb = sum(a) / n, sum(b) / n
    sab = sum((x - ma) * (y - mb) for x, y in zip(a, b))
    sa = sum((x - ma) ** 2 for x in a) ** 0.5
    sb = sum((y - mb) ** 2 for y in b) ** 0.5
    return sab / (sa * sb) if sa and sb else 0.0


def build(temp):
    exe = temp / "folder"
    subprocess.run([
        os.environ.get("CC") or shutil.which("gcc") or "cc", "-std=c11", "-O2", "-fms-extensions",
        "-include", "stdarg.h", "-DPLATFORM_64BIT=1", "-D_LANGUAGE_C", "-DGEVR", "-DVERSION_US",
        *["-I" + str(ROOT / p) for p in (".", "include", "src", "src/game", "port/include")],
        str(ROOT / "port/tests/gevr_bean_folder_test.c"),
        *[str(ROOT / "port/src" / (p + ".c")) for p in ("gevr_bean_folder", "gevr_bean_tex", "gevr_bean_logic")],
        "-lm", "-o", str(exe)], check=True)
    return exe


def main():
    rom_path = sys.argv[1] if len(sys.argv) > 1 else os.environ.get("GEVR_GE_ROM")
    files = sys.argv[2] if len(sys.argv) > 2 else os.environ.get("GEVR_BEAN_FILES")
    wallet = Path(files or "") / "new/prop/walletbond/default.bin"
    if not rom_path or not Path(rom_path).is_file() or not wallet.is_file():
        print("SKIP: pass a GoldenEye 007 (USA) ROM and the XBLA files folder to check the folder repaint")
        return
    import gevr_model_probe as probe
    tex = native(probe.load_rom(rom_path))
    with tempfile.TemporaryDirectory(prefix="gevr-bean-folder-") as temp:
        temp = Path(temp)
        blob = struct.pack("<I", len(tex)) + b"".join(
            struct.pack("<HHB3xI", t["w"], t["h"], t["depth"], len(t["pool"])) + t["pool"] for t in tex)
        (temp / "textures.bin").write_bytes(blob)
        exe = build(temp)
        run = subprocess.run([str(exe), str(temp / "textures.bin"), str(wallet), str(temp / "out.bin")],
                             capture_output=True, text=True, check=True)
        data = (temp / "out.bin").read_bytes()
    got, at = [], 0
    for t in tex:
        w, h = struct.unpack_from("<II", data, at)
        at += 8
        got.append((w, h, data[at:at + 4 * w * h]))
        at += 4 * w * h

    bad = []

    def check(cond, what):
        if not cond:
            bad.append(what)

    bound = sum(1 for w, _, _ in got if w)
    check(bound == TEXTURES, "%d of %d textures repainted\n%s" % (bound, TEXTURES, run.stderr[-600:]))
    worst = {}
    mine_of = {}
    for i in list(BRIEFS) + list(SLIDES) + list(BOND):
        t, (w, h, rgba) = tex[i], got[i]
        if not w or t["rgba"] is None:
            check(False, "texture %d: nothing to compare" % i)
            continue
        cw = max(1, round(w * t["w"] / padded(t["w"], t["depth"])))
        mine = shrink(rgba, w, h, cw, t["w"], t["h"])
        flipped = shrink(b"".join(rgba[(h - 1 - y) * w * 4:(h - y) * w * 4] for y in range(h)), w, h, cw,
                         t["w"], t["h"])
        rom = lum(t["rgba"], t["w"], t["h"])
        mine_of[i] = mine
        c, cf = corr(mine, rom), corr(flipped, rom)
        kind = "brief" if i in BRIEFS else "slide" if i in SLIDES else "Bond"
        worst[kind] = min(worst.get(kind, 1.0), c)
        if kind == "Bond":
            check(c > cf, "Bond quarter %d: correlation %.3f, upside down %.3f" % (i, c, cf))
            continue
        check(c >= 0.9 and c > cf + 0.1,
              "%s texture %d (image %d): correlation %.3f, upside down %.3f" % (kind, i, t["tid"], c, cf))
    # each of Bond's quarters is its own quarter of the release's photograph
    for i in BOND:
        if i in mine_of and tex[i]["rgba"]:
            rom = lum(tex[i]["rgba"], tex[i]["w"], tex[i]["h"])
            own = corr(mine_of[i], rom)
            others = [corr(mine_of[j], rom) for j in BOND if j != i and j in mine_of]
            check(own > max(others), "Bond quarter %d matches another quarter better (%.3f, %s)" % (i, own, others))
    for i in MASKS:
        w, h, rgba = got[i]
        check(w and all(rgba[k] == 255 for k in range(len(rgba)) if k % 4 != 3), "mask %d is not white" % i)
    for i in GREY:
        t, (w, h, rgba) = tex[i], got[i]
        romgrey = sum(lum(t["rgba"], t["w"], t["h"])) / (t["w"] * t["h"]) if t["rgba"] else None
        mean = sum(lum(rgba, w, h)) / (w * h) if w else None
        chan = [sum(rgba[4 * k + c] for k in range(w * h)) / (w * h) for c in range(3)] if w else [0, 99, 0]
        neutral = max(chan) - min(chan) <= 2   # its hue is the shade's; the grain stays per channel
        check(neutral and romgrey is not None and abs(mean - romgrey) <= 3,
              "grey tile %d: neutral %s, mean %s against the ROM's %s" % (i, bool(neutral), mean, romgrey))
    w, h, rgba = got[CREST]
    check(w and all(rgba[k] == 255 for k in range(3, len(rgba), 4)), "the crest is not opaque")

    if bad:
        print("\n".join(bad[:30]))
        print("FAIL: %d problems" % len(bad))
        sys.exit(1)
    print("PASS: briefing folder: %d/%d textures repainted; correlation with the ROM's own: briefs >= %.3f, "
          "slides >= %.3f, Bond quarters %.3f+ each in place; masks white, tiles grey at the ROM's brightness, crest opaque"
          % (bound, TEXTURES, worst["brief"], worst["slide"], worst["Bond"]))


main()
