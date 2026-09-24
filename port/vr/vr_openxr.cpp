// ============================================================================
// VR OpenXR - Perfect Dark VR By Alex_Le_Tux
// Unified: Android (OpenGL ES + OpenXR) / Windows (OpenGL + OpenXR)
//
// Vendored from Alex-LeTux/perfect_dark_VR (branch `port`) and adapted for
// GoldenEye. MIT licensed; the upstream notice is in port/vr/LICENSE and the
// provenance and list of changes are in port/vr/README.md.
// ============================================================================

#ifdef ANDROID
#  define XR_USE_PLATFORM_ANDROID 1
#  define XR_USE_GRAPHICS_API_OPENGL_ES 1
#else
#  ifndef XR_USE_PLATFORM_WIN32
#    define XR_USE_PLATFORM_WIN32
#  endif
#  ifndef XR_USE_GRAPHICS_API_OPENGL
#    define XR_USE_GRAPHICS_API_OPENGL
#  endif
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#endif

// ============================================================================
// INCLUDES
// ============================================================================

// Standard Library
#include <cstring>
#include <cmath>
#include <vector>
#include <array>
#include <algorithm>

#ifdef ANDROID
// Android Platform
//#  include <android/log.h>
#  include <android/native_window.h>
#  include <jni.h>
// Graphics APIs
#  include <EGL/egl.h>
#  include <GLES3/gl3.h>
#else
// Windows API
#  include <windows.h>
#  include <wingdi.h>
#  include "../vr/vr_log.h"
// Graphics APIs
#  include "../fast3d/glad/glad.h"
#  include <GL/gl.h>
#  include <cstdio>
#include "vr_runtime_launcher.h"
#endif


#include "../port/fast3d/gfx_rendering_api.h"
#include "../port/fast3d/gfx_pc.h"

// OpenXR Runtime
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>

// Project
#include "vr_openxr.h"
#include "vr_input.h"
#include "vr_log.h"
#include "vr_hub.h"
#include "vr_screen.h"

extern "C" struct GfxRenderingAPI* gfx_get_current_rendering_api(void);

// ============================================================================
// LOGGING
// ============================================================================

