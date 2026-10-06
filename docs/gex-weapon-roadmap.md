# GE-X weapon roadmap

Approved 2026-10-06. The first playable milestone is **PP7 and silenced PP7
together**, preserving the working KF7. Subsequent families follow only after
the previous stage passes its headset acceptance checks.

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
