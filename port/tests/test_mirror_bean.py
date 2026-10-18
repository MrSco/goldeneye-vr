"""Bean and HD packs in Wreck's Mirror Mode (port/src/gevr_mirror.c).

Compiles the production helpers, and gevr_bean.c's own fit transform, and
checks: a mirrored mesh is GoldenEye's about x = 0 and still faces out with
every corner's attributes; a flipped fit is the retail fit with x negated and
wound the other way; the lists name what Wreck changed and nothing he left;
a turned-over picture's columns reverse; only GoldenEye and Mirror Mode keep
the retail art Bean and HD packs need.
"""
from pathlib import Path
import re
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
bean = (ROOT / "port/src/gevr_bean.c").read_text(encoding="utf-8", errors="replace")


def block(signature):
    start = bean.index(signature)
    end = bean.index("{", start) + 1
    depth = 1
    while depth:
        depth += (bean[end] == "{") - (bean[end] == "}")
        end += 1
    return bean[start:end]


struct = re.search(r"struct beanFit \{.*?\};", bean, re.S).group()
fixture = r'''
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "gevr_mirror.h"
#include "gevr_romlayout.h"
#include "gevr_bean_int.h"
static int campaign;
int gevrRomCampaign(void) { return campaign; }
#include "MIRROR_C"
STRUCT
FITMIRRORS
FITPOINT

static void cross(const float *p, float *n)
{
    float a[3], b[3];
    for (int k = 0; k < 3; k++) { a[k] = p[3 + k] - p[k]; b[k] = p[6 + k] - p[k]; }
    n[0] = a[1] * b[2] - a[2] * b[1]; n[1] = a[2] * b[0] - a[0] * b[2]; n[2] = a[0] * b[1] - a[1] * b[0];
}

int main(void)
{
    /* the campaigns: retail art in GoldenEye and Mirror Mode only */
    assert(gevrCampaignRetailArt(GEVR_CAMPAIGN_GE) && gevrCampaignRetailArt(GEVR_CAMPAIGN_MIRROR));
    assert(!gevrCampaignRetailArt(GEVR_CAMPAIGN_GF64) && !gevrCampaignRetailArt(GEVR_CAMPAIGN_TND64));
    campaign = GEVR_CAMPAIGN_GE; assert(!gevrRomMirrored() && gevrRomRetailArt());
    campaign = GEVR_CAMPAIGN_MIRROR; assert(gevrRomMirrored() && gevrRomRetailArt());
    campaign = GEVR_CAMPAIGN_TND64; assert(!gevrRomMirrored() && !gevrRomRetailArt());

    /* the lists: Wreck's flipped objects and stage pictures, not the characters, guns or HUD */
    assert(GEVR_MIRROR_PROPS == 140 && GEVR_MIRROR_IMAGES == 116);
    assert(gevrMirrorPropFlipped("Pdesk1Z") && gevrMirrorPropFlipped("PcarbmwZ") && gevrMirrorPropFlipped("Pammo_crate1Z"));
    assert(!gevrMirrorPropFlipped("CdjbondZ") && !gevrMirrorPropFlipped("Gak47Z") && !gevrMirrorPropFlipped("Pak47magZ"));
    assert(!gevrMirrorPropFlipped(NULL) && !gevrMirrorPropFlipped("Pdesk1"));
    assert(gevrMirrorImageFlipped(2585) && gevrMirrorImageFlipped(2695));   /* Dam's stage picture, "where" */
    assert(!gevrMirrorImageFlipped(2236) && !gevrMirrorImageFlipped(2231)); /* the sight, the 9mm icon */

    /* a mesh about x = 0: positions, normals, binds; corners keep their own attributes */
    struct gevrBeanTri tri;
    memset(&tri, 0, sizeof(tri));
    const float p[9] = { 1, 2, 3,  5, 2, 3,  1, 7, 3 };
    memcpy(tri.p, p, sizeof(p));
    for (int c = 0; c < 3; c++) {
        tri.n[c * 3] = 0.6f; tri.n[c * 3 + 2] = 0.8f;
        tri.uv[c * 2] = (float)c; tri.uv[c * 2 + 1] = 10.0f + c;
        tri.rgba[c * 4] = (uint8_t)(100 + c); tri.bone[c * 4] = (uint8_t)c; tri.weight[c * 4] = (uint8_t)(50 + c);
    }
    struct gevrBeanTriSrc src = { { 7, 8, 9 }, 0, 0 };
    struct gevrBeanMesh mesh;
    memset(&mesh, 0, sizeof(mesh));
    mesh.tris = &tri; mesh.ntris = 1; mesh.triSrc = &src;
    mesh.boneBind[3][0] = 4; mesh.bind[2][0] = -6;
    float before[3], after[3];
    cross(tri.p, before);
    gevrBeanMeshMirrorX(&mesh);
    const int from[3] = { 0, 2, 1 };   /* the corner each now holds */
    for (int c = 0; c < 3; c++) {
        const int o = from[c];
        assert(tri.p[c * 3] == -p[o * 3] && tri.p[c * 3 + 1] == p[o * 3 + 1] && tri.p[c * 3 + 2] == p[o * 3 + 2]);
        assert(tri.n[c * 3] == -0.6f && tri.n[c * 3 + 2] == 0.8f);
        assert(tri.uv[c * 2] == (float)o && tri.uv[c * 2 + 1] == 10.0f + o);
        assert(tri.rgba[c * 4] == 100 + o && tri.bone[c * 4] == o && tri.weight[c * 4] == 50 + o);
        assert(src.vi[c] == (uint32_t)(7 + o));
    }
    cross(tri.p, after);
    /* still facing out: the mirrored face's normal is the old one with x negated */
    assert(fabsf(after[0] + before[0]) < 1e-4f && fabsf(after[1] - before[1]) < 1e-4f && fabsf(after[2] - before[2]) < 1e-4f);
    assert(mesh.boneBind[3][0] == -4 && mesh.bind[2][0] == 6);

    /* a flipped fit (gevr_bean.c gevrBeanModelApply): the retail fit with x negated, wound the other way */
    const struct beanFit fits[2] = {
        { "Pdesk1Z", "prop/desk1", { 0, 1, 2 }, { 1, 1, 1 }, 6.9984f, { 0, 0, 0 }, { -1020.48f, -466.56f, 699.96f } },
        { "PswapZ", "prop/swap", { 2, 0, 1 }, { -1, 1, -1 }, 2.5f, { 3, -2, 1 }, { 40, 5, -7 } },
    };
    for (int f = 0; f < 2; f++) {
        struct beanFit flipped = fits[f];
        flipped.sign[0] = -flipped.sign[0];
        flipped.bc[0] = -flipped.bc[0];
        flipped.nc[0] = -flipped.nc[0];
        const float s[3] = { 11.5f, -3.25f, 8.0f };
        float a[3], b[3];
        fitPoint(&fits[f], s, a);
        fitPoint(&flipped, s, b);
        assert(fabsf(b[0] + a[0]) < 1e-3f && fabsf(b[1] - a[1]) < 1e-3f && fabsf(b[2] - a[2]) < 1e-3f);
        assert(fitMirrors(&flipped) != fitMirrors(&fits[f]));
    }

    /* a turned-over picture */
    unsigned char rgba[3 * 2 * 4];
    for (int i = 0; i < 24; i++) rgba[i] = (unsigned char)i;
    gevrMirrorRgbaX(rgba, 3, 2);
    assert(rgba[0] == 8 && rgba[4] == 4 && rgba[8] == 0 && rgba[12] == 20 && rgba[20] == 12);
    puts("PASS: Mirror Mode for Bean: rooms about x = 0 facing out with their corners' attributes, flipped fits, "
         "Wreck's lists, turned-over pictures and the retail-art campaigns");
    return 0;
}
'''
fixture = (fixture.replace("MIRROR_C", (ROOT / "port/src/gevr_mirror.c").as_posix()).replace("STRUCT", struct)
           .replace("FITMIRRORS", block("static int fitMirrors(const struct beanFit *fit)"))
           .replace("FITPOINT", block("static void fitPoint(const struct beanFit *fit, const float *src, float *out)")))

with tempfile.TemporaryDirectory(prefix="gevr-mirror-bean-") as temp:
    temp = Path(temp)
    source = temp / "mirror_bean.c"
    source.write_text(fixture)
    exe = temp / "mirror_bean.exe"
    gcc = shutil.which("gcc") or "gcc"
    subprocess.run([gcc, "-std=c11", "-O2", "-I" + str(ROOT / "port/include"), "-I" + str(ROOT / "include"),
                    "-I" + str(ROOT / "port/src"), str(source), "-lm", "-o", str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
