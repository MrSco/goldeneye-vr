/*
 * A mod's attachments (mod.attach, docs/mod-system.md): a pack's model drawn on
 * the local player's gun, on a hand, or on the body, placed by the engine every
 * rendered frame where the anchor is then, so it never trails the hand as a
 * position set from a script's tick would (scripts tick at 60 Hz, a headset
 * draws at 72 to 120).
 *
 * The models: assets/models/<model> from the run's mod, built over one of the
 * cartridge's model files (like: a G gun or P prop file, its header's switch
 * and texture counts and its skeleton), as assets/models' replacements are;
 * loaded once into a buffer of its own, as gevr_heldgun.c loads guns, through
 * load_resource with the file's name pending (gevrModAssetPendingName). A file
 * that doesn't load is refused, never drawn as the cartridge's.
 *
 * The anchors, each a frame in view space at the viewmodel's scale (a gun
 * model's units, 0.085 cm each at the player's gun size), the model's own
 * offset (at, turn, scale: gevr_modapi.c builds it) laid in it:
 *  - the gun in a hand: its model's root matrix as drawn (gunfire.c), so the
 *    part moves with the gun's recoil, reload and switch, and a mirrored
 *    left gun's part mirrors with it;
 *  - a hand: the controller's grip (bondview2.c gevrModAttachHandFrame);
 *  - a body slot: the slot's centre on the torso (gevr_bodyslots.c
 *    gevrBodySlotFrame), kept while a mod runs even with body slots off.
 * The local player's view only; another player doesn't see them.
 *  - a guard's body part: the matrix of the part the game's hits name
 *    (its bounding box's ModelNumber, HIT_*), as drawn (chr.c
 *    chrRenderProp, after the body and what's stuck to it), in every view
 *    the guard's drawn in; kept on its body, dead or alive, until the body
 *    is removed (chrpropCleanupForRemoval).
 */

#include <ultra64.h>
#include <stdlib.h>
#include <string.h>
#include <bondtypes.h>
#include <bondconstants.h>
#include "system.h"
#include "bondview.h"
#include "chrobjdata.h"
#include "dyn.h"
#include "gun.h"
#include "image.h"
#include "matrixmath.h"
#include "model.h"
#include "objecthandler.h"
#include "player.h"
#include "sched.h"
#include "gevr_modapi.h"
#include "gevr_modattach.h"
#include "gevr_modassets.h"
#include "gevr_modmesh.h"
#include "gevr_modscript.h"

extern bool netIsActive(void);
extern int netGetLocalSlot(void);
extern GunModelFileRecord gitem_structs[];
extern s32 g_gevrHandPatchSkip;   /* gevr_handpatch.c: the VR hands' patches leave this load alone */
extern s32 g_gevrExtraPass;       /* lv.c: an extra view pass (the copies' own views) */
extern s32 g_gevrStereo;
extern void matrix_4x4_7F058C64(void);
extern void matrix_4x4_7F058C88(void);
extern s32 gevrModAttachHandFrame(s32 hand, Mtxf *root);   /* bondview2.c */
extern s32 gevrBodySlotFrame(s32 slot, Mtxf *root);        /* gevr_bodyslots.c */

#define ATT_BUFSIZE   0x23000    /* as a hand's gun buffer (gevr_heldgun.c HG_BUFSIZE) */
#define ATT_MODELSIZE 0xF000     /* and its model region */
#define ATT_RW        256        /* rwdata words per instance */
#define ATT_NAME      64

typedef struct
{
    char model[ATT_NAME], like[ATT_NAME];
    s32 ok;
    s32 rigid;                   /* joints only an animation poses (a gun file's): drawn at its root */
    s32 nodes;                   /* its nodes, for the room a draw takes (attachDraw) */
    u8 *buf;
    struct texpool pool;
    ModelFileHeader header;
    const GevrModMesh *mesh;
} AttachFile;

