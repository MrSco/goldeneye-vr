/*
 * Vision Pro, native: the OpenXR runtime the game links to
 * (docs/visionos-native.md).
 *
 * Apple has no OpenXR. port/vr is written against it (vr_openxr.cpp,
 * vr_input.cpp), and it runs unchanged on the Quest, on PC VR, and on Vision
 * Pro through Klepton (an Android emulation with an OpenXR runtime of its
 * own). Here the game is built for visionOS itself, and this file is the
 * runtime: the calls port/vr makes, answered with
 *
 *   - Compositor Services for the frame clock and the pictures: xrWaitFrame
 *     is the compositor's frame up to its drawable, xrEndFrame draws the
 *     submitted layers into the drawable and presents it;
 *   - ARKit's world tracking for the head (the device anchor at a time);
 *   - ANGLE for the swapchains: their images are Metal textures the game's GL
 *     draws into (vos_egl.mm), eye arrays copied into Metal textures at the
 *     frame's end (ANGLE's EGL images are 2D), and a shared event orders GL's
 *     work before Metal's.
 *
 * Coordinates: OpenXR's and ARKit's are the same (metres, +Y up, right handed,
 * -Z forward). ARKit's origin (the floor under the player when the space
 * opened) is the world here: STAGE and LOCAL_FLOOR are it, LOCAL is it raised
 * to the head's first height, VIEW is the head.
 *
 * Images: a GL texture's row 0 is its bottom, and Metal's row 0 the same row
 * (ANGLE flips only the default framebuffer), so a sub-image rectangle is read
 * bottom-up, as on the Quest (vr_openxr.cpp vr_subimage_y), and an eye image
 * is sampled upside down into the drawable, whose row 0 is its top.
 *
 * Colours: the game writes display-encoded (sRGB) values into GL_RGBA8 images.
 * They are sampled through an sRGB view of the same bytes, so the drawable
 * (an sRGB format) receives them unchanged.
 *
 * Every call comes from the game's thread, which holds the GL context.
 */
#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#import <QuartzCore/QuartzCore.h>
#import <CompositorServices/CompositorServices.h>
#import <ARKit/ARKit.h>

#define GL_GLEXT_PROTOTYPES
#include <EGL/egl.h>
#include <GLES3/gl3.h>
#include <GLES2/gl2ext.h>

#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>

#include <deque>
#include <map>
#include <string>
#include <vector>
#include <cmath>
#include <cstring>
#include <mutex>

#include "vos.h"

// ============================================================================
// Small things
// ============================================================================

static XrTime vosTime(double seconds) { return (XrTime)llround(seconds * 1e9); }
static double vosSeconds(XrTime t) { return (double)t * 1e-9; }

static simd_float4x4 vosMatrixFromPose(const XrPosef &p)
{
    simd_quatf q = simd_quaternion(p.orientation.x, p.orientation.y, p.orientation.z, p.orientation.w);
    if (simd_length(q.vector) < 1e-6f) q = simd_quaternion(0.0f, 0.0f, 0.0f, 1.0f);
    simd_float4x4 m = simd_matrix4x4(simd_normalize(q));
    m.columns[3] = simd_make_float4(p.position.x, p.position.y, p.position.z, 1.0f);
    return m;
}

static XrPosef vosPoseFromMatrix(const simd_float4x4 &m)
{
    simd_float3x3 r = simd_matrix(m.columns[0].xyz, m.columns[1].xyz, m.columns[2].xyz);
    // rigid transforms only, but ARKit's can carry a hair of scale
    r.columns[0] = simd_normalize(r.columns[0]);
    r.columns[1] = simd_normalize(r.columns[1]);
    r.columns[2] = simd_normalize(r.columns[2]);
    simd_quatf q = simd_quaternion(r);
    XrPosef p;
    p.orientation = { q.vector.x, q.vector.y, q.vector.z, q.vector.w };
    p.position = { m.columns[3].x, m.columns[3].y, m.columns[3].z };
    return p;
}

static simd_float4x4 vosTranslate(float x, float y, float z)
{
    simd_float4x4 m = matrix_identity_float4x4;
    m.columns[3] = simd_make_float4(x, y, z, 1.0f);
    return m;
}

// ============================================================================
// Paths
// ============================================================================

static std::vector<std::string> s_paths;   // XrPath = index + 1

static XrPath vosPath(const char *s)
{
    for (size_t i = 0; i < s_paths.size(); i++) {
        if (s_paths[i] == s) return (XrPath)(i + 1);
    }
    s_paths.push_back(s);
    return (XrPath)s_paths.size();
}

static const char *vosPathString(XrPath p)
{
    return p >= 1 && p <= s_paths.size() ? s_paths[p - 1].c_str() : "";
}

// ============================================================================
// Objects
// ============================================================================

struct VosInstanceObj { uint32_t magic = 0x564f5349; };
struct VosSessionObj { uint32_t magic = 0x564f5353; };

enum VosSpaceKind { VOS_SPACE_REFERENCE, VOS_SPACE_ACTION };
struct VosSpaceObj {
    VosSpaceKind kind;
    XrReferenceSpaceType type;
    XrAction action;
    XrPath subaction;
    simd_float4x4 offset;   // poseInReferenceSpace / poseInActionSpace
};

struct VosImage {
    GLuint tex = 0;                 // the game's GL name
    id<MTLTexture> mtl;             // its Metal texture (2D swapchains)
    id<MTLTexture> mtlSrgb;         // the same bytes read as sRGB
    // arrays: each layer copied into a Metal texture at the frame's end
    GLuint copyTex[2] = { 0, 0 };
    id<MTLTexture> copyMtl[2];
    id<MTLTexture> copySrgb[2];
    uint64_t lastUse = 0;           // the compositor frame that last read it
};

struct VosSwapchainObj {
    uint32_t w = 0, h = 0, arraySize = 1;
    int64_t format = 0;
    std::vector<VosImage> images;
    std::deque<uint32_t> acquired;
    uint32_t next = 0;
    int32_t lastReleased = -1;
    uint64_t copiedFrame = 0;       // arrays: the frame the copies are for
};

struct VosActionSetObj { std::string name; };
struct VosActionObj {
    std::string name;
    XrActionType type;
    std::vector<XrPath> subactions;
    std::vector<XrPath> bindings;   // Touch's
    // per hand (0 left, 1 right) and both (2): this sync's and the last
    float cur[3][2] = {}, prev[3][2] = {};
    XrTime changed[3] = {};
};

static VosInstanceObj *s_instance;
static VosSessionObj *s_session;
static bool s_sessionRunning;
static std::deque<XrEventDataBuffer> s_events;
static std::vector<VosActionObj *> s_actions;
static XrPath s_touchProfile, s_handPath[2];
static bool s_haveFocus;

extern "C" void pcReportForeground(int active);   // port/pc/pc_reports.cpp

static void vosPostState(XrSessionState state)
{
    // the crash report's marker: a run that ends while focused is a crash
    // (the Klepton build's MainActivity stand-in, vr_openxr.cpp)
    if (state == XR_SESSION_STATE_FOCUSED) pcReportForeground(1);
    else if (state == XR_SESSION_STATE_VISIBLE || state == XR_SESSION_STATE_STOPPING ||
             state == XR_SESSION_STATE_EXITING) pcReportForeground(0);
    XrEventDataBuffer b{};
    auto *e = reinterpret_cast<XrEventDataSessionStateChanged *>(&b);
    e->type = XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED;
    e->session = (XrSession)s_session;
    e->state = state;
    e->time = vosTime(vosNow());
    s_events.push_back(b);
    vosLog("visionos: xr: session state %d", (int)state);
}

// ============================================================================
// The head: ARKit world tracking
// ============================================================================

static ar_session_t s_arSession;
static ar_world_tracking_provider_t s_worldTracking;
static bool s_arStarted, s_arSupported;
static simd_float4x4 s_lastHead;
static bool s_haveHead;
static float s_localHeight = 1.6f;   // LOCAL's origin: the first tracked head height
static bool s_localSet;

static void vosStartTracking(void)
{
    if (s_arStarted) return;
    s_arStarted = true;
    s_lastHead = vosTranslate(0.0f, 1.6f, 0.0f);
    s_arSupported = ar_world_tracking_provider_is_supported();
    if (!s_arSupported) {
        vosLog("visionos: ARKit world tracking is not supported here: the head stays at 1.6 m");
        return;
    }
    ar_world_tracking_configuration_t config = ar_world_tracking_configuration_create();
    s_worldTracking = ar_world_tracking_provider_create(config);
    s_arSession = ar_session_create();
    ar_data_providers_t providers = ar_data_providers_create_with_data_providers(s_worldTracking, nullptr);
    ar_session_run(s_arSession, providers);
    vosLog("visionos: ARKit world tracking started");
}

/* The head at a time (the last good one when ARKit has none); returns the
 * device anchor queried, for the drawable, or nil. */
