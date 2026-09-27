#ifndef _NET_PROTOCOL_H
#define _NET_PROTOCOL_H

#include <stdint.h>
#include <stdbool.h>
#include <ultra64.h>
#include "bondtypes.h"
#include "net/netbuf.h"

#define GEVR_NET_MAGIC           0x47455652  /* "GEVR" */
#define GEVR_NET_VERSION         3
#define GEVR_DEFAULT_PORT        27007
#define GEVR_DISCOVERY_PORT      27008
#define GEVR_MAX_PLAYERS         4
#define GEVR_MAX_NAME_LEN        24
#define GEVR_VOIP_MAX_BYTES      128

/* ENet Channels */
enum {
    NET_CHAN_RELIABLE = 0,      /* Match state, lobby, damage, weapon events */
    NET_CHAN_PLAYER_STATE = 1,   /* Unreliable sequenced player commands / transforms */
    NET_CHAN_VOIP = 2,          /* Voice chat audio packets */
    NET_CHAN_MAX
};

/* User command bitflags (adapted from Perfect Dark port-net) */
#define UCMD_FIRE           (1 << 0)
#define UCMD_ACTIVATE       (1 << 1)
#define UCMD_RELOAD         (1 << 2)
#define UCMD_AIMMODE        (1 << 3)
#define UCMD_DUCK           (1 << 4)
#define UCMD_SELECT         (1 << 7)
#define UCMD_SELECT_DUAL    (1 << 8)

/* Packet Opcodes */
typedef enum {
    NET_MSG_NONE = 0,
    
    /* Handshake & Lobby */
    NET_MSG_HELLO = 1,          /* Client -> Server */
    NET_MSG_WELCOME = 2,        /* Server -> Client */
    NET_MSG_LOBBY_STATE = 3,    /* Server -> All */
    NET_MSG_LOBBY_READY = 4,    /* Client -> Server */
    NET_MSG_START_MATCH = 5,    /* Server -> All */
    NET_MSG_LOBBY_CHARACTER = 6, /* Client -> Server (change character without re-handshaking) */
    
    /* In-Match State */
    NET_MSG_PLAYER_STATE = 10,  /* Player -> Server / Peers */
    NET_MSG_FIRE_EVENT = 11,    /* Shooter -> Server / Peers */
    NET_MSG_HIT_REPORT = 12,    /* Shooter -> Server */
    NET_MSG_DAMAGE_EVENT = 13,  /* Server -> All */
    NET_MSG_RESPAWN = 14,       /* Respawning player -> host -> peers */
    NET_MSG_MATCH_END = 15,     /* Server -> All */
    
    /* Voice Chat */
    NET_MSG_VOIP_FRAME = 20,    /* Player -> Server / Peers */
} NetMsgType;

/* Player movement & input command struct (serialized via netbuf) */
struct netplayermove {
    u32 tick;
    u32 ucmd;
    f32 movespeed[2];   /* [0]: forward (-1..1), [1]: strafe (-1..1) */
    f32 angles[2];      /* [0]: theta (yaw), [1]: verta (pitch) */
    f32 crosspos[2];    /* crosshair pos */
    s8  weaponnum;      /* equipped weapon ITEM_* */
    s8  crouchpos;      /* 0: stand, 1: crouch */
    coord3d pos;        /* world position */
    coord3d handpos;    /* 6DoF hand aim position in world/view */
    coord3d handrot;    /* 6DoF hand aim rotation (pitch, yaw, roll) */
};

/* Serialization for netplayermove */
static inline u32 netbufWritePlayerMove(struct netbuf *buf, const struct netplayermove *m) {
    netbufWriteU32(buf, m->tick);
    netbufWriteU32(buf, m->ucmd);
    netbufWriteF32(buf, m->movespeed[0]);
    netbufWriteF32(buf, m->movespeed[1]);
    netbufWriteF32(buf, m->angles[0]);
    netbufWriteF32(buf, m->angles[1]);
    netbufWriteF32(buf, m->crosspos[0]);
    netbufWriteF32(buf, m->crosspos[1]);
    netbufWriteS8(buf, m->weaponnum);
    netbufWriteS8(buf, m->crouchpos);
    netbufWriteCoord(buf, &m->pos);
    netbufWriteCoord(buf, &m->handpos);
    netbufWriteCoord(buf, &m->handrot);
    return buf->error;
}

