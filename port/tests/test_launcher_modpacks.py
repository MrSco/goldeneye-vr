"""The launcher's mod packs (docs/mod-system.md, plan step 2b), cut from vr_launcher.cpp.

Drives gevrLauncherModsTick, the Mods page row's state and the campaign list's
mods against the real installer (port/src/gevr_modpack.c, gevr_sha256.c,
miniz), with the PC's picker stood in for: a pick handed over as an open
file, a cancel, a failure, a cancelled install, a temporary copy deleted
after, removal, and a mod campaign staying on its own cartridge.
Run: python port/tests/test_launcher_modpacks.py
"""
import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from test_modpack import MANIFEST, FILES, make_zip, sha256_list  # noqa: E402

ROOT = Path(__file__).resolve().parents[2]


def balanced_end(text, opening):
    end, depth = opening + 1, 1
    while depth:
        depth += (text[end] == "{") - (text[end] == "}")
        end += 1
    return end


source = (ROOT / "port/vr/vr_launcher.cpp").read_bytes().decode().replace("\r\n", "\n")
state = source[source.index("// Installed campaign mods, listed after the hacks"):source.index("static int gevrLauncherStartRow(")]
start = source.index("/*\n * Mod packs (docs/mod-system.md; the installer is port/src/gevr_modpack.c).")
online = source.index("static bool gevrLauncherCampaignOnline()", start)
machinery = source[start:balanced_end(source, source.index("{", online))]

fixture = r'''
#include <atomic>
#include <cassert>
#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include "gevr_modpack.h"
#include "gevr_romlayout.h"

extern "C" {
int VrCampaign;
char VrCampaignMod[GEVR_MOD_ID_MAX + 1];
const char *fsFullPath(const char *rel)
{
    static char buf[1024];
    if (rel[0] == '$' && rel[1] == 'S') { snprintf(buf, sizeof(buf), "save%s", rel + 2); return buf; }
    return rel;
}
FILE *fsFileOpenWrite(const char *name) { return fopen(fsFullPath(name), "wb"); }
void sysLogPrintf(int, const char *, ...) {}
void gevrLauncherModPicked(FILE *f, const char *removeAfter);
}

static bool s_campaignInstalled[GEVR_CAMPAIGN_COUNT] = { true, true, true, true, true };
static const char *gevrCampaignTitle(int c)
{
    static const char *const names[] = { "GoldenEye", "Goldfinger 64", "Tomorrow Never Dies", "Mirror Mode", "Mario Characters" };
    return c >= 0 && c < GEVR_CAMPAIGN_COUNT ? names[c] : "another campaign";
}
static int saves;
static void vrSettingsSave() { saves++; }
static void vr_log(const char *, ...) {}
static std::string pickResult;
static std::string gevrTakePickResult() { std::string r; r.swap(pickResult); return r; }
/* the PC's picker (pc_launcher.cpp): returns with the file handed over, or nothing for a cancel */
static std::string nextPick;
static bool nextPickIsCopy;
static bool gevrOpenPicker(const char *method)
{
    assert(!strcmp(method, "openModPicker"));
    if (!nextPick.empty()) gevrLauncherModPicked(fopen(nextPick.c_str(), "rb"), nextPickIsCopy ? nextPick.c_str() : nullptr);
    return true;
}

/* STATE */
/* MACHINERY */

static void settle()
{
    gevrLauncherModsTick();
    for (int i = 0; i < 20000 && s_modsView.busy; i++) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        gevrLauncherModsTick();
    }
    assert(!s_modsView.busy);
}

static void pick(const char *path, bool copy = false)
{
    nextPick = path ? path : "";
    nextPickIsCopy = copy;
    s_modsView.wantPick = true;
    settle();
}

/* a failed check says what the row said */
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "check failed at line %d: %s\n  row message: %s\n", __LINE__, #c, s_modsView.message.c_str()); return 1; } } while (0)

static bool exists(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (f) fclose(f);
    return f != nullptr;
}

int main(int argc, char **argv)
{
    CHECK(argc == 5);
    const char *good = argv[1], *bad = argv[2], *copy = argv[3], *big = argv[4];

    gevrLauncherModsTick();
    CHECK(s_modsView.mods.empty() && s_modCampaigns.empty());

    /* a cancelled pick: no note left behind */
    pick(nullptr);
    CHECK(s_modsView.message.empty() && !s_modsView.busy);

    /* a campaign mod installs and is listed beside START */
    pick(good);
    CHECK(s_modsView.message == "Installed Tactical 1.0.0. Choose it beside START." && !s_modsView.bad);
    CHECK(s_modsView.mods.size() == 1 && s_modsView.mods[0].campaign && s_modCampaigns.size() == 1);
    CHECK(s_modCampaigns[0].base == GEVR_CAMPAIGN_GF64 && s_modCampaigns[0].reason.empty());

    /* its cartridge's ROM decides whether it can start */
    VrCampaign = GEVR_CAMPAIGN_GF64;
    strcpy(VrCampaignMod, "tactical");
    /* online when its manifest offers co-op (the lobby's content check holds the party to it) */
    CHECK(gevrLauncherModReady() && s_modCampaigns[0].coop && gevrLauncherCampaignOnline());
    s_modCampaigns[0].coop = false;
    CHECK(!gevrLauncherCampaignOnline());
    s_modCampaigns[0].coop = true;
    s_campaignInstalled[GEVR_CAMPAIGN_GF64] = false;
    gevrLauncherModsTick();
    CHECK(s_modCampaigns[0].reason == "needs Goldfinger 64" && s_modsView.mods[0].reason == "needs Goldfinger 64");
    CHECK(!gevrLauncherModReady() && strcmp(VrCampaignMod, "tactical") == 0);
    s_campaignInstalled[GEVR_CAMPAIGN_GF64] = true;
    gevrLauncherModsTick();
    CHECK(gevrLauncherModReady());

    /* another cartridge chosen (a hack's ROM adopted, a listed game joined): the mod is off */
    const int before = saves;
    VrCampaign = GEVR_CAMPAIGN_GE;
    gevrLauncherModsTick();
    CHECK(!VrCampaignMod[0] && saves == before + 1 && gevrLauncherCampaignOnline());

    /* a zip that fails a check: said, and nothing changes */
    pick(bad);
    CHECK(s_modsView.bad && s_modsView.message.find("Not installed: ") == 0);
    CHECK(s_modsView.message.find("doesn't match its checksum") != std::string::npos);
    CHECK(s_modsView.mods.size() == 1);

    /* an Apple picker's own copy goes once installed */
    pick(copy, true);
    CHECK(!s_modsView.bad && !exists(copy));

    /* a cancelled install: nothing changes, and START waited for it */
    nextPick = big;
    nextPickIsCopy = false;
    s_modsView.wantPick = true;
    gevrLauncherModsTick();
    CHECK(s_modsView.busy && !gevrLauncherModReady());
    s_modsView.wantCancel = true;
    settle();
    CHECK(s_modsView.message == "Install cancelled. Nothing changed." && s_modsView.mods.size() == 1);
    CHECK(s_modsView.mods[0].version == "1.0.0");

    /* removing the chosen mod: its cartridge's own campaign stays */
    VrCampaign = GEVR_CAMPAIGN_GF64;
    strcpy(VrCampaignMod, "tactical");
    gevrLauncherModsTick();
    CHECK(VrCampaignMod[0]);
    s_modsView.wantRemove = "tactical";
    gevrLauncherModsTick();
    CHECK(s_modsView.message == "Removed tactical." && !VrCampaignMod[0] && VrCampaign == GEVR_CAMPAIGN_GF64);
    CHECK(s_modsView.mods.empty() && s_modCampaigns.empty());

    /* a chosen mod that isn't installed (the ini names one since removed) */
    strcpy(VrCampaignMod, "ghost");
    gevrLauncherModsTick();
    CHECK(!VrCampaignMod[0] && s_modsView.bad && s_modsView.message == "ghost is no longer installed, or can't start.");

    puts("PASS: launcher mod packs: picks handed over, installs on their thread, cancel, failures, copies deleted, "
         "removal, a mod campaign on its own cartridge, online when it offers co-op");
    return 0;
}
'''.replace("/* STATE */", state).replace("/* MACHINERY */", machinery)


