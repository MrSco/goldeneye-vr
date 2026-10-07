#ifdef GEVR
#include "net_game.h"
#endif
#include <ultra64.h>
#include "bondview.h"
#include "chr.h"
#include "player.h"
#include "textrelated.h"
#include <bondconstants.h>
#include "language.h"
#include "bondinv.h"
#include "gun.h"
#ifdef GEVR
extern int VrGexGuns;
/* GE-X's remote mines detonate themselves (gun.c gevrGexMineDetonates): the
 * Detonator leaves the cycle, ammo required or not */
#define GEVR_CYCLE_OK(requireammo, item) \
    (((requireammo) == FALSE && !(VrGexGuns && (item) == ITEM_TRIGGER)) || bondwalkItemHasAmmo(item))
#else
#define GEVR_CYCLE_OK(requireammo, item) ((requireammo) == FALSE || bondwalkItemHasAmmo(item))
#endif
#include "lv.h"
#include <bondtypes.h>
#include "system.h"

void bondinvReinitInv(void)
{
    s32 i;

    for (i = 0; i < g_CurrentPlayer->equipmaxitems; i++)
    {
        g_CurrentPlayer->p_itemcur[i].type = -1;
    }

    g_CurrentPlayer->ptr_inventory_first_in_cycle = NULL;
    g_CurrentPlayer->textoverrides                = NULL;
    g_CurrentPlayer->equipcuritem                 = ITEM_UNARMED;
}

/**
 * Sorts subject into its correct position in the inventory list.
 *
 * Subject is expected to initially be at the head of the list. It works by
 * swapping the subject with the item to its right as many times as needed.
 */
void bondinvSortInv(InvItem *subject)
{
    InvItem *candidate;
    s32      subjweapon1 = -1;
    s32      subjweapon2 = -1;
    s32      candweapon1;
    s32      candweapon2;

    // Prepare subject's properties for comparisons
    if (subject->type == INV_ITEM_WEAPON)
    {
        subjweapon1 = subject->type_inv_item.type_weap.weapon;
    }
    else if (subject->type == INV_ITEM_DUAL)
    {
        subjweapon1 = subject->type_inv_item.type_dual.weapon_right;
        subjweapon2 = subject->type_inv_item.type_dual.weapon_left;
    }
    else if (subject->type == INV_ITEM_PROP)
    {
        subjweapon1 = 2000;
    }

    candidate = subject->next;

    while (g_CurrentPlayer->ptr_inventory_first_in_cycle != subject->next)
    {
        // Prepare candidate's properties for comparisons
        candweapon1 = -1;
        candweapon2 = -1;

        if (subject->next->type == INV_ITEM_WEAPON)
        {
            candweapon1 = subject->next->type_inv_item.type_weap.weapon;
        }
        else if (subject->next->type == INV_ITEM_DUAL)
        {
            candweapon1 = subject->next->type_inv_item.type_dual.weapon_right;
            candweapon2 = subject->next->type_inv_item.type_dual.weapon_left;
        }
        else if (subject->next->type == INV_ITEM_PROP)
        {
            candweapon1 = 1000;
        }

        // If the candidate should sort ahead of subject
        // then subject is in the desired position.
        if (candweapon1 >= subjweapon1 &&
            (subjweapon1 != candweapon1 || subjweapon2 <= candweapon2))
        {
            return;
        }

        // If there's only two items in the list then there's no point swapping
        // them. Just set the list head to the candidate.
        if (candidate->next == subject)
        {
            g_CurrentPlayer->ptr_inventory_first_in_cycle = candidate;
        }
        else
        {
            // Swap subject with candidate
            subject->next         = candidate->next;
            candidate->prev       = subject->prev;
            subject->prev         = candidate;
            candidate->next       = subject;
            subject->next->prev   = subject;
            candidate->prev->next = candidate;

            // Set new list head if subject was the head
            if (subject == g_CurrentPlayer->ptr_inventory_first_in_cycle)
            {
                g_CurrentPlayer->ptr_inventory_first_in_cycle = candidate;
            }
        }

        candidate = subject->next;
    }
}

void bondinvInsertItem(InvItem *item)
{
    if (g_CurrentPlayer->ptr_inventory_first_in_cycle)
    {
        item->next = g_CurrentPlayer->ptr_inventory_first_in_cycle;
        item->prev = g_CurrentPlayer->ptr_inventory_first_in_cycle->prev;

        item->next->prev = item;
        item->prev->next = item;
    }
    else
    {
        item->next = item;
        item->prev = item;
    }

    g_CurrentPlayer->ptr_inventory_first_in_cycle = item;
    bondinvSortInv(item);
    return;
}

void bondinvRemoveItem(InvItem *item)
{
    InvItem *prev;
    InvItem *next;

    next = item->next;
    prev = item->prev;

    if (item == g_CurrentPlayer->ptr_inventory_first_in_cycle)
    {
        if (item == item->next)
        {
            g_CurrentPlayer->ptr_inventory_first_in_cycle = NULL;
        }
        else
        {
            g_CurrentPlayer->ptr_inventory_first_in_cycle = item->next;
        }
    }

    next->prev = prev;
    prev->next = next;
    item->type = -1;
    return;
}

InvItem *bondinvGetNextAvailItem(void)
{
    int i;

    for (i = 0; i < g_CurrentPlayer->equipmaxitems; i++)
    {
        if (g_CurrentPlayer->p_itemcur[i].type == -1)
        {
            return &g_CurrentPlayer->p_itemcur[i];
        }
    }

    #ifdef DEBUG
    osSyncPrintf("equipgetfreeitem: No free equip items!!!!\n");
    #endif

    return NULL;
}

void bondinvSetAllGunsFlag(s32 all_guns)
{
    g_CurrentPlayer->equipallguns = all_guns;
}

s32 bondinvGetAllGunsFlag(void)
{
    return g_CurrentPlayer->equipallguns;
}

