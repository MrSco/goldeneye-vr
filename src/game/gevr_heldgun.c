/*
 * The guns guards and other players hold, as the first-person models (user,
 * 2026-10-04, with #95's rocket launcher): the third-person Pchr* models a chr
 * holds are a few dozen triangles each, and the launcher's is an open tube
 * with no rocket.
 * The first-person G* models the watch and the weapon wheel show are three to
 * six times the detail, authored in the same units. Each is drawn in the hand
 * the Pchr model would be (propobj.c chrRenderHeldWeapon), fitted once to the
 * Pchr model's box, posed as the watch poses it (gunfire.c
 * set_enviro_fog_for_items_in_solo_watch_menu: hands and sleeve hidden), and a
 * launcher carries the rocket the first-person view shows (gun.c
 * gunUpdateAttachedRocket's PROP_CHRROCKET at the muzzle) until its holder
 * fires. Guards (solo, and co-op's host-run ones) and other players online.
 *
 * Drawn after the body (chr.c) as the first-person gun is drawn, PropType 4
 * with the body's shade as its tint: under the body's PropType 7 a gun-lit
 * list blends toward the blood colour and its second list writes no depth.
 */

#include <ultra64.h>
#include <stdlib.h>
#include <string.h>
#include <bondtypes.h>
#include <bondconstants.h>
#include "system.h"
#include "bondview.h"
#include "chr.h"
#include "chrobjdata.h"
#include "dyn.h"
#include "gun.h"
#include "image.h"
#include "matrixmath.h"
#include "model.h"
#include "objecthandler.h"
#include "player.h"
#include "propobj.h"
#include "net_game.h"
#include "net_rules.h"

extern bool netIsActive(void);
extern int netGetLocalSlot(void);
extern GunModelFileRecord gitem_structs[];
extern Lights1 g_WeaponEnvmapLight;
extern void sub_GAME_7F05E978(Model *model, s32 val);
extern void sub_GAME_7F05EA94(Model *model, s32 val);
extern PROP getPropForHeldItem(ITEM_IDS item);
extern s32 modelLoad(s32 modelid);
extern int VrDetailedGuns;             /* goldeneye-vr.ini DetailedGuns */
extern s32 g_gevrHandPatchSkip;        /* gevr_handpatch.c: the hands are hidden here */
extern s32 g_gevrExtraPass;            /* lv.c: an extra view pass (the copies' own views) */

#define HG_FILES      24
#define HG_BUFSIZE    0x23000          /* the game's per-hand gun buffer (gun.c size_item_buffer) */
#define HG_MODELSIZE  0xF000           /* and its model region (gun.c D_80032464) */
#define HG_RW         256              /* rwdata words per instance */
#define HG_ROCKET_US  1600000          /* the launcher's reload, after its owner fires */

typedef struct
{
    s32 item;
    s32 ok;
    u8 *buf;
    struct texpool pool;
    ModelFileHeader header;
    s32 fitted;                        /* 1 fitted, -1 no fit (the Pchr model is drawn) */
    Mtxf fit;                          /* G model space to the hand's (the Pchr model's) */
} HeldGunFile;

typedef struct
{
    HeldGunFile *file;
    Model model;
    u32 rw[HG_RW];
    Model rocket;
    u32 rocketrw[HG_RW];
    ChrRecord *chr;
    s32 pending;                       /* matrices built for the chr's current draw list */
    s32 rocketon;
    s32 envmap;
    u64 rockethideuntil;
} HeldGunInst;

static HeldGunFile s_files[HG_FILES];
static HeldGunInst s_inst[MAX_PLAYER_COUNT][2];

/*
 * Guards' instances, by chr: the least recently posed is reused, so a pool
 * this size outlasts every guard drawn in one frame. gevr_cheat.txt
 * "hqguards" (bondview2.c) flips guards back to the game's models to compare.
 */
#define HG_GUARDS 48
s32 g_gevrHeldGunGuards = TRUE;
static HeldGunInst s_guardinst[HG_GUARDS][2];
static ChrRecord *s_guardchr[HG_GUARDS];
static u64 s_guardused[HG_GUARDS];

/* ------------------------------------------------------------- the files */

