# Changes after v0.4.6

These changes are in the current source and are not a newly published APK
release. v0.4.6 remains the release baseline (versionCode 59, protocol 18).
Use a build containing these changes; use the same APK version on every
headset for multiplayer. No game data is shipped.

## GoldenEye X assets and VR controls — PR #117

- Optional **GE-X 6a KF7** model, with its fire/reload animations on the
  virtual screen, loaded from a player's patched Perfect Dark USA v1.1 ROM.
  Matching texture IDs use the existing HD packs. Other weapons, maps and
  the GE-X campaign are outside current support.
- **Hand reload** for the GE-X KF7 in stereo: removable magazines retain
  rounds, dropped rounds return to reserve, replacements come from the belt,
  and either gun can reload directly at the belt when dual-wielding. B/Y
  ejects a magazine; release grip after seating it before the next grab.
- Optional **GE-X arms wearing GoldenEye's watch**, including live wrist
  status and the pause watch. Both GE-X toggles are off by default.
- **Gun fit** keeps separate GE-X gun/grip/scope values, adds reload grab
  and belt points, off-hand palm and watch adjustment, and the GE-X
  supporting-hand grip point. X cycles the available fit modes.
- The **watch VR settings** now group the live options into Comfort,
  Controls, Gestures, Weapons, Display, Game rules, and Mods and fun.
  These include handedness, stick swapping, gestures, weapons, GE-X,
  screen size/distance/curve/passthrough and an immediate refresh-rate
  request. Texture-pack selection stays in the launcher; the existing
  stick-click gesture changes play mode.

Start with [GE-X setup and ROM preparation](../gex-setup.md). The
[engineering notes](../gex-weapons.md) describe the loader and model conversion.

## Co-op gun drops — PR #115

Dead guards now detach and activate their dropped hand weapons on joining
headsets, instead of leaving guns attached to the body. Pickups remain
individual: collecting your copy does not take another player's copy away.
Live guard weapon changes and protocol 18 are preserved. Fixes
[issue #114](https://github.com/MrSco/goldeneye-vr/issues/114).
See [co-op investigation and acceptance checks](../issue-94-coop.md).

## Co-op mission Statistics crash — PR #116

A joiner can receive the host's mission start before selecting a local save
folder. The debrief now preserves a chosen folder, or selects the first
local folder if the picker was bypassed. Missing saves return no recorded
best time instead of crashing Statistics. This fixes the crash in report
`a16b05b5`; the reported invisible Bunker ending cutscene remains unresolved.
See [the investigation](../crash-a16b05b5-coop-debrief.md).

## Quest 2 Facility frame-buffer crash — PR #118

Facility reports `4bde84c2` and `76717e5d` exceeded the old command-buffer
capacity, overwriting adjacent frame data and aborting the renderer. Frame
buffers now have larger command and auxiliary budgets, reject invalid or
exhausted auxiliary allocations, validate completed lists before submission,
and log capacities and usage peaks. This uses about 1.13 MiB more of
Facility's existing game heap; body-retention settings remain available.
Individual command writes are still unchecked. See
[the investigation](../crash-4bde84c2.md).

## Validation and remaining device checks

The combined source passes the 63 multiplayer native tests, seven frame-pool
regression groups, co-op debrief regression (including failures reproduced
against v0.4.6), watch-hand, ammo/input/wrist and pause UI checks, and
Quest/desktop display and settings round trips, including separate GE-X fits.
The combined Android ARM64 debug APK builds successfully with
`android/gradlew.bat :app:assembleDebug --console=plain`. It has not been
installed or played on a headset as a combined build.

Existing GE-X headset play covered the KF7, magazines, two-handed holds,
fit modes, arms and pause watch on the Dam. These checks remain pending:

- Two-headset co-op gun drops from bullets/explosions, off-screen drops,
  falling physics and independent weapon/ammo awards without reappearing drops.
- Sustained Facility combat on **Quest 2** with 48 bodies retained, HD
  textures, detailed guns and wrist status.
- Refresh-rate changes and left-handed mode changed mid-mission from the watch.
- The reported invisible ending cutscene on the co-op joiner.
