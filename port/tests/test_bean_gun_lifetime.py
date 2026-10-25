"""Stress production Bean gun registration and retirement without game assets.

--verify-baseline demonstrates retained geometry growth on unchanged main.
"""
import argparse
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--verify-baseline", action="store_true")
    args = parser.parse_args()
    # Reuse the emitter suite's external engine stubs, not its assertions.
    stubs = (ROOT / "port/tests/gevr_bean_render_test.c").read_text().split(
        '#include "gevr_bean_pose_test.inc.h"')[0]
    stubs = stubs.replace('../src/', str(ROOT / 'port/src') + '/')
    stubs = stubs.replace(
        "m->ntris = 1; m->tris = calloc(1, sizeof(*m->tris));",
        "m->ntris = 4266; m->tris = calloc(m->ntris, sizeof(*m->tris));")
    stubs = stubs.replace("*out = m;", """
    for (int t = 1; t < m->ntris; t++) m->tris[t] = m->tris[0];
    for (int t = 0; t < m->ntris; t++) {
        m->tris[t].p[0] = (t % 100) * 0.1f;
        m->tris[t].flags = t % 3 == 0 ? GEVR_BEAN_F_ROUND : 0;
    }
    *out = m;""")
    stubs = stubs.replace("{ (void)mesh; }", """{
        if (mesh->gpu) { free(mesh->gpu); mesh->gpu = NULL; }
        releasedMeshes++;
    }""")
    fixture = (ROOT / "port/tests/bean_gun_lifetime_native.c").read_text()
    with tempfile.TemporaryDirectory(prefix="gevr-bean-guns-") as directory:
        temp = Path(directory)
        for name in ("char/boilerbond", "gun/rocketlauncher", "gun/ak47", "prop/chrblueprints"):
            marker = temp / "added-content/xbla/files/new" / name / "default.bin"
            marker.parent.mkdir(parents=True, exist_ok=True)
            marker.touch()
        for baseline in ([False, True] if args.verify_baseline else [False]):
            prefix = stubs
            if baseline:
                snapshot = temp / "baseline"
                snapshot.mkdir()
                paths = ["port/src/gevr_bean.c", *subprocess.check_output([
                    "git", "ls-tree", "-r", "--name-only", "bdeb44dc", "port/src"],
                    cwd=ROOT, text=True).splitlines()]
                for path in dict.fromkeys(paths):
                    if path.endswith("gevr_bean.c") or (
                            Path(path).name.startswith("gevr_bean") and path.endswith(".inc.h")):
                        data = subprocess.check_output(["git", "show", "bdeb44dc:" + path], cwd=ROOT)
                        (snapshot / Path(path).name).write_bytes(data)
                prefix = prefix.replace(str(ROOT / "port/src/gevr_bean.c"), str(snapshot / "gevr_bean.c"))
            source, exe = temp / "test.c", temp / "test"
            source.write_text(fixture.replace("/* PRODUCTION AND EXISTING ENGINE STUBS */", prefix))
            result = subprocess.run([
                shutil.which("gcc") or "cc", "-std=c11", "-O1", "-g",
                "-U_FORTIFY_SOURCE",  # the repo's include/limits.h trips glibc's fortified MB_LEN_MAX check
                "-include", "stdarg.h", "-include", "stddef.h", "-include", "limits.h",
                "-fms-extensions", "-fno-strict-aliasing", "-Wno-incompatible-pointer-types",
                "-ffunction-sections", "-fdata-sections", "-fsanitize=address,undefined",
                "-fno-sanitize-recover=all", "-fno-omit-frame-pointer", "-fno-pie", "-no-pie",
                "-DPLATFORM_64BIT=1", "-D_LANGUAGE_C", "-DGEVR", "-DVERSION_US",
                *["-I" + str(ROOT / p) for p in (".", "include", "src", "src/game", "port/include")],
                str(source), "-Wl,--gc-sections", "-lm", "-o", str(exe)], capture_output=True, text=True)
            if result.returncode:
                raise RuntimeError(result.stderr)
            for case in (["growth"] if baseline else ["growth", "owner", "storage", "unsupported", "wrap", "mixed"]):
                result = subprocess.run([str(exe), case], cwd=temp, capture_output=True, text=True, timeout=60)
                print(result.stdout, end="")
                if baseline:
                    assert result.returncode == 3 and "retained geometry grew" in result.stderr, result.stderr
                    assert "runtime error:" not in result.stderr and "AddressSanitizer" not in result.stderr, result.stderr
                    print("Baseline reproduced retained launcher geometry growth")
                elif result.returncode:
                    raise RuntimeError(case + ": " + result.stderr)


if __name__ == "__main__":
    main()
