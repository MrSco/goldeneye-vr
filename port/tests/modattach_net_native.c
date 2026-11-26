/*
 * mod.attach on the other players' headsets (#335): port/src/gevr_modattach_net.c
 * as it is. The owner's side runs here; each message it sends is decoded and
 * applied to a mirror, as another headset's copy of the player would hold it,
 * so the checks read what the other player sees: a flip reaching it, a
 * player who joins getting the whole state, a removal (a gun swap, a detach,
 * a stop) reaching it, the rate and the packing; and every malformed message
 * refused whole. Then an optic's housing state riding its SET (bound, the
 * mode, folded, the folded housing's file) and each hand's pack gun body
 * (GUNBODY): set, reset to the hand's own, a swap, a join, a stop.
 */
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "gevr_modattach_net.h"

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #x); return 1; } } while (0)

/* what another headset holds of this player: by id */
typedef struct {
    int live;
    GevrAttachRecord r;
} Mirror;
static Mirror s_mirror[8][256];   /* by receiver slot: each one's own copy */
static GevrAttachRecord s_bodies[8][2];   /* and the pack gun body it draws in each hand (item 0 the own) */
static int s_asked[8];            /* ASKs heard, by the slot asked for */
static int s_sent, s_bytes, s_lastTo, s_cleared;
static char s_lastMod[64];
static unsigned long long s_now = 5000000;

static Mirror *mirrorOf(int who, int id)
{
    int i;

    for (i = 0; i < 256; i++) {
        if (s_mirror[who][i].live && s_mirror[who][i].r.id == id) return &s_mirror[who][i];
    }
    for (i = 0; i < 256; i++) {
        if (!s_mirror[who][i].live) return &s_mirror[who][i];
    }
    return NULL;
}

static void applyTo(int who, const GevrAttachRecord *r, int n)
{
    int i, k;

    for (i = 0; i < n; i++) {
        if (r[i].op == GEVR_ATTNET_SET) {
            Mirror *m = mirrorOf(who, r[i].id);

            m->live = 1;
            m->r = r[i];
        } else if (r[i].op == GEVR_ATTNET_REMOVE) {
            Mirror *m = mirrorOf(who, r[i].id);

            if (m->live && m->r.id == r[i].id) m->live = 0;
        } else if (r[i].op == GEVR_ATTNET_CLEAR) {
            for (k = 0; k < 256; k++) s_mirror[who][k].live = 0;
            memset(s_bodies[who], 0, sizeof(s_bodies[who]));
            s_cleared++;
        } else if (r[i].op == GEVR_ATTNET_GUNBODY) {
            s_bodies[who][r[i].where] = r[i];
        } else if (r[i].op == GEVR_ATTNET_ASK) {
            s_asked[r[i].slot]++;
        }
    }
}

/* the network: everyone but this headset (slot 0), or one */
static int sendFn(int to, const char *mod, const void *data, int len)
{
    static GevrAttachRecord r[GEVR_ATTNET_RECORDS];
    int stage, n, who;

    n = gevrAttachNetDecode((const unsigned char *)data, len, &stage, r);
    assert(n > 0 && stage == 9 && len <= GEVR_ATTNET_BYTES);
    s_sent++;
    s_bytes += len;
    s_lastTo = to;
    snprintf(s_lastMod, sizeof(s_lastMod), "%s", mod);
    for (who = 1; who < 8; who++) {
        if (to == 0xFF || to == who) applyTo(who, r, n);
    }
    return 1;
}

static int poll(void)
{
    s_now += 16667;
    return gevrAttachNetPoll(s_now, 9, sendFn);
}

static GevrAttachRecord part(int id, int on, int where, const char *model)
{
    GevrAttachRecord r;
    int i;

    memset(&r, 0, sizeof(r));
    r.op = GEVR_ATTNET_SET;
    r.id = id;
    r.on = on;
    r.where = where;
    r.shown = 1;
    for (i = 0; i < 3; i++) r.frame[i][i] = 1.0f;
    r.frame[3][1] = 90.0f;
    if (on == GEVR_ATTNET_BODYSLOT) {
        r.body[0] = 65.0f;
        r.body[1] = 20.0f;
        r.body[2] = -10.0f;
    }
    snprintf(r.model, sizeof(r.model), "%s", model);
    snprintf(r.like, sizeof(r.like), "%s", strstr(model, ".bin") ? "" : "PcartonZ");
    return r;
}

static int has(int who, int id)
{
    int i;

    for (i = 0; i < 256; i++) {
        if (s_mirror[who][i].live && s_mirror[who][i].r.id == id) return 1;
    }
    return 0;
}

static int live(int who)
{
    int i, n = 0;

    for (i = 0; i < 256; i++) n += s_mirror[who][i].live;
    return n;
}

/* the same as the owner's (what the other player sees) */
static int same(int who, const GevrAttachRecord *r)
{
    Mirror *m = mirrorOf(who, r->id);

    return m != NULL && m->live && m->r.id == r->id && m->r.shown == r->shown && m->r.on == r->on
        && m->r.where == r->where && m->r.node == r->node && !strcmp(m->r.model, r->model)
        && !strcmp(m->r.like, r->like) && !memcmp(m->r.frame, r->frame, sizeof(r->frame))
        && !memcmp(m->r.body, r->body, sizeof(r->body)) && m->r.optic == r->optic && m->r.mode == r->mode
        && m->r.folded == r->folded && !strcmp(m->r.housing, r->housing);
}

