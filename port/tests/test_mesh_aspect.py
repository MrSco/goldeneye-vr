"""Bean's GPU meshes keep the HUD aspect modes the CPU vertices keep.

Screen shape 16:9 pillarboxes the front end: gfx_sp_vertex maps each vertex's
x into the 4:3 canvas centred on the screen, and the canvas scissor is mapped
the same way. Bean's static meshes and skins are transformed on the GPU from
gevr_mesh_params's matrix, which took the raw MP: on the headset (GLES 3 has
GPU meshes; PC's GL 3.0 draws them on the CPU) the Bean legal page spread the
canvas across the whole screen and the canvas scissor cut its Rare logo, top
right. The production helpers are compiled here against the scissor mapping.
"""
from pathlib import Path
import re
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
source = (ROOT / 'port/fast3d/gfx_pc.cpp').read_text()
header = (ROOT / 'port/fast3d/gfx_pc.h').read_text()


def span(start, end):
    i = source.index(start)
    return source[i:source.index(end, i)]


aspect = span('static bool gfx_aspect_mode_applies(', 'static void gfx_adjust_width_height_for_scale(')
update = span('static inline void gfx_update_aspect_mode(', 'static void gfx_sp_extra_geometry_mode(')
scissor = span('static void gfx_adjust_viewport_or_scissor(', 'static void gfx_calc_and_set_viewport(')
params = span('static bool gevr_mesh_params(', 'static bool gevr_mesh_run_visible(')
visible = span('static bool gevr_mesh_run_visible(', 'static bool gevr_mesh_draw_run(')
# The GPU's matrix and its cull both take the mapping, before anything reads them.
assert 'gfx_adjust_mp_for_aspect_ratio(p->mp);' in params, 'gevr_mesh_params draws Bean meshes unpillarboxed'
assert params.index('gfx_adjust_mp_for_aspect_ratio(p->mp);') > params.index('p->mp[r * 4 + 1] = -p->mp[r * 4 + 1];')
assert 'gfx_adjust_mp_for_aspect_ratio(mp);' in visible, 'gevr_mesh_run_visible culls against the unpillarboxed matrix'
assert visible.index('gfx_adjust_mp_for_aspect_ratio(mp);') < visible.index('gevrMeshBoundsVisible(')
defines = '\n'.join(re.findall(r'^#define G_ASPECT_\w+ .*$', header, re.M))
api = (ROOT / 'port/fast3d/gfx_api.h').read_text()
structs = '\n'.join(re.search(r'^struct %s \{.*?^\};$' % name, api, re.M | re.S).group(0)
                    for name in ('XYWidthHeight', 'GfxDimensions'))

