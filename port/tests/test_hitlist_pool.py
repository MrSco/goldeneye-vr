"""An empty hit-entry pool still builds a list for a model that has hit nodes.

Report ee07735d: chrTestHit had already accepted the bounding sphere, then
walked a NULL field_20. sub_GAME_7F06B120 returns no list only when it takes
no entries. A header root is an entry, so the free list was empty. The
production walk now allocates a further chunk instead of stopping.
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
extend = function(source, "static ModelHitEntry *modelHitEntryExtend(void)")
release = function(source, "void modelHitEntryReleaseOverflow(void)")
build = function(source, "ModelHitEntry* sub_GAME_7F06B120(ModelHitEntry* head, Model* context)")
assert "modelHitEntryExtend" in build
assert "freeListCursor == NULL" in build

fixture = r"""
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct Model Model;
typedef struct ModelNode ModelNode;
typedef int s32;

typedef struct ModelHitEntry {
    struct Model *model;
    struct ModelNode *rootnode;
    float sortvalue;
    struct ModelHitEntry *next;
    struct ModelHitEntry *prev;
} ModelHitEntry;

struct ModelNode {
    unsigned short Opcode;
    struct ModelNode *Parent;
    struct ModelNode *Next;
    struct ModelNode *Child;
};

struct ModelObj {
    struct ModelNode *RootNode;
};

struct Model {
    struct ModelObj *obj;
};

#define MODELHIT_EXTEND_COUNT 128
typedef struct ModelHitChunk {
    struct ModelHitChunk *next;
    ModelHitEntry entries[MODELHIT_EXTEND_COUNT];
} ModelHitChunk;

static ModelHitChunk *g_ModelHitChunks;
static ModelHitEntry *g_ModelHitFreeList;

""" + extend + "\n" + release + "\n" + build + r"""

static void expect(int cond, const char *what)
{
    if (!cond) {
        fprintf(stderr, "fail: %s\n", what);
        exit(1);
    }
}

static int length(ModelHitEntry *entry)
{
    int n = 0;
    while (entry != NULL) {
        n++;
        entry = entry->next;
    }
    return n;
}

static void thread(ModelHitEntry *entries, int n)
{
    int i;
    memset(entries, 0, (size_t)n * sizeof(*entries));
    if (n == 0) {
        g_ModelHitFreeList = NULL;
        return;
    }
    for (i = 0; i < n - 1; i++) {
        entries[i].next = &entries[i + 1];
        entries[i + 1].prev = &entries[i];
    }
    g_ModelHitFreeList = entries;
}

static void chain(ModelNode *nodes, int n)
{
    int i;
    memset(nodes, 0, (size_t)n * sizeof(*nodes));
    for (i = 0; i < n; i++) {
        nodes[i].Opcode = 1; /* HEADER, a hit entry */
        if (i + 1 < n) nodes[i].Child = &nodes[i + 1];
    }
}

int main(void)
{
    ModelHitEntry pool[4];
    ModelNode nodes[4];
    struct ModelObj obj;
    Model model;
    ModelHitEntry *list;
    ModelHitEntry *cursor;
    int i;

    obj.RootNode = NULL;
    model.obj = &obj;
    g_ModelHitFreeList = NULL;
    list = sub_GAME_7F06B120(NULL, &model);
    expect(list == NULL, "no root stays a null list");
    expect(g_ModelHitFreeList == NULL, "no root does not allocate");

    thread(pool, 3);
    chain(nodes, 2);
    obj.RootNode = &nodes[0];
    list = sub_GAME_7F06B120(NULL, &model);
    expect(length(list) == 2, "two hit nodes take two entries");
    expect(list == &pool[0] && list->next == &pool[1], "those entries are the static pool");
    expect(g_ModelHitFreeList == &pool[2], "the unused static entry stays free");

    thread(pool, 2);
    chain(nodes, 4);
    obj.RootNode = &nodes[0];
    list = sub_GAME_7F06B120(NULL, &model);
    expect(length(list) == 4, "a short pool still returns every hit node");
    expect(list == &pool[0] && list->next == &pool[1], "the static entries are used first");
    cursor = list->next->next;
    expect(cursor != &pool[0] && cursor != &pool[1], "later entries come from the extra chunk");
    expect(g_ModelHitFreeList != NULL, "the rest of the chunk stays free");
    for (i = 0; i < 4; i++) {
        expect(list->rootnode == &nodes[i], "entry points at its node");
        list = list->next;
    }

    modelHitEntryReleaseOverflow();
    g_ModelHitFreeList = NULL;
    chain(nodes, 1);
    obj.RootNode = &nodes[0];
    list = sub_GAME_7F06B120(NULL, &model);
    expect(length(list) == 1 && list != NULL, "an empty pool still builds one entry");
    expect(list->rootnode == &nodes[0], "that entry is the header root");

    /* hat and weapon walks append onto the body list (chrTick) */
    {
        ModelHitEntry *body = list;
        ModelNode hat;
        memset(&hat, 0, sizeof(hat));
        hat.Opcode = 1;
        g_ModelHitFreeList = NULL;
        obj.RootNode = &hat;
        list = sub_GAME_7F06B120(body, &model);
        expect(list == body, "append keeps the body head");
        expect(length(list) == 2, "the hat node is added after an empty pool");
        expect(list->next->rootnode == &hat, "the added entry is the hat");
    }

    modelHitEntryReleaseOverflow();
    return 0;
}
"""

with tempfile.TemporaryDirectory(prefix="gevr-hitpool-") as temp:
    temp = Path(temp)
    src, exe = temp / "test.c", temp / "test.exe"
    src.write_text(fixture, encoding="utf-8")
    subprocess.run(
        [shutil.which("gcc") or "gcc", "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror", str(src), "-o", str(exe)],
        check=True,
    )
    subprocess.run([str(exe)], check=True)
