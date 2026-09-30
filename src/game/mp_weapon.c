#include <ultra64.h>
#include <bondconstants.h>
#include "mp_weapon.h"
#include "assets/obseg/text/LmpweaponsE.h"
// data
//D:80048670
struct s_mp_weapon_set mp_weapon_set_slaps[] = 
{
    INLINE_S_MP_WEAPON_SET(ITEM_UNARMED, PROP_CHRTT33, 1.0, AMMO_9MM, 0, 1),
    INLINE_S_MP_WEAPON_SET(ITEM_UNARMED, PROP_CHRTT33, 1.0, AMMO_9MM, 0, 1),
    INLINE_S_MP_WEAPON_SET(ITEM_UNARMED, PROP_CHRTT33, 1.0, AMMO_9MM, 0, 1),
    INLINE_S_MP_WEAPON_SET(ITEM_UNARMED, PROP_CHRTT33, 1.0, AMMO_9MM, 0, 1),
    INLINE_S_MP_WEAPON_SET(ITEM_UNARMED, PROP_CHRTT33, 1.0, AMMO_9MM, 0, 1),
    INLINE_S_MP_WEAPON_SET(ITEM_UNARMED, PROP_CHRTT33, 1.0, AMMO_9MM, 0, 1),
    INLINE_S_MP_WEAPON_SET(ITEM_UNARMED, PROP_CHRTT33, 1.0, AMMO_9MM, 0, 1),
    INLINE_S_MP_WEAPON_SET(ITEM_UNARMED, PROP_CHRTT33, 1.0, AMMO_9MM, 0, 1)
};

//D:80048730
struct s_mp_weapon_set mp_weapon_set_pistols[] = 
{
    INLINE_S_MP_WEAPON_SET(ITEM_TT33, PROP_CHRTT33, 1.0, AMMO_9MM, 0x32, 1),
    INLINE_S_MP_WEAPON_SET(ITEM_TT33, PROP_CHRTT33, 1.0, AMMO_9MM, 0x32, 1),
    INLINE_S_MP_WEAPON_SET(ITEM_TT33, PROP_CHRTT33, 1.0, AMMO_9MM, 0x32, 1),
    INLINE_S_MP_WEAPON_SET(ITEM_WPPKSIL, PROP_CHRWPPKSIL, 1.0, AMMO_9MM, 0x32, 1),
    INLINE_S_MP_WEAPON_SET(ITEM_WPPKSIL, PROP_CHRWPPKSIL, 1.0, AMMO_9MM, 0x32, 1),
    INLINE_S_MP_WEAPON_SET(ITEM_WPPKSIL, PROP_CHRWPPKSIL, 1.0, AMMO_9MM, 0x32, 1),
    INLINE_S_MP_WEAPON_SET(ITEM_RUGER, PROP_CHRRUGER, 1.0, AMMO_MAGNUM, 0x32, 1),
    INLINE_S_MP_WEAPON_SET(ITEM_RUGER, PROP_CHRRUGER, 1.0, AMMO_MAGNUM, 0x32, 1)
};

//D:800487F0
struct s_mp_weapon_set mp_weapon_set_knife[] = 
{
    INLINE_S_MP_WEAPON_SET(ITEM_THROWKNIFE, PROP_CHRTHROWKNIFE, 1.0, AMMO_KNIFE, 0xA, 0),
    INLINE_S_MP_WEAPON_SET(ITEM_THROWKNIFE, PROP_CHRTHROWKNIFE, 1.0, AMMO_KNIFE, 0xA, 0),
    INLINE_S_MP_WEAPON_SET(ITEM_THROWKNIFE, PROP_CHRTHROWKNIFE, 1.0, AMMO_KNIFE, 0xA, 0),
    INLINE_S_MP_WEAPON_SET(ITEM_THROWKNIFE, PROP_CHRTHROWKNIFE, 1.0, AMMO_KNIFE, 0xA, 0),
    INLINE_S_MP_WEAPON_SET(ITEM_THROWKNIFE, PROP_CHRTHROWKNIFE, 1.0, AMMO_KNIFE, 0xA, 0),
    INLINE_S_MP_WEAPON_SET(ITEM_THROWKNIFE, PROP_CHRTHROWKNIFE, 1.0, AMMO_KNIFE, 0xA, 0),
    INLINE_S_MP_WEAPON_SET(ITEM_THROWKNIFE, PROP_CHRTHROWKNIFE, 1.0, AMMO_KNIFE, 0xA, 0),
    INLINE_S_MP_WEAPON_SET(ITEM_THROWKNIFE, PROP_CHRTHROWKNIFE, 1.0, AMMO_KNIFE, 0xA, 0)
};

