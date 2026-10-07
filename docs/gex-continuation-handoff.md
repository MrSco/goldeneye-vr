# GE-X continuation handoff — 2026-10-06

Start here, then read [the roadmap](gex-weapon-roadmap.md) for the history of
PP7 grip, wrist orientation, duplicate arms, insertion targets and installed
magazine fitting. This file records the current implementation and remaining work.

## Latest headset feedback — unresolved, address before adding guns

Reported after the version-code-65 test build, 2026-10-06. User lowered the
weekly reserve floor from 2% to **1%** for this investigation. This pass made
documentation changes only: no gameplay fix or new APK is claimed. The current
code/APK remains build **9b212a0**, version **0.4.11 / code 65**, with the cuff
crash fix. PR #138 contains this investigation checkpoint.

### Rocket launcher: seated rocket disappears after reload

User sees rocket ammo at the end initially. After reloading, the gun can fire
but the rocket model does not stay at the tube's end. Ammo transfer works;
the missing representation is the visible loaded payload.

Confirmed source facts: `GdyrocketZ` definition has `singleRound=1`,
`gunMatrix=magMatrix=33`, `heldMatrix=37`, `parts={-1,40}`. There is deliberately
no installed switch; part 40 is driven as the held payload.
`gevrGexShowMagazines` drives part index 0 from inGun and index 1 from inHand.
`gevrGexPoseGun` normally clears inHand after physical insertion (state IN,
no active screen reload). This leaves no explicit loaded-round visibility path,
which is the leading explanation. Initial source visibility was not reproduced
on-device during this pass; do not treat that detail as resolved.

Next: inspect the ROM's native rest/fire/reload matrix 37 and part 40 geometry,
then render a seated round whenever authoritative GE loaded ammo is nonzero.
Keep its seated transform separate from the held/off-hand pose and fits. Since
capacity is one, held versus loaded rendering may be mutually exclusive, but
verify direct belt reload, cancelled/dropped rounds, fit preview and screen
reload before relying on that. Do not make shell or Golden Gun ammo permanently
visible through a generic singleRound override. Prevent a falling held mesh
from overwriting the seated pose. Hide the loaded rocket after firing and show
it again after either physical insertion or direct belt reload.

Required regression: actual render visibility/pose at spawn loaded, fire empty,
pickup held, insertion loaded, ready-animation end, next shot, cancel/drop,
weapon switch and both handedness/size modes. Existing ammo-only reload tests
and converter tests do **not** cover this loaded-round rendering lifecycle.

### Sniper: intermittent off-hand grip shows scope sight; clarify belt behavior

User reports that off-hand grip sometimes makes the scope crosshair appear.
Their exact wording says "i could grab magazine from my belt to reload while
that was happening"; do not assume belt pickup was blocked. Clarify whether
"could" was intended literally or meant "couldn't" before classifying that
part of the failure. Switching away from sniper and back restores correct
behavior. This is intermittent, unreproduced locally, and the cause is unconfirmed.

Trace `inputReadController` gripTaken/physical-to-logical grip mapping and
R_TRIG generation, `gevrGripGestureInput/Taken`, `gevrStereoTwoHandUpdate`,
`gevrGexClaimsOffHand`, `gevrReloadClaimsOffHand`, `gevrHandReloadTick`, and
`gunDrawSight` / gunsightmode / insightaimmode. In stereo, the input's normal
R_TRIG aim request uses logical **right** grip unless gripTaken[1]; off grip
alone should not request the primary sight. Two-hand state is s_gevrTwoHand;
the support update preserves an existing hold through tracking occlusion and
checks reload ownership only when !s_gevrTwoHand. Thus a stale support/gesture
owner is a candidate to investigate, not an established cause.

Record per tick: physical and logical grips, handedness mapping, gripTaken[2],
s_gevrGripGesture[2], s_gevrTwoHand, controller tracked flags, current GE item
and GEX definition, GEX magazine state/item, s_gevrGexGripSpent, fresh-grip flags,
R_TRIG, gunsightmode, insightaimmode and belt distance. Compare before/after
weapon switch; test release/repress, scope-near occlusion, and crossing from
support into belt reach. Preserve deliberate grip ownership and partial ammo;
do not erase all state each tick or let a support hold silently become reload.

### Moonraker laser: primary grip does not show sight during two-hand hold