#define XR_CHECK(call) \
do { \
    XrResult r = (call); \
    if (XR_FAILED(r)) { \
        LOGE("%s failed: %d", #call, (int)r); \
        return; \
    } \
} while (0)

#define XR_CHECK_R(call, retval) \
do { \
    XrResult r = (call); \
    if (XR_FAILED(r)) { \
        LOGE("%s failed: %d", #call, (int)r); \
        return (retval); \
    } \
} while (0)

// ============================================================================
// GLOBAL STATE - Core VR State
// ============================================================================

VRState g_vrState = {};
static bool g_vrInitialized = false;
extern "C" void vr_shutdown();

// ============================================================================
// GLOBAL STATE - others
// ============================================================================
extern bool vr_dl_is_pause_or_menu;
extern int VrIsPaused;
extern bool copy_fbo_menu;
// ============================================================================
// GLOBAL STATE - Platform-specific context
// ============================================================================

#ifdef ANDROID
extern JavaVM*        g_vm;
extern jobject        g_activity;
extern ANativeWindow* g_window;

static EGLDisplay g_sessionEglDisplay = EGL_NO_DISPLAY;
static EGLContext g_sessionEglContext  = EGL_NO_CONTEXT;
static EGLConfig  g_eglConfigForWindow = (EGLConfig)0;
#else
static HGLRC g_xr_hglrc = nullptr;
static HDC   g_xr_hdc   = nullptr;
extern SDL_Window *wnd;
extern SDL_GLContext ctx;
#endif

// ============================================================================
// GLOBAL STATE - Render Targets
// ============================================================================

static float g_eyeProjMtx[2][16] = {};
static float g_eyeViewMtx[2][16] = {};
extern float s_eye_offsets[8];
float XrFov = 0.0f;
float XrAspect = 1.0f;
float g_eyeTanHalfFov[2];
float ipd_meters = 0.0f;;
float VrStereoCrosshair = 0.70f;
extern int VrLeftHandedMode;

uint32_t VrRecommendedW = 0;
uint32_t VrRecommendedH = 0;
int VrSmallW = 0;
int VrSmallH = 0;
int32_t g_internalRenderWidth  = 0;
int32_t g_internalRenderHeight = 0;
float RENDER_SCALE = 1.0f;
extern "C" int vr_get_internal_render_width()  { return g_internalRenderWidth; }
extern "C" int vr_get_internal_render_height() { return g_internalRenderHeight; }
extern "C" s32 videoInitDisplayModes(void);

#ifdef ANDROID
extern "C"
JNIEXPORT jfloat JNICALL
Java_org_libsdl_app_SDLSurface_get_1RENDER_1SCALE(JNIEnv* env, jobject thiz) {
    return RENDER_SCALE;
}

extern "C"
JNIEXPORT jint JNICALL
Java_org_libsdl_app_SDLSurface_get_1targetW(JNIEnv* env, jobject thiz) {
    return VrRecommendedW;
}

extern "C"
JNIEXPORT jint JNICALL
Java_org_libsdl_app_SDLSurface_get_1targetH(JNIEnv* env, jobject thiz) {
    return VrRecommendedH;
}
#endif

// ============================================================================
// GLOBAL STATE - OVR_multiview
// ============================================================================
extern bool     use_multiview;
static uint32_t g_acquiredSwapchainImageIndex = 0;
// Tracks whether a swapchain image is currently checked out from the runtime, so a
// teardown that lands mid-frame can hand it back instead of destroying it underneath.
static bool     g_swapchainImageAcquired     = false;
GLuint          g_multiviewFBO               = 0;
static GLuint   g_multiviewDepthArray        = 0;
GLuint          g_currentMultiviewSwapchainTex = 0;
extern "C" void gfx_opengl_connect_multiview_fbo(GLuint fbo_id, uint32_t width, uint32_t height);

// ============================================================================
// GLOBAL STATE - Depth / scale
// ============================================================================
float g_camZNear = 10.0f;
float g_camZFar  = 10000.0f;
float vr_world_scale = 0.0f;

// ============================================================================
// GLOBAL STATE - MSAA
// ============================================================================
typedef void (*PFNGLFRAMEBUFFERTEXTUREMULTIVIEWOVRPROC)(GLenum, GLenum, GLuint, GLint, GLint, GLsizei);
extern PFNGLFRAMEBUFFERTEXTUREMULTIVIEWOVRPROC glFramebufferTextureMultiviewOVR;

typedef void (*PFNGLFRAMEBUFFERTEXTUREMULTISAMPLEMULTIVIEWOVRPROC)(GLenum, GLenum, GLuint, GLint, GLsizei, GLint, GLsizei);
extern PFNGLFRAMEBUFFERTEXTUREMULTISAMPLEMULTIVIEWOVRPROC pfnFramebufferTextureMultisampleMultiviewOVR;

extern uint32_t gfx_msaa_level;

// ============================================================================
// GLOBAL STATE - Rendering & Swapchains
// ============================================================================
#ifdef ANDROID
static bool g_swapchainImagesInit[1] = { false };
static std::vector<XrSwapchainImageOpenGLESKHR> g_swapchainImages[1];
#else
static bool g_swapchainImagesInit[1] = { false };
static std::vector<XrSwapchainImageOpenGLKHR> g_swapchainImages[1];
#endif

bool g_frameStarted = false;
static XrFrameState g_frameState   = { XR_TYPE_FRAME_STATE };
static std::array<XrView, 2> g_frameViews = { XrView{XR_TYPE_VIEW}, XrView{XR_TYPE_VIEW} };
// GoldenEye stereo: the views the game's camera was built from, and the views of
// the eye image actually in the swapchain. The game runs at 60 Hz against a
// 72+ Hz display and builds its camera a frame or more before the image is
// submitted, so declaring this XR frame's pose made the compositor reproject an
// older image as if it were current: the world jumped (stereo "flicker").
// Perfect Dark renders every XR frame and never hit it.
static std::array<XrView, 2> g_cameraViews = { XrView{XR_TYPE_VIEW}, XrView{XR_TYPE_VIEW} };
static std::array<XrView, 2> g_renderedViews = { XrView{XR_TYPE_VIEW}, XrView{XR_TYPE_VIEW} };
static bool g_haveCameraViews = false;
static bool g_haveRenderedViews = false;

static XrReferenceSpaceType gPlaySpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL;
static bool g_colorSpaceExtSupported = false;

// ============================================================================
// GLOBAL STATE - MENU Rendering & Swapchains
// ============================================================================

#ifdef ANDROID
static bool g_menuSwapchainIsSrgb = false;
#endif
static uint32_t g_menuSwapchainWidth  = 0;
static uint32_t g_menuSwapchainHeight = 0;
extern GLuint gfx_opengl_get_vr_menu_texture(void);  // Left-hand HUD texture
extern GLuint gfx_opengl_get_vr_menu_texture_R(void);// Right-hand HUD texture
extern GLuint gfx_opengl_get_vr_menu_texture_H(void);// Head HUD texture
extern bool is_weapon_hud;
float VrHudDistance = 0.8f;

// GLOBAL STATE - MENU Rendering Swapchains
static XrSwapchain g_menuSwapchain  = XR_NULL_HANDLE; // Left-hand HUD
static XrSwapchain g_menuSwapchainR = XR_NULL_HANDLE; // Right-hand HUD
static XrSwapchain g_menuSwapchainH = XR_NULL_HANDLE; // Head-locked HUD

#ifdef ANDROID
static std::vector<XrSwapchainImageOpenGLESKHR> g_menuSwapchainImages;
static std::vector<XrSwapchainImageOpenGLESKHR> g_menuSwapchainImagesR;
static std::vector<XrSwapchainImageOpenGLESKHR> g_menuSwapchainImagesH;
#else
static std::vector<XrSwapchainImageOpenGLKHR> g_menuSwapchainImages;
static std::vector<XrSwapchainImageOpenGLKHR> g_menuSwapchainImagesR;
static std::vector<XrSwapchainImageOpenGLKHR> g_menuSwapchainImagesH;
#endif

// ============================================================================
// GLOBAL STATE - Miroir layers
// ============================================================================
static XrCompositionLayerQuad g_lastMenuLayerL{};
static XrCompositionLayerQuad g_lastMenuLayerR{};
static XrCompositionLayerQuad g_lastMenuLayerH{};
static bool g_lastSubmitMenuL = false;
static bool g_lastSubmitMenuR = false;
static bool g_lastSubmitMenuH = false;
static std::array<XrView, 2> g_lastViews{};
extern "C" int gfx_sdl_get_mirror_eye();

// ============================================================================
// HEAD TRACKING - State
// ============================================================================

XrQuaternionf vr_HMD_rot_Q = { 0, 0, 0, 1 };
XrVector3f gHeadPos = { 0, 0, 0 };
bool positionValid = false;
bool orientationValid = false;

XrQuaternionf gRawHeadQ = { 0, 0, 0, 1 };
float g_yawOffsetDegrees = 0.0f;

// ============================================================
// FLOOR-REFERENCED HEAD HEIGHT
// ============================================================
// LOCAL_FLOOR/STAGE put the origin on the physical floor, so the runtime's Y is
// already the true head height and nothing has to be calibrated or remembered.
// A plain LOCAL space puts the origin wherever the head was when the space was
// created, so there we calibrate once per level: the median head Y over ~1 s is
// taken to be a standing pose. Median, not maximum -- a running maximum latches
// onto a physical jump and never comes back down, which is what used to leave
// the player permanently shorter for the rest of the session.
bool  gVrFloorRelativeSpace = false;
float gVrHeadHeightCm       = VR_NOMINAL_HEAD_HEIGHT_CM;

extern "C" float VrPlayerHeight;   // your standing height, cm (bondwalk.c)

#define VR_HEIGHT_CALIB_SAMPLES 90   // ~1 s of frames

static float sHeightCalibSamples[VR_HEIGHT_CALIB_SAMPLES];
static int   sHeightCalibCount  = 0;
static bool  sHeightCalibDone   = false;
static float sHeightCalibOffset = VR_NOMINAL_HEAD_HEIGHT_CM;

// The recenter alone, as a quaternion: play space -> recentered play space.
// vr_HMD_rot_Q is this times gRawHeadQ, so anything that starts from a vector
// already expressed in play space wants THIS, not vr_HMD_rot_Q -- applying the
// latter would fold the head yaw in a second time.
XrQuaternionf vr_recenter_rot_Q = { 0, 0, 0, 1 };

// Head linear velocity in play space, raw OpenXR axes, m/s. Subtract it from a
// controller's play-space velocity to get motion relative to the body.
float vr_head_velocity_play[3] = { 0.0f, 0.0f, 0.0f };

// ============================================================
// SMOOTHING HMD — When zoom is enabled
// ============================================================
#define HMD_SMOOTH_ALPHA_POS  0.05f   // Position
#define HMD_SMOOTH_ALPHA_ROT  0.05f   // Rotation

static float          sSmoothedHeadPos[3]  = {0, 0, 0};
static XrQuaternionf  sSmoothedHeadQ       = {0, 0, 0, 1};
static bool           sSmoothedHeadInit    = false;
extern void QuatSlerp(const float a[4], const float b[4], float t, float out[4]);
extern int vr_button_R_grip;
extern int vr_button_L_grip;
extern bool WepCanZoom;
// ============================================================================
// vr_is_initialized
// ============================================================================

extern "C" bool vr_is_initialized() {
    return g_vrInitialized;
}

// ============================================================================
// OpenXR Empty Frame
// ============================================================================

static inline void vr_end_empty_frame(XrTime t)
{
    XrFrameEndInfo endInfo{ XR_TYPE_FRAME_END_INFO };
    endInfo.displayTime            = t;
    endInfo.environmentBlendMode   = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
    endInfo.layerCount             = 0;
    endInfo.layers                 = nullptr;
    xrEndFrame(g_vrState.session, &endInfo);
}

// ============================================================================
// PC : Detect Runtime Steam VR / Meta oculus
// ============================================================================

enum class XrRuntimeType {
    Unknown,
    MetaOculusQuest,  // "Oculus" runtimeName
    SteamVR,          // "SteamVR" runtimeName
    VirtualDesktopXR, // "VirtualDesktopXR" runtimeName
};

static XrRuntimeType gActiveRuntime = XrRuntimeType::Unknown;
bool is_meta_runtime = false;
// GoldenEye: the curved virtual screen is a cylinder layer (VrScreenCurved).
static bool g_cylinderSupported = false;
static bool g_refreshRateSupported = false;
/*
 * Display refresh rate (VrRefreshRate, goldeneye-vr.ini; 0 = the runtime's).
 * The game runs at 60 Hz. At the default 72 Hz every sixth displayed frame
 * repeats a game frame; head turns are reprojected, but anything moving on
 * its own - the hands and guns above all - judders at that 12 Hz beat. At
 * 120 Hz each game frame is shown exactly twice.
 */
extern int VrRefreshRate;
static void vr_request_refresh_rate(void)
{
    if (!g_refreshRateSupported || VrRefreshRate <= 0) {
        return;
    }
    PFN_xrEnumerateDisplayRefreshRatesFB enumRates = nullptr;
    PFN_xrRequestDisplayRefreshRateFB request = nullptr;
    xrGetInstanceProcAddr(g_vrState.instance, "xrEnumerateDisplayRefreshRatesFB", (PFN_xrVoidFunction *)&enumRates);
    xrGetInstanceProcAddr(g_vrState.instance, "xrRequestDisplayRefreshRateFB", (PFN_xrVoidFunction *)&request);
    if (!enumRates || !request) {
        return;
    }
    uint32_t n = 0;
    enumRates(g_vrState.session, 0, &n, nullptr);
    std::vector<float> rates(n);
    enumRates(g_vrState.session, n, &n, rates.data());
    bool have = false;
    for (float r : rates) {
        LOGI("display: %.0f Hz available", r);
        if (fabsf(r - (float)VrRefreshRate) < 0.5f) have = true;
    }
    if (have) {
        XrResult res = request(g_vrState.session, (float)VrRefreshRate);
        LOGI("display: requested %d Hz (%d)", VrRefreshRate, (int)res);
    } else {
        LOGI("display: %d Hz not offered, keeping the default", VrRefreshRate);
    }
}

// XR_SESSION_STATE_FOCUSED: the game has the controllers (not the system menu).
static bool g_sessionFocused = false;
extern "C" int gevrVrSessionFocused(void) { return g_sessionFocused ? 1 : 0; }

/*
 * The game links its own atan2f (src/game/math_atan2f.c, range 0..2pi, as on
 * the N64), acosf and asinf into this library, and they replace libm's for the
 * VR code too. Use the double versions, which it does not define: with the
 * game's atan2f every point left of centre on the curved screen came out near
 * 2pi, far off the edge, and the pointer's dot vanished there.
 */
static inline float vr_atan2f(float y, float x) { return (float)atan2((double)y, (double)x); }
static inline float vr_asinf(float s) { return (float)asin((double)s); }
extern "C" int vr_screen_curve_supported(void) { return g_cylinderSupported ? 1 : 0; }

static void vr_detect_runtime() {
    XrInstanceProperties props{XR_TYPE_INSTANCE_PROPERTIES};
    if (XR_SUCCEEDED(xrGetInstanceProperties(g_vrState.instance, &props))) {
        LOGI("OpenXR Runtime: %s (ver %u.%u.%u)",
             props.runtimeName,
             XR_VERSION_MAJOR(props.runtimeVersion),
             XR_VERSION_MINOR(props.runtimeVersion),
             XR_VERSION_PATCH(props.runtimeVersion));

        if (strstr(props.runtimeName, "Oculus")) {
            gActiveRuntime = XrRuntimeType::MetaOculusQuest;
            is_meta_runtime = true;
            LOGI("Detected runtime: Meta/Oculus");
        } else if (strstr(props.runtimeName, "VirtualDesktopXR")){
            gActiveRuntime = XrRuntimeType::VirtualDesktopXR;
            is_meta_runtime = false;
            LOGI("Detected runtime: VirtualDesktopXR");
        } else if (strstr(props.runtimeName, "SteamVR")){
            gActiveRuntime = XrRuntimeType::SteamVR;
            is_meta_runtime = false;
            LOGI("Detected runtime: SteamVR");
        }else{
            gActiveRuntime = XrRuntimeType::SteamVR;
            is_meta_runtime = false;
            LOGI("Detected runtime: UNKNOWN, set to SteamVR by default");
        }
    }
}

// ============================================================================
// EXTENSIONS - OpenXR Instance Setup
// ============================================================================

static std::vector<const char*> vr_enumerate_extensions()
{
    LOGI("Querying available OpenXR extensions");

    uint32_t extensionCount = 0;
    xrEnumerateInstanceExtensionProperties(nullptr, 0, &extensionCount, nullptr);

    std::vector<XrExtensionProperties> extensionProperties(
            extensionCount, { XR_TYPE_EXTENSION_PROPERTIES }
    );
    xrEnumerateInstanceExtensionProperties(
            nullptr, extensionCount, &extensionCount, extensionProperties.data()
    );

    LOGI("Found %u OpenXR extensions", extensionCount);

    std::vector<const char*> enabledExts;

#ifdef ANDROID
    const char* requiredExts[] = {
            "XR_KHR_android_create_instance",
            "XR_KHR_opengl_es_enable"
    };
#else
    const char* requiredExts[] = {
        "XR_KHR_opengl_enable"
    };
#endif

    for (const auto& ext : extensionProperties) {
        for (const char* required : requiredExts) {
            if (std::strcmp(ext.extensionName, required) == 0) {
                enabledExts.push_back(required);
                LOGI("Extension enabled: %s", required);
            }
        }
        if (std::strcmp(ext.extensionName, "XR_EXT_local_floor") == 0) {
            enabledExts.push_back("XR_EXT_local_floor");
        }
        if (std::strcmp(ext.extensionName, XR_FB_DISPLAY_REFRESH_RATE_EXTENSION_NAME) == 0) {
            enabledExts.push_back(XR_FB_DISPLAY_REFRESH_RATE_EXTENSION_NAME);
            g_refreshRateSupported = true;
            LOGI("Extension enabled: %s", XR_FB_DISPLAY_REFRESH_RATE_EXTENSION_NAME);
        }
        if (std::strcmp(ext.extensionName, XR_KHR_COMPOSITION_LAYER_CYLINDER_EXTENSION_NAME) == 0) {
            enabledExts.push_back(XR_KHR_COMPOSITION_LAYER_CYLINDER_EXTENSION_NAME);
            g_cylinderSupported = true;
            LOGI("Extension enabled: %s", XR_KHR_COMPOSITION_LAYER_CYLINDER_EXTENSION_NAME);
        }
        if (std::strcmp(ext.extensionName, "XR_FB_color_space") == 0) {
            enabledExts.push_back("XR_FB_color_space");
            g_colorSpaceExtSupported = true;
            LOGI("Extension enabled: XR_FB_color_space");
        }
    }

    return enabledExts;
}

// ============================================================================
// INSTANCE - OpenXR Instance Creation
// ============================================================================

#ifdef ANDROID
static bool vr_create_instance(JavaVM* vm, jobject activity, const std::vector<const char*>& extensions)
#else
static bool vr_create_instance(const std::vector<const char*>& extensions)
#endif
{
    LOGI("Creating OpenXR instance");

#ifdef ANDROID
    if (extensions.size() < 2) {
        LOGE("Missing required extensions (need at least 2)");
        return false;
    }
    XrInstanceCreateInfoAndroidKHR androidInfo{ XR_TYPE_INSTANCE_CREATE_INFO_ANDROID_KHR };
    androidInfo.applicationVM       = vm;
    androidInfo.applicationActivity = activity;
#else
    if (extensions.empty()) {
        LOGE("No required OpenXR extensions found");
        return false;
    }
#endif

    XrInstanceCreateInfo ci{ XR_TYPE_INSTANCE_CREATE_INFO };
#ifdef ANDROID
    ci.next = &androidInfo;
#endif
    std::strncpy(ci.applicationInfo.applicationName, "GoldenEye", XR_MAX_APPLICATION_NAME_SIZE - 1);
    ci.applicationInfo.apiVersion    = XR_API_VERSION_1_0;
    ci.enabledExtensionCount         = (uint32_t)extensions.size();
    ci.enabledExtensionNames         = extensions.data();


    LOGI("vrcreateinstance: gvrState.instance = %p", (void*)(uintptr_t)g_vrState.instance);

    if (g_vrState.instance != XR_NULL_HANDLE) {
        LOGE("vrcreateinstance: instance already exists! (%p) — BUG", (void*)g_vrState.instance);
        return false;
    }
    XrResult result = xrCreateInstance(&ci, &g_vrState.instance);
    if (XR_FAILED(result)) {
        LOGE("xrCreateInstance failed: %d", (int)result);
        return false;
    }

    LOGI("OpenXR instance created successfully");
#ifndef ANDROID
    vr_detect_runtime();
#endif

    return true;
}

// ============================================================================
// SYSTEM - HMD System Detection
// ============================================================================

static bool vr_get_system()
{
    LOGI("Querying HMD system");
    XrSystemGetInfo sysInfo{XR_TYPE_SYSTEM_GET_INFO};
    sysInfo.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;

    XrResult result = xrGetSystem(g_vrState.instance, &sysInfo, &g_vrState.systemId);
    if (XR_FAILED(result)) {
        if (result == XR_ERROR_FORM_FACTOR_UNAVAILABLE)
            LOGE("HMD not connected or runtime not ready");
        LOGE("xrGetSystem failed: %d", (int)result);
        return false;
    }
    if (g_vrState.systemId == 0) {
        LOGE("No HMD system found!");
        return false;
    }
    LOGI("System found: %llu", (unsigned long long)g_vrState.systemId);

    // --- Retrieve HMD model ---
    XrSystemProperties sysProps{XR_TYPE_SYSTEM_PROPERTIES};
    XrResult propResult = xrGetSystemProperties(g_vrState.instance, g_vrState.systemId, &sysProps);
    if (XR_SUCCEEDED(propResult)) {
        LOGI("HMD model: %s (vendorId=%u)", sysProps.systemName, sysProps.vendorId);
        LOGI("Max swapchain: %u x %u, max layers: %u",
             sysProps.graphicsProperties.maxSwapchainImageWidth,
             sysProps.graphicsProperties.maxSwapchainImageHeight,
             sysProps.graphicsProperties.maxLayerCount);
    } else {
        LOGE("xrGetSystemProperties failed: %d", (int)propResult);
    }

    return true;
}




// ============================================================================
// RESOLUTION - View Configuration & Target Resolution
// ============================================================================

#if 0
struct vimode {
    s32 fbwidth;
    s32 fbheight;
    s32 width;
    f32 yscale;
    s32 xscale;
    s32 fullheight;
    s32 fulltop;
    s32 wideheight;
    s32 widetop;
    s32 cinemaheight;
    s32 cinematop;
};

extern struct vimode g_ViModes[6];
#else
extern OSViMode g_ViModes[2];
#endif

extern "C" bool vr_configure_resolution() {
    uint32_t viewCount = 0;
    XrResult r = xrEnumerateViewConfigurationViews(
            g_vrState.instance,
            g_vrState.systemId,
            XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,
            0, &viewCount, nullptr);

    if (XR_FAILED(r) || viewCount == 0) {
        LOGE("xrEnumerateViewConfigurationViews (count) failed: %d", (int)r);
        return false;
    }

    std::vector<XrViewConfigurationView> views(
            viewCount, { XR_TYPE_VIEW_CONFIGURATION_VIEW });

    r = xrEnumerateViewConfigurationViews(
            g_vrState.instance,
            g_vrState.systemId,
            XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,
            viewCount, &viewCount, views.data());

    if (XR_FAILED(r)) {
        LOGE("xrEnumerateViewConfigurationViews (data) failed: %d", (int)r);
        return false;
    }

// Both eyes normally have the same recommended resolution
    int VrRealRecommendedW = views[0].recommendedImageRectWidth;
    int VrRealRecommendedH = views[0].recommendedImageRectHeight;
    LOGI("HMD recommended resolution: %u x %u", VrRealRecommendedW, VrRealRecommendedH);

    // --- Calculate the actual HMD aspect ratio ---
    double aspectRatio = (double)VrRealRecommendedW / (double)VrRealRecommendedH;
    XrAspect = (float)aspectRatio;
    LOGI("HMD aspect ratio: %.4f", XrAspect);

    // --- Fixed width enforced, height derived from aspect ratio ---
    constexpr uint32_t VR_FIXED_WIDTH = 1832;

    VrRecommendedW = VR_FIXED_WIDTH & ~1u; // ensures an even width too, for consistency
    uint32_t derivedH = (uint32_t)std::lround((double)VrRecommendedW / XrAspect);
    VrRecommendedH = (derivedH + 1u) & ~1u; // rounds up to the nearest even number, instead of truncating down

    // Get correct aspect ratio and size for HUD VR
    // Add horizontal overscan (~35%) to compensate for the Quest 3's strong "cantingOffset".
    // This allows the 2D HUD to physically extend beyond the screen and cover the
    // empty areas on the sides when shifted by parallax.
    float overscan = 1.0f;
    VrSmallW = (VrRecommendedW / 4.5f) * overscan;
    VrSmallH = VrRecommendedH / 4.5f;

    LOGI("VR small resolution (fixed W, derived H): %u x %u", VrSmallW, VrSmallH);


    // Set correct aspect ratio and size for VR
    g_ViModes[0].comRegs.width = (u32)VrSmallW;
    g_ViModes[1].comRegs.width = (u32)VrSmallW;

    // --- Internal render resolution: keep the same aspect ratio, fixed width ---
    g_internalRenderWidth = (uint32_t)(VR_FIXED_WIDTH * RENDER_SCALE);
    g_internalRenderHeight = (uint32_t)std::lround(g_internalRenderWidth / XrAspect);
    LOGI("HMD g_internalRenderWidth/Height resolution: %u x %u", g_internalRenderWidth, g_internalRenderHeight);

    // Set real VR resolution in Display list
    videoInitDisplayModes();

#ifdef ANDROID
    // Set android surface with/height
    JNIEnv* env = (JNIEnv*)SDL_AndroidGetJNIEnv();
    jobject activity = (jobject)SDL_AndroidGetActivity();
    jclass clazz = env->GetObjectClass(activity);
    jmethodID method_id = env->GetStaticMethodID(clazz, "triggerResize", "()V");
    env->CallStaticVoidMethod(clazz, method_id);
    env->DeleteLocalRef(activity);
#else
    // Set windows with/height
    SDL_GL_MakeCurrent(wnd, ctx);
    SDL_SetWindowSize(wnd, g_internalRenderWidth, g_internalRenderHeight);
#endif

    return true;
}

// ============================================================================
// PLATFORM CONTEXT - Capture and Verify
// ============================================================================

#ifdef ANDROID
static bool vr_capture_egl_context()
{
    LOGI("Capturing EGL context");

    EGLDisplay display = eglGetCurrentDisplay();
    EGLContext context = eglGetCurrentContext();

    if (display == EGL_NO_DISPLAY || context == EGL_NO_CONTEXT) {
        LOGE("No current EGL context!");
        return false;
    }

    g_sessionEglDisplay = display;
    g_sessionEglContext = context;
    LOGI("EGL: display=%p context=%p", display, context);

    EGLint configId = 0;
    eglQueryContext(display, context, EGL_CONFIG_ID, &configId);

    EGLint configAttribs[] = { EGL_CONFIG_ID, configId, EGL_NONE };
    EGLConfig config = nullptr;
    EGLint numConfigs = 0;
    eglChooseConfig(display, configAttribs, &config, 1, &numConfigs);

    if (numConfigs <= 0 || config == nullptr) {
        LOGE("eglChooseConfig failed");
        return false;
    }

    g_eglConfigForWindow = config;
    LOGI("EGL config: %p", config);

    EGLint glVersion;
    eglQueryContext(display, context, EGL_CONTEXT_CLIENT_VERSION, &glVersion);
    LOGI("EGL context version: %d", glVersion);

    return true;
}
#endif // ANDROID

// ============================================================================
// GRAPHICS REQUIREMENTS - Verify Support
// ============================================================================

static bool vr_verify_graphics_requirements()
{
    LOGI("Verifying graphics requirements");

#ifdef ANDROID
    PFN_xrGetOpenGLESGraphicsRequirementsKHR xrGetGLReq = nullptr;
    XrResult pr = xrGetInstanceProcAddr(
            g_vrState.instance,
            "xrGetOpenGLESGraphicsRequirementsKHR",
            (PFN_xrVoidFunction*)&xrGetGLReq
    );
    if (XR_FAILED(pr) || !xrGetGLReq) {
        LOGE("xrGetInstanceProcAddr(xrGetOpenGLESGraphicsRequirementsKHR) failed: %d", (int)pr);
        return false;
    }
    XrGraphicsRequirementsOpenGLESKHR reqs{ XR_TYPE_GRAPHICS_REQUIREMENTS_OPENGL_ES_KHR };
    XrResult reqR = xrGetGLReq(g_vrState.instance, g_vrState.systemId, &reqs);
    if (XR_FAILED(reqR)) {
        LOGE("xrGetOpenGLESGraphicsRequirementsKHR failed: %d", (int)reqR);
        return false;
    }
    LOGI("GLES requirements OK: minApi=0x%x maxApi=0x%x",
         (unsigned)reqs.minApiVersionSupported, (unsigned)reqs.maxApiVersionSupported);
#else
    PFN_xrGetOpenGLGraphicsRequirementsKHR xrGetGLReq = nullptr;
    XrResult pr = xrGetInstanceProcAddr(
        g_vrState.instance,
        "xrGetOpenGLGraphicsRequirementsKHR",
        (PFN_xrVoidFunction*)&xrGetGLReq
    );
    if (XR_FAILED(pr) || !xrGetGLReq) {
        LOGE("xrGetInstanceProcAddr(xrGetOpenGLGraphicsRequirementsKHR) failed: %d", (int)pr);
        return false;
    }
    XrGraphicsRequirementsOpenGLKHR reqs{ XR_TYPE_GRAPHICS_REQUIREMENTS_OPENGL_KHR };
    XrResult reqR = xrGetGLReq(g_vrState.instance, g_vrState.systemId, &reqs);
    if (XR_FAILED(reqR)) {
        LOGE("xrGetOpenGLGraphicsRequirementsKHR failed: %d", (int)reqR);
        return false;
    }
    LOGI("OpenGL requirements OK: minApi=0x%llx maxApi=0x%llx",
        (unsigned long long)reqs.minApiVersionSupported, (unsigned long long)reqs.maxApiVersionSupported);
#endif

    return true;
}

// ============================================================================
// SESSION - OpenXR Session Creation
// ============================================================================

static bool vr_create_session()
{
    LOGI("Creating OpenXR session");

#ifdef ANDROID
    XrGraphicsBindingOpenGLESAndroidKHR binding{ XR_TYPE_GRAPHICS_BINDING_OPENGL_ES_ANDROID_KHR };
    binding.display = g_sessionEglDisplay;
    binding.config  = g_eglConfigForWindow;
    binding.context = g_sessionEglContext;
#else
    HDC   hdc   = wglGetCurrentDC();
    HGLRC hglrc = wglGetCurrentContext();
    if (!hdc || !hglrc) {
        LOGE("No current GL context");
        return false;
    }
    g_xr_hdc   = hdc;
    g_xr_hglrc = hglrc;

    XrGraphicsBindingOpenGLWin32KHR binding{ XR_TYPE_GRAPHICS_BINDING_OPENGL_WIN32_KHR };
    binding.hDC   = hdc;
    binding.hGLRC = hglrc;
#endif

    XrSessionCreateInfo sessionInfo{ XR_TYPE_SESSION_CREATE_INFO };
    sessionInfo.next     = &binding;
    sessionInfo.systemId = g_vrState.systemId;

    XrResult result = xrCreateSession(g_vrState.instance, &sessionInfo, &g_vrState.session);
    if (XR_FAILED(result)) {
        LOGE("xrCreateSession failed: %d", (int)result);
        return false;
    }

    LOGI("Session created successfully: %p", (void*)g_vrState.session);
    return true;
}

// ============================================================================
// COLOR SPACE - Declare sRGB content to the OpenXR runtime
// ============================================================================

static void vr_setup_color_space()
{
    if (!g_colorSpaceExtSupported) {
        LOGI("XR_FB_color_space not supported, skipping");
        return;
    }

    PFN_xrSetColorSpaceFB pfnSetColorSpaceFB = nullptr;
    XrResult r = xrGetInstanceProcAddr(
            g_vrState.instance,
            "xrSetColorSpaceFB",
            (PFN_xrVoidFunction*)&pfnSetColorSpaceFB
    );
    if (XR_FAILED(r) || pfnSetColorSpaceFB == nullptr) {
        LOGE("xrGetInstanceProcAddr(xrSetColorSpaceFB) failed: %d", (int)r);
        return;
    }

    XrResult cr = pfnSetColorSpaceFB(g_vrState.session, XR_COLOR_SPACE_REC709_FB);
    if (XR_FAILED(cr)) {
        LOGE("xrSetColorSpaceFB failed: %d", (int)cr);
    } else {
        LOGI("Color space set to XR_COLOR_SPACE_REC709_FB (sRGB)");
    }
}

// ============================================================================
// SPACE - Reference Space Creation
// ============================================================================

static bool vr_create_play_space()
{
    LOGI("Creating play space");

    uint32_t count = 0;
    XR_CHECK_R(xrEnumerateReferenceSpaces(g_vrState.session, 0, &count, nullptr), false);

    std::vector<XrReferenceSpaceType> types(count);
    XR_CHECK_R(xrEnumerateReferenceSpaces(g_vrState.session, count, &count, types.data()), false);

    auto has = [&](XrReferenceSpaceType t) {
        return std::find(types.begin(), types.end(), t) != types.end();
    };

    if (has(XR_REFERENCE_SPACE_TYPE_LOCAL_FLOOR_EXT)) {
        gPlaySpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL_FLOOR_EXT;
        LOGI("Play space = LOCAL_FLOOR");
    } else if (has(XR_REFERENCE_SPACE_TYPE_STAGE)) {
        gPlaySpaceType = XR_REFERENCE_SPACE_TYPE_STAGE;
        LOGI("Play space = STAGE");
    } else {
        gPlaySpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL;
        LOGI("Play space = LOCAL");
    }

    // Only the first two report Y relative to the physical floor; LOCAL needs
    // the one-shot height calibration instead.
    gVrFloorRelativeSpace = (gPlaySpaceType != XR_REFERENCE_SPACE_TYPE_LOCAL);
    vr_recalibrate_head_height();

    XrReferenceSpaceCreateInfo spaceInfo{ XR_TYPE_REFERENCE_SPACE_CREATE_INFO };
    spaceInfo.referenceSpaceType            = gPlaySpaceType;
    spaceInfo.poseInReferenceSpace.orientation = { 0, 0, 0, 1 };
    spaceInfo.poseInReferenceSpace.position    = { 0, 0, 0 };

    XrResult result = xrCreateReferenceSpace(g_vrState.session, &spaceInfo, &g_vrState.playSpace);
    if (XR_FAILED(result)) {
        LOGE("xrCreateReferenceSpace(playSpace) failed: %d", (int)result);
        return false;
    }

    LOGI("Play space type = %d", (int)gPlaySpaceType);
    return true;
}

static bool vr_create_view_space()
{
    LOGI("Creating view space (head)");

    XrReferenceSpaceCreateInfo spaceInfo{ XR_TYPE_REFERENCE_SPACE_CREATE_INFO };
    spaceInfo.referenceSpaceType               = XR_REFERENCE_SPACE_TYPE_VIEW;
    spaceInfo.poseInReferenceSpace.orientation = { 0, 0, 0, 1 };
    spaceInfo.poseInReferenceSpace.position    = { 0, 0, 0 };

    XrResult result = xrCreateReferenceSpace(g_vrState.session, &spaceInfo, &g_vrState.viewSpace);

    if (XR_FAILED(result)) {
        LOGE("xrCreateReferenceSpace(viewSpace) failed: %d", (int)result);
        return false;
    }

    LOGI("View space created: %p", (void*)g_vrState.viewSpace);
    return true;
}

// ============================================================================
// SWAPCHAINS - Format Selection
// ============================================================================

static bool vr_format_supported(const std::vector<int64_t>& formats, int64_t fmt) {
    for (int64_t f : formats) if (f == fmt) return true;
    return false;
}

static int64_t vr_pick_swapchain_format(bool forQuadLayer = false)
{
    uint32_t count = 0;
    XrResult r0 = xrEnumerateSwapchainFormats(g_vrState.session, 0, &count, nullptr);
    if (XR_FAILED(r0) || count == 0) {
        LOGE("xrEnumerateSwapchainFormats(count) failed r=%d count=%u", (int)r0, count);
        return (int64_t)GL_RGBA8;
    }

    std::vector<int64_t> formats(count);
    XrResult r1 = xrEnumerateSwapchainFormats(g_vrState.session, count, &count, formats.data());
    if (XR_FAILED(r1) || count == 0) {
        LOGE("xrEnumerateSwapchainFormats(data) failed r=%d count=%u", (int)r1, count);
        return (int64_t)GL_RGBA8;
    }

    for (uint32_t i = 0; i < count; i++) {
        LOGI("supported[%u] = 0x%llx (%lld)", i,
             (unsigned long long)formats[i], (long long)formats[i]);
    }

#ifdef ANDROID
    /*
     * The game's colours are already display-encoded (sRGB). The compositor
     * reads a UNORM swapchain (GL_RGBA8) as linear and encodes it again on
     * output: measured on device, a 128 grey came out as 188 - the whole game
     * too bright. An sRGB swapchain is decoded correctly, which is also what
     * the desktop build (below) and the framebuffer-effect blit's
     * linear_to_srgb assume. GLES encodes every write to an sRGB target unless
     * EXT_sRGB_write_control switches that off, so take the sRGB format only
     * when the extension is there (then writes are raw), and fall back to the
     * old order without it.
     */
    static int srgbWriteControl = -1;
    if (srgbWriteControl < 0) {
        const char *ext = (const char *)glGetString(GL_EXTENSIONS);
        srgbWriteControl = ext && strstr(ext, "GL_EXT_sRGB_write_control") ? 1 : 0;
        LOGI("sRGB write control %s", srgbWriteControl ? "available" : "missing");
    }
    if (srgbWriteControl && vr_format_supported(formats, (int64_t)GL_SRGB8_ALPHA8)) {
        glDisable(0x8DB9 /* GL_FRAMEBUFFER_SRGB_EXT */);
        LOGI("picked format=0x%llx (sRGB, writes raw)", (unsigned long long)GL_SRGB8_ALPHA8);
        return (int64_t)GL_SRGB8_ALPHA8;
    }
    // Without sRGB write control, upstream's order: eyes RGBA8 first, quads sRGB first.
    // Yeux (projection layer) : inchangé, GL_RGBA8 en premier.
    static const int64_t prefEyes[] = {
            (int64_t)GL_RGBA8,
            (int64_t)GL_RGB10_A2,
            (int64_t)GL_RGBA16F,
            (int64_t)GL_SRGB8_ALPHA8,
    };

    // Quads (menu / HUD armes) : GL_SRGB8_ALPHA8 en premier, sinon le compositeur
    // Quest ré-encode des valeurs déjà en gamma => couleurs délavées.
    static const int64_t prefQuad[] = {
            (int64_t)GL_SRGB8_ALPHA8,
            (int64_t)GL_RGBA8,
            (int64_t)GL_RGB10_A2,
            (int64_t)GL_RGBA16F,
    };

    const int64_t* preferred = forQuadLayer ? prefQuad : prefEyes;
#else
    // PC : inchangé (le paramètre forQuadLayer n'a aucun effet).
    static const int64_t prefPC[] = {
            (int64_t)GL_SRGB8_ALPHA8,
            (int64_t)GL_RGBA8,
            (int64_t)GL_RGB10_A2,
            (int64_t)GL_RGBA16F,
    };

    const int64_t* preferred = prefPC;
#endif

    const size_t preferredCount = 4;

    for (size_t i = 0; i < preferredCount; i++) {
        const int64_t fmt = preferred[i];
        if (vr_format_supported(formats, fmt)) {
            LOGI("picked format=0x%llx (%lld)%s",
                 (unsigned long long)fmt, (long long)fmt,
                 forQuadLayer ? " [quad]" : " [eye]");
            return fmt;
        }
    }

    LOGI("no preferred format found, using first supported=0x%llx (%lld)",
         (unsigned long long)formats[0], (long long)formats[0]);
    return formats[0];
}

// ============================================================================
// SWAPCHAINS - Texture Creation
// ============================================================================

static bool vr_create_swapchains()
{
    const int64_t chosenFormat = vr_pick_swapchain_format();

    XrSwapchainCreateInfo swapInfo{ XR_TYPE_SWAPCHAIN_CREATE_INFO };
    swapInfo.usageFlags  = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT | XR_SWAPCHAIN_USAGE_SAMPLED_BIT;
    swapInfo.format      = chosenFormat;
    swapInfo.sampleCount = 1;
    swapInfo.width       = g_internalRenderWidth;
    swapInfo.height      = g_internalRenderHeight;
    swapInfo.faceCount   = 1;
    swapInfo.arraySize   = use_multiview ? 2 : 1;
    swapInfo.mipCount    = 1;

    // Under multiview both eyes are array layers of a single swapchain, so only [0] is ever
    // acquired, submitted or enumerated -- a second one was a full extra set of eye buffers
    // that nothing ever read. At a 3.0 render scale that is ~735 MiB of dead VRAM.
    const int swapchainCount = use_multiview ? 1 : 2;

    for (int eye = 0; eye < swapchainCount; eye++) {
        XrResult result = xrCreateSwapchain(g_vrState.session, &swapInfo, &g_vrState.swapchains[eye]);
        if (XR_FAILED(result)) {
            LOGE("xrCreateSwapchain eye=%d failed: %d (format=0x%llx)",
                 eye, (int)result, (unsigned long long)chosenFormat);
            return false;
        }

        uint32_t imageCount = 0;
        XrResult countResult = xrEnumerateSwapchainImages(g_vrState.swapchains[eye], 0, &imageCount, nullptr);
        if (XR_FAILED(countResult)) {
            LOGE("xrEnumerateSwapchainImages(count) eye=%d failed: %d", eye, (int)countResult);
            return false;
        }

        LOGI("Swapchain eye=%d created (format=0x%llx) images=%u",
             eye, (unsigned long long)chosenFormat, imageCount);
    }

    return true;
}


// ============================================================================
// SWAPCHAINS - MENU Texture Creation
// ============================================================================
static bool vr_create_menu_swapchain()
{
    g_menuSwapchainWidth  = (uint32_t)g_internalRenderWidth;
    g_menuSwapchainHeight = (uint32_t)g_internalRenderHeight;

    // Quad layers: sRGB format preferred on Android (see vr_pick_swapchain_format)
    const int64_t chosenFormat = vr_pick_swapchain_format(/*forQuadLayer=*/true);
#ifdef ANDROID
    g_menuSwapchainIsSrgb = (chosenFormat == (int64_t)GL_SRGB8_ALPHA8);
#endif

    XrSwapchainCreateInfo swapchainInfo = {XR_TYPE_SWAPCHAIN_CREATE_INFO};
    swapchainInfo.usageFlags = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT | XR_SWAPCHAIN_USAGE_SAMPLED_BIT;
    swapchainInfo.format     = chosenFormat;
    swapchainInfo.sampleCount = 1;
    swapchainInfo.width      = g_menuSwapchainWidth;
    swapchainInfo.height     = g_menuSwapchainHeight;
    swapchainInfo.faceCount  = 1;
    swapchainInfo.arraySize  = 1;
    swapchainInfo.mipCount   = 1;

    // Swapchain Left
    if (XR_FAILED(xrCreateSwapchain(g_vrState.session, &swapchainInfo, &g_menuSwapchain))) {
        LOGE("xrCreateSwapchain (menu L) failed (format=0x%llx)", (unsigned long long)chosenFormat);
        return false;
    }
    uint32_t imageCount = 0;
    xrEnumerateSwapchainImages(g_menuSwapchain, 0, &imageCount, nullptr);
#ifdef ANDROID
    g_menuSwapchainImages.resize(imageCount, {XR_TYPE_SWAPCHAIN_IMAGE_OPENGL_ES_KHR});
#else
    g_menuSwapchainImages.resize(imageCount, {XR_TYPE_SWAPCHAIN_IMAGE_OPENGL_KHR});
#endif
    xrEnumerateSwapchainImages(g_menuSwapchain, imageCount, &imageCount,
                               reinterpret_cast<XrSwapchainImageBaseHeader*>(g_menuSwapchainImages.data()));

    // Swapchain Right
    if (XR_FAILED(xrCreateSwapchain(g_vrState.session, &swapchainInfo, &g_menuSwapchainR))) {
        LOGE("xrCreateSwapchain (menu R) failed (format=0x%llx)", (unsigned long long)chosenFormat);
        return false;
    }
    imageCount = 0;
    xrEnumerateSwapchainImages(g_menuSwapchainR, 0, &imageCount, nullptr);
#ifdef ANDROID
    g_menuSwapchainImagesR.resize(imageCount, {XR_TYPE_SWAPCHAIN_IMAGE_OPENGL_ES_KHR});
#else
    g_menuSwapchainImagesR.resize(imageCount, {XR_TYPE_SWAPCHAIN_IMAGE_OPENGL_KHR});
#endif
    xrEnumerateSwapchainImages(g_menuSwapchainR, imageCount, &imageCount,
                               reinterpret_cast<XrSwapchainImageBaseHeader*>(g_menuSwapchainImagesR.data()));

    // Swapchain head-locked
    if (XR_FAILED(xrCreateSwapchain(g_vrState.session, &swapchainInfo, &g_menuSwapchainH))) {
        LOGE("xrCreateSwapchain (menu H) failed (format=0x%llx)", (unsigned long long)chosenFormat);
        return false;
    }
    imageCount = 0;
    xrEnumerateSwapchainImages(g_menuSwapchainH, 0, &imageCount, nullptr);
#ifdef ANDROID
    g_menuSwapchainImagesH.resize(imageCount, {XR_TYPE_SWAPCHAIN_IMAGE_OPENGL_ES_KHR});
#else
    g_menuSwapchainImagesH.resize(imageCount, {XR_TYPE_SWAPCHAIN_IMAGE_OPENGL_KHR});
#endif
    xrEnumerateSwapchainImages(g_menuSwapchainH, imageCount, &imageCount,
                               reinterpret_cast<XrSwapchainImageBaseHeader*>(g_menuSwapchainImagesH.data()));

    LOGI("Menu swapchains created L/R/H: %u x %u (format=0x%llx)",
         g_menuSwapchainWidth, g_menuSwapchainHeight, (unsigned long long)chosenFormat);
    return true;
}





#ifdef ANDROID
static GLuint s_menuCopyProg = 0;
static GLuint s_menuCopyVao  = 0;
static bool   s_menuCopyFailed = false;

static bool vr_menu_copy_init()
{
    if (s_menuCopyProg) return true;
    if (s_menuCopyFailed) return false;

    static const char* vsSrc =
            "#version 300 es\n"
            "const vec2 pos[3] = vec2[3](vec2(-1.0,-1.0), vec2(3.0,-1.0), vec2(-1.0,3.0));\n"
            "void main() { gl_Position = vec4(pos[gl_VertexID], 0.0, 1.0); }\n";

    // Source : octets gamma PREMULTIPLIES (blend SRC_ALPHA sur clear transparent).
    // Sortie : on écrit du linéaire; le GPU ré-encode en sRGB à l'écriture dans
    // GL_SRGB8_ALPHA8 => octets finaux == octets du jeu, et le compositeur les
    // décode correctement (recette identique à celle du PC).
    static const char* fsSrc =
            "#version 300 es\n"
            "precision highp float;\n"
            "uniform sampler2D uTex;\n"
            "out vec4 o;\n"
            "vec3 srgb_to_linear(vec3 c) {\n"
            "    return mix(c / 12.92, pow((c + 0.055) / 1.055, vec3(2.4)), step(0.04045, c));\n"
            "}\n"
            "void main() {\n"
            "    vec4 c = texelFetch(uTex, ivec2(gl_FragCoord.xy), 0);\n"
            "    vec3 straight = (c.a > 0.0) ? clamp(c.rgb / c.a, 0.0, 1.0) : vec3(0.0);\n"
            "    o = vec4(srgb_to_linear(straight) * c.a, c.a);\n"   // re-prémultiplié en linéaire
            "}\n";

    auto compile = [](GLenum type, const char* src) -> GLuint {
        GLuint s = glCreateShader(type);
        glShaderSource(s, 1, &src, nullptr);
        glCompileShader(s);
        GLint ok = 0; glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
        if (!ok) {
            char log[512]; glGetShaderInfoLog(s, sizeof(log), nullptr, log);
            LOGE("menu copy shader compile error: %s", log);
            glDeleteShader(s); return 0;
        }
        return s;
    };

    GLuint vs = compile(GL_VERTEX_SHADER, vsSrc);
    GLuint fs = compile(GL_FRAGMENT_SHADER, fsSrc);
    if (!vs || !fs) { s_menuCopyFailed = true; return false; }

    GLuint p = glCreateProgram();
    glAttachShader(p, vs); glAttachShader(p, fs);
    glLinkProgram(p);
    glDeleteShader(vs); glDeleteShader(fs);
    GLint ok = 0; glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) { LOGE("menu copy program link failed"); glDeleteProgram(p); s_menuCopyFailed = true; return false; }

    glUseProgram(p);
    glUniform1i(glGetUniformLocation(p, "uTex"), 0);
    glUseProgram(0);

    glGenVertexArrays(1, &s_menuCopyVao);
    s_menuCopyProg = p;
    return true;
}
#endif

static void vr_copy_menu_layer(GLuint srcTex, GLuint dstTex, GLuint& srcFbo, GLuint& dstFbo)
{
    if (!dstFbo) glGenFramebuffers(1, &dstFbo);

    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, dstFbo);
    glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, dstTex, 0);

