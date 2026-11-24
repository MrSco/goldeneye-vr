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
#include "gevr_modapi.h"
#include "gevr_modassets.h"
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
    u8 *buf;
    struct texpool pool;
    ModelFileHeader header;
} AttachFile;

typedef struct
{
    s32 id;                      /* 0 free */
    AttachFile *file;
    s32 on, where, shown;
    s32 node;                    /* on the gun: the matrix of its model it rides */
    Mtxf offset;
    Model model;
    ModelFileHeader *modelOf;    /* the header model was set up for */
    u32 rw[ATT_RW];
} Attachment;

static AttachFile s_files[GEVR_MODAPI_ATTACH_MODELS];
static Attachment s_attach[GEVR_MODAPI_ATTACH_MAX];
static s32 s_nextId = 1;

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
    }
}

/* one attachment at its anchor's frame: the offset laid in it, the model's
 * node matrices from there as the game's props get theirs (instcalcmatrices),
 * or every one the pose for a model with animated joints (attachHasJoints) */
static Gfx *attachDraw(Gfx *gdl, ModelRenderData *templ, Attachment *a, Mtxf *anchor)
{
    ModelFileHeader *h = &a->file->header;
    ModelRenderData rd = *templ;
    Mtxf pose, *mtx;
    s32 i;

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
    return rd.gdl;
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
            gdl = attachDraw(gdl, templ, a, &node);
        }
    }
    return gdl;
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

        if (a->id == 0 || !a->shown || a->on == GEVR_MODATTACH_GUN)
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
        gdl = attachDraw(gdl, &rd, a, &frame);
        matrix_4x4_7F058C88();
        gDPNoOpTag(gdl++, 0x565F0000);   /* the world again */
    }
    if (set)
    {
        gSPPerspNormalize(gdl++, viGetPerspNorm());
    }
    return gdl;
}
