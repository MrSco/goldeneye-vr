/* Included by gfx_opengl.cpp, after the scope and eye capture state. */
static GLuint s_lensProgram, s_lensVao, s_lensVbo;
static std::unordered_map<const GevrOpticMode *, GLuint> s_lensMarks;

static GLuint compileLensShader(GLenum type, const std::string& source)
{
    GLuint shader = glCreateShader(type);
    const char *text = source.c_str();
    glShaderSource(shader, 1, &text, nullptr);
    glCompileShader(shader);
    GLint ok = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char info[1024];
        glGetShaderInfoLog(shader, sizeof(info), nullptr, info);
        sysLogPrintf(LOG_WARNING, "mods: optic lens shader: %s", info);
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

static bool prepareLensProgram()
{
    if (s_lensProgram) return true;
    static bool attempted;
    if (attempted) return false;
    attempted = true;
    const std::string version = std::string("#version ") + gl_glsl_version_str + "\n";
    const std::string precision = gl_es ? "precision highp float;\n" : "";
    const std::string multiview = use_multiview
        ? "#extension GL_OVR_multiview2 : require\n#define LENS_MULTIVIEW 1\nlayout(num_views=2) in;\n" : "";
    const GLuint vs = compileLensShader(GL_VERTEX_SHADER, version + multiview + precision + R"(
in vec2 aWindow;
uniform mat4 uProjection;
uniform vec3 uCentre;
uniform vec3 uRight;
uniform vec3 uUp;
uniform vec2 uHalf;
uniform vec4 uEyeOffset[2];
uniform vec3 uEyeInLens[2];
out vec2 vWindow;
flat out vec3 vEye;
void main() {
    vec3 p = uCentre + aWindow.x*uHalf.x*uRight + aWindow.y*uHalf.y*uUp;
    vec4 clip = uProjection * vec4(p, 1.0);
    int eye = 0;
#ifdef LENS_MULTIVIEW
    eye = int(gl_ViewID_OVR);
    vec4 offset = uEyeOffset[eye];
    clip.x -= offset.x + offset.y*clip.w;
    clip.y -= uEyeOffset[0].w*clip.w;
#endif
    gl_Position = vec4(clip.xy, .3*clip.z, clip.w);
    vWindow = aWindow;
    vEye = uEyeInLens[eye];
}
)");
    const GLuint fs = compileLensShader(GL_FRAGMENT_SHADER, version + precision + R"(
in vec2 vWindow;
flat in vec3 vEye;
out vec4 outColor;
uniform sampler2D uMark;
uniform sampler2D uScene;
uniform int uKind;
uniform int uLive;
uniform int uPalette;
uniform vec2 uHalf;
uniform vec2 uMarkAxis;
uniform vec3 uZero;
uniform float uTangentScale;
uniform float uMarkScale;
uniform float uFlip;
void main() {
    if (vEye.z >= 0.0) discard;
    vec2 windowUv = .5 + .5*vec2(uFlip*vWindow.x, vWindow.y);
    if (uKind == 1) {
        // Project the shared shot zero for each eye through this glass.
        vec2 angle = (vWindow*uHalf - vEye.xy)/max(-vEye.z, .01);
        vec2 zeroAngle = (uZero.xy - vEye.xy)/max(uZero.z - vEye.z, .01);
        vec2 offset = angle - zeroAngle;
        vec2 uv = uMarkAxis + uTangentScale*vec2(uFlip*offset.x, offset.y);
        uv.y = 1.0-uv.y;
        vec4 mark = vec4(0.0);
        if (all(greaterThanEqual(uv, vec2(0.0))) && all(lessThanEqual(uv, vec2(1.0))))
            mark = texture(uMark, uv);
        float a = max(mark.a, .025);
        outColor = vec4(mix(vec3(.18,.25,.24), mark.rgb, mark.a/a), a);
        return;
    }
    vec3 scene = uLive == 1 ? texture(uScene, windowUv).rgb : vec3(.022,.025,.027);
    bool magnified = uKind == 2;
    if (magnified && uLive == 1)
        scene = min(scene * 1.10, vec3(1.0));
    if (uKind == 3) {
        float luma = dot(scene, vec3(.299,.587,.114));
        scene = vec3(.18,.85,.33)*sqrt(clamp(.035+1.6*luma,0.0,1.0));
    }
    if (uKind == 4 && uLive == 1) {
        float heat = dot(scene, vec3(.299,.587,.114));
        if (uPalette == 0) scene = clamp(vec3(1.70*heat, 1.85*heat-.50, 2.3*heat-1.20), 0.0, 1.0);
        if (uPalette == 1) scene = clamp(vec3(1.75*heat-.20, 1.95*heat-.57, 2.10*(1.0-heat)-.66), 0.0, 1.0);
        if (uPalette == 2) scene = vec3(.91,1.0,.95)*(1.0-heat);
        if (uPalette == 3) scene = vec3(.93,1.0,.96)*heat;
    }
    vec2 markUv = vec2(uMarkAxis.x, 1.0-uMarkAxis.y)
        + vec2(uFlip*vWindow.x, -vWindow.y)*.5/uMarkScale;
    vec4 mark = texture(uMark, markUv);
    float angle = length(vEye.xy)/max(-vEye.z, .01);
    float shade = magnified ? 1.0-smoothstep(.22,.62,angle)
                            : 1.0-smoothstep(.13,.46,angle);
    float rim = length(vWindow);
    shade *= magnified ? 1.0-.14*smoothstep(.76,1.0,rim)
                       : 1.0-.24*smoothstep(.60,1.0,rim);
    vec3 shown = mix(scene, mark.rgb, mark.a);
    outColor = vec4(mix(vec3(.012,.015,.017), shown, shade), 1.0);
}
)");
    if (!vs || !fs) {
        if (vs) glDeleteShader(vs);
        if (fs) glDeleteShader(fs);
        return false;
    }
    GLuint program = glCreateProgram();
    glAttachShader(program, vs);
    glAttachShader(program, fs);
    glBindAttribLocation(program, 0, "aWindow");
    glLinkProgram(program);
    glDeleteShader(vs);
    glDeleteShader(fs);
    GLint ok;
    glGetProgramiv(program, GL_LINK_STATUS, &ok);
    if (!ok) {
        char info[1024];
        glGetProgramInfoLog(program, sizeof(info), nullptr, info);
        sysLogPrintf(LOG_WARNING, "mods: optic lens program: %s", info);
        glDeleteProgram(program);
        return false;
    }
    s_lensProgram = program;
    glGenVertexArrays(1, &s_lensVao);
    glGenBuffers(1, &s_lensVbo);
    return true;
}

static float lensDot(const float a[3], const float b[3])
{
    return a[0]*b[0] + a[1]*b[1] + a[2]*b[2];
}

static void lensCross(const float a[3], const float b[3], float c[3])
{
    c[0] = a[1]*b[2] - a[2]*b[1];
    c[1] = a[2]*b[0] - a[0]*b[2];
    c[2] = a[0]*b[1] - a[1]*b[0];
}

/* Clip X, Y and W are zero at the eye; solve those three planes in
 * recorded camera space, including the engine's per-eye IPD offset. */
static void lensEye(const float vp[16], float eyeOffset, float eye[3])
{
    const float x[3] = {vp[0], vp[4], vp[8]}, y[3] = {vp[1], vp[5], vp[9]};
    const float w[3] = {vp[3], vp[7], vp[11]};
    float yw[3], wx[3], xy[3];
    lensCross(y, w, yw);
    lensCross(w, x, wx);
    lensCross(x, y, xy);
    const float determinant = lensDot(x, yw);
    for (int i = 0; i < 3; ++i) eye[i] = fabsf(determinant) > 1e-6f
        ? ((eyeOffset-vp[12])*yw[i] - vp[13]*wx[i] - vp[15]*xy[i]) / determinant : 0.f;
}

static GLuint modOpticScopeImage(const GevrOpticSurface& surface)
{
    const int h = s_opticScopeSlot;
    if (!surface.live || h < 0 || !s_scopeTaken[h]) return 0;
    const float *forward = surface.aim;
    float right[3], up[3];
    lensCross(forward, surface.up, right);
    const float length = sqrtf(lensDot(right, right));
    if (length <= 1e-6f) return 0;
    for (int i = 0; i < 3; ++i) right[i] /= length;
    lensCross(right, forward, up);
    const float depth = std::max(1.f, lensDot(surface.centre, forward));
    const float zoom = std::max(1.f, surface.zoom);
    const float sx = zoom * depth / std::max(.2f, surface.half[0]);
    const float sy = zoom * depth / std::max(.2f, surface.half[1]);
    const float near = 1.f, far = 40000.f;
    const float A = (far + near) / (far - near), B = -2.f*far*near/(far-near);
    float *vp = s_scopeVP[h];
    for (int i = 0; i < 3; ++i) {
        vp[i*4] = sx*right[i];
        vp[i*4+1] = sy*up[i];
        vp[i*4+2] = A*forward[i];
        vp[i*4+3] = forward[i];
    }
    vp[12] = -sx*lensDot(right, surface.centre);
    vp[13] = -sy*lensDot(up, surface.centre);
    vp[15] = -lensDot(forward, surface.centre);
    vp[14] = A*vp[15] + B;
    s_opticScopeReady = true;
    gfx_vr_scope_render();
    return s_scopeDrawn[h] ? s_scopeTex[h] : 0;
}

static void drawModLens(const GevrEyeOptic& item, const float projection[16], bool invertY)
{
    const GevrOpticSurface& s = item.surface;
    if (!s.mode || s.half[0] < .01f || s.half[1] < .01f || !prepareLensProgram()) return;
    float matrix[16];
    memcpy(matrix, projection, sizeof(matrix));
    if (invertY)
        for (int i = 0; i < 4; ++i) matrix[i*4+1] = -matrix[i*4+1];
    float eyeLocal[6];
    for (int view = 0; view < 2; ++view) {
        float eye[3];
        lensEye(matrix, use_multiview ? s_eye_offsets[view*4] : 0.f, eye);
        for (int i = 0; i < 3; ++i) eye[i] -= s.centre[i];
        eyeLocal[view*3] = lensDot(eye, s.right);
        eyeLocal[view*3+1] = lensDot(eye, s.up);
        eyeLocal[view*3+2] = lensDot(eye, s.forward);
    }
    float right[3];
    lensCross(s.forward, s.up, right);

    GLint oldProgram, oldVao, oldBuffer, oldDepth, oldSrc, oldDst, oldSrcA, oldDstA;
    glGetIntegerv(GL_CURRENT_PROGRAM, &oldProgram);
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &oldVao);
    glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &oldBuffer);
    glGetIntegerv(GL_DEPTH_FUNC, &oldDepth);
    glGetIntegerv(GL_BLEND_SRC_RGB, &oldSrc);
    glGetIntegerv(GL_BLEND_DST_RGB, &oldDst);
    glGetIntegerv(GL_BLEND_SRC_ALPHA, &oldSrcA);
    glGetIntegerv(GL_BLEND_DST_ALPHA, &oldDstA);
    const GLboolean depth = glIsEnabled(GL_DEPTH_TEST), blend = glIsEnabled(GL_BLEND);
    const GLboolean cull = glIsEnabled(GL_CULL_FACE), stencil = glIsEnabled(GL_STENCIL_TEST);
    const GLboolean offset = glIsEnabled(GL_POLYGON_OFFSET_FILL);
    GLboolean depthMask;
    glGetBooleanv(GL_DEPTH_WRITEMASK, &depthMask);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glDepthMask(GL_FALSE);
    glDisable(GL_CULL_FACE);
    glDisable(GL_STENCIL_TEST);
    glDisable(GL_POLYGON_OFFSET_FILL);
    glEnable(GL_BLEND);
    glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    glUseProgram(s_lensProgram);
    const auto uniform = [](const char *name) { return glGetUniformLocation(s_lensProgram, name); };
    glUniformMatrix4fv(uniform("uProjection"), 1, GL_FALSE, matrix);
    glUniform3fv(uniform("uCentre"), 1, s.centre);
    glUniform3fv(uniform("uRight"), 1, s.right);
    glUniform3fv(uniform("uUp"), 1, s.up);
    glUniform2fv(uniform("uHalf"), 1, s.half);
    glUniform4fv(uniform("uEyeOffset"), 2, s_eye_offsets);
    glUniform3fv(uniform("uEyeInLens"), 2, eyeLocal);
    const float flip = lensDot(right, s.right) < 0 ? -1.f : 1.f;
    glUniform1f(uniform("uFlip"), flip);
    glUniform2fv(uniform("uMarkAxis"), 1, s.mode->reticleAxis);
    glUniform3f(uniform("uZero"), lensDot(s.zero, s.right), lensDot(s.zero, s.up), lensDot(s.zero, s.forward));
    glUniform1f(uniform("uTangentScale"), s.mode->angular);
    glUniform1f(uniform("uMarkScale"), s.mode->firstFocalPlane ? s.zoom/s.mode->lowZoom : 1.f);
    glUniform1i(uniform("uKind"), s.mode->kind);
    glUniform1i(uniform("uLive"), item.scene != 0);
    glUniform1i(uniform("uPalette"), s.mode->palette);
    glUniform1i(uniform("uMark"), 0);
    glUniform1i(uniform("uScene"), 1);
    glActiveTexture(GL_TEXTURE0);
    GLuint& mark = s_lensMarks[s.mode];
    if (!mark) {
        glGenTextures(1, &mark);
        glBindTexture(GL_TEXTURE_2D, mark);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, s.mode->width, s.mode->height, 0, GL_RGBA, GL_UNSIGNED_BYTE, s.mode->reticle);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }
    glBindTexture(GL_TEXTURE_2D, mark);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, item.scene);
    float fan[GEVR_OPTIC_POINTS+2][2] = {};
    memcpy(fan+1, s.mode->boundary, s.mode->points * sizeof(fan[0]));
    memcpy(fan+s.mode->points+1, fan+1, sizeof(fan[0]));
    glBindVertexArray(s_lensVao);
    glBindBuffer(GL_ARRAY_BUFFER, s_lensVbo);
    glBufferData(GL_ARRAY_BUFFER, (s.mode->points+2)*sizeof(fan[0]), fan, GL_STREAM_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, nullptr);
    glDrawArrays(GL_TRIANGLE_FAN, 0, s.mode->points+2);
    glBindBuffer(GL_ARRAY_BUFFER, oldBuffer);
    glBindVertexArray(oldVao);
    glUseProgram(oldProgram);
    for (int i = 0; i < 2; ++i) {
        glActiveTexture(GL_TEXTURE0+i);
        glBindTexture(GL_TEXTURE_2D, s_boundTex[i]);
    }
    glActiveTexture(GL_TEXTURE0+s_activeTexUnit);
    if (!depth) glDisable(GL_DEPTH_TEST);
    glDepthFunc(oldDepth);
    glDepthMask(depthMask);
    if (cull) glEnable(GL_CULL_FACE);
    if (stencil) glEnable(GL_STENCIL_TEST);
    if (offset) glEnable(GL_POLYGON_OFFSET_FILL);
    if (!blend) glDisable(GL_BLEND);
    glBlendFuncSeparate(oldSrc, oldDst, oldSrcA, oldDstA);
}

void gfx_mod_optic_surface(const GevrOpticSurface *surface, const float projection[16], bool invertY)
{
    gevr_mesh_settle();
    const GevrEyeOptic item = {*surface, modOpticScopeImage(*surface)};
    if (s_eyeRec) {
        s_eyeOptics.push_back(item);
        GevrEyeDraw event = {};
        event.optic = (uint32_t)s_eyeOptics.size();
        event.hand = (int8_t)s_eyeHand;
        memcpy(event.viewport, s_curViewport, sizeof(event.viewport));
        memcpy(event.scissor, s_curScissor, sizeof(event.scissor));
        event.clearDepthBefore = s_eyeClearDepth;
        s_eyeClearDepth = false;
        s_eyeDraws.push_back(event);
    }
    if (!s_eyeRecordOnly) drawModLens(item, projection, invertY);
}
