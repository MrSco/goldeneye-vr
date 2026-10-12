#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

#include <algorithm>
#include <map>
#include <unordered_map>
#include <vector>

#include <SDL.h>

#ifndef _LANGUAGE_C
#define _LANGUAGE_C
#endif
#include <PR/gbi.h>
#include "glad/glad.h"
#include "gfx_cc.h"
#include "gfx_rendering_api.h"
#include "gfx_pc.h"
#include "../vr/vr_log.h"
#include "../src/net/net_game.h"
#include "gevr_line_geometry.h"
#include "gevr_mesh_visibility.h"
#include "gevr_frame_timing.h"
#include "gevr_hud_bounds.h"
#include "gevr_vertex_ownership.h"
/* Inclusive CPU timings, only with Show stats. RAII balances early returns;
 * draw_issue is nested in draw_batch, shader_bind may nest in shader_compile. */
class GevrCpuSection {
    GevrProfileSection span;
public:
    explicit GevrCpuSection(GevrFrameTimingSection value) : span(value) {}
};
/* Line mode, online (host fun flag) or offline (cheat/debug toggle): the
   N64 coverage visualization has no GL equivalent, so world edges are drawn here. */
extern "C" int get_debug_VisCVG_flag(void);
#ifdef ANDROID
#include <android/log.h>
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  "GoldenEye-VR", __VA_ARGS__)
#else
#define LOGI(...) vr_log(__VA_ARGS__)
#endif



// ============================================================================
// GLOBAL STATE - OVR_multiview / Render Targets / others
// ============================================================================
bool use_multiview = false;
// Per eye: IPD translation, horizontal frustum centre, HUD
// vertical frustum centre.
float s_eye_offsets[8] = { 0.0f, 0.0f, 0.0f, 0.0f,
                           0.0f, 0.0f, 0.0f, 0.0f };
extern float g_eyeTanHalfFov[2];
extern "C" bool vr_dl_is_pause_or_menu;
extern float vr_world_scale;
extern bool is_meta_runtime;
bool copy_fbo_menu = false;
bool VrIsTitleLegal = true;
static bool hud_L_was_drawn = false;
static bool hud_R_was_drawn = false;
static bool hud_H_was_drawn = false;
static bool hud_P_was_drawn = false;   /* the weapon panel (issue #10) */
// ============================================================================
// GLOBAL STATE - MSAA
// ============================================================================
typedef void (*PFNGLFRAMEBUFFERTEXTUREMULTIVIEWOVRPROC)(
        GLenum, GLenum, GLuint, GLint, GLint, GLsizei);
PFNGLFRAMEBUFFERTEXTUREMULTIVIEWOVRPROC
        glFramebufferTextureMultiviewOVR = nullptr;

typedef void (APIENTRY* PFNGLFRAMEBUFFERTEXTUREMULTISAMPLEMULTIVIEWOVRPROC)(GLenum target, GLenum attachment, GLuint texture, GLint level, GLsizei samples, GLint baseViewIndex, GLsizei numViews);
PFNGLFRAMEBUFFERTEXTUREMULTISAMPLEMULTIVIEWOVRPROC pfnFramebufferTextureMultisampleMultiviewOVR = nullptr;

// ============================================================================
// GLOBAL STATE - GLuint
// ============================================================================
extern GLuint g_currentMultiviewSwapchainTex;
extern "C" GLuint vr_get_current_multiview_swapchain_tex();

static GLuint s_mirror_prog  = 0;
static GLuint s_mirror_vao   = 0;
static GLint  s_mirror_uloc_tex   = -1;
static GLint  s_mirror_uloc_layer = -1;
static GLint  s_mirror_uloc_rect  = -1;

static GLuint mv_blit_prog = 0;
static GLint mv_blit_uTexLoc = -1;
static GLint mv_blit_uFlipYLoc = -1;
static GLint mv_blit_uRectLoc = -1;

// ============================================================================
// GLOBAL STATE - gVrMenuH Head XR layer
// ============================================================================
static bool gVrMenuLWasClearedThisFrame = false;
static int gVrMenuLCaptureDepth = 0;

static bool gVrMenuHWasClearedThisFrame = false;
static int gVrMenuHCaptureDepth = 0;

static bool gVrMenuPWasClearedThisFrame = false;
static int gVrMenuPCaptureDepth = 0;

static GLint gCurEyeOffsetLeftLoc  = -1;
static GLint gCurEyeOffsetRightLoc = -1;

static int gMenuCaptureRefCount = 0;
bool gForceFlatShaderForMenu = false;

// Set while the display list renders the virtual screen: the vertex shader
// passes clip positions through untouched, so the frame is the game's own
// flat image (see port/vr/vr_screen.h).
bool gVrFlatPass = false;
static GLint gCurVrFlatLoc = -1;
static GLint gCurIsMenuLoc = -1;
static GLint gCurDecalBiasLoc = -1;
// The projection's depth term (rsp.P_matrix[3][2]), set by gfx_pc.cpp for the
// decal band: a point moved D view units along its ray changes clip z by
// P32 * D / w, so a band of constant real thickness is uDecalBias = P32 * D.
float gfx_decal_proj_z = 0.0f;
/*
 * The shader divides the band's offset by clip w. A large ground decal can
 * have a corner behind the camera (w < 0): clamped at 0.0001 that corner got
 * a pull of ~1e5 and dragged the whole triangle toward the eye - Runway's
 * scorch marks drew over the tank from inside it. Clamp at the near plane
 * instead (B ~ -2 * near), which no visible point is closer than.
 */
static void gevr_set_decal_bias(float bias)
{
    if (gCurDecalBiasLoc >= 0) {
        float nearW = -gfx_decal_proj_z * 0.5f;
        if (nearW < 0.01f) nearW = 0.01f;
        glUniform2f(gCurDecalBiasLoc, bias, nearW);
    }
}

static inline void gfx_opengl_menu_capture_push(void) {
    gMenuCaptureRefCount++;
    gForceFlatShaderForMenu = true;
}

static inline void gfx_opengl_menu_capture_pop(void) {
    if (gMenuCaptureRefCount > 0) gMenuCaptureRefCount--;
    gForceFlatShaderForMenu = (gMenuCaptureRefCount > 0);
}
// ============================================================================
// GLOBAL STATE - Mirror Layers
// ============================================================================
enum VrMirrorMenuSource {
    VR_MIRROR_MENU_L = 0,
    VR_MIRROR_MENU_R = 1,
    VR_MIRROR_MENU_H = 2,
};

struct VrMirrorMenuEntry {
    VrMirrorMenuSource source;
    float mvp[16];
};

extern "C" int vrGetMenuMirrorMVPList(int eyeIndex, VrMirrorMenuEntry* outEntries);
extern GLuint gfx_opengl_get_vr_menu_texture(void);
extern GLuint gfx_opengl_get_vr_menu_texture_R(void);
extern GLuint gfx_opengl_get_vr_menu_texture_H(void);
//---

// Fullscreen tri, VS without attributes (uses gl_VertexID)
//#version 300 es if gles, otherwise 330 core
static const char* mv_blit_vs_src =
        "#version 300 es\n"
        "precision highp float;\n"
        "out vec2 vUV;\n"
        "const vec2 pos[3] = vec2[3](\n"
        "    vec2(-1.0,-1.0),\n"
        "    vec2( 3.0,-1.0),\n"
        "    vec2(-1.0, 3.0)\n"
        ");\n"
        "void main() {\n"
        "    vec2 p = pos[gl_VertexID];\n"
        "    gl_Position = vec4(p, 0.0, 1.0);\n"
        "    vUV = p * 0.5 + 0.5;\n"
        "}\n";


#ifdef ANDROID
static const char* mv_blit_fs_src =
        "#version 300 es\n"
        "precision highp float;\n"
        "in vec2 vUV;\n"
        "out vec4 outColor;\n"
        "uniform sampler2DArray uTex;\n"
        "uniform int uLayer;\n"
        "uniform int uFlipY;\n"
        "uniform vec4 uRect; // u0, v0, u1, v1\n"
        "void main() {\n"
        "    vec2 uv = vUV;\n"
        "    if (uFlipY != 0) uv.y = 1.0 - uv.y;\n"
        "    // Remap 0..1 to the source sub-rectangle\n"
        "    vec2 srcUV;\n"
        "    srcUV.x = mix(uRect.x, uRect.z, uv.x);\n"
        "    srcUV.y = mix(uRect.y, uRect.w, uv.y);\n"
        "    vec4 col = texture(uTex, vec3(srcUV, float(uLayer)));\n"
        "    // Force l'Alpha à 1.0 pour corriger les layers OpenXR !\n"
        "    outColor = vec4(col.rgb, 1.0);\n"
        "}\n";

#else
extern "C" int gfx_sdl_get_mirror_eye();
extern "C" bool gfx_sdl_is_mirror_enabled();
static GLint s_mirror_uloc_sbs = -1;
extern "C" bool gfx_sdl_is_mirror_sbs();

static GLuint s_logo_tex = 0;
int    logo_w   = 0;
int    logo_h   = 0;

extern "C" GLuint gfx_opengl_get_logo_tex()  { return s_logo_tex; }
extern "C" bool mirror_enabled;
extern "C" void mirror_apply_size(bool enabled);

static GLuint load_bmp_texture(const char* path) {
    FILE* f = fopen(path, "rb");
    if (!f) {
//        vr_log("load_bmp_texture: cannot open %s", path);
        return 0;
    }

    uint8_t header[54];
    if (fread(header, 1, 54, f) != 54 || header[0] != 'B' || header[1] != 'M') {
//        vr_log("load_bmp_texture: not a valid BMP file");
        fclose(f);
        return 0;
    }

    int w      = *(int*)&header[18];
    int h      = *(int*)&header[22];
    int offset = *(int*)&header[10];
    int bpp    = *(uint16_t*)&header[28]; // bits per pixel

    fseek(f, offset, SEEK_SET);

    int bytes_per_pixel = bpp / 8; // 3 or 4
    int row_size = (w * bytes_per_pixel + 3) & ~3;

    std::vector<uint8_t> raw(row_size * abs(h));
    fread(raw.data(), 1, raw.size(), f);
    fclose(f);

    // Convert BGR(A) to RGB and flip vertically
    std::vector<uint8_t> pixels(w * abs(h) * 3);
    for (int y = 0; y < abs(h); y++) {
        int src_y = (h > 0) ? (abs(h) - 1 - y) : y;
        for (int x = 0; x < w; x++) {
            uint8_t* src = &raw[src_y * row_size + x * bytes_per_pixel];
            uint8_t* dst = &pixels[(y * w + x) * 3];
            dst[0] = src[2]; // R ← B
            dst[1] = src[1]; // G
            dst[2] = src[0]; // B ← R
        }
    }

    GLuint tex;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, w, abs(h), 0,
                 GL_RGB, GL_UNSIGNED_BYTE, pixels.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glBindTexture(GL_TEXTURE_2D, 0);

//    vr_log("load_bmp_texture: loaded %s (%dx%d, %dbpp)", path, w, abs(h), bpp);
    return tex;
}

extern "C" void gfx_opengl_load_mirror_logo(const char* path) {
    // Re-open to read dimensions from the BMP header
    FILE* f = fopen(path, "rb");
    if (!f) return;
    uint8_t header[54];
    if (fread(header, 1, 54, f) == 54 && header[0] == 'B' && header[1] == 'M') {
        logo_w = *(int*)&header[18];
        logo_h = abs(*(int*)&header[22]);
    }
    fclose(f);
    s_logo_tex = load_bmp_texture(path);
}


static const char* mv_blit_fs_src =
        "#version 300 es\n"
        "precision highp float;\n"
        "in vec2 vUV;\n"
        "out vec4 outColor;\n"
        "uniform sampler2DArray uTex;\n"
        "uniform int uLayer;\n"
        "uniform int uFlipY;\n"
        "uniform vec4 uRect;\n"
        "uniform int uSbs;\n"
        // sRGB encoding function
        "vec3 linear_to_srgb(vec3 c) {\n"
        "    return mix(c * 12.92,\n"
        "               1.055 * pow(clamp(c, 0.0, 1.0), vec3(1.0/2.2)) - 0.055,\n"
        "               step(0.0031308, c));\n"
        "}\n"
        "void main() {\n"
        "    vec2 uv = vUV;\n"
        "    if (uFlipY != 0) uv.y = 1.0 - uv.y;\n"
        "    vec4 col;\n"
        "    if (uSbs != 0) {\n"
        //      Left side (uv.x < 0.5) = eye 0, right side = eye 1
        "        int eye = (uv.x < 0.5) ? 0 : 1;\n"
        "        vec2 half_uv = vec2(\n"
        "            (eye == 0) ? uv.x * 2.0 : (uv.x - 0.5) * 2.0,\n"
        "            uv.y\n"
        "        );\n"
        //      Apply uRect in each half
        "        vec2 srcUV;\n"
        "        srcUV.x = mix(uRect.x, uRect.z, half_uv.x);\n"
        "        srcUV.y = mix(uRect.y, uRect.w, half_uv.y);\n"
        "        col = texture(uTex, vec3(srcUV, float(eye)));\n"
        "    } else {\n"
        //      Normal mode — single eye
        "        vec2 srcUV;\n"
        "        srcUV.x = mix(uRect.x, uRect.z, uv.x);\n"
        "        srcUV.y = mix(uRect.y, uRect.w, uv.y);\n"
        "        col = texture(uTex, vec3(srcUV, float(uLayer)));\n"
        "    }\n"
        "    outColor = vec4(linear_to_srgb(col.rgb), 1.0);\n"
        "}\n";

#endif

//---------------------------------------------------------

using namespace std;

struct ShaderProgram {
    GLuint opengl_program_id;
    uint8_t num_inputs;
    bool used_textures[SHADER_MAX_TEXTURES];
    uint8_t num_floats;
    GLint attrib_locations[16];
    uint8_t attrib_sizes[16];
    uint8_t num_attribs;
    GLint frame_count_location;
    GLint noise_scale_location;
    GLint three_point_filter_locations[2];
    GLint opaque_depth_pass_location;


    GLint eyeOffsetLeftLocation;
    GLint eyeOffsetRightLocation;

    GLint isMenuLocation;
    GLint worldScaleLocation;

    GLint IsTitleLegal;
    GLint vrFlatLocation;
    GLint decalBiasLocation;   // GoldenEye: view-space depth nudge for the decal band

    GLint TanHalfFovLeft;
    GLint TanHalfFovRight;

    GLint scopeLocation;       // GoldenEye (issue #40): the sniper scope's pass
    GLint scopeVPLocation;
    GLint scopeHeadPLocation;
    GLint reprojLocation;      // GoldenEye (issue #53): the in-between frame's redraw
    GLint reprojVPLocation;
    GLint zTintLocation;       // GoldenEye (issue #72): gevr_zdebug.txt's depth-state tint

    // GoldenEye: static meshes (gevr_static_mesh.h). A program's mesh variant
    // (gevr_mesh_program) is the same program with a vertex shader that reads a
    // mesh's own Vtx and takes the batch's floats as uniforms.
    uint64_t shader_id0;
    uint32_t shader_id1;
    bool opt_fog, opt_grayscale, is_mesh;
    GLint meshTintLocation, meshLitLocation, meshAmbientLocation, meshLightDirLocation, meshLightColorLocation,
        meshNumLightsLocation, meshFogLocation, meshFogParamsLocation, meshValidLocation;
    // each array element on its own: the compiler keeps only those the shader uses
    GLint meshMPLocation, meshTexLocation[2], meshTexRotationLocation[2], meshInSelLocation[GFX_MESH_MAX_INPUTS],
        meshInConstLocation[GFX_MESH_MAX_INPUTS];

};



struct Framebuffer {
    uint32_t width, height;
    bool has_depth_buffer;
    uint32_t msaa_level;
    bool invert_y;

    bool is_multiview; // VR

    GLuint fbo, clrbuf, clrbuf_msaa, rbo;
};

static std::map<pair<uint64_t, uint32_t>, struct ShaderProgram> shader_program_pool;
// GoldenEye: the static-mesh variants, by the same key; one that failed is kept with program 0
static std::map<pair<uint64_t, uint32_t>, struct ShaderProgram> mesh_program_pool;
static bool s_buildMesh;          // gfx_opengl_create_and_load_new_shader makes a mesh variant
static bool s_meshClipDistance;   // GL_EXT_clip_cull_distance: the CPU's far reject under depth clamp
static bool s_meshBroken;         // a mesh variant failed: the CPU draws the meshes
static GLuint s_skinPaletteBuffer; // one transient UBO; redraw uploads its saved palette
static uint32_t s_glGeneration;   // gfx_opengl_init: buffers made in an older context are gone
static bool s_drawElements;       // gevr_issue_draw: the bound static mesh's indices, not the vertex ring
// What gfx_opengl_set_uniforms last wrote to each mesh variant: a mesh draw binds
// its variant and the frontend's program again each time, and these rarely change
struct GevrMeshUniforms {
    int frame, filter[2], menu, title, flat;
    float noise, eye[8], scale, tan[2];
};
static std::map<const struct ShaderProgram*, GevrMeshUniforms> s_meshUniforms;
static GLuint opengl_vbo;
static GLuint opengl_vao;
static bool current_depth_mask;
// GoldenEye: the current draws use the RDP's decal Z mode (see gfx_opengl_draw_triangles).
static bool s_decalZ;
/*
 * PORT test switch (decal cropping): files/gevr_decal.txt "mode a b", re-read
 * every ~2 s, picks how ZMODE_DEC draws are depth-tested so candidates can be
 * compared live in the headset:
 *   0  polygon offset -2,-2 (the default, as gepc-ref)
 *   1  polygon offset a,b
 *   2  offset -2,-2 plus a fixed pull of a view units toward the eye
 *   3  the two-pass stencil band (HANDOFF 57/60), half-width a view units
 *   4  room decals reach a view units (at scale 1) behind the surface in front
 */
static bool s_isDecal;
static int s_decalMode = 0;
static float s_decalA = 0.0f, s_decalB = 0.0f;
static void gevr_decal_switch_poll(void)
{
    static unsigned n;
    if ((n++ % 240) != 0) return;
    FILE *f = fopen("/sdcard/Android/data/com.gevr.port/files/gevr_decal.txt", "r");
    int mode = 0;
    float a = 0.0f, b = 0.0f;
    if (f) {
        if (fscanf(f, "%d %f %f", &mode, &a, &b) < 1) mode = 0;
        fclose(f);
    }
    if (mode != s_decalMode || a != s_decalA || b != s_decalB) {
        s_decalMode = mode;
        s_decalA = a;
        s_decalB = b;
        sysLogPrintf(LOG_NOTE, "decalswitch: mode %d a %.2f b %.2f", mode, a, b);
    }
}

/*
 * PORT test switch (issue #72, geometry showing through walls):
 * files/gevr_zdebug.txt, one int, re-read every ~2 s, so the cause can be
 * seen in the headset without a rebuild:
 *   0  off
 *   1  tint every world draw by its depth state: red = no depth test,
 *      magenta = depth test without Z_CMP (drawn whatever is in front),
 *      green = decal (ZMODE_DEC), cyan = blended (ZMODE_XLU), yellow = Z_CMP
 *      without Z_UPD; untinted = ordinary opaque
 *   2  every draw without Z_CMP is depth-tested (GL_LESS) anyway
 *   3  stereo keeps the per-room portal scissor (bg.c bgScissorCurrentPlayerView)
 * While on, a count of draws per class is logged every 300 frames.
 */
extern "C" { int gevrZDebugMode = 0; }
static unsigned s_zDebugCount[6];
static void gevr_zdebug_poll(void)
{
    static unsigned n;
    if ((n++ % 120) != 0) return;
    FILE *f = fopen("/sdcard/Android/data/com.gevr.port/files/gevr_zdebug.txt", "r");
    int mode = 0;
    if (f) {
        if (fscanf(f, "%d", &mode) < 1) mode = 0;
        fclose(f);
    }
    if (mode != gevrZDebugMode) {
        gevrZDebugMode = mode;
        sysLogPrintf(LOG_NOTE, "zdebug: mode %d", mode);
    }
    if (gevrZDebugMode != 0 && (n % 600) == 0) {
        sysLogPrintf(LOG_NOTE, "zdebug: draws since last: notest %u nocmp %u decal %u xlu %u noupd %u opaque %u",
                     s_zDebugCount[0], s_zDebugCount[1], s_zDebugCount[2], s_zDebugCount[3], s_zDebugCount[4], s_zDebugCount[5]);
        memset(s_zDebugCount, 0, sizeof(s_zDebugCount));
    }
}

/* the class of the next draw under the depth state just set (gfx_opengl_set_depth_mode) */
static int s_zDebugClass = 5;
static const float s_zDebugTints[6][4] = {
    { 1.0f, 0.0f, 0.0f, 0.5f },   // no depth test
    { 1.0f, 0.0f, 1.0f, 0.5f },   // no Z_CMP
    { 0.0f, 1.0f, 0.0f, 0.5f },   // decal
    { 0.0f, 1.0f, 1.0f, 0.5f },   // blended
    { 1.0f, 1.0f, 0.0f, 0.5f },   // no Z_UPD
    { 0.0f, 0.0f, 0.0f, 0.0f },   // ordinary opaque
};
/* the replays (eye redraw, scope) write the tint per draw: the uniform is per program and the
 * game frame's last draw of that program would otherwise colour everything (it flickered) */
static void gevr_zdebug_apply(struct ShaderProgram* prg, bool world)
{
    if (prg == NULL || prg->zTintLocation < 0) return;
    glUniform4fv(prg->zTintLocation, 1, s_zDebugTints[(gevrZDebugMode == 1 && world) ? s_zDebugClass : 5]);
}

/*
 * Issue #72: a room's own display list is being drawn (bg.c VR_ROOM_DL_*).
 * Its decals (the striped walls, trims) take plain polygon offset, not the
 * stencil band: the band draws a decal only within 3 view units of what is
 * already in the depth buffer, and on a decal wall over a doorway of the
 * room behind that left a door-shaped hole with the far room showing
 * (user, Silo, 2026-10-02; Depot #72). The band stays for the game's own
 * decals (texSelect: bullet holes, which must not hang over an edge).
 */
bool gevrRoomDl;

static bool s_alphaArgs[2];
/*
 * Frigate's walls: an opaque room decal writes depth. The RDP's decal modes
 * don't, and where the wall's base mesh has an opening under the decal the
 * depth there stays far, so props and rooms drawn after it (behind the wall)
 * painted over the decal - a hole that came and went as the room order
 * changed. Not for blended decals or cutouts, whose clear pixels would hide
 * what is behind.
 */
static bool gevr_decal_writes_depth(void)
{
    return s_isDecal && gevrRoomDl && !s_alphaArgs[0];
}
/* gevrRoomDl only changes how decal batches draw (every use above and in the
 * draw path is under s_isDecal, which only changes after a flush), so a room
 * boundary needs to end the batch only when it is a decal batch. */
bool gfx_opengl_batch_is_decal(void)
{
    return s_isDecal;
}

/* Room decals sit a little off their walls: -2,-2 alone cut Frigate's
 * recessed fixtures. They draw pulled toward the eye by a reach
 * (gevr_room_decal_reach), in the draw itself so the eye pass and the 90 Hz
 * redraw match (a pull set only in draw_triangles, mode 2, made every
 * surface fight on the redraw).
 */
static float s_drawDecalPull;
static unsigned s_rdDraws, s_rdPulled, s_rdLarge, s_rdLogN;
static float s_rdMaxSpan;

static float gevr_vbo_span(const float* vbo, size_t ntris, size_t nfloats)
{
    if (vbo == NULL || ntris == 0 || nfloats < 3) return 0.0f;
    const size_t n = ntris * 3;
    float mn[3] = { vbo[0], vbo[1], vbo[2] };
    float mx[3] = { mn[0], mn[1], mn[2] };
    for (size_t i = 1; i < n; i++) {
        const float* p = vbo + i * nfloats;
        for (int k = 0; k < 3; k++) {
            if (p[k] < mn[k]) mn[k] = p[k];
            if (p[k] > mx[k]) mx[k] = p[k];
        }
    }
    const float dx = mx[0] - mn[0], dy = mx[1] - mn[1], dz = mx[2] - mn[2];
    return sqrtf(dx * dx + dy * dy + dz * dz);
}

/*
 * How far behind the surface in front a room decal still draws, in view
 * units at scale 1. A view unit is 1 / D_800364CC cm (5 cm on the Dam and
 * Surface), so the reach is held at one real size on every stage. Frigate's
 * decals sit a little off their walls: with none, the hull "06" flickered
 * and the port windows were cut. Too much draws a decal through whatever
 * lies just in front of it: at v0.4.5's 80 the Dam's start bridge showed the
 * shadows under it through its beams, and at 10 the ground's edge strip
 * still came through the end ramp. 3 held both (headset, 2026-10-04,
 * gevr_decal.txt mode 4, which sets it).
 */
extern "C" float D_800364CC;
static float gevr_room_decal_reach(void)
{
    const float reach = (s_decalMode == 4) ? s_decalA : 3.0f;
    return reach * D_800364CC;
}

static void gevr_room_decal_log(float span, float pull)
{
    if (s_decalMode == 0 && gevrZDebugMode == 0) return;
    s_rdDraws++;
    if (pull > 0.0f) s_rdPulled++;
    else if (span > 64.0f) s_rdLarge++;
    if (span > s_rdMaxSpan) s_rdMaxSpan = span;
    if ((++s_rdLogN % 240) != 0) return;
    sysLogPrintf(LOG_NOTE, "roomdecal: draws %u band %u large %u maxspan %.1f reach %.1f",
                 s_rdDraws, s_rdPulled, s_rdLarge, s_rdMaxSpan, pull);
    s_rdDraws = s_rdPulled = s_rdLarge = 0;
    s_rdMaxSpan = 0.0f;
}

static uint32_t frame_count;
/* performance pass: the per-draw uniform cache (draw_triangles) */
/*
 * Performance pass: a persistently mapped vertex buffer (GL_EXT_buffer_storage).
 * glBufferData per draw cost ~5 us on the Quest's driver - 2.6 ms a frame at
 * Frigate's hull (505 draws) - and appending with glBufferSubData stalled on
 * buffers the GPU was still reading (tried: 12 -> 35 ms). Mapped once, the
 * buffer takes each batch as a plain memcpy. It is split in three per-frame
 * segments; a fence closes each segment and is waited on only before that
 * segment is written again, two frames later.
 */
#define GEVR_PM_SEGMENTS 3
static uint8_t *s_pmPtr;
static GLsizeiptr s_pmSegSize, s_pmOff;
static int s_pmSeg = -1;
static uint32_t s_pmFrame = 0xffffffffu;
static GLsync s_pmFence[GEVR_PM_SEGMENTS];
static unsigned s_pmFallbacks;
static unsigned gevr_pm_poll(void *context, uint64_t timeout) {
    return glClientWaitSync(*(GLsync *)context, GL_SYNC_FLUSH_COMMANDS_BIT, timeout);
}
static void gevr_pm_finish(void *) { glFinish(); }
/* Fallbacks drain the GPU; log the 1st, 2nd, 4th, 8th... so a broken driver
 * cannot turn the warning itself into a per-frame cost. */
static void gevr_pm_fallback(const char *what, unsigned result) {
    s_pmFallbacks++;
    if (!(s_pmFallbacks & (s_pmFallbacks - 1)))
        sysLogPrintf(LOG_WARNING, "vertex-ring: %s (0x%x); completed outstanding GPU work (%u so far)",
                     what, result, s_pmFallbacks);
}
static void gevr_pm_next_segment(void)
{
    if (s_pmSeg >= 0) {
        if (s_pmFence[s_pmSeg]) glDeleteSync(s_pmFence[s_pmSeg]);
        s_pmFence[s_pmSeg] = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
        if (!s_pmFence[s_pmSeg]) {
            glFinish();   /* nothing to wait on later: the segment must be free now */
            gevr_pm_fallback("fence creation failed", 0);
        }
    }
    s_pmSeg = (s_pmSeg + 1) % GEVR_PM_SEGMENTS;
    if (s_pmFence[s_pmSeg]) {
        const uint64_t timingStart = gevrFrameTimingNow();
        const unsigned result = gevrVertexAwaitOwnership(&s_pmFence[s_pmSeg], gevr_pm_poll, gevr_pm_finish);
        gevrFrameTimingAdd(GEVR_TIME_VERTEX_WAIT, timingStart);
        if (result != GEVR_FENCE_ALREADY && result != GEVR_FENCE_SATISFIED)
            gevr_pm_fallback("fence wait did not signal", result);
        glDeleteSync(s_pmFence[s_pmSeg]);
        s_pmFence[s_pmSeg] = 0;
    }
    s_pmOff = (GLsizeiptr)s_pmSeg * s_pmSegSize;
}
#include <time.h>
static bool s_uniCacheValid;

