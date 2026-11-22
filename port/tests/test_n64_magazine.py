"""GoldenEye's own guns' removable magazines, on the ROM's models (port/src/gevr_n64mag.c).

  python port/tests/test_n64_magazine.py ROM.z64      (or GEVR_GE_ROM)

Each gun in the table is converted by the production converter and laid out:
the magazine's triangles are counted against the cartridge's, the gun without
it draws the rest, and the magazine alone draws only itself. Without a ROM the
table alone is checked. The ROM is read locally; nothing of it is kept.
"""
from pathlib import Path
import os
import re
import shutil
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))

# the cartridge's own counts: each hanging magazine is one list of the model
TRIANGLES = {"GskorpionZ": 18, "Gak47Z": 26, "GuziZ": 10, "Gmp5kZ": 26, "Gmp5ksilZ": 26, "GspectreZ": 26,
             "Gm16Z": 26, "Gfnp90Z": 12, "GshotgunZ": 33, "GautoshotZ": 34,
             # the Cougar's cylinder, the grenade launcher's drum of six chambers: all of a joint's lists
             "GrugerZ": 20, "GgrenadelaunchZ": 156}
MADE = {"GwppkZ", "GwppksilZ", "GsilverwppkZ", "GgoldwppkZ", "Gtt33Z", "GsniperrifleZ", "GgoldengunZ"}
PROP = {"GrocketlaunchZ"}


def table():
    source = (ROOT / "port/src/gevr_n64mag.c").read_text(encoding="utf-8")
    body = source[source.index("s_defs[] = {"):source.index("};", source.index("s_defs[] = {"))]
    return re.findall(r'"(G\w+Z)"', body)


def main():
    names = table()
    assert set(names) == set(TRIANGLES) | MADE | PROP, sorted(set(names) ^ (set(TRIANGLES) | MADE | PROP))
    assert len(names) == len(set(names)), "a gun twice in the table"
    # every gun the hand reload racks or loads by a magazine has a row, by its file (gun.c gevrN64MagItems)
    rom_path = sys.argv[1] if len(sys.argv) > 1 else os.environ.get("GEVR_GE_ROM")
    if not rom_path or not Path(rom_path).is_file():
        print("n64 magazine: table ok (%d guns); no ROM given, models not checked" % len(names))
        return
    from gevr_model_probe import load_rom, manifest_lookup, decompress_1172
    from gevr_model_export import header_counts
    rom = load_rom(rom_path)
    compiler = os.environ.get("CC") or shutil.which("gcc") or "cc"
    sanitizers = ["-fsanitize=address,undefined", "-fno-omit-frame-pointer", "-no-pie"] if os.environ.get("BEAN_SANITIZE") else []
    with tempfile.TemporaryDirectory(prefix="n64-magazine-") as temporary:
        directory = Path(temporary)
        exe = directory / "n64mag.exe"
        subprocess.run([
            compiler, "-std=c11", "-O2", "-fms-extensions", "-include", "stdarg.h", *sanitizers,
            "-DPLATFORM_64BIT=1", "-D_LANGUAGE_C", "-DGEVR", "-DVERSION_US",
            *["-I" + str(ROOT / p) for p in (".", "include", "src", "src/game", "port/include")],
            str(ROOT / "port/tests/gevr_n64mag_test.c"), str(ROOT / "port/tests/gevr_bean_native_model.c"),
            str(ROOT / "port/src/gevr_n64mag.c"), "-lm", "-o", str(exe),
        ], check=True)
        for name in names:
            offset, size = manifest_lookup(name)
            raw = directory / "native.bin"
            raw.write_bytes(decompress_1172(rom[offset:offset + size]))
            switches, matrices, textures = header_counts(name)
            run = subprocess.run([str(exe), str(raw), name, str(switches), str(textures), str(matrices)],
                                 cwd=directory, capture_output=True, text=True)
            assert run.returncode == 0, f"{name}: {run.stderr[-1500:]}"
            line = run.stdout.strip()
            print(line)
            m = re.match(r"MAGAZINE (\w+) triangles (\d+) lists (\d+) opened (\d+)", line)
            assert m, f"{name}: no magazine laid out"
            triangles, lists, opened = int(m.group(2)), int(m.group(3)), int(m.group(4))
            if name in PROP:
                assert triangles == 0 and lists == 0 and opened == 0, line
            elif name in MADE:
                assert triangles == 12 and lists == 0 and opened == 0, line
            else:
                assert triangles == TRIANGLES[name], f"{name}: {triangles} triangles, the cartridge's is {TRIANGLES[name]}"
                assert lists >= 1 and (opened >= 1) == (name not in ("GshotgunZ", "GautoshotZ")), line
    print("n64 magazine: ok (%d guns on the ROM's models)" % len(names))


if __name__ == "__main__":
    main()
