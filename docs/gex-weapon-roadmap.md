# GE-X weapon roadmap

Approved 2026-10-06. The first playable milestone is **PP7 and silenced PP7
together**, preserving the working KF7. The user then authorized a batch of
additional detachable-magazine models with in-headset fitting. Batch integration
is complete; acceptance of each new model remains pending.

## Interaction contract

- GoldenEye supplies damage, firing rules, capacities, reserve ammo and sounds.
- Screen mode plays full GE-X fire/reload animations, including the appropriate
  reload when the other hand is occupied.
- VR physically inserts ammunition and automatically readies the weapon. Reuse
  GE-X held-ammunition poses and receiver motion without moving tracked hands
  away from their controllers. Separate manual slides/bolts/latches come later.
- PP7 combines position and proximity: a clear underside grab selects magazine
  removal; ambiguous cupping selects support. Removal needs a deliberate pull.
  Grip ownership persists until release; support never converts into a reload.
- Seating requires release before another grab/support hold. Button eject stays.
- Dual wield keeps independent gun-to-belt reloads. Holster one gun for detailed
  off-hand manipulation. Dropped ammunition returns unused rounds to reserve.

## Foundation and PP7 milestone

Implemented in this change:

- `GexWeaponDef` registry for KF7, PP7 and silenced PP7. Definitions separate
  animation joints from render matrix indices, store animation/part events,
  muzzle origins, screen anchors, attachments, texture pairs and magazine fits.
- KF7 keeps legacy calibration and reload behavior. Both PP7s share independent
  gun, underside grab and support fits; muzzle calibration remains per item.
- `GwppkZ` installed magazine is matrix **38 / joint 41**, held magazine is
  **42 / joint 40**. The meshes need a rigid alignment transform; using KF7's
  shared local points would place PP7's held magazine incorrectly.
- PP7 single reload is 1047: installed magazine disappears at 19; the held part
  is shown at 1 and hidden at 24; ammo/ready alignment is 53. Other-hand-busy
  reload 1009 aligns ammo at 50 and keeps the held part hidden. Both have 92
  frames. Fire is 236, 18 frames, restarted on accepted shots.
- Main PP7 body vertices match GoldenEye exactly after translation **24,26,77**
  in the gun matrix's frame. Its screen anchor uses that offset. Stereo muzzle
  origins are derived from fire-frame-zero rig placement, separately for the
  barrel and silencer tip.
- Explicit shooting-palm support point: PP7's unused rest hand is parked
  off-screen. Support rendering uses a GE-X hand pose fixed to the pistol,
  with independently saved position and pitch/yaw/roll about the palm.
- After physical insertion, PP7 plays the receiver motion from reload frame
  53 onward. Only mechanism matrices move; the hands, body, installed magazine
  and muzzle stay tracked. Firing interrupts this cosmetic animation immediately.
- Initial PP7 thresholds: grab radius 4 cm, extraction pull 5 cm, seating radius
  3 cm, underside distinction 2 cm, proximity margin 1 cm. KF7 keeps its existing
  reload tuning. Headset comfort and controller clearance remain to be verified.
- Extractor reports model hierarchy, matrix/joint bindings, parts, bounds,
  textures and scripts. Texture matcher compares decoded RGBA pixels using the
  production non-zlib codecs and Python zlib; ambiguous/unmatched textures stay
  on GE-X IDs. Five PP7 mappings match exactly.
- Native checks execute production switch visibility, conversion, animation
  posing, screen anchoring, grip selection/latching, extraction, seating and
  ammo transfer. Converted PP7 host model uses 17,904 of the 61,440-byte budget.

This is a buildable milestone, **not a claim of completed headset validation**.
It introduces no special-weapon physical reloads yet.

## Headset findings and continuation handoff (2026-10-06)