#ifdef ANDROID
    if (g_menuSwapchainIsSrgb && vr_menu_copy_init()) {
        GLint prevProg = 0, prevVao = 0, prevTex = 0, prevActive = 0, vp[4];
        GLboolean prevBlend   = glIsEnabled(GL_BLEND);
        GLboolean prevScissor = glIsEnabled(GL_SCISSOR_TEST);
        glGetIntegerv(GL_CURRENT_PROGRAM, &prevProg);
        glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &prevVao);
        glGetIntegerv(GL_ACTIVE_TEXTURE, &prevActive);
        glActiveTexture(GL_TEXTURE0);
        glGetIntegerv(GL_TEXTURE_BINDING_2D, &prevTex);
        glGetIntegerv(GL_VIEWPORT, vp);

        glDisable(GL_SCISSOR_TEST);
        glDisable(GL_BLEND);
        glViewport(0, 0, g_menuSwapchainWidth, g_menuSwapchainHeight);

        glUseProgram(s_menuCopyProg);
        glBindVertexArray(s_menuCopyVao);
        glBindTexture(GL_TEXTURE_2D, srcTex);
        glDrawArrays(GL_TRIANGLES, 0, 3);

        glBindTexture(GL_TEXTURE_2D, (GLuint)prevTex);
        glActiveTexture((GLenum)prevActive);
        glBindVertexArray((GLuint)prevVao);
        glUseProgram((GLuint)prevProg);
        glViewport(vp[0], vp[1], vp[2], vp[3]);
        if (prevBlend)   glEnable(GL_BLEND);        else glDisable(GL_BLEND);
        if (prevScissor) glEnable(GL_SCISSOR_TEST); else glDisable(GL_SCISSOR_TEST);

        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        return;
    }
#endif

    if (!srcFbo) glGenFramebuffers(1, &srcFbo);

    // Chemin d'origine (PC, ou Android si le runtime n'offre pas SRGB8_ALPHA8)
    glBindFramebuffer(GL_READ_FRAMEBUFFER, srcFbo);
    glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, srcTex, 0);

    glClearColor(0.f, 0.f, 0.f, 0.f);
    glClear(GL_COLOR_BUFFER_BIT);

    glDisable(GL_SCISSOR_TEST);
    glBlitFramebuffer(0, 0, g_menuSwapchainWidth, g_menuSwapchainHeight,
                      0, 0, g_menuSwapchainWidth, g_menuSwapchainHeight,
                      GL_COLOR_BUFFER_BIT, GL_NEAREST);
    glEnable(GL_SCISSOR_TEST);

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}


static void vr_update_menu_swapchain_L()
{
    uint32_t imageIndex = 0;
    XrSwapchainImageAcquireInfo acquireInfo{ XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO };
    xrAcquireSwapchainImage(g_menuSwapchain, &acquireInfo, &imageIndex);

    XrSwapchainImageWaitInfo waitInfo{ XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO };
    waitInfo.timeout = XR_INFINITE_DURATION;
    xrWaitSwapchainImage(g_menuSwapchain, &waitInfo);

    GLuint srcTex = gfx_opengl_get_vr_menu_texture();
    GLuint dstTex = g_menuSwapchainImages[imageIndex].image;

    static GLuint srcFboL = 0, dstFboL = 0;
    vr_copy_menu_layer(srcTex, dstTex, srcFboL, dstFboL);

    XrSwapchainImageReleaseInfo releaseInfo{ XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO };
    xrReleaseSwapchainImage(g_menuSwapchain, &releaseInfo);
}

