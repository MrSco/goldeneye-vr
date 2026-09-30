# GoldenEye VR multiplayer

This is an experimental native Quest multiplayer mode. It uses ENet and borrows command and serialization ideas from the Perfect Dark PC port. Each headset simulates the match locally; the host relays player state and resolves reported player damage. The launcher can browse public internet games or join an unlisted private game by code. LAN and direct IP remain available.

## Network setup

- Use the same APK version on every headset. The current game and discovery protocol version is `8`; the lobby service lists and joins only games on the same protocol, so a v0.3.5 headset (protocol 6) or an earlier 0.3.6 test build (protocol 7) cannot join a protocol 8 game.
- The host listens for ENet game traffic on UDP `27007`.
- LAN discovery broadcasts on UDP `27008`. If discovery does not work on the Wi-Fi network, connect to the host's local IP directly.
- Internet games use the lobby service at `lobbies.goldeneyevr.com` for discovery and ICE signaling. Native libjuice carries ENet datagrams directly where possible and through Cloudflare TURN when needed, so players do not configure router forwarding. The host can have a mix of LAN and internet players in the same four-player lobby.
- Internet hosting requires the lobby Worker and Cloudflare TURN key described in `services/lobbies/README.md`. If they are unavailable, LAN and direct IP still work.
- The host picks the stage, a character and the weapons (the game's own multiplayer weapon sets, Slappers only to Golden Gun). The LAN list on the Join tab shows each game's stage, weapons and players.
- A match supports up to four occupied, consecutive player slots. The host launcher requires at least two players, all ready, and a stage with enough slots before launch.

## Implemented messages

- `NET_MSG_HELLO`, `NET_MSG_WELCOME`, `NET_MSG_LOBBY_STATE`, `NET_MSG_LOBBY_READY`, and `NET_MSG_LOBBY_CHARACTER` manage the lobby. A player whose name is already in a slot on a dead connection takes that slot back. After a host migration `HELLO` names the slot the player had and `WELCOME` names the host's slot (protocol 8).
- `NET_MSG_LOBBY_HANDOFF` (protocol 8) gives each client the internet lobby's code and owner token, the LAN beacon's name and the party size, so an elected host can carry the match on (see below).
- `NET_MSG_STAGE_VOTE` and `NET_MSG_STAGE_VOTES` (protocol 8) carry each player's next-map vote to the host and the tally back. `NET_MSG_ROUND_RESET` then names the next round's stage.
- `NET_MSG_START_MATCH` sends stage settings, character choices, player count, and the initial random seed.
- `NET_MSG_PLAYER_STATE` sends position, movement, head angle, stance, weapon ID, firing input, controller pose, and (protocol 7) the right gun's barrel in world space. Remote movement and held weapon models use this state; the copy's shots leave the owner's barrel. Remote 6DoF hand posing is not yet applied to character models.
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

## Next map and host migration

**Next map.** The multiplayer pause menu (START) has a NEXT MAP row after MIC: the right stick picks a map (only maps with room for the party; past either end is no vote), the value shows this player's vote and how many share it. When the host returns everyone to the warmup after the results, the most voted map is the next round's stage (the lowest slot's vote on a tie; with no votes the stage stays). The host's own vote counts like any other, so a host alone in voting picks the map.

**Host migration.** If the host quits (the pause menu's exit, or holding Menu) or vanishes (about 6 s of silence), the match goes on: the lowest remaining slot becomes the host and serves the same match from its own slot; the others rejoin it the way they came in, through the same internet lobby (the new host resumes it with the owner token the old host shared, and the old host left the lobby in place rather than deleting it) or through the LAN beacon under the game's name. Their slots, characters and scores are kept for 20 s; a player back in time carries on where they stood, one who is not is dropped. A client that finds no new host within 30 s returns to the launcher. Alone after the host left, the survivor hosts the warmup. Slot 0, the first host's, stays empty after a migration; it is the only slot a later host cannot hand out. Votes and the countdown restart with the new host.

## Voice chat

Allow microphone access when hosting or joining to talk. Denying it leaves voice receive available. The lobby uses full-volume voice; in a match voices pan with direction and fade from 2 m to silence at 20 m, including while dead or spectating. Walls do not occlude voice. Use **Microphone muted** in the lobby or the **Microphone** row on the watch's Game Options page. In play, hold left **X + Y** for half a second to toggle mute. The mute choice is remembered. Leaving the game or opening the Quest system menu stops capture and transmission.

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

The mode still needs a two-headset playtest of step 8, and the migration paths need three headsets for the rejoin case. Also test two separate home networks, a phone hotspot, a four-player game with mixed LAN and internet joins, private code visibility, and reconnecting after a disconnect. Confirm that damage and respawn state agree on all headsets after several kills.
