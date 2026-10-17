# Oddjob's hat (Mods)

Throwing knives look like Oddjob's hat: in Bond's hand, while they fly
(spinning flat), and where they lie. Only the look changes. Damage, hits,
pickups and the network still treat them as throwing knives. Each headset
keeps its own setting.

The hat is from 007 Nightfire (GameCube), so nothing of it is in this
repository or the APK. Players make the file from their own capture.

1. Capture a frame in Dolphin with 3DReaperDX while Oddjob's hat is in view.
   You get `frame_*.obj`, its `.mtl` and the `.dds` textures.
2. Run `python tools/gevr_oddjob_hat.py <frame.obj> build/oddjob_hat`. The
   script finds the view model by its textures; `--calls 538,539,540` names
   the crown, band and arms draw calls if that fails. It writes
   `oddjob_hat.bin` for the game, plus `oddjob_hat.obj` (the hat and both
   arms) for Blender.
3. Run `adb push build/oddjob_hat/oddjob_hat.bin /sdcard/Android/data/com.gevr.port/files/mods/oddjob_hat.bin`.
4. Turn it on and off with launcher Mods > "Oddjob's hat" (next to the blood
   patch) or the watch's VR settings, Mods and fun > Oddjob's hat. The ini key
   is `OddjobHat`. It is on by default, but has no effect until the file is
   present.

## How it works

`port/src/gevr_oddjobhat.c` swaps lists at draw time, the same way Bean
does, and it runs ahead of Bean.

| Model | Nodes | Drawn as |
|---|---|---|
| `GthrowknifeZ` (held) | blade, guard and handle (149, 126 and 60 vertices) | the hat, in the blade's node; Bond's hand and sleeves stay |
| `PchrknifeZ` (thrown) | the knife (39 vertices) | the hat, only for instances that `generate_player_thrown_knife` registered, so hunting knives stay knives |
| `PchrthrowknifeZ` (pickup) | the knife (39 vertices) | the hat |

The models are found by their vertex counts. If another ROM's model doesn't
match, the hat isn't drawn and the log names the file. While the hat is on,
Bean leaves these models alone (`activeSwap`), so Bond's N64 hand holds it
even with XBLA visuals.

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
prop <brim radius> <dx> <dy> <dz> <pitch>
```

Held rotations turn about the fist: yaw about the knife's axis (180 moves the
hat to the other side of the hand), pitch across the hand, roll about the
forearm.