static void vr_update_menu_swapchain_R()
{
    uint32_t imageIndex = 0;
    XrSwapchainImageAcquireInfo acquireInfo = {XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
    xrAcquireSwapchainImage(g_menuSwapchainR, &acquireInfo, &imageIndex);
    XrSwapchainImageWaitInfo waitInfo = {XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};
    waitInfo.timeout = XR_INFINITE_DURATION;
    xrWaitSwapchainImage(g_menuSwapchainR, &waitInfo);

    static GLuint srcFbo = 0, dstFbo = 0;
    vr_copy_menu_layer(gfx_opengl_get_vr_menu_texture_R(),
                       g_menuSwapchainImagesR[imageIndex].image, srcFbo, dstFbo);

    XrSwapchainImageReleaseInfo releaseInfo = {XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
    xrReleaseSwapchainImage(g_menuSwapchainR, &releaseInfo);
}

static void vr_update_menu_swapchain_H()
{
    uint32_t imageIndex = 0;
    XrSwapchainImageAcquireInfo acquireInfo = {XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
    xrAcquireSwapchainImage(g_menuSwapchainH, &acquireInfo, &imageIndex);

    XrSwapchainImageWaitInfo waitInfo = {XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};
    waitInfo.timeout = XR_INFINITE_DURATION;
    xrWaitSwapchainImage(g_menuSwapchainH, &waitInfo);

    GLuint srcTex = gfx_opengl_get_vr_menu_texture_H();
    GLuint dstTex = g_menuSwapchainImagesH[imageIndex].image;

    static GLuint srcFboH = 0, dstFboH = 0;
    vr_copy_menu_layer(srcTex, dstTex, srcFboH, dstFboH);

    XrSwapchainImageReleaseInfo releaseInfo = {XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
    xrReleaseSwapchainImage(g_menuSwapchainH, &releaseInfo);
}

// ============================================================================
// VIRTUAL SCREEN - world-locked quad layer (see vr_screen.h)
// ============================================================================
float VrScreenDistance = 2.5f;  // metres in front of the eyes when (re)centred
float VrScreenFov      = 60.0f; // degrees of horizontal view the screen spans at that distance
int   VrScreenCurved   = 0;     // 1 = a cylinder section around the viewer instead of a flat quad
float VrScreenHeight   = 0.0f;  // metres above (+) or below eye level when (re)centred

static XrSwapchain g_screenSwapchain = XR_NULL_HANDLE;
#ifdef ANDROID
static std::vector<XrSwapchainImageOpenGLESKHR> g_screenImages;
#else
static std::vector<XrSwapchainImageOpenGLKHR> g_screenImages;
#endif
static uint32_t g_screenW = 0, g_screenH = 0;
static bool     g_screenPending = false;
static bool     g_screenPlaced  = false;
static std::vector<XrTime> g_screenRecenterTimes;
static XrPosef  g_screenPose    = { {0.0f, 0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 0.0f} };
static float    g_screenYaw     = 0.0f;   // radians; the quad's +Z (its face) points back along it
// Cleared while the game draws true stereo into the eye buffers (bondview2.c
// gevrStereoFrame): the screen must not hang in front of the stereo view.
static bool     g_screenVisible = true;
// GoldenEye: while the watch holds the screen in stereo play (bondview2.c
// gevrStereoFrame) it is pinned to the view, at its usual distance, until
// the player looks toward its place in the room (below).
static bool     g_screenHeadLocked = false;
// Pinned, the screen sits a little below the line of sight. Looking toward
// the screen's usual place in the room (g_screenPose, where menus and
// cutscenes show) it glides there and stays: 0 pinned, 1 gliding, 2 placed.
static int      g_screenSnapState = 0;
static float    g_screenSnapT = 0.0f;
static XrTime   g_screenSnapLast = 0;
#define GEVR_SCREEN_PIN_DOWN_DEG  8.0f
#define GEVR_SCREEN_SNAP_DEG      20.0f
#define GEVR_SCREEN_SNAP_SECONDS  0.35f
extern "C" void gevrVrScreenHeadLock(int on)
{
    if (!on) {
        g_screenSnapState = 0;
        g_screenSnapT = 0.0f;
    }
    g_screenHeadLocked = on != 0;
}

static void vr_screen_destroy_swapchain(void)
{
    if (g_screenSwapchain != XR_NULL_HANDLE) {
        xrDestroySwapchain(g_screenSwapchain);
        g_screenSwapchain = XR_NULL_HANDLE;
    }
    g_screenImages.clear();
    g_screenW = g_screenH = 0;
    g_screenPending = false;
    g_screenPlaced = false;
}

static bool vr_screen_ensure_swapchain(uint32_t w, uint32_t h)
{
    if (g_screenSwapchain != XR_NULL_HANDLE && g_screenW == w && g_screenH == h) {
        return true;
    }
    if (g_screenSwapchain != XR_NULL_HANDLE) {
        xrDestroySwapchain(g_screenSwapchain);
        g_screenSwapchain = XR_NULL_HANDLE;
        g_screenImages.clear();
    }
    if (g_vrState.session == XR_NULL_HANDLE) {
        return false;
    }

    XrSwapchainCreateInfo info = {XR_TYPE_SWAPCHAIN_CREATE_INFO};
    info.usageFlags  = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT | XR_SWAPCHAIN_USAGE_SAMPLED_BIT;
    info.format      = vr_pick_swapchain_format();
    info.sampleCount = 1;
    info.width       = w;
    info.height      = h;
    info.faceCount   = 1;
    info.arraySize   = 1;
    info.mipCount    = 1;
    if (XR_FAILED(xrCreateSwapchain(g_vrState.session, &info, &g_screenSwapchain))) {
        LOGE("screen: xrCreateSwapchain %ux%u failed", w, h);
        g_screenSwapchain = XR_NULL_HANDLE;
        return false;
    }

    uint32_t n = 0;
    xrEnumerateSwapchainImages(g_screenSwapchain, 0, &n, nullptr);
#ifdef ANDROID
    g_screenImages.resize(n, {XR_TYPE_SWAPCHAIN_IMAGE_OPENGL_ES_KHR});
#else
    g_screenImages.resize(n, {XR_TYPE_SWAPCHAIN_IMAGE_OPENGL_KHR});
#endif
    xrEnumerateSwapchainImages(g_screenSwapchain, n, &n,
                               reinterpret_cast<XrSwapchainImageBaseHeader*>(g_screenImages.data()));
    g_screenW = w;
    g_screenH = h;
    LOGI("screen: swapchain %ux%u", w, h);
    return true;
}

// Hang the screen VrScreenDistance metres ahead of the head, at eye height,
// level, facing the viewer. Play space, so it stays put while the head moves.
extern "C" void vr_screen_recenter(void)
{
    const XrPosef& l = g_frameViews[0].pose;
    const XrPosef& r = g_frameViews[1].pose;
    const XrQuaternionf& q = l.orientation;

    // Where the head looks, flattened onto the floor plane.
    float fx = -2.0f * (q.x * q.z + q.w * q.y);
    float fz = -(1.0f - 2.0f * (q.x * q.x + q.y * q.y));
    float len = sqrtf(fx * fx + fz * fz);
    if (len < 0.05f) {
        fx = 0.0f; fz = -1.0f; len = 1.0f;
    }
    fx /= len;
    fz /= len;

    g_screenPose.position.x = (l.position.x + r.position.x) * 0.5f + fx * VrScreenDistance;
    g_screenPose.position.y = (l.position.y + r.position.y) * 0.5f + VrScreenHeight;
    g_screenPose.position.z = (l.position.z + r.position.z) * 0.5f + fz * VrScreenDistance;

    // A quad shows its +Z face, so turn its -Z axis along the view direction.
    const float yaw = vr_atan2f(-fx, -fz);
    g_screenYaw = yaw;
    g_screenPose.orientation = { 0.0f, sinf(yaw * 0.5f), 0.0f, cosf(yaw * 0.5f) };
    g_screenPlaced = true;
    LOGI("screen: placed at (%.2f, %.2f, %.2f), yaw %.0f deg",
         g_screenPose.position.x, g_screenPose.position.y, g_screenPose.position.z,
         yaw * 180.0f / 3.14159265f);
}

// The screen's physical width in metres (flat: the chord; curved: the same
// angle as an arc), from the distance and the degrees of view it spans.
static float vr_screen_width(void)
{
    return 2.0f * VrScreenDistance * tanf(VrScreenFov * 0.5f * 3.14159265f / 180.0f);
}

static void vr_screen_head(float out[3])
{
    const XrPosef& l = g_frameViews[0].pose;
    const XrPosef& r = g_frameViews[1].pose;
    out[0] = (l.position.x + r.position.x) * 0.5f;
    out[1] = (l.position.y + r.position.y) * 0.5f;
    out[2] = (l.position.z + r.position.z) * 0.5f;
}

// Face the screen back at the head from where it is now (level: yaw only).
static void vr_screen_face_head(void)
{
    float h[3];
    vr_screen_head(h);
    const float dx = g_screenPose.position.x - h[0];
    const float dz = g_screenPose.position.z - h[2];
    if (dx * dx + dz * dz < 0.01f) return;
    g_screenYaw = vr_atan2f(-dx, -dz);
    g_screenPose.orientation = { 0.0f, sinf(g_screenYaw * 0.5f), 0.0f, cosf(g_screenYaw * 0.5f) };
}

/*
 * Screen controls while the virtual screen is up (port/src/input.c): hold both
 * grips and the screen is in your hands - it follows the midpoint of the two
 * controllers (amplified, so a small move carries it across the room) and
 * turns to keep facing you, at the same physical size; the right stick then
 * moves it nearer/farther along your line of sight and makes it
 * bigger/smaller. On release the new distance, size and height are kept for
 * the next recentre (goldeneye-vr.ini).
 */
extern "C" int gevrVrGripPosePlay(int hand, float pos[3], float quat[4]); // vr_input.cpp
static bool g_screenGrabRebase = false;   // a stick resize moved it: grab on from there

extern "C" void vr_screen_grab(int active)
{
    static bool grabbing = false;
    static float mid0[3], pos0[3];
    float a[3], b[3], q[4];

    if (!g_screenPlaced || !active || g_screenHeadLocked || !gevrVrGripPosePlay(0, a, q) || !gevrVrGripPosePlay(1, b, q)) {
        if (grabbing) {
            float h[3];
            vr_screen_head(h);
            VrScreenHeight = g_screenPose.position.y - h[1];
            if (VrScreenHeight < -VR_SCREEN_HEIGHT_MAX) VrScreenHeight = -VR_SCREEN_HEIGHT_MAX;
            if (VrScreenHeight > VR_SCREEN_HEIGHT_MAX) VrScreenHeight = VR_SCREEN_HEIGHT_MAX;
        }
        grabbing = false;
        return;
    }
    const float mid[3] = { (a[0] + b[0]) * 0.5f, (a[1] + b[1]) * 0.5f, (a[2] + b[2]) * 0.5f };
    if (!grabbing || g_screenGrabRebase) {
        grabbing = true;
        g_screenGrabRebase = false;
        for (int k = 0; k < 3; k++) mid0[k] = mid[k];
        pos0[0] = g_screenPose.position.x;
        pos0[1] = g_screenPose.position.y;
        pos0[2] = g_screenPose.position.z;
        return;
    }

    const float gain = 3.0f;
    const float width = vr_screen_width();
    float p[3] = { pos0[0] + (mid[0] - mid0[0]) * gain,
                   pos0[1] + (mid[1] - mid0[1]) * gain,
                   pos0[2] + (mid[2] - mid0[2]) * gain };
    float h[3];
    vr_screen_head(h);
    float d[3] = { p[0] - h[0], p[1] - h[1], p[2] - h[2] };
    float dist = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
    if (dist < 0.01f) return;
    float clamped = dist;
    if (clamped < VR_SCREEN_DISTANCE_MIN) clamped = VR_SCREEN_DISTANCE_MIN;
    if (clamped > VR_SCREEN_DISTANCE_MAX) clamped = VR_SCREEN_DISTANCE_MAX;
    for (int k = 0; k < 3; k++) p[k] = h[k] + d[k] * (clamped / dist);

    g_screenPose.position = { p[0], p[1], p[2] };
    // same physical width at the new distance
    VrScreenDistance = clamped;
    VrScreenFov = 2.0f * atanf(width / (2.0f * clamped)) * 180.0f / 3.14159265f;
    if (VrScreenFov < VR_SCREEN_FOV_MIN) VrScreenFov = VR_SCREEN_FOV_MIN;
    if (VrScreenFov > VR_SCREEN_FOV_MAX) VrScreenFov = VR_SCREEN_FOV_MAX;
    vr_screen_face_head();
}

// The right stick while grabbing: a new distance and size. The screen stays in
// the direction it is in (not snapped back in front of the head).
extern "C" void vr_screen_resize(float dist, float fov)
{
    if (!g_screenPlaced) {
        VrScreenDistance = dist;
        VrScreenFov = fov;
        return;
    }
    float h[3];
    vr_screen_head(h);
    float d[3] = { g_screenPose.position.x - h[0], g_screenPose.position.y - h[1], g_screenPose.position.z - h[2] };
    float len = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
    if (len < 0.01f) {
        VrScreenDistance = dist;
        VrScreenFov = fov;
        vr_screen_recenter();
        return;
    }
    g_screenPose.position = { h[0] + d[0] * dist / len, h[1] + d[1] * dist / len, h[2] + d[2] * dist / len };
    VrScreenDistance = dist;
    VrScreenFov = fov;
    vr_screen_face_head();
    g_screenGrabRebase = true;
}

/*
 * Laser pointer: where a controller points on the virtual screen, as 0..1
 * across (u, left to right) and down (v, top to bottom), or false when it
 * points past it or the screen is not up. The pointing direction is the gun's
 * barrel (the grip's -Y, bondview2.c gevrGripAxes). Flat screens take a
 * ray-plane hit, curved ones a ray-cylinder hit from inside. `o` is the
 * controller position (play space).
 */
// PORT probe: why the last hit test failed (logged by vr_pointer_update).
static int g_ptrMissReason[2];      // 0 hit, 1 no pose/screen, 2 no intersection, 3 behind, 4 u, 5 v
static float g_ptrRawU[2], g_ptrRawV[2];

static bool vr_screen_hit(int hand, float *u, float *v, float o[3])
{
    float q[4];
    g_ptrMissReason[hand] = 1;
    if (!g_screenPlaced || !g_screenVisible || g_screenW == 0 || !gevrVrGripPosePlay(hand, o, q)) {
        return false;
    }
    const float qx = q[0], qy = q[1], qz = q[2], qw = q[3];
    const float dir[3] = { -2.0f * (qx * qy - qw * qz),
                           -(1.0f - 2.0f * (qx * qx + qz * qz)),
                           -2.0f * (qy * qz + qw * qx) };
    const float cy = cosf(g_screenYaw), sy = sinf(g_screenYaw);
    const float aspect = (float)g_screenW / (float)g_screenH;

    if (VrScreenCurved && g_cylinderSupported) {
        // cylinder centre: back from the screen centre toward the viewer
        const float c[3] = { g_screenPose.position.x + sy * VrScreenDistance,
                             g_screenPose.position.y,
                             g_screenPose.position.z + cy * VrScreenDistance };
        // into the cylinder's frame (rotate by -yaw about Y)
        const float rx = o[0] - c[0], ry = o[1] - c[1], rz = o[2] - c[2];
        const float lo[3] = { cy * rx - sy * rz, ry, sy * rx + cy * rz };
        const float ld[3] = { cy * dir[0] - sy * dir[2], dir[1], sy * dir[0] + cy * dir[2] };
        const float r = VrScreenDistance;
        const float A = ld[0] * ld[0] + ld[2] * ld[2];
        const float B = 2.0f * (lo[0] * ld[0] + lo[2] * ld[2]);
        const float C = lo[0] * lo[0] + lo[2] * lo[2] - r * r;
        const float disc = B * B - 4.0f * A * C;
        if (A < 1e-6f || disc < 0.0f) { g_ptrMissReason[hand] = 2; return false; }
        const float t = (-B + sqrtf(disc)) / (2.0f * A);
        if (t <= 0.0f) { g_ptrMissReason[hand] = 3; return false; }
        const float hx = lo[0] + ld[0] * t, hy = lo[1] + ld[1] * t, hz = lo[2] + ld[2] * t;
        const float angle = VrScreenFov * 3.14159265f / 180.0f;
        const float height = r * angle / aspect;
        *u = 0.5f + vr_atan2f(hx, -hz) / angle;
        *v = 0.5f - hy / height;
    } else {
        const float n[3] = { sy, 0.0f, cy };           // the face's normal (quad +Z)
        const float denom = dir[0] * n[0] + dir[1] * n[1] + dir[2] * n[2];
        if (denom > -1e-4f) { g_ptrMissReason[hand] = 2; return false; }  // pointing away from its face
        const float pp[3] = { g_screenPose.position.x - o[0], g_screenPose.position.y - o[1], g_screenPose.position.z - o[2] };
        const float t = (pp[0] * n[0] + pp[1] * n[1] + pp[2] * n[2]) / denom;
        if (t <= 0.0f) { g_ptrMissReason[hand] = 3; return false; }
        const float hx = o[0] + dir[0] * t - g_screenPose.position.x;
        const float hy = o[1] + dir[1] * t - g_screenPose.position.y;
        const float hz = o[2] + dir[2] * t - g_screenPose.position.z;
        const float width = vr_screen_width();
        const float lx = hx * cy - hz * sy;             // along the quad's +X
        *u = 0.5f + lx / width;
        *v = 0.5f - hy / (width / aspect);
    }
    g_ptrRawU[hand] = *u;
    g_ptrRawV[hand] = *v;
    if (*u < 0.0f || *u > 1.0f) { g_ptrMissReason[hand] = 4; return false; }
    if (*v < 0.0f || *v > 1.0f) { g_ptrMissReason[hand] = 5; return false; }
    g_ptrMissReason[hand] = 0;
    return true;
}

// A point on the screen's surface (play space) from its u, v.
static void vr_screen_uv_to_world(float u, float v, float out[3])
{
    const float cy = cosf(g_screenYaw), sy = sinf(g_screenYaw);
    const float aspect = (float)g_screenW / (float)g_screenH;
    float lx, ly, lz;           // in the screen's own frame, origin at its centre point's axis
    float bx, by, bz;           // origin of that frame
    if (VrScreenCurved && g_cylinderSupported) {
        const float r = VrScreenDistance;
        const float angle = VrScreenFov * 3.14159265f / 180.0f;
        const float th = (u - 0.5f) * angle;
        lx = r * sinf(th);
        ly = (0.5f - v) * r * angle / aspect;
        lz = -r * cosf(th);
        bx = g_screenPose.position.x + sy * r;
        by = g_screenPose.position.y;
        bz = g_screenPose.position.z + cy * r;
    } else {
        const float width = vr_screen_width();
        lx = (u - 0.5f) * width;
        ly = (0.5f - v) * width / aspect;
        lz = 0.0f;
        bx = g_screenPose.position.x;
        by = g_screenPose.position.y;
        bz = g_screenPose.position.z;
    }
    // rotate by yaw about Y
    out[0] = bx + cy * lx + sy * lz;
    out[1] = by + ly;
    out[2] = bz - sy * lx + cy * lz;
}

/*
 * The pointer, worked out once per XR frame (vr_begin_frame_and_update_poses)
 * for both hands. The spot is smoothed with a speed-dependent weight, like
 * the system pointer: heavy when the hand is nearly still (hand tremor and
 * tracking noise at a few metres were visible as jitter), none when it moves
 * fast. The active hand changes only when a button is pressed on the other
 * controller, as on Quest's own UI: switching on motion let an idle hand
 * whose ray happened to land on the screen steal the pointer (on a curved
 * screen, which wraps round, the dot kept vanishing from the middle and left).
 */
struct VrPointerHand {
    bool hit;
    float u, v;                 // smoothed spot
    float o[3], h[3];           // controller and smoothed spot, play space
    float lastU, lastV;         // for "moved"
    bool moved;
};
static VrPointerHand g_ptr[2];
static int g_ptrActive = 1;
// The beam shows only while something reads the pointer (the launcher and
// the front end's folders call gevrVrScreenPointer every frame): not over
// cutscenes, briefings or the watch.
static unsigned g_ptrFrame, g_ptrWantedFrame;

static void vr_pointer_update(void)
{
    g_ptrFrame++;
    for (int hand = 0; hand < 2; hand++) {
        VrPointerHand &p = g_ptr[hand];
        float u, v, o[3];
        const bool hit = vr_screen_hit(hand, &u, &v, o);
        if (hit) {
            if (!p.hit) {
                p.u = u;
                p.v = v;
            } else {
                const float d = fabsf(u - p.u) + fabsf(v - p.v);
                float a = 0.22f + d * 45.0f;
                if (a > 1.0f) a = 1.0f;
                p.u += (u - p.u) * a;
                p.v += (v - p.v) * a;
            }
            p.o[0] = o[0]; p.o[1] = o[1]; p.o[2] = o[2];
            vr_screen_uv_to_world(p.u, p.v, p.h);
        }
        p.moved = hit && (!p.hit || fabsf(p.u - p.lastU) + fabsf(p.v - p.lastV) > 0.004f);
        if (p.moved) {
            p.lastU = p.u;
            p.lastV = p.v;
        }
        p.hit = hit;
    }
    // any button on the other controller makes it the pointer
    {
        const int other = g_ptrActive ^ 1;
        // (not "menu": it is always the left controller's, vr_input.cpp)
        static const char *const buttons[] = { "trigger", "grip", "a", "b", "x", "y", "thumbstick_click" };
        for (const char *b : buttons) {
            if (get_button_state(other, b)) {
                g_ptrActive = other;
                break;
            }
        }
    }
}

/*
 * The pointer for the front end's cursor (front.c) and the launcher: 0 = no
 * controller points at the screen, 1 = one does, 2 = and it moved since the
 * last call.
 */
extern "C" int gevrVrScreenPointer(float *u, float *v)
{
    static float lastU = -1.0f, lastV = -1.0f;
    g_ptrWantedFrame = g_ptrFrame;
    const VrPointerHand &p = g_ptr[g_ptrActive];
    if (!p.hit) {
        lastU = lastV = -1.0f;
        return 0;
    }
    *u = p.u;
    *v = p.v;
    if (fabsf(p.u - lastU) + fabsf(p.v - lastV) > 0.004f) {
        lastU = p.u;
        lastV = p.v;
        return 2;
    }
    return 1;
}

/*
 * The beam and its spot, drawn into the eye buffers while the virtual screen
 * is up (the projection layer is then composited over the screen, with a
 * transparent clear - see the layer order in vr_end_frame): a thin white
 * ray from the controller that fades in, and a soft dot on the screen, as
 * Quest's own panels show. Premultiplied alpha, no depth.
 */
static GLuint s_ptrProg, s_ptrVao, s_ptrVbo;
static GLint s_ptrVpLoc = -1;
static void QuatToMat4(const XrQuaternionf& q, float* m);          // below
static void Mat4Mul(const float* a, const float* b, float* out);
static void InvertRigidMat4(const float* m, float* out);
static void ProjectionFromFov(const XrFovf& fov, float nearZ, float farZ, float* m);

// PORT probe: a file named gevr_ptrgrid in the app's files directory draws
// dots where the pointer model puts the screen's corners, edge midpoints and
// centre, to check it against the compositor's (curved) screen.
static bool vr_pointer_grid_probe(void)
{
    static unsigned n;
    static bool on;
    if ((n++ % 72) == 0) {
        FILE *f = fopen("/sdcard/Android/data/com.gevr.port/files/gevr_ptrgrid", "r");
        on = f != nullptr;
        if (f) fclose(f);
    }
    return on;
}

extern "C" void vr_pointer_draw(void)
{
    const VrPointerHand &p0 = g_ptr[g_ptrActive];
    const bool grid = g_screenVisible && g_screenW != 0 && vr_pointer_grid_probe();
    const bool wanted = g_ptrFrame - g_ptrWantedFrame < 20;
    if (!g_screenVisible || ((!p0.hit || !wanted) && !grid)) {
        return;
    }
    VrPointerHand p = p0;
    if (!p.hit || !wanted) {
        // grid only: a zero-length beam at the centre
        vr_screen_uv_to_world(0.5f, 0.5f, p.h);
        p.o[0] = p.h[0]; p.o[1] = p.h[1] + 0.001f; p.o[2] = p.h[2];
    }

    // Save the GL state first: the one-time setup below binds its own vertex
    // array and buffer. (Unbinding the buffer there, before saving, left
    // GL_ARRAY_BUFFER at 0 for fast3d, whose next shader switch then pointed
    // its attributes at address 0 - the driver crashed copying from it.)
    GLint prevProg = 0, prevVao = 0, prevBuf = 0;
    glGetIntegerv(GL_CURRENT_PROGRAM, &prevProg);
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &prevVao);
    glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &prevBuf);

    if (s_ptrProg == 0) {
        const char *vs =
            "#version 300 es\n"
            "#extension GL_OVR_multiview2 : require\n"
            "layout(num_views = 2) in;\n"
            "uniform mat4 uVP[2];\n"
            "layout(location = 0) in vec3 aPos;\n"
            "layout(location = 1) in vec4 aShape;\n"   // x across, y along, z kind (0 beam, 1 dot), w alpha
            "out vec4 vShape;\n"
            "void main() { vShape = aShape; gl_Position = uVP[gl_ViewID_OVR] * vec4(aPos, 1.0); }\n";
        const char *fs =
            "#version 300 es\n"
            "precision mediump float;\n"
            "in vec4 vShape;\n"
            "out vec4 outColor;\n"
            "void main() {\n"
            "    float a;\n"
            "    if (vShape.z < 0.5) {\n"
            "        float c = 1.0 - abs(vShape.x * 2.0 - 1.0);\n"
            "        a = vShape.w * c * c * smoothstep(0.0, 0.35, vShape.y);\n"
            "    } else {\n"
            "        float r = length(vShape.xy * 2.0 - 1.0);\n"
            "        a = vShape.w * (1.0 - smoothstep(0.55, 1.0, r));\n"
            "    }\n"
            "    outColor = vec4(vec3(a), a);\n"
            "}\n";
        GLuint v = glCreateShader(GL_VERTEX_SHADER);
        glShaderSource(v, 1, &vs, nullptr);
        glCompileShader(v);
        GLuint f = glCreateShader(GL_FRAGMENT_SHADER);
        glShaderSource(f, 1, &fs, nullptr);
        glCompileShader(f);
        s_ptrProg = glCreateProgram();
        glAttachShader(s_ptrProg, v);
        glAttachShader(s_ptrProg, f);
        glLinkProgram(s_ptrProg);
        glDeleteShader(v);
        glDeleteShader(f);
        GLint ok = 0;
        glGetProgramiv(s_ptrProg, GL_LINK_STATUS, &ok);
        if (!ok) {
            char log[512] = {0};
            glGetProgramInfoLog(s_ptrProg, sizeof(log) - 1, nullptr, log);
            LOGE("pointer: program failed: %s", log);
            glDeleteProgram(s_ptrProg);
            s_ptrProg = 0xffffffffu;
            return;
        }
        s_ptrVpLoc = glGetUniformLocation(s_ptrProg, "uVP");
        glGenVertexArrays(1, &s_ptrVao);
        glGenBuffers(1, &s_ptrVbo);
        glBindVertexArray(s_ptrVao);
        glBindBuffer(GL_ARRAY_BUFFER, s_ptrVbo);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 7 * sizeof(float), (void *)0);
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, 7 * sizeof(float), (void *)(3 * sizeof(float)));
        glBindVertexArray((GLuint)prevVao);
        glBindBuffer(GL_ARRAY_BUFFER, (GLuint)prevBuf);
    }
    if (s_ptrProg == 0xffffffffu) {
        return;
    }

    // The eye buffers now hold something pose-dependent: declare the views it
    // was drawn with, so that XR frames which reuse this image (72 Hz display,
    // 60 Hz game) are reprojected from the right pose instead of letting the
    // beam and spot swim off the screen as the head turns.
    g_renderedViews = g_frameViews;
    g_haveRenderedViews = true;

    // eye view-projections, play space
    float vp[32];
    for (int eye = 0; eye < 2; eye++) {
        float pose[16], view[16], proj[16];
        QuatToMat4(g_frameViews[eye].pose.orientation, pose);
        pose[12] = g_frameViews[eye].pose.position.x;
        pose[13] = g_frameViews[eye].pose.position.y;
        pose[14] = g_frameViews[eye].pose.position.z;
        InvertRigidMat4(pose, view);
        ProjectionFromFov(g_frameViews[eye].fov, 0.05f, 100.0f, proj);
        Mat4Mul(proj, view, vp + eye * 16);
    }

    float head[3];
    vr_screen_head(head);
    const float *o = p.o, *h = p.h;
    float d[3] = { h[0] - o[0], h[1] - o[1], h[2] - o[2] };
    // across the beam, facing the head
    float m[3] = { (o[0] + h[0]) * 0.5f - head[0], (o[1] + h[1]) * 0.5f - head[1], (o[2] + h[2]) * 0.5f - head[2] };
    float s[3] = { d[1] * m[2] - d[2] * m[1], d[2] * m[0] - d[0] * m[2], d[0] * m[1] - d[1] * m[0] };
    float sl = sqrtf(s[0] * s[0] + s[1] * s[1] + s[2] * s[2]);
    if (sl < 1e-6f) return;
    const float bw = 0.0035f;           // half width, metres
    for (int k = 0; k < 3; k++) s[k] *= bw / sl;

    // the dot: a square facing the head, sized to a constant angle
    float e[3] = { h[0] - head[0], h[1] - head[1], h[2] - head[2] };
    const float dist = sqrtf(e[0] * e[0] + e[1] * e[1] + e[2] * e[2]);
    if (dist < 0.05f) return;
    float ax[3] = { -e[2], 0.0f, e[0] };                    // horizontal, across the view
    float al = sqrtf(ax[0] * ax[0] + ax[2] * ax[2]);
    if (al < 1e-6f) { ax[0] = 1.0f; ax[2] = 0.0f; al = 1.0f; }
    const float rad = dist * 0.009f;
    for (int k = 0; k < 3; k++) ax[k] *= rad / al;
    float ay[3] = { e[1] * ax[2] - e[2] * ax[1], e[2] * ax[0] - e[0] * ax[2], e[0] * ax[1] - e[1] * ax[0] };
    float yl = sqrtf(ay[0] * ay[0] + ay[1] * ay[1] + ay[2] * ay[2]);
    for (int k = 0; k < 3; k++) ay[k] *= rad / (yl > 1e-6f ? yl : 1.0f);
    // pull the dot a touch toward the viewer so it sits on the surface, not in it
    float hn[3] = { h[0] - e[0] / dist * 0.01f, h[1] - e[1] / dist * 0.01f, h[2] - e[2] / dist * 0.01f };

    const float beamA = 0.55f, dotA = 0.95f;
    const float verts[12][7] = {
        // beam strip (two triangles)
        { o[0] - s[0], o[1] - s[1], o[2] - s[2], 0.0f, 0.0f, 0.0f, beamA },
        { o[0] + s[0], o[1] + s[1], o[2] + s[2], 1.0f, 0.0f, 0.0f, beamA },
        { h[0] + s[0], h[1] + s[1], h[2] + s[2], 1.0f, 1.0f, 0.0f, beamA },
        { o[0] - s[0], o[1] - s[1], o[2] - s[2], 0.0f, 0.0f, 0.0f, beamA },
        { h[0] + s[0], h[1] + s[1], h[2] + s[2], 1.0f, 1.0f, 0.0f, beamA },
        { h[0] - s[0], h[1] - s[1], h[2] - s[2], 0.0f, 1.0f, 0.0f, beamA },
        // dot
        { hn[0] - ax[0] - ay[0], hn[1] - ax[1] - ay[1], hn[2] - ax[2] - ay[2], 0.0f, 0.0f, 1.0f, dotA },
        { hn[0] + ax[0] - ay[0], hn[1] + ax[1] - ay[1], hn[2] + ax[2] - ay[2], 1.0f, 0.0f, 1.0f, dotA },
        { hn[0] + ax[0] + ay[0], hn[1] + ax[1] + ay[1], hn[2] + ax[2] + ay[2], 1.0f, 1.0f, 1.0f, dotA },
        { hn[0] - ax[0] - ay[0], hn[1] - ax[1] - ay[1], hn[2] - ax[2] - ay[2], 0.0f, 0.0f, 1.0f, dotA },
        { hn[0] + ax[0] + ay[0], hn[1] + ax[1] + ay[1], hn[2] + ax[2] + ay[2], 1.0f, 1.0f, 1.0f, dotA },
        { hn[0] - ax[0] + ay[0], hn[1] - ax[1] + ay[1], hn[2] - ax[2] + ay[2], 0.0f, 1.0f, 1.0f, dotA },
    };

    const GLboolean blend = glIsEnabled(GL_BLEND), depth = glIsEnabled(GL_DEPTH_TEST),
                    cull = glIsEnabled(GL_CULL_FACE), scissor = glIsEnabled(GL_SCISSOR_TEST);
    GLint srcRGB, dstRGB, srcA, dstA;
    glGetIntegerv(GL_BLEND_SRC_RGB, &srcRGB);
    glGetIntegerv(GL_BLEND_DST_RGB, &dstRGB);
    glGetIntegerv(GL_BLEND_SRC_ALPHA, &srcA);
    glGetIntegerv(GL_BLEND_DST_ALPHA, &dstA);

    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_SCISSOR_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    glUseProgram(s_ptrProg);
    glUniformMatrix4fv(s_ptrVpLoc, 2, GL_FALSE, vp);
    glBindVertexArray(s_ptrVao);
    glBindBuffer(GL_ARRAY_BUFFER, s_ptrVbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_STREAM_DRAW);
    glDrawArrays(GL_TRIANGLES, 0, 12);

    if (grid) {
        static const float gu[5] = { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f };
        static const float gv[3] = { 0.0f, 0.5f, 1.0f };
        for (int a = 0; a < 5; a++) {
            for (int b = 0; b < 3; b++) {
                float g[3];
                vr_screen_uv_to_world(gu[a], gv[b], g);
                float gverts[6][7];
                const float r2 = rad * 1.6f;
                const float cx[4][2] = { {-1, -1}, {1, -1}, {1, 1}, {-1, 1} };
                const int idx[6] = { 0, 1, 2, 0, 2, 3 };
                for (int k = 0; k < 6; k++) {
                    const float sx = cx[idx[k]][0], sy2 = cx[idx[k]][1];
                    for (int c = 0; c < 3; c++) {
                        gverts[k][c] = g[c] + (ax[c] * sx + ay[c] * sy2) * (r2 / rad);
                    }
                    gverts[k][3] = sx * 0.5f + 0.5f;
                    gverts[k][4] = sy2 * 0.5f + 0.5f;
                    gverts[k][5] = 1.0f;
                    gverts[k][6] = 1.0f;
                }
                glBufferData(GL_ARRAY_BUFFER, sizeof(gverts), gverts, GL_STREAM_DRAW);
                glDrawArrays(GL_TRIANGLES, 0, 6);
            }
        }
    }

    glBindVertexArray((GLuint)prevVao);
    glBindBuffer(GL_ARRAY_BUFFER, (GLuint)prevBuf);
    glUseProgram((GLuint)prevProg);
    glBlendFuncSeparate(srcRGB, dstRGB, srcA, dstA);
    if (!blend) glDisable(GL_BLEND);
    if (depth) glEnable(GL_DEPTH_TEST);
    if (cull) glEnable(GL_CULL_FACE);
    if (scissor) glEnable(GL_SCISSOR_TEST);
}

