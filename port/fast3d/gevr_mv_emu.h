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
// Set around a draw that belongs to the left view only: a HUD capture's draws
// (gForceFlatShaderForMenu), which hide the right eye by moving its geometry
// 1000 units away. Real multiview drops that view off-screen; a second pass
// is skipped instead.
extern int gevrMvEmuSingleView;
void gevr_mv_emu_enable(void);
// One view at a time (gfx_pc.cpp's eye replay): view 0 or 1 makes every draw
// and clear go once, as that view, into the framebuffer bound for it; view 1
// binds the multiview framebuffer's second layer first. -1 ends it (the
// multiview framebuffer bound again). Switching framebuffers per draw ends a
// Metal render pass each time, which reloads and stores both whole eye images
// (Vision Pro, 2026-11-25: ~300 draws made ~600 passes, 20-47 ms a frame).
// Returns 0 when the bound framebuffer has no second view (nothing changed).
int gevr_mv_emu_view_pass(int view);

void gevr_mv_FramebufferTextureMultiviewOVR(GLenum target, GLenum attachment, GLuint texture,
                                            GLint level, GLint baseViewIndex, GLsizei numViews);
// A plain attachment makes a framebuffer single-view again: GL reuses the
// name of a deleted multiview framebuffer (the eye's, when VR re-initialises)
// for an ordinary one, such as the HUD copy's.
void gevr_mv_FramebufferTexture2D(GLenum target, GLenum attachment, GLenum textarget, GLuint texture, GLint level);
void gevr_mv_FramebufferTextureLayer(GLenum target, GLenum attachment, GLuint texture, GLint level, GLint layer);
void gevr_mv_FramebufferRenderbuffer(GLenum target, GLenum attachment, GLenum renderbuffertarget,
                                     GLuint renderbuffer);
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
#undef glFramebufferTexture2D
#undef glFramebufferTextureLayer
#undef glFramebufferRenderbuffer
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
#define glFramebufferTexture2D    gevr_mv_FramebufferTexture2D
#define glFramebufferTextureLayer gevr_mv_FramebufferTextureLayer
#define glFramebufferRenderbuffer gevr_mv_FramebufferRenderbuffer
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
