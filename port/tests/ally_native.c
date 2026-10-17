/* Link the production companion controller against a small synthetic stage. */
#include <ultra64.h>
#include <bondgame.h>
#include <bondaicommands.h>
#include "game/gevr_ally.h"
#include "game/chraction.h"
#include "game/player.h"
#include "game/bondview.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

int VrAlly, VrAllyWeapon, VrAllyAggression, VrAllyHealth, VrAllyDamage, VrAllyInvincible;
s32 g_StageNum, g_ClockTimer;
CAMERAMODE g_CameraMode;
struct player *g_CurrentPlayer;
ChrRecord *g_ChrSlots;
static struct player player;
static ChrRecord guards[12];
static PropRecord props[12], bond;
static Model models[12];
static StandTile tile;
static s32 online, campaign, players, locked, failspawn, spawns, gives, walks, attacks, hits, los;
static s32 weapon, target, identity = 5000;
static f32 hitScale;

bool netIsActive(void) { return online; }
int gevrRomCampaign(void) { return campaign; }
s32 getPlayerCount(void) { return players; }
s32 lvlGetControlsLockedFlag(void) { return locked; }
s32 get_numguards(void) { return 12; }
bool chrIsNotDeadOrShot(ChrRecord *chr) {
    return !chrIsDead(chr) && chr->actiontype != ACT_ARGH && chr->actiontype != ACT_PREARGH;
}
bool handles_shot_actors(ChrRecord *victim, s32 part, coord3d *direction, s32 item, bool isPlayer) {
    assert(part == 0xF && !isPlayer);
    hits++; hitScale = gevrAllyDamageScale(); victim->damage += hitScale;
    return TRUE;
}
bool chrIsDead(ChrRecord *chr) { return chr->actiontype == ACT_DIE || chr->actiontype == ACT_DEAD; }
PropRecord *chrSpawnAtCoord(s32 body, s32 head, coord3d *pos, StandTile *stan, f32 angle, AIRecord *ai, s32 flags) {
    assert(body == BODY_Natalya_Jungle_Fatigues && head == HEAD_Natalya_Jungle_Fatigues);
    assert(stan == &tile && ai == NULL && flags == 0x10);
    spawns++;
    if (failspawn) return NULL;
    guards[0].chrnum = identity++;
    guards[0].bodynum = body; guards[0].headnum = head;
    guards[0].model = &models[0]; guards[0].prop = &props[0];
    props[0].chr = &guards[0]; props[0].pos = *pos; props[0].pos.z += 60;
    props[0].stan = stan; props[0].type = PROP_TYPE_CHR;
    return &props[0];
}
PropRecord *chrGiveWeapon(ChrRecord *chr, s32 model, ITEM_IDS item, s32 flags) {
    gives++; weapon = item; chr->weapons_held[0] = &props[11]; return &props[11];
}
s32 plot_course_for_actor(ChrRecord *chr, coord3d *pos, StandTile *stan, SPEED speed) {
    assert(stan == &tile && speed == SPEED_RUN); walks++; chr->actiontype = ACT_GOPOS; return TRUE;
}
bool check_set_actor_standing_still(ChrRecord *chr, s32 type, s32 id) {
    chr->actiontype = ACT_STAND; return TRUE;
}
bool actor_aim_at_actor(ChrRecord *chr, s32 type, s32 id) {
    assert(type == TARGET_CHR); attacks++; target = id; chr->actiontype = ACT_ATTACK; return TRUE;
}
bool check_if_position_in_same_room(ChrRecord *chr, coord3d *pos, StandTile *stan) { return los; }

