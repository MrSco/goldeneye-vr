"""Compile Jedi & Ninja core, settings table rules, and INI persistence."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import settings_stub_dir

ROOT = Path(__file__).resolve().parents[2]
CC = shutil.which("gcc") or "gcc"
CXX = shutil.which("g++") or "g++"

with tempfile.TemporaryDirectory(prefix="gevr-jedi-ninja-") as temp:
    temp_dir = Path(temp)
    exe = temp_dir / "jedi_ninja.exe"
    subprocess.run([
        CC, "-std=c11", "-O2", "-Wall", "-Werror",
        "-I" + str(ROOT / "port/include"),
        str(ROOT / "port/tests/jedi_ninja_native.c"),
        str(ROOT / "port/src/gevr_jedi_ninja.c"),
        "-lm", "-o", str(exe),
    ], check=True)
    subprocess.run([str(exe)], check=True)

    # Verify compatibility when GoldenEye overrides math_atan2f/asinf/acosf.
    game = [str(ROOT / "src/game" / name) for name in ("math_atan2f.c", "math_asinfacosf.c", "math_asinacos.c")]
    exe = temp_dir / "jedi_ninja_game_math.exe"
    subprocess.run([
        CC, "-std=c11", "-O2", "-D_LANGUAGE_C", "-Wno-incompatible-pointer-types",
        "-I" + str(ROOT / "port/include"),
        "-I" + str(ROOT / "include"),
        "-I" + str(ROOT / "src"),
        "-I" + str(ROOT / "src/game"),
        str(ROOT / "port/tests/jedi_ninja_native.c"),
        str(ROOT / "port/src/gevr_jedi_ninja.c"),
        *game,
        "-lm", "-o", str(exe),
    ], check=True)
    subprocess.run([str(exe)], check=True)

    # Verify settings table defaults, Jedi<->Ninja mutual exclusivity, and Reset all settings.
    stub_dir = temp_dir / "settings"
    stub_dir.mkdir()
    table = settings_stub_dir.make(stub_dir, ROOT)
    mutex_c = stub_dir / "jedi_ninja_settings_test.c"
    mutex_c.write_text(r'''
#include "gevr_settings.h"
#include "settings_table_stubs.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static int find(const char *label) {
    GevrSettingInfo info;
    for (int i = 0; i < gevrSettingCount(); i++) {
        if (gevrSettingInfo(i, &info) && !strcmp(info.label, label)) return i;
    }
    return -1;
}

int main(void) {
    gevrSettingsReadBuiltIn();
    assert(VrJediNinjaMode == 0);
    assert(VrJediBlasters == 1);
    assert(VrJediPickupGuns == 0);
    assert(VrNinjaPickupGuns == 0);

    const int jedi = find("Jedi mode");
    const int ninja = find("Ninja mode");
    const int blasters = find("Blasters");
    const int jPickup = find("Pick up guns");
    assert(jedi >= 0 && ninja >= 0 && blasters >= 0 && jPickup >= 0);

    gevrSettingSet(jedi, 1);
    assert(VrJediNinjaMode == 1 && gevrSettingIndex(jedi) == 1 && gevrSettingIndex(ninja) == 0);
    assert(VrRagdolls == 1 && VrDismemberment == 1 && VrBodiesStay == 24);

    gevrSettingSet(ninja, 1);
    assert(VrJediNinjaMode == 2 && gevrSettingIndex(jedi) == 0 && gevrSettingIndex(ninja) == 1);

    gevrSettingSet(blasters, 0);
    gevrSettingSet(jPickup, 1);
    gevrSettingResetAll();
    assert(VrJediNinjaMode == 0 && gevrSettingIndex(jedi) == 0 && gevrSettingIndex(ninja) == 0);
    assert(VrJediBlasters == 1 && VrJediPickupGuns == 0 && VrNinjaPickupGuns == 0);
    puts("jedi & ninja settings: defaults, mutual exclusivity and reset passed");
    return 0;
}
''')
    mutex_exe = stub_dir / "jedi_ninja_settings_test.exe"
    subprocess.run([
        CC, "-std=gnu11", "-O1", "-Wall", "-Werror=implicit-function-declaration",
        "-I" + str(stub_dir),
        "-I" + str(ROOT / "port/include"),
        "-I" + str(ROOT / "port/tests"),
        str(table), str(mutex_c), "-lm", "-o", str(mutex_exe),
    ], check=True)
    subprocess.run([str(mutex_exe)], check=True)

    # Verify goldeneye-vr.ini round-trip & clamping from vr_settings.cpp.
    vr_settings = (ROOT / "port/vr/vr_settings.cpp").read_text()
    save_start = vr_settings.index('    iniPrintf(f, "JediNinjaMode=%d')
    save_block = vr_settings[save_start:vr_settings.index('    iniPrintf(f, "Ragdolls=%d', save_start)].replace("iniPrintf(", "fprintf(")
    load_start = vr_settings.index('            else if (strcmp(key, "JediNinjaMode")')
    load_block = vr_settings[load_start:vr_settings.index('            else if (strcmp(key, "Ragdolls")', load_start)].replace("else if", "if", 1)
    ini_cpp = temp_dir / "jedi_ninja_ini_test.cpp"
    ini_cpp.write_text(f'''
#include <cassert>
#include <cstdio>
#include <cstring>

int VrJediNinjaMode, VrJediHand, VrSaberColor, VrSaberStyle, VrNinjaSword, VrNinjaBoomerang;
int VrJediForcePowers = 1, VrJediCatchProjectiles = 1, VrJnPerfectParry = 1, VrJnBladeTrails = 1, VrJnAudio = 1;
int VrNinjaShield = 1, VrNinjaRicochet = 1, VrNinjaShadowStep = 1, VrNinjaBulletSlice = 1;
int VrSithNpc, VrSithInNormalMode, VrSithScale, VrNinjaNpc, VrNinjaInNormalMode;
int VrAllyJedi, VrControllerWheel;
int VrJediBlasters = 1, VrJediPickupGuns, VrNinjaPickupGuns;

static void saveJn(FILE *f) {{
{save_block}
}}

static void loadJn(const char *line) {{
    char key[64];
    int ival;
    if (sscanf(line, "%63[^=]=%d", key, &ival) == 2) {{
{load_block}
    }}
}}

int main() {{
    VrJediNinjaMode = 2; VrJediHand = 1; VrSaberColor = 6; VrSaberStyle = 2;
    VrNinjaSword = 5; VrNinjaBoomerang = 3;
    VrJediForcePowers = 0; VrJediCatchProjectiles = 0; VrJnPerfectParry = 0;
    VrJnBladeTrails = 0; VrJnAudio = 0;
    VrNinjaShield = 0; VrNinjaRicochet = 0; VrNinjaShadowStep = 0; VrNinjaBulletSlice = 0;
    VrSithNpc = 2; VrSithInNormalMode = 1; VrSithScale = 3;
    VrNinjaNpc = 2; VrNinjaInNormalMode = 1;
    VrAllyJedi = 1; VrControllerWheel = 1;
    VrJediBlasters = 0; VrJediPickupGuns = 1; VrNinjaPickupGuns = 1;

    FILE *f = tmpfile();
    assert(f);
    saveJn(f);
    rewind(f);

    VrJediNinjaMode = VrJediHand = VrSaberColor = VrSaberStyle = VrNinjaSword = VrNinjaBoomerang = 0;
    VrJediForcePowers = VrJediCatchProjectiles = VrJnPerfectParry = VrJnBladeTrails = VrJnAudio = 1;
    VrNinjaShield = VrNinjaRicochet = VrNinjaShadowStep = VrNinjaBulletSlice = 1;
    VrSithNpc = VrSithInNormalMode = VrSithScale = VrNinjaNpc = VrNinjaInNormalMode = 0;
    VrAllyJedi = VrControllerWheel = VrJediPickupGuns = VrNinjaPickupGuns = 0;
    VrJediBlasters = 1;

    char line[256];
    while (fgets(line, sizeof(line), f)) loadJn(line);
    fclose(f);

    assert(VrJediNinjaMode == 2 && VrJediHand == 1 && VrSaberColor == 6 && VrSaberStyle == 2);
    assert(VrNinjaSword == 5 && VrNinjaBoomerang == 3);
    assert(!VrJediForcePowers && !VrJediCatchProjectiles && !VrJnPerfectParry && !VrJnBladeTrails && !VrJnAudio);
    assert(!VrNinjaShield && !VrNinjaRicochet && !VrNinjaShadowStep && !VrNinjaBulletSlice);
    assert(VrSithNpc == 2 && VrSithInNormalMode == 1 && VrSithScale == 3);
    assert(VrNinjaNpc == 2 && VrNinjaInNormalMode == 1);
    assert(VrAllyJedi == 1 && VrControllerWheel == 1);
    assert(VrJediBlasters == 0 && VrJediPickupGuns == 1 && VrNinjaPickupGuns == 1);

    loadJn("JediNinjaMode=99"); loadJn("SaberColor=-5"); loadJn("SaberStyle=9");
    loadJn("NinjaSword=42"); loadJn("NinjaBoomerang=-1"); loadJn("SithScale=9");
    assert(VrJediNinjaMode == 2 && VrSaberColor == 0 && VrSaberStyle == 2);
    assert(VrNinjaSword == 5 && VrNinjaBoomerang == 0 && VrSithScale == 3);
    puts("jedi & ninja ini: round-trip and clamping passed");
    return 0;
}}
''')
    ini_exe = temp_dir / "jedi_ninja_ini_test.exe"
    subprocess.run([CXX, "-std=c++17", "-O2", str(ini_cpp), "-o", str(ini_exe)], check=True)
    subprocess.run([str(ini_exe)], check=True)
