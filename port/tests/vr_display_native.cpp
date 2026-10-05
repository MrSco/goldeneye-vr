#include "gevr_render_size.h"
#include "gevr_locomotion.h"
#include <openxr/openxr.h>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <limits>
#include <set>
#include <vector>

#define LOGI(...) ((void)0)
#define LOGE(...) ((void)0)
void vr_log(const char *, ...) {}
extern "C" void gevrVrLocomotionReset(void) {}
extern "C" void gevrVrLocomotionResetReason(GevrLocomotionResetReason) {}
struct Fatal {};
extern "C" __attribute__((noreturn)) void sysFatalError(const char *, ...) { throw Fatal{}; }
using GLuint = unsigned;
using GLenum = unsigned;
using GLint = int;
using GLsizei = int;
enum { GL_NO_ERROR, GL_OUT_OF_MEMORY, GL_FRAMEBUFFER_COMPLETE, GL_FRAMEBUFFER,
       GL_COLOR_ATTACHMENT0, GL_DEPTH_STENCIL_ATTACHMENT, GL_TEXTURE_BINDING_2D_ARRAY,
       GL_FRAMEBUFFER_BINDING, GL_TEXTURE_2D_ARRAY, GL_DEPTH24_STENCIL8,
       GL_DEPTH_STENCIL, GL_UNSIGNED_INT_24_8 };
struct JavaVM {};
struct ANativeWindow {};
using jobject = void *;
using EGLDisplay = void *;
using EGLContext = void *;
constexpr auto EGL_NO_DISPLAY = nullptr, EGL_NO_CONTEXT = nullptr;
EGLDisplay eglGetCurrentDisplay() { return reinterpret_cast<void *>(1); }
EGLContext eglGetCurrentContext() { return reinterpret_cast<void *>(1); }
JavaVM *g_vm = reinterpret_cast<JavaVM *>(1);
jobject g_activity = reinterpret_cast<void *>(1);
ANativeWindow *g_window = reinterpret_cast<ANativeWindow *>(1);
struct VRState {
    XrInstance instance{};
    XrSystemId systemId{};
    XrSession session{};
    XrSpace playSpace{}, viewSpace{};
    XrSwapchain swapchains[2]{};
    bool sessionRunning{};
} g_vrState;
struct Image { XrStructureType type; void *next; GLuint image; };
bool use_multiview = true, g_vrInitialized = false, g_frameStarted = false;
XrFrameState g_frameState{};
bool g_swapchainImageAcquired = false, g_swapchainImagesInit[1] = {};
std::vector<Image> g_swapchainImages[1], g_menuSwapchainImages, g_menuSwapchainImagesR,
                   g_menuSwapchainImagesH, g_menuSwapchainImagesP, g_scopeSwapchainImages[2];
XrSwapchain g_menuSwapchain{}, g_menuSwapchainR{}, g_menuSwapchainH{}, g_menuSwapchainP{}, g_scopeSwapchain[2]{};
GLuint g_multiviewFBO = 0, g_multiviewDepthArray = 0, g_currentMultiviewSwapchainTex = 0;
uint32_t g_menuSwapchainWidth = 0, g_menuSwapchainHeight = 0;
std::vector<XrTime> g_screenRecenterTimes;
int32_t g_internalRenderWidth = 0, g_internalRenderHeight = 0;
uint32_t g_systemMaxRenderWidth = 0, g_systemMaxRenderHeight = 0, g_maxRenderWidth = 0, g_maxRenderHeight = 0;
float RENDER_SCALE = 1.0f;
bool g_refreshRateSupported = true, g_refreshRatesEnumerated = false;
float g_sessionRefreshPreference = 0.0f;
int VrRefreshRate = 0;
std::vector<float> g_supportedRefreshRates;
std::vector<float> offeredRates = {72, 80, 90, 120}, requests;
XrResult enumCountResult = XR_SUCCESS, enumDataResult = XR_SUCCESS, requestResult = XR_SUCCESS;
bool procFailure = false, imageEnumerationFailure = false, depthFailure = false, fboFailure = false,
     menuFailure = false;
