#include "gevr_tactical_inventory.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
int main(void)
{
    TacticalInventory inv, before;
    unsigned int ejected;
    TacticalMagazine dropped, ak = {1, 11, 30, 30, TACTICAL_MAG_PRIMARY};
    TacticalMagazine spare = {2, 11, 30, 30, TACTICAL_MAG_PRIMARY};
    TacticalMagazine pistol = {3, 22, 8, 8, TACTICAL_MAG_PISTOL};
    TacticalMagazine wrong = {4, 33, 30, 20, TACTICAL_MAG_PRIMARY};
    tacticalInventoryInit(&inv);
    assert(tacticalSetWell(&inv, TACTICAL_PRIMARY_WELL, 11) == TACTICAL_OK);
    assert(tacticalSetWell(&inv, TACTICAL_LEFT_PISTOL_WELL, 22) == TACTICAL_OK);
    assert(tacticalSetWell(&inv, TACTICAL_RIGHT_PISTOL_WELL, 22) == TACTICAL_OK);
    assert(tacticalAddMagazine(&inv, TACTICAL_PRIMARY_WELL, ak) == TACTICAL_OK);
    assert(tacticalAddMagazine(&inv, TACTICAL_RIG_PRIMARY_1, spare) == TACTICAL_OK);
    assert(tacticalAddMagazine(&inv, TACTICAL_RIG_PISTOL_1, pistol) == TACTICAL_OK);
    assert(tacticalCountRounds(&inv) == 68);
    assert(tacticalFire(&inv, TACTICAL_PRIMARY_WELL) == TACTICAL_EMPTY_CHAMBER);
    assert(tacticalRack(&inv, TACTICAL_PRIMARY_WELL, &ejected) == TACTICAL_OK && ejected == 0);
    assert(tacticalCountRounds(&inv) == 68);
    assert(tacticalFire(&inv, TACTICAL_PRIMARY_WELL) == TACTICAL_OK);
    assert(tacticalFire(&inv, TACTICAL_PRIMARY_WELL) == TACTICAL_OK);
    assert(tacticalMoveMagazine(&inv, TACTICAL_PRIMARY_WELL, TACTICAL_LEFT_HAND) == TACTICAL_OK);
    assert(tacticalMoveMagazine(&inv, TACTICAL_LEFT_HAND, TACTICAL_RIG_PRIMARY_2) == TACTICAL_OK);
    assert(inv.slots[TACTICAL_RIG_PRIMARY_2].rounds == 27); /* one more is chambered */
    assert(tacticalMoveMagazine(&inv, TACTICAL_RIG_PRIMARY_1, TACTICAL_RIGHT_HAND) == TACTICAL_OK);
    assert(tacticalMoveMagazine(&inv, TACTICAL_RIGHT_HAND, TACTICAL_PRIMARY_WELL) == TACTICAL_OK);
    assert(tacticalCountRounds(&inv) == 66);
    assert(tacticalAddMagazine(&inv, TACTICAL_LEFT_HAND, wrong) == TACTICAL_OK);
    before = inv;
    assert(tacticalMoveMagazine(&inv, TACTICAL_LEFT_HAND, TACTICAL_PRIMARY_WELL) == TACTICAL_OCCUPIED);
    assert(memcmp(&inv, &before, sizeof(inv)) == 0);
    assert(tacticalMoveMagazine(&inv, TACTICAL_PRIMARY_WELL, TACTICAL_RIGHT_HAND) == TACTICAL_OK);
    before = inv;
    assert(tacticalMoveMagazine(&inv, TACTICAL_LEFT_HAND, TACTICAL_PRIMARY_WELL) == TACTICAL_INCOMPATIBLE);
    assert(tacticalMoveMagazine(&inv, TACTICAL_LEFT_HAND, TACTICAL_RIG_PISTOL_2) == TACTICAL_INCOMPATIBLE);
    assert(memcmp(&inv, &before, sizeof(inv)) == 0);
    assert(tacticalDropMagazine(&inv, TACTICAL_LEFT_HAND, &dropped) == TACTICAL_OK);
    assert(dropped.id == 4 && dropped.rounds == 20 && tacticalCountRounds(&inv) == 66);
    assert(tacticalAddMagazine(&inv, TACTICAL_LEFT_HAND, spare) == TACTICAL_DUPLICATE);
    assert(tacticalMoveMagazine(&inv, TACTICAL_RIGHT_HAND, TACTICAL_PRIMARY_WELL) == TACTICAL_OK);
    assert(tacticalSetWell(&inv, TACTICAL_PRIMARY_WELL, 33) == TACTICAL_OCCUPIED);
    /* Tactical reload kept the chambered round. Cycling it discards one and feeds a new one. */
    assert(tacticalRack(&inv, TACTICAL_PRIMARY_WELL, &ejected) == TACTICAL_OK && ejected == 1);
    assert(tacticalCountRounds(&inv) == 65);
    for (int i = 0; i < 30; ++i) assert(tacticalFire(&inv, TACTICAL_PRIMARY_WELL) == TACTICAL_OK);
    assert(tacticalFire(&inv, TACTICAL_PRIMARY_WELL) == TACTICAL_EMPTY_CHAMBER);
    assert(tacticalCountRounds(&inv) == 35);
    assert(tacticalMoveMagazine(&inv, TACTICAL_RIG_PRIMARY_2, TACTICAL_RIG_PRIMARY_3) == TACTICAL_INVALID);
    assert(tacticalMoveMagazine(&inv, TACTICAL_RIG_PISTOL_1, TACTICAL_LEFT_HAND) == TACTICAL_OK);
    assert(tacticalMoveMagazine(&inv, TACTICAL_LEFT_HAND, TACTICAL_RIGHT_PISTOL_WELL) == TACTICAL_OK);
    assert(tacticalFire(&inv, TACTICAL_RIGHT_PISTOL_WELL) == TACTICAL_EMPTY_CHAMBER);
    assert(tacticalRack(&inv, TACTICAL_RIGHT_PISTOL_WELL, &ejected) == TACTICAL_OK && ejected == 0);
    assert(tacticalFire(&inv, TACTICAL_RIGHT_PISTOL_WELL) == TACTICAL_OK);
    assert(inv.slots[TACTICAL_RIGHT_PISTOL_WELL].rounds == 6 && inv.chambered[TACTICAL_RIGHT_PISTOL_WELL] == 1);
    {
        TacticalRackGesture g;
        tacticalRackGestureReset(&g);
        assert(!tacticalRackGestureTick(&g, 1, 1, 0, 0, 4)); /* grip far away */
        assert(!tacticalRackGestureTick(&g, 1, 1, 1, 10, 4)); /* can't acquire while held */
        assert(!tacticalRackGestureTick(&g, 1, 0, 1, 10, 4));
        assert(!tacticalRackGestureTick(&g, 1, 1, 1, 10, 4)); /* fresh grab */
        assert(!tacticalRackGestureTick(&g, 1, 1, 1, 13, 4)); /* short pull */
        assert(!tacticalRackGestureTick(&g, 1, 0, 1, 13, 4));
        assert(!tacticalRackGestureTick(&g, 1, 1, 1, 10, 4));
        assert(!tacticalRackGestureTick(&g, 1, 1, 1, 14.1f, 4));
        assert(tacticalRackGestureTick(&g, 1, 0, 1, 14.1f, 4));
        assert(!tacticalRackGestureTick(&g, 1, 0, 1, 14.1f, 4)); /* one event */
        assert(!tacticalRackGestureTick(&g, 1, 1, 1, 0, 4));
        assert(!tacticalRackGestureTick(&g, 1, 1, 1, 5, 4));
        assert(!tacticalRackGestureTick(&g, 0, 1, 0, 5, 4)); /* tracking loss cancels */
        assert(!tacticalRackGestureTick(&g, 1, 1, 1, 8, 4));
        assert(!tacticalRackGestureTick(&g, 1, 0, 1, 8, 4)); /* no delayed event */
    }
    {
        TacticalInventory x;
        TacticalInteraction interaction;
        TacticalInteractionInput in = {0};
        TacticalMagazine first = {10, 11, 30, 30, TACTICAL_MAG_PRIMARY};
        TacticalMagazine fresh = {11, 11, 30, 30, TACTICAL_MAG_PRIMARY};
        tacticalInventoryInit(&x);
        assert(tacticalSetWell(&x, TACTICAL_PRIMARY_WELL, 11) == TACTICAL_OK);
        assert(tacticalAddMagazine(&x, TACTICAL_PRIMARY_WELL, first) == TACTICAL_OK);
        assert(tacticalAddMagazine(&x, TACTICAL_RIG_PRIMARY_1, fresh) == TACTICAL_OK);
        assert(tacticalRack(&x, TACTICAL_PRIMARY_WELL, &ejected) == TACTICAL_OK);
        assert(tacticalFire(&x, TACTICAL_PRIMARY_WELL) == TACTICAL_OK);
        assert(tacticalFire(&x, TACTICAL_PRIMARY_WELL) == TACTICAL_OK);
        tacticalInteractionInit(&interaction, TACTICAL_PRIMARY_WELL, TACTICAL_LEFT_HAND);
        in.tracked = 1; in.grip = 1; in.magazine_distance_cm = 1; in.action_distance_cm = 99;
        for (int k = 0; k < 5; k++) in.rig_distance_cm[k] = 99;
        assert(tacticalInteractionTick(&interaction, &x, &in, &dropped, &ejected) == TACTICAL_EVENT_NONE);
        in.magazine_pull_cm = 4;
        assert(tacticalInteractionTick(&interaction, &x, &in, &dropped, &ejected) == TACTICAL_EVENT_NONE);
        in.magazine_pull_cm = 6;
        assert(tacticalInteractionTick(&interaction, &x, &in, &dropped, &ejected) == TACTICAL_EVENT_MAG_TAKEN);
        assert(x.slots[TACTICAL_LEFT_HAND].rounds == 27 && x.chambered[TACTICAL_PRIMARY_WELL] == 1);
        in.tracked = 0;
        assert(tacticalInteractionTick(&interaction, &x, &in, &dropped, &ejected) == TACTICAL_EVENT_NONE);
        in.tracked = 1; in.grip = 0; in.magazine_distance_cm = 99; in.rig_distance_cm[1] = 1;
        assert(tacticalInteractionTick(&interaction, &x, &in, &dropped, &ejected) == TACTICAL_EVENT_NONE);
        assert(x.slots[TACTICAL_LEFT_HAND].id == 10); /* tracking recovery cannot drop it */
        in.grip = 1;
        assert(tacticalInteractionTick(&interaction, &x, &in, &dropped, &ejected) == TACTICAL_EVENT_NONE);
        in.grip = 0;
        assert(tacticalInteractionTick(&interaction, &x, &in, &dropped, &ejected) == TACTICAL_EVENT_MAG_STORED);
        assert(x.slots[TACTICAL_RIG_PRIMARY_2].rounds == 27);
        in.rig_distance_cm[1] = 99; in.rig_distance_cm[0] = 1; in.grip = 1;
        assert(tacticalInteractionTick(&interaction, &x, &in, &dropped, &ejected) == TACTICAL_EVENT_MAG_TAKEN);
        in.rig_distance_cm[0] = 99; in.magazine_distance_cm = 1; in.grip = 0;
        assert(tacticalInteractionTick(&interaction, &x, &in, &dropped, &ejected) == TACTICAL_EVENT_MAG_SEATED);
        assert(tacticalCountRounds(&x) == 58);
        in.magazine_distance_cm = 99; in.action_distance_cm = 1; in.action_pull_cm = 0; in.grip = 1;
        assert(tacticalInteractionTick(&interaction, &x, &in, &dropped, &ejected) == TACTICAL_EVENT_NONE);
        in.action_pull_cm = 5;
        assert(tacticalInteractionTick(&interaction, &x, &in, &dropped, &ejected) == TACTICAL_EVENT_NONE);
        in.grip = 0;
        assert(tacticalInteractionTick(&interaction, &x, &in, &dropped, &ejected) == TACTICAL_EVENT_RACKED);
        assert(ejected == 1 && tacticalCountRounds(&x) == 57);
    }
    puts("tactical inventory: partial reload, chamber, racking, tracked interactions, conservation and drops passed");
    return 0;
}
