/* The Blood patch's splats: PD's sizes, the quad's sides and the growth
 * (src/game/gevr_blood.c between BEGIN_BLOOD_GEOMETRY and END_BLOOD_GEOMETRY,
 * inserted by test_blood_splats.py). */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

/* INSERT_BLOOD_GEOMETRY */

static int s_failed;

static void near(const char *what, float got, float want)
{
    if (fabsf(got - want) > 1e-3f) {
        printf("FAIL %s: %f, want %f\n", what, got, want);
        s_failed = 1;
    }
}

static float dot(const float a[3], const float b[3]) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }

int main(void)
{
    float w, h, a[3], u[3];
    int i;

    /* 50 units at the small size: 0.15 * 50 * 1.5, the hit's 0.8 on it */
    bloodSplatSize(50.0f, 0.8f, 0, 0.5f, 0.5f, &w, &h);
    near("hit w", w, 11.25f * 0.8f);
    near("hit h", h, 11.25f * 0.8f);
    /* the sides vary by half the size either way */
    bloodSplatSize(50.0f, 1.0f, 0, 0.0f, 1.0f, &w, &h);
    near("narrow w", w, 11.25f * 0.5f);
    near("wide h", h, 11.25f * 1.5f);
    /* the big size, one in three */
    bloodSplatSize(50.0f, 1.0f, 3, 0.5f, 0.5f, &w, &h);
    near("big", w, 37.5f);
    /* close: never under 5 */
    bloodSplatSize(10.0f, 1.0f, 0, 0.5f, 0.5f, &w, &h);
    near("min", w, 5.0f);
    /* far and big: 50 at most before the pool's 1.1 */
    bloodSplatSize(170.0f, 1.1f, 4, 1.0f, 1.0f, &w, &h);
    near("max", w, 55.0f);

    /* a floor (the room normal is not unit length): across +x, up +z */
    {
        const float floor[3] = { 0.0f, 5020.0f, 0.0f };
        bloodSurfaceAxes(floor, 0.0f, a, u);
        near("floor ax", a[0], 1.0f); near("floor az", a[2], 0.0f);
        near("floor uz", u[2], 1.0f); near("floor uy", u[1], 0.0f);
        bloodSurfaceAxes(floor, 90.0f, a, u);
        near("floor turned ax", a[2], 1.0f);
        near("floor turned ux", u[0], -1.0f);
    }
    /* any surface, any turn: two unit sides on it, square to each other */
    for (i = 0; i < 200; i++) {
        const float n[3] = { (float)(rand() % 2001 - 1000), (float)(rand() % 2001 - 1000), (float)(rand() % 2001 - 1000) };
        const float len = sqrtf(dot(n, n));
        if (len < 1.0f) continue;
        bloodSurfaceAxes(n, (float)(rand() % 360), a, u);
        near("across unit", dot(a, a), 1.0f);
        near("up unit", dot(u, u), 1.0f);
        near("square", dot(a, u), 0.0f);
        near("across on it", dot(a, n) / len, 0.0f);
        near("up on it", dot(u, n) / len, 0.0f);
    }

    /* wallhit.c's growth */
    near("grow 0", bloodGrowScale(0.0f), 0.2f);
    near("grow 1", bloodGrowScale(1.0f), 1.0f);
    near("grow past", bloodGrowScale(3.0f), 1.0f);
    near("alpha 0", bloodGrowAlpha(0.0f), 0.0f);
    near("alpha quarter", bloodGrowAlpha(0.25f), 0.5f);
    near("alpha half", bloodGrowAlpha(0.5f), 1.0f);

    if (s_failed) return 1;
    printf("blood splats ok\n");
    return 0;
}
