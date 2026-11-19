# Project Bean visuals (experimental)

Released in v0.4.20 after headset play on Quest 3 (2026-10-12): the import,
characters, guns and their moving parts, the watch and weapon wheel, the HUD
and menus, Bean's holding arm and Gun fit. Some material effects (Dam and
Complex water, Silo and Control floors) are still simplified.

An earlier candidate, build `0bf6179`, was withdrawn after report `a2167162`
(a Dam crash and incorrect model/texture rendering). The crash was in
`bgTestHitOnObj` during stereo aiming: the importer had
replaced native segmented display-list pointers with Bean heap pointers.
Replacements are now selected only during rendering. Native pointers, model
types and collision geometry remain intact. Model reloads also discard old
registrations, including different intro headers sharing one buffer.

The source comparison additionally found a missing decoded-image row
reversal, missing DAB magenta-placeholder handling, and fog/unset vertex alpha
being used as opacity. These have regression coverage and source fixes.
Bean diagnostics now use the game logger so exported reports include asset
names and replacement counts. See the [runtime audit](project-bean-runtime-audit.md) for evidence and the
remaining DAB paths required for mapping acceptance.

GoldenEye VR can load characters, heads, weapons, props, rooms and skies from
the player's own extracted Project Bean files. No game files ship in the APK.
The GoldenEye USA ROM remains required; gameplay, collision and animation
remain the original game's.