typedef struct
{
    s32 id;                      /* 0 free */
    AttachFile *file;
    s32 on, where, shown;
    s32 node;                    /* on the gun: the matrix of its model it rides; on a guard: the part */
    ChrRecord *chr;              /* on a guard: its record, kept past its id's retiring */
    ModelFileHeader *partOf;     /* and the body the part's matrix was found in */
    s32 partMtx;
    Mtxf offset;
    Mtxf drawnPose;
    u32 drawnFrame;
    ITEM_IDS drawnItem;
    s32 drawnMirrored;
    const ModelFileHeader *drawnGun;  /* NULL until this attachment emits a draw */
    const GevrModMesh *drawnGunMesh;
    Model model;
    ModelFileHeader *modelOf;    /* the header model was set up for */
    u32 rw[ATT_RW];
} Attachment;

static AttachFile s_files[GEVR_MODAPI_ATTACH_MODELS];
static Attachment s_attach[GEVR_MODAPI_ATTACH_MAX];
static s32 s_nextId = 1;
static s32 s_roomWarned;   /* a frame ran out of room: logged once a mission */

static s32 attachLocal(void)
{
    return g_CurrentPlayer != NULL && (!netIsActive() || get_cur_playernum() == netGetLocalSlot());
}

/* the cartridge's model file named like: its header and its own name */
static ModelFileHeader *likeHeader(const char *like, char **file)
{
    s32 i;

    for (i = 1; i < ITEM_IDS_MAX; i++)
    {
        if (gitem_structs[i].item_header != NULL && gitem_structs[i].item_file_name != NULL
            && strcmp(gitem_structs[i].item_file_name, like) == 0)
        {
            *file = gitem_structs[i].item_file_name;
            return gitem_structs[i].item_header;
        }
    }
    for (i = 0; PitemZ_entries != NULL && i < g_GevrPropCount; i++)
    {
        if (PitemZ_entries[i].header != NULL && PitemZ_entries[i].filename != NULL
            && strcmp(PitemZ_entries[i].filename, like) == 0)
        {
            *file = PitemZ_entries[i].filename;
            return PitemZ_entries[i].header;
        }
    }
    return NULL;
}

static s32 attachHasJoints(ModelNode *node);
static s32 attachNodes(ModelNode *node);

/* the model, loaded once per mission: NULL when it isn't the mod's or doesn't fit */
static AttachFile *attachLoad(const char *model, const char *like)
{
    AttachFile *f = NULL;
    ModelFileHeader *h;
    char *file = NULL;
    s32 i, used;

    for (i = 0; i < GEVR_MODAPI_ATTACH_MODELS; i++)
    {
        if (s_files[i].model[0] && strcmp(s_files[i].model, model) == 0 && strcmp(s_files[i].like, like) == 0)
        {
            return s_files[i].ok ? &s_files[i] : NULL;
        }
        if (f == NULL && !s_files[i].model[0])
        {
            f = &s_files[i];
        }
    }
    if (f == NULL)
    {
        sysLogPrintf(LOG_WARNING, "mods: attach: %d models already; %s isn't loaded", GEVR_MODAPI_ATTACH_MODELS, model);
        return NULL;
    }
    snprintf(f->model, sizeof(f->model), "%s", model);
    snprintf(f->like, sizeof(f->like), "%s", like);
    f->ok = FALSE;   /* tried: not again this mission */
    used = gevrModMeshLoad(model, &f->mesh);
    if (used != 0) {
        f->ok = used > 0;
        return f->ok ? f : NULL;
    }
    if ((h = likeHeader(like, &file)) == NULL)
    {
        sysLogPrintf(LOG_WARNING, "mods: attach: the cartridge has no model %s to build %s over", like, model);
        return NULL;
    }
    if (f->buf == NULL && (f->buf = malloc(ATT_BUFSIZE)) == NULL)
    {
        return NULL;
    }
    f->header = *h;
    texInitPool(&f->pool, f->buf + ATT_MODELSIZE, ATT_BUFSIZE - ATT_MODELSIZE);
    gevrModAssetPendingName = model;
    gevrModAssetPendingUsed = FALSE;
    g_gevrHandPatchSkip = TRUE;
    load_object_fill_header(&f->header, (u8 *) file, f->buf, ATT_MODELSIZE, &f->pool);
    g_gevrHandPatchSkip = FALSE;
    used = gevrModAssetPendingUsed;
    gevrModAssetPendingName = NULL;
    if (!used)
    {
        /* the cartridge's file came in its place: never drawn as the mod's (ob.c logged why) */
        sysLogPrintf(LOG_WARNING, "mods: attach: %s didn't load over %s", model, like);
        return NULL;
    }
    modelCalculateRwDataLen(&f->header);
    if (f->header.RootNode == NULL || f->header.numRecords > ATT_RW || f->header.numMatrices <= 0)
    {
        sysLogPrintf(LOG_WARNING, "mods: attach: %s loaded over %s, but isn't one to draw", model, like);
        return NULL;
    }
    f->header.isLoaded = TRUE;
    f->rigid = attachHasJoints(f->header.RootNode);
    f->nodes = attachNodes(f->header.RootNode);
    f->ok = TRUE;
    return f;
}

