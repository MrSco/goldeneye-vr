"""Headless expanded-map composition and network identity regression checks."""
from pathlib import Path
import os
import shutil
import subprocess
import tempfile
import test_multiplayer

ROOT = Path(__file__).resolve().parents[2]
with tempfile.TemporaryDirectory(prefix="gevr-expanded-") as temp:
    exe = Path(temp) / "expanded"
    args = [shutil.which("gcc") or "gcc", "-std=c11", "-O1", "-g", "-fms-extensions"]
    args += ["-D" + v for v in ("GEVR=1", "PLATFORM_64BIT=1", "_LANGUAGE_C=1", "VERSION=2", "VERSION_US=1", "LANG_US=1")]
    args += ["-I" + str(ROOT / v) for v in (".", "port/include", "include", "src", "src/game", "port/src", "port/src/net")]
    # Optional sanitizer run: GEVR_TEST_SANITIZE=1 python port/tests/test_expanded_maps.py
    if os.environ.get("GEVR_TEST_SANITIZE"):
        args += ["-fsanitize=address,undefined", "-fno-sanitize-recover=all", "-fno-omit-frame-pointer"]
    args += [str(ROOT / v) for v in ("port/tests/expanded_maps_native.c", "src/game/gevr_expanded.c", "port/src/gevr_setup.c")]
    result = subprocess.run(args + ["-lm", "-o", str(exe)], capture_output=True, text=True)
    if result.returncode:
        raise RuntimeError(result.stderr)
    subprocess.run([str(exe)], check=True)
    # Optional public decompilation records exercise all seven maps through
    # the production ROM converter and setup composer; no ROM is required.
    reference = os.environ.get("GEVR_DECOMP_ROOT")
    if reference:
        from expanded_map_records import build_records
        campaigns, templates = build_records(reference, temp)
        for campaign in campaigns:
            print(campaign.stem, flush=True)
            subprocess.run([str(exe), str(campaign), str(templates)], check=True)
    test_multiplayer.NATIVE = Path(temp)
    core = test_multiplayer.build_core()
    result = core.test_core_expanded_maps()
    if os.name == "nt":
        import _ctypes
        _ctypes.FreeLibrary(core._handle)
    if result:
        raise AssertionError(f"Expanded map network check failed at net_core_native.c:{result}")
    print("PASS: map selection, config roundtrip, PvE rejection, engine mapping, rotation and rejoin layout identity")