// Captures own drawing state, including nested captures on another quad.
struct HudCaptureState {
    GLint viewport[4], scissor[4];
    GLboolean scissorEnabled;
    bool hudSize, menu, forceFlat, flat;
};
static HudCaptureState s_captureStates[4];
static void gevr_capture_state_save(unsigned target) {
    HudCaptureState &state = s_captureStates[target];
    glGetIntegerv(GL_VIEWPORT, state.viewport);
    glGetIntegerv(GL_SCISSOR_BOX, state.scissor);
    state.scissorEnabled = glIsEnabled(GL_SCISSOR_TEST);
    state.hudSize = VrIsTitleLegal;
    state.menu = vr_dl_is_pause_or_menu;
    state.forceFlat = gForceFlatShaderForMenu;
    state.flat = gVrFlatPass;
    s_uniCacheValid = false;
}
static void gevr_capture_state_restore(unsigned target) {
    const HudCaptureState &state = s_captureStates[target];
    glViewport(state.viewport[0],state.viewport[1],state.viewport[2],state.viewport[3]);
    glScissor(state.scissor[0],state.scissor[1],state.scissor[2],state.scissor[3]);
    if (state.scissorEnabled) glEnable(GL_SCISSOR_TEST); else glDisable(GL_SCISSOR_TEST);
    VrIsTitleLegal = state.hudSize;
    vr_dl_is_pause_or_menu = state.menu;
    gForceFlatShaderForMenu = state.forceFlat;
    gVrFlatPass = state.flat;
    s_uniCacheValid = false;
}

static float s_uniEye[8], s_uniBias;
static int s_uniFlat, s_uniMenu, s_uniHudSize;

/*
 * Issue #40: the state the fast3d frontend last set, so the sniper scope's pass
 * (gfx_vr_scope_render) can put it back after drawing with its own.
 */
static struct ShaderProgram* s_curPrg;
static bool s_depthArgs[4];
static uint16_t s_depthZmode;
static bool s_opaqueDepthWrite;
// ... and for the in-between frame's redraw (issue #53, gfx_vr_eye_replay)
static GLint s_curViewport[4], s_curScissor[4];
static bool s_eyeRec, s_eyeReady;   // recording the eye pass / a frame to redraw
static float s_eyeProj[16], s_eyeHeadP[2], s_eyePresentationVP[16];
static int s_eyeHand = -1;
static bool s_eyePresent;
static std::vector<struct ShaderProgram*> s_eyePresentationPrograms;
static void gevr_eye_keep(GLint first, GLsizei count, bool lineMode);
static void gevr_issue_draw(GLint first, GLsizei count, bool decalZ, bool decalDepth, float decalPull, bool lineMode);
static void gevr_opaque_depth_after_blend(GLint first, GLsizei count);

static std::vector<Framebuffer> framebuffers;
static size_t current_framebuffer;
static float current_noise_scale;
static FilteringMode current_filter_mode = FILTER_LINEAR;
static bool current_textures_linear_filter[2] = { false, false };

#ifdef ANDROID // VR
static int gl_glsl_version = 130;
static char gl_glsl_version_str[16] = "130";
#else
static int gl_glsl_version = 330;
static char gl_glsl_version_str[16] = "330";
#endif

static GLenum gl_mirror_clamp = GL_MIRROR_CLAMP_TO_EDGE;
static bool gl_es = false;
static bool gl_core_profile = false;




//----------------------------------------------VR

static bool gfx_opengl_is_multiview(void) {
    return use_multiview;
}


static void gfx_opengl_set_eye_offsets(float left_ipd, float left_asym_x, float left_hud, float left_asym_y,
                                       float right_ipd, float right_asym_x, float right_hud, float right_asym_y) {
    // Left eye (gl_ViewID_OVR == 0)
    s_eye_offsets[0] = left_ipd;     // vec4.x: 3D IPD
    s_eye_offsets[1] = left_asym_x;  // vec4.y: horizontal lens asymmetry
    s_eye_offsets[2] = left_hud;     // vec4.z: 2D offset (HUD)
    s_eye_offsets[3] = left_asym_y;  // vec4.w: vertical lens asymmetry

    // Right eye (gl_ViewID_OVR == 1)
    s_eye_offsets[4] = right_ipd;
    s_eye_offsets[5] = right_asym_x;
    s_eye_offsets[6] = right_hud;
    s_eye_offsets[7] = right_asym_y;

}


extern "C" void gfx_opengl_connect_multiview_fbo(GLuint fbo_id, uint32_t width, uint32_t height) {
    if (framebuffers.empty()) framebuffers.resize(1);
    Framebuffer& fb = framebuffers[0];
    fb.fbo = fbo_id;   // points to gmultiviewFBO
    fb.width = width;
    fb.height = height;
    fb.has_depth_buffer = true;
    fb.is_multiview = true;
    fb.invert_y = false;
    fb.msaa_level = 1;
    fb.clrbuf = fb.clrbuf_msaa = fb.rbo = 0;
    use_multiview = true;
}

void gfx_opengl_init_multiview() { // VR
    bool found = false;
    if (gl_es) {
        GLint numExts = 0;
        glGetIntegerv(GL_NUM_EXTENSIONS, &numExts);
        for (GLint i = 0; i < numExts; i++) {
            const char* e = (const char*)glGetStringi(GL_EXTENSIONS, i);
            if (e && (strcmp(e, "GL_OVR_multiview2") == 0 ||
                      strcmp(e, "GL_OVR_multiview") == 0)) {
                found = true;
                break;
            }
        }
    }
    else {
        const char* e = (const char*)glGetString(GL_EXTENSIONS);
        found = e && strstr(e, "GL_OVR_multiview");
    }

    if (!found) {
        return;
    }

    glFramebufferTextureMultiviewOVR =
            (PFNGLFRAMEBUFFERTEXTUREMULTIVIEWOVRPROC)
                    SDL_GL_GetProcAddress("glFramebufferTextureMultiviewOVR");
    if (!glFramebufferTextureMultiviewOVR)
        sysFatalError("Could not resolve glFramebufferTextureMultiviewOVR");

    // MSAA multiview
    pfnFramebufferTextureMultisampleMultiviewOVR =
            (PFNGLFRAMEBUFFERTEXTUREMULTISAMPLEMULTIVIEWOVRPROC)SDL_GL_GetProcAddress("glFramebufferTextureMultisampleMultiviewOVR");

    if (!pfnFramebufferTextureMultisampleMultiviewOVR) {
        sysLogPrintf(LOG_WARNING, "GL: glFramebufferTextureMultisampleMultiviewOVR not available, multiview MSAA disabled");
    }

    use_multiview = true;
}

static void mv_blit_init() {
    if (mv_blit_prog != 0) return;

    GLuint vs = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vs, 1, &mv_blit_vs_src, NULL);
    glCompileShader(vs);

    GLuint fs = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fs, 1, &mv_blit_fs_src, NULL);
    glCompileShader(fs);

    mv_blit_prog = glCreateProgram();
    glAttachShader(mv_blit_prog, vs);
    glAttachShader(mv_blit_prog, fs);
    glLinkProgram(mv_blit_prog);
    glDeleteShader(vs);
    glDeleteShader(fs);

    mv_blit_uTexLoc = glGetUniformLocation(mv_blit_prog, "uTex");
    mv_blit_uFlipYLoc = glGetUniformLocation(mv_blit_prog, "uFlipY");
    mv_blit_uRectLoc = glGetUniformLocation(mv_blit_prog, "uRect");

}


// VR layers mirror
#ifndef ANDROID // if PC

static GLuint menuOverlayProg = 0;
static GLint  menuOverlayMvpLoc = -1;
static GLint  menuOverlayTexLoc = -1;
static GLuint menuOverlayVao = 0, menuOverlayVbo = 0;

static const char* menuOverlayVsSrc =
        "#version 330 core\n"
        "layout(location=0) in vec3 aPos;\n"
        "layout(location=1) in vec2 aUV;\n"
        "uniform mat4 uMVP;\n"
        "out vec2 vUV;\n"
        "void main() {\n"
        "    gl_Position = uMVP * vec4(aPos, 1.0);\n"
        "    vUV = aUV;\n"
        "}\n";

static const char* menuOverlayFsSrc =
        "#version 330 core\n"
        "in vec2 vUV;\n"
        "out vec4 outColor;\n"
        "uniform sampler2D uTex;\n"
        "void main() {\n"
        "    outColor = texture(uTex, vUV);\n"
        "}\n";

static void menuOverlayInit() {
    if (menuOverlayProg != 0) return;

    GLuint vs = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vs, 1, &menuOverlayVsSrc, NULL);
    glCompileShader(vs);

    GLuint fs = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fs, 1, &menuOverlayFsSrc, NULL);
    glCompileShader(fs);

    menuOverlayProg = glCreateProgram();
    glAttachShader(menuOverlayProg, vs);
    glAttachShader(menuOverlayProg, fs);
    glLinkProgram(menuOverlayProg);
    glDeleteShader(vs);
    glDeleteShader(fs);

    menuOverlayMvpLoc = glGetUniformLocation(menuOverlayProg, "uMVP");
    menuOverlayTexLoc = glGetUniformLocation(menuOverlayProg, "uTex");

    float verts[] = {
            // pos              // uv
            -0.5f, -0.5f, 0.0f,  0.0f, 0.0f,
            0.5f, -0.5f, 0.0f,  1.0f, 0.0f,
            0.5f,  0.5f, 0.0f,  1.0f, 1.0f,
            -0.5f, -0.5f, 0.0f,  0.0f, 0.0f,
            0.5f,  0.5f, 0.0f,  1.0f, 1.0f,
            -0.5f,  0.5f, 0.0f,  0.0f, 1.0f,
    };

    glGenVertexArrays(1, &menuOverlayVao);
    glGenBuffers(1, &menuOverlayVbo);
    glBindVertexArray(menuOverlayVao);
    glBindBuffer(GL_ARRAY_BUFFER, menuOverlayVbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)(3 * sizeof(float)));
    glBindVertexArray(0);
}

extern "C" void gfx_opengl_draw_mirror_menu_overlay(GLuint menuTex, const float* mvp,
                                                    int vpX, int vpY, int vpW, int vpH) {
    if (menuTex == 0 || mvp == NULL) return;
    menuOverlayInit();

    GLboolean depthWasEnabled = glIsEnabled(GL_DEPTH_TEST);
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_SCISSOR_TEST);
    glViewport(vpX, vpY, vpW, vpH);
    glScissor(vpX, vpY, vpW, vpH);

    glUseProgram(menuOverlayProg);
    glUniformMatrix4fv(menuOverlayMvpLoc, 1, GL_FALSE, mvp);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, menuTex);
    glUniform1i(menuOverlayTexLoc, 0);

    glBindVertexArray(menuOverlayVao);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindVertexArray(0);

    glDisable(GL_SCISSOR_TEST);
    if (depthWasEnabled) glEnable(GL_DEPTH_TEST);
    glUseProgram(0);
}
#endif
//---------------------------------------------------------------


static int gfx_opengl_get_max_texture_size() {
    GLint max_texture_size;
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &max_texture_size);
    return max_texture_size;
}

static const char* gfx_opengl_get_name() {
    return "OpenGL";
}

static struct GfxClipParameters gfx_opengl_get_clip_parameters(void) {
    return { false, framebuffers[current_framebuffer].invert_y };
}

static void gfx_opengl_vertex_array_set_attribs(struct ShaderProgram* prg) {
    size_t num_floats = prg->num_floats;
    size_t pos = 0;

    for (int i = 0; i < prg->num_attribs; i++) {
        if (prg->attrib_locations[i] >= 0) {
            glEnableVertexAttribArray(prg->attrib_locations[i]);
            glVertexAttribPointer(prg->attrib_locations[i], prg->attrib_sizes[i], GL_FLOAT, GL_FALSE,
                                  num_floats * sizeof(float), (void*)(pos * sizeof(float)));
        }
        pos += prg->attrib_sizes[i];
    }
}

static void gfx_opengl_set_uniforms(struct ShaderProgram* prg) {
    if (prg->frame_count_location >= 0) {
        glUniform1i(prg->frame_count_location, frame_count);
    }
    if (prg->noise_scale_location >= 0) {
        glUniform1f(prg->noise_scale_location, current_noise_scale);
    }
    if (prg->three_point_filter_locations[0] >= 0) {
        glUniform1i(prg->three_point_filter_locations[0], current_textures_linear_filter[0]);
    }
    if (prg->three_point_filter_locations[1] >= 0) {
        glUniform1i(prg->three_point_filter_locations[1], current_textures_linear_filter[1]);
    }


    if (use_multiview) { // VR

        if (prg->eyeOffsetLeftLocation >= 0)
            glUniform4f(prg->eyeOffsetLeftLocation,
                        s_eye_offsets[0], s_eye_offsets[1], s_eye_offsets[2], s_eye_offsets[3]);

        if (prg->eyeOffsetRightLocation >= 0)
            glUniform4f(prg->eyeOffsetRightLocation,
                        s_eye_offsets[4], s_eye_offsets[5], s_eye_offsets[6], s_eye_offsets[7]);

        if (prg->isMenuLocation >= 0)
            glUniform1i(prg->isMenuLocation, vr_dl_is_pause_or_menu ? 1 : 0);

        if (prg->worldScaleLocation >= 0)
            glUniform1f(prg->worldScaleLocation, vr_world_scale);

        if (prg->IsTitleLegal >= 0)
            glUniform1i(prg->IsTitleLegal, VrIsTitleLegal ? 1 : 0);

        if (prg->vrFlatLocation >= 0)
            glUniform1i(prg->vrFlatLocation, gVrFlatPass ? 1 : 0);

        if (prg->TanHalfFovLeft >= 0)
            glUniform1f(prg->TanHalfFovLeft, g_eyeTanHalfFov[0]);

        if (prg->TanHalfFovRight >= 0)
            glUniform1f(prg->TanHalfFovRight, g_eyeTanHalfFov[1]);


    }

}

static void gfx_opengl_unload_shader(struct ShaderProgram* old_prg) {
    if (old_prg != NULL) {
        for (int i = 0; i < old_prg->num_attribs; i++) {
            if (old_prg->attrib_locations[i] >= 0) {
                glDisableVertexAttribArray(old_prg->attrib_locations[i]);
            }
        }
    }
}

/* VR_HUD_FULL_SIZE_BEGIN/END (issue #42): the shrink is a uniform, written when
 * a program is loaded, so the current program's copy is written here too. */
void gfx_opengl_vr_hud_full_size(bool full)
{
    // Nested size scopes restore their caller's choice rather than forcing
    // the ordinary HUD size when an inner timer ends.
    static bool previous[16];
    static unsigned depth;
    if (full) {
        if (depth < 16) previous[depth++] = VrIsTitleLegal;
        VrIsTitleLegal = false;
    } else {
        VrIsTitleLegal = depth ? previous[--depth] : true;
    }
    s_uniCacheValid = false;
    if (s_curPrg != NULL && s_curPrg->IsTitleLegal >= 0) {
        glUniform1i(s_curPrg->IsTitleLegal, VrIsTitleLegal ? 1 : 0);
    }
}

static void gfx_opengl_load_shader(struct ShaderProgram* new_prg) {
    GevrCpuSection timing(GEVR_TIME_SHADER_BIND);
    // if (!new_prg) return;
    s_curPrg = new_prg;
    glUseProgram(new_prg->opengl_program_id);
    gfx_opengl_vertex_array_set_attribs(new_prg);
    gfx_opengl_set_uniforms(new_prg);
    if (new_prg->is_mesh) {
        s_meshUniforms.erase(new_prg);   // gevr_mesh_bind writes them again
    }

    gCurEyeOffsetLeftLoc  = new_prg->eyeOffsetLeftLocation;
    gCurEyeOffsetRightLoc = new_prg->eyeOffsetRightLocation;
    gCurVrFlatLoc = new_prg->vrFlatLocation;
    gCurIsMenuLoc = new_prg->isMenuLocation;
    gCurDecalBiasLoc = new_prg->decalBiasLocation;
    s_uniCacheValid = false;   /* gfx_opengl_set_uniforms just wrote them */
}

static void append_str(char* buf, size_t* len, const char* str) {
    while (*str != '\0') {
        buf[(*len)++] = *str++;
    }
}

static void append_line(char* buf, size_t* len, const char* str) {
    while (*str != '\0') {
        buf[(*len)++] = *str++;
    }
    buf[(*len)++] = '\n';
}

#define RAND_NOISE "((random(vec3(floor(gl_FragCoord.xy * noise_scale), float(frame_count))) + 1.0) / 2.0)"

static const char* shader_item_to_str(uint32_t item, bool with_alpha, bool only_alpha, bool inputs_have_alpha,
                                      bool hint_single_element) {
    if (!only_alpha) {
        switch (item) {
            case SHADER_0:
                return with_alpha ? "vec4(0.0, 0.0, 0.0, 0.0)" : "vec3(0.0, 0.0, 0.0)";
            case SHADER_1:
                return with_alpha ? "vec4(1.0, 1.0, 1.0, 1.0)" : "vec3(1.0, 1.0, 1.0)";
            case SHADER_INPUT_1:
                return with_alpha || !inputs_have_alpha ? "vInput1" : "vInput1.rgb";
            case SHADER_INPUT_2:
                return with_alpha || !inputs_have_alpha ? "vInput2" : "vInput2.rgb";
            case SHADER_INPUT_3:
                return with_alpha || !inputs_have_alpha ? "vInput3" : "vInput3.rgb";
            case SHADER_INPUT_4:
                return with_alpha || !inputs_have_alpha ? "vInput4" : "vInput4.rgb";
            case SHADER_TEXEL0:
                return with_alpha ? "texVal0" : "texVal0.rgb";
            case SHADER_TEXEL0A:
                return hint_single_element ? "texVal0.a"
                                           : (with_alpha ? "vec4(texVal0.a, texVal0.a, texVal0.a, texVal0.a)"
                                                         : "vec3(texVal0.a, texVal0.a, texVal0.a)");
            case SHADER_TEXEL1A:
                return hint_single_element ? "texVal1.a"
                                           : (with_alpha ? "vec4(texVal1.a, texVal1.a, texVal1.a, texVal1.a)"
                                                         : "vec3(texVal1.a, texVal1.a, texVal1.a)");
            case SHADER_TEXEL1:
                return with_alpha ? "texVal1" : "texVal1.rgb";
            case SHADER_COMBINED:
                return with_alpha ? "texel" : "texel.rgb";
            case SHADER_NOISE:
                return with_alpha ? "vec4(" RAND_NOISE ", " RAND_NOISE ", " RAND_NOISE ", " RAND_NOISE ")"
                                  : "vec3(" RAND_NOISE ", " RAND_NOISE ", " RAND_NOISE ")";
        }
    }
    else {
        switch (item) {
            case SHADER_0:
                return "0.0";
            case SHADER_1:
                return "1.0";
            case SHADER_INPUT_1:
                return "vInput1.a";
            case SHADER_INPUT_2:
                return "vInput2.a";
            case SHADER_INPUT_3:
                return "vInput3.a";
            case SHADER_INPUT_4:
                return "vInput4.a";
            case SHADER_TEXEL0:
                return "texVal0.a";
            case SHADER_TEXEL0A:
                return "texVal0.a";
            case SHADER_TEXEL1A:
                return "texVal1.a";
            case SHADER_TEXEL1:
                return "texVal1.a";
            case SHADER_COMBINED:
                return "texel.a";
            case SHADER_NOISE:
                return RAND_NOISE;
        }
    }
    return "";
}

#undef RAND_NOISE

static void append_formula(char* buf, size_t* len, uint8_t c[2][4], bool do_single, bool do_multiply, bool do_mix,
                           bool with_alpha, bool only_alpha, bool opt_alpha) {
    if (do_single) {
        append_str(buf, len, shader_item_to_str(c[only_alpha][3], with_alpha, only_alpha, opt_alpha, false));
    }
    else if (do_multiply) {
        append_str(buf, len, shader_item_to_str(c[only_alpha][0], with_alpha, only_alpha, opt_alpha, false));
        append_str(buf, len, " * ");
        append_str(buf, len, shader_item_to_str(c[only_alpha][2], with_alpha, only_alpha, opt_alpha, true));
    }
    else if (do_mix) {
        append_str(buf, len, "mix(");
        append_str(buf, len, shader_item_to_str(c[only_alpha][1], with_alpha, only_alpha, opt_alpha, false));
        append_str(buf, len, ", ");
        append_str(buf, len, shader_item_to_str(c[only_alpha][0], with_alpha, only_alpha, opt_alpha, false));
        append_str(buf, len, ", ");
        append_str(buf, len, shader_item_to_str(c[only_alpha][2], with_alpha, only_alpha, opt_alpha, true));
        append_str(buf, len, ")");
    }
    else {
        append_str(buf, len, "(");
        append_str(buf, len, shader_item_to_str(c[only_alpha][0], with_alpha, only_alpha, opt_alpha, false));
        append_str(buf, len, " - ");
        append_str(buf, len, shader_item_to_str(c[only_alpha][1], with_alpha, only_alpha, opt_alpha, false));
        append_str(buf, len, ") * ");
        append_str(buf, len, shader_item_to_str(c[only_alpha][2], with_alpha, only_alpha, opt_alpha, true));
        append_str(buf, len, " + ");
        append_str(buf, len, shader_item_to_str(c[only_alpha][3], with_alpha, only_alpha, opt_alpha, false));
    }
}



// OpenXR supplies asymmetric projection centres.  The original renderer only
// accounted for the horizontal centre, so carry the vertical centre in the
// fourth eye-offset component and apply it to clip-space Y below.
const char* vr_shader = R"(
vec4 mvPos = aVtxPos;

// GoldenEye (issue #53): an in-between XR frame redraws the last game frame
// from the head's newer pose - back to that frame's camera space (x/P00,
// y/P11, -w), then through the head's move and the projection. Not the
// screen-locked draws (w = 1, and the HUD's marker values).
if (uReproj == 1) {
    float vr_wm1 = abs(aVtxPos.w - 1.0);
    if (vr_wm1 != 0.0 && vr_wm1 != 7.0 && vr_wm1 != 8.0 && vr_wm1 != 9.0) {
        mvPos = uReprojVP * vec4(aVtxPos.x / uHeadP.x, aVtxPos.y / uHeadP.y, -aVtxPos.w, 1.0);
    }
}

if (uVrFlat == 0) {

bool vr_is_Menu_or_HUD = (abs(mvPos.w - 1.0) == 0.0);
bool vr_is_Menu_or_crosshair_right = (abs(mvPos.w - 1.0) == 9.0);
bool vr_is_crosshair_left = (abs(mvPos.w - 1.0) == 7.0);
bool vr_is_Menu_blur = (abs(mvPos.w - 1.0) == 8.0);

vec4 eyeOffset = (gl_ViewID_OVR == 0u) ? uEyeOffsetLeft : uEyeOffsetRight;

// --------------------
// MENU / HUD IN PAUSE MODE (uIsMenu == 1)
// --------------------

if (uIsMenu == 1){
    mvPos.x -= eyeOffset.z * mvPos.w;
}

// --------------------
// GAME (uIsMenu == 0): HUD
// --------------------

if (uIsMenu == 0 && vr_is_Menu_or_HUD) {
    mvPos.w *= 0.90f;
    mvPos.x -= eyeOffset.z * mvPos.w;
    mvPos.y -= eyeOffset.w * mvPos.w;
}
else if (uIsMenu == 0 && vr_is_Menu_or_crosshair_right) {
    mvPos.w *= 0.90f;
    mvPos.x -= eyeOffset.z * mvPos.w;
    mvPos.y -= eyeOffset.w * mvPos.w;
}
else if (uIsMenu == 0) {
    mvPos.x -= eyeOffset.x + (eyeOffset.y * mvPos.w);
    mvPos.y -= eyeOffset.w * mvPos.w;
}

// --------------------
// "Legal" title splash
// --------------------
if (uIsTitleLegal == 1 && vr_is_Menu_or_HUD) {
    mvPos.w = 1.5f;
}

} // uVrFlat == 0

gl_Position = mvPos;
)";





static struct ShaderProgram* gfx_opengl_create_and_load_new_shader(uint64_t shader_id0, uint32_t shader_id1) {
    GevrCpuSection timing(GEVR_TIME_SHADER_COMPILE);
    struct CCFeatures cc_features = { 0 };
    gfx_cc_get_features(shader_id0, shader_id1, &cc_features);

    // Shader variants used by menus can be substantially larger than gameplay
    // variants.  The VR prelude alone is over 3 KiB, so the former 4 KiB vertex
    // buffer could overflow while entering a menu and corrupt the native stack.
    char vs_buf[16384];
    char fs_buf[16384];
    size_t vs_len = 0;
    size_t fs_len = 0;
    size_t num_floats = 4;


    // Vertex shader

    vs_len += sprintf(vs_buf + vs_len, "#version %s\n", gl_glsl_version_str);
    if (s_buildMesh && s_meshClipDistance) {
        append_line(vs_buf, &vs_len, "#extension GL_EXT_clip_cull_distance : require");
    }


    if (use_multiview) { // VR
        append_line(vs_buf, &vs_len,
                    "#extension GL_OVR_multiview2 : require");
        append_line(vs_buf, &vs_len,
                    "layout(num_views = 2) in;");

        // --- New Menu uniforms ---
        append_line(vs_buf, &vs_len, "uniform int uIsMenu;");
        append_line(vs_buf, &vs_len, "uniform vec4 uEyeOffsetLeft;");
        append_line(vs_buf, &vs_len, "uniform vec4 uEyeOffsetRight;");
        append_line(vs_buf, &vs_len, "uniform float uWorldScale;");

        append_line(vs_buf, &vs_len, "uniform int uIsTitleLegal;");
        append_line(vs_buf, &vs_len, "uniform float uTanHalfFovLeft;");
        append_line(vs_buf, &vs_len, "uniform float uTanHalfFovRight;");
        append_line(vs_buf, &vs_len, "uniform int uVrFlat;");
        append_line(vs_buf, &vs_len, "uniform vec2 uDecalBias;");
        // GoldenEye (issue #40): the sniper scope's pass (gfx_vr_scope_render)
        append_line(vs_buf, &vs_len, "uniform int uScope;");
        append_line(vs_buf, &vs_len, "uniform mat4 uScopeVP;");
        append_line(vs_buf, &vs_len, "uniform vec2 uHeadP;");
        // GoldenEye (issue #53): the in-between frame's redraw (gfx_vr_eye_replay)
        append_line(vs_buf, &vs_len, "uniform int uReproj;");
        append_line(vs_buf, &vs_len, "uniform mat4 uReprojVP;");

    }


    if (gl_es) {
        append_line(vs_buf, &vs_len, "precision highp float;");
    }

    if (gl_glsl_version >= 130) {
        append_line(vs_buf, &vs_len, "#define INPUT in");
        append_line(vs_buf, &vs_len, "#define OUTPUT out");
    }
    else {
        append_line(vs_buf, &vs_len, "#define INPUT attribute");
        append_line(vs_buf, &vs_len, "#define OUTPUT varying");
    }

    if (s_buildMesh) {
        // GoldenEye: a static mesh's Vtx (gevr_mesh_gpu) and what gfx_sp_vertex and
        // gfx_sp_tri1 would make of it (gfx_pc.cpp gevr_mesh_params; DAB's
        // gfx_mesh_vs_main). Below, main() makes the usual inputs of them.
        append_line(vs_buf, &vs_len, "INPUT vec3 aMeshPos;");
        append_line(vs_buf, &vs_len, "INPUT vec2 aMeshST;");
        append_line(vs_buf, &vs_len, "INPUT vec4 aMeshColor;");
        append_line(vs_buf, &vs_len, "uniform mat4 uMeshMP;");
        append_line(vs_buf, &vs_len, "uniform vec4 uMeshTex[2]; uniform vec2 uMeshTexRotation[2];");
        append_line(vs_buf, &vs_len, "uniform mat4 uMeshInSel[7];");
        append_line(vs_buf, &vs_len, "uniform vec4 uMeshInConst[7];");
        append_line(vs_buf, &vs_len, "INPUT vec4 aSkinWeight;");
        append_line(vs_buf, &vs_len, "INPUT vec4 aSkinBone; INPUT vec4 aSkinTint; INPUT float aSkinUnlit; uniform int uMeshTinted;");
        for (int i = 0; i < 4; i++) {
            vs_len += sprintf(vs_buf + vs_len, "INPUT vec3 aSkinP%d;\nINPUT vec3 aSkinN%d;\n", i, i);
        }
        append_line(vs_buf, &vs_len, "layout(std140) uniform SkinPalette { vec4 uSkinRows[192]; };");
        append_line(vs_buf, &vs_len, "uniform uvec2 uSkinValid;");
        append_line(vs_buf, &vs_len, "uniform int uMeshLit, uMeshNumLights;");
        append_line(vs_buf, &vs_len, "uniform vec3 uMeshAmbient, uMeshLightDir[7], uMeshLightColor[7];");
        append_line(vs_buf, &vs_len, "uniform vec4 uMeshFog;");
        append_line(vs_buf, &vs_len, "uniform vec4 uMeshFogParams;");
        append_line(vs_buf, &vs_len, "void skinAdd(int b, float w, vec3 p, vec3 n, inout vec3 pos, inout vec3 norm, inout float total) {");
        append_line(vs_buf, &vs_len, "  if (w == 0.0 || (uSkinValid[b / 32] & (1u << uint(b % 32))) == 0u) return;");
        append_line(vs_buf, &vs_len, "  vec4 r0 = uSkinRows[b*3], r1 = uSkinRows[b*3+1], r2 = uSkinRows[b*3+2];");
        append_line(vs_buf, &vs_len, "  pos += w * vec3(dot(r0, vec4(p,1.0)), dot(r1, vec4(p,1.0)), dot(r2, vec4(p,1.0)));");
        append_line(vs_buf, &vs_len, "  norm += w * vec3(dot(r0.xyz,n), dot(r1.xyz,n), dot(r2.xyz,n)); total += w;");
        append_line(vs_buf, &vs_len, "}");
    } else {
        append_line(vs_buf, &vs_len, "INPUT vec4 aVtxPos;");
    }

