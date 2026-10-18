"""Exercise Mirror Mode's production import, loader, banks, save and watch clock.

The cartridge bodies are synthetic. No ROM or fan assets are needed. The
variant binder is stubbed so both its successful and failed load paths can be
exercised; these checks do not establish compatibility of a real patched ROM.
"""
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]


def function(source, signature):
    start = source.index(signature)
    opening = source.index("{", start)
    end, depth = opening + 1, 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]


launcher = (ROOT / "port/vr/vr_launcher.cpp").read_text()
loader = (ROOT / "port/src/gevr_romload.c").read_text()
view = (ROOT / "src/game/bondview2.c").read_text()
save = (ROOT / "port/src/libultra.c").read_text()
clock_start = view.index("    // VERSION_US\n", view.index("watch_time_0 += jpD_800484D0;"))
clock_end = view.index("\n#endif", view.index("watch_time_0 += speedgraphframes;", clock_start))
fixture = r'''
#include "gevr_romlayout.h"
#include "gevr_romcheck.h"
#include "gevr_rom_segments.h"
#include "gevr_rom_manifest.h"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <sys/stat.h>
using u8 = unsigned char; using u32 = unsigned; using s32 = int; using f32 = float;
#define sysLogPrintf(...) ((void)0)
#define GEVR_ROM_SIZE 12582912
#define GEVR_ROM_ID "NGEE"
#define GEVR_ROM_TITLE "GOLDENEYE"
static std::string dataDir() { return "."; }
static void vr_log(const char*, ...) {}
static u8 *g_RomFile;
static u32 g_RomFileSize;
static int VrCampaign, VrGexGuns, bankCalls, applyCalls, retailBinds;
static bool bindOk;
static void *fsFileLoad(const char *name, u32 *size) {
    FILE *f = fopen(name, "rb"); if (!f) return nullptr;
    fseek(f, 0, SEEK_END); *size = (u32)ftell(f); rewind(f);
    void *p = malloc(*size); assert(p); assert(fread(p, 1, *size, f) == *size); fclose(f); return p;
}
static FILE *fsFileOpenWrite(const char *path) { return fopen(path, "wb"); }
extern "C" int gevrRomApplyVariant(const u8*, u32) { ++applyCalls; return bindOk; }
void gevrRomBindSegments() { ++bankCalls; }
static void gevrRomBindFileTable() { ++retailBinds; }
static int gevrRomCheckFiles(void*, unsigned, char*, size_t) { return 1; }
static const struct gevrRomFile g_GevrRomSegments[GEVR_SEG_COUNT] = {
    {}, {}, {}, {0, 26608, 0}
};
/* IMPORT */
/* ROM_NAMES */
/* LOAD */
/* SEGMENTS */
#define FS_MAXPATH 256
#define EEPROM_PATH "$S/eeprom.bin"
static char eepromPath[FS_MAXPATH];
static const char *sysArgGetString(const char*) { return nullptr; }
static bool fsPathIsAbsolute(const char*) { return false; }
static bool fsPathIsCwdRelative(const char*) { return false; }
/* SAVE */
static s32 watch_time_0, speedgraphframes;
static void tick() {
/* CLOCK */
}
struct Mtxf { float m[4][4]; };
struct Node { float *Data; };
static Node nodes[3];
static struct { Node *Switches[3] = { &nodes[0], &nodes[1], &nodes[2] }; } s_gevrWatchHeader;
static float angles[3]; static int angleCount;
#define M_TAU_F 6.28318530717958647692f
static void matrix_4x4_set_position_and_rotation_around_y(float*, float a, Mtxf*) { angles[angleCount++] = a; }
static void gevrMtxMul(Mtxf*, Mtxf*, Mtxf*) {}
/* HANDS */
static void rom(const char *path, bool mirror, int order=0) {
    u8 *data = (u8*)calloc(1, GEVR_ROM_SIZE); assert(data);
    memcpy(data, "\x80\x37\x12\x40", 4);
    memcpy(data + 0x20, "GOLDENEYE", 9); memcpy(data + 0x3b, "NGEE", 4);
    memcpy(data + 0x10, mirror ? "\x94\xbd\x9a\x93\x4f\x82\x1a\xc8" : "\xdc\xbc\x50\xd1\x09\xfd\x1a\xa3", 8);
    int stride = order == 1 ? 2 : order == 2 ? 4 : 1;
    for (int i=0; i<GEVR_ROM_SIZE; i+=stride) for(int j=0; j<stride/2; j++)
        std::swap(data[i+j], data[i+stride-1-j]);
    FILE *f=fopen(path,"wb"); assert(f); assert(fwrite(data,1,GEVR_ROM_SIZE,f)==GEVR_ROM_SIZE); fclose(f); free(data);
}
static void reset() {
    free(g_RomFile); g_RomFile=nullptr; g_RomFileSize=0;
    VrCampaign=GEVR_CAMPAIGN_MIRROR; VrGexGuns=1; gevrGexCampaignOff=0;
    bankCalls=applyCalls=retailBinds=0;
    remove(GEVR_CAMPAIGN_FAILED_FILE);
}
int main() {
    rom("ge.z64", false);
    for (int order=0; order<3; order++) {
        rom("picked-mirror.z64", true, order);
        std::string message;
        assert(adoptHackFile("picked-mirror.z64", GEVR_ROM_MIRROR_FILE, "mirror", "ok", "bad", message));
        assert(message=="ok" && hackHeaderOk(GEVR_ROM_MIRROR_FILE,"mirror"));
        assert(hackHeaderOk("ge.z64","ge"));
        assert(access("picked-mirror.z64",0)!=0);
        reset(); bindOk=true; assert(romdataInit()==0);
        assert(gevrRomCampaign()==GEVR_CAMPAIGN_MIRROR && VrCampaign==GEVR_CAMPAIGN_MIRROR);
        assert(applyCalls==1 && bankCalls==1 && retailBinds==0);
        assert(!VrGexGuns && gevrGexCampaignOff);
        osEepromSetPath(); assert(!strcmp(eepromPath,"$S/eeprom-mirror.bin"));
    }
    std::string note;
    rom("picked-mirror.z64", false);
    assert(!adoptHackFile("picked-mirror.z64",GEVR_ROM_MIRROR_FILE,"mirror","ok","bad",note));
    assert(note=="bad" && hackHeaderOk(GEVR_ROM_MIRROR_FILE,"mirror"));
    assert(hackHeaderOk("ge.z64","ge"));
    reset(); bindOk=false; assert(romdataInit()==0);  // binder rejects damaged/incompatible data
    assert(VrCampaign==GEVR_CAMPAIGN_GE && gevrRomCampaign()==GEVR_CAMPAIGN_GE);
    assert(applyCalls==1 && retailBinds==1 && VrGexGuns==1 && !gevrGexCampaignOff);
    osEepromSetPath(); assert(!strcmp(eepromPath,EEPROM_PATH));
    assert(!access(GEVR_CAMPAIGN_FAILED_FILE,0));
    reset(); remove(GEVR_ROM_MIRROR_FILE); assert(romdataInit()==0); // missing cartridge
    assert(VrCampaign==0 && applyCalls==0 && retailBinds==1);
    reset(); rom(GEVR_ROM_MIRROR_FILE, false); assert(romdataInit()==0); // retail renamed as mirror
    assert(VrCampaign==0 && applyCalls==0 && retailBinds==1);
    reset(); rom("goldfinger64.z64",true); rom("tnd64.z64",true);
    assert(romdataInit()==0 && VrCampaign==0); // never substitute the other hack's filename
    gevrRomCampaignSet(GEVR_CAMPAIGN_MIRROR);
    u32 off, size;
    const u32 cases[][3] = {
        {GEVR_SEG_ANIMATION_DATA,0x28e980,0xe7d0},
        {GEVR_SEG_GLOBALIMAGETABLE,0x29d150,0xac8},
        {GEVR_SEG_RAREWARELOGO,0x29e550,26608},
        {GEVR_SEG_FONTBANKGOTHIC,0x2e63e0,0x24b0},
        {GEVR_SEG_FONTZURICHBOLD,0x2e8890,0x3540},
        {GEVR_SEG_GUNBARREL,0x2a4d40,107890},
    };
    for (auto &c: cases) { assert(gevrVariantSeg(c[0],&off,&size)); assert(off==c[1] && size==c[2]); }
    gevrRomSetImageBankBytes(1024);
    assert(gevrVariantSeg(GEVR_SEG_IMAGES,&off,&size) && off==0x8eda20 && size==1024);
    Mtxf matrices[4]{};
    for (int campaign=0; campaign<GEVR_CAMPAIGN_COUNT; campaign++) for (int mirrored=0; mirrored<2; mirrored++) {
        gevrRomCampaignSet(campaign); watch_time_0=0;
        int elapsed=0;
        for (int frames: {1,60,3601}) {
            speedgraphframes=frames; tick(); elapsed+=frames;
            assert(watch_time_0==(campaign==GEVR_CAMPAIGN_MIRROR ? -elapsed : elapsed));
            angleCount=0; gevrWatchArmHands(matrices,mirrored); assert(angleCount==3);
            float actual[3]; memcpy(actual,angles,sizeof(actual));
            watch_time_0=-watch_time_0;
            angleCount=0; gevrWatchArmHands(matrices,mirrored);
            for (int i=0;i<3;i++) {
                assert(fabs(sin(actual[i])+sin(angles[i]))<1e-5);
                assert(fabs(cos(actual[i])-cos(angles[i]))<1e-5);
            }
            watch_time_0=-watch_time_0;
        }
    }
    free(g_RomFile);
    puts("PASS: Mirror import byte orders/rejection, selected-file loading/fallback, banks, separate saves and reversed watch clock");
}
'''
fixture = fixture.replace("#include <sys/stat.h>", "#include <sys/stat.h>\n#include <unistd.h>")
fixture = fixture.replace("/* IMPORT */", "\n".join(function(launcher, sig) for sig in (
    "static bool hackHeaderOk(", "static bool adoptHackFile(")))