InvItem *bondinvGetInvItem(ITEM_IDS weapon)
{
    InvItem *first = g_CurrentPlayer->ptr_inventory_first_in_cycle;
    InvItem *item  = first;

    while (item)
    {
        if (item->type == INV_ITEM_WEAPON && item->type_inv_item.type_weap.weapon == weapon)
        {
            return item;
        }

        item = item->next;

        if (item == first)
        {
            break;
        }
    }

    return NULL;
}

/**
 * Is item in inventory
 * @param item: enum Item ID eg: ITEM_KNIFE
 * @return TRUE/FALSE
 */
int bondinvHasInvItem(ITEM_IDS item)
{
    return bondinvGetInvItem(item) != NULL;
}

InvItem *bondinvGetDualWeapon(ITEM_IDS right, ITEM_IDS left)
{
    InvItem *first = g_CurrentPlayer->ptr_inventory_first_in_cycle;
    InvItem *item  = first;

    while (item)
    {
        if (item->type == INV_ITEM_DUAL &&
            item->type_inv_item.type_dual.weapon_right == right &&
            item->type_inv_item.type_dual.weapon_left == left)
        {
            return item;
        }

        item = item->next;

        if (item == first)
        {
            break;
        }
    }

    return NULL;
}

/**
 * Is dual weapon in inventory
 * @param right: enum Item ID eg: ITEM_KNIFE
 * @param left: enum Item ID eg: ITEM_KNIFE
 * @return TRUE/FALSE
 */
bool bondinvHasDualWeapon(ITEM_IDS right, ITEM_IDS left)
{
    return bondinvGetDualWeapon(right, left) != NULL;
}

s32 bondinvItemAvailable(ITEM_IDS weaponid)
{
    if (((g_CurrentPlayer->equipallguns) && (weaponid != ITEM_UNARMED) && (weaponid < ITEM_BOMBCASE)))
    {
#ifdef BUGFIX_R1
        if ((!j_text_trigger || (weaponid != ITEM_KNIFE)))
        {
            return TRUE;
        }
#else
        return TRUE;
#endif
    }
    return bondinvHasInvItem(weaponid);
}

s32 bondinvItemAvailableForHand(ITEM_IDS right, ITEM_IDS left)
{
#ifdef BUGFIX_R0
    if (g_CurrentPlayer->equipallguns &&
        right < ITEM_BOMBCASE &&
        right == left &&
        getPlayerCount() == 1 &&
        bondwalkItemCheckBitflags(right, WEAPONSTATBITFLAG_CAN_DUAL_WIELD))
    {
        return TRUE;
    }
#else
    if (left == ITEM_UNARMED)
    {
        return TRUE;
    }
    else
    {
        if (g_CurrentPlayer->equipallguns &&
            right < ITEM_BOMBCASE &&
            right == left &&
            getPlayerCount() == 1 &&
            bondwalkItemCheckBitflags(right, WEAPONSTATBITFLAG_CAN_DUAL_WIELD) &&
            (j_text_trigger == FALSE || (right != ITEM_KNIFE)))
        {
            return TRUE;
        }
    }
#endif

    return bondinvHasDualWeapon(right, left);
}

int bondinvAddInvItem(ITEM_IDS item)
{
    InvItem *nextItem;

    if (bondinvHasInvItem(item) == FALSE)
    {
        nextItem = bondinvGetNextAvailItem();
        if (nextItem)
        {
            nextItem->type = INV_ITEM_WEAPON;
            nextItem->type_inv_item.type_weap.weapon = item;
            bondinvInsertItem(nextItem);
        }

        if ((g_CurrentPlayer->equipallguns) && (item < ITEM_BOMBCASE))
        {
#ifdef BUGFIX_R1
            if ((!j_text_trigger || (item != ITEM_KNIFE)))
            {
                return FALSE;
            }
#else
            return FALSE;
#endif
        }
        return TRUE;
    }
    return FALSE;
}

int bondinvAddDoublesInvItem(ITEM_IDS right, ITEM_IDS left)
{
    InvItem *item;

    if (bondinvHasDualWeapon(right, left) == FALSE)
    {
        item = bondinvGetNextAvailItem();

        if (item)
        {
            item->type                                 = INV_ITEM_DUAL;
            item->type_inv_item.type_dual.weapon_right = right;
            item->type_inv_item.type_dual.weapon_left  = left;
            bondinvInsertItem(item);
        }
        return TRUE;
    }
    else
    {
        return FALSE;
    }
}

WeaponObjRecord *bondinvRemovePropWeaponByID(ITEM_IDS weaponnum)
{
    if (g_CurrentPlayer->ptr_inventory_first_in_cycle)
    {
        InvItem *item = g_CurrentPlayer->ptr_inventory_first_in_cycle->next;

        while (TRUE)
        {
            InvItem *next = item->next;

            if (item->type == INV_ITEM_PROP)
            {
                PropRecord *prop = item->type_inv_item.type_prop.prop;

                if (prop->type == PROP_TYPE_WEAPON)
                {
                    ObjectRecord *obj = prop->obj;

                    if (obj->type == PROPDEF_COLLECTABLE)
                    {
                        WeaponObjRecord *weapon = (WeaponObjRecord *)prop->obj;

                        if (weapon->weaponnum == weaponnum)
                        {
                            bondinvRemoveItem(item);
                            return weapon;
                        }
                    }
                }
            }

            if ((item == g_CurrentPlayer->ptr_inventory_first_in_cycle) || (!g_CurrentPlayer->ptr_inventory_first_in_cycle))
            {
                break;
            }

            item = next;
        }
    }

    return NULL;
}

