# Running a GoldenEye VR dedicated server

A dedicated server is the game without a headset: a program that hosts a
Deathmatch match with bots and keeps it open for players to join, with
nobody playing at the machine. Players join it like any other game: from the
Online page's public list, by its code, from the LAN list, or by IP. Each
player who joins takes a bot's place.

It is **experimental**. Expect rough edges, and report them with the
server's log (see After setup).

You need **your own GoldenEye 007 cartridge dump**: the USA release, 12 MiB,
usually named `ge.z64`, the same file your headset uses. Nothing from the
game comes with the server.

## Quick start

One setup script does everything: it installs the server, asks you a few
questions (your ROM, the server's name, public or private, the map) and
starts it. Run it again any time to update the server or change those
answers.

### On a Windows PC

Open **PowerShell** (Start, type `PowerShell`, Enter) and paste:

```powershell
irm https://raw.githubusercontent.com/MrSco/goldeneye-vr/claude/dedicated-server-scope/tools/gevr-server-setup.ps1 | iex
```

It sets up WSL (Windows' built-in Linux) if your PC doesn't have it. That
takes one restart, after which the setup carries on by itself. The server
gets a Linux of its own, separate from any you already have. A window asks
you to pick your ROM.

The server runs while you are signed in to Windows. Sleep or shutdown stops
it, as it does any program.

### On a Linux machine or a cloud server (Oracle Cloud, a Raspberry Pi 5)

Ubuntu 24.04 or Debian 13 (or newer), 64-bit. Copy your ROM to the machine
first (for example `scp ge.z64 ubuntu@your-server:` from your PC), then:

```sh
curl -fsSL https://raw.githubusercontent.com/MrSco/goldeneye-vr/claude/dedicated-server-scope/tools/gevr-server-setup.sh | sudo bash
```

The server starts with the machine and restarts if it stops.

## What the setup asks

| Question | What to answer |
| --- | --- |
| Path to the ROM | Where you copied `ge.z64`. A `.v64` or `.n64` dump works too. |
| Server name | What players see in the lists, 15 characters at most. |
| List it publicly? | Yes: anyone finds it in the Online list. No: players join with a code. |
| Map number | From the list it shows; 34 is Facility. |
| Players | 2 to 8, counting the server's own seat (8 lets 7 people play). Bots fill the empty places. |
| Server key | Press Enter to skip (see Server keys). |
| Let players on your own network join? | Windows only (see Players on your own network). |

At the end it shows the **join code** (Online > Join by code on the headset).

## After setup

On Windows, type `wsl -d GoldenEyeVR-Server` in PowerShell first. That opens
the server's Linux, and the commands below then work as shown.

| To | Run |
| --- | --- |
| See its log | `journalctl -u gevr-server -f` (Ctrl+C stops watching) |
| Find the join code | `journalctl -u gevr-server \| grep "online as" \| tail -1` |
| Change the name, map or listing | run the setup again |
| Update to a new release | run the setup again |
| Restart it | `sudo systemctl restart gevr-server` |
| Stop it | `sudo systemctl stop gevr-server` |
| Remove it | `sudo bash /opt/gevr-server/gevr-server-setup.sh --uninstall` |

To remove it from Windows altogether (its Linux, its firewall rules and the
task that keeps it running), paste this in PowerShell:

```powershell
powershell -ExecutionPolicy Bypass -File "$env:LOCALAPPDATA\GoldenEyeVR-Server\gevr-server-setup.ps1" -Uninstall
```

**Every other match setting** (weapons, scenario, length, health, bot
difficulty) is in `/var/lib/gevr-server/goldeneye-vr.ini`. Edit its `Mp...`
lines, then restart the server. `/opt/gevr-server/gevr-server --list` prints
the values each line takes. A server hosts Deathmatch only (`MpMode=0`).

The log writes a status line every 10 seconds:

```
server: 60.0 game frames/s, CPU 3.5% of a core, 8 players, phase 2, rss 34 MB
```

60 game frames a second is full speed; much lower means the machine is too
slow or too busy. Phase 1 is warmup (waiting for players, or between
rounds), phase 2 a round in progress. A joining player shows as
`net: NAME takes the place of BOT`. An empty server idles in warmup: its
bots stand still and the game runs 5 frames a second (the line ends in
`, idle`), using almost no CPU. The first player to join wakes it
(`server: a player joined, waking`), a round starts 10 seconds later, and
once everyone has left it goes back to warmup and idles again.

**Players must have the same ROM** as the server: a join from someone whose
ROM differs is refused with "this game's GoldenEye ROM is not the same as
yours". They also need **the same GoldenEye VR version**, so update the
server with each release.

## Server keys

Without a key the server works, but the lobby service renews its listing
every two hours and a private server's code changes then. A server key from
the GoldenEye VR project keeps one code for as long as the server runs, and
marks it as a dedicated server in the lists. Paste the key when the setup
asks (or run the setup again to add one). The setup stores it where only the
server can read it.

## Players on your own network

Players over the internet (the public list or a code) reach the server with
no setup on your side.

**Windows:** for the LAN list and joins by IP, WSL has to share Windows'
network ("mirrored" networking, Windows 11 22H2 or newer). The setup offers
to turn that on and opens Windows' firewall for the server. Mirrored
networking applies to every Linux in WSL, Docker's too.

**Linux:** the setup opens the ports in the machine's own firewall (ufw, or
the iptables rules Oracle's images ship with).

**Oracle Cloud** also blocks ports outside the machine. Joins by code work
without it, but opening the ports makes joins more reliable and lets players
join by IP. In the Oracle console: Networking > Virtual cloud networks > your
network > Security lists > Default Security List > Add Ingress Rules:

| Source CIDR | IP protocol | Destination port range |
| --- | --- | --- |
| `0.0.0.0/0` | UDP | `27007` |
| `0.0.0.0/0` | UDP | `27010-27049` |

The ports, for any other router or firewall:

| Port | For |
| --- | --- |
| UDP 27007 | Joins by IP and from the LAN list |
| UDP 27010-27049 | Joins over the internet (one port per player joining) |
| UDP 27008 | The LAN list's announcements (on your own network only) |

The server also connects out: HTTPS to `lobbies.goldeneyevr.com`, and UDP to
`stun.cloudflare.com` and `turn.cloudflare.com` (port 3478).

## What it needs

- 64-bit Linux (x86-64 or ARM64) with glibc 2.39 or newer, and systemd; on
  Windows, Windows 10 version 2004 or newer. An Oracle Cloud Always Free
  Ampere A1 instance is plenty. The ARM64 build has so far been tried under
  emulation, not on real ARM hardware.
- About 60 MB of memory and a few percent of one CPU core for 8 players. It
  sends about 6 Mbit/s with 8 players.

## Setup options

`gevr-server-setup.sh` takes answers on the command line too, for scripting:
`--rom PATH`, `--name NAME`, `--public` or `--private`, `--map ID`,
`--players N`, `--key-file FILE`, `--version TAG` (a release tag; the newest
with a server otherwise), `--from TARBALL` (a server download you already
have), `--yes` (take the defaults instead of asking) and `--uninstall`.
Inside an unpacked server download, `sudo bash gevr-server-setup.sh`
installs that download.

`gevr-server-setup.ps1` takes `-Rom PATH`, `-Uninstall`, `-SkipNetwork`
(leave the firewall and WSL networking alone) and `-NoKeepAlive` (don't keep
the server running after sign-in).

## Without the setup script

Everything the setup does, by hand: the release's server downloads
(`gevr-server-VERSION-linux-x86_64.tar.gz` or `-aarch64`) hold the server,
the lobby bridge, the systemd unit and this guide.

```sh
sudo apt install libopus0          # the test.16 downloads need it; later ones don't
tar -xzf gevr-server-VERSION-linux-ARCH.tar.gz
sudo mkdir -p /opt/gevr-server /var/lib/gevr-server
sudo cp gevr-server-VERSION-linux-ARCH/* /opt/gevr-server/
sudo useradd --system --home /var/lib/gevr-server --shell /usr/sbin/nologin gevr
sudo cp ge.z64 /var/lib/gevr-server/
sudo chown -R gevr:gevr /var/lib/gevr-server
cd /var/lib/gevr-server
sudo -u gevr /opt/gevr-server/gevr-server --seconds 5    # writes goldeneye-vr.ini
sudo cp /opt/gevr-server/gevr-server.service /etc/systemd/system/
sudo systemctl daemon-reload
sudo systemctl enable --now gevr-server
```

The unit lists the server publicly as "GEVR Server". To change that, run
`sudo systemctl edit gevr-server` and add an `ExecStart` of your own:

```ini
[Service]
ExecStart=
ExecStart=/usr/bin/python3 /opt/gevr-server/gevr_lobby_bridge.py --public --restart --key-file /var/lib/gevr-server/server.key -- /opt/gevr-server/gevr-server --name "My Server" --port 27007 --ice-ports 27010-27049
```

The lobby bridge (`gevr_lobby_bridge.py [options] -- SERVER [SERVER OPTIONS]`)
lists the server online: `--public` (otherwise private, by code),
`--restart` (start the server again whenever it ends), `--key-file FILE`
(or `GEVR_SERVER_KEY` in the environment). The server (`gevr-server`) takes
`--name NAME`, `--port N`, `--ice-ports A-B`, `--list`, `--seconds N`,
`--bots-only` (rounds with nobody in them, for testing) and
`--beacon-to IP`. Without the bridge, `gevr-server` alone still takes joins
by IP and from the LAN list. One match per server process: to run more on
one machine, give each its own data folder, ports and service.

### Building from source

On any Linux with cmake, ninja, git and zlib's headers (Ubuntu:
`sudo apt install cmake ninja-build git zlib1g-dev python3`):

```sh
git clone --branch claude/dedicated-server-scope https://github.com/MrSco/goldeneye-vr.git
cd goldeneye-vr
sh tools/gevr_build_server.sh
```

(The server is on that branch until it reaches `main`; the VERSION file in a
download names the commit it was built from.) The server is then
`~/gevr-server-build/gevr-server`; `sudo bash tools/gevr-server-setup.sh
--from` a package made by `tools/gevr_package_server.sh` installs it. The
build script uses the system's clang if there is one, and otherwise unpacks
Ubuntu's clang 18 into `~/clang18` without needing root.