//D:800488B0
struct s_mp_weapon_set mp_weapon_set_auto[] = 
{
    INLINE_S_MP_WEAPON_SET(ITEM_WPPKSIL, PROP_CHRWPPKSIL, 1.0, AMMO_9MM, 0x32, 1),
    INLINE_S_MP_WEAPON_SET(ITEM_WPPKSIL, PROP_CHRWPPKSIL, 1.0, AMMO_9MM, 0x32, 1),
    INLINE_S_MP_WEAPON_SET(ITEM_TT33, PROP_CHRTT33, 1.0, AMMO_9MM, 0x32, 1),
    INLINE_S_MP_WEAPON_SET(ITEM_TT33, PROP_CHRTT33, 1.0, AMMO_9MM, 0x32, 1),
    INLINE_S_MP_WEAPON_SET(ITEM_SKORPION, PROP_CHRSKORPION, 1.5, AMMO_9MM, 0x64, 1),
    INLINE_S_MP_WEAPON_SET(ITEM_SKORPION, PROP_CHRSKORPION, 1.5, AMMO_9MM, 0x64, 1),
    INLINE_S_MP_WEAPON_SET(ITEM_MP5K, PROP_CHRMP5K, 1.0, AMMO_9MM, 0x64, 1),
    INLINE_S_MP_WEAPON_SET(ITEM_MP5K, PROP_CHRMP5K, 1.0, AMMO_9MM, 0x64, 1)
};

//D:80048970
struct s_mp_weapon_set mp_weapon_set_power[] = 
{
    INLINE_S_MP_WEAPON_SET(ITEM_TT33, PROP_CHRTT33, 3.0, AMMO_9MM, 0x32, 1),
    INLINE_S_MP_WEAPON_SET(ITEM_TT33, PROP_CHRTT33, 3.0, AMMO_9MM, 0x32, 1),
    INLINE_S_MP_WEAPON_SET(ITEM_RUGER, PROP_CHRRUGER, 1.0, AMMO_MAGNUM, 0x32, 1),
    INLINE_S_MP_WEAPON_SET(ITEM_RUGER, PROP_CHRRUGER, 1.0, AMMO_MAGNUM, 0x32, 1),
    INLINE_S_MP_WEAPON_SET(ITEM_FNP90, PROP_CHRFNP90, 1.0, AMMO_9MM, 0x64, 1),
    INLINE_S_MP_WEAPON_SET(ITEM_FNP90, PROP_CHRFNP90, 1.0, AMMO_9MM, 0x64, 1),
    INLINE_S_MP_WEAPON_SET(ITEM_AUTOSHOT, PROP_CHRAUTOSHOT, 1.0, AMMO_SHOTGUN, 0x1E, 1),
    INLINE_S_MP_WEAPON_SET(ITEM_AUTOSHOT, PROP_CHRAUTOSHOT, 1.0, AMMO_SHOTGUN, 0x1E, 1)
};

//D:80048A30
struct s_mp_weapon_set mp_weapon_set_sniper[] = 
{
    INLINE_S_MP_WEAPON_SET(ITEM_TT33, PROP_CHRTT33, 3.0, AMMO_9MM, 0x32, 1),
    INLINE_S_MP_WEAPON_SET(ITEM_TT33, PROP_CHRTT33, 3.0, AMMO_9MM, 0x32, 1),
    INLINE_S_MP_WEAPON_SET(ITEM_RUGER, PROP_CHRRUGER, 1.0, AMMO_MAGNUM, 0x32, 1),
    INLINE_S_MP_WEAPON_SET(ITEM_RUGER, PROP_CHRRUGER, 1.0, AMMO_MAGNUM, 0x32, 1),
    INLINE_S_MP_WEAPON_SET(ITEM_SKORPION, PROP_CHRSKORPION, 1.0, AMMO_9MM, 0x64, 1),
    INLINE_S_MP_WEAPON_SET(ITEM_SKORPION, PROP_CHRSKORPION, 1.0, AMMO_9MM, 0x64, 1),
    INLINE_S_MP_WEAPON_SET(ITEM_SNIPERRIFLE, PROP_CHRSNIPERRIFLE, 1.0, AMMO_RIFLE, 0x32, 1),
    INLINE_S_MP_WEAPON_SET(ITEM_SNIPERRIFLE, PROP_CHRSNIPERRIFLE, 1.0, AMMO_RIFLE, 0x32, 1)
};