void bondinvRemoveItemByID(ITEM_IDS weaponnum)
{
    if (g_CurrentPlayer->ptr_inventory_first_in_cycle)
    {
        InvItem *item = g_CurrentPlayer->ptr_inventory_first_in_cycle->next;

        while (TRUE)
        {
            InvItem *next = item->next;

            if (item->type == INV_ITEM_PROP)
            {
                PropRecord *prop = item->type_inv_item.type_prop.prop;

                if (prop->type == PROP_TYPE_WEAPON)
                {
                    ObjectRecord *obj = prop->obj;

                    if (obj->type == PROPDEF_COLLECTABLE)
                    {
                        WeaponObjRecord *weapon = (WeaponObjRecord *)prop->obj;

                        if (weapon->weaponnum == weaponnum)
                        {
                            bondinvRemoveItem(item);
                        }
                    }
                }
            }
            else if (item->type == INV_ITEM_WEAPON)
            {
                if (item->type_inv_item.type_weap.weapon == weaponnum)
                {
                    bondinvRemoveItem(item);
                }
            }

            if ((item == g_CurrentPlayer->ptr_inventory_first_in_cycle) || (!g_CurrentPlayer->ptr_inventory_first_in_cycle))
            {
                break;
            }

            item = next;
        }
    }
}

int bondinvAddPropToInv(PropRecord *prop)
{
    InvItem *item;

    item = bondinvGetNextAvailItem();

    if (item)
    {
        item->type                         = INV_ITEM_PROP;
        item->type_inv_item.type_prop.prop = prop;
        bondinvInsertItem(item);
    }

    return TRUE;
}

int bondinvAddWeaponByProp(PropRecord *prop)
{
    int added;
    added = FALSE;

    if (prop->type == PROP_TYPE_WEAPON)
    {
        ObjectRecord *obj = prop->obj;

        if (obj->type == PROPDEF_COLLECTABLE)
        {
            WeaponObjRecord *weapon = (WeaponObjRecord *)prop->obj;
            WeaponObjRecord *otherweapon;

            s8 weaponnum = weapon->weaponnum;
#ifdef GEVR
            extern s32 g_gevrStereo;
            if ((g_gevrStereo || netActiveDualWield()) && bondinvHasInvItem(weaponnum) &&
                (g_gevrStereo ? gevrWeaponUsesCopies(weaponnum) :
                    bondwalkItemCheckBitflags(weaponnum, WEAPONSTATBITFLAG_CAN_DUAL_WIELD)) &&
                !bondinvHasDualWeapon(weaponnum, weaponnum)) {
                added = bondinvAddDoublesInvItem(weaponnum, weaponnum);
                if (added && !g_gevrStereo) {
                    gunRequestHandWeaponChange(GUNRIGHT, weaponnum, 1);
                    gunRequestHandWeaponChange(GUNLEFT, weaponnum, 1);
                }
            } else
#endif
            added = bondinvAddInvItem(weaponnum);

            otherweapon = weapon->dualweapon;
            if (otherweapon)
            {
                if (weapon->flags & PROPFLAG_WEAPON_LEFTHANDED)
                {
                    added = bondinvHasDualWeapon(otherweapon->weaponnum, weaponnum) == 0;
                }
                else
                {
                    added = bondinvHasDualWeapon(weaponnum, otherweapon->weaponnum) == 0;
                }
                weapon->dualweapon->LinkedWeaponType = weaponnum;
                weapon->dualweapon->dualweapon       = NULL;
                weapon->dualweapon                   = NULL;
            }
            else
            {
                if (weapon->LinkedWeaponType >= 0)
                {
                    if (weapon->flags & PROPFLAG_WEAPON_LEFTHANDED)
                    {
                        added = bondinvAddDoublesInvItem(weapon->LinkedWeaponType, weaponnum);
                    }
                    else
                    {
                        added = bondinvAddDoublesInvItem(weaponnum, weapon->LinkedWeaponType);
                    }
                }
            }
        }
    }
    return added;
}

#ifdef GEVR
/*
 * A tester's headset locked up in bondinvCycleForward (watchdog stack,
 * 2026-09-30: advance_through_inventory from bondviewProcessInput). The
 * cycle walks the inventory ring until it finds a weapon past the current
 * one; a ring with no weapon in it, or one the head no longer belongs to,
 * never ends. Bounded here: after GEVR_INV_CYCLE_MAX steps the ring is
 * described in the log and the cycle keeps the current weapons.
 */
#define GEVR_INV_CYCLE_MAX 256

static void gevrInvCycleStuck(const char *where, InvItem *at, s32 weapon1, s32 weapon2, s32 requireammo)
{
    InvItem *first = g_CurrentPlayer->ptr_inventory_first_in_cycle;
    InvItem *item  = first;
    s32      n     = 0;

    sysLogPrintf(LOG_ERROR, "inventory: %s cycle stuck (player %d, at %p, weapons %d/%d, requireammo %d, equipallguns %d, first %p)",
                 where, get_cur_playernum(), (void *)at, weapon1, weapon2, requireammo, g_CurrentPlayer->equipallguns, (void *)first);

    while (item && n < 24)
    {
        sysLogPrintf(LOG_ERROR, "inventory:   [%d] %p type %d weapon %d/%d next %p prev %p", n, (void *)item, item->type,
                     item->type == INV_ITEM_WEAPON ? item->type_inv_item.type_weap.weapon : item->type == INV_ITEM_DUAL ? item->type_inv_item.type_dual.weapon_right : -1,
                     item->type == INV_ITEM_DUAL ? item->type_inv_item.type_dual.weapon_left : -1,
                     (void *)item->next, (void *)item->prev);
        item = item->next;
        n++;
        if (item == first)
        {
            break;
        }
    }
}
#endif

