# GoldenEye VR multiplayer

This is an experimental native Quest LAN and direct-IP multiplayer mode. It uses ENet and borrows command and serialization ideas from the Perfect Dark PC port. Each headset simulates the match locally; the host relays player state and resolves reported player damage.

## Network setup

- Use the same APK version on every headset. The current game and discovery protocol version is `4`.
- The host listens for ENet game traffic on UDP `27007`.
- LAN discovery broadcasts on UDP `27008`. If discovery does not work on the Wi-Fi network, connect to the host's local IP directly.
- The host picks the stage, a character and the weapons (the game's own multiplayer weapon sets, Slappers only to Golden Gun). The LAN list on the Join tab shows each game's stage, weapons and players.
- A match supports up to four occupied, consecutive player slots. The host launcher requires at least two players, all ready, and a stage with enough slots before launch.

## Implemented messages

- `NET_MSG_HELLO`, `NET_MSG_WELCOME`, `NET_MSG_LOBBY_STATE`, `NET_MSG_LOBBY_READY`, and `NET_MSG_LOBBY_CHARACTER` manage the lobby.
- `NET_MSG_START_MATCH` sends stage settings, character choices, player count, and the initial random seed.
- `NET_MSG_PLAYER_STATE` sends position, movement, head angle, stance, weapon ID, firing input, and controller pose. Remote movement and held weapon models use this state. Remote 6DoF hand posing is not yet applied to character models.
- `NET_MSG_HIT_REPORT` sends a locally detected hit to the host. The host applies it and sends `NET_MSG_DAMAGE_EVENT` to the clients.
- `NET_MSG_RESPAWN` sends the respawning player's spawn pad and facing angle through the host. Each receiving headset runs GoldenEye's respawn routine for that player.

`NET_MSG_FIRE_EVENT`, `NET_MSG_VOIP_FRAME`, and `NET_MSG_MATCH_END` are protocol scaffolding, not complete gameplay or voice features. Discovery uses a separate UDP beacon rather than these game messages.

## Two-headset test

1. Install the same debug APK on both Quest headsets and connect them to the same LAN.
2. On one headset open **ONLINE MULTIPLAYER** > **Host Game** and choose **START HOSTING LOBBY**.
3. On the other choose **Join Game (LAN / Direct IP)**. Select the discovered host or enter its LAN IP and press **Connect**.
4. Choose a character, check **I am Ready**, then on the host press **LAUNCH MULTIPLAYER MATCH!**.
5. Check movement through doorways, weapon models, shooting in both directions, explosive damage, stereo view while the other player dies or uses the watch, and health and scores after a respawn.

The mode still needs a two-headset playtest. In particular, confirm that damage and respawn state agree on both headsets after several kills, and that reconnecting after a disconnect behaves as expected.