    for (int i = 0; i < 2; i++) {
        if (cc_features.used_textures[i]) {
            if (!s_buildMesh) vs_len += sprintf(vs_buf + vs_len, "INPUT vec2 aTexCoord%d;\n", i);
            vs_len += sprintf(vs_buf + vs_len, "OUTPUT vec2 vTexCoord%d;\n", i);
            num_floats += 2;
        }
    }
    if (cc_features.opt_fog) {
        if (!s_buildMesh) append_line(vs_buf, &vs_len, "INPUT vec4 aFog;");
        append_line(vs_buf, &vs_len, "OUTPUT vec4 vFog;");
        num_floats += 4;
    }

    if (cc_features.opt_grayscale) {
        append_line(vs_buf, &vs_len, "INPUT vec4 aGrayscaleColor;");
        append_line(vs_buf, &vs_len, "OUTPUT vec4 vGrayscaleColor;");
        num_floats += 4;
    }

    for (int i = 0; i < cc_features.num_inputs; i++) {
        if (!s_buildMesh) vs_len += sprintf(vs_buf + vs_len, "INPUT vec%d aInput%d;\n", cc_features.opt_alpha ? 4 : 3,
                          i + 1);
        vs_len += sprintf(vs_buf + vs_len, "OUTPUT vec%d vInput%d;\n",
                          cc_features.opt_alpha ? 4 : 3, i + 1);
        num_floats += cc_features.opt_alpha ? 4 : 3;
    }

    append_line(vs_buf, &vs_len, "void main() {");
    if (s_buildMesh) {
        append_line(vs_buf, &vs_len, "    vec3 meshPos = aMeshPos, meshNormal = vec3(0.0), skinnedPos = vec3(0.0); float total = 0.0;");
        append_line(vs_buf, &vs_len, "    vec4 meshColor = aMeshColor;");
        for (int i = 0; i < 4; i++)
            vs_len += sprintf(vs_buf + vs_len, "    skinAdd(int(aSkinBone[%d]), aSkinWeight[%d], aSkinP%d, aSkinN%d, skinnedPos, meshNormal, total);\n", i, i, i, i);
        append_line(vs_buf, &vs_len, "    if (total > 0.0) {");
        append_line(vs_buf, &vs_len, "      vec3 p = skinnedPos / total;");
        append_line(vs_buf, &vs_len, "      meshPos = trunc(clamp(p + sign(p) * 0.5, -32768.0, 32767.0));");
        append_line(vs_buf, &vs_len, "      float len = length(meshNormal);");
        append_line(vs_buf, &vs_len, "      meshNormal = len > 1e-6 ? trunc(meshNormal * (127.0 / len)) : vec3(0.0);");
        append_line(vs_buf, &vs_len, "    } else { meshNormal = aMeshColor.xyz * 255.0; meshNormal -= step(vec3(128.0), meshNormal) * 256.0; }");
        append_line(vs_buf, &vs_len, "    if (total > 0.0 && uMeshLit == 0) meshColor.rgb = mod(meshNormal, 256.0) / 255.0;");
        append_line(vs_buf, &vs_len, "    if (uMeshLit != 0) {");
        append_line(vs_buf, &vs_len, "      vec3 rgb = uMeshAmbient;");
        append_line(vs_buf, &vs_len, "      for (int i = 0; i < uMeshNumLights; i++) rgb = trunc(rgb + max(0.0, dot(meshNormal, uMeshLightDir[i]) / 127.0) * uMeshLightColor[i]);");
        append_line(vs_buf, &vs_len, "      meshColor.rgb = min(rgb, vec3(255.0)) / 255.0;");
        append_line(vs_buf, &vs_len, "    }");
        append_line(vs_buf, &vs_len, "    if (uMeshTinted != 0) meshColor.rgb = aSkinUnlit > 0.0 || uMeshLit == 0 ? aSkinTint.rgb : meshColor.rgb * aSkinTint.rgb;");
        append_line(vs_buf, &vs_len, "    vec4 aVtxPos = uMeshMP * vec4(meshPos, 1.0);");
        if (cc_features.opt_fog) {
            append_line(vs_buf, &vs_len, "    float fw = abs(aVtxPos.w) < 0.001 ? 0.001 : aVtxPos.w;");
            append_line(vs_buf, &vs_len, "    float wi = fw < 0.0 ? 32767.0 : 1.0 / fw;");
            append_line(vs_buf, &vs_len, "    vec4 aFog = vec4(uMeshFog.rgb, uMeshFogParams.z != 0.0 ? clamp((uMeshFogParams.w != 0.0 ? aVtxPos.w : aVtxPos.z * wi) * uMeshFogParams.x + uMeshFogParams.y, 0.0, 255.0) / 255.0 : uMeshFog.a);");
        }
        if (s_meshClipDistance) {
            // gfx_sp_tri1 drops a triangle past the squashed far plane at all three
            // corners; depth clamp would draw it at the far plane instead (DAB)
            append_line(vs_buf, &vs_len, "    gl_ClipDistance[0] = (aVtxPos.w > 0.0 && aVtxPos.z * 0.3 > aVtxPos.w) ? -1.0 : 1.0e6;");
        }
        for (int i = 0; i < 2; i++) {
            if (cc_features.used_textures[i]) {
                vs_len += sprintf(vs_buf + vs_len, "    vec2 st%d = aMeshST * uMeshTex[%d].xy;\n    vec2 r%d = uMeshTexRotation[%d]; if (dot(r%d,r%d) == 0.0) r%d = vec2(1.0,0.0);\n    vec2 aTexCoord%d = vec2(r%d.x*st%d.x-r%d.y*st%d.y, r%d.y*st%d.x+r%d.x*st%d.y) + uMeshTex[%d].zw;\n", i,i,i,i,i,i,i,i,i,i,i,i,i,i,i,i,i);
            }
        }
        for (int i = 0; i < cc_features.num_inputs; i++) {
            vs_len += sprintf(vs_buf + vs_len, "    vec%d aInput%d = (uMeshInSel[%d] * meshColor + uMeshInConst[%d])%s;\n",
                              cc_features.opt_alpha ? 4 : 3, i + 1, i, i, cc_features.opt_alpha ? "" : ".xyz");
        }
    }
    for (int i = 0; i < 2; i++) {
        if (cc_features.used_textures[i]) {
            vs_len += sprintf(vs_buf + vs_len, "    vTexCoord%d = aTexCoord%d;\n", i, i);
        }
    }
    if (cc_features.opt_fog) {
        append_line(vs_buf, &vs_len, "    vFog = aFog;");
    }
    if (cc_features.opt_grayscale) {
        append_line(vs_buf, &vs_len, "    vGrayscaleColor = aGrayscaleColor;");
    }
    for (int i = 0; i < cc_features.num_inputs; i++) {
        vs_len += sprintf(vs_buf + vs_len, "    vInput%d = aInput%d;\n", i + 1, i + 1);
    }


    // VR
    if (use_multiview) {
        append_line(vs_buf, &vs_len, vr_shader);
        // GoldenEye decal band (gfx_opengl_draw_triangles): a view-space nudge
        append_line(vs_buf, &vs_len, "gl_Position.z += uDecalBias.x / max(gl_Position.w, max(uDecalBias.y, 0.0001));");
    }


    // The far reach (gfx_pc.h GEVR_FAR_DEPTH_SCALE), with or without depth
    // clamp: clamp then only catches what lies past 3.3x the far distance.
    append_line(vs_buf, &vs_len, "    gl_Position.z *= 0.3f;");
    if (use_multiview) {
        // The scope pass: back from the head's clip space to its camera space
        // (the head projection is symmetric, w = -z), then through the scope's
        // camera. It draws into a flat target, so the second view is dropped.
        append_line(vs_buf, &vs_len,
                    "if (uScope == 1) {"
                    " vec4 c = vec4(aVtxPos.x / uHeadP.x, aVtxPos.y / uHeadP.y, -aVtxPos.w, 1.0);"
                    " gl_Position = (gl_ViewID_OVR == 0u) ? uScopeVP * c : vec4(0.0, 0.0, 2.0, 1.0);"
                    " }");
    }
    append_line(vs_buf, &vs_len, "}");

    // Fragment shader

    fs_len += sprintf(fs_buf + fs_len, "#version %s\n", gl_glsl_version_str);

    if (gl_es) {
        append_line(fs_buf, &fs_len, "precision highp float;");
    }

    if (gl_glsl_version >= 130) {
        append_line(fs_buf, &fs_len, "#define INPUT in");
        append_line(fs_buf, &fs_len, "#define OUTPUT_COLOR outColor");
        append_line(fs_buf, &fs_len, "#define SAMPLE_TEX(tex, uv) texture(tex, uv)");
    }
    else {
        append_line(fs_buf, &fs_len, "#define INPUT varying");
        append_line(fs_buf, &fs_len, "#define OUTPUT_COLOR gl_FragColor");
        append_line(fs_buf, &fs_len, "#define SAMPLE_TEX(tex, uv) texture2D(tex, uv)");
    }

    // Reference approach to color wrapping as in GLideN64
    // Return the wrapped value of x in the interval [low, high)
    append_line(fs_buf, &fs_len, "#define WRAP(x, low, high) mod((x)-(low), (high)-(low)) + (low)");

    append_line(fs_buf, &fs_len,
                "#define TEX_OFFSET(tex, uv, texSize, off) SAMPLE_TEX(tex, uv - (off)/texSize)");

    // append_line(fs_buf, &fs_len, "precision mediump float;");
    for (int i = 0; i < 2; i++) {
        if (cc_features.used_textures[i]) {
            fs_len += sprintf(fs_buf + fs_len, "INPUT vec2 vTexCoord%d;\n", i);
        }
    }
    if (cc_features.opt_fog) {
        append_line(fs_buf, &fs_len, "INPUT vec4 vFog;");
    }
    if (cc_features.opt_grayscale) {
        append_line(fs_buf, &fs_len, "INPUT vec4 vGrayscaleColor;");
    }
    for (int i = 0; i < cc_features.num_inputs; i++) {
        fs_len += sprintf(fs_buf + fs_len, "INPUT vec%d vInput%d;\n", cc_features.opt_alpha ? 4 : 3,
                          i + 1);
    }

    if (cc_features.used_textures[0]) {
        append_line(fs_buf, &fs_len, "uniform sampler2D uTex0;");
        if (current_filter_mode == FILTER_THREE_POINT)
            append_line(fs_buf, &fs_len, "uniform int three_point_filter0;");
    }
    if (cc_features.used_textures[1]) {
        append_line(fs_buf, &fs_len, "uniform sampler2D uTex1;");
        if (current_filter_mode == FILTER_THREE_POINT)
            append_line(fs_buf, &fs_len, "uniform int three_point_filter1;");
    }

    append_line(fs_buf, &fs_len, "uniform int frame_count;");
    append_line(fs_buf, &fs_len, "uniform float noise_scale;");
    if (cc_features.opt_alpha) {
        append_line(fs_buf, &fs_len, "uniform int uOpaqueDepthPass;");
    }
    if (use_multiview) {
        // GoldenEye (issue #72): files/gevr_zdebug.txt mode 1 tints draws by depth state
        append_line(fs_buf, &fs_len, "uniform vec4 uZTint;");
    }

    append_line(fs_buf, &fs_len, "float random(in vec3 value) {");
    append_line(fs_buf, &fs_len,
                "    float random = dot(sin(value), vec3(12.9898, 78.233, 37.719));");
    append_line(fs_buf, &fs_len, "    return fract(sin(random) * 143758.5453);");
    append_line(fs_buf, &fs_len, "}");

    if (current_filter_mode == FILTER_THREE_POINT) {
        append_line(fs_buf, &fs_len,
                    "vec4 filter3point(in sampler2D tex, in vec2 texCoord, in vec2 texSize) {");
        append_line(fs_buf, &fs_len, "    vec2 offset = fract(texCoord*texSize - vec2(0.5));");
        append_line(fs_buf, &fs_len, "    offset -= step(1.0, offset.x + offset.y);");
        append_line(fs_buf, &fs_len, "    vec4 c0 = TEX_OFFSET(tex, texCoord, texSize, offset);");
        append_line(fs_buf, &fs_len,
                    "    vec4 c1 = TEX_OFFSET(tex, texCoord, texSize, vec2(offset.x - sign(offset.x), offset.y));");
        append_line(fs_buf, &fs_len,
                    "    vec4 c2 = TEX_OFFSET(tex, texCoord, texSize, vec2(offset.x, offset.y - sign(offset.y)));");
        append_line(fs_buf, &fs_len,
                    "    return c0 + abs(offset.x)*(c1-c0) + abs(offset.y)*(c2-c0);");
        append_line(fs_buf, &fs_len, "}");
    }

    if (cc_features.opt_blur) {
        // blur filter, used for menu backgrounds
        // used to be two for loops from 0 to 4, but apparently Intel drivers crashed when trying to unroll it
        // used to have a const weight array, but apparently drivers for the GT620 do not like const array initializers
        if (current_filter_mode == FILTER_THREE_POINT)
            append_line(fs_buf, &fs_len,
                        "lowp vec4 hookTexture2D(in sampler2D t, in vec2 uv, in vec2 texSize, in int three_point_filter) {");
        else
            append_line(fs_buf, &fs_len,
                        "lowp vec4 hookTexture2D(in sampler2D t, in vec2 uv, in vec2 texSize) {");

        append_line(fs_buf, &fs_len, "    lowp vec4 cw = vec4(0.0);");
        append_line(fs_buf, &fs_len, "    for (int i = 0; i < 16; ++i) {");
        append_line(fs_buf, &fs_len, "        vec2 xy = vec2(float(i & 3), float(i >> 2));");
        append_line(fs_buf, &fs_len, "        lowp float w = 0.009947 - length(xy) * 0.001;");
        append_line(fs_buf, &fs_len, "        vec2 scaled_uv = uv + (vec2(-1.5) + xy) / texSize;");

        if (current_filter_mode == FILTER_THREE_POINT)
            append_line(fs_buf, &fs_len,
                        "        lowp vec4 tex = mix(SAMPLE_TEX(t, scaled_uv), filter3point(t, scaled_uv, texSize), float(three_point_filter));");
        else
            append_line(fs_buf, &fs_len, "        lowp vec4 tex = SAMPLE_TEX(t, scaled_uv);");

        append_line(fs_buf, &fs_len, "        cw += vec4(tex.rgb * w, w);");
        append_line(fs_buf, &fs_len, "    }");
        append_line(fs_buf, &fs_len, "    return vec4(cw.rgb / cw.a, 1.0);");
        append_line(fs_buf, &fs_len, "}");
    }
    else {
        if (current_filter_mode == FILTER_THREE_POINT) {
            append_line(fs_buf, &fs_len,
                        "vec4 hookTexture2D(in sampler2D tex, in vec2 uv, in vec2 texSize, in int three_point_filter) {");
            append_line(fs_buf, &fs_len,
                        "    return mix(SAMPLE_TEX(tex, uv), filter3point(tex, uv, texSize), float(three_point_filter));");
            append_line(fs_buf, &fs_len, "}");
        }
        else {
            append_line(fs_buf, &fs_len,
                        "vec4 hookTexture2D(in sampler2D tex, in vec2 uv, in vec2 texSize) {");
            append_line(fs_buf, &fs_len, "    return SAMPLE_TEX(tex, uv);");
            append_line(fs_buf, &fs_len, "}");
        }
    }

    if (gl_glsl_version >= 130) {
        append_line(fs_buf, &fs_len, "out vec4 outColor;");
    }

    append_line(fs_buf, &fs_len, "void main() {");

    for (int i = 0; i < 2; i++) {
        if (cc_features.used_textures[i]) {
            bool s = cc_features.clamp[i][0], t = cc_features.clamp[i][1];

            fs_len += sprintf(fs_buf + fs_len,
                              "    vec2 texSize%d = vec2(textureSize(uTex%d, 0));\n", i, i);

            if (current_filter_mode == FILTER_THREE_POINT)
                fs_len += sprintf(fs_buf + fs_len, "    vec4 texVal%d = hookTexture2D(uTex%d, vTexCoord%d, texSize%d, three_point_filter%d);\n", i, i, i, i, i);
            else
                fs_len += sprintf(fs_buf + fs_len, "    vec4 texVal%d = hookTexture2D(uTex%d, vTexCoord%d, texSize%d);\n", i, i, i, i);
        }
    }

    append_line(fs_buf, &fs_len, cc_features.opt_alpha ? "    vec4 texel;" : "    vec3 texel;");
    for (int c = 0; c < (cc_features.opt_2cyc ? 2 : 1); c++) {
        append_str(fs_buf, &fs_len, "    texel = ");
        if (!cc_features.color_alpha_same[c] && cc_features.opt_alpha) {
            append_str(fs_buf, &fs_len, "vec4(");
            append_formula(fs_buf, &fs_len, cc_features.c[c], cc_features.do_single[c][0],
                           cc_features.do_multiply[c][0], cc_features.do_mix[c][0], false, false,
                           true);
            append_str(fs_buf, &fs_len, ", ");
            append_formula(fs_buf, &fs_len, cc_features.c[c], cc_features.do_single[c][1],
                           cc_features.do_multiply[c][1], cc_features.do_mix[c][1], true, true,
                           true);
            append_str(fs_buf, &fs_len, ")");
        }
        else {
            append_formula(fs_buf, &fs_len, cc_features.c[c], cc_features.do_single[c][0],
                           cc_features.do_multiply[c][0], cc_features.do_mix[c][0],
                           cc_features.opt_alpha, false,
                           cc_features.opt_alpha);
        }
        append_line(fs_buf, &fs_len, ";");

        if (c == 0) {
            append_line(fs_buf, &fs_len, "    texel = WRAP(texel, -1.01, 1.01);");
        }
    }

    append_line(fs_buf, &fs_len, "    texel = WRAP(texel, -0.51, 1.51);");
    append_line(fs_buf, &fs_len, "    texel = clamp(texel, 0.0, 1.0);");
    // TODO discard if alpha is 0?
    if (cc_features.opt_fog) {
        if (cc_features.opt_alpha) {
            append_line(fs_buf, &fs_len,
                        "    texel = vec4(mix(texel.rgb, vFog.rgb, vFog.a), texel.a);");
        }
        else {
            append_line(fs_buf, &fs_len, "    texel = mix(texel, vFog.rgb, vFog.a);");
        }
    }

    if (cc_features.opt_texture_edge && cc_features.opt_alpha) {
        append_line(fs_buf, &fs_len, "    if (texel.a > 0.19) texel.a = 1.0; else discard;");
    }

    if (cc_features.opt_alpha && cc_features.opt_noise) {
        append_line(fs_buf, &fs_len,
                    "    texel.a *= floor(clamp(random(vec3(floor(gl_FragCoord.xy * noise_scale), float(frame_count))) + "
                    "texel.a, 0.0, 1.0));");
    }

    if (cc_features.opt_grayscale) {
        append_line(fs_buf, &fs_len, "    float intensity = (texel.r + texel.g + texel.b) / 3.0;");
        append_line(fs_buf, &fs_len, "    vec3 new_texel = vGrayscaleColor.rgb * intensity;");
        append_line(fs_buf, &fs_len,
                    "    texel.rgb = mix(texel.rgb, new_texel, vGrayscaleColor.a);");
    }


    if (cc_features.opt_alpha) {
        if (cc_features.opt_alpha_threshold) {
            append_line(fs_buf, &fs_len, "    if (texel.a < 8.0 / 256.0) discard;");
        }
        if (cc_features.opt_invisible) {
            append_line(fs_buf, &fs_len, "    texel.a = 0.0;");
        }
        // Issue #71: the depth-only draw after a blended Z_UPD draw keeps
        // only the pixels that blending made opaque. Glass stays see-through.
        append_line(fs_buf, &fs_len, "    if (uOpaqueDepthPass != 0 && texel.a < 0.99) discard;");

        append_line(fs_buf, &fs_len, "    OUTPUT_COLOR = texel;");
    }
    else {
        append_line(fs_buf, &fs_len, "    OUTPUT_COLOR = vec4(texel, 1.0);");
    }
    if (use_multiview) {
        append_line(fs_buf, &fs_len, "    if (uZTint.a > 0.0) OUTPUT_COLOR = vec4(mix(OUTPUT_COLOR.rgb, uZTint.rgb, uZTint.a), OUTPUT_COLOR.a);");
    }

    append_line(fs_buf, &fs_len, "}");

    vs_buf[vs_len] = '\0';
    fs_buf[fs_len] = '\0';

    const GLchar* sources[2] = { vs_buf, fs_buf };
    const GLint lengths[2] = { (GLint)vs_len, (GLint)fs_len };
    GLint success;

    GLuint vertex_shader = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vertex_shader, 1, &sources[0], &lengths[0]);
    glCompileShader(vertex_shader);
    glGetShaderiv(vertex_shader, GL_COMPILE_STATUS, &success);
    if (!success) {
        GLint max_length = 1024;
        glGetShaderiv(vertex_shader, GL_INFO_LOG_LENGTH, &max_length);
        char error_log[1024];
        glGetShaderInfoLog(vertex_shader, max_length, &max_length, &error_log[0]);
        sysLogPrintf(LOG_ERROR, "Failed to compile this vertex shader (ID %llx, %x):\n%s",
                     shader_id0, shader_id1, vs_buf);
        if (s_buildMesh) {   // GoldenEye: the CPU draws what this mesh variant would have
            sysLogPrintf(LOG_ERROR, "%s", error_log);
            glDeleteShader(vertex_shader);
            return NULL;
        }
        sysFatalError("Vertex shader compilation failed:\n%s", error_log);
    }

    GLuint fragment_shader = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fragment_shader, 1, &sources[1], &lengths[1]);
    glCompileShader(fragment_shader);
    glGetShaderiv(fragment_shader, GL_COMPILE_STATUS, &success);
    if (!success) {
        GLint max_length = 1024;
        glGetShaderiv(fragment_shader, GL_INFO_LOG_LENGTH, &max_length);
        char error_log[1024];
        glGetShaderInfoLog(fragment_shader, max_length, &max_length, &error_log[0]);
        sysLogPrintf(LOG_ERROR, "Failed to compile this fragment shader (ID %llx, %x):\n%s",
                     shader_id0, shader_id1, fs_buf);
        sysFatalError("Fragment shader compilation failed:\n%s", error_log);
    }

    GLuint shader_program = glCreateProgram();
    glAttachShader(shader_program, vertex_shader);
    glAttachShader(shader_program, fragment_shader);
    if (s_buildMesh) {   // one vertex array layout for every mesh variant (gevr_mesh_gpu)
        glBindAttribLocation(shader_program, 0, "aMeshPos");
        glBindAttribLocation(shader_program, 1, "aMeshST");
        glBindAttribLocation(shader_program, 2, "aMeshColor");
        glBindAttribLocation(shader_program, 3, "aSkinWeight");
        glBindAttribLocation(shader_program, 4, "aSkinBone");
        glBindAttribLocation(shader_program, 13, "aSkinTint");
        glBindAttribLocation(shader_program, 14, "aSkinUnlit");
        for (int i = 0; i < 4; i++) {
            char name[32];
            snprintf(name, sizeof(name), "aSkinP%d", i);
            glBindAttribLocation(shader_program, 5 + i, name);
            snprintf(name, sizeof(name), "aSkinN%d", i);
            glBindAttribLocation(shader_program, 9 + i, name);
        }
    }
    glLinkProgram(shader_program);

    glDetachShader(shader_program, vertex_shader);
    glDetachShader(shader_program, fragment_shader);
    glDeleteShader(vertex_shader);
    glDeleteShader(fragment_shader);

    if (s_buildMesh) {
        GLint linked = 0;
        glGetProgramiv(shader_program, GL_LINK_STATUS, &linked);
        if (!linked) {
            sysLogPrintf(LOG_ERROR, "mesh: the static-mesh variant of %llx/%x did not link",
                         (unsigned long long)shader_id0, shader_id1);
            glDeleteProgram(shader_program);
            return NULL;
        }
    }

    size_t cnt = 0;

    struct ShaderProgram* prg = &(s_buildMesh ? mesh_program_pool : shader_program_pool)[make_pair(shader_id0, shader_id1)];
    prg->attrib_locations[cnt] = glGetAttribLocation(shader_program, "aVtxPos");
    prg->attrib_sizes[cnt] = 4;
    ++cnt;

    for (int i = 0; i < 2; i++) {
        if (cc_features.used_textures[i]) {
            char name[32];
            sprintf(name, "aTexCoord%d", i);
            prg->attrib_locations[cnt] = glGetAttribLocation(shader_program, name);
            prg->attrib_sizes[cnt] = 2;
            ++cnt;
        }
    }

    if (cc_features.opt_fog) {
        prg->attrib_locations[cnt] = glGetAttribLocation(shader_program, "aFog");
        prg->attrib_sizes[cnt] = 4;
        ++cnt;
    }

    if (cc_features.opt_grayscale) {
        prg->attrib_locations[cnt] = glGetAttribLocation(shader_program, "aGrayscaleColor");
        prg->attrib_sizes[cnt] = 4;
        ++cnt;
    }

    for (int i = 0; i < cc_features.num_inputs; i++) {
        char name[16];
        sprintf(name, "aInput%d", i + 1);
        prg->attrib_locations[cnt] = glGetAttribLocation(shader_program, name);
        prg->attrib_sizes[cnt] = cc_features.opt_alpha ? 4 : 3;
        ++cnt;
    }

    prg->opengl_program_id = shader_program;
    prg->num_inputs = cc_features.num_inputs;
    prg->used_textures[0] = cc_features.used_textures[0];
    prg->used_textures[1] = cc_features.used_textures[1];
    prg->num_floats = num_floats;
    prg->num_attribs = s_buildMesh ? 0 : cnt;   // a mesh's attributes are its vertex array's
    prg->shader_id0 = shader_id0;
    prg->shader_id1 = shader_id1;
    prg->opt_fog = cc_features.opt_fog;
    prg->opt_grayscale = cc_features.opt_grayscale;
    prg->is_mesh = s_buildMesh;
    prg->meshMPLocation = s_buildMesh ? glGetUniformLocation(shader_program, "uMeshMP") : -1;
    for (int i = 0; i < GFX_MESH_MAX_INPUTS; i++) {
        char name[32];
        if (i < 2) {
            snprintf(name, sizeof(name), "uMeshTex[%d]", i);
            prg->meshTexLocation[i] = s_buildMesh ? glGetUniformLocation(shader_program, name) : -1;
            snprintf(name, sizeof(name), "uMeshTexRotation[%d]", i);
            prg->meshTexRotationLocation[i] = s_buildMesh ? glGetUniformLocation(shader_program, name) : -1;
        }
        snprintf(name, sizeof(name), "uMeshInSel[%d]", i);
        prg->meshInSelLocation[i] = s_buildMesh ? glGetUniformLocation(shader_program, name) : -1;
        snprintf(name, sizeof(name), "uMeshInConst[%d]", i);
        prg->meshInConstLocation[i] = s_buildMesh ? glGetUniformLocation(shader_program, name) : -1;
    }

    if (s_buildMesh) {
        GLuint block = glGetUniformBlockIndex(shader_program, "SkinPalette");
        if (block != GL_INVALID_INDEX) glUniformBlockBinding(shader_program, block, 0);
    }
    prg->meshLitLocation = s_buildMesh ? glGetUniformLocation(shader_program, "uMeshLit") : -1;
    prg->meshTintLocation = s_buildMesh ? glGetUniformLocation(shader_program, "uMeshTinted") : -1;
    prg->meshAmbientLocation = s_buildMesh ? glGetUniformLocation(shader_program, "uMeshAmbient") : -1;
    prg->meshLightDirLocation = s_buildMesh ? glGetUniformLocation(shader_program, "uMeshLightDir[0]") : -1;
    prg->meshLightColorLocation = s_buildMesh ? glGetUniformLocation(shader_program, "uMeshLightColor[0]") : -1;
    prg->meshNumLightsLocation = s_buildMesh ? glGetUniformLocation(shader_program, "uMeshNumLights") : -1;
    prg->meshFogLocation = s_buildMesh ? glGetUniformLocation(shader_program, "uMeshFog") : -1;
    prg->meshFogParamsLocation = s_buildMesh ? glGetUniformLocation(shader_program, "uMeshFogParams") : -1;
    prg->meshValidLocation = s_buildMesh ? glGetUniformLocation(shader_program, "uSkinValid") : -1;
    glUseProgram(shader_program);

    if (cc_features.used_textures[0]) {
        GLint sampler_location = glGetUniformLocation(shader_program, "uTex0");
        glUniform1i(sampler_location, 0);
    }
    if (cc_features.used_textures[1]) {
        GLint sampler_location = glGetUniformLocation(shader_program, "uTex1");
        glUniform1i(sampler_location, 1);
    }

    prg->frame_count_location = glGetUniformLocation(shader_program, "frame_count");
    prg->noise_scale_location = glGetUniformLocation(shader_program, "noise_scale");
    prg->opaque_depth_pass_location = cc_features.opt_alpha
            ? glGetUniformLocation(shader_program, "uOpaqueDepthPass") : -1;
    prg->three_point_filter_locations[0] = glGetUniformLocation(shader_program,
                                                                "three_point_filter0");
    prg->three_point_filter_locations[1] = glGetUniformLocation(shader_program,
                                                                "three_point_filter1");

    // VR...
    prg->vrFlatLocation = -1;
    prg->scopeLocation = prg->scopeVPLocation = prg->scopeHeadPLocation = -1;
    prg->reprojLocation = prg->reprojVPLocation = -1;
    prg->zTintLocation = -1;
    if (use_multiview) {
        prg->zTintLocation = glGetUniformLocation(shader_program, "uZTint");
        prg->reprojLocation = glGetUniformLocation(shader_program, "uReproj");
        prg->reprojVPLocation = glGetUniformLocation(shader_program, "uReprojVP");
        prg->scopeLocation = glGetUniformLocation(shader_program, "uScope");
        prg->scopeVPLocation = glGetUniformLocation(shader_program, "uScopeVP");
        prg->scopeHeadPLocation = glGetUniformLocation(shader_program, "uHeadP");
        prg->eyeOffsetLeftLocation = glGetUniformLocation(shader_program, "uEyeOffsetLeft");
        prg->eyeOffsetRightLocation = glGetUniformLocation(shader_program, "uEyeOffsetRight");
        prg->isMenuLocation = glGetUniformLocation(shader_program, "uIsMenu");
        prg->worldScaleLocation = glGetUniformLocation(shader_program, "uWorldScale");
        prg->IsTitleLegal = glGetUniformLocation(shader_program, "uIsTitleLegal");
        prg->TanHalfFovRight = glGetUniformLocation(shader_program, "uTanHalfFovRight");
        prg->TanHalfFovLeft = glGetUniformLocation(shader_program, "uTanHalfFovLeft");
        prg->vrFlatLocation = glGetUniformLocation(shader_program, "uVrFlat");
        prg->decalBiasLocation = glGetUniformLocation(shader_program, "uDecalBias");

    }

    gfx_opengl_load_shader(prg);

    return prg;
}

