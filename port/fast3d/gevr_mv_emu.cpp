// GL_OVR_multiview emulation: see gevr_mv_emu.h.
#define GEVR_MV_EMU_IMPL
#ifdef ANDROID
#include <GLES3/gl3.h>
#else
#include "glad/glad.h"
#endif
#include "gevr_mv_emu.h"

#include <cstring>
#include <string>
#include <unordered_map>

extern "C" {
int gevrMvEmuActive = 0;
int gevrMvEmuSingleView = 0;
}

// A multiview framebuffer (the name the renderer binds, holding layer
// baseViewIndex) -> the framebuffer of ours holding the next layer.
static std::unordered_map<GLuint, GLuint> s_secondView;
// A linked program -> its gevr_ViewID location (only programs that have one)
// and the value it holds now (-1: unknown).
struct ViewUniform { GLint location; GLint value; };
static std::unordered_map<GLuint, ViewUniform> s_viewLocation;

static const char kViewUniform[] = "gevr_ViewID";

extern "C" void gevr_mv_emu_enable(void) { gevrMvEmuActive = 1; }

static GLuint vr_bound_framebuffer(GLenum binding)
{
    GLint fb = 0;
    glGetIntegerv(binding, &fb);
    return (GLuint)fb;
}

extern "C" void gevr_mv_FramebufferTextureMultiviewOVR(GLenum target, GLenum attachment, GLuint texture,
                                                       GLint level, GLint baseViewIndex, GLsizei numViews)
{
    const GLenum binding = target == GL_READ_FRAMEBUFFER ? GL_READ_FRAMEBUFFER_BINDING : GL_DRAW_FRAMEBUFFER_BINDING;
    const GLuint first = vr_bound_framebuffer(binding);
    if (first == 0) return;   // the default framebuffer takes no attachment (multiview sets INVALID_OPERATION)
    glFramebufferTextureLayer(target, attachment, texture, level, baseViewIndex);

    GLuint &second = s_secondView[first];
    if (second == 0) glGenFramebuffers(1, &second);
    const GLuint draw = vr_bound_framebuffer(GL_DRAW_FRAMEBUFFER_BINDING);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, second);
    glFramebufferTextureLayer(GL_DRAW_FRAMEBUFFER, attachment, texture, level,
                              numViews > 1 ? baseViewIndex + 1 : baseViewIndex);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, draw);
}

// A multiview shader without the extension: no #extension or num_views lines,
// and gl_ViewID_OVR read from a uniform declared after #version. (Renamed in
// the text rather than #defined: a macro named gl_* is reserved in GLSL.)
static bool vr_rewrite_multiview_shader(std::string &src)
{
    if (src.find("GL_OVR_multiview") == std::string::npos && src.find("gl_ViewID_OVR") == std::string::npos)
        return false;
    std::string out;
    out.reserve(src.size() + 64);
    size_t pos = 0;
    while (pos < src.size()) {
        size_t end = src.find('\n', pos);
        end = end == std::string::npos ? src.size() : end + 1;
        const std::string line = src.substr(pos, end - pos);
        const bool extension = line.find("#extension") != std::string::npos
                               && line.find("GL_OVR_multiview") != std::string::npos;
        const bool views = line.find("num_views") != std::string::npos && line.find("layout") != std::string::npos;
        if (!extension && !views) out += line;
        pos = end;
    }
    for (size_t at = out.find("gl_ViewID_OVR"); at != std::string::npos; at = out.find("gl_ViewID_OVR", at))
        out.replace(at, 13, kViewUniform);

    const std::string decl = std::string("uniform highp uint ") + kViewUniform + ";\n";
    const size_t version = out.find("#version");
    if (version == std::string::npos) {
        out.insert(0, decl);
    } else {
        const size_t eol = out.find('\n', version);
        out.insert(eol == std::string::npos ? out.size() : eol + 1, eol == std::string::npos ? "\n" + decl : decl);
    }
    src.swap(out);
    return true;
}

extern "C" void gevr_mv_ShaderSource(GLuint shader, GLsizei count, const GLchar *const *string, const GLint *length)
{
    if (!gevrMvEmuActive || count <= 0 || !string) {
        glShaderSource(shader, count, string, length);
        return;
    }
    std::string src;
    for (GLsizei i = 0; i < count; i++) {
        if (!string[i]) continue;
        if (length && length[i] >= 0) src.append(string[i], (size_t)length[i]);
        else src.append(string[i]);
    }
    if (!vr_rewrite_multiview_shader(src)) {
        glShaderSource(shader, count, string, length);
        return;
    }
    const GLchar *text = src.c_str();
    glShaderSource(shader, 1, &text, nullptr);
}

extern "C" void gevr_mv_LinkProgram(GLuint program)
{
    glLinkProgram(program);
    if (!gevrMvEmuActive) return;
    const GLint location = glGetUniformLocation(program, kViewUniform);
    if (location >= 0) s_viewLocation[program] = {location, -1};
    else s_viewLocation.erase(program);
}

