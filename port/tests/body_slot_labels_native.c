#include <assert.h>
#include <stdio.h>
#include <string.h>
typedef int s32;
typedef float f32;
typedef unsigned u32;
typedef int Gfx;
typedef struct { float m[4][4]; } Mtxf, Mtx;
typedef struct { struct { short ob[3], tc[2]; unsigned char cn[4]; int flag; } v; } Vtx;
struct fontchar { int kerningindex, baseline, height, width; void *pixeldata; } chars[96];
struct font { int kerning[169]; } font;
static struct fontchar *ptrFontZurichBoldChars = chars;
static struct font *ptrFontZurichBold = &font;
static Mtx matrices[4];
static Vtx vertices[100];
static int geometry, render, triangles, overlayExpected;
#define TRUE 1
#define FALSE 0
#define G_ZBUFFER 1
#define G_SHADE 2
#define G_SHADING_SMOOTH 4
#define G_LIGHTING 8
#define G_FOG 16
#define G_CULL_BOTH 32
#define G_TEXTURE_GEN 64
#define G_TEXTURE_GEN_LINEAR 128
#define G_RM_XLU_SURF 1
#define G_RM_XLU_SURF2 2
#define G_RM_ZB_XLU_SURF 3
#define G_RM_ZB_XLU_SURF2 4
#define osVirtualToPhysical(p) (p)
#define gSPMatrix(p, ...) ((void)(p))
#define gDPPipeSync(p) ((void)(p))
#define gSPClearGeometryMode(p, bits) ((void)(p), geometry &= ~(bits))
#define gSPSetGeometryMode(p, bits) ((void)(p), geometry |= (bits))
#define gDPSetRenderMode(p, a, b) ((void)(p), (void)(b), render = (a))
#define gDPSetCycleType(p, ...) ((void)(p))
#define gDPSetAlphaCompare(p, ...) ((void)(p))
#define gDPSetTexturePersp(p, ...) ((void)(p))
#define gDPSetTextureLOD(p, ...) ((void)(p))
#define gDPSetTextureLUT(p, ...) ((void)(p))
#define gDPSetTextureFilter(p, ...) ((void)(p))
#define gSPTexture(p, ...) ((void)(p))
#define gDPSetCombineMode(p, ...) ((void)(p))
#define gDPSetCombineLERP(p, ...) ((void)(p))
#define gDPSetPrimColor(p, ...) ((void)(p))
#define gSPVertex(p, ...) ((void)(p))
#define gDPLoadTextureBlock(p, ...) ((void)(p))
static void draw(void) {
    assert(!!(geometry & G_ZBUFFER) == !overlayExpected);
    assert(render == (overlayExpected ? G_RM_XLU_SURF : G_RM_ZB_XLU_SURF));
    triangles += 2;
}
#define gSP2Triangles(p, ...) ((void)(p), draw())
static void matrix_4x4_set_identity(Mtxf *m) { memset(m, 0, sizeof(*m)); }
static Mtx *dynAllocateMatrix(void) { return matrices; }
static Vtx *dynAllocateVertices(int n) { assert(n < 100); return vertices; }
static void guMtxF2L(float a[4][4], Mtx *b) { memcpy(b, a, sizeof(*b)); }
/* PRODUCTION */
int main(void) {
    Gfx commands[256]; float at[] = {0, -20, -30};
    for (int i = 0; i < 96; i++) { chars[i].height = 10; chars[i].width = 6; }
    geometry = G_ZBUFFER; overlayExpected = 1;
    assert(gevrDrawViewTagOverlay(commands, "SHOTGUN", at, 0.12f, 0) > commands);
    assert(triangles == 16 && (geometry & G_ZBUFFER) && render == G_RM_ZB_XLU_SURF);
    triangles = 0; overlayExpected = 0;
    assert(gevrDrawViewTag(commands, "PLAYER", at, 0.12f, 0, 0) > commands);
    assert(triangles == 14);
    puts("PASS: slot labels disable depth for panel and glyphs, restore it, keep player labels depth-tested");
    return 0;
}