static struct ShaderProgram* gfx_opengl_lookup_shader(uint64_t shader_id0, uint32_t shader_id1) {
    auto it = shader_program_pool.find(make_pair(shader_id0, shader_id1));
    return it == shader_program_pool.end() ? nullptr : &it->second;
}

static void gfx_opengl_shader_get_info(struct ShaderProgram* prg, uint8_t* num_inputs, bool used_textures[2]) {
    *num_inputs = prg->num_inputs;
    used_textures[0] = prg->used_textures[0];
    used_textures[1] = prg->used_textures[1];
}

static void gfx_opengl_clear_shaders(void) {
    glUseProgram(0);
    for (auto& pair : shader_program_pool) {
        glDeleteProgram(pair.second.opengl_program_id);
    }
    shader_program_pool.clear();
    for (auto& pair : mesh_program_pool) {
        if (pair.second.opengl_program_id != 0) {
            glDeleteProgram(pair.second.opengl_program_id);
        }
    }
    mesh_program_pool.clear();
    s_meshUniforms.clear();

}

static GLuint gfx_opengl_new_texture(void) {
    GLuint ret;
    glGenTextures(1, &ret);
    return ret;
}

static void gfx_opengl_delete_texture(uint32_t texID) {
    glDeleteTextures(1, &texID);
}

/*
 * Issue #25: textures from a texture pack (gevr_texpack.cpp) carry mipmaps and
 * always filter trilinearly, whatever the N64 asked for: hi-res detail
 * minified without them shimmers in VR. Tracked per GL texture name, since
 * the texture cache reuses names for native uploads.
 */
static std::vector<uint8_t> s_texIsHd;
static GLuint s_boundTex[2];
static int s_activeTexUnit;
static GLfloat s_maxAniso = -1.0f;

static bool gevr_tex_is_hd(GLuint id) {
    return id < s_texIsHd.size() && s_texIsHd[id];
}

static void gevr_tex_mark_hd(GLuint id, bool hd) {
    if (id >= s_texIsHd.size()) {
        if (!hd) return;
        s_texIsHd.resize(id + 256, 0);
    }
    s_texIsHd[id] = hd;
}

static void gfx_opengl_select_texture(int tile, GLuint texture_id, bool linear_filter) {
    s_boundTex[tile] = texture_id;
    s_activeTexUnit = tile;

    glActiveTexture(GL_TEXTURE0 + tile);
    glBindTexture(GL_TEXTURE_2D, texture_id);

    current_textures_linear_filter[tile] = linear_filter;
}

static void gfx_opengl_upload_texture(const uint8_t* rgba32_buf, uint32_t width, uint32_t height) {
    GevrCpuSection timing(GEVR_TIME_TEXTURE_UPLOAD);
    gevrFrameTimingCounters(0,0,0,(uint64_t)width*height*4,0,0);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba32_buf);
    // a name that held a pack image keeps its old mip levels: sample level 0 only
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
    if (gevr_tex_is_hd(s_boundTex[s_activeTexUnit]) && s_maxAniso > 1.0f) {
        // ... and its anisotropy, which a native texture doesn't need (issue #52)
        glTexParameterf(GL_TEXTURE_2D, 0x84FE /* GL_TEXTURE_MAX_ANISOTROPY_EXT */, 1.0f);
    }
    gevr_tex_mark_hd(s_boundTex[s_activeTexUnit], false);
}

static void gfx_opengl_upload_texture_hd(const uint8_t* rgba32_buf, uint32_t width, uint32_t height) {
    GevrCpuSection timing(GEVR_TIME_TEXTURE_UPLOAD);
    gevrFrameTimingCounters(0,0,0,(uint64_t)width*height*4,0,0);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba32_buf);
    // before generating: the cache reuses names, and a native upload left this
    // one at MAX_LEVEL 0 - glGenerateMipmap stops there, and raising it after
    // left the texture incomplete (sampled as opaque black: blocks for text)
    GLint levels = 0;
    for (uint32_t d = width > height ? width : height; d > 1; d >>= 1) levels++;
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, levels);
    glGenerateMipmap(GL_TEXTURE_2D);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    if (s_maxAniso < 0.0f) {
        // GL_EXT_texture_filter_anisotropic, which Quest has; an error means it's absent
        s_maxAniso = 0.0f;
        while (glGetError() != GL_NO_ERROR) {
        }
        glGetFloatv(0x84FF /* GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT */, &s_maxAniso);
        if (glGetError() != GL_NO_ERROR) s_maxAniso = 0.0f;
    }
    if (s_maxAniso > 1.0f) {
        // 2x: 4x cost the pack's big textures a lot of GPU time on outdoor levels (issue #52)
        glTexParameterf(GL_TEXTURE_2D, 0x84FE /* GL_TEXTURE_MAX_ANISOTROPY_EXT */, s_maxAniso < 2.0f ? s_maxAniso : 2.0f);
    }
    gevr_tex_mark_hd(s_boundTex[s_activeTexUnit], true);
}

static uint32_t gfx_cm_to_opengl(uint32_t val) {
    switch (val) {
        case G_TX_NOMIRROR | G_TX_CLAMP:
            return GL_CLAMP_TO_EDGE;
        case G_TX_MIRROR | G_TX_WRAP:
            return GL_MIRRORED_REPEAT;
        case G_TX_MIRROR | G_TX_CLAMP:
            return gl_mirror_clamp;
        case G_TX_NOMIRROR | G_TX_WRAP:
            return GL_REPEAT;
    }
    return 0;
}

static void gfx_opengl_set_sampler_parameters(int tile, bool linear_filter, uint32_t cms, uint32_t cmt) {
    const GLint filter = linear_filter && (current_filter_mode == FILTER_LINEAR) ? GL_LINEAR : GL_NEAREST;
    glActiveTexture(GL_TEXTURE0 + tile);
    s_activeTexUnit = tile;
    if (gevr_tex_is_hd(s_boundTex[tile])) {
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    } else {
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
    }
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, gfx_cm_to_opengl(cms));
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, gfx_cm_to_opengl(cmt));
}

// GoldenEye: the two-pass decal band below, on since HANDOFF 90. It was off
// from 0.1.0 because every tuning cut or popped decals: the band's view-space
// distance was scaled by P[3][2] of a matrix that has the camera folded in
// (gfx_pc.cpp gfx_decal_proj_z), the wrong sign and ~27x too large. With the
// real depth term, 3 view units held the Dam's stripes and bullet holes on the
// headset (files/gevr_decal.txt still overrides for testing).
#define GEVR_DECAL_BAND 1

static void gfx_opengl_set_depth_mode(bool depth_test, bool depth_update, bool depth_compare, bool depth_source_prim, uint16_t zmode) {
    // issue #72 test switch (gevr_zdebug.txt): the class of what follows, and mode 2's forced compare
    s_zDebugClass = !depth_test ? 0 : !depth_compare ? 1 : zmode == ZMODE_DEC ? 2 : zmode == ZMODE_XLU ? 3 : !depth_update ? 4 : 5;
    if (gevrZDebugMode == 2 && depth_test && !depth_compare) {
        depth_compare = true;
    }
    s_depthArgs[0] = depth_test;
    s_depthArgs[1] = depth_update;
    s_depthArgs[2] = depth_compare;
    s_depthArgs[3] = depth_source_prim;
    s_depthZmode = zmode;
    // The custom XLU + Z_UPD mode asks for a second, depth-only draw of
    // fully opaque fragments. The color draw keeps ordinary XLU blending.
    s_opaqueDepthWrite = depth_test && depth_compare && depth_update && zmode == ZMODE_XLU;
    s_isDecal = depth_test && depth_compare && zmode == ZMODE_DEC;
    s_decalZ = ((GEVR_DECAL_BAND && (s_decalMode == 0 || s_decalMode == 4)) || s_decalMode == 3) && s_isDecal;
    if (depth_test) {
        glEnable(GL_DEPTH_TEST);
        glDepthMask(depth_update && !s_opaqueDepthWrite ? GL_TRUE : GL_FALSE);
        current_depth_mask = depth_update && !s_opaqueDepthWrite;

        if (depth_compare) {
            switch (zmode) {
                case ZMODE_INTER:
                    glDepthFunc(GL_LEQUAL);
                    glDisable(GL_POLYGON_OFFSET_FILL);
                    glPolygonOffset(0, 0);
                    break;

                case ZMODE_OPA:
                case ZMODE_XLU:
                    if (depth_source_prim) {
                        glDepthFunc(GL_LEQUAL);
                    }
                    else {
                        glDepthFunc(GL_LESS);
                    }
                    glDisable(GL_POLYGON_OFFSET_FILL);
                    glPolygonOffset(0, 0);
                    break;

                case ZMODE_DEC:
                    glDepthFunc(GL_LEQUAL);
                    glEnable(GL_POLYGON_OFFSET_FILL);
                    if (s_decalMode == 1) {
                        glPolygonOffset(s_decalA, s_decalB);
                    } else {
                        glPolygonOffset(-2, -2);
                    }
                    break;
            }
        }
        else {
            glDepthFunc(GL_ALWAYS);
            glDisable(GL_POLYGON_OFFSET_FILL);
            glPolygonOffset(0, 0);
        }
    }
    else {
        glDisable(GL_DEPTH_TEST);
    }
}

static void gfx_opengl_set_depth_range(float znear, float zfar) {
    if (glDepthRangef) {
        glDepthRangef(znear, zfar);
    }
    else {
        glDepthRange(znear, zfar);
    }
}

static void gfx_opengl_set_viewport(int x, int y, int width, int height) {
    s_curViewport[0] = x; s_curViewport[1] = y; s_curViewport[2] = width; s_curViewport[3] = height;
    glViewport(x, y, width, height);
}

static void gfx_opengl_set_scissor(int x, int y, int width, int height) {
    s_curScissor[0] = x; s_curScissor[1] = y; s_curScissor[2] = width; s_curScissor[3] = height;
    glScissor(x, y, width, height);
}

static void gfx_opengl_set_use_alpha(bool use_alpha, bool modulate) { // VR
    s_alphaArgs[0] = use_alpha;
    s_alphaArgs[1] = modulate;
    if (use_alpha) glEnable(GL_BLEND);
    else glDisable(GL_BLEND);

    if (modulate) {
        glBlendFuncSeparate(GL_DST_COLOR, GL_ZERO, GL_DST_COLOR, GL_ZERO);
    } else {
        // RGB: standard straight-alpha blending
        // Alpha: correct destination alpha accumulation (no src_alpha squared)
        glBlendFuncSeparate(
                GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA,   // RGB
                GL_ONE,       GL_ONE_MINUS_SRC_ALPHA);  // Alpha
    }
}




// ============================================================================
// VR - the sniper scope (issue #40)
// ============================================================================
/*
 * bondview2.c gevrScopeBegin puts a camera on the sniper's shot line and
 * lvlRender tags the world's draws (VR_SCOPE_REC_*). Each is kept here as it
 * is drawn - its program, its range of the frame's vertex ring, its textures,
 * depth and blend modes - and drawn a second time after the eye pass, into a
 * small target through the scope's camera (the vertex shader's uScope branch).
 * The ring's segment is not written again until two frames later, so the
 * vertices are still there. Draws the scope's narrow view misses are dropped
 * as they are kept. vr_openxr.cpp shows the target as the lens.
 */
#include "gevr_scope.h"   // bondview2.c: one scope per hand (GUNRIGHT 0, GUNLEFT 1)

struct GevrScopeDraw {
    struct ShaderProgram* prg;
    GLint first;
    GLsizei count;
    bool lineMode;
    GLuint tex[2];
    bool linear[2];
    bool depth[4];
    uint16_t zmode;
    bool alpha[2];
    bool decalDepth;
    float decalPull;
    uint32_t mesh; // 1 + scope mesh record, zero for the vertex ring
    uint8_t mask;   // which scopes see it: bit (1 << hand)
};

#define GEVR_SCOPE_RES 512
static std::vector<GevrScopeDraw> s_scopeDraws;
struct GevrScopeMesh { GLuint vao; GfxMeshDrawParams params; };
static std::vector<GevrScopeMesh> s_scopeMeshes;
static void gevr_mesh_uniforms(const ShaderProgram*, const GfxMeshDrawParams*);
static void gevr_mesh_clip(bool);
static void gevr_mesh_cull(int);
static bool s_scopeRec;
static bool s_scopeTaken[2], s_scopeDrawn[2];   // per hand: recording this frame / image ready
static uint8_t s_scopeOnlyMask;   // VR_SCOPE_ONLY: kept for that scope alone, not drawn to the eyes
static float s_scopeVP[2][16], s_scopeHeadP[2][2];
static GLuint s_scopeFbo[2], s_scopeTex[2], s_scopeDepth[2];

static bool gevr_scope_any_taken(void)
{
    return s_scopeTaken[0] || s_scopeTaken[1];
}

void gfx_vr_scope_record(bool on, bool invert_y)
{
    s_scopeRec = false;
    if (!on) {
        return;
    }
    s_scopeDraws.clear();
    s_scopeMeshes.clear();
    s_scopeTaken[0] = s_scopeTaken[1] = false;
    // needs the mapped ring: without it every draw overwrites the last one's vertices
    if (!gevrScopeOn || s_pmPtr == NULL || gVrFlatPass) {
        return;
    }
    for (int h = 0; h < 2; h++) {
        const GevrScopeState& st = gevrScope[h];
        if (!(gevrScopeOn & (1 << h)) || fabsf(st.headP[0]) < 1e-6f || fabsf(st.headP[1]) < 1e-6f) {
            continue;
        }
        memcpy(s_scopeVP[h], st.vp, sizeof(s_scopeVP[h]));
        s_scopeHeadP[h][0] = st.headP[0];
        s_scopeHeadP[h][1] = invert_y ? -st.headP[1] : st.headP[1];
        s_scopeTaken[h] = true;
    }
    s_scopeRec = gevr_scope_any_taken();
}

// Whether any of a batch can show through a hand's scope: not when all its
// vertices lie outside one side of the scope's view (each side is a plane in
// clip space, so this holds for the whole triangle), or all behind it.
static bool gevr_scope_sees(int hand, const float* v, size_t len, size_t tris)
{
    const size_t n = 3 * tris;
    if (n == 0) {
        return false;
    }
    const size_t stride = len / n;
    const float* M = s_scopeVP[hand];
    const float ix = 1.0f / s_scopeHeadP[hand][0], iy = 1.0f / s_scopeHeadP[hand][1];
    unsigned all = 0x1f;
    for (size_t i = 0; i < n; i++, v += stride) {
        const float cx = v[0] * ix, cy = v[1] * iy, cz = -v[3];
        const float x = M[0] * cx + M[4] * cy + M[8] * cz + M[12];
        const float y = M[1] * cx + M[5] * cy + M[9] * cz + M[13];
        const float w = M[3] * cx + M[7] * cy + M[11] * cz + M[15];
        unsigned out = 0;
        if (x < -w) out |= 1;
        if (x > w) out |= 2;
        if (y < -w) out |= 4;
        if (y > w) out |= 8;
        if (w <= 0.0f) out |= 16;
        all &= out;
        if (all == 0) {
            return true;
        }
    }
    return false;
}

static uint8_t gevr_scope_mesh_mask(const float* mp, const float* lo, const float* hi)
{
    uint8_t mask = 0;
    for (int h = 0; h < 2; h++) {
        if (!s_scopeTaken[h]) continue;
        float scopeMP[16];
        const float* vp = s_scopeVP[h];
        for (int c = 0; c < 4; c++) {
            const float view[4] = {mp[c*4] / s_scopeHeadP[h][0], mp[c*4+1] / s_scopeHeadP[h][1],
                                   -mp[c*4+3], c == 3 ? 1.0f : 0.0f};
            for (int r = 0; r < 4; r++)
                scopeMP[c*4+r] = vp[r]*view[0] + vp[4+r]*view[1] + vp[8+r]*view[2] + vp[12+r]*view[3];
        }
        if (gevrMeshBoundsVisible(lo, hi, scopeMP, 0, 0, 0, 0)) mask |= 1 << h;
    }
    return mask;
}

static bool gfx_opengl_scope_sees_mesh(const float* mp, const float* lo, const float* hi)
{
    return s_scopeRec && gevr_scope_mesh_mask(mp, lo, hi) != 0;
}

// gunfire.c gunDrawSight: a hand's sight drawn for its scope alone, after the world (-1 ends)
void gfx_vr_scope_only(int hand)
{
    s_scopeOnlyMask = (hand == 0 || hand == 1) && s_scopeTaken[hand] ? (uint8_t)(1 << hand) : 0;
}

static GevrScopeDraw gevr_scope_draw_state(uint8_t mask, GLint first, GLsizei count)
{
    GevrScopeDraw d = {};
    d.mask = mask;
    d.prg = s_curPrg;
    d.first = first;
    d.count = count;
    d.lineMode = get_debug_VisCVG_flag() != 0;
    for (int t = 0; t < 2; t++) {
        d.tex[t] = s_boundTex[t];
        d.linear[t] = current_textures_linear_filter[t];
    }
    memcpy(d.depth, s_depthArgs, sizeof(d.depth));
    d.zmode = s_depthZmode;
    d.alpha[0] = s_alphaArgs[0];
    d.alpha[1] = s_alphaArgs[1];
    d.decalDepth = gevr_decal_writes_depth();
    d.decalPull = s_drawDecalPull;
    return d;
}

static void gevr_scope_keep_mesh(GLuint vao, GLint first, GLsizei count, const GfxMeshDrawParams* params)
{
    if (!s_scopeRec) return;
    const uint8_t mask = params->has_bounds ? gevr_scope_mesh_mask(params->mp, params->lo, params->hi)
                                           : (s_scopeTaken[0] ? 1 : 0) | (s_scopeTaken[1] ? 2 : 0);
    if (!mask) return;
    GevrScopeDraw d = gevr_scope_draw_state(mask, first, count);
    s_scopeMeshes.push_back({vao, *params});
    d.mesh = (uint32_t)s_scopeMeshes.size();
    s_scopeDraws.push_back(d);
}

static void gevr_scope_keep(GLint first, const float* buf_vbo, size_t buf_vbo_len, size_t buf_vbo_num_tris)
{
    // the HUD, rects and menus (w == 1: the shader's HUD branch) are not the world
    if (s_curPrg == NULL || gForceFlatShaderForMenu || vr_dl_is_pause_or_menu || buf_vbo[3] == 1.0f) {
        return;
    }
    uint8_t mask = s_scopeOnlyMask;
    if (mask == 0) {
        for (int h = 0; h < 2; h++) {
            if (s_scopeTaken[h] && gevr_scope_sees(h, buf_vbo, buf_vbo_len, buf_vbo_num_tris)) {
                mask |= (uint8_t)(1 << h);
            }
        }
        if (mask == 0) {
            return;
        }
    }
    s_scopeDraws.push_back(gevr_scope_draw_state(mask, first, (GLsizei)(3 * buf_vbo_num_tris)));
}

/* a hand's scope target, once (gfx_vr_scope_prepare makes both ahead of use) */
static void gevr_scope_make_target(int hand)
{
    if (s_scopeFbo[hand]) {
        return;
    }
    GLint prevFbo = 0;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFbo);
    glGenTextures(1, &s_scopeTex[hand]);
    glBindTexture(GL_TEXTURE_2D, s_scopeTex[hand]);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, GEVR_SCOPE_RES, GEVR_SCOPE_RES, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glGenRenderbuffers(1, &s_scopeDepth[hand]);
    glBindRenderbuffer(GL_RENDERBUFFER, s_scopeDepth[hand]);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, GEVR_SCOPE_RES, GEVR_SCOPE_RES);
    glBindRenderbuffer(GL_RENDERBUFFER, 0);
    glGenFramebuffers(1, &s_scopeFbo[hand]);
    glBindFramebuffer(GL_FRAMEBUFFER, s_scopeFbo[hand]);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, s_scopeTex[hand], 0);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, s_scopeDepth[hand]);
    const GLenum st = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    sysLogPrintf(st == GL_FRAMEBUFFER_COMPLETE ? LOG_NOTE : LOG_WARNING,
                 "scope: hand %d %dx%d target %s (0x%x)", hand, GEVR_SCOPE_RES, GEVR_SCOPE_RES,
                 st == GL_FRAMEBUFFER_COMPLETE ? "ready" : "incomplete", st);
    glBindTexture(GL_TEXTURE_2D, s_boundTex[s_activeTexUnit]);
    glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)prevFbo);
}

/*
 * vr_openxr.cpp, on the first XR frame: the targets made then, not the moment
 * a rifle first comes out (with its model loading and the lens shader
 * compiling, that swap was a big stutter: user).
 */
void gfx_vr_scope_prepare(void)
{
    gevr_scope_make_target(0);
    gevr_scope_make_target(1);
}