/* the gun body the other player sees in that hand */
static int sameBody(int who, const GevrAttachRecord *r)
{
    const GevrAttachRecord *b = &s_bodies[who][r->where];

    return b->op == GEVR_ATTNET_GUNBODY && b->item == r->item && b->fitted == r->fitted && b->muzzled == r->muzzled
        && !strcmp(b->model, r->model) && !memcmp(b->frame, r->frame, sizeof(r->frame))
        && !memcmp(b->muzzle, r->muzzle, sizeof(r->muzzle));
}

static void settle(void)
{
    int i;

    for (i = 0; i < 120; i++) poll();   /* two seconds of frames */
}

static int wire(void)
{
    unsigned char out[GEVR_ATTNET_BYTES + 64], bad[GEVR_ATTNET_BYTES + 64];
    GevrAttachRecord in[4], got[GEVR_ATTNET_RECORDS];
    int len, stage, n, i;

    in[0] = part(7, GEVR_ATTNET_GUN, 1, "GtacAcogZ");
    in[0].node = 12;
    in[0].shown = 0;
    in[0].frame[3][0] = -4000.0f;
    in[0].frame[0][2] = -100.0f;
    in[1] = part(0x7FFFFFFF, GEVR_ATTNET_BODYSLOT, 5, "mag-pouch.bin");
    memset(&in[2], 0, sizeof(in[2]));
    in[2].op = GEVR_ATTNET_REMOVE;
    in[2].id = 3;
    memset(&in[3], 0, sizeof(in[3]));
    in[3].op = GEVR_ATTNET_ASK;
    in[3].slot = 7;
    len = gevrAttachNetEncode(out, sizeof(out), 9, in, 4);
    /* the layout: stage, count; a SET's op, id little-endian, on, where, node, flags, 15 floats, mode, names */
    CHECK(len == 2 + (GEVR_ATTNET_SET_BYTES + 9 + 8) + (GEVR_ATTNET_SET_BYTES + 13) + 5 + 2);
    CHECK(out[0] == 9 && out[1] == 4 && out[2] == GEVR_ATTNET_SET && out[3] == 7 && out[4] == 0 && out[5] == 0 && out[6] == 0);
    CHECK(out[7] == GEVR_ATTNET_GUN && out[8] == 1 && out[9] == 12 && out[10] == 0);
    CHECK(out[2 + GEVR_ATTNET_SET_BYTES - 4] == 0 && out[2 + GEVR_ATTNET_SET_BYTES - 3] == 9
          && !memcmp(out + 2 + GEVR_ATTNET_SET_BYTES - 2, "GtacAcogZ", 9));
    n = gevrAttachNetDecode(out, len, &stage, got);
    CHECK(n == 4 && stage == 9);
    CHECK(got[0].op == GEVR_ATTNET_SET && got[0].id == 7 && got[0].node == 12 && !got[0].shown && got[0].where == 1
          && got[0].frame[3][0] == -4000.0f && got[0].frame[0][2] == -100.0f && !strcmp(got[0].model, "GtacAcogZ")
          && !strcmp(got[0].like, "PcartonZ"));
    CHECK(got[1].id == 0x7FFFFFFF && got[1].on == GEVR_ATTNET_BODYSLOT && got[1].where == 5 && got[1].body[0] == 65.0f
          && !strcmp(got[1].model, "mag-pouch.bin") && !got[1].like[0] && got[1].shown);
    CHECK(got[2].op == GEVR_ATTNET_REMOVE && got[2].id == 3 && got[3].op == GEVR_ATTNET_ASK && got[3].slot == 7);

    /* refused whole: each one byte or field wrong */
    memcpy(bad, out, (size_t)len);
    CHECK(gevrAttachNetDecode(bad, len - 1, NULL, got) < 0);             /* missing a byte */
    bad[len] = 0;
    CHECK(gevrAttachNetDecode(bad, len + 1, NULL, got) < 0);             /* a byte left over */
    bad[1] = 5;
    CHECK(gevrAttachNetDecode(bad, len, NULL, got) < 0);                 /* a record more than there is */
    bad[1] = 0;
    CHECK(gevrAttachNetDecode(bad, len, NULL, got) < 0);                 /* none */
    bad[1] = 4;
    bad[2] = 9;
    CHECK(gevrAttachNetDecode(bad, len, NULL, got) < 0);                 /* an unknown op */
    bad[2] = GEVR_ATTNET_SET;
    bad[3] = bad[4] = bad[5] = bad[6] = 0;
    CHECK(gevrAttachNetDecode(bad, len, NULL, got) < 0);                 /* id 0 */
    bad[6] = 0x80;
    CHECK(gevrAttachNetDecode(bad, len, NULL, got) < 0);                 /* past 0x7FFFFFFF */
    bad[3] = 7;
    bad[6] = 0;
    CHECK(gevrAttachNetDecode(bad, len, NULL, got) == 4);                /* back as it was */
    bad[7] = 3;
    CHECK(gevrAttachNetDecode(bad, len, NULL, got) < 0);                 /* on: no such anchor */
    bad[7] = GEVR_ATTNET_GUN;
    bad[8] = 2;
    CHECK(gevrAttachNetDecode(bad, len, NULL, got) < 0);                 /* a third hand */
    bad[8] = 1;
    bad[10] = 8;
    CHECK(gevrAttachNetDecode(bad, len, NULL, got) < 0);                 /* flags past shown, optic, folded */
    bad[10] = 2;
    CHECK(gevrAttachNetDecode(bad, len, NULL, got) < 0);                 /* an optic on a cartridge-built part */
    bad[10] = 0;
    {
        static const float nope[] = { NAN, INFINITY, 100.5f, -101.0f };

        for (i = 0; i < 4; i++) {
            memcpy(bad + 11, &nope[i], 4);                                  /* the X axis's x */
            CHECK(gevrAttachNetDecode(bad, len, NULL, got) < 0);
        }
        memcpy(bad + 11, &in[0].frame[0][0], 4);
        memcpy(bad + 11 + 36, &(float){ 4000.5f }, 4);                   /* the origin's x */
        CHECK(gevrAttachNetDecode(bad, len, NULL, got) < 0);
        memcpy(bad + 11 + 36, &in[0].frame[3][0], 4);
        memcpy(bad + 11 + 48, &(float){ 250.5f }, 4);                    /* a body centre's below */
        CHECK(gevrAttachNetDecode(bad, len, NULL, got) < 0);
        memcpy(bad + 11 + 48, &in[0].body[0], 4);
        CHECK(gevrAttachNetDecode(bad, len, NULL, got) == 4);
    }
    /* the names: none, a path, a dot dot, a NUL inside, a length past the end, a bad like */
    {
        const int at = 2 + GEVR_ATTNET_SET_BYTES - 3;   /* the model's length byte */
        static const char *const names[] = { "../GtacZZ", "Gt/acZZZZ", "Gt\\acZZZZ", "Gt.acZZZZ", "Gt acZZZZ", "Gt:acZZZZ" };

        for (i = 0; i < 6; i++) {
            memcpy(bad, out, (size_t)len);
            assert(strlen(names[i]) == 9);
            memcpy(bad + at + 1, names[i], 9);   /* in the place of "GtacAcogZ" */
            CHECK(gevrAttachNetDecode(bad, len, NULL, got) < 0);
        }
        memcpy(bad, out, (size_t)len);
        bad[at + 3] = 0;
        CHECK(gevrAttachNetDecode(bad, len, NULL, got) < 0);             /* a NUL inside */
        memcpy(bad, out, (size_t)len);
        bad[at] = 200;
        CHECK(gevrAttachNetDecode(bad, len, NULL, got) < 0);             /* past the end */
        memcpy(bad, out, (size_t)len);
        bad[at + 1 + 9 + 1] = '.';                                       /* the like: ".cartonZ" */
        CHECK(gevrAttachNetDecode(bad, len, NULL, got) < 0);
    }
    /* a body anchor past the six; a node off the gun; an ASK for a ninth player */
    in[1].where = GEVR_ATTNET_ANCHORS;
    CHECK(gevrAttachNetEncode(out, sizeof(out), 9, &in[1], 1) == 0);
    in[1].where = 5;
    in[1].node = 1;
    CHECK(gevrAttachNetEncode(out, sizeof(out), 9, &in[1], 1) == 0);
    in[1].node = 0;
    in[3].slot = 8;
    CHECK(gevrAttachNetEncode(out, sizeof(out), 9, &in[3], 1) == 0);
    memset(bad, 0, sizeof(bad));
    bad[1] = 1;
    bad[2] = GEVR_ATTNET_ASK;
    bad[3] = 8;
    CHECK(gevrAttachNetDecode(bad, 4, NULL, got) < 0);
    bad[3] = 3;
    CHECK(gevrAttachNetDecode(bad, 4, NULL, got) == 1 && got[0].slot == 3);
    /* the most bytes there may be, and one past */
    CHECK(gevrAttachNetDecode(bad, GEVR_ATTNET_BYTES + 1, NULL, got) < 0);
    {
        GevrAttachRecord many[GEVR_ATTNET_RECORDS + 1];

        for (i = 0; i <= GEVR_ATTNET_RECORDS; i++) {
            memset(&many[i], 0, sizeof(many[i]));
            many[i].op = GEVR_ATTNET_CLEAR;
        }
        CHECK(gevrAttachNetEncode(out, sizeof(out), 9, many, GEVR_ATTNET_RECORDS) == 2 + GEVR_ATTNET_RECORDS);
        CHECK(gevrAttachNetEncode(out, sizeof(out), 9, many, GEVR_ATTNET_RECORDS + 1) == 0);
        char longest[GEVR_ATTNET_NAME + 2];

        memset(longest, 'G', sizeof(longest));
        longest[GEVR_ATTNET_NAME + 1] = '\0';
        CHECK(!gevrAttachNetNameOk(longest));   /* 64: one past */
        longest[GEVR_ATTNET_NAME] = '\0';
        CHECK(gevrAttachNetNameOk(longest));
        for (i = 0; i < GEVR_ATTNET_RECORDS; i++) many[i] = part(i + 1, GEVR_ATTNET_GUN, 0, longest);
        CHECK(gevrAttachNetEncode(out, sizeof(out), 9, many, 7) == 2 + 7 * (GEVR_ATTNET_SET_BYTES + 63 + 8));
        CHECK(gevrAttachNetEncode(out, sizeof(out), 9, many, 8) == 0);   /* past GEVR_ATTNET_BYTES */
    }
    CHECK(gevrAttachNetNameOk("GtacAcogZ") && gevrAttachNetNameOk("part.bin") && !gevrAttachNetNameOk("")
          && !gevrAttachNetNameOk(".bin.bin") && !gevrAttachNetNameOk("a.b") && !gevrAttachNetNameOk(NULL));
    return 0;
}

