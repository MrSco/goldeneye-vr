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

static int online, logged;
static Mtxf meshPose;
bool netIsActive(void) { return online; }
int netGetLocalSlot(void) { return 0; }
s32 get_cur_playernum(void) { return 0; }
ITEM_IDS get_item_in_hand_or_watch_menu(GUNHAND hand) { return held; }
const GevrModMesh *gevrModGunMesh(s32 hand) { return gunMesh; }
void gevrModOpticCheckCarrier(int id, s32 hand) {}
void gevrModOpticDrawn(int id, s32 hand, const Mtxf *pose) {}
Gfx *gevrModOpticEmit(Gfx *gdl, s32 hand) { return gdl; }
int gevrModOpticFolded(int id) { return 0; }
void gevrModOpticUnbind(int id) {}
void gevrModOpticReset(void) {}
Gfx *gevrModMeshDraw(Gfx *gdl, const Mtxf *pose, const GevrModMesh *mesh) { meshPose = *pose; return gdl + room; }
/* another player's (#335): its copy is player 2 */
s32 getPlayerPointerIndex(PropRecord *prop) { return 2; }
f32 D_80032310[2] = { 65536.0f, 65536.0f };
void matrix_4x4_multiply(Mtxf *lhs, Mtxf *rhs, Mtxf *out)
{
    for (int i = 0; i < 4; i++) for (int j = 0; j < 4; j++) {
        out->m[i][j] = 0;
        for (int k = 0; k < 4; k++) out->m[i][j] += rhs->m[i][k] * lhs->m[k][j];
    }
}
/* This fixture uses the pack branch. Cartridge rendering needs the game. */
s32 gevrDynRoom(Gfx *gdl, s32 gfx, s32 bytes) { abort(); }
void sysLogPrintf(s32 level, const char *fmt, ...) { logged++; }
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
    /* The next frame's input tick runs before BeginGunDraw. It sees the last
     * actual draw, but must not use a pose after a second tick without a draw. */
    os_scheduler.frameCount++;
    assert(gevrModAttachGunFrame(33, &pose, &mirrored) && mirrored && pose.m[3][0] == 5);
    os_scheduler.frameCount++;
    assert(!gevrModAttachGunFrame(33, NULL, NULL));

    gevrModAttachBeginGunDraw();
    assert(gevrModAttachDrawGun(list, &rd, GUNRIGHT, gun) == list + 1);
    os_scheduler.frameCount++;
    assert(gevrModAttachGunFrame(33, NULL, NULL));
    gevrModAttachBeginGunDraw();
    assert(!gevrModAttachGunFrame(33, NULL, NULL));  /* even before the skipped part below */
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
    assert(!logged);

    /* Another player's (#335): kept apart from this player's own, drawn on
     * that player's held gun in the body's solid pass alone, at the level's
     * scale; this player's own always find a place. */
    {
        static PropRecord prop;
        static ChrRecord chr;
        static const float body[3] = { 65, 20, -10 };
        GevrModAttach a = { .on = GEVR_MODATTACH_GUN, .where = GUNRIGHT, .node = 1 };
        s32 i, n, id;

        prop.type = PROP_TYPE_VIEWER;
        chr.prop = &prop;
        strcpy(s_files[0].model, "part.bin");
        s_files[0].ok = TRUE;
        s_files[0].mesh = (const GevrModMesh *)&file;
        strcpy(a.model, "part.bin");
        for (i = 0; i < 4; i++) a.offset[i][i] = 1;
        a.offset[3][0] = 3;
        *part = (Attachment){.id = 33, .file = &s_files[0], .shown = TRUE,
                             .on = GEVR_MODATTACH_GUN, .where = GUNRIGHT, .node = 1, .offset = identity};
        online = 1;
        assert(gevrModGameAttachRemote(2, 7, &a, TRUE, body) && s_attach[1].id < 0 && s_attach[1].owner == 2);
        assert(!gevrModGameAttachRemote(0, 7, &a, TRUE, body));   /* this player's own slot: never another's */
        a.on = GEVR_MODATTACH_GUARD;
        assert(!gevrModGameAttachRemote(2, 8, &a, TRUE, body));   /* never a guard's */
        a.on = GEVR_MODATTACH_GUN;
        /* this player's own gun draw: only its own (one draw) */
        gevrModAttachBeginGunDraw();
        assert(gevrModAttachDrawGun(list, &rd, GUNRIGHT, gun) == list + 1);
        assert(!gevrModAttachGunFrame(s_attach[1].id, NULL, NULL));
        /* the copy's held gun: once, in the solid pass, at the level's scale */
        D_80032310[0] = 65536.0f * 0.5f;
        rd.flags = 2;
        assert(gevrModAttachDrawPlayerGun(list, &rd, &chr, GUNRIGHT, gun) == list);
        rd.flags = 1;
        assert(gevrModAttachDrawPlayerGun(list, &rd, &chr, GUNLEFT, gun) == list);
        assert(gevrModAttachDrawPlayerGun(list, &rd, &chr, GUNRIGHT, gun) == list + 1);
        assert(meshPose.m[3][0] == (8 + 3) * 0.5f && meshPose.m[0][0] == 0.5f && meshPose.m[3][3] == 1);
        /* a flip, a move */
        assert(gevrModGameAttachRemote(2, 7, &a, FALSE, body) && s_attach[1].id < 0 && !s_attach[1].shown);
        assert(gevrModAttachDrawPlayerGun(list, &rd, &chr, GUNRIGHT, gun) == list);
        a.offset[3][0] = 5;
        assert(gevrModGameAttachRemote(2, 7, &a, TRUE, body) && s_attach[2].id == 0);   /* the same one, changed */
        assert(gevrModAttachDrawPlayerGun(list, &rd, &chr, GUNRIGHT, gun) == list + 1 && meshPose.m[3][0] == (8 + 5) * 0.5f);
        /* not online, or an extra pass: none */
        online = 0;
        assert(gevrModAttachDrawPlayerGun(list, &rd, &chr, GUNRIGHT, gun) == list);
        online = 1;
        g_gevrExtraPass = 1;
        assert(gevrModAttachDrawPlayerGun(list, &rd, &chr, GUNRIGHT, gun) == list);
        g_gevrExtraPass = 0;
        /* removed: gone */
        gevrModGameAttachRemoteGone(2, 7);
        assert(s_attach[1].id == 0 && gevrModAttachDrawPlayerGun(list, &rd, &chr, GUNRIGHT, gun) == list);
        /* the table full of another player's: this player's own still finds a place */
        for (n = 0; gevrModGameAttachRemote(2, 100 + n, &a, TRUE, body); n++) {
        }
        assert(n == GEVR_MODAPI_ATTACH_MAX - 1 && logged == 1);   /* 127 beside the one own; refused past, logged once */
        id = gevrModGameAttach(&a);
        assert(id > 0 && logged == 1);
        for (i = n = 0; i < GEVR_MODAPI_ATTACH_MAX; i++) n += s_attach[i].id < 0;
        assert(n == GEVR_MODAPI_ATTACH_MAX - 2);
        gevrModGameAttachRemoteGone(2, 0);
        for (i = n = 0; i < GEVR_MODAPI_ATTACH_MAX; i++) n += s_attach[i].id != 0;
        assert(n == 2);   /* this player's own two kept */
        online = 0;
    }
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
s32 gevrBodySlotAt(s32 slot, f32 out[3]) { abort(); }
f32 gevrGunSizeNow(s32 *setting, s32 *online) { abort(); }
s32 gevrAvatarAttachFrame(s32 slot, s32 on, s32 where, const f32 bodyAt[3], f32 unit, Mtxf *out) { abort(); }
