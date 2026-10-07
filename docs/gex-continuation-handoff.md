# GE-X continuation handoff — 2026-10-06

Start here, then read [the roadmap](gex-weapon-roadmap.md) for the history of
PP7 grip, wrist orientation, duplicate arms, insertion targets and installed
magazine fitting. This file records the current implementation and remaining work.

## Bonus PP7s, knives, throwables, Gun hand fit — 2026-10-06, night

Branch `codex/gex-cougar-grenade` (local, not pushed). Latest test APK
`GoldenEyeVR-code65-throwables-6bd194d.apk`, label **6bd194d**, 0.4.11 / code 65,
SHA256 `71ddbc10cbd6f2f6cdfdf27db3597bcacdb7a75002a8380a495e9f604330fa29`.
Headset acceptance of this batch is **pending**; Cougar, grenade launcher (with Gun
hand), crosshair option and fits up to 3d5b733 were accepted/fitted by the user.

- **Gun hand fit (1034239).** Ninth X mode (GE-X guns): moves GE-X's right hand
  joints 1..16 on the gun (cm) and, holding the gun-hand grip, turns them about the
  palm. Fit components 7/8; table is `[64][9][3]`. Needed because the launcher reuses
  the Cougar rig; its baked hand is (0, 1.995, 9.35) cm.
- **Bonus PP7s (9ee6c15).** GE-X draws silver/gold PP7 as PD's DY357 / DY357-LX on
  the PP7's own rig (same scripts, clips and magazine). `PP7_RIG` macro; they share
  the PP7 family's fits (`pp7()`), the underside grab rule, no GexFit20/21 rows.
