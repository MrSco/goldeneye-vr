# Running a GoldenEye VR dedicated server

A dedicated server is the game without a headset: a Linux program that hosts
a Deathmatch match with bots and keeps it open for players to join, with
nobody playing at the machine. Players join it like any other game: from the
Online page's public list, by its private code, from the LAN list, or by IP.
Each player who joins takes a bot's place.

It is **experimental**. Expect rough edges, and report them with the server's
log (see Logs below).

## What you need

- **Linux on x86-64 or ARM64** with glibc 2.39 or newer: Ubuntu 24.04,
  Debian 13, or newer. An Oracle Cloud Always Free Ampere A1 instance or a
  Raspberry Pi 5 on a 64-bit OS should do (the ARM64 build has so far been
  tried under emulation). For anything older, build it yourself (Building
  from source, below).
- **Python 3.8 or newer**, for the lobby bridge that lists the server online.
- **Your own GoldenEye 007 cartridge dump**: the USA (NTSC-U) release,
  12 MiB, named `ge.z64`. Nothing from the game comes with the server.
  Players must have the same dump: a join from someone whose ROM differs is
  refused with "this game's GoldenEye ROM is not the same as yours".
- **Resources**: an 8-player match takes about 60 MB of memory and a few
  percent of one CPU core. It sends about 6 Mbit/s with 8 players.
- **The same GoldenEye VR version as your players.** Players on another
  version can't join; update the server with each release.

## Install

As root (or with sudo), on the server:

```sh
tar -xzf gevr-server-VERSION-linux-ARCH.tar.gz
sudo mkdir -p /opt/gevr-server /var/lib/gevr-server
sudo cp gevr-server-VERSION-linux-ARCH/* /opt/gevr-server/
sudo useradd --system --home /var/lib/gevr-server --shell /usr/sbin/nologin gevr
sudo cp ge.z64 /var/lib/gevr-server/
sudo chown -R gevr:gevr /var/lib/gevr-server
```

## Choose the match

The match comes from `goldeneye-vr.ini` in the data folder, the same file the
headset keeps its settings in. Started without one, the server writes it with
a server's match (Facility, 8 players, bots filling the empty places). Start
it once to create the file, then edit the `Mp...` lines:

```sh
cd /var/lib/gevr-server
sudo -u gevr /opt/gevr-server/gevr-server --seconds 15
sudo -u gevr nano goldeneye-vr.ini
```

`/opt/gevr-server/gevr-server --list` prints every value these lines take,
with names. The ones that matter most:

| Line | Meaning |
| --- | --- |
| `MpMode=0` | Deathmatch. A server hosts nothing else yet. |
| `MpStage=34` | The map (34 is Facility). |
| `MpScenario`, `MpWeaponSet`, `MpLength`, `MpHealth` | As on the headset's Host page. |
| `MpMaxPlayers=8` | Players at most, the server's own seat included (so 7 can play). |
| `MpBotMode=1` | Bots fill the empty places. `2` keeps a fixed number (`MpBotCount`). |
| `MpBotDifficulty=2` | Normal. |
| `MpNextRound=0` | Players vote for the next map. |

The server rewrites the file every time it starts, with every setting in it.
Your values stay; comments do not.

## Try it

From the data folder, run the server through the lobby bridge:

```sh
cd /var/lib/gevr-server
sudo -u gevr python3 /opt/gevr-server/gevr_lobby_bridge.py -- /opt/gevr-server/gevr-server --name "My Server"
```

The lobby is private, so the log shows its code: `lobby: online as ABCD1234`.
Join it from the headset's Online page with that code. Ctrl+C stops the
server and removes the lobby. Add `--public` (before `--`) to put it on
everyone's public list.

## Run it as a service

```sh
sudo cp /opt/gevr-server/gevr-server.service /etc/systemd/system/
sudo systemctl daemon-reload
sudo systemctl enable --now gevr-server
```

The service lists the server publicly as "GEVR Server" and starts a fresh
match whenever one ends (`--restart`). To change its name or keep it
private, run `sudo systemctl edit gevr-server` and add:

```ini
[Service]
ExecStart=
ExecStart=/usr/bin/python3 /opt/gevr-server/gevr_lobby_bridge.py --restart -- /opt/gevr-server/gevr-server --name "My Server" --port 27007 --ice-ports 27010-27049
```

Without `--public` the lobby is private; its code is in the log.

## A server key

Without a key the server is listed like any headset's game: the lobby
service ends it after two hours, and the bridge registers it again under a
new code. With a server key from the GoldenEye VR team (ask on the Discord),
it is listed as a dedicated server:

- it stays listed for as long as it runs;
- it keeps the same code every time it registers, so a private server's code
  can be shared once and keeps working;
- players can tell it from a game that only calls itself "Official".

Keep the key in a file only the service reads, and pass it to the bridge:

```sh
sudo -u gevr sh -c 'umask 077; cat > /var/lib/gevr-server/server.key'   # paste the key, then Ctrl+D
```

```ini
[Service]
ExecStart=
ExecStart=/usr/bin/python3 /opt/gevr-server/gevr_lobby_bridge.py --public --restart --key-file /var/lib/gevr-server/server.key -- /opt/gevr-server/gevr-server --name "My Server" --port 27007 --ice-ports 27010-27049
```

The code belongs to the key and the server's `--name`: two servers on one key
need different names. A key the lobby service doesn't know is refused, and
the log says so (`the lobby service refused the server key`).

## Ports

| Port | For |
| --- | --- |
| UDP 27007 | Joins by IP and from the LAN list (`--port`) |
| UDP 27010-27049 | Joins from the internet lobby (`--ice-ports`) |
| UDP 27008 | The LAN list's announcements (on your own network only) |

The server also needs to reach out: HTTPS to `lobbies.goldeneyevr.com`, and
UDP to `stun.cloudflare.com` and `turn.cloudflare.com` (port 3478). At home,
internet joins work through the router without opening anything; opening
the ports above helps players whose own network is strict.

**Oracle Cloud:** add ingress rules for UDP 27007 and UDP 27010-27049 to the
instance's subnet security list (Networking > Virtual cloud networks > your
network > Security lists). Oracle's Ubuntu images also block them in the
instance's own firewall:

```sh
sudo iptables -I INPUT -p udp --dport 27007 -j ACCEPT
sudo iptables -I INPUT -p udp --dport 27010:27049 -j ACCEPT
sudo netfilter-persistent save
```

## Options

The server (`gevr-server`):

| Option | Meaning |
| --- | --- |
| `--name NAME` | The name in the lists, 15 characters at most |
| `--port N` | The game's UDP port (27007) |
| `--ice-ports A-B` | The UDP ports internet joins use (27010-27049); one per player joining |
| `--list` | Print the values `goldeneye-vr.ini`'s match lines take, and exit |
| `--seconds N` | Stop after N seconds |
| `--bots-only` | Start rounds with nobody in them (testing) |
| `--beacon-to IP` | Also send the LAN announcements to IP (a server in WSL2 to Windows) |

The lobby bridge (`gevr_lobby_bridge.py [options] -- SERVER [SERVER OPTIONS]`):

| Option | Meaning |
| --- | --- |
| `--public` | List the server on everyone's public list (otherwise private, by code) |
| `--restart` | Start the server again whenever it ends |
| `--key-file FILE` | A server key (A server key, above); `GEVR_SERVER_KEY` in the environment works too |

Without the bridge, `gevr-server` alone still takes joins by IP and from the
LAN list.

## Logs

`journalctl -u gevr-server -f` follows the log. Every 10 seconds the server
writes a status line:

```
server: 60.0 game frames/s, CPU 3.5% of a core, 8 players, phase 2, rss 34 MB
```

60 game frames a second is the game's full speed; much lower means the
machine is too slow or too busy. Phase 1 is warmup (waiting for players, or
between rounds), phase 2 a round in progress. A joining player shows as
`net: NAME takes the place of BOT`.

## Things to know

- **One match per server process.** To run more on one machine, give each its
  own data folder, `--port` and `--ice-ports` range, and its own service.
- **An empty server waits in warmup**, bots playing, and starts a round 10
  seconds after someone joins.
- **Without a key the lobby expires.** The lobby service ends it after two
  hours; the bridge registers a new one at once, but a private lobby's code
  then changes (it is in the log). A server key avoids both.
- **Updating**: stop the service, unpack the new release over
  `/opt/gevr-server`, start it again. Your data folder is kept.

## Building from source

On any Linux with cmake, ninja, git and zlib's headers (Ubuntu:
`sudo apt install cmake ninja-build git zlib1g-dev python3`):

```sh
git clone https://github.com/MrSco/goldeneye-vr.git
cd goldeneye-vr
sh tools/gevr_build_server.sh
```

The server is then `~/gevr-server-build/gevr-server`. The script uses the
system's clang if there is one, and otherwise unpacks Ubuntu's clang 18 into
`~/clang18` without needing root.
