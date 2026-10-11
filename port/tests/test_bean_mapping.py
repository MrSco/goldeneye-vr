"""Map the real native model catalog, beyond testing CAFF decoding alone.

python port/tests/test_bean_mapping.py ROM.z64 path/to/xbla/files
The cartridge and extracted models are read locally and never committed.
"""
from pathlib import Path
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
from gevr_model_probe import load_rom, manifest_lookup, decompress_1172
from gevr_model_export import header_counts


def main():
    if len(sys.argv) != 3:
        raise SystemExit(__doc__)
    rom, assets = load_rom(sys.argv[1]), Path(sys.argv[2]).resolve()
    mappings = re.findall(r'\{ "([^"]+)", (\w+), "([^"]+)" \}',
                          (ROOT / "port/include/gevr_bean_files.h").read_text())
    assert len({m[0] for m in mappings}) == len(mappings), "duplicate native file"
    results, failures, rooms = [], [], []
    compiler = os.environ.get("CC") or shutil.which("gcc") or "cc"
    sanitizers = ["-fsanitize=address,undefined", "-fno-omit-frame-pointer", "-no-pie"] if os.environ.get("BEAN_SANITIZE") else []
    with tempfile.TemporaryDirectory(prefix="bean-mapping-") as temporary:
        directory = Path(temporary)
        exe = directory / "mapping.exe"
        subprocess.run([
            compiler, "-std=c11", "-O2", "-fms-extensions", "-include", "stdarg.h", *sanitizers,
            "-DPLATFORM_64BIT=1", "-D_LANGUAGE_C", "-DGEVR", "-DVERSION_US",
            *["-I" + str(ROOT / p) for p in (".", "include", "src", "src/game", "port/include")],
            *[str(ROOT / "port/tests" / p) for p in ("gevr_bean_mapping_test.c", "gevr_bean_native_model.c")],
            *[str(ROOT / "port/src" / (p + ".c")) for p in ("gevr_bean_load", "gevr_bean_tex", "gevr_bean_logic")],
            "-lm", "-o", str(exe),
        ], check=True)
        for name, kind, source in mappings:
            # Windows fopen ignores case; Android does not. Check every component.
            file = assets
            for component in (source + "/default.bin").split("/"):
                assert component in {p.name for p in file.iterdir()}, f"missing/case-mismatched {source}"
                file /= component
            offset, size = manifest_lookup(name)
            raw = directory / "native.bin"
            raw.write_bytes(decompress_1172(rom[offset:offset + size]))
            switches, matrices, textures = header_counts(name)
            run = subprocess.run([str(exe), str(raw), name, str(switches), str(textures),
                                  str(matrices), str(file), kind], capture_output=True, text=True)
            if run.returncode:
                failures.append(name)
                print(name, run.stderr[-1200:], flush=True)
            else:
                result = run.stdout.strip()
                results.append(result)
                print(result, flush=True)
        room_exe = directory / "rooms.exe"
        subprocess.run([
            compiler, "-std=c11", "-O2", "-fms-extensions", "-include", "stdarg.h", *sanitizers,
            "-DPLATFORM_64BIT=1", "-D_LANGUAGE_C", "-DGEVR", "-DVERSION_US",
            *["-I" + str(ROOT / p) for p in (".", "include", "src", "src/game", "port/include")],
            str(ROOT / "port/tests/gevr_bean_room_mapping_test.c"),
            str(ROOT / "port/src/gevr_romcheck.c"), str(ROOT / "port/src/gevr_rom_manifest.c"),
            "-lz", "-lm", "-o", str(room_exe),
        ], check=True)
        stage_ids = re.findall(r'\{\s*(LEVELID_\w+),', (ROOT / "port/src/gevr_bean_world.c").read_text())
        assert len(stage_ids) == 26, "stage catalog was not parsed completely"
        backgrounds = dict(re.findall(r'\{(LEVELID_\w+),\s*"([^" ]+)"', (ROOT / "src/game/bg.c").read_text()))
        for stage in stage_ids:
            name = backgrounds[stage]
            offset, size = manifest_lookup(name)
            raw = directory / "bg.bin"
            raw.write_bytes(rom[offset:offset + size])
            run = subprocess.run([str(room_exe), str(raw)], capture_output=True, text=True)
            if run.returncode:
                failures.append(stage)
                print(stage, run.stderr[-1200:], flush=True)
            else:
                result = stage + " " + run.stdout.splitlines()[0]
                rooms.append(result)
                print(result, flush=True)
    catalog = re.findall(r'/\*\s*\S+\s+([CGP]\w+Z)\s*\*/',
                         (ROOT / "port/src/gevr_rom_manifest.c").read_text())
    unmapped = sorted(set(catalog) - {row[0] for row in mappings})
    report = ROOT / "build/bean/mapping-audit.json"
    report.parent.mkdir(parents=True, exist_ok=True)
    report.write_text(json.dumps({"mappedFiles": len(mappings), "results": results, "rooms": rooms, "nativeFallbackFiles": unmapped, "failures": failures}, indent=2))
    print(f"Native Bean mapping: {len(results)}/{len(mappings)} passed; report: {report}")
    if failures:
        raise SystemExit("Failed mapping: " + ", ".join(failures))


if __name__ == "__main__":
    main()
