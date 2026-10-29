#!/bin/bash
# GoldenEye VR dedicated server: install, set up and start, in one go.
#
#   curl -fsSL https://raw.githubusercontent.com/MrSco/goldeneye-vr/claude/dedicated-server-scope/tools/gevr-server-setup.sh | sudo bash
#
# or, from a downloaded copy or an unpacked server download:
#
#   sudo bash gevr-server-setup.sh [options]
#
# It asks a few questions (your ROM, the server's name, public or private,
# the map) and does the rest: downloads the newest server release for this
# machine, installs it, opens the firewall, and runs it as a service that
# starts with the machine. Run it again to update the server or change its
# settings; your settings are kept. On Windows, run gevr-server-setup.ps1
# instead: it sets up WSL and runs this script inside it.
#
# Options (all optional; anything not given is asked, or defaulted with --yes):
#   --rom PATH          your GoldenEye USA ROM (ge.z64)
#   --name NAME         the server's name in the lists (15 characters at most)
#   --public | --private
#   --map ID            the map's number (gevr-server --list), 34 is Facility
#   --players N         players at most, 2-8, the server's own seat included
#   --key-file FILE     a server key from the GoldenEye VR project
#   --version TAG       a release tag (default: the newest with a server)
#   --from TARBALL      install this server download instead of downloading
#   --yes               take the defaults instead of asking
#   --uninstall         stop and remove the server (your data folder is kept)
set -eu

REPO=MrSco/goldeneye-vr
APP=/opt/gevr-server
DATA=/var/lib/gevr-server
UNIT=/etc/systemd/system/gevr-server.service
OVERRIDE=/etc/systemd/system/gevr-server.service.d/setup.conf
GAME_PORT=27007
ICE_PORTS=27010-27049
LAN_PORT=27008

ROM="" NAME="" VISIBILITY="" MAP="" PLAYERS="" KEYFILE="" VERSION="" FROM="" YES=0 UNINSTALL=0
while [ $# -gt 0 ]; do
	case "$1" in
	--rom) ROM=$2; shift ;;
	--name) NAME=$2; shift ;;
	--public) VISIBILITY=public ;;
	--private) VISIBILITY=private ;;
	--map) MAP=$2; shift ;;
	--players) PLAYERS=$2; shift ;;
	--key-file) KEYFILE=$2; shift ;;
	--version) VERSION=$2; shift ;;
	--from) FROM=$2; shift ;;
	--yes|-y) YES=1 ;;
	--uninstall) UNINSTALL=1 ;;
	-h|--help) sed -n '2,30p' "$0" 2>/dev/null | sed 's/^# \{0,1\}//'; exit 0 ;;
	*) echo "Unknown option: $1 (--help lists them)" >&2; exit 2 ;;
	esac
	shift
done

# ---- output and questions ------------------------------------------------

if [ -t 1 ]; then B=$'\e[1m' G=$'\e[32m' Y=$'\e[33m' R=$'\e[31m' N=$'\e[0m'; else B="" G="" Y="" R="" N=""; fi
STEP=0
step() { STEP=$((STEP + 1)); printf '\n%s[%d] %s%s\n' "$B" "$STEP" "$*" "$N"; }
ok() { printf '    %s✓%s %s\n' "$G" "$N" "$*"; }
note() { printf '    %s\n' "$*"; }
warn() { printf '    %s!%s %s\n' "$Y" "$N" "$*"; }
die() { printf '\n%sSetup stopped:%s %s\n' "$R" "$N" "$*" >&2; exit 1; }

# The questions come from the terminal even when this script arrives on a pipe
# (curl ... | sudo bash); with no terminal, or --yes, the defaults are taken.
TTY=""
if [ "$YES" = 0 ] && { exec 3</dev/tty; } 2>/dev/null; then TTY=3; fi
ask() {   # ask VAR "question" default
	local answer=""
	if [ -n "$TTY" ]; then
		printf '    %s [%s]: ' "$2" "$3" >/dev/tty
		IFS= read -r answer <&3 || true
	fi
	printf -v "$1" '%s' "${answer:-$3}"
}
yesno() {   # yesno "question" Y|N -> status
	local a
	ask a "$1 (y/n)" "$2"
	case "$a" in [Yy]*) return 0 ;; *) return 1 ;; esac
}

# ---- the machine ---------------------------------------------------------

