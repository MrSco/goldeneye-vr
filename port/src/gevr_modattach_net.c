/*
 * mod.attach on the other players' headsets (#335): the message's bytes and
 * the owner's side, what this player wears and what's waiting to be sent.
 * The layout and the rules are in gevr_modattach_net.h; gevr_modapi.c calls
 * it as the API attaches, and hands the others' to the game
 * (src/game/gevr_modattach.c), which draws them on their players.
 */
#include <math.h>
#include <string.h>

#include "gevr_modattach_net.h"

#define ALL (1u << GEVR_ATTNET_SLOTS)   /* pending: everyone */
#define MODS 8                          /* mods told a clear at once */

typedef struct {
    int id;                             /* 0 free */
    char mod[64];
    GevrAttachRecord r;
    unsigned pending;                   /* a bit a slot, ALL everyone */
    int gone;                           /* removed: a REMOVE to send, then free */
} Owned;

static Owned s_owned[GEVR_ATTNET_OWNED];
static char s_clear[MODS][64];          /* mods whose CLEAR waits */
static int s_clears;
static unsigned s_ask;                  /* the host's asks waiting: a bit a slot */
static char s_askMod[64];
static int s_sends;                     /* messages sent in this second (s_since on) */
static unsigned long long s_since;

/* ---- the bytes ---- */

int gevrAttachNetNameOk(const char *s)
{
    size_t n, i;

    if (s == NULL) return 0;
    n = strlen(s);
    if (n < 1 || n > GEVR_ATTNET_NAME) return 0;
    if (n > 4 && !memcmp(s + n - 4, ".bin", 4)) n -= 4;
    for (i = 0; i < n; i++) {
        const char c = s[i];

        if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-')) return 0;
    }
    return 1;
}

static int limit(float v, float most)
{
    return isfinite(v) && v >= -most && v <= most;
}

/* a SET's fields within the limits */
static int setOk(const GevrAttachRecord *r)
{
    int i, j;

    if (r->id < 1 || r->on < GEVR_ATTNET_GUN || r->on > GEVR_ATTNET_BODYSLOT || (r->shown != 0 && r->shown != 1)) return 0;
    if (r->on == GEVR_ATTNET_BODYSLOT ? r->where < 0 || r->where >= GEVR_ATTNET_ANCHORS : r->where < 0 || r->where > 1) return 0;
    if (r->node < 0 || r->node > 255 || (r->on != GEVR_ATTNET_GUN && r->node != 0)) return 0;
    for (i = 0; i < 4; i++) {
        for (j = 0; j < 3; j++) {
            if (!limit(r->frame[i][j], i == 3 ? GEVR_ATTNET_ORIGIN : GEVR_ATTNET_AXIS)) return 0;
        }
    }
    for (i = 0; i < 3; i++) {
        if (!limit(r->body[i], GEVR_ATTNET_BODY)) return 0;
    }
    return gevrAttachNetNameOk(r->model) && (!r->like[0] || gevrAttachNetNameOk(r->like));
}

static int recordBytes(const GevrAttachRecord *r)
{
    switch (r->op) {
    case GEVR_ATTNET_SET:
        return setOk(r) ? GEVR_ATTNET_SET_BYTES + (int)strlen(r->model) + (int)strlen(r->like) : -1;
    case GEVR_ATTNET_REMOVE:
        return r->id >= 1 ? 5 : -1;
    case GEVR_ATTNET_CLEAR:
        return 1;
    case GEVR_ATTNET_ASK:
        return r->slot >= 0 && r->slot < GEVR_ATTNET_SLOTS ? 2 : -1;
    default:
        return -1;
    }
}

static unsigned char *put32(unsigned char *p, unsigned v)
{
    p[0] = (unsigned char)v;
    p[1] = (unsigned char)(v >> 8);
    p[2] = (unsigned char)(v >> 16);
    p[3] = (unsigned char)(v >> 24);
    return p + 4;
}

static unsigned get32(const unsigned char *p)
{
    return (unsigned)p[0] | (unsigned)p[1] << 8 | (unsigned)p[2] << 16 | (unsigned)p[3] << 24;
}

static unsigned char *putFloat(unsigned char *p, float f)
{
    unsigned v;

    memcpy(&v, &f, sizeof(v));
    return put32(p, v);
}

