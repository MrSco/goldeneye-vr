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
    assert(!hordeUseBox(&econ,0,20,0));
    assert(hordeBuyWall(&econ,0,0,0,10,0) && econ.credits[0]==170 && econ.grant_seq[0]==1 && econ.grant_kind[0]==HORDE_GRANT_WALL_WEAPON);
    hordeAddCredits(&econ,0,300);
    assert(!hordeBuyWall(&econ,0,0,1,10,0) && econ.credits[0]==470 && econ.grant_seq[0]==1);
    assert(!hordeBuyWall(&econ,0,0,0,10,0)); /* repeat weapon purchase cannot refill */
    /* Roll and claim Mystery Box. */
    hordeAddCredits(&econ,0,1000);
    uint32_t before=econ.credits[0];
    assert(hordeUseBox(&econ,0,20,0)==1 && econ.box_state==HORDE_BOX_ROLLING && econ.credits[0]==before-HORDE_BOX_COST);
    assert(!hordeUseBox(&econ,0,20,0) && !hordeUseBox(&econ,1,20,0));
    for(unsigned i=0;i<12;i++)hordeAdvance(&econ,15,1);
    assert(econ.box_state==HORDE_BOX_READY && !hordeUseBox(&econ,1,20,0));
    uint8_t rolled=econ.box_weapon;
    assert(hordeUseBox(&econ,0,20,0)==2 && econ.box_state==HORDE_BOX_IDLE && econ.grant_seq[0]==2);
    assert(econ.grant_kind[0]==HORDE_GRANT_BOX_WEAPON && econ.grant_item[0]==rolled);
    /* Force Teddy Bear relocation on 10th use: refunds 950 pts and moves box_spot. */
    econ.box_uses=9;econ.credits[1]=HORDE_BOX_COST;
    uint8_t old_spot=econ.box_spot;
    assert(hordeUseBox(&econ,1,20,0)==1 && econ.credits[1]==0);
    for(unsigned i=0;i<12;i++)hordeAdvance(&econ,15,1);
    assert(econ.box_state==HORDE_BOX_MOVING && econ.credits[1]==HORDE_BOX_COST);
    for(unsigned i=0;i<12;i++)hordeAdvance(&econ,15,1);
    assert(econ.box_state==HORDE_BOX_IDLE && econ.box_spot!=old_spot && econ.box_uses==0);
    s.phase=HORDE_INTERMISSION;s.wave=UINT32_MAX;s.timer=0;s.alive=0;
    hordeAdvance(&s,1,4);assert(s.wave==UINT32_MAX && hordeStateValid(&s));
    char text[128];hordeStatus(&s,text,sizeof(text));assert(strstr(text,"WAVE") && strstr(text,"PTS"));
    s.phase=99;assert(!hordeStateValid(&s));
    puts("PASS: Horde preparation, spawn failures, quotas, points, Mystery Box, Wall Buys, scaling, caps, defeat and overflow bounds");

    /* Special rounds, enemy kinds and the starting wave (2026-10-17). */
    assert(hordeWaveKind(1)==HORDE_WAVE_NORMAL && hordeWaveKind(5)==HORDE_WAVE_SWARM
        && hordeWaveKind(10)==HORDE_WAVE_BOSS && hordeWaveKind(15)==HORDE_WAVE_SWARM
        && hordeWaveKind(20)==HORDE_WAVE_BOSS && hordeWaveKind(11)==HORDE_WAVE_NORMAL);
    assert(hordeWaveQuota(5,1)==hordeQuota(5,1)+hordeQuota(5,1)/2 && hordeWaveQuota(6,1)==hordeQuota(6,1));
    for (unsigned w=1;w<=60;w++) assert(hordeWaveQuota(w,4)<=HORDE_MAX_QUOTA);
    for (unsigned serial=1;serial<200;serial++) {
        assert(hordeEnemyKind(5,serial,0,1)==HORDE_ENEMY_SWARM);
        assert(hordeEnemyKind(1,serial,0,1)==HORDE_ENEMY_GUARD);     /* no specials early */
        unsigned k=hordeEnemyKind(31+serial%4,serial,5,4);   /* normal waves only */
        assert(k==HORDE_ENEMY_GUARD || k==HORDE_ENEMY_ARMORED || k==HORDE_ENEMY_JANUS);
    }
    assert(hordeEnemyKind(10,7,0,1)==HORDE_ENEMY_BOSS && hordeEnemyKind(10,7,1,1)!=HORDE_ENEMY_BOSS);
    assert(hordeEnemyKind(10,7,2,3)==HORDE_ENEMY_BOSS && hordeEnemyKind(10,7,3,3)!=HORDE_ENEMY_BOSS);
    unsigned armored=0,janus=0;
    for (unsigned serial=1;serial<=1000;serial++) {
        unsigned k=hordeEnemyKind(12,serial,9,1);
        armored+=k==HORDE_ENEMY_ARMORED;janus+=k==HORDE_ENEMY_JANUS;
    }
    assert(armored>100 && armored<300 && janus>100 && janus<300);
    assert(hordeEnemySpeedPercent(1,HORDE_ENEMY_GUARD)==100 && hordeEnemySpeedPercent(60,HORDE_ENEMY_GUARD)==140
        && hordeEnemySpeedPercent(5,HORDE_ENEMY_SWARM)>150 && hordeEnemySpeedPercent(30,HORDE_ENEMY_BOSS)<=110);
    assert(hordeEnemyKindHealthPercent(HORDE_ENEMY_BOSS,1)>=1000 && hordeEnemyKindHealthPercent(HORDE_ENEMY_SWARM,1)<100);
    HordeState late;hordeInitAt(&late,5,20);
    assert(hordeStateValid(&late) && late.wave==19 && late.completed==19 && late.credits[0]==hordeStartCredits(20));
    for(unsigned i=0;i<60 && late.phase!=HORDE_WAVE;i++)hordeAdvance(&late,15,2);
    assert(late.phase==HORDE_WAVE && late.wave==20 && late.remaining==hordeWaveQuota(20,2));
    HordeState capped;hordeInitAt(&capped,5,999);
    assert(capped.wave==HORDE_MAX_START_WAVE-1 && hordeStateValid(&capped));
    HordeState swarm;hordeInitAt(&swarm,5,5);
    for(unsigned i=0;i<60 && swarm.phase!=HORDE_WAVE;i++)hordeAdvance(&swarm,15,1);
    for(unsigned i=0;i<12;i++) {
        if (!hordeCanSpawn(&swarm)) { swarm.spawn_timer=0; }
        assert(hordeCanSpawn(&swarm));hordeSpawned(&swarm);swarm.spawn_timer=0;
    }
    assert(!hordeCanSpawn(&swarm) && swarm.alive==12 && hordeStateValid(&swarm));   /* 8 + 4 at once */
    puts("PASS: Horde special rounds, enemy kinds, speed/health scaling and starting wave");

    /* Two guns (the PP7 one): a third replaces the one asked, else the oldest. */
    HordeState guns;hordeInit(&guns,9);guns.guns[0][0]=2;   /* the PP7 */
    hordeAddCredits(&guns,0,20000);
    assert(hordeBuyWall(&guns,0,0,0,10,0) && guns.grant_replace[0]==0 && guns.guns[0][0]==2 && guns.guns[0][1]==10);
    assert(!hordeBuyWall(&guns,0,0,0,10,0));                        /* owned */
    assert(hordeBuyWall(&guns,0,2,0,30,10) && guns.grant_replace[0]==10 && guns.guns[0][0]==2 && guns.guns[0][1]==30);
    assert(hordeBuyWall(&guns,0,0,0,10,99) && guns.grant_replace[0]==2 && guns.guns[0][0]==30 && guns.guns[0][1]==10);
    assert(hordeBuyWall(&guns,0,5,0,0,0) && guns.grant_kind[0]==HORDE_GRANT_ARMOR && guns.grant_replace[0]==0);
    assert(hordeTakeGun(&guns,0,200,0)==0 && !hordeGunOwned(&guns,0,200));   /* out of range */
    assert(hordeStateValid(&guns));
    puts("PASS: Horde two-gun limit");
    /* One death in three drops a crate, every boss does */
    unsigned drops=0;
    for (uint32_t id=1;id<=3000;id++) { drops+=hordeDropsAmmo(id,0)!=0; assert(hordeDropsAmmo(id,1)); }
    assert(drops>800 && drops<1200);
    puts("PASS: Horde ammo drop rate");
    return 0;
}
