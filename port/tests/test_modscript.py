"""Mods' scripts (docs/mod-system.md, plan step 3): Lua 5.4 in its sandbox,
the lifecycle events, errors, runaway calls, the memory cap, the budget,
mod.save, mod.setting, mod.random and mod.capabilities.

Builds modscript_native.c around the production port/src/gevr_modscript.c
and port/external/lua, then runs each scenario's commands against a mod
written here. Run: python port/tests/test_modscript.py
"""
import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
LUA = ["lapi", "lcode", "lctype", "ldebug", "ldo", "ldump", "lfunc", "lgc", "llex", "lmem", "lobject", "lopcodes",
       "lparser", "lstate", "lstring", "ltable", "ltm", "lundump", "lvm", "lzio", "lauxlib", "lbaselib", "lcorolib",
       "lmathlib", "lstrlib", "ltablib", "lutf8lib"]
FAILS = []


def check(cond, what):
    if not cond:
        FAILS.append(what)
        print("FAIL:", what)


def build(temp):
    cc = shutil.which("gcc") or "cc"
    sanitize = [] if os.name == "nt" else ["-fsanitize=address,undefined", "-fno-omit-frame-pointer"]
    exe = temp / ("modscript_test.exe" if os.name == "nt" else "modscript_test")
    # the game's own flags on its C (CMakeLists.txt add_compile_options)
    flags = ["-std=gnu11", "-O1", "-funsigned-char", "-fwrapv", "-fno-strict-aliasing", "-D_LANGUAGE_C=1", "-DGEVR=1"]
    # Lua as the build makes it (gevr_lua): without the game's include/, whose
    # N64 stdlib.h and stddef.h aren't the C library's it is written for
    objects = []
    for name in LUA:
        obj = temp / (name + ".o")
        subprocess.run([cc, *flags, *sanitize, "-c", str(ROOT / "port/external/lua" / (name + ".c")), "-o", str(obj)],
                       check=True)
        objects.append(str(obj))
    subprocess.run([cc, *flags, *sanitize, "-I" + str(ROOT / "include"), "-I" + str(ROOT / "port/include"),
                    str(ROOT / "port/tests/modscript_native.c"), str(ROOT / "port/src/gevr_modscript.c"),
                    *objects, "-lm", "-o", str(exe)], check=True)
    return exe


class Scenario:
    def __init__(self, exe, temp, name, scripts, data=None):
        self.exe = exe
        self.dir = temp / name
        for rel, text in scripts.items():
            path = self.dir / "mods/tactical/scripts" / rel
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(text.encode())
        (self.dir / "mods/tactical").mkdir(parents=True, exist_ok=True)
        (self.dir / "save").mkdir(parents=True, exist_ok=True)
        for rel, text in (data or {}).items():
            path = self.dir / "save/mod-data/tactical" / rel
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(text.encode())

    def run(self, *commands):
        out = subprocess.run([str(self.exe), "tactical", "Tactical", *map(str, commands)], cwd=self.dir,
                             capture_output=True, text=True, timeout=300)
        assert out.returncode == 0, out.stderr
        return out.stdout

    def logs(self, out, prefix="mod tactical: "):
        return [line.split(" ", 2)[2][len(prefix):] for line in out.splitlines()
                if line.startswith("LOG ") and line.split(" ", 2)[2].startswith(prefix)]


