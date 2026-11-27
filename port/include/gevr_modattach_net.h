#ifndef GEVR_MODATTACH_NET_H
#define GEVR_MODATTACH_NET_H

/*
 * mod.attach on the other players' headsets (#335, docs/mod-system.md
 * "Network"): what this player wears (its attachments on the gun, a hand or
 * the body) and their state, sent when they change, and the others' as they
 * arrive. NET_MSG_MOD's NET_MOD_ATTACH carries it (net_protocol.h); this file
 * is the bytes and the owner's bookkeeping, with nothing of the game's, so the
 * native tests run it as it is (port/tests/modattach_net_native.c).
 *
 * A message (little-endian): u8 the sender's stage, u8 n (1 to
 * GEVR_ATTNET_RECORDS), then n records, each a u8 op and:
 *  - GEVR_ATTNET_SET: u32 id (the owner's attachment id, 1 to 0x7FFFFFFF),
 *    u8 on (gun 0, hand 1, body 2), u8 where (a hand 0 right 1 left; a body
 *    anchor 0 to GEVR_ATTNET_ANCHORS - 1), u8 node (the gun model's matrix;
 *    0 off the gun), u8 flags (bit 0 shown; bit 1 an optic is bound, bit 2
 *    its selected mode is folded: mod.attach.optic; bit 3 it moves with the
 *    action, bit 4 only with the rack's pull (a handle), bit 5 with timing of
 *    its own: mod.attach.action), 12 f32 frame (the model's X, Y and Z axes
 *    in the anchor's frame, then its origin), 3 f32 body (a body anchor's
 *    centre from the eye, cm: below, to the right, ahead; 0 off the body), 3
 *    f32 travel (fully back, in the anchor's frame; zero when it doesn't
 *    move), u8 open, u8 close (its own timing's ticks, 1 to
 *    GEVR_ATTNET_TICKS; 0 without), f32 ease (0 to 1; 0 without), u8 mode
 *    (the optic's selected mode from 0, below GEVR_ATTNET_MODES; 0 with
 *    none), u8 n and n bytes the model's file, u8
 *    n and n bytes its like ("" a pack mesh), u8 n and n bytes the optic's
 *    folded housing (a pack mesh; "" none). An optic only on a pack mesh on
 *    the gun; folded only with a housing; a housing or a mode only with an
 *    optic. Only the housing's state: the lens, its view and reticle are the
 *    owner's alone. An action only on the gun; a handle or a timing only
 *    with an action, not both;
 *  - GEVR_ATTNET_REMOVE: u32 id;
 *  - GEVR_ATTNET_CLEAR: none (all the sender has of the message's mod,
 *    its pack gun bodies too);
 *  - GEVR_ATTNET_ASK: u8 slot (the host's: send what you wear to that slot);
 *  - GEVR_ATTNET_GUNBODY: the pack gun body a hand draws (mod.weapon.model
 *    with a pack mesh): u8 hand (0 right, 1 left), u8 item (the gun it
 *    stands in for, 1 to 255; 0 the hand's own model again), u8 flags (bit 0
 *    fit, bit 1 muzzle, bit 2 moving parts, bit 3 its action holds open on
 *    an empty magazine: mod.weapon.model's action, handle and holdOpen), 12
 *    f32 fit (its frame on the gun model's root: axes, then origin; zero
 *    without), 3 f32 muzzle (its authored muzzle, the mesh's units; zero
 *    without), u32 the action's pictures, u32 the handle's (bit n picture
 *    n, none in both), 6 f32 their travels (fully back, the mesh's units;
 *    zero for a group without pictures), u8 open, u8 close, f32 ease (the
 *    action's own timing as a SET's, only with its pictures), u8 n and n
 *    bytes the mesh's file ("" with item 0, else a flat name). All of the
 *    parts' fields zero without bit 2; none with item 0.
 * At most GEVR_ATTNET_BYTES in all. Anything else (an unknown op, a field
 * past its limit, a name that isn't a flat model file's, bytes left over or
 * missing) and the whole message is refused.
 */

#define GEVR_ATTNET_BYTES 1024        /* a message's payload: NET_MOD_ATTACH_BYTES */
#define GEVR_ATTNET_RECORDS 32        /* records in one message */
#define GEVR_ATTNET_OWNED 128         /* what this player wears: GEVR_MODAPI_ATTACH_MAX */
#define GEVR_ATTNET_SLOTS 8           /* players: GEVR_MAX_PLAYERS */
#define GEVR_ATTNET_ANCHORS 6         /* body anchors: GEVR_MODAPI_ANCHORS */
#define GEVR_ATTNET_SENDS 8           /* messages a second at most, the burst too */
#define GEVR_ATTNET_NAME 63           /* a model file's name */
#define GEVR_ATTNET_AXIS 100.0f       /* a frame's axis components: the API's own limits */
#define GEVR_ATTNET_ORIGIN 4000.0f    /* and its origin's, model units */
#define GEVR_ATTNET_BODY 250.0f       /* a body anchor's centre from the eye, cm */
#define GEVR_ATTNET_MODES 12          /* an optic's modes: GEVR_OPTIC_MODES */
#define GEVR_ATTNET_TICKS 30          /* an action's ticks to open or close: GEVR_MODACTION_TICKS_MAX */
#define GEVR_ATTNET_SET_BYTES 91      /* a SET record without its names' bytes */
#define GEVR_ATTNET_GUNBODY_BYTES 103 /* a GUNBODY record without its name's bytes */

