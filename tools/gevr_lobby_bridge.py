#!/usr/bin/env python3
"""Put a dedicated server in the internet lobby (docs/dedicated-server.md, P2).

    tools/gevr_lobby_bridge.py [--public] [--restart] [--key-file FILE] -- SERVER [SERVER ARGS...]
    tools/gevr_lobby_bridge.py --key-hash FILE

Starts the server with --lobby-fd and does for it what the headset's
LobbyClient.java does for the launcher: registers the lobby at
lobbies.goldeneyevr.com, heartbeats it, hands the server each joiner's ICE
offer (HOST_PEER) and posts the server's answers back. The server and this
bridge trade the same one-line messages the launcher and LobbyClient do
(port/server/server_lobby.c).

The lobby is private (join by code) unless --public: the code is in the log.
--restart starts the server again whenever it ends (a match over, a crash),
as a fresh lobby. SIGTERM or Ctrl+C stops the server and removes the lobby.

--key-file FILE (or GEVR_SERVER_KEY) holds a server key from the lobby
service: the lobby is then listed as a dedicated server, never times out
while the server runs, and keeps the same code each time it registers.
Without one it is an ordinary lobby, ended after two hours (and registered
again, under a new code). An operator makes their own key and asks for it to
be listed with its SHA-256 (--key-hash FILE prints it; the key never leaves
the server): docs/dedicated-server-operators.md, "Server keys". Until it is
listed the lobby is an ordinary one, and each registration tries the key again.

Python 3.8+, standard library only.
"""

import base64
import hashlib
import json
import os
import queue
import re
import signal
import socket
import ssl
import subprocess
import sys
import threading
import time
import urllib.error
import urllib.parse
import urllib.request

BASE = os.environ.get("GEVR_LOBBY_URL", "https://lobbies.goldeneyevr.com/v1/lobbies")
AGENT = "GEVR-Server/1"
# Push signaling (the Worker's /socket): join requests arrive at once and the heartbeat
# rides the socket, so a server waiting for players makes no HTTP requests. Polling is
# the fallback, as in LobbyClient.java.
SOCKET_BASE = BASE.replace("https://", "wss://", 1).replace("http://", "ws://", 1)
SOCKET_RETRY = 60     # s after a failed or dropped socket
SOCKET_PING = 30      # s; the Worker answers "ping" without waking
SOCKET_SILENCE = 75   # s heard nothing: the socket is dead
STATE_REPLY = 10      # s a heartbeat may go unanswered before HTTP takes over


def key_hash(key):
    """The SHA-256 the lobby service lists an approved server key under (the key itself stays secret)."""
    return hashlib.sha256(key.strip().encode("utf-8")).hexdigest()


def log(message):
    print("lobby-bridge: " + message, flush=True)


def clean(value):
    return str(value).replace("|", " ").replace("\n", " ").replace("\r", " ").replace("\t", " ")


def encode(text):
    return base64.urlsafe_b64encode(text.encode("utf-8")).decode("ascii")


def decode(text):
    return base64.urlsafe_b64decode(text + "=" * (-len(text) % 4)).decode("utf-8")



def devices_json(devices):
    """devices|key@build,... as the Worker takes it: [{device, build}], at most eight,
    odd entries left out (the game's LobbyClient.java devicesJson)."""
    out = []
    for entry in devices.split(",") if devices else []:
        key, _, build = entry.partition("@")
        if re.fullmatch(r"[a-z-]{1,16}", key) and re.fullmatch(r"[0-9a-z.-]{0,23}", build) and len(out) < 8:
            out.append({"device": key, "build": build})
    return out

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