The user tested several signed release APKs on Quest 3. Firing and physical
reload broadly worked, but screenshots exposed issues that the original
native checks did not cover. Treat the following as requirements for every
later weapon, not PP7-only cosmetic details. After testing the latest arm/entrance
fixes, the user reported **"feels good"** and requested commit/push/PR. This is
headset smoke acceptance of the final iteration. The broader handedness, scale,
dual-wield and lifecycle acceptance matrix below still needs systematic coverage.

Local screenshot evidence is under
`C:/Users/Occor/AppData/Roaming/odh/device-media/2G0YC1ZF8Q0JQ6/`:
`com.gevr.port-20261006-141406.jpg` (twisted magazine wrist),
`com.gevr.port-20261006-142414.jpg` (magazine position/support pivot),
`com.gevr.port-20261006-143656.jpg` (support angle/hidden fit mode), and
`com.gevr.port-20261006-145436.jpg` (legacy arm overlapping magazine preview).
These user screenshots are references, not packaged game assets.

| Observed problem | Cause and implemented correction | Where to continue |
|---|---|---|
| Magazine hand inverted/twisted | Aligning the PP7 held mesh to the installed mesh left its reload wrist about 172 degrees from the tracked empty wrist. Replacing only the forearm then joined incompatible orientations. Keep the tracked wrist/forearm, reconstruct the reload finger chain relative to that wrist, and relocate the held magazine's grip point. Preserve the magazine's insertion orientation. | `gun.c`: `gevrGexPistolMagGrip`, `gevrGexOffSteady`. Do not infer hand orientation from a rigid mesh correspondence alone. |
| Pistol support hand pivots with the off controller | PP7 support reused the empty off-controller pose and only translated its palm. The original GE pistol already keeps the gun-hand aim. Anchor the support skeleton to the pistol's gun matrix; controller movement determines acquisition/release, not the attached hand's angle. | `gun.c`: `gevrGexPistolSupportPose`. Keep the pose static relative to the gun and rotate about its palm. |
| Support angle cannot be fitted | A three-value support-position fit cannot correct wrist angle. PP7 now has separate pitch/yaw/roll, shared by its two variants, with save/undo and restart persistence. | `VrGexPp7SupportRot`, key `GexPP7SupportRot`. In Gun mode with support held, hold gun-hand grip: move stick sets pitch/roll, turn-stick up/down sets yaw. |
| Held magazine cannot move within the fingers | Existing `GexHeldMag` moves the entire off-hand palm, including the watch. Add mesh-only offsets, separate for KF7 and PP7. Apply them before caching the held tip or dropped-magazine matrix. | `gevrGexHeldMagFitTo`; keys `GexKF7MagOff`, `GexPP7MagOff`. Do not move the hand/forearm or installed mesh when fitting this. |
| Held-magazine mode absent for PP7 | Its availability used `gevrReloadFitAvailable`, which used the original GE rifle/SMG magazine-fed list. That deliberately excludes original GE pistols. GE-X physical pistol magazines need their own eligibility. Held/well fit is available for supported GE-X guns without the physical-reload setting; reload grab/belt fit recognizes `gevrGexByHand`. | `input.c` X cycle and `bondview2.c` HUD/`gevrReloadFitAvailable`. Test the actual production cycle, not just the existence of a label in source. |
| Original arm overlaps the fit-preview hand | The magazine hand was rendered with the gun, but `gevrGexDrawOffHand` returned with `*drawn == FALSE`. `gunfire.c` therefore appended the legacy watch/arm fallback. Mark the render slot consumed when the gun's GE-X hand already owns it, including preview and optional standalone arms off. | `gevrGexOffHandConsumed`, `gevrGexDrawOffHand`, late off-hand fallback in `gunfire.c`. Visual ownership is separate from physical reload grip ownership. |
| PP7 insertion target near the slide/top of gun | Matching the held tip to the installed magazine's fully seated top requires pushing it all the way through the grip. The entrance belongs at the handle's bottom. Add an explicit `magWell` distinct from `magTop`/`heldTop`, then apply a separate gun-local well fit. | `GexWeaponDef.magWell`, `gevrGexWellPoint`, `gevrReloadFitSetWell`. Held tip approaching the entrance triggers automatic seating/ready. |

