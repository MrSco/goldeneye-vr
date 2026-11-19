"""Draw a Bean gun's removable magazine as the port cuts it (port/src/gevr_beanmag.c).

  python tools/gevr_bean_mag_preview.py XBLA_FILES OUT_DIR [gun/ppk ...]

XBLA_FILES is your imported release's files folder (the one holding new/gun).
For each gun that has a magazine it writes OUT_DIR/<gun>_magazine.png: the gun,
the gun with its magazine out (seen from the side and from below, into the
well), and the magazine on its own. The release's own triangles of the magazine
are drawn blue, what the port makes (a pistol's body, a lid, the well's floor)
orange. Needs a C compiler, numpy and Pillow.
Reads your files; writes pictures only, which hold the release's shapes - keep
them to yourself.
"""
import argparse
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
KIND = {0: (172, 172, 176), 1: (205, 155, 125), 2: (90, 150, 235), 3: (240, 150, 50)}
# (across, up, towards the eye, the eye's side): the mesh is x left, y up, z along the barrel
VIEWS = {"side": (2, 1, 0, -1), "below": (2, 0, 1, -1), "behind": (0, 1, 2, -1)}


def build(temp):
    exe = Path(temp) / ("magdump.exe" if os.name == "nt" else "magdump")
    subprocess.run([os.environ.get("CC") or shutil.which("gcc") or "cc", "-std=c11", "-O1",
                    "-include", "stdarg.h", "-I" + str(ROOT / "port/include"),
                    str(ROOT / "port/tests/gevr_bean_mag_dump.c"), str(ROOT / "port/src/gevr_beanmag.c"),
                    str(ROOT / "port/src/gevr_bean_logic.c"), str(ROOT / "port/src/gevr_bean_tex.c"),
                    "-lm", "-o", str(exe)], check=True)
    return exe


def dump(exe, files, source):
    path = Path(files) / "new" / source / "default.bin"
    run = subprocess.run([str(exe), str(path), source], capture_output=True, text=True)
    if run.returncode:
        raise SystemExit(f"{source}: {run.stderr.strip() or 'failed'}")
    sets, facts = {"g": [], "o": [], "m": []}, {}
    for line in run.stdout.splitlines():
        part = line.split()
        if not part:
            continue
        if part[0] == "#":
            facts[part[1]] = [float(v) for v in part[2:]] if part[1] != "tris" else part[2:]
        elif part[0] in sets:
            sets[part[0]].append((int(part[1]), [float(v) for v in part[2:11]]))
    return sets, facts


def render(tris, view, size, bounds):
    import numpy as np
    from PIL import Image, ImageDraw
    across, up, depth, side = view
    image = Image.new("RGB", (size, size), (22, 22, 26))
    if not tris:
        return image
    points = np.array([p for _, p in tris]).reshape(-1, 3, 3)
    lo, hi = bounds
    span = max(hi[across] - lo[across], hi[up] - lo[up]) * 1.08
    cx, cy = (hi[across] + lo[across]) / 2, (hi[up] + lo[up]) / 2
    normal = np.cross(points[:, 1] - points[:, 0], points[:, 2] - points[:, 0])
    length = np.linalg.norm(normal, axis=1)
    length[length == 0] = 1
    normal /= length[:, None]
    draw = ImageDraw.Draw(image)
    # the guns are drawn two-sided, so the far faces show through an open well
    for i in np.argsort(points[:, :, depth].mean(1) * side):
        shade = 0.35 + 0.65 * abs(normal[i, depth])
        colour = tuple(int(c * shade) for c in KIND[tris[i][0]])
        draw.polygon([((p[across] - cx) / span * size + size / 2, size / 2 - (p[up] - cy) / span * size)
                      for p in points[i]], fill=colour)
    return image


def sheet(sets, out):
    import numpy as np
    from PIL import Image
    size = 520
    gun = [t for t in sets["g"] if t[0] != 1]
    opened = [t for t in sets["o"] if t[0] != 1]
    points = np.array([p for _, p in gun]).reshape(-1, 3)
    bounds = (points.min(0), points.max(0))
    mag = np.array([p for _, p in sets["m"]]).reshape(-1, 3)
    magBounds = (mag.min(0), mag.max(0))
    cells = [render(gun, VIEWS["side"], size, bounds), render(opened, VIEWS["side"], size, bounds),
             render(opened, VIEWS["below"], size, bounds),
             render(sets["m"], VIEWS["side"], size, magBounds), render(sets["m"], VIEWS["behind"], size, magBounds),
             render(sets["m"], VIEWS["below"], size, magBounds)]
    page = Image.new("RGB", (size * 3, size * 2))
    for i, cell in enumerate(cells):
        page.paste(cell, ((i % 3) * size, (i // 3) * size))
    page.save(out)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("files", type=Path)
    parser.add_argument("out", type=Path)
    parser.add_argument("guns", nargs="*", help="as gun/ppk; every gun of the table when none is named")
    args = parser.parse_args()
    args.out.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="bean-mag-") as temp:
        exe = build(temp)
        table = subprocess.run([str(exe), "--guns"], capture_output=True, text=True, check=True).stdout.split("\n")
        for source in args.guns or [line.split()[0] for line in table if line.strip()]:
            sets, facts = dump(exe, args.files, source)
            out = args.out / (source.replace("/", "_") + "_magazine.png")
            sheet(sets, out)
            print(f"{source}: {facts['tris'][0]} triangles, {facts['tris'][6]} the magazine's own, "
                  f"{int(facts['tris'][4]) - int(facts['tris'][6])} made for it")
            for key in ("axis", "mouth", "top", "foot", "hold"):
                print(f"  {key} " + " ".join(f"{v:.2f}" for v in facts[key]))
            print(f"  {out}")


if __name__ == "__main__":
    main()
