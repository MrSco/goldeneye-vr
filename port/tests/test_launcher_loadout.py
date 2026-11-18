"""The START button's loadout follows the chosen campaign.

START names what the game starts with: "START  (Bean, GE-X, Oddjob's hat, 3
cheats)". A hack campaign turns GoldenEye X off, and Bean, HD packs and
Oddjob's hat are fitted to GoldenEye's own models, which only Mirror Mode
keeps. Mario Characters' START still listed all three while the line under it
said they were off (headset, 2026-11-18). This compiles the production
gevrLauncherLoadout from vr_launcher.cpp against stubs and checks each campaign.
"""
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
source = (ROOT / "port/vr/vr_launcher.cpp").read_text(encoding="utf-8")
start = source.index("static void gevrLauncherLoadout(char *buf, size_t size)")
end = source.index('extern "C" void gevrLauncherRun(void)', start)
production = source[start:end]

fixture = r'''
#include "gevr_romlayout.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>

static int VrCampaign, VrGunSizeCheat, VrUnlockAll, VrGexGuns, VrOddjobHat;
static unsigned long long VrCheatMask;
static char g_ActiveExtTexPack[256];
static int bean;
static int gevrBeanIsEnabled(void) { return bean; }   /* the last run's campaign: may still say yes */
static int gevrHatAvailable(void) { return 1; }

/* PRODUCTION */

static std::string label(void)
{
    char buf[160];
    gevrLauncherLoadout(buf, sizeof(buf));
    return buf;
}

int main()
{
    bean = 1; VrGexGuns = 1; VrOddjobHat = 1; VrCheatMask = 0x6; VrGunSizeCheat = 1;
    VrCampaign = GEVR_CAMPAIGN_GE;
    assert(label() == "START  (Bean, GE-X, Oddjob's hat, 3 cheats)###start");
    /* Mirror Mode keeps GoldenEye's models (Bean flips them); GE-X and the cheat list are off */
    VrCampaign = GEVR_CAMPAIGN_MIRROR;
    assert(label() == "START  (Bean, Oddjob's hat, 1 cheat)###start");
    /* their own models: no Bean, HD, GE-X or hat; the gun size still applies */
    for (int c : {GEVR_CAMPAIGN_GF64, GEVR_CAMPAIGN_TND64, GEVR_CAMPAIGN_MARIO}) {
        VrCampaign = c;
        assert(label() == "START  (1 cheat)###start");
    }
    /* an HD pack instead of Bean */
    bean = 0; std::strcpy(g_ActiveExtTexPack, "ge007-hd");
    VrCampaign = GEVR_CAMPAIGN_MARIO;
    VrGunSizeCheat = 0;
    assert(label() == "START###start");
    VrCampaign = GEVR_CAMPAIGN_GE;
    assert(label() == "START  (HD, GE-X, Oddjob's hat, 2 cheats)###start");
    std::puts("PASS: START lists only what the chosen campaign loads (Bean, HD, GE-X and the hat off in hacks with their own models)");
    return 0;
}
'''.replace("/* PRODUCTION */", production)

with tempfile.TemporaryDirectory(prefix="gevr-loadout-") as temp:
    cpp, exe = Path(temp) / "loadout.cpp", Path(temp) / "loadout.exe"
    cpp.write_text(fixture, encoding="utf-8")
    subprocess.run([shutil.which("g++") or "g++", "-std=c++17", "-O1", "-Wall", "-I" + str(ROOT / "port/include"),
                    str(cpp), "-o", str(exe)], check=True)
    out = subprocess.run([str(exe)], cwd=temp, check=True, capture_output=True, text=True).stdout
    print(out.strip())