- **Knives (dbfb6e5).** `GknifeZ` for knife and throwing knife (fits shared, item 2).
  Blade runs along the joint's x, so `screenFromRoot` keeps PD's own first-person pose
  in screen mode with the palm placed on the PP7's. No fire clip: GoldenEye's swing and
  throw move it (GE-X's would move it twice; its 1029 ends in the throwing grip).
- **Grenade and mines (6bd194d).** `GgrenadeZ` (root-anchored), timed/proximity/
  remote mines (GoldenEye's own meshes, every vertex matches). Idle clips raise the
  hand from below, so each use clip's frame 0 is the rest. GoldenEye hides these in the
  hand and stereo drew them as gadgets in a fist; GE-X items without an ammo payload
  now stay shown (gunfire.c), the gadget table/fist stand down (`gevrStereoItemShown`),
  and a spent item hides alone (part 100, or `spentMatrix`) so the GE-X hand stays.
  The remote mine's PD detonator (matrix 34) is collapsed (`hideMatrix`).

Headset checks: knife idle hold, stab by swing, throwing knife throw and redraw;
grenade hold/throw/next grenade, hand stays empty (no GE fist) between throws; each
mine's hold, throw/place, stick, re-grab, remote detonation; left-hand gadget mines;
screen mode for all; Gun hand mode on these too. Fit each and bake as before.

## Cougar, grenade launcher and switch blink — 2026-10-06, late

Branch `codex/gex-cougar-grenade` from Main `2dfbc84` (PRs #138/#139/#140 merged).
Version unchanged, **0.4.11 / code 65**. Signed test APK
`android/app/build/outputs/apk/release/GoldenEyeVR-code65-cougar-grenade-90f5a59.apk`,
label **90f5a59**, SHA256 `3dcee71edefc30baa76110dbd7e50560ed68a06cf8bb3587dc49cc574af97ef1`.
Main's own build is `GoldenEyeVR-code65-main-2dfbc84.apk`. Not pushed; no PR yet.
Headset acceptance of everything below is **pending**.

- **Arm blink on weapon switch (fixed, 191c41a).** Loading a GE-X gun cleared the
  off hand's cached empty pose; the hidden switch frames then fell back to GE's
  watch arm until the raised gun was drawn. The cache is now marked stale (it
  holds matrices only), the last empty hand keeps drawing, and it is rebuilt as
  soon as the new gun loads. Harness test: stale cache keeps drawing, same-address
  reload rebuilds.
- **Cougar (item 18, slot 17, `GmaianpistolZ`, 716ad55).** Body = GE `GrugerZ` +
  (0,50,42). Parts 40..45 on matrix 46 are a six-round **speedloader**, not cylinder
  seats: source shows them 92..121 in reload 1032 (ammo 123, closed 147) and never
  draws rounds in the cylinder. New `loaderRounds=6`: a belt pickup reserves
  min(capacity − loaded, reserve) rounds, insertion adds them all, release/tracking
  loss refunds them all; one bullet drawn per round held (also while falling). The
  cylinder (matrices 34, 39..45, still from 80 to 135) stands open at hold frame
  100 while held; target is the ring centre on the rear face (42.2,14.2,41.7).
- **Grenade launcher (item 24, slot 23, `GdydevastatorZ`, 90f5a59).** Body = GE
  `GgrenadelaunchZ` + (0,−24,118), 108/108. Cougar skeleton, drum on matrix 34,
  **no reload clip and no round mesh**. New `holdAnim` (Cougar 1032 @100 poses the
  hand on joint 46) and `payloadProp/payloadScale` (GE `PchrgrenaderoundZ`, nose +z,
  drawn on joint 46 at 0.385 ≈ 40 mm, loaded with the gun). One round per insertion
  into the drum's bottom chamber from behind (0,−25,42.3): the frame covers the
  drum's upper rear; chambers ring 47 units out. The 1032 drum "open" is only a
  38-unit sideways slide, so it is deliberately not used. Screen reload keeps GE's
  tilt (no clip); no receiver ready motion.
- Initial fits: computed gun placement for both; support/rotation copied from the
  fitted Golden Gun. Fit both in the headset and bake as before.

Headset checks: switch rapidly between GE-X guns and to/from non-GE-X items with arms
on (no GE arm flash); Cougar partial/empty reload, loader count visibly matching rounds,
cylinder open while held and closing after insertion, drop/refund; grenade launcher
held round size/orientation in the hand, chamber target comfort, six insertions,
projectile origin, screen-mode reload. Both handedness and size modes.

Remaining tracks: bonus PP7s (silver/gold; keep original appearance until verified),
knives and throwables. All ordinary guns now have GE-X models (19 variants).

## Latest headset session — resolved (2026-10-06, evening)

Current test APK: `android/app/build/outputs/apk/release/GoldenEyeVR-code65-fits-1100498.apk`,
build label **1100498**, still **0.4.11 / code 65** (version deliberately unchanged).
apksigner verifies; SHA256 `d364ad29142375507977fbab287da3eac71d88c9a30175db240f591b4687a439`.
PRs #140 and #139 were merged to main (6d246d6, d6ea0f7); main is merged into
this branch and PR #138 is pushed with these changes.

- **Rocket seated payload — fixed, headset-accepted.** GE draws a loaded rocket as
  its own prop (`hand->rocket`, gun.c `gunUpdateAttachedRocket`), created only by
  `currentPlayerCreateRocket` inside GE's reload ammo move `sub_GAME_7F0649D8`
  (equip and the gun-to-belt fallback use it). Physical insertion in
  `gevrGexRoundTick` incremented ammo directly, so no rocket was drawn. It now
  calls `currentPlayerCreateRocket` for the rocket. GE-X part 40 stays the held
  payload only. `hand_reload_native.c` requires exactly one creation on rocket
  insertion and none for shells/Golden Gun (fails on the old source).
- **Sniper/laser scope sight — closed, not a bug.** Change-only diagnostics log
  `aimsrc:` (input aim sources) and `aimstate:` (GE aim state) to logcat. The
  session showed two-handed laser + gun grip aiming with sight shown, sniper
  off-hand grips going to reload, and Gun fit mode stripping R_TRIG (the user had
  fit mode on when the laser sight did not appear). The off-hand trigger also
  aims in single-gun stereo by design; that is the likely sniper explanation.
  The user could not reproduce either and asked not to pursue them.
- **Fits baked** from the headset INI into `vr_settings_defaults.c`: Klobb (7),
  ZMG (9), Phantom installed mesh (12/6), sniper (17), Golden Gun (19), laser (22),
  rocket (25), plus `VrGexHeldMag`, `VrReloadBelt` and GE long-gun `VrGripTrim[1]`.
- **Statue Park** (#140) draws correctly in the headset; stage pool still had
  ~12 MB free after load with `-ma800`.
- **Merged PRs:** #140 (Statue pop-in) and #139 (crash detection, multiplayer ROM
  gate) merged locally. #139 did not compile on Android (`disconnect()` used
  above its lambda); the fix `d1a7b95` was pushed to #139's branch and is also
  on this branch as `9a29241`. Both PRs are now merged to main and main is
  merged here, so #138's diff contains only GE-X work.

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