// After the eye pass (gfx_pc.cpp gfx_run): the kept draws again, through each
// hand's scope. The GL state is saved and put back once around both passes.
void gfx_vr_scope_render(void)
{
    GevrProfileSection profile(GEVR_TIME_SCOPE);
    s_scopeRec = false;
    s_scopeOnlyMask = 0;
    if (!gevr_scope_any_taken()) {
        return;
    }
    bool taken[2] = { s_scopeTaken[0], s_scopeTaken[1] };
    s_scopeTaken[0] = s_scopeTaken[1] = false;

    // everything this changes, to put back for the rest of the frame
    GLint prevDraw = 0, prevRead = 0, prevVao = 0, vp[4], sc[4], depthFunc = GL_LEQUAL;
    GLint blendSrcRgb = GL_ONE, blendDstRgb = GL_ZERO, blendSrcA = GL_ONE, blendDstA = GL_ZERO;
    GLfloat clearCol[4], offFactor = 0.0f, offUnits = 0.0f;
    GLboolean depthMask = GL_TRUE;
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &prevDraw);
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &prevRead);
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &prevVao);
    glGetIntegerv(GL_VIEWPORT, vp);
    glGetIntegerv(GL_SCISSOR_BOX, sc);
    glGetIntegerv(GL_DEPTH_FUNC, &depthFunc);
    glGetIntegerv(GL_BLEND_SRC_RGB, &blendSrcRgb);
    glGetIntegerv(GL_BLEND_DST_RGB, &blendDstRgb);
    glGetIntegerv(GL_BLEND_SRC_ALPHA, &blendSrcA);
    glGetIntegerv(GL_BLEND_DST_ALPHA, &blendDstA);
    glGetFloatv(GL_COLOR_CLEAR_VALUE, clearCol);
    glGetFloatv(GL_POLYGON_OFFSET_FACTOR, &offFactor);
    glGetFloatv(GL_POLYGON_OFFSET_UNITS, &offUnits);
    glGetBooleanv(GL_DEPTH_WRITEMASK, &depthMask);
    const GLboolean scissorOn = glIsEnabled(GL_SCISSOR_TEST);
    const GLboolean depthOn = glIsEnabled(GL_DEPTH_TEST);
    const GLboolean blendOn = glIsEnabled(GL_BLEND);
    const GLboolean offsetOn = glIsEnabled(GL_POLYGON_OFFSET_FILL);
    struct ShaderProgram* const prevPrg = s_curPrg;
    bool savedDepth[4], savedAlpha[2];
    memcpy(savedDepth, s_depthArgs, sizeof(savedDepth));
    memcpy(savedAlpha, s_alphaArgs, sizeof(savedAlpha));
    const uint16_t savedZmode = s_depthZmode;
    const bool savedMask = current_depth_mask, savedIsDecal = s_isDecal, savedDecalZ = s_decalZ;
    const bool savedOpaqueDepthWrite = s_opaqueDepthWrite;

    if (opengl_vao) {
        glBindVertexArray(opengl_vao);
    }
    glBindBuffer(GL_ARRAY_BUFFER, opengl_vbo);

    static std::vector<struct ShaderProgram*> touched;
    touched.clear();
    struct ShaderProgram* bound = prevPrg;
    unsigned drawn[2] = { 0, 0 };
    for (int hand = 0; hand < 2; hand++) {
        if (!taken[hand]) {
            continue;
        }
        gevr_scope_make_target(hand);
        glBindFramebuffer(GL_FRAMEBUFFER, s_scopeFbo[hand]);
        glViewport(0, 0, GEVR_SCOPE_RES, GEVR_SCOPE_RES);
        glDisable(GL_SCISSOR_TEST);
        glDepthMask(GL_TRUE);
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);

        // each pass starts afresh (last == NULL): its first draw sets this scope's
        // camera, depth and blend state whatever the previous pass left
        const GevrScopeDraw* last = NULL;
        for (const GevrScopeDraw& d : s_scopeDraws) {
            if (!(d.mask & (1 << hand))) {
                continue;
            }
            drawn[hand]++;
            glBindVertexArray(d.mesh ? s_scopeMeshes[d.mesh - 1].vao : opengl_vao);
            s_drawElements = d.mesh != 0;
            gevr_mesh_clip(d.mesh != 0);
            if (!d.mesh) gevr_mesh_cull(0);
            if (last == NULL || d.prg != bound) {
                if (last != NULL || d.prg != bound) {
                    gfx_opengl_unload_shader(bound);
                }
                gfx_opengl_load_shader(d.prg);
                bound = d.prg;
                if (std::find(touched.begin(), touched.end(), d.prg) == touched.end()) {
                    touched.push_back(d.prg);
                }
                if (d.prg->scopeLocation >= 0) glUniform1i(d.prg->scopeLocation, 1);
                if (d.prg->scopeVPLocation >= 0) glUniformMatrix4fv(d.prg->scopeVPLocation, 1, GL_FALSE, s_scopeVP[hand]);
                if (d.prg->scopeHeadPLocation >= 0) glUniform2f(d.prg->scopeHeadPLocation, s_scopeHeadP[hand][0], s_scopeHeadP[hand][1]);
            }
            for (int t = 0; t < 2; t++) {
                if (d.prg->used_textures[t]) {
                    glActiveTexture(GL_TEXTURE0 + t);
                    glBindTexture(GL_TEXTURE_2D, d.tex[t]);
                    if (d.prg->three_point_filter_locations[t] >= 0) {
                        glUniform1i(d.prg->three_point_filter_locations[t], d.linear[t]);
                    }
                }
            }
            if (last == NULL || memcmp(d.depth, last->depth, sizeof(d.depth)) != 0 || d.zmode != last->zmode) {
                // a decal takes the plain polygon offset here, not the eye pass's stencil band
                gfx_opengl_set_depth_mode(d.depth[0], d.depth[1], d.depth[2], d.depth[3], d.zmode);
            }
            if (gevrZDebugMode == 1) gevr_zdebug_apply(d.prg, true);
            if (last == NULL || d.alpha[0] != last->alpha[0] || d.alpha[1] != last->alpha[1]) {
                gfx_opengl_set_use_alpha(d.alpha[0], d.alpha[1]);
            }
            if (d.mesh) {
                const GfxMeshDrawParams* params = &s_scopeMeshes[d.mesh - 1].params;
                for (GLuint a = 0; a < (params->skinned ? 15u : 3u); a++) glEnableVertexAttribArray(a);
                gevr_mesh_uniforms(d.prg, params);
                // Scope VP uses an upright target, undo the eye target winding flip.
                gevr_mesh_cull(params->invert_y ? -params->cull : params->cull);
            }
            gevr_issue_draw(d.first, d.count, false, d.decalDepth, d.decalPull, d.lineMode);
            last = &d;
        }
        s_scopeDrawn[hand] = true;

        // PORT probe: touching files/gevr_scopedump writes this frame's scope
        // image (the right hand's, or the left's alone) to files/gevr_scope.pam
        // (top row first) and logs how much of it was drawn, with the first
        // draws' first vertex through the scope camera.
        if (hand == 0 || !taken[0]) {
            static unsigned tick;
            const char* trig = "/sdcard/Android/data/com.gevr.port/files/gevr_scopedump";
            FILE* t = ((tick++ % 30) == 0) ? fopen(trig, "r") : NULL;
            if (t != NULL) {
                fclose(t);
                remove(trig);
                static std::vector<uint8_t> px;
                px.resize(GEVR_SCOPE_RES * GEVR_SCOPE_RES * 4);
                glReadPixels(0, 0, GEVR_SCOPE_RES, GEVR_SCOPE_RES, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
                unsigned lit = 0;
                for (size_t i = 0; i < px.size(); i += 4) {
                    if (px[i] > 8 || px[i + 1] > 8 || px[i + 2] > 8) lit++;
                }
                FILE* f = fopen("/sdcard/Android/data/com.gevr.port/files/gevr_scope.pam", "wb");
                if (f != NULL) {
                    fprintf(f, "P7\nWIDTH %d\nHEIGHT %d\nDEPTH 4\nMAXVAL 255\nTUPLTYPE RGB_ALPHA\nENDHDR\n",
                            GEVR_SCOPE_RES, GEVR_SCOPE_RES);
                    for (int y = GEVR_SCOPE_RES - 1; y >= 0; y--) {
                        fwrite(&px[(size_t)y * GEVR_SCOPE_RES * 4], 1, GEVR_SCOPE_RES * 4, f);
                    }
                    fclose(f);
                }
                sysLogPrintf(LOG_NOTE, "scope: hand %d dump, %u of %u pixels lit, %u draws, headP %.3f %.3f",
                             hand, lit, GEVR_SCOPE_RES * GEVR_SCOPE_RES, drawn[hand],
                             s_scopeHeadP[hand][0], s_scopeHeadP[hand][1]);
                unsigned k = 0;
                for (const GevrScopeDraw& d : s_scopeDraws) {
                    if (d.mesh || !(d.mask & (1 << hand)) || k >= 4) continue;
                    const float* v = (const float*)(s_pmPtr + (size_t)d.first * d.prg->num_floats * sizeof(float));
                    const float cx = v[0] / s_scopeHeadP[hand][0], cy = v[1] / s_scopeHeadP[hand][1], cz = -v[3];
                    const float* M = s_scopeVP[hand];
                    sysLogPrintf(LOG_NOTE, "scope: draw %u first %d count %d clip (%.2f %.2f %.2f %.2f) -> (%.2f %.2f %.2f %.2f)",
                                 k, d.first, d.count, v[0], v[1], v[2], v[3],
                                 M[0] * cx + M[4] * cy + M[8] * cz + M[12], M[1] * cx + M[5] * cy + M[9] * cz + M[13],
                                 M[2] * cx + M[6] * cy + M[10] * cz + M[14], M[3] * cx + M[7] * cy + M[11] * cz + M[15]);
                    k++;
                }
            }
        }
    }

    s_drawElements = false;
    gevr_mesh_clip(false);
    gevr_mesh_cull(0);
    glBindVertexArray(opengl_vao);
    // the programs back to the eye pass, and the state as fast3d left it
    for (struct ShaderProgram* p : touched) {
        if (p->scopeLocation >= 0) {
            glUseProgram(p->opengl_program_id);
            glUniform1i(p->scopeLocation, 0);
        }
    }
    if (bound != prevPrg) {
        gfx_opengl_unload_shader(bound);
    }
    if (prevPrg != NULL) {
        gfx_opengl_load_shader(prevPrg);
    } else {
        s_curPrg = NULL;
        glUseProgram(0);
    }
    for (int t = 0; t < 2; t++) {
        glActiveTexture(GL_TEXTURE0 + t);
        glBindTexture(GL_TEXTURE_2D, s_boundTex[t]);
    }
    glActiveTexture(GL_TEXTURE0 + s_activeTexUnit);
    memcpy(s_depthArgs, savedDepth, sizeof(savedDepth));
    memcpy(s_alphaArgs, savedAlpha, sizeof(savedAlpha));
    s_depthZmode = savedZmode;
    current_depth_mask = savedMask;
    s_opaqueDepthWrite = savedOpaqueDepthWrite;
    s_isDecal = savedIsDecal;
    s_decalZ = savedDecalZ;
    if (depthOn) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);
    glDepthMask(depthMask);
    glDepthFunc((GLenum)depthFunc);
    if (offsetOn) glEnable(GL_POLYGON_OFFSET_FILL); else glDisable(GL_POLYGON_OFFSET_FILL);
    glPolygonOffset(offFactor, offUnits);
    if (blendOn) glEnable(GL_BLEND); else glDisable(GL_BLEND);
    glBlendFuncSeparate((GLenum)blendSrcRgb, (GLenum)blendDstRgb, (GLenum)blendSrcA, (GLenum)blendDstA);
    glClearColor(clearCol[0], clearCol[1], clearCol[2], clearCol[3]);
    glBindVertexArray((GLuint)prevVao);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, (GLuint)prevDraw);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, (GLuint)prevRead);
    glViewport(vp[0], vp[1], vp[2], vp[3]);
    glScissor(sc[0], sc[1], sc[2], sc[3]);
    if (scissorOn) glEnable(GL_SCISSOR_TEST); else glDisable(GL_SCISSOR_TEST);
    s_uniCacheValid = false;

    static uint32_t logged;
    if ((logged++ % 300) == 0) {
        sysLogPrintf(LOG_NOTE, "scope: %u of the world's draws through the lens (right %u, left %u)",
                     (unsigned)s_scopeDraws.size(), drawn[0], drawn[1]);
    }
    s_scopeDraws.clear();
    s_scopeMeshes.clear();
}

// vr_openxr.cpp: this game frame's scope image for a hand, or 0 when there is none
GLuint gfx_vr_scope_texture(int hand)
{
    return (hand == 0 || hand == 1) && s_scopeDrawn[hand] ? s_scopeTex[hand] : 0;
}

static void gevr_eye_present_draw(void) {
    /* Presentation-only correction on fresh eye draws. Scope captures and
     * controller-held geometry retain their original camera-relative poses. */
    if (use_multiview && s_curPrg && s_curPrg->reprojLocation >= 0) {
        static int s_uniPresentation = -1;
        const bool present = s_eyePresent && s_eyeHand < 0 && !gForceFlatShaderForMenu && !gVrFlatPass;
        if (!s_uniCacheValid || s_uniPresentation != (int)present) {
            glUniform1i(s_curPrg->reprojLocation, present ? 1 : 0);
            if (present) {
                glUniformMatrix4fv(s_curPrg->reprojVPLocation, 1, GL_FALSE, s_eyePresentationVP);
                glUniform2f(s_curPrg->scopeHeadPLocation, s_eyeHeadP[0], s_eyeHeadP[1]);
                if (std::find(s_eyePresentationPrograms.begin(), s_eyePresentationPrograms.end(), s_curPrg) == s_eyePresentationPrograms.end())
                    s_eyePresentationPrograms.push_back(s_curPrg);
            }
            s_uniPresentation = present;
        }
    }
}

static void gfx_opengl_draw_triangles(float buf_vbo[], size_t buf_vbo_len, size_t buf_vbo_num_tris) {
    GevrCpuSection timing(GEVR_TIME_DRAW_BATCH);
    gevrFrameTimingCounters(1, 3*buf_vbo_num_tris, 0, 0, 0, 0);

    const bool lineMode = get_debug_VisCVG_flag() && !gForceFlatShaderForMenu && !gVrFlatPass &&
        !vr_dl_is_pause_or_menu && buf_vbo[3] != 1.0f;
    glBindBuffer(GL_ARRAY_BUFFER, opengl_vbo);   /* the menu overlay and hub setups leave theirs bound */
    GLint first = 0;
    if (s_pmPtr != NULL) {
        const GLsizeiptr bytes = (GLsizeiptr)(sizeof(float) * buf_vbo_len);
        const GLsizeiptr stride = bytes / (GLsizeiptr)(3 * buf_vbo_num_tris);
        if (s_pmFrame != frame_count) {
            s_pmFrame = frame_count;
            gevr_pm_next_segment();
        }
        GLsizeiptr off = (s_pmOff + stride - 1) / stride * stride;
        if (off + bytes > (GLsizeiptr)(s_pmSeg + 1) * s_pmSegSize) {
            gevr_pm_next_segment();   /* a frame too big for one segment: take the next */
            off = (s_pmOff + stride - 1) / stride * stride;
        }
        memcpy(s_pmPtr + off, buf_vbo, (size_t)bytes);
        s_pmOff = off + bytes;
        first = (GLint)(off / stride);
    } else {
        glBufferData(GL_ARRAY_BUFFER, sizeof(float) * buf_vbo_len, buf_vbo, GL_STREAM_DRAW);
    }
    s_drawDecalPull = 0.0f;
    if ((s_decalMode != 0 || gevrZDebugMode != 0) && gevrRoomDl && s_isDecal && s_curPrg != NULL) {
        const float span = gevr_vbo_span(buf_vbo, buf_vbo_num_tris, s_curPrg->num_floats);
        gevr_room_decal_log(span, gevr_room_decal_reach());
    }

    if (s_scopeRec || s_scopeOnlyMask) {
        gevr_scope_keep(first, buf_vbo, buf_vbo_len, buf_vbo_num_tris);   // issue #40
        if (s_scopeOnlyMask) {
            return;   // a scope's own sight: not for the eyes
        }
    }
    if (s_eyeRec) {
        gevr_eye_keep(first, (GLsizei)(3 * buf_vbo_num_tris), lineMode);   // issue #53
    }


    // A HUD capture draws into a single 2D texture, so it hides the right eye by
    // pushing that eye's geometry far away. That is correct while capturing --
    // but these uniforms are per shader program, and the real values are only
    // uploaded when a program is bound. Setting them here and never putting them
    // back left every later draw that reused the same program still hiding the
    // right eye, until some unrelated program switch happened to restore them.
    // That is how a full-screen effect could come out left-eye-only after a menu
    // had been opened, and why it looked intermittent. Always write the state
    // this draw actually wants.
    /* Performance pass: these are written only when they change for the bound
     * program (s_uniCache*, reset on every program bind and after the decal band). */
    if (use_multiview) {
        float eye[8];
        if (gForceFlatShaderForMenu) {
            // Correction : décalage à 1000.0f sur l'axe Z également
            const float flat[8] = { 0.0f, 0.0f, 0.0f, 0.0f, 1000.0f, 1000.0f, 1000.0f, 1000.0f };
            memcpy(eye, flat, sizeof(eye));
        } else {
            memcpy(eye, s_eye_offsets, sizeof(eye));
        }
        const int flatPass = gVrFlatPass ? 1 : 0;
        // GoldenEye: the stereo watch capture turns the menu flag on and off
        // between draws of the same program (bondview2.c bondviewRenderWatch).
        const int isMenu = vr_dl_is_pause_or_menu ? 1 : 0;
        const int hudSize = VrIsTitleLegal ? 1 : 0;
        if (!s_uniCacheValid || hudSize != s_uniHudSize) {
            if (s_curPrg && s_curPrg->IsTitleLegal >= 0) glUniform1i(s_curPrg->IsTitleLegal, hudSize);
            s_uniHudSize = hudSize;
        }
        if (!s_uniCacheValid || memcmp(eye, s_uniEye, sizeof(eye)) != 0) {
            if (gCurEyeOffsetLeftLoc  >= 0) glUniform4f(gCurEyeOffsetLeftLoc,  eye[0], eye[1], eye[2], eye[3]);
            if (gCurEyeOffsetRightLoc >= 0) glUniform4f(gCurEyeOffsetRightLoc, eye[4], eye[5], eye[6], eye[7]);
            memcpy(s_uniEye, eye, sizeof(eye));
        }
        if (!s_uniCacheValid || flatPass != s_uniFlat) {
            if (gCurVrFlatLoc >= 0) glUniform1i(gCurVrFlatLoc, flatPass);
            s_uniFlat = flatPass;
        }
        if (!s_uniCacheValid || isMenu != s_uniMenu) {
            if (gCurIsMenuLoc >= 0) glUniform1i(gCurIsMenuLoc, isMenu);
            s_uniMenu = isMenu;
        }
    }

    {
        const float bias = (s_isDecal && s_decalMode == 2) ? gfx_decal_proj_z * s_decalA : 0.0f;
        if (!s_uniCacheValid || bias != s_uniBias) {
            gevr_set_decal_bias(bias);
            s_uniBias = bias;
        }
    }
    if (use_multiview) {
        // issue #72 test switch: the world's draws tinted by depth class (gevr_zdebug_poll).
        // Always run, so the tint is written back to none when the switch goes off.
        static int s_uniTint = -1;
        const bool world = !gForceFlatShaderForMenu && !vr_dl_is_pause_or_menu && !gVrFlatPass && buf_vbo[3] != 1.0f;
        const int cls = world ? s_zDebugClass : 5;
        if (world && gevrZDebugMode != 0) {
            s_zDebugCount[cls]++;
        }
        const int shown = gevrZDebugMode == 1 ? cls : 5;
        if (!s_uniCacheValid || shown != s_uniTint) {
            if (s_curPrg && s_curPrg->zTintLocation >= 0) glUniform4fv(s_curPrg->zTintLocation, 1, s_zDebugTints[shown]);
            s_uniTint = shown;
        }
    }
    gevr_eye_present_draw();
    s_uniCacheValid = use_multiview;   /* without multiview the eye uniforms are never cached */

    gevr_issue_draw(first, (GLsizei)(3 * buf_vbo_num_tris), s_decalZ, gevrRoomDl && s_isDecal, 0.0f, lineMode);
}

/* the draw itself, with the decal band (the eye pass, and its redraw: issue #53) */
/* Only the optional online Line mode enters this path. Depth-filled faces
 * hide rear edges while the visible color is restricted to triangle edges.
 * Save the VAO's element binding and polygon offset so HUD draws remain filled. */
static void gevr_draw_world_lines(GLint first, GLsizei count) {
    static GLuint edges = 0;
    static std::vector<uint32_t> indices;
    if (!edges) glGenBuffers(1, &edges);
    indices.resize((size_t)count * 2);
    gevrTriangleEdgeIndices((uint32_t)first, (uint32_t)count, indices.data());
    GLint oldElements=0;
    glGetIntegerv(GL_ELEMENT_ARRAY_BUFFER_BINDING,&oldElements);
    if (current_depth_mask || s_opaqueDepthWrite) {
        GLfloat factor, units;
        GLboolean offset = glIsEnabled(GL_POLYGON_OFFSET_FILL);
        glGetFloatv(GL_POLYGON_OFFSET_FACTOR,&factor);glGetFloatv(GL_POLYGON_OFFSET_UNITS,&units);
        glEnable(GL_POLYGON_OFFSET_FILL);glPolygonOffset(1,1);
        glColorMask(GL_FALSE,GL_FALSE,GL_FALSE,GL_FALSE);glDepthMask(GL_TRUE);
        if (s_opaqueDepthWrite && s_curPrg && s_curPrg->opaque_depth_pass_location>=0)
            glUniform1i(s_curPrg->opaque_depth_pass_location,1);
        glDrawArrays(GL_TRIANGLES,first,count);
        if (s_opaqueDepthWrite && s_curPrg && s_curPrg->opaque_depth_pass_location>=0)
            glUniform1i(s_curPrg->opaque_depth_pass_location,0);
        glDepthMask(current_depth_mask ? GL_TRUE : GL_FALSE);glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);
        glPolygonOffset(factor,units);if (!offset) glDisable(GL_POLYGON_OFFSET_FILL);
    }
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,edges);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER,indices.size()*sizeof(uint32_t),indices.data(),GL_STREAM_DRAW);
    glDrawElements(GL_LINES,count*2,GL_UNSIGNED_INT,nullptr);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,(GLuint)oldElements);
}

/* the draw's triangles: from the vertex ring, or the bound static mesh's indices (gfx_opengl_draw_mesh) */
static void gevr_draw_range(GLint first, GLsizei count)
{
    if (s_drawElements) {
        glDrawElements(GL_TRIANGLES, count, GL_UNSIGNED_INT, (const void*)((uintptr_t)first * sizeof(uint32_t)));
    } else {
        glDrawArrays(GL_TRIANGLES, first, count);
    }
}

static void gevr_issue_draw(GLint first, GLsizei count, bool decalZ, bool decalDepth, float decalPull, bool lineMode)
{
    GevrCpuSection timing(GEVR_TIME_DRAW_ISSUE);
    if (lineMode) { gevr_draw_world_lines(first,count); return; }
    if (decalZ && decalDepth) {
        /*
         * A room's decals (bg.c VR_ROOM_DL_*): one pass at the game's depth
         * state (LEQUAL, -2,-2), pulled toward the eye by the reach. Nothing
         * cuts them in front, so a decal wall over a doorway still draws
         * (#72, #84: Depot, Silo, Bunker 2), and a fixture over an alcove
         * too. Overlapping decals pass alike, so the later primitive wins, as
         * on the RDP (Frigate's hull "06": shadow, then face).
         */
        gevr_set_decal_bias(gfx_decal_proj_z * gevr_room_decal_reach());
        gevr_draw_range(first, count);
        gevr_set_decal_bias(0.0f);
        s_uniCacheValid = false;   /* the pass wrote the bias */
        return;
    }
    if (decalZ) {
        /*
         * GoldenEye: the RDP's decal Z mode draws a pixel only where it lies
         * on the surface already in the depth buffer (within a small band).
         * Polygon offset toward the viewer alone passes everything in front,
         * so the part of a bullet-hole quad that hangs over an edge (the Dam
         * bunker's rim) was drawn against whatever lay behind - a torn,
         * angle-dependent sprite. Both sides of the band, with the stencil:
         *  A: mark pixels where the decal, pushed back, is at or behind the
         *     surface (so not hanging in front of a far background). The push
         *     is generous: GoldenEye rounds impact corners to whole units, so a
         *     hole sits up to half a unit off its wall, and a narrow band
         *     (units 8) cut most holes to slivers. Backgrounds behind an edge
         *     are metres away, still well outside it;
         *  B: draw where marked and, pulled forward, at or in front of it;
         *     every marked pixel is zeroed again (pass or fail).
         * The band is a fixed distance in view space (uDecalBias), not in
         * depth-buffer steps: GoldenEye's decals sit a little off their walls
         * (impact corners are rounded to whole units, level decals are
         * modelled apart), and at a grazing angle that offset is a large
         * depth difference while the headset's fine resolution keeps
         * polygon-offset slopes tiny - decals were cut along a diagonal as
         * you walked up to them. The N64's own decal test had coarse depth.
         */
        /* Mode 3 sets the band. Room decals take the pass above.
         *
         * The colour pass must NOT clear the marks as it draws. A decal never
         * writes depth, so on the RDP overlapping decal primitives resolve by
         * submission order: the later one wins. Zeroing the stencil on each
         * colour write made the first primitive in the draw win and reject
         * every later one at that pixel. The Frigate hull "06" is a black
         * shadow followed by a white face in the same batch, and it came out
         * black over white. Marks are cleared by a separate pass after the
         * colour pass. */
        const float bandD = (s_decalMode == 3 && s_decalA > 0.0f) ? s_decalA : 3.0f;
        GLboolean prevDepthMask = current_depth_mask ? GL_TRUE : GL_FALSE;
        glEnable(GL_STENCIL_TEST);
        glStencilMask(0xff);

        glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
        glDepthMask(GL_FALSE);
        glDepthFunc(GL_GEQUAL);
        glPolygonOffset(0.0f, 0.0f);
        gevr_set_decal_bias(-gfx_decal_proj_z * bandD);   // pushed away
        glStencilFunc(GL_ALWAYS, 1, 0xff);
        glStencilOp(GL_KEEP, GL_KEEP, GL_REPLACE);
        gevr_draw_range(first, count);

        glDepthFunc(GL_LEQUAL);
        gevr_set_decal_bias(gfx_decal_proj_z * bandD);   // pulled near
        glStencilFunc(GL_EQUAL, 1, 0xff);
        /* Colour: marks are kept, so every primitive of the draw can land on
         * a marked pixel and the last one submitted wins. */
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        glDepthMask(prevDepthMask);
        glPolygonOffset(-2.0f, -2.0f);
        glStencilOp(GL_KEEP, GL_KEEP, GL_KEEP);
        gevr_draw_range(first, count);

        /* Clear the marks: same push as the marking pass, so the same pixels
         * are covered; depth and colour untouched. */
        glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
        glDepthMask(GL_FALSE);
        glDepthFunc(GL_ALWAYS);
        glPolygonOffset(0.0f, 0.0f);
        gevr_set_decal_bias(-gfx_decal_proj_z * bandD);
        glStencilFunc(GL_ALWAYS, 0, 0xff);
        glStencilOp(GL_KEEP, GL_KEEP, GL_REPLACE);
        gevr_draw_range(first, count);
        glDepthFunc(GL_LEQUAL);
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        glDepthMask(prevDepthMask);
        glPolygonOffset(-2.0f, -2.0f);

        glStencilOp(GL_KEEP, GL_KEEP, GL_KEEP);
        glDisable(GL_STENCIL_TEST);
        gevr_set_decal_bias(0.0f);
        s_uniCacheValid = false;   /* the band wrote the bias */
        return;
    }

    if (decalDepth) {
        if (decalPull != 0.0f) gevr_set_decal_bias(gfx_decal_proj_z * decalPull);
        if (!current_depth_mask) glDepthMask(GL_TRUE);
        gevr_draw_range(first, count);
        if (!current_depth_mask) glDepthMask(GL_FALSE);
        if (decalPull != 0.0f) {
            gevr_set_decal_bias(0.0f);
            s_uniCacheValid = false;
        }
        return;
    }

    gevr_draw_range(first, count);
    gevr_opaque_depth_after_blend(first, count);

}

static void gevr_opaque_depth_after_blend(GLint first, GLsizei count)
{
    if (!s_opaqueDepthWrite || !s_curPrg || s_curPrg->opaque_depth_pass_location < 0) {
        return;
    }

    // Some model textures are cutouts in a translucent display list. Writing
    // depth during the color draw would also let clear and glass pixels hide
    // geometry. Only pixels whose final combiner alpha is opaque do so.
    glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
    glDepthMask(GL_TRUE);
    glUniform1i(s_curPrg->opaque_depth_pass_location, 1);
    gevr_draw_range(first, count);
    glUniform1i(s_curPrg->opaque_depth_pass_location, 0);
    glDepthMask(GL_FALSE);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
}

// ============================================================================
// VR - the in-between frame's redraw (issue #53)
// ============================================================================
/*
 * The game draws at 60 Hz and the display runs at 72 or 90. On the XR frames
 * between game frames the pump handed the compositor the last image again,
 * and it turns that to the head's new orientation only: the gun and hands,
 * leaning and anything near showed double - "reprojection ghosting" at a 12 Hz
 * beat at 72 Hz, 30 at 90 (issue #53). Perfect Dark VR draws a game frame for
 * every XR frame; GoldenEye's engine counts whole 60 Hz ticks and can't (and
 * gepc-ref parked running it faster). Instead the eye pass's draws are kept,
 * as the scope keeps the world's, and an in-between frame draws them again
 * from the head's new pose: the vertex shader's uReproj branch takes each
 * vertex back to the game frame's camera space and through the head's move
 * since (vr_openxr.cpp gevrVrRedrawDelta). Everything stays where it was in
 * the world; what moves on its own still moves at 60 Hz. (No depth range is
 * kept: fast3d never sets one here, and GLES has no glDepthRange for the
 * loader's fallback - calling it crashed.) The vertex ring's
 * segment isn't written again before the next game frame, whose fence then
 * covers these reads too.
 */
struct GevrEyeDraw {
    struct ShaderProgram* prg;
    GLint first;
    GLsizei count;
    bool lineMode;
    GLuint tex[2];
    bool linear[2];
    bool depth[4];
    uint16_t zmode;
    bool alpha[2];
    bool decalZ;
    bool decalDepth;
    float decalPull;
    bool isMenu;
    int8_t hand;       // the controller it follows (gunfire.c gevrHandTag), 2 the body, or -1: the world
    GLint viewport[4];
    GLint scissor[4];
    uint32_t mesh;     // a static mesh's draw: 1 + its s_eyeMeshes entry; 0 the vertex ring
};

static std::vector<GevrEyeDraw> s_eyeDraws;
struct GevrEyeMesh {
    GLuint vao;
    struct GfxMeshDrawParams params;
};
static std::vector<GevrEyeMesh> s_eyeMeshes;
static uint32_t s_eyeKeepMesh;   // gfx_opengl_draw_mesh: the draw gevr_eye_keep keeps
static void gevr_eye_proj_times(const float* delta, float M[16]);

// gfx_pc.cpp gfx_run, around the stereo eye pass
void gfx_vr_eye_record(bool on, const float* proj, bool invert_y, const float* presentation)
{
    if (on) {
        s_eyeDraws.clear();
        s_eyeMeshes.clear();
        s_eyeReady = false;
        s_eyeHand = -1;
        s_eyePresentationPrograms.clear();
        s_uniCacheValid = false;
        s_eyePresent = presentation && proj && !invert_y && fabsf(proj[0]) > 1e-6f && fabsf(proj[5]) > 1e-6f;
        // needs the mapped ring (each draw's vertices stay put) and the plain y
        s_eyeRec = proj != NULL && s_pmPtr != NULL && !invert_y
                && fabsf(proj[0]) > 1e-6f && fabsf(proj[5]) > 1e-6f;
        if (s_eyeRec || s_eyePresent) {
            memcpy(s_eyeProj, proj, sizeof(s_eyeProj));   // row-vector rows = GL columns
            s_eyeHeadP[0] = proj[0];
            s_eyeHeadP[1] = proj[5];
            if (s_eyePresent) gevr_eye_proj_times(presentation, s_eyePresentationVP);
        }
    } else {
        s_eyeReady = s_eyeRec && !s_eyeDraws.empty();
        s_eyeRec = false;
        s_eyePresent = false;
        for (struct ShaderProgram* p : s_eyePresentationPrograms) {
            glUseProgram(p->opengl_program_id);
            glUniform1i(p->reprojLocation, 0);
        }
        if (s_curPrg) glUseProgram(s_curPrg->opengl_program_id);
        s_uniCacheValid = false;
    }
}

// gfx_pc.cpp: VR_HAND_DRAW, what follows a controller, 2 the body (gevr_bodyslots.c), -1 the world again
void gfx_vr_eye_hand(int ctrl)
{
    s_eyeHand = (ctrl >= 0 && ctrl <= 2) ? ctrl : -1;
}

bool gfx_vr_eye_replay_ready(void)
{
    return s_eyeReady;
}

