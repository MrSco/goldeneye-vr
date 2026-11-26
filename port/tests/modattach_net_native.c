/*
 * mod.attach on the other players' headsets (#335): port/src/gevr_modattach_net.c
 * as it is. The owner's side runs here; each message it sends is decoded and
 * applied to a mirror, as another headset's copy of the player would hold it,
 * so the checks read what the other player sees: a flip reaching it, a
 * player who joins getting the whole state, a removal (a gun swap, a detach,
 * a stop) reaching it, the rate and the packing; and every malformed message
 * refused whole.
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
            s_cleared++;
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
        && !memcmp(m->r.body, r->body, sizeof(r->body));
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
    /* the layout: stage, count; a SET's op, id little-endian, on, where, node, flags, 15 floats, names */
    CHECK(len == 2 + (GEVR_ATTNET_SET_BYTES + 9 + 8) + (GEVR_ATTNET_SET_BYTES + 13) + 5 + 2);
    CHECK(out[0] == 9 && out[1] == 4 && out[2] == GEVR_ATTNET_SET && out[3] == 7 && out[4] == 0 && out[5] == 0 && out[6] == 0);
    CHECK(out[7] == GEVR_ATTNET_GUN && out[8] == 1 && out[9] == 12 && out[10] == 0);
    CHECK(out[2 + GEVR_ATTNET_SET_BYTES - 2] == 9 && !memcmp(out + 2 + GEVR_ATTNET_SET_BYTES - 1, "GtacAcogZ", 9));
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
    bad[10] = 2;
    CHECK(gevrAttachNetDecode(bad, len, NULL, got) < 0);                 /* flags past shown */
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
        const int at = 2 + GEVR_ATTNET_SET_BYTES - 2;   /* the model's length byte */
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

    if ((line = wire()) || (line = owner()) || (line = join())) return line;
    printf("modattach_net: ok\n");
    return 0;
}