User cups the pistol with off-hand grip, then grips the gun-hand controller to
enable the scope crosshair; no crosshair appears. Root cause unconfirmed.
Laser is compact/pistol, `hasScope=1`, no reload payload or physical reload.

`inputReadController` generates aim R_TRIG from rightGrip only if !menu,
!rightThrowable and !gripTaken[1]. Gun fit later removes R_TRIG; weapon/pause
flows can also clear it. `gunDrawSight` renders the primary scope sight only
when gunsightmode==0 and mpmenuon==FALSE, then requires gevrScopeOn's right bit.
`gevrGripSteadyOnCtrl` returning TRUE for two-hand support affects smoothing;
it is not itself a crosshair request. Inspect actual grip gesture consumption,
aim-state transition and scope bit at the failed press; do not equate steady
scope with aiming or remove valid damage/menu sight suppression.

Required paired scope tests: sniper and laser, one/two hands, right grip only,
off grip only, both grips in either press order, release/repress, tracking
loss/recovery, belt reload ownership, weapon switch, handedness and fit mode
on/off. Verify source GE behavior alongside GE-X to separate shared input/state
bugs from model bindings. No scope/grip regression or fix was added in this pass.

Finish these three and capture the user's new fits/acceptance before continuing
Cougar cylinder or grenade projectile integration below. Preserve all accepted
PP7/KF7/AR33/RC-P90 calibration and the version-code-65 cuff crash fix.

## Crash cf69dded follow-up — latest priority

Latest delivery supersedes the APK paths below: Android **versionCode 65**,
versionName **0.4.11**, build label **9b212a0**. Signed release rebuilt with
JDK 20; apksigner verifies and aapt confirms code 65. APK:
`android/app/build/outputs/apk/release/GoldenEyeVR-code65-cuff-fix-9b212a0.apk`.
SHA256: `4fb508d42c055ab91b79321a71e8cd0fc7d799d2fdd098022705b6e655eb2864`.
The version bump is pushed to PR #138; code retains the cf69dded cuff fix and
captured RC-P90 fit defaults. Headset retest remains pending.

User reported A-button switching away from RC-P90 crashes; wheel selection of
Klobb worked. Decoded report shows the destination was **GshotgunZ**, loaded
2026-10-06 19:44:02 immediately before SIGSEGV (fault address 0xc26).
The Android report says tombstone unavailable, but its logcat contains frames:
`modelGetNodeRwData+12 → bondviewSelectCuff+172 → gunUpdateAndFire+5872`.
The failing library BuildId is `117cacd78d54d5a9910b67f37ce5ebf392e9d787`.
Report file is base64 text; decode locally before reading. Raw/decoded reports
contain private configuration and remain outside commits (ignored build folder).

Root cause: original GE shotgun has 28 switch slots. Adding the GE-X two
payload slots makes 30, satisfying gunfire's original `numSwitches >= 30`
cuff-selector guard. `bondviewSelectCuff(...,29)` then treats the held shell
at slot 29 as a sleeve and reads slots 30..34, already in texture data. RC-P90
has 36 original slots; Klobb has 36, which explains the destination distinction.
This is a destination model table bug, not evidence of an A-only input bug.

Fix: skip original cuff selection for either current GE-X weapon header;
validate the table/index, then bound and type-check each original cuff lookup.
GE-X source arms keep their own material/visibility, without cuff writes altering
payload switches. Regression in `hand_native.c` poisons the entry after a
30-slot GE-X shotgun table with 0xc1e and verifies both hands preserve held-shell
visibility. It also tests truncated tables, invalid indices and missing tables,
while retaining all existing solo/online sleeve checks. `test_multiplayer.py`
passed all 64 native tests after the fix. Settings round-trip tests and the
JDK 20 signed release build also pass. Fix commit is `c3cdc1c` on PR #138.
Test APK: `android/app/build/outputs/apk/release/GoldenEyeVR-gex-cuff-fix-c3cdc1c.apk`.
In-game label: c3cdc1c; apksigner v2 verification passed. SHA256:
`9b66a16bdeac220466bb024b8f09818654ca885200bbf62e45ef136ee5975937`.
Use this APK instead of the original cdcb44a batch APK below.

Saved RC-P90 calibration recovered from the report and baked into item 14:
grab **-11.7789,-0.8653,13.1938**, held payload **1.2670,8.2710,-8.3611** cm.
Existing gun placement and other family defaults are preserved; no device INI
was overwritten. This feedback does not establish acceptance of all seven guns.