/* an optic's housing state in its SET, and a GUNBODY: bytes and refusals */
static int wireOptic(void)
{
    unsigned char out[GEVR_ATTNET_BYTES], bad[GEVR_ATTNET_BYTES];
    GevrAttachRecord in[3], got[GEVR_ATTNET_RECORDS];
    int len, n, i, at;

    in[0] = part(4, GEVR_ATTNET_GUN, 0, "hhs1.bin");
    in[0].optic = 1;
    in[0].mode = 2;
    in[0].folded = 1;
    strcpy(in[0].housing, "hhs1-folded.bin");
    memset(&in[1], 0, sizeof(in[1]));
    in[1].op = GEVR_ATTNET_GUNBODY;
    in[1].where = 1;
    in[1].item = 0x2A;
    in[1].fitted = in[1].muzzled = 1;
    for (i = 0; i < 3; i++) in[1].frame[i][i] = 2.0f;
    in[1].frame[3][2] = -4000.0f;
    in[1].muzzle[1] = 4000.0f;
    strcpy(in[1].model, "kelly-pp7.bin");
    memset(&in[2], 0, sizeof(in[2]));
    in[2].op = GEVR_ATTNET_GUNBODY;   /* the other hand's own again */
    len = gevrAttachNetEncode(out, sizeof(out), 9, in, 3);
    CHECK(len == 2 + (GEVR_ATTNET_SET_BYTES + 8 + 15) + (GEVR_ATTNET_GUNBODY_BYTES + 13) + GEVR_ATTNET_GUNBODY_BYTES);
    CHECK(out[10] == 7 && out[2 + GEVR_ATTNET_SET_BYTES - 4] == 2);    /* shown|optic|folded; the mode */
    at = 2 + GEVR_ATTNET_SET_BYTES - 3 + 1 + 8 + 1;                   /* the housing's length byte */
    CHECK(out[at] == 15 && !memcmp(out + at + 1, "hhs1-folded.bin", 15));
    at += 16;                                                         /* the GUNBODY */
    CHECK(out[at] == GEVR_ATTNET_GUNBODY && out[at + 1] == 1 && out[at + 2] == 0x2A && out[at + 3] == 3
          && out[at + 64] == 13 && !memcmp(out + at + 65, "kelly-pp7.bin", 13));
    n = gevrAttachNetDecode(out, len, NULL, got);
    CHECK(n == 3 && got[0].optic && got[0].mode == 2 && got[0].folded && got[0].shown && !strcmp(got[0].housing, "hhs1-folded.bin"));
    CHECK(got[1].op == GEVR_ATTNET_GUNBODY && got[1].where == 1 && got[1].item == 0x2A && got[1].fitted && got[1].muzzled
          && got[1].frame[1][1] == 2.0f && got[1].frame[3][2] == -4000.0f && got[1].muzzle[1] == 4000.0f
          && !strcmp(got[1].model, "kelly-pp7.bin"));
    CHECK(got[2].op == GEVR_ATTNET_GUNBODY && got[2].where == 0 && got[2].item == 0 && !got[2].model[0]);

    /* the optic's refusals, encoding: on a cartridge-built part, on a hand,
     * folded without a housing, a housing or a mode with no optic, a mode
     * past the profile's twelve, a housing that isn't a flat name */
    {
        GevrAttachRecord r = in[0];

        strcpy(r.like, "PcartonZ");
        CHECK(gevrAttachNetEncode(bad, sizeof(bad), 9, &r, 1) == 0);
        r = in[0];
        r.on = GEVR_ATTNET_HAND;
        CHECK(gevrAttachNetEncode(bad, sizeof(bad), 9, &r, 1) == 0);
        r = in[0];
        r.housing[0] = '\0';
        CHECK(gevrAttachNetEncode(bad, sizeof(bad), 9, &r, 1) == 0);
        r.folded = 0;
        CHECK(gevrAttachNetEncode(bad, sizeof(bad), 9, &r, 1) > 0);       /* bound, no housing: fine */
        r = in[0];
        r.optic = r.folded = 0;
        CHECK(gevrAttachNetEncode(bad, sizeof(bad), 9, &r, 1) == 0);
        r.housing[0] = '\0';
        CHECK(gevrAttachNetEncode(bad, sizeof(bad), 9, &r, 1) == 0);      /* the mode, still */
        r.mode = 0;
        CHECK(gevrAttachNetEncode(bad, sizeof(bad), 9, &r, 1) > 0);
        r = in[0];
        r.mode = GEVR_ATTNET_MODES;
        CHECK(gevrAttachNetEncode(bad, sizeof(bad), 9, &r, 1) == 0);
        r.mode = GEVR_ATTNET_MODES - 1;
        CHECK(gevrAttachNetEncode(bad, sizeof(bad), 9, &r, 1) > 0);
        strcpy(r.housing, "../hhs1.bin");
        CHECK(gevrAttachNetEncode(bad, sizeof(bad), 9, &r, 1) == 0);
    }
    /* and decoding: each one byte wrong refuses the whole message */
    at = 2 + GEVR_ATTNET_SET_BYTES - 4;                               /* the mode */
    memcpy(bad, out, (size_t)len);
    bad[at] = GEVR_ATTNET_MODES;
    CHECK(gevrAttachNetDecode(bad, len, NULL, got) < 0);
    memcpy(bad, out, (size_t)len);
    bad[10] = 8 | 7;
    CHECK(gevrAttachNetDecode(bad, len, NULL, got) < 0);
    memcpy(bad, out, (size_t)len);
    bad[10] = 1 | 4;                                                  /* folded, no optic */
    CHECK(gevrAttachNetDecode(bad, len, NULL, got) < 0);
    memcpy(bad, out, (size_t)len);
    bad[2 + GEVR_ATTNET_SET_BYTES - 3 + 1 + 8 + 1 + 3] = '/';         /* "hhs/..." */
    CHECK(gevrAttachNetDecode(bad, len, NULL, got) < 0);
    memcpy(bad, out, (size_t)len);
    bad[2 + GEVR_ATTNET_SET_BYTES - 3 + 1 + 8 + 1] = 63;              /* the housing past the end */
    CHECK(gevrAttachNetDecode(bad, len, NULL, got) < 0);

    /* the GUNBODY's: a third hand, flags past fit and muzzle, the hand's own
     * with a name or a fit, a body with no name, a bad name, floats past the
     * limits or not finite, a byte short */
    at = 2 + (GEVR_ATTNET_SET_BYTES + 8 + 15);
    {
        static const int field[][2] = { { 1, 2 }, { 3, 4 } };
        static const float nope[] = { NAN, INFINITY, 100.5f, -101.0f };

        for (i = 0; i < 2; i++) {
            memcpy(bad, out, (size_t)len);
            bad[at + field[i][0]] = (unsigned char)field[i][1];
            CHECK(gevrAttachNetDecode(bad, len, NULL, got) < 0);
        }
        for (i = 0; i < 4; i++) {
            memcpy(bad, out, (size_t)len);
            memcpy(bad + at + 4, &nope[i], 4);                          /* the fit's X axis x */
            CHECK(gevrAttachNetDecode(bad, len, NULL, got) < 0);
        }
        memcpy(bad, out, (size_t)len);
        memcpy(bad + at + 4 + 36, &(float){ 4000.5f }, 4);              /* its origin */
        CHECK(gevrAttachNetDecode(bad, len, NULL, got) < 0);
        memcpy(bad, out, (size_t)len);
        memcpy(bad + at + 4 + 48, &(float){ -4000.5f }, 4);             /* the muzzle */
        CHECK(gevrAttachNetDecode(bad, len, NULL, got) < 0);
        memcpy(bad, out, (size_t)len);
        memcpy(bad + at + 4 + 48, &(float){ NAN }, 4);
        CHECK(gevrAttachNetDecode(bad, len, NULL, got) < 0);
        memcpy(bad, out, (size_t)len);
        bad[at + 65] = '.';                                             /* ".elly-pp7.bin" */
        CHECK(gevrAttachNetDecode(bad, len, NULL, got) < 0);
        memcpy(bad, out, (size_t)len);
        bad[at + 2] = 0;                                                /* the hand's own, named */
        CHECK(gevrAttachNetDecode(bad, len, NULL, got) < 0);
        CHECK(gevrAttachNetDecode(out, len - 1, NULL, got) < 0);
        /* the hand's own with a fit flag; a body with no name */
        memcpy(bad, out, (size_t)len);
        bad[len - GEVR_ATTNET_GUNBODY_BYTES + 3] = 1;
        CHECK(gevrAttachNetDecode(bad, len, NULL, got) < 0);
        memcpy(bad, out, (size_t)len);
        bad[len - GEVR_ATTNET_GUNBODY_BYTES + 2] = 5;
        CHECK(gevrAttachNetDecode(bad, len, NULL, got) < 0);
    }
    {
        GevrAttachRecord r = in[1];

        r.where = 2;
        CHECK(gevrAttachNetEncode(bad, sizeof(bad), 9, &r, 1) == 0);
        r.where = 0;
        r.item = 256;
        CHECK(gevrAttachNetEncode(bad, sizeof(bad), 9, &r, 1) == 0);
        r.item = 1;
        strcpy(r.model, "a/b.bin");
        CHECK(gevrAttachNetEncode(bad, sizeof(bad), 9, &r, 1) == 0);
    }
    return 0;
}