For PP7, installed-magazine vertices span approximately Y -119..-4. Its new
default entrance is **(0.5, -118.5, -44)** in **matrix 38's local coordinates**,
at the base region. The fully seated top remains **(0.5, -4, -22.5)** and the
held tip remains in matrix 42's separate local frame. The entrance is about
9.7 cm below the seated top at normal model size. These are different semantic
points even when a magazine's meshes can be aligned exactly. The default is a
geometry-derived base-region estimate; use headset well fit to refine it.
KF7 retains its previously working target **(0, -10, 73)** with zero new offset.
Review each new weapon's actual well mouth instead of copying either default.

### Current fit controls and independent values

X cycles **Gun → Scope → Reload → Off hand → Held magazine → Magazine well →
Barrel tip**, skipping unavailable modes. Scope requires a scope; reload grab
and belt fit require physical reload and one suitable gun. Held magazine and
well fit require a supported GE-X gun and show a magazine on the off controller
even with physical reload disabled. The preview itself grants/consumes no ammo;
actual held magazines continue using the live reload state machine.

| Fit | Saved values / effect |
|---|---|
| Gun on shooting hand | `GexPP7GunOff`, legacy `GexGunOff` for KF7 |
| Magazine removal grab | `GexPP7Grab`, legacy `GexReloadGrab`; reload-mode off trigger sets it |
| Support position | `GexPP7Support`, legacy `GexForeHold`; moves attached support palm and acquisition point |
| Support angle | `GexPP7SupportRot` (degrees: pitch, yaw, roll); changes attached hand around palm, not pistol aim |
| Off-hand palm/watch | `GexHeldMag`, `GexWatch`; preserve existing user calibration |
| Held mesh within hand | `GexPP7MagOff`, `GexKF7MagOff` (cm: right, up, back on off controller) |
| Insertion entrance | `GexPP7WellOff`, `GexKF7WellOff` (cm: right, up, back on gun); changes target only |
| Barrel tip | Existing per-item GE-X muzzle trim; silenced/regular tips remain separate |

Magazine-well mode: sticks move the target; position the preview tip at the
desired mouth and press **off-hand trigger** to set the target there. HUD shows
tip-to-well distance. **A** saves all fits; **B** returns all values to the last
save; **Menu + A** exits. Both PP7 variants share hand/reload fits. New defaults
are zero deltas; do not replace existing headset INI values with defaults or
bake one user's calibration into later weapon definitions.

### Checks added from this feedback

- Native production-pose checks now cover both PP7 variants, wrist/forearm
  continuity, reload finger-chain preservation, static support under controller
  movement, palm-anchored rotations, both handedness mirrors and model scales.
- Actual input X-cycle and HUD-next-mode checks include held/well fit with
  physical reload disabled. Preview activity ends on mode/stereo/fit exit.
- Off-hand render consumption covers preview and a physically held magazine.
  Verify the complete renderer in headset too: one arm, one hand, one watch.
- Well fitting checks distinguish the PP7 entrance from its seated top,
  preserve KF7's default, follow gun rotation/mirroring/scale, align target to
  held tip through the real setter, and leave model matrices unchanged.
- Settings tests execute the production fit snapshot/undo and INI reader/writer
  for Quest and desktop, including support rotation and both families' held/well
  fits. Existing physical seating, extraction, grip-latching and ammo-conservation
  checks still run; a fitted target must feed `gevrGexMagPoints` used by gameplay.

### Immediate next work for another agent

1. Read this section and inspect the current working tree before starting DD44.
   The implementation and tests are the PP7 milestone on `codex/gex-pp7`.
   Check Git status/history and its PR before assuming any local edits are pending.
   The original approved plan remains context; this document contains the later
   headset discoveries and current control/geometry contract.
