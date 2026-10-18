"""Bean image repaints (gevr_bean_images.c) on real files.

usage: python3 port/tests/test_bean_images.py ROM XBLA_FILES
(or GEVR_GE_ROM and GEVR_BEAN_FILES). XBLA_FILES holds texture/<name>/default.rba.
Both are the player's own; without them this says SKIP.

Each row of the table in gevr_bean_images.c is run as texLoad runs it, on the
image's ROM size and depth, and the picture it binds is shrunk to the ROM's size
and correlated with the cartridge's own texels: it must match, and match better
than the same picture turned over. A portrait's tiles must each match their own
tile best. The menu cursor must be the release's 256x256 sight. An address that
another image reuses must lose its paint.
"""
import os
from pathlib import Path
import re
import shutil
import struct
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
sys.path.insert(0, str(ROOT / "tools" / "texai"))

# below 0.95: pictures 4J redrew (DAB's own rows), measured on the real files
FLOOR = {2236: 0.85, 2161: 0.75, 2164: 0.88, 2233: 0.80, 2234: 0.92, 2238: 0.85, 2235: 0.88, 2166: 0.94}
# not the cartridge's picture at all, bound by DAB all the same: texture/bulletholes/bullethole is a
# smaller hole than the ROM's 2171 (mean alpha 42 against 170), and geimpact.c gives it GE_IMAGE_IMPACT4
OTHER_PICTURE = {2171}


def padded(width, depth):
    return (width + 3) & ~3 if depth >= 2 else (width + 7) & ~7 if depth == 1 else (width + 15) & ~15


def lum(px, w, h):
    return [(px[4 * i] * 299 + px[4 * i + 1] * 587 + px[4 * i + 2] * 114) / 1000 * px[4 * i + 3] / 255
            for i in range(w * h)]


def shrink(rgba, w, h, cw, tw, th):
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


def main():
    rom_path = sys.argv[1] if len(sys.argv) > 1 else os.environ.get("GEVR_GE_ROM")
    files = sys.argv[2] if len(sys.argv) > 2 else os.environ.get("GEVR_BEAN_FILES")
    if not rom_path or not Path(rom_path).is_file() or not files or not (Path(files) / "texture").is_dir():
        print("SKIP: pass a GoldenEye 007 (USA) ROM and the XBLA files folder to check the image repaints")
        return
    import gevr_model_probe as probe
    import gevr_tex_decode as dec
    import romkeys

    rows = re.findall(r'\{ (\d+), "([^"]+)", (WHOLE|UL|UR|LL|LR) \}',
                      (ROOT / "port/src/gevr_bean_images.c").read_text())
    rom = probe.load_rom(rom_path)
    seg, _ = dec.images_segment()
    offs = dec.texture_offsets()
    native = {}
    for image, _, _ in rows:
        got = romkeys.named(rom, int(image), seg, offs)
        native[int(image)] = got   # (name, gbifmt, siz, w, h, rgba, crc)

    with tempfile.TemporaryDirectory(prefix="gevr-bean-images-") as temp:
        temp = Path(temp)
        (temp / "added-content/xbla").mkdir(parents=True)
        os.symlink(os.path.abspath(files), temp / "added-content/xbla/files")
        blob = struct.pack("<I", len(rows))
        for image, _, _ in rows:
            n = native[int(image)]
            blob += struct.pack("<iHHB3x", int(image), n[3], n[4], n[2])
        (temp / "in.bin").write_bytes(blob)
        exe = temp / "images"
        subprocess.run([
            os.environ.get("CC") or shutil.which("gcc") or "cc", "-std=c11", "-O2", "-fms-extensions",
            "-include", "stdarg.h", "-DPLATFORM_64BIT=1", "-D_LANGUAGE_C", "-DGEVR", "-DVERSION_US",
            *["-I" + str(ROOT / p) for p in (".", "include", "src", "src/game", "port/include")],
            str(ROOT / "port/tests/gevr_bean_images_test.c"),
            *[str(ROOT / "port/src" / (p + ".c")) for p in ("gevr_bean_images", "gevr_bean_tex", "gevr_bean_logic", "gevr_mirror")],
            "-lm", "-o", str(exe)], check=True)
        run = subprocess.run([str(exe), "in.bin", "out.bin"], cwd=temp, capture_output=True, text=True, check=True)
        data = (temp / "out.bin").read_bytes()

    got, at = [], 0
    for _ in rows:
        w, h = struct.unpack_from("<II", data, at)
        at += 8
        got.append((w, h, data[at:at + 4 * w * h]))
        at += 4 * w * h

    bad = []

    def check(cond, what):
        if not cond:
            bad.append(what)

    check(got[-1][0] == 0, "image %s kept its paint after another image took its address" % rows[-1][0])
    worst = 1.0
    tiles = {}
    for k, ((image, picture, crop), (w, h, rgba)) in enumerate(zip(rows, got)):
        if k == len(rows) - 1:
            continue
        image = int(image)
        name, _, siz, tw, th, nrgba, _ = native[image]
        if not w:
            check(False, "image %d (%s): not repainted" % (image, picture))
            continue
        if image in OTHER_PICTURE:
            continue
        cw = max(1, round(w * tw / padded(tw, siz)))
        mine = shrink(rgba, w, h, cw, tw, th)
        flipped = shrink(b"".join(rgba[(h - 1 - y) * w * 4:(h - y) * w * 4] for y in range(h)), w, h, cw, tw, th)
        ref = lum(nrgba, tw, th)
        c, cf = corr(mine, ref), corr(flipped, ref)
        worst = min(worst, c)
        check(c >= FLOOR.get(image, 0.95) and c >= cf - 0.01,
              "image %d (%s): correlation %.3f, turned over %.3f" % (image, picture, c, cf))
        if crop != "WHOLE":
            tiles.setdefault(picture, []).append((image, mine, ref))
    for picture, group in tiles.items():
        for image, mine, ref in group:
            own = corr(mine, ref)
            others = [corr(m2, ref) for i2, m2, _ in group if i2 != image]
            check(own > max(others), "%s tile %d matches another tile better (%.3f, %s)" % (picture, image, own, others))
    sight = [g for (image, _, _), g in zip(rows, got) if int(image) == 2236]
    check(sight and sight[0][:2] == (256, 256), "the menu cursor is %s, not 256x256" % (sight[0][:2] if sight else None,))

    if bad:
        print("\n".join(bad[:40]))
        print(run.stderr[-800:])
        print("FAIL: %d problems" % len(bad))
        sys.exit(1)
    print("PASS: %d Bean image repaints match the ROM's own pictures (worst %.3f), %d portraits tile by tile, "
          "the cursor at 256x256, a reused address unpainted" % (len(rows) - 1, worst, len(tiles)))


main()