static int owner(void)
{
    GevrAttachRecord sight = part(1, GEVR_ATTNET_GUN, 0, "GtacSightZ"), mag = part(2, GEVR_ATTNET_GUN, 0, "mag.bin");
    GevrAttachRecord holster = part(3, GEVR_ATTNET_BODYSLOT, 0, "PholsterZ"), glove = part(4, GEVR_ATTNET_HAND, 1, "glove.bin");
    float frame[4][3];
    int i, sent;

    /* nothing worn: nothing sent, ever */
    CHECK(!gevrAttachNetWorn() && poll() == 0);
    gevrAttachNetAnswer(3);
    gevrAttachNetReset(1);
    CHECK(poll() == 0 && s_sent == 0);

    /* attached: everyone hears it at the next frame, in one message */
    CHECK(gevrAttachNetSet("tactical", &sight) && gevrAttachNetSet("tactical", &mag) && gevrAttachNetSet("tactical", &holster)
          && gevrAttachNetSet("tactical", &glove));
    CHECK(gevrAttachNetWorn() && poll() == 1 && s_lastTo == 0xFF && !strcmp(s_lastMod, "tactical"));
    CHECK(live(1) == 4 && live(5) == 4 && same(1, &sight) && same(1, &mag) && same(2, &holster) && same(7, &glove));
    /* the same again: nothing */
    CHECK(gevrAttachNetSet("tactical", &sight) && poll() == 0);

    /* (1) a flip while both are there: the other player sees the new state */
    CHECK(gevrAttachNetShow(1, 0) && poll() == 1);
    sight.shown = 0;
    CHECK(same(1, &sight) && same(4, &sight));
    CHECK(gevrAttachNetShow(1, 0) && poll() == 0);   /* no change, nothing sent */
    for (i = 0; i < 3; i++) memcpy(frame[i], sight.frame[i], sizeof(frame[i]));
    frame[3][0] = 0;
    frame[3][1] = 90;
    frame[3][2] = -25;   /* a sight folded down: a variant's place */
    CHECK(gevrAttachNetPlace(1, (const float (*)[3])frame) && poll() == 1);
    sight.frame[3][2] = -25;
    CHECK(same(3, &sight));
    frame[3][2] = 5000;  /* past the limits: refused, unchanged */
    CHECK(!gevrAttachNetPlace(1, (const float (*)[3])frame) && poll() == 0 && same(3, &sight));
    CHECK(!gevrAttachNetShow(99, 1) && !gevrAttachNetPlace(99, (const float (*)[3])frame));   /* not this player's */

    /* flipped every frame for a second (a magnifier toggled fast): sent on change, at most
     * GEVR_ATTNET_SENDS a second, the last state the one that stays */
    s_now += 2000000;
    poll();
    sent = s_sent;
    for (i = 0; i < 60; i++) {
        gevrAttachNetShow(1, i & 1);
        poll();
    }
    CHECK(s_sent - sent <= GEVR_ATTNET_SENDS + 1 && s_sent - sent >= GEVR_ATTNET_SENDS);
    settle();
    sight.shown = 1;
    CHECK(same(1, &sight) && same(6, &sight));

    /* a body anchor's centre follows this player's fit, past half a cm */
    s_sent = 0;
    gevrAttachNetRefreshBody(NULL);
    {
        int bodyAt(int where, float out[3]);

        gevrAttachNetRefreshBody(bodyAt);
        CHECK(poll() == 1);
        holster.body[0] = 66.0f;
        CHECK(same(2, &holster));
        gevrAttachNetRefreshBody(bodyAt);
        CHECK(poll() == 0);
    }

    /* (3) removal on a gun swap: the gun's parts go, the new gun's come, in that order */
    GevrAttachRecord rail = part(5, GEVR_ATTNET_GUN, 0, "GtacRailZ");
    {
        gevrAttachNetRemove(1);
        gevrAttachNetRemove(2);
        CHECK(gevrAttachNetSet("tactical", &rail) && poll() == 1);
        CHECK(live(1) == 3 && !has(1, 1) && !has(1, 2) && same(1, &rail) && same(1, &holster) && same(1, &glove));
        gevrAttachNetRemove(1);   /* again: nothing */
        gevrAttachNetRemove(42);  /* never this player's: nothing */
        CHECK(poll() == 0);
        CHECK(!gevrAttachNetShow(1, 1));   /* gone: not shown again */
    }

    /* (2) a player joining: the host asks, and that player alone gets the whole state */
    for (i = 0; i < 256; i++) s_mirror[6][i].live = 0;
    gevrAttachNetAskFor("tactical", 6);
    CHECK(poll() == 1 && s_lastTo == 0xFF && s_asked[6] == 7);   /* the ASK, heard by all seven */
    gevrAttachNetAnswer(6);
    CHECK(poll() == 1 && s_lastTo == 6 && live(6) == 3 && same(6, &holster) && same(6, &glove) && same(6, &rail));
    CHECK(poll() == 0);

    /* a stop in the mission: the others clear this player's (CLEAR), nothing more after */
    s_cleared = 0;
    gevrAttachNetReset(1);
    CHECK(!gevrAttachNetWorn() && poll() == 1 && s_cleared == 7 && live(1) == 0 && live(6) == 0);
    CHECK(poll() == 0);
    /* a mission's end: none sent (the others clear theirs as the next loads) */
    CHECK(gevrAttachNetSet("tactical", &glove) && poll() == 1);
    gevrAttachNetReset(0);
    CHECK(poll() == 0 && live(1) == 1);
    return 0;
}

