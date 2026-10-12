#ifndef GFX_RENDERING_API_H
#define GFX_RENDERING_API_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "gevr_skin_mesh.h"

struct ShaderProgram;

struct GfxClipParameters {
    bool z_is_from_0_to_1;
    bool invert_y;
};

enum FilteringMode { FILTER_NONE, FILTER_LINEAR, FILTER_THREE_POINT, FILTER_TRILINEAR };

/*
 * A static mesh's draw (gevr_static_mesh.h): what gfx_sp_vertex and
 * gfx_sp_tri1 would have put in each vertex of the batch, as uniforms over the
 * mesh's own Vtx (DAB's GFX_MESH_PARAMS, port/licenses/dabs-mod.txt).
 */
#define GFX_MESH_MAX_INPUTS 7
struct GfxMeshDrawParams {
    float mp[16];        // clip = mp * (x, y, z, 1), column-major; y negated when the target is
    float tex_rotation[2][2]; // cos/sin, after normalized tile coordinates
    float tex[2][4];     // per texture: u = s * [0] + [2], v = t * [1] + [3]
    float in_sel[GFX_MESH_MAX_INPUTS][16];   // per combiner input: which colour bytes (column-major)
    float in_const[GFX_MESH_MAX_INPUTS][4];  // ... plus what does not come from the vertex
    bool normal_tex1;
    float normal_axis[2][3];
    int num_inputs;
    bool skinned, lit, has_bounds, invert_y, authored_color, world_vertices;
    float lo[3], hi[3];
    int cull, num_lights;
    float ambient[3], light_dir[7][3], light_color[7][3];
    float fog[4];       // RGB and constant factor when G_FOG is clear
    float fog_mul, fog_offset;
    bool fog_enabled, fog_linear;
    GevrSkinPalette palette; // copied into redraw records, never a frame-arena pointer
};

struct GfxRenderingAPI {
    const char* (*get_name)(void);
    int (*get_max_texture_size)(void);
    struct GfxClipParameters (*get_clip_parameters)(void);
    void (*unload_shader)(struct ShaderProgram* old_prg);
    void (*load_shader)(struct ShaderProgram* new_prg);
    struct ShaderProgram* (*create_and_load_new_shader)(uint64_t shader_id0, uint32_t shader_id1);
    struct ShaderProgram* (*lookup_shader)(uint64_t shader_id0, uint32_t shader_id1);
    void (*shader_get_info)(struct ShaderProgram* prg, uint8_t* num_inputs, bool used_textures[2]);
    void (*clear_shaders)(void);
    uint32_t (*new_texture)(void);
    void (*select_texture)(int tile, uint32_t texture_id, bool linear_filter);
    void (*upload_texture)(const uint8_t* rgba32_buf, uint32_t width, uint32_t height);
    void (*set_sampler_parameters)(int sampler, bool linear_filter, uint32_t cms, uint32_t cmt);
    void (*set_depth_mode)(bool depth_test, bool depth_update, bool depth_compare, bool depth_source_prim, uint16_t zmode);
    void (*set_depth_range)(float znear, float zfar);
    void (*set_viewport)(int x, int y, int width, int height);
    void (*set_scissor)(int x, int y, int width, int height);
    void (*set_use_alpha)(bool use_alpha, bool modulate);
    void (*draw_triangles)(float buf_vbo[], size_t buf_vbo_len, size_t buf_vbo_num_tris);
    void (*init)(void);
    void (*on_resize)(void);
    void (*start_frame)(void);
    void (*end_frame)(void);
    void (*finish_render)(void);
    int (*create_framebuffer)();
    void (*update_framebuffer_parameters)(int fb_id, uint32_t width, uint32_t height, uint32_t msaa_level,
                                          bool opengl_invert_y, bool render_target, bool has_depth_buffer,
                                          bool can_extract_depth);
    bool (*start_draw_to_framebuffer)(int fb_id, float noise_scale);
    void (*copy_framebuffer)(int fb_dst, int fb_src, int left, int top, bool flip_y, bool use_back);
    void (*clear_framebuffer)(bool clear_color, bool clear_depth);
    void (*resolve_msaa_color_buffer)(int fb_id_target, int fb_id_source);
    void* (*get_framebuffer_texture_id)(int fb_id);
    void (*select_texture_fb)(int fb_id);
    void (*delete_texture)(uint32_t texID);
    void (*set_texture_filter)(enum FilteringMode mode);
    enum FilteringMode (*get_texture_filter)(void);

    // VR
    void (*set_eye_offsets)(float lx, float lfrustum_x, float lhud, float lfrustum_y,
                            float rx, float rfrustum_x, float rhud, float rfrustum_y);
    bool (*is_multiview)(void);
    void (*mirror_to_desktop)(uint32_t src_w, uint32_t src_h, uint32_t dst_w, uint32_t dst_h);

    // GoldenEye issue #25: a texture-pack image, with mipmaps (gevr_texpack.cpp)
    void (*upload_texture_hd)(const uint8_t* rgba32_buf, uint32_t width, uint32_t height);

    // GoldenEye: a static mesh (gevr_static_mesh.h), its buffers made into *gpu the
    // first time; false when it cannot be drawn so, and the caller draws it as before
    bool (*draw_mesh)(void** gpu, const void* vertices, uint32_t num_vertices, const uint32_t* indices,
                      uint32_t num_indices, uint32_t first, uint32_t count, const struct GfxMeshDrawParams* params);
    void (*delete_mesh)(void* gpu);
    bool (*scope_sees_mesh)(const float* mp, const float* lo, const float* hi);

};

#endif