extern "C" void gevr_mv_DeleteFramebuffers(GLsizei n, const GLuint *framebuffers)
{
    if (gevrMvEmuActive && framebuffers) {
        for (GLsizei i = 0; i < n; i++) {
            auto it = s_secondView.find(framebuffers[i]);
            if (it == s_secondView.end()) continue;
            glDeleteFramebuffers(1, &it->second);
            s_secondView.erase(it);
        }
    }
    glDeleteFramebuffers(n, framebuffers);
}

// The framebuffer bound to `target` stops being multiview.
static void vr_single_view_again(GLenum target)
{
    if (!gevrMvEmuActive || s_secondView.empty()) return;
    const GLuint fb = vr_bound_framebuffer(target == GL_READ_FRAMEBUFFER ? GL_READ_FRAMEBUFFER_BINDING
                                                                         : GL_DRAW_FRAMEBUFFER_BINDING);
    auto it = s_secondView.find(fb);
    if (it == s_secondView.end()) return;
    glDeleteFramebuffers(1, &it->second);
    s_secondView.erase(it);
}

extern "C" void gevr_mv_FramebufferTexture2D(GLenum target, GLenum attachment, GLenum textarget, GLuint texture,
                                             GLint level)
{
    vr_single_view_again(target);
    glFramebufferTexture2D(target, attachment, textarget, texture, level);
}

extern "C" void gevr_mv_FramebufferTextureLayer(GLenum target, GLenum attachment, GLuint texture, GLint level,
                                                GLint layer)
{
    vr_single_view_again(target);
    glFramebufferTextureLayer(target, attachment, texture, level, layer);
}

extern "C" void gevr_mv_FramebufferRenderbuffer(GLenum target, GLenum attachment, GLenum renderbuffertarget,
                                                GLuint renderbuffer)
{
    vr_single_view_again(target);
    glFramebufferRenderbuffer(target, attachment, renderbuffertarget, renderbuffer);
}

// The current program's gevr_ViewID, set to `view` unless it already holds it.
static inline void vr_set_view(ViewUniform *uniform, GLint view)
{
    if (!uniform || uniform->value == view) return;
    glUniform1ui(uniform->location, (GLuint)view);
    uniform->value = view;
}

// Runs `draw` once per view when a multiview framebuffer is the draw target:
// layer 0 through the renderer's own framebuffer, layer 1 through ours, with
// gevr_ViewID set to match when the program has it. Any other target gets one
// draw as view 0, which is what gl_ViewID_OVR reads outside a multiview
// framebuffer (a HUD capture parks the right eye's offsets off-screen, so a
// leftover view 1 there drew the capture empty).
template <class Draw>
static inline void vr_each_view(bool usesProgram, Draw draw)
{
    if (!gevrMvEmuActive) {
        draw();
        return;
    }
    ViewUniform *uniform = nullptr;
    if (usesProgram && !s_viewLocation.empty()) {
        GLint program = 0;
        glGetIntegerv(GL_CURRENT_PROGRAM, &program);
        const auto it = s_viewLocation.find((GLuint)program);
        if (it != s_viewLocation.end()) uniform = &it->second;
    }
    const GLuint first = s_secondView.empty() ? 0 : vr_bound_framebuffer(GL_DRAW_FRAMEBUFFER_BINDING);
    const auto second = first ? s_secondView.find(first) : s_secondView.end();
    vr_set_view(uniform, 0);
    draw();
    if (second == s_secondView.end() || gevrMvEmuSingleView) return;
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, second->second);
    vr_set_view(uniform, 1);
    draw();
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, first);
}

extern "C" void gevr_mv_DrawArrays(GLenum mode, GLint first, GLsizei count)
{
    vr_each_view(true, [&] { glDrawArrays(mode, first, count); });
}

extern "C" void gevr_mv_DrawElements(GLenum mode, GLsizei count, GLenum type, const void *indices)
{
    vr_each_view(true, [&] { glDrawElements(mode, count, type, indices); });
}

extern "C" void gevr_mv_DrawRangeElements(GLenum mode, GLuint start, GLuint end, GLsizei count, GLenum type,
                                          const void *indices)
{
    vr_each_view(true, [&] { glDrawRangeElements(mode, start, end, count, type, indices); });
}

extern "C" void gevr_mv_DrawArraysInstanced(GLenum mode, GLint first, GLsizei count, GLsizei instances)
{
    vr_each_view(true, [&] { glDrawArraysInstanced(mode, first, count, instances); });
}

extern "C" void gevr_mv_DrawElementsInstanced(GLenum mode, GLsizei count, GLenum type, const void *indices,
                                              GLsizei instances)
{
    vr_each_view(true, [&] { glDrawElementsInstanced(mode, count, type, indices, instances); });
}

extern "C" void gevr_mv_Clear(GLbitfield mask)
{
    vr_each_view(false, [&] { glClear(mask); });
}

extern "C" void gevr_mv_ClearBufferfv(GLenum buffer, GLint drawbuffer, const GLfloat *value)
{
    vr_each_view(false, [&] { glClearBufferfv(buffer, drawbuffer, value); });
}

extern "C" void gevr_mv_ClearBufferfi(GLenum buffer, GLint drawbuffer, GLfloat depth, GLint stencil)
{
    vr_each_view(false, [&] { glClearBufferfi(buffer, drawbuffer, depth, stencil); });
}
