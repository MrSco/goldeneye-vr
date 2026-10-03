# Issue #94: online co-op campaign missions (2-4 players)

Branch `issue-94-coop`, on `issue-95-ge-plus-fixes` (the four #95 fixes).
Protocol 16. Compile-checked per file on the host only: not yet built for
the Quest or played. Decisions recorded on #94: downed players are revived,
each player keeps their own save, GE Plus findings go to #95.

## How it works

- **Lobby.** Host Lobby > Mode: Co-op mission, then a mission and a
  difficulty. Friendly fire is the only Match option. A listed co-op game
  shows "Co-op mission: <name> / <difficulty>"; its stage byte is
  `0x80 | LEVELID`. Up to four players.
- **Mission.** Every headset loads the solo mission (solo memory split, fog,
  sky, ammo, AI health and damage rules) with `gamemode` still MULTI so all
  four player structs exist. `gevrMpRules()` and `gevrSoloRules()` replace
  the retail player-count tests. Players 2-4 start 70 units right, left and
  behind Bond's pad, in multiplayer characters.
- **Guards** (port/src/net/net_coop.c). Only the host runs guard AI and
  background scripts. Ten times a second it sends each guard that changed
  (position, facing, animation and frame, aim, guns, fire, fade, hurt); the
  others play it as a puppet through the game's own animation code. Spawns
  and removals are reliable. Each guard acts on one player at a time
  (`set_cur_player`): the nearest one, or for ten seconds whoever shot it.
- **Combat.** A guard's damage to another player goes to that player's
  headset. A client's hit on a guard goes to the host as a report (the
  shooter hears the grunt at once). Explosions hurt guards on the host only.
  A guard's grenade or rocket is nobody's blast, reported by the host and
  shown everywhere. Auto-aim ignores teammates.
- **Mission state.** The host sends stage flags, the alarm, who is down and
  each objective's status; clients show the host's objectives. Clients report
  rooms entered, deposits, photographs, the key copy and the objective items
  they hold (a teammate's key counts for the team). Script messages and doors
  that guards or scripts move or lock reach every headset.
- **End.** The host's mission end (complete or failed) ends it for everyone.
  Each headset saves a completion to its own save (folder 1), then the host
  loads the next mission after 8 s, or the same one after a failure.
- **Revive.** At zero health a player is down, not dead: they can look but
  not move, fire or use, and take no damage; guards ignore them and the
  others see them crouched. A teammate within 160 units for 3 s revives
  them on half health. Everyone down fails the mission.
- **Drop in / out.** A joiner gets the world snapshot, then a roster of the
  guards the host removed and spawned. Once a mission has run 15 s, a joiner
  starts beside a teammate. A leaver just leaves; the guards retarget.
- **Host change.** The host sends every guard's AI state (list, offset,
  timers, presets, flags) and the background scripts' every 2 s. The new
  host applies it and runs the guards from where they stand. Returning
  players get a slot remap, then the roster.

## Headset test plan

1. Two headsets: host Co-op / Dam / Agent; the other joins. Both spawn near
   the start; guards move, shoot and die the same on both.
2. Client shoots, punches and grenades guards; host does the same. Guards
   fire at whichever player is nearest; both players take damage.
3. Complete an objective on each headset (Dam: a room, the alarms). Both
   see "Objective X: completed"; the watch shows the same list.
4. Let one player go down; the other stands beside them for 3 s. Then let
   both go down: "MISSION FAILED", the mission reloads after 8 s.
5. Finish Dam: "MISSION COMPLETE" on both; Facility loads; check each save.
6. Mid-mission, a third headset joins: it appears beside a teammate, dead
   guards lie dead, removed ones are gone.
7. The host quits mid-fight: a client takes over within seconds, and guards
   carry on (patrols may restart their list from where they stand).

## Known limits (best effort)

- Guard shots show muzzle flash and sound on clients, but no tracers or
  wall sparks; a guard's thrown grenade is not seen flying there.
- Scripts that test "Bond" (rooms, items, cutscene control) test the
  player the guard is targeting, or the host for background scripts.
- After a host change, objective events only the old host had (a client's
  photo or deposit) are not carried over; the stage flags are.
- Guards a host spawned show the old chrnum on clients for a clone (cosmetic).
- Saves go to folder 1 on each headset; there is no folder choice yet.
- Pickups from dead guards are per headset (each player can take the gun).