//D:80048AF0
struct s_mp_weapon_set mp_weapon_set_grenade[] = 
{
    INLINE_S_MP_WEAPON_SET(ITEM_TT33, PROP_CHRTT33, 3.0, AMMO_9MM, 0x32, 1),
    INLINE_S_MP_WEAPON_SET(ITEM_TT33, PROP_CHRTT33, 3.0, AMMO_9MM, 0x32, 1),
    INLINE_S_MP_WEAPON_SET(ITEM_SKORPION, PROP_CHRSKORPION, 1.5, AMMO_9MM, 0x64, 1),
    INLINE_S_MP_WEAPON_SET(ITEM_SKORPION, PROP_CHRSKORPION, 1.5, AMMO_9MM, 0x64, 1),
    INLINE_S_MP_WEAPON_SET(ITEM_AK47, PROP_CHRKALASH, 1.5, AMMO_RIFLE, 0x64, 1),
    INLINE_S_MP_WEAPON_SET(ITEM_AK47, PROP_CHRKALASH, 1.5, AMMO_RIFLE, 0x64, 1),
    INLINE_S_MP_WEAPON_SET(ITEM_GRENADE, PROP_CHRGRENADE, 1.5, AMMO_GRENADE, 5, 0),
    INLINE_S_MP_WEAPON_SET(ITEM_GRENADE, PROP_CHRGRENADE, 1.5, AMMO_GRENADE, 5, 0)
};

//D:80048BB0
struct s_mp_weapon_set mp_weapon_set_remote_m[] = 
{
    INLINE_S_MP_WEAPON_SET(ITEM_WPPK, PROP_CHRWPPK, 3.0, AMMO_9MM, 0x32, 1),
    INLINE_S_MP_WEAPON_SET(ITEM_WPPK, PROP_CHRWPPK, 3.0, AMMO_9MM, 0x32, 1),
    INLINE_S_MP_WEAPON_SET(ITEM_UZI, PROP_CHRUZI, 1.5, AMMO_9MM, 0x64, 1),
    INLINE_S_MP_WEAPON_SET(ITEM_UZI, PROP_CHRUZI, 1.5, AMMO_9MM, 0x64, 1),
#ifdef BUGFIX_R0
    INLINE_S_MP_WEAPON_SET(ITEM_M16, PROP_CHRKALASH, 1.5, AMMO_RIFLE, 0x64, 1),
    INLINE_S_MP_WEAPON_SET(ITEM_M16, PROP_CHRKALASH, 1.5, AMMO_RIFLE, 0x64, 1),
#else
    INLINE_S_MP_WEAPON_SET(ITEM_M16, PROP_CHRM16, 1.5, AMMO_RIFLE, 0x64, 1),
    INLINE_S_MP_WEAPON_SET(ITEM_M16, PROP_CHRM16, 1.5, AMMO_RIFLE, 0x64, 1),
#endif
    INLINE_S_MP_WEAPON_SET(ITEM_REMOTEMINE, PROP_CHRREMOTEMINE, 1.5, AMMO_REMOTEMINE, 5, 0),
    INLINE_S_MP_WEAPON_SET(ITEM_REMOTEMINE, PROP_CHRREMOTEMINE, 1.5, AMMO_REMOTEMINE, 5, 0)
};


//D:80048C70
struct s_mp_weapon_set mp_weapon_set_glaunch[] = 
{
    INLINE_S_MP_WEAPON_SET(ITEM_TT33, PROP_CHRTT33, 3.0, AMMO_9MM, 0x32, 1),
    INLINE_S_MP_WEAPON_SET(ITEM_TT33, PROP_CHRTT33, 3.0, AMMO_9MM, 0x32, 1),
    INLINE_S_MP_WEAPON_SET(ITEM_SKORPION, PROP_CHRSKORPION, 1.5, AMMO_9MM, 0x64, 1),
    INLINE_S_MP_WEAPON_SET(ITEM_SKORPION, PROP_CHRSKORPION, 1.5, AMMO_9MM, 0x64, 1),
    INLINE_S_MP_WEAPON_SET(ITEM_AK47, PROP_CHRKALASH, 1.5, AMMO_RIFLE, 0x64, 1),
    INLINE_S_MP_WEAPON_SET(ITEM_AK47, PROP_CHRKALASH, 1.5, AMMO_RIFLE, 0x64, 1),
    INLINE_S_MP_WEAPON_SET(ITEM_GRENADELAUNCH, PROP_CHRGRENADELAUNCH, 1.0, AMMO_GRENADEROUND, 6, 1),
    INLINE_S_MP_WEAPON_SET(ITEM_GRENADELAUNCH, PROP_CHRGRENADELAUNCH, 1.0, AMMO_GRENADEROUND, 6, 1)
};

