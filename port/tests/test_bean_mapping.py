"""Check real native catalog registration and memory safety.

Passing these checks does not establish correct meshes, materials or visual fit.

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

_SKELETONS = None


def header_skeleton(name):
    """GoldenEye's skeleton for a model, from its MODELFILEHEADER ('none' for NULL)."""
    global _SKELETONS
    if _SKELETONS is None:
        _SKELETONS = {}
        rx = re.compile(r"MODELFILEHEADER\(\s*(\w+)\s*,[^,]*,\s*&SKELETON\((\w+)\)")
        for path in (ROOT / "assets/obseg").rglob("*.inc.c"):
            for m in rx.finditer(path.read_text(encoding="utf-8", errors="replace")):
                _SKELETONS[m.group(1).lower()] = m.group(2)
    stem = re.sub(r"[Zz]$", "", re.sub(r"^[A-Z]", "", name)).lower()
    return _SKELETONS.get(stem, "none")


# DAB's GoldenEye monitor screens under Bean's meshes (gevr_bean_screens.inc.h),
# against what DAB's gebeanBuildRigid() logs (CLAUDE-notes/ge-bean.md). Backing:
# "doorconsole 4, bridgeconsole1a/2a/3a/3b 4, tuningconsole1 6, nothing else on
# the twenty missions' monitors" (2026-09-25), and since 0d0bf4a1d the panes of
# beanVertexDrops[] are backing too where they lie on a screen (Dam's modem 4,
# the gate switch's two lamps 8, Silo's console2/3 placeholders 16). Recess:
# "modembox 12, Silo's tuning console 10, nothing else on the missions' opening
# views" (2fead5355); the same rule gives the door console and Frigate's bridge
# consoles 2a/3a/3b theirs, which no opening view showed. Fitted: "consolesev2b,
# consolesevb, console1-3, tv1; the bridge consoles (placeholders) are left
# alone", consolesev2b's pane "594x377 where GoldenEye's quad is 535x423" and
# Silo's "screen 378 by 369".
SCREEN_BACKING = {"PdoorconsoleZ": 4, "Pbridge_console1aZ": 4, "Pbridge_console2aZ": 4,
                  "Pbridge_console3aZ": 4, "Pbridge_console3bZ": 4, "Ptuning_console1Z": 6,
                  "PmodemboxZ": 4, "PdoorpanelZ": 8, "Pconsole2Z": 16, "Pconsole3Z": 16}
SCREEN_RECESS = {"PmodemboxZ": 12, "Ptuning_console1Z": 10, "PdoorconsoleZ": 7,
                 "Pbridge_console2aZ": 4, "Pbridge_console3aZ": 4, "Pbridge_console3bZ": 4}
# beanVertexDrops[]: console2/3 118-133, modembox 0-3, doorpanel 0-7
SCREEN_PANES = {"Pconsole2Z": 16, "Pconsole3Z": 16, "PmodemboxZ": 4, "PdoorpanelZ": 8}
SCREEN_FITTED = {"Pconsole_sev2bZ": 0x1, "Pconsole_sevbZ": 0x1, "Pconsole1Z": 0xf, "Pconsole2Z": 0xf,
                 "Pconsole3Z": 0xf, "Ptv1Z": 0x1}
# GoldenEye's monitors with a Bean mesh: every screen list handed to the programme
MONITORS = set(SCREEN_BACKING) | set(SCREEN_RECESS) | set(SCREEN_FITTED)


