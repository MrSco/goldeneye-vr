"""A ROM hack's copies of the hand models use the hand patches (#9).

Mario Characters keeps GoldenEye's pistol and knife hands in a file per
character (Gwppk1Z, Gwppk2Z, Gwppk3Z for GwppkZ), the same vertices with the
skin in other texture numbers in the same slots. This compiles the production
lookup and per-slot texture choice from gevr_handpatch.c against a fixture:
the copies find GoldenEye's patch (and a ported patch serves its siblings),
unrelated names do not, and a group's texture follows its slot.
"""
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
src = (ROOT / "port/src/gevr_handpatch.c").read_text(encoding="utf-8")
lookup = src[src.index("static s32 hpCopyStem("):src.index("s32 gevrHandPatchWants(")]
texture = src[src.index("/* A patch group's texture as this model numbers it"):src.index("void gevrHandPatchApply(")]

FIXTURE = r"""
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
typedef int s32; typedef unsigned int u32; typedef unsigned short u16;
#define TRUE 1
#define FALSE 0
struct gevrHpModel { const char *name; u16 firstPart, numParts; u16 firstTex, numTex; };
static const u16 g_gevrHpTextures[] = { 0x648, 0x702, 0x703, 0x701 };
static const struct gevrHpModel g_gevrHpModels[] = {
    { "Gtt33Z", 0, 1, 0, 0 }, { "GtaserZ", 0, 1, 0, 0 }, { "GwppkZ", 0, 1, 0, 4 }, { "Gtaser1Z", 0, 1, 0, 0 },
};
static const s32 g_gevrHpNumModels = 4;
static u32 s_tex[64];
static s32 s_numTex;
/* hpMatches is not under test */
#define hpMatches(file, m) 1

@@LOOKUP@@
@@TEXTURE@@

static const char *first(const char *name)
{
    const struct gevrHpModel *c[HP_MAX_CANDIDATES];
    return hpCandidates(name, c) ? c[0]->name : "";
}

int main(void)
{
    const struct gevrHpModel *c[HP_MAX_CANDIDATES];

    assert(strcmp(first("GwppkZ"), "GwppkZ") == 0);     /* GoldenEye's own */
    assert(strcmp(first("Gwppk2Z"), "GwppkZ") == 0);    /* a hack's copy */
    assert(strcmp(first("Gtt332Z"), "Gtt33Z") == 0);    /* a name with its own digits */
    assert(hpCandidates("Gtt33Z", c) == 1);
    assert(hpCandidates("GmagnumZ", c) == 0);
    assert(hpCandidates("Gwppksil2Z", c) == 0);         /* not GwppkZ's */
    /* a ported patch serves its siblings, after its own model's */
    assert(hpCandidates("Gtaser3Z", c) == 2 && strcmp(c[0]->name, "GtaserZ") == 0 && strcmp(c[1]->name, "Gtaser1Z") == 0);
    assert(hpCandidates("Gtaser1Z", c) == 2 && strcmp(c[0]->name, "Gtaser1Z") == 0);

    /* the texture in the same slot: Luigi's hand numbers GoldenEye's 0x702 as 0x6f6 */
    s_tex[0] = 0x648; s_tex[1] = 0x6f6; s_tex[2] = 0x6f7; s_tex[3] = 0x6f5; s_numTex = 4;
    assert(hpTexture(&g_gevrHpModels[2], 0x702) == 0x6f6 && hpTexture(&g_gevrHpModels[2], 0x701) == 0x6f5);
    assert(hpTexture(&g_gevrHpModels[2], 0x999) == 0x999);
    s_numTex = 3;   /* another table: the patch's own numbers */
    assert(hpTexture(&g_gevrHpModels[2], 0x702) == 0x702);
    assert(hpTexture(&g_gevrHpModels[0], 0x702) == 0x702);   /* no table known */
    puts("PASS: a hack's renamed hand copies find the hand patch and draw its groups in the textures of the same slots");
    return 0;
}
"""


def main():
    cc = shutil.which("gcc") or shutil.which("cc") or shutil.which("clang")
    with tempfile.TemporaryDirectory() as tmp:
        c, exe = Path(tmp) / "copies.c", Path(tmp) / "copies.exe"
        c.write_text(FIXTURE.replace("@@LOOKUP@@", lookup).replace("@@TEXTURE@@", texture), encoding="utf-8")
        subprocess.run([cc, "-std=c11", "-Wall", "-Werror", "-Wno-unused-function", str(c), "-o", str(exe)], check=True)
        print(subprocess.run([str(exe)], check=True, capture_output=True, text=True).stdout.strip())


if __name__ == "__main__":
    main()
