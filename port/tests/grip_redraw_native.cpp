#include <array>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>
#include "gevr_scope.h"

struct XrVector3f { float x,y,z; };
struct XrQuaternionf { float x,y,z,w; };
struct XrPosef { XrQuaternionf orientation; XrVector3f position; };
struct XrView { XrPosef pose; };
struct Controller { XrPosef controller_pose; bool is_active; bool is_tracked; } gControllerStates[2];
static XrPosef gCtrlPosePlay[2],gCamCtrlPose[2];
static bool gCamCtrlValid[2],gCamCtrlTracked[2],gCamCtrlUsable[2],gSteadyUsed[2],gSteadyInit[2];
static XrQuaternionf gSteadyQ[2];
static unsigned gCamCtrlSnapshotId;
static std::array<XrView,2> g_frameViews;
static bool g_frameStarted=true,g_haveRecordedViews=true,g_posesEarly=false;
static float vr_world_scale=20.0f;
static int VrLeftHandedMode,VrAimSteady=0;
static bool vr_fake_grip(int,float*,float*) { return false; }
static void gevrRecoilGripPose(int,float*,float*) {}
static int gevrGripSteadyOnCtrl(int) { return 0; }
static int gevrStereoTwoHandGrip() { return 0; }
static XrQuaternionf gevr_steady(int h,const XrQuaternionf& raw,bool) {
    gSteadyQ[h]=raw; gSteadyInit[h]=true; return raw;
}
extern "C" int gevrVrGripPose(int,float*,float*);
extern "C" int gevrVrGripPoseCamera(int,float*,float*);
extern "C" int gevrVrGripPoseSteady(int,float*,float*);
/* PRODUCTION */

