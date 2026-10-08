# Body slots (holsters on the body), 2026-10-08

An option beside the weapon wheel: the wheel's five categories live on the
body. Reach to a slot and squeeze the grip to take what it holds. The wheel
(hold A/X) and A/X tap cycling stay exactly as they are. Off by default.
Branch: claude/hip-holster-equipment-system-a3104e. One commit per part, so a
part that misbehaves can be reverted alone.

```
          over the off-hand shoulder        over the gun-hand shoulder
          HEAVY (shotguns, sniper,          RIFLES and SMGs
          launchers, laser)                 (Klobb .. RC-P90)
                            \      O      /
                             \    /|\    /
                     chest:    [THROWN]          grenades, mines, knives
                              /   |   \
           belt, off side: [GADGETS]  |
             off-hand hip: [PISTOL]  [PISTOL] gun-hand hip
                                 /   \
```

Where to switch it: launcher Controls > Gestures... > BODY SLOTS (toggle,
Small/Normal/Large reach, Show); in a mission, Menu > Game Options > VR
settings > Gestures ("Body slots WIP", "Slot size"). The old hip holster is
greyed out while body slots are on.

## How it works

- **Taking.** A slot holds the last weapon you used from its category. With
  the hand in it (a light buzz on the way in, a ring and a label by the hand),
  squeeze the grip: that weapon goes into the hand, and what the hand held
  goes back to its own slot. Nothing is lost: GoldenEye keeps everything you
  carry. A full hip swaps; an empty one takes the gun.
- **Choosing.** While the hand is there, its own A (gun hand) or X (off hand)
  steps through the category, and so does a sideways flick of its own stick
  (the gun hand's turn stick, the off hand's move stick; Swap sticks swaps
  them). The label says what a squeeze takes ("DD44 2/3"). The last choice
  is "HOLSTER <what the hand holds>", which puts it away. When the category
  has nothing else, that is the only choice.
- **Which hand.** Each hip is for its own hand. The shoulders, chest and belt
  take either hand. The off hand needs dual wielding (solo, or online with
  dual wield allowed) and isn't offered what it can't hold (gadgets, mission
  items). A slot with nothing for the hand isn't there for it: the grip goes
  on to mine re-grab, pickup, use or aim as before.
