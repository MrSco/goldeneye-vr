/* The game draws self-contained pack models on the same view-space matrices
 * as cartridge models. Fast3D consumes the display list and RGBA images. */
#include "gevr_modmesh.h"
#include "gevr_modassets.h"
#include "gevr_packmesh.h"
#include "dyn.h"
#include "matrixmath.h"
#include "system.h"
#include <PR/gbi.h>
#include <ctype.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

extern void gfx_texture_cache_delete(const uint8_t *address);

struct GevrModMesh {
    char name[64];
    GevrPackMeshView view;
    uint8_t *bytes;
    Vtx *vertices;
    Gfx *commands;
    struct GevrModMesh *next;
    /* The load-block command names these aligned stand-in tiles. Fast3D
     * replaces each with the full RGBA picture before decoding the tile. */
    uint64_t keys[GEVR_PACKMESH_MAX_TEXTURES][64];
};

static GevrModMesh *s_meshes;
static GevrModMesh *s_retired;
static int s_frameWarning;

static s16 rounded(float v)
{
    return v > 32767 ? 32767 : v < -32768 ? -32768 : (s16)lrintf(v);
}

static int tileMask(int size)
{
    int mask = 0;
    while ((1 << mask) < size) ++mask;
    return mask;
}

/* ASCII picture indexes separated by whitespace, bounded for 32 pictures. */
static int readOpaquePictures(const char *name, unsigned pictures, uint32_t *mask)
{
    unsigned char bytes[128];
    size_t i = 0, n;
    FILE *f = gevrModAssetOpenModelOpacity(name);
    *mask = 0;
    if (f == NULL) return 1;
    n = fread(bytes, 1, sizeof(bytes), f);
    int complete = n < sizeof(bytes) && !ferror(f);
    fclose(f);
    if (!complete) return 0;
    while (i < n) {
        unsigned index = 0, digits = 0;
        if (isspace(bytes[i])) { ++i; continue; }
        while (i < n && bytes[i] >= '0' && bytes[i] <= '9') {
            index = index * 10 + bytes[i++] - '0';
            if (++digits > 2 || index >= pictures) return 0;
        }
        if (!digits || (i < n && !isspace(bytes[i]))) return 0;
        if (*mask & (UINT32_C(1) << index)) return 0;
        *mask |= UINT32_C(1) << index;
    }
    return 1;
}

