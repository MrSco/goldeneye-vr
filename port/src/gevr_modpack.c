/*
 * Mod packs (docs/mod-system.md): mod.json, install from a zip, the
 * installed list, and the mod campaign this run plays.
 *
 * Install checks everything before it puts anything in place, as the
 * texture packs' installers do (ModManager.java, port/pc/pc_mods.cpp): the
 * zip unpacks into texture-packs/mods/.<id>.unpacking, each file checked
 * against files.sha256 as it is written, and only then replaces <id>. A
 * leftover from a crash is tidied at the next boot.
 */
#include "gevr_modpack.h"
#include "gevr_sha256.h"
#include "gevr_romlayout.h"
#include "fs.h"
#include "system.h"
#include "../vr/miniz/miniz.h"

#include <dirent.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <direct.h>
#else
#include <unistd.h>
#endif

extern int VrCampaign;
extern char VrCampaignMod[GEVR_MOD_ID_MAX + 1];

#define PATH_CAP (FS_MAXPATH * 2)

static char s_root[FS_MAXPATH + 1];
static gevrModInfo s_active;
static int s_hasActive;

static int say(char *why, size_t n, const char *fmt, ...)
{
    if (why && n) {
        va_list ap;

        va_start(ap, fmt);
        vsnprintf(why, n, fmt, ap);
        va_end(ap);
    }
    return -1;
}

/* A name from a zip or a file, fit to print: anything odd becomes '?'. */
static const char *printable(const char *s, char *buf, size_t cap)
{
    size_t i;

    for (i = 0; s[i] && i + 1 < cap; i++) {
        buf[i] = (s[i] >= 0x20 && s[i] < 0x7f) ? s[i] : '?';
    }
    buf[i] = '\0';
    return buf;
}

/* ---- files and folders ---- */

/* a + "/" + b into out: 0, or -1 when it doesn't fit (a path is never cut short) */
static int joinPath(char *out, size_t cap, const char *a, const char *b)
{
    const int n = snprintf(out, cap, "%s/%s", a, b);

    return n >= 0 && (size_t)n < cap ? 0 : -1;
}

static int isDir(const char *path)
{
    struct stat st;

    return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

static int isFile(const char *path)
{
    struct stat st;

    return stat(path, &st) == 0 && S_ISREG(st.st_mode);
}

static int makeDir(const char *path)
{
#ifdef _WIN32
    return _mkdir(path);
#else
    return mkdir(path, 0777);
#endif
}

/* Each folder of path that is missing. */
static int makeDirs(const char *path)
{
    char buf[PATH_CAP];
    size_t i, n = strlen(path);

    if (n == 0 || n >= sizeof(buf)) {
        return -1;
    }
    memcpy(buf, path, n + 1);
    for (i = 1; i <= n; i++) {
        if (buf[i] == '/' || buf[i] == '\\' || buf[i] == '\0') {
            const char c = buf[i];

            if (i == 2 && buf[1] == ':') {
                continue;   /* C: */
            }
            buf[i] = '\0';
            if (!isDir(buf) && makeDir(buf) != 0 && errno != EEXIST) {
                return -1;
            }
            buf[i] = c;
        }
    }
    return isDir(path) ? 0 : -1;
}

/* A file, or a folder and everything in it; a link is removed, never followed. */
static void removeTree(const char *path)
{
    struct stat st;
#ifdef _WIN32
    if (stat(path, &st) != 0) {
        return;
    }
#else
    if (lstat(path, &st) != 0) {
        return;
    }
#endif
    if (S_ISDIR(st.st_mode)) {
        DIR *d = opendir(path);

        if (d) {
            struct dirent *e;
            char child[PATH_CAP];

            while ((e = readdir(d)) != NULL) {
                if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) {
                    continue;
                }
                if (joinPath(child, sizeof(child), path, e->d_name) == 0) {
                    removeTree(child);
                }
            }
            closedir(d);
        }
        rmdir(path);
    } else {
        remove(path);
    }
}

/* A whole small file into memory (NUL after it), or NULL when missing or over max. */
static char *readSmall(const char *path, size_t max, size_t *len)
{
    FILE *f = fopen(path, "rb");
    char *data;
    long size;

    if (!f) {
        return NULL;
    }
    if (fseek(f, 0, SEEK_END) != 0 || (size = ftell(f)) < 0 || (size_t)size > max || fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        return NULL;
    }
    data = (char *)malloc((size_t)size + 1);
    if (data && fread(data, 1, (size_t)size, f) != (size_t)size) {
        free(data);
        data = NULL;
    }
    fclose(f);
    if (data) {
        data[size] = '\0';
        *len = (size_t)size;
    }
    return data;
}

const char *gevrModsRoot(void)
{
    if (!s_root[0]) {
        strncpy(s_root, fsFullPath("$S/texture-packs/mods"), FS_MAXPATH);
        s_root[FS_MAXPATH] = '\0';
    }
    return s_root;
}

/* ---- names ---- */

int gevrModIdValid(const char *id)
{
    size_t i, n;

    if (!id) {
        return 0;
    }
    n = strlen(id);
    if (n == 0 || n > GEVR_MOD_ID_MAX || id[0] == '-' || id[n - 1] == '-') {
        return 0;
    }
    for (i = 0; i < n; i++) {
        const char c = id[i];

        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-')) {
            return 0;
        }
    }
    return 1;
}

int gevrModBaseCampaign(const char *base)
{
    /* the manifest's words for the cartridges (docs/mod-system.md, Pack format) */
    static const char *const words[] = { "ge", "gf", "tnd", "mirror", "mario" };
    static const int campaigns[] = { GEVR_CAMPAIGN_GE, GEVR_CAMPAIGN_GF64, GEVR_CAMPAIGN_TND64,
                                     GEVR_CAMPAIGN_MIRROR, GEVR_CAMPAIGN_MARIO };
    typedef char wordForEveryCampaign[sizeof(words) / sizeof(words[0]) == GEVR_CAMPAIGN_COUNT ? 1 : -1];
    size_t i;

    (void)sizeof(wordForEveryCampaign);
    for (i = 0; base && i < sizeof(words) / sizeof(words[0]); i++) {
        if (!strcmp(base, words[i])) {
            return campaigns[i];
        }
    }
    return -1;
}

