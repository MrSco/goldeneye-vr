/* Production monitor microcode and animation lists against a minimal model/DL stub (#309). */
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ultra64.h>
#include <bondtypes.h>
#include <bondconstants.h>
#include <assets/oddtextures.h>
#include "game/chrai.h"

#define MONITOR_TIMER_DELTA g_GlobalTimerDelta

s32 g_ClockTimer = 1;
f32 g_GlobalTimerDelta = 1.0f;

static Vertex s_vtxPool[4];
static Gfx s_gdlPool[64];
static union ModelRwData s_rwData;
static Vertex s_roVerts[4];
static union ModelRoData s_roData;
static ModelNode s_node;
static Model s_model;
static Mtxf s_renderPos;

Vtx *dynAllocateVertices(s32 count) { assert(count == 4); return (Vtx *)s_vtxPool; }
union ModelRwData *modelGetNodeRwData(Model *m, ModelNode *n) { (void)m; (void)n; return &s_rwData; }
s32 gevrBeanScreenQuad(Model *m, ModelNode *n, Vertex *v) { (void)m; (void)n; (void)v; return 0; }
void texSelect(Gfx **gdlptr, struct sImageTableEntry *tconfig, u32 a, s32 b, u32 c)
{
    (void)gdlptr; (void)tconfig; (void)a; (void)b; (void)c;
}
u32 randomGetNext(void) { return 0; }
uintptr_t osVirtualToPhysical(void *addr) { return (uintptr_t)addr; }

/* Minimal monitor image table matching assets/oddtextures.c entries exercised here. */
static struct sImageTableEntry s_testMonitorImages[50];
struct sImageTableEntry *monitorimages = s_testMonitorImages;

static void init_images(void)
{
    memset(s_testMonitorImages, 0, sizeof(s_testMonitorImages));
    s_testMonitorImages[IMGSINE]     = (struct sImageTableEntry){ 0, 0x20, 0x20, 0, G_IM_FMT_I, G_IM_SIZ_8b, G_TX_WRAP, G_TX_WRAP, 0 };
    s_testMonitorImages[IMGTEXT]     = (struct sImageTableEntry){ 0, 0x20, 0x20, 0, G_IM_FMT_I, G_IM_SIZ_8b, G_TX_WRAP, G_TX_WRAP, 0 };
    s_testMonitorImages[IMGBARS]     = (struct sImageTableEntry){ 0, 0x20, 0x20, 0, G_IM_FMT_I, G_IM_SIZ_8b, G_TX_WRAP, G_TX_WRAP, 0 };
    s_testMonitorImages[IMGWORLDMAP] = (struct sImageTableEntry){ 0, 0x80, 0x30, 0, G_IM_FMT_I, G_IM_SIZ_8b, G_TX_WRAP, G_TX_CLAMP, 0 };
}

static void init_model(void)
{
    memset(&s_model, 0, sizeof(s_model));
    memset(&s_node, 0, sizeof(s_node));
    memset(&s_roData, 0, sizeof(s_roData));
    memset(&s_rwData, 0, sizeof(s_rwData));
    memset(s_roVerts, 0, sizeof(s_roVerts));
    s_model.render_pos = &s_renderPos;
    s_roData.DisplayListCollisions.Vertices = s_roVerts;
    s_node.Opcode = MODELNODE_OPCODE_DLCOLLISION;
    s_node.Data = &s_roData;
}

static void init_screen(MonitorRecord *s, uintptr_t *list)
{
    memset(s, 0, sizeof(*s));
    s->cmdlist = list;
    s->offset = 0;
    s->pause60 = -1;
    s->xscale = 1.0f;
    s->yscale = 1.0f;
    s->xmid = 0.5f;
    s->ymid = 0.5f;
    s->red = s->green = s->blue = s->alpha = 255;
}

/* INSERT_TVCMD */
/* INSERT_ANIMS */
/* INSERT_HELPERS */
/* INSERT_MICROCODE */

static void step(MonitorRecord *s)
{
    process_monitor_animation_microcode(&s_model, &s_node, s, s_gdlPool, 1, 1);
}

