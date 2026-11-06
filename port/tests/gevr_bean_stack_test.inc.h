/* liftStackedDecals(): Streets' propaganda poster laid over a newspaper on one
 * wall (players, test.17). The later decal whose inside overlaps an earlier
 * one on its plane stands further off the wall, so the depths no longer tie;
 * a poster's own two halves, decals beside each other, decals on another
 * plane and see-through decals are left where they are. */
static void stackQuad(struct gevrBeanMesh *mesh, int tex, float x0, float y0, float x1, float y1, float z, int alpha)
{
    const float p[2][9] = {{x0, y0, z, x1, y0, z, x1, y1, z}, {x0, y0, z, x1, y1, z, x0, y1, z}};
    for (int i = 0; i < 2; i++) {
        struct gevrBeanTri *t = &mesh->tris[mesh->ntris++];
        memset(t, 0, sizeof(*t));
        memcpy(t->p, p[i], sizeof(t->p));
        t->tex = (int16_t)tex;
        t->alpha = (uint8_t)alpha;
        t->flags = GEVR_BEAN_F_DECAL;
    }
}

static float stackZ(const struct gevrBeanMesh *mesh, int tri)
{
    return mesh->tris[tri].p[2];
}

static void stackChecks(void)
{
    struct gevrBeanMesh mesh = {0};
    mesh.tris = calloc(16, sizeof(*mesh.tris));
    assert(mesh.tris);
    stackQuad(&mesh, 0, 0, 0, 20, 30, 100, 255);    /* 0-1: the newspaper, first */
    stackQuad(&mesh, 1, 15, 5, 35, 35, 100, 255);   /* 2-3: the poster over its right edge */
    stackQuad(&mesh, 2, 50, 0, 70, 30, 100, 255);   /* 4-5: a poster beside them */
    stackQuad(&mesh, 3, 0, 0, 20, 30, 300, 255);    /* 6-7: one on another wall's plane */
    stackQuad(&mesh, 4, 0, 0, 20, 30, 100, 128);    /* 8-9: a see-through one (no depth write) */
    stackQuad(&mesh, 5, 69.99f, 0, 90, 30, 100, 255); /* 10-11: touching the one beside */
    const int lifted = liftStackedDecals(&mesh);
    /* the poster's two halves both overlap the newspaper: lifted toward its front (+z here) */
    assert(lifted == 2);
    assert(fabsf(stackZ(&mesh, 2) - 100) > 0.4f && fabsf(stackZ(&mesh, 3) - 100) > 0.4f);
    assert(stackZ(&mesh, 2) == stackZ(&mesh, 3) || fabsf(stackZ(&mesh, 2) - stackZ(&mesh, 3)) < 1e-3f);
    for (int i = 0; i < 2; i++) assert(stackZ(&mesh, i) == 100);        /* the first stays on the wall */
    for (int i = 4; i < 6; i++) assert(stackZ(&mesh, i) == 100);        /* beside: no overlap */
    for (int i = 6; i < 8; i++) assert(stackZ(&mesh, i) == 300);        /* another plane */
    for (int i = 8; i < 12; i++) assert(stackZ(&mesh, i) == 100);       /* see-through; only touching */
    free(mesh.tris);
}
