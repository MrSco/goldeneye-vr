/* Native inventory poses leave most joints at identity. Replacement lists
 * load their own pivot matrix, so they all need the preview's root transform. */
int gevrBeanPreviewPose(Model *model, const float root[4][4])
{
    if (!model || !model->obj || !model->render_pos || !root || !gevrBeanIsEnabled()) return 0;
    int found = 0;
    for (int i = 0; i < s_nswap; i++) {
        const struct beanSwap *s = &s_swap[i];
        if (s->owner != model->obj || s->skin || s->ours == s_blank || activeSwap(s->node) != s) continue;
        int matrix = nodeMatrix(s->node);
        if (matrix < 0 || matrix >= model->obj->numMatrices) continue;
        float rest[3] = {0};
        int steps = 0;
        for (const ModelNode *n = s->node; n && steps++ < MAX_WALK; n = n->Parent) {
            const coord3d *p = !n->Data ? NULL : nodeOp(n) == MODELNODE_OPCODE_GROUP ? &n->Data->Group.Origin :
                nodeOp(n) == MODELNODE_OPCODE_GROUPSIMPLE ? &n->Data->GroupSimple.Origin : NULL;
            for (int k = 0; p && k < 3; k++) rest[k] += p->f[k];
        }
        float (*out)[4] = model->render_pos[matrix].pos.m;
        memcpy(out, root, 16 * sizeof(float));
        for (int k = 0; k < 3; k++)
            for (int j = 0; j < 3; j++) out[3][k] += rest[j] * root[j][k];
        found = 1;
    }
    return found;
}