/* the owner's optic, as gevr_modoptic.c holds it, by attachment id */
static struct { int bound, mode, folded; char housing[64]; } s_optic[16];
static int s_opticAsked;

static int opticState(int id, int *mode, int *folded, char housing[GEVR_ATTNET_NAME + 1])
{
    s_opticAsked++;
    if (id < 0 || id >= 16 || !s_optic[id].bound) return 0;
    *mode = s_optic[id].mode;
    *folded = s_optic[id].folded;
    strcpy(housing, s_optic[id].housing);
    return 1;
}

static int optic(void)
{
    GevrAttachRecord mag = part(1, GEVR_ATTNET_GUN, 0, "hhs1.bin"), rail = part(2, GEVR_ATTNET_GUN, 0, "GtacRailZ");
    GevrAttachRecord glove = part(3, GEVR_ATTNET_HAND, 0, "glove.bin");
    int i, sent;

    for (i = 0; i < 8; i++) memset(s_mirror[i], 0, sizeof(s_mirror[i]));
    s_now += 2000000;
    CHECK(gevrAttachNetSet("tactical", &mag) && gevrAttachNetSet("tactical", &rail) && gevrAttachNetSet("tactical", &glove));
    CHECK(poll() == 1 && same(1, &mag));
    /* asked of a pack mesh on the gun alone: never a cartridge-built part nor a hand's */
    s_opticAsked = 0;
    gevrAttachNetRefreshOptic(opticState);
    CHECK(s_opticAsked == 1 && poll() == 0);   /* none bound: nothing new */
    /* bound (the magnifier deployed): with the attachment, its mode and housing */
    s_optic[1].bound = 1;
    strcpy(s_optic[1].housing, "hhs1-folded.bin");
    gevrAttachNetRefreshOptic(opticState);
    CHECK(poll() == 1);
    mag.optic = 1;
    strcpy(mag.housing, "hhs1-folded.bin");
    CHECK(same(1, &mag) && same(7, &mag));
    gevrAttachNetRefreshOptic(opticState);
    CHECK(poll() == 0);   /* the same: nothing */
    /* (1) a flip: the next mode folds it */
    s_optic[1].mode = 1;
    s_optic[1].folded = 1;
    gevrAttachNetRefreshOptic(opticState);
    CHECK(poll() == 1);
    mag.mode = 1;
    mag.folded = 1;
    CHECK(same(2, &mag));
    /* flipped every frame for a second: on change, at the rate, the last state stays */
    s_now += 2000000;
    poll();
    sent = s_sent;
    for (i = 0; i < 60; i++) {
        s_optic[1].mode = i & 1;
        s_optic[1].folded = i & 1;
        gevrAttachNetRefreshOptic(opticState);
        poll();
    }
    CHECK(s_sent - sent <= GEVR_ATTNET_SENDS + 1 && s_sent - sent >= GEVR_ATTNET_SENDS);
    settle();
    mag.mode = mag.folded = 1;
    CHECK(same(3, &mag));
    /* a mode past the profile's or a housing that isn't flat: told as none */
    s_optic[1].mode = GEVR_ATTNET_MODES;
    gevrAttachNetRefreshOptic(opticState);
    CHECK(poll() == 1);
    mag.optic = mag.mode = mag.folded = 0;
    mag.housing[0] = '\0';
    CHECK(same(3, &mag));
    s_optic[1].mode = 1;
    gevrAttachNetRefreshOptic(opticState);
    CHECK(poll() == 1);
    mag.optic = mag.mode = mag.folded = 1;
    strcpy(mag.housing, "hhs1-folded.bin");
    /* (2) a player joining gets the optic's state with the attachment */
    for (i = 0; i < 256; i++) s_mirror[5][i].live = 0;
    gevrAttachNetAnswer(5);
    CHECK(poll() == 1 && s_lastTo == 5 && same(5, &mag) && same(5, &rail) && same(5, &glove));
    /* (3) unbound on a gun swap (the carrier retired): the housing goes, the part stays */
    s_optic[1].bound = 0;
    gevrAttachNetRefreshOptic(opticState);
    CHECK(poll() == 1);
    mag.optic = mag.mode = mag.folded = 0;
    mag.housing[0] = '\0';
    CHECK(same(1, &mag) && same(5, &mag));
    /* removed: the optic with it */
    s_optic[1].bound = 1;
    gevrAttachNetRemove(1);
    gevrAttachNetRefreshOptic(opticState);
    CHECK(poll() == 1 && !has(1, 1));
    s_optic[1].bound = 0;
    gevrAttachNetReset(0);
    return 0;
}

