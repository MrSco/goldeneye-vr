#!/bin/bash
# The dedicated server's regression run (docs/dedicated-server.md, H6): a
# server and two headless clients play a bot match on this machine, and the
# logs must show the round going as it does with headsets.
#
#   tools/gevr_server_test.sh ROM [SERVER_BINARY] [STAGE]
#
# Linux or WSL; build the server first (tools/gevr_build_server.sh). Takes
# about three minutes. Exits non-zero, listing what failed, if anything did.
set -u

ROM=${1:?usage: gevr_server_test.sh ROM [SERVER_BINARY] [STAGE]}
BIN=${2:-$HOME/gevr-server-build/gevr-server}
STAGE=${3:-34}
PORT=$((27200 + RANDOM % 500))
WORK=$(mktemp -d /tmp/gevr-server-test.XXXXXX)
fails=()

for d in server a b; do
	mkdir -p "$WORK/$d"
	ln -s "$(realpath "$ROM")" "$WORK/$d/ge.z64"
done
printf 'MpStage=%s\nMpMaxPlayers=8\nMpBotMode=1\nMpBotCount=6\nMpBotDifficulty=2\nMpLength=2\n' "$STAGE" > "$WORK/server/goldeneye-vr.ini"
printf 'PlayerName=TestA\nMpChr=2\n' > "$WORK/a/goldeneye-vr.ini"
printf 'PlayerName=TestB\nMpChr=3\n' > "$WORK/b/goldeneye-vr.ini"

echo "gevr_server_test: stage $STAGE on port $PORT in $WORK"
(cd "$WORK/server" && exec "$BIN" --port "$PORT" --seconds 170 > server.log 2>&1) &
server=$!
sleep 12
# A joins an empty server (warmup, the round starts by itself); B joins the
# round already under way, in a bot's place, and must play, not spectate.
(cd "$WORK/a" && exec "$BIN" --connect "127.0.0.1:$PORT" --seconds 100 > client.log 2>&1) &
a=$!
sleep 50
(cd "$WORK/b" && exec "$BIN" --connect "127.0.0.1:$PORT" --seconds 50 > client.log 2>&1) &
b=$!
wait "$a"; ca=$?
wait "$b"; cb=$?
wait "$server"; cs=$?

S=$WORK/server/server.log
check() {   # description, then a command that succeeds when it holds
	local what=$1; shift
	if "$@"; then echo "  ok   $what"; else echo "  FAIL $what"; fails+=("$what"); fi
}
count() { grep -ac "$1" "$2"; }

check "server exited cleanly ($cs)" test "$cs" -eq 0
check "client A exited cleanly ($ca)" test "$ca" -eq 0
check "client B exited cleanly ($cb)" test "$cb" -eq 0
check "no fatal error or crash" bash -c "! grep -aq 'FATAL\|server: signal' '$S' '$WORK/a/client.log' '$WORK/b/client.log'"
check "both clients took a bot's place" test "$(count 'takes the place of' "$S")" -ge 2
check "both clients reached the match" test "$(cat "$WORK/a/client.log" "$WORK/b/client.log" | grep -ac 'client: in the match')" -eq 2
check "the round started (countdown)" test "$(count 'Next round in' "$S")" -ge 1
check "the round ran (phase 2)" test "$(count 'phase 2' "$S")" -ge 1
check "the late joiner played, did not spectate" bash -c "! grep -aq 'spectator: following' '$WORK/b/client.log'"
check "kills were scored" test "$(count 'net: kill:' "$S")" -ge 3
check "back to warmup once the players left" test "$(count 'nobody left in the round' "$S")" -ge 1
frames=$(grep -a '^server: [0-9]' "$S" | sed 's/^server: \([0-9.]*\) game frames.*/\1/')
check "60 game frames a second throughout" bash -c "echo '$frames' | awk 'NF && (\$1 < 58 || \$1 > 62) { bad = 1 } END { exit bad }'"
cpu=$(grep -a '^server: [0-9]' "$S" | sed 's/.*CPU \([0-9.]*\)%.*/\1/' | sort -n | tail -1)
echo "  info highest CPU ${cpu}% of a core, $(count 'net: kill:' "$S") kills, logs in $WORK"

if [ ${#fails[@]} -gt 0 ]; then
	echo "gevr_server_test: ${#fails[@]} failed"
	exit 1
fi
echo "gevr_server_test: passed"
rm -rf "$WORK"