static inline u32 netbufReadPlayerMove(struct netbuf *buf, struct netplayermove *m) {
    m->tick = netbufReadU32(buf);
    m->ucmd = netbufReadU32(buf);
    m->movespeed[0] = netbufReadF32(buf);
    m->movespeed[1] = netbufReadF32(buf);
    m->angles[0] = netbufReadF32(buf);
    m->angles[1] = netbufReadF32(buf);
    m->crosspos[0] = netbufReadF32(buf);
    m->crosspos[1] = netbufReadF32(buf);
    m->weaponnum = netbufReadS8(buf);
    m->crouchpos = netbufReadS8(buf);
    netbufReadCoord(buf, &m->pos);
    netbufReadCoord(buf, &m->handpos);
    netbufReadCoord(buf, &m->handrot);
    return buf->error;
}

#pragma pack(push, 1)

/* Common packet header */
typedef struct {
    uint32_t magic;     /* GEVR_NET_MAGIC */
    uint16_t version;   /* GEVR_NET_VERSION */
    uint8_t  msg_type;  /* NetMsgType */
    uint8_t  slot_id;   /* Sender player slot (0..3) */
} NetHeader;

/* Client hello */
typedef struct {
    NetHeader header;
    char      player_name[GEVR_MAX_NAME_LEN];
    uint8_t   requested_chr_id;
} NetMsgHello;

/* Server welcome */
typedef struct {
    NetHeader header;
    uint8_t   assigned_slot;
    uint8_t   stage_num;
    uint8_t   scenario;
    uint8_t   weapon_set;
} NetMsgWelcome;

/* Lobby Player Slot Info */
typedef struct {
    uint8_t   connected;
    uint8_t   ready;
    uint8_t   chr_id;
    uint8_t   pad;
    uint16_t  ping_ms;
    char      name[GEVR_MAX_NAME_LEN];
} NetLobbySlot;

/* Lobby State Broadcast */
typedef struct {
    NetHeader     header;
    uint8_t       stage_num;
    uint8_t       scenario;
    uint8_t       weapon_set;
    uint8_t       countdown_secs;
    NetLobbySlot  slots[GEVR_MAX_PLAYERS];
} NetMsgLobbyState;

/* Match Launch */
typedef struct {
    NetHeader header;
    uint8_t   stage_num;
    uint8_t   scenario;
    uint8_t   weapon_set;
    uint8_t   start_pad[GEVR_MAX_PLAYERS];
    uint32_t  random_seed;
} NetMsgStartMatch;

/* High-frequency player state */
typedef struct {
    NetHeader header;
    uint32_t  sequence;
    
    /* Position and movement */
    float     pos_x, pos_y, pos_z;
    float     vel_x, vel_y, vel_z;
    
    /* Head (HMD) orientation in degrees */
    float     head_yaw;
    float     head_pitch;
    
    /* Aim / Gun Hand pose */
    float     hand_yaw;
    float     hand_pitch;
    float     hand_roll;
    float     hand_x, hand_y, hand_z;
    
    /* Action / Stance */
    uint8_t   stance;        /* 0: stand, 1: crouch */
    uint8_t   weapon_id;     /* ITEM_* id */
    uint8_t   is_firing;     /* 1 if trigger pulled */
    uint8_t   pad;
} NetMsgPlayerState;

/* Hit report from local client to host */
typedef struct {
    NetHeader header;
    uint8_t   target_slot;
    uint8_t   weapon_id;
    uint8_t   hit_location;  /* Head, torso, limb */
    uint8_t   pad;
    float     hit_x, hit_y, hit_z;
    float     damage;
} NetMsgHitReport;

/* Damage & Death broadcast from host to all */
typedef struct {
    NetHeader header;
    uint8_t   target_slot;
    uint8_t   attacker_slot;
    uint8_t   weapon_id;
    uint8_t   is_dead;
    float     damage;
    float     vector_x, vector_z;
    float     new_health;
    float     new_armor;
} NetMsgDamageEvent;

/* VoIP voice packet */
typedef struct {
    NetHeader header;
    uint32_t  sequence;
    uint16_t  payload_size;
    uint8_t   payload[GEVR_VOIP_MAX_BYTES];
} NetMsgVoipFrame;

#pragma pack(pop)

#endif /* _NET_PROTOCOL_H */
