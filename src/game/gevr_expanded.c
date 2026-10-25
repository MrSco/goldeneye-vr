/* Campaign-derived arenas. Mission entities/AI never enter the deathmatch
 * stream. Template pickups retain retail weapon-set and scenario behavior. */
#include <ultra64.h>
#include <bondtypes.h>
#include <bondconstants.h>
#include <memp.h>
#include <math.h>
#include <string.h>
#include "chrai.h"
#include "ob.h"
#include "stan.h"
#include "system.h"
#include "gevr_expanded.h"

extern s32 sizepropdef(PropDefHeaderRecord *p); /* loadobjectmodel.c */
#define STARTS 8
#define STATIONS 8
#define PLACEMENTS (STARTS + STATIONS * 2 + 3)
#define CANDIDATES 1024

static PropDefHeaderRecord *nextProp(PropDefHeaderRecord *p)
{
    return (PropDefHeaderRecord *)((u32 *)p + sizepropdef(p));
}

static int scenery(const PropDefHeaderRecord *p)
{
    switch (p->type) {
    case PROPDEF_DOOR_SCALE: return 1;
    case PROPDEF_DOOR: case PROPDEF_PROP: case PROPDEF_MONITOR: case PROPDEF_MULTI_MONITOR: case PROPDEF_RACK:
    case PROPDEF_GLASS: case PROPDEF_TINTED_GLASS:
    {
        /* Cartridge command streams are word-aligned; host structs contain
         * 64-bit pointers. Read through an aligned copy before inspecting. */
        ObjectRecord obj;
        memcpy(&obj,p,sizeof(obj));
        return obj.pad >= 0 && !(obj.flags & (PROPFLAG_INSIDEANOTHEROBJ | PROPFLAG_ASSIGNEDTOCHR));
    }
    default: return 0;
    }
}

static u8 *copyProp(u8 *out, PropDefHeaderRecord *p)
{
    size_t size = sizepropdef(p) * sizeof(u32);
    memcpy(out, p, size);
    return out + size;
}

static f32 distance2(const coord3d *a, const coord3d *b)
{
    f32 x=a->x-b->x, y=a->y-b->y, z=a->z-b->z;
    return x*x+y*y+z*z;
}

/* Use authored navigation/guard locations, not prop pads on desks or camera
 * positions. The all-pad pass is only a fallback for sparse mission paths. */
static void candidate(int id, PadRecord *pool, int *count, int padCount)
{
    if (id < 0 || id >= padCount || *count == CANDIDATES) return;
    PadRecord p=g_CurrentSetup.pads[id];
    if (!p.stan || p.up.y < .5f) return;
    p.pos.y=stanGetPositionYValue(p.stan,p.pos.x,p.pos.z);
    if (!isfinite(p.pos.y)) return;
    StandTile *tile=p.stan;
    if (stanTestVolume(&tile,p.pos.x,p.pos.z,30.0f,CDTYPE_BG,0.0f,1.0f)>=0) return;
    for (int i=0;i<*count;i++) if (distance2(&pool[i].pos,&p.pos)<100.0f*100.0f) return;
    pool[(*count)++]=p;
}

