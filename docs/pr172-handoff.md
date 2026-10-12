# PR #172 handoff (2026-10-11, end of session)

GPU-skinning follow-up: Bean character GPU skinning is implemented and has
offline shader/pose checks. See [GPU skinning notes](bean-gpu-skinning.md) for
the implementation, CPU fallback limits and the `gevr_noskin.txt` A/B switch.
It has not been installed or validated on the headset.

Start here. Detailed notes: [Bean handoff](project-bean-handoff.md),
[hack campaigns port notes](rom-hacks-port-notes.md),
[visual runtime repair](visual-runtime-repair.md), [Dam speedboat](dam-speedboat.md).

## Where things are

- **Branch:** `cursor/rom-hack-layouts-026b` (draft PR #172), pushed, clean.
  - Worktree: `.claude/worktrees/goldeneye-vr-pr-146-review-b63bd3`.
  - It combines four PRs: the Goldfinger 64 / TND campaigns, Project Bean (#170), the Dam speedboat (#171) and Dam wall alarms (#174, closes #173). Those PRs are closed and their branches deleted.
- **Latest test APK:** build `915f6c8` (release-signed, built 19:51). It has NOT been installed or headset-tested. Rebuild from that commit if needed; only test files and docs changed after it.
- **Bean pack:** MrSco/GoldenEye-007-Bean release `v2026.10.11.3`, the Latest.
  - VR zip 188,298,238 bytes: 499 meshes, 2 font atlases, 334 pictures, 122 PNGs.
  - `ModManager.java` pins it (version, URL, size).
  - Process: the repo's `GOLDENEYE/` folder is the PNG source; `tools/package_bean.py` builds both zips.
- **Dab's Mod reference** (MIT, pinned adef1dc2): `C:/Users/Occor/.codex/worktrees/d60f/goldeneye-vr/build/bean-reference/upstream`. Don't delete that Codex worktree until this reference is moved.
- **The player's own files** (never commit):
  - ROM: `007 - GoldenEye.z64` in the main checkout;
  - XBLA files: `added-content/xbla/files`;
  - Goldfinger ROM: `other_projects/goldfinger64`;
  - TND Expanded: rebuild with `tools/gex/vcdiff_apply.py ge.z64 TND64/expanded/TND64_Expanded_06-22.xdelta out.z64`.

## Last headset report (build cb33888+, Bean on, Dam)

The user saw:
- Goldfinger maps now draw.
- The Nintendo logo was translucent.
- Fonts were HD but upside down.
- The file-select crosshair was low-res.
- The watch was see-through.
- Guns, and guards' hands, sat set back.
- The map had holes.
- It stuttered: 52 fps of 120, DRAW/TICK 16 ms, GPU 2.6 ms.

Everything below is fixed in source and has an offline check, but nothing has been verified on the headset:

| Fix | Commit | Offline check |
|---|---|---|
| Fonts right way up | 1634b10 | `test_bean_fonts.py`: letter-shape check |
| 121 HUD/menu/effect images repainted from `files/texture` (cursor 256x256, ammo, portraits, stage pictures, bullet holes, puffs, clouds, water) | 56ecb44 | `test_bean_images.py` |
| Guns in GoldenEye's frame, glove on the hand list, pickups un-mirrored | 2334405 | mapping test |
| Props and guns two-sided | ca729c0 | — |
| fast3d presentation clip once per vertex, 1024-triangle batches, hashed Bean lookups, stats line VTX/TRI1/CLIP/ROOM/PRES | cf59657 | — |
| Dab's translucency and decal rules: logo, wrist watch, reservoir water | 1d02ef1 | `test_bean_material.py` |
| Bean rooms and sky as static GPU meshes (Dab's G_MESH_EXT) | 7f75f48 | offline bit-identical to the CPU path; room CPU 10 ms → 0.13 ms |
| Rooms drawn by their Bean bounds (the holes) | 915f6c8 | — |

To check on the headset:
1. Log line `GL: static meshes on`, and a `mesh:` line every 72 frames.
2. DRAW/TICK should fall well below 16 ms.
3. A/B by putting an empty `files/gevr_nomesh.txt` on the headset; it switches back to the CPU path.
4. Visuals: holes, water, logo, watch, guns, guards' guns, menus, the scope, and 90/120 Hz redraws.
5. Dump logcat to a file first, then search it.

## Next steps, in order

1. **Headset round on `915f6c8`.** Fix what it shows before adding more.
2. **GPU meshes phase 2:** Bean character skinning is implemented.
   - Validate GPU/CPU character parity and frame time with the checklist in
     `bean-gpu-skinning.md`. Lighting, fog, culling and palette redraws have
     offline checks; scope and first-person overrides retain CPU fallback.
   - Then rigid models (guns and props) as static meshes.
   - Then per-chunk culling for Dam rooms 35, 132 and 135.
   - Optionally A/B `-fno-inline-functions` for fast3d (`CMakeLists.txt:120`).
   - The `gevr_static_mesh.h` header and the agent's design notes in commit 7f75f48 are the starting point.
3. **Bean leftovers** (details in the Bean handoff):
   - `suitlfhand` texture 9: we drop it; Dab draws it. Unverified.
   - Glass texture 11 (Dab's `beanArmGlass`).
   - Decal sort order: `vertKey` sorts by texture before the decal bit.
   - Hiding native backdrop rooms (Dab's rule) and the far plane.
   - Per-bone routing of the trigger finger.
   - Monitors (Dab draws `new/texture/monitors`).
   - `titleblood` uses CAFF format 2, which doesn't decode.
   - Nintendo logo material.
   - User request: with GE-X and Bean both on, draw Bean's watch on the GE-X wrist.
4. **Bean PNG pack faults** (GoldenEye-007-Bean `GOLDENEYE/Matched`):
   - the ammo PNGs are flipped and BGR;
   - the 4 MP stage PNGs are keyed to the wrong images;
   - the clouds PNG is flipped;
   - the "portraits" are upscales of the ROM.
   VR's repaints override all of these; GLideN64 users still see them.
5. **Hack campaigns:**
   - remaining Goldfinger and TND code changes (port notes);
   - Goldfinger missions end to end on the headset.

## Tests

- **Windows:** run every `port/tests/test_*.py`. `test_romcheck` fails on Windows only, as on main (ctypes can't find `zlib1.dll`).
- **WSL** (`wsl -e bash -lc ...`), with `R=<ROM>` and `F=<xbla files>`:
  - `BEAN_SANITIZE=1 test_bean.py`
  - `test_bean_mapping.py R F`
  - `test_bean_modes.py`
  - `test_bean_fonts.py F`
  - `test_bean_folder.py R F`
  - `test_bean_images.py R F`
  - `test_bean_material.py F`
  - `test_launcher_mods.py`
  - `test_bean_launcher.py`
- **Real-ROM tests:**
  - `GEVR_GF64_ROM=... test_romcampaign.py`
  - `GEVR_GE_ROM=... test_speedboat.py`
- **Build:** `JAVA_HOME=C:/Program Files/Java/jdk-20 android/gradlew.bat assembleRelease testReleaseUnitTest` (36 JVM tests). If `mergeDexRelease` says the file is in use, run `gradlew --stop`.

## Traps

- **Line endings:** the repo is `* -text` with mixed endings. Edit byte-preserving and compare `git diff --stat` with and without `--ignore-cr-at-eol`.
- **Bash heredocs** eat backslashes. Write scripts with a file tool instead.
- **Test fixtures:** `test_locomotion`, `test_multiplayer` and others paste production renderer functions into fixtures. A new static used by those functions needs a declaration in the fixture.
- **Bean decode orientation:** `gevrBeanTexDecodeRgba` returns bottom row first. Repaints keep the ROM's own memory row order (checked by correlation tests); never flip by assumption.
- **Agent permissions:** a subagent was once refused edits to a shared file. Don't route around a refusal; ask the user.
- **Unconfirmed causes:** a plausible offline cause is not the cause until the headset confirms it. The Cartel's walls looked like a memory problem and were really `ALPHA_CVG_SEL`.