static float getFloat(const unsigned char *p)
{
    const unsigned v = get32(p);
    float f;

    memcpy(&f, &v, sizeof(f));
    return f;
}

static unsigned char *putName(unsigned char *p, const char *s)
{
    const size_t n = strlen(s);

    *p++ = (unsigned char)n;
    memcpy(p, s, n);
    return p + n;
}

int gevrAttachNetEncode(unsigned char *out, int cap, int stage, const GevrAttachRecord *r, int n)
{
    unsigned char *p = out;
    int i, j, k, total = 2;

    if (out == NULL || r == NULL || n < 1 || n > GEVR_ATTNET_RECORDS) return 0;
    if (cap > GEVR_ATTNET_BYTES) cap = GEVR_ATTNET_BYTES;
    for (i = 0; i < n; i++) {
        const int b = recordBytes(&r[i]);

        if (b < 0) return 0;
        total += b;
    }
    if (total > cap) return 0;
    *p++ = (unsigned char)stage;
    *p++ = (unsigned char)n;
    for (i = 0; i < n; i++) {
        *p++ = (unsigned char)r[i].op;
        switch (r[i].op) {
        case GEVR_ATTNET_SET:
            p = put32(p, (unsigned)r[i].id);
            *p++ = (unsigned char)r[i].on;
            *p++ = (unsigned char)r[i].where;
            *p++ = (unsigned char)r[i].node;
            *p++ = (unsigned char)(r[i].shown ? 1 : 0);
            for (j = 0; j < 4; j++) {
                for (k = 0; k < 3; k++) p = putFloat(p, r[i].frame[j][k]);
            }
            for (k = 0; k < 3; k++) p = putFloat(p, r[i].body[k]);
            p = putName(p, r[i].model);
            p = putName(p, r[i].like);
            break;
        case GEVR_ATTNET_REMOVE:
            p = put32(p, (unsigned)r[i].id);
            break;
        case GEVR_ATTNET_ASK:
            *p++ = (unsigned char)r[i].slot;
            break;
        default:
            break;
        }
    }
    return (int)(p - out);
}

/* a name of n bytes, read into out: 0 when it runs past the end */
static int getName(const unsigned char **p, const unsigned char *end, char *out)
{
    int n;

    if (*p >= end) return 0;
    n = *(*p)++;
    if (n > GEVR_ATTNET_NAME || end - *p < n) return 0;
    memcpy(out, *p, (size_t)n);
    out[n] = '\0';
    if (memchr(out, '\0', (size_t)n) != NULL) return 0;   /* a NUL inside */
    *p += n;
    return 1;
}

int gevrAttachNetDecode(const unsigned char *data, int len, int *stage, GevrAttachRecord *out)
{
    const unsigned char *p = data, *end;
    int n, i, j, k;

    if (data == NULL || out == NULL || len < 3 || len > GEVR_ATTNET_BYTES) return -1;
    end = data + len;
    if (stage != NULL) *stage = p[0];
    n = p[1];
    p += 2;
    if (n < 1 || n > GEVR_ATTNET_RECORDS) return -1;
    for (i = 0; i < n; i++) {
        GevrAttachRecord *r = &out[i];

        memset(r, 0, sizeof(*r));
        if (p >= end) return -1;
        r->op = *p++;
        switch (r->op) {
        case GEVR_ATTNET_SET:
            if (end - p < GEVR_ATTNET_SET_BYTES - 3) return -1;   /* less the op and the names' lengths */
            {
                const unsigned id = get32(p);

                if (id < 1 || id > 0x7FFFFFFFu) return -1;
                r->id = (int)id;
            }
            p += 4;
            r->on = p[0];
            r->where = p[1];
            r->node = p[2];
            if (p[3] > 1) return -1;
            r->shown = p[3];
            p += 4;
            for (j = 0; j < 4; j++) {
                for (k = 0; k < 3; k++, p += 4) r->frame[j][k] = getFloat(p);
            }
            for (k = 0; k < 3; k++, p += 4) r->body[k] = getFloat(p);
            if (!getName(&p, end, r->model) || !getName(&p, end, r->like) || !setOk(r)) return -1;
            break;
        case GEVR_ATTNET_REMOVE:
            if (end - p < 4) return -1;
            {
                const unsigned id = get32(p);

                if (id < 1 || id > 0x7FFFFFFFu) return -1;
                r->id = (int)id;
            }
            p += 4;
            break;
        case GEVR_ATTNET_CLEAR:
            break;
        case GEVR_ATTNET_ASK:
            if (p >= end || *p >= GEVR_ATTNET_SLOTS) return -1;
            r->slot = *p++;
            break;
        default:
            return -1;
        }
    }
    return p == end ? n : -1;
}