[ "$(id -u)" = 0 ] || {
	if [ -f "$0" ] && command -v sudo >/dev/null; then exec sudo bash "$0" "$@"; fi
	die "run it as root: put sudo in front (curl ... | sudo bash)"
}
WSL=0
grep -qi microsoft /proc/version 2>/dev/null && WSL=1

if [ "$UNINSTALL" = 1 ]; then
	step "Removing the server"
	systemctl disable --now gevr-server 2>/dev/null || true
	rm -f "$UNIT" "$OVERRIDE"
	rmdir "$(dirname "$OVERRIDE")" 2>/dev/null || true
	systemctl daemon-reload 2>/dev/null || true
	rm -rf "$APP"
	ok "stopped and removed; your ROM, settings and key are still in $DATA"
	exit 0
fi

printf '%sGoldenEye VR dedicated server setup%s\n' "$B" "$N"
note "This installs the server, asks a few questions and starts it."
note "Run it again any time to update the server or change its settings."

step "Checking this machine"
case "$(uname -m)" in
x86_64|amd64) ARCH=x86_64 ;;
aarch64|arm64) ARCH=aarch64 ;;
*) die "the server runs on 64-bit Intel/AMD or ARM; this machine is $(uname -m)" ;;
esac
ok "Linux on $ARCH$([ "$WSL" = 1 ] && echo ' (WSL)')"

GLIBC=$(getconf GNU_LIBC_VERSION 2>/dev/null | awk '{print $2}')
[ -n "$GLIBC" ] || GLIBC=0
if [ "$(printf '%s\n2.39\n' "$GLIBC" | sort -V | head -1)" != 2.39 ]; then
	die "the server needs a newer Linux (glibc 2.39, this one has $GLIBC): Ubuntu 24.04 or Debian 13, or newer"
fi
ok "glibc $GLIBC"

[ -d /run/systemd/system ] || {
	[ "$WSL" = 1 ] && die "systemd is off in this WSL distribution: add [boot] systemd=true to /etc/wsl.conf, run 'wsl --shutdown' in Windows, and run this again (gevr-server-setup.ps1 does this for you)"
	die "this machine has no systemd, which runs the server as a service"
}

need=""
command -v python3 >/dev/null || need="$need python3"
command -v curl >/dev/null || need="$need curl"
[ -e /etc/ssl/certs/ca-certificates.crt ] || need="$need ca-certificates"
if [ -n "$need" ]; then
	if command -v apt-get >/dev/null; then
		note "installing:$need"
		DEBIAN_FRONTEND=noninteractive apt-get update -qq >/dev/null
		# shellcheck disable=SC2086
		DEBIAN_FRONTEND=noninteractive apt-get install -y -qq $need >/dev/null
	else
		die "please install:$need"
	fi
fi
ok "python3 and curl"

# ---- the server ----------------------------------------------------------

step "Getting the server"
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT
SELF=""
[ -f "$0" ] && SELF=$(readlink -f "$0")
if [ -z "$FROM" ] && [ -n "$SELF" ]; then
	# an unpacked server download carries the server itself: use that
	HERE=$(dirname "$SELF")
	if [ -n "$HERE" ] && [ -x "$HERE/gevr-server" ] && [ -f "$HERE/gevr_lobby_bridge.py" ] && [ -f "$HERE/VERSION" ]; then
		LOCAL=$HERE
	fi
fi
if [ -n "${LOCAL:-}" ]; then
	SRC=$LOCAL
	ok "using the server beside this script ($(cat "$SRC/VERSION"))"
