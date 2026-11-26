"""Mod packs (docs/mod-system.md, phase 2a): mod.json, install from a zip,
the installed list, and the mod campaign a run plays.

Builds modpack_native.c around the production port/src/gevr_modpack.c,
gevr_sha256.c and miniz, with libultra.c's osEepromSetPath and
gevr_ghost.c's ghostCampaignTag pasted in, then drives it with zips made
here. Run: python port/tests/test_modpack.py
"""
import hashlib
import json
import os
import shutil
import subprocess
import sys
import tempfile
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
FAILS = []


def function(source, signature):
    start = source.index(signature)
    opening = source.index("{", start)
    end, depth = opening + 1, 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]


def check(cond, what):
    if not cond:
        FAILS.append(what)
        print("FAIL:", what)


def build(temp):
    native = (ROOT / "port/tests/modpack_native.c").read_text()
    save = (ROOT / "port/src/libultra.c").read_bytes().decode().replace("\r\n", "\n")
    ghost = (ROOT / "src/game/gevr_ghost.c").read_bytes().decode().replace("\r\n", "\n")
    native = native.replace("/* SAVE */", function(save, "static inline void osEepromSetPath("))
    native = native.replace("/* GHOST_TAG */", function(ghost, "static const char *ghostCampaignTag("))
    src = temp / "modpack_test.c"
    src.write_text(native)
    exe = temp / ("modpack_test.exe" if os.name == "nt" else "modpack_test")
    cc = shutil.which("gcc") or "cc"
    # the sanitizers where the compiler has them (not Windows' MinGW gcc): run in WSL for them
    sanitize = [] if os.name == "nt" else ["-fsanitize=address,undefined", "-fno-omit-frame-pointer"]
    # as PC VR (GEVR_PCVR): the session's play mode follows VrPlayMode (Play modes)
    subprocess.run([cc, "-std=gnu11", "-O1", "-Wall", "-Wno-unused-function", "-DGEVR=1", "-D_LANGUAGE_C=1", "-DGEVR_PCVR=1", *sanitize,
                    "-I" + str(ROOT / "include"), "-I" + str(ROOT / "port/include"),
                    str(src), str(ROOT / "port/src/gevr_modpack.c"), str(ROOT / "port/src/gevr_sha256.c"),
                    str(ROOT / "port/vr/miniz/miniz.c"), "-o", str(exe)], check=True)
    return exe


def settings_round_trip(temp):
    """CampaignMod= in goldeneye-vr.ini, cut from vr_settings.cpp: text even when it reads as a number."""
    settings = (ROOT / "port/vr/vr_settings.cpp").read_bytes().decode().replace("\r\n", "\n")
    start = settings.index('    iniPrintf(f, "; A mod campaign')
    save = settings[start:settings.index("\n", settings.index('"CampaignMod=%s', start)) + 1].replace("iniPrintf(", "fprintf(")
    load = function(settings, 'if (strncmp(line, "CampaignMod=", 12) == 0)')
    fixture = r'''
#include <cassert>
#include <cstdio>
#include <cstring>
char VrCampaignMod[33];
static void save(FILE *f) {
SAVE}
static void loadLine(const char *line) { for (int once = 0; once < 1; once++) { LOAD } }
int main() {
    strcpy(VrCampaignMod, "tactical");
    FILE *f = tmpfile(); assert(f); save(f); rewind(f); VrCampaignMod[0] = 0;
    char line[256]; while (fgets(line, sizeof(line), f)) loadLine(line); fclose(f);
    assert(!strcmp(VrCampaignMod, "tactical"));
    loadLine("CampaignMod=1942\r\n"); assert(!strcmp(VrCampaignMod, "1942"));
    loadLine("CampaignMod=a-mod \t\n"); assert(!strcmp(VrCampaignMod, "a-mod"));
    loadLine("CampaignMod=\n"); assert(!VrCampaignMod[0]);
    puts("ok");
}
'''.replace("SAVE", save).replace("LOAD", load)
    src, exe = temp / "settings.cpp", temp / ("settings.exe" if os.name == "nt" else "settings")
    src.write_text(fixture)
    subprocess.run([shutil.which("g++") or "c++", "-std=c++17", "-O1", str(src), "-o", str(exe)], check=True)
    out = subprocess.run([str(exe)], capture_output=True, text=True)
    check(out.returncode == 0 and out.stdout.strip() == "ok", "CampaignMod= round trip: " + out.stderr)