/* ---- the owner ---- */

static Owned *owned(int id)
{
    int i;

    for (i = 0; id > 0 && i < GEVR_ATTNET_OWNED; i++) {
        if (s_owned[i].id == id) return &s_owned[i];
    }
    return NULL;
}

/* a live one's state changed: to everyone (that covers any one player's) */
static void changed(Owned *o)
{
    o->pending = ALL;
}

int gevrAttachNetSet(const char *mod, const GevrAttachRecord *r)
{
    Owned *o;
    GevrAttachRecord s;
    int i;

    if (mod == NULL || !mod[0] || strlen(mod) >= sizeof(o->mod) || r == NULL) return 0;
    s = *r;
    s.op = GEVR_ATTNET_SET;
    s.slot = 0;
    if (recordBytes(&s) < 0) return 0;
    o = owned(s.id);
    if (o != NULL && !o->gone && !strcmp(o->mod, mod) && !memcmp(&o->r, &s, sizeof(s))) return 1;   /* no change */
    for (i = 0; o == NULL && i < GEVR_ATTNET_OWNED; i++) {
        if (s_owned[i].id == 0) o = &s_owned[i];
    }
    if (o == NULL) return 0;
    o->id = s.id;
    strcpy(o->mod, mod);
    o->r = s;
    o->gone = 0;
    changed(o);
    return 1;
}

int gevrAttachNetShow(int id, int shown)
{
    Owned *o = owned(id);

    if (o == NULL || o->gone) return 0;
    shown = shown != 0;
    if (o->r.shown != shown) {
        o->r.shown = shown;
        changed(o);
    }
    return 1;
}

int gevrAttachNetPlace(int id, const float frame[4][3])
{
    Owned *o = owned(id);
    GevrAttachRecord s;

    if (o == NULL || o->gone || frame == NULL) return 0;
    s = o->r;
    memcpy(s.frame, frame, sizeof(s.frame));
    if (recordBytes(&s) < 0) return 0;
    if (memcmp(s.frame, o->r.frame, sizeof(s.frame))) {
        o->r = s;
        changed(o);
    }
    return 1;
}

void gevrAttachNetRemove(int id)
{
    Owned *o = owned(id);

    if (o != NULL && !o->gone) {
        o->gone = 1;
        o->pending = ALL;
    }
}

void gevrAttachNetRefreshBody(int (*at)(int where, float out[3]))
{
    int i, k;

    for (i = 0; at != NULL && i < GEVR_ATTNET_OWNED; i++) {
        Owned *o = &s_owned[i];
        float now[3];
        int moved = 0;

        if (o->id == 0 || o->gone || o->r.on != GEVR_ATTNET_BODYSLOT || !at(o->r.where, now)) continue;
        for (k = 0; k < 3; k++) {
            if (!limit(now[k], GEVR_ATTNET_BODY)) break;
            moved |= fabsf(now[k] - o->r.body[k]) > 0.5f;
        }
        if (k == 3 && moved) {
            memcpy(o->r.body, now, sizeof(now));
            changed(o);
        }
    }
}

void gevrAttachNetReset(int tell)
{
    int i, k;

    for (i = 0; i < GEVR_ATTNET_OWNED; i++) {
        Owned *o = &s_owned[i];

        if (o->id == 0) continue;
        for (k = 0; tell && k < s_clears && strcmp(s_clear[k], o->mod); k++) {
        }
        if (tell && k == s_clears && s_clears < MODS) strcpy(s_clear[s_clears++], o->mod);
        o->id = 0;
        o->pending = 0;
        o->gone = 0;
    }
    if (!tell) s_clears = 0;
    s_ask = 0;
}

int gevrAttachNetWorn(void)
{
    int i;

    for (i = 0; i < GEVR_ATTNET_OWNED; i++) {
        if (s_owned[i].id != 0) return 1;
    }
    return 0;
}

void gevrAttachNetAnswer(int slot)
{
    int i;

    if (slot < 0 || slot >= GEVR_ATTNET_SLOTS) return;
    for (i = 0; i < GEVR_ATTNET_OWNED; i++) {
        if (s_owned[i].id != 0 && !s_owned[i].gone) s_owned[i].pending |= 1u << slot;
    }
}

