"""Desktop smoke for real pack optics. Uses the same isolated run as test_packmesh_runtime."""
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import struct
import subprocess
import tempfile

from test_packmesh_runtime import mesh


def optic_profile():
    """A large magenta reticle cross with transparent corners and two modes."""
    width = height = 16
    mark = bytes(
        component
        for y in range(height)
        for x in range(width)
        for component in (255, 0, 255, 255 if x in (7, 8) or y in (7, 8) else 0)
    )
    # Right, up and forward in the mesh frame. The lens is 6 mm by 6 mm.
    lens = (0, .085, .105, 1, 0, 0, 0, 0, 1, 0, 1, 0, .003, .003, .5, .5, 1)
    outline = (-1, -1, 1, -1, 1, 1, -1, 1)
    return b"GEVROPT4" + struct.pack("<I", 2) + b"".join(
        struct.pack("<4I19f8f", flags, 4, width, height, *lens, low, high, *outline) + mark
        for flags, low, high in ((2, 2, 4), (1 | 256, 1, 1))
    )


def reticle_pixels(path):
    """Count the fixture's unique magenta in the middle third of the frame."""
    data = path.read_bytes()
    offset, = struct.unpack_from("<I", data, 10)
    w, h, _, bpp, compression = struct.unpack_from("<iiHHI", data, 18)
    assert bpp in (24, 32) and compression == 0
    stride = ((w * bpp + 31) // 32) * 4
    count = 0
    for y in range(abs(h) // 3, 2 * abs(h) // 3):
        row = offset + y * stride
        for x in range(w // 3, 2 * w // 3):
            at = row + x * (bpp // 8)
            b, g, r = data[at:at + 3]
            count += r > 160 and g < 100 and b > 160
    return count


def main():
    exe = os.environ.get("GEVR_PACKMESH_EXE")
    rom = os.environ.get("GEVR_GE_ROM")
    if not (exe and rom):
        print("SKIP: set GEVR_PACKMESH_EXE and GEVR_GE_ROM for the desktop pack optic integration test")
        return
    exe = Path(exe).resolve(strict=True)
    rom = Path(rom).resolve(strict=True)

    with tempfile.TemporaryDirectory(prefix="gevr-packoptic-") as temp:
        root = Path(temp)
        runner = root / exe.name
        shutil.copy2(exe, runner)
        for pattern in ("*.dll", "*.so*", "*.dylib", "gevr_*.rgba"):
            for dependency in exe.parent.glob(pattern):
                shutil.copy2(dependency, root / dependency.name)
        pack = root / "texture-packs/mods/optic-test"
        (pack / "scripts").mkdir(parents=True)
        assets = pack / "assets/models"
        assets.mkdir(parents=True)
        (pack / "mod.json").write_text(json.dumps({
            "format": 1, "id": "optic-test", "title": "Optic Test", "by": "Tests",
            "version": "1.0.0", "api": 1, "kind": "campaign", "base": "ge", "coop": False,
        }), encoding="ascii")
        (assets / "body.bin").write_bytes(mesh((37, 241, 113, 255)))
        (assets / "folded.bin").write_bytes(mesh((243, 29, 73, 255)))
        profile = optic_profile()
        (assets / "fixture.opt").write_bytes(profile)
        (assets / "bad.opt").write_bytes(profile[:-1])
        (pack / "scripts/main.lua").write_text(r'''local time, phase, id, original = 0, 0, nil, nil
mod.on("tick", function(dt)
  time = time + dt
  local item = mod.weapon.held("right")
  if not item or item < 4 then return end
  if time > 8 and phase == 0 then
    phase = 1
    original = item
    id = assert(mod.attach.add{model="body.bin", on="gun", hand="right"})
    assert(mod.attach.optic(id, {profile="fixture.opt", folded="folded.bin"}))
    assert(not mod.attach.optic(id, {profile="bad.opt"}))
    assert(not pcall(mod.attach.optic, id, {profile="../fixture.opt"}))
    assert(not pcall(mod.attach.optic, id, {profile="fixture.opt", folded="bad/mesh.bin"}))
    print("optic-test scope", mod.attach.opticMode(id, 1, 3))
  elseif time > 11 and phase == 1 then
    phase = 2
    print("optic-test folded", mod.attach.opticMode(id, 2))
  elseif time > 13 and phase == 2 then
    phase = 3
    assert(mod.inventory.give(mod.items.sniperrifle))
  elseif time > 14 and phase == 3 and item ~= original then
    phase = 4
    print("optic-test swapped", mod.attach.opticMode(id))
    assert(mod.attach.remove(id))
    print("optic-test removed", mod.attach.opticMode(id))
  end
end)
''', encoding="ascii")
        (pack / "files.sha256").write_text("".join(
            f"{hashlib.sha256(file.read_bytes()).hexdigest()}  {file.relative_to(pack).as_posix()}\n"
            for file in sorted(pack.rglob("*")) if file.is_file()), encoding="ascii")
        (root / "goldeneye-vr.ini").write_text(
            "[VR]\nPlayMode=1\nXblaVisualsMode=1\nCampaign=0\nCampaignMod=optic-test\n", encoding="ascii")
        shutil.copyfile(rom, root / "ge.z64")
        (root / "input.txt").write_text(
            "1000 key Return 200\n2000 click 200\n3500 key Return 200\n"
            "7400 shot before\n10000 shot live\n12500 shot folded\n13900 key Q 100\n"
            "15900 shot removed\n16800 quit\n", encoding="ascii")
        result = subprocess.run(
            [str(runner), "--no-launcher", "--boot-stage", "33", "--input-script", "input.txt", "--mod-optic-preview"],
            cwd=root, timeout=70, capture_output=True, text=True, errors="replace")
        log_path = root / "gevr.log"
        log = log_path.read_text(errors="replace") if log_path.exists() else ""
        if evidence := os.environ.get("GEVR_OPTIC_EVIDENCE"):
            output = Path(evidence)
            output.mkdir(parents=True, exist_ok=True)
            (output / "gevr.log").write_text(log, encoding="utf-8")
            for image in (root / "shots").glob("*"):
                shutil.copy2(image, output / image.name)

        assert result.returncode == 0, (result.returncode, result.stderr[-3000:], log[-5000:])
        for pattern in (
            r"optic fixture\.opt: 2 modes", r"optic bad\.opt refused", r"optic-test scope 1 3(?:\.0)? 2 scope false",
            r"optic-test folded 2 1(?:\.0)? 2 collimator true", r"optic-test swapped nil", r"optic-test removed nil",
            r"scope: [1-9]\d* of the world's draws through the lens .*?(?:right|left) [1-9]\d*",
        ):
            assert re.search(pattern, log), (pattern, "\n".join(
                line for line in log.splitlines() if re.search(r"mods?:|optic-test|scope:|input script:", line)))
        assert "optic lens shader:" not in log and "optic lens program:" not in log
        pixels = [reticle_pixels(root / "shots" / f"{name}.bmp")
                  for name in ("before", "live", "folded", "removed")]
        assert pixels[0] == pixels[3] == 0 and min(pixels[1:3]) > 20, pixels
    print("PASS: desktop Lua pack optics, malformed rejection, world pass, visible reticle in both modes, swap and detach")


if __name__ == "__main__":
    main()
