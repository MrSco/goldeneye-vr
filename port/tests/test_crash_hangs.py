"""Regressions for the v0.4.14 watchdog hangs.

Reports 7b162d90 and 919e73ee (manual 3e30f876 is the same session as
919e73ee) hung leaving the co-op briefing. Report c3ad6b13 hung waiting
for a sequence player to stop during the Caverns intro.
"""
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]


def function(path, signature):
    text = (ROOT / path).read_text(encoding="utf-8")
    start = text.index(signature)
    brace = text.index("{", start)
    depth = 1
    end = brace + 1
    while depth:
        depth += (text[end] == "{") - (text[end] == "}")
        end += 1
    return text[start:end]


def build(directory, name, fixture):
    source = directory / (name + ".c")
    exe = directory / name
    source.write_text(fixture, encoding="utf-8")
    gcc = shutil.which("gcc") or "gcc"
    result = subprocess.run(
        [gcc, "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror", "-DGEVR=1", str(source), "-o", str(exe)],
        capture_output=True, text=True)
    if result.returncode:
        raise RuntimeError(result.stderr)
    subprocess.run([str(exe)], check=True)


def main():
    menu = (ROOT / "port/tests/coop_menu_follow_native.c").read_text(encoding="utf-8")
    menu = menu.replace("/* INSERT_TARGET */", function(
        "port/src/net/net_coop_menu.c", "static int coopMenuTarget(void)"))
    menu = menu.replace("/* INSERT_ACTION */", function(
        "port/src/net/net_coop_menu.c", "static int gevrCoopMenuMissionAction(int target)"))

    music = (ROOT / "port/tests/music_wait_native.c").read_text(encoding="utf-8")
    music = music.replace("/* INSERT_WAIT */", function(
        "src/music.c", "static void gevrWaitSeqStopped(ALCSPlayer *seqp)"))

    briefing = (ROOT / "port/tests/briefing_level_native.c").read_text(encoding="utf-8")
    briefing = briefing.replace("/* INSERT_LEVEL */", function(
        "src/game/front.c", "static s32 gevrBriefingLevel(void)"))

    with tempfile.TemporaryDirectory(prefix="gevr-crash-hangs-") as temp:
        directory = Path(temp)
        build(directory, "coop-menu", menu)
        build(directory, "music-wait", music)
        build(directory, "briefing-level", briefing)
    print("crash hang regressions passed")


if __name__ == "__main__":
    main()
