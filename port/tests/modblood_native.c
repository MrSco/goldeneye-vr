/* gevr_modblood.c against a stub game: the seven effect sheets either all
 * load or the call is refused and stock effects stay; Blood patch off and
 * co-op keep stock effects; blood(false) clears what's on screen.
 *
 *   modblood_native <sheet folder> ok|refused
 *
 * One case a run: the module tries its sheets once a process, as the game
 * does once a boot (test_modblood.py writes each folder). */
#include "gevr_modblood.c"

#include <errno.h>

static const char *s_folder;
static int s_net;
static char s_lastLog[256];

f32 D_800364CC = 1.f;
int VrBloodPatch = 1;
s32 g_GlobalTimer = 100;
bg_portal_data_entry *g_BgPortals;
struct image_entry *g_Textures;

/* The sheets: assets/effects/<name>.png in the test's folder */
FILE *gevrModAssetOpenEffect(const char *name)
{
    char path[1024];
    snprintf(path, sizeof(path), "%s/%s.png", s_folder, name);
    return fopen(path, "rb");
}

/* pngread.c's decode isn't under test: an RGBA buffer the IHDR's size */
u8 *pngReadMem(const void *data, u32 size, const char *name, s32 *outWidth, s32 *outHeight)
{
    const u8 *b = data;
    (void)name;
    if (size < 24) return NULL;
    *outWidth = (b[16] << 24) | (b[17] << 16) | (b[18] << 8) | b[19];
    *outHeight = (b[20] << 24) | (b[21] << 16) | (b[22] << 8) | b[23];
    return calloc((size_t)*outWidth * (size_t)*outHeight, 4);
}

bool netIsActive(void) { return s_net; }
s32 sysArgCheck(const char *arg) { (void)arg; return 0; }
void sysLogPrintf(s32 level, const char *format, ...)
{
    va_list args;
    (void)level;
    va_start(args, format);
    vsnprintf(s_lastLog, sizeof(s_lastLog), format, args);
    va_end(args);
}

/* The view: identity, so a point at z -500 is in front of the camera */
static Mtxf s_view = {.m = {{1, 0, 0, 0}, {0, 1, 0, 0}, {0, 0, 1, 0}, {0, 0, 0, 1}}};
Mtxf *camGetWorldToScreenMtxf(void) { return &s_view; }
static Mtx s_projection;
Mtx *currentPlayerGetProjectionMatrix(void) { return &s_projection; }
void mtx4TransformVecInPlace(Mtxf *matrix, struct coord3d *vector)
{
    f32 *vec = vector->f, out[3];
    for (int i = 0; i < 3; ++i) {
        out[i] = matrix->m[0][i] * vec[0] + matrix->m[1][i] * vec[1] + matrix->m[2][i] * vec[2] + matrix->m[3][i];
    }
    memcpy(vec, out, sizeof(out));
}
void mtx4RotateVecInPlace(Mtxf *matrix, struct coord3d *vector)
{
    f32 *vec = vector->f, out[3];
    for (int i = 0; i < 3; ++i) {
        out[i] = matrix->m[0][i] * vec[0] + matrix->m[1][i] * vec[1] + matrix->m[2][i] * vec[2];
    }
    memcpy(vec, out, sizeof(out));
}
static Mtx s_matrices[4096];
static Vtx s_vertices[16384];
static int s_matrixCount, s_vertexCount;
Mtx *dynAllocateMatrix(void) { return &s_matrices[s_matrixCount++ % 4096]; }
Vtx *dynAllocateVertices(s32 count)
{
    Vtx *v = &s_vertices[s_vertexCount % (16384 - 64)];
    s_vertexCount += count;
    return v;
}
void guMtxF2L(float mf[4][4], Mtx *m) { (void)mf; (void)m; }
uintptr_t osVirtualToPhysical(void *address) { return (uintptr_t)address; }

