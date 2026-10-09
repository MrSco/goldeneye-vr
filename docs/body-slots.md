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
  the hand in it (a light buzz on the way in, and after a moment a label by
  the hand), squeeze the grip: that weapon goes into the hand, and what the hand held
  goes back to its own slot. Nothing is lost: GoldenEye keeps everything you
  carry. A full hip swaps; an empty one takes the gun.
- **Choosing.** While the hand is there, its own A (gun hand) or X (off hand)
  steps through the category, and so does a sideways flick of its own stick
  (the gun hand's turn stick, the off hand's move stick; Swap sticks swaps
  them). The label says what a squeeze takes ("DD44 2/3"). The last choice
  is "HOLSTER <what the hand holds>", which puts it away. When the category
  has nothing else, that is the only choice. A scrolling flick captures both
  stick axes until they return to centre, even if the hand leaves the slot;
  a diagonal flick cannot also walk or turn. Walking before scrolling remains
  available. Labels draw over hands and arms so they stay readable.
- **Throwing or grabbing.** With motion throwing enabled and a throwable in
  that hand, grabbing needs a continuous 350 ms pause at the slot, moving
  slower than 0.35 m/s relative to the head. The label/ring turns green and
  a small buzz signals readiness. A squeeze then chooses either that grab or
  a throw once; moving through a shoulder during the same squeeze cannot
  switch weapons. Partial release keeps that choice until grip drops below
  25%. A released throw cannot rearm until that full let-go. Guns, gadgets
  and throwables with motion throwing off need no extra pause.
- **Which hand.** Each hip is for its own hand. The shoulders, chest and belt
  take either hand. The off hand needs dual wielding (solo, or online with
  dual wield allowed) and isn't offered what it can't hold (gadgets, mission
  items). A slot with nothing for the hand isn't there for it: the grip goes
  on to mine re-grab, pickup, use or aim as before.