//D:80048D30
struct s_mp_weapon_set mp_weapon_set_timed_m[] = 
{
    INLINE_S_MP_WEAPON_SET(ITEM_WPPK, PROP_CHRWPPK, 3.0, AMMO_9MM, 0x32, 1),
    INLINE_S_MP_WEAPON_SET(ITEM_WPPK, PROP_CHRWPPK, 3.0, AMMO_9MM, 0x32, 1),
    INLINE_S_MP_WEAPON_SET(ITEM_UZI, PROP_CHRUZI, 1.5, AMMO_9MM, 0x64, 1),
    INLINE_S_MP_WEAPON_SET(ITEM_UZI, PROP_CHRUZI, 1.5, AMMO_9MM, 0x64, 1),
#ifdef BUGFIX_R0
    INLINE_S_MP_WEAPON_SET(ITEM_M16, PROP_CHRKALASH, 1.5, AMMO_RIFLE, 0x64, 1),
    INLINE_S_MP_WEAPON_SET(ITEM_M16, PROP_CHRKALASH, 1.5, AMMO_RIFLE, 0x64, 1),
#else
    INLINE_S_MP_WEAPON_SET(ITEM_M16, PROP_CHRM16, 1.5, AMMO_RIFLE, 0x64, 1),
    INLINE_S_MP_WEAPON_SET(ITEM_M16, PROP_CHRM16, 1.5, AMMO_RIFLE, 0x64, 1),
#endif
    INLINE_S_MP_WEAPON_SET(ITEM_TIMEDMINE, PROP_CHRTIMEDMINE, 1.5, AMMO_TIMEDMINE, 5, 0),
    INLINE_S_MP_WEAPON_SET(ITEM_TIMEDMINE, PROP_CHRTIMEDMINE, 1.5, AMMO_TIMEDMINE, 5, 0)
};

//D:80048DF0 
struct s_mp_weapon_set mp_weapon_set_prox_m[] = 
{
    INLINE_S_MP_WEAPON_SET(ITEM_WPPK, PROP_CHRWPPK, 3.0, AMMO_9MM, 0x32, 1),
    INLINE_S_MP_WEAPON_SET(ITEM_WPPK, PROP_CHRWPPK, 3.0, AMMO_9MM, 0x32, 1),
    INLINE_S_MP_WEAPON_SET(ITEM_UZI, PROP_CHRUZI, 1.5, AMMO_9MM, 0x64, 1),
    INLINE_S_MP_WEAPON_SET(ITEM_UZI, PROP_CHRUZI, 1.5, AMMO_9MM, 0x64, 1),
    INLINE_S_MP_WEAPON_SET(ITEM_M16, PROP_CHRM16, 1.5, AMMO_RIFLE, 0x64, 1),
    INLINE_S_MP_WEAPON_SET(ITEM_M16, PROP_CHRM16, 1.5, AMMO_RIFLE, 0x64, 1),
    INLINE_S_MP_WEAPON_SET(ITEM_PROXIMITYMINE, PROP_CHRPROXIMITYMINE, 1.5, AMMO_PROXMINE, 5, 0),
    INLINE_S_MP_WEAPON_SET(ITEM_PROXIMITYMINE, PROP_CHRPROXIMITYMINE, 1.5, AMMO_PROXMINE, 5, 0)
};