static size_t buildMesh(GevrModMesh *mesh, uint32_t opaquePictures)
{
    const GevrPackMeshView *view = &mesh->view;
    Gfx *gdl = mesh->commands;
    unsigned tex, pass;
    uint32_t used = 0, translucent = 0;
    int rgb = 0;
    size_t tri;
    unsigned i;

    for (tri = 0; tri < view->triangles; ++tri) {
        GevrPackCorner corners[3];
        float u, v;
        const size_t first = tri * 3;
        const GevrPackPicture *pic;
        for (i = 0; i < 3; ++i) gevrPackMeshCorner(view, first + i, &corners[i]);
        pic = &view->pictures[corners[0].texture];
        used |= UINT32_C(1) << corners[0].texture;
        u = floorf(corners[0].uv[0]);
        v = floorf(corners[0].uv[1]);
        for (i = 0; i < 3; ++i) {
            const GevrPackCorner *corner = &corners[i];
            Vtx *vertex = &mesh->vertices[first + i];
            /* X right/Y ahead/Z up -> X left/Y up/Z ahead. */
            vertex->v.ob[0] = rounded(-corner->position[0] * 1000.f);
            vertex->v.ob[1] = rounded(corner->position[2] * 1000.f);
            vertex->v.ob[2] = rounded(corner->position[1] * 1000.f);
            vertex->v.tc[0] = rounded((corner->uv[0] - u) * pic->width * 32);
            vertex->v.tc[1] = rounded((corner->uv[1] - v) * pic->height * 32);
            memcpy(vertex->v.cn, corner->rgba, sizeof(corner->rgba));
            if (corner->rgba[3] < 255) translucent |= UINT32_C(1) << corner->texture;
        }
    }

    for (tex = 0; tex < view->textures; ++tex) {
        const uint32_t bit = UINT32_C(1) << tex;
        const GevrPackPicture *pic = &view->pictures[tex];
        size_t a, bytes = (size_t)pic->width * pic->height * 4;
        if (!(used & bit) || ((translucent | opaquePictures) & bit)) continue;
        for (a = 3; a < bytes; a += 4) {
            if (pic->rgba[a] < 255) { translucent |= bit; break; }
        }
    }

    /* Pack draws bypass subdraw. Set up depth explicitly so opaque triangles
     * occlude one another across the held model and its attachments. */
    gDPPipeSync(gdl++);
    gDPSetCycleType(gdl++, G_CYC_2CYCLE);
    gDPSetTexturePersp(gdl++, G_TP_PERSP);
    gDPSetTextureLUT(gdl++, G_TT_NONE);
    gDPSetAlphaCompare(gdl++, G_AC_NONE);
    gSPClearGeometryMode(gdl++, G_TEXTURE_GEN | G_TEXTURE_GEN_LINEAR | G_LIGHTING | G_FOG | G_CULL_BOTH);
    gSPSetGeometryMode(gdl++, G_ZBUFFER | G_SHADE | G_SHADING_SMOOTH);
    gDPSetRenderMode(gdl++, G_RM_AA_ZB_OPA_SURF, G_RM_AA_ZB_OPA_SURF2);
    gDPSetCombineMode(gdl++, G_CC_MODULATERGBA, G_CC_MODULATERGBA2);
    for (pass = 0; pass < 2; ++pass) {
        if (pass && translucent) {
            gDPPipeSync(gdl++);
            gDPSetRenderMode(gdl++, G_RM_AA_ZB_XLU_SURF, G_RM_AA_ZB_XLU_SURF2);
        }
        for (tex = 0; tex < view->textures; ++tex) {
            const GevrPackPicture *pic = &view->pictures[tex];
            const uint32_t bit = UINT32_C(1) << tex;
            const int w = pic->width, h = pic->height, ms = tileMask(w), mt = tileMask(h);
            size_t start = 0;
            unsigned run = 0;
            int tile;
            if (!(used & bit) || ((translucent & bit) != 0) != (pass != 0)) continue;
            const int ignoresTextureAlpha = (opaquePictures & bit) != 0;
            if (rgb != ignoresTextureAlpha) {
                rgb = ignoresTextureAlpha;
                gDPPipeSync(gdl++);
                if (rgb) {
                    gDPSetCombineMode(gdl++, G_CC_MODULATERGB, G_CC_MODULATERGB2);
                } else {
                    gDPSetCombineMode(gdl++, G_CC_MODULATERGBA, G_CC_MODULATERGBA2);
                }
            }
            gSPTexture(gdl++, 0xffff, 0xffff, 0, G_TX_RENDERTILE, G_ON);
            gDPLoadTextureBlock(gdl++, mesh->keys[tex], G_IM_FMT_RGBA, G_IM_SIZ_16b, 16, 16, 0,
                G_TX_WRAP, G_TX_WRAP, 4, 4, G_TX_NOLOD, G_TX_NOLOD);
            for (tile = G_TX_RENDERTILE; tile <= G_TX_RENDERTILE + 1; ++tile) {
                gDPSetTile(gdl++, G_IM_FMT_RGBA, G_IM_SIZ_16b, (w * 2 + 7) >> 3, 0, tile,
                    0, G_TX_WRAP, mt, G_TX_NOLOD, G_TX_WRAP, ms, G_TX_NOLOD);
                gDPSetTileSize(gdl++, tile, 0, 0, (w - 1) << G_TEXTURE_IMAGE_FRAC, (h - 1) << G_TEXTURE_IMAGE_FRAC);
            }
            for (tri = 0; tri <= view->triangles; ++tri) {
                const int match = tri < view->triangles && view->corners[tri * 3 * GEVR_PACKMESH_CORNER_BYTES + 24] == tex;
                if (run && (!match || run == 5)) {
                    gSPVertex(gdl++, osVirtualToPhysical(&mesh->vertices[start * 3]), run * 3, 0);
                    for (i = 0; i < run; ++i) gSP1Triangle(gdl++, i * 3, i * 3 + 1, i * 3 + 2, 0);
                    run = 0;
                }
                if (match) {
                    if (!run) start = tri;
                    ++run;
                }
            }
        }
    }
    if (translucent || rgb) {
        gDPPipeSync(gdl++);
        if (translucent) { gDPSetRenderMode(gdl++, G_RM_AA_ZB_OPA_SURF, G_RM_AA_ZB_OPA_SURF2); }
        if (rgb) { gDPSetCombineMode(gdl++, G_CC_MODULATERGBA, G_CC_MODULATERGBA2); }
    }
    gSPEndDisplayList(gdl++);
    return (size_t)(gdl - mesh->commands);
}