static int lowerChar(int c)
{
    return c >= 'A' && c <= 'Z' ? c + 32 : c;
}

static int compareFolded(const char *a, const char *b)
{
    for (;; a++, b++) {
        const int x = lowerChar((unsigned char)*a), y = lowerChar((unsigned char)*b);

        if (x != y || !x) {
            return x - y;
        }
    }
}

static int endsWithFolded(const char *s, const char *tail)
{
    const size_t n = strlen(s), t = strlen(tail);

    return n >= t && compareFolded(s + n - t, tail) == 0;
}

/*
 * A path inside the mod's folder: printable ASCII, '/' between parts, no
 * empty, "." or ".." part, and nothing Windows can't name (\ : * ? " < > |,
 * a part ending in a space or a dot, CON, NUL, COM1 and the like).
 */
static int pathOk(const char *rel)
{
    static const char *const devices[] = { "con", "prn", "aux", "nul", "com", "lpt" };
    const char *part = rel, *c;
    int depth = 0;

    if (!rel[0] || strlen(rel) > 200) {
        return 0;
    }
    for (c = rel;; c++) {
        if (*c == '/' || *c == '\0') {
            const size_t len = (size_t)(c - part);
            size_t stem = 0, i;

            if (len == 0 || (len == 1 && part[0] == '.') || (len == 2 && part[0] == '.' && part[1] == '.')
                || part[len - 1] == ' ' || part[len - 1] == '.' || ++depth > 16) {
                return 0;
            }
            while (stem < len && part[stem] != '.') {
                stem++;
            }
            for (i = 0; i < sizeof(devices) / sizeof(devices[0]); i++) {
                const int digit = i >= 4;   /* COM1..9, LPT1..9 */

                if (stem == 3u + (unsigned)digit && lowerChar(part[0]) == devices[i][0]
                    && lowerChar(part[1]) == devices[i][1] && lowerChar(part[2]) == devices[i][2]
                    && (!digit || (part[3] >= '1' && part[3] <= '9'))) {
                    return 0;
                }
            }
            if (*c == '\0') {
                return 1;
            }
            part = c + 1;
        } else if (*c < 0x20 || *c > 0x7e || strchr("\\:*?\"<>|", *c)) {
            return 0;
        }
    }
}

/* What a computer adds to a zip by itself (a Mac's __MACOSX/ and .DS_Store,
 * Windows' Thumbs.db and desktop.ini): left out, never listed. */
static int zipJunk(const char *name)
{
    const char *last = strrchr(name, '/');

    last = last ? last + 1 : name;
    return !strncmp(name, "__MACOSX/", 9) || !strcmp(last, ".DS_Store") || !compareFolded(last, "Thumbs.db")
        || !compareFolded(last, "desktop.ini");
}

static char *copyString(const char *s)
{
    const size_t n = strlen(s) + 1;
    char *copy = (char *)malloc(n);

    if (copy) {
        memcpy(copy, s, n);
    }
    return copy;
}

/* What a pack may hold (docs/mod-system.md, Pack format). */
static int layoutOk(const char *rel)
{
    if (!strcmp(rel, "mod.json") || !strcmp(rel, "files.sha256")) {
        return 1;
    }
    if (!strncmp(rel, "scripts/", 8)) {
        return endsWithFolded(rel, ".lua");
    }
    if (!strncmp(rel, "assets/", 7)) {
        return 1;
    }
    return !strchr(rel, '/') && (endsWithFolded(rel, ".md") || endsWithFolded(rel, ".txt"));
}

/* ---- mod.json: one JSON object ---- */

typedef struct Json {
    const char *p, *end;
    char *why;
    size_t whyLen;
    int failed;
} Json;

static int jFail(Json *j, const char *fmt, ...)
{
    if (!j->failed && j->why && j->whyLen) {
        char text[160];
        va_list ap;

        va_start(ap, fmt);
        vsnprintf(text, sizeof(text), fmt, ap);
        va_end(ap);
        snprintf(j->why, j->whyLen, "mod.json: %s", text);
    }
    j->failed = 1;
    return -1;
}

static void jSpace(Json *j)
{
    while (j->p < j->end && (*j->p == ' ' || *j->p == '\t' || *j->p == '\n' || *j->p == '\r')) {
        j->p++;
    }
}

static int jPeek(Json *j)
{
    jSpace(j);
    return j->p < j->end ? (unsigned char)*j->p : -1;
}

static int jTake(Json *j, char c)
{
    if (jPeek(j) != (unsigned char)c) {
        return 0;
    }
    j->p++;
    return 1;
}

static int jWord(Json *j, const char *word)
{
    const size_t n = strlen(word);

    jSpace(j);
    if ((size_t)(j->end - j->p) < n || memcmp(j->p, word, n) != 0) {
        return 0;
    }
    j->p += n;
    return 1;
}