static void require(int ok, const char *msg)
{
    if (!ok) {
        fprintf(stderr, "REGRESSION: %s\n", msg);
        exit(1);
    }
}

static int span_near(s32 actual, s32 expected)
{
    s32 diff = actual - expected;
    return diff >= -1 && diff <= 1;
}

static void test_green_text_up(void)
{
    MonitorRecord s;
    init_screen(&s, monAnim05GreenTextUp);

    /* Tick through the first MONVERTSCROLL(0xFFFFFE00, 80) = -0.5 texture heights. */
    for (int t = 1; t <= 80; t++) {
        step(&s);
        require(s.ymid <= 0.5f && s.ymid >= -0.001f, "green text ymid jumped out of range");
        require(span_near(s_vtxPool[0].t - s_vtxPool[2].t, 1024), "green text vertical UV span collapsed");
        require(span_near(s_vtxPool[0].s - s_vtxPool[2].s, 1024), "green text horizontal UV span collapsed");
    }
    require(fabsf(s.ymid - 0.0f) < 1e-3f, "green text did not scroll up by -0.5 over 80 ticks");

    /* Run long past 32 full wraps so wrapping UVs cannot overflow s16. */
    for (int t = 0; t < 20000; t++) {
        step(&s);
        require(span_near(s_vtxPool[0].t - s_vtxPool[2].t, 1024), "green text UV span broke after long scroll");
        require(span_near(s_vtxPool[0].s - s_vtxPool[2].s, 1024), "green text S span broke after long scroll");
    }
    require(s.ymid < -32.0f, "green text did not accumulate past 32 wraps in long-run check");
}

static void test_bar_graphs(void)
{
    MonitorRecord g, r;
    init_screen(&g, monAnim0AGreenBarGraph);
    init_screen(&r, monAnim08RedBarGraph);

    step(&g);
    require(fabsf(g.xmid - (0.5f - 640.0f / 1024.0f)) < 1e-3f,
            "green bar graph negative scroll did not sign-extend 0xFFFFFD80");
    require(span_near(s_vtxPool[0].s - s_vtxPool[1].s, 1024), "green bar graph S span collapsed");

    /* Bar graphs add +-0.625 every 10 ticks, crossing +-32 wraps in ~510 ticks. */
    for (int t = 0; t < 2000; t++) {
        step(&g);
        require(span_near(s_vtxPool[0].s - s_vtxPool[1].s, 1024), "green bar graph S span wrapped s16");
        step(&r);
        require(span_near(s_vtxPool[0].s - s_vtxPool[1].s, 1024), "red bar graph S span wrapped s16");
    }
    require(g.xmid < -32.0f && r.xmid > 32.0f, "bar graphs did not cross 32 wraps");
}

static void test_wave_and_worldmap(void)
{
    MonitorRecord w, m;
    init_screen(&w, monAnim03ThreeWavePattern);
    for (int t = 0; t < 300; t++) {
        step(&w);
        require(w.xmid > -20.0f && w.xmid < 20.0f, "three-wave pattern negative scroll exploded");
        require(s_vtxPool[0].s != s_vtxPool[1].s, "three-wave pattern horizontal UV collapsed");
    }

    init_screen(&m, monAnim10GlobalMap);
    /* First command scrolls X by 0xFFFFFC00 (-1.0) over 1024 ticks; T is clamped. */
    for (int t = 0; t < 1024; t++) {
        step(&m);
        require(m.xmid <= 0.501f && m.xmid >= -0.501f, "global map negative scroll exploded");
        require(span_near(s_vtxPool[0].s - s_vtxPool[1].s, 4096), "global map S span broke");
        require(span_near(s_vtxPool[0].t - s_vtxPool[2].t, 1536), "global map clamped T span broke");
    }
    require(fabsf(m.xmid - (-0.5f)) < 1e-3f, "global map did not scroll left by -1.0");
}

int main(void)
{
    init_images();
    init_model();
    test_green_text_up();
    test_bar_graphs();
    test_wave_and_worldmap();
    printf("PASS: 64-bit monitor negative scroll deltas and long-run wrapping UVs\n");
    return 0;
}