static void near(float a,float b) {
    if (fabsf(a-b)>0.00002f) { std::fprintf(stderr,"expected %.6f, got %.6f\n",b,a); assert(false); }
}
static void samePose(const float p[3],const float q[4],const XrPosef& expected) {
    near(p[0],expected.position.x); near(p[1],expected.position.y); near(p[2],expected.position.z);
    near(q[0],expected.orientation.x); near(q[1],expected.orientation.y);
    near(q[2],expected.orientation.z); near(q[3],expected.orientation.w);
}
static XrPosef identityHead() { return {{0,0,0,1},{0,0,0}}; }
static XrPosef movedHead() { return {{0,sqrtf(0.5f),0,sqrtf(0.5f)},{0.1f,0.2f,0.3f}}; }
static void head(const XrPosef& h) {
    for(auto& view:g_frameViews) view.pose=h;
    // Use an asymmetric eye pair to verify the midpoint, not either eye.
    g_frameViews[0].pose.position.x-=0.031f;
    g_frameViews[1].pose.position.x+=0.031f;
}
static void reset() {
    std::memset(gControllerStates,0,sizeof(gControllerStates));
    std::memset(gCtrlPosePlay,0,sizeof(gCtrlPosePlay));
    std::memset(gCamCtrlPose,0,sizeof(gCamCtrlPose));
    std::memset(gCamCtrlValid,0,sizeof(gCamCtrlValid));
    std::memset(gSteadyUsed,0,sizeof(gSteadyUsed));
    std::memset(gSteadyInit,0,sizeof(gSteadyInit));
    VrAimSteady=0; g_frameStarted=g_haveRecordedViews=true;
    head(identityHead());
}
static void lostTracking(int ctrl,bool steady) {
    reset();
    const int phys=gevrPhysHand(ctrl);
    const XrPosef play={{0,0,0,1},{0.4f,0.5f,-0.6f}};
    gCtrlPosePlay[phys]=play;
    gControllerStates[phys].controller_pose=play;
    gControllerStates[phys].is_active=true;
    XrPosef h=identityHead();
    gevrVrSnapshotControllers(&h,1);
    gControllerStates[phys].is_active=false;
    XrQuaternionf orientation=play.orientation;
    if(steady) {
        gSteadyUsed[phys]=gSteadyInit[phys]=true;
        gSteadyQ[phys]={sqrtf(0.5f),0,0,sqrtf(0.5f)};
        // conj(head yaw 90) * steady(play pitch 90).
        orientation={0.5f,-0.5f,0.5f,0.5f};
    } else orientation={0,-sqrtf(0.5f),0,sqrtf(0.5f)};
    h=movedHead(); head(h);
    // Expected room-fixed controller seen after the head moves/yaws 90 deg:
    // inverse yaw maps (0.3,0.3,-0.9) to (0.9,0.3,0.3).
    const XrPosef expected={orientation,{0.9f,0.3f,0.3f}};
    float p[3],q[4];
    assert(gevrVrGripPoseSteady(ctrl,p,q)); samePose(p,q,expected);
    gevrVrSnapshotControllers(&h,1);
    assert(!gCamCtrlTracked[phys]);
    assert(gevrVrGripPoseCamera(ctrl,p,q)); samePose(p,q,expected);
    assert(gevrVrGripPoseSteady(ctrl,p,q)); samePose(p,q,expected);
    float delta[16]; assert(gevrVrRedrawHandDelta(ctrl,delta));
    for(int r=0;r<4;r++) for(int c=0;c<4;c++) near(delta[c*4+r],r==c?1.0f:0.0f);
    // Tracking recovery at the same physical pose cannot change coordinates.
    gControllerStates[phys].is_active=true;
    gControllerStates[phys].controller_pose={{0,-sqrtf(0.5f),0,sqrtf(0.5f)},{0.9f,0.3f,0.3f}};
    assert(gevrVrGripPoseSteady(ctrl,p,q)); samePose(p,q,expected);
    if(!steady) {
        gevrVrSnapshotControllers(&h,1);
        assert(gevrVrGripPoseCamera(ctrl,p,q)); samePose(p,q,expected);
    }
}
static void scopeAttachment(int ctrl,bool lost) {
    reset();
    const int phys=gevrPhysHand(ctrl);
    gControllerStates[phys].is_active=true;
    gControllerStates[phys].controller_pose={{0,0,0,1},{0.4f,0.5f,-0.6f}};
    gCtrlPosePlay[phys]=gControllerStates[phys].controller_pose;
    XrPosef h=identityHead(); gevrVrSnapshotControllers(&h,1);
    GevrScopeState sc={}; sc.gunValid=true;
    sc.gunOrigin[0]=0.4f; sc.gunOrigin[1]=0.5f; sc.gunOrigin[2]=-0.6f;
    // A supported gun's basis differs from the controller's raw basis.
    sc.gunAxes[0][0]=0.8f; sc.gunAxes[0][2]=-0.6f;
    sc.gunAxes[1][1]=1;
    sc.gunAxes[2][0]=0.6f; sc.gunAxes[2][2]=0.8f;
    sc.lens[0]=0.03f; sc.lens[1]=0.06f; sc.lens[2]=0.11f;
    const GevrScopeState original=sc;
    head(movedHead());
    gControllerStates[phys].is_active=!lost;
    // Live head-relative equivalent of the same play-space controller.
    if(!lost) gControllerStates[phys].controller_pose={{0,-sqrtf(0.5f),0,sqrtf(0.5f)},{0.9f,0.3f,0.3f}};
    float delta[16]; assert(gevrVrRedrawHandDelta(ctrl,delta));
    for(int pass=0;pass<2;pass++) {
        float p[3],axes[3][3]; assert(vr_scope_gun_pose(ctrl,sc,p,axes));
        near(p[0],0.9f); near(p[1],0.3f); near(p[2],0.3f);
        // The gun eyepiece after eye replay must equal the compositor lens.
        float oldLens[3],lens[3];
        for(int r=0;r<3;r++) {
            oldLens[r]=sc.gunOrigin[r]; lens[r]=p[r];
            for(int a=0;a<3;a++) {
                oldLens[r]+=sc.gunAxes[a][r]*sc.lens[a];
                lens[r]+=axes[a][r]*sc.lens[a];
            }
        }
        for(int r=0;r<3;r++) {
            float replay=delta[12+r]/vr_world_scale;
            for(int k=0;k<3;k++) replay+=delta[k*4+r]*oldLens[k];
            near(lens[r],replay);
        }
        assert(std::memcmp(&sc,&original,sizeof(sc))==0); // absolute, never cumulative
    }
    // A new game frame records the current pose: correction becomes identity.
    h=movedHead(); gevrVrSnapshotControllers(&h,1);
    sc.gunOrigin[0]=0.9f; sc.gunOrigin[1]=0.3f; sc.gunOrigin[2]=0.3f;
    float p[3],axes[3][3]; assert(vr_scope_gun_pose(ctrl,sc,p,axes));
    for(int r=0;r<3;r++) near(p[r],sc.gunOrigin[r]);
    g_haveRecordedViews=false; assert(!vr_scope_gun_pose(ctrl,sc,p,axes));
}
static void inferredSnapshot(int ctrl) {
    reset();
    int phys=gevrPhysHand(ctrl);
    gControllerStates[phys].is_active=true;
    XrPosef h=identityHead();float p[3],q[4];
    for(int sample=0;sample<300;sample++) {
        XrPosef play={{0,0,0,1},{0.2f+sample*0.001f,0.3f,-0.4f}};
        gCtrlPosePlay[phys]=play;gControllerStates[phys].controller_pose=play;
        gevrVrSnapshotControllers(&h,1);
        assert(gevrVrGripUsable(ctrl) && !gevrVrGripTracked(ctrl));
        assert(gevrVrGripPoseCamera(ctrl,p,q));samePose(p,q,play);
        // A newer XR pose updates redraws before the next game snapshot.
        play.position.x+=0.0005f;gCtrlPosePlay[phys]=play;
        gControllerStates[phys].controller_pose=play;
        assert(gevrVrGripPoseSteady(ctrl,p,q));samePose(p,q,play);
    }
    gControllerStates[phys].is_tracked=true;gevrVrSnapshotControllers(&h,1);
    assert(gevrVrGripUsable(ctrl) && gevrVrGripTracked(ctrl));
    gevrVrSnapshotControllers(&h,0);
    assert(!gevrVrGripUsable(ctrl) && !gevrVrGripTracked(ctrl));
}
int main(int argc,char**) {
    const bool scopeOnly=argc>1;
    for(int lefty=0;lefty<2;lefty++) {
        VrLeftHandedMode=lefty;
        for(int ctrl=0;ctrl<2;ctrl++) {
            if(!scopeOnly) {
                inferredSnapshot(ctrl);
                lostTracking(ctrl,false);
                lostTracking(ctrl,true);
            }
            for(float scale:{20.0f,100.0f}) {
                vr_world_scale=scale;
                scopeAttachment(ctrl,false);
                if(!scopeOnly) scopeAttachment(ctrl,true);
            }
        }
    }
    if(!scopeOnly) {
        reset(); float p[3],q[4];
        assert(!gevrVrGripPose(0,p,q)); // never tracked
        gCtrlPosePlay[0]=identityHead(); g_frameStarted=false;
        assert(!gevrVrGripPose(0,p,q)); // no current XR view
        assert(!gevrVrGripPose(-1,p,q) && !gevrVrGripPoseSteady(2,p,q));
    }
    std::puts("PASS: lost-tracking fresh/redraw agreement, steady pose retention, recovery, both controller roles/handedness and scope/eyepiece alignment without cumulative drift");
}