- **The torso.** The slots ride on a torso that follows your head slowly
  (Perfect Dark VR's smoothed yaw, ArmBodyFollow). It holds still while a
  hand is in a front slot or you look down more than 20 degrees, so glancing
  aside or looking down at the belt doesn't move them. Stick turns carry it with you. Recentring
  starts it again.
- **On the body.** Only the hips show what they hold, when you look down;
  the chest, belt and shoulders show just the label when a hand is there.
  After the 150 ms dwell, the occupied slot gets a ring and a hovered hip
  gun gets its category highlight. Gun fit's Slots mode rings every slot. Looking ahead,
  nothing is drawn. GoldenEye X players see GoldenEye's models on the hips
  for now.

Defaults at 170 cm eye height (PlayerHeight), in cm below the eye / out to
that side / ahead. The torso frame's eye is the eye held upright over the
neck, so looking down doesn't move these.

| Slot | Below | Out | Ahead | Reach (Normal) |
|---|---|---|---|---|
| Hips | 65 | 20 | -10 | 13 |
| Shoulders | 8 | 19 | -20 | 20 |
| Chest | 39 | 0 | 4 | 10 |
| Belt (off side, front) | 60 | 7 | 10 | 10 |

Small and Large scale the reach by 0.8 and 1.25.

**Personal slot positions.** With Body slots on and a gun equipped, Menu + A
opens Gun fit. Press X until "BODY SLOT FIT", then flick the move stick
left/right to select either hip, either shoulder, the chest or the gadget
belt. Put either hand where you want that slot and squeeze its grip. The
readout names the selected slot and shows the hand's distance, so shoulder
positions can be set by reach without seeing the ring behind you. Y resets
the selected slot, A saves, B restores the last save, and Menu + A exits.
Magazine pickup has its own belt fit: in Gun fit's Reload mode, put the off
hand at the desired pickup point and press Y, then A to save.
The Belt slot here holds gadgets, including the taser. Its default is low
on the front of the waist, slightly toward the off-hand side; it is separate
from the wrist/watch interaction and the chest's thrown items. Moving Belt
in Slots fit moves that shared gadget location.

The chest default was moved about 4 cm closer after the combined headset
test. Saved custom slot fits take priority and are kept.

**Reloading an off-hand gun.** With the primary hand empty, it can grip and
pull the off-hand gun's magazine, carry it and insert it at the green reload
ring. Its replacement magazine is offered at the primary-hand hip as well.
The held magazine keeps its rounds; releasing it refunds them once. These
reload controls and visual guides also work with Body slots disabled.

**Hand reload (GoldenEye X) with body slots on.** The fitted belt sits where
the hip holster does. A touch there still reloads, 100 ms later. While a
magazine is missing, its pickup sphere wins over the hip and belt slots;
the off hand takes a magazine and the gun hand can load at the belt.
Otherwise a squeeze in that time (holstering) cancels the belt touch until the hand has left it.
The chest cross doesn't fire while the hand is in a slot. While the GE-X gun
waits for a magazine, the off hand's grip at its belt takes a magazine, as
it did with the old hip holster.

With hand reload on, switching or holstering keeps each hand's weapon's
loaded rounds and missing-magazine state. Stored rounds stay with that gun,
so another gun sharing the ammo type cannot spend them. Turning hand reload
off returns those stored rounds to reserve; a new stage clears the saved state.
Closing the watch also keeps the current magazine's rounds.

B/Y eject only and no longer activate doors or switches. Grip use is forced
on while GoldenEye X hand reload is active; the launcher and watch show it
as required. The saved grip-use preference returns when hand reload is off.

A missing removable magazine gets a cyan translucent copy at its fitted
installed position. The insertion target gets a green ring when the held
magazine is within 1.5 times the seating radius, and briefly after seating.
The actual seating radius and ammunition transfer are unchanged. While a
magazine is missing, the corresponding hip guns give way to matching GE-X
magazines at the fitted reload-belt points. A green ring marks a hand inside
the pickup sphere; this uses the same calibration as the gesture. These ammo
guides also work with body slots off. Their position, orientation, grab
sphere and belt calibration now use the same smoothed torso and neck frame
as the holsters, so head turns and tilts do not twist the magazines around
the player. Reaching into an available magazine's belt sphere holds the
torso just as reaching into a holster does.

## Headset rounds and what's next

**Round 1 (2026-10-08, build 4753007): fixed in 7c72bc7 and 088c037.**
The guns and the ring blinked and the slots moved about: the torso started
again at the head every tick (8,668 log lines in three minutes). The game
links GoldenEye's own atan2f, which returns 0..2 pi, so a small turn read
as 359 degrees, a "jump". The slots now use their own atan2 (tested against
the game's math too). Also from that round: the chest's and belt's models
were in the way when looking down (not drawn now), a label waits 150 ms so
a passing hand shows nothing, a hand leaves 5 cm past the reach (was 3), and
the torso holds from a 20 degree downward look (was 35). 088c037 also took
out the ring and the highlight.

**Round 1 follow-up from the user (2026-10-09, build 088c037).** Open, for
the next iteration:

1. *Bring back the ring and the highlight, one at a time.* The user liked
   them; the trouble was too many on screen at once (with the torso bug,
   several slots' rings and highlights blinked together). Restore them only
   for the slot a hand is actually in, after the 150 ms dwell: the hovered
   hip gun drawn brighter, one ring at that slot's centre, at most one per
   hand, nothing on slots without a hand. Both were in b345a45:
   gevrBodySlotsDraw's envcolour lift for `by >= 0`, and the 4 cm
   gevrBodyRing in gevrBodySlotsDrawLabels, tagged with the body
   (0x565F0003).
2. *Hand reload: reaching for a new magazine sometimes takes the
   holstered gun instead (a swap).* The off hand's GoldenEye X magazine
   claim (bondview2.c gevrGexClaimsOffHand, checked first in
   gevrGripGestureTry) only holds at the belt while the gun hand's GE-X
   magazine is already OUT and the off hand is inside the fitted belt
   sphere (VrReloadBelt, radius 18, in the head-yaw frame). Every other
   reach falls through to the hip slot:
   - the magazine still in the gun when the hand reaches for the next one
     (the claim checks the gun's magazine, not the belt, then);
   - the belt sphere is in the head's frame and the hip slot in the
     torso's, so looking toward the hip turns the sphere away from the hand
     while the slot stays put;
   - dual wielding (a reloadable gun in the left hand ends the claim);
   - the gun hand, which has no claim at all: its belt touch reloads, but
     a squeeze there swaps.

   Suggested rule: while hand reload is on and a gun the hand could reload
   at the belt isn't full (gevrReloadNeedsAmmo), or its GE-X magazine is
   out, the belt wins at the hip. A grip there goes to the magazine or the
   reload, never the holster. With every gun full, the hip swaps as now.
   Test the belt with the hand reload test cases (port/tests
   test_hand_reload.py). Another option the user could choose instead: move
   the hips' default away from the belt, or show both in Gun fit.

Other things still to check: the throw-versus-swap gate (350 ms continuously settled / 0.35 m/s)
is a guess; GE-X players see GoldenEye's models on the hips; seated reach
for the shoulders; two-headset Doubles/CTF.

**Next iteration (2026-10-09, awaiting headset testing).** Occupied-slot
rings and hip highlights are restored. Missing-magazine pickup now wins for
both controllers, including dual wielding. Ammo persistence, exclusive
B/Y eject, required grip use, ghost magazines and reload rings are added.
The full-magazine belt/holster arbitration remains as before. The ammo model
and pickup ring show where the fitted belt actually is.

**Headset follow-up (2026-10-09, combined test build b694283).** The belt
magazines twisted with head movement while the holstered guns stayed on the
torso. Both their centres and mesh axes were in the camera's frame. Belt
magazines, pickup zones and Gun fit now share the holsters' torso frame;
the frame stays available for hand reload with body slots off. Check this
by looking down and slowly turning/tilting the head: magazines should move
with the hips' guns, and a hand at the visible magazine should still get the
pickup ring. Repeat with a custom belt fit and left-handed mode.

**Next headset follow-up (combined build 5d5a510).** Magazine placement was
better. Slot labels were obscured by arms, a diagonal scroll could also walk,
and a throw could take the shoulder weapon as well. Labels now use a depth-free
overlay, scrolling captures both axes through stick centring, and pending grip
arbitration blocks throw wind-up. Grip ownership persists through partial
release. Throwable grabs require the continuous 350 ms pause and show a green
ready cue; other items still grab immediately. Retest fast shoulder wind-ups,
paused deliberate swaps, partial releases and diagonal scrolling on each hand.

Reload checks for the next headset round:

1. Fire part of a PP7 and KF7 magazine, switch away and back using the wheel,
   A/X and a body slot. Check the round count stays the same; repeat empty.
2. Eject with B/Y, switch away and back. Check the magazine remains missing,
   the gun stays empty, and the cyan ghost stays in its installed position.
3. Stand at a door or switch and press B/Y: it should eject only. Squeeze
   grip with the hand in use range: it should activate, including when the
   saved Grip use option was off before enabling hand reload.
4. Look at the hips after ejecting. Check matching magazines replace the
   hip guns, at the points set in Gun fit's reload mode. Reach into that
   sphere: a green ring should appear and a squeeze must not draw a pistol.
5. Bring the held magazine toward the well. Check the green ring appears
   before seating, the ghost disappears when seated, and the ring follows
   the gun briefly afterward. Remove/reinsert a partially loaded magazine:
   its round count must remain the same.
6. Repeat left-handed, with two pistols, and with a custom belt fit. Toggle
   hand reload off and confirm B/Y activation and the saved Grip use choice
   return. Check extra guides do not cause a noticeable frame-rate drop.

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
   keep working before scrolling. Scroll with a diagonal flick on either
   hand's stick: no movement or turn. Leave the slot with the stick still
   pushed: it stays captured until both axes centre, then movement resumes.
5. Off hand: same at the left hip (dual pistols).

Long guns over the shoulders:
6. Reach over the right shoulder, squeeze: the last rifle or SMG. Over the
   left shoulder: the last heavy gun. Either hand can reach either shoulder.
   The label shows in front of your chest.

Chest and belt:
7. Hand to the chest: grenades, mines, knives (A steps). With motion
   throwing on and a grenade in hand: reach back and throw in one go, and it
   still throws. Pause at the chest/shoulder until its label turns green and
   buzzes, then squeeze: only the slot takes it. Squeeze before reaching,
   pause at the shoulder, partially release and squeeze again: no weapon
   change and no second throw until a full grip release. Repeat with knives.
8. Belt, front, off side: gadgets (camera, bug, etc. on missions that give
   them). Watch items stay on the watch.

The torso:
9. Look down at your hips: the pistols on the hips are drawn and stay put as
   you turn your head while looking down. Nothing blinks; nothing from the
   chest or belt is in the way.
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
0    0     0      0     5      350     100     0      -20         60       0
```

A radius of 0 keeps the size setting's. follow 0 keeps ArmBodyFollow (0.02).
markers 1 rings every slot outside Gun fit. Push with
`adb push gevr_bodyslots.txt /sdcard/Android/data/com.gevr.port/files/`.

## Where the code is

- `port/src/gevr_bodyslot.c`, `port/include/gevr_bodyslot.h`: the arithmetic
  (torso chase, neck frame, slots and fits, zones, choices, stick, belt),
  tested by `port/tests/test_body_slots.py`.
- `port/tests/test_body_slot_interactions.py`: production grip ownership,
  throwable-only pause, partial-release behavior, diagonal stick capture
  and label display-list depth state.
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
