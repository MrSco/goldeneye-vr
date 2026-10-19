"""Kept chunks: the record keeps what its replays may turn or step to, each draw culls to its own camera."""
from pathlib import Path
import os, re, shutil, subprocess, tempfile
ROOT = Path(__file__).resolve().parents[2]


def function(source, signature):
    match = re.search(re.escape(signature) + r"(?:\s|//[^\n]*\n)*\{", source)
    assert match, signature
    end, depth = match.end(), 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[match.start():end]


pc = (ROOT / 'port/fast3d/gfx_pc.cpp').read_text().replace('\r\n', '\n')
gl = (ROOT / 'port/fast3d/gfx_opengl.cpp').read_text().replace('\r\n', '\n')
slack = pc[pc.index('static float s_gevrKeptPresent[16];'):pc.index('extern "C" void gfx_static_mesh_release(')]
visible = pc[pc.index('static bool gevr_mesh_run_visible('):pc.index('static bool gevr_mesh_draw_run(')]
replay = function(gl, 'static bool gevr_eye_mesh_visible(const struct GfxMeshDrawParams* p, const float* reprojVP)')
fixture = r'''
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>
#include "gevr_static_mesh.h"
#include "gevr_mesh_visibility.h"
#include "gfx_rendering_api.h"
#define GEVR_FAR_DEPTH_SCALE .3f
enum { LOG_NOTE };
static void sysLogPrintf(int, const char*, ...) {}
static float g_viProjectionMatrixF[4][4], s_gevrPresentationDelta[16], vr_world_scale = 100;
static float vr_clip_margin_x_const, vr_clip_margin_x_w, vr_clip_margin_y_w;
static float s_gevrPresentA[4][4], s_gevrPresentB[4]; static bool s_gevrPresentReady, gVrFlatPass;
static bool vr_is_initialized(void) { return true; }
static struct { float MP_matrix[4][4]; } rsp;
static GfxClipParameters clip() { return {false, false}; }
static GfxRenderingAPI api; static GfxRenderingAPI* gfx_rapi = &api;
/* SLACK */
/* VISIBLE */
static float s_eyeHeadP[2], s_eye_offsets[8];
/* REPLAY */
static const float identity[16] = {1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
static void yaw(float degrees, float m[16]) {
    const float a = degrees * 3.14159265f / 180, c = cosf(a), s = sinf(a);
    memcpy(m, identity, sizeof(identity));
    m[0] = c; m[8] = s; m[2] = -s; m[10] = c;
}
static GevrStaticMeshRun box(float x0, float x1, float z0, float z1) {
    GevrStaticMeshRun r = {}; r.hasBounds = 1;
    r.lo[0] = x0; r.hi[0] = x1; r.lo[1] = -0.5f; r.hi[1] = 0.5f; r.lo[2] = z0; r.hi[2] = z1;
    return r;
}
int main() {
    /* 90 degrees each way, camera space = model space; clip w = -z */
    const float P[16] = {1,0,0,0, 0,1,0,0, 0,0,-1,-1, 0,0,-0.2f,0};
    memcpy(g_viProjectionMatrixF, P, sizeof(P)); memcpy(rsp.MP_matrix, P, sizeof(P));
    api.get_clip_parameters = clip;
    /* view units: 100 a metre (vr_world_scale) */
    GevrStaticMeshRun edge = box(105, 110, -100, -100);  /* 46 degrees, a metre ahead: just past the right edge */
    GevrStaticMeshRun wide = box(300, 310, -100, -100);  /* 72 degrees */
    GevrStaticMeshRun beside = box(20, 30, -15, -10);    /* 53 degrees, 15 cm ahead: a 10 cm step reaches it */
    GevrStaticMeshRun behind = box(-1, 1, 20, 21);       /* 20 cm behind */
    assert(!gevr_mesh_run_visible(&edge) && !gevr_mesh_run_visible(&beside));

    /* at rest: the floor (5 degrees, 10 cm) */
    gevr_mesh_slack_begin(false);
    assert(s_gevrSlack.on);
    assert(gevr_mesh_run_visible(&edge) && gevr_mesh_run_visible(&beside));
    assert(!gevr_mesh_run_visible(&wide) && !gevr_mesh_run_visible(&behind));
    gVrFlatPass = true; assert(!gevr_mesh_run_visible(&edge)); gVrFlatPass = false;
    gevr_mesh_slack_end();
    assert(!gevr_mesh_run_visible(&edge));

    /* replays 20 degrees from the kept pass: the next record keeps 30 */
    float d[16]; yaw(20, d);
    gevr_mesh_slack_measure(d);
    assert(fabsf(s_gevrReplayTurn - 20 * 3.14159265f / 180) < 1e-3f && s_gevrReplayStep < 1e-3f);
    gevr_mesh_slack_begin(false);
    assert(gevr_mesh_run_visible(&wide) && !gevr_mesh_run_visible(&behind));
    gevr_mesh_slack_end();
    /* ... measured against the kept pass's own presentation */
    yaw(20, s_gevrPresentationDelta);
    gevr_mesh_slack_begin(true);
    gevr_mesh_slack_end();
    s_gevrReplayTurn = 0;
    gevr_mesh_slack_measure(d);
    assert(s_gevrReplayTurn < 1e-3f);
    /* a step: translation in view units, decaying */
    memcpy(d, identity, sizeof(d)); d[12] = 30;
    gevr_mesh_slack_measure(d);
    assert(fabsf(s_gevrReplayStep - 30) < 1e-3f);
    memcpy(d, s_gevrPresentationDelta, sizeof(d));
    gevr_mesh_slack_measure(d);
    assert(s_gevrReplayStep < 30 && s_gevrReplayStep > 29);
    gevr_mesh_slack_begin(true);
    assert(fabsf(s_gevrSlack.near_w - 1.5f * s_gevrReplayStep) < 1e-3f && gevr_mesh_run_visible(&behind));
    gevr_mesh_slack_end();
    /* off: the old record cull */
    s_gevrSlackOn = false; gevr_mesh_slack_begin(false); assert(!s_gevrSlack.on); s_gevrSlackOn = true;

    /* the replay's matrix is the shader's: A * (x / p0, y / p5, -w, 1) */
    float A[16]; yaw(30, d);
    for (int c = 0; c < 4; c++) for (int r = 0; r < 4; r++) {
        A[c*4+r] = 0; for (int k = 0; k < 4; k++) A[c*4+r] += P[k*4+r] * d[c*4+k];
    }
    float mp[16], E[16];
    memcpy(mp, P, sizeof(P)); mp[12] = 3; mp[13] = -2;   /* the model somewhere ahead */
    gevrMeshReprojectMP(A, P[0], P[5], mp, E);
    const float pt[3] = {4, 1, -7};
    float clipRec[4], viaShader[4], viaE[4];
    for (int r = 0; r < 4; r++) clipRec[r] = mp[r]*pt[0] + mp[4+r]*pt[1] + mp[8+r]*pt[2] + mp[12+r];
    const float eye[4] = {clipRec[0] / P[0], clipRec[1] / P[5], -clipRec[3], 1};
    for (int r = 0; r < 4; r++) {
        viaShader[r] = A[r]*eye[0] + A[4+r]*eye[1] + A[8+r]*eye[2] + A[12+r]*eye[3];
        viaE[r] = E[r]*pt[0] + E[4+r]*pt[1] + E[8+r]*pt[2] + E[12+r];
        assert(fabsf(viaShader[r] - viaE[r]) < 1e-4f);
    }

    /* each draw: the kept chunk past the edge drawn only once the head turns to it */
    GfxMeshDrawParams p = {}; memcpy(p.mp, P, sizeof(P)); p.has_bounds = true;
    memcpy(p.lo, edge.lo, sizeof(p.lo)); memcpy(p.hi, edge.hi, sizeof(p.hi));
    s_eyeHeadP[0] = P[0]; s_eyeHeadP[1] = P[5];
    assert(!gevr_eye_mesh_visible(&p, NULL));
    yaw(0, d); assert(!gevr_eye_mesh_visible(&p, P));
    yaw(10, d);   /* turned right by 10 degrees: the chunk at 46 now at 36 */
    for (int c = 0; c < 4; c++) for (int r = 0; r < 4; r++) {
        A[c*4+r] = 0; for (int k = 0; k < 4; k++) A[c*4+r] += P[k*4+r] * d[c*4+k];
    }
    assert(gevr_eye_mesh_visible(&p, A));
    s_eye_offsets[0] = 0; s_eye_offsets[4] = 12;   /* a stereo margin reaches it too */
    assert(gevr_eye_mesh_visible(&p, NULL));
    p.skinned = true; s_eye_offsets[4] = 0; assert(gevr_eye_mesh_visible(&p, NULL));
    puts("Kept chunks: replay slack floor and departure, presentation, decay, shader-matched replay cull passed");
}
'''
fixture = fixture.replace('/* SLACK */', slack).replace('/* VISIBLE */', visible).replace('/* REPLAY */', replay)
with tempfile.TemporaryDirectory(prefix='gevr-replay-cull-') as temp:
    cpp = Path(temp) / 'cull.cpp'; cpp.write_text(fixture)
    exe = Path(temp) / 'cull.exe'
    sanitize = ['-fsanitize=address,undefined', '-fno-omit-frame-pointer', '-no-pie'] if os.environ.get('BEAN_SANITIZE') else []
    subprocess.run([shutil.which('g++') or 'c++', '-std=c++17', '-O2', '-Wall', '-Wextra', '-Wno-unused-function', *sanitize,
                    '-I' + str(ROOT / 'port/include'), '-I' + str(ROOT / 'port/fast3d'), str(cpp), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