def check_screens(results):
    seen = {}
    rx = re.compile(r"SCREENS (\w+) backing (\d+) recess (\d+) fitted (\d+) panes (\d+) screens (\d+) "
                    r"handed (\d+) drawn (\d+) main (\d+) pane (\d+)x(\d+) quad (\d+)x(\d+)")
    for line in results:
        for m in rx.finditer(line):
            seen[m.group(1)] = tuple(int(x) for x in m.groups()[1:])
    assert seen, "no SCREENS lines: the monitor screen checks did not run"
    for name, (backing, recess, fitted, panes, screens, handed, drawn, main, *_) in sorted(seen.items()):
        assert panes == SCREEN_PANES.get(name, 0), f"{name}: {panes} vertices on beanVertexDrops[]"
        assert backing == SCREEN_BACKING.get(name, 0), f"{name}: {backing} screen backing vertices"
        assert recess == SCREEN_RECESS.get(name, 0), f"{name}: {recess} screen recess vertices"
        assert fitted == SCREEN_FITTED.get(name, 0), f"{name}: screens fitted mask {fitted}"
        assert not panes or drawn, f"{name}: the panes were not laid behind any screen"
        if name in MONITORS:
            assert screens and handed == screens and not main, f"{name}: {handed}/{screens} screens handed back"
    for name in MONITORS:
        assert name in seen, f"{name}: screens not checked"
    sizes = {name: seen[name][8:] for name in seen}
    assert abs(sizes["Pconsole_sev2bZ"][0] - 594) <= 1 and abs(sizes["Pconsole_sev2bZ"][1] - 377) <= 1
    assert sizes["Pconsole_sev2bZ"][2:] == (535, 423)
    for name in ("Pconsole1Z", "Pconsole2Z", "Pconsole3Z"):
        assert abs(sizes[name][2] - 378) <= 1 and abs(sizes[name][3] - 369) <= 1, name
    print(f"Monitor screens: {len(seen)} props checked against DAB's counts")


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
            *[str(ROOT / "port/src" / (p + ".c")) for p in ("gevr_bean_load", "gevr_bean_tex", "gevr_bean_logic",
                                                              "gevr_bean_folder")],
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
                                  str(matrices), str(file), kind, header_skeleton(name)],
                                 cwd=directory, capture_output=True, text=True)
            if run.returncode:
                failures.append(name)
                print(name, run.stderr[-1200:], flush=True)
            else:
                result = run.stdout.strip()
                if name == 'CcamguardZ':
                    assert 'COMBINED ' in result, 'guard whole-body proof was not exercised'
                results.append(result)
                print(result, flush=True)
        room_exe = directory / "rooms.exe"
        subprocess.run([
            compiler, "-std=c11", "-O2", "-fms-extensions", "-include", "stdarg.h", *sanitizers,
            "-DPLATFORM_64BIT=1", "-D_LANGUAGE_C", "-DGEVR", "-DVERSION_US",
            *["-I" + str(ROOT / p) for p in (".", "include", "src", "src/game", "port/include")],
            str(ROOT / "port/tests/gevr_bean_room_mapping_test.c"),
            str(ROOT / "port/src/gevr_romcheck.c"), str(ROOT / "port/src/gevr_rom_manifest.c"),
            *[str(ROOT / "port/src" / (p + ".c")) for p in ("gevr_bean_load", "gevr_bean_tex", "gevr_bean_logic")],
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
            world = (ROOT / "port/src/gevr_bean_world.c").read_text()
            bean_name, converted_scale = re.search(r'\{\s*' + stage + r',\s*"([^" ]+)",\s*([0-9.]+)f', world).groups()
            render_level_scale = float(re.search(r'\{' + stage + r',\s*"[^" ]+",\s*"[^" ]+",\s*([0-9.]+)',
                                                   (ROOT / "src/game/bg.c").read_text()).group(1))
            original = assets / "original/background" / bean_name / "default.bin"
            args = [str(room_exe), str(raw)]
            if original.exists():
                args += [str(original), converted_scale, str(1 / render_level_scale)]
            run = subprocess.run(args, capture_output=True, text=True)
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
    report.write_text(json.dumps({"catalogRows": len(mappings), "renderParityVerified": False, "results": results, "rooms": rooms, "nativeFallbackFiles": unmapped, "failures": failures}, indent=2))
    print(f"Native Bean registration checks: {len(results)}/{len(mappings)} passed; report: {report}")
    if failures:
        raise SystemExit("Failed mapping: " + ", ".join(failures))
    check_screens(results)


if __name__ == "__main__":
    main()
