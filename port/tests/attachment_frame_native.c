#include <assert.h>
#include "../../src/game/gevr_modattach.c"

static struct player player;
struct player *g_CurrentPlayer = &player;
OSSched os_scheduler;
static ITEM_IDS held = ITEM_WPPKSIL;
static const GevrModMesh *gunMesh;
static int room = 1;
static Gfx list[16];
static Mtxf nodes[2];
static ModelFileHeader header;
static AttachFile file;

bool netIsActive(void) { return 0; }
int netGetLocalSlot(void) { return 0; }
s32 get_cur_playernum(void) { return 0; }
ITEM_IDS get_item_in_hand_or_watch_menu(GUNHAND hand) { return held; }
const GevrModMesh *gevrModGunMesh(s32 hand) { return gunMesh; }
Gfx *gevrModMeshDraw(Gfx *gdl, const Mtxf *pose, const GevrModMesh *mesh) { return gdl + room; }
void matrix_4x4_multiply(Mtxf *lhs, Mtxf *rhs, Mtxf *out)
{
    for (int i = 0; i < 4; i++) for (int j = 0; j < 4; j++) {
        out->m[i][j] = 0;
        for (int k = 0; k < 4; k++) out->m[i][j] += rhs->m[i][k] * lhs->m[k][j];
    }
}
/* This fixture uses the pack branch. Cartridge rendering needs the game. */
s32 gevrDynRoom(Gfx *gdl, s32 gfx, s32 bytes) { abort(); }
void sysLogPrintf(s32 level, const char *fmt, ...) { abort(); }
void *dynAllocate(s32 bytes) { abort(); }
void modelInit(Model *m, ModelFileHeader *h, u32 *rw) { abort(); }
void instcalcmatrices(ModelRenderData *rd, Model *m) { abort(); }
void modelUpdateRelationsQuick(Model *m, ModelNode *n) { abort(); }
void subdraw(ModelRenderData *rd, Model *m) { abort(); }
void bondviewTransformManyPosToViewMatrix(RenderPosView *p, s32 n) { abort(); }

int main(void)
{
    Model *gun = &player.hands[GUNRIGHT].weaponModel;
    ModelRenderData rd = {0};
    Attachment *part = &s_attach[0];
    Mtxf pose;
    s32 mirrored;
    static const Mtxf identity = {.m = {{1,0,0,0}, {0,1,0,0}, {0,0,1,0}, {0,0,0,1}}};
    header.numMatrices = 2;
    gun->obj = &header;
    gun->render_pos = (RenderPosView *)nodes;
    nodes[1] = identity;
    nodes[1].m[0][0] = -1;
    nodes[1].m[3][0] = 8;
    file.mesh = (const GevrModMesh *)&file;
    *part = (Attachment){.id = 33, .file = &file, .shown = TRUE,
                         .on = GEVR_MODATTACH_GUN, .where = GUNRIGHT, .node = 1, .offset = identity};
    part->offset.m[3][0] = 3;
    assert(!gevrModAttachGunFrame(33, NULL, NULL));
    gevrModAttachBeginGunDraw();
    assert(gevrModAttachDrawGun(list, &rd, GUNRIGHT, gun) == list + 1);
    assert(gevrModAttachGunFrame(33, &pose, &mirrored) && mirrored && pose.m[3][0] == 5);
    assert(!gevrModAttachGunFrame(34, NULL, NULL));
    header.numMatrices = 1;
    assert(!gevrModAttachGunFrame(33, NULL, NULL));
    header.numMatrices = 2;
    held = ITEM_AK47;
    assert(!gevrModAttachGunFrame(33, NULL, NULL));
    held = ITEM_WPPKSIL;
    gunMesh = file.mesh;
    assert(!gevrModAttachGunFrame(33, NULL, NULL));
    gunMesh = NULL;
    os_scheduler.frameCount++;
    assert(!gevrModAttachGunFrame(33, NULL, NULL));
    gevrModAttachBeginGunDraw();
    header.numMatrices = 1;
    assert(gevrModAttachDrawGun(list, &rd, GUNRIGHT, gun) == list);
    assert(!gevrModAttachGunFrame(33, NULL, NULL));
    header.numMatrices = 2;
    room = 0;
    assert(gevrModAttachDrawGun(list, &rd, GUNRIGHT, gun) == list);
    assert(!gevrModAttachGunFrame(33, NULL, NULL));
    room = 1;
    nodes[1].m[0][0] = 1;
    assert(gevrModAttachDrawGun(list, &rd, GUNRIGHT, gun) == list + 1);
    assert(gevrModAttachGunFrame(33, &pose, &mirrored) && !mirrored && pose.m[3][0] == 11);
    assert(gevrModGameAttachShow(33, FALSE) && !gevrModAttachGunFrame(33, NULL, NULL));
    assert(gevrModGameAttachShow(33, TRUE) && !gevrModAttachGunFrame(33, NULL, NULL));
    assert(gevrModAttachDrawGun(list, &rd, GUNRIGHT, gun) == list + 1);
    assert(gevrModGameDetach(33) && !gevrModAttachGunFrame(33, NULL, NULL));
    return 0;
}

/* Isolate the actual draw/lookup logic from asset and world rendering. */
GunModelFileRecord gitem_structs[ITEM_IDS_MAX];
struct ItemModelFileRecord *PitemZ_entries;
s32 g_GevrPropCount, g_gevrExtraPass, g_gevrHandPatchSkip, g_gevrStereo;
const char *gevrModAssetPendingName;
int gevrModAssetPendingUsed;
ChrRecord *gevrModGameGuardChr(int id) { abort(); }
int gevrModMeshLoad(const char *model, const GevrModMesh **out) { abort(); }
void texInitPool(struct texpool *pool, u8 *buf, s32 size) { abort(); }
void load_object_fill_header(ModelFileHeader *h, u8 *name, u8 *dst, s32 size, struct texpool *pool) { abort(); }
void modelCalculateRwDataLen(ModelFileHeader *h) { abort(); }
s32 modelFindNodeMtxIndex(ModelNode *node, s32 index) { abort(); }
void gevrModApiAttachGone(int id) { abort(); }
s32 gevrModAttachHandFrame(s32 hand, Mtxf *root) { abort(); }
s32 gevrBodySlotFrame(s32 slot, Mtxf *root) { abort(); }
u32 matrix_4x4_calc_depth_scale(f32 a, f32 b) { abort(); }
void matrix_4x4_7F058C64(void) { abort(); }
void matrix_4x4_7F058C88(void) { abort(); }
u16 viGetPerspNorm(void) { abort(); }