/* a joint the game poses only from an animation (MODELNODE_OPCODE_GROUP: a gun
 * file's, a character's). With none to play, model.c's process_02_position
 * leaves its matrix unset, so such a model is drawn rigid at its root, as the
 * body slots draw a gun (gevr_heldgun.c gevrHeldGunMatrices). A prop file's
 * nodes are laid out from their own positions (instcalcmatrices). */
static s32 attachHasJoints(ModelNode *node)
{
    while (node != NULL)
    {
        if ((node->Opcode & 0xff) == MODELNODE_OPCODE_GROUP
            || (node->Child != NULL && attachHasJoints(node->Child)))
        {
            return TRUE;
        }
        node = node->Next;
    }
    return FALSE;
}

static s32 attachNodes(ModelNode *node)
{
    s32 n = 0;

    for (; node != NULL; node = node->Next)
    {
        n += 1 + (node->Child != NULL ? attachNodes(node->Child) : 0);
    }
    return n;
}

/* a character model's body part (HIT_*): the matrix of its bounding box's
 * node, or -1 */
static s32 attachPartMtx(ModelFileHeader *h, s32 part)
{
    ModelNode *node = h != NULL ? h->RootNode : NULL;

    while (node != NULL)
    {
        if ((node->Opcode & 0xff) == MODELNODE_OPCODE_BBOX && node->Data != NULL
            && (s32) node->Data->BoundingBox.ModelNumber == part)
        {
            const s32 index = modelFindNodeMtxIndex(node, 0);

            return index >= 0 && index < h->numMatrices ? index : -1;
        }
        if (node->Child != NULL)
        {
            node = node->Child;
        }
        else
        {
            while (node != NULL && node->Next == NULL)
            {
                node = node->Parent;
            }
            if (node != NULL)
            {
                node = node->Next;
            }
        }
    }
    return -1;
}

static Attachment *attachFind(s32 id)
{
    s32 i;

    for (i = 0; id > 0 && i < GEVR_MODAPI_ATTACH_MAX; i++)
    {
        if (s_attach[i].id == id)
        {
            return &s_attach[i];
        }
    }
    return NULL;
}

int gevrModGameAttach(const GevrModAttach *a)
{
    Attachment *slot = NULL;
    AttachFile *f;
    ChrRecord *chr = NULL;
    s32 i, j;

    for (i = 0; i < GEVR_MODAPI_ATTACH_MAX && slot == NULL; i++)
    {
        if (s_attach[i].id == 0)
        {
            slot = &s_attach[i];
        }
    }
    if (slot == NULL)
    {
        return -1;
    }
    if (a->on == GEVR_MODATTACH_GUARD)
    {
        /* a guard with that body part now; kept by its record from here */
        extern ChrRecord *gevrModGameGuardChr(int id);   /* gevr_modgame.c */

        chr = gevrModGameGuardChr(a->where);
        if (chr == NULL || chr->model == NULL || attachPartMtx(chr->model->obj, a->node) < 0)
        {
            return 0;
        }
    }
    if ((f = attachLoad(a->model, a->like)) == NULL)
    {
        return 0;
    }
    memset(slot, 0, sizeof(*slot));
    slot->id = s_nextId++;
    slot->file = f;
    slot->on = a->on;
    slot->where = a->where;
    slot->node = a->node;
    slot->chr = chr;
    slot->partOf = NULL;
    slot->shown = TRUE;
    for (i = 0; i < 4; i++)
    {
        for (j = 0; j < 4; j++)
        {
            slot->offset.m[i][j] = a->offset[i][j];
        }
    }
    return slot->id;
}

