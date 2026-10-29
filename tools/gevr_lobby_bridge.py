#!/usr/bin/env python3
"""Put a dedicated server in the internet lobby (docs/dedicated-server.md, P2).

    tools/gevr_lobby_bridge.py [--public] [--restart] -- SERVER [SERVER ARGS...]

Starts the server with --lobby-fd and does for it what the headset's
LobbyClient.java does for the launcher: registers the lobby at
lobbies.goldeneyevr.com, heartbeats it, hands the server each joiner's ICE
offer (HOST_PEER) and posts the server's answers back. The server and this
bridge trade the same one-line messages the launcher and LobbyClient do
(port/server/server_lobby.c).

The lobby is private (join by code) unless --public: the code is in the log.
--restart starts the server again whenever it ends (a match over, a crash),
as a fresh lobby. SIGTERM or Ctrl+C stops the server and removes the lobby.

Python 3.8+, standard library only.
"""

import base64
import json
import os
import queue
import signal
import subprocess
import sys
import threading
import time
import urllib.error
import urllib.request

BASE = os.environ.get("GEVR_LOBBY_URL", "https://lobbies.goldeneyevr.com/v1/lobbies")
AGENT = "GEVR-Server/1"


def log(message):
    print("lobby-bridge: " + message, flush=True)


def clean(value):
    return str(value).replace("|", " ").replace("\n", " ").replace("\r", " ").replace("\t", " ")


def encode(text):
    return base64.urlsafe_b64encode(text.encode("utf-8")).decode("ascii")


def decode(text):
    return base64.urlsafe_b64decode(text + "=" * (-len(text) % 4)).decode("utf-8")


class HttpError(Exception):
    def __init__(self, status, message):
        super().__init__(message)
        self.status = status


def http(method, url, data=None, token=""):
    body = None if data is None else json.dumps(data).encode("utf-8")
    request = urllib.request.Request(url, data=body, method=method)
    request.add_header("Accept", "application/json")
    request.add_header("User-Agent", AGENT)
    if body is not None:
        request.add_header("Content-Type", "application/json")
    if token:
        request.add_header("Authorization", "Bearer " + token)
    try:
        with urllib.request.urlopen(request, timeout=5) as response:
            raw = response.read(65536)
    except urllib.error.HTTPError as e:
        raw = e.read(65536)
        try:
            message = json.loads(raw or b"{}").get("error", "HTTP %d" % e.code)
        except ValueError:
            message = "HTTP %d" % e.code
        raise HttpError(e.code, message)
    return json.loads(raw) if raw else {}


