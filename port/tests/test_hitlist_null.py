"""A missing character hit list is a miss, not a NULL+0x18 SIGSEGV.

Report ee07735d died in sub_GAME_7F06C010 while the player was firing.
The production function is compiled from src/game/objecthandler.c.
"""
from pathlib import Path
import re
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]


def function(source, signature):
    match = re.search(re.escape(signature) + r"(?:\s|//[^\n]*\n|/\*.*?\*/\s*)*\{", source, re.S)
    assert match, signature
    end, depth = match.end(), 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[match.start():end]


source = (ROOT / "src/game/objecthandler.c").read_text(encoding="utf-8")
body = function(
    source,
    "s32 sub_GAME_7F06C010(ModelHitEntry **entryptr, coord3d *modelRayStart, coord3d *modelRayDir, Model **outModel, ModelNode **outNode)",
)
assert "entry->next" in body
assert "*entryptr == NULL" in body

fixture = r"""
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct Model Model;
typedef struct ModelNode ModelNode;
typedef struct coord3d { float x, y, z; } coord3d;
typedef int s32;

typedef struct ModelHitEntry {
    struct Model *model;
    struct ModelNode *rootnode;
    float sortvalue;
    struct ModelHitEntry *next;
    struct ModelHitEntry *prev;
} ModelHitEntry;

static int g_searched;

s32 probably_damage_detail_blood_effect_related(ModelHitEntry **entryptr, coord3d *raypos, coord3d *raydir, Model **outModel, ModelNode **inoutNode)
{
    (void)raypos;
    (void)raydir;
    g_searched++;
    if (entryptr == NULL || *entryptr == NULL) return -1;
    if ((*entryptr)->next != NULL) return -2;
    *outModel = (*entryptr)->model;
    *inoutNode = (*entryptr)->rootnode;
    return 7;
}

""" + body + r"""

static void expect(int cond, const char *what)
{
    if (!cond) {
        fprintf(stderr, "fail: %s\n", what);
        exit(1);
    }
}

int main(void)
{
    ModelHitEntry *entry = NULL;
    Model *model = (Model *)(void *)1;
    ModelNode *node = (ModelNode *)(void *)1;
    ModelHitEntry head, tail;
    coord3d origin, dir;

    expect(offsetof(ModelHitEntry, next) == 0x18, "next is arm64 fault offset 0x18");
    expect(sizeof(ModelHitEntry) == 40, "host entry is 40 bytes");

    expect(sub_GAME_7F06C010(NULL, &origin, &dir, &model, &node) == 0, "null entryptr");
    expect(model == NULL && node == NULL, "outputs cleared");
    expect(g_searched == 0, "null entryptr does not search");

    model = (Model *)(void *)1;
    node = (ModelNode *)(void *)1;
    expect(sub_GAME_7F06C010(&entry, &origin, &dir, &model, &node) == 0, "null list");
    expect(entry == NULL && model == NULL && node == NULL, "null list stays a miss");
    expect(g_searched == 0, "null list does not search");

    memset(&head, 0, sizeof(head));
    memset(&tail, 0, sizeof(tail));
    head.model = (Model *)(void *)0x11;
    head.rootnode = (ModelNode *)(void *)0x22;
    head.next = &tail;
    tail.model = (Model *)(void *)0x33;
    tail.rootnode = (ModelNode *)(void *)0x44;
    entry = &head;
    expect(sub_GAME_7F06C010(&entry, &origin, &dir, &model, &node) == 7, "walks to the tail");
    expect(entry == &tail, "entryptr is the last node");
    expect(model == tail.model && node == tail.rootnode, "search sees the tail");
    expect(g_searched == 1, "a real list is searched once");
    return 0;
}
"""

with tempfile.TemporaryDirectory(prefix="gevr-hitlist-") as temp:
    temp = Path(temp)
    src, exe = temp / "test.c", temp / "test.exe"
    src.write_text(fixture, encoding="utf-8")
    subprocess.run(
        [shutil.which("gcc") or "gcc", "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror", str(src), "-o", str(exe)],
        check=True,
    )
    subprocess.run([str(exe)], check=True)
