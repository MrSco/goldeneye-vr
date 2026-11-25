"""Cross-platform sync (docs/cloud-sync.md): the rules with simulated devices, then
the real file glue (port/pc/gevr_sync.cpp) launching several save folders against
one folder store, each launch its own process, as on the devices."""
from pathlib import Path
import os
import shutil
import subprocess
import tempfile
import time

ROOT = Path(__file__).resolve().parents[2]


def build(temp):
    exe = Path(temp) / "sync_test.exe"
    args = [shutil.which("g++") or "g++", "-std=c++20", "-O1", "-Wall", "-Wno-unused-function",
            "-DGEVR_PC=1", "-DGEVR_SYNC_TEST=1", "-Iport/pc", "-Iport/include",
            "port/tests/sync_native.cpp", "port/pc/gevr_sync_core.cpp", "port/pc/gevr_sync.cpp", "-o", str(exe)]
    subprocess.run(args, cwd=ROOT, check=True)
    return exe


def run(exe, action, folder, kind, store):
    env = dict(os.environ)
    env.pop("GEVR_SYNC_DIR", None)
    if store is not None:
        env["GEVR_SYNC_DIR"] = str(store)
    out = subprocess.run([str(exe), action, str(folder), kind], env=env, check=True,
                         capture_output=True, text=True).stdout
    return dict(line.split("=", 1) for line in out.splitlines() if "=" in line)


def ini(folder):
    p = Path(folder) / "goldeneye-vr.ini"
    return dict(l.split("=", 1) for l in p.read_text().splitlines() if "=" in l) if p.exists() else {}


def blank():
    return bytes(512)


def progress(level):
    b = bytearray(512)
    b[32 + 20] = level
    return bytes(b)


def check(ok, what):
    if not ok:
        raise AssertionError(what)


with tempfile.TemporaryDirectory(prefix="gevr-sync-") as temp:
    exe = build(temp)
    subprocess.run([str(exe), "core"], check=True)

    t = Path(temp)
    store, a, b = t / "store", t / "quest", t / "tv"
    for d in (store, a, b):
        d.mkdir()

    # nothing linked: nothing happens
    (a / "goldeneye-vr.ini").write_text("[VR]\nLookSpeed=5\nGuest2Name=Den\nFrameRate=90\n")
    (a / "eeprom.bin").write_bytes(progress(1))
    old = time.time() - 3600
    os.utime(a / "eeprom.bin", (old, old))
    r = run(exe, "launch", a, "quest", None)
    check(r["active"] == "0" and not list(store.iterdir()), "off until a store is linked")
    # a store that can't be reached: nothing changes either
    r = run(exe, "launch", a, "quest", t / "missing")
    check(r["active"] == "0" and ini(a)["LookSpeed"] == "5", "an unreachable store changes nothing")

    # the first device
    r = run(exe, "launch", a, "quest", store)
    records = [p.name for p in store.iterdir()]
    check(r["active"] == "1" and len(records) == 1 and records[0].startswith("device-"), "the first device's record")
    text = (store / records[0]).read_text()
    check("LookSpeed" in text and "FrameRate@quest" in text and "Guest2Name" not in text,
          "shared and kind keys go up, the guest seats stay")

    # a second device joins: the first's settings and save win, its own seats stay
    (b / "goldeneye-vr.ini").write_text("[VR]\nLookSpeed=3\nGuest2Name=Bedroom\n")
    (b / "eeprom.bin").write_bytes(blank())
    r = run(exe, "launch", b, "tv", store)
    check(ini(b)["LookSpeed"] == "5" and ini(b)["Guest2Name"] == "Bedroom" and "FrameRate" not in ini(b),
          "joining takes the others' shared settings, not another kind's, keeps its seats")
    check((b / "eeprom.bin").read_bytes() == progress(1), "progress beats a new device's blank save")
    check(abs((b / "eeprom.bin").stat().st_mtime - old) < 2, "the save keeps the time it was written")
    check(len(list(b.glob("eeprom.bin.before-sync-*"))) == 1, "the blank save is kept aside")
    check("message" not in r and r["status"].startswith("Sync: 1 setting"), "status, no conflict note")

    # a change on the TV while playing goes up; the Quest's next launch takes it
    (b / "goldeneye-vr.ini").write_text(ini_text := "[VR]\nLookSpeed=7\nGuest2Name=Bedroom\n")
    run(exe, "push", b, "tv", store)
    run(exe, "launch", a, "quest", store)
    check(ini(a)["LookSpeed"] == "7" and ini(a)["Guest2Name"] == "Den", "a pushed change arrives")

    # both play between syncs: the newer save is kept, the other device is told
    run(exe, "launch", b, "tv", store)
    (a / "eeprom.bin").write_bytes(progress(3))
    run(exe, "push", a, "quest", store)
    time.sleep(1.2)
    (b / "eeprom.bin").write_bytes(progress(4))
    run(exe, "push", b, "tv", store)
    r = run(exe, "launch", a, "quest", store)
    check((a / "eeprom.bin").read_bytes() == progress(4), "the newer save wins")
    check("message" in r and "Apple TV" in r["message"], "the conflict is told, naming the other device")
    check(len(list(a.glob("eeprom.bin.before-sync-*"))) == 1, "this device's save is kept aside")
    r = run(exe, "launch", b, "tv", store)
    check("message" not in r and (b / "eeprom.bin").read_bytes() == progress(4), "the winner hears nothing")

print("sync: ok")
