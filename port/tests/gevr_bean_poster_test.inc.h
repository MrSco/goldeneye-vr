/* fixFlatPosters(): Streets' wall at x 1750 has three posters whose every
 * corner Rare left on one point of the picture (red and blue-grey cards,
 * 2026-11-05). Such a face takes the picture as a whole copy on another wall
 * lays it, over its own extent; a picture the level also lays as anything but
 * a decal is not a poster and is left alone. */
static void posterTri(struct gevrBeanTri *t, int tex, int decal, const float p[9], const float uv[6])
{
    memset(t, 0, sizeof(*t));
    memcpy(t->p, p, sizeof(t->p));
    memcpy(t->uv, uv, sizeof(t->uv));
    t->tex = (int16_t)tex;
    t->flags = decal ? GEVR_BEAN_F_DECAL : 0;
}

static void posterChecks(void)
{
    /* the copy laid whole, facing -z (the Streets copy at z 3744) */
    static const float donorP[2][9] = {{0, 28, 0, 17, 28, 0, 17, 0, 0}, {0, 28, 0, 17, 0, 0, 0, 0, 0}};
    static const float donorUv[2][6] = {{1, 1, 0, 1, 0, 2}, {1, 1, 0, 2, 1, 2}};
    /* the flat copy, facing -x, as at x 1750 */
    static const float flatP[2][9] = {{100, 28, 17, 100, 28, 0, 100, 0, 0}, {100, 28, 17, 100, 0, 0, 100, 0, 17}};
    static const float flatUv[2][6] = {{0.035f, 0.836f, 0.035f, 0.836f, 0.035f, 0.871f},
                                       {0.035f, 0.836f, 0.035f, 0.871f, 0.035f, 0.871f}};
    static const float want[2][6] = {{1, 1, 0, 1, 0, 2}, {1, 1, 0, 2, 1, 2}};
    struct gevrBeanMesh mesh = {0};
    mesh.ntex = 2;
    mesh.tris = calloc(9, sizeof(*mesh.tris));
    assert(mesh.tris);
    for (int tex = 0; tex < 2; tex++)
        for (int i = 0; i < 2; i++) {
            float p[9];
            memcpy(p, donorP[i], sizeof(p));
            for (int c = 0; c < 3; c++) p[c * 3 + 2] += tex * 500;
            posterTri(&mesh.tris[mesh.ntris++], tex, 1, p, donorUv[i]);
            memcpy(p, flatP[i], sizeof(p));
            for (int c = 0; c < 3; c++) p[c * 3 + 2] += tex * 500;
            posterTri(&mesh.tris[mesh.ntris++], tex, 1, p, flatUv[i]);
        }
    /* picture 1 is also a wall's own face somewhere: not a poster */
    posterTri(&mesh.tris[mesh.ntris++], 1, 0, donorP[0], donorUv[0]);
    assert(fixFlatPosters(&mesh) == 2);
    for (int i = 0; i < 2; i++)
        for (int k = 0; k < 6; k++)
            assert(fabsf(mesh.tris[1 + i * 2].uv[k] - want[i][k]) < 1e-3f);
    for (int i = 0; i < 2; i++)
        assert(!memcmp(mesh.tris[5 + i * 2].uv, flatUv[i], sizeof(flatUv[i])));
    free(mesh.tris);
}