bool controllerFailure = false;
uint32_t imageCount = 3;
int swapchainFailAt = 0, swapchainCalls = 0;
uintptr_t nextHandle = 1;
std::set<uintptr_t> runtimeResources;
std::set<GLuint> glResources;
template <class T> T allocate() {
    uintptr_t id = nextHandle++;
    runtimeResources.insert(id);
    return reinterpret_cast<T>(id);
}
template <class T> XrResult destroy(T handle) {
    assert(runtimeResources.erase(reinterpret_cast<uintptr_t>(handle)) == 1);
    return XR_SUCCESS;
}
XrResult xrDestroySwapchain(XrSwapchain h) { return destroy(h); }
XrResult xrDestroySpace(XrSpace h) { return destroy(h); }
XrResult xrDestroySession(XrSession h) { return destroy(h); }
XrResult xrDestroyInstance(XrInstance h) { return destroy(h); }
XrResult xrEndSession(XrSession) { return XR_SUCCESS; }
XrResult xrReleaseSwapchainImage(XrSwapchain, const XrSwapchainImageReleaseInfo *) { return XR_SUCCESS; }
XrResult enumerateRates(XrSession, uint32_t capacity, uint32_t *count, float *rates) {
    *count = uint32_t(offeredRates.size());
    if (!capacity) return enumCountResult;
    if (XR_FAILED(enumDataResult)) return enumDataResult;
    assert(capacity >= offeredRates.size());
    std::copy(offeredRates.begin(), offeredRates.end(), rates);
    return XR_SUCCESS;
}
XrResult requestRate(XrSession, float rate) { requests.push_back(rate); return requestResult; }
XrResult xrGetInstanceProcAddr(XrInstance, const char *name, PFN_xrVoidFunction *fn) {
    if (procFailure) return XR_ERROR_RUNTIME_FAILURE;
    *fn = reinterpret_cast<PFN_xrVoidFunction>(std::strcmp(name, "xrEnumerateDisplayRefreshRatesFB") == 0
                                                 ? reinterpret_cast<void *>(enumerateRates)
                                                 : reinterpret_cast<void *>(requestRate));
    return XR_SUCCESS;
}
XrResult xrCreateSwapchain(XrSession, const XrSwapchainCreateInfo *, XrSwapchain *out) {
    if (++swapchainCalls == swapchainFailAt) return XR_ERROR_OUT_OF_MEMORY;
    *out = allocate<XrSwapchain>();
    return XR_SUCCESS;
}
XrResult xrEnumerateSwapchainImages(XrSwapchain h, uint32_t capacity, uint32_t *count,
                                   XrSwapchainImageBaseHeader *out) {
    if (imageEnumerationFailure) return XR_ERROR_RUNTIME_FAILURE;
    *count = imageCount;
    if (capacity)
        for (uint32_t i = 0; i < std::min(capacity, imageCount); ++i)
            reinterpret_cast<Image *>(out)[i].image = GLuint(reinterpret_cast<uintptr_t>(h) * 10 + i);
    return XR_SUCCESS;
}
GLenum pendingGlError = GL_NO_ERROR;
GLenum glGetError() { auto error = pendingGlError; pendingGlError = GL_NO_ERROR; return error; }
void glGetIntegerv(GLenum, GLint *value) { *value = 0; }
void glGenTextures(int, GLuint *out) { *out = GLuint(nextHandle++); glResources.insert(*out); }
void glGenFramebuffers(int, GLuint *out) { *out = GLuint(nextHandle++); glResources.insert(*out); }
void glDeleteTextures(int, const GLuint *id) { assert(glResources.erase(*id) == 1); }
void glDeleteFramebuffers(int, const GLuint *id) { assert(glResources.erase(*id) == 1); }
void glBindTexture(GLenum, GLuint) {}
void glBindFramebuffer(GLenum, GLuint) {}
void glTexImage3D(GLenum, int, int, int, int, int, int, GLenum, GLenum, const void *) {
    if (depthFailure) pendingGlError = GL_OUT_OF_MEMORY;
}
GLenum glCheckFramebufferStatus(GLenum) { return fboFailure ? 999 : GL_FRAMEBUFFER_COMPLETE; }
void attachMultiview(GLenum, GLenum, GLuint, GLint, GLint, GLsizei) {}
void attachMultisample(GLenum, GLenum, GLuint, GLint, GLsizei, GLint, GLsizei) {}
auto glFramebufferTextureMultiviewOVR = attachMultiview;
auto pfnFramebufferTextureMultisampleMultiviewOVR = attachMultisample;
uint32_t gfx_msaa_level = 1;
void gfx_opengl_connect_multiview_fbo(GLuint, uint32_t, uint32_t) {}
int64_t vr_pick_swapchain_format() { return 1; }
std::vector<const char *> vr_enumerate_extensions() { return {"graphics", "android"}; }
bool vr_create_instance(JavaVM *, jobject, const std::vector<const char *> &) {
    g_vrState.instance = allocate<XrInstance>(); return true;
}
bool vr_get_system() { g_vrState.systemId = 1; return true; }
bool vr_capture_egl_context() { return true; }
bool vr_verify_graphics_requirements() { return true; }
bool vr_configure_resolution() {
    g_systemMaxRenderWidth = g_systemMaxRenderHeight = g_maxRenderWidth = g_maxRenderHeight = 8192;
    auto size = gevrQuestRenderSize(1680, 1760, RENDER_SCALE, g_maxRenderWidth, g_maxRenderHeight);
    g_internalRenderWidth = size.width; g_internalRenderHeight = size.height; return true;
}
bool vr_create_session() { g_vrState.session = allocate<XrSession>(); return true; }
bool vr_create_play_space() { g_vrState.playSpace = allocate<XrSpace>(); return true; }
bool vr_create_view_space() { g_vrState.viewSpace = allocate<XrSpace>(); return true; }
void vr_setup_color_space() {}
void vr_passthrough_create() {}
void vr_passthrough_destroy() {}
bool vr_init_controllers() { return !controllerFailure; }
bool vr_create_menu_swapchain() {
    if (menuFailure) {
        g_menuSwapchain = allocate<XrSwapchain>();
        g_menuSwapchainImages.resize(3);
    }
    return !menuFailure;
}
void vr_screen_destroy_swapchain() {}
void vr_end_empty_frame(XrTime) {}
extern "C" void vr_shutdown();