class PushSocket:
    """The lobby Worker's push signaling socket (LobbySocket.java): an RFC 6455 client
    for text frames. Its reader thread puts ("message", self, text) and, once,
    ("closed", self, why) on the inbox; the bridge's loop handles them."""

    GUID = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"
    MAX_MESSAGE = 65536

    def __init__(self, url, token, inbox, timeout=5):
        parts = urllib.parse.urlsplit(url)
        tls = parts.scheme == "wss"
        conn = socket.create_connection((parts.hostname, parts.port or (443 if tls else 80)), timeout=timeout)
        try:
            if tls:
                conn = ssl.create_default_context().wrap_socket(conn, server_hostname=parts.hostname)
            key = base64.b64encode(os.urandom(16)).decode("ascii")
            path = parts.path + ("?" + parts.query if parts.query else "")
            host = parts.hostname + (":%d" % parts.port if parts.port else "")
            conn.sendall(("GET %s HTTP/1.1\r\nHost: %s\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
                          "Sec-WebSocket-Key: %s\r\nSec-WebSocket-Version: 13\r\nAuthorization: Bearer %s\r\n"
                          "User-Agent: %s\r\n\r\n" % (path, host, key, token, AGENT)).encode("utf-8"))
            self.reader = conn.makefile("rb")
            status = self.reader.readline(8192)
            accept = None
            while True:
                line = self.reader.readline(8192)
                if line in (b"\r\n", b"\n", b""):
                    break
                name, _, value = line.decode("latin-1").partition(":")
                if name.strip().lower() == "sec-websocket-accept":
                    accept = value.strip()
            if not status.startswith(b"HTTP/1.1 101"):
                raise OSError("upgrade refused: %s" % status.decode("latin-1").strip())
            if accept != base64.b64encode(hashlib.sha1((key + self.GUID).encode("ascii")).digest()).decode("ascii"):
                raise OSError("bad upgrade reply")
            conn.settimeout(SOCKET_SILENCE)
        except BaseException:
            conn.close()
            raise
        self.conn = conn
        self.inbox = inbox
        self.lock = threading.Lock()         # one frame written at a time
        self.state_lock = threading.Lock()   # closed() goes on the inbox once
        self.done = False
        threading.Thread(target=self.read, daemon=True).start()

    def send(self, text):
        self.write(0x1, text.encode("utf-8"))

    def close(self):
        if self.done:
            return
        try:
            self.write(0x8, b"\x03\xe8")   # 1000, normal
        except OSError:
            pass
        self.finish("closed here")

    def write(self, opcode, payload):
        if self.done:
            raise OSError("socket closed")
        n = len(payload)
        head = bytes([0x80 | opcode]) + (bytes([0x80 | n]) if n < 126 else bytes([0x80 | 126, n >> 8, n & 0xFF]))
        mask = os.urandom(4)
        frame = head + mask + bytes(b ^ mask[i & 3] for i, b in enumerate(payload))
        try:
            with self.lock:
                self.conn.sendall(frame)
        except OSError as e:
            self.finish("send failed: %s" % e)
            raise

    def exactly(self, n):
        data = self.reader.read(n)
        if data is None or len(data) < n:
            raise EOFError("connection ended")
        return data

    def read(self):
        text = b""
        try:
            while not self.done:
                b0, b1 = self.exactly(2)
                opcode, length = b0 & 0x0F, b1 & 0x7F
                if length == 126:
                    length = int.from_bytes(self.exactly(2), "big")
                elif length == 127:
                    length = int.from_bytes(self.exactly(8), "big")
                if length > self.MAX_MESSAGE:
                    raise OSError("frame too large")
                mask = self.exactly(4) if b1 & 0x80 else None
                payload = self.exactly(length) if length else b""
                if mask:
                    payload = bytes(b ^ mask[i & 3] for i, b in enumerate(payload))
                if opcode in (0x0, 0x1):
                    text += payload
                    if len(text) > self.MAX_MESSAGE:
                        raise OSError("message too large")
                    if b0 & 0x80:
                        self.inbox.put(("message", self, text.decode("utf-8")))
                        text = b""
                elif opcode == 0x8:
                    code = int.from_bytes(payload[:2], "big") if len(payload) >= 2 else 1005
                    try:
                        self.write(0x8, payload[:2])
                    except OSError:
                        pass
                    self.finish("closed by the service: %d %s" % (code, payload[2:].decode("utf-8", "replace")))
                    return
                elif opcode == 0x9:
                    self.write(0xA, payload)
            self.finish("closed")
        except EOFError:
            self.finish("connection ended")
        except Exception as e:
            self.finish(str(e) or type(e).__name__)

    def finish(self, why):
        with self.state_lock:
            if self.done:
                return
            self.done = True
        try:
            self.conn.close()
        except OSError:
            pass
        self.inbox.put(("closed", self, why))