2. The user smoke-tested **goldeneye-vr-mag-well-fit.apk** and accepted its feel.
   For systematic verification, confirm no legacy arm in
   held/well preview, insertion at the PP7 handle bottom, well-fit sticks and
   trigger setter, support pitch/yaw/roll, and save/undo/restart. Latest corrections
   have smoke acceptance; record which remaining matrix cases were actually tested.
3. Test arms on/off, both handedness modes, scale cheats, both PP7s and KF7.
   Test partial-mag reinsertion, fresh belt mags, release/re-arm, dropped rounds,
   real magazine versus fit preview, and leaving fit while a magazine is held.
   Do not mark the PP7 stage complete just because compilation/native tests pass.
4. Then start DD44 with its own reviewed rig bindings, held-mesh alignment,
   wrist/grip orientation, real well entrance and per-family fit settings.
   Expand the registry/settings selectors intentionally; they currently route
   PP7 versus KF7 and are not a general per-weapon settings store yet.

All test APKs still report v0.4.10 / code 63 and the same dirty commit prefix
`9f92e6c+`; that prefix alone cannot distinguish iterations. Check the launcher's
build time, APK filename and packaged native UI strings. Gradle replaces the
APK output folder, so named copies from an earlier iteration may disappear.
Current build output is `android/app/build/outputs/apk/release/app-release.apk`;
the test copy is `goldeneye-vr-mag-well-fit.apk` in that folder. Signing/local
configuration supplied by the user is ignored under `android/`; never expose
signing passwords or package the ROM. Preserve user settings on the headset.

On this machine, release compilation once failed silently inside Strawberry
ccache on two SDL files. Retrying with **CCACHE_DISABLE=1** succeeded. Continue
setting **JAVA_HOME=C:/Program Files/Java/jdk-20 before every Gradle invocation**.
Use `android/gradlew.bat -p android assembleRelease --console=plain`, then verify
the APK with SDK build-tools `apksigner.bat verify --verbose`. No device installation
was performed by the agent; the user installs test builds. A connected Quest can
be inspected read-only to compare the installed native library with the artifact.

## Remaining stages

| Stage | Weapons | Physical reload work |
|---|---|---|
| 2 | DD44 | Another compact pistol rig, independently reviewed fits and poses. |
| 3 | Klobb, ZMG, D5K, silenced D5K, Phantom | Detachable magazines, individual support points and attachments. |
| 4 | AR33, RC-P90, sniper rifle | Rifle geometry, magazine orientation, scopes and firing cadence. |
| 5 | Shotgun, automatic shotgun | Grab/insert individual shells; repeated insertions and interrupted reloads. |
| 6 | Cougar, Golden Gun | Individual cylinder rounds and one-round insertion; mechanisms auto-ready. |
| 7 | Grenade launcher, rocket launcher, Moonraker laser | Insert grenade rounds/rockets; laser retains its ammo behavior. |

Every weapon follows extract → reviewed definition → integration → native checks
→ headset acceptance. Infer neither reload family nor mechanism geometry merely
from its Perfect Dark carrier. New loading mechanisms reuse GE-X animation
poses; use GE-X ammunition meshes where present, suitable existing GoldenEye
geometry otherwise. Bonus pistols retain their appearance and original models
until verified. Knives, throwables, mines and gadgets form a separate track.

## Verification gate

- Both handedness modes; arms on/off; normal/tiny/big guns; screen and stereo;
  watch weapon preview; HD textures; single and dual variants.
- Repeated cupping must never remove the magazine. Direct underside grabs must
  work comfortably without controller collision. Moving both hands together
  must not count as extraction; a support hold remains support after moving.
- Partial/fresh magazines, empty reserve, canceled pulls, insertion boundaries,
  button eject, belt re-entry, independent dual ammo and release after seating.
