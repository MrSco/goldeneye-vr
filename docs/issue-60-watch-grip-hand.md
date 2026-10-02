# Issue #60: separate watch and gripping arms

Resumed 2026-10-02 on `fix/60-watch-grip-hand`, merging main `0e3464b`.
The earlier wrist-lock commit `7a07ebb` had been built but never installed
or tested on the headset. This continuation carries that attachment forward
with main's hand-shell patches, control changes, haptics and multiplayer fixes.

## Behavior

The watch arm remains the regular `Csuit_lf_handZ`. While the watch laser or
detonator is gripped, the normal `GtaserZ` hand and sleeve attach to the watch
wrist frame, with the taser geometry hidden. The model scale is the same as
the existing grenade/two-handed gripping hand. Rotation and translation put
the palm at the watch face plus its configured offset. The watch arm does
not change model or size when the grip engages.

Both arms use the watch controller's rendering tag while attached, including
XR frames between game frames. Releasing restores the gun hand's own
controller placement and tag. Existing proximity/hysteresis detection,
firing rules, laser origin, handedness, outfit selection and haptics remain.
The old private two-arm `GwatchlaserZ` loader and drawing path are removed.
If the regular gripping model fails to load, the watch arm stays visible;
the open fist is not incorrectly attached using a different model's palm.

`GripWatch` in `goldeneye-vr.ini` stores six values: centimetres along the
arm, out of the watch face and across the thumb direction, then X/Y/Z
rotation in degrees. Missing or incomplete values keep the defaults:
`0 -5 0 90 0 0`. Existing saved values are retained.

In the launcher's Gun fit mode, bring the gun hand to the watch to show
WATCH GRIP FIT. The move stick changes along/across; the turn stick changes
out/in and tilt. Hold the gun hand's grip to adjust roll instead of tilt.
A keeps and saves the pose; B restores the values from the start of fitting.
These are logical gun/off-hand controls, including left-handed mode.

## Validation

- `python port/tests/test_watch_grip.py` passes 72 cases using the production
  gun matrix, watch face frame, attachment and rotation functions: wrist
  rotation/translation, mirrored handedness, normal/tiny/big scale factors
  and 0.2/1 level scales. It checks unchanged scale, perpendicular axes,
  reflection sign, palm alignment, independence from the free controller,
  release placement, tracking loss and actual GripWatch save/load code.
- Native hand checks pass for pickup/equipment, character sleeves, input
  routing, independent weapon cycling and depletion/switching. Two stale
  fixture expectations also failed on untouched main: they still counted
  the left panel's duplicate ITEM_FIST/Unarmed entry, removed on main by
  `68a8274`. The fixture now expects the single Holstered entry and checks
  that ITEM_FIST is absent from the left list.
- Android `assembleRelease` passes using NDK 25.1.8937393, CMake 3.22.1 and
  the existing release key; APK v2 signature verification passes. Builds use
  the short junction `C:\gvr60` pointing to this worktree to avoid Windows
  path limits. The final build follows the source commit so its launcher
  hash identifies the candidate. Version remains 0.3.9 / versionCode 49 /
  protocol 15; this is a test candidate, not a published release.
- Headset validation remains pending. This branch has not been installed,
  merged into main or published.

## Headset acceptance

1. Equip the watch laser. Grip and release repeatedly: the watch arm must
   retain its size and the normal gripping hand must snap to the wrist.
2. Turn/move the watch arm while gripped; move the free controller inside
   the release threshold. The attached hand must remain locked to the
   watch, including between game frames. Move away to release.
3. Fire the laser and use the detonator. Confirm the beam origin and watch
   arm rumble, and that firing still requires the hand near the watch.
4. Repeat in left-handed mode and with different outfits; inspect both
   arms' undersides, cuffs and the contact pose for clipping.
5. Set Watch Grip Fit, keep it with A, restart and confirm persistence.
   Change the pose again and undo with B; the saved pose must return.
6. Switch weapons, open/close the watch menu and change stages. Confirm
   normal grenade gripping and handgun/long-gun two-handed holds still work.