else
	if [ -z "$FROM" ]; then
		note "looking for the newest server release on GitHub"
		RELEASES=$WORK/releases.json
		curl -fsSL "https://api.github.com/repos/$REPO/releases?per_page=40" -o "$RELEASES" ||
			die "could not reach GitHub to download the server"
		# the newest release (pre-releases included) with a server for this
		# machine, and its SHA-256 from the release notes where they list it
		read -r TAG URL FILE SUM < <(python3 - "$RELEASES" "$ARCH" "$VERSION" <<'PY'
import json, re, sys
releases, arch, want = json.load(open(sys.argv[1])), sys.argv[2], sys.argv[3]
for r in releases:
    if want and r["tag_name"] != want:
        continue
    for a in r.get("assets", []):
        if re.fullmatch(r"gevr-server-.+-linux-%s\.tar\.gz" % arch, a["name"]):
            body = r.get("body") or ""
            m = re.search(re.escape(a["name"]) + r"[^\n]*?\b([0-9a-f]{64})\b", body)
            print(r["tag_name"], a["browser_download_url"], a["name"], m.group(1) if m else "-")
            sys.exit(0)
print("- - - -")
PY
		)
		[ "$TAG" != - ] || die "no server download for $ARCH${VERSION:+ in $VERSION} on GitHub"
		note "downloading $FILE ($TAG)"
		curl -fsSL "$URL" -o "$WORK/$FILE" || die "the download failed"
		FROM=$WORK/$FILE
		if [ "$SUM" != - ]; then
			[ "$(sha256sum "$FROM" | cut -d' ' -f1)" = "$SUM" ] || die "the download's SHA-256 is not the one in the release notes"
			ok "downloaded and checked (SHA-256 matches the release notes)"
		else
			warn "downloaded; the release notes list no SHA-256 to check it against"
		fi
	fi
	[ -f "$FROM" ] || die "no such file: $FROM"
	tar -xzf "$FROM" -C "$WORK"
	SRC=$(find "$WORK" -mindepth 2 -maxdepth 2 -name gevr-server -printf '%h\n' | head -1)
	[ -n "$SRC" ] || die "$FROM is not a server download"
fi

id gevr >/dev/null 2>&1 || useradd --system --home "$DATA" --shell /usr/sbin/nologin gevr
mkdir -p "$APP" "$DATA"
RUNNING=0
systemctl is-active --quiet gevr-server 2>/dev/null && RUNNING=1 && systemctl stop gevr-server
if [ "$SRC" != "$APP" ]; then
	install -m 755 "$SRC/gevr-server" "$SRC/gevr_lobby_bridge.py" "$APP/"
	for f in gevr-server.service README.md LICENSE VERSION; do
		if [ -f "$SRC/$f" ]; then install -m 644 "$SRC/$f" "$APP/"; fi
	done
fi
# this script beside the server, for updates and changes later
if [ -n "$SELF" ] && [ "$SELF" != "$APP/gevr-server-setup.sh" ]; then
	install -m 755 "$SELF" "$APP/gevr-server-setup.sh"
elif [ -f "$SRC/gevr-server-setup.sh" ] && [ "$SRC" != "$APP" ]; then
	install -m 755 "$SRC/gevr-server-setup.sh" "$APP/"
fi
ok "installed in $APP: $(cat "$APP/VERSION" 2>/dev/null || echo server)"
# libraries the server needs and this Linux lacks (the test.16 downloads
# need libopus0, which a clean Ubuntu doesn't have)
missing=$(ldd "$APP/gevr-server" 2>/dev/null | awk '/not found/ {print $1}')
if [ -n "$missing" ]; then
	pkgs=""
	for lib in $missing; do
		case "$lib" in
		libopus.so.*) pkgs="$pkgs libopus0" ;;
		libz.so.*) pkgs="$pkgs zlib1g" ;;
		libstdc++.so.*) pkgs="$pkgs libstdc++6" ;;
		libgcc_s.so.*) pkgs="$pkgs libgcc-s1" ;;
		*) die "the server needs $lib, which this Linux doesn't have" ;;
		esac
	done
	command -v apt-get >/dev/null || die "please install:$pkgs"
	note "installing:$pkgs"
	DEBIAN_FRONTEND=noninteractive apt-get update -qq >/dev/null
	# shellcheck disable=SC2086
	DEBIAN_FRONTEND=noninteractive apt-get install -y -qq $pkgs >/dev/null
	ok "installed the libraries it needs:$pkgs"
fi

# ---- the ROM -------------------------------------------------------------