void bondinvCycleForward(s32 *nextright, s32 *nextleft, s32 requireammo)
{
    s32      weapon1 = *nextright;
    s32      weapon2 = *nextleft;
    InvItem *item    = g_CurrentPlayer->ptr_inventory_first_in_cycle;
#ifdef GEVR
    s32      steps   = 0;
#endif

    while (item)
    {
#ifdef GEVR
        if (++steps > GEVR_INV_CYCLE_MAX)
        {
            gevrInvCycleStuck("forward", item, weapon1, weapon2, requireammo);
            weapon1 = *nextright;
            weapon2 = *nextleft;
            break;
        }
#endif
        if (item->type == INV_ITEM_WEAPON)
        {
            if (item->type_inv_item.type_weap.weapon < ITEM_BOMBCASE && item->type_inv_item.type_weap.weapon > weapon1)
            {
                if (GEVR_CYCLE_OK(requireammo, item->type_inv_item.type_weap.weapon))
                {
                    weapon1 = item->type_inv_item.type_weap.weapon;
                    weapon2 = 0;
                    break;
                }
            }
        }
        else if (item->type == INV_ITEM_DUAL)
        {
            if (item->type_inv_item.type_dual.weapon_right > weapon1 || (weapon1 == item->type_inv_item.type_dual.weapon_right && item->type_inv_item.type_dual.weapon_left > weapon2))
            {
                if (requireammo == FALSE || bondwalkItemHasAmmo(item->type_inv_item.type_dual.weapon_right) || bondwalkItemHasAmmo(item->type_inv_item.type_dual.weapon_left))
                {
                    weapon1 = item->type_inv_item.type_dual.weapon_right;
                    weapon2 = item->type_inv_item.type_dual.weapon_left;
                    break;
                }
            }
        }

        item = item->next;

        if (item == g_CurrentPlayer->ptr_inventory_first_in_cycle)
        {
            if (requireammo)
            {
                break;
            }

            weapon1 = -1;
            weapon2 = -1;
        }
    }

    if (g_CurrentPlayer->equipallguns)
    {
        s32 candidate = *nextright;

        if (getPlayerCount() == 1 && bondwalkItemCheckBitflags(*nextright, WEAPONSTATBITFLAG_CAN_DUAL_WIELD) && (*nextleft < *nextright) && (GEVR_CYCLE_OK(requireammo, *nextright)) && (weapon1 != *nextright || *nextright < weapon2)
#ifdef BUGFIX_R1
            && (!j_text_trigger || *nextright != ITEM_KNIFE)
#endif
        )
        {
            weapon1 = *nextright;
            weapon2 = *nextright;
        }
        else
        {
            if ((weapon1 != *nextright) || (weapon2 == *nextleft))
            {
                // Find next weapon
                do
                {
#ifdef GEVR
                    if (++steps > GEVR_INV_CYCLE_MAX)
                    {
                        gevrInvCycleStuck("forward all-guns", NULL, weapon1, weapon2, requireammo);
                        weapon1 = *nextright;
                        weapon2 = *nextleft;
                        break;
                    }
#endif
                    candidate = (candidate + 1) % ITEM_BOMBCASE;

                    if (candidate == ITEM_UNARMED)
                    {
                        candidate = (candidate + 1) % ITEM_BOMBCASE;
                    }

                    if ((GEVR_CYCLE_OK(requireammo, candidate))
#ifdef BUGFIX_R1
                        && (!j_text_trigger || candidate != ITEM_KNIFE)
#endif
                    )
                    {
                        weapon1 = candidate;
                        weapon2 = ITEM_UNARMED;
                        break;
                    }
                } while (candidate != weapon1);
            }
        }
    }

    *nextright = weapon1;
    *nextleft  = weapon2;
}

void bondinvCycleBackward(s32 *nextright, s32 *nextleft, s32 requireammo)
{
    s32 weapon1 = *nextright;
    s32 weapon2 = *nextleft;

#ifdef GEVR
    s32 steps = 0;
#endif

    if (g_CurrentPlayer->ptr_inventory_first_in_cycle != NULL)
    {
        InvItem *item = g_CurrentPlayer->ptr_inventory_first_in_cycle->prev;

        while (TRUE)
        {
#ifdef GEVR
            if (++steps > GEVR_INV_CYCLE_MAX)
            {
                gevrInvCycleStuck("backward", item, weapon1, weapon2, requireammo);
                weapon1 = *nextright;
                weapon2 = *nextleft;
                break;
            }
#endif
            if (item->type == INV_ITEM_WEAPON)
            {
                if (item->type_inv_item.type_weap.weapon < ITEM_BOMBCASE && (item->type_inv_item.type_weap.weapon < weapon1 || (weapon1 == item->type_inv_item.type_weap.weapon && weapon2 > 0)))
                {
                    if (GEVR_CYCLE_OK(requireammo, item->type_inv_item.type_weap.weapon))
                    {
                        weapon1 = item->type_inv_item.type_weap.weapon;
                        weapon2 = ITEM_UNARMED;
                        break;
                    }
                }
            }
            else if (item->type == INV_ITEM_DUAL)
            {
                if (item->type_inv_item.type_dual.weapon_right < weapon1 || (weapon1 == item->type_inv_item.type_dual.weapon_right && item->type_inv_item.type_dual.weapon_left < weapon2))
                {
                    if (requireammo == FALSE || bondwalkItemHasAmmo(item->type_inv_item.type_dual.weapon_right) || bondwalkItemHasAmmo(item->type_inv_item.type_dual.weapon_left))
                    {
                        weapon1 = item->type_inv_item.type_dual.weapon_right;
                        weapon2 = item->type_inv_item.type_dual.weapon_left;
                        break;
                    }
                }
            }

            if (item == g_CurrentPlayer->ptr_inventory_first_in_cycle)
            {
                if (requireammo)
                {
                    break;
                }

                weapon1 = 1000;
                weapon2 = 1000;
            }

            item = item->prev;
        }
    }

    if (g_CurrentPlayer->equipallguns)
    {
        s32 candidate = *nextright;

        if (*nextleft == ITEM_UNARMED)
        {
            candidate = (candidate + ITEM_BOMBCASE - 1) % ITEM_BOMBCASE;
            if (candidate == ITEM_UNARMED)
            {
                candidate = (candidate + ITEM_BOMBCASE - 1) % ITEM_BOMBCASE;
            }
        }

        while (TRUE)
        {
#ifdef GEVR
            if (++steps > GEVR_INV_CYCLE_MAX)
            {
                gevrInvCycleStuck("backward all-guns", NULL, weapon1, weapon2, requireammo);
                weapon1 = *nextright;
                weapon2 = *nextleft;
                break;
            }
#endif
            if (candidate == weapon1)
            {
                if (getPlayerCount() == 1 && bondwalkItemCheckBitflags(candidate, WEAPONSTATBITFLAG_CAN_DUAL_WIELD) && (GEVR_CYCLE_OK(requireammo, candidate)) && (candidate != *nextright || candidate < *nextleft) && (weapon2 < candidate)
#ifdef BUGFIX_R1
                    && (!j_text_trigger || candidate != ITEM_KNIFE)
#endif
                )
                {
                    weapon1 = candidate;
                    weapon2 = candidate;
                }

                break;
            }
            else if (
                (GEVR_CYCLE_OK(requireammo, candidate))
#ifdef BUGFIX_R1
                && (!j_text_trigger || candidate != ITEM_KNIFE)
#endif
            )
            {
                if (getPlayerCount() == 1 && bondwalkItemCheckBitflags(candidate, WEAPONSTATBITFLAG_CAN_DUAL_WIELD) && (candidate != *nextright || candidate < *nextleft))
                {
                    weapon1 = candidate;
                    weapon2 = candidate;
                }
                else
                {
                    weapon1 = candidate;
                    weapon2 = ITEM_UNARMED;
                }

                break;
            }
            else
            {
                candidate = (candidate + ITEM_BOMBCASE - 1) % ITEM_BOMBCASE;
                if (candidate == ITEM_UNARMED)
                {
                    candidate = (candidate + ITEM_BOMBCASE - 1) % ITEM_BOMBCASE;
                }
            }
        }
    }

    *nextright = weapon1;
    *nextleft  = weapon2;
}

