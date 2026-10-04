# Issue #95, round 2 (branch issue-95-ge-plus-round2)

Branch notes; fold into STATUS.md at merge. Started 2026-10-04 from main
e431819 (v0.4.5).

## On the branch

| Commit | What | Headset check |
|---|---|---|
| 048cfab | Launcher cheats: 2x/10x Health, 2x Armor, Max ammo, Extra weapons | not yet |
| e935b73 | Comfort tab WHEN HIT: no knockback (on by default), keep firing when hit, red flash toggle | not yet |
| 6118db9 | Statue and Cradle online (ROM MP setups, MP memory strings, Statue keeps its solo fog) | not yet |
| 556eecf | Cylinder floor finder from Perfect Dark (remote bodies' tile and feet, Bond's tile recovery) | not yet |
| ba01477 | MP: the level music returns after the death sting when someone else is dead too | not yet |
| 844140e | HD pack: the stage's gun textures decoded at load | not yet |
| 0c5cbd4 | MP: other players hold the first-person gun models; launchers carry the rocket | hqguards on the Dam 2026-10-04: launcher fit (scale 1.02) and rocket look fine; range too short |
| 688d10e | Detailed guns out to 50 m (10 m swapped in sight) | better; user asked for 100 m |
| 6871745 | Detailed guns out to 100 m | user: detailed always, at any distance |
| 9b01e1c | No distance limit | installed 09:46, to check |
| 8249f70 | Every guard holds the detailed guns too, solo and co-op (DetailedGuns=0 turns them all off; hqguards flips guards back to compare) | to check |

## Release notes for whoever releases this

- Statue and Cradle change what a match config may name: v0.4.5 rejects
  stage 22/41. Bump GEVR_NET_VERSION at release.
- No Fly or No Clipping: they are not in the retail code (only the debug
  menu's object "collisions" switch). Bond Phase is multiplayer-only and
  launcher cheats apply in solo, so it was left out.

## Headset tests

Solo (one headset):

1. Launcher > Cheats: the HEALTH & AMMO group shows; ticking 2x Health
   unticks 10x Health and back; the page fits without scrolling. In a
   mission each cheat's message shows at the start (Max ammo, Extra
   weapons give the Cougar, Laser, Golden Gun and both PP7s).
2. Comfort tab: WHEN HIT (stereo) shows three boxes. Get shot by a guard:
   - No knockback on: the view does not slide; off: it slides as before.
   - Keep firing when hit on: hold the trigger while being hit, the gun
     keeps firing; off: it stops during the red flash.
   - Red flash off: no red; you still take damage at the normal rate.
3. Gun textures (HD pack on): start Facility or Archives, switch to each
   weapon for the first time. Its HD textures should show from the first
   frame. Log: `gunpreload: N stage guns, M pack textures queued`.
4. Floor self-test: `touch files/gevr_stancyl_selftest.txt`, load any
   level, then delete the file. The log should say
   `stancyl: self-test: N tiles, 0 not found at their centre` (a few
   misses at seams are worth a look; the second count is the old
   centre-only lookup, for comparison).

5. Detailed guns (one headset): guards hold them by default. With Enemy
   rockets their launchers carry a rocket that leaves when they fire and
   returns 1.6 s later. Check the KF7 and D5K guards on Dam/Facility;
   `echo hqguards > files/gevr_cheat.txt` (chmod 666) flips guards back
   to the game's models to compare. Log: `heldgun: item N ... fitted: scale S offset X,Y,Z` per
   gun; a "kept the Pchr model" line names a gun whose fit failed.

Two headsets:

6. Statue and Cradle in the stage list (last), 2-8 players: spawns on
   distinct pads, pickups for a few weapon sets, the stage music, Statue
   with its orange clouds, frame rate with as many players as possible.
7. Watch the other player on stairs, at a ledge edge, dropping off a
   ledge, crouched on stairs: the body never snaps to the floor below,
   follows the drop, keeps its feet on the floor when crouched.
8. Trade kill (both die together): after your death sting the level
   music comes back before you respawn.
9. Other players' guns: each weapon set, dual wielding, tiny/big guns;
   the launcher's rocket gone when its owner fires and back after the
   reload; the gun fits the hand (left hand too); frame time with as
   many players as possible.
