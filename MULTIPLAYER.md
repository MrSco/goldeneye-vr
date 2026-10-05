# GoldenEye VR multiplayer

This is an experimental native Quest multiplayer mode. It uses ENet and borrows command and serialization ideas from the Perfect Dark PC port. Each headset simulates the match locally; the host relays player state and resolves reported player damage. The launcher can browse public internet games or join an unlisted private game by code. LAN and direct IP remain available.

## Network setup

- Use the same APK version on every headset. The current game and discovery protocol version is `18` (v0.4.6: a match may be on Statue or Cradle, which v0.4.5 rejects; `17` was v0.4.0 to v0.4.5, `16` only unreleased test builds); the lobby service lists and joins only games on the same protocol, so an earlier release or test build cannot join a v0.4.6 game.
- The host listens for ENet game traffic on UDP `27007`.
- LAN discovery broadcasts on UDP `27008`. If discovery does not work on the Wi-Fi network, connect to the host's local IP directly.
- Internet games use the lobby service at `lobbies.goldeneyevr.com` for discovery and ICE signaling. Native libjuice carries ENet datagrams directly where possible and through Cloudflare TURN when needed, so players do not configure router forwarding. The host can have a mix of LAN and internet players in the same lobby of up to eight.
- TURN is a fallback, not a gate. A headset publishes its session once STUN has found its public address; a relay candidate is added when the lobby service issues credentials (UDP 3478, with UDP 443 as a second server for networks that block 3478). If the relay is refused or unreachable, the join still goes ahead and only fails against peers that hole punching cannot reach (symmetric or carrier-grade NAT, typically phone hotspots). The log line `net: ice <id>: no relay candidate` records a direct-only session. "No internet path found" means STUN itself failed.
- Internet hosting requires the lobby Worker described in `services/lobbies/README.md`; the Cloudflare TURN key is optional and capped per month there. If the Worker is unavailable, LAN and direct IP still work.
- The host picks the stage (the game's eleven, plus Statue and Cradle from the ROM's cut multiplayer setups, #95), the player count, a character and the weapons (the game's own multiplayer weapon sets, Slappers only to Golden Gun). The LAN list on the Join tab shows each game's stage, weapons and players.
- A match supports up to eight player slots (protocol 16, issue #88). The host picks the count, two to eight, on any stage: the launcher's **Players** beside the stage, or the **PLAYERS** row of the in-match lobby page (default four, saved as `MpMaxPlayers`). It travels in the match config (`max_players`); a team scenario takes its own size instead. The count cannot drop below the connected players or a connected slot; in a match a new count applies from the next load. The host launcher requires at least two players, all ready, before launch.

## Co-op campaign

Choose **Co-op mission** in the launcher for up to four players. Each player
uses their own local save folder; the host selects the mission and difficulty.
The current source includes these fixes after v0.4.6:

- Dead guards' hand weapons drop on joining headsets, with independent
  pickups for each player. Taking your copy leaves teammates' copies available.
- Mission start preserves your chosen save folder. If the host starts before
  you finish choosing one, the first local folder is used; a missing save
  reports no recorded best time instead of crashing mission Statistics.

The reported invisible Bunker ending cutscene on a joiner remains unresolved.
Two-headset acceptance for the new gun-drop behavior is pending. See
[co-op notes](docs/issue-94-coop.md),
[debrief crash analysis](docs/crash-a16b05b5-coop-debrief.md) and
[unreleased changes](docs/releases/unreleased.md) for validation and limits.
Protocol 18 is unchanged by these fixes.

GE-X gun and arm replacements are local options. Only a player enabling them
needs the additional prepared ROM; everyone still needs their own GoldenEye
ROM and the same APK version. See [GE-X setup](docs/gex-setup.md).

## Implemented messages

- `NET_MSG_HELLO`, `NET_MSG_WELCOME`, `NET_MSG_LOBBY_STATE`, `NET_MSG_LOBBY_READY`, and `NET_MSG_LOBBY_CHARACTER` manage the lobby. A player whose name is already in a slot on a dead connection takes that slot back. After a host migration `HELLO` names the slot the player had and `WELCOME` names the host's slot (protocol 8).
- `NET_MSG_LOBBY_HANDOFF` (protocol 8) gives each client the internet lobby's code and owner token, the LAN beacon's name and the party size, so an elected host can carry the match on (see below).
- `NET_MSG_STAGE_VOTE` and `NET_MSG_STAGE_VOTES` (protocol 8) carry each player's next-map vote to the host and the tally back. `NET_MSG_ROUND_RESET` then names the next round's stage.
- `NET_MSG_START_MATCH` sends stage settings, character choices, player count, and the initial random seed.
- `NET_MSG_PLAYER_STATE` sends position, movement, head angle, stance, weapon ID, firing input, controller pose, (protocol 7) the right gun's barrel in world space and (protocol 10) the owner's health, armour and death. Remote movement and held weapon models use this state; the copy's shots leave the owner's barrel; the copy's life follows its owner: a copy takes no damage of its own (the host's damage events are applied to the local player only; a copy just plays the hit sound), it dies when its owner reports dead, credited to whoever last hurt it on that headset, and a copy dead where its owner lives respawns. So every headset counts the same deaths and the score agrees. Remote 6DoF hand posing is not yet applied to character models.
- `NET_MSG_HIT_REPORT` sends a locally detected hit to the host. The host applies it and sends `NET_MSG_DAMAGE_EVENT` to the clients.
- `NET_MSG_RESPAWN` sends the respawning player's spawn pad and facing angle through the host. Each receiving headset runs GoldenEye's respawn routine for that player.
- `NET_MSG_PROJECTILE` (protocol 7) sends a thrown or launched projectile (grenade, knife, mine or other thrown object, launcher round, rocket) with its spawn point, velocity and orientation. Each receiving headset runs the same spawner for that player's copy, so the projectile flies and bounces everywhere.
- `NET_MSG_EXPLOSION` (protocol 7) sends a damaging explosion the player caused. Receivers create it where it happened, remove that player's nearest projectile, and damage objects; hits on players still come only from the owner's hit reports. Bullet puffs and world explosions stay local.
- `NET_MSG_OBJECT_STATE` (protocol 7) sends a door the player used (its new state) or a pickup the player collected. Receivers animate the door or remove the pickup on the same regeneration timer.
- `NET_MSG_VOIP_FRAME` carries sequenced 20 ms Opus voice frames on the unreliable voice channel. The host validates and relays client frames.

The host relays every player event to the other clients. `NET_MSG_FIRE_EVENT` and `NET_MSG_MATCH_END` are protocol scaffolding. Discovery uses a separate UDP beacon rather than these game messages.

## Remote players on this headset

Each other player is a local player slot whose position is overwritten from the network each tick (`port/src/net/net_player_sync.c`). Its stand tile is walked along with the position (or found below it after a snap), because the copy's rooms and its model's height come from that tile; a stale tile filed the body in a room the view never draws (v0.3.4). The copy's gun is kept loaded so its trigger pulls fire instead of clicking and reloading. Each copy then gets its own view pass without the drawing (`lv.c` gevrRemotePlayerPass: a camera on the owner's barrel, room visibility, the on-screen prop list and the props' matrices for that camera, its projectiles stepped there, as split screen does for every player), and its shots are traced in that pass from the owner's barrel, with impacts and object damage but no player damage; their sound is placed by distance and direction. Your own shots get the same treatment when a hand fires more than 35 degrees from where you look (round a corner, beside or behind you): that hand gets a pass with the camera on its barrel before your head pass, so a player outside your view can be hit without looking. Solo keeps the head pass alone, as Perfect Dark VR does. Only the local player's gun reaches the controllers' haptics or the motion-throw state.

The copy's body aims where its owner's barrel points: its torso pitch and its turn off the view are computed from the barrel each tick (`field_2A08/field_2A0C`, what `playerTick` reads for a drawn view), since online the copy's own view is never built. The chr's `aimendback` is `playerTick`'s alone: written elsewhere in degrees it stood whenever an animation blend skipped that write, and the torso pitched through whole turns each time the copy changed step. The turn toward a strafe step advances once a frame, not once per view pass. A copy's crouch comes with the state, not as a C-down press (which stepped it back or turned its look, by control style).

Melee: the copies swing when their owner does, on the trigger or on a swing of either hand (`gevrHandChopTick`): the swing is heard from where they stand, a landed slap plays its punch on the player hit, and the damage still comes only from the owner's hit report. The ROM has no third-person slap animation (GoldenEye's split screen shows none either), so the copy's arm does not move.

