# Oddjob's hat (Mods)

Throwing knives look like Oddjob's hat: in Bond's hand, while they fly
(spinning flat), and where they lie. Only the look changes. Damage, hits,
pickups and the network still treat them as throwing knives. Each headset
keeps its own setting.

The hat is 007 Nightfire's (GameCube). It is not in the APK: the launcher's
Mods page downloads it as the `oddjob-hat` pack ("Get Oddjob's hat", on the
blood patch's row) from GoldenEye VR's mods repository
(github.com/MrSco/goldeneye-vr-mods, release `oddjob-hat-<version>`, asset
`oddjob-hat-<version>.zip` holding `oddjob_hat.bin`) into
`files/texture-packs/oddjob-hat/`. Then the row is a checkbox (plus Remove,
and Update when the repository has a newer `oddjob-hat-` release). The watch's
VR settings, Mods and fun > Oddjob's hat, turns it on and off in game. The ini
key is `OddjobHat`, on by default; it does nothing until the pack is there.

## Making the pack

1. Capture a frame in Dolphin with 3DReaperDX while Oddjob's hat is in view.
   You get `frame_*.obj`, its `.mtl` and the `.dds` textures.
2. Run `python tools/gevr_oddjob_hat.py <frame.obj> build/oddjob_hat`. The
   script finds the view model by its textures; `--calls 538,539,540` names
   the crown, band and arms draw calls if that fails. It writes
   `oddjob_hat.bin` for the game, plus `oddjob_hat.obj` (the hat and both
   arms) for Blender.
3. Zip `oddjob_hat.bin` as `oddjob-hat-<version>.zip`, publish it as release
   `oddjob-hat-<version>` in the mods repository, and update the `oddjob-hat`
   entry in `ModManager.java` (version, URL, exact size). A newer release
   reaches installed apps as an Update without an app release.

For testing, `files/mods/oddjob_hat.bin` (adb push) is read when no pack is
installed.

## How it works

`port/src/gevr_oddjobhat.c` swaps lists at draw time, the same way Bean
does, and it runs ahead of Bean.

| Model | Nodes | Drawn as |
|---|---|---|
| `GthrowknifeZ` (held) | blade, guard and handle (149, 126 and 60 vertices) | the hat, in the blade's node; the hand's lists stay with whoever draws them |
| `GthrowknifeZ` (GoldenEye X) | its one list (347 vertices: the file is the knife alone) | the hat on the blade's matrix (33, `KNIFE_DEF`), at GE-X's hand's palm |
| `PchrknifeZ` (thrown) | the knife (39 vertices) | the hat, only for instances that `generate_player_thrown_knife` registered, so hunting knives stay knives |
| `PchrthrowknifeZ` (pickup) | the knife (39 vertices) | the hat |

The models are found by their vertex counts. If another ROM's model doesn't
match, the hat isn't drawn and the log names the file. Only the knife's lists
are the hat's, so each look keeps its own hand:

- Original: Bond's N64 hand and sleeves.
- Bean: Bean puts its knife on a knife list and its glove on the hand's list
  (`applyMesh`); the hat takes the knife lists (`activeSwap` stands aside for
  them) and the glove stays. Dab's Mod draws the XBLA knife's hand where
  GoldenEye has it, so the N64 fist fits.
- GoldenEye X: the converted file is Perfect Dark's combat knife alone; GE-X's
  hand is its own model on the gun's matrices. The hat hangs on the knife's
  matrix, so it follows the knife's fit and GoldenEye's swing and throw. Its
  fist is measured on the first frame it is drawn in: the right wrist's
  matrix (2) with the palm 50 units along it, and the hand's forearm, in the
  knife's matrix space, with the blade's end that points up as the handle.

The lists keep the node's render mode: types 3 and 4 give texel times shade
when the shade alpha is full. That means the hat is fogged and depth-tested
like the hand next to it. Its lighting is baked into vertex colours by the
converter. Its pictures load through 16x16 stand-ins that fast3d resolves
with `gevrHatTexture`, alongside Bean's.

Held placement copies Oddjob's grip:
- The converter measures his right fist under the brim: the fist centre, the
  forearm direction, and the direction from the fist to the brim, all in the
  hat's own space.
- The runtime maps that frame onto Bond's knife fist: the fist centre on the
  knife's axis, the forearm, and the knife's axis towards the handle. These
  were measured on the cartridge model.
- The brim radius is 133 model units, about 3.3 hand widths, which suits a
  30 cm bowler.

The thrown prop sits brim-centred on the model's origin with its crown on +Y.
That is the axis a thrown knife spins about, so the hat flies flat. Its brim
radius is 200 units.

## Capture notes

- 3DReaperDX unprojected X with the window's 4:3 aspect while the game drew
  16:9, so the hat band is round only after X is scaled by 4/3. Y comes from
  the game's projection.
- Nothing in the capture fixes the height (see `height` below).
- Every scene draw call appears twice, once per eye of Dolphin's
  side-by-side view. The arms appear once more, in a second pose, off
  screen.
- The view model is drawn after the depth clear (a full-screen quad), just
  before the HUD.

## Live tuning

`files/gevr_oddjobhat.txt` is read again whenever it changes. Units are model
units and degrees:

```
held <brim radius> <dx> <dy> <dz> <yaw> <pitch> <roll>
gex  <brim radius cm> <dx> <dy> <dz> <yaw> <pitch> <roll>
prop <brim radius> <dx> <dy> <dz> <pitch>
height <times>
```

Defaults: held 133 (about 3.3 of Bond's hand widths, a 30 cm bowler), gex
15 cm (converted through the knife matrix's own scale, which GE-X's item fit
changes, and the guns' size; sized in GE-X's hand units it was drawn far
bigger than the cartridge hat), prop 200, height 0.55. The capture fixes
the hat's width against its depth (the band is round) but not its height:
as captured the crown is a top hat's, 1.2 times as tall as it is wide. The
game draws it at 0.55 of that, a bowler's 0.6 crown (still flat-topped:
Nightfire's crown is a cylinder, not a dome).

Held and gex rotations turn about the fist: yaw about the knife's axis (180 moves the
hat to the other side of the hand), pitch across the hand, roll about the
forearm.
