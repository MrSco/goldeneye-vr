"""A destroyed prop never draws whole again (Statue's helicopter, 2026-10-27).
The store sizing is exercised natively (vtxstore_native.c, test_wreck_store)."""
from pathlib import Path
import re
import unittest

root = Path(__file__).resolve().parents[2]


def function(path, signature):
    source = (root / path).read_text(encoding="utf-8")
    start = source.index(signature)
    opening = source.index("{", start)
    depth, end = 1, opening + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end].replace("\r\n", "\n")


class WreckDraw(unittest.TestCase):
    def test_wreck_shot_again_is_kept_when_the_store_is_full(self):
        deform = function("src/game/propobj.c", "void objDeform(ObjectRecord *obj, E_EXPLOSIONTYPE explosiontype)")
        failed = deform[deform.index("    else\n    {", deform.index("newverts = vtxstore_allocate")):]
        keep = failed.index("if ((*vtxslot) != rodata->Vertices)\n        {\n            return;")
        self.assertLess(keep, failed.index("*vtxslot = rodata->Vertices;"))
        self.assertLess(failed.index("#ifdef GEVR"), keep)

    def test_bean_wreck_falls_back_to_the_game_wreck(self):
        node = function("src/game/model.c", "void modelRenderNodeDl(ModelRenderData *renderdata, Model *model, ModelNode *node)")
        self.assertIn("vtxstoreIsWreckCopy(rwdata->DisplayListCollisions.Vertices)", node)
        fallback = node[node.index("if (scorched)"):node.index("#else")]
        self.assertIn("shownGdl = rwdata->DisplayListCollisions.gdl;", fallback)
        self.assertIn("shownType = rodata->DisplayListCollisions.ModelType;", fallback)
        self.assertIn("nodeType = shownType;", fallback)

    def test_scorched_list_never_returns_the_whole_prop(self):
        scorched = function("port/src/gevr_bean_pose.inc.h", "void *gevrBeanScorchedGdl(")
        self.assertNotIn("shown", scorched)
        self.assertEqual(re.findall(r"return (\w+);", scorched), ["NULL", "NULL", "NULL", "list", "head"])

    def test_no_bounded_list_of_wrecks(self):
        bean = (root / "port/src/gevr_bean.c").read_text(encoding="utf-8")
        self.assertNotIn("MAX_DEFORMED", bean)


if __name__ == "__main__":
    unittest.main()
