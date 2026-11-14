// GL_OVR_multiview emulation, for a GL without the extension (ANGLE on Metal:
// Vision Pro through Klepton, docs/mobile-platforms.md). The VR renderer is
// written for multiview: one framebuffer holds both eyes as layers of an array
// texture, and the shaders pick the eye with gl_ViewID_OVR. Here that becomes:
//   - a "multiview" framebuffer is two, one per layer (gevr_mv_FramebufferTextureMultiviewOVR);
//   - a multiview shader reads the eye from a uniform, gevr_ViewID (gevr_mv_ShaderSource);
//   - each draw or clear aimed at such a framebuffer runs once per eye.
// Off (every call passes straight through) unless gfx_opengl_init_multiview
// finds no GL_OVR_multiview and turns it on; the Quest keeps real multiview.
//
// Include after the file's GL headers (glad or GLES3): it redirects the calls
// below to the wrappers in gevr_mv_emu.cpp.
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

extern int gevrMvEmuActive;
void gevr_mv_emu_enable(void);

void gevr_mv_FramebufferTextureMultiviewOVR(GLenum target, GLenum attachment, GLuint texture,
                                            GLint level, GLint baseViewIndex, GLsizei numViews);
void gevr_mv_ShaderSource(GLuint shader, GLsizei count, const GLchar *const *string, const GLint *length);
void gevr_mv_LinkProgram(GLuint program);
void gevr_mv_DeleteFramebuffers(GLsizei n, const GLuint *framebuffers);
void gevr_mv_DrawArrays(GLenum mode, GLint first, GLsizei count);
void gevr_mv_DrawElements(GLenum mode, GLsizei count, GLenum type, const void *indices);
void gevr_mv_DrawRangeElements(GLenum mode, GLuint start, GLuint end, GLsizei count, GLenum type,
                               const void *indices);
void gevr_mv_DrawArraysInstanced(GLenum mode, GLint first, GLsizei count, GLsizei instances);
void gevr_mv_DrawElementsInstanced(GLenum mode, GLsizei count, GLenum type, const void *indices,
                                   GLsizei instances);
void gevr_mv_Clear(GLbitfield mask);
void gevr_mv_ClearBufferfv(GLenum buffer, GLint drawbuffer, const GLfloat *value);
void gevr_mv_ClearBufferfi(GLenum buffer, GLint drawbuffer, GLfloat depth, GLint stencil);

#ifdef __cplusplus
}
#endif

#ifndef GEVR_MV_EMU_IMPL
#undef glShaderSource
#undef glLinkProgram
#undef glDeleteFramebuffers
#undef glDrawArrays
#undef glDrawElements
#undef glDrawRangeElements
#undef glDrawArraysInstanced
#undef glDrawElementsInstanced
#undef glClear
#undef glClearBufferfv
#undef glClearBufferfi
#define glShaderSource          gevr_mv_ShaderSource
#define glLinkProgram           gevr_mv_LinkProgram
#define glDeleteFramebuffers    gevr_mv_DeleteFramebuffers
#define glDrawArrays            gevr_mv_DrawArrays
#define glDrawElements          gevr_mv_DrawElements
#define glDrawRangeElements     gevr_mv_DrawRangeElements
#define glDrawArraysInstanced   gevr_mv_DrawArraysInstanced
#define glDrawElementsInstanced gevr_mv_DrawElementsInstanced
#define glClear                 gevr_mv_Clear
#define glClearBufferfv         gevr_mv_ClearBufferfv
#define glClearBufferfi         gevr_mv_ClearBufferfi
#endif
