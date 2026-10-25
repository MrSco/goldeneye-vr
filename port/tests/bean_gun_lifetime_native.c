/* Actual Bean registration/emission/mesh teardown, authored launcher geometry.
 * Track requested heap bytes, including the mesh builder's vertex/index copies. */
#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

static size_t liveBytes, peakBytes;
static int releasedMeshes;
union allocationHeader {
    struct { size_t size; unsigned magic; } info;
    max_align_t alignment;
};
static void *testMalloc(size_t size)
{
    union allocationHeader *h = malloc(sizeof(*h) + size);
    assert(h);
    h->info.size = size;
    h->info.magic = 0xb3a90007;
    liveBytes += size;
    if (liveBytes > peakBytes) peakBytes = liveBytes;
    return h + 1;
}
static void testFree(void *p)
{
    if (!p) return;
    union allocationHeader *h = (union allocationHeader *)p - 1;
    assert(h->info.magic == 0xb3a90007);
    liveBytes -= h->info.size;
    h->info.magic = 0;
    free(h);
}
static void *testCalloc(size_t count, size_t size)
{
    assert(!count || size <= SIZE_MAX / count);
    void *p = testMalloc(count * size);
    memset(p, 0, count * size);
    return p;
}
static void *testRealloc(void *p, size_t size)
{
    if (!p) return testMalloc(size);
    union allocationHeader *h = (union allocationHeader *)p - 1;
    void *next = testMalloc(size);
    memcpy(next, p, size < h->info.size ? size : h->info.size);
    testFree(p);
    return next;
}
#define malloc testMalloc
#define calloc testCalloc
#define realloc testRealloc
#define free testFree

/* PRODUCTION AND EXISTING ENGINE STUBS */

const char *gevrBeanCeRoot(void) { return NULL; }

#define OWNERS 16 /* two hand buffers for eight players */
struct nativeModel {
    ModelFileHeader header;
    ModelNode nodes[2];
    union ModelRoData data[2];
    Vertex host[3];
};
static struct nativeModel models[OWNERS + 1];

static void initModel(struct nativeModel *m)
{
    memset(m, 0, sizeof(*m));
    m->host[1].coord.x = 10;
    m->host[2].coord.y = 10;
    m->nodes[0].Opcode = MODELNODE_OPCODE_GROUP;
    m->nodes[0].Data = &m->data[0];
    m->nodes[0].Child = &m->nodes[1];
    m->nodes[1].Opcode = MODELNODE_OPCODE_DL;
    m->nodes[1].Data = &m->data[1];
    m->nodes[1].Parent = &m->nodes[0];
    m->data[1].DisplayList.Vertices = m->host;
    m->data[1].DisplayList.numVertices = 3;
    m->data[1].DisplayList.ModelType = 3;
    m->header.RootNode = m->nodes;
    m->header.numMatrices = 1;
}
static Gfx *shown(struct nativeModel *m)
{
    return gevrBeanShownGdl(&m->nodes[1], m->data[1].DisplayList.Primary);
}
static GevrStaticMesh *meshIn(const Gfx *gdl)
{
    for (int i = 0; i < 12; i++) {
        unsigned op = (gdl->words.w0 >> 24) & 255;
        if (op == G_GEVR_MESH) return (void *)gdl->words.w1;
        gdl = op == (G_DL & 255) ? (void *)gdl->words.w1 : gdl + 1;
    }
    assert(!"launcher has no retained mesh");
    return NULL;
}
static void ageFrames(int n)
{
    for (int i = 0; i < n; i++) gevrBeanBeginFrame();
}
static void forgetAll(void)
{
    for (int i = 0; i < OWNERS; i++) gevrBeanForgetModel(&models[i].header);
    ageFrames(3);
}