class Lobby:
    """LobbyClient.java's host side, driven by the server's commands."""

    def __init__(self, send):
        self.send = send   # an event line to the server
        self.forget()

    def forget(self):
        self.code = ""
        self.token = ""
        self.name = ""
        self.pending = None
        self.players = 1
        self.max_players = 4
        self.open = True
        self.phase = "waiting"
        self.next_heartbeat = 0.0
        self.next_requests = 0.0
        self.seen = set()
        self.last_keepalive = 0.0
        self.last_error = ""

    def command(self, line):
        f = line.split("|")
        now = time.monotonic()
        if f[0] == "create" and len(f) == 7:
            self.stop()
            self.name = f[2]
            self.last_keepalive = now
            self.players = 1
            self.max_players = int(f[6])
            self.open = True
            self.phase = "waiting"
            self.pending = {"visibility": f[1], "name": f[2], "version": int(f[3]), "stage": int(f[4]),
                            "weapons": int(f[5]), "maxPlayers": int(f[6])}
            self.register()
        elif f[0] == "stop":
            self.stop()
        elif f[0] == "refresh" and len(f) in (3, 4):
            self.last_keepalive = now
            players, open_, max_players = int(f[1]), f[2] == "1", int(f[3]) if len(f) == 4 else self.max_players
            if (players, open_, max_players) != (self.players, self.open, self.max_players):
                self.players, self.open, self.max_players = players, open_, max_players
                self.next_heartbeat = 0
        elif f[0] == "phase" and len(f) == 3 and f[1] in ("warmup", "in_progress"):
            self.last_keepalive = now
            self.phase = f[1]
            self.players = int(f[2])
            self.open = self.players < self.max_players
            self.next_heartbeat = 0
            self.next_requests = 0
        elif f[0] == "answer" and len(f) == 3 and self.code:
            http("PUT", "%s/%s/joins/%s/answer" % (BASE, self.code, f[1]), {"sdp": decode(f[2])}, self.token)

    def register(self):
        if self.pending is None:
            return
        result = http("POST", BASE, self.pending)
        self.code, self.token = result["code"], result["ownerToken"]
        visibility = self.pending["visibility"]
        self.pending = None
        self.next_heartbeat = self.next_requests = 0
        self.seen.clear()
        log("lobby %s registered (%s)" % (self.code, visibility))
        self.send("CREATED|%s|%s" % (self.code, self.token))

    def lost(self, e):
        if not self.code or e.status not in (404, 410):
            return False
        code = self.code
        log("lobby %s is gone (HTTP %d: %s)" % (code, e.status, e))
        self.forget()
        self.send("LOBBY_LOST|%s|%s" % (code, clean(e)))
        return True

    def relay(self):
        """TURN credentials for a joiner; empty (direct only) if refused."""
        try:
            turn = http("POST", "%s/%s/turn" % (BASE, self.code), {}, self.token)
            return "%s|%s" % (clean(turn["username"]), clean(turn["credential"]))
        except Exception as e:
            log("relay credentials unavailable, direct connection only: %s" % e)
            return "|"

    def poll(self):
        """Every 2 s, as LobbyClient's worker."""
        now = time.monotonic()
        try:
            if now - self.last_keepalive > 60:
                return   # the server stopped talking; it is restarting or gone
            if self.pending is not None:
                self.register()
            if not self.code:
                return
            if now >= self.next_heartbeat:
                state = {"players": self.players, "open": self.open, "phase": self.phase,
                         "maxPlayers": self.max_players}
                if self.name:
                    state["name"] = self.name
                try:
                    http("PUT", "%s/%s" % (BASE, self.code), state, self.token)
                except HttpError as e:
                    if self.lost(e):
                        return
                    raise
                self.next_heartbeat = time.monotonic() + 15
            if self.open and time.monotonic() >= self.next_requests:
                try:
                    requests = http("GET", "%s/%s/joins" % (BASE, self.code), None, self.token)["requests"]
                except HttpError as e:
                    if self.lost(e):
                        return
                    raise
                self.next_requests = time.monotonic() + (4 if self.phase == "waiting" else 10)
                for request in requests:
                    if request["id"] in self.seen:
                        continue
                    self.seen.add(request["id"])
                    log("join request %s" % request["id"])
                    self.send("HOST_PEER|%s|%s|%s" % (request["id"], encode(request["offer"]), self.relay()))
            self.last_error = ""
        except Exception as e:
            message = "Lobby service: %s" % e
            if message != self.last_error:
                log(message)
                self.send("ERROR|" + clean(message))
            self.last_error = message

    def stop(self):
        self.pending = None
        if self.code:
            try:
                http("DELETE", "%s/%s" % (BASE, self.code), None, self.token)
                log("lobby %s removed" % self.code)
            except HttpError as e:
                if e.status in (404, 410):
                    log("lobby %s already gone" % self.code)
                else:
                    log("could not remove lobby %s: %s" % (self.code, e))
            except Exception as e:
                log("could not remove lobby %s: %s" % (self.code, e))
        self.forget()


def run_server(argv, public, stopping):
    """One server process in one lobby, until it ends or we are told to stop."""
    to_server_r, to_server_w = os.pipe()
    from_server_r, from_server_w = os.pipe()
    args = argv + ["--lobby-fd", "%d,%d" % (to_server_r, from_server_w)] + (["--lobby-public"] if public else [])
    server = subprocess.Popen(args, pass_fds=(to_server_r, from_server_w))
    os.close(to_server_r)
    os.close(from_server_w)
    out = os.fdopen(to_server_w, "w", buffering=1)
    lines = queue.Queue()

    def send(event):
        try:
            out.write(event + "\n")
        except (BrokenPipeError, ValueError):
            pass

    def reader():
        with os.fdopen(from_server_r, "r") as source:
            for line in source:
                lines.put(line.rstrip("\n"))
        lines.put(None)

    threading.Thread(target=reader, daemon=True).start()
    lobby = Lobby(send)
    next_poll = 0.0
    ended = False
    while not ended:
        try:
            line = lines.get(timeout=0.25)
            if line is None:
                ended = True
            else:
                try:
                    lobby.command(line)
                except Exception as e:
                    log("command %s failed: %s" % (line.split("|")[0], e))
                    send("ERROR|" + clean("Lobby service: %s" % e))
        except queue.Empty:
            pass
        if stopping.is_set() and server.poll() is None:
            server.send_signal(signal.SIGTERM)
        if time.monotonic() >= next_poll:
            lobby.poll()
            next_poll = time.monotonic() + 2
    lobby.stop()   # the server's own "stop" got there first, unless it crashed
    code = server.wait()
    out.close()
    return code


def main():
    argv = sys.argv[1:]
    if "--" not in argv:
        sys.exit(__doc__)
    split = argv.index("--")
    options, server_argv = argv[:split], argv[split + 1:]
    if not server_argv:
        sys.exit(__doc__)
    public = "--public" in options
    restart = "--restart" in options

    stopping = threading.Event()
    for sig in (signal.SIGTERM, signal.SIGINT):
        signal.signal(sig, lambda *_: stopping.set())

    while True:
        started = time.monotonic()
        code = run_server(server_argv, public, stopping)
        log("server exited (%d)" % code)
        if stopping.is_set() or not restart:
            return code
        # a server that dies at once is not restarted in a tight loop
        time.sleep(max(0.0, 10 - (time.monotonic() - started)))


if __name__ == "__main__":
    sys.exit(main())
