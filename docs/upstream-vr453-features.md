# Upstream GEVR PC features (vr444 to vr453.1), 2026-10-04

These are no6969el/GEVR's player features, ported where we lacked them. Upstream
publishes docs only, so its CONTROLS.md, GEVR-SETTINGS.md and release notes
are the spec. Perfect Dark VR supplied the code where it has some: the reload
pull, the recoil table, and a pause-menu VR page. Branch:
claude/gevr-feature-review-93fe4d. One commit per part, so a part that
misbehaves can be reverted alone.

| Part | Where to switch it | Default |
|---|---|---|
| Smooth-turn speed, 45..240 deg/s | Launcher Comfort tab; watch VR page | 120 (as before) |
| Watch "VR settings" page | Watch > Game Options, last row | n/a |
| Hip holster / grip use / mine re-grab | Controls > Gestures...; watch VR page | on |
| Grip to hand (a gun on the floor into that hand) | Controls > Gestures...; watch VR page | off |
| Hand reload | Controls > Gestures...; watch VR page | off |
| Per-gun recoil | Controls > Gestures...; watch VR page | off |
| Mines stick to guards | Play > Game rules... | off |
| Bodies stay 12/24/48 | Play > Game rules... | off |

Not taken (desktop-only or already better here): monitor mirror, Flat/XR
profiles, hand cubes, the tank auto-mount, crouch on the right stick while
aiming. Upstream bugs already fixed or impossible here: #84 rifle guards and
running, #68 far guards, #66 grenade launcher double fire, #55/#89 matrix
saturation per eye, #38 double pad scale, #67 rocket yaw.

## Test sheet (one headset session)

Before the build: check for things upstream got wrong. These need no
settings.
1. Statue: does the Janus meeting spawn? (Upstream #103 is still open.)
2. Do guards flinch when shot?
3. Does the loaded rocket sit straight on the launcher while you move (#125)?
4. Facility end: any black flicker after throwing mines at the gas tanks?
5. Bunker command room: do the ceiling TV monitors draw?

Comfort and the watch page:
- Comfort tab: Turn speed is greyed out unless Smooth. Try 45 and 240.
- In a mission, Menu > Game Options > last row "VR settings". Up/down picks a
  row, left/right steps it, A steps it forward, Back returns. Each change is
  live, and stays after a relaunch.

Grip gestures (all on by default):
- **Aim beside a door with the right grip: it must not open.** Then reach out,
  touch the door and squeeze: it opens. Try a switch or console the same way.
- **Gun hand at your right hip, squeeze:** the gun holsters (fist out).
  Squeeze there again: it comes back. The left hand works the same at the
  left hip. Seated play counts too (the zone is 40 cm below the eye, 12 cm
  out to the side).
- **Grip to hand (turn it on):** walking over a gun still picks it up, so
  this is for the guns left on the floor (full ammo, or the pair already
  carried) and for choosing the hand. Reach to one and squeeze with the left
  hand: that gun goes into the left hand (the floor one stays if the game
  wouldn't take it). Upstream calls it grip pickup.
- **Cheats > All guns, throw a remote mine at a wall, reach and squeeze at
  it:** it comes back (ammo +1). A proximity mine can be taken back only
  while it is still arming.
- Each toggle off: that gesture stops, and the grip aims there.

Hand reload (turn it on):
- KF7: empty the magazine. It does not reload, and B does not reload.
  Squeeze the left hand under the gun in front of the trigger, then pull
  down about 8 cm: it reloads.
- PP7, Cougar, shotguns, rocket and grenade launchers: sweep the gun hand
  across your chest to the other side: it reloads.
- Dual PP7s: each hand's sweep reloads its own gun.
- Grenades and mines still come back by themselves.

Per-gun recoil (turn it on):
- The PP7 kicks a little; the Cougar, shotgun and rocket launcher kick a lot.
  The KF7 kicks less when held with both hands. Toggle off: no kick.

Game rules (turn them on):
- Mines stick to guards: throw a remote mine at a Dam guard. It rides on him
  and detonates on him. When he dies it drops.
- Bodies stay 48: bodies stay down. Frigate: the hostages must still free
  (upstream's #78 softlock). Statue: Janus still spawns after a firefight.

## Tuning without a rebuild

Logs say "stereo: grip gesture ...", "stereo: hand reload ..." and "stereo:
gesture zones" / "reload zones" when a file below changes.
- files/gevr_gesture.txt: `hipdrop hipside hipahead use pickup mine`
  (cm; defaults `40 12 20 15 25 20`)
- files/gevr_reload.txt: `magfwd magdown magradius pull crossside crossahead`
  (cm; defaults `8 7 10 8 10 35`)

## Known limits

- Mine re-grab finds mines stuck to walls and floors, not ones stuck to
  doors or vehicles (those hang off another prop).
- A grip press waits one game frame before it aims, while the game decides
  whether a gesture takes it.
- Mines on guards, bodies stay and mine re-grab are single player (mine
  re-grab also works for the co-op host).