static bool vr_screen_present_common(unsigned int srcTex, bool is2d, int w, int h);

// Copy layer 0 of the finished frame into the screen swapchain; the layer is
// added to this frame's submission.
extern "C" bool vr_screen_present(unsigned int srcArrayTex, int w, int h)
{
    return vr_screen_present_common(srcArrayTex, false, w, h);
}

// The same from a plain 2D texture (the in-VR launcher, vr_launcher.cpp).
extern "C" bool vr_screen_present_tex2d(unsigned int srcTex, int w, int h)
{
    return vr_screen_present_common(srcTex, true, w, h);
}

static bool vr_screen_present_common(unsigned int srcArrayTex, bool is2d, int w, int h)
{
    if (!g_frameStarted || srcArrayTex == 0 || w <= 0 || h <= 0) {
        return false;
    }
    if (!vr_screen_ensure_swapchain((uint32_t)w, (uint32_t)h)) {
        return false;
    }
    if (!g_screenPlaced) {
        vr_screen_recenter();
    }

    uint32_t idx = 0;
    XrSwapchainImageAcquireInfo acquireInfo = {XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
    if (XR_FAILED(xrAcquireSwapchainImage(g_screenSwapchain, &acquireInfo, &idx))) {
        return false;
    }
    XrSwapchainImageWaitInfo waitInfo = {XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};
    waitInfo.timeout = XR_INFINITE_DURATION;
    xrWaitSwapchainImage(g_screenSwapchain, &waitInfo);

    static GLuint srcFbo = 0, dstFbo = 0;
    if (!srcFbo) glGenFramebuffers(1, &srcFbo);
    if (!dstFbo) glGenFramebuffers(1, &dstFbo);

    glBindFramebuffer(GL_READ_FRAMEBUFFER, srcFbo);
    if (is2d) {
        glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, (GLuint)srcArrayTex, 0);
    } else {
        glFramebufferTextureLayer(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, (GLuint)srcArrayTex, 0, 0);
    }
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, dstFbo);
    glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, g_screenImages[idx].image, 0);
    glDisable(GL_SCISSOR_TEST);
    glBlitFramebuffer(0, 0, w, h, 0, 0, w, h, GL_COLOR_BUFFER_BIT, GL_NEAREST);
    glEnable(GL_SCISSOR_TEST);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    XrSwapchainImageReleaseInfo releaseInfo = {XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
    xrReleaseSwapchainImage(g_screenSwapchain, &releaseInfo);
    g_screenPending = true;
    return true;
}

// ============================================================================
// GOLDENEYE STEREO GAMEPLAY - what the game-side camera reads
// ============================================================================
// Perfect Dark's game code reads these globals directly (bondwalk.c
// vr_player_rot, pdmain.c VrApplySettingsOnStart). GoldenEye's game files are
// C and cannot see the C++ globals, so they come through here.

extern "C" void vr_screen_set_visible(int visible) { g_screenVisible = visible != 0; }
extern "C" int gevrVrGripPose(int hand, float pos[3], float quat[4]); // vr_input.cpp
extern "C" bool gfx_vr_menu_R_bbox(float out[4]);                     // gfx_opengl.cpp

// The game sampled the head for this frame's camera (bondview2.c gevrStereoFrame).
extern "C" void gevrVrSnapshotControllers(const XrPosef *head, int focused);   // vr_input.cpp

extern "C" void gevrVrSnapshotCameraPose(void)
{
    g_cameraViews = g_frameViews;
    g_haveCameraViews = true;
    XrPosef head;
    head.orientation = g_frameViews[0].pose.orientation;
    head.position = { (g_frameViews[0].pose.position.x + g_frameViews[1].pose.position.x) * 0.5f,
                      (g_frameViews[0].pose.position.y + g_frameViews[1].pose.position.y) * 0.5f,
                      (g_frameViews[0].pose.position.z + g_frameViews[1].pose.position.z) * 0.5f };
    gevrVrSnapshotControllers(&head, g_sessionFocused ? 1 : 0);
}

// gfx_run finished drawing a frame into the eye buffers: stereo (the camera
// views above) or not (the screen pass; the eyes hold nothing pose-dependent).
extern "C" void gevrVrMarkEyesRendered(int stereo)
{
    if (stereo && g_haveCameraViews) {
        g_renderedViews = g_cameraViews;
        g_haveRenderedViews = true;
    } else {
        g_haveRenderedViews = false;
    }
}

// int, not bool: GoldenEye's game files see bool as a 32-bit int, and a C++
// bool return leaves the upper bits of w0 undefined.
extern "C" int gevrVrReady(void) { return (vr_is_initialized() && XrFov > 1.0f) ? 1 : 0; }

// The head rotation Perfect Dark's vr_player_rot applies to the look vector:
// the runtime's orientation with the recentre yaw folded in. {x, y, z, w}.
extern "C" void gevrVrHeadQuat(float out[4])
{
    out[0] = vr_HMD_rot_Q.x;
    out[1] = vr_HMD_rot_Q.y;
    out[2] = vr_HMD_rot_Q.z;
    out[3] = vr_HMD_rot_Q.w;
}

// The single symmetric frustum both eyes share (degrees, width/height); the
// multiview shader shears it per eye. Zero until the first frame is located.
extern "C" float gevrVrFov(void) { return XrFov; }

// Perfect Dark's head position (vr_update_head_tracking): centimetres, in the
// frame its look vector uses (OpenXR with X and Z mirrored, recentre yaw applied).
extern "C" void gevrVrHeadPosCm(float out[3])
{
    out[0] = gHeadPos.x;
    out[1] = gHeadPos.y;
    out[2] = gHeadPos.z;
}
extern "C" float gevrVrAspect(void) { return XrAspect; }

// Game units per metre: scales the eye separation (vr_get_eye_view_offset).
extern "C" void gevrVrSetWorldScale(float unitsPerMetre) { vr_world_scale = unitsPerMetre; }

// ============================================================================
// FBOs - Render Targets
// ============================================================================

static bool vr_create_eye_fbos()
{
    if (use_multiview) {
        glGenTextures(1, &g_multiviewDepthArray);
        glBindTexture(GL_TEXTURE_2D_ARRAY, g_multiviewDepthArray);
        glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, GL_DEPTH24_STENCIL8,
                     g_internalRenderWidth, g_internalRenderHeight, 2,
                     0, GL_DEPTH_STENCIL, GL_UNSIGNED_INT_24_8, nullptr);
        glBindTexture(GL_TEXTURE_2D_ARRAY, 0);

        glGenFramebuffers(1, &g_multiviewFBO);
        gfx_opengl_connect_multiview_fbo(g_multiviewFBO, g_internalRenderWidth, g_internalRenderHeight);
        return true;
    }
    else{
        return false;
    }

}

// ============================================================================
// CONTROLLERS - VR Controller Initialization
// ============================================================================

static void vr_init_controllers()
{
    LOGI("Initializing VR controllers");
    XrResult result = create_vr_controllers_complete();
    if (XR_FAILED(result)) {
        LOGE("Failed to initialize controllers");
    }
}

// ============================================================================
// HELPERS - Swapchain Image Management
// ============================================================================

static bool vr_ensure_swapchain_images()
{
    if (g_swapchainImagesInit[0]) return true;
    if (g_vrState.swapchains[0] == XR_NULL_HANDLE) return false;

    uint32_t count = 0;
    XrResult r0 = xrEnumerateSwapchainImages(g_vrState.swapchains[0], 0, &count, nullptr);
    if (XR_FAILED(r0) || count == 0) {
        LOGE("xrEnumerateSwapchainImages(count) failed: %d", (int)r0);
        return false;
    }

    g_swapchainImages[0].resize(count);
    for (auto& img : g_swapchainImages[0]) {
#ifdef ANDROID
        img.type = XR_TYPE_SWAPCHAIN_IMAGE_OPENGL_ES_KHR;
#else
        img.type = XR_TYPE_SWAPCHAIN_IMAGE_OPENGL_KHR;
#endif
        img.next = nullptr;
    }

    XrResult r1 = xrEnumerateSwapchainImages(
            g_vrState.swapchains[0],
            count, &count,
            reinterpret_cast<XrSwapchainImageBaseHeader*>(g_swapchainImages[0].data())
    );
    if (XR_FAILED(r1)) {
        LOGE("xrEnumerateSwapchainImages(data) failed: %d", (int)r1);
        g_swapchainImages[0].clear();
        return false;
    }

    g_swapchainImagesInit[0] = true;
    return true;
}

// ============================================================================
// HEAD TRACKING - Math Helpers
// ============================================================================

XrQuaternionf MultiplyQuaternions(XrQuaternionf q1, XrQuaternionf q2) {
    XrQuaternionf result;
    result.x =  q1.x * q2.w + q1.y * q2.z - q1.z * q2.y + q1.w * q2.x;
    result.y = -q1.x * q2.z + q1.y * q2.w + q1.z * q2.x + q1.w * q2.y;
    result.z =  q1.x * q2.y - q1.y * q2.x + q1.z * q2.w + q1.w * q2.z;
    result.w = -q1.x * q2.x - q1.y * q2.y - q1.z * q2.z + q1.w * q2.w;
    return result;
}

XrQuaternionf YawToQuaternion(float angleDegrees) {
    float rad = angleDegrees * (3.14159265f / 180.0f) * 0.5f;
    return { 0.0f, std::sinf(rad), 0.0f, std::cosf(rad) };
}

static float GetYawDegreesFromQuaternion(XrQuaternionf q) {
    float siny_cosp = 2.0f * (q.w * q.y + q.x * q.z);
    float cosy_cosp = 1.0f - 2.0f * (q.y * q.y + q.z * q.z);
    return vr_atan2f(siny_cosp, cosy_cosp) * (180.0f / 3.14159265f);
}

extern "C" void vr_align_with_game_angle(float target_game_angle) {
    float current_physical_yaw = GetYawDegreesFromQuaternion(gRawHeadQ);
    g_yawOffsetDegrees = target_game_angle - current_physical_yaw;
    while (g_yawOffsetDegrees >  180.0f) g_yawOffsetDegrees -= 360.0f;
    while (g_yawOffsetDegrees < -180.0f) g_yawOffsetDegrees += 360.0f;
//    LOGI("Recenter: Target=%.2f, Physical=%.2f -> Offset=%.2f",
//         target_game_angle, current_physical_yaw, g_yawOffsetDegrees);
}

XrVector3f RotateVectorY(XrVector3f v, float angleDegrees) {
    float rad = angleDegrees * (3.14159265f / 180.0f);
    float s = std::sinf(rad), c = std::cosf(rad);
    return { v.x * c + v.z * s, v.y, -v.x * s + v.z * c };
}

// ============================================================================
// HEAD TRACKING - HMD Position & Rotation
// ============================================================================

static XrQuaternionf SlerpQuaternions(XrQuaternionf a, XrQuaternionf b, float t)
{
    const float qa[4] = {a.w, a.x, a.y, a.z};  // → [w,x,y,z]
    const float qb[4] = {b.w, b.x, b.y, b.z};
    float out[4];
    QuatSlerp(qa, qb, t, out);
    return {out[1], out[2], out[3], out[0]};    // → {x,y,z,w}
}

// Turn the runtime's head Y into a height above the physical floor, in cm.
static void vr_update_head_height()
{
    if (gVrFloorRelativeSpace) {
        gVrHeadHeightCm = gHeadPos.y;
        return;
    }

    if (!sHeightCalibDone) {
        sHeightCalibSamples[sHeightCalibCount++] = gHeadPos.y;

        if (sHeightCalibCount >= VR_HEIGHT_CALIB_SAMPLES) {
            std::sort(sHeightCalibSamples, sHeightCalibSamples + VR_HEIGHT_CALIB_SAMPLES);

            float median = sHeightCalibSamples[VR_HEIGHT_CALIB_SAMPLES / 2];
            sHeightCalibOffset = VrPlayerHeight - median;
            sHeightCalibDone   = true;

            LOGI("Head height calibrated (LOCAL space): median %.1f cm, offset %.1f cm",
                 median, sHeightCalibOffset);
        }
    }

    gVrHeadHeightCm = gHeadPos.y + sHeightCalibOffset;
}

