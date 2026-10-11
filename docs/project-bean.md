# Project Bean visuals (experimental)

**Runtime validation failed on Quest 3.** Report `a2167162` from build
`0bf6179` reproduced a Dam crash and incorrect model/texture rendering. That
APK is withdrawn as a release candidate. The catalog checks below establish
registration and memory safety, not correct visual mapping. DAB renderer
parity and headset acceptance remain unfinished.

The crash was in `bgTestHitOnObj` during stereo aiming: the importer had
replaced native segmented display-list pointers with Bean heap pointers.
Replacements are now selected only during rendering. Native pointers, model
types and collision geometry remain intact. Model reloads also discard old
registrations, including different intro headers sharing one buffer.

The source comparison additionally found a missing decoded-image row
reversal, missing DAB magenta-placeholder handling, and fog/unset vertex alpha
being used as opacity. These have regression coverage and source fixes.
Bean diagnostics now use the game logger so exported reports include asset
names and replacement counts. These fixes still require headset verification.
See the [runtime audit](project-bean-runtime-audit.md) for evidence and the
remaining DAB paths required for mapping acceptance.

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
CAFF files have no GPU mesh payload and use this fallback. The three broken
release civilian bodies, poorly fitting wallet/plastique meshes and animated
intro logo also keep native geometry, following DAB's fallback choices.
Statue now has Bean rooms. The installed edition determines which skies exist;
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

The adapter reuses parts of [DAB's Bean importer](https://github.com/DabDavis/perfect-dark-dabs-mod/tree/adef1dc2d4eca1a5c453a7f2bf03a3b6cdad99b8)
and its native prop fits, adapted to GoldenEye's own skeleton and room format.
There are 457 case-sensitive native model mappings across characters, heads,
first-person guns and props. The catalog audit applies each to the actual USA
cartridge model, rather than checking CAFF decoding alone. Two mapped TV files
intentionally fall back. The remaining 55 native catalog entries have absent,
broken or unsupported release replacements.

Characters retain four bone weights per vertex and blend the game's animated
matrices. Definitions stay shared; each instance gets its own frame copy.
Heads resolve the body's neck/back matrices after attachment. Watch and detonator
sleeves follow all six outfit switches; clock hands follow the native clock
matrices. Moving prop parts retain their wheel, rotor, turret and barrel matrices.
The ending helicopter keeps the native pilot inside the Bean aircraft.

Room ownership uses the nearest original room triangle, with a temporary BVH
for the search. This removes ambiguous overlapping-box assignments. All 26
supported stage entries, including multiplayer aliases and Statue, pass native
room-reference checks. The retail sky filenames are mapped explicitly.

Run the authored regression fixtures, then optionally test local game files:

```
python port/tests/test_bean.py
python port/tests/test_bean.py path/to/xbla/files
python port/tests/test_bean_mapping.py path/to/GoldenEye-USA.z64 path/to/xbla/files
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

Registration checks pass for all 457 catalog rows and 26 stage entries,
including ASan/UBSan checks. This confirms decoding and matrix-index bounds,
weighted pose isolation, source filename case and reference-room decoding.
The reported wrong/missing meshes and materials remain acceptance failures.
Visual fit, head/neck seams, portal visibility and Quest performance require
rendered comparison and headset testing. Animated CAFF pictures currently use their first frame, and
layered reflections/material shaders are simplified; this is not full visual
parity with DAB's renderer.
