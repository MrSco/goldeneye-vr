"""Cross-platform sync's Google Drive (port/pc/gevr_sync_drive.cpp, docs/cloud-sync.md)
against a stand-in for Google's sign-in and Drive API on 127.0.0.1: the device code
sign-in, the folder made once, records found by their tag (renamed, moved or
trashed), two devices syncing, a token refreshed, unlinking. Then ROMs and packs
(gevr_sync_files.cpp): resumable uploads in pieces, downloads, a pack zipped and
unpacked, already here stays, a changed ROM waiting for the next launch, the old
blob removed; the same through a folder store; and step 4's device name and a
save set aside used again."""
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
        self.sessions = {}        # resumable upload id -> dict(meta, total, data)
        self.pieces = 0
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
                if u.path == "/upload/drive/v3/files" and parse_qs(u.query).get("uploadType") == ["resumable"]:
                    sid = "s%d" % next(g.ids)
                    g.sessions[sid] = dict(meta=json.loads(self.body()), data=bytearray(),
                                           total=int(self.headers.get("X-Upload-Content-Length")))
                    self.send_response(200)
                    self.send_header("Location", base_url[0] + "/upload/session/" + sid)
                    self.send_header("Content-Length", "0")
                    self.end_headers()
                    return
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

        def do_PUT(self):
            u = urlparse(self.path)
            with g.lock:
                g.calls.append(("PUT", u.path))
                if not self.authed():
                    return
                m = re.fullmatch(r"/upload/session/(\w+)", u.path)
                if not m or m.group(1) not in g.sessions:
                    return self.reply(404, {"error": {"message": "no such upload"}})
                sess = g.sessions[m.group(1)]
                rng = re.fullmatch(r"bytes (\d+)-(\d+)/(\d+)", self.headers.get("Content-Range", ""))
                body = self.body()
                if not rng or int(rng.group(1)) != len(sess["data"]) or int(rng.group(2)) - int(rng.group(1)) + 1 != len(body):
                    return self.reply(400, {"error": {"message": "bad range"}})
                if len(body) % (256 * 1024) and int(rng.group(2)) + 1 != sess["total"]:
                    return self.reply(400, {"error": {"message": "a piece not a multiple of 256 KiB"}})
                sess["data"] += body
                g.pieces += 1
                if len(sess["data"]) < sess["total"]:
                    self.send_response(308)
                    self.send_header("Range", "bytes=0-%d" % (len(sess["data"]) - 1))
                    self.send_header("Content-Length", "0")
                    self.end_headers()
                    return
                meta = sess["meta"]
                fid = g.new_id()
                g.files[fid] = dict(name=meta["name"], mimeType="application/octet-stream", parents=meta.get("parents", []),
                                    appProperties=meta.get("appProperties", {}), data=bytes(sess["data"]), trashed=False,
                                    created=next(g.clock), modified=next(g.clock))
                del g.sessions[m.group(1)]
                return self.reply(200, {"id": fid})

        def do_DELETE(self):
            u = urlparse(self.path)
            with g.lock:
                g.calls.append(("DELETE", u.path))
                if not self.authed():
                    return
                m = re.fullmatch(r"/drive/v3/files/(\w+)", u.path)
                if m and m.group(1) in g.files:
                    del g.files[m.group(1)]
                    self.send_response(204)
                    self.send_header("Content-Length", "0")
                    self.end_headers()
                    return
            self.reply(404, {"error": {"message": "File not found"}})

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
                    return self.reply(200, {"files": [{"id": fid, "name": f["name"], "appProperties": f["appProperties"],
                                                       "size": str(len(f["data"]))} for fid, f in hits]})
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
            "port/pc/gevr_sync.cpp", "port/pc/gevr_sync_drive.cpp", "port/pc/gevr_sync_files.cpp",
            "port/pc/pc_http.cpp", "port/pc/pc_files.cpp",
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
base_url = [base]