static ar_device_anchor_t vosQueryHead(double seconds, simd_float4x4 *out, bool *tracked)
{
    if (tracked) *tracked = false;
    *out = s_lastHead;
    if (!s_worldTracking) return nil;
    ar_device_anchor_t anchor = ar_device_anchor_create();
    const ar_device_anchor_query_status_t st =
        ar_world_tracking_provider_query_device_anchor_at_timestamp(s_worldTracking, seconds, anchor);
    if (st != ar_device_anchor_query_status_success) return anchor;
    s_lastHead = ar_device_anchor_get_origin_from_anchor_transform(anchor);
    s_haveHead = true;
    if (!s_localSet) {
        s_localSet = true;
        s_localHeight = s_lastHead.columns[3].y;
        vosLog("visionos: first tracked head at (%.2f %.2f %.2f) m", s_lastHead.columns[3].x,
               s_lastHead.columns[3].y, s_lastHead.columns[3].z);
    }
    *out = s_lastHead;
    if (tracked) *tracked = true;
    return anchor;
}

extern "C" int vosHeadAt(double seconds, simd_float4x4 *out)
{
    bool tracked;
    vosQueryHead(seconds, out, &tracked);
    return tracked ? 1 : 0;
}

extern "C" double vosNow(void)
{
    return CACurrentMediaTime();
}

// ============================================================================
// The compositor's views
// ============================================================================

struct VosViewInfo {
    simd_float4x4 deviceFromView;
    float tanL, tanR, tanU, tanD;   // signed tangents (left and down negative)
};
static VosViewInfo s_views[2];
static size_t s_viewCount;
static uint32_t s_eyeW = 1888, s_eyeH = 1824;
static bool s_viewsKnown;

static void vosDefaultViews(void)
{
    for (int i = 0; i < 2; i++) {
        s_views[i].deviceFromView = vosTranslate(i == 0 ? -0.032f : 0.032f, 0.0f, 0.0f);
        s_views[i].tanL = -1.1f;
        s_views[i].tanR = 1.1f;
        s_views[i].tanU = 1.1f;
        s_views[i].tanD = -1.1f;
    }
    s_viewCount = 2;
}

/* Each view's transform and field of view, from the drawable (the
 * projection's tangents: cp_view_get_tangents aborts under mixed immersion,
 * Midas found, so they are read off the projection the compositor builds). */
static void vosReadViews(cp_drawable_t drawable)
{
    const size_t n = cp_drawable_get_view_count(drawable);
    s_viewCount = n < 1 ? 1 : (n > 2 ? 2 : n);
    for (size_t v = 0; v < s_viewCount; v++) {
        cp_view_t view = cp_drawable_get_view(drawable, v);
        VosViewInfo &vi = s_views[v];
        vi.deviceFromView = cp_view_get_transform(view);
        const simd_float4x4 p = cp_drawable_compute_projection(drawable, cp_axis_direction_convention_right_up_back, v);
        const float sx = p.columns[0].x, ox = p.columns[2].x;
        const float sy = p.columns[1].y, oy = p.columns[2].y;
        if (fabsf(sx) > 1e-4f && fabsf(sy) > 1e-4f) {
            vi.tanL = (ox - 1.0f) / sx;
            vi.tanR = (ox + 1.0f) / sx;
            vi.tanD = (oy - 1.0f) / sy;
            vi.tanU = (oy + 1.0f) / sy;
        }
        if (v == 0) {
            cp_view_texture_map_t map = cp_view_get_view_texture_map(view);
            const MTLViewport vp = cp_view_texture_map_get_viewport(map);
            if (vp.width >= 16 && vp.height >= 16) {
                s_eyeW = (uint32_t)vp.width;
                s_eyeH = (uint32_t)vp.height;
            }
        }
    }
    // the simulator draws one view: both eyes see it
    if (s_viewCount == 1) s_views[1] = s_views[0];
    if (!s_viewsKnown) {
        s_viewsKnown = true;
        vosLog("visionos: compositor: %zu view(s), %ux%u, fov L(%.2f %.2f %.2f %.2f) R(%.2f %.2f %.2f %.2f) tangents, "
               "eye offset %.4f m", n, s_eyeW, s_eyeH, s_views[0].tanL, s_views[0].tanR, s_views[0].tanU,
               s_views[0].tanD, s_views[1].tanL, s_views[1].tanR, s_views[1].tanU, s_views[1].tanD,
               s_views[1].deviceFromView.columns[3].x - s_views[0].deviceFromView.columns[3].x);
    }
}

// ============================================================================
// The compositor's frame
// ============================================================================

static cp_layer_renderer_t vosLayer(void)
{
    return (__bridge cp_layer_renderer_t)vosLayerRenderer();
}

static cp_frame_t s_frame;
static cp_drawable_t s_drawable;
static ar_device_anchor_t s_frameAnchor;
static simd_float4x4 s_frameHead;
static double s_frameDisplay, s_lastDisplay;
static double s_period = 1.0 / 90.0;
static uint64_t s_frameNo;
static bool s_frameBegun;

static id<MTLDevice> s_dev;
static id<MTLCommandQueue> s_queue;
static id<MTLSharedEvent> s_doneEvent;   // signalled with a frame's number when its composite has run
static id<MTLRenderPipelineState> s_projPipe[3], s_quadPipe[3];   // opaque, premultiplied, straight alpha
static id<MTLSamplerState> s_sampler;
static id<MTLDepthStencilState> s_noDepth;
static MTLPixelFormat s_colorFormat, s_depthFormat;

static const char *kVosShaders = R"(
#include <metal_stdlib>
using namespace metal;
struct VOut { float4 pos [[position]]; float2 uv; };
struct ProjParams { float4 dst; float4 src; float4 rect; float opaque; };
struct QuadParams { float4x4 mvp; float4 rect; float opaque; };
vertex VOut vos_full_vs(uint vid [[vertex_id]]) {
    float2 p = float2((vid << 1) & 2, vid & 2);
    VOut o;
    o.pos = float4(p * 2.0 - 1.0, 0.0, 1.0);
    o.uv = float2(p.x, 1.0 - p.y);
    return o;
}
fragment float4 vos_proj_fs(VOut in [[stage_in]], texture2d<float> tex [[texture(0)]],
                            sampler s [[sampler(0)]], constant ProjParams &p [[buffer(0)]]) {
    // this view's pixel as a direction (tangents), then where the eye image saw it
    float tx = mix(p.dst.x, p.dst.y, in.uv.x);
    float ty = mix(p.dst.z, p.dst.w, in.uv.y);
    float u = (tx - p.src.x) / (p.src.y - p.src.x);
    float vtop = (p.src.z - ty) / (p.src.z - p.src.w);
    if (u < 0.0 || u > 1.0 || vtop < 0.0 || vtop > 1.0) return float4(0.0, 0.0, 0.0, p.opaque);
    // the image's rows run bottom-up (GL's)
    float2 st = float2(p.rect.x + u * p.rect.z, p.rect.y + (1.0 - vtop) * p.rect.w);
    float4 c = tex.sample(s, st);
    return p.opaque > 0.5 ? float4(c.rgb, 1.0) : c;
}
vertex VOut vos_quad_vs(uint vid [[vertex_id]], constant QuadParams &q [[buffer(0)]]) {
    float2 c = float2(vid & 1, vid >> 1);
    VOut o;
    o.pos = q.mvp * float4(c.x - 0.5, c.y - 0.5, 0.0, 1.0);
    o.uv = float2(q.rect.x + c.x * q.rect.z, q.rect.y + c.y * q.rect.w);
    return o;
}
fragment float4 vos_quad_fs(VOut in [[stage_in]], texture2d<float> tex [[texture(0)]],
                            sampler s [[sampler(0)]], constant QuadParams &q [[buffer(0)]]) {
    float4 c = tex.sample(s, in.uv);
    return q.opaque > 0.5 ? float4(c.rgb, 1.0) : c;
}
)";

static bool vosBuildPipelines(MTLPixelFormat color, MTLPixelFormat depth)
{
    NSError *err = nil;
    id<MTLLibrary> lib = [s_dev newLibraryWithSource:@(kVosShaders) options:nil error:&err];
    if (!lib) {
        vosLog("visionos: compositor shaders: %s", err.localizedDescription.UTF8String);
        return false;
    }
    for (int kind = 0; kind < 2; kind++) {
        for (int blend = 0; blend < 3; blend++) {
            MTLRenderPipelineDescriptor *d = [MTLRenderPipelineDescriptor new];
            d.vertexFunction = [lib newFunctionWithName:kind == 0 ? @"vos_full_vs" : @"vos_quad_vs"];
            d.fragmentFunction = [lib newFunctionWithName:kind == 0 ? @"vos_proj_fs" : @"vos_quad_fs"];
            d.colorAttachments[0].pixelFormat = color;
            d.depthAttachmentPixelFormat = depth;
            if (blend > 0) {
                MTLRenderPipelineColorAttachmentDescriptor *a = d.colorAttachments[0];
                a.blendingEnabled = YES;
                a.rgbBlendOperation = MTLBlendOperationAdd;
                a.alphaBlendOperation = MTLBlendOperationAdd;
                a.sourceRGBBlendFactor = blend == 1 ? MTLBlendFactorOne : MTLBlendFactorSourceAlpha;
                a.sourceAlphaBlendFactor = MTLBlendFactorOne;
                a.destinationRGBBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
                a.destinationAlphaBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
            }
            id<MTLRenderPipelineState> ps = [s_dev newRenderPipelineStateWithDescriptor:d error:&err];
            if (!ps) {
                vosLog("visionos: compositor pipeline: %s", err.localizedDescription.UTF8String);
                return false;
            }
            (kind == 0 ? s_projPipe : s_quadPipe)[blend] = ps;
        }
    }
    MTLSamplerDescriptor *sd = [MTLSamplerDescriptor new];
    sd.minFilter = MTLSamplerMinMagFilterLinear;
    sd.magFilter = MTLSamplerMinMagFilterLinear;
    sd.sAddressMode = MTLSamplerAddressModeClampToEdge;
    sd.tAddressMode = MTLSamplerAddressModeClampToEdge;
    s_sampler = [s_dev newSamplerStateWithDescriptor:sd];
    MTLDepthStencilDescriptor *dd = [MTLDepthStencilDescriptor new];
    dd.depthCompareFunction = MTLCompareFunctionAlways;
    dd.depthWriteEnabled = NO;
    s_noDepth = [s_dev newDepthStencilStateWithDescriptor:dd];
    s_colorFormat = color;
    s_depthFormat = depth;
    vosLog("visionos: compositor pipelines for colour %lu, depth %lu", (unsigned long)color, (unsigned long)depth);
    return true;
}

