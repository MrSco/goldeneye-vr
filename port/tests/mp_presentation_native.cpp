#include <cassert>
#include <cmath>
#include <cstdio>
#include <initializer_list>
using s32=int;
#define M_PI_F 3.14159265358979323846f
#define M_TAU_F (2*M_PI_F)
struct coord3d {float x,y,z;};
struct Model {float frame,end;};
struct player {
    float vv_theta,vv_verta,vv_verta360,vv_costheta,vv_sintheta,vv_cosverta,vv_sinverta;
    struct {coord3d theta_transform,applied_view,applied_view2;} field_488;
    float field_2A08,field_2A0C,field_1280;
    int bonddead,startnewbonddie;
    Model model;
};
struct netplayermove {float angles[2];};
bool online;int currentSlot,localSlot,shuffled,g_gevrExtraPass;
bool netIsActive(){return online;}
int netGetLocalSlot(){return localSlot;}
int get_cur_playernum(){return currentSlot;}
int get_player_position_in_shuffled(int){return shuffled;}
float modelGetAnimFrame(Model*m){return m->frame;}
float modelGetAnimEndFrame(Model*m){return m->end;}
/* INSERT_POSE */
/* INSERT_OWNER */
/* INSERT_RESPAWN */
float renderedYaw;
void setsubroty(Model*,float yaw){renderedYaw=yaw;}
void renderHeading(player*p) {
    int index=0;player*ppointers[]={p};struct {Model*model;} body{&p->model};auto*chr=&body;
    /* INSERT_HEADING */
}
int main() {
    online=true;
    for(localSlot=0;localSlot<8;localSlot++)for(currentSlot=0;currentSlot<8;currentSlot++)
        for(shuffled=0;shuffled<8;shuffled++)for(g_gevrExtraPass=0;g_gevrExtraPass<2;g_gevrExtraPass++)
            assert(!!gevrPlayerModelTickOwner()==(currentSlot==localSlot&&!g_gevrExtraPass));
    online=false;
    for(shuffled=0;shuffled<8;shuffled++)assert(!!gevrPlayerModelTickOwner()==(shuffled==0));
    for(float yaw : {0.f,90.f,180.f,270.f,359.f})for(float pitch : {-70.f,0.f,70.f}) {
        player p{};netplayermove m{{yaw,pitch}};
        netSyncCopyOrientation(&p,&m);
        // Simulate the copy's movement tick changing its camera pose. The
        // after-tick restoration must recover all heading/aim fields together.
        p.vv_theta=180;p.vv_verta=45;p.field_2A08=9;p.field_2A0C=7;
        netSyncCopyOrientation(&p,&m);renderHeading(&p);
        assert(p.vv_theta==yaw && p.vv_verta==pitch && p.field_2A0C==0);
        assert(p.vv_verta360>=0 && p.vv_verta360<360);
        assert(fabsf(sinf(renderedYaw)-p.field_488.theta_transform.x)<.00001f);
        assert(fabsf(cosf(renderedYaw)-p.field_488.theta_transform.z)<.00001f);
        assert(fabsf(p.vv_cosverta-cosf(pitch*M_PI_F/180))<.00001f);
        const auto f=p.field_488.applied_view,u=p.field_488.applied_view2;
        assert(fabsf(f.x*f.x+f.y*f.y+f.z*f.z-1)<.00001f);
        assert(fabsf(f.x*u.x+f.y*u.y+f.z*u.z)<.00001f);
        assert(fabsf(f.x-sinf(renderedYaw)*cosf(pitch*M_PI_F/180))<.00001f);
        assert(fabsf(f.z-cosf(renderedYaw)*cosf(pitch*M_PI_F/180))<.00001f);
    }
    player dead{};dead.bonddead=1;dead.model={30,30};dead.startnewbonddie=1;
    assert(!gevrOnlineRespawnReady(&dead));
    dead.startnewbonddie=0;dead.model.frame=0;assert(!gevrOnlineRespawnReady(&dead));
    dead.model.frame=29;assert(!gevrOnlineRespawnReady(&dead));
    dead.model.frame=30;assert(gevrOnlineRespawnReady(&dead));
    dead.bonddead=0;assert(!gevrOnlineRespawnReady(&dead));
    dead.bonddead=1;dead.model={0,0};assert(!gevrOnlineRespawnReady(&dead));
    puts("PASS: online animation frame ownership, rendered heading, pose restoration and death-skip gate");
}
