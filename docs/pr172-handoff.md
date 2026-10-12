# PR 172 handoff, October 12, 2026

The renderer has working GPU skinning and complete character-body batching,
float room geometry, world material batching, retained weapons, watch repairs,
animated UVs/frames, HD fog and several normal-reflection shader forms.
Fresh Quest traces show reduced Dam frame times, with fast-turn spikes still
remaining. The work is not complete.

Read [current renderer fixes and evidence](pr172-renderer-follow-up.md) first.
The earlier material notes/census document historical checkpoints.

## Headset round 2026-10-12 morning (installed e4367b7, Dam)

The user reported stutter in the tunnel and when turning, tower glass no
longer see-through, and the gate switch missing its red/green lamp.
Installed for them: release `07b1920` (user-authorized test install).

- **Tower glass** (c24451d): 4f51c0c read the two-constant reflection
  form's alpha from c13 (1.0); DAB reads the material alpha from c12 (the
  window's 0.56). The pane is glass at 142 again; capture shows the
  outside through it. `test_bean_material.py` checks the 142.
- **Monitor screens** (76d7920, port of DAB's screen code in
  `port/src/gevr_bean_screens.inc.h`): every Bean monitor had its part 0-3
  screen lists registered blank, so no programme drew (gate switch lamps,
  modem, consoles, TVs). Live screens are handed back, Bean's grey cards and
  placeholders laid black behind the screen, recesses flattened, fitted
  screens moved. Capture: the gate switch shows its red lamp, desk
  monitors their green text. Open: the agent's extra recesses (door console
  7, bridge consoles 2a/3a/3b 4) beyond DAB's logged ones, and the modem's
  decal-depth screen, need a look in their scenes.
- **Turning stutter**: two CPU changes, measured with `tools/perf` at pad
  285 (3 runs each, user's settings, 120 Hz):
  - a52a18e: the opaque world mesh splits runs at 2048 triangles, not 512
    (Dam: 234 chunks, 126 of them from two landscape pictures).
  - 35aba5e: mesh runs keep program/VAO/clip/cull bound for the next run
    of the list (`gevr_mesh_settle()` at every other backend entry and at
    the list's G_ENDDL).
  - Smooth-turn fresh frames: median 6.5 -> 5.2 ms, p95 11.2 -> 10.1,
    p99 12.4 -> 11.1; compositor dropped frames 906 -> 592 per 224 s.
    Still over the 8.3 ms budget while turning: not solved.
  - At 90 Hz (same build, 3 runs): 17 missed frames in turns vs 197 at
    120; compositor drops 176 vs 592 per 224 s; turning p99 11.0 ms at
    the 11.1 ms budget. The user chose a warning, not a cap: the Screen
    tab says "90 Hz advised for Bean" when Bean is on above 90 (0557419).
  - Ruled out: mipmapping Bean textures (no GPU change on this route).
  - simpleperf after both: GL driver 37% of the render thread (mesh draw
    7.7%, redraw replay 7.4%, CPU batches 3.6%), vrapi 7%, libc 7%,
    phonon 4% on the render thread, gfx_sp_tri1 11% inclusive.
  - The user's own session (sniper held, near the gate) was GPU-bound:
    VrApi App 8-10 ms GPU, 64-76 fps. Not reproduced by the benchmark
    routes; profile that scene next (`--scope-probe` at a gate pad).
- `gevr_warp.txt` "near <model> <reach>" (07b1920) faces an object for
  unattended captures: 336 gate switch, 104 window, 335 modem.
  Driver used: `tools/perf/quest_profile.py` backup/restore around it.

## Headset round 2026-10-12 afternoon

- Launcher: the Mods page's hint line clears Done (9e297ab); the Screen tab
  warns "90 Hz advised for Bean" above 90 Hz (0557419).
- Damage (ea90845): native behaviour, logged per hit ("damage:" lines and
  the guards' fire counters in chrlvUpdateShotbondsum).
- Muzzle flashes stuck on: pickups leave out the painted flash (88ef968),
  first-person guns treat piece 0 as the flash (a9d4023, DAB fpDrawIsCard),
  and first-person guns take no material glass alpha (984cc13). Headset
  checked. Left as DAB has it: prop/chrfnp90's magazine (alpha 0) on the
  pickup is see-through.
- **Bean from the player's own copy** (9d3e89a). The user asked that no
  game files be distributed: the Mods page no longer downloads the
  GoldenEye-007-Bean release. "Choose copy..." opens the file picker and
  `port/src/gevr_bean_import.c` (DAB archive.c 7z streaming, x360.c STFS,
  LZMA SDK in port/external/lzma) imports the 835 files Bean reads from a
  .7z of the release, a .zip, the Xbox 360 package or a zip holding it.
  Bean reads no PNG pack now (DAB uses none): its pictures come from the
  copy, the ROM's otherwise, so explosions are the cartridge's, as DAB's.
  On the Quest: retail zip 4.3 s, solid 7z 11.5 s, identical output.
  `test_bean_import.py --files <xbla>/files --copy <copy>` compares real
  copies byte for byte. Texture 2171 takes the release's bullet hole as
  DAB's geimpact.c (a0783d5).
- The Bean releases are deleted and MrSco/GoldenEye-007-Bean is private
  (user, 2026-10-12).

## Headset round 2026-10-12 evening (be8b409 on the Quest)

Rule from the user: check Dab's Mod (other_projects/dabs-mod, `git pull`
first) before designing anything Bean, XBLA or GE-X.

- Watch and weapon panel (003475d): gevrBeanPreviewPose put the HD gun at
  the root plus the root's origin, which the watch leaves out (PP7 root at
  0 -58 -73, the watch prop 2732 -1247 -436): off-axis spin, parts beside
  the gun. And as DAB gebeanBuildFirstPerson(): each release bone on the
  nearest matrix with a visible list (30 units), the slide on part 7's, so
  GoldenEye moves Bean's slide, cylinder, hammer and drum instead of its
  N64 pieces drawn over the HD gun. Offline probe of all 67 hand models:
  every visible list where its rest puts it.
- Small pictures (a0de5fd): DAB beanGunExtent() drops the HD guns'
  pictures of 64x64 and under (every flash card; the rocket launcher's in
  its mouth was on no piece).
- Gun fit (59c7199): a third look, Bean's (bondview2.c gevrFitLook), with
  its own gun place, two-handed hold, reload grab, scope lenses and barrel
  tips (Bean* keys); an ini without them starts from the GoldenEye fits.
- Taser (5d10c2b): native, as DAB (the release's is the N64 one again).
- 99f23fa: gold/silver metal (DAB fpTint, beanShadeTint, beanGoldPicture;
  the gold and silver PP7 by the same rule, no DAB row), the N64 trigger
  finger hidden under Bean's glove (the HD pistols carry their own), and
  DAB fpRound (the launcher built loaded and spent, gevrBeanSetLoaded /
  gevrBeanHasRound; GoldenEye's rocket not drawn over Bean's).
- be8b409: watch laser on gun/trigger as DAB; watchmagnetattract turned
  round (its strap wraps the other way from GoldenEye's); a gun's body
  never on a hand list (the throwing knife vanished in the watch).
- Asked, not changed: Dam water slides 0.5 repeats/s, DAB's Xenia figure
  (the user will compare with the Xbox release); no off-hand watch fit for
  Bean's arm (user: "nah").
- Still open: the multiplayer stage dropdown cuts off "Caverns"; clipping
  through Caverns' narrow tunnel doors (likely old).

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
