# Hand reload on GoldenEye's own guns

**Hand reload WIP** (Controls → Gestures) reloads GoldenEye's own models by
hand too, with GoldenEye X's guns and Bean off (owner, 2026-11-21: "and also
original mode guns ... you'll probably have to cut some meshes to make
magazines removable"). The states, the belt, the rack and the labels are the
ones GoldenEye X's and Bean's guns use ([project-bean](project-bean.md#removable-magazines-hand-reload-wip),
[hand-reload-rack](hand-reload-rack.md)); only where the magazine comes from
is new (`port/src/gevr_n64mag.c`, `port/include/gevr_n64mag.h`).

Not yet played in the headset: the places below are read off the ROM's models.

## The magazines

| Gun | Magazine | Out by |
| --- | --- | --- |
| PP7, silenced, silver, gold; DD44 | made: a dark box up the grip, its foot showing under it | B / Y |
| Klobb, KF7, ZMG, D5K, silenced D5K, Phantom, AR33 | the model's own, lifted out | pull down, or B / Y |
| RC-P90 | the model's own, along the top of the receiver | lift up, or B / Y |
| Sniper rifle | made: a box under the stock ahead of the trigger | pull down, or B / Y |
| Shotgun, automatic shotgun | one of the shells it carries, taken there | never out: a shell at a time goes in |
| Cougar, Golden Gun, grenade launcher, rocket launcher | none | the belt's touch or the chest cross runs the game's reload |

- **A rifle's or a submachine gun's** magazine is its own triangles on the
  cartridge, none shared with the gun (on most it is one display list). The
  magazine is every triangle wholly inside a box round it. The gun without it
  is a copy of each list with those triangles' commands emptied; the magazine
  alone is the same lists with every other triangle emptied. No vertex is made
  and nothing is cut.
- **A pistol's** is inside its grip with nothing of it modelled, and **the
  sniper rifle** has none, so one is made: a plain dark box, drawn in the gun
  while it is in and only with Hand reload on. The gun itself stays whole.
- **A shotgun's shells** are the model's (the cartridge shows as many as the
  reserve has, up to five). The round is the front shell's own triangles, the
  gun is never opened, and it goes in at a port under the receiver. A ring
  marks the shells as the hand comes near, as on Bean's and GoldenEye X's.
- **The placeholders** (in the empty well, at the belt) are the magazine's own
  triangles in the guide's flat see-through colour, as GoldenEye X's.
- **The off hand** is the watch arm, as with Bean: the place that takes a
  magazine is its palm, Gun fit's held ammo.

Gun fit's held ammo and ammo insertion apply (saved with Bean's, as
`BeanMagFit<item>`: both are in the same model space); the rack's place is
`Rack<gun>`.

A ROM hack's own guns under GoldenEye's names (Goldfinger 64, Tomorrow Never
Dies) have no magazines here: the setting is blocked there unless GoldenEye
X's guns are on, and the game reloads itself. Mirror Mode's shotguns, flipped
left to right, may find no shell and fall back to the belt's touch.

## Adding or moving one

1. `python tools/gevr_model_export.py <ROM> <FileZ> <out dir>` writes the
   model's triangles; the coordinates in `gevr_n64mag.c` are those of the root
   joint (subtract the root group's origin), 0.085 cm a unit.
2. Add or change the row in `s_defs`: a box that holds the magazine's triangles
   and no other whole triangle (`HANGING`), or a made box (`made`, `foot`,
   `into`, `length`, `half`).
3. `python port/tests/test_n64_magazine.py <ROM>` lays every row out on the
   ROM's models and checks the triangle counts.

## Test sheet (headset)

Hand reload on, GoldenEye X's guns off, Visuals: Original (then HD).

1. PP7: is the foot under the grip right, or in the way? B drops the
   magazine: does the dark box look like one in the hand and falling? A fresh
   one from the belt, its ghost in the grip, push it in, rack the slide.
2. KF7, Klobb, D5K, Phantom, AR33, ZMG: pull the magazine down. Is the gun
   without it clean (no stray triangle gone, none left hanging)? Does the
   magazine in the hand sit in the palm? Does it seat where the ghost is?
3. RC-P90: lift the magazine off the top. Does the right piece come off?
4. Sniper rifle: the made box under the stock. Does it sit right there?
5. Shotgun and automatic shotgun: the ring on the shells; take one, push it in
   under the receiver. Is the port where the hand expects it (Gun fit's ammo
   insertion moves it)? With the reserve empty the model shows no shells.
6. Cougar, Golden Gun, launchers: touch the belt or cross the chest: the
   game's reload plays.
7. Hand reload off: no box under any grip or the sniper rifle; guns reload
   themselves.
8. Bean on, then GoldenEye X on: their own magazines, as before.
9. Dual wielding, left-handed mode, a put-away gun with its magazine out.