static bool vosEnsureMetal(cp_drawable_t drawable)
{
    id<MTLTexture> c = cp_drawable_get_color_texture(drawable, 0);
    id<MTLTexture> d = cp_drawable_get_depth_texture(drawable, 0);
    if (!s_dev) {
        s_dev = (__bridge id<MTLDevice>)vosMetalDevice();
        if (!s_dev) s_dev = c.device;
        if (s_dev != c.device) vosLog("visionos: ANGLE's MTLDevice is not the compositor's (%s, %s)",
                                      s_dev.name.UTF8String, c.device.name.UTF8String);
        s_queue = [s_dev newCommandQueue];
        s_doneEvent = [s_dev newSharedEvent];
    }
    if (!s_projPipe[0] || c.pixelFormat != s_colorFormat || (d ? d.pixelFormat : MTLPixelFormatInvalid) != s_depthFormat) {
        return vosBuildPipelines(c.pixelFormat, d ? d.pixelFormat : MTLPixelFormatInvalid);
    }
    return true;
}

/* Waits for the compositor's next frame up to its drawable. 0: no drawable
 * (the frame is skipped). */
static bool vosNextFrame(void)
{
    cp_layer_renderer_t layer = vosLayer();
    if (!layer) return false;
    switch (cp_layer_renderer_get_state(layer)) {
    case cp_layer_renderer_state_paused:
        vosLog("visionos: compositor paused");
        if (s_haveFocus) { s_haveFocus = false; vosPostState(XR_SESSION_STATE_VISIBLE); }
        cp_layer_renderer_wait_until_running(layer);
        vosLog("visionos: compositor running");
        return false;
    case cp_layer_renderer_state_invalidated:
        // the player closed the space: there is nothing else to show, so the
        // game saves and goes (an app in one full space has no window to return to)
        vosLog("visionos: the immersive space closed");
        vosSpaceClosed();
        return false;
    default:
        break;
    }
    if (!s_haveFocus && s_sessionRunning) { s_haveFocus = true; vosPostState(XR_SESSION_STATE_FOCUSED); }

    cp_frame_t frame = cp_layer_renderer_query_next_frame(layer);
    if (!frame) return false;
    cp_frame_timing_t timing = cp_frame_predict_timing(frame);
    if (!timing) return false;
    cp_frame_start_update(frame);
    cp_frame_end_update(frame);
    cp_time_wait_until(cp_frame_timing_get_optimal_input_time(timing));
    cp_frame_start_submission(frame);
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
    // the singular query: with a NULL drawable, end_submission must NOT run
    // (it aborts the process, Midas found)
    cp_drawable_t drawable = cp_frame_query_drawable(frame);
#pragma clang diagnostic pop
    if (!drawable) return false;

    s_frame = frame;
    s_drawable = drawable;
    vosReadViews(drawable);
    s_frameDisplay = cp_time_to_cf_time_interval(
        cp_frame_timing_get_presentation_time(cp_drawable_get_frame_timing(drawable)));
    if (s_lastDisplay > 0.0) {
        const double dt = s_frameDisplay - s_lastDisplay;
        if (dt > 0.004 && dt < 0.05) s_period += (dt - s_period) * 0.1;
    }
    s_lastDisplay = s_frameDisplay;
    // the head at THIS frame's presentation: the compositor reprojects with it
    s_frameAnchor = vosQueryHead(s_frameDisplay, &s_frameHead, nullptr);
    return true;
}

// ============================================================================
// Spaces
// ============================================================================

static simd_float4x4 vosHeadFor(double seconds)
{
    if (s_drawable && fabs(seconds - s_frameDisplay) < 0.0005) return s_frameHead;
    simd_float4x4 h;
    (void)vosQueryHead(seconds, &h, nullptr);
    return h;
}

/* world (ARKit's origin) from a space, at a time; false: not located */
static bool vosWorldFromSpace(const VosSpaceObj *s, double seconds, simd_float4x4 *out)
{
    if (s->kind == VOS_SPACE_REFERENCE) {
        switch (s->type) {
        case XR_REFERENCE_SPACE_TYPE_VIEW:
            *out = simd_mul(vosHeadFor(seconds), s->offset);
            return true;
        case XR_REFERENCE_SPACE_TYPE_LOCAL:
            *out = simd_mul(vosTranslate(0.0f, s_localHeight, 0.0f), s->offset);
            return true;
        default:   // STAGE, LOCAL_FLOOR
            *out = s->offset;
            return true;
        }
    }
    const VosActionObj *a = (const VosActionObj *)s->action;
    int hand = -1;
    if (s->subaction == s_handPath[0]) hand = 0;
    else if (s->subaction == s_handPath[1]) hand = 1;
    else if (a && !a->subactions.empty()) hand = a->subactions[0] == s_handPath[0] ? 0 : 1;
    if (hand < 0) return false;
    const VosHand *h = vosInputHand(hand);
    if (!h || !h->poseValid) return false;
    *out = simd_mul(h->grip, s->offset);
    return true;
}

// ============================================================================
// Swapchains
// ============================================================================

static id<MTLTexture> vosNewTexture(uint32_t w, uint32_t h)
{
    MTLTextureDescriptor *td = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
                                                                                  width:w height:h mipmapped:NO];
    td.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead | MTLTextureUsagePixelFormatView;
    // shared, not private: ANGLE reads an EGL image back with getBytes (Midas)
    td.storageMode = MTLStorageModeShared;
    return [(__bridge id<MTLDevice>)vosMetalDevice() newTextureWithDescriptor:td];
}

static id<MTLTexture> vosSrgbView(id<MTLTexture> t)
{
    return [t newTextureViewWithPixelFormat:MTLPixelFormatRGBA8Unorm_sRGB];
}

/* waits until the compositor has read an image (its last frame's commands ran) */
static void vosWaitImage(const VosImage &img)
{
    if (!s_doneEvent || img.lastUse == 0 || s_doneEvent.signaledValue >= img.lastUse) return;
    if (![s_doneEvent waitUntilSignaledValue:img.lastUse timeoutMS:100]) {
        vosLog("visionos: swapchain image still in the compositor's hands after 100 ms (frame %llu)",
               (unsigned long long)img.lastUse);
    }
}

// ============================================================================
// The composite
// ============================================================================

struct VosLayerDraw {
    bool projection;
    int blend;                       // 0 opaque, 1 premultiplied, 2 straight alpha
    // projection: per eye
    id<MTLTexture> eyeTex[2];
    float eyeTan[2][4];
    float eyeRect[2][4];
    // quad
    id<MTLTexture> tex;
    simd_float4x4 worldFromQuad;     // scaled by its size
    float rect[4];
    int eyes;                        // XrEyeVisibility
};

static int vosBlendOf(XrCompositionLayerFlags f)
{
    if (!(f & XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT)) return 0;
    return (f & XR_COMPOSITION_LAYER_UNPREMULTIPLIED_ALPHA_BIT) ? 2 : 1;
}

static void vosTangents(const XrFovf &f, float out[4])
{
    out[0] = tanf(f.angleLeft);
    out[1] = tanf(f.angleRight);
    out[2] = tanf(f.angleUp);
    out[3] = tanf(f.angleDown);
}

/* An array swapchain's released image, its layers copied into Metal textures
 * (once a frame however many views read it). */
static bool vosCopyArrayImage(VosSwapchainObj *sc)
{
    if (sc->lastReleased < 0) return false;
    VosImage &img = sc->images[sc->lastReleased];
    if (sc->copiedFrame == s_frameNo) return true;
    sc->copiedFrame = s_frameNo;
    GLint prevRead = 0, prevDraw = 0;
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &prevRead);
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &prevDraw);
    const GLboolean scissor = glIsEnabled(GL_SCISSOR_TEST);
    if (scissor) glDisable(GL_SCISSOR_TEST);
    static GLuint s_readFbo, s_drawFbo;
    if (!s_readFbo) glGenFramebuffers(1, &s_readFbo);
    if (!s_drawFbo) glGenFramebuffers(1, &s_drawFbo);
    for (uint32_t layer = 0; layer < sc->arraySize && layer < 2; layer++) {
        glBindFramebuffer(GL_READ_FRAMEBUFFER, s_readFbo);
        glFramebufferTextureLayer(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, img.tex, 0, (GLint)layer);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, s_drawFbo);
        glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, img.copyTex[layer], 0);
        glBlitFramebuffer(0, 0, (GLint)sc->w, (GLint)sc->h, 0, 0, (GLint)sc->w, (GLint)sc->h,
                          GL_COLOR_BUFFER_BIT, GL_NEAREST);
    }
    glBindFramebuffer(GL_READ_FRAMEBUFFER, (GLuint)prevRead);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, (GLuint)prevDraw);
    if (scissor) glEnable(GL_SCISSOR_TEST);
    return true;
}