static GevrAttachRecord body(int hand, int item, const char *model)
{
    GevrAttachRecord r;
    int i;

    memset(&r, 0, sizeof(r));
    r.op = GEVR_ATTNET_GUNBODY;
    r.where = hand;
    r.item = item;
    r.fitted = 1;
    for (i = 0; i < 3; i++) r.frame[i][i] = 1.0f;
    r.frame[3][2] = 120.0f;
    r.muzzled = 1;
    r.muzzle[1] = 300.0f;
    snprintf(r.model, sizeof(r.model), "%s", model);
    return r;
}

static int gunBody(void)
{
    GevrAttachRecord pp7 = body(0, 0x17, "kelly-pp7.bin"), ak = body(0, 0x1E, "kelly-ak.bin"), own;
    GevrAttachRecord sight = part(1, GEVR_ATTNET_GUN, 0, "sight.bin");
    int i, sent;

    memset(s_bodies, 0, sizeof(s_bodies));
    memset(&own, 0, sizeof(own));
    own.op = GEVR_ATTNET_GUNBODY;
    s_now += 2000000;
    s_sent = 0;
    /* the hand's own, never a body: nothing sent */
    CHECK(gevrAttachNetGunBody(NULL, 0, NULL) && gevrAttachNetGunBody(NULL, 1, NULL) && poll() == 0 && !gevrAttachNetWorn());
    /* set: everyone hears it */
    CHECK(gevrAttachNetGunBody("tactical", 0, &pp7) && gevrAttachNetWorn() && poll() == 1 && s_lastTo == 0xFF);
    CHECK(sameBody(1, &pp7) && sameBody(7, &pp7) && s_bodies[1][1].op == 0);
    CHECK(gevrAttachNetGunBody("tactical", 0, &pp7) && poll() == 0);   /* the same: nothing */
    /* a new fit: sent */
    pp7.frame[3][2] = 125.0f;
    CHECK(gevrAttachNetGunBody("tactical", 0, &pp7) && poll() == 1 && sameBody(3, &pp7));
    /* with the parts on it, the body first in the same message */
    CHECK(gevrAttachNetSet("tactical", &sight));
    pp7.muzzled = 0;
    memset(pp7.muzzle, 0, sizeof(pp7.muzzle));
    CHECK(gevrAttachNetGunBody("tactical", 0, &pp7) && poll() == 1 && sameBody(2, &pp7) && same(2, &sight));
    /* (2) a join: the body and the parts to that player alone */
    memset(s_bodies[4], 0, sizeof(s_bodies[4]));
    for (i = 0; i < 256; i++) s_mirror[4][i].live = 0;
    gevrAttachNetAnswer(4);
    CHECK(poll() == 1 && s_lastTo == 4 && sameBody(4, &pp7) && same(4, &sight));
    /* a gun swap: the new gun's body */
    CHECK(gevrAttachNetGunBody("tactical", 0, &ak) && poll() == 1 && sameBody(1, &ak) && sameBody(6, &ak));
    /* reset to the hand's own: told once, then nothing */
    CHECK(gevrAttachNetGunBody(NULL, 0, NULL) && poll() == 1 && sameBody(1, &own) && sameBody(4, &own));
    CHECK(gevrAttachNetGunBody(NULL, 0, NULL) && poll() == 0);
    /* a join now: no body to tell */
    memset(s_bodies[3], 0, sizeof(s_bodies[3]));
    gevrAttachNetAnswer(3);
    poll();
    CHECK(s_bodies[3][0].op == 0);
    /* refused: a body with no name or a bad one, no mod, a third hand; nothing sent */
    {
        GevrAttachRecord r = pp7;

        r.model[0] = '\0';
        CHECK(!gevrAttachNetGunBody("tactical", 0, &r));
        strcpy(r.model, "../x.bin");
        CHECK(!gevrAttachNetGunBody("tactical", 0, &r));
        CHECK(!gevrAttachNetGunBody(NULL, 0, &pp7) && !gevrAttachNetGunBody("tactical", 2, &pp7));
        r = pp7;
        r.frame[3][0] = NAN;
        CHECK(!gevrAttachNetGunBody("tactical", 0, &r));
        r = pp7;
        r.item = 0;
        CHECK(!gevrAttachNetGunBody("tactical", 0, &r));
        CHECK(poll() == 0);
    }
    /* swapped every frame for a second: at the rate, the last one stays */
    s_now += 2000000;
    poll();
    sent = s_sent;
    for (i = 0; i < 60; i++) {
        CHECK(i & 1 ? gevrAttachNetGunBody("tactical", 0, &pp7) : gevrAttachNetGunBody("tactical", 0, &ak));
        poll();
    }
    CHECK(s_sent - sent <= GEVR_ATTNET_SENDS + 1 && s_sent - sent >= GEVR_ATTNET_SENDS);
    settle();
    CHECK(sameBody(2, &pp7));
    /* the other hand on its own */
    ak.where = 1;
    CHECK(gevrAttachNetGunBody("tactical", 1, &ak) && poll() == 1 && sameBody(2, &ak) && sameBody(2, &pp7));
    /* caps: the bodies never take an attachment's place; with the table full they still go */
    for (i = 0; i < GEVR_ATTNET_OWNED; i++) {
        GevrAttachRecord r = part(200 + i, GEVR_ATTNET_HAND, 0, "glove.bin");

        if (!gevrAttachNetSet("tactical", &r)) break;
    }
    CHECK(i == GEVR_ATTNET_OWNED - 1);   /* beside the sight */
    settle();
    pp7.frame[3][2] = 130.0f;
    CHECK(gevrAttachNetGunBody("tactical", 0, &pp7) && poll() == 1 && sameBody(5, &pp7));
    /* a stop: the others drop the bodies with the parts (a CLEAR) */
    s_cleared = 0;
    gevrAttachNetReset(1);
    CHECK(!gevrAttachNetWorn() && poll() == 1 && s_cleared == 7 && s_bodies[2][0].op == 0 && s_bodies[2][1].op == 0);
    CHECK(poll() == 0);
    /* a body alone, then a stop: its mod's CLEAR still goes */
    CHECK(gevrAttachNetGunBody("other", 0, &pp7) && poll() == 1 && sameBody(2, &pp7) && !strcmp(s_lastMod, "other"));
    gevrAttachNetReset(1);
    CHECK(poll() == 1 && !strcmp(s_lastMod, "other") && s_bodies[2][0].op == 0);
    /* a mission's end: nothing sent */
    CHECK(gevrAttachNetGunBody("tactical", 0, &pp7) && poll() == 1);
    gevrAttachNetReset(0);
    CHECK(poll() == 0 && !gevrAttachNetWorn());
    return 0;
}

