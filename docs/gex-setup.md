# GoldenEye X setup

GoldenEye VR can use GoldenEye X's **KF7, both PP7s, DD44, Klobb, ZMG, D5K variants, Phantom and AR33 models and animations** and **VR arms
wearing GoldenEye's watch**, supplied by your own patched ROM. These options are
WIP; GE-X gun replacements are off by default. KF7 support is available in
**v0.4.7 or later** (PR #117); PP7 support is in the
[changes after v0.4.10](releases/unreleased.md).

You still play GoldenEye's missions with your normal GoldenEye 007 (USA) ROM.
This support does not run the GE-X campaign or import its maps, unsupported weapons,
music or sounds. No Perfect Dark ROM, patched ROM or GE-X assets are included
in the APK or repository. Supply a legally obtained dump of your own game;
do not upload either ROM to GitHub or a bug report.

## What you need

- Your normal **GoldenEye 007 USA ROM**, already working in GoldenEye VR.
- A clean, legally obtained **Perfect Dark USA v1.1 / Rev 1** ROM. The
  [Perfect Dark port's ROM reference](https://github.com/perfect-dark-pc-port/perfect_dark/blob/port/README.md)
  identifies this revision as `ntsc-final`, with MD5
  `e03b088b6ac9e0080440efed07c1e40f` in big-endian `.z64` order.
- **GoldenEye X patch 6a**, dated January 19, 2025, from
  [Wreck's official N64 Vault page](https://n64vault.com/pd-multi-levels:goldeneye-x).
  Download `GE-X_6a_01-19-25.zip` and extract
  `GE-X_6a_01-19-25.xdelta`. Download the patch, not a prepatched ROM.
- A computer for patching, plus USB or SideQuest for copying the result to Quest.

The loader uses **6a's file, texture and animation layout**. Older GE-X patches
such as 5e, other Perfect Dark revisions and additional ROM modifications are
not supported by this integration.

## 1. Prepare and check the clean Perfect Dark ROM

Make a working copy and preserve your original dump. Both the input and the
patched output must contain exactly **33,554,432 bytes (32 MiB)**. A ZIP/7z
archive must be extracted first.

The input must be **big-endian `.z64`**. N64 dumps also occur as byte-swapped
`.v64` or little-endian `.n64`; renaming an extension does not convert their
bytes. The GoldenEye ROM picker handles byte order for the main game, but the
GE-X picker requires an already prepared `.z64` file.

If your dump is already `.z64`, copy it to `pd.z64` and check the hash. On Windows:

```powershell
Get-FileHash -LiteralPath .\pd.z64 -Algorithm MD5
(Get-Item -LiteralPath .\pd.z64).Length
```

Or on any computer with Python 3:

```text
python -c "from pathlib import Path; import hashlib; b=Path('pd.z64').read_bytes(); print(len(b), hashlib.md5(b).hexdigest())"
```

Expected: `33554432 e03b088b6ac9e0080440efed07c1e40f`. Check the **unpatched**
file. If it differs, stop and check the revision, byte order or dump. Perfect
Dark USA v1.0, PAL, Japanese, XBLA and an already patched ROM are not this input.

For `.v64`/`.n64` conversion, save the following as `normalize_pd.py` in your
working folder and run `python normalize_pd.py "Your Perfect Dark dump.n64"`
(substitute your actual filename). On systems using `python3`, use that name
for the Python commands throughout this guide. The script detects byte order
from the header, checks the normalized hash, and writes a new `pd.z64` only
when it matches; it refuses to overwrite an existing `pd.z64`.

```python
from pathlib import Path
import hashlib
import sys

rom = bytearray(Path(sys.argv[1]).read_bytes())
if len(rom) != 33554432:
    raise SystemExit("Expected a 32 MiB ROM; extract archives first.")
header = bytes(rom[:4])
if header == b"\x37\x80\x40\x12":          # .v64: swap each byte pair
    rom[0::2], rom[1::2] = rom[1::2], rom[0::2]
elif header == b"\x40\x12\x37\x80":        # .n64: reverse each four-byte word
    rom[0::4], rom[1::4], rom[2::4], rom[3::4] = (
        rom[3::4], rom[2::4], rom[1::4], rom[0::4]
    )
elif header != b"\x80\x37\x12\x40":
    raise SystemExit("Unrecognized N64 ROM header.")
digest = hashlib.md5(rom).hexdigest()
if digest != "e03b088b6ac9e0080440efed07c1e40f":
    raise SystemExit("Not a clean Perfect Dark USA v1.1 dump: " + digest)
with Path("pd.z64").open("xb") as output:
    output.write(rom)
print("Created pd.z64:", len(rom), "bytes; MD5", digest)
```

## 2. Apply GE-X 6a to Perfect Dark

Use **one** of these methods. Keep `pd.z64` unchanged and write a separate
`gex.z64`. The patch's source is Perfect Dark, not your GoldenEye ROM.

### With a desktop patcher

Use a local xdelta patcher, such as the
[GoldenEye Setup Editor](https://github.com/carnivoroussociety/GoldEditor),
and choose its xdelta patch application tool. Set:

| Field | File |
|---|---|
| Source / original ROM | Your checked, clean `pd.z64` |
| Patch | `GE-X_6a_01-19-25.xdelta` from the extracted 6a ZIP |
| Output / patched ROM | A new `gex.z64` |

If the editor requires a `.rom` output extension, save a big-endian output
and rename that result to `gex.z64`. This rename is appropriate because the
bytes are already `.z64` order. Check the result in step 3.

The [xdelta3 command-line tool](https://github.com/jmacd/xdelta/releases) is
another option. With it installed and available on your PATH, run in the
folder containing the source and patch:

```text
xdelta3 -d -s pd.z64 GE-X_6a_01-19-25.xdelta gex.z64
```

### With the repository's Python patcher

Python 3 and the source checkout are enough; no extra Python packages or game
build are needed. From the repository root, with the checked `pd.z64` and
extracted patch there temporarily, run:

```text
python tools/gex/vcdiff_apply.py pd.z64 GE-X_6a_01-19-25.xdelta gex.z64
```

You can also use absolute paths for all three input/output filenames, quoting
paths with spaces. This decoder supports the VCDIFF form used by this specific
6a patch, including its Adler-32 window checks; it is not a general xdelta3
replacement. Expected output: `4 windows, 33554432 bytes written`.

Keep these local files out of commits. Never select the same file for source
and output. If a patcher reports a source or checksum error, recheck step 1
and the patch filename instead of forcing the patch to apply.

## 3. Verify the patched result

For the unmodified 6a patch above, this integration expects:

| Check | Expected `gex.z64` |
|---|---|
| Size | 33,554,432 bytes (32 MiB) |
| MD5 | `640923b68b9281044ac1b518612e979b` |
| First four bytes | `80 37 12 40` (big-endian N64) |
| ROM title at offset `0x20` | `GoldenEye X` |

Check the output using the same hash command as step 1, now with `gex.z64`:

```text
python -c "from pathlib import Path; import hashlib; b=Path('gex.z64').read_bytes(); print(len(b), hashlib.md5(b).hexdigest()); print(b[:4].hex(' '), b[0x20:0x34].decode('ascii', errors='replace'))"
```

These checksums identify the clean input and expected patch result; the app
itself checks the output's size/header at import and the 6a data layout at use.
The ROM title alone does not guarantee the right revision.

## 4. Import and enable it on Quest

1. Start GoldenEye VR once so it creates its own data folder. Keep your
   normal GoldenEye ROM selected on **Play**.
2. Copy `gex.z64` to the Quest's **Download** folder with Windows Explorer,
   USB file transfer or SideQuest's file manager.
3. In the launcher, open **Play → Mods...**. Under **GOLDENEYE X**, press
   **Choose ROM...** (or **Change ROM...** if you already imported one).
4. Select `gex.z64` from **Download**. The success message is
   **GoldenEye X ROM chosen.** The app stores the file as
   `Android/data/com.gevr.port/files/data/gex.z64` and keeps the main game's
   ROM separately. Do not use the Play page's main **Choose ROM file...**.
5. Enable **Its guns (WIP)**, **Its arms, wearing the watch (VR, WIP)**,
   or both. Start a mission and equip a supported gun to check the replacement.

For manual transfer, copy the verified `gex.z64` to the app-created data
folder above. With ADB, the destination is:

```text
adb push gex.z64 /sdcard/Android/data/com.gevr.port/files/data/gex.z64
```

Fully close and reopen the app after replacing a ROM that was already loaded,
or after a missing/invalid-ROM load attempt. The runtime caches the ROM and
its first load attempt for the process. **GoldenEye X ROM chosen.** confirms
import, while `gex: GoldenEye X ROM loaded, 2013 files` in a game log confirms
runtime loading. An enabled GE-X option can fall back to GoldenEye's assets
if loading fails.

HD textures remain a separate Mods option. GE-X weapon textures that match
GoldenEye's texture IDs can use the existing HD packs; importing a GE-X ROM
does not install the emulator texture files bundled in its patch ZIP.

## Reload and fit controls

Controls below assume the default right-handed layout. Left-handed mode
swaps hands and face buttons as described in the [README controls](../README.md#-controls).

**Virtual screen:** supported guns use GE-X's fire and reload animations. Normal
button reloads still work here.

**Stereo VR:** enable **Hand reload (WIP)** under **Controls → Gestures...**,
or **watch → Game Options → VR settings → Weapons → Reload WIP**. With it
on, guns no longer auto-reload. For a supported GE-X magazine-fed gun:

1. With one gun, put the off hand at its magazine, squeeze grip and pull down.
   The magazine retains its remaining rounds while held; you can put it back.
2. Release it to drop it, or press **B/Y** to eject the gun's magazine.
   Dropped magazines return their remaining rounds to reserve.
3. Grip at your belt to take a fresh magazine, then push it into the magazine
   well while holding grip. New magazines fill from your available reserve.
4. Release grip after seating it before another grab or two-handed hold.

You can also bring an empty GE-X gun to the belt to reload it directly,
including either gun when dual-wielding. Left GE-X guns are mirrored. For KF7,
a grip near the magazine or the supporting-hand point takes the nearer one.
For **PP7**, ordinary cupping favors support; release grip and take a fresh grab
distinctly below the pistol to pull its magazine, or use button eject. A support
hold never turns into a magazine pull while grip stays pressed. Both paths give
haptic feedback when the off hand takes hold.

Both PP7 variants share their own gun, support and magazine-grab calibration,
independent of KF7. Their muzzle calibration remains separate. PP7 fit and
controller clearance still require headset validation.

**Gun fit:** enable **Controls → Gun fit...**, or toggle it during a solo
mission with **Menu + A**. **A** saves; **B** returns to the last saved fit.
**X** cycles only the available modes:

| Mode | Controls |
|---|---|
| Gun | Sticks move the gun in your hand. GE-X saves its own fit separately. With both hands holding a GE-X gun and GE-X arms enabled, sticks adjust its supporting-hand position and grip point. Hold the gun-hand grip to rotate: move-stick forward/back sets pitch, sideways sets roll; turn-stick up/down sets yaw. |
| Scope | Sticks move the lens; turn-stick sideways changes its width. Available for a scoped weapon, with separate GE-X values. |
| Reload | Available with hand reload on and one magazine-fed gun. Place the off hand at the desired grab point and press its trigger (left trigger by default). Place it at your belt and press Y. Sticks are idle in this mode. |
| Off hand | Available with a GE-X gun. Sticks adjust the off-hand palm/magazine pose. Hold the gun-hand grip (right grip by default) to adjust the watch's position and size instead; turn-stick sideways changes size. |
| Held magazine | Available with a supported GE-X gun. A preview magazine appears in the off hand, including with hand reload disabled. Move-stick forward/back and sideways, turn-stick up/down, move only the magazine within the fingers. Each family saves its own fit; PP7 variants share theirs, as do D5K variants. |
| Magazine well | Available with a supported GE-X gun. Sticks move the insertion target on the gun. Place the preview magazine's tip at the desired entrance and press the off-hand trigger to set the target there. HUD shows tip-to-well distance; this fit leaves the meshes in place. |
| Installed magazine | Moves the visible magazine in the gun, even when it is out. Move stick: forward/sideways; turn stick: up/down. Saved reload grab and insertion targets stay put. This is separate from Held magazine. |
| Barrel tip | Sticks adjust the muzzle point. |

The new guns save seven vectors under `GexFit<item>_<component>` keys, with D5K
variants sharing item 10. Compiled defaults include the user's latest PP7/KF7
fits; saved settings take precedence. See the [weapon roadmap](gex-weapon-roadmap.md#fit-persistence-and-baking-workflow)
for the item/component mapping and how to bake later headset fits. The seven
new guns await headset fitting and acceptance.

GE-X arms retain GoldenEye's live wrist status and pause watch. The watch
stays at least its original visible size so its face remains readable.
Toggles and fits are saved in `data/goldeneye-vr.ini` (`GexGuns`, `GexArms`,
`GexReloadGrab`, `ReloadBelt`, `GexHeldMag`, `GexWatch`, `GexForeHold`,
`GexPP7GunOff`, `GexPP7Grab`, `GexPP7Support`, `GexPP7SupportRot`, `GexPP7MagOff`, `GexKF7MagOff`,
`GexPP7WellOff`, `GexKF7WellOff` and
the GE-X gun/grip/scope fits). They can be changed from the launcher or
the watch's VR settings where offered.

PP7's two-hand support pose stays fixed to the pistol, like the original GE
pistol grip. Turning the off controller does not pivot the supporting hand.
Support rotation pivots around that palm and is saved in degrees as pitch,
yaw and roll, independently of the hand's position and magazine fit.
PP7 insertion uses the magazine-well entrance at the handle's bottom rather
than the installed magazine's top near the slide. Well fitting refines that
target independently of the grab point and held-magazine position.

## Troubleshooting

| Symptom | Check |
|---|---|
| No GOLDENEYE X group in Mods | Your build predates PR #117. Install a build containing the source changes. |
| Patcher reports a source/checksum mismatch | Check the unpatched, normalized Perfect Dark USA v1.1 hash. Apply 6a once to that clean file. |
| "That is not a GoldenEye X .z64 ROM..." | Select the extracted, patched 32 MiB output. The picker does not accept the ZIP, xdelta patch, clean Perfect Dark ROM or a byte-swapped output. |
| `gex: no GoldenEye X ROM (data/gex.z64)` | Import from the Mods picker or check the exact manual destination, then restart. |
| `gex: not GE-X 6a's layout (file or texture table)` | Recreate the output from the checked source and official 6a patch, verify its MD5, import and restart. |
| Original supported gun still appears | Enable **Its guns (WIP)**, equip a supported gun and check the runtime log. Other weapons retain their existing models. Restart after a failed load or a ROM replacement. |
| Magazine or supporting-hand grip is hard to reach | Use Gun fit's reload mode to set the grab/belt points, or its two-handed gun fit to adjust the supporting hand. Release grip after seating a magazine. |

For implementation and model-format notes, see [GE-X weapons](gex-weapons.md).
For remaining headset checks and integration fixes, see
[v0.4.7 changes](releases/v0.4.7.md).