def main():
    with tempfile.TemporaryDirectory(prefix="gevr-launcher-mods-") as temp:
        temp = Path(temp)
        cc, cxx = shutil.which("gcc") or "cc", shutil.which("g++") or "c++"
        # the sanitizers where the compiler has them (not Windows' MinGW gcc): run in WSL for them
        sanitize = [] if os.name == "nt" else ["-fsanitize=address,undefined", "-fno-omit-frame-pointer"]
        objects = []
        for src in ("port/src/gevr_modpack.c", "port/src/gevr_sha256.c", "port/vr/miniz/miniz.c"):
            obj = temp / (Path(src).stem + ".o")
            subprocess.run([cc, "-std=gnu11", "-O1", "-D_LANGUAGE_C=1", "-DGEVR=1", *sanitize, "-I" + str(ROOT / "include"),
                            "-I" + str(ROOT / "port/include"), "-c", str(ROOT / src), "-o", str(obj)], check=True)
            objects.append(str(obj))
        cpp = temp / "launcher_mods.cpp"
        cpp.write_text(fixture)
        exe = temp / ("launcher_mods.exe" if os.name == "nt" else "launcher_mods")
        subprocess.run([cxx, "-std=c++17", "-O1", *sanitize, "-I" + str(ROOT / "include"), "-I" + str(ROOT / "port/include"),
                        str(cpp), *objects, "-lpthread", "-o", str(exe)], check=True)

        run = temp / "run"
        (run / "save").mkdir(parents=True)
        good, bad, copy, big = (temp / n for n in ("good.zip", "bad.zip", "copy.zip", "big.zip"))
        make_zip(good)
        mod_json = __import__("json").dumps(MANIFEST, indent=2).encode()
        wrong = sha256_list({**FILES, "mod.json": mod_json}).replace(
            __import__("hashlib").sha256(FILES["scripts/main.lua"]).hexdigest().encode(), b"0" * 64)
        make_zip(bad, listing=wrong)
        make_zip(copy)
        make_zip(big, manifest={**MANIFEST, "version": "2.0.0"}, files={**FILES, "assets/big.bin": os.urandom(24 << 20)})
        out = subprocess.run([str(exe), str(good), str(bad), str(copy), str(big)], cwd=run, capture_output=True, text=True)
        print(out.stdout.strip() or out.stderr.strip())
        if out.returncode != 0:
            sys.exit(out.returncode or 1)


if __name__ == "__main__":
    main()