step "Your GoldenEye ROM"
# The cartridge's USA release, 12 MiB, in .z64 byte order. A .v64 or .n64
# dump of the same cartridge is put in that order here.
rom_check() {   # rom_check FILE -> prints z64|v64|n64|size|region|bad
	python3 - "$1" <<'PY'
import sys
data = open(sys.argv[1], "rb").read()
if len(data) != 12 * 1024 * 1024:
    print("size"); sys.exit()
order = {b"\x80\x37\x12\x40": "z64", b"\x37\x80\x40\x12": "v64", b"\x40\x12\x37\x80": "n64"}.get(data[:4], "bad")
if order == "v64":
    data = bytes(b for i in range(0, len(data), 2) for b in (data[i + 1], data[i]))
elif order == "n64":
    data = b"".join(data[i:i + 4][::-1] for i in range(0, len(data), 4))
if order != "bad" and data[0x3B:0x3F] != b"NGEE":
    order = "region"
print(order)
PY
}
if [ -f "$DATA/ge.z64" ] && [ -z "$ROM" ]; then
	ok "already have $DATA/ge.z64"
else
	while :; do
		if [ -z "$ROM" ]; then
			hint=$(ls ./ge.z64 ./*.z64 2>/dev/null | head -1 || true)
			[ -n "$TTY" ] || die "give your ROM with --rom PATH (ge.z64, GoldenEye 007, USA)"
			note "The server needs your own GoldenEye 007 cartridge dump (USA, 12 MiB),"
			note "the same file as on your headset. Nothing from the game comes with it."
			[ "$WSL" = 1 ] && note "A Windows file is under /mnt/c/... (C:\\Users\\you\\Downloads\\ge.z64 is /mnt/c/Users/you/Downloads/ge.z64)."
			ask ROM "Path to the ROM" "${hint:-ge.z64}"
		fi
		ROM=${ROM/#\~/$HOME}
		if [ ! -f "$ROM" ]; then warn "no file at $ROM"; ROM=""; [ -n "$TTY" ] || exit 1; continue; fi
		case "$(rom_check "$ROM")" in
		z64) install -m 644 "$ROM" "$DATA/ge.z64" ;;
		v64|n64)
			python3 - "$ROM" "$DATA/ge.z64" <<'PY'
import sys
d = open(sys.argv[1], "rb").read()
if d[:4] == b"\x37\x80\x40\x12":
    d = bytes(b for i in range(0, len(d), 2) for b in (d[i + 1], d[i]))
else:
    d = b"".join(d[i:i + 4][::-1] for i in range(0, len(d), 4))
open(sys.argv[2], "wb").write(d)
PY
			note "put into .z64 byte order" ;;
		size) warn "that file is not 12 MiB: not a GoldenEye 007 cartridge dump"; ROM=""; [ -n "$TTY" ] || exit 1; continue ;;
		region) warn "that is not the USA release (NTSC-U), which the game needs"; ROM=""; [ -n "$TTY" ] || exit 1; continue ;;
		*) warn "that is not an N64 ROM"; ROM=""; [ -n "$TTY" ] || exit 1; continue ;;
		esac
		break
	done
	ok "ROM copied to $DATA/ge.z64"
fi
chown -R gevr:gevr "$DATA"

# ---- the match -----------------------------------------------------------

step "The match"
INI=$DATA/goldeneye-vr.ini
if [ ! -f "$INI" ]; then
	# the server writes a server's settings file the first time it runs
	(cd "$DATA" && timeout 20 runuser -u gevr -- "$APP/gevr-server" --seconds 3 >/dev/null 2>&1) || true
	[ -f "$INI" ] || die "the server did not start (run '$APP/gevr-server --seconds 5' in $DATA to see why)"
fi
ini_get() { sed -n "s/^$1=//p" "$INI" | head -1; }
ini_set() { if grep -q "^$1=" "$INI"; then sed -i "s/^$1=.*/$1=$2/" "$INI"; else echo "$1=$2" >>"$INI"; fi; }

OLDNAME="" OLDVIS=""
if [ -f "$OVERRIDE" ]; then
	OLDNAME=$(grep -oE -- '--name "[^"]*"' "$OVERRIDE" | head -1 | sed 's/--name "\(.*\)"/\1/' || true)
	grep -q -- ' --public ' "$OVERRIDE" && OLDVIS=public || OLDVIS=private
fi
if [ -z "$NAME" ] && [ -n "$TTY" ]; then
	note "The name players see in the server lists (15 characters at most)."
	ask NAME "Server name" "${OLDNAME:-$(hostname | cut -c1-15)}"
fi
[ -n "$NAME" ] || NAME=${OLDNAME:-$(hostname | cut -c1-15)}
NAME=$(printf '%s' "$NAME" | tr -cd 'A-Za-z0-9 ._-' | cut -c1-15)
[ -n "$NAME" ] || NAME="GEVR Server"
if [ -z "$VISIBILITY" ] && [ -z "$TTY" ]; then
	VISIBILITY=${OLDVIS:-public}
elif [ -z "$VISIBILITY" ]; then
	note "Public: anyone can find it in the game's Online list."
	note "Private: players join with a code (shown at the end)."
	if yesno "List it publicly?" "$([ "${OLDVIS:-public}" = public ] && echo y || echo n)"; then VISIBILITY=public; else VISIBILITY=private; fi
fi
ok "\"$NAME\", $VISIBILITY"

if [ -z "$MAP" ] && [ -n "$TTY" ]; then
	note "Maps:"
	"$APP/gevr-server" --list 2>/dev/null | sed -n '/^MpStage=/,/^$/p' | sed '1d;/^$/d' | paste - - - | sed 's/^/   /'
	ask MAP "Map number" "$(ini_get MpStage)"
fi
[ -n "$MAP" ] || MAP=$(ini_get MpStage)
case "$MAP" in ''|*[!0-9]*) die "the map is a number ('$APP/gevr-server --list' lists them)" ;; esac
"$APP/gevr-server" --list 2>/dev/null | sed -n '/^MpStage=/,/^$/p' | grep -qE "^ +$MAP +" || die "there is no map $MAP ('$APP/gevr-server --list' lists them)"
if [ -z "$PLAYERS" ] && [ -n "$TTY" ]; then
	note "Players at most, counting the server's own seat: 8 lets 7 people play."
	ask PLAYERS "Players" "$(ini_get MpMaxPlayers)"
