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
    s.phase=HORDE_INTERMISSION;s.wave=UINT32_MAX;s.timer=0;s.alive=0;
    hordeAdvance(&s,1,4);assert(s.wave==UINT32_MAX && hordeStateValid(&s));
    char text[128];hordeStatus(&s,text,sizeof(text));assert(strstr(text,"WAVE"));
    s.phase=99;assert(!hordeStateValid(&s));
    puts("PASS: Horde preparation, spawn failures, quotas, player scaling, caps, defeat and overflow bounds");
    return 0;
}
