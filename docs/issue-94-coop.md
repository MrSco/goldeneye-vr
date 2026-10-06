# Issue #94: online co-op campaign missions (2-4 players)

Branch `issue-94-coop`, on `issue-95-ge-plus-fixes` (the four #95 fixes).
Protocol 16. Built and signed (2026-10-02); first headset test, the host
alone, played Dam from the launcher. Decisions recorded on #94: downed
players are revived, each player keeps their own save, GE Plus findings go
to #95.

User decisions after that test (2026-10-03): co-op is the solo campaign
through the game's own menus (mission select, briefings, cutscenes), the
host driving them; the pause is the solo watch and the world keeps running
(that player stands, can be shot); each player skips their own intro,
scripted and ending cutscenes follow the host; keep the teammate radar.

Headset-accepted, host alone (2026-10-03, build 1a2808d): Launch to the
folders, mission select, briefing, Start; the Dam intro (captions at the
bottom) and its skip; the watch; the radar with its health and armour arcs
in stereo and 2D; the menus' music. Not yet played: two headsets (menus
followed, Start, debrief and statistics on a teammate's headset, revive),
and scripted/ending cutscenes on a teammate's headset (host only so far).

Lessons from that test: a stage's network settings must apply at its load
(boss.c), not when the reset arrives, or the frame under way runs the next
stage's code in the old one (the Dam start crashed in the title stage);
matrix_4x4_f32_to_s32 multiplies by the level's world scale, so an
unscaled HUD matrix needs matrix_4x4_7F058C64/C88 round it (the radar arcs
on solo Dam); four player slots lay out the HUD as a four-way split screen
unless co-op asks for solo's layout.

## How it works

- **Lobby.** Host Lobby > Mode: Co-op mission. Friendly fire is the only
  Match option. A listed co-op game shows "Co-op campaign, in the menus" or
  "Co-op campaign: <mission>, <difficulty>"; its stage byte is
  `0x80 | LEVELID` (90 = the menus). Up to four players.
- **Menus** (port/src/net/net_coop_menu.c). Launch takes every headset into
  the solo front end (the title stage; the session lasts through it). Each
  player picks their own folder, which leads straight to mission select.
  From there the host drives: mission, difficulty, 007 options, briefing,
  debrief, statistics. The others show the host's screen, cursor and
  choices (NET_MSG_COOP_MENU, 10 Hz, a screen change reliable) drawn from
  their own saves, and take no input of their own. The host's Start loads
  the mission everywhere as a round reset (netCoopHostStartMission).
- **Intro.** Each headset plays the solo intro (the same camera everywhere,
  from the party's seed), driven by its own player: its own skip, then the
  swirl and first person. The mission timer is each headset's own.
- **Pause.** START opens the solo watch. Nothing freezes: that player
  stands still and can be shot. The host's Abort ends the mission for the
  party; a teammate's leaves the party.
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
- **End.** The host's mission end (complete, failed, everyone down,
  aborted) ends it for everyone, with every player's kills and hits as the
  host counted them. Each headset saves a completion to its own folder;
  after 4 s the party is back in the menus at the debrief (KIA when everyone
  went down), then statistics (each player's own, plus a team kill line),
  and the host's Next goes on as solo does (Cradle to Cuba).
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

0. One headset, the host alone: Launch shows the folders; pick one, then
   Dam / Agent / briefing / Start. The intro plays and skips; START opens
   the watch (objectives, abort) while guards keep shooting; the radar and
   its health/armour arcs sit together. Abort: debrief "aborted",
   statistics, Next.
1. Two headsets: host Co-op; the other joins. Each picks a folder, then
   follows the host's mission select, briefing and Start. Both spawn near
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

## Enemy gun drops (#114, 2026-10-04)

The joining player could not see or collect a dead guard's gun. Client
guards bypass the host's death code, which calls `propobjSetDropped` to
prepare a projectile before `chrTick` detaches the weapon. `coopSyncHand`
only set `CHRHIDDEN_DROP_HELD_ITEMS`; without that preparation, `objDrop`
returned false and left the gun attached to the guard.

The client now calls `propobjSetDropped(prop, DROPTYPE_DEFAULT)` before
queuing the drop for each disappearing hand weapon on a dying/dead guard.
The normal character tick detaches and activates it, using the held gun's
world position when visible, or the guard's position when off screen.
Ordinary pickups remain per headset, and protocol 18 is unchanged. Live
guards putting away or replacing a gun still use the existing paths.

Validation at code commit `88c978e`:

- The new native regression failed on the original code: the client gun
  stayed attached after the drop tick. It passes with the preparation call.
- `python port/tests/test_multiplayer.py`: all 63 tests pass. The drop
  fixture extracts the production hand sync, projectile preparation,
  `objDrop`, detach/active-list functions and the character's drop loop.
  It covers both hands and dual guns, dying/dead states, visible/off-screen
  placement, repeated updates, local removal without recreation, separate
  host/client copies, and live hand removal/replacement. World services and
  collection removal are stubbed; this does not test inventory awards or
  headset rendering.
- `android/gradlew.bat :app:assembleDebug --console=plain`: successful,
  arm64-v8a with NDK 25.1.8937393; the APK identifies code commit `88c978e`.

**Pending acceptance:** two headsets, Dam or Facility. Have the host and
client each kill guards with bullets and explosions; both should see the
guns fall and collect their own weapon/ammo. Picking up one headset's copy
must leave the other's available. Repeat with a guard dying outside the
client's view, then look back and collect the drop. Watch through several
guard updates to confirm a collected gun does not reappear.

Historical drops from guards already removed, or dynamically spawned
guards that died before joining, are not reconstructed by this fix.

## Mission gadgets and the tank (#128, #129)

A host-only script can give Bond an item with `AI_BondCollectObject`, and
Dr Doak can instead drop the door decoder. Either way the pickup landed in
one slot, on the host, and the other headsets' watch never listed it.
Guard guns stay per headset. A mission gadget does not: bomb case through
the watch magnets, plus the DAT tape. Keys, documents, the Golden Gun and
the flag token are unchanged (the last two already use `netSendSpecialTaken`).

`gevrCoopGrantItem` adds that gadget to every occupied, non-spectator slot
and tells the party (`NET_COOP_EVENT_GRANT` up, `NET_MSG_COOP_GRANT` back
down). A second grant does not add a second copy. A keyed door still checks
the headset that uses it, so a teammate's key does not open it from here.

Background lists have no guard slot, so "Bond" was the host. They now run
as the nearest living player, the same choice a guard already makes. A
client standing with Dr Doak is who the script sees.

Using the door decoder, data thief, bomb defuser, explosive floppy or DAT
tape sets `PROPSTATE_ACTIVATED` on that headset only. A client reports the
object's tag (`NET_COOP_EVENT_GADGET`); the host sets the bit, and
`AI_IFBondUsedGadgetOnObject` opens the door. Protocol 18 is unchanged:
an older build ignores the new message and event numbers.

The tank on Runway and Streets keeps one set of globals
(`g_WorldTankProp`, `g_PlayerTankProp`, `g_PlayerIsInTank`,
`g_PlayerTankYOffset`, `g_BondCanEnterTank`). Co-op ticks every player
through `MoveBond`, and another player's tick cleared those globals, so
the rider fell through the hull. A remote tick now borrows a cleared copy
and puts the rider's state back. Leaving the deck re-enables hull
collision even when the pointer is dropped. Enter, exit and tank shells
stay on this headset's player. Other headsets still see a parked tank;
the driven pose is not synced.

## The Surface 2 mine (#130)

Surface 2's background list watches the remote mine. Stuck to the tagged
helicopter, it starts a ten-second countdown and then the script destroys
the aircraft. Any other settled remote mine fails that objective ("Bomb
incorrectly placed"). Shooting the mine does not: the helicopter stays
invincible until that countdown.

Co-op was taking the multiplayer weapon path, which hangs a world copy of
the equipped item on the player. That copy is a settled remote mine, so
equipping the mine failed the objective before it was thrown. The hand
keeps the solo mine (no world copy). Guns still get one, so other players
can see them.

A thrown mine is simulated on every headset. The list runs only on the
host, and the host's copy of someone else's mine can land somewhere the
thrower's did not. The host now leaves that mine in the air until the
thrower's headset reports where it stuck (`NET_COOP_EVENT_MINE`: the
object's tag, or -1 if it landed free). The host parents its copy there,
and the list sees the same thing solo would. A co-op mission also uses
the solo fuse on a thrown mine. Protocol 18 is unchanged.

## Known limits (best effort)

- Guard shots show muzzle flash and sound on clients, but no tracers or
  wall sparks; a guard's thrown grenade is not seen flying there.
- Scripts that test "Bond" (rooms, items, cutscene control) test the
  player the guard is targeting, or the nearest living player for a
  background list. Keys and documents stay on the headset that picked
  them up; a keyed door does not accept a teammate's copy.
- A driven tank is solid here. Other headsets still see it parked.
- A co-op player's held mine, bug, camera, plastique or bomb case has no
  world model, so other headsets do not see it in the hand. Guns still do.
- After a host change, objective events only the old host had (a client's
  photo or deposit) are not carried over; the stage flags are.
- Guards a host spawned show the old chrnum on clients for a clone (cosmetic).
- Scripted and ending cutscenes (branch coop-cutscenes, not yet played on
  two headsets): the host streams its cutscene camera (eye, target, the pad
  that places it among the rooms), its screen fade and "no control"
  (NET_MSG_COOP_CINEMA, 20 Hz, a change of state reliable). The others watch
  through the host's camera, their own player standing; every other
  player's copy is out of the shot except the host's, who is Bond. Bond's
  cinema animations reach the others only as the host's moving copy; script
  music plays on the host only.
- Pickups from dead guards are per headset (each player can take the gun).
- Shooting a gun out of a guard's hand, or its held grenade, does nothing
  (the hit is the host's to apply, and that path skips it).
- After a host change, a script's world changes other than doors, text,
  stage flags and the alarm (objects moved, gas) stay as each headset had them.
