"""Cross-platform sync's Google Drive (port/pc/gevr_sync_drive.cpp, docs/cloud-sync.md)
against a stand-in for Google's sign-in and Drive API on 127.0.0.1: the device code
sign-in, the folder made once, records found by their tag (renamed, moved or
trashed), two devices syncing, a token refreshed, unlinking."""
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import urlparse, parse_qs
import itertools
import json
import os
import re
import shutil
import subprocess
import tempfile
import threading

ROOT = Path(__file__).resolve().parents[2]


class FakeGoogle:
    def __init__(self):
        self.files = {}           # id -> dict(name, mimeType, parents, appProperties, data, trashed, created, modified)
        self.ids = itertools.count(1)
        self.clock = itertools.count(1)
        self.pending = {}         # device_code -> polls left before it is allowed
        self.refresh = {}         # refresh token -> access token generation
        self.access = set()
        self.revoked = []
        self.lock = threading.Lock()
        self.calls = []

    def new_id(self):
        return "f%d" % next(self.ids)


def handler_for(g):
    class H(BaseHTTPRequestHandler):
        def log_message(self, *a):
            pass

        def reply(self, code, obj=None, raw=None):
            body = raw if raw is not None else json.dumps(obj or {}).encode()
            self.send_response(code)
            self.send_header("Content-Type", "application/json")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)

        def body(self):
            n = int(self.headers.get("Content-Length") or 0)
            return self.rfile.read(n) if n else b""

        def authed(self):
            a = self.headers.get("Authorization", "")
            ok = a.startswith("Bearer ") and a[7:] in g.access
            if not ok:
                self.reply(401, {"error": {"message": "Invalid Credentials"}})
            return ok

        def do_POST(self):
            u = urlparse(self.path)
            with g.lock:
                g.calls.append(("POST", u.path))
                if u.path == "/device/code":
                    f = parse_qs(self.body().decode())
                    if f.get("scope") != ["https://www.googleapis.com/auth/drive.file"]:
                        return self.reply(400, {"error": "invalid_scope"})
                    code = "dc%d" % next(g.ids)
                    g.pending[code] = 2
                    return self.reply(200, {"device_code": code, "user_code": "GEVR-TEST",
                                            "verification_url": "https://www.google.com/device",
                                            "expires_in": 60, "interval": 1})
                if u.path == "/token":
                    f = {k: v[0] for k, v in parse_qs(self.body().decode()).items()}
                    if f.get("client_secret") != "test-secret":
                        return self.reply(401, {"error": "invalid_client"})
                    if f.get("grant_type") == "urn:ietf:params:oauth:grant-type:device_code":
                        left = g.pending.get(f.get("device_code"))
                        if left is None:
                            return self.reply(400, {"error": "expired_token"})
                        if left > 0:
                            g.pending[f["device_code"]] = left - 1
                            return self.reply(428, {"error": "authorization_pending"})
                        rt = "rt%d" % next(g.ids)
                        at = "at%d" % next(g.ids)
                        g.refresh[rt] = 0
                        g.access.add(at)
                        return self.reply(200, {"access_token": at, "expires_in": 3599, "refresh_token": rt})
                    if f.get("grant_type") == "refresh_token":
                        if f.get("refresh_token") not in g.refresh:
                            return self.reply(400, {"error": "invalid_grant"})
                        at = "at%d" % next(g.ids)
                        g.access.add(at)
                        return self.reply(200, {"access_token": at, "expires_in": 3599})
                    return self.reply(400, {"error": "unsupported_grant_type"})
                if u.path == "/revoke":
                    tok = parse_qs(self.body().decode()).get("token", [""])[0]
                    g.revoked.append(tok)
                    g.refresh.pop(tok, None)
                    return self.reply(200, {})
                if not self.authed():
                    return
                if u.path == "/drive/v3/files":
                    meta = json.loads(self.body())
                    fid = g.new_id()
                    g.files[fid] = dict(name=meta["name"], mimeType=meta.get("mimeType"), parents=meta.get("parents", []),
                                        appProperties=meta.get("appProperties", {}), data=b"", trashed=False,
                                        created=next(g.clock), modified=next(g.clock))
                    return self.reply(200, {"id": fid})
                if u.path == "/upload/drive/v3/files":
                    ctype = self.headers.get("Content-Type", "")
                    boundary = re.search(r"boundary=(\S+)", ctype).group(1).encode()
                    parts = self.body().split(b"--" + boundary)
                    meta = json.loads(parts[1].split(b"\r\n\r\n", 1)[1].rsplit(b"\r\n", 1)[0])
                    data = parts[2].split(b"\r\n\r\n", 1)[1].rsplit(b"\r\n", 1)[0]
                    fid = g.new_id()
                    g.files[fid] = dict(name=meta["name"], mimeType=meta.get("mimeType"), parents=meta.get("parents", []),
                                        appProperties=meta.get("appProperties", {}), data=data, trashed=False,
                                        created=next(g.clock), modified=next(g.clock))
                    return self.reply(200, {"id": fid})
            self.reply(404, {"error": {"message": "no " + u.path}})

        def do_PATCH(self):
            u = urlparse(self.path)
            with g.lock:
                g.calls.append(("PATCH", u.path))
                if not self.authed():
                    return
                m = re.fullmatch(r"/upload/drive/v3/files/(\w+)", u.path)
                if m and m.group(1) in g.files and not g.files[m.group(1)]["trashed"]:
                    g.files[m.group(1)]["data"] = self.body()
                    g.files[m.group(1)]["modified"] = next(g.clock)
                    return self.reply(200, {"id": m.group(1)})
            self.reply(404, {"error": {"message": "File not found"}})

        def do_GET(self):
            u = urlparse(self.path)
            qs = parse_qs(u.query)
            with g.lock:
                g.calls.append(("GET", u.path))
                if not self.authed():
                    return
                if u.path == "/drive/v3/about":
                    return self.reply(200, {"user": {"emailAddress": "bond@example.com"}})
                if u.path == "/drive/v3/files":
                    q = qs.get("q", [""])[0]
                    role = re.search(r"value='(\w+)'", q).group(1)
                    hits = [(fid, f) for fid, f in g.files.items()
                            if f["appProperties"].get("gevr") == role and not f["trashed"]]
                    key = "created" if "createdTime" in qs.get("orderBy", [""])[0] else "modified"
                    hits.sort(key=lambda h: h[1][key], reverse=(key == "modified"))
                    return self.reply(200, {"files": [{"id": fid, "name": f["name"], "appProperties": f["appProperties"]}
                                                      for fid, f in hits]})
                m = re.fullmatch(r"/drive/v3/files/(\w+)", u.path)
                if m and qs.get("alt") == ["media"] and m.group(1) in g.files:
                    return self.reply(200, raw=g.files[m.group(1)]["data"])
            self.reply(404, {"error": {"message": "no " + u.path}})
    return H