//D:80048EB0
struct s_mp_weapon_set mp_weapon_set_rockets[] = 
{
    INLINE_S_MP_WEAPON_SET(ITEM_TT33, PROP_CHRTT33, 3.0, AMMO_9MM, 0x32, 1),
    INLINE_S_MP_WEAPON_SET(ITEM_TT33, PROP_CHRTT33, 3.0, AMMO_9MM, 0x32, 1),
    INLINE_S_MP_WEAPON_SET(ITEM_SKORPION, PROP_CHRSKORPION, 1.5, AMMO_9MM, 0x64, 1),
    INLINE_S_MP_WEAPON_SET(ITEM_SKORPION, PROP_CHRSKORPION, 1.5, AMMO_9MM, 0x64, 1),
    INLINE_S_MP_WEAPON_SET(ITEM_AK47, PROP_CHRKALASH, 1.5, AMMO_RIFLE, 0x64, 1),
    INLINE_S_MP_WEAPON_SET(ITEM_AK47, PROP_CHRKALASH, 1.5, AMMO_RIFLE, 0x64, 1),
    INLINE_S_MP_WEAPON_SET(ITEM_ROCKETLAUNCH, PROP_CHRROCKETLAUNCH, 1.5, AMMO_ROCKETS, 6, 1),
    INLINE_S_MP_WEAPON_SET(ITEM_ROCKETLAUNCH, PROP_CHRROCKETLAUNCH, 1.5, AMMO_ROCKETS, 6, 1)
};

//D:80048F70
struct s_mp_weapon_set mp_weapon_set_lasers[] = 
{
    INLINE_S_MP_WEAPON_SET(ITEM_TT33, PROP_CHRTT33, 3.0, AMMO_9MM, 0x32, 1),
    INLINE_S_MP_WEAPON_SET(ITEM_TT33, PROP_CHRTT33, 3.0, AMMO_9MM, 0x32, 1),
    INLINE_S_MP_WEAPON_SET(ITEM_SKORPION, PROP_CHRSKORPION, 1.5, AMMO_9MM, 0x64, 1),
    INLINE_S_MP_WEAPON_SET(ITEM_SKORPION, PROP_CHRSKORPION, 1.5, AMMO_9MM, 0x64, 1),
    INLINE_S_MP_WEAPON_SET(ITEM_AK47, PROP_CHRKALASH, 1.5, AMMO_RIFLE, 0x64, 1),
    INLINE_S_MP_WEAPON_SET(ITEM_AK47, PROP_CHRKALASH, 1.5, AMMO_RIFLE, 0x64, 1),
    INLINE_S_MP_WEAPON_SET(ITEM_LASER, PROP_CHRLASER, 1.5, AMMO_NONE, 0, 1),
    INLINE_S_MP_WEAPON_SET(ITEM_LASER, PROP_CHRLASER, 1.5, AMMO_NONE, 0, 1)
};

//D:80049030
struct s_mp_weapon_set mp_weapon_set_golden[] = 
{
    INLINE_S_MP_WEAPON_SET(ITEM_TT33, PROP_CHRTT33, 3.0, AMMO_9MM, 0x32, 1),
    INLINE_S_MP_WEAPON_SET(ITEM_TT33, PROP_CHRTT33, 3.0, AMMO_9MM, 0x32, 1),
    INLINE_S_MP_WEAPON_SET(ITEM_SKORPION, PROP_CHRSKORPION, 1.5, AMMO_9MM, 0x64, 1),
    INLINE_S_MP_WEAPON_SET(ITEM_SKORPION, PROP_CHRSKORPION, 1.5, AMMO_9MM, 0x64, 1),
    INLINE_S_MP_WEAPON_SET(ITEM_AK47, PROP_CHRKALASH, 1.5, AMMO_RIFLE, 0x64, 1),
    INLINE_S_MP_WEAPON_SET(ITEM_AK47, PROP_CHRKALASH, 1.5, AMMO_RIFLE, 0x64, 1),
    INLINE_S_MP_WEAPON_SET(ITEM_WPPKSIL, PROP_CHRWPPKSIL, 1.0, AMMO_9MM, 0x32, 1),
    INLINE_S_MP_WEAPON_SET(ITEM_GOLDENGUN, PROP_CHRGOLDEN, 1.5, AMMO_GGUN, 0xA, 1)
};

/* The host's own set online (net_core.c netApplyMatchConfig): four guns,
 * each in two slots as the presets pair them. mpBuildCustomWeaponSet fills
 * it before a stage load; the game's own menu never reaches it. */
struct s_mp_weapon_set mp_weapon_set_custom[8];