def updater_sha256(temp):
    """pc_http.cpp's sha256File (the PC updater's download check) over the shared gevr_sha256.c."""
    http = (ROOT / "port/pc/pc_http.cpp").read_bytes().decode().replace("\r\n", "\n")
    fixture = r'''
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>
#include "gevr_sha256.h"
#ifdef _WIN32
static std::wstring wide(const std::string &s) { return std::wstring(s.begin(), s.end()); }
#endif
FUNCTION
static void put(const char *name, const std::string &data) {
    FILE *f = fopen(name, "wb"); assert(f); fwrite(data.data(), 1, data.size(), f); fclose(f);
}
int main() {
    put("abc.bin", "abc");
    assert(sha256File("abc.bin") == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    put("empty.bin", "");
    assert(sha256File("empty.bin") == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    put("million.bin", std::string(1000000, 'a'));
    assert(sha256File("million.bin") == "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
    assert(sha256File("missing.bin").empty());
    puts("ok");
}
'''.replace("FUNCTION", function(http, "std::string sha256File(const std::string &path)"))
    src, exe = temp / "updater_sha.cpp", temp / ("updater_sha.exe" if os.name == "nt" else "updater_sha")
    src.write_text(fixture)
    subprocess.run([shutil.which("g++") or "c++", "-std=c++17", "-O1", "-I" + str(ROOT / "port/include"), str(src),
                    str(ROOT / "port/src/gevr_sha256.c"), "-o", str(exe)], check=True)
    out = subprocess.run([str(exe)], cwd=temp, capture_output=True, text=True)
    check(out.returncode == 0 and out.stdout.strip() == "ok", "pc_http.cpp sha256File: " + out.stderr)


class Runner:
    def __init__(self, exe, cwd):
        self.exe, self.cwd = exe, cwd

    def __call__(self, *args, env=None):
        out = subprocess.run([str(self.exe), *map(str, args)], cwd=self.cwd, capture_output=True, text=True,
                             env={**os.environ, **env} if env else None)
        assert out.returncode == 0, out.stderr
        return out.stdout.strip()


MANIFEST = {"format": 1, "id": "tactical", "title": "Tactical", "by": "Kelly-D-Ripper", "version": "1.0.0",
            "api": 1, "kind": "campaign", "base": "gf", "coop": True, "site": "https://example.org/tactical"}
FILES = {"scripts/main.lua": b"print('hi')\n", "assets/models/vest.bin": bytes(range(256)) * 9,
         "assets/empty.dat": b"", "README.md": b"# Tactical\n"}


def sha256_list(files, eol="\n", marker=" "):
    return "".join(f"{hashlib.sha256(data).hexdigest()} {marker}{path}{eol}" for path, data in sorted(files.items())).encode()


def make_zip(path, manifest=None, files=None, prefix="", listing=None, extra=(), drop=(), method=zipfile.ZIP_DEFLATED):
    manifest = dict(MANIFEST) if manifest is None else manifest
    files = dict(FILES if files is None else files)
    body = manifest if isinstance(manifest, (bytes, str)) else json.dumps(manifest, indent=2)
    files["mod.json"] = body.encode() if isinstance(body, str) else body
    listed = sha256_list(files) if listing is None else listing
    def entry(z, name, data):
        info = zipfile.ZipInfo("x")
        info.filename = info.orig_filename = name   # as given: ZipInfo would turn a Windows '\\' into '/'
        info.compress_type = method
        z.writestr(info, data)

    with zipfile.ZipFile(path, "w", method) as z:
        for name, data in list(files.items()) + [("files.sha256", listed)]:
            if name not in drop:
                entry(z, prefix + name, data)
        for name, data in extra:
            entry(z, name, data)
    return listed