static void gevr_eye_keep(GLint first, GLsizei count, bool lineMode)
{
    // not the HUD captures, which draw into their own targets
    if (s_curPrg == NULL || gForceFlatShaderForMenu || gVrFlatPass) {
        return;
    }
    GevrEyeDraw d;
    d.prg = s_curPrg;
    d.first = first;
    d.count = count;
    d.lineMode = lineMode;
    for (int t = 0; t < 2; t++) {
        d.tex[t] = s_boundTex[t];
        d.linear[t] = current_textures_linear_filter[t];
    }
    memcpy(d.depth, s_depthArgs, sizeof(d.depth));
    d.zmode = s_depthZmode;
    d.alpha[0] = s_alphaArgs[0];
    d.alpha[1] = s_alphaArgs[1];
    d.decalZ = s_decalZ;   // a room's decals take the reach pass, the game's own the band
    d.decalDepth = gevrRoomDl && s_isDecal;
    d.decalPull = 0.0f;
    d.isMenu = vr_dl_is_pause_or_menu;
    d.hand = (int8_t)s_eyeHand;
    memcpy(d.viewport, s_curViewport, sizeof(d.viewport));
    memcpy(d.scissor, s_curScissor, sizeof(d.scissor));
    d.mesh = s_eyeKeepMesh;
    s_eyeDraws.push_back(d);
}

/* Keep the redraw's cached drawing boxes identical to the restored GL state. */
static void gevr_eye_capture_restored(const GLint vp[4], const GLint scissor[4])
{
    memcpy(s_curViewport, vp, sizeof(s_curViewport));
    memcpy(s_curScissor, scissor, sizeof(s_curScissor));
}

/* projection x a move in the game frame's camera space */
static void gevr_eye_proj_times(const float* delta, float M[16])
{
    for (int c = 0; c < 4; c++) {
        for (int r = 0; r < 4; r++) {
            float v = 0.0f;
            for (int k = 0; k < 4; k++) {
                v += s_eyeProj[k * 4 + r] * delta[c * 4 + k];
            }
            M[c * 4 + r] = v;
        }
    }
}

/*
 * Into the eye buffers already bound and cleared (gfx_pc.cpp
 * gfx_vr_redraw_frame). delta: the head's move from the game frame's pose,
 * in its camera space (view units), a GL matrix. hand0/hand1: each
 * controller's move in that space, or NULL, for what it holds: kept where it
 * was in the world, a gun lagged the hand every third frame at 90 Hz (user:
 * controller tracking not as smooth). body: what sits on the body (the body
 * slots' weapons), which goes with the head's position but not its turn.
 */
/* GoldenEye: static meshes (gfx_opengl_draw_mesh) - the batch's floats for the bound mesh variant */
static void gevr_mesh_uniforms(const struct ShaderProgram* prg, const struct GfxMeshDrawParams* p)
{
    const bool made = s_skinPaletteBuffer == 0;
    if (made) glGenBuffers(1, &s_skinPaletteBuffer);
    glBindBuffer(GL_UNIFORM_BUFFER, s_skinPaletteBuffer);
    // Static rooms need the block bound but never read it; do not stream a
    // zero palette for every room run and every redraw.
    if (made || p->skinned)
        glBufferData(GL_UNIFORM_BUFFER, sizeof(p->palette.rows), p->palette.rows, GL_STREAM_DRAW);
    glBindBufferBase(GL_UNIFORM_BUFFER, 0, s_skinPaletteBuffer);
    if (prg->meshValidLocation >= 0) glUniform2uiv(prg->meshValidLocation, 1, p->palette.valid);
    if (prg->meshLitLocation >= 0) glUniform1i(prg->meshLitLocation, p->lit);
    if (prg->meshTintLocation >= 0) glUniform1i(prg->meshTintLocation, p->authored_color);
    if (prg->meshAmbientLocation >= 0) glUniform3fv(prg->meshAmbientLocation, 1, p->ambient);
    if (prg->meshNumLightsLocation >= 0) glUniform1i(prg->meshNumLightsLocation, p->num_lights);
    if (p->num_lights) {
        if (prg->meshLightDirLocation >= 0) glUniform3fv(prg->meshLightDirLocation, p->num_lights, &p->light_dir[0][0]);
        if (prg->meshLightColorLocation >= 0) glUniform3fv(prg->meshLightColorLocation, p->num_lights, &p->light_color[0][0]);
    }
    if (prg->meshFogLocation >= 0) glUniform4fv(prg->meshFogLocation, 1, p->fog);
    if (prg->meshFogParamsLocation >= 0) glUniform4f(prg->meshFogParamsLocation, p->fog_mul, p->fog_offset, p->fog_enabled, p->fog_linear);
    if (prg->meshMPLocation >= 0) glUniformMatrix4fv(prg->meshMPLocation, 1, GL_FALSE, p->mp);
    for (int t = 0; t < 2; t++) {
        if (prg->meshTexLocation[t] >= 0) glUniform4fv(prg->meshTexLocation[t], 1, p->tex[t]);
        if (prg->meshTexRotationLocation[t] >= 0) {
            const float *r = p->tex_rotation[t];
            glUniform2f(prg->meshTexRotationLocation[t], r[0] == 0 && r[1] == 0 ? 1 : r[0], r[1]);
        }
    }
    for (int j = 0; j < p->num_inputs && j < GFX_MESH_MAX_INPUTS; j++) {
        if (prg->meshInSelLocation[j] >= 0) glUniformMatrix4fv(prg->meshInSelLocation[j], 1, GL_FALSE, p->in_sel[j]);
        if (prg->meshInConstLocation[j] >= 0) glUniform4fv(prg->meshInConstLocation[j], 1, p->in_const[j]);
    }
}

/*
 * gfx_sp_tri1's far reject while a mesh variant draws: a triangle past the
 * squashed far plane at all three corners is dropped on the CPU, and depth
 * clamp would draw it at the far plane. With clip distances the shader drops
 * it (DAB's way); without them depth clamp is off for the draw, so what lies
 * past that plane, 3.3 times the game's far distance, is clipped instead.
 */
static void gevr_mesh_clip(bool on)
{
    if (s_meshClipDistance) {
        if (on) glEnable(GL_CLIP_DISTANCE0); else glDisable(GL_CLIP_DISTANCE0);
    } else if (GLAD_GL_ARB_depth_clamp) {
        if (on) glDisable(GL_DEPTH_CLAMP); else glEnable(GL_DEPTH_CLAMP);
    }
}

static void gevr_mesh_cull(int mode)
{
    if (!mode) { glDisable(GL_CULL_FACE); return; }
    glEnable(GL_CULL_FACE);
    glFrontFace(mode < 0 ? GL_CW : GL_CCW);
    glCullFace(abs(mode) == 1 ? GL_BACK : GL_FRONT);
}

/* The redraw: a mesh draw binds its own vertex array before its program, so the
 * vertex ring's attributes are never set on it; back to the ring's after. */
static bool s_replayMesh;
static void gevr_eye_replay_done(void)
{
    if (s_replayMesh) {
        glBindVertexArray(opengl_vao);
        glBindBuffer(GL_ARRAY_BUFFER, opengl_vbo);
        gevr_mesh_clip(false);
        gevr_mesh_cull(0);
        s_replayMesh = false;
        s_drawElements = false;
    }
}
static void gevr_eye_replay_vao(const GevrEyeDraw& d)
{
    if (d.mesh == 0) {
        gevr_eye_replay_done();
        return;
    }
    glBindVertexArray(s_eyeMeshes[d.mesh - 1].vao);
    if (!s_replayMesh) gevr_mesh_clip(true);
    s_replayMesh = true;
    s_drawElements = true;
}
static void gevr_eye_replay_mesh(const GevrEyeDraw& d)
{
    if (d.mesh == 0) return;
    // unloading the program before may have disabled these on the bound array
    for (GLuint a = 0; a < (s_eyeMeshes[d.mesh - 1].params.skinned ? 15u : 3u); a++) glEnableVertexAttribArray(a);
    gevr_mesh_uniforms(d.prg, &s_eyeMeshes[d.mesh - 1].params);
    gevr_mesh_cull(s_eyeMeshes[d.mesh - 1].params.cull);
}

void gfx_vr_eye_replay(const float* delta, const float* hand0, const float* hand1, const float* body)
{
    if (!s_eyeReady) {
        return;
    }

    float Ms[4][16];   // [0] the world (the head's move), [1] left, [2] right controller, [3] the body
    gevr_eye_proj_times(delta, Ms[0]);
    gevr_eye_proj_times(hand0 != NULL ? hand0 : delta, Ms[1]);
    gevr_eye_proj_times(hand1 != NULL ? hand1 : delta, Ms[2]);
    gevr_eye_proj_times(body != NULL ? body : delta, Ms[3]);

    // everything this changes, to put back as fast3d left it
    GLint prevVao = 0, depthFunc = GL_LEQUAL;
    GLint blendSrcRgb = GL_ONE, blendDstRgb = GL_ZERO, blendSrcA = GL_ONE, blendDstA = GL_ZERO;
    GLfloat offFactor = 0.0f, offUnits = 0.0f;
    GLboolean depthMask = GL_TRUE;
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &prevVao);
    glGetIntegerv(GL_DEPTH_FUNC, &depthFunc);
    glGetIntegerv(GL_BLEND_SRC_RGB, &blendSrcRgb);
    glGetIntegerv(GL_BLEND_DST_RGB, &blendDstRgb);
    glGetIntegerv(GL_BLEND_SRC_ALPHA, &blendSrcA);
    glGetIntegerv(GL_BLEND_DST_ALPHA, &blendDstA);
    glGetFloatv(GL_POLYGON_OFFSET_FACTOR, &offFactor);
    glGetFloatv(GL_POLYGON_OFFSET_UNITS, &offUnits);
    glGetBooleanv(GL_DEPTH_WRITEMASK, &depthMask);
    const GLboolean depthOn = glIsEnabled(GL_DEPTH_TEST);
    const GLboolean blendOn = glIsEnabled(GL_BLEND);
    const GLboolean offsetOn = glIsEnabled(GL_POLYGON_OFFSET_FILL);
    struct ShaderProgram* const prevPrg = s_curPrg;
    bool savedDepth[4], savedAlpha[2];
    memcpy(savedDepth, s_depthArgs, sizeof(savedDepth));
    memcpy(savedAlpha, s_alphaArgs, sizeof(savedAlpha));
    const uint16_t savedZmode = s_depthZmode;
    const bool savedMask = current_depth_mask, savedIsDecal = s_isDecal, savedDecalZ = s_decalZ;
    const bool savedOpaqueDepthWrite = s_opaqueDepthWrite;
    GLint savedViewport[4], savedScissor[4];
    memcpy(savedViewport, s_curViewport, sizeof(savedViewport));
    memcpy(savedScissor, s_curScissor, sizeof(savedScissor));

    if (opengl_vao) {
        glBindVertexArray(opengl_vao);
    }
    glBindBuffer(GL_ARRAY_BUFFER, opengl_vbo);

    static std::vector<struct ShaderProgram*> touched;
    touched.clear();
    struct ShaderProgram* bound = prevPrg;
    const GevrEyeDraw* last = NULL;
    int lastMenu = -1;
    int lastMove = -2;
    GLuint replayTex[2] = {};
    bool haveTexture[2] = {};
    int replayFilter[2] = { -1, -1 };
    for (const GevrEyeDraw& d : s_eyeDraws) {
        gevr_eye_replay_vao(d);
        if (last == NULL || d.prg != bound) {
            if (last != NULL || d.prg != bound) {
                gfx_opengl_unload_shader(bound);
            }
            gfx_opengl_load_shader(d.prg);   // the eye offsets, as the eye pass had them
            bound = d.prg;
            lastMenu = -1;
            if (std::find(touched.begin(), touched.end(), d.prg) == touched.end()) {
                touched.push_back(d.prg);
            }
            if (d.prg->reprojLocation >= 0) glUniform1i(d.prg->reprojLocation, 1);
            if (d.prg->scopeHeadPLocation >= 0) glUniform2f(d.prg->scopeHeadPLocation, s_eyeHeadP[0], s_eyeHeadP[1]);
            lastMove = -2;
            // Filtering uniforms belong to the program; texture bindings do not.
            replayFilter[0] = replayFilter[1] = -1;
        }
        if (d.hand != lastMove) {
            if (d.prg->reprojVPLocation >= 0) glUniformMatrix4fv(d.prg->reprojVPLocation, 1, GL_FALSE, Ms[d.hand + 1]);
            lastMove = d.hand;
        }
        if ((int)d.isMenu != lastMenu) {
            if (gCurIsMenuLoc >= 0) glUniform1i(gCurIsMenuLoc, d.isMenu ? 1 : 0);
            lastMenu = d.isMenu ? 1 : 0;
        }
        for (int t = 0; t < 2; t++) {
            if (d.prg->used_textures[t]) {
                if (!haveTexture[t] || replayTex[t] != d.tex[t]) {
                    glActiveTexture(GL_TEXTURE0 + t);
                    glBindTexture(GL_TEXTURE_2D, d.tex[t]);
                    replayTex[t] = d.tex[t];
                    haveTexture[t] = true;
                }
                if (d.prg->three_point_filter_locations[t] >= 0 && replayFilter[t] != (int)d.linear[t]) {
                    glUniform1i(d.prg->three_point_filter_locations[t], d.linear[t]);
                    replayFilter[t] = (int)d.linear[t];
                }
            }
        }
        if (last == NULL || memcmp(d.depth, last->depth, sizeof(d.depth)) != 0 || d.zmode != last->zmode) {
            gfx_opengl_set_depth_mode(d.depth[0], d.depth[1], d.depth[2], d.depth[3], d.zmode);
        }
        if (gevrZDebugMode == 1) gevr_zdebug_apply(d.prg, !d.isMenu);
        if (last == NULL || d.alpha[0] != last->alpha[0] || d.alpha[1] != last->alpha[1]) {
            gfx_opengl_set_use_alpha(d.alpha[0], d.alpha[1]);
        }
        if (last == NULL || memcmp(d.viewport, last->viewport, sizeof(d.viewport)) != 0) {
            glViewport(d.viewport[0], d.viewport[1], d.viewport[2], d.viewport[3]);
        }
        if (last == NULL || memcmp(d.scissor, last->scissor, sizeof(d.scissor)) != 0) {
            glScissor(d.scissor[0], d.scissor[1], d.scissor[2], d.scissor[3]);
        }
        gevr_eye_replay_mesh(d);
        gevr_issue_draw(d.first, d.count, d.decalZ, d.decalDepth, d.decalPull, d.lineMode);
        last = &d;
    }
    gevr_eye_replay_done();

    // the programs back to the eye pass, and the state as fast3d left it
    for (struct ShaderProgram* p : touched) {
        if (p->reprojLocation >= 0) {
            glUseProgram(p->opengl_program_id);
            glUniform1i(p->reprojLocation, 0);
        }
    }
    if (bound != prevPrg) {
        gfx_opengl_unload_shader(bound);
    }
    if (prevPrg != NULL) {
        gfx_opengl_load_shader(prevPrg);
    } else {
        s_curPrg = NULL;
        glUseProgram(0);
    }
    for (int t = 0; t < 2; t++) {
        glActiveTexture(GL_TEXTURE0 + t);
        glBindTexture(GL_TEXTURE_2D, s_boundTex[t]);
    }
    glActiveTexture(GL_TEXTURE0 + s_activeTexUnit);
    memcpy(s_depthArgs, savedDepth, sizeof(savedDepth));
    memcpy(s_alphaArgs, savedAlpha, sizeof(savedAlpha));
    s_depthZmode = savedZmode;
    current_depth_mask = savedMask;
    s_opaqueDepthWrite = savedOpaqueDepthWrite;
    s_isDecal = savedIsDecal;
    s_decalZ = savedDecalZ;
    if (depthOn) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);
    glDepthMask(depthMask);
    glDepthFunc((GLenum)depthFunc);
    if (offsetOn) glEnable(GL_POLYGON_OFFSET_FILL); else glDisable(GL_POLYGON_OFFSET_FILL);
    glPolygonOffset(offFactor, offUnits);
    if (blendOn) glEnable(GL_BLEND); else glDisable(GL_BLEND);
    glBlendFuncSeparate((GLenum)blendSrcRgb, (GLenum)blendDstRgb, (GLenum)blendSrcA, (GLenum)blendDstA);
    gfx_opengl_set_viewport(savedViewport[0], savedViewport[1], savedViewport[2], savedViewport[3]);
    gfx_opengl_set_scissor(savedScissor[0], savedScissor[1], savedScissor[2], savedScissor[3]);
    glBindVertexArray((GLuint)prevVao);
    s_uniCacheValid = false;
}

/*
 * GoldenEye: static meshes (gevr_static_mesh.h; DAB's gfx_opengl_mesh_create
 * and gfx_opengl_mesh_draw, port/licenses/dabs-mod.txt). A mesh's Vtx go into
 * buffers once, with a vertex array of their own: position and s, t as shorts,
 * the colour as normalised bytes, where every mesh variant binds them.
 */
struct GevrMeshGpu {
    GLuint vao, vbo, ibo;
    uint32_t generation;
};

static bool gevr_mesh_supported(void)
{
    return use_multiview && gl_es && GLVersion.major >= 3 && opengl_vao != 0 && !s_meshBroken;
}

static GevrMeshGpu* gevr_mesh_gpu(void** slot, const void* vertices, uint32_t num_vertices,
                                  const uint32_t* indices, uint32_t num_indices, bool skinned)
{
    GevrMeshGpu* g = (GevrMeshGpu*)*slot;
    if (g != NULL && g->generation != s_glGeneration) {
        delete g;   // made in a context that is gone, and its names with it
        g = NULL;
        *slot = NULL;
    }
    if (g != NULL) {
        return g;
    }
    g = new GevrMeshGpu();
    g->generation = s_glGeneration;
    glGenVertexArrays(1, &g->vao);
    glGenBuffers(1, &g->vbo);
    glGenBuffers(1, &g->ibo);
    glBindVertexArray(g->vao);
    glBindBuffer(GL_ARRAY_BUFFER, g->vbo);
    const GLsizei stride = skinned ? sizeof(GevrSkinVertex) : 16;
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)num_vertices * stride, vertices, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_SHORT, GL_FALSE, stride, (const void*)0);            // Vtx ob
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_SHORT, GL_FALSE, stride, (const void*)8);            // Vtx tc
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 4, GL_UNSIGNED_BYTE, GL_TRUE, stride, (const void*)12);    // Vtx cn
    if (skinned) {
        glEnableVertexAttribArray(13);
        glVertexAttribPointer(13,4,GL_UNSIGNED_BYTE,GL_TRUE,stride,(const void*)offsetof(GevrSkinVertex,tint));
        glEnableVertexAttribArray(14);
        glVertexAttribPointer(14,1,GL_UNSIGNED_BYTE,GL_FALSE,stride,(const void*)(offsetof(GevrSkinVertex,tint)+4));
        glEnableVertexAttribArray(3);
        glVertexAttribPointer(3, 4, GL_UNSIGNED_BYTE, GL_FALSE, stride, (const void*)offsetof(GevrSkinVertex, weight));
        glEnableVertexAttribArray(4);
        glVertexAttribPointer(4, 4, GL_UNSIGNED_BYTE, GL_FALSE, stride, (const void*)offsetof(GevrSkinVertex, bone));
        for (int i = 0; i < 4; i++) {
            glEnableVertexAttribArray(5 + i);
            glVertexAttribPointer(5 + i, 3, GL_FLOAT, GL_FALSE, stride, (const void*)(offsetof(GevrSkinVertex, p) + i * 3 * sizeof(float)));
            glEnableVertexAttribArray(9 + i);
            glVertexAttribPointer(9 + i, 3, GL_FLOAT, GL_FALSE, stride, (const void*)(offsetof(GevrSkinVertex, n) + i * 3 * sizeof(float)));
        }
    }
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, g->ibo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, (GLsizeiptr)num_indices * sizeof(uint32_t), indices, GL_STATIC_DRAW);
    glBindVertexArray(opengl_vao);
    glBindBuffer(GL_ARRAY_BUFFER, opengl_vbo);
    *slot = g;
    return g;
}

static void gfx_opengl_delete_mesh(void* gpu)
{
    GevrMeshGpu* g = (GevrMeshGpu*)gpu;
    if (g == NULL) {
        return;
    }
    if (g->generation == s_glGeneration) {
        glDeleteVertexArrays(1, &g->vao);
        glDeleteBuffers(1, &g->vbo);
        glDeleteBuffers(1, &g->ibo);
        s_eyeReady = false;   // the redraw may still name its vertex array
    }
    delete g;
}

/* a mesh variant, its gfx_opengl_set_uniforms written only when they changed */
static void gevr_mesh_bind(struct ShaderProgram* mp)
{
    s_curPrg = mp;
    glUseProgram(mp->opengl_program_id);
    gCurEyeOffsetLeftLoc = mp->eyeOffsetLeftLocation;
    gCurEyeOffsetRightLoc = mp->eyeOffsetRightLocation;
    gCurVrFlatLoc = mp->vrFlatLocation;
    gCurIsMenuLoc = mp->isMenuLocation;
    gCurDecalBiasLoc = mp->decalBiasLocation;
    s_uniCacheValid = false;
    GevrMeshUniforms now;
    memset(&now, 0, sizeof(now));
    now.frame = (int)frame_count;
    now.filter[0] = current_textures_linear_filter[0];
    now.filter[1] = current_textures_linear_filter[1];
    now.menu = vr_dl_is_pause_or_menu ? 1 : 0;
    now.title = VrIsTitleLegal ? 1 : 0;
    now.flat = gVrFlatPass ? 1 : 0;
    now.noise = current_noise_scale;
    memcpy(now.eye, s_eye_offsets, sizeof(now.eye));
    now.scale = vr_world_scale;
    now.tan[0] = g_eyeTanHalfFov[0];
    now.tan[1] = g_eyeTanHalfFov[1];
    auto it = s_meshUniforms.find(mp);
    if (it == s_meshUniforms.end() || memcmp(&it->second, &now, sizeof(now)) != 0) {
        gfx_opengl_set_uniforms(mp);
        s_meshUniforms[mp] = now;
    }
}

/* the frontend's program again, with the uniforms it had (they are its own) */
static void gevr_mesh_rebind(struct ShaderProgram* base)
{
    s_curPrg = base;
    glUseProgram(base->opengl_program_id);
    gCurEyeOffsetLeftLoc = base->eyeOffsetLeftLocation;
    gCurEyeOffsetRightLoc = base->eyeOffsetRightLocation;
    gCurVrFlatLoc = base->vrFlatLocation;
    gCurIsMenuLoc = base->isMenuLocation;
    gCurDecalBiasLoc = base->decalBiasLocation;
    s_uniCacheValid = false;
}

static struct ShaderProgram* gevr_mesh_program(struct ShaderProgram* base)
{
    const pair<uint64_t, uint32_t> key = make_pair(base->shader_id0, base->shader_id1);
    auto it = mesh_program_pool.find(key);
    if (it != mesh_program_pool.end()) {
        return it->second.opengl_program_id != 0 ? &it->second : NULL;
    }
    s_buildMesh = true;
    struct ShaderProgram* mp = gfx_opengl_create_and_load_new_shader(base->shader_id0, base->shader_id1);
    s_buildMesh = false;
    if (mp == NULL) {
        mesh_program_pool[key].opengl_program_id = 0;   // not again
        s_meshBroken = true;
        sysLogPrintf(LOG_WARNING, "mesh: no static-mesh shader for %llx/%x; the CPU draws the meshes",
                     (unsigned long long)base->shader_id0, base->shader_id1);
    }
    return mp;
}

/*
 * One run of a static mesh with the state fast3d set for it (gfx_pc.cpp
 * gevr_mesh_draw_run): what gfx_opengl_draw_triangles does for a batch, from
 * the mesh's buffers through its program's mesh variant. false where only the
 * vertex ring will do: scope-only sights, HUD/menu captures and line mode.
 * World scope captures retain the mesh VAO and this draw's parameters.
 */
static bool gfx_opengl_draw_mesh(void** gpu, const void* vertices, uint32_t num_vertices, const uint32_t* indices,
                                 uint32_t num_indices, uint32_t first, uint32_t count,
                                 const struct GfxMeshDrawParams* p)
{
    struct ShaderProgram* base = s_curPrg;
    if (!gevr_mesh_supported() || base == NULL || base->is_mesh || base->opt_grayscale ||
        p->num_inputs != base->num_inputs || s_scopeOnlyMask || gForceFlatShaderForMenu ||
        vr_dl_is_pause_or_menu || get_debug_VisCVG_flag()) {
        return false;
    }
    GevrMeshGpu* g = gevr_mesh_gpu(gpu, vertices, num_vertices, indices, num_indices, p->skinned);
    struct ShaderProgram* mp = gevr_mesh_program(base);
    if (mp == NULL) {
        gevr_mesh_rebind(base);   // building it bound it
        return false;
    }
    GevrCpuSection timing(GEVR_TIME_DRAW_BATCH);
    gevrFrameTimingCounters(1, count, 0, 0, 0, 0);
    gevr_mesh_bind(mp);
    gevr_mesh_uniforms(mp, p);
    // gfx_opengl_draw_triangles' per-draw uniforms, for this world draw
    gevr_set_decal_bias((s_isDecal && s_decalMode == 2) ? gfx_decal_proj_z * s_decalA : 0.0f);
    if (gevrZDebugMode != 0) {
        s_zDebugCount[s_zDebugClass]++;
    }
    if (mp->zTintLocation >= 0) {
        glUniform4fv(mp->zTintLocation, 1, s_zDebugTints[gevrZDebugMode == 1 ? s_zDebugClass : 5]);
    }
    gevr_eye_present_draw();
    glBindVertexArray(g->vao);
    gevr_mesh_clip(true);
    gevr_mesh_cull(p->cull);
    gevr_scope_keep_mesh(g->vao, (GLint)first, (GLsizei)count, p);
    s_drawElements = true;
    gevr_issue_draw((GLint)first, (GLsizei)count, s_decalZ, gevrRoomDl && s_isDecal, 0.0f, false);
    s_drawElements = false;
    if (s_eyeRec) {
        s_eyeMeshes.push_back({ g->vao, *p });
        s_eyeKeepMesh = (uint32_t)s_eyeMeshes.size();
        gevr_eye_keep((GLint)first, (GLsizei)count, false);
        s_eyeKeepMesh = 0;
    }
    gevr_mesh_clip(false);
    gevr_mesh_cull(0);
    glBindVertexArray(opengl_vao);
    glBindBuffer(GL_ARRAY_BUFFER, opengl_vbo);
    gevr_mesh_rebind(base);
    return true;
}

typedef void (APIENTRY* DEBUGPROC)(GLenum source,
                                   GLenum type,
                                   GLuint id,
                                   GLenum severity,
                                   GLsizei length,
                                   const GLchar* message,
                                   const void* userParam);

static void APIENTRY gl_debug(GLenum source, GLenum type, GLuint id, GLenum severity, GLsizei length, const GLchar* msg, const void* p) {
    sysLogPrintf(LOG_WARNING, "GL: (%05x) %s", id, msg);
}

static void gfx_opengl_enable_debug(void) {
    if (GLAD_GL_KHR_debug) {
        glEnable(GL_DEBUG_OUTPUT);
    }
    if (glDebugMessageControl != NULL) {
        // enable everything except some specific spam messages
        const GLuint disable[] = {
                0x20061, /* "Framebuffer detailed info" */
                0x20071  /* "Buffer detailed info" */
        };
        glDebugMessageControl(GL_DONT_CARE, GL_DONT_CARE, GL_DONT_CARE, 0, NULL, GL_TRUE);
        glDebugMessageControl(GL_DEBUG_SOURCE_API, GL_DEBUG_TYPE_OTHER, GL_DONT_CARE, 2, disable, GL_FALSE);
    }
    if (glDebugMessageCallback != NULL) {
        glDebugMessageCallback(gl_debug, NULL);
    }
}

static bool gfx_opengl_supports_framebuffers(void) {
    if (GLVersion.major > 2) {
        // GL3.0+ supports everything we need, but we'll still check it for sanity
        return (glad_glFramebufferRenderbuffer && glad_glBlitFramebuffer && glad_glRenderbufferStorageMultisample);
    }
    if (GLAD_GL_ARB_framebuffer_object) {
        // some implementations might be missing these functions
        return (glad_glBlitFramebuffer && glad_glRenderbufferStorageMultisample);
    }
    if (GLAD_GL_EXT_framebuffer_object && GLAD_GL_EXT_framebuffer_blit && GLAD_GL_EXT_framebuffer_multisample) {
        // sanity check
        return (glad_glFramebufferRenderbuffer && glad_glBlitFramebuffer && glad_glRenderbufferStorageMultisample);
    }
    // nothing supported
    return false;
}

static bool gfx_opengl_supports_shaders(void) {
    if (GLVersion.major > 2) {
        // should support GLSL 1.30 or higher
        return true;
    }

    // check supported GLSL version
    const char* ver = (const char*)glGetString(GL_SHADING_LANGUAGE_VERSION);
    if (ver) {
        int maj = 0, min = 0;
        sscanf(ver, "%d.%d", &maj, &min);
        if (maj > 1 || (maj == 1 && min > 20)) {
            // above 1.20, it should be fine
            return true;
        }
    }

    // check for an extension that adds textureSize
    return GLAD_GL_EXT_gpu_shader4;
}

