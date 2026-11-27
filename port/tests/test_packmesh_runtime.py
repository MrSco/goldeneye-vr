"""Exercise real Lua, model loading and display-list creation in a desktop build.

Set GEVR_PACKMESH_EXE to the flat executable and GEVR_GE_ROM to the USA ROM.
Uses an isolated profile, generated meshes and a deterministic Dam input file.
The desktop build needs a working window and graphics context.
"""
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import struct
import subprocess
import tempfile


def mesh(colour):
    rgba = bytes(colour)
    # A plain panel at the carrier root, with nonzero offsets on all three axes.
    corners = b"".join(struct.pack("<5f4BB3x", x, y, z, u, v, 255, 255, 255, 255, 0)
                       for x, y, z, u, v in ((-.07, .06, .01, .1, .1),
                                              (.08, .10, .01, .9, .1),
                                              (.06, .11, .08, .9, .9)))
    return b"GEVRWPN1" + struct.pack("<IHH", 1, 2, 2) + rgba * 4 + struct.pack("<I", 1) + corners


def moving_mesh():
    """Two pictures: a receiver panel on picture 0 and a bolt panel on picture 1."""
    def triangle(picture, z):
        return b"".join(struct.pack("<5f4BB3x", x, y, z + dz, u, v, 255, 255, 255, 255, picture)
                        for x, y, dz, u, v in ((-.03, .02, 0, .1, .1), (.03, .08, 0, .9, .1), (.02, .09, .03, .9, .9)))
    picture = struct.pack("<HH", 2, 2)
    return (b"GEVRWPN1" + struct.pack("<I", 2) + picture + bytes((90, 90, 90, 255)) * 4 + picture
            + bytes((200, 160, 40, 255)) * 4 + struct.pack("<I", 2) + triangle(0, .01) + triangle(1, .04))


def main():
    exe = os.environ.get("GEVR_PACKMESH_EXE")
    rom = os.environ.get("GEVR_GE_ROM")
    if not (exe and rom):
        print("SKIP: set GEVR_PACKMESH_EXE and GEVR_GE_ROM for the desktop pack mesh integration test")
        return
    exe = Path(exe).resolve(strict=True)
    rom = Path(rom).resolve(strict=True)
    with tempfile.TemporaryDirectory(prefix="gevr-packmesh-") as temp:
        root = Path(temp)
        # The game changes directory to the executable, even with cwd set.
        runner = root / exe.name
        shutil.copy2(exe, runner)
        for pattern in ("*.dll", "*.so*", "*.dylib", "gevr_*.rgba"):
            for dependency in exe.parent.glob(pattern):
                shutil.copy2(dependency, root / dependency.name)
        pack = root / "texture-packs/mods/mesh-test"
        (pack / "scripts").mkdir(parents=True)
        assets = pack / "assets/models"
        assets.mkdir(parents=True)
        (pack / "mod.json").write_text(json.dumps({"format": 1, "id": "mesh-test", "title": "Mesh Test",
                                                 "by": "Tests", "version": "1.0.0", "api": 1,
                                                 "kind": "campaign", "base": "ge", "coop": False}))
        (assets / "held.bin").write_bytes(mesh((243, 29, 73, 255)))
        (assets / "attachment.bin").write_bytes(mesh((37, 241, 113, 191)))
        (assets / "bad.bin").write_bytes(mesh((0, 0, 0, 255))[:-1])
        (assets / "moving.bin").write_bytes(moving_mesh())
        # Non-self-describing input must not reach the cartridge loader without a header.
        (assets / "cartridge.bin").write_bytes(bytes(32))
        (pack / "scripts/main.lua").write_text(r'''local time, phase = 0, 0
local function attach(file)
  return mod.attach.add({ model = file, on = "gun", hand = "right" })
end
mod.on("tick", function(dt)
  time = time + dt
  local item = mod.weapon.held("right")
  if not item or item < 4 then return end
  if time > 8 and phase == 0 then
    phase = 1
    print("mesh-test bad", mod.weapon.model("right", item, "bad.bin"), attach("bad.bin"))
    print("mesh-test cartridge", attach("cartridge.bin"))
    print("mesh-test load", mod.weapon.model("right", item, "held.bin"), attach("attachment.bin"))
  elseif time > 9 and phase == 1 then
    phase = 2
    print("mesh-test drawn", mod.weapon.look("right"))
    print("mesh-test stray", mod.weapon.model("right", item, "moving.bin", {action={pictures={2}, travel={0, 0, -40}}}))
    local bolt = attach("attachment.bin")
    print("mesh-test moving", mod.weapon.model("right", item, "moving.bin",
      {action={pictures={1}, travel={0, 0, -40}}, holdOpen=false}), mod.attach.action(bolt, {travel={0, 0, -40}}))
    print("mesh-test grip", mod.weapon.model("right", item, "moving.bin", {grip={0, -20, 40}}))
    print("mesh-test gloves", mod.weapon.hands("right", item, {ready="held.bin", trigger="attachment.bin"}),
      mod.weapon.hands("right", item, {ready="cartridge.bin"}))
  elseif time > 10 and phase == 2 then
    phase = 3
    print("mesh-test moving drawn", mod.weapon.look("right"))
  end
end)
''')
        (pack / "files.sha256").write_text("".join(
            f"{hashlib.sha256(file.read_bytes()).hexdigest()}  {file.relative_to(pack).as_posix()}\n"
            for file in sorted(pack.rglob("*")) if file.is_file()), encoding="ascii")
        (root / "goldeneye-vr.ini").write_text("[VR]\nPlayMode=1\nXblaVisualsMode=1\nCampaign=0\nCampaignMod=mesh-test\n")
        shutil.copyfile(rom, root / "ge.z64")
        (root / "input.txt").write_text("1000 key Return 200\n2000 click 200\n3500 key Return 200\n15500 quit\n")
        result = subprocess.run([str(runner), "--no-launcher", "--boot-stage", "33", "--input-script", "input.txt"],
                                cwd=root, timeout=60, capture_output=True, text=True, errors="replace")
        log_path = root / "gevr.log"
        log = log_path.read_text(errors="replace") if log_path.exists() else ""
        assert result.returncode == 0, (result.returncode, result.stdout[-3000:], result.stderr[-3000:], log[-5000:])
        for pattern in (r"mesh-test bad false nil", r"mesh-test cartridge nil", r"mesh-test load true [1-9]\d*",
                        r"mesh-test drawn mod", r"refused pack model bad\.bin: malformed model",
                        r"mesh-test stray false", r"mesh-test moving true true", r"mesh-test gloves true false", r"mesh-test grip true", r"mesh-test moving drawn mod",
                        r"pack model moving\.bin: 2 triangles, 2 RGBA textures \(GEVRWPN1\)",
                        r"pack model moving\.bin: moving pictures 0x2 with the action, 0x0 with the handle"):
            assert re.search(pattern, log), (pattern, "\n".join(line for line in log.splitlines() if re.search(r"\bmods?:|mesh-test|input script:", line)))
        assert log.count("refused pack model bad.bin: malformed model") == 2
        for name in ("held", "attachment"):
            assert re.search(rf"pack model {name}\.bin: 1 triangles, 1 RGBA textures \(GEVRWPN1\), [1-9]\d* display-list commands", log)
    print("PASS: desktop Lua held and attached pack meshes, malformed refusals, legacy header requirement and display lists;"
          " a body's moving pictures split into their own lists and a gun part with an action")


if __name__ == "__main__":
    main()