static int growth(void)
{
    gevrBeanModelApply(&models[0].header, "GrocketlaunchZ");
    const size_t first = liveBytes;
    size_t settled = 0;
    for (int i = 1; i <= 300; i++) {
        ageFrames(1);
        gevrBeanModelApply(&models[0].header, "GrocketlaunchZ");
        assert(shown(&models[0]));
        if (i == 20) settled = liveBytes;
    }
    printf("launcher: first %zu bytes, after 21 loads %zu, after 301 loads %zu; "
           "%d allocations, %d meshes\n", first, settled, liveBytes, s_nalloc, s_nrigidMeshes);
    if (liveBytes > first * 5 || liveBytes != settled) {
        fprintf(stderr, "retained geometry grew across launcher reloads\n");
        return 3;
    }
    forgetAll();
    assert(s_nalloc == 0 && s_nrigidMeshes == 0);
    gevrBeanReset();
    assert(liveBytes == 0);
    return 0;
}

static void lifetime(const char *kind)
{
    gevrBeanModelApply(&models[0].header, "GrocketlaunchZ");
    const Gfx *oldList = shown(&models[0]);
    GevrStaticMesh *oldMesh = meshIn(oldList);
    const unsigned vertices = oldMesh->numVertices;
    const size_t before = liveBytes;
    /* Stand in for the backend's handles; production teardown must call it. */
    oldMesh->gpu = malloc(16);
    int releases = releasedMeshes;
    if (!strcmp(kind, "storage")) {
        gevrBeanForgetModelStorage(models[0].nodes, sizeof(models[0].nodes));
    } else if (!strcmp(kind, "unsupported")) {
        gevrBeanModelApply(&models[0].header, "NoBeanReplacementZ");
    } else {
        gevrBeanForgetModel(&models[0].header);
    }
    assert(!shown(&models[0]));
    /* Lists from either of the two preceding frames must remain readable. */
    for (int i = 0; i < 2; i++) {
        ageFrames(1);
        assert(meshIn(oldList) == oldMesh && oldMesh->numVertices == vertices);
        assert(((const Vtx *)oldMesh->vertices)[0].v.cn[3] == 255);
        assert(releasedMeshes == releases && liveBytes >= before);
    }
    ageFrames(1);
    assert(releasedMeshes > releases && liveBytes < before);
    assert(s_nalloc == 0 && s_nrigidMeshes == 0);
    gevrBeanReset();
    gevrBeanReset();
    assert(liveBytes == 0);
}

static void mixed(void)
{
    /* Stage-owned scenery must survive collection of unrelated hand buffers. */
    struct nativeModel *prop = &models[OWNERS];
    gevrBeanModelApply(&prop->header, "PchrblueprintsZ");
    Gfx *propList = shown(prop);
    GevrStaticMesh *propMesh = meshIn(propList);
    const size_t permanent = liveBytes;
    for (int i = 0; i < OWNERS; i++)
        gevrBeanModelApply(&models[i].header, "GrocketlaunchZ");
    ageFrames(3);
    size_t settled = 0;
    for (int i = 0; i < 320; i++) {
        int owner = i % OWNERS;
        ageFrames(1);
        /* A new header can reuse the old model's native file storage. */
        gevrBeanForgetModelStorage(models[owner].nodes, sizeof(models[owner].nodes));
        gevrBeanModelApply(&models[owner].header,
                          (i / OWNERS) & 1 ? "Gak47Z" : "GrocketlaunchZ");
        assert(shown(&models[owner]));
        assert(shown(prop) == propList && meshIn(propList) == propMesh);
        if (i == 159) settled = liveBytes;
    }
    assert(liveBytes == settled);
    forgetAll();
    assert(shown(prop) == propList && liveBytes >= permanent);
    /* Reset with both a current and an uncollected retired weapon. */
    gevrBeanModelApply(&models[0].header, "GrocketlaunchZ");
    gevrBeanModelApply(&models[0].header, "Gak47Z");
    gevrBeanReset();
    gevrBeanReset();
    assert(liveBytes == 0);
    printf("16 hand buffers and scenery remain bounded: %zu bytes\n", settled);
}

int main(int argc, char **argv)
{
    assert(argc == 2);
    for (int i = 0; i <= OWNERS; i++) initModel(&models[i]);
    gevrBeanInit();
    gevrBeanSetEnabled(1);
    if (!strcmp(argv[1], "growth")) return growth();
    if (!strcmp(argv[1], "mixed")) mixed();
    else {
        if (!strcmp(argv[1], "wrap")) s_frameCount = UINT_MAX - 1;
        lifetime(argv[1]);
    }
    puts("gun geometry lifetime passed");
    return 0;
}
