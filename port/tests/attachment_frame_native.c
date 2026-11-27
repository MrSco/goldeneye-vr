#include <assert.h>
#include <math.h>
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
f32 gevrModGunActionShare(s32 hand, s32 handle) { (void)hand; (void)handle; return 0.0f; }
f32 gevrRackPullShare(s32 hand) { (void)hand; return 0.0f; }
void gevrModOpticCheckCarrier(int id, s32 hand) {}
void gevrModOpticDrawn(int id, s32 hand, const Mtxf *pose) {}
Gfx *gevrModOpticEmit(Gfx *gdl, s32 hand) { return gdl; }
int gevrModOpticFolded(int id) { return 0; }
static int opticBound, opticMode, opticFolded;
int gevrModOpticState(int id, int *mode, int *folded) { *mode = opticMode; *folded = opticFolded; return opticBound; }
void gevrModOpticUnbind(int id) {}
void gevrModOpticReset(void) {}
static const GevrModMesh *meshDrawn;
static int meshLoads;
static int meshFile;   /* what a new pack mesh file loads as */
Gfx *gevrModMeshDraw(Gfx *gdl, const Mtxf *pose, const GevrModMesh *mesh) { meshPose = *pose; meshDrawn = mesh; return gdl + room; }
/* gevr_modmesh.c's split body (#353): what it was asked to draw, and whether a split builds */
static int splitOk = 1, splits, partsDraws;
static unsigned partsPictures[GEVR_MODPART_GROUPS];
static f32 partsOffset[GEVR_MODPART_GROUPS][3];
int gevrModMeshSplit(const GevrModMesh *mesh, const unsigned pictures[GEVR_MODPART_GROUPS]) { splits++; return splitOk; }
Gfx *gevrModMeshDrawParts(Gfx *gdl, const Mtxf *pose, const GevrModMesh *mesh,
                          const unsigned pictures[GEVR_MODPART_GROUPS], const f32 offset[GEVR_MODPART_GROUPS][3])
{
    meshPose = *pose;
    meshDrawn = mesh;
    memcpy(partsPictures, pictures, sizeof(partsPictures));
    memcpy(partsOffset, offset, sizeof(partsOffset));
    partsDraws++;
    return gdl + room;
}
void gevrModMeshPoint(const Mtxf *fit, const float point[3], float out[3])
{
    for (int axis = 0; axis < 3; ++axis)
        out[axis] = fit == NULL ? point[axis] : fit->m[3][axis] + fit->m[0][axis] * point[0]
            + fit->m[1][axis] * point[1] + fit->m[2][axis] * point[2];
}
/* the copy's hand lists and flash (model.c), as a pack body draws them */
static int handDraws, flashDraws;
static const char *handFile;
void modelDrawGunHand(ModelRenderData *rd, Model *m, const char *file) { handDraws++; handFile = file; }
void modelDrawGunFlash(ModelRenderData *rd, Model *m) { flashDraws++; }
/* another player's (#335): its copy is player 2 */
static s32 copySlot = 2;
s32 getPlayerPointerIndex(PropRecord *prop) { return copySlot; }
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

/* a hand's or the body's frame (gevr_avatar.c): the tracked pose's when that
 * player has one (VR), else the body model's as drawn; each marks its own
 * frame (m[3][1]: 100 tracked, 200 the model) and records what it was asked */