static VosImage *vosReleasedImage(XrSwapchain h)
{
    auto *sc = (VosSwapchainObj *)h;
    if (!sc || sc->lastReleased < 0 || sc->lastReleased >= (int32_t)sc->images.size()) return nullptr;
    return &sc->images[sc->lastReleased];
}

static void vosComposite(const XrFrameEndInfo *info)
{
    std::vector<VosLayerDraw> draws;
    const double t = info->displayTime ? vosSeconds(info->displayTime) : s_frameDisplay;
    for (uint32_t i = 0; i < info->layerCount; i++) {
        const XrCompositionLayerBaseHeader *base = info->layers[i];
        if (!base) continue;
        if (base->type == XR_TYPE_COMPOSITION_LAYER_PROJECTION) {
            const auto *l = (const XrCompositionLayerProjection *)base;
            if (l->viewCount < 1) continue;
            VosLayerDraw d{};
            d.projection = true;
            d.blend = vosBlendOf(l->layerFlags);
            bool ok = true;
            for (uint32_t e = 0; e < 2; e++) {
                const XrCompositionLayerProjectionView &pv = l->views[e < l->viewCount ? e : 0];
                auto *sc = (VosSwapchainObj *)pv.subImage.swapchain;
                VosImage *img = vosReleasedImage(pv.subImage.swapchain);
                if (!sc || !img) { ok = false; break; }
                if (sc->arraySize > 1) {
                    vosCopyArrayImage(sc);
                    d.eyeTex[e] = img->copySrgb[pv.subImage.imageArrayIndex < 2 ? pv.subImage.imageArrayIndex : 0];
                } else {
                    d.eyeTex[e] = img->mtlSrgb;
                }
                img->lastUse = s_frameNo;
                vosTangents(pv.fov, d.eyeTan[e]);
                const XrRect2Di &r = pv.subImage.imageRect;
                d.eyeRect[e][0] = (float)r.offset.x / (float)sc->w;
                d.eyeRect[e][1] = (float)r.offset.y / (float)sc->h;
                d.eyeRect[e][2] = (float)(r.extent.width > 0 ? r.extent.width : (int32_t)sc->w) / (float)sc->w;
                d.eyeRect[e][3] = (float)(r.extent.height > 0 ? r.extent.height : (int32_t)sc->h) / (float)sc->h;
            }
            if (ok && d.eyeTex[0] && d.eyeTex[1]) draws.push_back(d);
        } else if (base->type == XR_TYPE_COMPOSITION_LAYER_QUAD) {
            const auto *q = (const XrCompositionLayerQuad *)base;
            auto *sc = (VosSwapchainObj *)q->subImage.swapchain;
            VosImage *img = vosReleasedImage(q->subImage.swapchain);
            if (!sc || !img) continue;
            simd_float4x4 worldFromSpace;
            if (!vosWorldFromSpace((const VosSpaceObj *)q->space, t, &worldFromSpace)) continue;
            VosLayerDraw d{};
            d.projection = false;
            d.blend = vosBlendOf(q->layerFlags);
            if (sc->arraySize > 1) {
                vosCopyArrayImage(sc);
                d.tex = img->copySrgb[q->subImage.imageArrayIndex < 2 ? q->subImage.imageArrayIndex : 0];
            } else {
                d.tex = img->mtlSrgb;
            }
            if (!d.tex) continue;
            img->lastUse = s_frameNo;
            simd_float4x4 scale = matrix_identity_float4x4;
            scale.columns[0].x = q->size.width;
            scale.columns[1].y = q->size.height;
            d.worldFromQuad = simd_mul(simd_mul(worldFromSpace, vosMatrixFromPose(q->pose)), scale);
            const XrRect2Di &r = q->subImage.imageRect;
            d.rect[0] = (float)r.offset.x / (float)sc->w;
            d.rect[1] = (float)r.offset.y / (float)sc->h;
            d.rect[2] = (float)r.extent.width / (float)sc->w;
            d.rect[3] = (float)r.extent.height / (float)sc->h;
            d.eyes = (int)q->eyeVisibility;
            draws.push_back(d);
        } else {
            static int said;
            if (said++ < 4) vosLog("visionos: xrEndFrame: layer type %d not composited", (int)base->type);
        }
    }

    // GL's work for these images, before Metal reads them
    void *evtRaw = nullptr;
    uint64_t evtValue = 0;
    vosGLFence(&evtRaw, &evtValue);
    id<MTLSharedEvent> glDone = evtRaw ? (__bridge_transfer id<MTLSharedEvent>)evtRaw : nil;

    cp_drawable_t drawable = s_drawable;
    if (!vosEnsureMetal(drawable)) return;
    id<MTLCommandBuffer> cmd = [s_queue commandBuffer];
    if (glDone) [cmd encodeWaitForEvent:glDone value:evtValue];

    const size_t views = cp_drawable_get_view_count(drawable);
    for (size_t v = 0; v < views; v++) {
        cp_view_t view = cp_drawable_get_view(drawable, v);
        cp_view_texture_map_t map = cp_view_get_view_texture_map(view);
        const size_t texIdx = cp_view_texture_map_get_texture_index(map);
        const size_t slice = cp_view_texture_map_get_slice_index(map);
        const MTLViewport vp = cp_view_texture_map_get_viewport(map);

        MTLRenderPassDescriptor *pass = [MTLRenderPassDescriptor renderPassDescriptor];
        pass.colorAttachments[0].texture = cp_drawable_get_color_texture(drawable, texIdx);
        pass.colorAttachments[0].slice = slice;
        pass.colorAttachments[0].loadAction = MTLLoadActionClear;
        pass.colorAttachments[0].storeAction = MTLStoreActionStore;
        pass.colorAttachments[0].clearColor = MTLClearColorMake(0.0, 0.0, 0.0, 1.0);
        id<MTLTexture> depth = cp_drawable_get_depth_texture(drawable, texIdx);
        if (depth) {
            // reverse Z: 0 is infinitely far, so the compositor turns the
            // picture with the head and does not move it with the head's position
            pass.depthAttachment.texture = depth;
            pass.depthAttachment.slice = slice;
            pass.depthAttachment.loadAction = MTLLoadActionClear;
            pass.depthAttachment.storeAction = MTLStoreActionStore;
            pass.depthAttachment.clearDepth = 0.0;
        }
        id<MTLRenderCommandEncoder> enc = [cmd renderCommandEncoderWithDescriptor:pass];
        [enc setViewport:vp];
        [enc setDepthStencilState:s_noDepth];
        [enc setFragmentSamplerState:s_sampler atIndex:0];

        const VosViewInfo &vi = s_views[v < 2 ? v : 1];
        const int eye = (views == 1) ? 0 : (int)(v < 2 ? v : 1);
        const simd_float4x4 proj = cp_drawable_compute_projection(drawable, cp_axis_direction_convention_right_up_back, v);
        const simd_float4x4 viewFromWorld = simd_inverse(simd_mul(s_frameHead, vi.deviceFromView));
        for (const VosLayerDraw &d : draws) {
            if (d.projection) {
                struct { simd_float4 dst, src, rect; float opaque; } p;
                p.dst = simd_make_float4(vi.tanL, vi.tanR, vi.tanU, vi.tanD);
                p.src = simd_make_float4(d.eyeTan[eye][0], d.eyeTan[eye][1], d.eyeTan[eye][2], d.eyeTan[eye][3]);
                p.rect = simd_make_float4(d.eyeRect[eye][0], d.eyeRect[eye][1], d.eyeRect[eye][2], d.eyeRect[eye][3]);
                p.opaque = d.blend == 0 ? 1.0f : 0.0f;
                [enc setRenderPipelineState:s_projPipe[d.blend]];
                [enc setFragmentBytes:&p length:sizeof(p) atIndex:0];
                [enc setFragmentTexture:d.eyeTex[eye] atIndex:0];
                [enc drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
            } else {
                if (d.eyes == XR_EYE_VISIBILITY_LEFT && eye != 0) continue;
                if (d.eyes == XR_EYE_VISIBILITY_RIGHT && eye != 1) continue;
                struct { simd_float4x4 mvp; simd_float4 rect; float opaque; } q;
                q.mvp = simd_mul(proj, simd_mul(viewFromWorld, d.worldFromQuad));
                q.rect = simd_make_float4(d.rect[0], d.rect[1], d.rect[2], d.rect[3]);
                q.opaque = d.blend == 0 ? 1.0f : 0.0f;
                [enc setRenderPipelineState:s_quadPipe[d.blend]];
                [enc setVertexBytes:&q length:sizeof(q) atIndex:0];
                [enc setFragmentBytes:&q length:sizeof(q) atIndex:0];
                [enc setFragmentTexture:d.tex atIndex:0];
                [enc drawPrimitives:MTLPrimitiveTypeTriangleStrip vertexStart:0 vertexCount:4];
            }
        }
        [enc endEncoding];
    }
    [cmd encodeSignalEvent:s_doneEvent value:s_frameNo];
    if (s_frameAnchor) cp_drawable_set_device_anchor(drawable, s_frameAnchor);
    cp_drawable_encode_present(drawable, cmd);
    [cmd commit];
}

/* A frame with no drawable work of the game's: black, presented (the
 * compositor expects every drawable it hands out back). */
static void vosPresentEmpty(void)
{
    XrFrameEndInfo none{ XR_TYPE_FRAME_END_INFO };
    none.displayTime = vosTime(s_frameDisplay);
    vosComposite(&none);
}

extern "C" void vosKeepAlive(void)
{
    if (s_frame) {
        // mid-frame (the picker was asked for from the launcher's frame): end
        // it black; the game's own xrEndFrame then has nothing to present
        vosPresentEmpty();
        cp_frame_end_submission(s_frame);
        s_frame = nil;
        s_drawable = nil;
        s_frameAnchor = nil;
    }
    s_frameNo++;
    if (vosNextFrame()) {
        vosPresentEmpty();
        cp_frame_end_submission(s_frame);
        s_frame = nil;
        s_drawable = nil;
        s_frameAnchor = nil;
    } else {
        usleep(10000);
    }
}

// ============================================================================
// The API
// ============================================================================

#define VOS_EXT(name, version) { XR_TYPE_EXTENSION_PROPERTIES, nullptr, name, version }
static const XrExtensionProperties kVosExtensions[] = {
    VOS_EXT("XR_KHR_opengl_es_enable", 10),
    VOS_EXT("XR_MNDX_egl_enable", 2),
    VOS_EXT("XR_EXT_local_floor", 1),
};

static void vosProbeViews(void);

static XrResult vosCopyOut(uint32_t cap, uint32_t *count, uint32_t n)
{
    *count = n;
    if (cap == 0) return XR_SUCCESS;
    return cap < n ? XR_ERROR_SIZE_INSUFFICIENT : XR_SUCCESS;
}

extern "C" {

XRAPI_ATTR XrResult XRAPI_CALL xrEnumerateInstanceExtensionProperties(const char *layerName, uint32_t cap,
                                                                       uint32_t *count, XrExtensionProperties *props)
{
    if (layerName) return XR_ERROR_API_LAYER_NOT_PRESENT;
    const uint32_t n = sizeof(kVosExtensions) / sizeof(kVosExtensions[0]);
    XrResult r = vosCopyOut(cap, count, n);
    if (cap && r == XR_SUCCESS) {
        for (uint32_t i = 0; i < n; i++) {
            props[i].extensionVersion = kVosExtensions[i].extensionVersion;
            strncpy(props[i].extensionName, kVosExtensions[i].extensionName, XR_MAX_EXTENSION_NAME_SIZE - 1);
        }
    }
    return r;
}

XRAPI_ATTR XrResult XRAPI_CALL xrCreateInstance(const XrInstanceCreateInfo *ci, XrInstance *instance)
{
    if (s_instance) return XR_ERROR_LIMIT_REACHED;
    for (uint32_t i = 0; i < ci->enabledExtensionCount; i++) {
        bool known = false;
        for (const auto &e : kVosExtensions) known |= strcmp(e.extensionName, ci->enabledExtensionNames[i]) == 0;
        if (!known) {
            vosLog("visionos: xrCreateInstance: extension %s is not offered", ci->enabledExtensionNames[i]);
            return XR_ERROR_EXTENSION_NOT_PRESENT;
        }
    }
    s_instance = new VosInstanceObj();
    s_touchProfile = vosPath("/interaction_profiles/oculus/touch_controller");
    s_handPath[0] = vosPath("/user/hand/left");
    s_handPath[1] = vosPath("/user/hand/right");
    *instance = (XrInstance)s_instance;
    vosLog("visionos: xr: instance for %s", ci->applicationInfo.applicationName);
    return XR_SUCCESS;
}

XRAPI_ATTR XrResult XRAPI_CALL xrDestroyInstance(XrInstance instance)
{
    if ((VosInstanceObj *)instance != s_instance || !s_instance) return XR_ERROR_HANDLE_INVALID;
    delete s_instance;
    s_instance = nullptr;
    return XR_SUCCESS;
}

XRAPI_ATTR XrResult XRAPI_CALL xrGetInstanceProperties(XrInstance, XrInstanceProperties *p)
{
    p->runtimeVersion = XR_MAKE_VERSION(0, 1, 0);
    snprintf(p->runtimeName, XR_MAX_RUNTIME_NAME_SIZE, "GoldenEye VR visionOS");
    return XR_SUCCESS;
}

XRAPI_ATTR XrResult XRAPI_CALL xrPollEvent(XrInstance, XrEventDataBuffer *out)
{
    if (s_events.empty()) return XR_EVENT_UNAVAILABLE;
    *out = s_events.front();
    s_events.pop_front();
    return XR_SUCCESS;
}

XRAPI_ATTR XrResult XRAPI_CALL xrGetSystem(XrInstance, const XrSystemGetInfo *info, XrSystemId *system)
{
    if (info->formFactor != XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY) return XR_ERROR_FORM_FACTOR_UNSUPPORTED;
    if (!vosLayer()) return XR_ERROR_FORM_FACTOR_UNAVAILABLE;
    *system = 1;
    return XR_SUCCESS;
}

XRAPI_ATTR XrResult XRAPI_CALL xrGetSystemProperties(XrInstance, XrSystemId, XrSystemProperties *p)
{
    p->systemId = 1;
    p->vendorId = 0x05ac;   // Apple's USB vendor id
    snprintf(p->systemName, XR_MAX_SYSTEM_NAME_SIZE, "Apple Vision Pro");
    p->graphicsProperties.maxSwapchainImageWidth = 8192;
    p->graphicsProperties.maxSwapchainImageHeight = 8192;
    p->graphicsProperties.maxLayerCount = 16;
    p->trackingProperties.orientationTracking = XR_TRUE;
    p->trackingProperties.positionTracking = XR_TRUE;
    return XR_SUCCESS;
}

XRAPI_ATTR XrResult XRAPI_CALL xrEnumerateViewConfigurationViews(XrInstance, XrSystemId,
                                                                 XrViewConfigurationType type, uint32_t cap,
                                                                 uint32_t *count, XrViewConfigurationView *views)
{
    if (type != XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO) return XR_ERROR_VIEW_CONFIGURATION_TYPE_UNSUPPORTED;
    vosProbeViews();   // the drawable's size is the eyes'
    XrResult r = vosCopyOut(cap, count, 2);
    if (cap && r == XR_SUCCESS) {
        for (int i = 0; i < 2; i++) {
            views[i].recommendedImageRectWidth = s_eyeW;
            views[i].recommendedImageRectHeight = s_eyeH;
            views[i].maxImageRectWidth = 8192;
            views[i].maxImageRectHeight = 8192;
            views[i].recommendedSwapchainSampleCount = 1;
            views[i].maxSwapchainSampleCount = 1;
        }
    }
    return r;
}

static XrResult XRAPI_CALL vosGetGLESRequirements(XrInstance, XrSystemId, XrGraphicsRequirementsOpenGLESKHR *r)
{
    r->minApiVersionSupported = XR_MAKE_VERSION(3, 0, 0);
    r->maxApiVersionSupported = XR_MAKE_VERSION(3, 2, 0);
    return XR_SUCCESS;
}

/* The compositor's first drawable, presented black: its size and views are
 * the eyes' (the session asks before any frame). */
static void vosProbeViews(void)
{
    if (s_viewsKnown) return;
    vosStartTracking();
    vosDefaultViews();
    for (int tries = 0; tries < 200 && !s_viewsKnown; tries++) {
        s_frameNo++;
        if (vosNextFrame()) {
            vosPresentEmpty();
            cp_frame_end_submission(s_frame);
            s_frame = nil;
            s_drawable = nil;
            s_frameAnchor = nil;
        } else {
            usleep(5000);
        }
    }
}

XRAPI_ATTR XrResult XRAPI_CALL xrCreateSession(XrInstance, const XrSessionCreateInfo *ci, XrSession *session)
{
    if (s_session) return XR_ERROR_LIMIT_REACHED;
    if (!eglGetCurrentContext()) {
        vosLog("visionos: xrCreateSession: no current EGL context");
        return XR_ERROR_GRAPHICS_DEVICE_INVALID;
    }
    s_session = new VosSessionObj();
    *session = (XrSession)s_session;
    vosStartTracking();
    vosProbeViews();
    vosPostState(XR_SESSION_STATE_IDLE);
    vosPostState(XR_SESSION_STATE_READY);
    return XR_SUCCESS;
}

XRAPI_ATTR XrResult XRAPI_CALL xrDestroySession(XrSession session)
{
    if ((VosSessionObj *)session != s_session || !s_session) return XR_ERROR_HANDLE_INVALID;
    delete s_session;
    s_session = nullptr;
    return XR_SUCCESS;
}

XRAPI_ATTR XrResult XRAPI_CALL xrBeginSession(XrSession, const XrSessionBeginInfo *info)
{
    if (info->primaryViewConfigurationType != XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO)
        return XR_ERROR_VIEW_CONFIGURATION_TYPE_UNSUPPORTED;
    if (s_sessionRunning) return XR_ERROR_SESSION_RUNNING;
    s_sessionRunning = true;
    vosPostState(XR_SESSION_STATE_SYNCHRONIZED);
    vosPostState(XR_SESSION_STATE_VISIBLE);
    vosPostState(XR_SESSION_STATE_FOCUSED);
    s_haveFocus = true;
    return XR_SUCCESS;
}

XRAPI_ATTR XrResult XRAPI_CALL xrEndSession(XrSession)
{
    if (!s_sessionRunning) return XR_ERROR_SESSION_NOT_RUNNING;
    s_sessionRunning = false;
    vosPostState(XR_SESSION_STATE_IDLE);
    vosPostState(XR_SESSION_STATE_EXITING);
    return XR_SUCCESS;
}

XRAPI_ATTR XrResult XRAPI_CALL xrEnumerateReferenceSpaces(XrSession, uint32_t cap, uint32_t *count,
                                                          XrReferenceSpaceType *spaces)
{
    static const XrReferenceSpaceType kSpaces[] = { XR_REFERENCE_SPACE_TYPE_VIEW, XR_REFERENCE_SPACE_TYPE_LOCAL,
                                                    XR_REFERENCE_SPACE_TYPE_STAGE,
                                                    XR_REFERENCE_SPACE_TYPE_LOCAL_FLOOR_EXT };
    XrResult r = vosCopyOut(cap, count, 4);
    if (cap && r == XR_SUCCESS) memcpy(spaces, kSpaces, sizeof(kSpaces));
    return r;
}

XRAPI_ATTR XrResult XRAPI_CALL xrCreateReferenceSpace(XrSession, const XrReferenceSpaceCreateInfo *ci, XrSpace *space)
{
    auto *s = new VosSpaceObj{ VOS_SPACE_REFERENCE, ci->referenceSpaceType, XR_NULL_HANDLE, XR_NULL_PATH,
                               vosMatrixFromPose(ci->poseInReferenceSpace) };
    *space = (XrSpace)s;
    return XR_SUCCESS;
}

XRAPI_ATTR XrResult XRAPI_CALL xrCreateActionSpace(XrSession, const XrActionSpaceCreateInfo *ci, XrSpace *space)
{
    auto *s = new VosSpaceObj{ VOS_SPACE_ACTION, XR_REFERENCE_SPACE_TYPE_VIEW, ci->action, ci->subactionPath,
                               vosMatrixFromPose(ci->poseInActionSpace) };
    *space = (XrSpace)s;
    return XR_SUCCESS;
}

XRAPI_ATTR XrResult XRAPI_CALL xrDestroySpace(XrSpace space)
{
    delete (VosSpaceObj *)space;
    return XR_SUCCESS;
}

XRAPI_ATTR XrResult XRAPI_CALL xrLocateSpace(XrSpace space, XrSpace baseSpace, XrTime time, XrSpaceLocation *loc)
{
    const double t = vosSeconds(time);
    simd_float4x4 a, b;
    loc->locationFlags = 0;
    if (!space || !baseSpace || !vosWorldFromSpace((VosSpaceObj *)space, t, &a) ||
        !vosWorldFromSpace((VosSpaceObj *)baseSpace, t, &b)) {
        return XR_SUCCESS;
    }
    loc->pose = vosPoseFromMatrix(simd_mul(simd_inverse(b), a));
    loc->locationFlags = XR_SPACE_LOCATION_ORIENTATION_VALID_BIT | XR_SPACE_LOCATION_POSITION_VALID_BIT |
                         XR_SPACE_LOCATION_ORIENTATION_TRACKED_BIT | XR_SPACE_LOCATION_POSITION_TRACKED_BIT;
    for (auto *n = (XrBaseOutStructure *)loc->next; n; n = n->next) {
        if (n->type == XR_TYPE_SPACE_VELOCITY) {
            auto *v = (XrSpaceVelocity *)n;
            v->velocityFlags = 0;
        }
    }
    return XR_SUCCESS;
}

XRAPI_ATTR XrResult XRAPI_CALL xrLocateViews(XrSession, const XrViewLocateInfo *info, XrViewState *state,
                                             uint32_t cap, uint32_t *count, XrView *views)
{
    XrResult r = vosCopyOut(cap, count, 2);
    if (!cap || r != XR_SUCCESS) return r;
    const double t = vosSeconds(info->displayTime);
    simd_float4x4 base;
    if (!vosWorldFromSpace((const VosSpaceObj *)info->space, t, &base)) {
        state->viewStateFlags = 0;
        return XR_SUCCESS;
    }
    const simd_float4x4 head = vosHeadFor(t);
    const simd_float4x4 baseInv = simd_inverse(base);
    for (int i = 0; i < 2; i++) {
        const VosViewInfo &vi = s_views[i];
        views[i].pose = vosPoseFromMatrix(simd_mul(baseInv, simd_mul(head, vi.deviceFromView)));
        views[i].fov.angleLeft = atanf(vi.tanL);
        views[i].fov.angleRight = atanf(vi.tanR);
        views[i].fov.angleUp = atanf(vi.tanU);
        views[i].fov.angleDown = atanf(vi.tanD);
    }
    state->viewStateFlags = XR_VIEW_STATE_ORIENTATION_VALID_BIT | XR_VIEW_STATE_POSITION_VALID_BIT |
                            XR_VIEW_STATE_ORIENTATION_TRACKED_BIT | XR_VIEW_STATE_POSITION_TRACKED_BIT;
    return XR_SUCCESS;
}

XRAPI_ATTR XrResult XRAPI_CALL xrEnumerateSwapchainFormats(XrSession, uint32_t cap, uint32_t *count, int64_t *formats)
{
    XrResult r = vosCopyOut(cap, count, 1);
    if (cap && r == XR_SUCCESS) formats[0] = GL_RGBA8;
    return r;
}

XRAPI_ATTR XrResult XRAPI_CALL xrCreateSwapchain(XrSession, const XrSwapchainCreateInfo *ci, XrSwapchain *out)
{
    if (ci->format != GL_RGBA8) return XR_ERROR_SWAPCHAIN_FORMAT_UNSUPPORTED;
    if (ci->arraySize < 1 || ci->arraySize > 2 || ci->faceCount != 1 || ci->mipCount > 1 || ci->sampleCount > 1 ||
        ci->width < 1 || ci->height < 1) {
        vosLog("visionos: xrCreateSwapchain %ux%u x%u faces %u mips %u samples %u: not supported", ci->width,
               ci->height, ci->arraySize, ci->faceCount, ci->mipCount, ci->sampleCount);
        return XR_ERROR_FEATURE_UNSUPPORTED;
    }
    auto *sc = new VosSwapchainObj();
    sc->w = ci->width;
    sc->h = ci->height;
    sc->arraySize = ci->arraySize;
    sc->format = ci->format;
    sc->images.resize(3);
    while (glGetError() != GL_NO_ERROR) {}   // the game's earlier errors are not ours
    GLint prev2d = 0, prevArr = 0;
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &prev2d);
    glGetIntegerv(GL_TEXTURE_BINDING_2D_ARRAY, &prevArr);
    bool ok = true;
    for (VosImage &img : sc->images) {
        if (sc->arraySize == 1) {
            img.mtl = vosNewTexture(sc->w, sc->h);
            img.tex = img.mtl ? vosGLTextureFromMetal((__bridge void *)img.mtl) : 0;
            img.mtlSrgb = img.mtl ? vosSrgbView(img.mtl) : nil;
            ok &= img.tex != 0 && img.mtlSrgb != nil;
        } else {
            glGenTextures(1, &img.tex);
            glBindTexture(GL_TEXTURE_2D_ARRAY, img.tex);
            glTexStorage3D(GL_TEXTURE_2D_ARRAY, 1, GL_RGBA8, (GLsizei)sc->w, (GLsizei)sc->h, (GLsizei)sc->arraySize);
            glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            for (uint32_t l = 0; l < sc->arraySize; l++) {
                img.copyMtl[l] = vosNewTexture(sc->w, sc->h);
                img.copyTex[l] = img.copyMtl[l] ? vosGLTextureFromMetal((__bridge void *)img.copyMtl[l]) : 0;
                img.copySrgb[l] = img.copyMtl[l] ? vosSrgbView(img.copyMtl[l]) : nil;
                ok &= img.copyTex[l] != 0 && img.copySrgb[l] != nil;
            }
        }
    }
    glBindTexture(GL_TEXTURE_2D, (GLuint)prev2d);
    glBindTexture(GL_TEXTURE_2D_ARRAY, (GLuint)prevArr);
    const GLenum err = glGetError();
    if (!ok || err != GL_NO_ERROR) {
        vosLog("visionos: xrCreateSwapchain %ux%u x%u failed (GL 0x%x)", sc->w, sc->h, sc->arraySize, err);
        delete sc;
        return XR_ERROR_RUNTIME_FAILURE;
    }
    vosLog("visionos: xr: swapchain %ux%u x%u", sc->w, sc->h, sc->arraySize);
    *out = (XrSwapchain)sc;
    return XR_SUCCESS;
}

XRAPI_ATTR XrResult XRAPI_CALL xrDestroySwapchain(XrSwapchain h)
{
    auto *sc = (VosSwapchainObj *)h;
    if (!sc) return XR_ERROR_HANDLE_INVALID;
    for (VosImage &img : sc->images) {
        vosWaitImage(img);
        if (img.mtl) {
            vosGLTextureRelease(img.tex);
        } else if (img.tex) {
            glDeleteTextures(1, &img.tex);
        }
        for (int l = 0; l < 2; l++) {
            if (img.copyTex[l]) vosGLTextureRelease(img.copyTex[l]);
        }
    }
    delete sc;
    return XR_SUCCESS;
}

XRAPI_ATTR XrResult XRAPI_CALL xrEnumerateSwapchainImages(XrSwapchain h, uint32_t cap, uint32_t *count,
                                                          XrSwapchainImageBaseHeader *images)
{
    auto *sc = (VosSwapchainObj *)h;
    if (!sc) return XR_ERROR_HANDLE_INVALID;
    XrResult r = vosCopyOut(cap, count, (uint32_t)sc->images.size());
    if (cap && r == XR_SUCCESS) {
        auto *out = reinterpret_cast<XrSwapchainImageOpenGLESKHR *>(images);
        for (size_t i = 0; i < sc->images.size(); i++) {
            out[i].type = XR_TYPE_SWAPCHAIN_IMAGE_OPENGL_ES_KHR;
            out[i].image = sc->images[i].tex;
        }
    }
    return r;
}

XRAPI_ATTR XrResult XRAPI_CALL xrAcquireSwapchainImage(XrSwapchain h, const XrSwapchainImageAcquireInfo *,
                                                       uint32_t *index)
{
    auto *sc = (VosSwapchainObj *)h;
    if (!sc) return XR_ERROR_HANDLE_INVALID;
    if (sc->acquired.size() >= sc->images.size()) return XR_ERROR_CALL_ORDER_INVALID;
    *index = sc->next;
    sc->acquired.push_back(sc->next);
    sc->next = (sc->next + 1) % (uint32_t)sc->images.size();
    return XR_SUCCESS;
}

XRAPI_ATTR XrResult XRAPI_CALL xrWaitSwapchainImage(XrSwapchain h, const XrSwapchainImageWaitInfo *)
{
    auto *sc = (VosSwapchainObj *)h;
    if (!sc || sc->acquired.empty()) return XR_ERROR_CALL_ORDER_INVALID;
    vosWaitImage(sc->images[sc->acquired.front()]);
    return XR_SUCCESS;
}

XRAPI_ATTR XrResult XRAPI_CALL xrReleaseSwapchainImage(XrSwapchain h, const XrSwapchainImageReleaseInfo *)
{
    auto *sc = (VosSwapchainObj *)h;
    if (!sc || sc->acquired.empty()) return XR_ERROR_CALL_ORDER_INVALID;
    sc->lastReleased = (int32_t)sc->acquired.front();
    sc->acquired.pop_front();
    return XR_SUCCESS;
}

XRAPI_ATTR XrResult XRAPI_CALL xrWaitFrame(XrSession, const XrFrameWaitInfo *, XrFrameState *state)
{
    if (!s_sessionRunning) return XR_ERROR_SESSION_NOT_RUNNING;
    if (s_frame) {
        // a frame waited for and never ended: present it black
        vosPresentEmpty();
        cp_frame_end_submission(s_frame);
        s_frame = nil;
        s_drawable = nil;
        s_frameAnchor = nil;
    }
    s_frameNo++;
    const bool have = vosNextFrame();
    if (!have) {
        // no drawable this time (paused, closing, or a skipped frame): the
        // game ends an empty frame and asks again
        usleep(2000);
        s_frameDisplay = vosNow() + s_period;
    }
    state->predictedDisplayTime = vosTime(s_frameDisplay);
    state->predictedDisplayPeriod = vosTime(s_period);
    state->shouldRender = have ? XR_TRUE : XR_FALSE;
    s_frameBegun = false;
    return XR_SUCCESS;
}

XRAPI_ATTR XrResult XRAPI_CALL xrBeginFrame(XrSession, const XrFrameBeginInfo *)
{
    if (!s_sessionRunning) return XR_ERROR_SESSION_NOT_RUNNING;
    const bool discarded = s_frameBegun;
    s_frameBegun = true;
    return discarded ? XR_FRAME_DISCARDED : XR_SUCCESS;
}

XRAPI_ATTR XrResult XRAPI_CALL xrEndFrame(XrSession, const XrFrameEndInfo *info)
{
    if (!s_frameBegun) return XR_ERROR_CALL_ORDER_INVALID;
    s_frameBegun = false;
    if (!s_frame || !s_drawable) return XR_SUCCESS;
    vosComposite(info);
    cp_frame_end_submission(s_frame);
    s_frame = nil;
    s_drawable = nil;
    s_frameAnchor = nil;
    static unsigned frames;
    if (++frames == 3 || frames % 900 == 0) {
        vosLog("visionos: xr: frame %u, %u layers, %.1f Hz, head (%.2f %.2f %.2f)", frames, info->layerCount,
               1.0 / s_period, s_frameHead.columns[3].x, s_frameHead.columns[3].y, s_frameHead.columns[3].z);
    }
    return XR_SUCCESS;
}

// ---- actions ----

XRAPI_ATTR XrResult XRAPI_CALL xrStringToPath(XrInstance, const char *s, XrPath *path)
{
    if (!s || s[0] != '/') return XR_ERROR_PATH_FORMAT_INVALID;
    *path = vosPath(s);
    return XR_SUCCESS;
}

XRAPI_ATTR XrResult XRAPI_CALL xrPathToString(XrInstance, XrPath path, uint32_t cap, uint32_t *count, char *buffer)
{
    const char *s = vosPathString(path);
    if (!s[0]) return XR_ERROR_PATH_INVALID;
    const uint32_t n = (uint32_t)strlen(s) + 1;
    XrResult r = vosCopyOut(cap, count, n);
    if (cap && r == XR_SUCCESS) memcpy(buffer, s, n);
    return r;
}

XRAPI_ATTR XrResult XRAPI_CALL xrCreateActionSet(XrInstance, const XrActionSetCreateInfo *ci, XrActionSet *set)
{
    *set = (XrActionSet) new VosActionSetObj{ ci->actionSetName };
    return XR_SUCCESS;
}

XRAPI_ATTR XrResult XRAPI_CALL xrCreateAction(XrActionSet, const XrActionCreateInfo *ci, XrAction *action)
{
    auto *a = new VosActionObj();
    a->name = ci->actionName;
    a->type = ci->actionType;
    for (uint32_t i = 0; i < ci->countSubactionPaths; i++) a->subactions.push_back(ci->subactionPaths[i]);
    s_actions.push_back(a);
    *action = (XrAction)a;
    return XR_SUCCESS;
}

XRAPI_ATTR XrResult XRAPI_CALL xrSuggestInteractionProfileBindings(XrInstance,
                                                                   const XrInteractionProfileSuggestedBinding *s)
{
    // Touch's (vos_input.mm answers a Touch pair); the other profiles' are
    // accepted and not used
    if (s->interactionProfile != s_touchProfile) return XR_SUCCESS;
    for (VosActionObj *a : s_actions) a->bindings.clear();
    for (uint32_t i = 0; i < s->countSuggestedBindings; i++) {
        auto *a = (VosActionObj *)s->suggestedBindings[i].action;
        if (a) a->bindings.push_back(s->suggestedBindings[i].binding);
    }
    return XR_SUCCESS;
}

XRAPI_ATTR XrResult XRAPI_CALL xrAttachSessionActionSets(XrSession, const XrSessionActionSetsAttachInfo *)
{
    return XR_SUCCESS;
}

/* one binding's value: [0] the value (or x), [1] y; false if no such input */
static bool vosBindingValue(XrPath binding, int *handOut, float out[2])
{
    const char *p = vosPathString(binding);
    int hand;
    if (!strncmp(p, "/user/hand/left/", 16)) hand = 0;
    else if (!strncmp(p, "/user/hand/right/", 17)) hand = 1;
    else return false;
    *handOut = hand;
    const char *c = p + (hand == 0 ? 16 : 17);
    const VosHand *h = vosInputHand(hand);
    out[0] = out[1] = 0.0f;
    if (!h) return false;
    auto is = [&](const char *s) { return strcmp(c, s) == 0; };
    if (is("input/trigger/value")) out[0] = h->trigger;
    else if (is("input/trigger/click")) out[0] = h->trigger > 0.5f;
    else if (is("input/trigger/touch")) out[0] = h->triggerTouch || h->trigger > 0.05f;
    else if (is("input/squeeze/value") || is("input/squeeze/force")) out[0] = h->squeeze;
    else if (is("input/squeeze/click")) out[0] = h->squeeze > 0.5f;
    else if (is("input/thumbstick")) { out[0] = h->stickX; out[1] = h->stickY; }
    else if (is("input/thumbstick/x")) out[0] = h->stickX;
    else if (is("input/thumbstick/y")) out[0] = h->stickY;
    else if (is("input/thumbstick/click")) out[0] = h->stickClick;
    else if (is("input/a/click") || is("input/x/click")) out[0] = h->a;
    else if (is("input/b/click") || is("input/y/click")) out[0] = h->b;
    else if (is("input/menu/click") || is("input/system/click")) out[0] = h->menu;
    else if (is("input/grip/pose") || is("input/aim/pose")) out[0] = h->poseValid;
    else return false;
    return true;
}

static void vosEvaluate(VosActionObj *a, int which, float out[2], bool *active)
{
    out[0] = out[1] = 0.0f;
    *active = false;
    for (XrPath b : a->bindings) {
        int hand;
        float v[2];
        if (!vosBindingValue(b, &hand, v)) continue;
        if (which < 2 && hand != which) continue;
        const VosHand *h = vosInputHand(hand);
        if (!h || !h->active) continue;
        *active = true;
        if (a->type == XR_ACTION_TYPE_VECTOR2F_INPUT) {
            if (v[0] * v[0] + v[1] * v[1] > out[0] * out[0] + out[1] * out[1]) { out[0] = v[0]; out[1] = v[1]; }
        } else if (fabsf(v[0]) > fabsf(out[0])) {
            out[0] = v[0];
        }
    }
}

XRAPI_ATTR XrResult XRAPI_CALL xrSyncActions(XrSession, const XrActionsSyncInfo *)
{
    const double t = s_frameDisplay > 0.0 ? s_frameDisplay : vosNow();
    simd_float4x4 head = vosHeadFor(t);
    vosInputSync(t, &head);
    const XrTime now = vosTime(vosNow());
    for (VosActionObj *a : s_actions) {
        for (int w = 0; w < 3; w++) {
            a->prev[w][0] = a->cur[w][0];
            a->prev[w][1] = a->cur[w][1];
            bool active;
            vosEvaluate(a, w, a->cur[w], &active);
            if (a->cur[w][0] != a->prev[w][0] || a->cur[w][1] != a->prev[w][1]) a->changed[w] = now;
        }
    }
    return XR_SUCCESS;
}

static int vosWhich(XrPath subaction)
{
    if (subaction == s_handPath[0]) return 0;
    if (subaction == s_handPath[1]) return 1;
    return 2;
}

XRAPI_ATTR XrResult XRAPI_CALL xrGetActionStateBoolean(XrSession, const XrActionStateGetInfo *info,
                                                       XrActionStateBoolean *s)
{
    auto *a = (VosActionObj *)info->action;
    if (!a) return XR_ERROR_HANDLE_INVALID;
    const int w = vosWhich(info->subactionPath);
    float v[2];
    bool active;
    vosEvaluate(a, w, v, &active);
    s->isActive = active;
    s->currentState = a->cur[w][0] > 0.5f;
    s->changedSinceLastSync = (a->cur[w][0] > 0.5f) != (a->prev[w][0] > 0.5f);
    s->lastChangeTime = a->changed[w];
    return XR_SUCCESS;
}

XRAPI_ATTR XrResult XRAPI_CALL xrGetActionStateFloat(XrSession, const XrActionStateGetInfo *info,
                                                     XrActionStateFloat *s)
{
    auto *a = (VosActionObj *)info->action;
    if (!a) return XR_ERROR_HANDLE_INVALID;
    const int w = vosWhich(info->subactionPath);
    float v[2];
    bool active;
    vosEvaluate(a, w, v, &active);
    s->isActive = active;
    s->currentState = a->cur[w][0];
    s->changedSinceLastSync = a->cur[w][0] != a->prev[w][0];
    s->lastChangeTime = a->changed[w];
    return XR_SUCCESS;
}

XRAPI_ATTR XrResult XRAPI_CALL xrGetActionStateVector2f(XrSession, const XrActionStateGetInfo *info,
                                                        XrActionStateVector2f *s)
{
    auto *a = (VosActionObj *)info->action;
    if (!a) return XR_ERROR_HANDLE_INVALID;
    const int w = vosWhich(info->subactionPath);
    float v[2];
    bool active;
    vosEvaluate(a, w, v, &active);
    s->isActive = active;
    s->currentState = { a->cur[w][0], a->cur[w][1] };
    s->changedSinceLastSync = a->cur[w][0] != a->prev[w][0] || a->cur[w][1] != a->prev[w][1];
    s->lastChangeTime = a->changed[w];
    return XR_SUCCESS;
}

XRAPI_ATTR XrResult XRAPI_CALL xrGetActionStatePose(XrSession, const XrActionStateGetInfo *info,
                                                    XrActionStatePose *s)
{
    auto *a = (VosActionObj *)info->action;
    if (!a) return XR_ERROR_HANDLE_INVALID;
    const int w = vosWhich(info->subactionPath);
    bool active = false;
    for (int hand = 0; hand < 2; hand++) {
        if (w < 2 && w != hand) continue;
        const VosHand *h = vosInputHand(hand);
        active |= h && h->active && h->poseValid;
    }
    s->isActive = active;
    return XR_SUCCESS;
}

XRAPI_ATTR XrResult XRAPI_CALL xrApplyHapticFeedback(XrSession, const XrHapticActionInfo *info,
                                                     const XrHapticBaseHeader *haptic)
{
    if (!haptic || haptic->type != XR_TYPE_HAPTIC_VIBRATION) return XR_SUCCESS;
    const auto *v = (const XrHapticVibration *)haptic;
    const int w = vosWhich(info->subactionPath);
    const double seconds = v->duration > 0 ? vosSeconds(v->duration) : 0.03;
    for (int hand = 0; hand < 2; hand++) {
        if (w < 2 && w != hand) continue;
        vosInputHaptic(hand, v->amplitude, seconds);
    }
    return XR_SUCCESS;
}

XRAPI_ATTR XrResult XRAPI_CALL xrStopHapticFeedback(XrSession, const XrHapticActionInfo *info)
{
    const int w = vosWhich(info->subactionPath);
    for (int hand = 0; hand < 2; hand++) {
        if (w < 2 && w != hand) continue;
        vosInputHaptic(hand, 0.0f, 0.0);
    }
    return XR_SUCCESS;
}

XRAPI_ATTR XrResult XRAPI_CALL xrGetCurrentInteractionProfile(XrSession, XrPath topLevel,
                                                              XrInteractionProfileState *state)
{
    const int w = vosWhich(topLevel);
    const VosHand *h = w < 2 ? vosInputHand(w) : nullptr;
    state->interactionProfile = (h && h->active) ? s_touchProfile : XR_NULL_PATH;
    return XR_SUCCESS;
}

// ---- the extension door ----

static XrResult XRAPI_CALL vosInitializeLoader(const XrLoaderInitInfoBaseHeaderKHR *) { return XR_SUCCESS; }

struct VosProc { const char *name; PFN_xrVoidFunction fn; };
#define VOS_PROC(n) { #n, (PFN_xrVoidFunction)n }
static const VosProc kVosProcs[] = {
    VOS_PROC(xrEnumerateInstanceExtensionProperties), VOS_PROC(xrCreateInstance), VOS_PROC(xrDestroyInstance),
    VOS_PROC(xrGetInstanceProperties), VOS_PROC(xrPollEvent), VOS_PROC(xrGetSystem), VOS_PROC(xrGetSystemProperties),
    VOS_PROC(xrEnumerateViewConfigurationViews), VOS_PROC(xrCreateSession), VOS_PROC(xrDestroySession),
    VOS_PROC(xrBeginSession), VOS_PROC(xrEndSession), VOS_PROC(xrEnumerateReferenceSpaces),
    VOS_PROC(xrCreateReferenceSpace), VOS_PROC(xrCreateActionSpace), VOS_PROC(xrDestroySpace),
    VOS_PROC(xrLocateSpace), VOS_PROC(xrLocateViews), VOS_PROC(xrEnumerateSwapchainFormats),
    VOS_PROC(xrCreateSwapchain), VOS_PROC(xrDestroySwapchain), VOS_PROC(xrEnumerateSwapchainImages),
    VOS_PROC(xrAcquireSwapchainImage), VOS_PROC(xrWaitSwapchainImage), VOS_PROC(xrReleaseSwapchainImage),
    VOS_PROC(xrWaitFrame), VOS_PROC(xrBeginFrame), VOS_PROC(xrEndFrame), VOS_PROC(xrStringToPath),
    VOS_PROC(xrPathToString), VOS_PROC(xrCreateActionSet), VOS_PROC(xrCreateAction),
    VOS_PROC(xrSuggestInteractionProfileBindings), VOS_PROC(xrAttachSessionActionSets), VOS_PROC(xrSyncActions),
    VOS_PROC(xrGetActionStateBoolean), VOS_PROC(xrGetActionStateFloat), VOS_PROC(xrGetActionStateVector2f),
    VOS_PROC(xrGetActionStatePose), VOS_PROC(xrApplyHapticFeedback), VOS_PROC(xrStopHapticFeedback),
    VOS_PROC(xrGetCurrentInteractionProfile),
    { "xrGetOpenGLESGraphicsRequirementsKHR", (PFN_xrVoidFunction)vosGetGLESRequirements },
    { "xrInitializeLoaderKHR", (PFN_xrVoidFunction)vosInitializeLoader },
};

XRAPI_ATTR XrResult XRAPI_CALL xrGetInstanceProcAddr(XrInstance, const char *name, PFN_xrVoidFunction *fn)
{
    for (const VosProc &p : kVosProcs) {
        if (!strcmp(p.name, name)) {
            *fn = p.fn;
            return XR_SUCCESS;
        }
    }
    *fn = nullptr;
    return XR_ERROR_FUNCTION_UNSUPPORTED;
}

}   // extern "C"
