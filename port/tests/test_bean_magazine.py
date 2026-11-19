"""Bean guns' removable magazines (port/src/gevr_beanmag.c): the cut on a made-up
grip, and with your release's files the PP7s' own.

  python port/tests/test_bean_magazine.py [path/to/xbla/files]

Without the files only the made-up grip is checked. With them each gun in the
table is loaded by the production loader, cut, and checked: a foot comes off,
none of it stands above the plane, the gun without it keeps everything else.
The release's files are read locally and never copied.
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
        if len(sys.argv) < 2:
            print("SKIP: the release's guns (give your xbla files folder to check them)")
            return
        files = Path(sys.argv[1])
        dump = Path(temp) / ("dump" + EXE)
        subprocess.run([CC, "-std=c11", "-O1", "-include", "stdarg.h", "-I" + str(ROOT / "port/include"),
                        str(ROOT / "port/tests/gevr_bean_mag_dump.c"), str(ROOT / "port/src/gevr_beanmag.c"),
                        str(ROOT / "port/src/gevr_bean_logic.c"), str(ROOT / "port/src/gevr_bean_tex.c"),
                        "-lm", "-o", str(dump)], check=True)
        table = (ROOT / "port/src/gevr_beanmag.c").read_text(encoding="utf-8")
        guns = re.findall(r'^\tPP7_MAGAZINE\("([^"]+)"\)', table, re.M)
        assert guns, "no guns in gevr_beanmag.c's table"
        plane = [float(v) for v in re.search(r"\.plane = \{([^}]+)\}", table).group(1).replace("f", "").split(",")]
        # every gun the table names is one the port maps, and the hand reload knows its item
        mapped = set(re.findall(r'"new/(gun/[^"]+)"', (ROOT / "port/include/gevr_bean_files.h").read_text()))
        assert set(guns) <= mapped, set(guns) - mapped
        for source in guns:
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
            foot = [p for kind, p in sets["m"] if kind == 2]
            made = [p for kind, p in sets["m"] if kind == 3]
            assert len(foot) >= 20 and len(made) >= 12, (source, len(foot), len(made))

            def side(p, c):
                return plane[0] * p[c] + plane[1] * p[c + 1] + plane[2] * p[c + 2] + plane[3]
            # the foot hangs below the plane, the body stands on it, the gun keeps the rest
            assert all(side(p, c) <= 0.3 for p in foot for c in (0, 3, 6)), source
            assert all(side(p, c) >= -0.3 for p in made for c in (0, 3, 6)), source
            hands = sum(1 for kind, _ in sets["g"] if kind == 1)
            assert hands == sum(1 for kind, _ in sets["o"] if kind == 1), f"{source}: the hand was cut"
            floor = [p for kind, p in sets["o"] if kind == 3]
            assert len(floor) >= 3, source
            axis = [float(v) for v in facts["axis"]]
            assert abs(sum(v * v for v in axis) - 1) < 1e-3 and axis[1] > 0.9, (source, axis)
            mouth, top = ([float(v) for v in facts[k]] for k in ("mouth", "top"))
            assert top[1] - mouth[1] > 250, (source, mouth, top)   # a PP7's well: most of the grip
            print(f"{source}: foot {len(foot)} triangles, body {len(made)}, floor {len(floor)}")
        print("PASS: the release's PP7s cut - foot below the plane, body on it, hands whole, wells floored")


if __name__ == "__main__":
    main()