with tempfile.TemporaryDirectory(prefix="gevr-sync-drive-") as temp:
    exe = build(temp)
    env = dict(os.environ, GEVR_GOOGLE_CLIENT_ID="test-client", GEVR_GOOGLE_CLIENT_SECRET="test-secret",
               GEVR_GOOGLE_OAUTH_URL=base, GEVR_GOOGLE_API_URL=base)
    env.pop("GEVR_SYNC_DIR", None)
    env["GEVR_SYNC_NO_FILES_WORKER"] = "1"   # the test runs the files passes itself

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

    # ---- ROMs and packs through Drive (step 3) ----
    def blobs():
        return {f["appProperties"]["blob"]: f for f in g.files.values() if f["appProperties"].get("gevr") == "blob"}

    rom = os.urandom(5 * 1024 * 1024 + 12345)   # two pieces and a bit
    (a / "ge.z64").write_bytes(rom)
    hd = a / "texture-packs" / "hd"
    (hd / "sub").mkdir(parents=True)
    (hd / "one.png").write_bytes(os.urandom(70000))
    (hd / "sub" / "two.png").write_bytes(os.urandom(3000))
    (hd / "empty.txt").write_bytes(b"")
    run("drive-link", a, "quest")
    run("launch", a, "quest")
    g.pieces = 0
    r = run("files-packs", a, "quest")
    check(r["files"].startswith("2 0 "), "the ROM and the pack go up: " + r["files"])
    check(len(blobs()) == 2 and g.pieces >= 3, "two blobs, the ROM in pieces")
    rom_blob = [k for k in blobs() if k.startswith("r")][0]
    check(blobs()[rom_blob]["data"] == rom and blobs()[rom_blob]["name"] == "ge.z64", "the ROM whole, named for a person")
    r = run("files-packs", a, "quest")
    check(r["files"].startswith("0 0 "), "nothing moves a second time: " + r["files"])

    # the PC (linked again earlier): the ROM comes, the pack only with Big packs too
    r = run("files", b, "pc")
    check((b / "ge.z64").read_bytes() == rom and r["rom"] == "1" and "ge.z64" in r.get("message", ""),
          "the ROM arrives and the launcher hears of it")
    check(not (b / "texture-packs").exists(), "no pack without Big packs too")
    r = run("files-packs", b, "pc")
    check((b / "texture-packs/hd/one.png").read_bytes() == (hd / "one.png").read_bytes()
          and (b / "texture-packs/hd/sub/two.png").read_bytes() == (hd / "sub" / "two.png").read_bytes()
          and (b / "texture-packs/hd/empty.txt").exists(), "the pack arrives whole")
    check(len(blobs()) == 2, "the same content is not uploaded again")

    # a changed ROM on the Quest: the PC has the old one, so the new one waits for its next launch
    rom2 = os.urandom(300000)
    (a / "ge.z64").write_bytes(rom2)
    r = run("files", a, "quest")
    check(r["files"].startswith("1 0 "), "the change goes up: " + r["files"])
    check(rom_blob not in blobs(), "the blob it replaced is removed")
    r = run("files", b, "pc")
    check(r["files"].split()[3] == "1" and (b / "ge.z64").read_bytes() == rom, "in use here: it waits")
    run("launch", b, "pc")
    check((b / "ge.z64").read_bytes() == rom2, "in place at the next launch")
    r = run("files", b, "pc")
    check(r["files"].startswith("0 0 "), "and in step after: " + r["files"])

    # already here stays: a new device with its own ge.z64 keeps it
    c = t / "tv"
    c.mkdir()
    (c / "goldeneye-vr.ini").write_text("[VR]\n")
    mine = os.urandom(1000)
    (c / "ge.z64").write_bytes(mine)
    (c / ".gevr-sync").mkdir()
    shutil.copy(a / ".gevr-sync" / "google.txt", c / ".gevr-sync" / "google.txt")
    run("launch", c, "tv")
    r = run("files", c, "tv")
    check((c / "ge.z64").read_bytes() == mine and r["files"].startswith("0 0 "), "a ROM already here stays")

    # ---- step 4: the device's name in its record, a save set aside used again ----
    r = run("name", c, "tv", "Den")
    check(r["name"] == "Den", "the name kept")
    check(any(b"name\tDen" in f["data"] for f in g.files.values() if f["appProperties"].get("gevr") == "record"),
          "and carried in the record")
    (c / "eeprom.bin").write_bytes(b"\0" * 512)
    aside = "eeprom.bin.before-sync-20261125-201400"
    kept = bytearray(512)
    kept[32 + 20] = 7
    (c / aside).write_bytes(bytes(kept))
    r = run("aside", c, "tv", aside)
    check(r.get("aside") == aside and r.get("chosen") == aside, "listed and chosen")
    r = run("launch", c, "tv")
    check((c / "eeprom.bin").read_bytes() == bytes(kept) and not (c / aside).exists(), "used again at the next start")
    check("message" in r and "save you chose" in r["message"], "and said so")

    # ---- the same through a folder store (GEVR_SYNC_DIR) ----
    store = t / "folder-store"
    store.mkdir()
    fenv = dict(env)
    for k in ("GEVR_GOOGLE_CLIENT_ID", "GEVR_GOOGLE_CLIENT_SECRET"):
        fenv.pop(k, None)
    fenv["GEVR_SYNC_DIR"] = str(store)
    x, y = t / "fx", t / "fy"
    for d in (x, y):
        d.mkdir()
        (d / "goldeneye-vr.ini").write_text("[VR]\n")
    (x / "data").mkdir()
    (x / "data" / "gf.z64").write_bytes(os.urandom(4096))

    def frun(*a_):
        out = subprocess.run([str(exe), *map(str, a_)], env=fenv, check=True, capture_output=True, text=True, timeout=120).stdout
        return dict(line.split("=", 1) for line in out.splitlines() if "=" in line)

    frun("launch", x, "pc")
    frun("files", x, "pc")
    frun("launch", y, "quest")
    frun("files", y, "quest")
    check((y / "data" / "gf.z64").read_bytes() == (x / "data" / "gf.z64").read_bytes(), "a ROM in data/ through a folder")
    check(len([n for n in os.listdir(store) if n.startswith("blob-")]) == 1, "one blob in the folder")

server.shutdown()
print("sync drive: ok")