static void reset(void) {
    memset(guards, 0, sizeof(guards)); memset(props, 0, sizeof(props));
    memset(&player, 0, sizeof(player)); memset(&bond, 0, sizeof(bond));
    g_ChrSlots = guards; g_CurrentPlayer = &player; player.prop = &bond;
    bond.stan = &tile; bond.type = PROP_TYPE_VIEWER;
    g_StageNum = LEVELID_DAM; g_ClockTimer = 15; g_CameraMode = CAMERAMODE_FP;
    online = campaign = locked = failspawn = spawns = gives = walks = attacks = hits = 0;
    players = los = 1; VrAlly = 1; VrAllyWeapon = 0; VrAllyAggression = 1;
    VrAllyHealth = VrAllyDamage = 100; VrAllyInvincible = 0;
    gevrAllyReset();
}
static ChrRecord *enemy(int slot, f32 distance) {
    ChrRecord *chr = &guards[slot];
    memset(chr, 0, sizeof(*chr)); chr->model = &models[slot]; chr->prop = &props[slot];
    chr->chrnum = slot; chr->bodynum = BODY_Russian_Soldier; chr->headnum = HEAD_Male_Karl;
    chr->weapons_held[0] = &props[11]; chr->actiontype = ACT_STAND;
    props[slot].type = PROP_TYPE_CHR; props[slot].stan = &tile; props[slot].pos.x = distance;
    return chr;
}
static void think(void) { assert(gevrAllyThink(&guards[0])); }

