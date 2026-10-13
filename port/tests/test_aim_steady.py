"""Exercise headset-rate steadying using the production filter and pose getters."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import re

ROOT = Path(__file__).resolve().parents[2]


def function(source, signature):
    match = re.search(re.escape(signature) + r'[^;{]*\{', source)
    assert match, signature
    start, end = match.start(), match.end()
    depth = 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]


inp = (ROOT / 'port/vr/vr_input.cpp').read_text()
xr = (ROOT / 'port/vr/vr_openxr.cpp').read_text()
fixture = (ROOT / 'port/tests/grip_redraw_native.cpp').read_text()
start = fixture.index('static XrQuaternionf gevr_steady(')
end = fixture.index('extern "C" int gevrVrGripPose(', start)
fixture = fixture[:start] + '''using XrTime = long long;
static float magnification = 1;
static float gevrScopeMagnificationCtrl(int) { return magnification; }
static int gevrStereoTwoHandGun() { return 0; }
''' + fixture[end:]
fixture = fixture.replace('static int gevrGripSteadyOnCtrl(', 'static int scopedGrip, twoHand;\nstatic int gevrGripSteadyOnCtrl(')
fixture = fixture.replace('return 0; }\nstatic int gevrStereoTwoHandGrip()',
                          'return scopedGrip; }\nstatic int gevrStereoTwoHandGrip()')
fixture = fixture.replace('static int gevrStereoTwoHandGrip() { return 0; }',
                          'static int gevrStereoTwoHandGrip() { return twoHand; }')
production = '\n'.join(function(source, signature) for source, signature in (
    (xr, 'static void vr_quat_to_mat3('),
    (xr, 'extern "C" int gevrVrFrameHeadPose('),
    (inp, 'static inline int gevrPhysHand('),
    (inp, 'static XrQuaternionf gevr_steady('),
    (inp, 'extern "C" void gevrVrSteadyControllers('),
    (inp, 'static XrPosef gevrPlayPoseToView('),
    (inp, 'extern "C" void gevrVrSnapshotControllers('),
    (inp, 'extern "C" int gevrVrGripPoseCamera('),
    (inp, 'extern "C" int gevrVrGripPoseSteady('),
    (inp, 'extern "C" int gevrVrGripPose('),
    (inp, 'extern "C" int gevrVrGripUsable('),
    (inp, 'extern "C" int gevrVrGripTracked('),
    (xr, 'extern "C" int gevrVrRedrawHandDelta('),
    (xr, 'static bool vr_scope_gun_pose('),
))
fixture = fixture.replace('/* PRODUCTION */', production)
fixture = fixture[:fixture.index('int main(')] + r'''
static XrTime timeNow = 1000000000;
static void sample(int ctrl, float degrees, XrTime step) {
    int h = gevrPhysHand(ctrl);
    float radians = degrees * 3.14159265f / 180;
    gCtrlPosePlay[h] = {{0,sinf(radians/2),0,cosf(radians/2)}, {0.4f,0.5f,-0.6f}};
    gControllerStates[h].controller_pose = gCtrlPosePlay[h];
    gControllerStates[h].is_active = true;
    timeNow += step;
    gevrVrSteadyControllers(timeNow, 1);
}
int main() {
    for (int lefty=0; lefty<2; lefty++) for (int ctrl=0; ctrl<2; ctrl++) {
        VrLeftHandedMode = lefty;
        for (int level=0; level<3; level++) {
            reset(); VrAimSteady=level;
            sample(ctrl,0,1000000000);
            XrPosef camera=identityHead();
            gevrVrSnapshotControllers(&camera,1);
            float last=0;
            for (int frame=1; frame<=120; frame++) {
                sample(ctrl,frame*.1f,8333333);
                float p[3],q[4];
                assert(gevrVrGripPoseSteady(ctrl,p,q));
                assert(q[1]>last); // rotation advances on EVERY XR frame
                last=q[1];
                const auto saved=gSteadyQ[gevrPhysHand(ctrl)];
                const auto old=gCamCtrlPose[gevrPhysHand(ctrl)];
                for (int read=0; read<4; read++) {
                    float delta[16]; assert(gevrVrRedrawHandDelta(ctrl,delta));
                    gevrVrSteadyControllers(timeNow,1); // no double filtering
                }
                assert(memcmp(&saved,&gSteadyQ[gevrPhysHand(ctrl)],sizeof(saved))==0);
                assert(memcmp(&old,&gCamCtrlPose[gevrPhysHand(ctrl)],sizeof(old))==0);
                if (!(frame%2)) {
                    gevrVrSnapshotControllers(&camera,1);
                    float cp[3],cq[4]; assert(gevrVrGripPoseCamera(ctrl,cp,cq));
                    for (int k=0;k<4;k++) near(q[k],cq[k]);
                    assert(memcmp(&saved,&gSteadyQ[gevrPhysHand(ctrl)],sizeof(saved))==0);
                }
            }
        }
        // Equal elapsed time gives equal low-speed steadying at 60/90/120 Hz.
        float result[3]; int rates[]={60,90,120};
        for(int r=0;r<3;r++) {
            reset(); VrAimSteady=2; sample(ctrl,0,1000000000);
            for(int n=0;n<rates[r]/30;n++) sample(ctrl,.2f,1000000000/rates[r]);
            float p[3],q[4]; assert(gevrVrGripPoseSteady(ctrl,p,q)); result[r]=q[1];
        }
        near(result[0],result[1]); near(result[0],result[2]);
        // A moving hand lags the same at 60/90/120 Hz: the speed test is per
        // 60 Hz frame. Compare the steadied turn at the shared 1/30 s instants.
        for(float speed: {60.0f,150.0f,300.0f}) {
            float lag[3][16];
            for(int r=0;r<3;r++) {
                reset(); VrAimSteady=2; sample(ctrl,0,1000000000);
                int per=rates[r]/30;
                for(int k=1;k<=16*per;k++) {
                    sample(ctrl,speed*k/rates[r],1000000000/rates[r]);
                    if(k%per) continue;
                    float p[3],q[4]; assert(gevrVrGripPoseSteady(ctrl,p,q));
                    lag[r][k/per-1]=(2*asinf(gCtrlPosePlay[gevrPhysHand(ctrl)].orientation.y)-2*asinf(q[1]))*180/3.14159265f;
                }
            }
            float worst=0;
            for(int r=1;r<3;r++) for(int i=0;i<16;i++) worst=fmaxf(worst,fabsf(lag[r][i]-lag[0][i]));
            std::printf("%.0f deg/s: worst lag gap vs 60 Hz %.2f deg\n",speed,worst);
            assert(worst<0.75f);
        }
        // Scope grip still filters with the launcher steadying setting Off.
        reset(); scopedGrip=1; magnification=25; twoHand=1;
        sample(ctrl,0,1000000000); sample(ctrl,1,8333333);
        float p[3],q[4]; assert(gevrVrGripPoseSteady(ctrl,p,q));
        assert(q[1]>0 && q[1]<gCtrlPosePlay[gevrPhysHand(ctrl)].orientation.y);
        scopedGrip=twoHand=0; magnification=1;
    }
    puts("PASS: 120 Hz rotation advances, immutable game snapshots, idempotent getters, refresh-rate response and scoped grip in either handedness");
}
'''
with tempfile.TemporaryDirectory(prefix='gevr-aim-steady-') as directory:
    temp = Path(directory)
    source, exe = temp / 'steady.cpp', temp / 'steady'
    source.write_text(fixture)
    subprocess.run([shutil.which('g++') or 'g++', '-std=c++17', '-O2',
                    '-I' + str(ROOT / 'port/include'), str(source), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