enum { GEVR_ATTNET_SET = 1, GEVR_ATTNET_REMOVE = 2, GEVR_ATTNET_CLEAR = 3, GEVR_ATTNET_ASK = 4, GEVR_ATTNET_GUNBODY = 5 };
enum { GEVR_ATTNET_GUN = 0, GEVR_ATTNET_HAND = 1, GEVR_ATTNET_BODYSLOT = 2 };   /* GEVR_MODATTACH_* */

typedef struct GevrAttachRecord {
    int op;               /* GEVR_ATTNET_* */
    int id;               /* SET, REMOVE: the owner's attachment id */
    int slot;             /* ASK: the player to send to */
    int on, where, node, shown;   /* GUNBODY: where the hand */
    float frame[4][3];    /* rows: the model's X, Y and Z axes in the anchor's frame, then its origin (GUNBODY: the fit) */
    float body[3];        /* on the body: the anchor's centre from the eye, cm (below, right, ahead) */
    char model[GEVR_ATTNET_NAME + 1], like[GEVR_ATTNET_NAME + 1];   /* GUNBODY: model the mesh's file */
    int optic, mode, folded;                  /* SET: an optic bound, its mode, folded now */
    char housing[GEVR_ATTNET_NAME + 1];       /* SET: the optic's folded housing, "" none */
    int item, fitted, muzzled;                /* GUNBODY: the gun (0 the hand's own), a fit, a muzzle */
    float muzzle[3];                          /* GUNBODY: the authored muzzle */
    int moves, handle, timed;                 /* SET: mod.attach.action: it moves, only with the rack, its own timing */
    float travel[3];                          /* SET: fully back, in the anchor's frame */
    int parts, holdOpen;                      /* GUNBODY: moving parts; its action holds open */
    unsigned pictures[2];                     /* GUNBODY: the action's and the handle's pictures */
    float partTravel[2][3];                   /* GUNBODY: and their travels */
    int open, close;                          /* SET, GUNBODY: the action's own timing, ticks; 0 none */
    float ease;                               /* and its ease; 0 none */
} GevrAttachRecord;

/* ---- the bytes ---- */

/* A flat model file's name: 1 to 63 letters, digits, _ and -, with .bin at
 * its end or not (gevr_modapi.c modelNameOk, gevr_modassets.c nameOk) */
int gevrAttachNetNameOk(const char *s);
/* n records as a message: its length, or 0 when they don't fit or one is
 * outside the limits (nothing written then) */
int gevrAttachNetEncode(unsigned char *out, int cap, int stage, const GevrAttachRecord *r, int n);
/* A message's records into out (room for GEVR_ATTNET_RECORDS): how many, or
 * -1 when the message is refused whole */
int gevrAttachNetDecode(const unsigned char *p, int len, int *stage, GevrAttachRecord *out);

/* ---- the owner: what this headset's player wears ---- */

/* Attached or changed (a SET's fields; op ignored): sent to everyone at the
 * next poll; the same state again sends nothing. 0 when the table is full. */
int gevrAttachNetSet(const char *mod, const GevrAttachRecord *r);
/* Shown or hidden, moved: the owned one with that id changes (0 none such) */
int gevrAttachNetShow(int id, int shown);
int gevrAttachNetPlace(int id, const float frame[4][3]);
/* Removed: the others are told (nothing for one never set here) */
void gevrAttachNetRemove(int id);
/* Its action changed (mod.attach.action: moves, handle, travel and its own
 * timing, open 0 none): the owned one with that id is told again (0 none
 * such, or it's out of the limits) */
int gevrAttachNetAction(int id, int moves, int handle, const float travel[3], int open, int close, float ease);
/* Every owned attachment's optic asked again (mod.attach.optic: bound, its
 * mode, folded now, its folded housing): state(id, &mode, &folded, housing)
 * 1 bound, 0 none. A change is sent (a flip, a bind, an unbind on a swap). */
void gevrAttachNetRefreshOptic(int (*state)(int id, int *mode, int *folded, char housing[GEVR_ATTNET_NAME + 1]));
/* The pack gun body a hand draws now (a GUNBODY's fields: item, model,
 * fitted and frame, muzzled and muzzle, its parts; op and where ignored), or NULL the
 * hand's own model: a change is sent to everyone, nothing while it's the
 * same; the hand's own only once a body was told. 0 when it's refused. */
int gevrAttachNetGunBody(const char *mod, int hand, const GevrAttachRecord *r);
/* Every body anchor's centre asked again (the player's fit can change):
 * at(where, out) 0 keeps it; a change of more than half a cm is sent */
void gevrAttachNetRefreshBody(int (*at)(int where, float out[3]));
/* None worn now (a mission's end, a mod stopped). tell: the others are told
 * (a CLEAR for each mod that had any); else nothing is sent (they clear
 * theirs as the next mission loads). Asks waiting go too. */
void gevrAttachNetReset(int tell);
/* Something is worn (or waiting to be told gone), a pack gun body too */
int gevrAttachNetWorn(void);
/* The host asks: everything worn now to that slot at the next poll */
void gevrAttachNetAnswer(int slot);
/* The host: ask the others to send what they wear to slot (a player who just
 * reached the stage), in mod's name */
void gevrAttachNetAskFor(const char *mod, int slot);

/* Each frame in play: what's waiting goes out, packed, at most
 * GEVR_ATTNET_SENDS messages a second. send(to, mod, bytes, len): to 0xFF
 * everyone, else a slot; 0 when it couldn't go (what it carried is dropped:
 * an ask heals it). The messages sent. */
typedef int (*GevrAttachNetSend)(int to, const char *mod, const void *data, int len);
int gevrAttachNetPoll(unsigned long long nowUs, int stage, GevrAttachNetSend send);

#endif