static int trackedSlot = -1, trackedOn = 0x6, modelFrames = 1, trackedAsks, modelAsks, ownFit = 1;
static ChrRecord *modelChr;
static f32 askedAt[3], askedUnit;
s32 gevrBodySlotAt(s32 slot, f32 out[3])
{
    out[0] = 30 + slot; out[1] = 12; out[2] = -4;   /* this player's own fit */
    return ownFit;
}
f32 gevrGunSizeNow(s32 *setting, s32 *online) { return 2.0f; }
static void markFrame(Mtxf *out, f32 tag, s32 on, s32 where, const f32 bodyAt[3], f32 unit)
{
    memset(out, 0, sizeof(*out));
    for (int i = 0; i < 4; i++) out->m[i][i] = 1;
    out->m[3][0] = (f32)(on * 10 + where);
    out->m[3][1] = tag;
    for (int k = 0; k < 3; k++) askedAt[k] = bodyAt != NULL ? bodyAt[k] : -1;
    askedUnit = unit;
}
s32 gevrAvatarAttachFrame(s32 slot, s32 on, s32 where, const f32 bodyAt[3], f32 unit, Mtxf *out)
{
    trackedAsks++;
    if (slot != trackedSlot || !(trackedOn >> on & 1)) return 0;
    markFrame(out, 100, on, where, bodyAt, unit);
    return 1;
}
s32 gevrAvatarModelAttachFrame(ChrRecord *chr, s32 on, s32 where, const f32 bodyAt[3], f32 unit, Mtxf *out)
{
    modelAsks++;
    modelChr = chr;
    if (!modelFrames) return 0;
    markFrame(out, 200, on, where, bodyAt, unit);
    return 1;
}
/* the flat third person (gevr_thirdperson.c): this player's own body in its own view */
static int thirdPerson;
s32 gevrTpDrawsOwnBody(s32 playernum) { return thirdPerson && playernum == 0; }

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

        /* an optic's housing on another player's pack mesh (mod.attach.optic):
         * the folded housing drawn while its told mode folds it; none of the lens */
        {
            static int foldedMesh;
            Attachment *r = NULL;

            strcpy(s_files[1].model, "part-folded.bin");
            s_files[1].ok = TRUE;
            s_files[1].mesh = (const GevrModMesh *)&foldedMesh;
            a.offset[3][0] = 3;
            assert(gevrModGameAttachRemote(2, 9, &a, TRUE, body));
            for (i = 0; i < GEVR_MODAPI_ATTACH_MAX; i++)
                if (s_attach[i].id < 0 && s_attach[i].owner == 2 && s_attach[i].ownerId == 9) r = &s_attach[i];
            assert(r != NULL && r->folded == NULL);
            assert(gevrModGameAttachRemoteOptic(2, 9, "part-folded.bin", 1, 0) && r->folded == &s_files[1]
                   && !r->remoteFolded && r->remoteMode == 1);
            rd.flags = 1;
            assert(gevrModAttachDrawPlayerGun(list, &rd, &chr, GUNRIGHT, gun) == list + 1 && meshDrawn == file.mesh);
            assert(gevrModGameAttachRemoteOptic(2, 9, "part-folded.bin", 2, 1) && r->remoteFolded);   /* a flip */
            assert(gevrModAttachDrawPlayerGun(list, &rd, &chr, GUNRIGHT, gun) == list + 1
                   && meshDrawn == (const GevrModMesh *)&foldedMesh);
            assert(gevrModGameAttachRemoteOptic(2, 9, NULL, 0, 0) && r->folded == NULL && !r->remoteFolded);   /* unbound */
            assert(gevrModAttachDrawPlayerGun(list, &rd, &chr, GUNRIGHT, gun) == list + 1 && meshDrawn == file.mesh);
            /* a housing that isn't a pack mesh, or a part that isn't one on the gun: refused */
            strcpy(s_files[2].model, "box.bin");
            s_files[2].ok = TRUE;
            s_files[2].mesh = NULL;
            assert(!gevrModGameAttachRemoteOptic(2, 9, "box.bin", 0, 1) && r->folded == NULL && !r->remoteFolded);
            assert(!gevrModGameAttachRemoteOptic(2, 99, "part-folded.bin", 0, 1));   /* no such */
            a.on = GEVR_MODATTACH_HAND;
            assert(gevrModGameAttachRemote(2, 10, &a, TRUE, body) && !gevrModGameAttachRemoteOptic(2, 10, "part-folded.bin", 0, 1));
            a.on = GEVR_MODATTACH_GUN;
            /* this player's own: its optic as the others are told it */
            {
                char housing[64];
                int mode, folded;

                part->folded = &s_files[1];
                opticBound = 0;
                assert(!gevrModGameAttachOpticState(33, &mode, &folded, housing, sizeof(housing)));
                opticBound = 1;
                opticMode = 3;
                opticFolded = 1;
                assert(gevrModGameAttachOpticState(33, &mode, &folded, housing, sizeof(housing)) && mode == 3 && folded
                       && !strcmp(housing, "part-folded.bin"));
                assert(!gevrModGameAttachOpticState(r->id, &mode, &folded, housing, sizeof(housing)));   /* not its own */
                part->folded = NULL;
                opticBound = 0;
            }
            gevrModGameAttachRemoteGone(2, 0);
        }

        /* another player's pack gun body (mod.weapon.model): drawn on its copy's
         * held gun in place of the item's model, the hand's lists and the flash
         * as its own hand draws them; gone with a reset, a swap, its player */
        {
            static int bodyMesh;
            static const float fit[4][4] = {{2,0,0,0}, {0,2,0,0}, {0,0,2,0}, {10,0,0,1}};
            static const float muzzle[3] = {1, 0, 0};
            Model copy = {0};
            s32 replaced;
            f32 at[3];
            Gfx *end;

            gitem_structs[ITEM_WPPKSIL].item_file_name = "GwppksilZ";
            nodes[0] = identity;
            nodes[0].m[3][0] = 2;
            copy.obj = &header;
            copy.render_pos = (RenderPosView *)nodes;
            strcpy(s_files[3].model, "body.bin");
            s_files[3].ok = TRUE;
            s_files[3].mesh = (const GevrModMesh *)&bodyMesh;
            /* none set: the item's own */
            assert(gevrModAttachDrawPlayerBody(list, &rd, &chr, GUNRIGHT, ITEM_WPPKSIL, &copy, &replaced) == list && !replaced);
            assert(gevrModGameAttachRemoteBody(2, GUNRIGHT, ITEM_WPPKSIL, "body.bin", fit, muzzle, NULL));
            handDraws = flashDraws = 0;
            rd.flags = 1;   /* the solid pass: the mesh on the root and its fit, at the level's scale */
            end = gevrModAttachDrawPlayerBody(list, &rd, &chr, GUNRIGHT, ITEM_WPPKSIL, &copy, &replaced);
            assert(replaced && end == list + 1 && meshDrawn == (const GevrModMesh *)&bodyMesh);
            assert(meshPose.m[3][0] == (2 + 10) * 0.5f && meshPose.m[0][0] == 2 * 0.5f && meshPose.m[3][3] == 1);
            assert(handDraws == 1 && !strcmp(handFile, "GwppksilZ") && flashDraws == 1);
            rd.flags = 2;   /* the see-through pass: the hand and flash again, the mesh not */
            end = gevrModAttachDrawPlayerBody(list, &rd, &chr, GUNRIGHT, ITEM_WPPKSIL, &copy, &replaced);
            assert(replaced && end == list && handDraws == 2 && flashDraws == 2);
            rd.flags = 1;
            assert(gevrModAttachPlayerMuzzle(&chr, GUNRIGHT, ITEM_WPPKSIL, at) && at[0] == 12 && at[1] == 0 && at[2] == 0);
            /* the copy holds another gun (a swap not yet told), or the other hand: the item's own */
            assert(gevrModAttachDrawPlayerBody(list, &rd, &chr, GUNRIGHT, ITEM_AK47, &copy, &replaced) == list && !replaced);
            assert(!gevrModAttachPlayerMuzzle(&chr, GUNRIGHT, ITEM_AK47, at));
            assert(gevrModAttachDrawPlayerBody(list, &rd, &chr, GUNLEFT, ITEM_WPPKSIL, &copy, &replaced) == list && !replaced);
            /* no fit and no muzzle: on the root, no flash */
            assert(gevrModGameAttachRemoteBody(2, GUNRIGHT, ITEM_WPPKSIL, "body.bin", NULL, NULL, NULL));
            end = gevrModAttachDrawPlayerBody(list, &rd, &chr, GUNRIGHT, ITEM_WPPKSIL, &copy, &replaced);
            assert(replaced && end == list + 1 && meshPose.m[3][0] == 2 * 0.5f && meshPose.m[0][0] == 0.5f && flashDraws == 2);
            assert(!gevrModAttachPlayerMuzzle(&chr, GUNRIGHT, ITEM_WPPKSIL, at));
            /* reset to the hand's own */
            assert(gevrModGameAttachRemoteBody(2, GUNRIGHT, 0, NULL, NULL, NULL, NULL));
            assert(gevrModAttachDrawPlayerBody(list, &rd, &chr, GUNRIGHT, ITEM_WPPKSIL, &copy, &replaced) == list && !replaced);
            /* a file that isn't a pack mesh: refused, and the old body gone with it */
            assert(gevrModGameAttachRemoteBody(2, GUNRIGHT, ITEM_WPPKSIL, "body.bin", fit, muzzle, NULL));
            assert(!gevrModGameAttachRemoteBody(2, GUNRIGHT, ITEM_WPPKSIL, "box.bin", fit, muzzle, NULL));
            gevrModAttachDrawPlayerBody(list, &rd, &chr, GUNRIGHT, ITEM_WPPKSIL, &copy, &replaced);
            assert(!replaced);
            /* never this player's own slot, nor a hand or item out of range */
            assert(!gevrModGameAttachRemoteBody(0, GUNRIGHT, ITEM_WPPKSIL, "body.bin", fit, muzzle, NULL));
            assert(!gevrModGameAttachRemoteBody(2, 2, ITEM_WPPKSIL, "body.bin", fit, muzzle, NULL));
            assert(!gevrModGameAttachRemoteBody(2, GUNRIGHT, ITEM_IDS_MAX, "body.bin", fit, muzzle, NULL));
            /* not online, an extra pass: the item's own */
            assert(gevrModGameAttachRemoteBody(2, GUNRIGHT, ITEM_WPPKSIL, "body.bin", fit, muzzle, NULL));
            online = 0;
            gevrModAttachDrawPlayerBody(list, &rd, &chr, GUNRIGHT, ITEM_WPPKSIL, &copy, &replaced);
            assert(!replaced);
            online = 1;
            g_gevrExtraPass = 1;
            gevrModAttachDrawPlayerBody(list, &rd, &chr, GUNRIGHT, ITEM_WPPKSIL, &copy, &replaced);
            assert(!replaced);
            g_gevrExtraPass = 0;
            /* its player gone: its bodies with it */
            gevrModGameAttachRemoteGone(2, 0);
            gevrModAttachDrawPlayerBody(list, &rd, &chr, GUNRIGHT, ITEM_WPPKSIL, &copy, &replaced);
            assert(!replaced);

            /* the shared files: another player's body loads a new file only
             * while ATT_REMOTE_FILES_FREE stay free; this player's own still loads */
            for (i = 4; i < GEVR_MODAPI_ATTACH_MODELS - ATT_REMOTE_FILES_FREE; i++) {
                snprintf(s_files[i].model, sizeof(s_files[i].model), "f%d.bin", i);
                s_files[i].ok = TRUE;
                s_files[i].mesh = (const GevrModMesh *)&meshFile;
            }
            meshLoads = 0;
            logged = 0;
            assert(!gevrModGameAttachRemoteBody(2, GUNRIGHT, ITEM_WPPKSIL, "new.bin", fit, muzzle, NULL) && meshLoads == 0);
            assert(gevrModGameAttachRemoteBody(2, GUNRIGHT, ITEM_WPPKSIL, "f4.bin", fit, muzzle, NULL));   /* one loaded already */
            strcpy(a.model, "own.bin");
            assert(gevrModGameAttach(&a) > 0 && meshLoads == 1);
            /* a mission's reset: none kept */
            gevrModGameAttachReset();
            gevrModAttachDrawPlayerBody(list, &rd, &chr, GUNRIGHT, ITEM_WPPKSIL, &copy, &replaced);
            assert(!replaced);
        }

        /* another player's moving parts (#353): its pack body's groups and its
         * gun parts' actions follow the shots its copy fires here, tick by
         * tick on gevr_modaction.h's rule with the owner's timing and the
         * fast-gun cut, never held open; drawn at their offsets as the
         * owner's are; gone with a swap, its player, a reset */
        {
            static int bodyMesh;
            static const GevrModGunParts parts = { { 6, 1 }, { { 0, 0, -44 }, { 0, 3, -55 } }, TRUE, { 1, 5, 0.4f } };
            static const float travel[3] = { 0, 0, -10 };
            const GevrModActionTiming own = { 2, 6, 1.0f };
            GevrModAction ref = { 0 }, refPart = { 0 };
            Model copy = {0};
            s32 replaced, k;
            Attachment *bolt = NULL, *follow = NULL;
            int open, close;

#define NEAR(a, b) (fabsf((a) - (b)) < 1e-5f)
            nodes[0] = identity;
            copy.obj = &header;
            copy.render_pos = (RenderPosView *)nodes;
            strcpy(s_files[3].model, "body.bin");
            s_files[3].ok = TRUE;
            s_files[3].mesh = (const GevrModMesh *)&bodyMesh;
            strcpy(s_files[0].model, "part.bin");
            s_files[0].ok = TRUE;
            s_files[0].mesh = (const GevrModMesh *)&file;
            D_80032310[0] = 65536.0f;
            rd.flags = 1;
            /* the parts with the body, split as the owner's; drawn through the split draw at rest */
            splits = partsDraws = 0;
            assert(gevrModGameAttachRemoteBody(2, GUNRIGHT, ITEM_WPPKSIL, "body.bin", NULL, NULL, &parts) && splits == 1);
            assert(gevrModAttachDrawPlayerBody(list, &rd, &chr, GUNRIGHT, ITEM_WPPKSIL, &copy, &replaced) == list + 1
                   && replaced && partsDraws == 1 && partsPictures[0] == 6 && partsPictures[1] == 1
                   && partsOffset[0][2] == 0.0f && partsOffset[1][1] == 0.0f);
            /* a gun part following the hand's action, and one with its own timing */
            a.on = GEVR_MODATTACH_GUN;
            a.where = GUNRIGHT;
            a.node = 0;
            strcpy(a.model, "part.bin");
            a.like[0] = '\0';
            memset(a.offset, 0, sizeof(a.offset));
            for (i = 0; i < 4; i++) a.offset[i][i] = 1;
            assert(gevrModGameAttachRemote(2, 50, &a, TRUE, body) && gevrModGameAttachRemote(2, 51, &a, TRUE, body));
            assert(gevrModGameAttachRemoteAction(2, 50, travel, FALSE, NULL));
            assert(gevrModGameAttachRemoteAction(2, 51, travel, FALSE, &own));
            for (i = 0; i < GEVR_MODAPI_ATTACH_MAX; i++)
                if (s_attach[i].id < 0 && s_attach[i].owner == 2) {
                    if (s_attach[i].ownerId == 51) bolt = &s_attach[i];
                    if (s_attach[i].ownerId == 50) follow = &s_attach[i];
                }
            assert(bolt != NULL && bolt->moves && bolt->timed && bolt->timing.open == 2);
            assert(follow != NULL && follow->moves && !follow->timed && !follow->handle && follow->travel[2] == -10.0f);
            /* tick by tick against the rule itself: shots, a frame of two ticks, the closing */
            {
                static const int shots[] = { 1, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0 };

                for (k = 0; k < (s32)(sizeof(shots) / sizeof(shots[0])); k++) {
                    const f32 ticks = k == 6 ? 2.0f : 1.0f;
                    f32 share, partShare;

                    gevrModAttachRemoteActionTick(2, GUNRIGHT, ITEM_WPPKSIL, shots[k], ticks, 0);
                    if (shots[k]) gevrModActionShot(&ref, ITEM_WPPKSIL);
                    gevrModActionTicks(&parts.timing, 0, &open, &close);
                    gevrModActionStep(&ref, ITEM_WPPKSIL, ticks, 1, FALSE, open, close);
                    if (shots[k]) gevrModActionShot(&refPart, ITEM_WPPKSIL);
                    gevrModActionTicks(&own, 0, &open, &close);
                    gevrModActionStep(&refPart, ITEM_WPPKSIL, ticks, 1, FALSE, open, close);
                    share = gevrModActionShare(&ref, 0.0f, FALSE, &parts.timing);
                    partShare = gevrModActionShare(&refPart, 0.0f, FALSE, &own);
                    assert(gevrModAttachDrawPlayerBody(list, &rd, &chr, GUNRIGHT, ITEM_WPPKSIL, &copy, &replaced) == list + 1);
                    assert(NEAR(partsOffset[0][2], share * -44.0f) && partsOffset[1][1] == 0.0f && partsOffset[1][2] == 0.0f);
                    /* the gun parts: the one along the body's action, the other its own */
                    assert(NEAR(attachRemoteShare(follow), share) && NEAR(attachRemoteShare(bolt), partShare));
                    if (k == 0) assert(NEAR(share, 1.0f) && NEAR(partShare, 0.5f));   /* the shot's own tick: 1 and 2 ticks to open */
                }
                assert(NEAR(gevrModActionShare(&ref, 0.0f, FALSE, &parts.timing), 0.0f));
            }
            /* the owner's body holds open, but a copy never does: the owner's magazine isn't known here */
            gevrModAttachRemoteActionTick(2, GUNRIGHT, ITEM_WPPKSIL, 1, 1.0f, 0);
            for (k = 0; k < 6; k++) gevrModAttachRemoteActionTick(2, GUNRIGHT, ITEM_WPPKSIL, 0, 1.0f, 0);
            gevrModAttachDrawPlayerBody(list, &rd, &chr, GUNRIGHT, ITEM_WPPKSIL, &copy, &replaced);
            assert(partsOffset[0][2] == 0.0f);
            /* an automatic firing every 4 ticks: the close is cut to 3 so a cycle fits, as the owner's */
            gevrModAttachRemoteActionTick(2, GUNRIGHT, ITEM_WPPKSIL, 1, 1.0f, 4);
            for (k = 1; k <= 4; k++) {
                const f32 o = k < 3 ? 1.0f - k / 3.0f : 0.0f;

                gevrModAttachRemoteActionTick(2, GUNRIGHT, ITEM_WPPKSIL, 0, 1.0f, 4);
                gevrModAttachDrawPlayerBody(list, &rd, &chr, GUNRIGHT, ITEM_WPPKSIL, &copy, &replaced);
                assert(NEAR(partsOffset[0][2], o * (0.4f + 0.6f * o) * -44.0f));
            }
            /* the gun part's draw: along its travel in the anchor's frame where its action is */
            gevrModAttachRemoteActionTick(2, GUNRIGHT, ITEM_WPPKSIL, 1, 1.0f, 0);
            assert(gevrModAttachDrawPlayerGun(list, &rd, &chr, GUNRIGHT, gun) == list + 2);
            assert(gevrModGameAttachRemote(2, 50, &a, FALSE, body) && follow->moves);   /* hidden: its action kept */
            assert(gevrModAttachDrawPlayerGun(list, &rd, &chr, GUNRIGHT, gun) == list + 1);
            assert(NEAR(meshPose.m[3][2], 0.5f * -10.0f) && meshPose.m[3][0] == 0.0f);   /* the timed one, half open */
            assert(gevrModGameAttachRemote(2, 50, &a, TRUE, body));
            assert(gevrModGameAttachRemoteAction(2, 51, NULL, FALSE, NULL));
            assert(gevrModAttachDrawPlayerGun(list, &rd, &chr, GUNRIGHT, gun) == list + 2
                   && meshPose.m[3][2] == 0.0f);   /* the last, the other one put still, at its place */
            assert(gevrModGameAttachRemoteAction(2, 51, travel, FALSE, &own));
            /* a handle stays home: the rack's pull isn't known here */
            assert(gevrModGameAttachRemoteAction(2, 51, travel, TRUE, NULL) && !bolt->timed && bolt->handle);
            assert(attachRemoteShare(bolt) == 0.0f);
            /* a change starts it closed; still: no travel; a hand's part or one never told: none */
            assert(gevrModGameAttachRemoteAction(2, 51, travel, FALSE, &own) && bolt->action.open == 0.0f);
            assert(gevrModGameAttachRemoteAction(2, 51, NULL, FALSE, NULL) && !bolt->moves);
            assert(!gevrModGameAttachRemoteAction(2, 99, travel, FALSE, NULL));
            a.on = GEVR_MODATTACH_HAND;
            assert(gevrModGameAttachRemote(2, 52, &a, TRUE, body) && !gevrModGameAttachRemoteAction(2, 52, travel, FALSE, NULL));
            a.on = GEVR_MODATTACH_GUN;
            /* another gun in the copy's hand: closed, and the body's parts not for it */
            gevrModAttachRemoteActionTick(2, GUNRIGHT, ITEM_WPPKSIL, 1, 1.0f, 0);
            gevrModAttachRemoteActionTick(2, GUNRIGHT, ITEM_AK47, 0, 1.0f, 0);
            assert(s_remoteAction[2][GUNRIGHT].item == ITEM_AK47 && s_remoteAction[2][GUNRIGHT].open == 0.0f);
            gevrModAttachDrawPlayerBody(list, &rd, &chr, GUNRIGHT, ITEM_WPPKSIL, &copy, &replaced);
            assert(replaced && partsOffset[0][2] == 0.0f);
            /* the other hand, the other player: apart */
            gevrModAttachRemoteActionTick(2, GUNRIGHT, ITEM_WPPKSIL, 1, 1.0f, 0);
            assert(s_remoteAction[2][GUNLEFT].open == 0.0f && s_remoteAction[3][GUNRIGHT].open == 0.0f);
            /* a new body for the gun starts closed */
            assert(gevrModGameAttachRemoteBody(2, GUNRIGHT, ITEM_AK47, "body.bin", NULL, NULL, &parts)
                   && s_remoteAction[2][GUNRIGHT].open == 0.0f);
            /* pictures the mesh hasn't: the body still drawn, without its parts (the whole mesh) */
            splitOk = 0;
            logged = 0;
            assert(gevrModGameAttachRemoteBody(2, GUNRIGHT, ITEM_WPPKSIL, "body.bin", NULL, NULL, &parts) && logged == 1);
            partsDraws = 0;
            assert(gevrModAttachDrawPlayerBody(list, &rd, &chr, GUNRIGHT, ITEM_WPPKSIL, &copy, &replaced) == list + 1
                   && replaced && partsDraws == 0 && meshDrawn == (const GevrModMesh *)&bodyMesh);
            splitOk = 1;
            /* parts that only hold open: nothing to split, the hand's timing GoldenEye's */
            {
                GevrModGunParts hold = { { 0, 0 }, { { 0 } }, TRUE, { 0, 0, 1.0f } };

                splits = 0;
                assert(gevrModGameAttachRemoteBody(2, GUNRIGHT, ITEM_WPPKSIL, "body.bin", NULL, NULL, &hold) && splits == 0);
                assert(attachRemoteTiming(2, GUNRIGHT, ITEM_WPPKSIL) == &s_remoteBody[2][GUNRIGHT].parts.timing);
                gevrModAttachRemoteActionTick(2, GUNRIGHT, ITEM_WPPKSIL, 1, 1.0f, 0);
                assert(NEAR(s_remoteAction[2][GUNRIGHT].open, 0.25f));   /* a quarter a tick, as GoldenEye's */
            }
            /* its player gone: the actions with it */
            assert(gevrModGameAttachRemoteBody(2, GUNRIGHT, ITEM_WPPKSIL, "body.bin", NULL, NULL, &parts));
            gevrModAttachRemoteActionTick(2, GUNRIGHT, ITEM_WPPKSIL, 1, 1.0f, 0);
            assert(s_remoteAction[2][GUNRIGHT].open == 1.0f);
            gevrModGameAttachRemoteGone(2, 0);
            assert(s_remoteAction[2][GUNRIGHT].open == 0.0f && s_remoteAction[2][GUNRIGHT].item == 0);
            /* a reset (a mission, a mod stopped): none kept, and no tick works on what isn't there */
            strcpy(s_files[3].model, "body.bin");
            assert(gevrModGameAttachRemoteBody(2, GUNRIGHT, ITEM_WPPKSIL, "body.bin", NULL, NULL, &parts));
            gevrModAttachRemoteActionTick(2, GUNRIGHT, ITEM_WPPKSIL, 1, 1.0f, 0);
            gevrModGameAttachReset();
            assert(s_remoteAction[2][GUNRIGHT].open == 0.0f && !s_remoteBody[2][GUNRIGHT].hasParts);
            gevrModAttachRemoteActionTick(2, GUNRIGHT, ITEM_WPPKSIL, 1, 1.0f, 0);
            assert(s_remoteAction[2][GUNRIGHT].open == 0.0f);   /* none of another player's placed: nothing stepped */
            gevrModAttachRemoteActionTick(-1, GUNRIGHT, ITEM_WPPKSIL, 1, 1.0f, 0);
            gevrModAttachRemoteActionTick(MAX_PLAYER_COUNT, 2, ITEM_WPPKSIL, 1, 1.0f, 0);
            D_80032310[0] = 65536.0f * 0.5f;   /* the level's scale the checks below take */
#undef NEAR
        }

        /* a guard's pack mesh (#335 review): in the body's passes, so once, in
         * the solid pass, at the level's scale, as another player's */
        {
            static PropRecord gprop;
            static ChrRecord guard;
            static Model gmodel;
            static ModelFileHeader gheader;
            Attachment *g = &s_attach[5];

            gprop.type = PROP_TYPE_CHR;
            guard.prop = &gprop;
            guard.model = &gmodel;
            gheader.numMatrices = 2;
            gmodel.obj = &gheader;
            gmodel.render_pos = (RenderPosView *)nodes;
            strcpy(s_files[0].model, "patch.bin");
            s_files[0].ok = TRUE;
            s_files[0].mesh = (const GevrModMesh *)&file;
            *g = (Attachment){.id = 77, .file = &s_files[0], .shown = TRUE, .on = GEVR_MODATTACH_GUARD, .node = 3,
                              .chr = &guard, .partOf = &gheader, .partMtx = 1, .offset = identity};
            g->offset.m[3][0] = 4;
            rd.flags = 2;
            assert(gevrModAttachDrawGuard(&guard, &rd, 0xFF, list) == list);
            rd.flags = 1;
            assert(gevrModAttachDrawGuard(&guard, &rd, 0xFF, list) == list + 1);
            assert(meshPose.m[3][0] == (8 + 4) * 0.5f && meshPose.m[0][0] == 0.5f);
            rd.flags = 3;   /* a fading body's one pass */
            assert(gevrModAttachDrawGuard(&guard, &rd, 0x80, list) == list + 1);
            g->id = 0;
        }

        /* hand and body parts on a player's body: another player's at its
         * tracked pose (VR, as before), else at the anchors on its body model
         * as drawn (a flat player, a bot, a hand or head not tracked); this
         * player's own on its own body in the flat third person. Once, in the
         * solid pass, at the level's scale, never while the body fades. */
        {
            static PropRecord pprop;
            static ChrRecord pchr;
            static const float body[3] = { 65, 20, -10 };
            GevrModAttach h = { .on = GEVR_MODATTACH_HAND, .where = GUNLEFT };
            GevrModAttach b = { .on = GEVR_MODATTACH_BODY, .where = 3 };
            s32 hid, bid;

            pprop.type = PROP_TYPE_VIEWER;
            pchr.prop = &pprop;
            strcpy(h.model, "patch.bin");
            strcpy(b.model, "patch.bin");
            for (i = 0; i < 4; i++) h.offset[i][i] = b.offset[i][i] = 1;
            h.offset[3][2] = 7;
            b.offset[3][2] = 9;
            online = 1;
            copySlot = 2;
            assert(gevrModGameAttachRemote(2, 21, &h, TRUE, body) && gevrModGameAttachRemote(2, 22, &b, TRUE, body));
            rd.flags = 1;
            /* a flat player: no tracked pose, so its body model's frames, both parts */
            trackedSlot = -1;
            trackedAsks = modelAsks = 0;
            assert(gevrModAttachDrawPlayer(&pchr, &rd, 0xFF, list) == list + 2);
            assert(trackedAsks == 2 && modelAsks == 2 && modelChr == &pchr);
            /* the last, the body part: the owner's anchor centre, the gun size's unit, its offset, the level's scale */
            assert(askedAt[0] == 65 && askedAt[1] == 20 && askedAt[2] == -10 && askedUnit == 0.085f * 2.0f);
            assert(meshPose.m[3][0] == 23 * 0.5f && meshPose.m[3][1] == 200 * 0.5f && meshPose.m[3][2] == 9 * 0.5f);
            assert(gevrModGameAttachRemote(2, 22, &b, FALSE, body));   /* hidden: the hand alone */
            assert(gevrModAttachDrawPlayer(&pchr, &rd, 0xFF, list) == list + 1);
            assert(meshPose.m[3][0] == 11 * 0.5f && meshPose.m[3][1] == 200 * 0.5f && meshPose.m[3][2] == 7 * 0.5f);
            assert(gevrModGameAttachRemote(2, 22, &b, TRUE, body));
            /* the see-through pass, a fading body, an extra pass: none */
            rd.flags = 2;
            assert(gevrModAttachDrawPlayer(&pchr, &rd, 0xFF, list) == list);
            rd.flags = 1;
            modelAsks = 0;
            assert(gevrModAttachDrawPlayer(&pchr, &rd, 0x80, list) == list && modelAsks == 0);
            g_gevrExtraPass = 1;
            assert(gevrModAttachDrawPlayer(&pchr, &rd, 0xFF, list) == list && modelAsks == 0);
            g_gevrExtraPass = 0;
            /* no joints to build one from: not drawn (never at the origin) */
            modelFrames = 0;
            assert(gevrModAttachDrawPlayer(&pchr, &rd, 0xFF, list) == list);
            modelFrames = 1;
            /* a VR player: its tracked pose, exactly as before; the model never asked */
            trackedSlot = 2;
            trackedAsks = modelAsks = 0;
            assert(gevrModAttachDrawPlayer(&pchr, &rd, 0xFF, list) == list + 2 && trackedAsks == 2 && modelAsks == 0);
            assert(meshPose.m[3][1] == 100 * 0.5f && askedAt[0] == 65);
            /* its hand lost (the head still tracked): that hand from the body as drawn, the body part tracked */
            trackedOn = 1 << GEVR_MODATTACH_BODY;
            trackedAsks = modelAsks = 0;
            assert(gevrModAttachDrawPlayer(&pchr, &rd, 0xFF, list) == list + 2 && trackedAsks == 2 && modelAsks == 1);
            assert(meshPose.m[3][1] == 100 * 0.5f);   /* the body's, drawn last */
            trackedOn = 0x6;
            /* a bot (another slot, no pose): its own parts on its model, never slot 2's */
            copySlot = 3;
            assert(gevrModGameAttachRemote(3, 5, &h, TRUE, body));
            trackedAsks = modelAsks = 0;
            assert(gevrModAttachDrawPlayer(&pchr, &rd, 0xFF, list) == list + 1 && modelAsks == 1);
            assert(meshPose.m[3][0] == 11 * 0.5f && meshPose.m[3][1] == 200 * 0.5f);
            gevrModGameAttachRemoteGone(3, 0);
            assert(gevrModAttachDrawPlayer(&pchr, &rd, 0xFF, list) == list);
            trackedSlot = -1;

            /* this player's own, offline: none on its body in first person; in
             * the flat third person both on the body as drawn, the body part at
             * this player's own fit; never another player's there */
            online = 0;
            copySlot = 0;
            hid = gevrModGameAttach(&h);
            bid = gevrModGameAttach(&b);
            assert(hid > 0 && bid > 0);
            thirdPerson = 0;
            trackedAsks = modelAsks = 0;
            assert(gevrModAttachDrawPlayer(&pchr, &rd, 0xFF, list) == list && modelAsks == 0);
            thirdPerson = 1;
            assert(gevrModAttachDrawPlayer(&pchr, &rd, 0xFF, list) == list + 2);
            assert(trackedAsks == 0 && modelAsks == 2 && modelChr == &pchr);
            assert(askedAt[0] == 33 && askedAt[1] == 12 && askedAt[2] == -4 && askedUnit == 0.085f * 2.0f);
            assert(meshPose.m[3][0] == 23 * 0.5f && meshPose.m[3][1] == 200 * 0.5f && meshPose.m[3][2] == 9 * 0.5f);
            ownFit = 0;   /* no anchor centre: the hand alone */
            assert(gevrModAttachDrawPlayer(&pchr, &rd, 0xFF, list) == list + 1);
            ownFit = 1;
            assert(gevrModAttachDrawPlayer(&pchr, &rd, 0x80, list) == list);   /* fading */
            g_gevrExtraPass = 1;
            assert(gevrModAttachDrawPlayer(&pchr, &rd, 0xFF, list) == list);
            g_gevrExtraPass = 0;
            /* online, this headset's own copy in third person: still its own alone */
            online = 1;
            assert(gevrModAttachDrawPlayer(&pchr, &rd, 0xFF, list) == list + 2 && askedAt[0] == 33);
            /* another player's body in this view isn't this player's own */
            copySlot = 2;
            assert(gevrModAttachDrawPlayer(&pchr, &rd, 0xFF, list) == list + 2 && askedAt[0] == 65);
            /* a guard (not a player's body): none */
            pprop.type = PROP_TYPE_CHR;
            assert(gevrModAttachDrawPlayer(&pchr, &rd, 0xFF, list) == list);
            pprop.type = PROP_TYPE_VIEWER;
            thirdPerson = 0;
            gevrModGameAttachReset();
        }
        D_80032310[0] = 65536.0f;
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
int gevrModMeshLoad(const char *model, const GevrModMesh **out)
{
    meshLoads++;
    *out = (const GevrModMesh *)&meshFile;
    return 1;
}
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
