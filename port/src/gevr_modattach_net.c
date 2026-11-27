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

/* the pack gun body each hand draws, as the others were told */
typedef struct {
    char mod[64];
    GevrAttachRecord r;                 /* a GUNBODY; item 0 the hand's own */
    unsigned pending;
    int told;                           /* the others may hold a pack body for this hand */
} Body;

static Owned s_owned[GEVR_ATTNET_OWNED];
static Body s_body[2];
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

/* an action's own timing: open and close 1 to GEVR_ATTNET_TICKS with an
 * ease from 0 to 1, or none (all 0) */
static int timingOk(int open, int close, float ease)
{
    if (open == 0 && close == 0) return ease == 0.0f;
    return open >= 1 && open <= GEVR_ATTNET_TICKS && close >= 1 && close <= GEVR_ATTNET_TICKS
        && isfinite(ease) && ease >= 0.0f && ease <= 1.0f;
}

/* a travel within the limits, zero where nothing moves */
static int travelOk(const float t[3], int moves)
{
    int k;

    for (k = 0; k < 3; k++) {
        if (!limit(t[k], GEVR_ATTNET_ORIGIN) || (!moves && t[k] != 0.0f)) return 0;
    }
    return 1;
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
    /* an optic only on a pack mesh on the gun; folded only with a housing;
     * a housing or a mode only with an optic */
    if ((r->optic != 0 && r->optic != 1) || (r->folded != 0 && r->folded != 1)) return 0;
    if (r->optic && (r->on != GEVR_ATTNET_GUN || r->like[0])) return 0;
    if (r->mode < 0 || r->mode >= GEVR_ATTNET_MODES || (!r->optic && (r->mode || r->housing[0]))) return 0;
    if (r->folded && !r->housing[0]) return 0;
    if (r->housing[0] && !gevrAttachNetNameOk(r->housing)) return 0;
    /* an action only on the gun; a handle or a timing only with one, not both */
    if ((r->moves != 0 && r->moves != 1) || (r->handle != 0 && r->handle != 1) || (r->timed != 0 && r->timed != 1)) return 0;
    if ((r->moves && r->on != GEVR_ATTNET_GUN) || (!r->moves && (r->handle || r->timed)) || (r->handle && r->timed)) return 0;
    if (!travelOk(r->travel, r->moves) || !timingOk(r->open, r->close, r->ease) || r->timed != (r->open != 0)) return 0;
    return gevrAttachNetNameOk(r->model) && (!r->like[0] || gevrAttachNetNameOk(r->like));
}

/* a GUNBODY's fields within the limits: a gun's body or the hand's own */
static int gunBodyOk(const GevrAttachRecord *r)
{
    int i, j;

    if (r->where < 0 || r->where > 1 || r->item < 0 || r->item > 255) return 0;
    if ((r->fitted != 0 && r->fitted != 1) || (r->muzzled != 0 && r->muzzled != 1)) return 0;
    for (i = 0; i < 4; i++) {
        for (j = 0; j < 3; j++) {
            if (!limit(r->frame[i][j], i == 3 ? GEVR_ATTNET_ORIGIN : GEVR_ATTNET_AXIS)) return 0;
        }
    }
    for (i = 0; i < 3; i++) {
        if (!limit(r->muzzle[i], GEVR_ATTNET_ORIGIN)) return 0;
    }
    /* its moving parts: the groups' pictures apart, a travel only for a
     * group with pictures, a timing only for the action's; all zero without */
    if ((r->parts != 0 && r->parts != 1) || (r->holdOpen != 0 && r->holdOpen != 1)) return 0;
    if (!r->parts && (r->holdOpen || r->pictures[0] || r->pictures[1])) return 0;
    if (r->pictures[0] & r->pictures[1]) return 0;
    for (i = 0; i < 2; i++) {
        if (!travelOk(r->partTravel[i], r->pictures[i] != 0)) return 0;
    }
    if (!timingOk(r->open, r->close, r->ease) || (r->open && !r->pictures[0])) return 0;
    if (r->item == 0) return !r->fitted && !r->muzzled && !r->parts && !r->model[0];
    return gevrAttachNetNameOk(r->model);
}