// Start a fresh calibration. No-op under a floor-relative space, where the
// runtime already gives us absolute height.
extern "C" void vr_recalibrate_head_height(void)
{
    sHeightCalibCount  = 0;
    sHeightCalibDone   = false;
    // Until the median lands, assume the head is at standing height, which is
    // where a LOCAL space puts its own origin anyway.
    sHeightCalibOffset = VrPlayerHeight;
}

static void vr_update_head_tracking(XrTime predictedDisplayTime)
{
    if (!g_vrState.sessionRunning || g_vrState.session == XR_NULL_HANDLE) return;

    XrSpaceLocation headLocation = {XR_TYPE_SPACE_LOCATION};
    XrSpaceVelocity headVelocity = {XR_TYPE_SPACE_VELOCITY};
    headLocation.next = &headVelocity;

    XrResult result = xrLocateSpace(
            g_vrState.viewSpace, g_vrState.playSpace,
            predictedDisplayTime, &headLocation);

    if (XR_FAILED(result)) {
        LOGE("xrLocateSpace failed %d", (int)result);
        positionValid = orientationValid = false;
        return;
    }

    positionValid    = (headLocation.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT)    != 0;
    orientationValid = (headLocation.locationFlags & XR_SPACE_LOCATION_ORIENTATION_VALID_BIT) != 0;

    if (positionValid && orientationValid) {
        // Raw position and rotation (with yaw offset) — same calculation as the original
        XrVector3f rawPos = {
                -headLocation.pose.position.x * 100.0f,
                headLocation.pose.position.y * 100.0f,
                -headLocation.pose.position.z * 100.0f
        };
        gRawHeadQ = headLocation.pose.orientation;
        XrQuaternionf offsetQ = YawToQuaternion(g_yawOffsetDegrees);
        XrQuaternionf rawQ    = MultiplyQuaternions(offsetQ, gRawHeadQ);
        XrVector3f    rawHead = RotateVectorY(rawPos, g_yawOffsetDegrees);

        vr_recenter_rot_Q = offsetQ;

        if (headVelocity.velocityFlags & XR_SPACE_VELOCITY_LINEAR_VALID_BIT) {
            vr_head_velocity_play[0] = headVelocity.linearVelocity.x;
            vr_head_velocity_play[1] = headVelocity.linearVelocity.y;
            vr_head_velocity_play[2] = headVelocity.linearVelocity.z;
        }

        // --- Grip pressed on either controller ---
        bool gripAny = (vr_button_R_grip != 0) || (vr_button_L_grip != 0);

        if (gripAny && WepCanZoom) {
            // Initialize the buffer on the first frame with grip
            if (!sSmoothedHeadInit) {
                sSmoothedHeadPos[0] = rawHead.x;
                sSmoothedHeadPos[1] = rawHead.y;
                sSmoothedHeadPos[2] = rawHead.z;
                sSmoothedHeadQ      = rawQ;
                sSmoothedHeadInit   = true;
            }

            // POSITION smoothing (EMA)
            sSmoothedHeadPos[0] = HMD_SMOOTH_ALPHA_POS * rawHead.x + (1.0f - HMD_SMOOTH_ALPHA_POS) * sSmoothedHeadPos[0];
            sSmoothedHeadPos[1] = HMD_SMOOTH_ALPHA_POS * rawHead.y + (1.0f - HMD_SMOOTH_ALPHA_POS) * sSmoothedHeadPos[1];
            sSmoothedHeadPos[2] = HMD_SMOOTH_ALPHA_POS * rawHead.z + (1.0f - HMD_SMOOTH_ALPHA_POS) * sSmoothedHeadPos[2];

            gHeadPos = {sSmoothedHeadPos[0], sSmoothedHeadPos[1], sSmoothedHeadPos[2]};

            // ROTATION smoothing (SLERP)
            sSmoothedHeadQ = SlerpQuaternions(sSmoothedHeadQ, rawQ, HMD_SMOOTH_ALPHA_ROT);
            vr_HMD_rot_Q      = sSmoothedHeadQ;


        } else {
            // Grip released: raw values + reset initialization
            sSmoothedHeadInit = false;
            gHeadPos          = rawHead;
            vr_HMD_rot_Q         = rawQ;
        }

        vr_update_head_height();
    }
}



// ============================================================================
// PC miroir - Layers
// ============================================================================

static void QuatToMat4(const XrQuaternionf& q, float* m) {
    float x = q.x, y = q.y, z = q.z, w = q.w;

    float xx = x * x;
    float yy = y * y;
    float zz = z * z;
    float xy = x * y;
    float xz = x * z;
    float yz = y * z;
    float wx = w * x;
    float wy = w * y;
    float wz = w * z;

    m[0]  = 1.0f - 2.0f * (yy + zz);
    m[1]  = 2.0f * (xy + wz);
    m[2]  = 2.0f * (xz - wy);
    m[3]  = 0.0f;

    m[4]  = 2.0f * (xy - wz);
    m[5]  = 1.0f - 2.0f * (xx + zz);
    m[6]  = 2.0f * (yz + wx);
    m[7]  = 0.0f;

    m[8]  = 2.0f * (xz + wy);
    m[9]  = 2.0f * (yz - wx);
    m[10] = 1.0f - 2.0f * (xx + yy);
    m[11] = 0.0f;

    m[12] = 0.0f;
    m[13] = 0.0f;
    m[14] = 0.0f;
    m[15] = 1.0f;
}
static void Mat4Mul(const float* a, const float* b, float* out) {
    float r[16];
    for (int c = 0; c < 4; ++c) {
        for (int rIdx = 0; rIdx < 4; ++rIdx) {
            r[c * 4 + rIdx] =
                    a[0 * 4 + rIdx] * b[c * 4 + 0] +
                    a[1 * 4 + rIdx] * b[c * 4 + 1] +
                    a[2 * 4 + rIdx] * b[c * 4 + 2] +
                    a[3 * 4 + rIdx] * b[c * 4 + 3];
        }
    }
    memcpy(out, r, sizeof(r));
}

static void ProjectionFromFov(const XrFovf& fov, float nearZ, float farZ, float* m) {
    float l = tanf(fov.angleLeft);
    float r = tanf(fov.angleRight);
    float u = tanf(fov.angleUp);
    float d = tanf(fov.angleDown);

    float w = r - l;
    float h = u - d;

    memset(m, 0, 16 * sizeof(float));
    m[0]  = 2.0f / w;
    m[5]  = 2.0f / h;
    m[8]  = (r + l) / w;
    m[9]  = (u + d) / h;
    m[10] = -(farZ + nearZ) / (farZ - nearZ);
    m[11] = -1.0f;
    m[14] = -(farZ * (nearZ + nearZ)) / (farZ - nearZ);
}

// Inverse of a rigid pose matrix (rotation + translation only)
static void InvertRigidMat4(const float* m, float* out) {

    out[0] = m[0]; out[1] = m[4]; out[2] = m[8];
    out[4] = m[1]; out[5] = m[5]; out[6] = m[9];
    out[8] = m[2]; out[9] = m[6]; out[10] = m[10];
    out[3] = 0.0f; out[7] = 0.0f; out[11] = 0.0f; out[15] = 1.0f;

    float tx = m[12], ty = m[13], tz = m[14];
    out[12] = -(out[0] * tx + out[4] * ty + out[8]  * tz);
    out[13] = -(out[1] * tx + out[5] * ty + out[9]  * tz);
    out[14] = -(out[2] * tx + out[6] * ty + out[10] * tz);
}

#ifndef ANDROID

enum VrMirrorMenuSource {
    VR_MIRROR_MENU_L = 0,
    VR_MIRROR_MENU_R = 1,
    VR_MIRROR_MENU_H = 2,
};

struct VrMirrorMenuEntry {
    VrMirrorMenuSource source;
    float mvp[16];
};

// Compute the MVP for a given quad (factored out from your existing code)
static void ComputeQuadMirrorMVP(const XrCompositionLayerQuad* quad,
                                 const XrView& view,
                                 float* outMvp) {
    // Model (rotation + translation + scale)
    float rot[16];
    QuatToMat4(quad->pose.orientation, rot);

    float model[16];
    memcpy(model, rot, sizeof(rot));
    model[12] = quad->pose.position.x;
    model[13] = quad->pose.position.y;
    model[14] = quad->pose.position.z;

    // Quad size in meters
    model[0] *= quad->size.width;
    model[1] *= quad->size.width;
    model[2] *= quad->size.width;

    model[4] *= quad->size.height;
    model[5] *= quad->size.height;
    model[6] *= quad->size.height;

    float proj[16];
    ProjectionFromFov(view.fov, 0.05f, 100.0f, proj);

    // If the quad is in viewSpace (head-locked), its pose is already in
    // camera space: no View transform needs to be applied.
    if (quad->space == g_vrState.viewSpace) {
        Mat4Mul(proj, model, outMvp);
        return;
    }

    // Quad in playSpace (hands): Proj * View * Model
    float eyeRot[16];
    QuatToMat4(view.pose.orientation, eyeRot);
    eyeRot[12] = view.pose.position.x;
    eyeRot[13] = view.pose.position.y;
    eyeRot[14] = view.pose.position.z;

    float viewMat[16];
    InvertRigidMat4(eyeRot, viewMat);

    float vm[16];
    Mat4Mul(viewMat, model, vm);
    Mat4Mul(proj, vm, outMvp);
}

// Returns the number of quads to draw in the mirror, and fills outEntries
// (outEntries must point to an array of at least 3 elements)
extern "C" int vrGetMenuMirrorMVPList(int eyeIndex, VrMirrorMenuEntry* outEntries) {
    int count = 0;
    const XrView& view = g_lastViews[eyeIndex];

    if (g_lastSubmitMenuL) {
        outEntries[count].source = VR_MIRROR_MENU_L;
        ComputeQuadMirrorMVP(&g_lastMenuLayerL, view, outEntries[count].mvp);
        count++;
    }
    if (g_lastSubmitMenuR) {
        outEntries[count].source = VR_MIRROR_MENU_R;
        ComputeQuadMirrorMVP(&g_lastMenuLayerR, view, outEntries[count].mvp);
        count++;
    }
    if (g_lastSubmitMenuH) {
        outEntries[count].source = VR_MIRROR_MENU_H;
        ComputeQuadMirrorMVP(&g_lastMenuLayerH, view, outEntries[count].mvp);
        count++;
    }

    return count;
}
#endif



// ============================================================================
// COMPOSITION - Layer Submission
// ============================================================================
static void rotvec(float *vx, float *vy, float *vz,
                   float qw, float qx, float qy, float qz)
{
    float tx = 2.0f*(qy*(*vz) - qz*(*vy));
    float ty = 2.0f*(qz*(*vx) - qx*(*vz));
    float tz = 2.0f*(qx*(*vy) - qy*(*vx));
    *vx += qw*tx + qy*tz - qz*ty;
    *vy += qw*ty + qz*tx - qx*tz;
    *vz += qw*tz + qx*ty - qy*tx;
}
static void quaternionMul_XR(const XrQuaternionf *a, const XrQuaternionf *b, XrQuaternionf *out)
{
    out->x = a->w * b->x + a->x * b->w + a->y * b->z - a->z * b->y;
    out->y = a->w * b->y - a->x * b->z + a->y * b->w + a->z * b->x;
    out->z = a->w * b->z + a->x * b->y - a->y * b->x + a->z * b->w;
    out->w = a->w * b->w - a->x * b->x - a->y * b->y - a->z * b->z;
}

extern bool gfx_vr_menu_L_dirty_and_clear(void);
extern bool gfx_vr_menu_R_dirty_and_clear(void);
extern bool gfx_vr_menu_H_dirty_and_clear(void);

static const float VR_MENU_FACING_THRESHOLD = 0.8f;

struct VrMenuResult {
    XrPosef pose;
    bool facingPlayer;
};


static VrMenuResult vr_compute_weapon_menu(int ctrlIndex, bool mirror,
                                           float offsetX, float offsetY, float offsetZ) {

    XrQuaternionf qRaw;
    qRaw.x = -gCtrlQuatRaw[ctrlIndex][1];
    qRaw.y =  gCtrlQuatRaw[ctrlIndex][2];
    qRaw.z = -gCtrlQuatRaw[ctrlIndex][3];
    qRaw.w =  gCtrlQuatRaw[ctrlIndex][0];

    float yawAngle = 0.0f;
    if(!VrLeftHandedMode) {
         yawAngle = mirror ? M_PI : -M_PI;
    }
    else{
         yawAngle = mirror ? -M_PI : M_PI;
    }

    XrQuaternionf qYaw = {0.0f, sinf(yawAngle * 0.25f), 0.0f, cosf(yawAngle * 0.25f)};
    XrQuaternionf qFinal;
    quaternionMul_XR(&qRaw, &qYaw, &qFinal);

    VrMenuResult r;
    r.pose.position = {
            gCtrlPos[ctrlIndex][0] / 100 + offsetX,
            gCtrlPos[ctrlIndex][1] / 100 + offsetY,
            gCtrlPos[ctrlIndex][2] / 100 + offsetZ
    };
    r.pose.orientation = qFinal;

    float fwdX = 0.0f, fwdY = 0.0f, fwdZ = 1.0f;
    rotvec(&fwdX, &fwdY, &fwdZ, qFinal.w, qFinal.x, qFinal.y, qFinal.z);

    float lx = r.pose.position.x, ly = r.pose.position.y, lz = r.pose.position.z;
    float len = sqrtf(lx*lx + ly*ly + lz*lz);
    if (len < 0.0001f) len = 0.0001f;

    float dot = (fwdX * -lx + fwdY * -ly + fwdZ * -lz) / len;
    r.facingPlayer = dot > VR_MENU_FACING_THRESHOLD;
    return r;
}




// Initialize the common fields of a menu quad (everything except pose/size/facing)
static XrCompositionLayerQuad vr_init_menu_quad(XrSwapchain swapchain) {
    XrCompositionLayerQuad q = {XR_TYPE_COMPOSITION_LAYER_QUAD};

    q.layerFlags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;
    q.space = g_vrState.viewSpace;
    q.eyeVisibility = XR_EYE_VISIBILITY_BOTH;
    q.subImage.swapchain = swapchain;
    q.subImage.imageArrayIndex = 0;
    q.subImage.imageRect.offset = {0, 0};
    q.subImage.imageRect.extent = {(int32_t)g_menuSwapchainWidth, (int32_t)g_menuSwapchainHeight};
    return q;
}