bool bondinvCheckHasKeyFlags(u32 wantkeyflags)
{
    u32      heldkeyflags = 0;
    InvItem *item         = g_CurrentPlayer->ptr_inventory_first_in_cycle;

    while (item)
    {
        if (item->type == INV_ITEM_PROP)
        {
            PropRecord *prop = item->type_inv_item.type_prop.prop;

            if (prop->type == PROP_TYPE_OBJ)
            {
                ObjectRecord *obj = prop->obj;

                if (obj->type == PROPDEF_KEY)
                {
                    KeyRecord *key = (KeyRecord *)prop->obj;

                    heldkeyflags |= key->keyflags;

                    if ((wantkeyflags & heldkeyflags) == wantkeyflags)
                    {
                        return TRUE;
                    }
                }
            }
        }

        item = item->next;

        if (item == g_CurrentPlayer->ptr_inventory_first_in_cycle)
        {
            break;
        }
    }

    return FALSE;
}

bool bondinvHasGEKey(void)
{
    InvItem      *item;
    PropRecord   *prop;
    ObjectRecord *obj;

    item = g_CurrentPlayer->ptr_inventory_first_in_cycle;

    while (item)
    {
        if (item->type == INV_ITEM_PROP)
        {
            prop = item->type_inv_item.type_prop.prop;

            if (prop->type == PROP_TYPE_WEAPON)
            {
                obj = prop->obj;

                if (obj->obj == PROJECTILES_TYPE_GE_KEY)
                {
                    return TRUE;
                }
            }
        }

        item = item->next;

        if (item == g_CurrentPlayer->ptr_inventory_first_in_cycle)
        {
            break;
        }
    }

    return FALSE;
}

/**
 * Is the player alive with flag tag token in inventory
 * @return TRUE/FALSE
 */
bool bondinvIsAliveWithFlag(void)
{
    if (!g_CurrentPlayer->bonddead)
    {
        return bondinvHasInvItem(ITEM_TOKEN);
    }

    return FALSE;
}

/**
 * Is the Golden Gun in inventory
 * @return TRUE/FALSE
 */
bool bondinvHasGoldenGun(void)
{
    return bondinvHasInvItem(ITEM_GOLDENGUN);
}

bool bondinvHasPropInInv(PropRecord *prop)
{
    InvItem *item = g_CurrentPlayer->ptr_inventory_first_in_cycle;

    while (item)
    {
        if (item->type == INV_ITEM_PROP && item->type_inv_item.type_prop.prop == prop)
        {
            return TRUE;
        }

        item = item->next;

        if (item == g_CurrentPlayer->ptr_inventory_first_in_cycle)
        {
            break;
        }
    }

    return FALSE;
}

#ifdef GEVR
/*
 * GE-X's remote mines detonate themselves (gun.c gevrGexMineDetonates; user:
 * the Detonator was still a choice on the watch): with VrGexGuns its entry
 * leaves the list. The game's own walk below is the raw list; the watch's
 * index and count (bondinvCountTotalItemsInInv, bondinvGetItemByIndex,
 * bondinvGetTextbyInvIndex, which every index lookup uses) skip it.
 */
static s32 bondinvCountRaw(void);
static InvItem *bondinvGetItemByIndexRaw(s32 index);
static s32 bondinvGetTextbyInvIndexRaw(s32 index);

static s32 gevrInvHides(s32 raw)
{
    return VrGexGuns && bondinvGetTextbyInvIndexRaw(raw) == ITEM_TRIGGER;
}

static s32 gevrInvRaw(s32 index)
{
    const s32 n = bondinvCountRaw();
    s32 raw;

    if (!VrGexGuns || index < 0)
    {
        return index;
    }
    for (raw = 0; raw < n; raw++)
    {
        if (gevrInvHides(raw))
        {
            continue;
        }
        if (index == 0)
        {
            return raw;
        }
        index--;
    }
    return n + index;
}

s32 bondinvCountTotalItemsInInv(void)
{
    const s32 n = bondinvCountRaw();
    s32 raw, shown = n;

    for (raw = 0; VrGexGuns && raw < n; raw++)
    {
        shown -= gevrInvHides(raw);
    }
    return shown;
}

InvItem *bondinvGetItemByIndex(s32 index)
{
    return bondinvGetItemByIndexRaw(gevrInvRaw(index));
}

s32 bondinvGetTextbyInvIndex(s32 index)
{
    return bondinvGetTextbyInvIndexRaw(gevrInvRaw(index));
}

