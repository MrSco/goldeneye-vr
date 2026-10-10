#include <openxr/openxr.h>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <initializer_list>
#define CHECK(x) do { if (!(x)) { std::printf("FAIL line %d\n",__LINE__); return 1; } } while(0)
#define XR_TRY(x) do { XrResult r=(x); if(XR_FAILED(r)) return r; } while(0)
#define vr_log(...) ((void)0)
struct Controller { XrActionStatePose pose; XrPosef controller_pose; bool is_active,is_tracked; };
static Controller gControllerStates[2];
static XrPosef gCtrlPosePlay[2];
static XrSpaceVelocity gCachedVelocity[2],gCachedVelocityPlay[2];
static XrSpaceLocation spaceLocation;
static XrPath gHandPaths[2]={1,2};
static XrSpace gControllerSpace[2]={(XrSpace)(uintptr_t)1,(XrSpace)(uintptr_t)2};
static XrAction gPoseAction;
static struct { XrSpace viewSpace,playSpace; } g_vrState={(XrSpace)(uintptr_t)3,(XrSpace)(uintptr_t)4};
static XrPosef mockPose[2][2];
static XrSpaceLocationFlags flags[2][2];
static XrResult result[2][2];
static bool actionActive[2]={true,true};
static XrResult GetActionState(XrAction,XrPath path,XrActionStatePose& pose) {
    pose.isActive=actionActive[path-1];return XR_SUCCESS;
}
extern "C" XrResult XRAPI_CALL xrLocateSpace(XrSpace space,XrSpace base,XrTime,XrSpaceLocation *out) {
    int hand=(int)(uintptr_t)space-1,frame=base==g_vrState.playSpace;
    out->locationFlags=flags[hand][frame];out->pose=mockPose[hand][frame];
    auto *velocity=(XrSpaceVelocity *)out->next;
    velocity->velocityFlags=XR_SPACE_VELOCITY_LINEAR_VALID_BIT|XR_SPACE_VELOCITY_ANGULAR_VALID_BIT;
    velocity->linearVelocity={4,5,6};velocity->angularVelocity={1,2,3};
    return result[hand][frame];
}
/* PRODUCTION */
static bool same(const XrPosef& a,const XrPosef& b) { return std::memcmp(&a,&b,sizeof(a))==0; }
int main() {
    const XrSpaceLocationFlags valid=XR_SPACE_LOCATION_POSITION_VALID_BIT|XR_SPACE_LOCATION_ORIENTATION_VALID_BIT;
    const XrSpaceLocationFlags tracked=XR_SPACE_LOCATION_POSITION_TRACKED_BIT|XR_SPACE_LOCATION_ORIENTATION_TRACKED_BIT;
    for(int hand=0;hand<2;hand++) {
        flags[hand][0]=flags[hand][1]=valid|tracked;
        for(int frame=0;frame<2;frame++) mockPose[hand][frame]={{0,0,0,1},{0.2f+hand,0.3f,-0.4f}};
        CHECK(sample(hand)==XR_SUCCESS);
        CHECK(gControllerStates[hand].is_active && gCachedVelocity[hand].velocityFlags);
        CHECK(gCachedVelocityPlay[hand].velocityFlags && same(gCtrlPosePlay[hand],mockPose[hand][1]));
        // Occlusion: the runtime keeps supplying valid, changing poses for seconds.
        for(int update=0;update<300;update++) {
            for(int frame=0;frame<2;frame++) {
                flags[hand][frame]=valid|(update%2 ? XR_SPACE_LOCATION_ORIENTATION_TRACKED_BIT:0);
                mockPose[hand][frame].position.x+=0.001f;
                float angle=update*0.001f;
                mockPose[hand][frame].orientation={0,std::sin(angle),0,std::cos(angle)};
            }
            CHECK(sample(hand)==XR_SUCCESS);
            CHECK(gControllerStates[hand].is_active && !gControllerStates[hand].is_tracked);
            CHECK(same(gControllerStates[hand].controller_pose,mockPose[hand][0]));
            CHECK(same(gCtrlPosePlay[hand],mockPose[hand][1]));
            CHECK(!gCachedVelocity[hand].velocityFlags && !gCachedVelocityPlay[hand].velocityFlags);
        }
        // Invalid coordinates cannot replace the most recent usable pose.
        const XrPosef savedView=gControllerStates[hand].controller_pose,savedPlay=gCtrlPosePlay[hand];
        for(XrSpaceLocationFlags missing:{XR_SPACE_LOCATION_POSITION_VALID_BIT,XR_SPACE_LOCATION_ORIENTATION_VALID_BIT,(XrSpaceLocationFlags)0}) {
            flags[hand][0]=flags[hand][1]=missing;
            mockPose[hand][0].position.x=mockPose[hand][1].position.x=999;
            CHECK(sample(hand)==XR_SUCCESS);
            CHECK(!gControllerStates[hand].is_active && !gControllerStates[hand].is_tracked);
            CHECK(same(gControllerStates[hand].controller_pose,savedView) && same(gCtrlPosePlay[hand],savedPlay));
        }
        flags[hand][0]=flags[hand][1]=valid|tracked;
        result[hand][0]=result[hand][1]=XR_ERROR_RUNTIME_FAILURE;
        CHECK(sample(hand)==XR_SUCCESS && !gControllerStates[hand].is_active);
        CHECK(same(gCtrlPosePlay[hand],savedPlay));
        result[hand][0]=result[hand][1]=XR_SUCCESS;actionActive[hand]=false;
        CHECK(sample(hand)==XR_SUCCESS && !gControllerStates[hand].is_active);
        CHECK(same(gCtrlPosePlay[hand],savedPlay));
        actionActive[hand]=true;mockPose[hand][0].position.x=mockPose[hand][1].position.x=1;
        CHECK(sample(hand)==XR_SUCCESS && gControllerStates[hand].is_active && gControllerStates[hand].is_tracked);
        CHECK(same(gCtrlPosePlay[hand],mockPose[hand][1]) && gCachedVelocityPlay[hand].velocityFlags);
        // Different tracking quality in the two spaces never freezes either valid pose.
        flags[hand][1]=valid;mockPose[hand][1].position.x=2;
        CHECK(sample(hand)==XR_SUCCESS && gControllerStates[hand].is_active && !gControllerStates[hand].is_tracked);
        CHECK(gCtrlPosePlay[hand].position.x==2 && !gCachedVelocityPlay[hand].velocityFlags);
    }
    puts("PASS: both controllers keep moving through valid inferred poses; invalid/inactive/error poses retained; recovery and velocity gating");
    return 0;
}