static void vr_submit_frame(XrFrameState& frameState, const std::array<XrView, 2>& views) {
    std::array<XrCompositionLayerProjectionView, 2> projViews;
    bool is_mv = gfx_get_current_rendering_api()->is_multiview();

    for (int eye = 0; eye < 2; ++eye) {
        projViews[eye] = {XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW};
        projViews[eye].pose = views[eye].pose;
        projViews[eye].fov = views[eye].fov;

        if (is_mv) {
            projViews[eye].subImage.swapchain = g_vrState.swapchains[0];
            projViews[eye].subImage.imageArrayIndex = eye;
        } else {
            projViews[eye].subImage.swapchain = g_vrState.swapchains[eye];
            projViews[eye].subImage.imageArrayIndex = 0;
        }

        projViews[eye].subImage.imageRect.offset = {0, 0};
        projViews[eye].subImage.imageRect.extent = {
                (int32_t) g_internalRenderWidth,
                (int32_t) g_internalRenderHeight
        };
    }

    XrCompositionLayerProjection layer{XR_TYPE_COMPOSITION_LAYER_PROJECTION};
    layer.space = g_vrState.playSpace;
    layer.viewCount = 2;
    layer.views = projViews.data();

    const int ctrlL = 0;
    const int ctrlR = 1;

    // --- HUD Left Hand ---
    bool submitMenuL = (g_menuSwapchain != XR_NULL_HANDLE) && gfx_vr_menu_L_dirty_and_clear();

    if (submitMenuL) vr_update_menu_swapchain_L();

    XrCompositionLayerQuad menuLayerL = vr_init_menu_quad(g_menuSwapchain);

    if (is_weapon_hud) {
        VrMenuResult res = vr_compute_weapon_menu(ctrlL, /*mirror=*/true, 0.0f, 0.0f, 0.0f);
        menuLayerL.pose = res.pose;
        menuLayerL.size = {1.0f * XrAspect * 0.8f, 1.0f * 0.8f};
        submitMenuL = submitMenuL && res.facingPlayer;
    } else {
        XrQuaternionf qRaw;
        qRaw.x = -gCtrlQuatRaw[ctrlL][1];
        qRaw.y =  gCtrlQuatRaw[ctrlL][2];
        qRaw.z = -gCtrlQuatRaw[ctrlL][3];
        qRaw.w =  gCtrlQuatRaw[ctrlL][0];
        menuLayerL.pose.orientation = qRaw;
        menuLayerL.pose.position = {
                gCtrlPos[ctrlL][0] / 100 + 0.2f,
                gCtrlPos[ctrlL][1] / 100 + 0.2f,
                gCtrlPos[ctrlL][2] / 100 - 0.2f
        };
        menuLayerL.size = {1.0f * XrAspect, 1.0f};
    }

    // --- HUD Right hand ---
    bool submitMenuR = (g_menuSwapchainR != XR_NULL_HANDLE) && gfx_vr_menu_R_dirty_and_clear();

    if (submitMenuR) vr_update_menu_swapchain_R();

    XrCompositionLayerQuad menuLayerR = vr_init_menu_quad(g_menuSwapchainR);

    if (is_weapon_hud) {
        VrMenuResult res = vr_compute_weapon_menu(ctrlR, /*mirror=*/false, 0.0f, 0.0f, 0.0f);
        menuLayerR.pose = res.pose;
        menuLayerR.size = {1.0f * XrAspect * 0.8f, 1.0f * 0.8f};
        submitMenuR = submitMenuR && res.facingPlayer;

        // GoldenEye: the capture is the whole eye buffer; only the ammo count is
        // drawn into it, wherever fast3d's VR mapping puts it. Crop the panel to
        // the measured box (gfx_opengl.cpp gevr_measure_R_capture) plus a margin,
        // at 2.5 mm a texel-row-of-128 so the digits read about 2 cm tall.
        {
            const int32_t w = (int32_t)g_menuSwapchainWidth, h = (int32_t)g_menuSwapchainHeight;
            float box[4] = {0.60f, 0.02f, 1.0f, 0.22f};
            gfx_vr_menu_R_bbox(box);
            const float m = 0.02f;
            float bx0 = box[0] - m, by0 = box[1] - m, bx1 = box[2] + m, by1 = box[3] + m;
            if (bx0 < 0.0f) bx0 = 0.0f;
            if (by0 < 0.0f) by0 = 0.0f;
            if (bx1 > 1.0f) bx1 = 1.0f;
            if (by1 > 1.0f) by1 = 1.0f;
            // OpenGL swapchain images: texel origin bottom-left, as the box is.
            menuLayerR.subImage.imageRect.offset = {(int32_t)(bx0 * w), (int32_t)(by0 * h)};
            menuLayerR.subImage.imageRect.extent = {(int32_t)((bx1 - bx0) * w), (int32_t)((by1 - by0) * h)};
            const float width = (bx1 - bx0) * 0.6f;   // the whole capture would be 0.6 m wide
            menuLayerR.size = {width, width * ((by1 - by0) * h) / ((bx1 - bx0) * w)};

            // Place it from the raw right grip pose (view space, as the gun is
            // placed: bondview2.c gevrGripAxes), 7 cm up along the fist's
            // thumb side (grip -Z), turned to face the eyes so it reads from any
            // hand angle. PD's facing test (mirrored gCtrl* conventions) hid it.
            float gp[3], gq[4];
            if (gevrVrGripPose(1, gp, gq)) {
                const float x = gq[0], y = gq[1], z = gq[2], ww = gq[3];
                const float upx = -(2.0f * (x * z + ww * y));
                const float upy = -(2.0f * (y * z - ww * x));
                const float upz = -(1.0f - 2.0f * (x * x + y * y));
                const float px = gp[0] + upx * 0.07f, py = gp[1] + upy * 0.07f, pz = gp[2] + upz * 0.07f;
                menuLayerR.pose.position = {px, py, pz};

                // yaw/pitch so the quad's +Z points back at the eye (origin)
                const float len = sqrtf(px * px + py * py + pz * pz);
                if (len > 0.01f) {
                    const float yaw = vr_atan2f(-px, -pz);
                    const float pitch = vr_asinf(py / len);
                    const XrQuaternionf qy = {0.0f, sinf(yaw * 0.5f), 0.0f, cosf(yaw * 0.5f)};
                    const XrQuaternionf qx = {sinf(pitch * 0.5f), 0.0f, 0.0f, cosf(pitch * 0.5f)};
                    menuLayerR.pose.orientation = MultiplyQuaternions(qy, qx);
                }
                submitMenuR = (g_menuSwapchainR != XR_NULL_HANDLE) && gfx_vr_menu_R_dirty_and_clear();
            }
        }
    } else {
        submitMenuR = false;
    }

    // --- HUD head-locked ---
    bool submitMenuH = (g_menuSwapchainH != XR_NULL_HANDLE) && gfx_vr_menu_H_dirty_and_clear();
    if (submitMenuH) vr_update_menu_swapchain_H();

    XrCompositionLayerQuad menuLayerH = vr_init_menu_quad(g_menuSwapchainH);

    menuLayerH.pose.orientation = {0.f, 0.f, 0.f, 1.f};

    // GoldenEye: 1.4 m out and 44 degrees tall. At PD's 0.8 m the health and
    // armour arcs sat so close that, with the eyes on the world, each eye saw
    // them in a different place (doubled); further out they fuse, and a
    // fixed angular size keeps them clear of the lens edges.
    {
        const float d = 1.4f;   // 2 m read as a bit too far (user)
        const float hgt = 2.0f * d * tanf(22.0f * 3.14159265f / 180.0f);
        menuLayerH.pose.position = {0.f, 0.f, -d};
        menuLayerH.size = {hgt * XrAspect, hgt};
    }

    // --- Virtual screen (world-locked) ---
    // Once the screen exists it is submitted on every frame, including the
    // XR frames the pump closes without a game frame (72 Hz display, 60 Hz
    // game): the swapchain keeps its last image, and a frame without the
    // layer is a visible flicker.
    const bool submitScreen = g_screenVisible && g_screenPlaced && g_screenSwapchain != XR_NULL_HANDLE;
    g_screenPending = false;
    XrCompositionLayerQuad screenLayer = {XR_TYPE_COMPOSITION_LAYER_QUAD};
    XrCompositionLayerCylinderKHR screenCyl = {XR_TYPE_COMPOSITION_LAYER_CYLINDER_KHR};
    const bool screenCurved = VrScreenCurved && g_cylinderSupported;
    if (submitScreen) {
        screenLayer.layerFlags    = 0; // opaque
        screenLayer.space         = g_vrState.playSpace;
        screenLayer.eyeVisibility = XR_EYE_VISIBILITY_BOTH;
        screenLayer.subImage.swapchain        = g_screenSwapchain;
        screenLayer.subImage.imageArrayIndex  = 0;
        screenLayer.subImage.imageRect.offset = {0, 0};
        screenLayer.subImage.imageRect.extent = {(int32_t)g_screenW, (int32_t)g_screenH};
        screenLayer.pose = g_screenPose;
        if (g_screenHeadLocked && g_screenSnapState != 2) {
            // the head this frame, in play space (as the eye views)
            const XrQuaternionf hq = views[0].pose.orientation;
            const XrVector3f hp = { (views[0].pose.position.x + views[1].pose.position.x) * 0.5f,
                                    (views[0].pose.position.y + views[1].pose.position.y) * 0.5f,
                                    (views[0].pose.position.z + views[1].pose.position.z) * 0.5f };
            const float d = VrScreenDistance;
            float lx = 0.0f, ly = -d * tanf(GEVR_SCREEN_PIN_DOWN_DEG * 3.14159265f / 180.0f), lz = -d;
            rotvec(&lx, &ly, &lz, hq.w, hq.x, hq.y, hq.z);
            XrPosef pinned;
            pinned.orientation = hq;
            pinned.position = { hp.x + lx, hp.y + ly, hp.z + lz };

            if (g_screenSnapState == 0) {
                float fx = 0.0f, fy = 0.0f, fz = -1.0f;
                rotvec(&fx, &fy, &fz, hq.w, hq.x, hq.y, hq.z);
                float tx = g_screenPose.position.x - hp.x;
                float ty = g_screenPose.position.y - hp.y;
                float tz = g_screenPose.position.z - hp.z;
                const float tl = sqrtf(tx * tx + ty * ty + tz * tz);
                if (tl > 0.01f && (fx * tx + fy * ty + fz * tz) / tl > cosf(GEVR_SCREEN_SNAP_DEG * 3.14159265f / 180.0f)) {
                    g_screenSnapState = 1;
                    g_screenSnapT = 0.0f;
                    g_screenSnapLast = frameState.predictedDisplayTime;
                }
            }
            if (g_screenSnapState == 1) {
                float dt = (float)(frameState.predictedDisplayTime - g_screenSnapLast) * 1e-9f;
                g_screenSnapLast = frameState.predictedDisplayTime;
                if (dt < 0.0f) dt = 0.0f;
                if (dt > 0.1f) dt = 0.1f;
                g_screenSnapT += dt / GEVR_SCREEN_SNAP_SECONDS;
                if (g_screenSnapT >= 1.0f) {
                    g_screenSnapState = 2;
                } else {
                    const float e = g_screenSnapT * g_screenSnapT * (3.0f - 2.0f * g_screenSnapT);
                    XrQuaternionf a = pinned.orientation, b = g_screenPose.orientation;
                    if (a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w < 0.0f) {
                        b.x = -b.x; b.y = -b.y; b.z = -b.z; b.w = -b.w;
                    }
                    XrQuaternionf q = { a.x + (b.x - a.x) * e, a.y + (b.y - a.y) * e,
                                        a.z + (b.z - a.z) * e, a.w + (b.w - a.w) * e };
                    const float ql = sqrtf(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
                    if (ql > 1e-6f) { q.x /= ql; q.y /= ql; q.z /= ql; q.w /= ql; }
                    pinned.orientation = q;
                    pinned.position = { pinned.position.x + (g_screenPose.position.x - pinned.position.x) * e,
                                        pinned.position.y + (g_screenPose.position.y - pinned.position.y) * e,
                                        pinned.position.z + (g_screenPose.position.z - pinned.position.z) * e };
                }
            }
            if (g_screenSnapState != 2) {
                screenLayer.pose = pinned;
            }
        }
        const float width = vr_screen_width();
        screenLayer.size = { width, width * (float)g_screenH / (float)g_screenW };

        if (screenCurved) {
            // The same view angle as an arc around a centre VrScreenDistance
            // back toward the viewer; the cylinder shows its inside, centred
            // on its pose's -Z (the direction the flat quad faces away from).
            screenCyl.layerFlags    = 0;
            screenCyl.space         = g_vrState.playSpace;
            screenCyl.eyeVisibility = XR_EYE_VISIBILITY_BOTH;
            screenCyl.subImage      = screenLayer.subImage;
            // the arc's centre: VrScreenDistance behind the quad's pose along
            // its +Z (for the room screen, sin/cos of its yaw)
            screenCyl.pose = screenLayer.pose;
            {
                float cx = 0.0f, cyy = 0.0f, cz = VrScreenDistance;
                const XrQuaternionf& sq = screenLayer.pose.orientation;
                rotvec(&cx, &cyy, &cz, sq.w, sq.x, sq.y, sq.z);
                screenCyl.pose.position.x += cx;
                screenCyl.pose.position.y += cyy;
                screenCyl.pose.position.z += cz;
            }
            screenCyl.radius       = VrScreenDistance;
            screenCyl.centralAngle = VrScreenFov * 3.14159265f / 180.0f;
            screenCyl.aspectRatio  = (float)g_screenW / (float)g_screenH;
        }
    }

    // --- Layer submission ---
    // With the virtual screen up, the screen goes first and the eye buffers
    // (cleared transparent, holding only the laser pointer) over it, so the
    // beam and its spot show in front of the screen; otherwise the eye
    // buffers are the scene.
    int numLayers = 0;
    const XrCompositionLayerBaseHeader* layers[5];
    if (submitScreen) {
        layers[numLayers++] = screenCurved
            ? reinterpret_cast<const XrCompositionLayerBaseHeader*>(&screenCyl)
            : reinterpret_cast<const XrCompositionLayerBaseHeader*>(&screenLayer);
        layer.layerFlags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;
    }
    layers[numLayers++] = reinterpret_cast<const XrCompositionLayerBaseHeader *>(&layer);

    if (submitMenuL) layers[numLayers++] = reinterpret_cast<const XrCompositionLayerBaseHeader*>(&menuLayerL);
    if (submitMenuR) layers[numLayers++] = reinterpret_cast<const XrCompositionLayerBaseHeader*>(&menuLayerR);
    if (submitMenuH) layers[numLayers++] = reinterpret_cast<const XrCompositionLayerBaseHeader*>(&menuLayerH);

#ifndef ANDROID // if PC
    // for miroir PC
    g_lastMenuLayerL   = menuLayerL;
    g_lastMenuLayerR   = menuLayerR;
    g_lastMenuLayerH   = menuLayerH;
    g_lastSubmitMenuL  = submitMenuL;
    g_lastSubmitMenuR  = submitMenuR;
    g_lastSubmitMenuH  = submitMenuH;
    g_lastViews        = views;
#endif

    XrFrameEndInfo endInfo = {XR_TYPE_FRAME_END_INFO};
    endInfo.displayTime          = frameState.predictedDisplayTime;
    endInfo.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
    endInfo.layerCount           = numLayers;
    endInfo.layers               = layers;

    XrResult re = xrEndFrame(g_vrState.session, &endInfo);
    if (XR_FAILED(re))
        LOGE("xrEndFrame failed %d", (int)re);

}



// ============================================================================
// EVENTS - Session State Management
// ============================================================================

extern "C" void vr_poll_events(void)
{
    if (!g_vrState.instance) return;

    while (true) {
        XrEventDataBuffer event;
        std::memset(&event, 0, sizeof(event));
        event.type = XR_TYPE_EVENT_DATA_BUFFER;

        XrResult r = xrPollEvent(g_vrState.instance, &event);
        if (r == XR_EVENT_UNAVAILABLE) break;
        if (XR_FAILED(r)) {
            LOGE("xrPollEvent failed: %d", (int)r);
            break;
        }

        const XrEventDataBaseHeader* baseEvent = (const XrEventDataBaseHeader*)&event;

        switch (baseEvent->type) {
            case XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED: {
                const XrEventDataSessionStateChanged* ssEvent =
                        (const XrEventDataSessionStateChanged*)baseEvent;
                if (ssEvent->session != g_vrState.session) break;
                LOGI("Session state changed: %d", (int)ssEvent->state);
                g_sessionFocused = ssEvent->state == XR_SESSION_STATE_FOCUSED;

                switch (ssEvent->state) {
                    case XR_SESSION_STATE_READY:
                        if (!g_vrState.sessionRunning) {
                            XrSessionBeginInfo begin{ XR_TYPE_SESSION_BEGIN_INFO };
                            begin.primaryViewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
                            XrResult br = xrBeginSession(g_vrState.session, &begin);
                            if (br == XR_SUCCESS) {
                                g_vrState.sessionRunning = true;
                                LOGI("Session begun");
                                vr_request_refresh_rate();
                            }
                        }
                        break;
                    case XR_SESSION_STATE_STOPPING:
                        if (g_vrState.sessionRunning) {
                            xrEndSession(g_vrState.session);
                            g_vrState.sessionRunning = false;
                            LOGI("Session ended");
                        }
                        break;
                    case XR_SESSION_STATE_EXITING:
                    case XR_SESSION_STATE_LOSS_PENDING:
                        g_vrState.sessionRunning = false;
                        LOGI("Session exiting/loss pending");
                        break;
                    case XR_SESSION_STATE_VISIBLE:
                    default: break;
                }
                break;
            }
            case XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING:
                g_vrState.sessionRunning = false;
                LOGI("Instance loss pending");
                break;
            case XR_TYPE_EVENT_DATA_REFERENCE_SPACE_CHANGE_PENDING: {
                const auto* change = reinterpret_cast<const XrEventDataReferenceSpaceChangePending*>(&event);
                // Existing reference-space handles follow the runtime's new origin.
                // Re-place the cinema screen only after views use that origin;
                // poses located before changeTime are still in the old space.
                if (change->session == g_vrState.session &&
                    (change->referenceSpaceType == gPlaySpaceType ||
                     (gPlaySpaceType == XR_REFERENCE_SPACE_TYPE_LOCAL_FLOOR_EXT &&
                      change->referenceSpaceType == XR_REFERENCE_SPACE_TYPE_LOCAL))) {
                    g_screenRecenterTimes.push_back(change->changeTime);
                    LOGI("screen: reference change queued type=%d time=%lld",
                        (int)change->referenceSpaceType, (long long)change->changeTime);
                }
                break;
            }
            default: break;
        }
    }
}

// ============================================================================
// INITIALIZATION - Complete VR Setup Pipeline
// ============================================================================

#ifdef ANDROID
extern "C" void openxr_initialize_vr(JavaVM* vm, jobject activity, ANativeWindow* window)
{
    LOGI("========== OPENXR INIT (Android) START ==========");
    g_vrState = VRState{};

    auto extensions = vr_enumerate_extensions();
    if (extensions.size() < 2) {
        LOGE("Missing required extensions");
        return;
    }

    if (!vr_create_instance(vm, activity, extensions)) return;
    if (!vr_get_system()) return;
    if (!vr_configure_resolution()) return;
    if (!vr_capture_egl_context()) return;
    if (!vr_verify_graphics_requirements()) return;
    if (!vr_create_session()) return;
    vr_setup_color_space();
    vr_init_controllers();
    if (!vr_create_play_space()) return;
    if (!vr_create_view_space()) return;
    if (!vr_create_swapchains()) return;
    if (!vr_create_eye_fbos()) return;

    // Swapchain dedicated to the menu quad panel
    if (!vr_create_menu_swapchain()) {
        vr_log("vr_create_menu_swapchain failed (non-fatal, menu quad disabled)");
    // non-fatal: continue without the panel
    }

    LOGI("========== OPENXR INIT (Android) COMPLETE ==========");
}
#else


extern "C" bool vrWaitForRuntime(int waitSeconds) {
    return vrEnsureDefaultRuntimeRunning();
}


static void vrDestroyInstanceIfNeeded(void) {
    if (g_vrState.instance != XR_NULL_HANDLE) {
        xrDestroyInstance(g_vrState.instance);
        g_vrState.instance = XR_NULL_HANDLE;
    }
}

static bool openxrInitializeVRwindowsInternal(void) {
    auto extensions = vr_enumerate_extensions();
    if (extensions.empty())              return false;
    if (!vr_create_instance(extensions)) return false;
    if (!vr_get_system())                  return false;
    if (!vr_configure_resolution())        return false;
    if (!vr_verify_graphics_requirements()) return false;
    if (!vr_create_session())              return false;
    vr_setup_color_space();
    vr_init_controllers();
    if (!vr_create_play_space())            return false;
    if (!vr_create_view_space())            return false;
    if (!vr_create_swapchains())           return false;
    if (!vr_create_eye_fbos())              return false;

    // Swapchain dedicated to the menu quad panel
    if (!vr_create_menu_swapchain()) {
        LOGE("vr_create_menu_swapchain failed (non-fatal, menu quad disabled)");
     // non-fatal: continue without the panel
    }

    return true;
}

extern "C" void openxr_initialize_vr_windows(void) {
    LOGI("========== OPENXR INIT (Windows) START ==========");
    g_vrState = VRState{};

    if (!openxrInitializeVRwindowsInternal()) {
        LOGE("OpenXR init failed, cleanup...");
        vrDestroyInstanceIfNeeded();
        return;
    }

    LOGI("========== OPENXR INIT (Windows) COMPLETE ==========");
}

#endif

// ============================================================================
// LIFECYCLE - Entry Point
// ============================================================================

extern "C" void vr_initialize()
{
#ifdef ANDROID
    if (g_vrInitialized || !g_activity || !g_window) return;

    EGLDisplay display = eglGetCurrentDisplay();
    EGLContext context  = eglGetCurrentContext();
    if (display == EGL_NO_DISPLAY || context == EGL_NO_CONTEXT) {
        LOGE("No OpenGL context available");
        return;
    }

    LOGI("Initializing from OpenGL thread (Android)");
    openxr_initialize_vr(g_vm, g_activity, g_window);
#else
    if (g_vrInitialized) return;

    LOGI("Initializing VR (Windows)");
    openxr_initialize_vr_windows();
#endif

    if (g_vrState.instance == XR_NULL_HANDLE || g_vrState.session == XR_NULL_HANDLE) {
        LOGE("vr_initialize: Failed to initialize OpenXR");
        return;
    }

    g_vrInitialized = true;
    LOGI("VR system ready");

}



// ----------------------------------------------------------------------
// STEREO: Actual horizontal lens offset ratio (NDC offset),
// derived from the asymmetric FOV reported by the OpenXR runtime for this eye.
// Replaces any fixed constant used to expand the scissor box.
// ----------------------------------------------------------------------
float vr_get_horizontal_fov_offset_ratio(int eye) {
    if (!g_vrInitialized || eye < 0 || eye > 1) return 0.0f;

    XrFovf& fov = g_frameViews[eye].fov;
    const float tanLeft  = std::tan(fov.angleLeft);
    const float tanRight = std::tan(fov.angleRight);
    const float denom = tanRight - tanLeft;
    if (std::fabs(denom) < 0.0001f) return 0.0f;

    // Fraction [-1, 1] indicating the optical center offset relative
    // to the geometric center of the image (0 = symmetric FOV)
    return (tanRight + tanLeft) / denom;
}

struct VrEyeFovTan {
    float tanLeft, tanRight, tanCenter, tanHalfWidth;
};

// Helper to compute the tangent of FOV angles for a given eye
VrEyeFovTan vr_get_eye_fov_tan(int eye) {
    XrFovf& fov = g_frameViews[eye].fov;
    VrEyeFovTan out;
    out.tanLeft  = tanf(fov.angleLeft);
    out.tanRight = tanf(fov.angleRight);
    out.tanCenter    = (out.tanLeft + out.tanRight) * 0.5f;
    out.tanHalfWidth = (out.tanRight - out.tanLeft) * 0.5f;
    return out;
}

extern float s_eye_offsets[8];

// -----------------------------------------------------------------------
// STEREO: Asymmetric projection from XrFovf angles
// -----------------------------------------------------------------------
int shaders_build_xr_projection(
        float tanLeft, float tanRight,
        float tanUp, float tanDown,
        float znear, float zfar,
        float outP[16])
{
    if (!outP) return 0;

    float rw = 1.0f / (tanRight - tanLeft);
    float rh = 1.0f / (tanUp - tanDown);
    float rz = 1.0f / (znear - zfar);

    // Reset to 0
    memset(outP, 0, 16 * sizeof(float));

    // DIRECT generation in N64 Row-Major format (transposed from OpenGL)
    outP[0 * 4 + 0] = 2.0f * rw;                    // P_n64[0][0]
    outP[1 * 4 + 1] = 2.0f * rh;                    // P_n64[1][1]

    outP[2 * 4 + 0] = (tanRight + tanLeft) * rw;    // P_n64[2][0] — offset X
    outP[2 * 4 + 1] = (tanUp + tanDown) * rh;       // P_n64[2][1] — offset Y
    outP[2 * 4 + 2] = (znear + zfar) * rz;          // P_n64[2][2] — scale Z
    outP[2 * 4 + 3] = -1.0f;                        // P_n64[2][3] — w_clip = -z

    outP[3 * 4 + 2] = 2.0f * znear * zfar * rz;     // P_n64[3][2] — offset Z

    g_camZNear = znear;
    g_camZFar = zfar;
    return 1;
}

// -----------------------------------------------------------------------
// STEREO: View matrix from XrPosef (position + quaternion)
// -----------------------------------------------------------------------
int shaders_build_xr_view(
        float px, float py, float pz,          // XrVector3f position
        float qx, float qy, float qz, float qw,// XrQuaternionf orientation
        float outV[16])
{
    if (!outV) return 0;

    // Convert the quaternion into a 3x3 rotation matrix
    float x2 = qx*qx, y2 = qy*qy, z2 = qz*qz;
    float xy = qx*qy, xz = qx*qz, yz = qy*qz;
    float wx = qw*qx, wy = qw*qy, wz = qw*qz;

    // Camera axes (row-major first, then transpose for the View matrix)
    float rx = 1 - 2*(y2+z2),  ux = 2*(xy-wz), fx = 2*(xz+wy);
    float ry = 2*(xy+wz),      uy = 1 - 2*(x2+z2), fy = 2*(yz-wx);
    float rz = 2*(xz-wy),      uz = 2*(yz+wx), fz = 1 - 2*(x2+y2);

    // Translations: -dot(axis, position)
    float tx = -(rx*px + ry*py + rz*pz);
    float ty = -(ux*px + uy*py + uz*pz);
    float tz = -(fx*px + fy*py + fz*pz);

    // Col-major for OpenGL
    outV[0]=rx; outV[1]=ry; outV[2]=rz; outV[3]=tx;
    outV[4]=ux; outV[5]=uy; outV[6]=uz; outV[7]=ty;
    outV[8]=fx; outV[9]=fy; outV[10]=fz;outV[11]=tz;
    outV[12]=0; outV[13]=0; outV[14]=0; outV[15]=1;
    return 1;
}

// ============================================================================
// FRAME LIFECYCLE - Begin Frame & Update Poses
// ============================================================================

// Sleep out the remainder of a nominal frame interval.
//
// xrWaitFrame below is the ONLY thing pacing the game's tick: the desktop mirror forces
// swap interval 0 every frame and Video.FramerateLimit defaults to 0, so nothing else
// throttles it. If the session dies and we just return early, mainTick free-runs -- it
// pegs a core and floods the GPU queue, which starves the desktop compositor and any
// streaming runtime alongside it. That is a whole-machine hang, not just a dead game.
static void vr_idle_pace()
{
    static uint64_t sLastTick = 0;

    const uint64_t freq = SDL_GetPerformanceFrequency();
    const uint64_t now  = SDL_GetPerformanceCounter();

    if (sLastTick != 0 && freq != 0) {
        const double elapsedMs = (double)(now - sLastTick) * 1000.0 / (double)freq;
        const double targetMs  = 1000.0 / 72.0;
        if (elapsedMs < targetMs) {
            SDL_Delay((Uint32)(targetMs - elapsedMs));
        }
    }

    sLastTick = SDL_GetPerformanceCounter();
}

extern "C" bool vr_begin_frame_and_update_poses()
{
    if (!g_vrState.sessionRunning || g_vrState.session == XR_NULL_HANDLE) {
        vr_idle_pace();
        return false;
    }

    XrFrameWaitInfo waitInfo{ XR_TYPE_FRAME_WAIT_INFO };
    g_frameState = { XR_TYPE_FRAME_STATE };
    XrResult r = xrWaitFrame(g_vrState.session, &waitInfo, &g_frameState);
    if (XR_FAILED(r)) {
        LOGE("xrWaitFrame failed: %d", (int)r);
        return false;
    }


    XrFrameBeginInfo beginInfo{ XR_TYPE_FRAME_BEGIN_INFO };
    r = xrBeginFrame(g_vrState.session, &beginInfo);
    if (XR_FAILED(r)) {
        LOGE("xrBeginFrame failed: %d", (int)r);
        return false;
    }

    g_frameStarted = true;

    if (!g_frameState.shouldRender) {
        vr_end_empty_frame(g_frameState.predictedDisplayTime);
        g_frameStarted = false;
        return false;
    }

    for (auto& v : g_frameViews) { v.type = XR_TYPE_VIEW; v.next = nullptr; }

    XrViewLocateInfo viewLocate{ XR_TYPE_VIEW_LOCATE_INFO };
    viewLocate.viewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
    viewLocate.displayTime           = g_frameState.predictedDisplayTime;
    viewLocate.space                 = g_vrState.playSpace;

    XrViewState viewState{ XR_TYPE_VIEW_STATE };
    uint32_t viewCount = 2;
    r = xrLocateViews(g_vrState.session, &viewLocate, &viewState, 2, &viewCount, g_frameViews.data());
    if (XR_FAILED(r) || viewCount < 2) {
        LOGE("xrLocateViews failed: %d", (int)r);
        vr_end_empty_frame(g_frameState.predictedDisplayTime);
        g_frameStarted = false;
        return false;
    }

    const XrViewStateFlags needed = XR_VIEW_STATE_POSITION_VALID_BIT | XR_VIEW_STATE_ORIENTATION_VALID_BIT;
    if ((viewState.viewStateFlags & needed) != needed) {
        LOGE("viewStateFlags missing position/orientation");
        vr_end_empty_frame(g_frameState.predictedDisplayTime);
        g_frameStarted = false;
        return false;
    }

    for (auto it = g_screenRecenterTimes.begin(); it != g_screenRecenterTimes.end();) {
        if (g_frameState.predictedDisplayTime >= *it) {
            g_screenPlaced = false;
            it = g_screenRecenterTimes.erase(it);
        } else {
            ++it;
        }
    }

    // Build the symmetric projection used by the original game from the
    // average OpenXR tangent extents.  Using the angular span directly is
    // only correct for a perfectly symmetric frustum, and using the render
    // target dimensions is not guaranteed to match the optical frustum.
    // The per-eye centre offsets are applied later by the multiview shader.
    float tanHalfWidthSum = 0.0f;
    float tanHalfHeightSum = 0.0f;

    for (int eye = 0; eye < 2; eye++) {
        XrFovf& fov = g_frameViews[eye].fov;
        tanHalfWidthSum += (std::tanf(fov.angleRight) - std::tanf(fov.angleLeft)) * 0.5f;
        tanHalfHeightSum += (std::tanf(fov.angleUp) - std::tanf(fov.angleDown)) * 0.5f;
    }

    const float tanHalfWidth = tanHalfWidthSum * 0.5f;
    const float tanHalfHeight = tanHalfHeightSum * 0.5f;

    if (tanHalfWidth > 0.001f && tanHalfHeight > 0.001f) {
        XrFov = 2.0f * std::atanf(tanHalfHeight) * (180.0f / 3.14159265f);
        XrAspect = tanHalfWidth / tanHalfHeight;
    }

    static bool projectionLogged = false;
    if (!projectionLogged) {
        for (int eye = 0; eye < 2; eye++) {
            XrFovf& fov = g_frameViews[eye].fov;
            LOGI("OpenXR eye %d FOV radians: left=%.6f right=%.6f up=%.6f down=%.6f",
                 eye, fov.angleLeft, fov.angleRight, fov.angleUp, fov.angleDown);
        }
        LOGI("OpenXR game projection: vertical_fov=%.4f aspect=%.6f",
             XrFov, XrAspect);
        for (int eye = 0; eye < 2; eye++) {
            XrFovf& fov = g_frameViews[eye].fov;
            const float tanLeft = std::tanf(fov.angleLeft);
            const float tanRight = std::tanf(fov.angleRight);
            const float tanUp = std::tanf(fov.angleUp);
            const float tanDown = std::tanf(fov.angleDown);
            LOGI("OpenXR eye %d projection: scale_x=%.6f center_x=%.6f scale_y=%.6f center_y=%.6f",
                 eye,
                 2.0f / (tanRight - tanLeft),
                 (tanRight + tanLeft) / (tanRight - tanLeft),
                 2.0f / (tanUp - tanDown),
                 (tanUp + tanDown) / (tanUp - tanDown));
        }
        projectionLogged = true;
    }

    for (int eye = 0; eye < 2; eye++) {
        XrFovf&  fov  = g_frameViews[eye].fov;
        const XrPosef& pose = g_frameViews[eye].pose;

        float projMtx[16], viewMtx[16];

        shaders_build_xr_projection(
                std::tanf(fov.angleLeft),  std::tanf(fov.angleRight),
                std::tanf(fov.angleUp),    std::tanf(fov.angleDown),
                g_camZNear, g_camZFar, projMtx
        );
        shaders_build_xr_view(
                pose.position.x,    pose.position.y,    pose.position.z,
                pose.orientation.x, pose.orientation.y, pose.orientation.z, pose.orientation.w,
                viewMtx
        );

        std::memcpy(g_eyeProjMtx[eye], projMtx, 16 * sizeof(float));
        std::memcpy(g_eyeViewMtx[eye], viewMtx, 16 * sizeof(float));

        VrEyeFovTan fovTan = vr_get_eye_fov_tan(eye);
        float tanFovHalf = fovTan.tanHalfWidth;

        g_eyeTanHalfFov[eye] = tanFovHalf;

    }



    vr_update_head_tracking(g_frameState.predictedDisplayTime);
    update_vr_controllers(g_frameState.predictedDisplayTime);
    controller_pose();
    vr_pointer_update();

    return true;
}


// ============================================================================
// STEREO API
// ============================================================================


float* vr_get_eye_proj_mtx(int eye) { return g_eyeProjMtx[eye]; }

// Standard OpenGL-convention combined Proj*View for one eye, in play space.
// Used by anything that draws with plain GLSL (mat4 * vec4) instead of the
// game's own fast3d vertex shader convention — e.g. the pause-menu hub.
extern "C" void vr_get_eye_view_proj_gl(int eye, float outVP[16]) {
    if (eye < 0 || eye > 1) { std::memset(outVP, 0, 16 * sizeof(float)); outVP[0]=outVP[5]=outVP[10]=outVP[15]=1.0f; return; }

    const XrView& view = g_frameViews[eye];

    float proj[16];
    ProjectionFromFov(view.fov, 0.05f, 2000.0f, proj);   // <-- far: 100 -> 2000

    float rot[16];
    QuatToMat4(view.pose.orientation, rot);
    rot[12] = view.pose.position.x;
    rot[13] = view.pose.position.y;
    rot[14] = view.pose.position.z;

    float viewMat[16];
    InvertRigidMat4(rot, viewMat);

    Mat4Mul(proj, viewMat, outVP);
}

extern "C" GLuint vr_get_current_multiview_swapchain_tex() {
    return g_currentMultiviewSwapchainTex;
}

// Begin OpenGL rendering for the current eye in multiview.
// Returns false when there is nothing valid to render into, in which case the caller must
// skip the display list rather than submit it against a dead swapchain.
bool vr_begin_eye_render()
{
    if (!gfx_get_current_rendering_api()->is_multiview()) return false;
    if (!vr_ensure_swapchain_images()) return false;


    XrSwapchainImageAcquireInfo acquireInfo = { XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO };
    if (XR_FAILED(xrAcquireSwapchainImage(g_vrState.swapchains[0], &acquireInfo, &g_acquiredSwapchainImageIndex))) {
        return false;
    }
    g_swapchainImageAcquired = true;

    XrSwapchainImageWaitInfo waitInfo = { XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO, nullptr, XR_INFINITE_DURATION };
    xrWaitSwapchainImage(g_vrState.swapchains[0], &waitInfo);


    GLuint swapchainTex = g_swapchainImages[0][g_acquiredSwapchainImageIndex].image;
    g_currentMultiviewSwapchainTex = swapchainTex;

    // For menu blur under the Oculus/Meta PC runtime
    if (is_meta_runtime && copy_fbo_menu) {
        gfx_copy_framebuffer(25, 0, 0, 0, true);
    }

    glBindFramebuffer(GL_FRAMEBUFFER, g_multiviewFBO);

    if (gfx_msaa_level > 1 && pfnFramebufferTextureMultisampleMultiviewOVR != nullptr) {
#ifdef ANDROID
        GLsizei safe_msaa = (gfx_msaa_level > 4) ? 4 : gfx_msaa_level; // limit to 4x
#else
        GLsizei safe_msaa = gfx_msaa_level; // no limit for PC

#endif
        pfnFramebufferTextureMultisampleMultiviewOVR(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,     swapchainTex,          0, safe_msaa, 0, 2);
        pfnFramebufferTextureMultisampleMultiviewOVR(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, g_multiviewDepthArray, 0, safe_msaa, 0, 2);
    } else if (glFramebufferTextureMultiviewOVR) {
        glFramebufferTextureMultiviewOVR(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,        swapchainTex,          0, 0, 2);
        glFramebufferTextureMultiviewOVR(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, g_multiviewDepthArray, 0, 0, 2);
    }

    glViewport(0, 0, g_internalRenderWidth, g_internalRenderHeight);
    glScissor(0, 0, g_internalRenderWidth, g_internalRenderHeight);
    // glClear honours the depth write mask: with the last draw of the previous
    // frame leaving it off, the depth buffer was never cleared and every
    // depth-tested draw failed against stale values (GoldenEye's rooms vanished
    // in stereo; the screen target, vr_screen.cpp, already set it). Restore it
    // afterwards so fast3d's cached depth state stays true.
    GLboolean depthMask = GL_TRUE;
    glGetBooleanv(GL_DEPTH_WRITEMASK, &depthMask);
    glDepthMask(GL_TRUE);
    GLfloat clearCol[4];
    glGetFloatv(GL_COLOR_CLEAR_VALUE, clearCol);
    if (g_screenVisible) {
        // The eye buffers go over the virtual screen (vr_end_frame's layer
        // order) and carry only the pointer: clear to transparent.
        glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    }
    glStencilMask(0xff);
    glClearStencil(0);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);   // stencil: fast3d's decal test
    glClearColor(clearCol[0], clearCol[1], clearCol[2], clearCol[3]);
    glDepthMask(depthMask);
    return true;
}