- Switching supported/unsupported models, pause/death, tracking loss and local
  multiplayer ownership must not reuse interaction state or credit wrong ammo.
- Gun fit save/cancel and restart preserve separate PP7/KF7 values.
- Android builds explicitly use `JAVA_HOME=C:/Program Files/Java/jdk-20` before
  every Gradle invocation. Keep GE-X opt-in, WIP and dependent on the user's ROM.

## Local verification commands

The development ROM is `C:/Users/Occor/Documents/other_projects/gex/gex-6a.z64`,
MD5 `640923b68b9281044ac1b518612e979b`, verified as GE-X 6a. It is not copied into
the repository or packaged. Use your own ROM paths:

```text
python tools/gex/gex_extract.py GEX.z64 3 4 7
python tools/gex/gex_texmatch.py GEX.z64 GE.z64 3
python tools/gex/gex_texmatch.py GEX.z64 GE.z64 4
python port/tests/test_gex_weapons.py GEX.z64
python port/tests/test_gex_muzzle.py GEX.z64
python port/tests/test_hand_reload.py
python port/tests/test_vr_display.py
```

The tests also run without ROM arguments using native/synthetic fixtures.
The inspection scripts require Python and GCC/compatible `cc`. JSON extraction
reports are candidates for review, not automatically approved runtime entries.


## Detachable-magazine batch and saved fits (2026-10-06)

Seven additional guns are integrated: **DD44, Klobb, ZMG, D5K, silenced D5K,
Phantom and AR33**. This makes ten supported items including KF7 and both PP7s.
Native conversion, pose and physical ammo-transfer checks cover each item.
The new models have not yet received user headset acceptance. Test all of them
before proceeding to the next loading mechanisms.

| Weapon / item | GE-X slot and model | Fire | Reload: ammo / installed out / held shown / held hidden | Magazine matrices installed / held | Host bytes |
| --- | --- | --- | --- | --- | --- |
| DD44 / 6 | 5 `Gtt33Z` | 236 | 237: 24 / 19 / 1 / 24 | 38 / 42 | 16928 |
| Klobb / 7 | 6 `GskorpionZ` | 1011 | 1012: 25 / 18 / 4 / 25 | 37 / 38 | 17504 |
| ZMG / 9 | 8 `GuziZ` | 278 | 277: 45 / 23 / 33 / 45 | 38 / 39 | 12352 |
| D5K / 10 | 9 `Gmp5kZ` | 1022 | 1019: 48 / 25 / 25 / 48 | 38 / 41 | 18768 |
| D5K silenced / 11 | 10 `Gcmp150Z` | 1022 | 1019: 48 / 25 / 25 / 48 | 38 / 41 | 19488 |
| Phantom / 12 | 11 `GcycloneZ` | 1068 | 1020: 48 / 16 / 16 / 48 | 38 / 41 | 18784 |
| AR33 / 13 | 12 `Gm16Z` | 1003 | 1004: 41 / 17 / 17 / 41 | 39 / 40 | 13920 |

Important differences to preserve:

- DD44 shares the PP7 magazine's rigid alignment and compact support pose, but
  its single reload transfers ammo at **24**, not PP7's 53. Dual reload 1009
  transfers at 50 without a held mesh. Do not copy an entire pistol definition.
- Klobb dual reload 1013 transfers at 47 and keeps the held mesh hidden; ZMG
  dual reload 1058 transfers at 61 with held visibility 33..61.
- Klobb and ZMG also park their unused idle left arms. The `compact` field
  selects the fixed shooting-grip support pose and tracked magazine wrist;
  `pistol` separately selects semi-auto fire restart and receiver readying.
- PP7's overlapping support/magazine points retain the underside requirement.
  The other compact guns select the closer distinct point with the same margin
  and fresh-grip latch; they must not inherit PP7's underside geometry blindly.