static int recordBytes(const GevrAttachRecord *r)
{
    switch (r->op) {
    case GEVR_ATTNET_SET:
        return setOk(r) ? GEVR_ATTNET_SET_BYTES + (int)strlen(r->model) + (int)strlen(r->like) + (int)strlen(r->housing) : -1;
    case GEVR_ATTNET_REMOVE:
        return r->id >= 1 ? 5 : -1;
    case GEVR_ATTNET_CLEAR:
        return 1;
    case GEVR_ATTNET_ASK:
        return r->slot >= 0 && r->slot < GEVR_ATTNET_SLOTS ? 2 : -1;
    case GEVR_ATTNET_GUNBODY:
        return gunBodyOk(r) ? GEVR_ATTNET_GUNBODY_BYTES + (int)strlen(r->model) : -1;
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
            *p++ = (unsigned char)((r[i].shown ? 1 : 0) | (r[i].optic ? 2 : 0) | (r[i].folded ? 4 : 0)
                                   | (r[i].moves ? 8 : 0) | (r[i].handle ? 16 : 0) | (r[i].timed ? 32 : 0));
            for (j = 0; j < 4; j++) {
                for (k = 0; k < 3; k++) p = putFloat(p, r[i].frame[j][k]);
            }
            for (k = 0; k < 3; k++) p = putFloat(p, r[i].body[k]);
            for (k = 0; k < 3; k++) p = putFloat(p, r[i].travel[k]);
            *p++ = (unsigned char)r[i].open;
            *p++ = (unsigned char)r[i].close;
            p = putFloat(p, r[i].ease);
            *p++ = (unsigned char)r[i].mode;
            p = putName(p, r[i].model);
            p = putName(p, r[i].like);
            p = putName(p, r[i].housing);
            break;
        case GEVR_ATTNET_GUNBODY:
            *p++ = (unsigned char)r[i].where;
            *p++ = (unsigned char)r[i].item;
            *p++ = (unsigned char)((r[i].fitted ? 1 : 0) | (r[i].muzzled ? 2 : 0) | (r[i].parts ? 4 : 0)
                                   | (r[i].holdOpen ? 8 : 0));
            for (j = 0; j < 4; j++) {
                for (k = 0; k < 3; k++) p = putFloat(p, r[i].frame[j][k]);
            }
            for (k = 0; k < 3; k++) p = putFloat(p, r[i].muzzle[k]);
            for (j = 0; j < 2; j++) p = put32(p, r[i].pictures[j]);
            for (j = 0; j < 2; j++) {
                for (k = 0; k < 3; k++) p = putFloat(p, r[i].partTravel[j][k]);
            }
            *p++ = (unsigned char)r[i].open;
            *p++ = (unsigned char)r[i].close;
            p = putFloat(p, r[i].ease);
            p = putName(p, r[i].model);
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
            if (end - p < GEVR_ATTNET_SET_BYTES - 4) return -1;   /* less the op and the names' lengths */
            {
                const unsigned id = get32(p);

                if (id < 1 || id > 0x7FFFFFFFu) return -1;
                r->id = (int)id;
            }
            p += 4;
            r->on = p[0];
            r->where = p[1];
            r->node = p[2];
            if (p[3] > 63) return -1;
            r->shown = p[3] & 1;
            r->optic = (p[3] >> 1) & 1;
            r->folded = (p[3] >> 2) & 1;
            r->moves = (p[3] >> 3) & 1;
            r->handle = (p[3] >> 4) & 1;
            r->timed = (p[3] >> 5) & 1;
            p += 4;
            for (j = 0; j < 4; j++) {
                for (k = 0; k < 3; k++, p += 4) r->frame[j][k] = getFloat(p);
            }
            for (k = 0; k < 3; k++, p += 4) r->body[k] = getFloat(p);
            for (k = 0; k < 3; k++, p += 4) r->travel[k] = getFloat(p);
            r->open = *p++;
            r->close = *p++;
            r->ease = getFloat(p);
            p += 4;
            r->mode = *p++;
            if (!getName(&p, end, r->model) || !getName(&p, end, r->like) || !getName(&p, end, r->housing)
                || !setOk(r)) return -1;
            break;
        case GEVR_ATTNET_GUNBODY:
            if (end - p < GEVR_ATTNET_GUNBODY_BYTES - 2) return -1;   /* less the op and the name's length */
            r->where = p[0];
            r->item = p[1];
            if (p[2] > 15) return -1;
            r->fitted = p[2] & 1;
            r->muzzled = (p[2] >> 1) & 1;
            r->parts = (p[2] >> 2) & 1;
            r->holdOpen = (p[2] >> 3) & 1;
            p += 3;
            for (j = 0; j < 4; j++) {
                for (k = 0; k < 3; k++, p += 4) r->frame[j][k] = getFloat(p);
            }
            for (k = 0; k < 3; k++, p += 4) r->muzzle[k] = getFloat(p);
            for (j = 0; j < 2; j++, p += 4) r->pictures[j] = get32(p);
            for (j = 0; j < 2; j++) {
                for (k = 0; k < 3; k++, p += 4) r->partTravel[j][k] = getFloat(p);
            }
            r->open = *p++;
            r->close = *p++;
            r->ease = getFloat(p);
            p += 4;
            if (!getName(&p, end, r->model) || !gunBodyOk(r)) return -1;
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

int gevrAttachNetAction(int id, int moves, int handle, const float travel[3], int open, int close, float ease)
{
    Owned *o = owned(id);
    GevrAttachRecord s;

    if (o == NULL || o->gone) return 0;
    s = o->r;
    memset(s.travel, 0, sizeof(s.travel));
    s.moves = moves != 0 && travel != NULL;
    s.handle = s.moves && handle != 0;
    s.timed = s.moves && !s.handle && open > 0;
    s.open = s.timed ? open : 0;
    s.close = s.timed ? close : 0;
    s.ease = s.timed ? ease : 0.0f;
    if (s.moves) memcpy(s.travel, travel, sizeof(s.travel));
    if (recordBytes(&s) < 0) return 0;
    if (memcmp(&s, &o->r, sizeof(s))) {
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

void gevrAttachNetRefreshOptic(int (*state)(int id, int *mode, int *folded, char housing[GEVR_ATTNET_NAME + 1]))
{
    int i;

    for (i = 0; state != NULL && i < GEVR_ATTNET_OWNED; i++) {
        Owned *o = &s_owned[i];
        GevrAttachRecord s;

        if (o->id == 0 || o->gone || o->r.on != GEVR_ATTNET_GUN || o->r.like[0]) continue;
        s = o->r;
        s.mode = s.folded = 0;
        memset(s.housing, 0, sizeof(s.housing));
        s.optic = state(o->id, &s.mode, &s.folded, s.housing) != 0;
        s.housing[GEVR_ATTNET_NAME] = '\0';
        s.folded = s.folded != 0;
        if (!s.optic || recordBytes(&s) < 0) {
            /* none bound, or not one to tell (a mode or housing out of bounds): none */
            s.optic = s.mode = s.folded = 0;
            memset(s.housing, 0, sizeof(s.housing));
        }
        if (s.optic != o->r.optic || s.mode != o->r.mode || s.folded != o->r.folded || strcmp(s.housing, o->r.housing)) {
            o->r = s;
            changed(o);
        }
    }
}

int gevrAttachNetGunBody(const char *mod, int hand, const GevrAttachRecord *r)
{
    Body *b;
    GevrAttachRecord s;

    if (hand < 0 || hand > 1) return 0;
    b = &s_body[hand];
    memset(&s, 0, sizeof(s));
    s.op = GEVR_ATTNET_GUNBODY;
    s.where = hand;
    if (r != NULL) {
        if (mod == NULL || !mod[0] || strlen(mod) >= sizeof(b->mod)) return 0;
        s.item = r->item;
        s.fitted = r->fitted != 0;
        s.muzzled = r->muzzled != 0;
        if (s.fitted) memcpy(s.frame, r->frame, sizeof(s.frame));
        if (s.muzzled) memcpy(s.muzzle, r->muzzle, sizeof(s.muzzle));
        s.parts = r->parts != 0;
        if (s.parts) {
            /* its moving parts, as mod.weapon.model gave them; a timing only with one */
            s.holdOpen = r->holdOpen != 0;
            memcpy(s.pictures, r->pictures, sizeof(s.pictures));
            memcpy(s.partTravel, r->partTravel, sizeof(s.partTravel));
            if (r->open > 0) {
                s.open = r->open;
                s.close = r->close;
                s.ease = r->ease;
            }
        }
        if (strlen(r->model) > GEVR_ATTNET_NAME) return 0;
        strcpy(s.model, r->model);
        if (s.item < 1 || recordBytes(&s) < 0) return 0;
    } else if (!b->told || !b->r.item) {
        return 1;   /* the hand's own, and the others hold (or are told) nothing else */
    }
    if (b->told && !memcmp(&b->r, &s, sizeof(s)) && !strcmp(b->mod, r != NULL ? mod : b->mod)) return 1;   /* no change */
    if (r != NULL) strcpy(b->mod, mod);
    b->r = s;
    b->told = 1;
    b->pending = ALL;
    return 1;
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
    for (i = 0; i < 2; i++) {
        Body *b = &s_body[i];

        for (k = 0; tell && b->told && k < s_clears && strcmp(s_clear[k], b->mod); k++) {
        }
        if (tell && b->told && k == s_clears && s_clears < MODS) strcpy(s_clear[s_clears++], b->mod);
        memset(b, 0, sizeof(*b));
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
    return s_body[0].told || s_body[1].told;
}

void gevrAttachNetAnswer(int slot)
{
    int i;

    if (slot < 0 || slot >= GEVR_ATTNET_SLOTS) return;
    for (i = 0; i < GEVR_ATTNET_OWNED; i++) {
        if (s_owned[i].id != 0 && !s_owned[i].gone) s_owned[i].pending |= 1u << slot;
    }
    for (i = 0; i < 2; i++) {
        if (s_body[i].told && s_body[i].r.item) s_body[i].pending |= 1u << slot;   /* a pack body, not the hand's own */
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

    /* the hands' gun bodies first: what the parts on the gun are fitted to */
    for (i = 0; i < 2; i++) {
        Body *b = &s_body[i];

        if (!b->told || !(b->pending & dest) || strcmp(b->mod, mod)) continue;
        if (dest != ALL && (b->pending & ALL)) continue;   /* the broadcast carries it */
        r[n] = b->r;
        bytes += recordBytes(&r[n]);
        n++;
        b->pending &= dest == ALL ? 0u : ~dest;
        if (!b->pending && !b->r.item) b->told = 0;   /* the hand's own, told to everyone: nothing held */
    }

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

    for (i = 0; i < 2; i++) {
        const Body *b = &s_body[i];

        if (b->told && (b->pending & dest) && (dest == ALL || !(b->pending & ALL))) return b->mod;
    }
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
