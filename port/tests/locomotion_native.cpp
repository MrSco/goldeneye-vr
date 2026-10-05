#include "gevr_locomotion.h"
#include "gevr_frame_timing.h"
#include <openxr/openxr.h>
#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <map>
#include <vector>

static bool g_frameStarted = true, positionValid = true, orientationValid = true;
static bool g_haveCameraViews = true, g_haveRecordedViews = true, g_haveRenderedViews, g_eyesHoldStereo;
static bool s_haveSourceCamera, s_haveRecordedCamera;
static GevrLocomotionHistory s_locomotion;
static GevrLocomotionStats s_locomotionStats;
static GevrPresentationCamera s_sourceCamera, s_recordedCamera, s_presentedCamera, s_recordedPresentation;
static GevrLocomotionPose s_recordedLocomotion, s_evaluatedLocomotion;
static XrTime s_lastPresentationTime;
static bool s_collisionContact;
static float s_collisionNormal[3], s_collisionHead[3];
static unsigned s_statClamps, s_statRedraw, freshRenders;
static float vr_world_scale = 100;
static XrFrameState g_frameState{};
static std::array<XrView, 2> g_frameViews{}, g_cameraViews{}, g_recordedViews{}, g_renderedViews{};
static void vr_stats_game_frame() { freshRenders++; }
static XrQuaternionf oldHandQ{0,0,0,1}, newHandQ{0,0,0,1};
static float oldHandP[3], newHandP[3];
extern "C" int gevrVrGripPoseCamera(int, float p[3], float q[4]) {
    std::memcpy(p, oldHandP, sizeof(oldHandP)); std::memcpy(q, &oldHandQ, sizeof(oldHandQ)); return 1;
}
extern "C" int gevrVrGripPoseSteady(int, float p[3], float q[4]) {
    std::memcpy(p, newHandP, sizeof(newHandP)); std::memcpy(q, &newHandQ, sizeof(newHandQ)); return 1;
}
/* INSERT_RUNTIME */
using u32 = uint32_t;
static u32 g_gevrTeleportEpoch;
/* INSERT_TELEPORT */

using GLint = int;
struct ShaderProgram { int opengl_program_id, reprojLocation=1, reprojVPLocation=2, scopeHeadPLocation=3; };
static ShaderProgram program{1}, secondProgram{2}, *s_curPrg = &program;
static bool use_multiview = true, gForceFlatShaderForMenu, gVrFlatPass, s_uniCacheValid;
static bool s_eyePresent, s_eyeRec, s_eyeReady;
static int s_eyeHand=-1, boundProgram=1;
static float s_eyeProj[16], s_eyeHeadP[2], s_eyePresentationVP[16];
static std::vector<ShaderProgram*> s_eyePresentationPrograms;
static std::vector<int> s_eyeDraws;
static void *s_pmPtr = &program;
static std::map<int,int> reprojection;
static float lastUniformMatrix[16];
static void glUseProgram(int id) { boundProgram = id; }
static void glUniform1i(int, int value) { reprojection[boundProgram] = value; }
static void glUniform2f(int, float, float) {}
static void glUniformMatrix4fv(int, int, bool, const float *m) { std::memcpy(lastUniformMatrix,m,sizeof(lastUniformMatrix)); }
static constexpr bool GL_FALSE = false;
/* INSERT_BACKEND */