static HeldGunFile *gevrHeldGunLoad(s32 item)
{
    HeldGunFile *f = NULL;
    s32 i;

    if (item <= ITEM_UNARMED || item >= ITEM_IDS_MAX || gitem_structs[item].item_header == NULL
        || gitem_structs[item].item_file_name == NULL)
    {
        return NULL;
    }

    for (i = 0; i < HG_FILES; i++)
    {
        if (s_files[i].item == item)
        {
            return s_files[i].ok ? &s_files[i] : NULL;
        }
        if (f == NULL && s_files[i].item == 0)
        {
            f = &s_files[i];
        }
    }

    if (f == NULL)
    {
        return NULL;
    }

    f->item = item;
    f->ok = FALSE;
    f->fitted = 0;

    if (f->buf == NULL && (f->buf = malloc(HG_BUFSIZE)) == NULL)
    {
        return NULL;
    }

    f->header = *gitem_structs[item].item_header;
    texInitPool(&f->pool, f->buf + HG_MODELSIZE, HG_BUFSIZE - HG_MODELSIZE);
    g_gevrHandPatchSkip = TRUE;
    load_object_fill_header(&f->header, (u8 *)gitem_structs[item].item_file_name, f->buf, HG_MODELSIZE, &f->pool);
    g_gevrHandPatchSkip = FALSE;
    modelCalculateRwDataLen(&f->header);

    if (f->header.RootNode == NULL || f->header.numRecords > HG_RW || f->header.numMatrices <= 0)
    {
        sysLogPrintf(LOG_WARNING, "heldgun: item %d (%s) did not load", item, gitem_structs[item].item_file_name);
        return NULL;
    }

    f->header.isLoaded = TRUE;
    f->ok = TRUE;
    return f;
}

/* A stage's guns, as the stage load meets them (bondview_r.c gevrPreloadStageGuns) */
ModelFileHeader *gevrHeldGunPreload(s32 item)
{
    HeldGunFile *f = gevrHeldGunLoad(item);

    return f != NULL ? &f->header : NULL;
}

void gevrHeldGunReset(void)
{
    s32 i, j;

    for (i = 0; i < HG_GUARDS; i++)
    {
        s_guardchr[i] = NULL;
        s_guardused[i] = 0;
        s_guardinst[i][0].file = s_guardinst[i][1].file = NULL;
        s_guardinst[i][0].pending = s_guardinst[i][1].pending = FALSE;
    }

    for (i = 0; i < HG_FILES; i++)
    {
        s_files[i].item = 0;
        s_files[i].ok = FALSE;
    }

    for (i = 0; i < MAX_PLAYER_COUNT; i++)
    {
        for (j = 0; j < 2; j++)
        {
            s_inst[i][j].file = NULL;
            s_inst[i][j].chr = NULL;
            s_inst[i][j].pending = FALSE;
            s_inst[i][j].rockethideuntil = 0;
        }
    }
}

/* ------------------------------------------------------------- the pose */

/* As the watch poses it: no hands, sleeve or cuff; shells shown; no flash */
static void gevrHeldGunStaticPose(Model *m, ModelFileHeader *h)
{
    union ModelRwData *rw;
    s32 k;

    sub_GAME_7F05E978(m, 0);
    sub_GAME_7F05EA94(m, 1);

    if (h->Switches[1] != NULL && (rw = modelGetNodeRwData(m, h->Switches[1])) != NULL)
    {
        rw->Raw.unk00 = 0;
    }

    if (h->numSwitches >= 19)
    {
        for (k = 0; k < 5; k++)
        {
            if (h->Switches[18 + k] != NULL && (rw = modelGetNodeRwData(m, h->Switches[18 + k])) != NULL)
            {
                rw->Raw.unk00 = 1;
            }
            if (h->Switches[23 + k] != NULL && (rw = modelGetNodeRwData(m, h->Switches[23 + k])) != NULL)
            {
                rw->Raw.unk00 = 1;
            }
        }
    }

    if (h->numSwitches >= 17 && h->Switches[16] != NULL && (rw = modelGetNodeRwData(m, h->Switches[17])) != NULL)
    {
        rw->Raw.unk00 = 0;
    }
}