int gevrModGameDetach(int id)
{
    Attachment *a = attachFind(id);

    if (a != NULL)
    {
        a->id = 0;
    }
    return a != NULL;
}

int gevrModGameAttachPlace(int id, const float offset[4][4])
{
    Attachment *a = attachFind(id);
    s32 i, j;

    if (a != NULL)
    {
        for (i = 0; i < 4; i++)
        {
            for (j = 0; j < 4; j++)
            {
                a->offset.m[i][j] = offset[i][j];
            }
        }
    }
    return a != NULL;
}

int gevrModGameAttachShow(int id, int shown)
{
    Attachment *a = attachFind(id);

    if (a != NULL)
    {
        a->shown = shown != 0;
        if (!a->shown) a->drawnGun = NULL;
    }
    return a != NULL;
}

/* a new mission, a mod stopped: none attached, no model kept (the buffers stay
 * for reuse); only tables, never a player that may be gone */
void gevrModGameAttachReset(void)
{
    s32 i;

    for (i = 0; i < GEVR_MODAPI_ATTACH_MAX; i++)
    {
        s_attach[i].id = 0;
    }
    for (i = 0; i < GEVR_MODAPI_ATTACH_MODELS; i++)
    {
        s_files[i].model[0] = '\0';
        s_files[i].ok = FALSE;
        s_files[i].mesh = NULL;
    }
    s_roomWarned = FALSE;
}

/* one attachment at its anchor's frame: the offset laid in it, the model's
 * node matrices from there as the game's props get theirs (instcalcmatrices),
 * or every one the pose for a model with animated joints (attachHasJoints) */
static Gfx *attachDraw(Gfx *gdl, ModelRenderData *templ, Attachment *a, Mtxf *anchor, Mtxf *drawnPose)
{
    ModelFileHeader *h = &a->file->header;
    ModelRenderData rd = *templ;
    Mtxf pose, *mtx;
    s32 i;

    if (a->file->mesh != NULL) {
        matrix_4x4_multiply(anchor, &a->offset, &pose);
        Gfx *end = gevrModMeshDraw(gdl, &pose, a->file->mesh);
        if (drawnPose != NULL && end != gdl) *drawnPose = pose;
        return end;
    }

    /* its matrices, and its commands (a few a node, as subdraw lays them,
     * with margin), else skipped this frame: an overrun stops the game */
    if (!gevrDynRoom(gdl, a->file->nodes * 8 + 64, h->numMatrices * (s32) sizeof(Mtxf) + 0x400))
    {
        if (!s_roomWarned)
        {
            sysLogPrintf(LOG_WARNING, "mods: attach: no room left this frame for %s; skipped while there's none",
                         a->file->model);
            s_roomWarned = TRUE;
        }
        return gdl;
    }
    if (a->modelOf != h)
    {
        modelInit(&a->model, h, a->rw);
        a->modelOf = h;
    }
    matrix_4x4_multiply(anchor, &a->offset, &pose);
    mtx = dynAllocate(h->numMatrices * sizeof(Mtxf));
    for (i = 0; i < h->numMatrices; i++)
    {
        mtx[i] = pose;
    }
    if (a->file->rigid)
    {
        a->model.render_pos = (RenderPosView *) mtx;
    }
    else
    {
        rd.basemtx = &pose;
        rd.mtxlist = mtx;
        instcalcmatrices(&rd, &a->model);
    }
    modelUpdateRelationsQuick(&a->model, h->RootNode);
    rd.gdl = gdl;
    subdraw(&rd, &a->model);
    bondviewTransformManyPosToViewMatrix(a->model.render_pos, h->numMatrices);
    if (drawnPose != NULL && rd.gdl != gdl) *drawnPose = pose;
    return rd.gdl;
}