// End OpenGL rendering for the current eye and release swapchain image
void vr_end_eye_render()
{
    if (!gfx_get_current_rendering_api()->is_multiview()) return;
    if (!g_swapchainImageAcquired) return;

    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    XrSwapchainImageReleaseInfo releaseInfo = { XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO };
    xrReleaseSwapchainImage(g_vrState.swapchains[0], &releaseInfo);
    g_swapchainImageAcquired = false;

}

void vr_get_eye_view_offset(int eye, float* out_tx, float* out_ty, float* out_tz, float* out_tx_HUD)
{
    if (!g_vrInitialized || eye < 0 || eye > 1) {
        if (out_tx)     *out_tx     = 0.0f;
        if (out_ty)     *out_ty     = 0.0f;
        if (out_tz)     *out_tz     = 0.0f;
        if (out_tx_HUD) *out_tx_HUD = 0.0f;
        return;
    }

    // Actual distance between the two eyes, measured by the OpenXR runtime
    const XrVector3f& posL = g_frameViews[0].pose.position;
    const XrVector3f& posR = g_frameViews[1].pose.position;

    float dx = posR.x - posL.x;
    float dy = posR.y - posL.y;
    float dz = posR.z - posL.z;

    ipd_meters = std::sqrt(dx * dx + dy * dy + dz * dz);

    if (ipd_meters < 0.01f || ipd_meters > 0.10f)
        ipd_meters = 0.064f;

    float localEyeX = (eye == 0 ? -ipd_meters * 0.5f : ipd_meters * 0.5f);

    if (out_tx)   *out_tx   = localEyeX * vr_world_scale;
    if (out_ty)   *out_ty   = 0.0f;
    if (out_tz)   *out_tz   = 0.0f;


    if (out_tx_HUD) {
        VrEyeFovTan fovTan = vr_get_eye_fov_tan(eye);

        // CORRECTION: Use division to correctly project the distance.
        // Use 'VrHudDistance' (already set to 0.8f) to place the HUD at 80 cm.
        float parallaxOffset = (localEyeX / VrHudDistance) / fovTan.tanHalfWidth;

        // Canting managed separately via vr_get_horizontal_fov_offset_ratio
        float cantingOffset = vr_get_horizontal_fov_offset_ratio(eye);

        *out_tx_HUD = parallaxOffset + cantingOffset;
    }
}


// ============================================================================
// FRAME LIFECYCLE - Submit
// ============================================================================

extern "C" bool vr_end_frame_and_submit()
{
    if (!g_vrState.sessionRunning || g_vrState.session == XR_NULL_HANDLE)
        return false;
    if (!g_frameStarted)
        return false;

    bool submitted = false;
    if (gfx_get_current_rendering_api()->is_multiview()) {
        vr_submit_frame(g_frameState, g_haveRenderedViews ? g_renderedViews : g_frameViews);
        submitted = true;
    }

    // xrEndFrame has run (or there was nothing to submit), so the frame is closed. This
    // used to be left set on the success path, which meant g_frameStarted was stuck true
    // for the rest of the process: the "called during open frame" guard in vr_shutdown
    // fired unconditionally and could not distinguish a genuine mid-frame teardown.
    g_frameStarted = false;

    return submitted;
}

// ============================================================================
// VR Shutdown
// ============================================================================
extern "C" void vr_shutdown()
{
    LOGI("========== VR SHUTDOWN START ==========");

    // 1. Ensure no frame is currently in progress
    // (if we are between begin/end, we cannot destroy cleanly)
    if (g_frameStarted) {
        LOGI("vr_shutdown: called during open frame! Forcing end.");
        // Submit an empty frame to release the runtime
        vr_end_empty_frame(g_frameState.predictedDisplayTime);
        g_frameStarted = false;
    }
    // 2. FBOs OpenGL
    if (g_multiviewFBO) {
        glDeleteFramebuffers(1, &g_multiviewFBO);
        g_multiviewFBO = 0;
    }
    if (g_multiviewDepthArray) {
        glDeleteTextures(1, &g_multiviewDepthArray);
        g_multiviewDepthArray = 0;
    }
    g_currentMultiviewSwapchainTex = 0;

    // 3. Swapchains. Hand back any image still checked out before destroying anything --
    // tearing down a swapchain while the runtime thinks we hold one of its images is how a
    // teardown that lands mid-frame leaves an out-of-process runtime in a bad state.
    if (g_swapchainImageAcquired && g_vrState.swapchains[0] != XR_NULL_HANDLE) {
        XrSwapchainImageReleaseInfo releaseInfo = { XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO };
        xrReleaseSwapchainImage(g_vrState.swapchains[0], &releaseInfo);
    }
    g_swapchainImageAcquired = false;

    // Destroy both handles: [1] only exists when multiview is off, and is XR_NULL_HANDLE
    // otherwise. Only [0] is ever enumerated, so only its image cache needs clearing.
    for (int eye = 0; eye < 2; eye++) {
        if (g_vrState.swapchains[eye] != XR_NULL_HANDLE) {
            xrDestroySwapchain(g_vrState.swapchains[eye]);
            g_vrState.swapchains[eye] = XR_NULL_HANDLE;
        }
    }
    g_swapchainImagesInit[0] = false;
    g_swapchainImages[0].clear();

    if (g_menuSwapchain != XR_NULL_HANDLE) {
        xrDestroySwapchain(g_menuSwapchain);
        g_menuSwapchain = XR_NULL_HANDLE;
        g_menuSwapchainImages.clear();
    }
    if (g_menuSwapchainR != XR_NULL_HANDLE) {
        xrDestroySwapchain(g_menuSwapchainR);
        g_menuSwapchainR = XR_NULL_HANDLE;
        g_menuSwapchainImagesR.clear();
    }
    if (g_menuSwapchainH != XR_NULL_HANDLE) {
        xrDestroySwapchain(g_menuSwapchainH);
        g_menuSwapchainH = XR_NULL_HANDLE;
        g_menuSwapchainImagesH.clear();
    }
    vr_screen_destroy_swapchain();
    g_screenRecenterTimes.clear();

    // 4. Reference spaces
    if (g_vrState.viewSpace != XR_NULL_HANDLE) {
        xrDestroySpace(g_vrState.viewSpace);
        g_vrState.viewSpace = XR_NULL_HANDLE;
    }
    if (g_vrState.playSpace != XR_NULL_HANDLE) {
        xrDestroySpace(g_vrState.playSpace);
        g_vrState.playSpace = XR_NULL_HANDLE;
    }
    // 5. Session
    if (g_vrState.session != XR_NULL_HANDLE) {
        if (g_vrState.sessionRunning) {
            xrEndSession(g_vrState.session);
            g_vrState.sessionRunning = false;
        }
        xrDestroySession(g_vrState.session);
        g_vrState.session = XR_NULL_HANDLE;
    }
    // 6. Instance
    if (g_vrState.instance != XR_NULL_HANDLE) {
        xrDestroyInstance(g_vrState.instance);
        g_vrState.instance = XR_NULL_HANDLE;
    }
    // 7. Reset globals
    g_vrInitialized = false;
    g_internalRenderWidth  = 0;
    g_internalRenderHeight = 0;
    g_frameStarted = false;

    LOGI("========== VR SHUTDOWN COMPLETE ==========");
}

// ============================================================================
// Restart VR with new scale
// ============================================================================

extern "C" bool vr_restart_with_new_scale(float new_scale) {
    LOGI("vr_restart_with_new_scale: %.2f -> %.2f", RENDER_SCALE, new_scale);
    RENDER_SCALE = new_scale;
    vr_shutdown();

    vr_initialize();
    if (!g_vrInitialized) {
        LOGE("vr_restart_with_new_scale: re-init FAILED at scale %.2f", new_scale);
    }
    return g_vrInitialized;
}

// A resolution change arrives from the options menu, which the game ticks with an OpenXR
// frame open. Rebuilding the instance there is what wedges the runtime, so the request is
// parked here and acted on from vr_apply_pending_scale() once the frame has been submitted.
static float g_pendingRenderScale = 0.0f;

extern "C" void vr_request_scale(float new_scale) {
    if (new_scale > 0.0f) {
        g_pendingRenderScale = new_scale;
    }
}

// Called from mainTick after vr_end_frame_and_submit(), where no frame is open.
extern "C" bool vr_apply_pending_scale(void) {
    const float scale = g_pendingRenderScale;
    if (scale <= 0.0f) {
        return true;
    }
    g_pendingRenderScale = 0.0f;

    if (scale == RENDER_SCALE) {
        return true;
    }

    return vr_restart_with_new_scale(scale);
}