/* every matrix the root, the revolver's and sw6/7's parts at their offsets */
static void gevrHeldGunMatrices(ModelFileHeader *h, Mtxf *root, Mtxf *mtx)
{
    Mtxf off;
    s32 i;

    for (i = 0; i < h->numMatrices; i++)
    {
        matrix_4x4_copy(root, &mtx[i]);
    }

    if (h->Skeleton == &skeleton_gun_revolver)
    {
        if (h->Switches[4] != NULL)
        {
            matrix_4x4_set_identity_and_position((coord3d *)h->Switches[4]->Data, &off);
            matrix_4x4_multiply(root, &off, &mtx[3]);
        }
        if (h->Switches[5] != NULL)
        {
            matrix_4x4_set_identity_and_position((coord3d *)h->Switches[5]->Data, &off);
            matrix_4x4_multiply(root, &off, &mtx[4]);
        }
    }

    for (i = 6; i <= 7; i++)
    {
        if (h->Switches[i] != NULL)
        {
            s32 index = modelFindNodeMtxIndex(h->Switches[i], 0);

            if (index >= 0 && index < h->numMatrices)
            {
                matrix_4x4_set_identity_and_position((coord3d *)h->Switches[i]->Data, &off);
                matrix_4x4_multiply(root, &off, &mtx[index]);
            }
        }
    }
}

/* ------------------------------------------------------------- the fit */

typedef struct { f32 min[3], max[3]; s32 n; } HgBox;

static void hgBoxAdd(HgBox *b, f32 x, f32 y, f32 z)
{
    f32 p[3] = { x, y, z };
    s32 i;

    for (i = 0; i < 3; i++)
    {
        if (b->n == 0 || p[i] < b->min[i]) b->min[i] = p[i];
        if (b->n == 0 || p[i] > b->max[i]) b->max[i] = p[i];
    }
    b->n++;
}

/* The display lists the posed tree draws (relations applied), skipping the subtrees in skip[] */
static void hgBoxModel(ModelFileHeader *h, ModelNode **skip, s32 nskip, f32 ox, f32 oy, f32 oz, HgBox *b)
{
    ModelNode *node = h->RootNode;

    while (node != NULL)
    {
        s32 descend = TRUE;
        s32 i;

        for (i = 0; i < nskip; i++)
        {
            if (skip[i] != NULL && node == skip[i])
            {
                descend = FALSE;
            }
        }

        if (descend && (node->Opcode & 0xff) == MODELNODE_OPCODE_DL && node->Data != NULL)
        {
            ModelRoData_DisplayListRecord *dl = &node->Data->DisplayList;

            for (i = 0; dl->Vertices != NULL && i < dl->numVertices; i++)
            {
                hgBoxAdd(b, dl->Vertices[i].coord.x + ox, dl->Vertices[i].coord.y + oy, dl->Vertices[i].coord.z + oz);
            }
        }

        if (descend && node->Child != NULL)
        {
            node = node->Child;
            continue;
        }

        while (node != NULL)
        {
            if (node->Next != NULL)
            {
                node = node->Next;
                break;
            }
            node = node->Parent;
        }
    }
}

/*
 * The G model's axes in the hand's frame (the Pchr model's): its barrel +z is
 * the Pchr barrel's -x, its up +y is +z, its +x is -y (the launcher's muzzles
 * agree to a unit). Scaled to the Pchr model's length, its front, top and
 * middle on the Pchr model's.
 */
