# Issue #60: keep the regular left arm and original primary watch hand

Resumed 2026-10-02 on `fix/60-watch-grip-hand`, with main `0e3464b` merged.

The first candidate `2f7aca7` retained the regular left watch arm but used
GtaserZ for the right grip, with a new 90-degree default rotation. Headset
review (`com.gevr.port-20261002-111736.jpg`) confirmed the left arm looked
correct; the user rejected the upside-down, different right-hand model and
asked to preserve the original primary hand.

## Corrected behavior

- Keep the regular patched `Csuit_lf_handZ` left watch arm throughout grip.
  Its renderer is unchanged from main and the approved first candidate.
- Restore the original right hand/forearm and pressing finger from
  `GwatchlaserZ`: display lists 0 and 8. Lists 1-6 (old left sleeves) and
  7 (old left fist/watch) are hidden in the private gripping-hand copy.
- Preserve main's right-hand placement, scale, finger setup and legacy
  `gevr_watchhand.txt` override exactly. Both arms follow the watch controller
  while attached. Outside watch grip, ordinary right-hand rendering is
  restored exactly to main, including the grenade/taser hand where appropriate.
- Shell patches for the two hands now attach to their own nodes (0x01c8 and
  0x0300). Previously left-hand additions were appended to the right-hand
  node; hiding only the original left geometry would leave those faces
  floating. All patch vertices, triangles, weights and textures are unchanged.
  The recipe and generated C data agree with the separated patch hosts.
- Remove the rejected taser-specific `GripWatch` controls/settings from this
  continuation. Existing values in the INI are ignored, so a saved 90-degree
  taser rotation cannot change the restored primary watch hand. The original
  `gevr_watchhand.txt` trim continues to work as on main.

Grip detection/hysteresis, firing rules, laser origin, watch-arm haptics,
handedness and multiplayer behavior remain unchanged.

## Validation

- `python port/tests/test_watch_grip.py` verifies the original canonical
  primary-hand orientation, 72 wrist attachment cases (rotation, translation,
  mirrored handedness, normal/tiny/big sizes and 0.2/1 level scales), stable
  scale and watch anchor, independence from the free controller, tracking
  loss and hiding only the old left display lists. It checks that every
  patch group belongs to its own source node.
- Compared all watch patch groups before/after: triangles, vertices and
  weights are preserved exactly; only their hosts/grouping change.
- Compared production functions against main: ordinary right-fist rendering,
  original watch-hand rendering/placement and regular left-arm rendering
  are identical. Only watch-model loading hides the old left side.
- Native hand checks pass for pickups/equipment, outfit sleeves, input
  routing, independent cycling and depletion/switching. Their prior stale
  duplicate-left-Unarmed expectations were corrected in `2f7aca7`.
- Android `assembleRelease` passes with the existing release key, NDK
  25.1.8937393 and CMake 3.22.1. The short junction `C:\gvr60` points to this
  worktree for building. The final build follows the source commit so the
  launcher identifies the candidate. Version remains 0.3.9 / versionCode 49 /
  protocol 15. No headset installation, main merge or publication was done.

## Headset recheck

1. Grip the watch laser: confirm the left arm still looks like the approved
   screenshot and the right hand matches the original model and orientation.
2. Inspect the contact pose and undersides: no extra left sleeve/watch or
   floating shell faces should remain from the old combined model.
3. Rotate/move the watch and release repeatedly. The right hand must stay
   attached during grip and return to its own controller on release.
4. Fire the laser and use the detonator; check original beam origin,
   proximity firing gate and watch-arm rumble.
5. Repeat with mirrored handedness, different outfits, weapon switches,
   watch-menu opening/closing and stage changes. Check ordinary grenade
   gripping and handgun/long-gun two-handed holds.
