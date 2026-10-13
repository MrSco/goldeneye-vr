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

int main(void)
{
    /* the flash's two lists, its star's list, the gun body's list */
    Gfx flash[4], flash2[2], star[3], body[2];
    union ModelRoData flashdl, stardl, bodydl;
    ModelNode root, sw, group, a, b, gun;
    ModelNode *switches[2];
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

    flashdl.DisplayList.Primary = flash;
    flashdl.DisplayList.Secondary = flash2;
    stardl.DisplayListPrimary.Primary = star;
    bodydl.DisplayList.Primary = body;

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

    if (fails)
    {
        return 1;
    }
    printf("PASS: a held gun's muzzle flash compares depth; the gun and the player's own gun are untouched\n");
    return 0;
}