static s32 gevrHeldGunFit(HeldGunFile *f, Model *pchr)
{
    ModelFileHeader *g = &f->header;
    ModelFileHeader *p = pchr->obj;
    HgBox gb = { { 0 }, { 0 }, 0 }, pb = { { 0 }, { 0 }, 0 };
    ModelNode *pskip[3];
    Model tmp;
    u32 rw[HG_RW];
    Mtxf *mtx;
    f32 s, ox = 0, oy = 0, oz = 0;
    s32 i;

    mtx = malloc(g->numMatrices * sizeof(Mtxf));
    if (mtx == NULL)
    {
        return -1;
    }
    for (i = 0; i < g->numMatrices; i++)
    {
        matrix_4x4_set_identity(&mtx[i]);
    }
    modelInit(&tmp, g, rw);
    tmp.render_pos = (RenderPosView *)mtx;
    gevrHeldGunStaticPose(&tmp, g);
    modelUpdateNodeRelations(&tmp);
    hgBoxModel(g, NULL, 0, 0, 0, 0, &gb);
    free(mtx);

    /* the Pchr model's gun, less its flash parts (Switches 0-2), from its root's origin */
    for (i = 0; i < 3; i++)
    {
        pskip[i] = p->numSwitches > i ? p->Switches[i] : NULL;
    }
    if (p->RootNode != NULL && (p->RootNode->Opcode & 0xff) == MODELNODE_OPCODE_GROUPSIMPLE)
    {
        ox = p->RootNode->Data->GroupSimple.Origin.x;
        oy = p->RootNode->Data->GroupSimple.Origin.y;
        oz = p->RootNode->Data->GroupSimple.Origin.z;
    }
    hgBoxModel(p, pskip, 3, ox, oy, oz, &pb);

    if (gb.n < 3 || pb.n < 3 || gb.max[2] - gb.min[2] < 1.0f)
    {
        sysLogPrintf(LOG_WARNING, "heldgun: item %d: no box to fit (%d / %d points)", f->item, gb.n, pb.n);
        return -1;
    }

    s = (pb.max[0] - pb.min[0]) / (gb.max[2] - gb.min[2]);
    if (s < 0.15f || s > 2.0f)
    {
        sysLogPrintf(LOG_WARNING, "heldgun: item %d: length ratio %.2f, kept the Pchr model", f->item, s);
        return -1;
    }

    matrix_4x4_set_identity(&f->fit);
    f->fit.m[0][0] = 0; f->fit.m[0][1] = -s; f->fit.m[0][2] = 0;   /* G +x -> -y */
    f->fit.m[1][0] = 0; f->fit.m[1][1] = 0;  f->fit.m[1][2] = s;   /* G +y -> +z */
    f->fit.m[2][0] = -s; f->fit.m[2][1] = 0; f->fit.m[2][2] = 0;   /* G +z -> -x */
    f->fit.m[3][0] = pb.min[0] + s * gb.max[2];
    f->fit.m[3][1] = (pb.min[1] + pb.max[1]) * 0.5f + s * (gb.min[0] + gb.max[0]) * 0.5f;
    f->fit.m[3][2] = pb.max[2] - s * gb.max[1];

    sysLogPrintf(LOG_NOTE, "heldgun: item %d %s fitted: scale %.2f offset %.0f,%.0f,%.0f (G %.0f long, Pchr %.0f)",
                 f->item, gitem_structs[f->item].item_file_name, s, f->fit.m[3][0], f->fit.m[3][1], f->fit.m[3][2],
                 gb.max[2] - gb.min[2], pb.max[0] - pb.min[0]);
    return 1;
}

/* ------------------------------------------------------------- per frame */

static HeldGunInst *gevrHeldGunInstFor(ChrRecord *chr, GUNHAND hand)
{
    s32 slot;

    if (g_gevrHeldGunGuards && chr != NULL && chr->prop != NULL && chr->prop->type == PROP_TYPE_CHR
        && hand >= 0 && hand <= 1)
    {
        s32 oldest = 0;

        for (slot = 0; slot < HG_GUARDS; slot++)
        {
            if (s_guardchr[slot] == chr)
            {
                s_guardused[slot] = sysGetMicroseconds();
                return &s_guardinst[slot][hand];
            }
            if (s_guardused[slot] < s_guardused[oldest])
            {
                oldest = slot;
            }
        }
        slot = oldest;
        s_guardchr[slot] = chr;
        s_guardused[slot] = sysGetMicroseconds();
        s_guardinst[slot][0].file = s_guardinst[slot][1].file = NULL;
        s_guardinst[slot][0].pending = s_guardinst[slot][1].pending = FALSE;
        s_guardinst[slot][0].rockethideuntil = s_guardinst[slot][1].rockethideuntil = 0;
        return &s_guardinst[slot][hand];
    }

    if (!netIsActive() || chr == NULL || chr->prop == NULL || chr->prop->type != PROP_TYPE_VIEWER
        || hand < 0 || hand > 1)
    {
        return NULL;
    }

    slot = getPlayerPointerIndex(chr->prop);

    if (slot < 0 || slot >= MAX_PLAYER_COUNT || slot == netGetLocalSlot())
    {
        return NULL;
    }

    return &s_inst[slot][hand];
}