static void gfx_opengl_log_info(void) {
    const char* version = (const char*)glGetString(GL_VERSION);
    const char* vendor = (const char*)glGetString(GL_VENDOR);
    const char* renderer = (const char*)glGetString(GL_RENDERER);
    const char* glsl_version = (const char*)glGetString(GL_SHADING_LANGUAGE_VERSION);
    sysLogPrintf(LOG_NOTE, "GL: version: %s", version ? version : "unknown");
    sysLogPrintf(LOG_NOTE, "GL: vendor: %s", vendor ? vendor : "unknown");
    sysLogPrintf(LOG_NOTE, "GL: renderer: %s", renderer ? renderer : "unknown");
    sysLogPrintf(LOG_NOTE, "GL: GLSL version: %s", glsl_version ? glsl_version : "unknown");
    sysLogPrintf(LOG_NOTE, "GL: ARB_framebuffer_object: %s", gfx_opengl_supports_framebuffers() ? "yes" : "no");
    sysLogPrintf(LOG_NOTE, "GL: ARB_depth_clamp: %s", GLAD_GL_ARB_depth_clamp ? "yes" : "no");
    sysLogPrintf(LOG_NOTE, "GL: ARB_texture_mirror_clamp_to_edge: %s", GLAD_GL_ARB_texture_mirror_clamp_to_edge ? "yes" : "no");
}

static void* gl_load_proc(const char* name) {
    void* ret = SDL_GL_GetProcAddress(name);
    if (ret) {
        return ret;
    }

    // try with postfixes
    static const char* post[] = { "ARB", "EXT" };
    char tmp[256] = { 0 };
    for (size_t i = 0; i < sizeof(post) / sizeof(*post); ++i) {
        snprintf(tmp, sizeof(tmp), "%s%s", name, post[i]);
        ret = SDL_GL_GetProcAddress(tmp);
        if (ret) {
            return ret;
        }
    }

    sysLogPrintf(LOG_ERROR, "GL: could not find function: %s", name);

    return NULL;
}

#ifndef __ANDROID__ // if PC
// VR try to fix camspy bug on AMD graphic card
typedef void (APIENTRY *PFNGLTEXTUREBARRIERPROC)(void);
static PFNGLTEXTUREBARRIERPROC glTextureBarrier_ptr = nullptr;
static bool gl_has_texture_barrier = false;

static void gfx_opengl_init_texture_barrier(void) {
    glTextureBarrier_ptr = (PFNGLTEXTUREBARRIERPROC)SDL_GL_GetProcAddress("glTextureBarrier");
    if (!glTextureBarrier_ptr) {
        glTextureBarrier_ptr = (PFNGLTEXTUREBARRIERPROC)SDL_GL_GetProcAddress("glTextureBarrierNV");
    }
    gl_has_texture_barrier = (glTextureBarrier_ptr != nullptr);

    sysLogPrintf(LOG_NOTE, "GL texture_barrier: %s", gl_has_texture_barrier ? "yes" : "no");
}

static void gl_texture_barrier_safe(void) {
    if (gl_has_texture_barrier && glTextureBarrier_ptr) {
        glTextureBarrier_ptr();
    }

    else {
        glFinish();
    }
}
#endif
//---

static void gfx_opengl_init_extensions(void) {
    // patch some extension values and pointers
#ifdef GEVR
    /*
     * PORT: glad only looks for GL_EXT_depth_clamp in its GLES2 loader, and we
     * load GLES through the desktop one, so the Quest (which has it) always fell
     * back to the z *= 0.3 hack below and lost ~3x depth precision - bullet holes
     * and wall decals cropped and fought their walls at a slant. Ask the driver.
     */
    if (gl_es && !GLAD_GL_EXT_depth_clamp && glGetStringi && GLVersion.major >= 3) {
        GLint n = 0;
        glGetIntegerv(GL_NUM_EXTENSIONS, &n);
        for (GLint i = 0; i < n; i++) {
            const char *ext = (const char *)glGetStringi(GL_EXTENSIONS, i);
            if (ext && strcmp(ext, "GL_EXT_depth_clamp") == 0) {
                GLAD_GL_EXT_depth_clamp = 1;
                break;
            }
        }
    }
#endif
    if (!GLAD_GL_ARB_depth_clamp) {
        if (GLAD_GL_EXT_depth_clamp || GLAD_GL_NV_depth_clamp) {
            GLAD_GL_ARB_depth_clamp = 1;
        }
        else if (!gl_es && GLVersion.major >= 3) {
            // GL3.2+ should have depth_clamp as part of the spec, but some devices don't report it for some reason
            GLAD_GL_ARB_depth_clamp = (GLVersion.major > 3 || (GLVersion.major == 3 && GLVersion.minor >= 2));
        }
    }

    if (!GLAD_GL_ARB_texture_mirror_clamp_to_edge) {
        GLAD_GL_ARB_texture_mirror_clamp_to_edge = GLAD_GL_EXT_texture_mirror_clamp_to_edge;
    }

    if (GLVersion.major < 3 && !GLAD_GL_ARB_framebuffer_object) {
        if (GLAD_GL_EXT_framebuffer_object && GLAD_GL_EXT_framebuffer_blit && GLAD_GL_EXT_framebuffer_multisample) {
            // because of the way glad works we'll have to copy the pointers over
            glad_glGenFramebuffers = glad_glGenFramebuffersEXT;
            glad_glGenRenderbuffers = glad_glGenRenderbuffersEXT;
            glad_glDeleteFramebuffers = glad_glDeleteFramebuffersEXT;
            glad_glDeleteRenderbuffers = glad_glDeleteRenderbuffersEXT;
            glad_glBindFramebuffer = glad_glBindFramebufferEXT;
            glad_glBindRenderbuffer = glad_glBindRenderbufferEXT;
            glad_glFramebufferRenderbuffer = glad_glFramebufferRenderbufferEXT;
            glad_glFramebufferTexture2D = glad_glFramebufferTexture2DEXT;
            glad_glRenderbufferStorage = glad_glRenderbufferStorageEXT;
            glad_glRenderbufferStorageMultisample = glad_glRenderbufferStorageMultisampleEXT;
            glad_glBlitFramebuffer = glad_glBlitFramebufferEXT;
        }
    }


}


static void gfx_opengl_init(void) {
    if (!gladLoadGLLoader(gl_load_proc) || glGetString == NULL || glEnable == NULL) {
        sysFatalError("Could not load OpenGL.\nReported SDL error: %s", SDL_GetError());
    }

    // check if we're using ES or core, which have more limited feature sets
    int val = 0;
    SDL_GL_GetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, &val);
    gl_core_profile = (val == SDL_GL_CONTEXT_PROFILE_CORE);
    gl_es = (val == SDL_GL_CONTEXT_PROFILE_ES);

    gfx_opengl_init_extensions();
    // GoldenEye: static meshes made in an older context are gone with it
    s_glGeneration++;
    s_skinPaletteBuffer = 0; // the previous context owns the old name
    s_meshClipDistance = false;
    if (gl_es && glGetStringi && GLVersion.major >= 3) {
        GLint n = 0;
        glGetIntegerv(GL_NUM_EXTENSIONS, &n);
        for (GLint i = 0; i < n && !s_meshClipDistance; i++) {
            const char *ext = (const char *)glGetStringi(GL_EXTENSIONS, i);
            s_meshClipDistance = ext && strcmp(ext, "GL_EXT_clip_cull_distance") == 0;
        }
    }
    sysLogPrintf(LOG_NOTE, "GL: static meshes %s", (!gl_es || GLVersion.major < 3) ? "off (not GLES 3)"
                 : s_meshClipDistance ? "on (far reject: clip distance)" : "on (far reject: no depth clamp)");
    sysLogPrintf(LOG_NOTE, "GL: depth clamp: %s (EXT %d, NV %d, es %d)", GLAD_GL_ARB_depth_clamp ? "yes" : "no (z *= 0.3 hack)",
                 GLAD_GL_EXT_depth_clamp, GLAD_GL_NV_depth_clamp, gl_es ? 1 : 0);

#ifndef ANDROID // if PC
    gfx_opengl_init_texture_barrier(); // VR AMD fix
#endif

    if (sysArgCheck("--debug-gl")) {
        gfx_opengl_enable_debug();
        // dump version information as early as possible
        gfx_opengl_log_info();
    }

    if (GLVersion.major < 2 || (GLVersion.major == 2 && GLVersion.minor < 1)) {
        const char* ver = (const char*)glGetString(GL_VERSION);
        sysFatalError("Could not load OpenGL 2.1.\nReported version: %d.%d (%s)",
                      GLVersion.major, GLVersion.minor, ver ? ver : "unknown");
    }

    if (!gfx_opengl_supports_shaders()) {
        sysLogPrintf(LOG_WARNING, "GL: GLSL 1.30 may be unsupported");
        // maybe replace this with sysFatalError, although the GLSL compiler will cause that later anyway
    }

    if (!gfx_framebuffers_enabled) {
        sysLogPrintf(LOG_WARNING, "GL: framebuffer effects disabled by user");
    }
    else if (!gfx_opengl_supports_framebuffers()) {
        sysLogPrintf(LOG_WARNING, "GL: GL_ARB_framebuffer_object unsupported, framebuffer effects disabled");
        gfx_framebuffers_enabled = false;
    }

    if ((GLVersion.major < 4 || GLVersion.minor < 4) && !GLAD_GL_ARB_texture_mirror_clamp_to_edge) {
        // GL_MIRROR_CLAMP_TO_EDGE unsupported
        gl_mirror_clamp = GL_MIRRORED_REPEAT;
    }

    // determine GLSL version
    if (gl_es) {
        // ES has its own numbering scheme, but it should support 300 even on 3.1 and 3.2
        gl_glsl_version = 300;
        snprintf(gl_glsl_version_str, sizeof(gl_glsl_version_str), "%d es", gl_glsl_version);
    }
    else if (!gl_core_profile) {
        // in compatibility profile we can just request the lowest possible version
#ifdef ANDROID // VR
        gl_glsl_version = 130;
#else
        gl_glsl_version = 330;
#endif
        snprintf(gl_glsl_version_str, sizeof(gl_glsl_version_str), "%d", gl_glsl_version);
    }
    else {
        // otherwise we have to pick a specific version
        if (GLVersion.major == 3 && GLVersion.minor == 2) {
            // 3.2 core is the earliest core version and it follows the old numbering scheme
            gl_glsl_version = 150;
        }
        else {
            // 3.3 and above follow the new numbering scheme
            gl_glsl_version = GLVersion.major * 100 + GLVersion.minor * 10;
        }
        snprintf(gl_glsl_version_str, sizeof(gl_glsl_version_str), "%d core", gl_glsl_version);
    }

    // VR
    gfx_opengl_init_multiview();

    glGenBuffers(1, &opengl_vbo);
    glBindBuffer(GL_ARRAY_BUFFER, opengl_vbo);
    {
        typedef void (*PFN_BufferStorageEXT)(GLenum, GLsizeiptr, const void *, GLbitfield);
        PFN_BufferStorageEXT bufferStorage = (PFN_BufferStorageEXT)SDL_GL_GetProcAddress("glBufferStorageEXT");
        const GLbitfield flags = GL_MAP_WRITE_BIT | 0x0040 /* PERSISTENT */ | 0x0080 /* COHERENT */;
        const GLsizeiptr segment = 4 << 20;
        if (gl_es && bufferStorage != NULL && glMapBufferRange != NULL && glFenceSync != NULL) {
            bufferStorage(GL_ARRAY_BUFFER, segment * GEVR_PM_SEGMENTS, NULL, flags);
            s_pmPtr = (uint8_t *)glMapBufferRange(GL_ARRAY_BUFFER, 0, segment * GEVR_PM_SEGMENTS, flags);
            s_pmSegSize = segment;
        }
        sysLogPrintf(LOG_NOTE, "GL: persistent vertex buffer %s", s_pmPtr ? "on (3 x 4 MB)" : "off (glBufferData per draw)");
    }

    if (gl_core_profile || gl_es) {
        // warn the user that odd behavior can occur
        sysLogPrintf(LOG_WARNING, "GL: using core profile or ES, watch out for errors");
        // core/ES will fail badly if we don't use a VAO for our VBO
        glGenVertexArrays(1, &opengl_vao);
        glBindVertexArray(opengl_vao);
    }

    if (GLAD_GL_ARB_depth_clamp) {
        glEnable(GL_DEPTH_CLAMP);
        /*
         * PORT: depth clamp pins anything past the squashed far plane to the
         * far depth, which equalled the cleared depth, so the opaque GL_LESS
         * test threw it away. The N64's clamped far z sits just in front of
         * its cleared z buffer and draws; end the range a step short of 1.0 so
         * it does here too. glad's desktop loader leaves glDepthRangef null on
         * GLES (calling it crashed at start), so ask the driver.
         */
        typedef void (*PFN_DepthRangef)(GLfloat, GLfloat);
        PFN_DepthRangef depthRangef = (PFN_DepthRangef)SDL_GL_GetProcAddress("glDepthRangef");
        if (depthRangef) {
            depthRangef(0.0f, 1.0f - 1.0f / 65536.0f);
        }
    }
    glDepthFunc(GL_LEQUAL);
    glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);

#ifndef ANDROID // if PC
    mirror_apply_size(mirror_enabled); // VR
#endif
    framebuffers.resize(1); // for the default screen buffer

}


static void gfx_opengl_on_resize(void) {
}

static void gfx_opengl_start_frame(void) {
    frame_count++;
    gevr_decal_switch_poll();
    gevr_zdebug_poll();

    gVrMenuLWasClearedThisFrame = false;
    gVrMenuLCaptureDepth = 0;

    gVrMenuHWasClearedThisFrame = false;
    gVrMenuHCaptureDepth = 0;

    gVrMenuPWasClearedThisFrame = false;
    gVrMenuPCaptureDepth = 0;

}

static void gfx_opengl_end_frame(void) {
    glFlush();

}

static void gfx_opengl_finish_render(void) {

}

static int gfx_opengl_create_framebuffer() {
    size_t i = framebuffers.size();
    framebuffers.resize(i + 1);

    GLuint clrbuf;
    glGenTextures(1, &clrbuf);
    glBindTexture(GL_TEXTURE_2D, clrbuf);

    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);


    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glBindTexture(GL_TEXTURE_2D, 0);
    framebuffers[i].clrbuf = clrbuf;

    if (!gfx_framebuffers_enabled) {
        return i;
    }

    GLuint clrbuf_msaa;
    glGenRenderbuffers(1, &clrbuf_msaa);
    framebuffers[i].clrbuf_msaa = clrbuf_msaa;

    GLuint rbo;
    glGenRenderbuffers(1, &rbo);
    glBindRenderbuffer(GL_RENDERBUFFER, rbo);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, 1, 1);
    glBindRenderbuffer(GL_RENDERBUFFER, 0);
    framebuffers[i].rbo = rbo;

    GLuint fbo;
    glGenFramebuffers(1, &fbo);
    framebuffers[i].fbo = fbo;

    return i;
}

static void gfx_opengl_update_framebuffer_parameters(int fb_id, uint32_t width, uint32_t height, uint32_t msaa_level,
                                                     bool opengl_invert_y, bool render_target, bool has_depth_buffer,
                                                     bool can_extract_depth) {

    if (fb_id < 0 || fb_id >= (int)framebuffers.size()) {
        return;
    }


    Framebuffer& fb = framebuffers[fb_id];
    // Multiview FBO 0 is managed by vr_openxr.cpp, just update the size
    if (fb_id == 0 && !framebuffers.empty() && framebuffers[0].is_multiview) {
        framebuffers[0].width = width;
        framebuffers[0].height = height;
        return;
    }

    width = max(width, 1U);
    height = max(height, 1U);

    if (gfx_framebuffers_enabled) {
        glBindFramebuffer(GL_FRAMEBUFFER, fb.fbo);

        if (fb_id != 0) {
            if (fb.width != width || fb.height != height || fb.msaa_level != msaa_level) {
                if (msaa_level <= 1) {
                    glBindTexture(GL_TEXTURE_2D, fb.clrbuf);
                    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);

                    glBindTexture(GL_TEXTURE_2D, 0);
                    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, fb.clrbuf, 0);
                }
                else {
                    glBindRenderbuffer(GL_RENDERBUFFER, fb.clrbuf_msaa);
                    glRenderbufferStorageMultisample(GL_RENDERBUFFER, msaa_level, GL_RGBA8, width, height);
                    glBindRenderbuffer(GL_RENDERBUFFER, 0);
                    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, fb.clrbuf_msaa);
                }
            }

            if (has_depth_buffer &&
                (fb.width != width || fb.height != height || fb.msaa_level != msaa_level || !fb.has_depth_buffer)) {
                glBindRenderbuffer(GL_RENDERBUFFER, fb.rbo);
                if (msaa_level <= 1) {
                    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, width, height);
                }
                else {
                    glRenderbufferStorageMultisample(GL_RENDERBUFFER, msaa_level, GL_DEPTH24_STENCIL8, width, height);
                }
                glBindRenderbuffer(GL_RENDERBUFFER, 0);
            }

            if (!fb.has_depth_buffer && has_depth_buffer) {
                glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, fb.rbo);
            }
            else if (fb.has_depth_buffer && !has_depth_buffer) {
                glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, 0);
            }
        }
    }
    else {
        has_depth_buffer = false;
    }

    fb.width = width;
    fb.height = height;
    fb.has_depth_buffer = has_depth_buffer;
    fb.msaa_level = msaa_level;
    fb.invert_y = opengl_invert_y;
}

bool gfx_opengl_start_draw_to_framebuffer(int fbid, float noisescale) {
    if (gfx_framebuffers_enabled && fbid < (int)framebuffers.size()) {
        Framebuffer& fb = framebuffers[fbid];

        if (noisescale != 0.0f) {
            current_noise_scale = 1.0f / noisescale;
        }

        glBindFramebuffer(GL_FRAMEBUFFER, fb.fbo);
        current_framebuffer = fbid;

        //        LOGI("GL start_draw_to_fb: fbid=%d w=%u h=%u is_mv=%d",
        //                     fbid, fb.width, fb.height, fb.is_multiview);

        return true;
    }
    return false;
}



// ============================================================================
// VR Layers HUD/MENU LEFT Hand
// ============================================================================

static int gVrMenuFb = -1;
static int gVrMenuFbPrevious = -1;
extern "C" int  vr_get_internal_render_width(void);
extern "C" int  vr_get_internal_render_height(void);
static GLint gVrMenuPrevViewport[4] = {0, 0, 0, 0 };
static GLint gVrMenuCaptureViewport[4] = {0, 0, 0, 0 };
extern void gfx_flush(void);

static void gfx_opengl_vr_menu_fb_init(void) {
    int w = vr_get_internal_render_width();
    int h = vr_get_internal_render_height();

    if (gVrMenuFb < 0) {
        gVrMenuFb = gfx_opengl_create_framebuffer();
    }

    // Recreate/resize if the VR resolution has changed (e.g. on the first frame)
    gfx_opengl_update_framebuffer_parameters(
            gVrMenuFb,
            (uint32_t)w, (uint32_t)h,
            /*msaa_level=*/1,
            /*opengl_invert_y=*/false,
            /*render_target=*/true,
            /*has_depth_buffer=*/true,
            /*can_extract_depth=*/true
    );
}

bool gfx_vr_menu_L_dirty_and_clear(void) {
    // GoldenEye: kept until the next game frame, see gfx_vr_menu_H_dirty_and_clear.
    return hud_L_was_drawn;
}

void gfx_vr_hud_capture_begin_L(void)
{
    GevrProfileSection profile(GEVR_TIME_CAPTURE);
    gfx_flush();
    gfx_opengl_vr_menu_fb_init();

    if (gVrMenuLCaptureDepth++ > 0) {
        return;
    }
    gevr_capture_state_save(0);
    gfx_opengl_menu_capture_push();

    glGetIntegerv(GL_VIEWPORT, gVrMenuPrevViewport);
    gVrMenuFbPrevious = (int)current_framebuffer;

    gfx_opengl_start_draw_to_framebuffer(gVrMenuFb, 0.0f);

    int w = vr_get_internal_render_width();
    int h = vr_get_internal_render_height();

    glViewport(0, 0, w, h);
    glDisable(GL_SCISSOR_TEST);

    if (!gVrMenuLWasClearedThisFrame) {
        // GoldenEye: depth too - the stereo watch's pages must not test
        // against whatever an earlier frame left in this buffer.
        GLboolean depthMask = GL_TRUE;
        glGetBooleanv(GL_DEPTH_WRITEMASK, &depthMask);
        glDepthMask(GL_TRUE);
        glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        glDepthMask(depthMask);
        gVrMenuLWasClearedThisFrame = true;
    }

    glEnable(GL_SCISSOR_TEST);
    glScissor(0, 0, w, h);

    gVrMenuCaptureViewport[0] = 0;
    gVrMenuCaptureViewport[1] = 0;
    gVrMenuCaptureViewport[2] = w;
    gVrMenuCaptureViewport[3] = h;
}


void gfx_vr_hud_capture_end_L(void)
{
    GevrProfileSection profile(GEVR_TIME_CAPTURE);

    gfx_flush();

    if (gVrMenuLCaptureDepth <= 0) {
        gVrMenuLCaptureDepth = 0;
        return;
    }

    if (--gVrMenuLCaptureDepth > 0) {
        return;
    }

    gfx_opengl_menu_capture_pop();
    hud_L_was_drawn = true;


    if (gVrMenuFbPrevious >= 0) {
        gfx_opengl_start_draw_to_framebuffer(gVrMenuFbPrevious, 0.0f);
    }

    gVrMenuFbPrevious = -1;

    glViewport(
            gVrMenuPrevViewport[0],
            gVrMenuPrevViewport[1],
            gVrMenuPrevViewport[2],
            gVrMenuPrevViewport[3]);

    glScissor(
            gVrMenuPrevViewport[0],
            gVrMenuPrevViewport[1],
            gVrMenuPrevViewport[2],
            gVrMenuPrevViewport[3]);
    gevr_eye_capture_restored(gVrMenuPrevViewport, s_captureStates[0].scissor);   // issue #53
    gevr_capture_state_restore(0);
}

GLuint gfx_opengl_get_vr_menu_texture(void) {
    if (gVrMenuFb < 0) return 0;
    return framebuffers[gVrMenuFb].clrbuf;
}
//---


// ============================================================================
// VR - Second menu/HUD quad: RIGHT hand
// ============================================================================
static int  gVrMenuRFb          = -1;
static int  gVrMenuRFbPrevious  = -1;
static GLint gVrMenuRPrevViewport[4]    = {0, 0, 0, 0};
static GLint gVrMenuRCaptureViewport[4] = {0, 0, 0, 0};


static void gfx_opengl_vr_menu_R_fb_init(void)
{
    int w = vr_get_internal_render_width();
    int h = vr_get_internal_render_height();
    if (gVrMenuRFb < 0)
        gVrMenuRFb = gfx_opengl_create_framebuffer();
    gfx_opengl_update_framebuffer_parameters(
            gVrMenuRFb, (uint32_t)w, (uint32_t)h,
            /*msaalevel*/1, /*inverty*/false,
            /*rendertarget*/true, /*hasdepth*/true, /*canextractdepth*/true);
}

void gfx_vr_hud_capture_begin_R(void)
{
    GevrProfileSection profile(GEVR_TIME_CAPTURE);
    gfx_flush();
    gfx_opengl_vr_menu_R_fb_init();
    gevr_capture_state_save(1);
    gfx_opengl_menu_capture_push();

    glGetIntegerv(GL_VIEWPORT, gVrMenuRPrevViewport);
    gVrMenuRFbPrevious = (int)current_framebuffer;
    gfx_opengl_start_draw_to_framebuffer(gVrMenuRFb, 0.0f);
    int w = vr_get_internal_render_width();
    int h = vr_get_internal_render_height();
    glViewport(0, 0, w, h);
    glDisable(GL_SCISSOR_TEST);
    glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glEnable(GL_SCISSOR_TEST);
    glScissor(0, 0, w, h);
    gVrMenuRCaptureViewport[0] = 0;
    gVrMenuRCaptureViewport[1] = 0;
    gVrMenuRCaptureViewport[2] = w;
    gVrMenuRCaptureViewport[3] = h;
}

// Alpha bounds are measured through a fenced pixel-pack ring. Poll every
// capture, including frames that do not request a new measurement.
extern "C" bool gfx_vr_menu_R_bbox(float out[4])
{
    return gfx_vr_hud_bounds_box(out);
}

extern "C" s32 gevrAimModeOn(void);
static void gevr_measure_R_capture(void)
{
    GevrCpuSection timing(GEVR_TIME_HUD_READBACK);
    gfx_vr_hud_bounds_update(framebuffers[gVrMenuRFb].fbo,
        vr_get_internal_render_width(), vr_get_internal_render_height(), gevrAimModeOn());
}

void gfx_vr_hud_capture_end_R(void)
{
    GevrProfileSection profile(GEVR_TIME_CAPTURE);
    hud_R_was_drawn = true;
    gfx_flush();
    gevr_measure_R_capture();
    gfx_opengl_menu_capture_pop();

    if (gVrMenuRFbPrevious >= 0)
        gfx_opengl_start_draw_to_framebuffer(gVrMenuRFbPrevious, 0.0f);
    gVrMenuRFbPrevious = -1;
    glViewport(gVrMenuRPrevViewport[0], gVrMenuRPrevViewport[1],
               gVrMenuRPrevViewport[2], gVrMenuRPrevViewport[3]);
    glScissor (gVrMenuRPrevViewport[0], gVrMenuRPrevViewport[1],
               gVrMenuRPrevViewport[2], gVrMenuRPrevViewport[3]);
    gevr_eye_capture_restored(gVrMenuRPrevViewport, s_captureStates[1].scissor);   // issue #53


    gevr_capture_state_restore(1);
}

GLuint gfx_opengl_get_vr_menu_texture_R(void)
{
    if (gVrMenuRFb < 0) return 0;
    return framebuffers[gVrMenuRFb].clrbuf;
}

bool gfx_vr_menu_R_dirty_and_clear(void) {
    // GoldenEye: kept until the next game frame, see gfx_vr_menu_H_dirty_and_clear.
    bool v = hud_R_was_drawn;
    return v;
}

// ============================================================================
// --- VR - Third menu/HUD quad HEAD-LOCKED
// ============================================================================
static int gVrMenuHFb = -1;
static int gVrMenuHFbPrevious = -1;
static GLint gVrMenuHPrevViewport[4] = {0,0,0,0};
static GLint gVrMenuHCaptureViewport[4] = {0,0,0,0};


static void gfx_opengl_vr_menu_H_fb_init(void) {
    int w = vr_get_internal_render_width();
    int h = vr_get_internal_render_height();
    if (gVrMenuHFb <= 0)
        gVrMenuHFb = gfx_opengl_create_framebuffer();
    gfx_opengl_update_framebuffer_parameters(
            gVrMenuHFb, (uint32_t)w, (uint32_t)h,
            /*msaalevel=*/1, /*inverty=*/false,
            /*rendertarget=*/true, /*hasdepth=*/true, /*canextractdepth=*/true);
}

void gfx_vr_hud_capture_begin_H(void)
{
    GevrProfileSection profile(GEVR_TIME_CAPTURE);
    gfx_flush();
    gfx_opengl_vr_menu_H_fb_init();

    if (gVrMenuHCaptureDepth++ > 0) {
        return;
    }
    gevr_capture_state_save(2);
    gfx_opengl_menu_capture_push();

    glGetIntegerv(GL_VIEWPORT, gVrMenuHPrevViewport);
    // Every independent capture starts with the same ordinary HUD transform.
    VrIsTitleLegal = true;
    s_uniCacheValid = false;
    gVrMenuHFbPrevious = (int)current_framebuffer;

    gfx_opengl_start_draw_to_framebuffer(gVrMenuHFb, 0.0f);

    int w = vr_get_internal_render_width();
    int h = vr_get_internal_render_height();

    glViewport(0, 0, w, h);
    glDisable(GL_SCISSOR_TEST);

    if (!gVrMenuHWasClearedThisFrame) {
        glClearColor(0.f, 0.f, 0.f, 0.f);
        glClear(GL_COLOR_BUFFER_BIT);
        gVrMenuHWasClearedThisFrame = true;
    }

    glEnable(GL_SCISSOR_TEST);
    glScissor(0, 0, w, h);

    gVrMenuHCaptureViewport[0] = 0;
    gVrMenuHCaptureViewport[1] = 0;
    gVrMenuHCaptureViewport[2] = w;
    gVrMenuHCaptureViewport[3] = h;
}