fixture = fixture.replace("/* ROM_NAMES */", loader[loader.index("static const char *const gevrRomNames[]"):loader.index("static s32 gevrRomValidate(")])
fixture = fixture.replace("/* LOAD */", "\n".join((
    function(loader, "static s32 gevrRomValidate("),
    loader[loader.index("static const char *const gevrGoldfingerNames[]"):loader.index("s32 romdataInit(")],
    function(loader, "s32 romdataInit("))))
fixture = fixture.replace("/* SEGMENTS */", function(loader, "static int gevrVariantSeg("))
fixture = fixture.replace("/* SAVE */", function(save, "static inline void osEepromSetPath("))
fixture = fixture.replace("/* CLOCK */", view[clock_start:clock_end])
fixture = fixture.replace("/* HANDS */", function(view, "static void gevrWatchArmHands("))
with tempfile.TemporaryDirectory(prefix="gevr-mirror-") as temp:
    temp = Path(temp)
    layout = temp / "layout.o"
    subprocess.run([shutil.which("gcc") or "cc", "-std=c11", "-O2",
                    "-I" + str(ROOT / "port/include"), "-c", str(ROOT / "port/src/gevr_romlayout.c"),
                    "-o", str(layout)], check=True)
    # The loader fixture replaces asset validation, which is covered by test_romcheck.
    # Only retain the real byte-order decoder from gevr_romcheck.c.
    check = temp / "check.c"
    check.write_text('#include <string.h>\n' + function((ROOT / "port/src/gevr_romcheck.c").read_text(), "int gevrRomNormaliseOrder("))
    obj = temp / "order.o"
    subprocess.run([shutil.which("gcc") or "cc", "-O2", "-c", str(check), "-o", str(obj)], check=True)
    cpp, exe = temp / "mirror.cpp", temp / "mirror"
    cpp.write_text(fixture)
    subprocess.run([shutil.which("g++") or "c++", "-std=c++17", "-O2", "-DGEVR=1",
                    "-I" + str(ROOT / "port/include"), str(cpp), str(layout), str(obj), "-lm", "-o", str(exe)], check=True)
    subprocess.run([str(exe)], cwd=temp, check=True)