- Long guns now have support pitch/yaw/roll about the palm. With both hands
  holding the gun in Gun fit, hold the **gun-hand grip** and use move Y for
  pitch, move X for roll, turn Y for yaw. KF7 preserves its old support drawing
  at zero rotation. Moving/turning the off controller cannot pivot the posed hand.
- Body translations into GE's screen frame are DD44 `24,26,76`, Klobb `0,32,13`,
  ZMG `0,23,0`, D5K variants `0,8,6`, Phantom `0,0,0`, AR33 `8,22,4`.
  Initial muzzle origins come from the original GE flash group's position,
  transformed through these body offsets and the GE-X fire-frame-zero gun joint.
  Keep per-item barrel-tip fitting independent of shared body/grip fits.
- Magazine alignment is identity for Klobb/D5K/Phantom, a `1,0,0.5` translation
  for ZMG, and `-2,0,0` for AR33. Do not conflate mesh coordinates with animation
  joints (Phantom's matrices 38/41 are animation joints 34/36).
- Exact texture matches were checked against each original GE model. Body
  matches use GE IDs to retain HD replacements; unmatched images keep GE-X IDs.
  `gex_texmatch.py --model-only` avoids decoding unrelated cartridge images.

### Fit persistence and baking workflow

`VrGexWeaponFits[64][7][3]` stores the new fits. Saved rows are
`GexFit<item>_<component>=x y z`, with components:

| Component | Meaning / axes |
| --- | --- |
| 0 | Gun: controller right, up, back (cm) |
| 1 | Reload grab: delta from the rest magazine in raw gun-controller right, up, back (cm) |
| 2 | Support: gun forward, up, out (cm) |
| 3 | Support rotation: pitch, yaw, roll (degrees) |
| 4 | Held magazine: off-controller right, up, back (cm); mesh and seating tip move together |
| 5 | Magazine well: gun right, up, back (cm); insertion target only |
| 6 | Installed magazine mesh: gun right, up, back (cm); visible magazine only, targets stay put |

D5K silenced shares **item 10**, including magazine/well fits. Both PP7s retain
legacy `GexPP7*` keys, plus `GexFit4_6` for their shared installed mesh; KF7 retains its old gun, grab, support, held-mag and well
keys, with generic item 8 / component 3 for its newly adjustable rotation.
Save/undo captures the whole fit set. Parsing rejects invalid indices,
malformed rows and non-finite vectors. Saving only writes implemented families,
so an older build cannot zero out defaults for guns added by a future build.

After the user fits a gun and presses **A**:

1. Read `/sdcard/Android/data/com.gevr.port/files/data/goldeneye-vr.ini` using
   adb. Read only the relevant fit keys; do not overwrite headset settings.
2. Copy that family's seven rows into the designated initializer in
   `port/vr/vr_settings_defaults.c`; copy its `GexMuzzle*` trim separately.
   Preserve the canonical item sharing above. Existing saved INI overrides
   remain authoritative; compiled defaults apply when keys are missing.
3. Run settings save/load/undo and weapon/reload native checks, then build a
   signed release with JDK 20 and ask the user to verify placement in the headset.
4. Record the user's actual headset acceptance, including exceptions, here.

The user's latest saved PP7/KF7 fits were read on 2026-10-06 and baked into
compiled defaults: gun, reload grab, support position/rotation, held-hand palm,
held-mag mesh, watch and silenced PP7 muzzle. New gun placements align their
shooting palms against the fitted KF7; DD44 starts with the fitted PP7 gun pose.
These are initial placements, not validated replacements for user fitting.

### Remaining next models

RC-P90 (slot 13, `Gfnp90Z`) has a top magazine, matrices 38/39 and parts 41/42.
Reload 1038 transfers at 104 of 156 frames without scripted magazine toggles.
Review removal direction and explicit visibility/held pose before integrating;
the existing downward-pull gesture should not be assumed correct for it.

Sniper (slot 16, `GsniperrifleZ`) has parts 40/41 at matrices 40/41. Reload
1039 removes at 42, hides the held mesh at 70 and transfers ammo at 72. Its
script has no ordinary fire animation entry or flash part 90: implement and
validate a separate rest/firing pose and scope/muzzle binding, rather than
supplying an arbitrary animation to satisfy loader validation.

Shell, cylinder, grenade and rocket loading remain separate later stages.
Do not expand their physical reload rules by item range or Perfect Dark name.


## Batch headset feedback and captured calibration (2026-10-06, 15:59)

The user reported the batch "everything looked pretty good" and adjusted Gun
fit. This is smoke acceptance with two remaining exceptions: AR33/M16's hand
holding the magazine was twisted even though the magazine orientation looked
correct, and the grey Phantom's installed magazine appeared beside the gun.
Only its detection/insertion target could previously be adjusted.

Screenshot evidence (same Quest media directory as above):
`com.gevr.port-20261006-155837.jpg` (grey gun / well fit),
`com.gevr.port-20261006-155903.jpg` (AR33 magazine wrist).
The grey gun is identified as Phantom from its appearance and the changed
item-12 well fit; confirm this identification if later feedback differs.

The headset INI was pulled read-only, its finite fit vectors parsed, and all
seven canonical family rows copied into compiled defaults. No device settings
were overwritten. Existing PP7 gun/support/held-mag and watch fits were retained.
New grab calibration (controller right, up, back; cm):

| Family / item | Grab vector |
| --- | --- |
| KF7 / legacy `GexReloadGrab` | -8.21, -11.79, -11.28 |
| DD44 / 6 | -8.8699, -13.2398, 7.5154 |
| Klobb / 7 | -9.0799, -9.3980, 2.1436 |
| ZMG / 9 | -8.3530, -10.2353, 6.7583 |
| D5K variants / 10 | -8.9007, -9.7985, 4.6520 |
| Phantom / 12 | -9.4191, -10.0058, 6.3455 |
| AR33 / 13 | -9.4418, -9.0044, 6.4789 |

Phantom's well vector is **-2.0765, 1.9985, 0.5825** cm. Every other new family
well vector remains zero. The complete gun/support/rotation/held/well vectors
are in the designated defaults in `vr_settings_defaults.c`.

Fixes in the follow-up:

- AR33 explicitly enables `trackedMagWrist`, independent of `compact` and
  semi-auto animation rules. It reuses reload finger curl on the tracked empty
  wrist and keeps the magazine's rotation. This preserves the working magazine
  orientation and avoids changing KF7's accepted wrist behavior. Future rigs
  must review hand orientation separately from magazine orientation.
- X cycle adds **Installed Magazine** after Magazine Well and before Barrel
  Tip. Move stick moves forward/sideways; turn stick moves up/down. It previews
  a seated mesh even when the magazine is out, without granting ammo. It moves
  only the gun's visible magazine in stereo; held-mag fit remains independent.
- Installed mesh fit is component **6**, appended without renumbering old keys.
  Generic storage is now seven vectors; old INIs with six rows stay valid. Both
  PP7s share item 4 / component 6; D5K variants share item 10 / component 6.
- The visual delta is excluded from well-point calculation and raw grab-point
  calibration. The user can align the visible mesh while preserving the reload
  targets already fitted. Its rendered matrix is also used for a dropped mesh.
- Native tests now require actual installed-mesh movement, unchanged hand and
  magazine rotations, unchanged well target, proper X/HUD cycling, stereo-only
  preview, and persistence/undo of component 6. Wrist tests check AR33's exact
  tracked orientation and unchanged magazine rotation at both handedness/scale.

The follow-up AR33 headset calibration was verified and baked into defaults:
support hand **4.4486, -0.1748, -0.1474** cm, held magazine **-5.6976, 4.9249, 6.2176** cm,
and magazine well **0.6024, -0.1363, 0.6389** cm.