class Lobby:
    """LobbyClient.java's host side, driven by the server's commands."""

    def __init__(self, send, key=""):
        self.send = send   # an event line to the server
        self.key = key     # the server key ("": an ordinary lobby)
        self.push = None   # the push socket, None while polling
        self.inbox = queue.Queue()
        self.forget()

    def forget(self):
        self.close_socket()
        self.next_socket_try = 0.0
        self.code = ""
        self.token = ""
        self.name = ""
        self.pending = None
        self.players = 1
        self.max_players = 4
        self.vr_only = None        # "VR players only" as the server last said; None: not said
        self.flat_players = None   # how many play flat, as the server last counted; None: not said
        self.devices = None        # devices|key@build,... as the server last said; None: not said
        self.hand_only = None      # "Hand reload only" as the server last said; None: not said
        self.button_players = None # how many reload with the button; None: not said
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
        if f[0] == "create" and len(f) in (7, 8):   # create|...|maxPlayers[|vrOnly]
            self.stop()
            self.name = f[2]
            self.last_keepalive = now
            self.players = 1
            self.max_players = int(f[6])
            self.vr_only = f[7] == "1" if len(f) == 8 else None
            self.flat_players = None
            self.devices = None
            self.hand_only = None
            self.button_players = None
            self.open = True
            self.phase = "waiting"
            self.pending = {"visibility": f[1], "name": f[2], "version": int(f[3]), "stage": int(f[4]),
                            "weapons": int(f[5]), "maxPlayers": int(f[6])}
            if self.vr_only is not None:
                self.pending["vrOnly"] = self.vr_only
            self.register()
        elif f[0] == "stop":
            self.stop()
        elif f[0] == "refresh" and 3 <= len(f) <= 6:   # refresh|players|open[|max[|vrOnly[|flatPlayers]]]
            self.last_keepalive = now
            players, open_ = int(f[1]), f[2] == "1"
            max_players = int(f[3]) if len(f) >= 4 else self.max_players
            vr_only = f[4] == "1" if len(f) >= 5 else self.vr_only
            flat = max(0, min(int(f[5]), players)) if len(f) == 6 else self.flat_players
            state = (players, open_, max_players, vr_only, flat)
            if state != (self.players, self.open, self.max_players, self.vr_only, self.flat_players):
                self.players, self.open, self.max_players, self.vr_only, self.flat_players = state
                self.next_heartbeat = 0
        elif f[0] == "reload" and len(f) == 3:   # reload|handOnly|buttonPlayers: the Join list's filter
            hand_only, button = f[1] == "1", max(0, int(f[2]))
            if (hand_only, button) != (self.hand_only, self.button_players):
                self.hand_only, self.button_players = hand_only, button
                self.next_heartbeat = 0
        elif f[0] == "devices" and len(f) == 2:   # devices|key@build,...: what each person runs
            devices = devices_json(f[1])
            if devices != self.devices:
                self.devices = devices
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
        dedicated = bool(self.key)
        try:
            result = http("POST", BASE, self.pending, self.key)
        except HttpError as e:
            if e.status != 403 or not self.key:
                raise
            # A key the project has not approved yet (or has withdrawn): listed as an
            # ordinary lobby meanwhile. Each registration tries the key again, so the
            # server becomes a dedicated one at its next lobby once the hash is listed.
            log("the lobby service does not know the server key yet (%s); registering as an ordinary lobby."
                " Its SHA-256 for the request: %s" % (e, key_hash(self.key)))
            result = http("POST", BASE, self.pending, "")
            dedicated = False
        self.code, self.token = result["code"], result["ownerToken"]
        visibility = self.pending["visibility"]
        self.pending = None
        self.next_heartbeat = self.next_requests = 0
        self.seen.clear()
        log("lobby %s registered (%s%s)" % (self.code, visibility, ", dedicated" if dedicated else ""))
        self.send("CREATED|%s|%s" % (self.code, self.token))

    def lost(self, e):
        if not self.code or e.status not in (404, 410):
            return False
        code = self.code
        log("lobby %s is gone (HTTP %d: %s)" % (code, e.status, e))
        self.forget()
        self.send("LOBBY_LOST|%s|%s" % (code, clean(e)))
        return True

    def open_socket(self):
        try:
            self.push = PushSocket("%s/%s/socket" % (SOCKET_BASE, self.code), self.token, self.inbox)
            self.last_ping = time.monotonic()
            log("lobby socket open")
        except Exception as e:
            log("lobby socket unavailable, polling instead: %s" % e)
            self.push = None
            self.next_socket_try = time.monotonic() + SOCKET_RETRY

    def close_socket(self):
        push, self.push = self.push, None
        self.state_sent_at = 0.0
        if push is not None:
            push.close()

    def drop_socket(self, why):
        log("lobby socket closed (%s); polling, socket again in %d s" % (why, SOCKET_RETRY))
        self.close_socket()
        self.next_socket_try = time.monotonic() + SOCKET_RETRY
        self.next_heartbeat = self.next_requests = 0   # the last heartbeat may not have landed

    def request(self, id_, offer):
        if id_ in self.seen:
            return
        self.seen.add(id_)
        log("join request %s" % id_)
        self.send("HOST_PEER|%s|%s|%s" % (id_, encode(offer), self.relay()))

    def pump(self):
        """The push socket's messages, on the bridge's loop (as LobbyClient's worker thread)."""
        while True:
            try:
                kind, push, payload = self.inbox.get_nowait()
            except queue.Empty:
                return
            if push is not self.push:
                continue   # an old socket's
            if kind == "closed":
                self.drop_socket(payload)
                continue
            if payload == "pong":
                continue
            try:
                message = json.loads(payload)
                if message.get("type") == "request":
                    self.request(message["id"], message["offer"])
                elif message.get("type") == "state":
                    self.state_sent_at = 0.0
                    status = int(message.get("status", 200))
                    if status >= 400:
                        e = HttpError(status, message.get("error", "HTTP %d" % status))
                        if not self.lost(e):
                            log("Lobby service: %s" % e)
                            self.send("ERROR|" + clean("Lobby service: %s" % e))
            except Exception as e:
                log("lobby socket message ignored: %s" % e)

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
                self.close_socket()
                self.next_socket_try = 0.0
                return   # the server stopped talking; it is restarting or gone
            if self.pending is not None:
                self.register()
            if not self.code:
                return
            if self.push is None and now >= self.next_socket_try:
                self.open_socket()
            if self.push is not None and self.state_sent_at and time.monotonic() - self.state_sent_at > STATE_REPLY:
                self.drop_socket("a heartbeat went unanswered")
            if time.monotonic() >= self.next_heartbeat:
                state = {"players": self.players, "open": self.open, "phase": self.phase,
                         "maxPlayers": self.max_players}
                if self.vr_only is not None:
                    state["vrOnly"] = self.vr_only
                if self.flat_players is not None:
                    state["flatPlayers"] = min(self.flat_players, self.players)
                if self.devices is not None:
                    state["devices"] = self.devices
                if self.hand_only is not None:
                    state["handReloadOnly"] = self.hand_only
                if self.button_players is not None:
                    state["buttonPlayers"] = min(self.button_players, self.players)
                if self.name:
                    state["name"] = self.name
                sent = False
                if self.push is not None:
                    try:
                        self.push.send(json.dumps(dict(state, type="state")))
                        self.state_sent_at = time.monotonic()
                        sent = True
                    except OSError as e:
                        self.drop_socket(str(e))
                if not sent:
                    try:
                        http("PUT", "%s/%s" % (BASE, self.code), state, self.token)
                    except HttpError as e:
                        if self.lost(e):
                            return
                        raise
                self.next_heartbeat = time.monotonic() + 15
            if self.push is not None and time.monotonic() - self.last_ping >= SOCKET_PING:
                self.last_ping = time.monotonic()
                try:
                    self.push.send("ping")
                except OSError:
                    pass   # its "closed" follows and polling resumes
            # With a socket the Worker sends each request itself (the pending ones on connect).
            if self.push is None and self.open and time.monotonic() >= self.next_requests:
                try:
                    requests = http("GET", "%s/%s/joins" % (BASE, self.code), None, self.token)["requests"]
                except HttpError as e:
                    if self.lost(e):
                        return
                    raise
                self.next_requests = time.monotonic() + (4 if self.phase == "waiting" else 10)
                for request in requests:
                    self.request(request["id"], request["offer"])
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


