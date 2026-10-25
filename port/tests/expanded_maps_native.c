#include <ultra64.h>
#include <bondtypes.h>
#include <bondconstants.h>
#include <memp.h>
#include "chrai.h"
#include "ob.h"
#include "gevr_stage.h"
#include "gevr_expanded.h"
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>

stagesetup g_CurrentSetup;
static unsigned char campaign[262144], multiplayer[262144];
static StandTile tile;
static void *allocations[32];
static int allocated;
s32 sizepropdef(PropDefHeaderRecord *p) { return gevrSetupPropWords(p->type); }
void *mempAllocBytesInBank(u32 bytes, u8 bank) {
    assert(bank == MEMPOOL_STAGE && allocated < 32);
    void *p = calloc(1, bytes); assert(p); allocations[allocated++] = p; return p;
}
void *_fileNameLoadToBank(char *name, FILELOADMETHOD method, s32 size, u8 bank) {
    assert(!strcmp(name, "Ump_setuparkZ") && method == FILELOADMETHOD_DEFAULT && size == 256 && bank == MEMPOOL_STAGE);
    return multiplayer;
}
void sysLogPrintf(int level, const char *format, ...) { (void)level; (void)format; }
void sysFatalError(const char *format, ...) { fputs(format, stderr); abort(); }
f32 get_room_data_float2(void) { return 1.25f; }
f32 stanGetPositionYValue(StandTile *stan, f32 x, f32 z) {
    assert(stan == &tile); return x * .1f + z * .2f;
}

static unsigned char *append(unsigned char *out, void *p) {
    size_t bytes = sizepropdef(p) * 4; assert(bytes);
    memcpy(out, p, bytes); return out + bytes;
}

int gevrRomItemHost(int item) { return item; }
int gevrRomIsVariant(void) { return 0; }
int gevrRomAiHooks(void) { return 0; }
uint32_t gevrRomAiPrepare(uint8_t *code, uint32_t len, int drop) { (void)code; (void)drop; return len; }

static void loadSetup(const char *path, unsigned char *buffer, size_t capacity) {
    FILE *f=fopen(path,"rb"); assert(f);
    size_t size=fread(buffer,1,capacity,f); assert(!ferror(f)); fclose(f);
    assert(size<capacity && gevrConvertSetup(buffer,size,capacity));
}

s32 stanTestVolume(StandTile **stan,f32 x,f32 z,f32 radius,s32 mask,f32 a,f32 b) {
    (void)x;(void)z;(void)a;(void)b;assert(*stan==&tile && radius==30 && mask==CDTYPE_BG);return -1;
}

static int reference(const char *campaignPath, const char *mpPath) {
    loadSetup(campaignPath,campaign,sizeof(campaign));
    loadSetup(mpPath,multiplayer,sizeof(multiplayer));
    stagesetup *mp=(void *)campaign; g_CurrentSetup=*mp;
    g_CurrentSetup.propDefs=(void *)(campaign+(uintptr_t)mp->propDefs);
    g_CurrentSetup.intro=(void *)(campaign+(uintptr_t)mp->intro);
    g_CurrentSetup.pads=(void *)(campaign+(uintptr_t)mp->pads);
    g_CurrentSetup.boundpads=(void *)(campaign+(uintptr_t)mp->boundpads);
    for(PadRecord *p=g_CurrentSetup.pads;p->plink;p++) {
        p->plink=(char *)campaign+(uintptr_t)p->plink; p->stan=&tile;
        for(int i=0;i<3;i++)p->pos.f[i]*=get_room_data_float2();
    }
    for(BoundPadRecord *p=g_CurrentSetup.boundpads;p->plink;p++) {
        p->plink=(char *)campaign+(uintptr_t)p->plink; p->stan=&tile;
        for(int i=0;i<3;i++)p->pos.f[i]*=get_room_data_float2();
    }
    g_CurrentSetup.pathwaypoints=NULL; g_CurrentSetup.waypointgroups=NULL;
    gevrExpandedPrepareSetup();
    int doors=0, scenery=0, guns=0, ammo=0, armour=0;
    for(PropDefHeaderRecord *p=g_CurrentSetup.propDefs;p->type!=PROPDEF_END;p=(void *)((u32 *)p+sizepropdef(p))) {
        switch(p->type) {
        case PROPDEF_DOOR: {
            DoorRecord *d=(void *)p; doors++;
            assert(!d->keyflags && !d->linkedDoorOffset && !(d->flags&PROPFLAG_CANNOT_ACTIVATE));
            assert(!(d->flags2 & (PROPFLAG2_08000000|PROPFLAG2_10000000|PROPFLAG2_40000000)));
            break;
        }
        case PROPDEF_DOOR_SCALE: break;
        case PROPDEF_PROP: case PROPDEF_MONITOR: case PROPDEF_MULTI_MONITOR: case PROPDEF_RACK: case PROPDEF_GLASS: case PROPDEF_TINTED_GLASS: scenery++;break;
        case PROPDEF_COLLECTABLE: guns++;break;
        case PROPDEF_AMMO: ammo++;break;
        case PROPDEF_ARMOUR: armour++;break;
        default: assert(!"mission record entered expanded setup");
        }
    }
    assert(doors>0 && guns==9 && ammo==8 && armour==2);
    printf("PASS: public campaign setup records: %d unlocked doors, %d scenery, %d weapons, %d ammo, %d armour\n",doors,scenery,guns,ammo,armour);
    for(int i=0;i<allocated;i++)free(allocations[i]);
    return 0;
}