def main():
    with tempfile.TemporaryDirectory(prefix="gevr-modpack-") as temp:
        temp = Path(temp)
        settings_round_trip(temp)
        updater_sha256(temp)
        exe = build(temp)
        work = temp / "run"
        (work / "save").mkdir(parents=True)
        run = Runner(exe, work)
        mods = work / "save/texture-packs/mods"
        zips = temp / "zips"
        zips.mkdir()

        def z(name, **kw):
            path = zips / (name + ".zip")
            make_zip(path, **kw)
            return path

        def leftovers():
            return sorted(p.name for p in mods.iterdir() if p.name.startswith(".")) if mods.exists() else []

        # ---- SHA-256: FIPS 180-4's answers and hashlib's, across block edges ----
        for data in (b"", b"abc", b"a" * 55, b"a" * 56, b"a" * 63, b"a" * 64, b"a" * 65, b"a" * 1000000, os.urandom(4099)):
            f = temp / "sha.bin"
            f.write_bytes(data)
            check(run("sha", f) == hashlib.sha256(data).hexdigest(), f"sha256 of {len(data)} bytes")
        f.write_bytes(b"abc")
        check(run("sha", f) == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad", "sha256 abc known answer")

        # ---- mod.json ----
        def manifest(text):
            f = temp / "mod.json"
            f.write_bytes(text if isinstance(text, bytes) else text.encode())
            return run("manifest", f)

        def with_(**changes):
            m = dict(MANIFEST)
            for k, v in changes.items():
                if v is None:
                    m.pop(k)
                else:
                    m[k] = v
            return json.dumps(m)

        out = manifest(json.dumps(MANIFEST))
        check(out == "OK id=tactical title=Tactical by=Kelly-D-Ripper version=1.0.0 api=1 kind=1 base=1 coop=1 "
              "affects=1 modes=0 play=3 site=https://example.org/tactical\nvr=-\nflat=-\nserver=-",
              "a full manifest reads, and runs in VR and flat without \"playModes\": " + out)
        # ---- play modes (docs/mod-system.md, Play modes) ----
        for modes, play, vr, flat in ((["vr"], 1, "-", "This mod is for VR"),
                                      (["flat"], 2, "This mod is for flat screen", "-"),
                                      (["vr", "flat"], 3, "-", "-"), (["flat", "vr"], 3, "-", "-")):
            out = manifest(with_(playModes=modes))
            check(out.splitlines() == [out.splitlines()[0], f"vr={vr}", f"flat={flat}", "server=-"] and f" play={play} " in out,
                  f"playModes {modes}: " + out)
        out = manifest(with_(kind="overlay", affectsPlay=True, modes=["coop"], playModes=["vr"]))
        check(" modes=1 play=1 " in out, "an overlay names its play modes beside its multiplayer modes: " + out)
        check(run("session") == "stereo=1 screen=2 build=3", "a headset's session follows Play mode: " + run("session"))
        check(manifest(b"\xef\xbb\xbf" + json.dumps(MANIFEST).encode()).startswith("OK"), "a byte-order mark is fine")
        check(manifest(with_(coop=None, site=None)).startswith("OK"), "coop and site are optional")
        nested = json.loads(with_())
        nested["notes"] = {"a": [1, 2.5e3, {"b": None}], "t": "Café ☃"}
        check(manifest(json.dumps(nested, ensure_ascii=False)).startswith("OK"), "unknown keys, nested and UTF-8, are skipped")
        overlay = manifest(with_(kind="overlay", affectsPlay=True, modes=["coop", "horde"]))
        check("kind=2" in overlay and "affects=1" in overlay and "modes=5" in overlay, "an overlay's modes: " + overlay)
        check("modes=0" in manifest(with_(kind="overlay", modes=["coop"])), "a looks-only overlay has no modes")
        check(manifest(with_(title='Tac \\"Ops\\"')).startswith("OK"), "escaped quotes in a title")
        bad = {
            "missing id": (with_(id=None), '"id" is missing'),
            "empty": ("{}", '"format" is missing'),
            "twice": (with_()[:-1] + ', "api": 1}', '"api" appears twice'),
            "trailing comma": (with_()[:-1] + ",}", "a comma before '}'"),
            "after": (with_() + " x", "text after"),
            "not an object": ("[1]", "one JSON object"),
            "upper-case id": (with_(id="Tactical"), '"id" must be'),
            "dash id": (with_(id="-tactical"), '"id" must be'),
            "long id": (with_(id="a" * 33), '"id" is longer'),
            "long title": (with_(title="T" * 49), '"title" is longer than 48'),
            "utf-8 title": (with_(title="Café"), "plain ASCII"),
            "escaped non-ascii": ('{"title": "Caf\\u00e9"' + with_()[1:].replace('"title": "Tactical", ', ""), "plain ASCII"),
            "space in version": (with_(version="1 0"), "has a space"),
            "kind": (with_(kind="total-conversion"), '"kind" must be'),
            "base": (with_(base="perfectdark"), '"base" must be'),
            "api 2": (with_(api=2), "needs a newer GoldenEye VR"),
            "api 0": (with_(api=0), "no longer runs"),
            "format 2": (with_(format=2), '"format" 2'),
            "fraction": (with_(api=1.5), "whole number"),
            "http site": (with_(site="http://example.org"), "https://"),
            "coop string": (with_(coop="yes"), "true or false"),
            "mode": (with_(kind="overlay", affectsPlay=True, modes=["coop", "race"]), '"modes" can hold'),
            "play empty": (with_(playModes=[]), '"playModes" is empty'),
            "play unknown": (with_(playModes=["vr", "ar"]), '"playModes" can hold "vr" and "flat"'),
            "play case": (with_(playModes=["VR"]), '"playModes" can hold'),
            "play twice": (with_(playModes=["flat", "flat"]), '"playModes" names "flat" twice'),
            "play string": (with_(playModes="vr"), '"playModes" must be a list'),
            "play null": (with_()[:-1] + ', "playModes": null}', '"playModes" must be a list'),
            "play number": (with_(playModes=[1]), '"playModes" can hold'),
            "play again": (with_(playModes=["vr"])[:-1] + ', "playModes": ["vr"]}', '"playModes" appears twice'),
            "deep": ('{"x":' + "[" * 40 + "]" * 40 + "}", "nested too deep"),
            "unclosed": ('{"id": "tac', "isn't closed"),
            "control": ('{"id": "ta\tc"}', "control character"),
        }
        for name, (text, want) in bad.items():
            out = manifest(text)
            check(out.startswith("FAIL") and want in out, f"manifest {name}: {out}")

        # ---- install ----
        listed = make_zip(zips / "good.zip")
        out = run("install", zips / "good.zip")
        check(out == "OK tactical 1.0.0 " + hashlib.sha256(listed).hexdigest(), "a good mod installs, content ID = SHA-256 of files.sha256: " + out)
        for path, data in FILES.items():
            check((mods / "tactical" / path).read_bytes() == data, "installed " + path)
        check((mods / "tactical/files.sha256").read_bytes() == listed, "files.sha256 is kept")
        check(run("load", "tactical") == out, "load reads what install wrote")
        check(leftovers() == [], "nothing left beside the mod")

        # the same mod zipped in a folder, with a Mac's and Windows' extras
        folder = z("folder", prefix="tactical-1.0/", extra=[("__MACOSX/tactical-1.0/._mod.json", b"x"),
                                                         ("tactical-1.0/.DS_Store", b"x"), ("tactical-1.0/assets/Thumbs.db", b"x")])
        check(run("install", folder).startswith("OK tactical"), "a mod in one top folder, junk left out")
        check(not (mods / "tactical/.DS_Store").exists() and not (mods / "tactical/assets/Thumbs.db").exists(), "junk not unpacked")

        stored = z("stored", method=zipfile.ZIP_STORED, files={**FILES, "assets/big.bin": os.urandom(3 << 20)})
        check(run("install", stored).startswith("OK tactical"), "stored entries and a 3 MB file")
        crlf = make_zip(zips / "crlf.zip", listing=sha256_list({**FILES, "mod.json": json.dumps(MANIFEST, indent=2).encode()}, "\r\n", "*"))
        check(run("install", zips / "crlf.zip").startswith("OK tactical"), "files.sha256 with CRLF and sha256sum's binary '*'")

        def refused(name, want, **kw):
            before = sorted(p.relative_to(mods).as_posix() for p in mods.rglob("*")) if mods.exists() else []
            out = run("install", z(name, **kw))
            after = sorted(p.relative_to(mods).as_posix() for p in mods.rglob("*")) if mods.exists() else []
            check(out.startswith("FAIL") and want in out, f"install {name}: {out}")
            check(before == after, f"install {name} changed nothing installed")

        files_plus = lambda **more: {**FILES, **more}
        refused("no-list", "no files.sha256", drop=("files.sha256",))
        refused("no-manifest", "no mod.json", drop=("mod.json",))
        mod_json = json.dumps(MANIFEST, indent=2).encode()
        wrong = sha256_list({**FILES, "mod.json": mod_json}).replace(hashlib.sha256(FILES["scripts/main.lua"]).hexdigest().encode(), b"0" * 64)
        refused("mismatch", "scripts/main.lua doesn't match its checksum", listing=wrong)
        refused("unlisted", "assets/extra.png isn't in files.sha256",
                files=files_plus(**{"assets/extra.png": b"png"}), listing=sha256_list({**FILES, "mod.json": mod_json}))
        refused("missing", "lists assets/gone.png, which isn't in the zip",
                listing=sha256_list({**FILES, "mod.json": mod_json, "assets/gone.png": b"x"}))
        respelled = sha256_list({**FILES, "mod.json": mod_json}).replace(b"assets/models/vest.bin", b"Assets/models/vest.bin")
        refused("respelled", "assets/models/vest.bin isn't in files.sha256", listing=respelled)
        for name, path in (("traversal", "assets/../../evil.lua"), ("dotdot", "../evil.lua"), ("backslash", "assets\\x.png"),
                           ("device", "assets/CON.png"), ("trailing-dot", "assets/x."), ("colon", "assets/c:x")):
            refused(name, "isn't a safe name", files=files_plus(**{path: b"x"}))
        refused("absolute", "isn't a safe name", files=files_plus(**{"/etc/evil": b"x"}))
        refused("case-twice", "twice", files=files_plus(**{"assets/A.png": b"1", "assets/a.png": b"2"}))
        refused("layout", "isn't in scripts/ (.lua) or assets/", files=files_plus(**{"bin/x.so": b"x"}))
        refused("script-kind", "isn't in scripts/ (.lua) or assets/", files=files_plus(**{"scripts/notes.txt": b"x"}))
        refused("api", "needs a newer GoldenEye VR", manifest={**MANIFEST, "api": 2})
        refused("play-modes", '"playModes" can hold', manifest={**MANIFEST, "playModes": ["vr", "headset"]})
        refused("bad-json", "mod.json:", manifest="{nope")
        refused("two-folders", "b/x.txt is outside the mod's folder", prefix="a/", extra=[("b/x.txt", b"x")])
        refused("deep-manifest", "no mod.json at its top", prefix="a/b/")
        (zips / "notzip.zip").write_bytes(b"PK? not really")
        out = run("install", zips / "notzip.zip")
        check(out.startswith("FAIL") and "isn't a zip" in out, "not a zip: " + out)
        check(not (work / "evil.lua").exists() and not (temp / "evil.lua").exists(), "nothing written outside")

        # an update replaces the old files; a failed one leaves the old version
        v2files = {k: v for k, v in FILES.items() if k != "assets/empty.dat"}
        check(run("install", z("v2", manifest={**MANIFEST, "version": "2.0.0"}, files=v2files)).startswith("OK tactical 2.0.0"), "update to 2.0.0")
        check(not (mods / "tactical/assets/empty.dat").exists(), "an update drops files the new version doesn't have")
        refused("v3-bad", "doesn't match", manifest={**MANIFEST, "version": "3.0.0"}, listing=wrong)
        check(run("load", "tactical").startswith("OK tactical 2.0.0"), "a failed update keeps 2.0.0")
        out = run("install", z("v4", manifest={**MANIFEST, "version": "4.0.0"}, files=files_plus(**{"assets/big.bin": os.urandom(2 << 20)})), 50)
        check(out.startswith("CANCELLED"), "cancelled half way: " + out)
        check(run("load", "tactical").startswith("OK tactical 2.0.0") and leftovers() == [], "a cancel keeps 2.0.0 and leaves nothing")

        # ---- installed mods ----
        check(run("install", z("other", manifest={**MANIFEST, "id": "a-mod", "kind": "overlay"})).startswith("OK a-mod"), "a second mod")
        (mods / "Not-An-Id").mkdir()
        (mods / "loose-file").write_bytes(b"x")
        check(run("list") == "a-mod tactical", "list: sorted ids, folders only: " + run("list"))
        os.remove(mods / "tactical/scripts/main.lua")
        out = run("load", "tactical")
        check("missing scripts/main.lua; install it again" in out, "load sees a missing file: " + out)
        run("install", zips / "good.zip")
        renamed = (mods / "tactical/mod.json").read_text().replace('"tactical"', '"other"')
        (mods / "tactical/mod.json").write_text(renamed)
        check("calls itself other" in run("load", "tactical"), "load sees a renamed mod")
        run("install", zips / "good.zip")
        check(run("remove", "a-mod") == "OK" and run("list") == "tactical", "remove")
        check("isn't installed" in run("remove", "a-mod"), "remove twice")
        check("isn't a mod's id" in run("load", "../save"), "load refuses a path")

        # a crash's leftovers are tidied at boot
        (mods / ".x.unpacking/assets").mkdir(parents=True)
        (mods / ".y.removing").mkdir()
        shutil.copytree(mods / "tactical", mods / ".tactical.backup")
        shutil.rmtree(mods / "tactical")
        (mods / ".a-mod.backup").mkdir()
        (mods / "a-mod").mkdir()
        run("boot", "-", 0)
        check(leftovers() == [] and run("list") == "a-mod tactical", "tidy: " + str(leftovers()) + " " + run("list"))
        check(run("load", "tactical").startswith("OK tactical"), "an interrupted update gets its old version back")
        shutil.rmtree(mods / "a-mod")

        # ---- the mod a machine plays before the game starts (the lobby's content check) ----
        check(run("play", "nope") == "play=- - -", "play: a choice not installed")
        listed1 = make_zip(zips / "play1.zip")
        run("install", zips / "play1.zip")
        same_size = {**MANIFEST, "version": "1.0.1"}   # files.sha256 keeps its size: only a digit changes
        listed2 = make_zip(zips / "play2.zip", manifest=same_size)
        check(len(listed1) == len(listed2), "the two builds' lists are the same size")
        out = run("play", "tactical", zips / "play2.zip").splitlines()
        check(out == ["play=tactical 1.0.0 " + hashlib.sha256(listed1).hexdigest(),
                      "play=tactical 1.0.1 " + hashlib.sha256(listed2).hexdigest()],
              "play: the choice, then an update read at once: " + str(out))
        check(run("install", z("play-overlay", manifest={**MANIFEST, "id": "looks", "kind": "overlay"})).startswith("OK looks"),
              "an overlay installs")
        check(run("play", "looks") == "play=- - -", "play: an overlay is no campaign")
        run("install", zips / "good.zip")

        # ---- the run's mod campaign ----
        out = run("boot", "tactical", 0)
        check("campaign=1 active=tactical setting=tactical" in out, "a mod campaign picks its cartridge: " + out)
        check("save=$S/eeprom-mod-tactical.bin" in out, "its own save")
        check("tag=mod-tactical ge-tag=ge" in out, "its own ghost tag")
        check(out.endswith("note="), "no note")
        out = run("boot", "-", 1)
        check("campaign=1 active=- setting=-" in out and "save=$S/eeprom-gf64.bin" in out and "tag=gf " in out, "no mod: " + out)
        out = run("boot", "-", 0)
        check("save=$S/eeprom.bin" in out and "tag=ge ge-tag=ge" in out, "GoldenEye: " + out)
        out = run("boot", "nope", 2)
        check("campaign=2 active=- setting=-" in out and "note=The mod campaign did not start: nope isn't installed." in out, "missing mod: " + out)
        out = run("boot", "../evil", 0)
        check("active=- setting=-" in out and "isn't a mod's id" in out, "a path as the mod: " + out)
        run("install", z("overlay", manifest={**MANIFEST, "id": "looks", "kind": "overlay"}))
        out = run("boot", "looks", 0)
        check("active=-" in out and "note=Tactical is an overlay, not a campaign" in out, "an overlay as a campaign: " + out)
        # a mod made for one play mode: off in the other, with the launcher's note (cloud sync can name it)
        run("install", z("vr-only", manifest={**MANIFEST, "id": "vr-only", "title": "Hands On", "playModes": ["vr"]}))
        run("install", z("flat-only", manifest={**MANIFEST, "id": "flat-only", "title": "Desk Job", "playModes": ["flat"]}))
        out = run("boot", "vr-only", 0)
        check("campaign=1 active=vr-only setting=vr-only" in out and out.endswith("note="), "a VR mod starts in VR: " + out)
        out = run("boot", "vr-only", 0, env={"PLAY_MODE": "flat"})
        check("campaign=0 active=- setting=-" in out and "save=$S/eeprom.bin" in out
              and "note=Hands On did not start: This mod is for VR." in out, "a VR mod on the flat screen: " + out)
        out = run("boot", "flat-only", 0)
        check("active=- setting=-" in out and "note=Desk Job did not start: This mod is for flat screen." in out,
              "a flat mod in VR: " + out)
        out = run("boot", "flat-only", 0, env={"PLAY_MODE": "flat"})
        check("active=flat-only" in out and out.endswith("note="), "a flat mod on the flat screen: " + out)
        out = run("boot", "tactical", 0, env={"PLAY_MODE": "flat"})
        check("active=tactical" in out and out.endswith("note="), "a mod without playModes starts flat too: " + out)
        out = run("boot", "tactical", 0, "Goldfinger 64")
        check("campaign=0 active=- setting=-" in out and "save=$S/eeprom.bin" in out
              and "note=Tactical needs Goldfinger 64, which did not load, so GoldenEye started instead." in out, "cartridge failed: " + out)

    if FAILS:
        print(f"{len(FAILS)} check(s) failed")
        sys.exit(1)
    print("PASS: mod packs: manifest, install checks and atomic updates, list/remove/tidy, the run's mod campaign")


if __name__ == "__main__":
    main()