def run_server(argv, public, stopping, key):
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
    lobby = Lobby(send, key)
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
        lobby.pump()
        if time.monotonic() >= next_poll:
            lobby.poll()
            next_poll = time.monotonic() + 2
    lobby.stop()   # the server's own "stop" got there first, unless it crashed
    code = server.wait()
    out.close()
    return code


def main():
    argv = sys.argv[1:]
    if len(argv) == 2 and argv[0] == "--key-hash":
        try:
            with open(argv[1], encoding="utf-8") as f:
                print(key_hash(f.read()))
        except OSError as e:
            sys.exit("cannot read the server key: %s" % e)
        return
    if "--" not in argv:
        sys.exit(__doc__)
    split = argv.index("--")
    options, server_argv = argv[:split], argv[split + 1:]
    if not server_argv:
        sys.exit(__doc__)
    public = "--public" in options
    restart = "--restart" in options
    key = os.environ.get("GEVR_SERVER_KEY", "").strip()
    if "--key-file" in options:
        at = options.index("--key-file") + 1
        if at >= len(options):
            sys.exit("--key-file needs a file")
        try:
            with open(options[at], encoding="utf-8") as f:
                key = f.read().strip()
        except OSError as e:
            sys.exit("cannot read the server key: %s" % e)

    stopping = threading.Event()
    for sig in (signal.SIGTERM, signal.SIGINT):
        signal.signal(sig, lambda *_: stopping.set())

    while True:
        started = time.monotonic()
        code = run_server(server_argv, public, stopping, key)
        log("server exited (%d)" % code)
        if stopping.is_set() or not restart:
            return code
        # a server that dies at once is not restarted in a tight loop
        time.sleep(max(0.0, 10 - (time.monotonic() - started)))


if __name__ == "__main__":
    sys.exit(main())
