# Issue #60: regular left watch arm and original primary watch hand

Resumed 2026-10-02 on `fix/60-watch-grip-hand`, with main `0e3464b` merged.

Headset feedback established that the regular left arm is correct and the
primary right hand must keep its original model, scale and orientation.
Candidate `2f7aca7` substituted the taser hand and was rejected in
`com.gevr.port-20261002-111736.jpg`. Candidate `4d952dd` restored the original
model but misidentified its sides: it hid the right palm and sleeves,
leaving only the pressing finger (`com.gevr.port-20261002-113259.jpg`).
The private renderer also never applied the firing animation to that finger.
Candidate `cbfd799` restored the palm, sleeves and finger animation. Review
(`com.gevr.port-20261002-115110.jpg`) confirmed improvement, but exposed the
smaller watch face/band still embedded in the right palm's display list.
The user also requested more forgiving close-hand snap activation.
Candidate `6212896` removed that duplicate watch and widened snap detection,
but review identified the old small sleeve still visible. There is no need
for that sleeve: the regular watch-arm renderer already supplies the arm.
The gripping copy now retains only its original palm and animated finger.

## Corrected behavior

- Keep the regular patched `Csuit_lf_handZ` left watch arm throughout grip.
  Its renderer and placement remain unchanged from the approved first candidate.
- Keep only the original `GwatchlaserZ` right gripping palm (0x0300,
  886 vertices) and pressing finger (0x0330, 104 vertices). Hide the old
  left hand (0x01c8, 566 vertices) and all six small outfit sleeve nodes,
  including their patched undersides. No cuff selection runs on this copy.
  The regular watch-arm renderer supplies the complete outfit arm.
- Validate unique palm, finger and old-left identities before hiding any
  lists. The hand-only visibility filter is independent of traversal order;
  an unexpected/ambiguous model fails without mutating its lists.
- During only the private grip-model load, omit the embedded dial, bezel
  and band triangles (textures 0x5dd-0x5e3, 0x648 and 0x809) before texture
  expansion. Keep the palm/finger geometry, matrices, vertices and their shell
  additions. Shared weapon models and the regular watch arm remain intact.
  Local ROM inspection confirms 244 watch triangles removed from 0x0300
  with all 414 right-palm skin triangles preserved; no asset was added.
- Widen the physical snap-entry radius from 10 to 16 cm and retention from
  14 to 22 cm. The existing 10 cm fingertip segment, firing gate, haptics
  and multiplayer input routing still use this same grip state. Real-world
  distances remain independent of level scale and weapon-size cheats.
- Apply the original switch-6/switch-28 finger hinge to the private model's
  attached matrix, using the game's `field_A84` press/release angle. Advance
  the angle once in the game tick when the ordinary weapon model is hidden;
  when visible, its existing update handles it. Rendering only reads the angle.
- Preserve main's primary-hand placement, scale and legacy
  `gevr_watchhand.txt` override. Both arms follow the watch controller while
  attached. Ordinary right-hand rendering outside watch grip remains main's,
  including the grenade/taser hand where appropriate.
- Each hand's shell additions attach to its own node (0x01c8 and 0x0300),
  so hiding the old left hand hides its underside patches too. All patch
  vertices, triangles, weights and textures remain unchanged.
- The rejected taser-specific `GripWatch` controls/settings remain removed.
  Saved INI values are ignored; the original `gevr_watchhand.txt` trim applies.

The snap threshold is more forgiving; laser origin, press/release animation,
handedness, ordinary two-handed holds and multiplayer handling remain intact.

## Validation

- `python port/tests/test_watch_grip.py` passes 72 wrist/finger pose cases:
  controller translation/rotation, mirrored handedness, normal/tiny/big
  sizes and 0.2/1 level scales. It checks the original primary orientation,
  scale, wrist anchor, finger rotation, independence from the free controller,
  tracking loss, and deterministic repeated rendering without advancing time.
- Visibility checks retain only the right palm and finger in nine reordered
  traversals, hide the old left node and all six sleeve primary/secondary
  lists, and reject missing or ambiguous node identities without mutation. Patch checks ensure every
  group belongs to its own source node.
- Press/release checks exercise the production angle update for watch laser
  and detonator, including its original rates/limits and gates for visible
  weapon models, non-watch items, the off hand and non-stereo mode.
- Production display-list filter checks remove watch triangles only for
  the selected private header and model name, retaining all skin/cuff/state
  commands and subsequent skin triangles. The shared watch model and regular
  watch arm retain all commands. Existing fist texture filtering still applies.
- Snap checks cover entry at 15.9 cm, retention through 21.9 cm, release at
  22.1 cm and re-entry, through controller rotation, level scales and weapon
  size cheats. Held-item/tracking loss reset grip; remote input bypass leaves
  the local grip state unchanged.
- Native hand checks pass: pickups/equipment, outfit sleeves, input routing,
  independent cycling and depletion/switching.
- Android `assembleRelease` passes with the existing release key, NDK
  25.1.8937393 and CMake 3.22.1. Build via junction `C:\gvr60` to this worktree.
  The final signed build follows the source commit so its displayed hash
  identifies the changes. Version remains 0.3.9 / versionCode 49 / protocol 15.
  Candidate delivery involved no headset installation or publication.

## Headset acceptance

On 2026-10-02, the maintainer accepted signed candidate `57f00e5` ("looks
good") and requested merging into main, pushing, and closing #60 and #75
with comments. This acceptance covers the reviewed watch-arm/grip changes;
this continuation does not introduce a separate crosshair/auto-crouch change.

## Regression checklist

1. Bring hands together with the watch laser selected: activation should
   be easier. Verify one watch face/band, the original right palm and pressing finger,
   and the approved regular left arm. No smaller old sleeve or its underside
   patches should appear over that arm, in any outfit.
2. Fire/release repeatedly: the index finger must press the watch and return.
   Repeat with the detonator; verify beam origin, firing gate and haptics.
3. Move/rotate the watch and grip/release repeatedly. Check steady attachment,
   unchanged arm sizes, restored controller following on release and undersides.
4. Repeat with mirrored handedness, outfits, weapon switches, watch-menu
   opening/closing and stage changes. Check ordinary grenades and two-hand holds.
