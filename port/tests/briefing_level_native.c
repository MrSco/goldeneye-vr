/* Briefing cleanup must not look up a text bank for an unset folder entry. */
#include <stdio.h>

typedef int s32;
typedef unsigned short u16;

#define LEVELID_NONE -1
#define LEVELID_FRIGATE 26
#define LEVELID_SURFACE2 12

struct mission_folder_setup {
    void *string_ptr;
    u16 folder_text_preset;
    u16 icon_text_preset;
    s32 stage_id;
    s32 unknown;
    s32 type;
    s32 mission_num;
    void *briefing_name_ptr;
};

struct mission_folder_setup mission_folder_setup_entries[] = {
    {"1", 1, 0, LEVELID_NONE, 0, 0, -1, 0},
    {"i", 2, 0, LEVELID_FRIGATE, 0, 0, 6, "UbriefdestZ"},
    {"2", 3, 0, LEVELID_NONE, 0, 0, -1, 0},
    {"i", 4, 0, LEVELID_SURFACE2, 0, 0, 7, "UbriefsevxbZ"},
    {0, 0, 0, LEVELID_NONE, 0, 0, -1, 0},
};

s32 briefingpage;

/* INSERT_LEVEL */

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "fail %s line %d\n", #x, __LINE__); return 1; } } while (0)

int main(void)
{
    briefingpage = -1;
    CHECK(gevrBriefingLevel() == LEVELID_NONE);

    briefingpage = 255;
    CHECK(gevrBriefingLevel() == LEVELID_NONE);

    briefingpage = 0;
    CHECK(gevrBriefingLevel() == LEVELID_NONE);

    briefingpage = 1;
    CHECK(gevrBriefingLevel() == LEVELID_FRIGATE);

    briefingpage = 2;
    CHECK(gevrBriefingLevel() == LEVELID_NONE);

    briefingpage = 3;
    CHECK(gevrBriefingLevel() == LEVELID_SURFACE2);

    printf("briefing level ok\n");
    return 0;
}
