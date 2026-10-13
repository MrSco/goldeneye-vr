/* Native check: a held first-person gun's muzzle flash compares depth (gevr_heldgun.c). */
#include <stdio.h>
#include <string.h>
#include <ultra64.h>
#include <bondtypes.h>
#include <bondconstants.h>

/* INSERT_HELDGUN */

#define FLASH_MODE 0x00504340u   /* every first-person gun's flash render mode */

static int fails;

static void check(int ok, const char *what)
{
    if (!ok)
    {
        printf("FAIL: %s\n", what);
        fails++;
    }
}

static void mode(Gfx *g, u32 sft, u32 len, u32 data)
{
    g->words.w0 = ((u32)(u8)G_SETOTHERMODE_L << 24) | (sft << 8) | len;
    g->words.w1 = data;
}

static void end(Gfx *g)
{
    g->words.w0 = (u32)(u8)G_ENDDL << 24;
    g->words.w1 = 0;
}

/*
 * A loaded model file: the switch table first, and the lists in the file, as
 * the converter leaves them. Its lists are reached by segment 5 addresses.
 */
static struct
{
    ModelNode *switches[2];
    Gfx flash[4], flash2[2], star[3], body[2];   /* the flash's two lists, its star's, the gun body's */
} file;

#define SEG5(p) ((Gfx *)(uintptr_t)(0x05000000u + (u32)((u8 *)(p) - (u8 *)&file)))

/* gevr_bean_native_model.c: the production converter on a decompressed cartridge file */
extern void free(void *);
extern void *beanNativeModel(const char *path, const char *name, int switches, int textures, int matrices,
                             ModelFileHeader *header);

static ModelNode *nextNode(ModelNode *n)
{
    if (n->Child != NULL)
    {
        return n->Child;
    }
    while (n != NULL && n->Next == NULL)
    {
        n = n->Parent;
    }
    return n != NULL ? n->Next : NULL;
}

static int underFlash(ModelFileHeader *h, ModelNode *n)
{
    for (; n != NULL; n = n->Parent)
    {
        if (n == h->Switches[1])
        {
            return 1;
        }
    }
    return 0;
}

/* render-mode commands in a list, and how many compare depth */
static void countModes(ModelFileHeader *h, Gfx *list, int *modes, int *compare)
{
    uintptr_t at = (uintptr_t)list;
    Gfx *g = (at >> 24) == 0x05 ? (Gfx *)((u8 *)h->Switches + (at & 0xffffff)) : list;

    for (; g != NULL; g++)
    {
        u32 w0 = (u32)g->words.w0;

        if ((s8)(w0 >> 24) == (s8)G_ENDDL)
        {
            break;
        }
        if ((s8)(w0 >> 24) == (s8)G_SETOTHERMODE_L && ((w0 >> 8) & 0xff) == G_MDSFT_RENDERMODE)
        {
            ++*modes;
            *compare += (g->words.w1 & Z_CMP) != 0;
        }
    }
}

/* [0] the flash's modes, [1] those comparing depth, [2] the rest, [3] those comparing */
static void census(ModelFileHeader *h, int out[4])
{
    ModelNode *n;

    memset(out, 0, 4 * sizeof(int));
    for (n = h->RootNode; n != NULL; n = nextNode(n))
    {
        int in = underFlash(h, n) ? 0 : 2;
        s32 op = n->Opcode & 0xff;

        if (n->Data != NULL && op == MODELNODE_OPCODE_DL)
        {
            countModes(h, n->Data->DisplayList.Primary, &out[in], &out[in + 1]);
            countModes(h, n->Data->DisplayList.Secondary, &out[in], &out[in + 1]);
        }
        else if (n->Data != NULL && op == MODELNODE_OPCODE_DLPRIMARY)
        {
            countModes(h, n->Data->DisplayListPrimary.Primary, &out[in], &out[in + 1]);
        }
    }
}

static int num(const char *s)
{
    int v = 0;

    while (*s >= '0' && *s <= '9')
    {
        v = v * 10 + (*s++ - '0');
    }
    return v;
}

/* argv: decompressed file, name, switches, textures, matrices */
static void romModel(char **argv)
{
    ModelFileHeader h;
    int before[4], after[4];
    void *model = beanNativeModel(argv[1], argv[2], num(argv[3]), num(argv[4]), num(argv[5]), &h);

    if (model == NULL)
    {
        check(0, "the cartridge model converts");
        return;
    }

    census(&h, before);
    gevrHeldGunFlashDepth(&h);
    census(&h, after);

    printf("%s: flash render modes %d (%d compare depth, %d before); other %d (%d compare, %d before)\n",
           argv[2], after[0], after[1], before[1], after[2], after[3], before[3]);
    check(after[0] > 0 && after[1] == after[0], "every flash render mode compares depth");
    check(before[1] == 0, "the cartridge's flash compared no depth");
    check(after[2] == before[2] && after[3] == before[3], "the rest of the gun is as the cartridge has it");
    free(model);
}

