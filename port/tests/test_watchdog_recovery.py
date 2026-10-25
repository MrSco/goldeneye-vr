"""Run the production watchdog with deterministic progress during its callbacks.

--verify-baseline demonstrates the stale abort on the unchanged main branch.
No real signals, threads, sleeps, ROM or headset are used.
"""
import argparse
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
PATH = "port/src/gevr_engine_shim.c"


def function(source, signature):
    start = source.index(signature)
    opening = source.index("{", start)
    depth, end = 1, opening + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--verify-baseline", action="store_true")
    args = parser.parse_args()
    fixture = (ROOT / "port/tests/watchdog_recovery_native.c").read_text()
    with tempfile.TemporaryDirectory(prefix="gevr-watchdog-") as directory:
        temp = Path(directory)
        for baseline in ([False, True] if args.verify_baseline else [False]):
            source = (subprocess.check_output(
                ["git", "show", "bdeb44dc:" + PATH], cwd=ROOT, text=True)
                if baseline else (ROOT / PATH).read_text())
            production = function(source, "static void *gevrWatchdog(void *arg)")
            if "static int gevrWatchdogCanAbort(" in source:
                production = function(source, "static int gevrWatchdogCanAbort(") + "\n" + production
            file, exe = temp / "watchdog.c", temp / "watchdog"
            file.write_text(fixture.replace("/* PRODUCTION */", production))
            subprocess.run([
                shutil.which("gcc") or "cc", "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror",
                "-fsanitize=address,undefined", "-fno-omit-frame-pointer", "-fno-pie", "-no-pie",
                str(file), "-o", str(exe)], check=True)
            cases = ["hang", "recovery-log", "recovery-focus", "pause-log",
                     "unfocus-log", "background", "unfocused", "frame-wait",
                     "dump-pending", "marker", "second-hang"]
            for case in cases:
                result = subprocess.run([str(exe), case], capture_output=True, text=True, timeout=5)
                stale = baseline and case in ("recovery-log", "recovery-focus", "pause-log", "unfocus-log")
                if stale:
                    assert result.returncode != 0 and "stale abort" in result.stderr, result.stderr
                    print("Baseline reproduced stale abort:", case)
                else:
                    assert result.returncode == 0, case + ": " + result.stderr
                    assert "runtime error:" not in result.stderr, result.stderr
                    print(("Baseline" if baseline else "Fixed") + " passed:", case)


if __name__ == "__main__":
    main()
