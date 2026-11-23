"""Bean guns' removable magazines (port/src/gevr_beanmag.c): the cut on made-up
guns, and with your release's files every gun of the table.

  python port/tests/test_bean_magazine.py [path/to/xbla/files]

Without the files only the made-up guns are checked. With them each gun in the
table is loaded by the production loader, its magazine taken, and checked: the
hand is whole, and a pistol's foot hangs below its plane with the body standing
on it and the well floored, or a rifle's magazine is a piece of the mesh on its
own with a lid and the gun otherwise untouched. The release's files are read locally, never copied.
"""
from pathlib import Path
import os
import re
import shutil
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
CC = os.environ.get("CC") or shutil.which("gcc") or "cc"
EXE = ".exe" if os.name == "nt" else ""


def main():
    with tempfile.TemporaryDirectory(prefix="gevr-bean-mag-") as temp:
        unit = Path(temp) / ("unit" + EXE)
        subprocess.run([CC, "-std=c11", "-O1", "-Wall", "-Wextra", "-Werror", "-I" + str(ROOT / "port/include"),
                        str(ROOT / "port/tests/gevr_beanmag_test.c"), str(ROOT / "port/src/gevr_beanmag.c"),
                        "-lm", "-o", str(unit)], check=True)
        subprocess.run([str(unit)], check=True)
        dump = Path(temp) / ("dump" + EXE)
        subprocess.run([CC, "-std=c11", "-O1", "-include", "stdarg.h", "-I" + str(ROOT / "port/include"),
                        str(ROOT / "port/tests/gevr_bean_mag_dump.c"), str(ROOT / "port/src/gevr_beanmag.c"),
                        str(ROOT / "port/src/gevr_bean_logic.c"), str(ROOT / "port/src/gevr_bean_tex.c"),
                        "-lm", "-o", str(dump)], check=True)
        table = [line.split() for line in subprocess.run([str(dump), "--guns"], capture_output=True, text=True,
                                                         check=True).stdout.splitlines() if line.strip()]
        guns = [row[0] for row in table]
        assert len(guns) == len(set(guns)) and "gun/ppk" in guns and "gun/ak47" in guns and "gun/shotgun" in guns, guns
        # every gun the table names is one the port maps and fits by that name
        mapped = set(re.findall(r'"new/(gun/[^"]+)"', (ROOT / "port/include/gevr_bean_files.h").read_text()))
        fitted = set(re.findall(r'BEANFIT\("\w+", "(gun/[^"]+)"', (ROOT / "port/include/gevr_bean_fits.h").read_text()))
        assert set(guns) <= mapped and set(guns) <= fitted, (set(guns) - mapped, set(guns) - fitted)
        files = sys.argv[1] if len(sys.argv) > 1 else os.environ.get("GEVR_BEAN_FILES")
        if not files:
            print("SKIP: the release's guns (give your xbla files folder, or GEVR_BEAN_FILES, to check them)")
            return
        files = Path(files)
        for source, island, lifts, single, keep, built in table:
            run = subprocess.run([str(dump), str(files / "new" / source / "default.bin"), source],
                                 capture_output=True, text=True)
            assert run.returncode == 0, f"{source}: {run.stderr.strip()[-400:]}"
            facts, sets = {}, {"g": [], "o": [], "m": []}
            for line in run.stdout.splitlines():
                part = line.split()
                if part and part[0] == "#":
                    facts[part[1]] = part[2:]
                elif part and part[0] in sets:
                    sets[part[0]].append((int(part[1]), [float(v) for v in part[2:11]]))
            own = [p for kind, p in sets["m"] if kind == 2]
            made = [p for kind, p in sets["m"] if kind == 3]
            hands = sum(1 for kind, _ in sets["g"] if kind == 1)
            assert hands == sum(1 for kind, _ in sets["o"] if kind == 1), f"{source}: the hand was cut"
            floor = [p for kind, p in sets["o"] if kind == 3]
            if int(built):
                # the sniper rifle's: the mesh has none, so a box and its foot plate are made,
                # under the gun (its top inside, its foot below the gun's underside there)
                assert len(own) == 0 and len(made) == 24, (source, len(own), len(made))
                under = [p[c + 1] for kind, p in sets["g"] if kind == 0 for c in (0, 3, 6)
                         if abs(p[c]) < 60 and min(q[2] for q in [m[i:i + 3] for m in made for i in (0, 3, 6)]) <= p[c + 2]
                         <= max(q[2] for q in [m[i:i + 3] for m in made for i in (0, 3, 6)])]
                low = min(p[c + 1] for p in made for c in (0, 3, 6))
                high = max(p[c + 1] for p in made for c in (0, 3, 6))
                assert under and low < min(under) - 60 and high > min(under) + 60, (source, low, high, min(under))
            else:
                assert len(own) >= 20, (source, len(own))
            # a pistol's empty well is floored; where a rifle's magazine went in is left as the
            # release has it (the AR33's floor hung in the air under the receiver)
            assert (len(floor) == 0) if int(island) else (len(floor) >= 3), (source, len(floor))
            axis, mouth, top, hold = ([float(v) for v in facts[k]] for k in ("axis", "mouth", "top", "hold"))
            assert abs(sum(v * v for v in axis) - 1) < 1e-3, (source, axis)
            # into the gun is up for one under it, down for one that lifts off its top; a
            # cylinder goes in from the side and a rocket down the tube
            if int(lifts):
                assert axis[1] < -0.7, (source, axis)
            # the top is the mouth's side of it that way; a loose round goes in at a port instead
            if not int(single):
                assert sum((t - m) * a for t, m, a in zip(top, mouth, axis)) >= -1e-3, (source, mouth, top)
            # the palm's point is within the magazine
            points = [p[c:c + 3] for p in own + made for c in (0, 3, 6)]
            for k in range(3):
                assert min(p[k] for p in points) - 1 <= hold[k] <= max(p[k] for p in points) + 1, (source, hold)
            if int(island):
                # the release's own piece, whole: the gun lost exactly those triangles, or
                # none of them when the round is a copy of a shell that stays on its saddle
                gun = sum(1 for kind, _ in sets["g"] if kind == 0)
                assert sum(1 for kind, _ in sets["o"] if kind == 0) == gun - (0 if int(keep) else len(own)), source
            else:
                plane = [float(v) for v in facts["plane"]]

                def side(p, c):
                    return plane[0] * p[c] + plane[1] * p[c + 1] + plane[2] * p[c + 2] + plane[3]
                # the foot hangs below the plane, the body stands on it
                assert all(side(p, c) <= 0.3 for p in own for c in (0, 3, 6)), source
                assert all(side(p, c) >= -0.3 for p in made for c in (0, 3, 6)), source
                assert top[1] - mouth[1] > 250, (source, mouth, top)   # a pistol's well: most of the grip
            print(f"{source}: {'made for it' if int(built) else 'a piece of its own' if int(island) else 'cut from the grip'}, "
                  f"{len(own)} triangles, {len(made)} made, floor {len(floor)}")
        print(f"PASS: the release's {len(guns)} guns - magazines taken, hands whole, pistols' wells floored")


if __name__ == "__main__":
    main()