Nothing of the release is downloaded. Copy your own GoldenEye XBLA to the
headset (its `.7z`, a `.zip`, or the Xbox 360 package file), open **Mods** in
the launcher and press **Choose copy...** beside **XBLA / Bean**, then pick
it. The app keeps only the 835 files Bean draws (meshes, the two font atlases
and the release's pictures, about 360 MB) in
`Android/data/com.gevr.port/files/texture-packs/ge007-bean`; the copy itself
is read in place and can be deleted afterwards. A zip holding the package
(the retail copy) needs about 1.2 GB free while it is read. Select Bean under
**Visuals** alongside Original, HD and HD + AI. This is Dab's Mod's import
(`gebean.c`, `archive.c`, `x360.c`); see `port/src/gevr_bean_import.c`.

Bean uses only its own CAFF textures and the release's pictures. Missing
pictures use the original game textures, as in Dab's Mod (its explosions are
the cartridge's); HD / AI and legacy texture packs cannot fill those gaps. Switching during a level clears previous texture uploads and caches.
With GoldenEye X enabled, its first-person guns, arms, watch and hand-reload rig
keep priority and their own ROM textures. Bean supplies the other models and rooms.
With it off, Bean's magazine-fed guns have a magazine to take out by hand (below).

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

## Removable magazines (hand reload, WIP)

With GoldenEye X off, Bean on and **Hand reload WIP** on (Controls → Gestures), a
Bean gun that has a magazine reloads by hand through the same states as GE-X's:
B / Y drops the magazine out, take a fresh one at the belt, bring its top to the
well. A pistol's magazine is not pulled out by hand, GE-X's or Bean's: it is
inside the grip, with too little showing to take hold of. Rifles and submachine
guns, whose magazine hangs out of the gun, keep the pull (`bondview2.c
gevrReloadMagPulls`), so a Bean rifle added to the table gets it too.
A touch of the gun hand at the belt still loads it one-handed. Every other
Bean gun keeps the game's own reload, B and all.

| Gun | Magazine | Out by |
| --- | --- | --- |
| PP7, silenced, silver, gold (one grip) | cut from the grip | B / Y |
| DD44 | cut from the grip | B / Y |
| Klobb, KF7, ZMG, D5K, silenced D5K, Phantom, AR33 | a piece of the mesh on its own | pull down, or B / Y |
| RC-P90 | the see-through one on top, with its rounds | lift up, or B / Y |

The sniper rifle's mesh shows no magazine, so it keeps the game's reload (GE-X's
has one). The shotguns, launchers, Cougar and Golden Gun load a round at a time
on GE-X; Bean has no loose rounds for them yet.

The release's guns are one skinned mesh each with no magazine bone, and its
dropped-magazine props are a 28-triangle wedge, so the magazine is made from the
gun when the gun is laid out (`port/src/gevr_beanmag.c`, `gevr_bean.c`
`gunMagazine()`):

- **The foot** is the gun's own triangles below a plane across the grip (the
  PP7's floor plate and finger rest), cut along it. Triangles that straddle the
  plane are split, with their picture, so nothing is lost or doubled.
- **The body** is a plain dark prism up the well from that plane: the grip's own
  outline there, drawn in. The well's direction is read from the grip's outline
  at two heights, so it follows the grip's lean.
- **The gun without it** is everything else plus a dark floor a little way up the
  empty well. The guns draw two-sided, so the grip's inner walls show round it.

That is a pistol's. A rifle's or a submachine gun's magazine hangs out of the
gun, and in the release's meshes it is a piece on its own: it shares no corner
with the rest of the gun. There the magazine is every such piece wholly inside
a box round it, taken whole, with a lid just under its top; the gun without it
is floored where it went in. Nothing is cut and no body is made. It goes in the
way its own top third runs, so a curved or leaning magazine seats along itself.

The gun's body list then has three builds on the same matrix: whole (the
release's own, drawn while the magazine is in), open, and the magazine alone.
`gun.c gevrBeanMagDraw` picks per draw (`gevrBeanSetMagazine`), and draws the
magazine by itself under another matrix: the off hand's, or a falling one's.
It lies in the gun's space, so under the gun's own matrix it is seated. In the
hand it is turned as the gun would be in that hand; held alike, it lines up
with the well. The hands are the player's own. Nothing is animated.

**The slide** needs no code of its own. GoldenEye's slide clock already holds the
slide back while the magazine is empty and lets it home once it holds rounds
(`gun.c sub_GAME_7F05E83C`), and the release's slide rides GoldenEye's matrix.
With hand reload the gun stays empty until a magazine seats, so the PP7 locks
open on its last round and closes on a loaded magazine, as GE-X's does.

**Gun fit** (launcher or in-game): X steps to **held ammo** and **ammo
insertion** for a Bean magazine too. They move the magazine in the off hand and
the well's mouth on the gun; the off trigger in the insertion mode puts the
mouth at the held magazine's top. Saved per gun as `BeanMagFit<item>_<0|1>`,
the PP7s sharing item 4's. Unfitted, both are where the mesh has them.

### Adding a gun

1. Add a row in `gevr_beanmag.c` for the gun's file, then draw it:
   `python tools/gevr_bean_mag_preview.py <xbla files> <out dir> gun/<name>`
   (no gun named: the whole table).
   - A magazine that is a piece of its own: `ISLAND_MAGAZINE` with a box that
     holds all of it and no other whole piece, and how much of its top is inside
     the gun.
   - One inside the grip: copy the PP7's row and move its plane (unit normal up
     the well; `n.p + d >= 0` stays on the gun) and box.

   The picture should show the magazine blue, what was made for it (body, lid,
   the well's floor) orange, and the hand whole.
2. Name its item in `gun.c gevrBeanMagItem` (and `gevrBeanMagFit` if it shares
   another's fits).
3. `python port/tests/test_bean_magazine.py <xbla files>` and
   `python port/tests/test_hand_reload.py`; with the ROM,
   `python port/tests/test_bean_mapping.py <ROM> <xbla files>` prints a
   `MAGAZINE` line for it.

A pistol's item also needs no more than that: `bondview2.c gevrReloadMagPulls`
goes by the weapon wheel's pistols.

### Not done yet

- Checked in the headset: the PP7 (2026-11-18), whose held place is now every
  gun's default. Not yet: the DD44, the rifles and submachine guns, and the
  RC-P90 - where each magazine sits in the hand, the pull (taken at the
  magazine's middle as drawn, within 10 cm and nearer than the two-handed hold),
  the lids and floors, and the RC-P90's see-through magazine drawn on its own.
- The magazine at the belt is drawn untinted, and there is no ghost in the well
  (GE-X has both).
- Nothing is sent to other players: the magazine's state is local, as GE-X's.

### Path to a manual slide rack (both families)

Not built. GE-X has none either: its slide cycles, locks open and closes on its
own. The plan keeps GoldenEye's `field_A88` as the one slide position for both
model sets, so the mesh side is already shared (GE-X: part 0x33's matrix,
`gevrGexSlideTo`; Bean: `Switches[7]`).

1. **A chambered flag per gun hand**, beside the magazine's state in
   `bondview2.c`. A shot chambers the next round while the magazine has one; the
   last shot leaves the chamber empty and the slide back.
2. **The clock waits for the hand.** `sub_GAME_7F05E83C` returns the slide while
   `weapon_ammo_in_magazine > 0`; with manual rack on it also needs the flag, so
   a magazine seated on an empty chamber leaves the slide locked.
3. **The grab.** A point at the slide's rear per gun (GE-X: a `GexWeaponDef`
   field in the slide matrix's space; Bean: a field in `GevrBeanMagDef`'s row).
   The off hand's fresh grip within reach owns the slide until let go, ahead of
   the magazine and the two-handed hold (`gevrGexClaimsOffHand` is where that
   order is decided). While held, `field_A88` is the hand's pull along the
   slide's own z, clamped to `BoltRecoilBack`.
4. **The rack.** Pulled past most of its travel and let go, the slide runs home
   on the existing clock and sets the flag. Let go short of that, it returns
   without chambering. A release button can do the same from locked.
5. **Rules.** A reload with a round still chambered needs no rack. Racking a
   loaded gun throws a round away (reserve unchanged, magazine less one), or is
   refused; decide in the headset.
6. **Around it.** A `RACK THE SLIDE` label (`gevr_hand_reload.h`), a Gun fit mode
   for the grab point, its own toggle (off by default, under Hand reload), and
   `test_slide_lock.py` extended to the held slide and the flag.

Only guns with a slide part and a bolt stat take part: the PP7s and the DD44.
Rifle bolts and charging handles are the same mechanism with another point and
axis.

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
