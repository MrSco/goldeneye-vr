# Teleport locomotion

For players the stick's slide makes ill. **Comfort > Movement: Teleport**
(launcher and the in-game settings; `Teleport=1` in goldeneye-vr.ini; off by
default). Headset only, on foot: the tank, the Dam speedboat and a flat screen
keep their controls.

Played on a Quest 3 by the owner (2026-11-21): "works really well". The test
sheet below has not been gone through item by item.

## Using it

- **Aim and jump.** Push the move stick forward: an arc leaves that hand and
  a ring marks where it meets the floor. Point the hand to move the ring; let
  the stick go to jump there. The view blinks to black for the jump (about a
  sixth of a second in all), so the eyes never see the world slide.
- **Cancel.** Point at your feet, or anywhere the ring turns into a small red
  mark, and let go: nothing happens.
- **Hops.** Flick the stick back, left or right for a one metre hop that way,
  from where your head faces. One hop a flick.
- **Stamina.** The white band round the ring is the distance in hand: a full
  band is a full 6 m jump. The arc reaches no farther than it shows.
- Turning, physical walking and crouching, the weapon wheels, the sniper's
  zoom on the stick and everything else are unchanged. Swap sticks moves the
  arc to the other hand with the stick.
- While a grip aims, the stick no longer leans or ducks you (that is a slide
  of the view too): duck with your body, or with Stick click to crouch.

The vignette setting is not needed for it and does not react to it: nothing
slides.

## Why it is not cheap

The same rules apply solo, in co-op and in a deathmatch. Each headset applies
them to its own player, as it does that player's walking.

1. **Only where you could walk in a straight line.** The jump is tested by
   the walk's own collision check (`bondviewTryMoveToStan`) in 20 cm steps
   from the body to the mark (`bondview2.c gevrTeleportWalk`): the line to
   each step and the body's volume there, against walls, shut or half-open
   doors, glass, crates and other objects, guards, players (as the walk treats
   them), tile edges (drops, rails, gaps) and the low ceilings that need a
   crouch. The walk's slide along what it meets is left out. Aimed past any of
   those, the ring stops in front of it. The steps are shorter than the body
   is wide, so nothing the body would not fit through is stepped over.
   Checked again at the moment of the jump (a door may have shut).
2. **Never faster than running.** A jump costs its length from a stamina that
   holds 6 m and fills at 90% of Bond's forward run (503 units a second at
   100%, docs/speedrun.md), scaled by the movement speed rule, the host's
   online. Over any stretch a teleporting player covers less ground than one
   running forward, and much less than one running diagonally (710). A burst
   is one full jump, then the refill rate. `port/tests/test_teleport.py`
   holds this.
3. **A quarter second between jumps**, whatever their size: no flickering
   about.
4. **No time away.** The body is where it stood until the black frame and at
   the mark from it on. It can be shot at both; nothing is invulnerable.
5. **Nothing skipped that matters.** Pickups and triggers work from where the
   body is each tick, so a jump over a pickup does not collect it; landing on
   it does.

## What the others see

No protocol change (27 still): a jump is the player's position in its
`PLAYER_STATE`. A headset on v0.5.23 or this build shows it as a dash: up to
5.12 m is eased over a few ticks, a longer one is set at once
(`net_player_sync.c`). The copy plays no walk animation for it.

Not done: a host rule to forbid teleporting in a match (the match config's
flags are checked bit for bit, so it needs protocol 28), and any effect on
the other headsets where a player jumps from or to.

## Speedruns

A run with teleport on is slower than a run on the stick (rule 2) and is not
stamped or ranked apart.

## How it is built

- `port/src/gevr_teleport.c`, `port/include/gevr_teleport.h`: the stick's
  gestures, the stamina, the cooldown, the blink's timing, the arc. No game
  state; `port/tests/teleport_native.c`.
- `src/game/gevr_teleport.c`: the tick (from `MoveBond`, this headset's
  player), the arc's landing over the floor's tiles, the mark, the jump, and
  the drawing (with the force pull's line, over everything).
- `bondview2.c`: `gevrTeleportWalk` (the straight-walk test, which moves
  nothing) and `gevrTeleportPlace` (the body there as a respawn or a co-op
  join puts it; the view follows at once, `GEVR_LOCO_RESET_TELEPORT`).
- `input.c`: with teleport on the move stick presses no C buttons, so the
  game walks nobody.
- `gfx_opengl.cpp`: the vignette's pass takes the blink (`uBlink`), drawn
  with the vignette off too.

## Test sheet (headset)

Solo, Movement: Teleport.

1. Facility's start: aim down the corridor; the ring follows the hand, the
   jump blinks, you stand on the ring. No slide at any point, no judder after.
2. Aim through the shut door, the vent's grate and a wall: the ring stops in
   front of each. Open the door: now it goes through. Half open: not yet.
3. Glass (Facility's lab windows, Library's panes): the ring stops at it;
   shoot it out, and it passes.
4. Aim past a crate, a guard, a table: the ring stops short. Between two
   objects closer than your shoulders: not through.
5. Edges: Dam's walkway rails, Cradle's and Caverns' catwalks, a balcony: no
   jump over or off.
6. Stairs and ramps, up and down (Bunker II's stairs, Archives, Surface's
   slopes, Complex's ramps): the ring climbs them and the jump ends at floor
   height with no vertical glide. A stair the ring will not cross is a bug:
   note the level and the place.
7. The vents (crouch needed): standing, the ring will not enter; crouched it
   does.
8. Hops: back, left, right; into a wall (a short buzz, no move); several in a
   row (the quarter second between).
9. Stamina: jump 6 m twice at once. The second arc is short, the band part
   full, and it fills in about a second and a third.
10. Pause, the watch, the weapon wheel on the move stick, the sniper's zoom,
    the tank, the Dam speedboat, dying mid-aim: no stray jump, never left in
    the dark.
11. Swap sticks and left-handed: the arc comes from the hand whose stick it
    is.
12. 72, 90 and 120 Hz, SpaceWarp on: the blink is brief and even.

Two headsets, one on Smooth and one on Teleport:

13. Race a corridor: the runner wins.
14. The runner watches the jumps: a quick dash, the body always somewhere,
    hits land before and after.
15. Doors and glass between the two: the teleporting player cannot get to
    the runner's side any way the runner could not.
16. Co-op and Horde: the same, and the movement speed rule changes the
    refill (50%: half as fast).