int gevrModMeshLoad(const char *name, const GevrModMesh **out)
{
    FILE *f;
    uint8_t magic[8], *bytes = NULL;
    GevrPackMeshView view;
    GevrModMesh *mesh = NULL;
    long length;
    size_t commands;
    uint32_t opaquePictures;
    const char *reason = "invalid file length";

    *out = NULL;
    if (name == NULL || !name[0]) return 0;
    for (const GevrModMesh *cached = s_meshes; cached != NULL; cached = cached->next) {
        if (strcmp(cached->name, name) == 0) {
            *out = cached;
            return 1;
        }
    }
    f = gevrModAssetOpenModel(name);
    if (f == NULL) return 0;
    if (fread(magic, 1, sizeof(magic), f) != sizeof(magic)
        || (memcmp(magic, "GEVRWPN1", 8) && memcmp(magic, "GEVRWPN2", 8))) {
        fclose(f);
        return 0;
    }
    if (fseek(f, 0, SEEK_END) || (length = ftell(f)) < 16
        || (uintmax_t)length > PTRDIFF_MAX || fseek(f, 0, SEEK_SET)) goto refused;
    reason = "model bytes could not be allocated";
    bytes = malloc((size_t)length);
    if (bytes == NULL) goto refused;
    reason = "malformed model";
    if (fread(bytes, 1, (size_t)length, f) != (size_t)length
        || gevrPackMeshRead(bytes, (size_t)length, &view) != 1) goto refused;
    reason = "invalid .opaque picture indexes";
    if (!readOpaquePictures(name, view.textures, &opaquePictures)) goto refused;
    commands = 32 + (size_t)view.textures * 18;
    reason = "display list size overflow";
    if (view.triangles > (SIZE_MAX / sizeof(Gfx) - commands) / 2
        || view.triangles > SIZE_MAX / sizeof(Vtx) / 3) goto refused;
    commands += (size_t)view.triangles * 2;
    reason = "display list or vertices could not be allocated";
    if ((mesh = calloc(1, sizeof(*mesh))) == NULL) goto refused;
    mesh->vertices = calloc((size_t)view.triangles * 3, sizeof(Vtx));
    mesh->commands = calloc(commands, sizeof(Gfx));
    if (!mesh->vertices || !mesh->commands) goto refused;
    fclose(f);
    mesh->bytes = bytes;
    mesh->view = view;
    snprintf(mesh->name, sizeof(mesh->name), "%s", name);
    commands = buildMesh(mesh, opaquePictures);
    mesh->next = s_meshes;
    s_meshes = mesh;
    *out = mesh;
    sysLogPrintf(LOG_NOTE, "mods: pack model %s: %u triangles, %u RGBA textures (GEVRWPN%d), %zu display-list commands",
        name, view.triangles, view.textures, view.hasWatchMount ? 2 : 1, commands);
    return 1;

refused:
    fclose(f);
    free(bytes);
    if (mesh) {
        free(mesh->vertices);
        free(mesh->commands);
        free(mesh);
    }
    sysLogPrintf(LOG_WARNING, "mods: refused pack model %s: %s", name, reason);
    return -1;
}

static const GevrPackPicture *meshPicture(const GevrModMesh *m, const void *address)
{
    const uintptr_t off = (uintptr_t)address - (uintptr_t)m->keys;
    if (off < sizeof(m->keys) && off % sizeof(m->keys[0]) == 0) {
        const unsigned tex = (unsigned)(off / sizeof(m->keys[0]));
        if (tex < m->view.textures) return &m->view.pictures[tex];
    }
    return NULL;
}

int gevrPackMeshTexture(const void *address, const uint8_t **rgba, int *width, int *height)
{
    const GevrModMesh *m;
    const GevrPackPicture *pic = NULL;
    for (m = s_meshes; pic == NULL && m != NULL; m = m->next) pic = meshPicture(m, address);
    /* A Lua callback can reset the mission's models while commands already
     * emitted this frame still reference them. Keep their textures valid. */
    for (m = s_retired; pic == NULL && m != NULL; m = m->next) pic = meshPicture(m, address);
    if (pic == NULL) return 0;
    *rgba = pic->rgba;
    *width = pic->width;
    *height = pic->height;
    return 1;
}

Gfx *gevrModMeshDraw(Gfx *gdl, const Mtxf *pose, const GevrModMesh *mesh)
{
    Mtx *mtx;
    if (!gevrDynRoom(gdl, 4, sizeof(Mtx))) {
        if (!s_frameWarning) {
            sysLogPrintf(LOG_WARNING, "mods: no frame room for pack model %s", mesh->name);
            s_frameWarning = 1;
        }
        return gdl;
    }
    mtx = dynAllocate(sizeof(*mtx));
    guMtxF2L((float (*)[4])pose->m, mtx);
    gSPMatrix(gdl++, osVirtualToPhysical(mtx), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
    gDPNoOpTag(gdl++, GEVR_PACKMESH_DRAW_BEGIN);
    gSPDisplayList(gdl++, osVirtualToPhysical(mesh->commands));
    gDPNoOpTag(gdl++, GEVR_PACKMESH_DRAW_END);
    return gdl;
}

void gevrModMeshPoint(const Mtxf *fit, const float point[3], float out[3])
{
    for (int axis = 0; axis < 3; ++axis) {
        out[axis] = fit == NULL ? point[axis] : fit->m[3][axis]
            + fit->m[0][axis] * point[0] + fit->m[1][axis] * point[1] + fit->m[2][axis] * point[2];
    }
}

void gevrPackMeshCollect(void)
{
    while (s_retired != NULL) {
        GevrModMesh *m = s_retired;
        unsigned tex;
        s_retired = m->next;
        for (tex = 0; tex < m->view.textures; ++tex) gfx_texture_cache_delete((const uint8_t *)m->keys[tex]);
        free(m->commands);
        free(m->vertices);
        free(m->bytes);
        free(m);
    }
}

void gevrModMeshReset(void)
{
    while (s_meshes != NULL) {
        GevrModMesh *mesh = s_meshes;
        s_meshes = mesh->next;
        mesh->next = s_retired;
        s_retired = mesh;
    }
#ifdef GEVR_HEADLESS
    gevrPackMeshCollect();
#endif
    s_frameWarning = 0;
}