int main(int argc, char **argv) {
    if(argc==3)return reference(argv[1],argv[2]);
    PadRecord pads[312] = {0}; BoundPadRecord bounds[3] = {0};
    stagesetup *solo = (void *)campaign;
    solo->pads = (void *)128;
    solo->boundpads = (void *)(128 + sizeof(pads));
    solo->propDefs = (void *)(128 + sizeof(pads) + sizeof(bounds));
    PadRecord *raw = (void *)(campaign + (uintptr_t)solo->pads);
    BoundPadRecord *rawBounds = (void *)(campaign + (uintptr_t)solo->boundpads);
    strcpy((char *)campaign + 64000, "floor");
    for (int i = 0; i < 311; i++) {
        pads[i].plink = "floor"; pads[i].stan = &tile;
        pads[i].pos = (coord3d){ .x = i * 100, .y = i * 10, .z = i * -40 };
        pads[i].up.y = 1; pads[i].look.x = 1;
        raw[i] = pads[i]; raw[i].plink = (void *)64000; raw[i].stan = NULL;
        for (int axis = 0; axis < 3; axis++) raw[i].pos.f[axis] /= get_room_data_float2();
    }
    for (int i = 0; i < 2; i++) {
        bounds[i].plink = "floor"; bounds[i].stan = &tile;
        rawBounds[i] = bounds[i]; rawBounds[i].plink = (void *)64000;
    }
    DoorRecord door = {0}; door.type = PROPDEF_DOOR; door.pad = 1;
    door.keyflags = 7; door.linkedDoorOffset = -23; door.flags = PROPFLAG_CANNOT_ACTIVATE | PROPFLAG_CULL_BEHIND_DOOR;
    door.flags2 = PROPFLAG2_08000000 | PROPFLAG2_10000000 | PROPFLAG2_40000000 | PROPFLAG2_00400000;
    ObjectRecord tank = {0}; tank.type = PROPDEF_PROP; tank.pad = 88;
    tank.flags2 = PROPFLAG2_00000010 | PROPFLAG2_01000000; tank.obj = 77;
    GuardRecord guard = {0}; guard.type = PROPDEF_GUARD;
    struct TagObjectRecord tag = {0}; tag.type = PROPDEF_TAG;
    struct MissionObjectiveRecord objective = {0}; objective.type = PROPDEF_OBJECTIVE_START;
    PropDefHeaderRecord end = {0}; end.type = PROPDEF_END;
    unsigned char *out = campaign + (uintptr_t)solo->propDefs;
    PropDefHeaderRecord objectiveEnd={0};objectiveEnd.type=PROPDEF_OBJECTIVE_END;
    out=append(out,&objectiveEnd); /* deliberately misalign the host records */
    out = append(out, &door); out = append(out, &tank); out = append(out, &guard);
    out = append(out, &tag); out = append(out, &objective);
    tank.pad = -1; out = append(out, &tank); append(out, &end);

    stagesetup *templates=(void *)multiplayer; templates->propDefs=(void *)128;
    unsigned char *mp=multiplayer+128, original[4096]; out = mp;
    for (int i = 0; i < 8; i++) {
        WeaponObjRecord gun = {0}; gun.type = PROPDEF_COLLECTABLE; gun.weaponnum = 0xf0 + i;
        gun.pad = 5 + i; gun.extrascale = 512; gun.flags = 1;
        out = append(out, &gun);
    }
    MultiAmmoCrateRecord ammo = {0}; ammo.type = PROPDEF_AMMO; ammo.pad = 20; ammo.extrascale = 153;
    out = append(out, &ammo);
    BodyArmourRecord armour = {0}; armour.type = PROPDEF_ARMOUR; armour.pad = 35;
    s32 amount = 65536; memcpy(&armour.initialamount, &amount, 4);
    out = append(out, &armour);
    WeaponObjRecord token={0};token.type=PROPDEF_COLLECTABLE;token.weaponnum=ITEM_TOKEN;
    out=append(out,&token); append(out, &end); memcpy(original, mp, sizeof(original));
    struct { SetupIntroSpawn starts[8]; struct SetupIntroItem gun; struct SetupIntroCuff cuff; s32 end; } intro = {0};
    for (int i = 0; i < 8; i++) intro.starts[i] = (SetupIntroSpawn){INTROTYPE_SPAWN, i, 0};
    intro.gun.type = INTROTYPE_ITEM; intro.gun.item_right = ITEM_UNARMED; intro.gun.item_left = -1;
    intro.cuff.type = INTROTYPE_CUFF; intro.cuff.bondtype = 3; intro.end = INTROTYPE_END;
    unsigned char composed[32768]; size_t composedBytes = 0;
    for (int reload = 0; reload < 2; reload++) {
        g_CurrentSetup = (stagesetup){.pads=pads, .boundpads=bounds, .propDefs=(void *)(campaign+(uintptr_t)solo->propDefs), .intro=(void *)&intro,
            .ailists=(void *)1, .patrolpaths=(void *)1, .pathwaypoints=NULL, .waypointgroups=NULL};
        gevrExpandedPrepareSetup();
        assert(!memcmp(original, mp, sizeof(original))); /* original map/templates untouched */
        assert(!g_CurrentSetup.ailists && !g_CurrentSetup.patrolpaths && !g_CurrentSetup.pathwaypoints && !g_CurrentSetup.waypointgroups);
        int counts[PROPDEF_MAX] = {0}, gunSlots[8] = {0};
        PropDefHeaderRecord *p = g_CurrentSetup.propDefs;
        for (; p->type != PROPDEF_END; p = (void *)((u32 *)p + sizepropdef(p))) {
            counts[p->type]++;
            if (p->type == PROPDEF_DOOR) {
                DoorRecord *d = (void *)p;
                assert(!d->keyflags && !d->linkedDoorOffset && !(d->flags & PROPFLAG_CANNOT_ACTIVATE) && !d->flags2);
                assert(d->flags & PROPFLAG_CULL_BEHIND_DOOR);
            }
            if (p->type == PROPDEF_PROP) { ObjectRecord *o=(void *)p; assert(o->obj==77 && !o->flags2); }
            if (p->type == PROPDEF_COLLECTABLE) {
                unsigned slot=(u8)((WeaponObjRecord *)p)->weaponnum-0xf0u;
                if(slot<8)gunSlots[slot]++;else assert(((WeaponObjRecord *)p)->weaponnum==ITEM_TOKEN);
            };
        }
        assert(counts[PROPDEF_DOOR]==1 && counts[PROPDEF_PROP]==1 && counts[PROPDEF_COLLECTABLE]==9);
        assert(counts[PROPDEF_AMMO]==8 && counts[PROPDEF_ARMOUR]==2);
        for (int i=0;i<8;i++) assert(gunSlots[i]==1);
        size_t bytes = (unsigned char *)p + sizeof(*p) - (unsigned char *)g_CurrentSetup.propDefs;
        if (reload) assert(bytes==composedBytes && !memcmp(composed, g_CurrentSetup.propDefs, bytes));
        else { composedBytes=bytes; memcpy(composed,g_CurrentSetup.propDefs,bytes); }
        SetupIntroSpawn *starts=(void *)g_CurrentSetup.intro;
        for (int i=0;i<8;i++) {
            assert(starts[i].type==INTROTYPE_SPAWN && !starts[i].is_demo_playback);
            assert(starts[i].index >= 0 && g_CurrentSetup.pads[starts[i].index].stan==&tile);
            for(int j=0;j<i;j++) assert(starts[i].index!=starts[j].index);
        }
        assert(((struct SetupIntroItem *)(starts+8))->type==INTROTYPE_ITEM);
        for(int i=311;i<338;i++) {
            PadRecord *p=&g_CurrentSetup.pads[i];
            assert(p->plink && p->stan==&tile);
            for (PropDefHeaderRecord *q=g_CurrentSetup.propDefs;q->type!=PROPDEF_END;q=(void *)((u32 *)q+sizepropdef(q)))
                if(q->type==PROPDEF_COLLECTABLE || q->type==PROPDEF_AMMO || q->type==PROPDEF_ARMOUR)
                    for(int j=0;j<8;j++) assert(((ObjectRecord *)q)->pad!=starts[j].index);
        }
        assert(!g_CurrentSetup.pads[338].plink);
    }
    for(int i=0;i<allocated;i++) free(allocations[i]);
    puts("PASS: expanded setup composition, unlocked doors, no mission entities, MP weapon slots, 8 starts, deterministic reloads");
    return 0;
}