int bodyAt(int where, float out[3])
{
    out[0] = 66.0f + where;
    out[1] = 20.0f;
    out[2] = -10.0f;
    return 1;
}

/* a player joining a full rig: the measured loadout (#291, 35), packed */
static int join(void)
{
    static const char *const names[] = { "PlBaseZ", "PlPart1Z", "PlPart2Z", "PlPart3Z", "PlPart4Z", "PlMagZ", "sling.bin" };
    GevrAttachRecord r;
    int i, n0;

    for (i = 0; i < 256; i++) s_mirror[2][i].live = 0;
    s_sent = s_bytes = 0;
    for (i = 0; i < 35; i++) {
        r = part(100 + i, i < 14 ? GEVR_ATTNET_GUN : i < 30 ? GEVR_ATTNET_BODYSLOT : GEVR_ATTNET_HAND,
                 i < 14 ? 0 : i < 30 ? i % GEVR_ATTNET_ANCHORS : i & 1, names[i % 7]);
        r.node = i < 14 ? i : 0;
        CHECK(gevrAttachNetSet("tactical", &r));
    }
    s_now += 2000000;
    settle();
    n0 = s_sent;
    CHECK(live(2) == 35 && n0 >= 3 && n0 <= 5);   /* 35 in a few messages */
    for (i = 0; i < 256; i++) s_mirror[2][i].live = 0;
    s_now += 2000000;
    gevrAttachNetAnswer(2);
    settle();
    CHECK(live(2) == 35 && s_sent - n0 == n0 && s_lastTo == 2);
    /* past the table: refused, the rest kept */
    for (i = 0; i < GEVR_ATTNET_OWNED; i++) {
        r = part(1000 + i, GEVR_ATTNET_HAND, 0, "glove.bin");
        if (!gevrAttachNetSet("tactical", &r)) break;
    }
    CHECK(i == GEVR_ATTNET_OWNED - 35);
    gevrAttachNetReset(0);
    return 0;
}

int main(void)
{
    int line;

    if ((line = wire()) || (line = wireOptic()) || (line = owner()) || (line = join()) || (line = optic())
        || (line = gunBody())) return line;
    printf("modattach_net: ok\n");
    return 0;
}