static int jHex(int c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/*
 * A string into out (cap bytes with the NUL; out NULL skips it). Returns its
 * length, -1 on bad JSON, -2 when it doesn't fit. *plain: printable ASCII only.
 */
static int jString(Json *j, char *out, size_t cap, int *plain)
{
    size_t n = 0;
    int ascii = 1;

    if (jPeek(j) != '"') {
        return jFail(j, "expected a string");
    }
    j->p++;
    for (;;) {
        int c;

        if (j->p >= j->end) {
            return jFail(j, "a string isn't closed");
        }
        c = (unsigned char)*j->p++;
        if (c == '"') {
            break;
        }
        if (c < 0x20) {
            return jFail(j, "a string holds a control character");
        }
        if (c == '\\') {
            if (j->p >= j->end) {
                return jFail(j, "a string isn't closed");
            }
            c = (unsigned char)*j->p++;
            switch (c) {
                case '"': case '\\': case '/': break;
                case 'b': c = '\b'; break;
                case 'f': c = '\f'; break;
                case 'n': c = '\n'; break;
                case 'r': c = '\r'; break;
                case 't': c = '\t'; break;
                case 'u': {
                    int v = 0, i;

                    for (i = 0; i < 4; i++) {
                        const int h = j->p < j->end ? jHex((unsigned char)*j->p) : -1;

                        if (h < 0) {
                            return jFail(j, "a \\u escape needs four hex digits");
                        }
                        v = v * 16 + h;
                        j->p++;
                    }
                    c = v >= 0x20 && v < 0x7f ? v : '?';
                    if (c == '?' && v != '?') {
                        ascii = 0;
                    }
                    break;
                }
                default:
                    return jFail(j, "a string has a bad escape");
            }
        }
        if (c < 0x20 || c >= 0x7f) {
            ascii = 0;
        }
        if (out && n + 1 < cap) {
            out[n] = (char)c;
        }
        n++;
    }
    if (out && cap) {
        out[n < cap ? n : cap - 1] = '\0';
    }
    if (plain) {
        *plain = ascii;
    }
    return out && n >= cap ? -2 : (int)n;
}

/* A number; *whole when it has no fraction or exponent. */
static int jNumber(Json *j, long long *value, int *whole)
{
    const char *start;
    long long v = 0;
    int neg = 0, digits = 0, big = 0;

    jSpace(j);
    start = j->p;
    *whole = 1;
    if (j->p < j->end && *j->p == '-') {
        neg = 1;
        j->p++;
    }
    if (j->p < j->end && *j->p == '0') {
        j->p++;
        digits = 1;
    } else {
        while (j->p < j->end && *j->p >= '0' && *j->p <= '9') {
            if (v > 100000000) {
                big = 1;
            } else {
                v = v * 10 + (*j->p - '0');
            }
            j->p++;
            digits++;
        }
    }
    if (!digits) {
        j->p = start;
        return jFail(j, "expected a value");
    }
    if (j->p < j->end && *j->p == '.') {
        *whole = 0;
        j->p++;
        if (j->p >= j->end || *j->p < '0' || *j->p > '9') {
            return jFail(j, "a number has a bad fraction");
        }
        while (j->p < j->end && *j->p >= '0' && *j->p <= '9') j->p++;
    }
    if (j->p < j->end && (*j->p == 'e' || *j->p == 'E')) {
        *whole = 0;
        j->p++;
        if (j->p < j->end && (*j->p == '+' || *j->p == '-')) j->p++;
        if (j->p >= j->end || *j->p < '0' || *j->p > '9') {
            return jFail(j, "a number has a bad exponent");
        }
        while (j->p < j->end && *j->p >= '0' && *j->p <= '9') j->p++;
    }
    if (big) {
        *whole = 0;
    }
    *value = neg ? -v : v;
    return 0;
}

/* Any value, read past (the keys this build doesn't know). */
static int jSkip(Json *j, int depth)
{
    const int c = jPeek(j);
    long long n;
    int whole;

    if (depth > 32) {
        return jFail(j, "nested too deep");
    }
    if (c == '"') {
        return jString(j, NULL, 0, NULL) < 0 ? -1 : 0;
    }
    if (c == '{' || c == '[') {
        const char close = c == '{' ? '}' : ']';

        j->p++;
        if (jTake(j, close)) {
            return 0;
        }
        for (;;) {
            if (c == '{') {
                if (jString(j, NULL, 0, NULL) < 0) return -1;
                if (!jTake(j, ':')) return jFail(j, "expected ':'");
            }
            if (jSkip(j, depth + 1)) return -1;
            if (jTake(j, close)) return 0;
            if (!jTake(j, ',')) return jFail(j, "expected ',' or '%c'", close);
        }
    }
    if (jWord(j, "true") || jWord(j, "false") || jWord(j, "null")) {
        return 0;
    }
    return jNumber(j, &n, &whole);
}

enum { K_FORMAT, K_ID, K_TITLE, K_BY, K_VERSION, K_API, K_KIND, K_BASE, K_COOP, K_SITE, K_AFFECTS, K_MODES, K_COUNT };

static const char *const s_keys[K_COUNT] = {
    "format", "id", "title", "by", "version", "api", "kind", "base", "coop", "site", "affectsPlay", "modes" };

static int jText(Json *j, const char *key, char *out, size_t cap, int spaces)
{
    int plain = 1;
    const int n = jString(j, out, cap, &plain);
    size_t i;

    if (n == -1) {
        return -1;
    }
    if (n == -2) {
        return jFail(j, "\"%s\" is longer than %d characters", key, (int)cap - 1);
    }
    if (n == 0) {
        return jFail(j, "\"%s\" is empty", key);
    }
    if (!plain) {
        return jFail(j, "\"%s\" uses characters other than plain ASCII letters and signs", key);
    }
    for (i = 0; !spaces && out[i]; i++) {
        if (out[i] == ' ') {
            return jFail(j, "\"%s\" has a space in it", key);
        }
    }
    return 0;
}

static int jBool(Json *j, const char *key, int *out)
{
    if (jWord(j, "true")) {
        *out = 1;
    } else if (jWord(j, "false")) {
        *out = 0;
    } else {
        return jFail(j, "\"%s\" must be true or false", key);
    }
    return 0;
}

static int jInt(Json *j, const char *key, int *out)
{
    long long v;
    int whole;

    if (jNumber(j, &v, &whole) != 0) {
        return -1;
    }
    if (!whole || v < 0 || v > 1000000) {
        return jFail(j, "\"%s\" must be a whole number", key);
    }
    *out = (int)v;
    return 0;
}

static int jValue(Json *j, int key, gevrModManifest *m)
{
    char word[24];
    int n;

    switch (key) {
        case K_FORMAT: return jInt(j, "format", &m->format);
        case K_API: return jInt(j, "api", &m->api);
        case K_ID:
            if (jText(j, "id", m->id, sizeof(m->id), 0)) return -1;
            if (!gevrModIdValid(m->id)) {
                return jFail(j, "\"id\" must be lower-case letters, digits and '-', up to %d", GEVR_MOD_ID_MAX);
            }
            return 0;
        case K_TITLE: return jText(j, "title", m->title, sizeof(m->title), 1);
        case K_BY: return jText(j, "by", m->by, sizeof(m->by), 1);
        case K_VERSION: return jText(j, "version", m->version, sizeof(m->version), 0);
        case K_SITE:
            if (jText(j, "site", m->site, sizeof(m->site), 0)) return -1;
            if (strncmp(m->site, "https://", 8) != 0) {
                return jFail(j, "\"site\" must be an https:// address");
            }
            return 0;
        case K_KIND:
            n = jString(j, word, sizeof(word), NULL);
            if (n == -1) return -1;
            if (n >= 0 && !strcmp(word, "campaign")) m->kind = GEVR_MOD_CAMPAIGN;
            else if (n >= 0 && !strcmp(word, "overlay")) m->kind = GEVR_MOD_OVERLAY;
            else return jFail(j, "\"kind\" must be \"campaign\" or \"overlay\"");
            return 0;
        case K_BASE:
            n = jString(j, word, sizeof(word), NULL);
            if (n == -1) return -1;
            m->base = n >= 0 ? gevrModBaseCampaign(word) : -1;
            if (m->base < 0) {
                return jFail(j, "\"base\" must be \"ge\", \"gf\", \"tnd\", \"mirror\" or \"mario\"");
            }
            return 0;
        case K_COOP: return jBool(j, "coop", &m->coop);
        case K_AFFECTS: return jBool(j, "affectsPlay", &m->affectsPlay);
        case K_MODES:
            if (!jTake(j, '[')) return jFail(j, "\"modes\" must be a list");
            if (jTake(j, ']')) return 0;
            for (;;) {
                n = jString(j, word, sizeof(word), NULL);
                if (n == -1) return -1;
                if (n >= 0 && !strcmp(word, "coop")) m->modes |= GEVR_MOD_MODE_COOP;
                else if (n >= 0 && !strcmp(word, "deathmatch")) m->modes |= GEVR_MOD_MODE_DEATHMATCH;
                else if (n >= 0 && !strcmp(word, "horde")) m->modes |= GEVR_MOD_MODE_HORDE;
                else return jFail(j, "\"modes\" can hold \"coop\", \"deathmatch\" and \"horde\"");
                if (jTake(j, ']')) return 0;
                if (!jTake(j, ',')) return jFail(j, "expected ',' or ']' in \"modes\"");
            }
    }
    return -1;
}

int gevrModParseManifest(const char *json, size_t len, gevrModManifest *out, char *why, size_t whyLen)
{
    Json j;
    unsigned seen = 0;
    int k;

    memset(out, 0, sizeof(*out));
    out->base = -1;
    j.p = json;
    j.end = json + len;
    j.why = why;
    j.whyLen = whyLen;
    j.failed = 0;
    if (len > GEVR_MOD_MAX_MANIFEST_BYTES) {
        return jFail(&j, "bigger than %d KB", GEVR_MOD_MAX_MANIFEST_BYTES >> 10);
    }
    if (len >= 3 && (unsigned char)json[0] == 0xef && (unsigned char)json[1] == 0xbb && (unsigned char)json[2] == 0xbf) {
        j.p += 3;   /* a UTF-8 byte-order mark, as some editors write */
    }
    if (!jTake(&j, '{')) {
        return jFail(&j, "must be one JSON object ({ ... })");
    }
    if (!jTake(&j, '}')) {
        for (;;) {
            char key[24];
            const int n = jString(&j, key, sizeof(key), NULL);

            if (n == -1) return -1;
            if (!jTake(&j, ':')) return jFail(&j, "expected ':' after a key");
            for (k = 0; n >= 0 && k < K_COUNT && strcmp(key, s_keys[k]) != 0; k++) {
            }
            if (n >= 0 && k < K_COUNT) {
                if (seen & (1u << k)) return jFail(&j, "\"%s\" appears twice", s_keys[k]);
                seen |= 1u << k;
                if (jValue(&j, k, out)) return -1;
            } else if (jSkip(&j, 0)) {
                return -1;
            }
            if (jTake(&j, '}')) break;
            if (!jTake(&j, ',')) return jFail(&j, "expected ',' or '}'");
            if (jPeek(&j) == '}') return jFail(&j, "a comma before '}'");
        }
    }
    if (jPeek(&j) != -1) {
        return jFail(&j, "has text after its closing '}'");
    }
    for (k = 0; k <= K_BASE; k++) {
        if (!(seen & (1u << k))) {
            return jFail(&j, "\"%s\" is missing", s_keys[k]);
        }
    }
    if (out->format != GEVR_MOD_FORMAT) {
        return jFail(&j, "\"format\" %d isn't one this build reads (%d)", out->format, GEVR_MOD_FORMAT);
    }
    if (out->api > GEVR_MOD_API_MAX) {
        say(why, whyLen, "%s needs a newer GoldenEye VR (mod API %d; this build runs %d to %d)", out->id,
            out->api, GEVR_MOD_API_MIN, GEVR_MOD_API_MAX);
        return -1;
    }
    if (out->api < GEVR_MOD_API_MIN) {
        say(why, whyLen, "%s was made for mod API %d, which this build no longer runs (%d to %d)", out->id,
            out->api, GEVR_MOD_API_MIN, GEVR_MOD_API_MAX);
        return -1;
    }
    if (out->kind == GEVR_MOD_CAMPAIGN) {
        out->affectsPlay = 1;   /* a campaign is its own play: checked in the lobby */
        out->modes = 0;
    } else if (!out->affectsPlay) {
        out->modes = 0;         /* an overlay that changes looks only works anywhere, unchecked */
    }
    return 0;
}

/* ---- files.sha256 ---- */

typedef struct ListEntry {
    char *path;
    uint8_t sum[32];
} ListEntry;

static int compareListEntries(const void *a, const void *b)
{
    return compareFolded(((const ListEntry *)a)->path, ((const ListEntry *)b)->path);
}

static void freeList(ListEntry *list, int count)
{
    int i;

    for (i = 0; list && i < count; i++) {
        free(list[i].path);
    }
    free(list);
}

/* "<64 hex>  <path>" or "<64 hex> *<path>" per line (sha256sum's own output), sorted. */
static int parseList(const char *text, size_t len, ListEntry **out, int *count, char *why, size_t whyLen)
{
    const char *p = text, *end = text + len;
    ListEntry *list = NULL;
    int n = 0, cap = 0, line = 0, i;
    char shown[96];

    *out = NULL;
    *count = 0;
    if (len >= 3 && (unsigned char)p[0] == 0xef && (unsigned char)p[1] == 0xbb && (unsigned char)p[2] == 0xbf) {
        p += 3;
    }
    while (p < end) {
        const char *eol = memchr(p, '\n', (size_t)(end - p));
        const char *stop = eol ? eol : end;
        const char *q = p;
        size_t plen;
        ListEntry e;

        line++;
        if (stop > p && stop[-1] == '\r') {
            stop--;
        }
        while (q < stop && (*q == ' ' || *q == '\t')) q++;
        if (q == stop) {
            p = eol ? eol + 1 : end;
            continue;
        }
        if (stop - p < 67 || (p[64] != ' ') || (p[65] != ' ' && p[65] != '*')) {
            freeList(list, n);
            return say(why, whyLen, "files.sha256 line %d isn't \"<sha256>  <path>\"", line);
        }
        for (i = 0; i < 32; i++) {
            const int hi = jHex((unsigned char)p[2 * i]), lo = jHex((unsigned char)p[2 * i + 1]);

            if (hi < 0 || lo < 0) {
                freeList(list, n);
                return say(why, whyLen, "files.sha256 line %d has a bad checksum", line);
            }
            e.sum[i] = (uint8_t)(hi * 16 + lo);
        }
        plen = (size_t)(stop - (p + 66));
        e.path = (char *)malloc(plen + 1);
        if (!e.path) {
            freeList(list, n);
            return say(why, whyLen, "out of memory");
        }
        memcpy(e.path, p + 66, plen);
        e.path[plen] = '\0';
        if (!pathOk(e.path) || !strcmp(e.path, "files.sha256")) {
            printable(e.path, shown, sizeof(shown));
            free(e.path);
            freeList(list, n);
            return say(why, whyLen, "files.sha256 line %d: \"%s\" isn't a path it can list", line, shown);
        }
        if (n >= GEVR_MOD_MAX_FILES) {
            free(e.path);
            freeList(list, n);
            return say(why, whyLen, "the mod has more than %d files", GEVR_MOD_MAX_FILES);
        }
        if (n == cap) {
            ListEntry *grown;

            cap = cap ? cap * 2 : 64;
            grown = (ListEntry *)realloc(list, (size_t)cap * sizeof(*list));
            if (!grown) {
                free(e.path);
                freeList(list, n);
                return say(why, whyLen, "out of memory");
            }
            list = grown;
        }
        list[n++] = e;
        p = eol ? eol + 1 : end;
    }
    qsort(list, (size_t)n, sizeof(*list), compareListEntries);
    for (i = 1; i < n; i++) {
        if (compareFolded(list[i - 1].path, list[i].path) == 0) {
            printable(list[i].path, shown, sizeof(shown));
            freeList(list, n);
            return say(why, whyLen, "files.sha256 lists %s twice", shown);
        }
    }
    *out = list;
    *count = n;
    return 0;
}

static ListEntry *findListed(ListEntry *list, int count, const char *path)
{
    ListEntry key;

    key.path = (char *)path;
    return (ListEntry *)bsearch(&key, list, (size_t)count, sizeof(*list), compareListEntries);
}

/* ---- install ---- */

typedef struct ZipEntry {
    char *rel;
    mz_uint index;
    mz_uint64 size;
} ZipEntry;

static int compareZipEntries(const void *a, const void *b)
{
    return compareFolded(((const ZipEntry *)a)->rel, ((const ZipEntry *)b)->rel);
}

typedef struct Sink {
    FILE *f;
    gevrSha256 sha;
    mz_uint64 bytes, limit;
    long long *done, total;
    int (*progress)(int percent, void *ud);
    void *ud;
    int *lastPercent;
    int cancelled;
} Sink;

static size_t sinkWrite(void *opaque, mz_uint64 offset, const void *data, size_t n)
{
    Sink *s = (Sink *)opaque;

    if (offset != s->bytes || s->bytes + n > s->limit || fwrite(data, 1, n, s->f) != n) {
        return 0;
    }
    gevrSha256Update(&s->sha, data, n);
    s->bytes += n;
    *s->done += (long long)n;
    if (s->progress) {
        const int percent = (int)(*s->done * 99 / (s->total > 0 ? s->total : 1));

        if (percent != *s->lastPercent) {
            *s->lastPercent = percent;
            if (!s->progress(percent, s->ud)) {
                s->cancelled = 1;
                return 0;
            }
        }
    }
    return n;
}

static void *extractSmall(mz_zip_archive *z, const ZipEntry *e, size_t max, size_t *len)
{
    if (e->size > max) {
        return NULL;
    }
    return mz_zip_reader_extract_to_heap(z, e->index, len, 0);
}

static int installFrom(mz_zip_archive *z, gevrModInfo *out, int (*progress)(int, void *), void *ud,
                       char *why, size_t whyLen)
{
    const mz_uint total = mz_zip_reader_get_num_files(z);
    ZipEntry *entries = NULL, *manifestEntry = NULL, *listEntry = NULL;
    ListEntry *list = NULL;
    void *manifest = NULL, *listText = NULL;
    size_t manifestLen = 0, listLen = 0;
    char prefix[256] = "", staging[PATH_CAP], dest[PATH_CAP], backup[PATH_CAP], shown[96], name[GEVR_MOD_ID_MAX + 16];
    int count = 0, listCount = 0, rc = -1, i, lastPercent = -1, staged = 0;
    long long bytes = 0, done = 0;
    mz_uint k;
    size_t prefixLen;
    int at;

    staging[0] = '\0';
    if (total == 0) {
        say(why, whyLen, "the zip is empty");
        goto done;
    }
    /* mod.json at the top, or inside the one folder everything is in */
    at = mz_zip_reader_locate_file(z, "mod.json", NULL, MZ_ZIP_FLAG_CASE_SENSITIVE);
    if (at < 0) {
        char name[MZ_ZIP_MAX_ARCHIVE_FILENAME_SIZE];
        const char *slash = NULL;

        for (k = 0; k < total && !slash; k++) {
            mz_zip_reader_get_filename(z, k, name, sizeof(name));
            slash = zipJunk(name) ? NULL : strchr(name, '/');
        }
        if (slash && (size_t)(slash - name) + 1 < sizeof(prefix)) {
            memcpy(prefix, name, (size_t)(slash - name) + 1);
            prefix[slash - name + 1] = '\0';
            snprintf(name, sizeof(name), "%smod.json", prefix);
            at = mz_zip_reader_locate_file(z, name, NULL, MZ_ZIP_FLAG_CASE_SENSITIVE);
        }
        if (at < 0) {
            say(why, whyLen, "this zip isn't a mod: it has no mod.json at its top");
            goto done;
        }
    }
    prefixLen = strlen(prefix);
    entries = (ZipEntry *)calloc(total, sizeof(*entries));
    if (!entries) {
        say(why, whyLen, "out of memory");
        goto done;
    }
    for (k = 0; k < total; k++) {
        mz_zip_archive_file_stat st;

        if (!mz_zip_reader_file_stat(z, k, &st)) {
            say(why, whyLen, "the zip is damaged");
            goto done;
        }
        printable(st.m_filename, shown, sizeof(shown));
        if (zipJunk(st.m_filename)) {
            continue;
        }
        if (strncmp(st.m_filename, prefix, prefixLen) != 0) {
            say(why, whyLen, "%s is outside the mod's folder in the zip", shown);
            goto done;
        }
        if (st.m_is_directory || !st.m_filename[prefixLen]) {
            continue;
        }
        if (st.m_is_encrypted) {
            say(why, whyLen, "%s is encrypted", shown);
            goto done;
        }
        if (!st.m_is_supported) {
            say(why, whyLen, "%s is packed in a way this build can't read (use plain zip)", shown);
            goto done;
        }
        if (!pathOk(st.m_filename + prefixLen)) {
            say(why, whyLen, "%s isn't a safe name for a mod file", shown);
            goto done;
        }
        if (!layoutOk(st.m_filename + prefixLen)) {
            say(why, whyLen, "%s isn't in scripts/ (.lua) or assets/", shown);
            goto done;
        }
        if (st.m_uncomp_size > (mz_uint64)GEVR_MOD_MAX_FILE_BYTES) {
            say(why, whyLen, "%s is bigger than %d MB", shown, (int)(GEVR_MOD_MAX_FILE_BYTES >> 20));
            goto done;
        }
        bytes += (long long)st.m_uncomp_size;
        if (bytes > GEVR_MOD_MAX_TOTAL_BYTES) {
            say(why, whyLen, "the mod is bigger than %d GB", (int)(GEVR_MOD_MAX_TOTAL_BYTES >> 30));
            goto done;
        }
        if (count > GEVR_MOD_MAX_FILES) {
            say(why, whyLen, "the mod has more than %d files", GEVR_MOD_MAX_FILES);
            goto done;
        }
        entries[count].rel = copyString(st.m_filename + prefixLen);
        entries[count].index = k;
        entries[count].size = st.m_uncomp_size;
        if (!entries[count].rel) {
            say(why, whyLen, "out of memory");
            goto done;
        }
        count++;
    }
    qsort(entries, (size_t)count, sizeof(*entries), compareZipEntries);
    for (i = 0; i < count; i++) {
        if (i > 0 && compareFolded(entries[i - 1].rel, entries[i].rel) == 0) {
            say(why, whyLen, "the zip holds %s twice", printable(entries[i].rel, shown, sizeof(shown)));
            goto done;
        }
        if (!strcmp(entries[i].rel, "mod.json")) manifestEntry = &entries[i];
        if (!strcmp(entries[i].rel, "files.sha256")) listEntry = &entries[i];
    }
    if (!manifestEntry) {
        say(why, whyLen, "this zip isn't a mod: it has no mod.json at its top");
        goto done;
    }
    if (!listEntry) {
        say(why, whyLen, "the zip has no files.sha256 beside its mod.json");
        goto done;
    }
    manifest = extractSmall(z, manifestEntry, GEVR_MOD_MAX_MANIFEST_BYTES, &manifestLen);
    if (!manifest) {
        say(why, whyLen, "mod.json is unreadable or bigger than %d KB", GEVR_MOD_MAX_MANIFEST_BYTES >> 10);
        goto done;
    }
    if (gevrModParseManifest((const char *)manifest, manifestLen, &out->m, why, whyLen) != 0) {
        goto done;
    }
    listText = extractSmall(z, listEntry, GEVR_MOD_MAX_LIST_BYTES, &listLen);
    if (!listText) {
        say(why, whyLen, "files.sha256 is unreadable or bigger than %d MB", GEVR_MOD_MAX_LIST_BYTES >> 20);
        goto done;
    }
    if (parseList((const char *)listText, listLen, &list, &listCount, why, whyLen) != 0) {
        goto done;
    }
    /* the list and the zip hold the same files, spelled the same */
    for (i = 0; i < count; i++) {
        const ListEntry *e = findListed(list, listCount, entries[i].rel);

        if (entries[i].rel == listEntry->rel) {
            continue;
        }
        if (!e || strcmp(e->path, entries[i].rel) != 0) {
            say(why, whyLen, "%s isn't in files.sha256", printable(entries[i].rel, shown, sizeof(shown)));
            goto done;
        }
    }
    if (listCount != count - 1) {
        for (i = 0; i < listCount; i++) {
            ZipEntry key;

            key.rel = list[i].path;
            if (!bsearch(&key, entries, (size_t)count, sizeof(*entries), compareZipEntries)) {
                say(why, whyLen, "files.sha256 lists %s, which isn't in the zip", printable(list[i].path, shown, sizeof(shown)));
                goto done;
            }
        }
    }
    /* unpack beside the installed mods, checking each file as it lands */
    if (makeDirs(gevrModsRoot()) != 0) {
        say(why, whyLen, "couldn't make the mods folder");
        goto done;
    }
    snprintf(name, sizeof(name), ".%s.unpacking", out->m.id);
    if (joinPath(staging, sizeof(staging), gevrModsRoot(), name) != 0) {
        say(why, whyLen, "the mods folder's path is too long");
        staging[0] = '\0';
        goto done;
    }
    removeTree(staging);
    if (makeDir(staging) != 0) {
        say(why, whyLen, "couldn't make a folder to unpack into");
        staging[0] = '\0';
        goto done;
    }
    staged = 1;
    if (progress && !progress(0, ud)) {
        rc = -2;
        say(why, whyLen, "cancelled");
        goto done;
    }
    for (i = 0; i < count; i++) {
        char path[PATH_CAP], *slash;
        Sink sink;
        int ok;

        if (joinPath(path, sizeof(path), staging, entries[i].rel) != 0) {
            say(why, whyLen, "the path for %s is too long", entries[i].rel);
            goto done;
        }
        slash = strrchr(path, '/');
        *slash = '\0';
        if (makeDirs(path) != 0) {
            say(why, whyLen, "couldn't make a folder for %s", entries[i].rel);
            goto done;
        }
        *slash = '/';
        memset(&sink, 0, sizeof(sink));
        sink.f = fopen(path, "wb");
        if (!sink.f) {
            say(why, whyLen, "couldn't write %s", entries[i].rel);
            goto done;
        }
        gevrSha256Init(&sink.sha);
        sink.limit = entries[i].size;
        sink.done = &done;
        sink.total = bytes;
        sink.progress = progress;
        sink.ud = ud;
        sink.lastPercent = &lastPercent;
        ok = mz_zip_reader_extract_to_callback(z, entries[i].index, sinkWrite, &sink, 0) != 0;
        ok = fclose(sink.f) == 0 && ok;
        if (sink.cancelled) {
            rc = -2;
            say(why, whyLen, "cancelled");
            goto done;
        }
        if (!ok || sink.bytes != entries[i].size) {
            say(why, whyLen, "%s is damaged in the zip", entries[i].rel);
            goto done;
        }
        if (&entries[i] != listEntry) {
            uint8_t sum[32];

            gevrSha256Final(&sink.sha, sum);
            if (memcmp(sum, findListed(list, listCount, entries[i].rel)->sum, 32) != 0) {
                say(why, whyLen, "%s doesn't match its checksum in files.sha256", entries[i].rel);
                goto done;
            }
        }
    }
    /* in place: the old version aside, the new one in, the old one gone */
    snprintf(name, sizeof(name), ".%s.backup", out->m.id);
    if (joinPath(dest, sizeof(dest), gevrModsRoot(), out->m.id) != 0 || joinPath(backup, sizeof(backup), gevrModsRoot(), name) != 0) {
        say(why, whyLen, "the mods folder's path is too long");
        goto done;
    }
    removeTree(backup);
    if (isDir(dest) && rename(dest, backup) != 0) {
        say(why, whyLen, "couldn't move the installed %s aside (is it in use?)", out->m.id);
        goto done;
    }
    if (rename(staging, dest) != 0) {
        rename(backup, dest);
        say(why, whyLen, "couldn't put %s in place", out->m.id);
        goto done;
    }
    staged = 0;
    removeTree(backup);
    {
        gevrSha256 s;

        gevrSha256Init(&s);
        gevrSha256Update(&s, listText, listLen);
        gevrSha256Final(&s, out->content);
    }
    if (progress) {
        progress(100, ud);
    }
    sysLogPrintf(LOG_NOTE, "mods: installed %s %s (%d files)", out->m.id, out->m.version, count);
    rc = 0;
done:
    if (staged) {
        removeTree(staging);
    }
    for (i = 0; entries && i < count; i++) {
        free(entries[i].rel);
    }
    free(entries);
    freeList(list, listCount);
    mz_free(manifest);
    mz_free(listText);
    return rc;
}

int gevrModInstallZip(const char *zipPath, gevrModInfo *out, int (*progress)(int percent, void *ud), void *ud,
                      char *why, size_t whyLen)
{
    mz_zip_archive z;
    int rc;

    memset(out, 0, sizeof(*out));
    memset(&z, 0, sizeof(z));
    if (!mz_zip_reader_init_file(&z, zipPath, 0)) {
        return say(why, whyLen, "that file isn't a zip, or couldn't be read");
    }
    rc = installFrom(&z, out, progress, ud, why, whyLen);
    mz_zip_reader_end(&z);
    if (rc != 0) {
        sysLogPrintf(LOG_WARNING, "mods: install failed: %s", why ? why : "");
    }
    return rc;
}

/* ---- installed mods ---- */

int gevrModLoad(const char *id, gevrModInfo *out, char *why, size_t whyLen)
{
    char dir[PATH_CAP], path[PATH_CAP], shown[96];
    char *text;
    size_t len;
    ListEntry *list;
    int count, i, rc;

    memset(out, 0, sizeof(*out));
    if (!gevrModIdValid(id)) {
        return say(why, whyLen, "\"%s\" isn't a mod's id", printable(id ? id : "", shown, sizeof(shown)));
    }
    if (joinPath(dir, sizeof(dir), gevrModsRoot(), id) != 0 || !isDir(dir)) {
        return say(why, whyLen, "%s isn't installed", id);
    }
    joinPath(path, sizeof(path), dir, "mod.json");   /* dir is under PATH_CAP - 40 */
    text = readSmall(path, GEVR_MOD_MAX_MANIFEST_BYTES, &len);
    if (!text) {
        return say(why, whyLen, "%s has no readable mod.json; install it again", id);
    }
    rc = gevrModParseManifest(text, len, &out->m, why, whyLen);
    free(text);
    if (rc != 0) {
        return -1;
    }
    if (strcmp(out->m.id, id) != 0) {
        return say(why, whyLen, "the mod in %s/ calls itself %s; install it again", id, out->m.id);
    }
    joinPath(path, sizeof(path), dir, "files.sha256");
    text = readSmall(path, GEVR_MOD_MAX_LIST_BYTES, &len);
    if (!text) {
        return say(why, whyLen, "%s has no readable files.sha256; install it again", id);
    }
    {
        gevrSha256 s;

        gevrSha256Init(&s);
        gevrSha256Update(&s, text, len);
        gevrSha256Final(&s, out->content);
    }
    rc = parseList(text, len, &list, &count, why, whyLen);
    free(text);
    if (rc != 0) {
        return -1;
    }
    /* each listed file is there (install checked their contents) */
    for (i = 0; i < count; i++) {
        if (joinPath(path, sizeof(path), dir, list[i].path) != 0 || !isFile(path)) {
            say(why, whyLen, "%s is missing %s; install it again", id, printable(list[i].path, shown, sizeof(shown)));
            freeList(list, count);
            return -1;
        }
    }
    freeList(list, count);
    return 0;
}

int gevrModRemove(const char *id, char *why, size_t whyLen)
{
    char dest[PATH_CAP], gone[PATH_CAP], name[GEVR_MOD_ID_MAX + 16];

    if (!gevrModIdValid(id)) {
        return say(why, whyLen, "that isn't a mod's id");
    }
    snprintf(name, sizeof(name), ".%s.removing", id);
    if (joinPath(dest, sizeof(dest), gevrModsRoot(), id) != 0 || joinPath(gone, sizeof(gone), gevrModsRoot(), name) != 0
        || !isDir(dest)) {
        return say(why, whyLen, "%s isn't installed", id);
    }
    removeTree(gone);
    if (rename(dest, gone) != 0) {
        return say(why, whyLen, "couldn't remove %s (is it in use?)", id);
    }
    removeTree(gone);
    sysLogPrintf(LOG_NOTE, "mods: removed %s", id);
    return 0;
}

static int compareIds(const void *a, const void *b)
{
    return strcmp((const char *)a, (const char *)b);
}

int gevrModList(char (*ids)[GEVR_MOD_ID_MAX + 1], int max)
{
    DIR *d = opendir(gevrModsRoot());
    struct dirent *e;
    char path[PATH_CAP];
    int n = 0;

    if (!d) {
        return 0;
    }
    while ((e = readdir(d)) != NULL && n < max) {
        if (!gevrModIdValid(e->d_name)) {
            continue;
        }
        if (joinPath(path, sizeof(path), gevrModsRoot(), e->d_name) == 0 && isDir(path)) {
            strcpy(ids[n++], e->d_name);
        }
    }
    closedir(d);
    qsort(ids, (size_t)n, sizeof(ids[0]), compareIds);
    return n;
}

/* A crash's leftovers: a half-unpacked or half-removed mod goes, and an
 * update caught between its two renames gets its old version back. */
static void tidy(void)
{
    DIR *d = opendir(gevrModsRoot());
    struct dirent *e;
    char names[64][GEVR_MOD_ID_MAX + 16];
    int n = 0, i;

    if (!d) {
        return;
    }
    while ((e = readdir(d)) != NULL && n < 64) {
        if (e->d_name[0] == '.' && strcmp(e->d_name, ".") != 0 && strcmp(e->d_name, "..") != 0
            && strlen(e->d_name) < sizeof(names[0])) {
            strcpy(names[n++], e->d_name);
        }
    }
    closedir(d);
    for (i = 0; i < n; i++) {
        char path[PATH_CAP], dest[PATH_CAP], id[GEVR_MOD_ID_MAX + 16];
        const size_t len = strlen(names[i]);

        if (joinPath(path, sizeof(path), gevrModsRoot(), names[i]) != 0) {
            continue;
        }
        if (len > 8 && !strcmp(names[i] + len - 7, ".backup")) {
            memcpy(id, names[i] + 1, len - 8);
            id[len - 8] = '\0';
            if (gevrModIdValid(id) && joinPath(dest, sizeof(dest), gevrModsRoot(), id) == 0 && !isDir(dest)
                && rename(path, dest) == 0) {
                sysLogPrintf(LOG_NOTE, "mods: put %s back after an interrupted update", id);
                continue;
            }
            removeTree(path);
        } else if (endsWithFolded(names[i], ".unpacking") || endsWithFolded(names[i], ".removing")) {
            removeTree(path);
        }
    }
}

/* ---- this run's mod campaign ---- */

static void note(const char *fmt, ...)
{
    char text[400];
    FILE *f;
    va_list ap;

    va_start(ap, fmt);
    vsnprintf(text, sizeof(text), fmt, ap);
    va_end(ap);
    sysLogPrintf(LOG_ERROR, "mods: %s", text);
    /* the launcher shows it next and deletes it (vr_launcher.cpp) */
    f = fsFileOpenWrite(GEVR_CAMPAIGN_FAILED_FILE);
    if (f) {
        fputs(text, f);
        fclose(f);
    }
}

void gevrModBoot(void)
{
    char why[256];

    gevrModsRoot();
    tidy();
    s_hasActive = 0;
    if (!VrCampaignMod[0]) {
        return;
    }
    if (gevrModLoad(VrCampaignMod, &s_active, why, sizeof(why)) != 0) {
        note("The mod campaign did not start: %s.", why);
    } else if (s_active.m.kind != GEVR_MOD_CAMPAIGN) {
        note("%s is an overlay, not a campaign, so it did not start.", s_active.m.title);
    } else {
        VrCampaign = s_active.m.base;
        s_hasActive = 1;
        sysLogPrintf(LOG_NOTE, "mods: playing %s %s by %s", s_active.m.id, s_active.m.version, s_active.m.by);
        return;
    }
    VrCampaignMod[0] = '\0';
}

void gevrModBaseFailed(const char *campaignName)
{
    if (!s_hasActive) {
        return;
    }
    note("%s needs %s, which did not load, so GoldenEye started instead.", s_active.m.title,
         campaignName ? campaignName : "its cartridge");
    s_hasActive = 0;
    VrCampaignMod[0] = '\0';
}

const gevrModInfo *gevrModActive(void)
{
    return s_hasActive ? &s_active : NULL;
}