def main():
    with tempfile.TemporaryDirectory(prefix="gevr-modscript-") as temp:
        temp = Path(temp)
        exe = build(temp)

        # ---- the lifecycle: each event once, in order, with its arguments ----
        s = Scenario(exe, temp, "lifecycle", {"main.lua": """
mod.on("campaign_enter", function() print("campaign_enter", mod.id, mod.title, mod.version, mod.api) end)
mod.on("mission_prepare", function(stage) print("mission_prepare", stage) end)
mod.on("mission_start", function(stage) print("mission_start", stage) end)
mod.on("tick", function(dt) print(string.format("tick %.4f", dt)) end)
mod.on("mission_end", function(stage, how) print("mission_end", stage, how) end)
"""})
        out = s.run("start", "prepare", 9, "begin", 9, "frames", 2, 2, 1, "frames", 1, 60, 1, "complete", "title",
                    "prepare", 10, "begin", 10, "title", "running")
        check(s.logs(out) == ["campaign_enter tactical Tactical 1.0.0 1", "mission_prepare 9", "mission_start 9",
                              "tick 0.0333", "tick 0.0333", "tick 0.2500", "mission_end 9 complete",
                              "mission_prepare 10", "mission_start 10", "mission_end 10 left"],
              "lifecycle: " + str(s.logs(out)))
        check("RUNNING 1" in out, "running after the missions")
        out = s.run("start", "frames", 3, 1, 1)
        check(not any(l.startswith("tick") for l in s.logs(out)), "no tick outside a mission")

        # ---- the sandbox ----
        s = Scenario(exe, temp, "sandbox", {"main.lua": r"""
for _, name in ipairs({"io", "os", "package", "debug", "dofile", "loadfile", "collectgarbage"}) do
  assert(_G[name] == nil, name .. " is reachable")
end
assert(string.dump == nil and ("x").dump == nil, "string.dump")
assert(math.randomseed == nil, "math.randomseed")
assert(load("return 1 + 1")() == 2, "load text")
local f, err = load("return 1", "x", "b")
assert(f == nil and err:find("binary"), "load binary")
local env = { y = 5 }
assert(load("return y", "y", "t", env)() == 5, "load env")
local util = require("lib.util")
assert(util.answer == 42 and require("lib.util") == util, "require once")
assert(not pcall(require, "../evil"), "require outside")
assert(not pcall(require, "missing"), "require missing")
assert(not pcall(require, "lib..util"), "require dots")
local co = coroutine.wrap(function() coroutine.yield(1) return 2 end)
assert(co() == 1 and co() == 2, "coroutines")
assert(utf8.len("Bond") == 4 and table.concat({1, 2}, ",") == "1,2", "utf8 and table")
print("sandbox ok")
""", "lib/util.lua": "return { answer = 42 }\n"})
        out = s.run("start")
        check(s.logs(out) == ["sandbox ok"], "sandbox: " + out)

        # ---- an error in play: off for the mission, back for the next ----
        s = Scenario(exe, temp, "error", {"main.lua": """
local n = 0
mod.on("tick", function() n = n + 1; print("tick", n); if n == 2 then error("boom") end end)
"""})
        out = s.run("start", "prepare", 9, "begin", 9, "frames", 4, 1, 1, "note", "title", "prepare", 9, "begin", 9,
                    "frames", 1, 1, 1, "running")
        check(s.logs(out) == ["tick 1", "tick 2", "tick 3"], "error: off for the mission, then on: " + str(s.logs(out)))
        check("mods: tactical: tick: scripts/main.lua:3: boom|stack traceback:" in out, "error: logged with its name")
        check("NOTE Tactical stopped for this mission: a script error (see the log)." in out, "error: the note")
        check("RUNNING 1" in out, "error: still running")

        # ---- an error at start: off for the run, a data-only mod, an unknown event ----
        s = Scenario(exe, temp, "broken", {"main.lua": "error('no')\n"})
        out = s.run("start", "prepare", 9, "begin", 9, "frames", 2, 1, 1, "note", "running")
        check("mods: tactical: scripts/main.lua: scripts/main.lua:1: no" in out and "RUNNING 0" in out, "broken: " + out)
        check("NOTE Tactical's scripts didn't start (see the log); it plays without them." in out, "broken: the note")
        s = Scenario(exe, temp, "typo", {"main.lua": "mod.on('tik', function() end)\n"})
        out = s.run("start", "running")
        check("there is no event \"tik\"" in out and "RUNNING 0" in out, "an unknown event: " + out)
        s = Scenario(exe, temp, "data-only", {})
        out = s.run("start", "running")
        check("has no scripts/main.lua; nothing to run" in out and "RUNNING 0" in out, "data only: " + out)

        # ---- a runaway call, and memory past the cap ----
        s = Scenario(exe, temp, "runaway", {"main.lua": "mod.on('tick', function() while true do end end)\n"})
        out = s.run("start", "prepare", 9, "begin", 9, "frames", 1, 1, 1000, "note", "running")
        check("a call ran over 100 ms (an endless loop?)" in out and "stopped for this mission" in out, "runaway: " + out)
        s = Scenario(exe, temp, "memory", {"main.lua": """
mod.on('tick', function() local t = {} for i = 1, 1e9 do t[i] = ('x'):rep(1000) .. i end end)
"""})
        out = s.run("start", "prepare", 9, "begin", 9, "frames", 1, 1, 1, "note", "running")
        check("not enough memory" in out and "stopped for this mission" in out and "RUNNING 1" in out, "memory: " + out)

        # ---- the budget: under it nothing happens; over it for 3 seconds, a pause ----
        s = Scenario(exe, temp, "budget", {"main.lua": "mod.on('tick', function() end)\n"})
        # a frame reads the clock 4 times: at 50 us a read, 100 us of script and 200 us a frame (5000 a second)
        out = s.run("start", "prepare", 9, "begin", 9, "frames", 12000, 1, 50, "note")
        check("paused" not in out and "NOTE -" in out, "under budget: " + out[-300:])
        check("perf: mod tactical 0.100 ms a frame (all mods 0.100 of 0.25 ms)" in out, "the perf log's line")
        out = s.run("start", "prepare", 9, "begin", 9, "frames", 5000, 1, 300, "note")
        check("mods: tactical paused for this mission: 0.600 ms a frame for 3 seconds" in out, "over budget: " + out[-400:])
        check("NOTE Tactical paused for this mission: its scripts took too long." in out, "over budget: the note")
        pauses = out.count("mods: tactical paused for this mission:")
        check(pauses == 1, f"paused once, then nothing runs ({pauses})")

        # ---- mod.save, mod.setting ----
        s = Scenario(exe, temp, "save", {"main.lua": r"""
assert(mod.save.write("progress.txt", "level=3") == true)
assert(mod.save.read("progress.txt") == "level=3")
assert(mod.save.read("never") == nil)
assert(not pcall(mod.save.write, "../escape", "x"), "a path")
assert(not pcall(mod.save.write, ".hidden", "x"), "a dot")
assert(not pcall(mod.save.write, "settings.txt", "x"), "the player's settings")
local ok, err = mod.save.write("big.bin", ("x"):rep(1048577))
assert(ok == nil and err:find("1024 KB"), "over the cap")
assert(mod.save.write("gone.txt", "x") and mod.save.remove("gone.txt") and mod.save.read("gone.txt") == nil)
print(mod.setting("speed", 1), mod.setting("hard", false), mod.setting("name", "x"), mod.setting("missing", 7))
print("save ok")
"""}, data={"settings.txt": "speed=2.5\r\nhard=true\nname=bond\n"})
        out = s.run("start")
        check(s.logs(out) == ["2.5 true bond 7", "save ok"], "save and setting: " + out)
        check((s.dir / "save/mod-data/tactical/progress.txt").read_bytes() == b"level=3", "saved beside the install")
        check(not (s.dir / "save/mod-data/tactical/big.bin").exists(), "nothing written over the cap")

        # ---- mod.random: the mission's seed (in co-op the same on every headset); capabilities ----
        s = Scenario(exe, temp, "random", {"main.lua": """
mod.on("mission_prepare", function()
  local a = {}
  for i = 1, 5 do a[#a + 1] = mod.random(1000) end
  print(table.concat(a, ","))
  for i = 1, 200 do
    local r, k, j = mod.random(), mod.random(3), math.random(5, 6)
    assert(r >= 0 and r < 1 and k >= 1 and k <= 3 and (j == 5 or j == 6))
  end
  assert(not pcall(mod.random, 3, 1), "an empty interval")
  local c = mod.capabilities()
  print(c.vr, c.hands, c.headless, c.platform)
end)
"""})
        first = s.logs(s.run("online", 1, "start", "prepare", 9))
        second = s.logs(s.run("online", 1, "start", "prepare", 9))
        other = s.logs(s.run("online", 1, "start", "prepare", 10))
        check(first == second and first[0] != other[0], "random: one seed a mission online: " + str((first, other)))
        check(first[1] == "true true false pc", "capabilities: " + str(first))

        # ---- the log's rate ----
        s = Scenario(exe, temp, "chatty", {"main.lua": "for i = 1, 100 do print(i) end\n"})
        out = s.run("start")
        kept = [l for l in s.logs(out) if "left out" not in l]
        check(len(kept) == 30 and "(more lines this second left out)" in out, "log rate: " + str(len(kept)))

    if FAILS:
        print(f"{len(FAILS)} check(s) failed")
        sys.exit(1)
    print("PASS: mods' scripts: the sandbox, lifecycle, errors, runaway calls, the memory cap, the budget, "
          "mod.save, mod.setting, mod.random and mod.capabilities")


if __name__ == "__main__":
    main()