fixture = r'''
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <algorithm>
/* STRUCTS */
/* DEFINES */
static struct GfxDimensions gfx_current_dimensions;
static struct { uint32_t aspect_mode; float aspect_ofs, aspect_scale; } rsp;
static bool fbActive, gevr_screen_pass;
static float g_vr_internal_scale = 1.0f;
static float targetW = 2560, targetH = 1440;   // the headset's 16:9 screen target
static float gevr_target_width(void) { return targetW; }
static float gevr_target_height(void) { return targetH; }
static bool vr_is_initialized(void) { return true; }
#define SCREEN_WIDTH 440    // the front end's canvas
#define SCREEN_HEIGHT 330
/* PRODUCTION */
static void mode(uint32_t m) {
    rsp.aspect_mode = m;
    gfx_update_aspect_mode();
}
// x_clip and w_clip of p through a column-per-input matrix (gevr_mesh_params's layout)
static void clip(const float mp[16], const float p[3], float *x, float *w) {
    *x = mp[0] * p[0] + mp[4] * p[1] + mp[8] * p[2] + mp[12];
    *w = mp[3] * p[0] + mp[7] * p[1] + mp[11] * p[2] + mp[15];
}
int main() {
    gfx_current_dimensions.width = 2560;
    gfx_current_dimensions.height = 1440;
    gfx_current_dimensions.aspect_ratio = 2560.0f / 1440.0f;
    gevr_screen_pass = true;
    // A perspective view of a turned model, as rsp.MP_matrix holds it
    const float raw[16] = { 0.9f, 0.1f, -0.3f, 0.3f,   -0.05f, 1.2f, 0.2f, -0.2f,
                            0.2f, -0.1f, -1.01f, -1.0f,  15.0f, -8.0f, 3990.0f, 4000.0f };
    const float pts[4][3] = { {0, 0, 0}, {-1400, 900, 30}, {1700, -1100, -60}, {2500, 1000, 400} };
    const uint32_t modes[3] = { G_ASPECT_LEFT_EXT, G_ASPECT_RIGHT_EXT, G_ASPECT_CENTER_EXT };
    for (uint32_t m : modes) {
        mode(m);
        float gpu[16];
        memcpy(gpu, raw, sizeof(gpu));
        gfx_adjust_mp_for_aspect_ratio(gpu);
        for (const auto &p : pts) {
            float x, w, gx, gw;
            clip(raw, p, &x, &w);
            clip(gpu, p, &gx, &gw);
            // the GPU's vertex lands where gfx_sp_vertex puts the CPU's
            assert(gw == w);
            assert(std::fabs(gx - gfx_adjust_x_for_aspect_ratio(x, w)) <= 1e-3f * std::fabs(w));
        }
    }
    // No mode, stereo frames, framebuffers and a 4:3 screen: the matrix as it was
    for (int c = 0; c < 4; c++) {
        float gpu[16];
        memcpy(gpu, raw, sizeof(gpu));
        mode(c == 0 ? 0 : G_ASPECT_CENTER_EXT);
        gevr_screen_pass = c != 1;
        fbActive = c == 2;
        gfx_current_dimensions.aspect_ratio = c == 3 ? 4.0f / 3.0f : 2560.0f / 1440.0f;
        gfx_adjust_mp_for_aspect_ratio(gpu);
        assert(!memcmp(gpu, raw, sizeof(gpu)));
    }
    gevr_screen_pass = true;
    fbActive = false;
    gfx_current_dimensions.aspect_ratio = 2560.0f / 1440.0f;

    // The legal screen: pillarboxed (front.c menu_jump_constructor_handler),
    // the canvas scissor, and the Bean page's Rare logo, whose right edge sits
    // at 0.775 of the canvas's half width (PC capture, 1280x720: x 1012 of
    // the 960-wide canvas from 160).
    mode(G_ASPECT_CENTER_EXT);
    XYWidthHeight area = { 0, 0, 440, 330 };
    gfx_adjust_viewport_or_scissor(&area, true);
    const float left = area.x, right = area.x + (float)area.width;
    assert(std::fabs(left - 320.0f) <= 1.0f && std::fabs(right - 2240.0f) <= 1.0f);
    const float page[16] = { 1, 0, 0, 0,  0, 1, 0, 0,  0, 0, 1, 0,  0, 0, 0, 1 };   // x_clip = page x
    const float logo[3] = { 0.775f, 0.8f, 0 };
    float gpu[16];
    memcpy(gpu, page, sizeof(gpu));
    gfx_adjust_mp_for_aspect_ratio(gpu);
    float x, w;
    clip(page, logo, &x, &w);
    const float before = (x / w + 1.0f) * 0.5f * targetW;
    clip(gpu, logo, &x, &w);
    const float after = (x / w + 1.0f) * 0.5f * targetW;
    const float edge[3] = { 1.0f, 0, 0 };
    clip(gpu, edge, &x, &w);
    const float canvasEdge = (x / w + 1.0f) * 0.5f * targetW;
    printf("scissor %.0f..%.0f px; logo edge %.0f px raw, %.0f px pillarboxed; canvas edge %.0f px\n",
           left, right, before, after, canvasEdge);
    assert(before > right);                    // the bug: past the scissor, cut
    assert(after > left && after < right);     // the fix: inside
    assert(std::fabs(canvasEdge - right) <= 1.0f);
    puts("PASS: Bean GPU meshes take the CPU's aspect mapping; the legal screen's logo is inside the canvas scissor");
}
'''

with tempfile.TemporaryDirectory(prefix='gevr-mesh-aspect-') as directory:
    temp = Path(directory)
    cpp, exe = temp / 'aspect.cpp', temp / 'aspect.exe'
    cpp.write_text(fixture.replace('/* STRUCTS */', structs).replace('/* DEFINES */', defines).replace('/* PRODUCTION */', aspect + update + scissor))
    subprocess.run([shutil.which('g++') or 'c++', '-std=c++17', '-O2', '-Wall', '-Werror',
                    '-I' + str(ROOT / 'port/include'), '-I' + str(ROOT / 'port/fast3d'),
                    str(cpp), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
