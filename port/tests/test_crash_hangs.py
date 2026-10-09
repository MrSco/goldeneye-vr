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
    flags = []
    if name == "music-wait":
        # The AL helpers share the players' common layout, as in the game build.
        flags = ["-fms-extensions", "-fno-strict-aliasing", "-fno-inline",
                 "-Wno-builtin-declaration-mismatch",
                 "-DPLATFORM_64BIT=1", "-D_LANGUAGE_C=1", "-DVERSION=2",
                 "-DVERSION_US=1", "-DLANG_US=1"]
        flags += ["-I" + str(ROOT / path) for path in
                  ("include", "include/PR", "port/include", "src/libultra/audio")]
    result = subprocess.run(
        [gcc, "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror", "-DGEVR=1"] +
        flags + [str(source), "-o", str(exe)],
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
    for file in ("sl.c", "copy.c", "event.c", "cspstop.c", "cspgetstate.c",
                 "cspsetseq.c", "cspplay.c"):
        music = music.replace("../../src/libultra/audio/" + file,
                              str(ROOT / "src/libultra/audio" / file))
    helpers = [function("src/libultra/audio/seqplayer.c", signature) for signature in (
        "ALVoiceState *__mapVoice(ALSeqPlayer *seqp, u8 key, u8 vel, u8 channel)",
        "void __unmapVoice(ALSeqPlayer *seqp, ALVoice *voice)",
        "void __seqpReleaseVoice(ALSeqPlayer *seqp, ALVoice *voice,",
        "char __voiceNeedsNoteKill (ALSeqPlayer *seqp, ALVoice *voice, ALMicroTime killTime)",
        "void __seqpStopOsc(ALSeqPlayer *seqp, ALVoiceState *vs)")]
    music = music.replace("/* INSERT_VOICE_HELPERS */", "\n\n".join(helpers))
    music = music.replace("/* INSERT_FORCE_STOP */", function(
        "src/libultra/audio/csplayer.c", "void gevrCSPForceStop(ALCSPlayer *seqp)"))
    music = music.replace("/* INSERT_VOICE_HANDLER */", function(
        "src/libultra/audio/csplayer.c", "static ALMicroTime __CSPVoiceHandler(void *node)\n"))
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