- **The torso.** The slots ride on a torso that follows your head slowly
  (Perfect Dark VR's smoothed yaw, ArmBodyFollow). It holds still while a
  hand is in a front slot or you look down, so glancing aside or looking down
  at the belt doesn't move them. Stick turns carry it with you. Recentring
  starts it again.
- **On the body.** The hips, chest and belt show what they hold when you
  look down. Looking ahead, nothing is drawn. GoldenEye X players see
  GoldenEye's models there for now.

Defaults at 170 cm eye height (PlayerHeight), in cm below the eye / out to
that side / ahead. The torso frame's eye is the eye held upright over the
neck, so looking down doesn't move these.

| Slot | Below | Out | Ahead | Reach (Normal) |
|---|---|---|---|---|
| Hips | 65 | 20 | -10 | 13 |
| Shoulders | 8 | 19 | -20 | 20 |
| Chest | 39 | 0 | 8 | 10 |
| Belt (off side, front) | 60 | 7 | 10 | 10 |

Small and Large scale the reach by 0.8 and 1.25.

**Hand reload (GoldenEye X) with body slots on.** The fitted belt sits where
the hip holster does. A touch there still reloads, 100 ms later. A squeeze
in that time (holstering) drops the reload until the hand has left the belt.
The chest cross doesn't fire while the hand is in a slot. While the GE-X gun
waits for a magazine, the off hand's grip at its belt takes a magazine, as
it did with the old hip holster.

## Test sheet (one headset session)

Setup: launcher > Controls > Gestures... > Body slots on, Normal. Then a
solo mission with Cheats > All guns (or `files/gevr_cheat.txt` "allguns").

Pistols at the hips:
1. PP7 in the gun hand. Reach to the right hip, squeeze: it holsters (fists).
   Squeeze again: it comes back.
2. DD44 in hand: reach the hip. The label shows the PP7 (the last pistol).
   Squeeze: PP7 in hand, DD44 on the hip. Squeeze again: they swap back.
3. Hand at the hip, press A a few times, then flick the turn stick left and
   right: the label steps through every pistol, then "HOLSTER ...". Squeeze
   on a pick: that one comes out. Leave and come back: the slot offers it.
4. While the hand is at the hip, walk with the left stick: forward and back
   keep working. Turning with a flick should not happen until you leave the
   hip and centre the stick.
5. Off hand: same at the left hip (dual pistols).

Long guns over the shoulders:
6. Reach over the right shoulder, squeeze: the last rifle or SMG. Over the
   left shoulder: the last heavy gun. Either hand can reach either shoulder.
   The label shows in front of your chest.

Chest and belt:
7. Hand to the chest: grenades, mines, knives (A steps). With motion
   throwing on and a grenade in hand: reach back and throw in one go, and it
   still throws. Hold the hand still at the chest a moment and squeeze: the
   slot takes it.
8. Belt, front, off side: gadgets (camera, bug, etc. on missions that give
   them). Watch items stay on the watch.

The torso:
9. Look down at your hips: the pistols on the hips are drawn and stay put as
   you turn your head while looking down.
10. Stand still, turn your head 45 degrees and hold it: after about a
    second the slots turn to follow. Snap and smooth turn: the slots turn
    with you. Recentre (both stick clicks): they face ahead again.
11. Gun fit (Menu + A), press X until "BODY SLOT FIT": rings at every slot.
    **The rings must not swing when you turn your head** (if they turn the
    wrong way, the torso's sign is backwards: tell me). Flick the move stick
    sideways to pick a slot, put a hand where you want it, squeeze: the ring
    jumps there. Y puts it back. A saves, B undoes.

Mixes and regressions:
12. Hand reload on (GE-X): a touch at the belt still reloads; holstering a
    pistol at the hip does not also reload it.
13. Punches: a fast reach to the hip makes no whiff or hit.
14. The weapon wheel still opens with a held A/X, except while that hand is
    in a slot.
15. Left-handed mode: the slots mirror (the gun hip is on the left).
16. Seated: the hips and chest should still be reachable. The shoulders may
    be blocked by the chair (use the wheel).
17. Performance: Show stats, look down at the hips. DRAWS should rise by a
    handful only.
18. Body slots off: the old hip holster, the wheel and belt reload work as
    before.

Online (two headsets, when convenient): Doubles keeps both hands matching;
the CTF flag carrier is refused at every slot ("FLAG"); dead or spectating,
no slots.

Logs: start a capture for the round, then
`adb logcat | grep bodyslot:`. That gives one line per hover, leave, step,
take, stow, refuse, belt touch, torso reset and every 10 degrees of twist.

## Tuning without a rebuild

`files/gevr_bodyslots.txt`, re-read every two seconds, eleven numbers:

```
hipr backr chestr beltr exitcm dwellms deferms follow freezepitch maxtwist markers
0    0     0      0     3      120     100     0      -35         60       0
```

A radius of 0 keeps the size setting's. follow 0 keeps ArmBodyFollow (0.02).
markers 1 rings every slot outside Gun fit. Push with
`adb push gevr_bodyslots.txt /sdcard/Android/data/com.gevr.port/files/`.

## Where the code is

- `port/src/gevr_bodyslot.c`, `port/include/gevr_bodyslot.h`: the arithmetic
  (torso chase, neck frame, slots and fits, zones, choices, stick, belt),
  tested by `port/tests/test_body_slots.py`.
- `src/game/gevr_bodyslots.c`: the game side (tick, grips, stepping, drawing,
  labels, Gun fit's Slots mode).
- Hooks: `bondview2.c` gevrGripGestureTick / gevrGripGestureTry /
  gevrMotionThrowTick / gevrHandReloadTick / gevrHandChopTick /
  maybe_mp_interface; `port/src/input.c` (A/X, stick, Gun fit);
  `gevr_heldgun.c` gevrHeldGunDrawPosed; fast3d's VR_HAND_DRAW 3 (the body's
  in-between-frame redraw, `vr_openxr.cpp` gevrVrRedrawBodyDelta).

Precedents: Doom3Quest (waist frame, hip and back slots, holstered model
drawn), Quake VR (hip, shoulder and chest holsters; radii; haptics),
RTCWQuest and Lambda1VR (reaching over the shoulder), Fallout 4
VirtualHolsters (one zone at a time, world grab off in a zone), Zero Caliber
(fixed points on a visible belt), Perfect Dark VR (the torso's yaw, the
belt magazine drawn), GEVR PC vr456 (a hip grip is never a no-op).