def build(temp):
    exe = Path(temp) / ("sync_drive_test" + (".exe" if os.name == "nt" else ""))
    miniz = Path(temp) / "miniz.o"
    subprocess.run([shutil.which("gcc") or "gcc", "-O1", "-c", "port/vr/miniz/miniz.c", "-o", str(miniz)], cwd=ROOT, check=True)
    args = [shutil.which("g++") or "g++", "-std=c++20", "-O1", "-Wno-unused-function",
            "-DGEVR_PC=1", "-DGEVR_SYNC_TEST=1", "-DGEVR_SYNC_DRIVE=1", '-DGEVR_APP_VERSION="test"',
            "-Iport/pc", "-Iport/include", "port/tests/sync_native.cpp", "port/pc/gevr_sync_core.cpp",
            "port/pc/gevr_sync.cpp", "port/pc/gevr_sync_drive.cpp", "port/pc/pc_http.cpp", "port/pc/pc_files.cpp",
            str(miniz), "-o", str(exe)]
    if os.name == "nt":
        args[1:1] = ["-include", "io.h"]
        args += ["-lwinhttp", "-lbcrypt"]
    else:
        args += ["-lcurl", "-lpthread"]
    subprocess.run(args, cwd=ROOT, check=True)
    return exe


def check(ok, what):
    if not ok:
        raise AssertionError(what)


g = FakeGoogle()
server = ThreadingHTTPServer(("127.0.0.1", 0), handler_for(g))
threading.Thread(target=server.serve_forever, daemon=True).start()
base = "http://127.0.0.1:%d" % server.server_address[1]