/* Clear an earlier view's results even if the gun will be skipped in this one. */
void gevrModAttachBeginGunDraw(void)
{
    s32 i;
    if (!attachLocal()) return;
    for (i = 0; i < GEVR_MODAPI_ATTACH_MAX; i++) s_attach[i].drawnGun = NULL;
}

int gevrModAttachGunFrame(int id, Mtxf *pose, s32 *mirrored)
{
    Attachment *a = attachFind(id);
    /* Input runs after the counter advances but before the next gun draw.
     * BeginGunDraw clears every old entry when that draw starts, even if the
     * part is then skipped, so only the latest view can answer. */
    if (a == NULL || !a->shown || a->drawnGun == NULL
        || (u32)os_scheduler.frameCount - a->drawnFrame > 1u || !attachLocal()) return 0;
    if (g_CurrentPlayer->hands[a->where].weaponModel.obj != a->drawnGun
        || a->node >= a->drawnGun->numMatrices
        || get_item_in_hand_or_watch_menu(a->where) != a->drawnItem
        || gevrModGunMesh(a->where) != a->drawnGunMesh) return 0;
    if (pose != NULL) *pose = a->drawnPose;
    if (mirrored != NULL) *mirrored = a->drawnMirrored;
    return 1;
}

static s32 attachMirrored(const Mtxf *pose)
{
    const f32 (*m)[4] = pose->m;
    const f32 determinant = m[0][0] * (m[1][1] * m[2][2] - m[1][2] * m[2][1])
                          - m[0][1] * (m[1][0] * m[2][2] - m[1][2] * m[2][0])
                          + m[0][2] * (m[1][0] * m[2][1] - m[1][1] * m[2][0]);
    return determinant < 0.0f;
}

/* gunfire.c, as the hand's gun is drawn: what sits on that gun, on the matrix
 * of its model it rides as drawn (the root, or a part the game animates: a
 * slide, a bolt), none on a model without that matrix */
Gfx *gevrModAttachDrawGun(Gfx *gdl, ModelRenderData *templ, s32 hand, Model *gun)
{
    Mtxf node;
    s32 i;

    if (!attachLocal() || gun == NULL || gun->obj == NULL || gun->render_pos == NULL || gun->obj->numMatrices <= 0)
    {
        return gdl;
    }
    for (i = 0; i < GEVR_MODAPI_ATTACH_MAX; i++)
    {
        Attachment *a = &s_attach[i];

        if (a->id != 0 && a->shown && a->on == GEVR_MODATTACH_GUN && a->where == hand && a->node < gun->obj->numMatrices)
        {
            node = ((Mtxf *) gun->render_pos)[a->node];
            Gfx *end = attachDraw(gdl, templ, a, &node, &a->drawnPose);
            if (end != gdl) {
                a->drawnFrame = os_scheduler.frameCount;
                a->drawnItem = get_item_in_hand_or_watch_menu(hand);
                a->drawnMirrored = attachMirrored(&a->drawnPose);
                a->drawnGun = gun->obj;
                a->drawnGunMesh = gevrModGunMesh(hand);
            }
            gdl = end;
        }
    }
    return gdl;
}

/* chr.c chrRenderProp, after a chr's body and what's stuck to it, in each of
 * its passes (body's flags: the solid parts, the see-through, or a fading
 * body's all): what a mod put on that guard, at its parts' matrices as just
 * drawn, drawn as the objects stuck to it are */
