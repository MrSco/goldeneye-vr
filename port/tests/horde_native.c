#include "horde_rules.h"
#include <assert.h>
#include <string.h>

int main(void)
{
    HordeState s;hordeInit(&s,123);
    assert(hordeStateValid(&s));
    unsigned timer=s.timer;
    assert(!hordeAdvance(&s,0,1) && s.timer==timer);
    for(unsigned i=0;i<60;i++)hordeAdvance(&s,15,1);
    assert(s.phase==HORDE_WAVE && s.wave==1 && s.remaining==6);
    unsigned quota=s.remaining;
    /* Allocation failure retries without spending quota. */
    for(unsigned i=0;i<20;i++)hordeAdvance(&s,1,1);
    assert(s.remaining==quota && s.alive==0);
    while(s.remaining) {
        if(hordeCanSpawn(&s))hordeSpawned(&s);
        hordeAdvance(&s,15,4); /* joiners do not change this wave's scale */
    }
    assert(s.alive==quota && s.players==1);
    assert(!hordeAdvance(&s,1,1));
    unsigned kills=s.kills, remaining=s.remaining;
    hordeEnemyGone(&s,0);assert(s.remaining==remaining+1 && s.kills==kills);
    s.spawn_timer=0;hordeSpawned(&s); /* replace an unseen stuck/despawned guard */
    while(s.alive)hordeEnemyGone(&s,1);
    assert(s.kills==quota && hordeAdvance(&s,1,1));
    assert(s.completed==1 && s.phase==HORDE_INTERMISSION);
    for(unsigned i=0;i<60;i++)hordeAdvance(&s,15,4);
    assert(s.wave==2 && s.players==4 && s.remaining==20);
    for(unsigned i=0;i<16;i++){s.spawn_timer=0;assert(hordeCanSpawn(&s));hordeSpawned(&s);}
    s.spawn_timer=0;assert(!hordeCanSpawn(&s));
    s.phase=HORDE_DEFEAT;HordeState ended=s;
    assert(!hordeAdvance(&s,100,4) && !memcmp(&s,&ended,sizeof(s)));
    assert(hordeQuota(UINT32_MAX,4)<=HORDE_MAX_QUOTA);
    assert(hordeEnemyHealthPercent(1)==100 && hordeEnemyHealthPercent(5)==160 && hordeEnemyHealthPercent(50)==400);
    assert(hordeEnemyWeaponTier(1,1)==0 && hordeEnemyWeaponTier(5,1)>=1 && hordeEnemyWeaponTier(15,2)>=4);
    /* Call of Duty points, Mystery Box rolling/claiming/Teddy Bear relocation, and Wall Buys. */
    HordeState econ;hordeInit(&econ,999);
    for(unsigned p=0;p<4;p++)assert(econ.credits[p]==HORDE_START_CREDITS);
    hordeRewardHit(&econ,0);assert(econ.credits[0]==510);
    econ.alive=2;
    hordeEnemyKilled(&econ,1,0,0);assert(econ.credits[0]==570 && econ.kills==1 && econ.alive==1);
    hordeEnemyKilled(&econ,1,0,1);assert(econ.credits[0]==670 && econ.kills==2 && econ.alive==0);
    /* Need 950 pts for Mystery Box; 670 pts is rejected, but Wall Buy Klobb (500 pts) succeeds. */
    assert(!hordeUseBox(&econ,0));
    assert(hordeBuyWall(&econ,0,0,0) && econ.credits[0]==170 && econ.grant_seq[0]==1 && econ.grant_kind[0]==HORDE_GRANT_WALL_WEAPON);
    hordeAddCredits(&econ,0,300);
    assert(hordeBuyWall(&econ,0,0,1) && econ.credits[0]==220 && econ.grant_seq[0]==2 && econ.grant_kind[0]==HORDE_GRANT_WALL_AMMO);
    /* Roll and claim Mystery Box. */
    hordeAddCredits(&econ,0,1000);
    uint32_t before=econ.credits[0];
    assert(hordeUseBox(&econ,0)==1 && econ.box_state==HORDE_BOX_ROLLING && econ.credits[0]==before-HORDE_BOX_COST);
    assert(!hordeUseBox(&econ,0) && !hordeUseBox(&econ,1));
    for(unsigned i=0;i<12;i++)hordeAdvance(&econ,15,1);
    assert(econ.box_state==HORDE_BOX_READY && !hordeUseBox(&econ,1));
    uint8_t rolled=econ.box_weapon;
    assert(hordeUseBox(&econ,0)==2 && econ.box_state==HORDE_BOX_IDLE && econ.grant_seq[0]==3);
    assert(econ.grant_kind[0]==HORDE_GRANT_BOX_WEAPON && econ.grant_item[0]==rolled);
    /* Force Teddy Bear relocation on 10th use: refunds 950 pts and moves box_spot. */
    econ.box_uses=9;econ.credits[1]=HORDE_BOX_COST;
    uint8_t old_spot=econ.box_spot;
    assert(hordeUseBox(&econ,1)==1 && econ.credits[1]==0);
    for(unsigned i=0;i<12;i++)hordeAdvance(&econ,15,1);
    assert(econ.box_state==HORDE_BOX_MOVING && econ.credits[1]==HORDE_BOX_COST);
    for(unsigned i=0;i<12;i++)hordeAdvance(&econ,15,1);
    assert(econ.box_state==HORDE_BOX_IDLE && econ.box_spot!=old_spot && econ.box_uses==0);
    s.phase=HORDE_INTERMISSION;s.wave=UINT32_MAX;s.timer=0;s.alive=0;
    hordeAdvance(&s,1,4);assert(s.wave==UINT32_MAX && hordeStateValid(&s));
    char text[128];hordeStatus(&s,text,sizeof(text));assert(strstr(text,"WAVE") && strstr(text,"PTS"));
    s.phase=99;assert(!hordeStateValid(&s));
    puts("PASS: Horde preparation, spawn failures, quotas, points, Mystery Box, Wall Buys, scaling, caps, defeat and overflow bounds");
    return 0;
}