/* chrRenderHeldWeapon, as it rebuilds the chr's draw list: nothing drawn yet for this hand */
void gevrHeldGunBegin(ChrRecord *chr, GUNHAND hand)
{
    HeldGunInst *inst = gevrHeldGunInstFor(chr, hand);

    if (inst != NULL)
    {
        inst->pending = FALSE;
    }
}

/*
 * The hand's G model (and rocket) posed at base, the hand's matrix; TRUE when
 * it is drawn in place of the Pchr model.
 */
s32 gevrHeldGunCompute(ChrRecord *chr, PropRecord *weapon, GUNHAND hand, Mtxf *base, Model *pchr)
{
    HeldGunInst *inst = gevrHeldGunInstFor(chr, hand);
    HeldGunFile *f;
    ModelFileHeader *h;
    s32 item, slot;
    Mtxf scaled, root, *mtx;
    union ModelRwData *rw;
    f32 size;

    if (inst == NULL || !VrDetailedGuns || weapon == NULL || weapon->weapon == NULL || pchr == NULL
        || chr->fadealpha < 0xff || g_gevrExtraPass)
    {
        return FALSE;
    }

    item = weapon->weapon->weaponnum;
    f = gevrHeldGunLoad(item);

    if (f == NULL)
    {
        return FALSE;
    }

    if (f->fitted == 0)
    {
        f->fitted = gevrHeldGunFit(f, pchr);
    }

    if (f->fitted < 0)
    {
        return FALSE;
    }

    h = &f->header;

    if (inst->file != f)
    {
        modelInit(&inst->model, h, inst->rw);
        gevrHeldGunStaticPose(&inst->model, h);
        inst->file = f;
        inst->envmap = item == ITEM_GOLDENGUN || item == ITEM_RUGER || item == ITEM_KNIFE || item == ITEM_THROWKNIFE
                    || item == ITEM_SILVERWPPK || item == ITEM_GOLDWPPK;
    }

    /* the fun options' tiny and big guns, about the hand */
    size = netGunSizeFactor(netActiveGunSize());
    matrix_4x4_copy(&f->fit, &scaled);
    matrix_scalar_multiply(size, scaled.m[0]);   /* the axes; the offset too, below */
    scaled.m[3][0] *= size;
    scaled.m[3][1] *= size;
    scaled.m[3][2] *= size;
    matrix_4x4_multiply(base, &scaled, &root);

    mtx = dynAllocate(h->numMatrices * sizeof(Mtxf));
    gevrHeldGunMatrices(h, &root, mtx);

    /* the muzzle flash while the copy fires, at the muzzle (gunfire.c's first-person flash) */
    if (h->Switches[1] != NULL && (rw = modelGetNodeRwData(&inst->model, h->Switches[1])) != NULL)
    {
        rw->Raw.unk00 = weaponIsGunfireVisible(weapon) && h->Switches[3] != NULL && h->numMatrices > 1;

        if (rw->Raw.unk00)
        {
            Mtxf flash;
            WeaponStats *stats = get_ptr_item_statistics((ITEM_IDS)item);

            matrix_4x4_set_identity_and_position((coord3d *)h->Switches[3]->Data, &flash);
            if (stats != NULL)
            {
                matrix_column_3_scalar_multiply(stats->MuzzleFlashExtension, flash.m[0]);
            }
            matrix_4x4_multiply(&root, &flash, &mtx[1]);
        }
    }

    inst->model.render_pos = (RenderPosView *)mtx;
    modelUpdateNodeRelations(&inst->model);

    /* the launcher's rocket, until its owner fires and again after the reload */
    slot = inst >= &s_inst[0][0] && inst < &s_inst[MAX_PLAYER_COUNT][0] ? (s32)(inst - &s_inst[0][0]) / 2 : -1;
    inst->rocketon = FALSE;

    if (item == ITEM_ROCKETLAUNCH && h->Switches[3] != NULL && sysGetMicroseconds() >= inst->rockethideuntil
        && (slot < 0 || (g_playerPointers[slot] != NULL && !g_playerPointers[slot]->bonddead))
        && PitemZ_entries[PROP_CHRROCKET].header != NULL && PitemZ_entries[PROP_CHRROCKET].header->RootNode != NULL
        && PitemZ_entries[PROP_CHRROCKET].header->numRecords <= HG_RW)
    {
        ModelFileHeader *rh = PitemZ_entries[PROP_CHRROCKET].header;
        Mtxf at, *rmtx;
        s32 i;

        if (inst->rocket.obj != rh)
        {
            modelInit(&inst->rocket, rh, inst->rocketrw);
        }

        /* PchrrocketZ is authored in the launcher's frame: its matrix at the muzzle, unscaled */
        rmtx = dynAllocate(rh->numMatrices * sizeof(Mtxf));
        matrix_4x4_set_identity_and_position((coord3d *)h->Switches[3]->Data, &at);
        matrix_4x4_multiply(&root, &at, &rmtx[0]);
        for (i = 1; i < rh->numMatrices; i++)
        {
            matrix_4x4_copy(&rmtx[0], &rmtx[i]);
        }
        inst->rocket.render_pos = (RenderPosView *)rmtx;
        modelUpdateRelationsQuick(&inst->rocket, rh->RootNode);
        inst->rocketon = TRUE;
    }

    inst->chr = chr;
    inst->pending = TRUE;
    return TRUE;
}