/* No world, no ragdolls, no Bean: drops fly without landing */
void bgFindRoomsAlongSegment(coord3d *pos1, coord3d *pos2, u8 *initialRooms, u8 *outRoomSet, s32 *outRoomNums,
    s32 *outRoomNumsCount, s32 outRoomNumsMax)
{
    (void)pos1, (void)pos2, (void)initialRooms, (void)outRoomSet, (void)outRoomNums, (void)outRoomNumsMax;
    *outRoomNumsCount = 0;
}
s32 bgTestBulletHitBackground(coord3d *from, coord3d *to, s32 roomnum, struct HitThing *hit)   /* bg.h's bool */
{
    (void)from, (void)to, (void)roomnum, (void)hit;
    return FALSE;
}
s32 getTileRoom(StandTile *tile) { (void)tile; return -1; }
f32 get_room_data_float1(void) { return 1.f; }
int gevrBeanIsEnabled(void) { return 0; }
int gevrBeanWorldShotSurface(const float from[3], const float dir[3], float near, float far, float hit[3],
    float normal[3])
{
    (void)from, (void)dir, (void)near, (void)far, (void)hit, (void)normal;
    return 0;
}
void gevrRagdollBloodStain(int handle, int part, const float hit[3], const float direction[3])
{
    (void)handle, (void)part, (void)hit, (void)direction;
}
int gevrTextureCount(void) { return 0; }
int pdphysIsReady(void) { return 0; }
int pdphysRaycastAnyRoom(const float from[3], const float to[3], float outhit[3], float outnormal[3], int *outroom)
{
    (void)from, (void)to, (void)outhit, (void)outnormal, (void)outroom;
    return 0;
}
int pdphysRaycastRagdoll(const float from[3], const float to[3], float radius, float outhit[3], int *outhandle,
    int *outpart)
{
    (void)from, (void)to, (void)radius, (void)outhit, (void)outhandle, (void)outpart;
    return 0;
}
StandTile *stanFindTileBelowPos(coord3d *pos, u8 *rooms, f32 *yRtn)
{
    (void)pos, (void)rooms, (void)yRtn;
    return NULL;
}

static int failures;
static void expect(int ok, const char *what)
{
    if (!ok) {
        printf("FAIL %s\n", what);
        ++failures;
    }
}

static const f32 s_point[3] = {0, 0, -500}, s_shot[3] = {0, 0, -1};
static Gfx s_list[8192];

/* How many commands a frame's draw adds */
static long drawn(void)
{
    s_matrixCount = s_vertexCount = 0;
    return (long)(gevrModBloodDraw(s_list) - s_list);
}

static GevrModBloodConfig config(void)
{
    GevrModBloodConfig c = {.headBurst = 10};
    c.bodies[2] = 1;
    return c;
}

static void refused(void)
{
    const GevrModBloodConfig c = config();
    expect(!gevrModGameBlood(&c), "a bad sheet refuses the call");
    expect(strstr(s_lastLog, "missing or invalid blood atlas") != NULL, "the log names the bad sheet");
    expect(!gevrModBloodEnabled(), "nothing is enabled");
    expect(!gevrModBloodImpact(0, s_point, s_shot), "an impact keeps the stock hit puffs");
    expect(!gevrModBloodHeadChance(2), "no head bursts");
    expect(drawn() == 0, "nothing draws");
    for (int i = 0; i < FX_ATLASES; ++i) expect(s_atlases[i] == NULL, "no sheet is kept");
    expect(!gevrModGameBlood(&c), "a later call is refused too");
}

static void loaded(void)
{
    const GevrModBloodConfig c = config();
    expect(gevrModGameBlood(&c), "seven good sheets load");
    expect(gevrModBloodEnabled(), "enabled");
    expect(gevrModBloodHeadChance(2) == 10 && gevrModBloodHeadChance(3) == 0, "head bursts for the named bodies");

    VrBloodPatch = 0;
    expect(!gevrModBloodEnabled() && !gevrModBloodImpact(0, s_point, s_shot),
           "Blood patch off: the impact keeps the stock hit puffs");
    expect(!gevrModBloodHeadChance(2), "Blood patch off: no head bursts");
    VrBloodPatch = 1;
    s_net = 1;
    expect(!gevrModBloodEnabled() && !gevrModBloodImpact(0, s_point, s_shot),
           "co-op: the impact keeps the stock hit puffs");
    s_net = 0;
    expect(drawn() == 0, "refused impacts left nothing to draw");

    expect(gevrModBloodImpact(0, s_point, s_shot), "an impact takes the hit");
    expect(drawn() > 0, "the impact draws");
    expect(gevrModGameBlood(NULL), "blood(false) answers true");
    expect(!gevrModBloodEnabled() && !gevrModBloodImpact(0, s_point, s_shot),
           "after blood(false) the stock hit puffs are back");
    expect(drawn() == 0, "blood(false) clears what was on screen");
    expect(gevrModGameBlood(&c), "a later call enables again from the kept sheets");
    expect(drawn() == 0, "and nothing from before comes back");
}

int main(int argc, char **argv)
{
    if (argc != 3) return 2;
    s_folder = argv[1];
    if (!strcmp(argv[2], "ok")) loaded();
    else if (!strcmp(argv[2], "refused")) refused();
    else return 2;
    if (failures) return EXIT_FAILURE;
    printf("modblood %s: ok\n", argv[2]);
    return EXIT_SUCCESS;
}
