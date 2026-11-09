// ============================================================================
// Virtual screen: off-screen render target
//
// The game's display list is rendered into a two-layer texture array through
// the same GL_OVR_multiview path the eye buffers use, with the per-eye offsets
// zeroed so both layers hold the same flat frame. A two-layer target rather
// than a plain 2D one keeps the number of views equal to what every game
// shader declares (layout(num_views = 2)), which is the one combination the
// extension guarantees. Layer 0 is then handed to the compositor as a quad.
// ============================================================================

#include "vr_screen.h"

#include <cstdint>
#include <cstdio>

#ifdef ANDROID
#include <GLES3/gl3.h>
#include <android/log.h>
#define SCR_LOGI(...) __android_log_print(ANDROID_LOG_INFO,  "GoldenEye-VR", __VA_ARGS__)
#define SCR_LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "GoldenEye-VR", __VA_ARGS__)
#else
#include "../fast3d/glad/glad.h"
#define SCR_LOGI(...) fprintf(stderr, __VA_ARGS__)
#define SCR_LOGE(...) fprintf(stderr, __VA_ARGS__)
#endif

typedef void (*GevrFramebufferTextureMultiviewOVRProc)(GLenum, GLenum, GLuint, GLint, GLint, GLsizei);
extern GevrFramebufferTextureMultiviewOVRProc glFramebufferTextureMultiviewOVR; // resolved by gfx_opengl.cpp

extern GLuint g_multiviewFBO; // the eye-buffer FBO, vr_openxr.cpp
extern "C" void gfx_opengl_connect_multiview_fbo(GLuint fbo_id, uint32_t width, uint32_t height);
extern "C" void gfx_opengl_connect_single_view_fbo(GLuint fbo_id, uint32_t width, uint32_t height);
extern "C" int vr_get_internal_render_width();
extern "C" int vr_get_internal_render_height();

static GLuint sFbo = 0, sColor = 0, sDepth = 0;
static int sW = 0, sH = 0;
static bool sActive = false;
/* No GL_OVR_multiview (a flat game on a GPU without it: the Android emulator,
 * ANGLE on Metal, older phones): layer 0 alone, drawn with single-view shaders */
static bool sSingle = false;

static void vr_screen_target_release(void)
{
    if (sFbo)   { glDeleteFramebuffers(1, &sFbo); sFbo = 0; }
    if (sColor) { glDeleteTextures(1, &sColor);   sColor = 0; }
    if (sDepth) { glDeleteTextures(1, &sDepth);   sDepth = 0; }
    sW = sH = 0;
}

static bool vr_screen_target_ensure(int w, int h)
{
    if (sFbo && sW == w && sH == h) {
        return true;
    }
    if (w <= 0 || h <= 0) {
        return false;
    }
    sSingle = !glFramebufferTextureMultiviewOVR;
    vr_screen_target_release();

    glGenTextures(1, &sColor);
    glBindTexture(GL_TEXTURE_2D_ARRAY, sColor);
    glTexStorage3D(GL_TEXTURE_2D_ARRAY, 1, GL_RGBA8, w, h, 2);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glGenTextures(1, &sDepth);
    glBindTexture(GL_TEXTURE_2D_ARRAY, sDepth);
    glTexStorage3D(GL_TEXTURE_2D_ARRAY, 1, GL_DEPTH24_STENCIL8, w, h, 2);
    glBindTexture(GL_TEXTURE_2D_ARRAY, 0);

    glGenFramebuffers(1, &sFbo);
    glBindFramebuffer(GL_FRAMEBUFFER, sFbo);
    if (sSingle) {
        glFramebufferTextureLayer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, sColor, 0, 0);
        glFramebufferTextureLayer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, sDepth, 0, 0);
    } else {
        glFramebufferTextureMultiviewOVR(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, sColor, 0, 0, 2);
        glFramebufferTextureMultiviewOVR(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, sDepth, 0, 0, 2);
    }

    const GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    if (status != GL_FRAMEBUFFER_COMPLETE) {
        GLint maxTex = 0, maxLayers = 0;
        glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maxTex);
        glGetIntegerv(GL_MAX_ARRAY_TEXTURE_LAYERS, &maxLayers);
        SCR_LOGE("screen: GL error 0x%x, max texture %d, max layers %d", (unsigned)glGetError(), maxTex, maxLayers);
        SCR_LOGE("screen: render target %dx%d%s incomplete (0x%x)", w, h, sSingle ? " (single view)" : " (two views)", (unsigned)status);
        vr_screen_target_release();
        return false;
    }

    sW = w;
    sH = h;
    SCR_LOGI("screen: render target %dx%d%s", w, h, sSingle ? " (single view: no GL_OVR_multiview)" : "");
    return true;
}

bool vr_screen_target_begin(int w, int h)
{
    if (!vr_screen_target_ensure(w, h)) {
        return false;
    }

    // Framebuffer 0 is what the game's display list draws into; point it at the
    // screen target for the duration of the pass.
    if (sSingle) gfx_opengl_connect_single_view_fbo(sFbo, (uint32_t)w, (uint32_t)h);
    else gfx_opengl_connect_multiview_fbo(sFbo, (uint32_t)w, (uint32_t)h);

    glBindFramebuffer(GL_FRAMEBUFFER, sFbo);
    glViewport(0, 0, w, h);
    glScissor(0, 0, w, h);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glDepthMask(GL_TRUE);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
    sActive = true;
    return true;
}

void vr_screen_target_end(void)
{
    if (!sActive) {
        return;
    }
    sActive = false;
    if (sSingle) {
        gfx_opengl_connect_single_view_fbo(0, (uint32_t)sW, (uint32_t)sH);   // the window's
    } else {
        gfx_opengl_connect_multiview_fbo(g_multiviewFBO,
                                         (uint32_t)vr_get_internal_render_width(),
                                         (uint32_t)vr_get_internal_render_height());
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

unsigned int vr_screen_target_tex(void)
{
    return sColor;
}