with tempfile.TemporaryDirectory(prefix="gevr-sync-drive-") as temp:
    exe = build(temp)
    env = dict(os.environ, GEVR_GOOGLE_CLIENT_ID="test-client", GEVR_GOOGLE_CLIENT_SECRET="test-secret",
               GEVR_GOOGLE_OAUTH_URL=base, GEVR_GOOGLE_API_URL=base)
    env.pop("GEVR_SYNC_DIR", None)

    def run(*a):
        out = subprocess.run([str(exe), *map(str, a)], env=env, check=True, capture_output=True, text=True, timeout=120).stdout
        return dict(line.split("=", 1) for line in out.splitlines() if "=" in line)

    def ini(folder):
        return dict(l.split("=", 1) for l in (folder / "goldeneye-vr.ini").read_text().splitlines() if "=" in l)

    t = Path(temp)
    a, b = t / "quest", t / "pc"
    a.mkdir()
    b.mkdir()
    (a / "goldeneye-vr.ini").write_text("[VR]\nPadLookSpeed=1.7\nGuest2Name=Den\n")
    (b / "goldeneye-vr.ini").write_text("[VR]\nPadLookSpeed=0.5\n")

    # not linked: nothing reaches Google
    r = run("launch", a, "quest")
    check(r["active"] == "0" and not g.calls, "off until linked")

    # the sign-in: a code shown, Google polled until allowed, the account kept
    r = run("drive-link", a, "quest")
    check(r.get("code") == "GEVR-TEST" and r["url"] == "https://www.google.com/device", "the code and where to enter it")
    check(r["state"] == "5" and r["account"] == "bond@example.com" and r["syncing"] == "0", "linked, syncs from the next start")
    link = (a / ".gevr-sync" / "google.txt").read_text()
    check("refresh=rt" in link and "email=bond@example.com" in link, "the sign-in kept in the save folder")

    # the first launch: the folder (with its README) and this device's record
    r = run("launch", a, "quest")
    check(r["active"] == "1", "syncs once linked")
    folders = [f for f in g.files.values() if f["appProperties"].get("gevr") == "root"]
    readme = [f for f in g.files.values() if f["appProperties"].get("gevr") == "readme"]
    records = {fid: f for fid, f in g.files.items() if f["appProperties"].get("gevr") == "record"}
    check(len(folders) == 1 and folders[0]["name"] == "GoldenEye VR", "one GoldenEye VR folder")
    check(len(readme) == 1 and b"Please don't edit" in readme[0]["data"], "a README beside the records")
    check(len(records) == 1, "one record")
    rid, rec = next(iter(records.items()))
    check(rec["name"].startswith("device-") and rec["appProperties"]["name"] == rec["name"], "the record is tagged")
    check(b"PadLookSpeed\t1.7" in rec["data"] and b"Guest2Name" not in rec["data"], "settings up, the guest seats stay")

    # a second device links and joins: the first one's settings win
    run("drive-link", b, "pc")
    r = run("launch", b, "pc")
    check(ini(b)["PadLookSpeed"] == "1.7", "the second device takes the first one's settings")
    check(len([f for f in g.files.values() if f["appProperties"].get("gevr") == "root"]) == 1, "the folder is made once")

    # a change pushed from the PC reaches the Quest, though the player renamed its record
    (b / "goldeneye-vr.ini").write_text("[VR]\nPadLookSpeed=2.2\n")
    run("push", b, "pc")
    pc_record = [f for f in g.files.values() if f["appProperties"].get("gevr") == "record" and f is not rec][0]
    pc_record["name"] = "renamed by the player.txt"
    run("launch", a, "quest")
    check(ini(a)["PadLookSpeed"] == "2.2", "a pushed change arrives through a renamed record")
    records = [f for f in g.files.values() if f["appProperties"].get("gevr") == "record" and not f["trashed"]]
    check(len(records) == 2, "no duplicate made for a renamed record")

    # the Quest's record trashed: made again at its next launch
    rec["trashed"] = True
    (a / "goldeneye-vr.ini").write_text("[VR]\nPadLookSpeed=3.3\nGuest2Name=Den\n")
    run("launch", a, "quest")
    live = [f for f in g.files.values() if f["appProperties"].get("gevr") == "record" and not f["trashed"]]
    check(len(live) == 2 and any(b"PadLookSpeed\t3.3" in f["data"] for f in live), "a trashed record is made again")
    run("launch", b, "pc")
    check(ini(b)["PadLookSpeed"] == "3.3", "and the other device reads it")

    # access tokens die: the stored refresh token gets a new one
    g.access.clear()
    r = run("launch", b, "pc")
    check(r["active"] == "1", "a dead access token is refreshed")

    # the PC signed out on Google's side: its launch changes nothing and says why
    g.refresh.pop(re.search(r"refresh=(\S+)", (b / ".gevr-sync" / "google.txt").read_text()).group(1))
    before = ini(b)
    r = run("launch", b, "pc")
    check(r["active"] == "0" and ini(b) == before, "a revoked sign-in changes nothing")
    check(not (b / ".gevr-sync" / "google.txt").exists()
          and "Link it again" in (b / ".gevr-sync" / "google-signed-out.txt").read_text(),
          "signed out here too, with the reason kept for the launcher")
    r = run("launch", b, "pc")
    check(r["active"] == "0", "stays off until linked again")
    run("drive-link", b, "pc")
    check(not (b / ".gevr-sync" / "google-signed-out.txt").exists(), "linking again clears the reason")
    r = run("launch", b, "pc")
    check(r["active"] == "1" and ini(b)["PadLookSpeed"] == "3.3", "and syncs again")

    # unlinking: the file goes, Google is told
    rt = re.search(r"refresh=(\S+)", (a / ".gevr-sync" / "google.txt").read_text()).group(1)
    r = run("drive-unlink", a, "quest")
    check(r["state"] == "1" and not (a / ".gevr-sync" / "google.txt").exists() and rt in g.revoked,
          "unlinked: forgotten here and at Google")
    r = run("launch", a, "quest")
    check(r["active"] == "0", "off again once unlinked")

server.shutdown()
print("sync drive: ok")