static s32 bondinvCountRaw(void)
#else
s32 bondinvCountTotalItemsInInv(void)
#endif
{
    InvItem *item;
    s32      numitems = 0;

    if (g_CurrentPlayer->equipallguns)
    {
#ifdef BUGFIX_R1
        numitems = (j_text_trigger ? ITEM_TASER : ITEM_TANKSHELLS);
#else
        numitems = ITEM_TANKSHELLS;
#endif
    }

    item = g_CurrentPlayer->ptr_inventory_first_in_cycle;

    while (item)
    {
        if (item->type == INV_ITEM_PROP)
        {
            PropRecord *prop = item->type_inv_item.type_prop.prop;

            if (prop->type == PROP_TYPE_WEAPON)
            {
                ObjectRecord *obj = prop->obj;

                if (obj->runtime_bitflags & 0x400)
                {
                    numitems = numitems + 1;
                }
            }
            else if (prop->type == PROP_TYPE_OBJ)
            {
                if ((prop->obj->flags2 & 0x40000) == 0)
                {
                    numitems = numitems + 1;
                }
            }
        }
        else if (item->type == INV_ITEM_WEAPON)
        {
            if ((g_CurrentPlayer->equipallguns == FALSE) || (item->type_inv_item.type_weap.weapon > ITEM_TANKSHELLS))
            {
                numitems = numitems + 1;
            }
        }

        item = item->next;

        if (item == g_CurrentPlayer->ptr_inventory_first_in_cycle)
        {
            break;
        }
    }

    return numitems;
}

#ifdef GEVR
static InvItem *bondinvGetItemByIndexRaw(s32 index)
#else
InvItem *bondinvGetItemByIndex(s32 index)
#endif
{
    InvItem *item;

    if (g_CurrentPlayer->equipallguns)
    {
#ifdef BUGFIX_R1
        if (index < (j_text_trigger ? ITEM_TASER : ITEM_TANKSHELLS))
#else
        if (index < ITEM_TANKSHELLS)
#endif
        {
            return NULL;
        }

#ifdef BUGFIX_R1
        index = index - (j_text_trigger ? ITEM_TASER : ITEM_TANKSHELLS);
#else
        index = index - ITEM_TANKSHELLS;
#endif
    }

    item = g_CurrentPlayer->ptr_inventory_first_in_cycle;

    while (item)
    {
        if (item->type == INV_ITEM_PROP)
        {
            PropRecord *prop = item->type_inv_item.type_prop.prop;

            if (prop->type == PROP_TYPE_WEAPON)
            {
                ObjectRecord *obj = prop->obj;

                if (obj->runtime_bitflags & 0x400)
                {
                    if (index == 0)
                    {
                        return item;
                    }
                    index--;
                }
            }
            else if (prop->type == PROP_TYPE_OBJ)
            {
                if ((prop->obj->flags2 & 0x40000) == 0)
                {
                    if (index == 0)
                    {
                        return item;
                    }
                    index--;
                }
            }
        }
        else if (item->type == INV_ITEM_WEAPON)
        {
            if ((g_CurrentPlayer->equipallguns == FALSE) || (item->type_inv_item.type_weap.weapon > ITEM_TANKSHELLS))
            {
                if (index == 0)
                {
                    return item;
                }
                index--;
            }
        }

        item = item->next;

        if (item == g_CurrentPlayer->ptr_inventory_first_in_cycle)
        {
            break;
        }
    }

    return NULL;
}

textoverride *bondinvGetTextbyObj(ObjectRecord *obj)
{
    textoverride *override = g_CurrentPlayer->textoverrides;

    while (override)
    {
        if (override->obj == obj)
        {
            return override;
        }

        override = override->next;
    }

    return NULL;
}

textoverride *bondinvGetTextbyWeaponID(ITEM_IDS weaponnum)
{
    textoverride *override = g_CurrentPlayer->textoverrides;

    while (override)
    {
        if ((override->objoffset == 0) && (override->weapon == weaponnum))
        {
            return override;
        }

        override = override->next;
    }

    return NULL;
}

#ifdef GEVR
static s32 bondinvGetTextbyInvIndexRaw(s32 index)
#else
s32 bondinvGetTextbyInvIndex(s32 index)
#endif
{
    textoverride *override;
    InvItem *     inv_item;

#ifdef GEVR
    inv_item = bondinvGetItemByIndexRaw(index);
#else
    inv_item = bondinvGetItemByIndex(index);
#endif

    if (inv_item)
    {
        if (inv_item->type == INV_ITEM_PROP)
        {
            PropRecord *prop = inv_item->type_inv_item.type_prop.prop;

            override = bondinvGetTextbyObj(prop->obj);

            if (override)
            {
                return override->weapon;
            }
        }
        else if (inv_item->type == INV_ITEM_WEAPON)
        {
            return inv_item->type_inv_item.type_weap.weapon;
        }
    }
    else
    {
        if (g_CurrentPlayer->equipallguns)
        {
#ifdef BUGFIX_R1
            if (index < (j_text_trigger ? ITEM_TASER : ITEM_TANKSHELLS))
            {
                if (j_text_trigger && ((index + 1) >= ITEM_KNIFE))
                {
                    return index + 2;
                }

                return index + 1;
            }
#else
            if (index < ITEM_TANKSHELLS)
            {
                return index + 1;
            }
#endif
        }
    }

    return 0;
}