fi
[ -n "$PLAYERS" ] || PLAYERS=$(ini_get MpMaxPlayers)
case "$PLAYERS" in [2-8]) ;; *) die "players must be 2 to 8" ;; esac
ini_set MpMode 0
ini_set MpStage "$MAP"
ini_set MpMaxPlayers "$PLAYERS"
ini_set MpBotMode 1
chown gevr:gevr "$INI"
MAPNAME=$("$APP/gevr-server" --list 2>/dev/null | sed -n '/^MpStage=/,/^$/p' | sed -nE "s/^ +$MAP +//p")
ok "$MAPNAME, $PLAYERS players, bots fill the empty places"
note "Every other setting (weapons, length, bots) is in $INI;"
note "'$APP/gevr-server --list' shows the values. Restart the server after changing it."

# ---- the key -------------------------------------------------------------

step "Server key (optional)"
KEY=$DATA/server.key
if [ -n "$KEYFILE" ]; then
	install -m 600 -o gevr -g gevr "$KEYFILE" "$KEY"
	ok "key installed"
elif [ -s "$KEY" ]; then
	ok "keeping your key"
else
	k=""
	if [ -n "$TTY" ]; then
		note "Without a key the server works, but its lobby is renewed every two"
		note "hours with a new code. A key from the GoldenEye VR project keeps one"
		note "code for good. Paste it here, or press Enter to skip."
		printf '    Key: ' >/dev/tty
		IFS= read -rs k <&3 || true
		echo >/dev/tty
	fi
	if [ -n "$k" ]; then
		(umask 077; printf '%s\n' "$k" >"$KEY")
		chown gevr:gevr "$KEY"
		ok "key saved in $KEY (only the server can read it)"
	else
		note "skipped"
	fi
fi

# ---- the network ---------------------------------------------------------

step "Network"
if [ "$WSL" = 1 ]; then
	ok "WSL: Windows' firewall is set up by gevr-server-setup.ps1"
