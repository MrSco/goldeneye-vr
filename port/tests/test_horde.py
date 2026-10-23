"""Compile and run Horde's production director rules without a ROM or headset."""
from pathlib import Path
import os
import shutil
import subprocess
import tempfile
import test_multiplayer

ROOT = Path(__file__).resolve().parents[2]
with tempfile.TemporaryDirectory(prefix="gevr-horde-") as temp:
    exe = Path(temp) / "horde"
    subprocess.run([shutil.which("cc") or "gcc", "-std=c11", "-Wall", "-Wextra", "-Werror",
                    "-I" + str(ROOT / "port/src/net"), str(ROOT / "port/tests/horde_native.c"),
                    "-o", str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
    # Exercise the production launch/load handler without Steam Audio or a ROM.
    test_multiplayer.NATIVE = Path(temp)
    core = test_multiplayer.build_core()
    result = core.test_core_horde_admission()
    if os.name == "nt":
        import _ctypes
        _ctypes.FreeLibrary(core._handle)  # release before removing the temp DLL
    if result:
        raise AssertionError(f"Horde admission failed at net_core_native.c:{result}")
    print("Horde initial-party and late-admission checks passed")
    exe = Path(temp) / "horde-runtime"
    runtime_args = [shutil.which("gcc") or "gcc", "-std=c11", "-O2", "-fms-extensions"]
    runtime_args += ["-D" + v for v in ("GEVR=1", "PLATFORM_64BIT=1", "_LANGUAGE_C=1",
                                       "VERSION=2", "VERSION_US=1", "LANG_US=1")]
    runtime_args += ["-I" + str(ROOT / p) for p in (".", "port/include", "include", "src", "src/game", "port/src", "port/src/net")]
    result = subprocess.run(runtime_args + [str(ROOT / "port/tests/horde_runtime_native.c"),
                            str(ROOT / "src/game/gevr_horde.c"), str(ROOT / "src/game/gevr_arena.c"),
                            str(ROOT / "port/src/net/net_match.c"), "-lm", "-o", str(exe)], capture_output=True, text=True)
    if result.returncode:
        raise RuntimeError(result.stderr)
    subprocess.run([str(exe)], check=True)
    # Exercise the actual engine hooks without linking the ROM-dependent engine.
    def function(path, signature):
        source = (ROOT / path).read_text()
        start = source.index(signature)
        end = source.index("{", start) + 1
        depth = 1
        while depth:
            depth += (source[end] == "{") - (source[end] == "}")
            end += 1
        return source[start:end]
    hooks = "\n".join((
        function("src/game/bondview2.c", "static s32 gevrHordeGripWorld("),
        function("src/game/propobj.c", "void doorSetOpenState(DoorRecord *door, s32 newstate)"),
        function("src/game/chr.c", "void chrSetHiddenToRandom("),
        function("src/game/chraction.c", "void chrlvTriggerFireWeapon("),
    ))
    (Path(temp) / "horde_controls_production.h").write_text(hooks)
    exe = Path(temp) / "horde-controls"
    result = subprocess.run(runtime_args + ["-I" + temp, str(ROOT / "port/tests/horde_controls_native.c"),
                                           "-lm", "-o", str(exe)], capture_output=True, text=True)
    if result.returncode:
        raise RuntimeError(result.stderr)
    subprocess.run([str(exe)], check=True)
    exe = Path(temp) / "horde-network"
    args = [shutil.which("gcc") or "gcc", "-std=c11", "-O2", "-fms-extensions"]
    args += ["-D" + v for v in ("GEVR=1", "PLATFORM_64BIT=1", "_LANGUAGE_C=1",
                               "VERSION=2", "VERSION_US=1", "LANG_US=1")]
    args += ["-I" + str(ROOT / p) for p in (".", "port/include", "include", "src", "src/game", "port/src", "port/src/net")]
    args += [str(ROOT / p) for p in ("port/tests/horde_network_native.c", "port/src/net/net_horde.c",
                                    "port/src/net/netbuf.c", "port/src/net/net_match.c")]
    result = subprocess.run(args + ["-o", str(exe)], capture_output=True, text=True)
    if result.returncode:
        raise RuntimeError(result.stderr)
    subprocess.run([str(exe)], check=True)