/* INSERT_REFRESH */
/* INSERT_RESOURCES */
/* INSERT_LIFECYCLE */

static void clean() {
    vr_shutdown();
    assert(!g_vrInitialized && runtimeResources.empty() && glResources.empty());
    swapchainCalls = swapchainFailAt = 0;
    imageEnumerationFailure = depthFailure = fboFailure = menuFailure = false;
    controllerFailure = false; imageCount = 3;
    use_multiview = true;
}
static void mathChecks() {
    for (float scale : {0.5f, 1.0f, 2.0f, 4.0f}) {
        auto size = gevrQuestRenderSize(1680, 1760, scale, 8192, 8192);
        assert(size.width == int(1680 * scale) && size.height == int(1760 * scale));
        assert(!size.limited && !size.fallback);
    }
    for (auto dims : {std::pair<unsigned, unsigned>{0, 0}, {1, 1760}, {1680, 1}}) {
        auto size = gevrQuestRenderSize(dims.first, dims.second, 1, 8192, 8192);
        assert(size.fallback && size.width == 1832 && size.height == 1920);
    }
    auto odd = gevrQuestRenderSize(1681, 1761, 1.5f, 8192, 8192);
    assert(odd.width == 2520 && odd.height == 2640);
    for (float invalid : {std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity()}) {
        auto size = gevrQuestRenderSize(1680, 1760, invalid, 8192, 8192);
        assert(size.width == 1680 && size.height == 1760);
    }
    assert(gevrQuestRenderSize(1680, 1760, -1, 8192, 8192).width == 840);
    assert(gevrQuestRenderSize(1680, 1760, 100, 8192, 8192).width == 6720);
    auto capped = gevrQuestRenderSize(2400, 2600, 4, 8192, 8192);
    assert(capped.limited && capped.height == 8192 && capped.width == 7562);
    auto narrow = gevrQuestRenderSize(2400, 2600, 2, 2001, 8192);
    assert(narrow.limited && narrow.width == 2000 && narrow.height == 2166);
    assert(gevrQuestRenderSize(2, 2, 0.5f, 2, 2).width == 2);
    assert(gevrQuestRenderSize(1680, 1760, 1, 0, 8192).width == 0);
    auto huge = gevrQuestRenderSize(UINT32_MAX, UINT32_MAX, 4, 8192, 8192);
    assert(huge.width == 8192 && huge.height == 8192);
    const int widths[] = {916, 1832, 3664, 7328}, heights[] = {960, 1919, 3838, 7677};
    int i = 0;
    for (float scale : {0.5f, 1.0f, 2.0f, 4.0f}) {
        auto size = gevrLegacyRenderSize(1680, 1760, scale);
        assert(size.baseWidth == 1832 && size.baseHeight == 1920);
        assert(size.width == widths[i] && size.height == heights[i]); ++i;
    }
}
static void refreshChecks() {
    vr_initialize(); g_vrState.sessionRunning = true;
    vr_request_refresh_rate();
    assert(requests.empty());
    int rates[4]{};
    assert(vr_get_supported_refresh_rates(rates, 4) == 4 && rates[1] == 80);
    VrRefreshRate = 80; vr_request_refresh_rate();
    assert(requests.back() == 80 && g_sessionRefreshPreference == 80);
    VrRefreshRate = 0; requestResult = XR_ERROR_RUNTIME_FAILURE; vr_request_refresh_rate();
    assert(g_sessionRefreshPreference == 80);
    requestResult = XR_SUCCESS; vr_request_refresh_rate();
    assert(requests.back() == 0 && g_sessionRefreshPreference == 0);
    const auto count = requests.size();
    vr_request_refresh_rate(); assert(requests.size() == count);
    VrRefreshRate = 87; vr_request_refresh_rate();
    assert(VrRefreshRate == 87 && requests.size() == count);
    clean();
    offeredRates = {72, 90}; requests.clear(); VrRefreshRate = 0;
    vr_initialize(); g_vrState.sessionRunning = true;
    assert(g_sessionRefreshPreference == 0 && g_supportedRefreshRates.empty());
    enumCountResult = XR_ERROR_RUNTIME_FAILURE; vr_request_refresh_rate();
    assert(!g_refreshRatesEnumerated && requests.empty());
    enumCountResult = XR_SUCCESS; enumDataResult = XR_ERROR_RUNTIME_FAILURE; vr_request_refresh_rate();
    assert(!g_refreshRatesEnumerated);
    enumDataResult = XR_SUCCESS; vr_request_refresh_rate();
    assert(g_supportedRefreshRates.size() == 2 && requests.empty());
    VrRefreshRate = 90; procFailure = true; vr_request_refresh_rate(); assert(requests.empty());
    procFailure = false; vr_request_refresh_rate(); assert(requests.back() == 90);
    clean(); VrRefreshRate = 0;
}
static void failureChecks() {
    for (int failure = 0; failure < 7; ++failure) {
        if (failure == 0) swapchainFailAt = 1;
        if (failure == 1) { use_multiview = false; swapchainFailAt = 2; }
        if (failure == 2) imageEnumerationFailure = true;
        if (failure == 3) depthFailure = true;
        if (failure == 4) fboFailure = true;
        if (failure == 5) controllerFailure = true;
        if (failure == 6) imageCount = 0;
        bool fatal = false;
        try { vr_initialize(); } catch (Fatal &) { fatal = true; }
        assert(fatal && !g_vrInitialized && runtimeResources.empty() && glResources.empty());
        assert(!g_vrState.instance && !g_vrState.session && !g_vrState.swapchains[0]);
        clean();
    }
    menuFailure = true; vr_initialize();
    assert(g_vrInitialized && !g_menuSwapchain && g_menuSwapchainImages.empty());
    clean();
    vr_initialize(); assert(g_vrInitialized); clean();
}
int main() {
    mathChecks(); refreshChecks(); failureChecks();
    std::puts("PASS: Quest/desktop sizing, refresh transitions/errors/recreation, injected resource failures and cleanup");
}