void gevrExpandedPrepareSetup(void)
{
    int padCount=0, count=0;
    while (g_CurrentSetup.pads[padCount].plink) padCount++;
    PadRecord *pool=mempAllocBytesInBank(CANDIDATES*sizeof(*pool),MEMPOOL_STAGE);
    if (g_CurrentSetup.pathwaypoints)
        for (waypoint *w=g_CurrentSetup.pathwaypoints;w->padID>=0;w++) candidate(w->padID,pool,&count,padCount);
    PropDefHeaderRecord *campaign=g_CurrentSetup.propDefs;
    for (PropDefHeaderRecord *p=campaign;p->type!=PROPDEF_END;p=nextProp(p))
        if (p->type==PROPDEF_GUARD) {
            GuardRecord guard;memcpy(&guard,p,sizeof(guard));
            candidate(guard.PadID,pool,&count,padCount);
        }
    if (count<PLACEMENTS)
        for(int i=0;i<padCount;i++) candidate(i,pool,&count,padCount);
    if (!count) sysFatalError("expanded arena: no usable floor pads");

    /* Farthest-point sampling covers the campaign's additional rooms. Starts
     * and pickups have separate positions; every peer chooses identically.
     * Small maps may reuse floor locations after exhausting distinct pads. */
    PadRecord *pads=mempAllocBytesInBank((padCount+PLACEMENTS+1)*sizeof(*pads),MEMPOOL_STAGE);
    memcpy(pads,g_CurrentSetup.pads,padCount*sizeof(*pads));
    for(int i=0;i<PLACEMENTS;i++) {
        int best=0; f32 far=-1;
        for(int j=0;j<count;j++) {
            f32 near=1e30f;
            for(int k=(i/count)*count;k<i;k++) {
                f32 d=distance2(&pool[j].pos,&pads[padCount+k].pos);
                if(d<near)near=d;
            }
            if(near>far) {far=near;best=j;}
        }
        pads[padCount+i]=pool[best];
    }
    /* Spawn pads are floor positions as in the retail MP intros. Pickup pads
     * get a small lift and use the ordinary object's floor/fall handling. */
    for(int i=STARTS;i<PLACEMENTS;i++) pads[padCount+i].pos.y+=20.0f;
    memset(&pads[padCount+PLACEMENTS],0,sizeof(*pads));

    stagesetup *templates=_fileNameLoadToBank("Ump_setuparkZ",FILELOADMETHOD_DEFAULT,256,MEMPOOL_STAGE);
    if(!templates)sysFatalError("expanded arena: missing MP templates");
    WeaponObjRecord *weapons[STATIONS]={0}, *token=NULL;
    MultiAmmoCrateRecord *ammo=NULL;
    BodyArmourRecord *armour=NULL;
    PropDefHeaderRecord *mp=(void *)((u8 *)templates+(uintptr_t)templates->propDefs);
    for(PropDefHeaderRecord *p=mp;p->type!=PROPDEF_END;p=nextProp(p)) {
        if(p->type==PROPDEF_COLLECTABLE) {
            WeaponObjRecord w;memcpy(&w,p,sizeof(w));
            unsigned slot=(u8)w.weaponnum-0xf0u;
            if(slot<STATIONS)weapons[slot]=(void *)p;
            if((u8)w.weaponnum==ITEM_TOKEN)token=(void *)p;
        }
        if(p->type==PROPDEF_AMMO)ammo=(void *)p;
        if(p->type==PROPDEF_ARMOUR)armour=(void *)p;
    }
    if(!ammo || !armour || !token)sysFatalError("expanded arena: incomplete MP templates");
    for(int i=0;i<STATIONS;i++) if(!weapons[i])sysFatalError("expanded arena: missing MP weapon slot");

    size_t bytes=sizeof(PropDefHeaderRecord)+sizeof(WeaponObjRecord)*(STATIONS+1)+
        sizeof(MultiAmmoCrateRecord)*STATIONS+2*sizeof(BodyArmourRecord);
    for(PropDefHeaderRecord *p=campaign;p->type!=PROPDEF_END;p=nextProp(p))
        if(scenery(p))bytes+=sizepropdef(p)*sizeof(u32);
    u8 *props=mempAllocBytesInBank(bytes,MEMPOOL_STAGE), *out=props;
    for(PropDefHeaderRecord *p=campaign;p->type!=PROPDEF_END;p=nextProp(p)) {
        if(!scenery(p))continue;
        ObjectRecord *obj=(void *)out; out=copyProp(out,p);
        if(p->type==PROPDEF_DOOR_SCALE)continue;
        /* No mission difficulty/player exclusions; no live mission pointers. */
        obj->flags2 &= ~(0x000000f8u | 0x01c00000u);
        if(obj->type==PROPDEF_DOOR) {
            DoorRecord *door=(void *)obj;
            /* Compaction changes command indices, so remove relative links.
             * Each leaf remains usable without mission scripts or keys. */
            door->linkedDoorOffset=0; door->keyflags=0;
            door->flags &= ~PROPFLAG_CANNOT_ACTIVATE;
            door->flags2 &= ~(PROPFLAG2_08000000|PROPFLAG2_10000000|PROPFLAG2_20000000|PROPFLAG2_40000000);
        }
    }
    int place=padCount+STARTS;
    for(int i=0;i<STATIONS;i++) {
        WeaponObjRecord *gun=(void *)out; out=copyProp(out,(void *)weapons[i]); gun->pad=place++;
        /* Keep ammo immediately after its gun: prop.c uses lastmpweaponnum. */
        MultiAmmoCrateRecord *crate=(void *)out; out=copyProp(out,(void *)ammo); crate->pad=place++;
    }
    for(int i=0;i<2;i++) {
        BodyArmourRecord *vest=(void *)out; out=copyProp(out,(void *)armour); vest->pad=place++;
    }
    WeaponObjRecord *flag=(void *)out; out=copyProp(out,(void *)token); flag->pad=place++;
    memset(out,0,sizeof(PropDefHeaderRecord)); ((PropDefHeaderRecord *)out)->type=PROPDEF_END;
    struct ArenaIntro {
        SetupIntroSpawn spawn[STARTS];
        struct SetupIntroItem item;
        struct SetupIntroCuff cuff;
        SetupIntroEmpty end;
    } *intro=mempAllocBytesInBank(sizeof(*intro),MEMPOOL_STAGE);
    memset(intro,0,sizeof(*intro));
    for(int i=0;i<STARTS;i++)intro->spawn[i]=(SetupIntroSpawn){INTROTYPE_SPAWN,padCount+i,0};
    intro->item.type=INTROTYPE_ITEM;intro->item.item_right=ITEM_UNARMED;intro->item.item_left=-1;
    intro->cuff.type=INTROTYPE_CUFF;intro->cuff.bondtype=3;intro->end.type=INTROTYPE_END;
    g_CurrentSetup.propDefs=(void *)props;g_CurrentSetup.pads=pads;g_CurrentSetup.intro=(void *)intro;
    g_CurrentSetup.ailists=NULL;g_CurrentSetup.patrolpaths=NULL;
    g_CurrentSetup.pathwaypoints=NULL;g_CurrentSetup.waypointgroups=NULL;
    sysLogPrintf(LOG_NOTE,"expanded arena: %d candidate floors, 8 starts, 8 weapon/ammo stations",count);
}