Next headset checks: A from RC-P90 to shotgun repeatedly, wheel directly into
shotgun, reverse cycling, original GE guns with GE-X disabled, both handedness
modes and the other new destination guns. The native reproduction is fixed;
on-device retest is still required before calling the crash headset-verified.

## Branch and scope

- Branch: `codex/gex-remaining-weapons`; base Main:
  `4786b2aa52ee526b15a6f671a61a522d5138aec2` (v0.4.11 / code 64).
- Main includes the previously merged magazine batch and accepted AR33 fits.
  Those defaults remain intact. Multiplayer protocol remains 18.
- Added RC-P90, sniper, Moonraker laser, shotgun, auto shotgun, rocket and Golden Gun.
  Total: 17 weapon variants. These seven are implemented and pass native checks;
  **headset placement and gameplay acceptance are pending**.
- Remaining ordinary guns: Cougar Magnum and grenade launcher.
  Bonus PP7s, knives and throwables remain a separate follow-on track.
- Latest user instruction: stop before weekly remaining usage reaches 2%; the
  older 4% floor is superseded. Do not use a reset credit automatically.

## Completed delivery and stop point

- Implementation commit: `d3ea3b8e4159038d565a0fe0dc59cc532fc94475`.
- Open PR: [#138 — seven GE-X guns](https://github.com/MrSco/goldeneye-vr/pull/138),
  pushed and attached to the chat. No GitHub checks were listed at delivery;
  validation reported below was run locally. This PR has not been merged.
- Signed test APK: `android/app/build/outputs/apk/release/GoldenEyeVR-gex-seven-cdcb44a.apk`
  in this workspace, 21978610 bytes; apksigner confirms v2 signature, one signer.
  SHA256: `6231ec3313e27ca67e20444cb996a3bc9e6a282c162bdda7575b6b37c28d8bdf`.
  Built from checkpoint `cdcb44a` with JDK 20; in-game build label is `cdcb44a`. App version remains
  0.4.11 / code 64; this is a test build, not a published release.
- Weekly usage last reported 96% used / approximately 4% remaining at delivery.
  Stopped with the user's 2% reserve intact after seven complete additions.
  Cougar needs distinct cylinder-seat/partial-round rendering; grenade launcher
  needs a held projectile mesh absent from its source rig. Resume from those
  inspected cases below after accepting/fitting the current batch.
- Subsequent commits that only update this handoff do not change APK code.

## Code landmarks

| File | Responsibility |
| --- | --- |
| `port/include/gevr_gexweapon.h`, `port/src/gevr_gexweapon.c` | Explicit item registry, source model slots, joint/matrix bindings, texture matches, timing, root anchors and scope/muzzle points |
| `src/game/gun.c` | Source pose decoding, held payloads, tracked wrists, support rotation, screen anchor, visible parts, insertion/held previews and dropped payloads |
| `src/game/bondview2.c` | Physical reload and ammo accounting, grip ownership, source scope root, HUD and geometric fit targets |
| `port/src/input.c` | X fit cycle and availability |
| `port/vr/vr_settings_defaults.c` | Baked user calibration and starting fits for new families |
| `tools/gex/gexguns.py` | Inspect equip/idle, fire and reload scripts from the ROM |
| `port/tests/test_gex_weapons.py` | Actual converter/animation functions, wrists, support rotation, previews, scopes and host budgets |
| `port/tests/test_hand_reload.py`, `hand_reload_native.c` | Actual production physical reload functions and reserve accounting |

New registry flags are explicit rather than item-range assumptions:
`restAnim`, `gripMatrix`, `pullUp`, `hasScope/scopeRoot`, `singleRound`.
`HasAmmo` includes held shells/rockets; `HasMagazine` excludes them and laser.
The roadmap's seven-gun table contains every binding, timing and converted size.

## Immediate headset session

1. Test all seven in stereo and screen mode, arms enabled and disabled, both hands,
   small/normal gun size, firing, reload, death/switch/watch and dual wield where
   permitted. Native success does not establish comfortable physical placement.
2. RC-P90: lift the top magazine up; inspect tracked fingers independently from
   magazine orientation. Test screen reload's moving magazine and held duplicate.
3. Sniper/laser: check scope ring/zoom and barrel tip; laser must have no held,
   insertion or installed-mag fit modes. No fabricated source firing clip should
   play on RC-P90, sniper or auto shotgun; GE firing/recoil still applies.
4. Shotguns: start partly loaded; grip one shell at the belt and insert it into
   the port. Each insertion adds exactly one and auto-readies. Repeat after
   releasing grip. Loaded ammo survives pickup/drop; B/Y does not eject shells.
5. Rocket: pick up one rocket, insert its selected end into the front tube face,
   inspect the supporting hand under the tube and verify firing/projectile origin.
6. Held Ammo fit moves only the visible held payload; Ammo Insertion fit moves
   only its detection target. Installed Magazine is offered only on detachable
   magazines. Fit previews must consume the old off arm even with hand reload off.
7. Move each gun itself to the fitted belt for the retained direct-reload fallback.
   With two guns equipped, use that path or holster one for detailed off-hand loading.

## Read and bake the next fits

Device serial: `2G0YC1ZF8Q0JQ6`. INI:
`/sdcard/Android/data/com.gevr.port/files/data/goldeneye-vr.ini`.
Pull read-only into ignored `android/app/build/`; do not push/reset device settings.
Print only relevant fit keys, not private configuration or signing information.

New canonical GE items: 14 RC-P90, 15 shotgun, 16 auto shotgun, 17 sniper,
19 Golden Gun, 22 laser, 25 rocket. Keys: `GexFit<item>_<component>`.
Components: 0 gun, 1 magazine grab, 2 support position, 3 support rotation in
degrees, 4 held mesh, 5 insertion target, 6 installed mesh. Component 1 and 6
are not offered for single rounds; 1/4/5/6 are not offered for laser. Scope and
`GexMuzzle*` trims have separate keys. Keep PP7 sharing item 4 for component 6,
D5K sharing item 10, and legacy KF7/PP7 keys. Saved INI values override defaults.

The accepted AR33 rows from Main are support `4.4486,-0.1748,-0.1474`, held
`-5.6976,4.9249,6.2176`, well `0.6024,-0.1363,0.6389` cm. Never replace these
with the initial estimates in older roadmap sections. New seven-gun defaults
are initial estimates pending fitting. Record acceptance and each exception.

## Remaining rig inspection and implementation order

### Implemented Golden Gun — GE item 19, PD slot 18, `Gleegun1Z`

43 source matrices. Fire 236; equip 234. Single reload 1045, 134 frames:
open parts 45/42 at 19; held bullet part 43 shows 38; ammo transfers 82;
parts 45/42/43 hide at 115. Dual reload 1059 transfers 68; shows 45/42 at 19,
hides 59, no held-bullet show. Bullet part 42 binds output matrix 38 / joint 41;
held part 43 binds matrix 42 / joint 40. Both have bounds
(-28,30,-77)..(-20,38,-51), so their mesh alignment is identity. Part 45 is
open-cover geometry bound to gun matrix 33; default parts 42..47 are hidden.

Implemented with single-round reserve accounting, a fixed chamber target in
matrix 33 at (-24,34,-51), held round matrix 42 / part 43, and timed open meshes
45/42. Source matrix 38 is the animated ejected case, not the chamber target:
its rest translation (-22,31,-16) would offset insertion above/behind the gun.
Both meshes use identical local vertex coordinates, but that does not make
all their animated matrix frames interchangeable. Held pose is frame 60.

Reload metadata now stores openShow/openHide. Screen single/dual timings are
19..115 and 19..59 respectively. Physical pickup opens the chamber using the
source held frame while preserving tracked hand/gun poses. Insertion readies
from frame 82, then closes at 115; cancellation closes it immediately. Tests
cover these boundaries, idle/held/ready visibility, unchanged gun and held
matrix during mechanism motion, target fit and ammo conservation. Original
GE-X texture IDs remain in use; HD texture matches have not been verified.
Headset acceptance is pending, including chamber height and supporting grip.

### Cougar — GE item 18, PD slot 17, `GmaianpistolZ`

47 matrices; fire 1030 (26 frames), whip 1031. Reload 1032, 166 frames:
open/eject at 50, six POPOUTPILLS at 80..85, held hand 53 and bullet parts
40..45 show at 92, bullets hide at 121, ammo transfer 123, close at 147.
Dual 1056 transfers 123 without showing held bullets. Six different meshes
40..45 bind the same output matrix 46 / joint 46, arranged around the cylinder
with z=0..34. Parts 10..15 are empty markers; flash part 90 uses matrices 36/37.

Next: model the six cylinder seats/round visibility and partial ammo rather
than treating all six bullets as one magazine. Decide explicit per-round pickup
versus a six-round loader with correct GE capacity and reserve accounting.
Render only occupied seats and maintain spent/remaining state through fire,
cancel, weapon switch and reload. Use actual hand contact and open-cylinder
frames; a shared matrix does not mean the bullets are one object.

### Grenade launcher — GE item 24, PD slot 23, `GdydevastatorZ`

47 matrices; fire/equip 1030; no ordinary reload script. Parts 40..45, 10..15
and 90 are empty. The first-person model contains no held grenade payload.

Next: provide a compatible existing GE grenade projectile mesh for the held
payload and derive the actual breech/insertion frame. Confirm the source's
function/ammo metadata and GE capacity. Do not reuse the Cougar bullet meshes
or invent a full-mag transfer. Define screen fallback and physical ready poses
explicitly, then test the inserted payload and grenade projectile origin.

Use `python tools/gex/gexguns.py <GE-X.z64> 17 18 23` to reproduce script data.
Inspect actual model vertices and output matrix indices; source joint IDs are
not interchangeable with matrix IDs. Extend native fixtures for each new type.

## Windows build and validation

Current workspace: `C:/Users/Occor/.codex/worktrees/37d1/goldeneye-vr`.
Local GE-X ROM: `C:/Users/Occor/Documents/other_projects/gex/gex-6a.z64`.
Expected size 33554432; MD5 `640923b68b9281044ac1b518612e979b`.
GE ROM: `C:/Users/Occor/Documents/other_projects/goldeneye-vr/007 - GoldenEye.z64`.
Analysis scripts/data under ignored `android/app/build/` are optional; production
constants, rig notes and reproducible tools above are the durable handoff.

Always set **JAVA_HOME to JDK 20 before every Gradle invocation**. Android Studio
JBR is Java 25 and fails Gradle 8.4 / AGP 8.1.2 with class version 69.

```powershell
$env:PYTHONUTF8='1'
python port/tests/test_gex_weapons.py 'C:/Users/Occor/Documents/other_projects/gex/gex-6a.z64'
python port/tests/test_gex_muzzle.py 'C:/Users/Occor/Documents/other_projects/gex/gex-6a.z64'
python port/tests/test_hand_reload.py
python port/tests/test_vr_display.py
python port/tests/test_watch_grip.py
python port/tests/test_launcher_ui.py
$env:JAVA_HOME='C:/Program Files/Java/jdk-20'
$env:ANDROID_HOME='C:/Users/Occor/AppData/Local/Android/Sdk'
$env:CCACHE_DISABLE='1'
./android/gradlew.bat -p android assembleRelease
```

All six native checks and the signed release build passed for this continuation.
The converter checks all 17 models and fit previews, mirrored/scaled hand poses,
support rotation, explicit rest anchors, scope placement and model budgets.
Physical tests cover top-mag extraction, one-round insertion, partial loaded
ammo, drop/refund, captured ammo type, switches and both handedness/world scales.
The recurring GAME_TICKRATE redefinition warning is pre-existing.

Signing/local configuration are already ignored in `android/`. APK outputs are
under `android/app/build/outputs/apk/release/`. Verify signatures with
`C:/Users/Occor/AppData/Local/Android/Sdk/build-tools/33.0.1/apksigner.bat`.
Do not commit ROMs, APKs, keystore, private INI or extracted assets. Attach any
created PR to the chat, and leave merge/release publication to the user.

Mixed historical line endings need care: preserve unchanged CRLF lines and
use LF for edited text rather than rewriting entire large C files. Run
`git diff --check` and inspect the final diff before committing. The optional
ignored `android/app/build/normalize.py` restores unchanged line bytes from HEAD.

Build label cache note: CMake configures `port/include/versioninfo.h` from Git
at configure time. Incremental Gradle builds can retain an old hash even when
new code was compiled. This delivery explicitly refreshed CMake configuration
before rebuilding and signing. For this checkout's existing release cache:

```powershell
& 'C:/Users/Occor/AppData/Local/Android/Sdk/cmake/3.22.1/bin/cmake.exe' -S . -B android/app/.cxx/RelWithDebInfo/5n53247b/arm64-v8a
```

Use the actual cache directory in another checkout, and inspect its generated
`port/include/versioninfo.h` before the final Gradle build. Keep the JDK 20
requirement above for every Gradle invocation. The earlier d3ea3b8-named local
APK had stale build-label metadata; use the cdcb44a-named APK listed above.