Gfx *gevrModAttachDrawGuard(ChrRecord *chr, ModelRenderData *body, s32 fadealpha, Gfx *gdl)
{
    ModelRenderData objdata;
    Mtxf node;
    s32 i;

    if (chr == NULL || chr->model == NULL || chr->model->render_pos == NULL || chr->prop == NULL
        || chr->prop->type != PROP_TYPE_CHR)
    {
        return gdl;
    }
    objdata = *body;
    objdata.PropType = fadealpha < 0xFF ? 5 : 9;   /* as gevrChrEmbeddedRender's objects */
    objdata.envcolour.word = fadealpha < 0xFF ? (u32) fadealpha : 0;
    for (i = 0; i < GEVR_MODAPI_ATTACH_MAX; i++)
    {
        Attachment *a = &s_attach[i];

        if (a->id == 0 || !a->shown || a->on != GEVR_MODATTACH_GUARD || a->chr != chr)
        {
            continue;
        }
        if (a->partOf != chr->model->obj)
        {
            a->partMtx = attachPartMtx(chr->model->obj, a->node);   /* found again for another body */
            a->partOf = chr->model->obj;
        }
        if (a->partMtx < 0)
        {
            continue;
        }
        node = ((Mtxf *) chr->model->render_pos)[a->partMtx];
        objdata.gdl = gdl;
        gdl = attachDraw(gdl, &objdata, a, &node, NULL);
    }
    return gdl;
}

/* chr.c chrpropCleanupForRemoval: the guard's body goes, and what a mod put on
 * it with it (its record may hold another guard next) */
void gevrModAttachChrGone(ChrRecord *chr)
{
    s32 i;

    for (i = 0; chr != NULL && i < GEVR_MODAPI_ATTACH_MAX; i++)
    {
        if (s_attach[i].id != 0 && s_attach[i].on == GEVR_MODATTACH_GUARD && s_attach[i].chr == chr)
        {
            gevrModApiAttachGone(s_attach[i].id);
            s_attach[i].id = 0;
        }
    }
}

/* gevr_bodyslots.c, with what the body holds: what sits on a hand or the body
 * (VR: a controller's frame, and the torso's, kept while a mod runs) */
Gfx *gevrModAttachDrawBody(Gfx *gdl)
{
    ModelRenderData rd;
    s32 i, set = FALSE;

    if (!g_gevrStereo || g_gevrExtraPass || !attachLocal())
    {
        return gdl;
    }
    rd = (ModelRenderData){0};
    rd.zbufferenabled = TRUE;
    rd.flags = 3;
    rd.PropType = 4;
    rd.envcolour.word = g_CurrentPlayer->tileColor.a | ((u32) g_CurrentPlayer->tileColor.r << 24)
                      | ((u32) g_CurrentPlayer->tileColor.g << 16) | ((u32) g_CurrentPlayer->tileColor.b << 8);
    for (i = 0; i < GEVR_MODAPI_ATTACH_MAX; i++)
    {
        Attachment *a = &s_attach[i];
        Mtxf frame;
        s32 ok;

        if (a->id == 0 || !a->shown || (a->on != GEVR_MODATTACH_HAND && a->on != GEVR_MODATTACH_BODY))
        {
            continue;
        }
        ok = a->on == GEVR_MODATTACH_HAND ? gevrModAttachHandFrame(a->where, &frame) : gevrBodySlotFrame(a->where, &frame);
        if (!ok)
        {
            continue;
        }
        if (!set)
        {
            gSPPerspNormalize(gdl++, matrix_4x4_calc_depth_scale(0.0f, 300.0f));   /* as the guns are */
            set = TRUE;
        }
        /* VR_HAND_DRAW: a hand's follows its controller between game frames (1 the
         * off hand, 2 the gun hand, as gunfire.c gevrHandTag), the body's the body (3) */
        gDPNoOpTag(gdl++, 0x565F0000 | (u32) (a->on == GEVR_MODATTACH_HAND ? (a->where == GUNRIGHT ? 2 : 1) : 3));
        matrix_4x4_7F058C64();
        gdl = attachDraw(gdl, &rd, a, &frame, NULL);
        matrix_4x4_7F058C88();
        gDPNoOpTag(gdl++, 0x565F0000);   /* the world again */
    }
    if (set)
    {
        gSPPerspNormalize(gdl++, viGetPerspNorm());
    }
    return gdl;
}