void gevrAttachNetAskFor(const char *mod, int slot)
{
    if (mod == NULL || !mod[0] || strlen(mod) >= sizeof(s_askMod) || slot < 0 || slot >= GEVR_ATTNET_SLOTS) return;
    strcpy(s_askMod, mod);
    s_ask |= 1u << slot;
}

/* One message to dest (ALL or a slot's bit) of mod's records waiting for it:
 * as many as fit, their bits cleared (a removed one freed once told). 0 when
 * there's none. */
static int pack(unsigned dest, const char *mod, int stage, unsigned char *out, GevrAttachRecord *r)
{
    int i, n = 0, bytes = 2;

    for (i = 0; i < GEVR_ATTNET_OWNED && n < GEVR_ATTNET_RECORDS; i++) {
        Owned *o = &s_owned[i];
        int b;

        if (o->id == 0 || !(o->pending & dest) || strcmp(o->mod, mod)) continue;
        if (dest != ALL && (o->pending & ALL)) continue;   /* the broadcast carries it */
        r[n] = o->r;
        if (o->gone) {
            memset(&r[n], 0, sizeof(r[n]));
            r[n].op = GEVR_ATTNET_REMOVE;
            r[n].id = o->id;
        }
        b = recordBytes(&r[n]);
        if (bytes + b > GEVR_ATTNET_BYTES) break;
        bytes += b;
        n++;
        o->pending &= dest == ALL ? 0u : ~dest;
        if (o->gone && !o->pending) o->id = 0;
    }
    return n ? gevrAttachNetEncode(out, GEVR_ATTNET_BYTES, stage, r, n) : 0;
}

/* the first mod with something waiting for dest, or NULL */
static const char *waiting(unsigned dest)
{
    int i;

    for (i = 0; i < GEVR_ATTNET_OWNED; i++) {
        const Owned *o = &s_owned[i];

        if (o->id != 0 && (o->pending & dest) && (dest == ALL || !(o->pending & ALL))) return o->mod;
    }
    return NULL;
}

int gevrAttachNetPoll(unsigned long long nowUs, int stage, GevrAttachNetSend send)
{
    static unsigned char out[GEVR_ATTNET_BYTES];
    static GevrAttachRecord r[GEVR_ATTNET_RECORDS];
    int sent = 0, slot, len;

    if (send == NULL) return 0;
    if (nowUs < s_since || nowUs - s_since >= 1000000u) {
        s_since = nowUs;   /* a second at a time, as the host's relay counts */
        s_sends = 0;
    }
    /* a stop's clears first, then the host's asks, then the changes to all,
     * then what a player asked for */
    while (s_sends < GEVR_ATTNET_SENDS && s_clears > 0) {
        GevrAttachRecord c;

        memset(&c, 0, sizeof(c));
        c.op = GEVR_ATTNET_CLEAR;
        len = gevrAttachNetEncode(out, sizeof(out), stage, &c, 1);
        s_clears--;
        sent += send(0xFF, s_clear[s_clears], out, len) != 0;
        s_sends++;
    }
    if (s_sends < GEVR_ATTNET_SENDS && s_ask) {
        int n = 0;

        for (slot = 0; slot < GEVR_ATTNET_SLOTS; slot++) {
            if (s_ask & (1u << slot)) {
                memset(&r[n], 0, sizeof(r[n]));
                r[n].op = GEVR_ATTNET_ASK;
                r[n++].slot = slot;
            }
        }
        s_ask = 0;
        len = gevrAttachNetEncode(out, sizeof(out), stage, r, n);
        sent += send(0xFF, s_askMod, out, len) != 0;
        s_sends++;
    }
    for (slot = -1; slot < GEVR_ATTNET_SLOTS; slot++) {
        const unsigned dest = slot < 0 ? ALL : 1u << slot;
        const char *mod;

        while (s_sends < GEVR_ATTNET_SENDS && (mod = waiting(dest)) != NULL) {
            char name[64];

            strcpy(name, mod);   /* a removed one's own may be freed by pack */
            len = pack(dest, name, stage, out, r);
            if (len <= 0) break;
            sent += send(slot < 0 ? 0xFF : slot, name, out, len) != 0;
            s_sends++;
        }
    }
    return sent;
}
