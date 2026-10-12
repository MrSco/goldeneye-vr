# PR 172 handoff, October 12, 2026

The renderer has working GPU skinning and complete character-body batching,
float room geometry, world material batching, retained weapons, watch repairs,
animated UVs/frames, HD fog and several normal-reflection shader forms.
Fresh Quest traces show reduced Dam frame times, with fast-turn spikes still
remaining. The work is not complete.

Read [current renderer fixes and evidence](pr172-renderer-follow-up.md) first.
The earlier material notes/census document historical checkpoints.

## Repository and build

- Worktree: `C:/Users/Occor/.codex/worktrees/abe0/goldeneye-vr`.
- Local branch: `codex/pr172-gpu-skinning`.
- PR branch: `cursor/rom-hack-layouts-026b`, PR #172.
- Signed material checkpoint: `16f18ad`; newer local build identities and
  installation receipts are archived with their symbols. See renderer notes.
- Explicitly set `JAVA_HOME=C:/Program Files/Java/jdk-20` before every Gradle
  invocation. Android Studio's Java 25 is incompatible with this Gradle setup.
- The user explicitly authorized installing ongoing PR 172 test builds on Quest.
  Preserve settings and save files; the profiling driver's backup/restore checks
  hashes and test markers. Finish unattended testing with its full restore.

## Evidence and diagnostics

Quest 3 serial `2G0YC1ZF8Q0JQ6`, Bean pack `2026.10.11.3`. Current defaults:
world batching on, body batching on, retained prop threshold 256 triangles.
The GPU-body anchor retains heads/hats/weapons and native gameplay traversal.
Both regional gameplay joint walkers are covered by an executed fixture.

Controls are read from Android's absolute external files root. Require renderer
acknowledgments. Before `f169043` the requested switches were not reliably read.
`f169043` body-on traces omitted body geometry and are invalid. `95dc1f2` repairs
that regression. Use its valid full-body traces or later builds only.
Pad 285 is the reported curved road tunnel; pad 32 is near a guard post.

`tools/perf/quest_profile.py` preserves save/settings, installs a benchmark APK,
launches the level, captures the actual scene, records a moving minute and pulls
CSV/GPU/events/logs. `--scope-probe` equips a sniper and explicitly supplies a
synthetic grip through the existing file hook so scope replay and lens
submission run unattended; label that evidence.
`tools/perf/analyze.py` separates fresh/redraw, reset and detailed frames.
Local traces/captures live in `build/perf/pr172-follow-up/`.

The signed `e4367b7` checkpoint is installed and hash-verified. Full Quest
backup restoration passed. No test process or synthetic grip is left active.

## Next work

1. Finish the special Dam/Complex water and Silo/Control floor shader forms.
   Diffuse water UV motion is implemented; full bump/reflection/distortion is not.
2. Remaining prop shader variants: the reflection census has 20 families with
   some supported draws and seven with none. Do not treat these as 27 bugs or
   count a whole family fixed because one variant now reflects.
3. Diagnose remaining 120 Hz fast-turn and active-scope spikes. Straight tunnel
   movement improved in controlled traces; whole-game performance is unproven.
4. Verify road-dash fighting and distant fog in suitable moving views. Do not
   remove coincident faces merely because the census found them.
5. Complete menu/material appearance checks. Runtime folder bindings and watch
   previews have evidence; every image/effect still needs scene validation.

## Older open items (from the 2026-10-11 handoff)

The rewrite above left these out. Checked against `87370e0`: the watch light
(texture 9), watch glass (texture 11), decal sort order and Nintendo logo are
done and not listed.

- **Bean world:** hide the native backdrop rooms that have no Bean triangles
  (Dam rooms 8-13, 18 and 36-50; DAB `gebeanStageRoomHidden`) and raise the
  far plane (DAB `gebeanStageTickFar`).
- **Trigger finger:** the glove's finger is static and the native finger list
  still draws. DAB routes each bone to its nearest matrix; not ported.
- **Monitors:** DAB draws the 50 `texture/monitors` pictures; the image
  repaints (`gevr_bean_images.c`) leave them out.
- **`titleblood`:** its 41 frames are CAFF format 2, which the decoder does
  not read. Unused today.
- **GE-X with Bean (user request):** draw Bean's watch on the GE-X wrist.
  Today `activeSwap` in `gevr_bean.c` skips the Bean watch arm when GE-X
  guns are on.
- **Bean PNG pack** (GoldenEye-007-Bean `GOLDENEYE/Matched`): the ammo PNGs
  are flipped and BGR; the 4 MP stage PNGs are keyed to the wrong images; the
  clouds PNG is flipped; the "portraits" are upscales of the ROM. VR's
  repaints override all of these; GLideN64 users still see them.
- **Hack campaigns:** the remaining Goldfinger/TND code changes
  ([port notes](rom-hacks-port-notes.md)) and Goldfinger missions end to end
  on the headset.

## Assets and references

The player's ROM and extracted files stay local and must not be committed:
`C:/Users/Occor/Documents/other_projects/goldeneye-vr/007 - GoldenEye.z64` and
`added-content/xbla/files` in that checkout. DAB reference is pinned at
`adef1dc2d4eca1a5c453a7f2bf03a3b6cdad99b8`, locally in
`build/bean-reference/upstream`, under `port/licenses/dabs-mod.txt`.
The pack-wide census has 987 CAFFs, all 7,880 textures decoded, 25,769 materials,
and zero missing bindings on loaded meshes. Four empty dynamic TV containers
retain native fallback. Current [reflection census](pr172-reflection-census.json).

## Validation

Run real-asset `test_bean_material.py` and `test_bean_mapping.py` under WSL with
`BEAN_SANITIZE=1`; mapping includes 457 registrations and 26 stages. The GLES
fixture `test_skin_shader.py` runs under Mesa/WSL, with skin/tint/fog/UV/scope and
reflection RGB/alpha execution. Native emitter, joint walker, scope replay,
uniform ownership, two-hand and lost-tracking/grip-redraw checks cover the new paths.
Existing broader renderer/game and 36 JVM tests passed earlier checkpoints.
Run checks appropriate to new changes rather than repeating every suite.

## Other PR scope and historical notes

This branch combines Goldfinger 64 / TND campaigns, Project Bean (#170),
Dam speedboat (#171) and alarm-hit repairs (#174, closes #173). See
[ROM hacks](rom-hacks-port-notes.md), [Bean](project-bean-handoff.md),
[visual repairs](visual-runtime-repair.md) and [speedboat](dam-speedboat.md).
Campaign end-to-end runs, multiplayer interactions and boat/alarm scene tests
are separate from the renderer evidence above.

Preserve mixed line endings (`* -text`); edit byte-preserving. Source-extraction
fixtures need any new statics declared. A plausible offline cause is not a
verified cause until the relevant scene is rendered and inspected.

## Latest local shader research (unshipped)

Ignored `build/remaining-shaders/` has CAFF extraction and a partial Xenos
instruction decoder. Later level-shader offsets became invalid when repeated
shader headers were counted as new physical blocks. Metadata interning is now
being investigated in that offline script; validate every resulting program
before using it to derive water/floor formulas. This research changes no runtime
shader behavior and does not count as a water/reflection fix.