//D:800490F0
struct s_mp_weapon_set_text mp_weapon_set_text_table[] = 
{
    {getStringID(LMPWEAPONS, MPWEAPON_STR_00_SLAPPERSONLY), mp_weapon_set_slaps},
    {getStringID(LMPWEAPONS, MPWEAPON_STR_01_PISTOLS), mp_weapon_set_pistols},
    {getStringID(LMPWEAPONS, MPWEAPON_STR_0D_THROWINGKNIVES), mp_weapon_set_knife},
    {getStringID(LMPWEAPONS, MPWEAPON_STR_02_AUTOMATICS), mp_weapon_set_auto},
    {getStringID(LMPWEAPONS, MPWEAPON_STR_03_POWERWEAPONS), mp_weapon_set_power},
    {getStringID(LMPWEAPONS, MPWEAPON_STR_04_SNIPERRIFLES), mp_weapon_set_sniper},
    {getStringID(LMPWEAPONS, MPWEAPON_STR_05_GRENADES), mp_weapon_set_grenade},
    {getStringID(LMPWEAPONS, MPWEAPON_STR_06_REMOTEMINES), mp_weapon_set_remote_m},
    {getStringID(LMPWEAPONS, MPWEAPON_STR_07_GRENADELAUNCHERS), mp_weapon_set_glaunch},
    {getStringID(LMPWEAPONS, MPWEAPON_STR_08_TIMEDMINES), mp_weapon_set_timed_m},
    {getStringID(LMPWEAPONS, MPWEAPON_STR_09_PROXIMITYMINES), mp_weapon_set_prox_m},
    {getStringID(LMPWEAPONS, MPWEAPON_STR_0A_ROCKETS), mp_weapon_set_rockets},
    {getStringID(LMPWEAPONS, MPWEAPON_STR_0B_LASERS), mp_weapon_set_lasers},
    {getStringID(LMPWEAPONS, MPWEAPON_STR_0C_GOLDENGUN), mp_weapon_set_golden},
    {getStringID(LMPWEAPONS, MPWEAPON_STR_00_SLAPPERSONLY), mp_weapon_set_custom}   /* MP_WEAPON_SET_CUSTOM, online; net_match.c names it */
};

s32 mp_weapon_set = 0xB;

/* The preset entry that carries a gun - its model, size, ammo type and amount,
 * and whether the gun itself lies on the pad: the custom set and the spawn
 * loadouts take theirs from here. NULL for a gun no preset has. */
const struct s_mp_weapon_set *mpPresetEntryForItem(s32 item)
{
    s32 set;
    s32 slot;

    for (set = 0; set < MP_WEAPON_SET_CUSTOM; set++)
    {
        for (slot = 0; slot < 8; slot++)
        {
            if (mp_weapon_set_text_table[set].weapon_set[slot].itemID == item)
            {
                return &mp_weapon_set_text_table[set].weapon_set[slot];
            }
        }
    }
    return NULL;
}

void mpBuildCustomWeaponSet(const u8 items[4])
{
    extern PROP getPropForHeldItem(ITEM_IDS arg0);          /* player.c */
    extern s32 get_ammo_type_for_weapon(ITEM_IDS weapon);   /* gunfire.c */
    s32 i;

    for (i = 0; i < 4; i++)
    {
        const struct s_mp_weapon_set *e = mpPresetEntryForItem(items[i]);
        struct s_mp_weapon_set slot;

        if (e != NULL)
        {
            slot = *e;
        }
        else
        {
            /* a gun no preset has (the knife, the shotgun, the Phantom, the silenced D5K) */
            slot.itemID = items[i];
            slot.propID = getPropForHeldItem((ITEM_IDS) items[i]);
            slot.size = 1.0f;
            slot.ammotype = get_ammo_type_for_weapon((ITEM_IDS) items[i]);
            slot.ammoamount = 0x32;
            slot.allowpickup = 1;
            if (slot.propID < 0)
            {
                slot = mp_weapon_set_slaps[0];   /* nothing to place on the pad */
            }
        }
        mp_weapon_set_custom[2 * i] = slot;
        mp_weapon_set_custom[2 * i + 1] = slot;
    }
}


//increment mp_weapon_set by 1, capping at 0xE
void incrementMPWeaponSet(void)
{
    mp_weapon_set = (mp_weapon_set + 1) % 0xe;
}

//return pointer to selected mp_weapon_set textID
u16* getPtrMPWeaponSetTextID(void)
{
    return &mp_weapon_set_text_table[mp_weapon_set].textID;
}

//return pointer to selected mp_weapon_set data
struct s_mp_weapon_set* getPtrMPWeaponSetData(void)
{
    return mp_weapon_set_text_table[mp_weapon_set].weapon_set;
}

//set mp weapon set
void setMPWeaponSet(s32 setNUM)
{
    mp_weapon_set = setNUM;
}

//return mp weapon set
s32 getMPWeaponSet(void)
{
    return mp_weapon_set;
}
