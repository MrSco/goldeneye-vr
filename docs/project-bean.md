# Project Bean visuals (experimental)

GoldenEye VR can load characters, heads, weapons, props, rooms and skies from
the player's own extracted Project Bean files. No game files ship in the APK.
The GoldenEye USA ROM remains required; gameplay, collision and animation
remain the original game's.

Open **Mods** in the launcher, download **GoldenEye 007 XBLA / Project Bean**,
then select it under **Visuals** alongside Original, HD and HD + AI. The VR
pack contains 499 CAFF files and 122 Bean PNG textures (173 MiB download).
It installs in `Android/data/com.gevr.port/files/texture-packs/ge007-bean`.
The standalone PNG archive in the same release remains available for emulators.

Bean uses only its own CAFF textures and PNG replacements. Missing replacements
use the original game textures; HD / AI and legacy texture packs cannot fill
those gaps. Switching during a level clears previous texture uploads and caches.
With GoldenEye X enabled, its first-person guns, arms, watch and hand-reload rig
keep priority and their own ROM textures. Bean supplies the other models and rooms.

During play, use **watch → VR options → Mods and fun → Project Bean** or hold
exactly one grip + right-stick click for one second. Both switches affect meshes
and texture ownership together. Right-stick click alone still changes play mode.
Without installed meshes, the watch shows **NEEDS FILES**.

Manual installs remain supported: copy your extracted `files` directory to
`Android/data/com.gevr.port/files/data/added-content/xbla/files`, preserving
`files/new/char/boilerbond/default.bin`. Restart after copying. The launcher
shows a local-files choice when it detects these meshes.

Files under `new/` preserve their original names and case, including
`new/gun/silverPPK/default.bin`. Copy the full tree to retain all categories.
The package's XEX, music and sound banks are not used by this feature.

Missing or unsupported models keep their N64 geometry. The two TV-screen
CAFF files have no GPU mesh payload and use this fallback. Statue keeps its
N64 rooms and sky. The installed edition determines which skies exist;
the game keeps its sky when a replacement is absent and keeps its original
water pass when a Bean dome is drawn.

## Validation for PR #170

The original PR failed Android compilation on the combiner macros. Local
asset tests also reproduced invalid Dam positions from the rigid stride-36
vertex format. Review fixes cover that layout, rigid stride-32 attributes,
palette-slot/weight indexing, vertex colour and alpha, triangle UV wrapping,
texture normalization, failed texture decode indices, texture-cache
invalidation, same-level toggles, bounded allocations and room-matrix
snapshots. Display lists establish their cycle and texture state explicitly.

Run the authored regression fixtures, then optionally test local game files:

```
python port/tests/test_bean.py
python port/tests/test_bean.py path/to/xbla/files
```

The supplied local set contains 499 files: 497 decoded meshes and two expected
N64 fallbacks. Dam decodes 110,448 triangles and 73 textures. The mesh sweep
passes AddressSanitizer and UndefinedBehaviorSanitizer under WSL/Linux. The
surface and GE-X regression checks also pass.

The release JVM tests pass (32 tests), as do frame-pool, launcher and bounded
ROM decoding checks. Full `lintRelease` reports 35 errors and 74 warnings in
unchanged Android Java/resources, primarily SDL Bluetooth permissions. The
normal release assembly uses the passing fatal-issue `lintVitalRelease` gate.

Shader fetch field interpretation was checked against [Xenia's instruction
definitions](https://github.com/xenia-project/xenia/blob/master/src/xenia/gpu/ucode.h).
The local Dam stride-36 shader fetches position at dword 1, normal at dword 4,
diffuse UV at dword 5 and colour at dword 8.

## Headset checks before publishing

- Dam: ground alignment, sky rotation, water, portal visibility, texture seams,
  Bond's body and head, KF7, slide and muzzle flash.
- A water stage with a replacement sky, plus Facility, Silo and Statue.
- Start with HD + AI and toggle Bean through both the watch and controller shortcut.
  Check that HD textures disappear, including where Bean has no PNG replacement.
- Turn Bean off at launch, start a stage and toggle on, then off again. Check
  characters, gun and rooms together, and repeat after restarting a mission.
- Look at glass, fences and decals. Check left-handed mode and GE-X hands,
  weapon fits, scopes and reloads with Bean enabled.
- In multiplayer, take health and armour hits with each hit comfort toggle on and off.
  Check the temporary gauges with radar enabled and disabled.
- Measure frame time and memory on Quest 2 and Quest 3, including stage changes.

The character replacement uses one rigid triangle group per joint. Visual
fit and animation quality, room assignment and performance still require
headset testing. A successful host decoder check does not establish those.