u16 *bondinvGetNameByIndex(s32 index)
{
#ifdef GEVR
    /* the filtered index once, then the raw list: the all-guns lines below read it too */
    InvItem      *item      = bondinvGetItemByIndexRaw(index = gevrInvRaw(index));
#else
    InvItem      *item      = bondinvGetItemByIndex(index);
#endif
    ITEM_IDS      weaponnum = 0;
    textoverride *override;

    if (item)
    {
        if (item->type == INV_ITEM_PROP)
        {
            PropRecord *prop = item->type_inv_item.type_prop.prop;
            override         = bondinvGetTextbyObj(prop->obj);

            if (override)
            {
                if (override->shorttext)
                {
                    return langGet(override->shorttext);
                }

                weaponnum = override->weapon;
            }
        }
        else if (item->type == INV_ITEM_WEAPON)
        {
            weaponnum = item->type_inv_item.type_weap.weapon;
            override  = bondinvGetTextbyWeaponID(weaponnum);

            if (override && override->shorttext)
            {
                return langGet(override->shorttext);
            }
        }
    }
    else
    {
        if (g_CurrentPlayer->equipallguns)
        {
#ifdef BUGFIX_R1
            if (index < (j_text_trigger ? ITEM_TASER : ITEM_TANKSHELLS))
            {
                if (j_text_trigger && ((index + 1) >= ITEM_KNIFE))
                {
                    return get_ptr_short_watch_text_for_item(index + 2);
                }

                return get_ptr_short_watch_text_for_item(index + 1);
            }
#else
            if (index < ITEM_TANKSHELLS)
            {
                return get_ptr_short_watch_text_for_item(index + 1);
            }
#endif
        }
    }

    return get_ptr_short_watch_text_for_item(weaponnum);
}

u16 *bondinvGetLongNameByIndex(s32 index)
{
#ifdef GEVR
    /* the filtered index once, then the raw list: the all-guns lines below read it too */
    InvItem      *item      = bondinvGetItemByIndexRaw(index = gevrInvRaw(index));
#else
    InvItem      *item      = bondinvGetItemByIndex(index);
#endif
    ITEM_IDS      weaponnum = 0;
    textoverride *override;

    if (item)
    {
        if (item->type == INV_ITEM_PROP)
        {
            PropRecord *prop = item->type_inv_item.type_prop.prop;
            override         = bondinvGetTextbyObj(prop->obj);

            if (override)
            {
                if (override->longtext)
                {
                    return langGet(override->longtext);
                }

                weaponnum = override->weapon;
            }
        }
        else if (item->type == INV_ITEM_WEAPON)
        {
            weaponnum = item->type_inv_item.type_weap.weapon;
            override  = bondinvGetTextbyWeaponID(weaponnum);

            if (override && override->longtext)
            {
                return langGet(override->longtext);
            }
        }
    }
    else
    {
        if (g_CurrentPlayer->equipallguns)
        {
#ifdef BUGFIX_R1
            if (index < (j_text_trigger ? ITEM_TASER : ITEM_TANKSHELLS))
            {
                if (j_text_trigger && ((index + 1) >= ITEM_KNIFE))
                {
                    return get_ptr_long_watch_text_for_item(index + 2);
                }

                return get_ptr_long_watch_text_for_item(index + 1);
            }
#else
            if (index < ITEM_TANKSHELLS)
            {
                return get_ptr_long_watch_text_for_item(index + 1);
            }
#endif
        }
    }

    return get_ptr_long_watch_text_for_item(weaponnum);
}

extern f32 get_45_degree_angle_0(s32 item);
f32 bondinvGet45AngleForIndex(int index)
{
    return get_45_degree_angle_0(bondinvGetTextbyInvIndex(index));
}

int bondinvGetHoffsetForIndex(int index)
{
    return get_horizontal_offset_on_solo_watch_menu_for_item(bondinvGetTextbyInvIndex(index));
}

int bondinvGetVoffsetForIndex(int index)
{
    return get_vertical_offset_on_solo_watch_menu_for_item(bondinvGetTextbyInvIndex(index));
}

int bondinvGetDepthForIndex(int index)
{
    return get_depth_offset_solo_watch_menu_inventory_page_for_item(bondinvGetTextbyInvIndex(index));
}

u16 *bondinvGetFirstTitlebyIndex(s32 index)
{
#ifdef GEVR
    /* the filtered index once, then the raw list: the all-guns lines below read it too */
    InvItem      *item      = bondinvGetItemByIndexRaw(index = gevrInvRaw(index));
#else
    InvItem      *item      = bondinvGetItemByIndex(index);
#endif
    ITEM_IDS      weaponnum = 0;
    textoverride *override;

    if (item)
    {
        if (item->type == INV_ITEM_PROP)
        {
            PropRecord *prop = item->type_inv_item.type_prop.prop;
            override         = bondinvGetTextbyObj(prop->obj);

            if (override)
            {
                if (override->titletext1)
                {
                    return langGet(override->titletext1);
                }

                weaponnum = override->weapon;
            }
        }
        else if (item->type == INV_ITEM_WEAPON)
        {
            weaponnum = item->type_inv_item.type_weap.weapon;
            override  = bondinvGetTextbyWeaponID(weaponnum);

            if (override && override->titletext1)
            {
                return langGet(override->titletext1);
            }
        }
    }
    else
    {
        if (g_CurrentPlayer->equipallguns)
        {
#ifdef BUGFIX_R1
            if (index < (j_text_trigger ? ITEM_TASER : ITEM_TANKSHELLS))
            {
                if (j_text_trigger && ((index + 1) >= ITEM_KNIFE))
                {
                    return get_ptr_first_title_line_item(index + 2);
                }

                return get_ptr_first_title_line_item(index + 1);
            }
#else
            if (index < ITEM_TANKSHELLS)
            {
                return get_ptr_first_title_line_item(index + 1);
            }
#endif
        }
    }

    return get_ptr_first_title_line_item(weaponnum);
}

