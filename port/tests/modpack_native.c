/*
 * The mod packs' production code (port/src/gevr_modpack.c, gevr_sha256.c,
 * miniz) behind a small command line, so test_modpack.py can build zips
 * with Python's zipfile and check each rule. The save path
 * (libultra.c osEepromSetPath) and the ghost tag (gevr_ghost.c
 * ghostCampaignTag) are pasted in by the test at SAVE and GHOST_TAG.
 *
 * Run from the test's folder: $S is ./save, so mods land in
 * ./save/texture-packs/mods.
 */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fs.h"
#include "gevr_modpack.h"
#include "gevr_romlayout.h"
#include "gevr_sha256.h"

int VrCampaign;
char VrCampaignMod[GEVR_MOD_ID_MAX + 1];

const char *fsFullPath(const char *relPath)
{
    static char buf[FS_MAXPATH + 1];

    if (relPath[0] == '$' && relPath[1] == 'S') {
        snprintf(buf, sizeof(buf), "save%s", relPath + 2);
        return buf;
    }
    return relPath;
}

FILE *fsFileOpenWrite(const char *name)
{
    return fopen(fsFullPath(name), "wb");
}

s32 fsPathIsAbsolute(const char *path)
{
    return path[0] == '/';
}

s32 fsPathIsCwdRelative(const char *path)
{
    return path[0] == '.';
}

void sysLogPrintf(s32 level, const char *fmt, ...)
{
    va_list ap;

    (void)level;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
}

static const char *sysArgGetString(const char *name)
{
    (void)name;
    return NULL;
}

int gevrRomIsVariant(void)
{
    return VrCampaign != GEVR_CAMPAIGN_GE;
}

const struct gevrRomLayout *gevrRomActiveLayout(void)
{
    static const char *const tags[] = { "ge", "gf64", "tnd64", "mirror", "mario" };
    static struct gevrRomLayout layout;

    layout.tag = tags[VrCampaign];
    return &layout;
}

#define EEPROM_PATH "$S/eeprom.bin"
static char eepromPath[FS_MAXPATH];
/* SAVE */
/* GHOST_TAG */

static int cancelAt = 101;

static int progress(int percent, void *ud)
{
    (void)ud;
    return percent < cancelAt;
}

static void hex(const uint8_t sum[32], char out[65])
{
    gevrSha256Hex(sum, out);
}

static char *slurp(const char *path, size_t *len)
{
    FILE *f = fopen(path, "rb");
    char *data;
    long n;

    if (!f) {
        return NULL;
    }
    fseek(f, 0, SEEK_END);
    n = ftell(f);
    fseek(f, 0, SEEK_SET);
    data = (char *)malloc((size_t)n + 1);
    if (fread(data, 1, (size_t)n, f) != (size_t)n) {
        n = 0;
    }
    fclose(f);
    *len = (size_t)n;
    return data;
}

static void printInfo(const gevrModInfo *info)
{
    char sum[65];

    hex(info->content, sum);
    printf("OK %s %s %s\n", info->m.id, info->m.version, sum);
}

static void printNote(void)
{
    size_t len;
    char *text = slurp(GEVR_CAMPAIGN_FAILED_FILE, &len);

    if (text) {
        printf("note=%.*s\n", (int)len, text);
        free(text);
        remove(GEVR_CAMPAIGN_FAILED_FILE);
    } else {
        printf("note=\n");
    }
}

static void printRun(void)
{
    const gevrModInfo *mod = gevrModActive();

    osEepromSetPath();
    printf("campaign=%d active=%s setting=%s\n", VrCampaign, mod ? mod->m.id : "-", VrCampaignMod[0] ? VrCampaignMod : "-");
    printf("save=%s\n", eepromPath);
    printf("tag=%s ge-tag=%s\n", ghostCampaignTag(VrCampaign), ghostCampaignTag(GEVR_CAMPAIGN_GE));
}

int main(int argc, char **argv)
{
    char why[300] = "";
    const char *cmd = argc > 1 ? argv[1] : "";

    if (!strcmp(cmd, "sha") && argc > 2) {
        /* fed in uneven pieces, so every block boundary is crossed */
        static const size_t pieces[] = { 1, 3, 63, 64, 65, 127, 1000 };
        size_t len, at = 0, i = 0;
        char *data = slurp(argv[2], &len);
        gevrSha256 s;
        uint8_t sum[32];
        char out[65];

        gevrSha256Init(&s);
        while (at < len) {
            size_t n = pieces[i++ % 7];

            if (n > len - at) n = len - at;
            gevrSha256Update(&s, data + at, n);
            at += n;
        }
        gevrSha256Final(&s, sum);
        hex(sum, out);
        printf("%s\n", out);
        free(data);
        return 0;
    }
    if (!strcmp(cmd, "manifest") && argc > 2) {
        size_t len;
        char *text = slurp(argv[2], &len);
        gevrModManifest m;

        if (gevrModParseManifest(text, len, &m, why, sizeof(why)) != 0) {
            printf("FAIL %s\n", why);
        } else {
            printf("OK id=%s title=%s by=%s version=%s api=%d kind=%d base=%d coop=%d affects=%d modes=%u site=%s\n",
                   m.id, m.title, m.by, m.version, m.api, m.kind, m.base, m.coop, m.affectsPlay, m.modes, m.site);
        }
        free(text);
        return 0;
    }
    if (!strcmp(cmd, "install") && argc > 2) {
        gevrModInfo info;
        int rc;

        cancelAt = argc > 3 ? atoi(argv[3]) : 101;
        rc = gevrModInstallZip(argv[2], &info, progress, NULL, why, sizeof(why));
        if (rc == 0) printInfo(&info);
        else printf("%s %s\n", rc == -2 ? "CANCELLED" : "FAIL", why);
        return 0;
    }
    if (!strcmp(cmd, "load") && argc > 2) {
        gevrModInfo info;

        if (gevrModLoad(argv[2], &info, why, sizeof(why)) == 0) printInfo(&info);
        else printf("FAIL %s\n", why);
        return 0;
    }
    if (!strcmp(cmd, "remove") && argc > 2) {
        if (gevrModRemove(argv[2], why, sizeof(why)) == 0) printf("OK\n");
        else printf("FAIL %s\n", why);
        return 0;
    }
    if (!strcmp(cmd, "list")) {
        char ids[16][GEVR_MOD_ID_MAX + 1];
        const int n = gevrModList(ids, 16);
        int i;

        for (i = 0; i < n; i++) printf("%s%s", i ? " " : "", ids[i]);
        printf("\n");
        return 0;
    }
    if (!strcmp(cmd, "boot") && argc > 3) {
        /* boot <CampaignMod or -> <Campaign> [basefail] */
        strncpy(VrCampaignMod, strcmp(argv[2], "-") ? argv[2] : "", GEVR_MOD_ID_MAX);
        VrCampaignMod[GEVR_MOD_ID_MAX] = '\0';
        VrCampaign = atoi(argv[3]);
        gevrModBoot();
        if (argc > 4) {
            gevrModBaseFailed(argv[4]);
            VrCampaign = GEVR_CAMPAIGN_GE;   /* romdataInit's fallback */
        }
        printRun();
        printNote();
        return 0;
    }
    fprintf(stderr, "usage: sha|manifest|install|load|remove|list|boot\n");
    return 2;
}