void gfx_vr_hud_capture_end_H(void)
{
    GevrProfileSection profile(GEVR_TIME_CAPTURE);
    gfx_flush();

    if (gVrMenuHCaptureDepth <= 0) {
        gVrMenuHCaptureDepth = 0;
        return;
    }

    if (--gVrMenuHCaptureDepth > 0) {
        return;
    }

    gfx_opengl_menu_capture_pop();
    hud_H_was_drawn = true;

    if (gVrMenuHFbPrevious >= 0) {
        gfx_opengl_start_draw_to_framebuffer(gVrMenuHFbPrevious, 0.0f);
    }

    gVrMenuHFbPrevious = -1;

    glViewport(
            gVrMenuHPrevViewport[0],
            gVrMenuHPrevViewport[1],
            gVrMenuHPrevViewport[2],
            gVrMenuHPrevViewport[3]);

    glScissor(
            gVrMenuHPrevViewport[0],
            gVrMenuHPrevViewport[1],
            gVrMenuHPrevViewport[2],
            gVrMenuHPrevViewport[3]);
    gevr_eye_capture_restored(gVrMenuHPrevViewport, s_captureStates[2].scissor);   // issue #53
    gevr_capture_state_restore(2);
}

bool gfx_vr_menu_H_dirty_and_clear(void) {
    // GoldenEye: the game draws at 60 Hz and the display runs at 72+, so a
    // captured HUD must stay up through the XR frames between game frames or
    // it flickers; gfx_vr_hud_H_new_frame (gfx_run) drops it instead.
    return hud_H_was_drawn;
}

// A new game frame is being drawn: whether it shows the head-locked HUD is
// decided by whether it captures one.
void gfx_vr_hud_H_new_frame(void) {
    hud_H_was_drawn = false;
    hud_P_was_drawn = false;
    hud_R_was_drawn = false;
    hud_L_was_drawn = false;
    s_scopeDrawn[0] = s_scopeDrawn[1] = false;   // the scopes' images, likewise (issue #40)
}

GLuint gfx_opengl_get_vr_menu_texture_H(void) {
    if (gVrMenuHFb <= 0) return 0;
    return framebuffers[gVrMenuHFb].clrbuf;
}

// ============================================================================
// --- VR - the weapon panel (issue #10), a copy of the head-locked capture
// ============================================================================
static int gVrMenuPFb = -1;
static int gVrMenuPFbPrevious = -1;
static GLint gVrMenuPPrevViewport[4] = {0,0,0,0};

void gfx_vr_hud_capture_begin_P(void)
{
    GevrProfileSection profile(GEVR_TIME_CAPTURE);
    gfx_flush();
    {
        int w = vr_get_internal_render_width();
        int h = vr_get_internal_render_height();
        if (gVrMenuPFb <= 0)
            gVrMenuPFb = gfx_opengl_create_framebuffer();
        gfx_opengl_update_framebuffer_parameters(
                gVrMenuPFb, (uint32_t)w, (uint32_t)h,
                /*msaalevel=*/1, /*inverty=*/false,
                /*rendertarget=*/true, /*hasdepth=*/true, /*canextractdepth=*/true);
    }

    if (gVrMenuPCaptureDepth++ > 0) {
        return;
    }
    gevr_capture_state_save(3);
    gfx_opengl_menu_capture_push();

    glGetIntegerv(GL_VIEWPORT, gVrMenuPPrevViewport);
    gVrMenuPFbPrevious = (int)current_framebuffer;

    gfx_opengl_start_draw_to_framebuffer(gVrMenuPFb, 0.0f);

    int w = vr_get_internal_render_width();
    int h = vr_get_internal_render_height();

    glViewport(0, 0, w, h);
    glDisable(GL_SCISSOR_TEST);

    if (!gVrMenuPWasClearedThisFrame) {
        glClearColor(0.f, 0.f, 0.f, 0.f);
        glDepthMask(GL_TRUE);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);   /* the item model is depth-tested */
        glDepthMask(current_depth_mask ? GL_TRUE : GL_FALSE);
        gVrMenuPWasClearedThisFrame = true;
    }

    glEnable(GL_SCISSOR_TEST);
    glScissor(0, 0, w, h);
}

void gfx_vr_hud_capture_end_P(void)
{
    GevrProfileSection profile(GEVR_TIME_CAPTURE);
    gfx_flush();

    if (gVrMenuPCaptureDepth <= 0) {
        gVrMenuPCaptureDepth = 0;
        return;
    }

    if (--gVrMenuPCaptureDepth > 0) {
        return;
    }

    gfx_opengl_menu_capture_pop();
    hud_P_was_drawn = true;

    if (gVrMenuPFbPrevious >= 0) {
        gfx_opengl_start_draw_to_framebuffer(gVrMenuPFbPrevious, 0.0f);
    }
    gVrMenuPFbPrevious = -1;

    glViewport(gVrMenuPPrevViewport[0], gVrMenuPPrevViewport[1], gVrMenuPPrevViewport[2], gVrMenuPPrevViewport[3]);
    glScissor(gVrMenuPPrevViewport[0], gVrMenuPPrevViewport[1], gVrMenuPPrevViewport[2], gVrMenuPPrevViewport[3]);
    gevr_eye_capture_restored(gVrMenuPPrevViewport, s_captureStates[3].scissor);   // issue #53
    gevr_capture_state_restore(3);
}

bool gfx_vr_menu_P_dirty_and_clear(void) {
    return hud_P_was_drawn;   /* dropped by gfx_vr_hud_H_new_frame, as the head-locked HUD */
}

GLuint gfx_opengl_get_vr_menu_texture_P(void) {
    if (gVrMenuPFb <= 0) return 0;
    return framebuffers[gVrMenuPFb].clrbuf;
}
//---






void gfx_opengl_clear_framebuffer(bool clear_color, bool clear_depth) {
    glDisable(GL_SCISSOR_TEST);

    GLbitfield mask = 0;
    if (clear_color) {
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        mask |= GL_COLOR_BUFFER_BIT;
    }
    if (clear_depth) {
        glDepthMask(GL_TRUE);
        glStencilMask(0xff);
        mask |= GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT;   // stencil: the decal test
    }
    glClear(mask);
    if (clear_depth) {
        glDepthMask(current_depth_mask ? GL_TRUE : GL_FALSE);
    }

    glEnable(GL_SCISSOR_TEST);
}

void gfx_opengl_resolve_msaa_color_buffer(int fb_id_target, int fb_id_source) {
    if (!gfx_framebuffers_enabled) {
        return;
    }

    Framebuffer& fb_dst = framebuffers[fb_id_target];
    Framebuffer& fb_src = framebuffers[fb_id_source];
    glDisable(GL_SCISSOR_TEST);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, fb_dst.fbo);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, fb_src.fbo);
    glBlitFramebuffer(0, 0, fb_src.width, fb_src.height, 0, 0, fb_dst.width, fb_dst.height, GL_COLOR_BUFFER_BIT,
                      GL_NEAREST);
    glBindFramebuffer(GL_FRAMEBUFFER, current_framebuffer);
    glEnable(GL_SCISSOR_TEST);
}


void* gfx_opengl_get_framebuffer_texture_id(int fbid) {
    const Framebuffer& fb = framebuffers[fbid];

    void* tex = nullptr;
    if (fb.is_multiview) {
        tex = (void*)(uintptr_t)vr_get_current_multiview_swapchain_tex();
    }
    else {
        tex = (void*)(uintptr_t)fb.clrbuf;
    }

    return tex;
}



void gfx_opengl_select_texture_fb(int fb_id) {
    //    LOGI( "GL select_texture_fb: fbid=%d clrbuf=%u is_mv=%d",
    //                 fb_id, framebuffers[fb_id].clrbuf, framebuffers[fb_id].is_multiview);
    // glDisable(GL_DEPTH_TEST);
    glActiveTexture(GL_TEXTURE0 + 0);
    glBindTexture(GL_TEXTURE_2D, framebuffers[fb_id].clrbuf);
    s_boundTex[0] = framebuffers[fb_id].clrbuf;
    s_activeTexUnit = 0;

    current_textures_linear_filter[0] = true;
}



void gfx_opengl_copy_framebuffer(int fb_dst, int fb_src, int left, int top, bool flip_y, bool use_back) {
    if (!gfx_framebuffers_enabled || fb_dst >= (int)framebuffers.size() || fb_src >= (int)framebuffers.size()) {
        return;
    }

    Framebuffer& src = framebuffers[fb_src];
    Framebuffer& dst = framebuffers[fb_dst];

//    LOGI("Framebuffer src %d", fb_src);
//    LOGI("Framebuffer dst %d", fb_dst);

    int srcX0, srcY0, srcX1, srcY1;
    int dstX0, dstY0, dstX1, dstY1;

    dstX0 = dstY0 = 0;
    dstX1 = dst.width;
    dstY1 = dst.height;

    if (left >= 0 && top >= 0) {
        // unscaled rectangle copy
        srcX0 = left;
        srcY0 = top;
        srcX1 = left + dst.width;
        srcY1 = top + dst.height;
    }
    else {
        // scaled full copy
        srcX0 = 0;
        srcY0 = 0;
        srcX1 = src.width;
        srcY1 = src.height;
    }



    // For PC Oculus Meta runtime
    // Special menu case (fb_dst == 25): direct blit from layer 0 of the swapchain
    if ( is_meta_runtime && fb_dst == 25) {
        copy_fbo_menu = true;
        GLuint texArray = vr_get_current_multiview_swapchain_tex();

        // Temporary FBO pointing to layer 0 of the swapchain
        GLuint tmpFbo = 0;
        glGenFramebuffers(1, &tmpFbo);
        glBindFramebuffer(GL_READ_FRAMEBUFFER, tmpFbo);
        glFramebufferTextureLayer(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, texArray, 0, 0);

        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, dst.fbo);
        glDisable(GL_SCISSOR_TEST);

        // Blit with scaling (swapchain 1832x1920 → FBO 25 698x524)
        glBlitFramebuffer(
                0, framebuffers[0].height, framebuffers[0].width, 0,  // inverted src Y
                0, 0, dst.width, dst.height,
                GL_COLOR_BUFFER_BIT, GL_LINEAR
        );

        glEnable(GL_SCISSOR_TEST);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glDeleteFramebuffers(1, &tmpFbo);
        return;
    }


    // Fix for background menu, camspy, cloaking effect on enemies or bots (G5 building / challenges)
    if (fb_dst != 4 && framebuffers[0].is_multiview) { // fb_dst 4 = cutescene

        mv_blit_init();

        // Minimal GL state backup
        GLint prevFbo = 0, prevProg = 0;
        GLint viewport[4];
        GLboolean prevDepthTest = GL_FALSE;
        GLboolean prevDepthMask = GL_FALSE;
        GLboolean prevBlend = GL_FALSE;

        glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFbo);
        glGetIntegerv(GL_CURRENT_PROGRAM, &prevProg);
        glGetIntegerv(GL_VIEWPORT, viewport);
        glGetBooleanv(GL_DEPTH_TEST, &prevDepthTest);
        glGetBooleanv(GL_DEPTH_WRITEMASK, &prevDepthMask);
        glGetBooleanv(GL_BLEND, &prevBlend);


        // Calculation of normalized UVs corresponding to the source rectangle
        float u0, v0, u1, v1;

        if (left >= 0 && top >= 0) {
            // Sub-rectangle (invisible cloak, FB 5-19 effects)
            u0 = (float)srcX0 / (float)src.width;
            v0 = (float)srcY0 / (float)src.height;
            u1 = (float)srcX1 / (float)src.width;
            v1 = (float)srcY1 / (float)src.height;
        }
        else {
            // Full-screen copy (camera, menus, etc.) → full UV
            u0 = 0.0f;
            v0 = 0.0f;
            u1 = 1.0f;
            v1 = 1.0f;
        }


        // We leave the flip_y handling to the shader (as before), so no swapping of v0/v1 here
        // The shader will use uRect and uFlipY together.

        glBindFramebuffer(GL_FRAMEBUFFER, dst.fbo);
        glViewport(0, 0, dst.width, dst.height);

        glDisable(GL_SCISSOR_TEST);

        // IMPORTANT: copy the color without taking the destination FBO depth into account
        glDisable(GL_DEPTH_TEST);
        glDepthMask(GL_FALSE);
        glDisable(GL_BLEND);

        glUseProgram(mv_blit_prog);

        GLuint texArray = vr_get_current_multiview_swapchain_tex();
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D_ARRAY, texArray);


#ifndef __ANDROID__ // if PC
        // Fix AMD
        gfx_flush();
        gl_texture_barrier_safe(); // force visibility of the previous write
#endif

        glUniform1i(mv_blit_uTexLoc, 0);
        glUniform1i(mv_blit_uFlipYLoc, flip_y ? 1 : 0);

        // NEW: source rectangle in UV
        glUniform4f(mv_blit_uRectLoc, u0, v0, u1, v1);

        glBindVertexArray(opengl_vao);
        glDrawArrays(GL_TRIANGLES, 0, 3);

        // Restore GL state
        glBindVertexArray(0);
        glUseProgram(prevProg);
        glBindFramebuffer(GL_FRAMEBUFFER, prevFbo);
        glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);

        if (prevDepthTest) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);
        glDepthMask(prevDepthMask);
        if (prevBlend) glEnable(GL_BLEND); else glDisable(GL_BLEND);
        glEnable(GL_SCISSOR_TEST);


        return;
    }

    // Unchanged non-multiview path (classic glBlitFramebuffer)
    glDisable(GL_SCISSOR_TEST);

    glBindFramebuffer(GL_READ_FRAMEBUFFER, src.fbo);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, dst.fbo);

    if (flip_y) {
        std::swap(dstY0, dstY1);
    }

    if (fb_src == 0) {
        if (framebuffers[0].is_multiview) {
            glReadBuffer(GL_COLOR_ATTACHMENT0);
        }
        else {
            glReadBuffer(use_back && !gl_es ? GL_BACK : GL_FRONT);
        }
    }
    else {
        glReadBuffer(GL_COLOR_ATTACHMENT0);
    }

    glBlitFramebuffer(
            srcX0, srcY0, srcX1, srcY1,
            dstX0, dstY0, dstX1, dstY1,
            GL_COLOR_BUFFER_BIT, GL_NEAREST
    );

    glBindFramebuffer(GL_FRAMEBUFFER, framebuffers[current_framebuffer].fbo);
    glReadBuffer(GL_BACK);
    glEnable(GL_SCISSOR_TEST);
}




void gfx_opengl_set_texture_filter(FilteringMode mode) {
    current_filter_mode = mode;
}

FilteringMode gfx_opengl_get_texture_filter(void) {
    return current_filter_mode;
}




static void gfx_opengl_init_mirror_shader() {
#ifndef ANDROID // if PC
    if (s_mirror_prog) return;

    auto compile = [](GLenum type, const char* src) -> GLuint {
        GLuint s = glCreateShader(type);
        glShaderSource(s, 1, &src, nullptr);
        glCompileShader(s);
        return s;
    };

    GLuint vs = compile(GL_VERTEX_SHADER,   mv_blit_vs_src);
    GLuint fs = compile(GL_FRAGMENT_SHADER, mv_blit_fs_src);
    s_mirror_prog = glCreateProgram();
    glAttachShader(s_mirror_prog, vs);
    glAttachShader(s_mirror_prog, fs);
    glLinkProgram(s_mirror_prog);
    glDeleteShader(vs);
    glDeleteShader(fs);

    s_mirror_uloc_tex   = glGetUniformLocation(s_mirror_prog, "uTex");
    s_mirror_uloc_layer = glGetUniformLocation(s_mirror_prog, "uLayer");
    s_mirror_uloc_rect  = glGetUniformLocation(s_mirror_prog, "uRect");
    s_mirror_uloc_sbs = glGetUniformLocation(s_mirror_prog, "uSbs");

    // Empty VAO required for the fullscreen triangle trick
    glGenVertexArrays(1, &s_mirror_vao);
#endif
}


static void gfx_opengl_mirror_to_desktop(
        uint32_t src_w, uint32_t src_h,   // VR resolution
        uint32_t dst_w, uint32_t dst_h)   // real size of mirror_wnd
{
#ifndef ANDROID // if PC

    if (!gfx_sdl_is_mirror_enabled()) return;

    GLuint eye_array_tex = vr_get_current_multiview_swapchain_tex();
    if (!eye_array_tex) return;

    extern SDL_Window*   mirror_wnd;
    extern SDL_GLContext mirror_ctx;
    extern SDL_Window*   wnd;
    extern SDL_GLContext ctx;
    if (!mirror_wnd || !mirror_ctx) return;

    gfx_opengl_init_mirror_shader();

    // Letterbox calculation: rendering area in UV coordinates [0..1]
    float src_ratio = (float)src_w / (float)src_h;
    float dst_ratio = (float)dst_w / (float)dst_h;
    float rect_x = 0.0f, rect_y = 0.0f, rect_w = 1.0f, rect_h = 1.0f;


    // ── Switch to mirror_wnd ──
    SDL_GL_MakeCurrent(mirror_wnd, mirror_ctx);

    // Disable auto sRGB conversion on the desktop backbuffer
    glDisable(GL_FRAMEBUFFER_SRGB);

    glViewport(0, 0, (GLsizei)dst_w, (GLsizei)dst_h);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    glUseProgram(s_mirror_prog);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D_ARRAY, eye_array_tex);
    glUniform1i(s_mirror_uloc_tex,   0);

    bool sbs = gfx_sdl_is_mirror_sbs();
    glUniform1i(s_mirror_uloc_sbs, gfx_sdl_is_mirror_sbs() ? 1 : 0);

    if (sbs) {
        glUniform4f(s_mirror_uloc_rect, 0.0f, 0.0f, 1.0f, 1.0f);
    } else {
        glUniform4f(s_mirror_uloc_rect, rect_x, rect_y, rect_x + rect_w, rect_y + rect_h);
    }
    glUniform1i(s_mirror_uloc_layer, gfx_sdl_get_mirror_eye());

    glBindVertexArray(s_mirror_vao);
    glDrawArrays(GL_TRIANGLES, 0, 3);  // fullscreen triangle

    // --- NEW BLOCK 2: HUD DISPLAY ---
    // ── Switch to mirror_wnd ──
    SDL_GL_MakeCurrent(mirror_wnd, mirror_ctx);

    glDisable(GL_FRAMEBUFFER_SRGB);
    glViewport(0, 0, (GLsizei)dst_w, (GLsizei)dst_h);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    glUseProgram(s_mirror_prog);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D_ARRAY, eye_array_tex);
    glUniform1i(s_mirror_uloc_tex, 0);
    // ... (uniforms sbs/layer/rect unchanged) ...

    glBindVertexArray(s_mirror_vao);
    glDrawArrays(GL_TRIANGLES, 0, 3); // fullscreen triangle

// --- XR Layers: quad menu panel, same as in the headset ---
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D_ARRAY, 0);

    if (sbs) {
        // Left half = left eye, right half = right eye
        int halfW = dst_w / 2;

        VrMirrorMenuEntry entriesL[3];
        int nL = vrGetMenuMirrorMVPList(0, entriesL);
        for (int i = 0; i < nL; i++) {
            GLuint tex = 0;
            switch (entriesL[i].source) {
                case VR_MIRROR_MENU_L: tex = gfx_opengl_get_vr_menu_texture();   break;
                case VR_MIRROR_MENU_R: tex = gfx_opengl_get_vr_menu_texture_R(); break;
                case VR_MIRROR_MENU_H: tex = gfx_opengl_get_vr_menu_texture_H(); break;
            }
            gfx_opengl_draw_mirror_menu_overlay(tex, entriesL[i].mvp, 0, 0, halfW, dst_h);
        }

        VrMirrorMenuEntry entriesR[3];
        int nR = vrGetMenuMirrorMVPList(1, entriesR);
        for (int i = 0; i < nR; i++) {
            GLuint tex = 0;
            switch (entriesR[i].source) {
                case VR_MIRROR_MENU_L: tex = gfx_opengl_get_vr_menu_texture();   break;
                case VR_MIRROR_MENU_R: tex = gfx_opengl_get_vr_menu_texture_R(); break;
                case VR_MIRROR_MENU_H: tex = gfx_opengl_get_vr_menu_texture_H(); break;
            }
            gfx_opengl_draw_mirror_menu_overlay(tex, entriesR[i].mvp, halfW, 0, dst_w - halfW, dst_h);
        }

        // Restore the full-screen viewport for the rest
        glViewport(0, 0, dst_w, dst_h);
    } else {
        int mirrorEye = gfx_sdl_get_mirror_eye();
        VrMirrorMenuEntry entries[3];
        int n = vrGetMenuMirrorMVPList(mirrorEye, entries);
        for (int i = 0; i < n; i++) {
            GLuint tex = 0;
            switch (entries[i].source) {
                case VR_MIRROR_MENU_L: tex = gfx_opengl_get_vr_menu_texture();   break;
                case VR_MIRROR_MENU_R: tex = gfx_opengl_get_vr_menu_texture_R(); break;
                case VR_MIRROR_MENU_H: tex = gfx_opengl_get_vr_menu_texture_H(); break;
            }
            gfx_opengl_draw_mirror_menu_overlay(tex, entries[i].mvp, 0, 0, dst_w, dst_h);
        }
    }

// Cleanup
glBindVertexArray(0);
glBindTexture(GL_TEXTURE_2D, 0);
glUseProgram(0);
glDisable(GL_BLEND);
glFlush();

SDL_GL_MakeCurrent(wnd, ctx);
#endif
}


struct GfxRenderingAPI gfx_opengl_api = {
        gfx_opengl_get_name,
        gfx_opengl_get_max_texture_size,
        gfx_opengl_get_clip_parameters,
        gfx_opengl_unload_shader,
        gfx_opengl_load_shader,
        gfx_opengl_create_and_load_new_shader,
        gfx_opengl_lookup_shader,
        gfx_opengl_shader_get_info,
        gfx_opengl_clear_shaders,
        gfx_opengl_new_texture,
        gfx_opengl_select_texture,
        gfx_opengl_upload_texture,
        gfx_opengl_set_sampler_parameters,
        gfx_opengl_set_depth_mode,
        gfx_opengl_set_depth_range,
        gfx_opengl_set_viewport,
        gfx_opengl_set_scissor,
        gfx_opengl_set_use_alpha,
        gfx_opengl_draw_triangles,
        gfx_opengl_init,
        gfx_opengl_on_resize,
        gfx_opengl_start_frame,
        gfx_opengl_end_frame,
        gfx_opengl_finish_render,
        gfx_opengl_create_framebuffer,
        gfx_opengl_update_framebuffer_parameters,
        gfx_opengl_start_draw_to_framebuffer,
        gfx_opengl_copy_framebuffer,
        gfx_opengl_clear_framebuffer,
        gfx_opengl_resolve_msaa_color_buffer,
        gfx_opengl_get_framebuffer_texture_id,
        gfx_opengl_select_texture_fb,
        gfx_opengl_delete_texture,
        gfx_opengl_set_texture_filter,
        gfx_opengl_get_texture_filter,
        gfx_opengl_set_eye_offsets,
        gfx_opengl_is_multiview,
        gfx_opengl_mirror_to_desktop,
        gfx_opengl_upload_texture_hd,
        gfx_opengl_draw_mesh,
        gfx_opengl_delete_mesh,
        gfx_opengl_scope_sees_mesh
};

// ============================================================================
// GoldenEye comfort vignette: a dark ring over both eyes while moving in stereo,
// the common VR remedy for vection sickness. Drawn last into the bound
// multiview eye FBO with its own tiny program; strength 0..1.
//
// The ring is laid out by viewing angle, not by screen position: Quest's eye
// images are off-centre (asymmetric FOV), so a ring around each image's centre
// sits in a different direction for each eye and the two never fuse (seen as a
// doubled ring). Each eye gets where "straight ahead" lands in its image - the
// same shift the game shader applies (s_eye_offsets asym_x/asym_y, from the
// eye projection's [8]/[9]) - and its NDC-per-tangent scale ([0]/[5]), so the
// fragment works in tangent space and both eyes draw one ring at infinity.
// ============================================================================
static GLuint s_vignetteProg = 0, s_vignetteVao = 0;
static GLint s_vignetteStrengthLoc = -1, s_vignetteEyeLoc = -1;
float* vr_get_eye_proj_mtx(int eye);   // vr_openxr.cpp

void gfx_opengl_draw_vignette(float strength)
{
    if (strength <= 0.001f || !use_multiview) {
        return;
    }

    if (s_vignetteProg == 0) {
        const char* vs =
            "#version 300 es\n"
            "#extension GL_OVR_multiview2 : require\n"
            "layout(num_views = 2) in;\n"
            "uniform vec4 uEye[2];\n"   // xy: NDC of straight ahead, zw: NDC per unit tangent
            "out vec2 vTan;\n"
            "void main() {\n"
            "    vec2 p = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2));\n"
            "    vec2 ndc = p * 2.0 - 1.0;\n"
            "    vec4 e = uEye[gl_ViewID_OVR];\n"
            "    vTan = (ndc - e.xy) / e.zw;\n"
            "    gl_Position = vec4(ndc, 0.0, 1.0);\n"
            "}\n";
        const char* fs =
            "#version 300 es\n"
            "precision mediump float;\n"
            "in vec2 vTan;\n"
            "uniform float uStrength;\n"
            "out vec4 outColor;\n"
            "void main() {\n"
            // radius as an angle from straight ahead: 48 deg clear at the
            // lowest strength down to 18 deg at full, fading over 22 deg
            "    float ang = atan(length(vTan));\n"
            "    float inner = mix(0.84, 0.31, uStrength);\n"
            "    float a = smoothstep(inner, inner + 0.38, ang);\n"
            "    outColor = vec4(0.0, 0.0, 0.0, a * min(1.0, uStrength * 1.5));\n"
            "}\n";
        GLuint v = glCreateShader(GL_VERTEX_SHADER);
        glShaderSource(v, 1, &vs, nullptr);
        glCompileShader(v);
        GLuint f = glCreateShader(GL_FRAGMENT_SHADER);
        glShaderSource(f, 1, &fs, nullptr);
        glCompileShader(f);
        s_vignetteProg = glCreateProgram();
        glAttachShader(s_vignetteProg, v);
        glAttachShader(s_vignetteProg, f);
        glLinkProgram(s_vignetteProg);
        glDeleteShader(v);
        glDeleteShader(f);
        GLint ok = 0;
        glGetProgramiv(s_vignetteProg, GL_LINK_STATUS, &ok);
        if (!ok) {
            char log[512] = {0};
            glGetProgramInfoLog(s_vignetteProg, sizeof(log) - 1, nullptr, log);
            vr_log("vignette: program failed: %s", log);
            glDeleteProgram(s_vignetteProg);
            s_vignetteProg = 0xffffffffu;   // do not retry every frame
            return;
        }
        s_vignetteStrengthLoc = glGetUniformLocation(s_vignetteProg, "uStrength");
        s_vignetteEyeLoc = glGetUniformLocation(s_vignetteProg, "uEye");
        glGenVertexArrays(1, &s_vignetteVao);
    }
    if (s_vignetteProg == 0xffffffffu) {
        return;
    }

    GLint prevProg = 0, prevVao = 0;
    glGetIntegerv(GL_CURRENT_PROGRAM, &prevProg);
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &prevVao);
    GLboolean blend = glIsEnabled(GL_BLEND), depth = glIsEnabled(GL_DEPTH_TEST), scissor = glIsEnabled(GL_SCISSOR_TEST);
    GLboolean depthMask = GL_TRUE;
    glGetBooleanv(GL_DEPTH_WRITEMASK, &depthMask);
    GLint vp[4];
    glGetIntegerv(GL_VIEWPORT, vp);

    glViewport(0, 0, vr_get_internal_render_width(), vr_get_internal_render_height());
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_SCISSOR_TEST);
    glDepthMask(GL_FALSE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    glUseProgram(s_vignetteProg);
    glUniform1f(s_vignetteStrengthLoc, strength);
    float eyes[8];
    for (int i = 0; i < 2; i++) {
        const float *p = vr_get_eye_proj_mtx(i);
        eyes[i * 4 + 0] = -s_eye_offsets[i * 4 + 1];
        eyes[i * 4 + 1] = -s_eye_offsets[i * 4 + 3];
        eyes[i * 4 + 2] = (p[0] > 0.01f) ? p[0] : 1.0f;
        eyes[i * 4 + 3] = (p[5] > 0.01f) ? p[5] : 1.0f;
    }
    glUniform4fv(s_vignetteEyeLoc, 2, eyes);
    glBindVertexArray(s_vignetteVao);
    glDrawArrays(GL_TRIANGLES, 0, 3);

    glBindVertexArray((GLuint)prevVao);
    glUseProgram((GLuint)prevProg);
    glViewport(vp[0], vp[1], vp[2], vp[3]);
    glDepthMask(depthMask);
    if (depth) glEnable(GL_DEPTH_TEST);
    if (scissor) glEnable(GL_SCISSOR_TEST);
    if (!blend) glDisable(GL_BLEND);
}