static const float identity3[9] = {1,0,0,0,1,0,0,0,1};
static const float identity4[16] = {1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
static void near(float a, float b, float tolerance=0.002f) { assert(std::fabs(a-b) < tolerance); }

static void cadence(int hz, const std::array<float,3>& velocity, int64_t periodJitter=0) {
    GevrLocomotionHistory h{};
    const int64_t period = std::llround(1e9 / hz) + periodJitter, anchor=1000000000;
    const int64_t delay=gevrLocomotionDelay(period);
    near((float)delay/1e6f, hz==120 ? 16.666667f : hz==90 ? 22.222222f : hz==80 ? 25.f : 27.777778f);
    uint64_t tick=0;
    int64_t due=anchor;
    GevrLocomotionPose pose{};
    for (int frame=0; frame<hz*4; frame++) {
        const int64_t now=anchor+frame*period;
        if (now+2000000 >= due) {
            float tracking[3], position[3];
            for (int i=0;i<3;i++) {
                tracking[i]=(float)(std::sin(tick * 0.27+i) * 15); // independent physical movement
                position[i]=tracking[i]+velocity[i]*(float)tick/60;
            }
            gevrLocomotionSnapshot(&h,position,tracking,std::fmod(359.f+tick*1.5f,360.f),tick,now,period);
            tick++;
            due=anchor+(int64_t)(tick*1000000000/60);
        }
        assert(gevrLocomotionQuery(&h,now,&pose));
        if (now-anchor < delay) continue;
        const float seconds=(float)(now-anchor-delay)/1e9f;
        for (int i=0;i<3;i++) near(pose.position[i],velocity[i]*seconds);
        near(std::remainder(pose.yaw-(359.f+seconds*90.f),360.f),0);
        /* The latest physical offset bypasses interpolation at every display frame. */
        GevrPresentationCamera source{{0,0,0},{1,0,0,0,1,0,0,0,1}}, camera{};
        for (int i=0;i<3;i++) source.position[i]=h.poses[h.count-1].position[i]+23+i;
        float headT[3]={1,2,3};
        gevrLocomotionCamera(&source,&h.poses[h.count-1],&pose,identity3,headT,&camera);
        for (int i=0;i<3;i++) near(camera.position[i],pose.position[i]+23+i+headT[i]);
    }
    assert(h.count==8);
}

static void stopsAndResets() {
    GevrLocomotionHistory h{};
    float p[3]={0,0,0}, tracking[3]={0,0,0};
    for (uint64_t i=0;i<6;i++) {
        p[0]=(float)std::min<uint64_t>(i,2)*4;
        assert(gevrLocomotionSnapshot(&h,p,tracking,0,i,1000000000+i*16666667,8333333));
        GevrLocomotionPose q{};
        assert(gevrLocomotionQuery(&h,1000000000+i*16666667+8333333,&q));
        assert(q.position[0]>=0 && q.position[0]<=8); // confirmed wall stop, no overshoot
    }
    GevrLocomotionPose q{};
    gevrLocomotionQuery(&h,1400000000,&q); near(q.position[0],8);
    assert(gevrLocomotionSnapshot(&h,p,tracking,90,6,1400000000,8333333)==2);
    assert(h.count==1);
    assert(gevrLocomotionSnapshot(&h,p,tracking,90,7,1416666667,11111111)==2);
    assert(h.count==1); // rate change
    assert(gevrLocomotionSnapshot(&h,p,tracking,90,7,1420000000,11111111)==0);
    assert(h.count==1); // redraw must not publish another simulation sample
    assert(gevrLocomotionSnapshot(&h,p,tracking,90,0,1430000000,11111111)==2);
    assert(gevrLocomotionSnapshot(&h,p,tracking,90,1,1420000000,11111111)==2);
    gevrLocomotionReset(&h); assert(!gevrLocomotionQuery(&h,1500000000,&q));
    p[0]=std::numeric_limits<float>::quiet_NaN();
    assert(!gevrLocomotionSnapshot(&h,p,tracking,0,0,1000000000,8333333)); assert(h.count==0);
}

static void runtimeFrame(uint64_t tick, int64_t time, float z) {
    g_frameState.predictedDisplayTime=time;
    g_frameState.predictedDisplayPeriod=8333333;
    g_frameState.shouldRender=true;
    for (auto &v:g_frameViews) v.pose.orientation={0,0,0,1};
    g_cameraViews=g_frameViews;
    const float pos[3]={0,175,z}, tracking[3]={0,0,0}, look[3]={0,0,1}, up[3]={0,1,0};
    gevrVrLocomotionSnapshot(pos,tracking,0,tick);
    gevrVrCameraWorld(pos,look,up);
}

static void runtimeIntegration() {
    gevrVrLocomotionReset();
    s_locomotionStats={};
    float fresh[16], replay[16];
    runtimeFrame(0,1000000000,0); assert(gevrVrPresentationDelta(fresh));
    gevrVrMarkEyesRendered(1);
    runtimeFrame(1,1016666666,6); assert(gevrVrPresentationDelta(fresh));
    near(s_presentedCamera.position[2],0); near(fresh[14],-6);
    gevrVrMarkEyesRendered(1);
    g_frameState.predictedDisplayTime=1024999999;
    assert(gevrVrRedrawDelta(replay)); near(s_presentedCamera.position[2],3);
    assert(gevrVrRedrawDelta(replay)); near(s_presentedCamera.position[2],3); // no accumulating correction
    gevrVrMarkRedrawn(); assert(s_statRedraw==1);
    /* Old cached geometry and a new game render evaluated at the same target
     * must present the same camera, even though their raw vertex frames differ. */
    g_frameState.predictedDisplayTime=1033333332;
    assert(gevrVrRedrawDelta(replay)); const float boundary=s_presentedCamera.position[2];
    runtimeFrame(2,1033333332,12); assert(gevrVrPresentationDelta(fresh));
    near(s_presentedCamera.position[2],boundary);
    gevrVrMarkEyesRendered(1);
    /* Head translation and rotation are current on the intermediate frame. */
    g_frameState.predictedDisplayTime=1041666665;
    for (auto &v:g_frameViews) {
        v.pose.position.x=0.1f;
        v.pose.orientation={0,std::sin(0.1f),0,std::cos(0.1f)};
    }
    assert(gevrVrRedrawDelta(replay)); near(s_presentedCamera.position[0],-10);
    near(s_presentedCamera.position[2],9);
    near(s_presentedCamera.rotation[0],-std::cos(0.2f));
    /* Locomotion corrections must not displace a stationary tracked gun. */
    float hand[16]; assert(gevrVrRedrawHandDelta(0,hand));
    for (int i=0;i<16;i++) near(hand[i],identity4[i]);
    newHandP[0]=0.05f; assert(gevrVrRedrawHandDelta(1,hand)); near(hand[12],5);
    newHandP[0]=0;
    gevrNotifyTeleport(); assert(g_gevrTeleportEpoch==1); assert(s_locomotion.count==0);
    assert(s_locomotionStats.resets[GEVR_LOCO_RESET_TELEPORT]==1);
    gevrVrLocomotionResetReason(GEVR_LOCO_RESET_TELEPORT);
    assert(s_locomotionStats.resets[GEVR_LOCO_RESET_TELEPORT]==1); // no history to clear twice
    assert(!gevrVrPresentationDelta(fresh));
    for (auto &v:g_frameViews) v.pose.position={0,0,0};
    runtimeFrame(0,1100000000,1000); assert(gevrVrPresentationDelta(fresh)); near(fresh[14],0);
    gevrVrMarkEyesRendered(0); assert(!s_haveRecordedCamera); assert(s_locomotion.count==0);
    assert(s_locomotionStats.resets[GEVR_LOCO_RESET_SCREEN]==1);
    runtimeFrame(1,1116666667,1000); positionValid=false;
    const float pos[3]={0,0,0}; gevrVrLocomotionSnapshot(pos,pos,0,2); assert(s_locomotion.count==0);
    assert(s_locomotionStats.resets[GEVR_LOCO_RESET_TRACKING]==1);
    positionValid=true;
    assert(freshRenders==4);
}

static void clampDiagnosticsAndPhysicalCollision() {
    const float wall[3]={10,0,0}, stopped[3]={0,0,0};
    float correction[3], normal[3];
    for (float jitter:{-0.05f,-0.005f,0.f,0.005f,0.05f}) {
        const float step[3]={jitter,0,jitter};
        assert(gevrLocomotionCollision(step,wall,stopped,correction,normal));
        near(correction[0],-std::max(0.f,jitter)); near(correction[2],0);
    }
    const float parallel[3]={0,0,0.5f}, request[3]={10,0,0.5f}, slide[3]={0,0,0.5f};
    assert(gevrLocomotionCollision(parallel,request,slide,correction,normal));
    near(correction[0],0); near(correction[2],0);
    const float away[3]={-0.5f,0,0}, into[3]={0.5f,0,0};
    assert(gevrLocomotionCollision(away,wall,stopped,correction,normal)); near(correction[0],0);
    assert(gevrLocomotionCollision(into,into,stopped,correction,normal)); near(correction[0],-0.5f);
    const float partly[3]={0.3f,0,0};
    assert(gevrLocomotionCollision(into,into,partly,correction,normal)); near(correction[0],-0.2f);
    assert(!gevrLocomotionCollision(into,into,into,correction,normal));
    const float rotated[9]={0,0,1,0,1,0,-1,0,0}, contact[3]={0.2f,0.1f,0.3f}, axis[3]={1,0,0};
    float total[3]={1,2,3};
    gevrLocomotionClipHead(rotated,contact,axis,total);
    near(total[0],1); near(total[1],2); near(total[2],2.7f); // only post-contact motion in rotated camera basis

    for (int hz:{72,80,90,120}) {
        GevrLocomotionHistory h{};
        GevrLocomotionPose q{};
        const int64_t period=std::llround(1e9/hz), start=1000000000;
        assert(!gevrLocomotionQuery(&h,start,&q));
        assert(h.clampReason==GEVR_LOCO_CLAMP_MISSING);
        gevrLocomotionSnapshot(&h,stopped,stopped,0,0,start,period);
        gevrLocomotionQuery(&h,start,&q);
        assert(h.clampReason==GEVR_LOCO_CLAMP_EARLY);
        assert(h.targetLead==-gevrLocomotionDelay(period));
        gevrLocomotionQuery(&h,start+h.delay-999,&q);
        assert(h.clampReason==GEVR_LOCO_CLAMP_NONE && q.time==h.poses[0].time);
        for (uint64_t tick=1;tick<8;tick++) {
            const int64_t time=start+(int64_t)(tick*1000000000/60);
            gevrLocomotionSnapshot(&h,stopped,stopped,0,tick,time,period);
            gevrLocomotionQuery(&h,time,&q);
            if (tick>=2) assert(h.clampReason==GEVR_LOCO_CLAMP_NONE);
        }
        const int64_t edge = h.poses[h.count-1].time + h.delay;
        gevrLocomotionQuery(&h,edge+999,&q);
        assert(h.clampReason==GEVR_LOCO_CLAMP_NONE && q.time==h.poses[h.count-1].time);
        gevrLocomotionQuery(&h,edge+1001,&q);
        assert(h.clampReason==GEVR_LOCO_CLAMP_LATE);
        /* Interrupted scheduling can leave the logical anchor behind even
         * with regular subsequent arrivals. Report that separately from a
         * reset/early-history clamp; do not silently shift the camera timeline. */
        for (uint64_t tick=8;tick<38;tick++) {
            const int64_t time=start+(int64_t)(tick*1000000000/60)+40000000;
            assert(gevrLocomotionSnapshot(&h,stopped,stopped,0,tick,time,period)==1);
            gevrLocomotionQuery(&h,time,&q);
            assert(h.clampReason==GEVR_LOCO_CLAMP_LATE);
            assert(h.targetLead==40000000-gevrLocomotionDelay(period));
            assert(h.resetReason==GEVR_LOCO_RESET_NONE);
        }
        gevrLocomotionQuery(&h,h.lastDisplayTime+101000000,&q);
        assert(h.clampReason==GEVR_LOCO_CLAMP_STALE);
        const int64_t resume=h.lastDisplayTime+101000000;
        assert(gevrLocomotionSnapshot(&h,stopped,stopped,0,38,resume,period)==2);
        assert(h.resetReason==GEVR_LOCO_RESET_GAP);
        assert(gevrLocomotionSnapshot(&h,stopped,stopped,0,39,resume+16666667,period+2000)==2);
        assert(h.resetReason==GEVR_LOCO_RESET_REFRESH);
        assert(gevrLocomotionSnapshot(&h,stopped,stopped,0,1,resume+33333334,period+2000)==2);
        assert(h.resetReason==GEVR_LOCO_RESET_CLOCK);
    }
    assert(std::strcmp(gevrLocomotionResetName(GEVR_LOCO_RESET_PHYSICAL),"PHYSICAL")==0);
}

static void matricesAndUniforms() {
    GevrPresentationCamera source{{12,175,30},{-1,0,0,0,1,0,0,0,-1}}, mid=source, end=source;
    mid.position[0]-=3; end.position[2]-=5;
    float a[16],b[16],composed[16],direct[16];
    gevrCameraDelta(&source,&mid,0.2f,a); gevrCameraDelta(&mid,&end,0.2f,b);
    gevrMat4Multiply(b,a,composed); gevrCameraDelta(&source,&end,0.2f,direct);
    for (int i=0;i<16;i++) near(composed[i],direct[i]);
    const float proj[16]={2,0,0,0,0,3,0,0,0,0,-1,-1,0,0,-2,0};
    const float clip[4]={4,9,8,10}; float moved[4];
    gevrPresentationClip(a,proj,clip,moved); near(moved[0],2*(2-0.6f)); near(moved[3],10);
    gfx_vr_eye_record(true,proj,false,a); assert(s_eyePresent);
    s_curPrg=&program; boundProgram=1; s_uniCacheValid=false;
    gevr_eye_present_draw(); assert(reprojection[1]==1);
    float expected[16]; gevrMat4Multiply(proj,a,expected);
    for (int i=0;i<16;i++) near(lastUniformMatrix[i],expected[i]);
    s_uniCacheValid=true; gfx_vr_eye_hand(0); gevr_eye_present_draw(); assert(reprojection[1]==0);
    gfx_vr_eye_hand(-1); gevr_eye_present_draw(); assert(reprojection[1]==1);
    gForceFlatShaderForMenu=true; gevr_eye_present_draw(); assert(reprojection[1]==0);
    gForceFlatShaderForMenu=false; gVrFlatPass=true; gevr_eye_present_draw(); assert(reprojection[1]==0);
    gVrFlatPass=false; s_curPrg=&secondProgram; boundProgram=2; s_uniCacheValid=false;
    gevr_eye_present_draw(); assert(reprojection[2]==1);
    gfx_vr_eye_record(false,nullptr,false,nullptr);
    assert(reprojection[1]==0 && reprojection[2]==0); assert(!s_eyePresent);
    /* Fresh presentation also works without the replay ring. */
    s_pmPtr=nullptr; gfx_vr_eye_record(true,proj,false,a); assert(s_eyePresent && !s_eyeRec);
    gfx_vr_eye_record(false,nullptr,false,nullptr);
}

static void combinedHeadAndBody() {
    gevrVrLocomotionReset();
    for (auto &v:g_frameViews) {
        v.pose.position={0,0,0}; v.pose.orientation={0,0,0,1};
    }
    g_frameState.predictedDisplayTime=2000000000;
    g_frameState.predictedDisplayPeriod=8333333;
    const float zero[3]={0,0,0}, up[3]={0,1,0};
    gevrVrLocomotionSnapshot(zero,zero,359,0);
    g_frameState.predictedDisplayTime=2016666666;
    const float pos[3]={0,0,6};
    gevrVrLocomotionSnapshot(pos,zero,1,1);
    g_cameraViews=g_frameViews;
    const float angle=-1.f*3.14159265359f/180.f;
    const float look[3]={-std::sin(angle),0,-std::cos(angle)};
    gevrVrCameraWorld(pos,look,up);
    float delta[16]; assert(gevrVrPresentationDelta(delta));
    gevrVrMarkEyesRendered(1);
    g_frameState.predictedDisplayTime=2024999999;
    for (auto &v:g_frameViews) {
        v.pose.position={0.1f,0,0.03f};
        v.pose.orientation={0,std::sin(0.1f),0,std::cos(0.1f)};
    }
    assert(gevrVrRedrawDelta(delta));
    near(s_presentedCamera.position[0],std::cos(angle)*10+std::sin(angle)*3);
    near(s_presentedCamera.position[2],3-std::sin(angle)*10+std::cos(angle)*3);
    /* Body interpolates across 359->1 while the physical head has its current
     * 0.2 rad turn: the combined world orientation is exactly the head turn. */
    near(s_presentedCamera.rotation[0],std::cos(0.2f));
    near(s_presentedCamera.rotation[2],std::sin(0.2f));
    const GevrPresentationCamera redraw=s_presentedCamera;
    /* Rebuild raw geometry at this same display time, including the already
     * confirmed physical displacement, and obtain the identical eye pose. */
    const float tracking[3]={redraw.position[0],0,redraw.position[2]-3};
    const float advanced[3]={tracking[0],0,6+tracking[2]};
    const float currentLook[3]={-std::sin(angle+0.2f),0,-std::cos(angle+0.2f)};
    gevrVrCameraWorld(advanced,currentLook,up);
    g_cameraViews=g_frameViews;
    assert(gevrVrPresentationDelta(delta));
    for (int i=0;i<3;i++) near(s_presentedCamera.position[i],redraw.position[i]);
    for (int i=0;i<9;i++) near(s_presentedCamera.rotation[i],redraw.rotation[i]);
}

/* Blocked physical motion, diagonal joystick wall sliding and current tangential
 * head travel must agree across retained geometry and fresh game cameras. */
static void collisionContinuity(int hz, float joystickSlide, float headSlide) {
    gevrVrLocomotionReset(); s_locomotionStats={}; s_statClamps=0;
    for (auto &v:g_frameViews) { v.pose.position={0,0,0}; v.pose.orientation={0,0,0,1}; }
    g_frameState.predictedDisplayPeriod=std::llround(1e9/hz);
    g_frameState.predictedDisplayTime=3000000000;
    g_cameraViews=g_frameViews;
    const float zero[3]={0,0,0}, seed[3]={0,175,0}, look[3]={0,0,1}, up[3]={0,1,0};
    gevrVrLocomotionSnapshot(seed,zero,0,0); gevrVrCameraWorld(seed,look,up);
    float delta[16]; assert(gevrVrPresentationDelta(delta)); gevrVrMarkEyesRendered(1);
    for (uint64_t tick=1;tick<40;tick++) {
        /* Simulate exact logical arrivals here; cadence tests independently
         * cover XR-scheduled game ticks at all four rates. */
        g_frameState.predictedDisplayTime=3000000000+(int64_t)(tick*1000000000/60);
        const float tracking[3]={tick*0.6f,0,tick*headSlide};
        for (auto &v:g_frameViews) v.pose.position={-tracking[0]/100,0,-tracking[2]/100};
        const float step[3]={0.6f,0,headSlide}, requested[3]={6.6f,0,joystickSlide+headSlide};
        const float actual[3]={0,0,joystickSlide+headSlide};
        gevrVrLocomotionCollision(step,requested,actual);
        assert(s_collisionContact);
        assert(gevrVrRedrawDelta(delta));
        const GevrPresentationCamera boundary=s_presentedCamera;
        const float body[3]={0,175,tick*(joystickSlide+headSlide)};
        gevrVrLocomotionSnapshot(body,tracking,0,tick);
        g_cameraViews=g_frameViews; gevrVrCameraWorld(body,look,up);
        assert(gevrVrPresentationDelta(delta));
        for (int i=0;i<3;i++) near(s_presentedCamera.position[i],boundary.position[i]);
        near(s_presentedCamera.position[0],0);
        gevrVrMarkEyesRendered(1);
        g_frameState.predictedDisplayTime+=8333333;
        for (auto &v:g_frameViews) {
            v.pose.position.x-=0.003f; v.pose.position.z-=headSlide/200;
        }
        assert(gevrVrRedrawDelta(delta)); near(s_presentedCamera.position[0],0);
        if (tick>=2) {
            const float t=(float)(g_frameState.predictedDisplayTime-3000000000-s_locomotion.delay)/1e9f;
            near(s_presentedCamera.position[2],t*60*joystickSlide+(tick+0.5f)*headSlide);
        }
        float hand[16]; assert(gevrVrRedrawHandDelta(0,hand));
        for (int i=0;i<16;i++) near(hand[i],identity4[i]);
    }
    assert(s_locomotion.count==8 && s_locomotionStats.seeds==1);
    assert(s_locomotionStats.resets[GEVR_LOCO_RESET_PHYSICAL]==0);
    for (int i=1;i<GEVR_LOCO_RESET_COUNT;i++) assert(s_locomotionStats.resets[i]==0);
    /* Late head retreat/tangential motion is free, as is movement after contact clears. */
    for (auto &v:g_frameViews) v.pose.position.x+=0.009f;
    assert(gevrVrRedrawDelta(delta)); near(s_presentedCamera.position[0],-0.6f);
    gevrVrLocomotionCollision(zero,zero,zero); assert(!s_collisionContact);
    gevrVrLocomotionResetReason(GEVR_LOCO_RESET_RECENTER); assert(!s_collisionContact);
}

static void collisionCadence(int hz, float origin=0) {
    gevrVrLocomotionReset(); s_locomotionStats={};
    const int64_t anchor=4000000000, period=std::llround(1e9/hz);
    const float up[3]={0,1,0}, look[3]={0,0,1};
    const float zero[3]={0,0,0};
    uint64_t tick=0; int64_t due=anchor; float previousHead[3]={0,0,0};
    float delta[16];
    g_frameState.predictedDisplayPeriod=period;
    for (int frame=0;frame<hz*3;frame++) {
        const int64_t now=anchor+frame*period;
        g_frameState.predictedDisplayTime=now;
        const float seconds=(float)(now-anchor)/1e9f;
        const float head[3]={seconds*36,0,seconds*18};
        for (auto &v:g_frameViews) { v.pose.position={-head[0]/100,0,-head[2]/100}; v.pose.orientation={0,0,0,1}; }
        if (now+2000000>=due) {
            const float step[3]={head[0]-previousHead[0],0,head[2]-previousHead[2]};
            const float request[3]={6+step[0],0,2+step[2]}, actual[3]={0,0,2+step[2]};
            gevrVrLocomotionCollision(step,request,actual);
            const bool cached=tick>2 && gevrVrRedrawDelta(delta);
            const GevrPresentationCamera boundary=s_presentedCamera;
            const float body[3]={origin,175,origin+tick*2+head[2]};
            gevrVrLocomotionSnapshot(body,head,0,tick);
            gevrVrCameraWorld(body,look,up); g_cameraViews=g_frameViews;
            assert(gevrVrPresentationDelta(delta));
            if (cached) for (int i=0;i<3;i++) near(boundary.position[i],s_presentedCamera.position[i],origin ? 0.03f : 0.002f);
            gevrVrMarkEyesRendered(1);
            std::memcpy(previousHead,head,sizeof(head));
            due=anchor+(int64_t)(++tick*1000000000/60);
        } else assert(gevrVrRedrawDelta(delta));
        near(s_presentedCamera.position[0],origin,origin ? 0.03f : 0.002f);
        if (now-anchor > s_locomotion.delay+33333334) {
            const float expected=origin+(float)(now-anchor-s_locomotion.delay)/1e9f*120+head[2];
            near(s_presentedCamera.position[2],expected,origin ? 0.03f : 0.002f);
        }
    }
    assert(s_locomotion.count==8 && s_locomotionStats.seeds==1);
    for (int i=1;i<GEVR_LOCO_RESET_COUNT;i++) assert(s_locomotionStats.resets[i]==0);
    gevrVrLocomotionCollision(zero,zero,zero);
}

int main() {
    /* Runtime periods can differ by a few ns from the rounded nominal period.
     * 8,333,332 ns must still select two 120 Hz frames, not three. */
    for (int64_t jitter : {-100LL, -2LL, -1LL, 0LL, 1LL, 2LL, 100LL}) {
        assert(gevrLocomotionDelay(8333333+jitter)==16666667);
        for (int hz : {72,80,90,120}) cadence(hz,{120,0,-120},jitter);
    }
    for (int hz:{72,80,90,120}) {
        cadence(hz,{0,0,120}); cadence(hz,{0,0,-120}); cadence(hz,{120,0,0});
        cadence(hz,{120,30,-120});
    }
    stopsAndResets(); clampDiagnosticsAndPhysicalCollision(); runtimeIntegration(); combinedHeadAndBody(); matricesAndUniforms();
    for (int hz:{72,80,90,120}) for (float speed:{-2.f,0.f,2.f}) collisionContinuity(hz,speed,0.3f);
    for (int hz:{72,80,90,120}) { collisionCadence(hz); collisionCadence(hz,-30000); }
    std::puts("PASS: 72/80/90/120 Hz cadence, physical/head separation, yaw wrap, stops/stalls/resets");
    std::puts("PASS: production XR fresh/redraw continuity, current head/hands, teleport and screen reset");
    std::puts("PASS: production fresh-draw uniforms, controller/HUD exclusion, scope-state cleanup, C linkage");
    std::puts("PASS: clamp/reset reasons, timeline lag, wall jitter/sliding/retreat and blocked physical movement");
    std::puts("PASS: physical collision rebase, late contact clipping, retreat, forward/back wall slides and fresh/redraw continuity at all rates");
}
