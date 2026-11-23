"""Test 64-bit monitor microcode negative scrolls and long-run UV wrapping (#309)."""
from pathlib import Path
import shutil
import subprocess
import tempfile

from test_reinforcements import block

ROOT = Path(__file__).resolve().parents[2]


def build(directory, propobj_text):
    fixture = (ROOT / "port/tests/monitor_scroll_native.c").read_text(encoding="utf-8")
    anims = "\n".join(
        block(propobj_text, sig) + ";"
        for sig in (
            "uintptr_t monAnim03ThreeWavePattern[] =",
            "uintptr_t monAnim05GreenTextUp[] =",
            "uintptr_t monAnim08RedBarGraph[] =",
            "uintptr_t monAnim0AGreenBarGraph[] =",
            "uintptr_t monAnim10GlobalMap[] =",
        )
    )
    helpers = "\n".join((
        block(propobj_text, "void save_ptr_monitor_ani_code_to_obj_ani_slot(MonitorRecord *mon, void *image)\n{"),
        block(propobj_text, "void save_img_index_to_obj_ani_slot(MonitorRecord *mon, void *unk88)\n{"),
    ))
    parts = {
        "TVCMD": block(propobj_text, "struct tvcmd {") + ";",
        "ANIMS": anims,
        "HELPERS": helpers,
        "MICROCODE": block(propobj_text, "Gfx *process_monitor_animation_microcode("),
    }
    for marker, code in parts.items():
        fixture = fixture.replace("/* INSERT_" + marker + " */", code)
    source = directory / "monitor_scroll.c"
    exe = directory / "monitor_scroll.exe"
    source.write_text(fixture, encoding="utf-8")
    args = [shutil.which("gcc") or "gcc", "-std=c11", "-O2", "-fms-extensions",
            "-Wno-builtin-declaration-mismatch", "-Wno-int-conversion",
            "-Wno-incompatible-pointer-types"]
    args += ["-D" + v for v in ("GEVR=1", "PLATFORM_64BIT=1", "_LANGUAGE_C=1",
                                "VERSION=2", "VERSION_US=1", "LANG_US=1")]
    args += ["-I" + str(ROOT / path) for path in
             (".", "port/include", "include", "src", "src/game", "port/src")]
    result = subprocess.run(args + [str(source), "-o", str(exe), "-lm"],
                            capture_output=True, text=True)
    if result.returncode:
        raise RuntimeError(result.stderr)
    return exe


if __name__ == "__main__":
    propobj_text = (ROOT / "src/game/propobj.c").read_text(encoding="utf-8")
    with tempfile.TemporaryDirectory(prefix="gevr-monitor-scroll-") as temp:
        exe = build(Path(temp), propobj_text)
        subprocess.run([str(exe)], check=True)