u16 *bondinvGetSecondTitlebyIndex(s32 index)
{
#ifdef GEVR
    /* the filtered index once, then the raw list: the all-guns lines below read it too */
    InvItem      *item      = bondinvGetItemByIndexRaw(index = gevrInvRaw(index));
#else
    InvItem      *item      = bondinvGetItemByIndex(index);
#endif
    ITEM_IDS      weaponnum = 0;
    textoverride *override;

    if (item)
    {
        if (item->type == INV_ITEM_PROP)
        {
            PropRecord *prop = item->type_inv_item.type_prop.prop;
            override         = bondinvGetTextbyObj(prop->obj);

            if (override)
            {
                if (override->titletext2)
                {
                    return langGet(override->titletext2);
                }

                weaponnum = override->weapon;
            }
        }
        else if (item->type == INV_ITEM_WEAPON)
        {
            weaponnum = item->type_inv_item.type_weap.weapon;
            override  = bondinvGetTextbyWeaponID(weaponnum);

            if (override && override->titletext2)
            {
                return langGet(override->titletext2);
            }
        }
    }
    else
    {
        if (g_CurrentPlayer->equipallguns)
        {
#ifdef BUGFIX_R1
            if (index < (j_text_trigger ? ITEM_TASER : ITEM_TANKSHELLS))
            {
                if (j_text_trigger && ((index + 1) >= ITEM_KNIFE))
                {
                    return get_ptr_second_title_line_item(index + 2);
                }

                return get_ptr_second_title_line_item(index + 1);
            }
#else
            if (index < ITEM_TANKSHELLS)
            {
                return get_ptr_second_title_line_item(index + 1);
            }
#endif
        }
    }

    return get_ptr_second_title_line_item(weaponnum);
}
extern f32 get_45_degree_angle(s32 item);
f32 bondinvGetDifferent45AngleForIndex(int index)
{
    return get_45_degree_angle(bondinvGetTextbyInvIndex(index));
}

int bondinvGetVposWatchForIndex(int index)
{
    return get_vertical_position_solo_watch_menu_main_page_for_item(bondinvGetTextbyInvIndex(index));
}

int bondinvGetHposWatchForIndex(int index)
{
    return get_lateral_position_solo_watch_menu_main_page_for_item(bondinvGetTextbyInvIndex(index));
}

int bondinvGetDepthWatchForIndex(int index)
{
    return get_depth_on_solo_watch_menu_page_for_item(bondinvGetTextbyInvIndex(index));
}

int bondinvGetXrotWatchForIndex(int index)
{
    return get_xrotation_solo_watch_menu_for_item(bondinvGetTextbyInvIndex(index));
}

int bondinvGetYrotWatchForIndex(int index)
{
    return get_yrotation_solo_watch_menu_for_item(bondinvGetTextbyInvIndex(index));
}

void bondinvAddTextOverride(textoverride *override)
{
    override->next                 = g_CurrentPlayer->textoverrides;
    g_CurrentPlayer->textoverrides = override;
}

int bondinvGetCurEquippedItem(void)
{
    return g_CurrentPlayer->equipcuritem;
}

void bondinvSetCurEquippedItem(int current_item)
{
    g_CurrentPlayer->equipcuritem = current_item;
}

void bondinvDetermineEquippedItem(void)
{
    s32 current_weapon;
    s32 i;

    current_weapon = getCurrentPlayerWeaponId(GUNRIGHT);

    g_CurrentPlayer->equipcuritem = ITEM_UNARMED;

    for (i = 0; i < bondinvCountTotalItemsInInv(); i++)
    {
        if (bondinvGetTextbyInvIndex(i) == current_weapon)
        {
            g_CurrentPlayer->equipcuritem = i;
            return;
        }
    }
}

u8 *bondinvGetActivatedTextObject(ObjectRecord *obj)
{
    textoverride *override = bondinvGetTextbyObj(obj);

    if (override && override->pickuptext)
    {
        return langGet(override->pickuptext);
    }

    return NULL;
}

u8 *bondinvGetActivatedTextWeapon(ITEM_IDS weaponnum)
{
    textoverride *override = bondinvGetTextbyWeaponID(weaponnum);

    if (override && override->pickuptext)
    {
        return langGet(override->pickuptext);
    }

    return NULL;
}

void bondinvIncrementHeldTime(s32 weapon1, s32 weapon2)
{
    s32 leastusedtime;
    s32 leastusedindex;
    s32 i;

    if (!bondwalkItemCheckBitflags(weapon1, WEAPONSTATBITFLAG_USE_HOLD_TIME))
    {
        return;
    }

    leastusedtime  = 0x7fffffff;
    leastusedindex = 0;

    if (!bondwalkItemCheckBitflags(weapon2, WEAPONSTATBITFLAG_USE_HOLD_TIME))
    {
        weapon2 = ITEM_UNARMED;
    }

    for (i = 0; i < 10; i++)
    {
        s32 time = g_CurrentPlayer->gunheldarr[i].totaltime;

        if (time >= 0)
        {
            if (weapon1 == g_CurrentPlayer->gunheldarr[i].weapon1 &&
                weapon2 == g_CurrentPlayer->gunheldarr[i].weapon2)
            {
                g_CurrentPlayer->gunheldarr[i].totaltime = time + g_ClockTimer;
                break;
            }

            if (time < leastusedtime)
            {
                leastusedtime  = time;
                leastusedindex = i;
            }
        }
        else
        {
            leastusedindex = i;
            i              = 10;
            break;
        }
    }

    if (i == 10)
    {
        g_CurrentPlayer->gunheldarr[leastusedindex].totaltime = g_ClockTimer;
        g_CurrentPlayer->gunheldarr[leastusedindex].weapon1   = weapon1;
        g_CurrentPlayer->gunheldarr[leastusedindex].weapon2   = weapon2;
    }
}

s32 bondinvGetWeaponOfChoice(s32 *weapon1, s32 *weapon2)
{
    s32 mosttime = -1;
    s32 i;

    *weapon1 = ITEM_UNARMED;
    *weapon2 = ITEM_UNARMED;

    for (i = 0; i < 10; i++)
    {
        if (g_CurrentPlayer->gunheldarr[i].totaltime >= 0 && g_CurrentPlayer->gunheldarr[i].totaltime > mosttime)
        {
            mosttime = g_CurrentPlayer->gunheldarr[i].totaltime;
            *weapon1 = g_CurrentPlayer->gunheldarr[i].weapon1;
            *weapon2 = g_CurrentPlayer->gunheldarr[i].weapon2;
        }
    }
}