Known limits of protocol 8: guns dropped by a dying player and picked up are not mirrored (they live in a per-headset pool, so only setup objects carry a shared identity, in events and in the late-join snapshot). A copy's mine tripped by a moving object goes off locally without its owner's explosion. Glass another player breaks is broken here when their copy's traced shot hits it, which can differ from their own headset by a frame of position. Explosions caused by shooting another player's live grenade or mine can happen twice, once from the shooter and once from its owner's fuse. Other players' hands do not move (the held gun models are the ROM's third-person `PROP_CHR*` models; there are no better ones).

## In-level lobby, voting, and host migration

**In-level lobby.** The online pause menu includes an online-only `LOBBY` page right after `PAUSE`. It uses a unified scrolling row framework for audio controls, game ballots (NEXT MAP and NEXT WEAPONS), host match options (SCENARIO, LENGTH, HEALTH, DUAL WIELD, LOADOUTS, CUSTOM 1–4), player character selection (all 64 characters), personal spawn loadout (LOADOUT 1–4), favorite toggles (FAV MAP, FAV SET), and round actions (START MATCH, RETURN TO LOBBY). Host-only rows appear dimmed on clients. The results screen permits paging to `LOBBY` so all players can vote or configure their next round.

**Round transitions.** Settings edits made in the lobby take effect at the next match load; the active round stays frozen on its latched settings. Results screen: host A/START initiates a 20-second countdown; host B returns to an indefinite warmup lobby on the current map; after 30 seconds without host input, continue begins automatically. From warmup, START MATCH initiates a 10-second countdown. At countdown zero, ballots/rotations resolve, and the game reloads once directly into play.

**Spectators.** Late joiners entering during active play or results spectate until the next round. The spectator camera rides a living player's position while preserving independent 6DoF headset yaw. Pressing A/B cycles between living players. Spectators cannot shoot, interact, pick up items, or appear on radar/tags.

**Online dual wielding & spawn loadouts.** Dual wielding supports Off, Doubles (picking up a second copy of a dual-wieldable weapon creates a pair), and Any Two (the left-hand weapon panel allows selecting any held weapon for the left hand). Both hands synchronize aim and firing over the network, with dual view passes for remote copies. Spawn loadouts grant the applied 4-gun kit at initial spawn and respawn.

**Host migration.** If the host quits (the pause menu's exit, or holding Menu) or vanishes (about 6 s of silence), the match goes on: the lowest remaining slot becomes the host and serves the same match from its own slot; the others rejoin it the way they came in, through the same internet lobby or LAN beacon. Slots, characters and scores are preserved for 20 s.

## Eight players (protocol 16)

Issue #88 raises the slots from four to eight. Every slot is still a game player number on every headset (`MAX_PLAYER_COUNT` 8 under `GEVR`; split screen keeps its four, since the front end counts controllers and lays its menus out 2x2).

- **Packets.** WELCOME, the ballot tally (VOTES) and the late-join MATCH_SNAPSHOT outgrew their buffers at eight (67, 17 and 715 bytes); the buffers are larger and a WELCOME or snapshot that does not fit is logged instead of sent short. The combat epoch keeps the host's slot in four low bits. The host's ENet peer pool has one spare for a rejoin, and an internet host takes seven ICE peers.
- **Start pads.** Every headset still deals the start pads from the match seed. Any stage takes any count (the game capped Egypt at two and Caverns, Bunker II and Archives at three, for split screen). A stage with fewer start pads than players gains more at its ammo spots, one per missing pad, each the farthest from every start pad so far (11 to 40 m on the ROM's maps that need them, in line with Rare's own spacing there; the old fallback stood a player 1 m from another). A spot must sit on its floor (not on a table or crate), with floor a player's width around it and no solid object within 1.5 m; a blocked spot tries 60 cm off it in eight directions, as Perfect Dark's `chrAdjustPosForSpawn` does (`bondview_r.c gevrAddOnlineStartPads`, log `spawn: N players, M start pads; K more at ammo spots`). Ammo, not weapon or armour spots, so standing on one hands out nothing that matters. Respawns pick among the added pads too, with the game's own 10 m rule. Only if no ammo spot qualifies does a later slot share a pad, standing a metre off it per earlier sharer (`gevrSpreadStartPad`, log `spawn: slot N shares its start pad`). The pads each stage has are logged at load (`stage: intro cams=... pads=...`).
- **Teams.** Team 3v3 and Team 4v4 (scenarios 8 and 9) play by the game's 2v2 rules with our team sizes. A team is ranked first or second whatever its size.
- **Thrown objects.** An object's owner had two bits (players 0..3); the owner's third bit is bit 20 of the runtime flags (`RUNTIME_OWNER` in `bondconstants.h`), so grenades, mines and rockets of players 5..8 credit and detonate correctly.
- **Controllers.** The N64 had four ports, and the ramrom demos record four. Slots 4..7 read their pads once a frame through `src/joy.c gevrReadSlotPads` (the local headset's controllers, or a copy's trigger), so a headset in slot 5..8 has its controls.
- **Stage look.** Past four players a stage loads its objects, fog, glass and debris budgets as for four (the setup files and fog tables stop at four). Each player past four adds a share to the vertex/matrix buffer that the view passes draw from.
- **Not yet done** (from the issue's plan): batching the host's relayed player states into one packet per client, running the other players' view passes only when they fire, spawn protection, and binaural voice for only the nearest speakers. At eight players the host relays about 6 Mbit/s of player state at 90 Hz, and every headset runs up to fourteen view passes a frame; measure both on a Quest before deciding.

## Voice chat

Allow microphone access when hosting or joining to talk. Denying it leaves voice receive available. The lobby uses full-volume voice; in a match voices pan with direction and fade from 2 m to silence at 20 m. Spectators speak and hear only other spectators, without positional attenuation. In play, hold **Menu + physical right B** to toggle microphone mute (displays "MIC MUTED" / "MIC ON"). The mute choice is remembered. Leaving the game or opening the Quest system menu stops capture and transmission.

## Two-headset test

1. Install the same debug APK on both Quest headsets and connect them to the same LAN.
2. On one headset open **ONLINE MULTIPLAYER** > **Host Game** and choose **START HOSTING LOBBY**.
3. On the other choose **Join Game (LAN / Direct IP)**. Select the discovered host or enter its LAN IP and press **Connect**.
4. Choose a character, check **I am Ready**, then on the host press **LAUNCH MULTIPLAYER MATCH!**.
5. Check movement through doorways, weapon models, shooting in both directions, explosive damage, stereo view while the other player dies or uses the watch, and health and scores after a respawn.
6. Check lobby voice, distance and direction in the match, both mute controls, and continued voice after a death. Repeat with microphone permission denied on one headset to check listen-only mode. The log shows `voice: microphone permission granted` and `voice: capture open, 16000 Hz 1 ch` when the microphone is live, or `voice: capture open failed` with SDL's reason.
7. Protocol 7 checks, on both headsets:
   - Bodies: the other player is visible after spawn, after walking through several rooms, up and down stairs and ramps, and after a death and respawn; the walk animation plays, the name tag shows, bullets hit. The log prints `net: remote N at x,y,z tile room R, prop rooms ... on screen` every 5 s per remote slot.
   - Speed: forward walking speed is the same hosting solo and with two players; strafing is normal in both.
   - Gunfire: remote shots sound like the gun, fade and pan with distance, show a muzzle flash, and hit walls where the shooter aimed. No reload clicking. Your controllers do not buzz when they shoot.
   - Shooting without looking: aim the gun at the other player while looking elsewhere (beside you, behind you, round a doorframe with only the gun through it). They take damage and the impacts land where the gun pointed, on both headsets.
   - Throws and explosions: thrown grenades and knives fly, rockets fly, and explosions appear once at the true spot. Barrels and crates break on both headsets. Log: `projectile tx/rx`, `explosion tx/rx`.
   - World: doors the other player opens open for you; pickups they take vanish for you and come back on the same timer. Log: `object tx/rx`.
   - A v0.3.5 headset cannot see or join a protocol 7 game.
8. Protocol 8 checks (the 2026-09-29 report's fixes), on both headsets:
   - Start: the players spawn on different pads at the match start and after every round reset.
   - Body: strafe left and right with the left stick in front of the other player; their view of your body turns with the step and stays upright, no pitching or twitching. Their torso follows where your gun points.
   - Melee: slap the other player (trigger, or a swing of the hand) with fists; on their headset they hear the swing from you, the hit's punch when it lands, and take the damage; slapping the air is heard as a whiff from where you stand.
   - Countdown: a player joining a warmup starts a 10 s countdown on every headset, the round fades out at its end and in on the reload; after the results, 15 s.
   - Sounds: only the player hit hears their own hurt sound at full volume; the others hear it from that player's position.
   - Doors: the wall over a vertical door does not flicker while the door is up.
   - Quit: leave the match through the pause menu (or hold Menu, A). On the host the player is gone within a second (log `Peer disconnected`), and a rejoin from the launcher works at once (`is back on a new connection` if the old connection was still listed). Force-quit the app instead: the host drops the player after about 6 s.
   - Next map: pick a map on the NEXT MAP row on one headset, another on the other; the value shows each player's own pick with its count. After the results and the host's exit to the warmup, the reload is on the most voted map (host log `Next map: ...`), and the tie goes to the lowest slot.
    - Host migration: with two players in a match, the host quits through the pause menu. On the other headset the match continues: log `Host left: this headset (slot 1) takes the match over`, then `Hosting the match from slot 1`; alone, the warmup starts. With three players, the host quits: slot 1 hosts, slot 2 logs `Host left: slot 1 takes over; rejoining as slot 2`, `launcher: rejoining ...`, and the new host logs `... is back in slot 2 after the host change`; the match goes on with scores kept, and slot 2 stands where it stood. Repeat with the old host force-quit instead (about 6 s later the same happens). Repeat over the internet lobby (the new host logs `took the match over with the internet lobby`).
9. Protocol 9 checks (the full punch list), on both headsets:
   - In-level lobby: pause menu pages to `LOBBY`; rows scroll with right stick, options adjust, host-only rows are dimmed on clients.
   - Ballots & Rotation: both map and weapon ballots are available; in shuffle/playlist modes, next map/set picks are generated from host favorites.
   - Results & Warmup: host pressing A on results triggers 20s countdown; B returns to warmup lobby; START MATCH triggers 10s countdown. The round reloads once directly into live play.
   - Spectators: a late joiner during live play spectates a living player; A/B buttons cycle between living players; spectator voice is heard only by other spectators.
   - Dual wielding: with dual wield enabled (doubles or any-two), both hands fire with independent aim and view passes, and gunfire sounds originate from the firing hand.
   - Loadouts: players spawn with their selected 4-gun kit, equipped with their primary weapon.

The mode still needs a two-headset playtest of steps 8 and 9. Also test two separate home networks, a phone hotspot, a four-player and an eight-player game with mixed LAN and internet joins, private code visibility, and reconnecting after a disconnect. Confirm that damage and respawn state agree on all headsets after several kills.