/* gun.c gevrNetSpawnProjectile: the copy fired its rocket */
void gevrHeldGunRocketFired(s32 slot, s32 hand)
{
    if (slot >= 0 && slot < MAX_PLAYER_COUNT && hand >= 0 && hand <= 1)
    {
        s_inst[slot][hand].rockethideuntil = sysGetMicroseconds() + HG_ROCKET_US;
    }
}

/* chraction.c: a guard fired its rocket (either hand's launcher) */
void gevrHeldGunGuardFired(ChrRecord *chr)
{
    s32 slot;

    for (slot = 0; slot < HG_GUARDS; slot++)
    {
        if (s_guardchr[slot] == chr)
        {
            s_guardinst[slot][0].rockethideuntil = s_guardinst[slot][1].rockethideuntil
                = sysGetMicroseconds() + HG_ROCKET_US;
        }
    }
}

/*
 * chr.c, after the body's draw list in each pass: the chr's G models. After
 * the second (alpha) pass their matrices go to the fixed point the display
 * list reads, as the body's do.
 */
Gfx *gevrHeldGunDraw(ChrRecord *chr, ModelRenderData *body, Gfx *gdl, s32 withalpha)
{
    s32 hand;

    for (hand = 0; hand < 2; hand++)
    {
        HeldGunInst *inst = gevrHeldGunInstFor(chr, hand);
        ModelRenderData rd;

        if (inst == NULL || !inst->pending || inst->chr != chr || inst->file == NULL)
        {
            continue;
        }

        rd = *body;
        rd.gdl = gdl;
        rd.PropType = PROP_TYPE_CHR + 1;            /* the first-person gun's render modes */
        rd.envcolour.word = body->fogcolour.word;   /* tinted with the body's shade */
        rd.zbufferenabled = TRUE;

        if (inst->envmap)
        {
            gSPSetLights1(rd.gdl++, g_WeaponEnvmapLight);
            gSPLookAt(rd.gdl++, sub_GAME_7F078474());
        }

        subdraw(&rd, &inst->model);

        if (inst->rocketon)
        {
            subdraw(&rd, &inst->rocket);
        }

        gdl = rd.gdl;

        if (withalpha)
        {
            bondviewTransformManyPosToViewMatrix(inst->model.render_pos, inst->file->header.numMatrices);

            if (inst->rocketon)
            {
                bondviewTransformManyPosToViewMatrix(inst->rocket.render_pos, inst->rocket.obj->numMatrices);
            }

            inst->pending = FALSE;
        }
    }

    return gdl;
}
