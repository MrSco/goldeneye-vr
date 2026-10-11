# Project Bean visuals (experimental)

GoldenEye VR can load characters, heads, weapons, props, rooms and skies from
the player's own extracted Project Bean files. No game files ship in the APK.
The GoldenEye USA ROM remains required; gameplay, collision and animation
remain the original game's.

Copy the extracted `files` directory to:

```
Android/data/com.gevr.port/files/data/added-content/xbla/files
```

For example, Bond's body must exist at
`files/new/char/boilerbond/default.bin`. Restart the app after copying files.
The launcher's Play tab enables **Project Bean models** when that file exists.
Models default on when installed. Disable the checkbox to use N64 visuals.
During play, hold exactly one grip and the right stick click for one second
to switch. The right stick click alone still changes play mode.

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

The release JVM tests pass (24 tests), as do frame-pool, launcher and bounded
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
- Turn Bean off at launch, start a stage and toggle on, then off again. Check
  characters, gun and rooms together, and repeat after restarting a mission.
- Look at glass, fences and decals. Check left-handed mode and GE-X hands,
  weapon fits, scopes and reloads with Bean enabled.
- Measure frame time and memory on Quest 2 and Quest 3, including stage changes.

The character replacement uses one rigid triangle group per joint. Visual
fit and animation quality, room assignment and performance still require
headset testing. A successful host decoder check does not establish those.
