# Hand reload: racking the slide

A third step for the pistols, the submachine guns and the KF7 when they are
reloaded by hand (owner, 2026-11-21: "drop the clip, put a clip in, and then
rack the slide"). Part of **Hand reload WIP** (Controls → Gestures): no
setting of its own. GoldenEye X's guns and Bean's alike.

Not yet played in a headset: see the test sheet below.

## What the player does

1. Fire the gun dry. A pistol's slide locks back, as before.
2. Drop the magazine (B / Y, or pull it on a gun whose magazine hangs out) and
   push a fresh one in from the belt, as before.
3. **New:** the label over the gun reads `RACK THE SLIDE` (a pistol) or `PULL
   THE BOLT` (a submachine gun, the KF7). With the off hand, grip the gun at
   the rear of its slide (over the gun hand) or at its bolt handle (on the
   receiver, ahead of the gun hand), pull back and let go. The gun gives a
   little with the pull, clicks at the end of its throw, and as it is let go
   the bolt goes home with its sound and a kick: the label reads `MAG IN` and
   the gun fires.

Pushing the gun forward against a still hand racks it too: what counts is the
hand's place along the gun.

## The rules

- **Only after running dry.** A magazine changed while rounds are still in the
  gun needs no rack: one is chambered. The gun is dry once it has had no round
  in it with its magazine in.
- **Until the rack the gun is empty**: it dry-fires, a pistol's slide stays
  back, and the count on the hand reads 0. The new magazine's rounds are not
  lost or spent: they wait (`GevrRack.pending`), count toward the player's
  ammo, and stop the out-of-ammo weapon switch.
- **Letting go short** of the throw does nothing. So does a grip away from the
  slide. The grip that pushed the magazine in is not a rack: let go first.
- **B / Y before the rack** drops the magazine again, its rounds back to the
  reserve. The chamber is still empty.
- **Dual wielding, or one-handed: no rack.** Reloading a gun by touching it to
  the belt (the only way with a gun in each hand) loads and chambers it, as it
  always has. With one gun, touching it to the belt after pushing a magazine
  in chambers it too, so the rack can always be skipped.
- **Put away unracked**, a gun comes back unracked, with the same rounds.
- Hand reload turned off, a level's end or a death with one waiting: it is
  simply loaded (or gone with everything else on a death).
- **Which guns:** PP7 and its silenced, silver and gold kin, DD44, Klobb, ZMG,
  D5K and silenced D5K, Phantom, KF7. The AR33, RC-P90, sniper rifle and the
  round-at-a-time guns keep their reloads.

## What moves

Only the pistols' slides move on the models (GoldenEye's own slide clock holds
them back while the gun is empty and runs them home once it is not, so the
rack needed nothing drawn: `gun.c sub_GAME_7F05E83C`). The submachine guns and
the KF7 have no bolt or handle to move in either model set. For every gun the
whole gun is drawn back up to 1.2 cm with the pull and jumps 0.8 cm forward as
the bolt goes home (`gevrRackNudge`, in `gevrStereoGunMatrix`). Cutting a
bolt out of each mesh, as the magazines were, is the next step.

## Numbers (port/include/gevr_rack.h)

| | |
| --- | --- |
| Reach | a fresh grip within 12 cm of the place |
| A pistol's place | 7 cm above the gun hand's grip, 1 cm behind it |
| A bolt handle's place | 6 cm above, 6 cm ahead |
| Throw | 3.5 cm for a slide, 5 cm for a bolt |
| Still gripped | done once the hand is back within half the throw |

The places are guesses from the grip, not fitted per gun: the reach is wide to
make up for it. A Gun fit mode for them is not built.

## How it is built

- `port/include/gevr_rack.h`: the state per gun hand and its arithmetic
  (`port/tests/rack_native.c`).
- `bondview2.c`: `s_gevrRack`; `gevrRackGun`, `gevrRackMeasure`; the hold when
  a magazine is pushed in and the rack itself in `gevrHandReloadTick`; the
  waiting rounds in `gevrReloadReservedRounds` / `gevrReloadCarriesRounds`;
  given back in `gevrGexMagOut` and `gevrReloadStow`; chambered by
  `gevrGexMagIn` (the belt's paths); the off hand claimed in
  `gevrGexClaimsOffHand`, ahead of the two-handed hold.
- `gun.c`, `gevr_hand_reload.h`: the two labels.
- `port/tests/test_hand_reload.py` runs the whole reload with the rack in it
  (`rackReload`); `port/tests/test_rack.py` the state machine and the wiring.

Nothing is sent to other players: as the magazine's state, the rack is local.

## Test sheet (headset)

Hand reload on, one gun, GoldenEye X's models and then Bean's.

1. PP7: fire it dry (slide back). B, a magazine from the belt, push it in:
   `RACK THE SLIDE`, the slide still back, the trigger clicks. Grip over the
   gun hand, pull back, let go: the slide closes, the sound, `MAG IN`, it fires.
2. PP7 with rounds left: B, a fresh magazine in: ready at once, no label.
3. KF7 and Klobb the same with `PULL THE BOLT`. Is the handle's place where
   the hand goes by itself? Is 5 cm the right throw, 3.5 cm for the pistols?
4. The nudge and the kick: felt, not distracting.
5. Let go short: nothing. Grip the fore-end instead (KF7): the two-handed
   hold, and the gun still waits.
6. After the rack, still gripping: the hand does not become the two-handed
   hold until let go and gripped again.
7. Skip it: push a magazine in, then touch the gun to the belt: ready.
8. Dual wielding two PP7s: belt reloads as before, no label, no rack.
9. Put the unracked gun away (a body slot, the wheel) and draw it: still
   `RACK THE SLIDE`, the same ammo in total on the watch.
10. The last magazine with an empty reserve: seated and waiting, the game
    does not switch weapons; racked, it fires.
11. Left-handed mode and Swap sticks: the off hand is the other one.