int main(int argc, char **argv)
{
    Gfx *flash = file.flash, *flash2 = file.flash2, *star = file.star, *body = file.body;
    ModelNode **switches = file.switches;
    union ModelRoData flashdl, stardl, bodydl;
    ModelNode root, sw, group, a, b, gun;
    ModelFileHeader h;

    memset(&flashdl, 0, sizeof(flashdl));
    memset(&stardl, 0, sizeof(stardl));
    memset(&bodydl, 0, sizeof(bodydl));
    memset(&root, 0, sizeof(root));
    memset(&sw, 0, sizeof(sw));
    memset(&group, 0, sizeof(group));
    memset(&a, 0, sizeof(a));
    memset(&b, 0, sizeof(b));
    memset(&gun, 0, sizeof(gun));
    memset(&h, 0, sizeof(h));

    mode(&flash[0], G_MDSFT_ALPHACOMPARE, 2, 0);
    flash[1].words.w0 = 0x01020304;   /* anything else */
    flash[1].words.w1 = 0x05060708;
    mode(&flash[2], G_MDSFT_RENDERMODE, 29, FLASH_MODE);
    end(&flash[3]);
    mode(&flash2[0], G_MDSFT_RENDERMODE, 29, FLASH_MODE);
    end(&flash2[1]);
    mode(&star[0], G_MDSFT_RENDERMODE, 29, FLASH_MODE);
    end(&star[1]);
    mode(&star[2], G_MDSFT_RENDERMODE, 29, FLASH_MODE);   /* past the end: not the list's */
    mode(&body[0], G_MDSFT_RENDERMODE, 29, FLASH_MODE);
    end(&body[1]);

    flashdl.DisplayList.Primary = SEG5(flash);
    flashdl.DisplayList.Secondary = flash2;   /* a plain pointer works as well */
    stardl.DisplayListPrimary.Primary = SEG5(star);
    bodydl.DisplayList.Primary = SEG5(body);

    /* root -> switch 1 -> group -> (a: list, b: star); the gun body is the switch's sibling */
    root.Opcode = MODELNODE_OPCODE_GROUPSIMPLE;
    root.Child = &sw;
    sw.Opcode = MODELNODE_OPCODE_SWITCH;
    sw.Parent = &root;
    sw.Next = &gun;
    sw.Child = &group;
    group.Opcode = MODELNODE_OPCODE_GROUP;
    group.Parent = &sw;
    group.Child = &a;
    a.Opcode = MODELNODE_OPCODE_DL;
    a.Data = &flashdl;
    a.Parent = &group;
    a.Next = &b;
    b.Opcode = MODELNODE_OPCODE_DLPRIMARY;
    b.Data = &stardl;
    b.Parent = &group;
    b.Prev = &a;
    gun.Opcode = MODELNODE_OPCODE_DL;
    gun.Data = &bodydl;
    gun.Parent = &root;
    gun.Prev = &sw;

    switches[0] = &gun;
    switches[1] = &sw;
    h.Switches = switches;
    h.numSwitches = 2;

    gevrHeldGunFlashDepth(&h);

    check(flash[2].words.w1 == (FLASH_MODE | Z_CMP), "the flash list's render mode compares depth");
    check(flash2[0].words.w1 == (FLASH_MODE | Z_CMP), "the flash's second list compares depth");
    check(star[0].words.w1 == (FLASH_MODE | Z_CMP), "the star (DLPRIMARY) compares depth");
    check(flash[0].words.w1 == 0, "an alpha-compare command is left alone");
    check(flash[1].words.w0 == 0x01020304 && flash[1].words.w1 == 0x05060708, "other commands are left alone");
    check(star[2].words.w1 == FLASH_MODE, "nothing past the list's end is touched");
    check(body[0].words.w1 == FLASH_MODE, "the gun body, outside switch 1, keeps its mode");
    check((FLASH_MODE & Z_UPD) == 0 && (flash[2].words.w1 & Z_UPD) == 0, "the flash still writes no depth");

    /* a file without a switch 1, and one whose switch 1 is empty */
    h.numSwitches = 1;
    gevrHeldGunFlashDepth(&h);
    switches[1] = NULL;
    h.numSwitches = 2;
    gevrHeldGunFlashDepth(&h);
    check(body[0].words.w1 == FLASH_MODE, "no switch 1: nothing changes");

    if (argc == 6)
    {
        romModel(argv);
    }

    if (fails)
    {
        return 1;
    }
    printf("PASS: a held gun's muzzle flash compares depth; the gun and the player's own gun are untouched\n");
    return 0;
}