else
	opened=0
	if command -v ufw >/dev/null && ufw status 2>/dev/null | grep -q '^Status: active'; then
		ufw allow $GAME_PORT/udp >/dev/null
		ufw allow ${ICE_PORTS/-/:}/udp >/dev/null
		ufw allow $LAN_PORT/udp >/dev/null
		opened=1
		ok "opened UDP $GAME_PORT, $ICE_PORTS and $LAN_PORT in ufw"
	fi
	if command -v iptables >/dev/null && iptables -S INPUT 2>/dev/null | grep -q -- '-j REJECT' &&
	   ! iptables -C INPUT -p udp --dport $GAME_PORT -j ACCEPT 2>/dev/null; then
		# Oracle's (and some other clouds') images reject everything not listed
		iptables -I INPUT -p udp --dport $GAME_PORT -j ACCEPT
		iptables -I INPUT -p udp --dport ${ICE_PORTS/-/:} -j ACCEPT
		iptables -I INPUT -p udp --dport $LAN_PORT -j ACCEPT
		command -v netfilter-persistent >/dev/null && netfilter-persistent save >/dev/null 2>&1 || true
		opened=1
		ok "opened UDP $GAME_PORT, $ICE_PORTS and $LAN_PORT in iptables"
	fi
	[ "$opened" = 1 ] || ok "no firewall on this machine needs opening"
	if [ "$(cat /sys/class/dmi/id/chassis_asset_tag 2>/dev/null)" = OracleCloud.com ]; then
		warn "Oracle Cloud also blocks these ports outside the machine. In the Oracle console:"
		note "  Networking > Virtual cloud networks > your network > Security lists >"
		note "  Default Security List > Add Ingress Rules, twice:"
		note "    Source CIDR 0.0.0.0/0, IP protocol UDP, destination port $GAME_PORT"
		note "    Source CIDR 0.0.0.0/0, IP protocol UDP, destination port range $ICE_PORTS"
		note "  Players joining by code work without it; it makes joins more reliable"
		note "  and lets players join by IP."
		if [ -n "$TTY" ]; then ask pause "Press Enter to go on" ""; fi
	fi
fi

# ---- the service ---------------------------------------------------------

step "Starting the server"
[ -f "$APP/gevr-server.service" ] || die "$APP/gevr-server.service is missing from the download"
install -m 644 "$APP/gevr-server.service" "$UNIT"
mkdir -p "$(dirname "$OVERRIDE")"
FLAGS="--restart"
[ "$VISIBILITY" = public ] && FLAGS="--public $FLAGS"
[ -s "$KEY" ] && FLAGS="$FLAGS --key-file $KEY"
cat >"$OVERRIDE" <<EOF
# Written by gevr-server-setup.sh; run it again to change these.
[Service]
ExecStart=
ExecStart=/usr/bin/python3 $APP/gevr_lobby_bridge.py $FLAGS -- $APP/gevr-server --name "$NAME" --port $GAME_PORT --ice-ports $ICE_PORTS
EOF
systemctl daemon-reload
SINCE=$(date '+%Y-%m-%d %H:%M:%S')
systemctl enable gevr-server >/dev/null 2>&1
systemctl restart gevr-server
note "waiting for it to come online..."
CODE=""
for _ in $(seq 60); do
	sleep 1
	systemctl is-active --quiet gevr-server || break
	CODE=$(journalctl -u gevr-server --since "$SINCE" -o cat 2>/dev/null | grep -oE 'lobby: online as [A-Z0-9]+' | tail -1 | awk '{print $4}' || true)
	[ -n "$CODE" ] && break
done
if ! systemctl is-active --quiet gevr-server; then
	journalctl -u gevr-server --since "$SINCE" -o cat --no-pager | tail -15 | sed 's/^/    /'
	die "the server stopped; its log is above ('journalctl -u gevr-server' shows all of it)"
fi
if [ -n "$CODE" ]; then ok "online"; else warn "running, but not listed online yet (check 'journalctl -u gevr-server -f')"; fi

# ---- done ----------------------------------------------------------------

IP=$(hostname -I 2>/dev/null | awk '{print $1}')
printf '\n%sYour server is running.%s\n\n' "$B$G" "$N"
if [ "$VISIBILITY" = public ]; then
	note "Players find \"$NAME\" in the game's Online list."
fi
[ -n "$CODE" ] && note "Join code: ${B}$CODE${N}   (Online > Join by code)"
if [ "$WSL" = 0 ] && [ -n "$IP" ]; then
	note "On the same network: the LAN list, or Join by IP $IP"
fi
note "It starts again by itself when this machine restarts."
echo
note "Useful commands:"
note "  journalctl -u gevr-server -f            watch its log"
note "  sudo systemctl restart gevr-server      restart it (after editing $INI)"
note "  sudo systemctl stop gevr-server         stop it"
note "  sudo bash $APP/gevr-server-setup.sh     update it or change these settings"
note "  sudo bash $APP/gevr-server-setup.sh --uninstall"
[ "$RUNNING" = 1 ] && note "(It was running before; it has been restarted with the new settings.)"
exit 0