int main(void) {
    reset(); VrAlly = 0; gevrAllyReset(); gevrAllyTick(); assert(spawns == 0);
    reset(); online = 1; gevrAllyTick(); assert(spawns == 0);
    reset(); players = 2; gevrAllyTick(); assert(spawns == 0);
    reset(); campaign = 1; gevrAllyTick(); assert(spawns == 0);
    reset(); g_StageNum = LEVELID_TITLE; gevrAllyTick(); assert(spawns == 0);
    reset(); g_StageNum = LEVELID_CUBA; gevrAllyTick(); assert(spawns == 0);
    reset(); g_CameraMode = CAMERAMODE_INTRO; gevrAllyTick(); assert(spawns == 0);
    reset(); locked = 1; gevrAllyTick(); assert(spawns == 0);
    reset(); g_ClockTimer = 0; gevrAllyTick(); assert(spawns == 0);
    reset(); player.bonddead = 1; gevrAllyTick(); assert(spawns == 0);
    reset(); bond.stan = NULL; gevrAllyTick(); assert(spawns == 0);
    reset(); failspawn = 1; gevrAllyTick(); assert(spawns == 1);
    failspawn = 0; for (int i=0; i<5; i++) gevrAllyTick(); assert(spawns == 2 && gives == 1);
    for (int i=0; i<50; i++) gevrAllyTick(); assert(spawns == 2);
    assert(gevrAllyIs(&guards[0]) && guards[0].maxdamage == 40);
    assert(!(guards[0].chrflags & CHRFLAG_INVINCIBLE));
    guards[0].actiontype = ACT_DEAD; for (int i=0; i<10; i++) gevrAllyTick(); assert(spawns == 2);
    assert(gevrAllyThink(&guards[0]));
    guards[0].chrnum++; assert(!gevrAllyIs(&guards[0]) && !gevrAllyThink(&guards[0]));
    reset(); VrAllyWeapon = 7; VrAllyHealth = 250; VrAllyDamage = 200; VrAllyInvincible = 1;
    gevrAllyReset(); VrAllyWeapon = 0; VrAllyHealth = VrAllyDamage = 100; VrAllyInvincible = 0;
    gevrAllyTick(); assert(weapon == ITEM_FNP90 && guards[0].maxdamage == 100);
    assert(guards[0].chrflags & CHRFLAG_INVINCIBLE);
    ChrRecord *foe = enemy(1, 300); coord3d direction = {0};
    gevrAllyHitGuard(&guards[0], foe, &direction, ITEM_FNP90);
    assert(hits == 1 && hitScale == 2 && gevrAllyDamageScale() == 1);
    gevrAllyHitGuard(foe, &guards[0], &direction, ITEM_FNP90);
    assert(hits == 2 && hitScale == 1);
    foe->chrflags = CHRFLAG_COUNT_DEATH_AS_CIVILIAN;
    gevrAllyHitGuard(&guards[0], foe, &direction, ITEM_FNP90); assert(hits == 2);
    reset(); gevrAllyTick(); foe = enemy(1, 300); think(); assert(attacks == 1 && target == 1);
    think(); assert(attacks == 1); /* do not restart an unfinished burst */
    foe->actiontype = ACT_DEAD; think(); assert(guards[0].actiontype == ACT_STAND);
    reset(); gevrAllyTick(); foe = enemy(1, 300); los = 0; think(); assert(attacks == 0);
    reset(); gevrAllyTick(); foe = enemy(1, 100);
    const s32 neutralBodies[] = { BODY_Natalya_Skirt, BODY_Natalya_Jungle_Fatigues,
        BODY_Boris, BODY_Valentin_, BODY_Trevelyan_006, BODY_Trevelyan_Janus,
        BODY_Scientist_1_Male, BODY_Scientist_2_Female, BODY_Civilian_1_Female,
        BODY_Civilian_2, BODY_Civilian_3, BODY_Civilian_4 };
    for (unsigned i=0; i<sizeof(neutralBodies)/sizeof(*neutralBodies); i++) {
        foe->bodynum = neutralBodies[i]; think(); assert(attacks == 0);
    }
    foe->bodynum = BODY_Siberian_Guard_1_Mishkin; foe->headnum = HEAD_Male_Mishkin;
    think(); assert(attacks == 0);
    foe = enemy(1, 100); foe->chrflags = CHRFLAG_CAN_SHOOT_CHRS; think(); assert(attacks == 0);
    foe = enemy(1, 100); foe->chrflags = CHRFLAG_COUNT_DEATH_AS_CIVILIAN; think(); assert(attacks == 0);
    foe = enemy(1, 100); foe->actiontype = ACT_SURRENDER; think(); assert(attacks == 0);
    foe = enemy(1, 100); foe->weapons_held[0] = NULL; think(); assert(attacks == 0);
    foe = enemy(1, 100); foe->prop->type = PROP_TYPE_VIEWER; think(); assert(attacks == 0);
    reset(); VrAllyAggression = 0; gevrAllyReset(); gevrAllyTick(); foe = enemy(1, 300);
    think(); assert(attacks == 0); foe->alertness = 100; think(); assert(attacks == 1);
    reset(); VrAllyAggression = 2; gevrAllyReset(); gevrAllyTick(); enemy(1, 1500);
    think(); assert(attacks == 1);
    reset(); gevrAllyTick(); enemy(1, 1500); think(); assert(attacks == 0);
    reset(); gevrAllyTick(); bond.pos.x = 600; think(); assert(walks == 1);
    think(); assert(walks == 1); /* avoid restarting paths every tick */
    for (int i=0; i<4; i++) think(); assert(walks == 2);
    props[0].pos = bond.pos; think(); assert(guards[0].actiontype == ACT_STAND);
    g_CameraMode = CAMERAMODE_POSEND; enemy(1, 600); think(); assert(attacks == 0);
    reset(); gevrAllyTick(); enemy(1, 300); props[0].pos.x = 1400; think(); assert(attacks == 0 && walks == 1);
    gevrAllyReset(); assert(!gevrAllyIs(&guards[0])); gevrAllyTick(); assert(spawns == 2);
    reset(); VrAllyWeapon = 999; VrAllyHealth = -10; VrAllyDamage = 999; gevrAllyReset(); gevrAllyTick();
    assert(weapon == ITEM_AUTOSHOT && guards[0].maxdamage == 10);
    gevrAllyHitGuard(&guards[0], enemy(1, 100), &direction, weapon); assert(hitScale == 4);
    puts("PASS: companion lifecycle, solo gating, configuration snapshot, targeting, following and damage isolation");
    return 0;
}
