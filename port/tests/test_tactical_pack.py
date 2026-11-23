"""Exercise Tactical readiness and retail layout against the native code."""

import os
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
FIXTURE = r'''
#include "gevr_romlayout.h"
#include "gevr_tactical_pack.h"
#include "fs.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>

static const char *relative(const char *path)
{
    const char prefix[] = "$B/texture-packs/tactical/";
    assert(!strncmp(path, prefix, sizeof(prefix) - 1));
    return path + sizeof(prefix) - 1;
}

FILE *fsFileOpenRead(const char *path) { return fopen(relative(path), "rb"); }
s32 fsFileSize(const char *path)
{
    FILE *file = fsFileOpenRead(path);
    if (!file) return -1;
    fseek(file, 0, SEEK_END);
    const s32 size = ftell(file);
    fclose(file);
    return size;
}

int main(int argc, char **argv)
{
    assert(argc == 2);
    struct gevrTacticalPackInfo pack = {0};
    const int installed = atoi(argv[1]);
    assert(gevrTacticalPackInstalled(&pack) == installed);
    if (installed) {
        assert(!strcmp(pack.version, "0.16-draft.1"));
        assert(strlen(pack.contentId) == 64);
    }
    assert(gevrRomCampaign() == GEVR_CAMPAIGN_GE);
    const struct gevrRomLayout *retail = gevrRomActiveLayout();
    gevrRomCampaignSet(GEVR_CAMPAIGN_TACTICAL);
    assert(gevrRomCampaign() == GEVR_CAMPAIGN_TACTICAL);
    assert(gevrRomActiveLayout() == retail && !gevrRomIsVariant() && gevrRomRetailArt());
    gevrRomCampaignSet(GEVR_CAMPAIGN_MIRROR);
    assert(gevrRomIsVariant() && gevrRomRetailArt());
    assert(gevrCampaignCoop(GEVR_CAMPAIGN_GE));
    assert(!gevrCampaignCoop(GEVR_CAMPAIGN_TACTICAL));
    return 0;
}
'''

with tempfile.TemporaryDirectory(prefix="gevr-tactical-pack-") as temp:
    folder = Path(temp)
    main = folder / "pack.c"
    main.write_text(FIXTURE, encoding="utf-8")
    output = folder / "test.exe"
    cc = os.environ.get("CC") or shutil.which("clang") or "gcc"
    subprocess.run([cc, "-std=c11", "-D_LANGUAGE_C", "-DPLATFORM_64BIT",
                    "-I", str(ROOT / "port/include"), "-I", str(ROOT / "include"),
                    str(main), str(ROOT / "port/src/gevr_tactical_pack.c"),
                    str(ROOT / "port/src/gevr_romlayout.c"), "-o", str(output)], check=True)
    subprocess.run([str(output), "0"], cwd=folder, check=True)
    gear = folder / "mods/tactical/gear"
    gear.mkdir(parents=True)
    manifest = folder / "tactical.manifest"
    good = b"GEVRTACTICAL1\n0.16-draft.1\n" + b"a" * 64 + b"\n"
    manifest.write_bytes(good)
    subprocess.run([str(output), "0"], cwd=folder, check=True)
    for path in (folder / "files.sha256", gear / "catalog.txt", gear / "presets.txt", gear / "stats.txt"):
        path.write_text("fixture\n", encoding="ascii")
    for invalid in (good.replace(b"GEVRTACTICAL1", b"GEVRTACTICAL2"),
                    good.replace(b"0.16-draft.1", b"1" * 33),
                    good.replace(b"a" * 64, b"z" * 64), good + b"trailing\n"):
        manifest.write_bytes(invalid)
        subprocess.run([str(output), "0"], cwd=folder, check=True)
    for valid in (good, good.replace(b"\n", b"\r\n")):
        manifest.write_bytes(valid)
        subprocess.run([str(output), "1"], cwd=folder, check=True)

print("PASS: Tactical pack readiness, version and retail ROM layout")
