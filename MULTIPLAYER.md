# GoldenEye VR multiplayer

This is an experimental native Quest multiplayer mode. It uses ENet and borrows command and serialization ideas from the Perfect Dark PC port. Each headset simulates the match locally; the host relays player state and resolves reported player damage. The launcher can browse public internet games or join an unlisted private game by code. LAN and direct IP remain available.

## Network setup

- Use the same APK version on every headset. The current game and discovery protocol version is `7`; the lobby service lists and joins only games on the same protocol, so a v0.3.5 headset (protocol 6) cannot join a protocol 7 game.
- The host listens for ENet game traffic on UDP `27007`.
- LAN discovery broadcasts on UDP `27008`. If discovery does not work on the Wi-Fi network, connect to the host's local IP directly.
- Internet games use the lobby service at `lobbies.goldeneyevr.com` for discovery and ICE signaling. Native libjuice carries ENet datagrams directly where possible and through Cloudflare TURN when needed, so players do not configure router forwarding. The host can have a mix of LAN and internet players in the same four-player lobby.
- Internet hosting requires the lobby Worker and Cloudflare TURN key described in `services/lobbies/README.md`. If they are unavailable, LAN and direct IP still work.
- The host picks the stage, a character and the weapons (the game's own multiplayer weapon sets, Slappers only to Golden Gun). The LAN list on the Join tab shows each game's stage, weapons and players.
- A match supports up to four occupied, consecutive player slots. The host launcher requires at least two players, all ready, and a stage with enough slots before launch.

## Implemented messages

- `NET_MSG_HELLO`, `NET_MSG_WELCOME`, `NET_MSG_LOBBY_STATE`, `NET_MSG_LOBBY_READY`, and `NET_MSG_LOBBY_CHARACTER` manage the lobby.
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

Known limits of protocol 7: guns dropped by a dying player and picked up are not mirrored (they live in a per-headset pool, so only setup objects carry a shared identity, in events and in the late-join snapshot). A copy's mine tripped by a moving object goes off locally without its owner's explosion. Glass another player breaks is broken here when their copy's traced shot hits it, which can differ from their own headset by a frame of position. Explosions caused by shooting another player's live grenade or mine can happen twice, once from the shooter and once from its owner's fuse.

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

The mode still needs a two-headset playtest. Also test two separate home networks, a phone hotspot, a four-player game with mixed LAN and internet joins, private code visibility, and reconnecting after a disconnect. Confirm that damage and respawn state agree on all headsets after several kills.
