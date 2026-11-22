#include "gevr_jedi_ninja.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void test_palette_and_metrics(void)
{
    for (int c = 0; c < GEVR_JN_COLOR_COUNT; c++) {
        unsigned int core = 0, glow = 0;
        gevrJnSaberPalette(c, &core, &glow);
        assert((core & 0xFFu) == 0xFFu);
        assert((glow & 0xFFu) > 0u);
    }
    assert(gevrJnWeaponReachCm(GEVR_JN_JEDI, 0) > gevrJnWeaponReachCm(GEVR_JN_NINJA, GEVR_JN_SWORD_SHORT));
    assert(gevrJnWeaponReachCm(GEVR_JN_NINJA, GEVR_JN_SWORD_KATANA) > gevrJnWeaponReachCm(GEVR_JN_NINJA, GEVR_JN_SWORD_SHORT));
    assert(gevrJnIsDualSword(GEVR_JN_NINJA, GEVR_JN_SWORD_DUAL_SHORT));
    assert(gevrJnIsDualSword(GEVR_JN_NINJA, GEVR_JN_SWORD_DUAL_GOLD_SHORT));
    assert(!gevrJnIsDualSword(GEVR_JN_NINJA, GEVR_JN_SWORD_KATANA));
    assert(GEVR_JN_STYLE_COUNT == 3);
    assert(GEVR_JN_NINJA_SLOT_SHADOW < GEVR_JN_NINJA_SLOT_COUNT);
    assert(GEVR_JN_NINJA_SLOT_COUNT == 6);
    assert(gevrJnIsGoldSword(GEVR_JN_SWORD_GOLD_KATANA));
    assert(gevrJnIsGoldSword(GEVR_JN_SWORD_GOLD_SHORT));
    assert(gevrJnIsGoldSword(GEVR_JN_SWORD_DUAL_GOLD_SHORT));
    assert(!gevrJnIsGoldSword(GEVR_JN_SWORD_SHORT));

    assert(fabsf(gevrJnSithScaleFactor(GEVR_JN_SCALE_NORMAL, 1) - 1.0f) < 1e-4f);
    assert(gevrJnSithScaleFactor(GEVR_JN_SCALE_GIANT, 1) > 1.2f);
    assert(gevrJnSithScaleFactor(GEVR_JN_SCALE_TINY, 1) < 0.7f);
    int seenTiny = 0, seenNormal = 0, seenGiant = 0;
    for (unsigned int s = 0; s < 64; s++) {
        float sc = gevrJnSithScaleFactor(GEVR_JN_SCALE_RANDOM, s);
        if (sc < 0.7f) seenTiny = 1;
        else if (sc > 1.2f) seenGiant = 1;
        else seenNormal = 1;
    }
    assert(seenTiny && seenNormal && seenGiant);
}

static void test_triggers_and_slash(void)
{
    GevrJnTrigger t;
    gevrJnTriggerReset(&t);

    /* Single quick tap emits TAP immediately on release */
    assert(gevrJnTriggerUpdate(&t, 1, 0.016f) == GEVR_JN_TRIG_NONE);
    assert(gevrJnTriggerUpdate(&t, 1, 0.050f) == GEVR_JN_TRIG_NONE);
    assert(gevrJnTriggerUpdate(&t, 0, 0.016f) == GEVR_JN_TRIG_TAP);

    /* Wait past double-tap window -> fresh press does not double-tap */
    assert(gevrJnTriggerUpdate(&t, 0, 0.35f) == GEVR_JN_TRIG_NONE);
    assert(gevrJnTriggerUpdate(&t, 1, 0.016f) == GEVR_JN_TRIG_NONE);
    assert(gevrJnTriggerUpdate(&t, 0, 0.016f) == GEVR_JN_TRIG_TAP);

    /* Second quick press within 0.28s emits DOUBLE_TAP, release emits NONE */
    assert(gevrJnTriggerUpdate(&t, 1, 0.05f) == GEVR_JN_TRIG_DOUBLE_TAP);
    assert(gevrJnTriggerUpdate(&t, 1, 0.30f) == GEVR_JN_TRIG_NONE);
    assert(gevrJnTriggerUpdate(&t, 0, 0.016f) == GEVR_JN_TRIG_NONE);

    /* Hold >= 0.22s emits HOLD_START, HOLD_ACTIVE, then HOLD_RELEASE */
    gevrJnTriggerReset(&t);
    assert(gevrJnTriggerUpdate(&t, 1, 0.016f) == GEVR_JN_TRIG_NONE);
    assert(gevrJnTriggerUpdate(&t, 1, 0.10f) == GEVR_JN_TRIG_NONE);
    assert(gevrJnTriggerUpdate(&t, 1, 0.10f) == GEVR_JN_TRIG_NONE);
    assert(gevrJnTriggerUpdate(&t, 1, 0.05f) == GEVR_JN_TRIG_HOLD_START);
    assert(gevrJnTriggerUpdate(&t, 1, 0.05f) == GEVR_JN_TRIG_HOLD_ACTIVE);
    assert(gevrJnTriggerUpdate(&t, 0, 0.016f) == GEVR_JN_TRIG_HOLD_RELEASE);

    /* Alternating slash: blocked while blocking=1, alternates R->L / L->R otherwise */
    GevrJnSlash s = {0};
    assert(gevrJnSlashStart(&s, 1) == 0);
    assert(gevrJnSlashStart(&s, 0) == 1);
    assert(s.dir == 0);
    assert(gevrJnSlashStart(&s, 0) == 0); /* cannot start while active */
    assert(gevrJnSlashStep(&s, 0.13f) > 0.45f);
    assert(gevrJnSlashStep(&s, 0.20f) == 1.0f);
    assert(gevrJnSlashStep(&s, 0.016f) < 0.0f);
    assert(gevrJnSlashStart(&s, 0) == 1);
    assert(s.dir == 1);
}

static void test_vr_wrist_and_ignition(void)
{
    const float fwd[3] = { 0.0f, 0.0f, -1.0f };
    const float throwVel[3] = { 0.0f, 0.0f, -240.0f };
    const float pullVel[3] = { 0.0f, 0.0f, 200.0f };
    assert(gevrJnVrWristThrow(throwVel, fwd, 1) == 1);
    assert(gevrJnVrWristThrow(throwVel, fwd, 0) == 0);
    assert(gevrJnVrWristThrow(pullVel, fwd, 1) == 0);
    assert(gevrJnVrWristPull(pullVel, fwd, 1) == 1);
    assert(gevrJnVrWristPull(pullVel, fwd, 0) == 0);

    /* 1P ignition animates once, subsequent 1P re-equips snap to 1.0 */
    GevrJnIgnition fp = {0};
    float ext = gevrJnIgnitionUpdate(&fp, 1, 1, 0.21f);
    assert(ext > 0.45f && ext < 0.55f);
    ext = gevrJnIgnitionUpdate(&fp, 1, 1, 0.25f);
    assert(fabsf(ext - 1.0f) < 1e-5f);
    assert(gevrJnIgnitionUpdate(&fp, 0, 1, 0.016f) == 0.0f);
    assert(gevrJnIgnitionUpdate(&fp, 1, 1, 0.016f) == 1.0f);

    /* NPC / MP ignition replays every draw */
    GevrJnIgnition npc = {0};
    assert(gevrJnIgnitionUpdate(&npc, 1, 0, 0.21f) < 0.6f);
    assert(gevrJnIgnitionUpdate(&npc, 1, 0, 0.25f) == 1.0f);
    assert(gevrJnIgnitionUpdate(&npc, 0, 0, 0.016f) == 0.0f);
    assert(gevrJnIgnitionUpdate(&npc, 1, 0, 0.10f) < 0.35f);
}

static void test_thrown_saber_and_boomerang(void)
{
    GevrJnThrown th;
    float owner[3] = { 0.0f, 150.0f, 0.0f };
    const float aim[3] = { 0.0f, 0.0f, -1.0f };

    /* Lightsaber: straight out up to 1200 cm, steers back to moved owner */
    gevrJnThrownLaunch(&th, 0, owner, aim);
    assert(th.active && !th.returning);
    for (int i = 0; i < 200; i++) {
        int caught = gevrJnThrownStep(&th, owner, 0, 0.02f);
        if (i < 65) assert(!caught);
        if (th.returning) {
            owner[0] = 200.0f; /* strafe right while returning */
        }
        if (caught) break;
    }
    assert(!th.active);
    assert(fabsf(th.pos[0] - 200.0f) < 60.0f);

    /* Wall bounce turns immediately into returning */
    gevrJnThrownLaunch(&th, 0, owner, aim);
    assert(gevrJnThrownStep(&th, owner, 1, 0.02f) == 0);
    assert(th.returning == 1);

    /* Boomerang reaches ~1400 cm and returns cleanly */
    owner[0] = 0.0f;
    gevrJnThrownLaunch(&th, 1, owner, aim);
    float maxZ = 0.0f;
    int caught = 0;
    for (int i = 0; i < 300 && !caught; i++) {
        caught = gevrJnThrownStep(&th, owner, 0, 0.02f);
        if (-th.pos[2] > maxZ) maxZ = -th.pos[2];
    }
    assert(caught && maxZ >= 1350.0f);
}

static void test_choke_limbs_and_thrown_body(void)
{
    const float origin[3] = { 10.0f, 150.0f, 20.0f };
    const float fwd[3] = { 0.0f, 0.0f, -1.0f };
    float choke[3];
    gevrJnChokeTargetPos(origin, fwd, choke);
    assert(fabsf(choke[0] - 10.0f) < 1e-3f);
    assert(fabsf(choke[1] - 165.0f) < 1e-3f);
    assert(fabsf(choke[2] - (20.0f - GEVR_JN_CHOKE_DIST_CM)) < 1e-3f);

    /* 24 limbs orbit cleanly without NaN or duplicates */
    float pts[GEVR_JN_MAX_LIMBS][3];
    for (int i = 0; i < GEVR_JN_MAX_LIMBS; i++) {
        gevrJnLimbOrbitPos(choke, i, GEVR_JN_MAX_LIMBS, 0.5f, pts[i]);
        for (int k = 0; k < 3; k++) assert(isfinite(pts[i][k]));
        float dx = pts[i][0] - choke[0], dy = pts[i][1] - choke[1], dz = pts[i][2] - choke[2];
        float d = sqrtf(dx * dx + dy * dy + dz * dz);
        assert(d >= 18.0f && d <= 45.0f);
        for (int j = 0; j < i; j++) {
            float ddx = pts[i][0] - pts[j][0];
            float ddy = pts[i][1] - pts[j][1];
            float ddz = pts[i][2] - pts[j][2];
            assert(ddx * ddx + ddy * ddy + ddz * ddz > 1.0f);
        }
    }
    assert(gevrJnLimbBlastDamage(150.0f) >= 100.0f);
    assert(gevrJnLimbBlastDamage(500.0f) > gevrJnLimbBlastDamage(1000.0f));
    assert(gevrJnLimbBlastDamage(1300.0f) == 0.0f);

    /* Thrown body: dismembers at most 2 close NPCs, then ragdoll-kills/knocks down */
    GevrJnThrownBody tb;
    float knock = 0.0f;
    gevrJnThrownBodyInit(&tb, 80.0f, 1600.0f);
    assert(gevrJnThrownBodyImpact(&tb, 100.0f, &knock) == GEVR_JN_IMPACT_DISMEMBER);
    assert(knock > 500.0f);
    assert(gevrJnThrownBodyImpact(&tb, 150.0f, &knock) == GEVR_JN_IMPACT_DISMEMBER);
    assert(gevrJnThrownBodyImpact(&tb, 180.0f, &knock) == GEVR_JN_IMPACT_RAGDOLL_KILL);
    assert(tb.dismemberedCount == 2);

    /* Thrown body past arm's reach does not dismember */
    gevrJnThrownBodyInit(&tb, 80.0f, 1600.0f);
    assert(gevrJnThrownBodyImpact(&tb, 450.0f, &knock) == GEVR_JN_IMPACT_RAGDOLL_KILL);
}

static void test_block_and_deflect(void)
{
    const float fwd[3] = { 0.0f, 0.0f, -1.0f };
    const float shooterFront[3] = { 0.1f, 0.0f, -0.99f };
    const float shooterBack[3] = { 0.0f, 0.0f, 1.0f };
    float refl[3] = {0};

    assert(gevrJnCheckBlock(GEVR_JN_JEDI, 0, 0, fwd, shooterFront, refl) == GEVR_JN_BLOCK_NONE);
    assert(gevrJnCheckBlock(GEVR_JN_JEDI, 1, 0, fwd, shooterBack, refl) == GEVR_JN_BLOCK_NONE);
    assert(gevrJnCheckBlock(GEVR_JN_JEDI, 1, 0, fwd, shooterFront, refl) == GEVR_JN_BLOCK_DEFLECT);
    assert(refl[2] < -0.9f);
    assert(gevrJnCheckBlock(GEVR_JN_NINJA, 1, 0, fwd, shooterFront, refl) == GEVR_JN_BLOCK_ABSORB);
    assert(gevrJnCheckBlock(GEVR_JN_NINJA, 1, 1, fwd, shooterFront, refl) == GEVR_JN_BLOCK_DEFLECT);

    int rt = 0, lt = 0, mouse = 0;
    gevrJnInputFeedFlat(1, 0, 1);
    gevrJnInputReadFlat(&rt, &lt, &mouse);
    assert(rt == 1 && lt == 0 && mouse == 1);

    /* Corridor boomerang reflection */
    const float v[3] = { 10.0f, 0.0f, -10.0f };
    const float wallN[3] = { -1.0f, 0.0f, 0.0f };
    float bounce[3] = {0};
    gevrJnReflectVec(v, wallN, bounce);
    assert(fabsf(bounce[0] + 10.0f) < 1e-4f);
    assert(fabsf(bounce[1]) < 1e-4f);
    assert(fabsf(bounce[2] + 10.0f) < 1e-4f);

    /* Bullet travel vector (shooter -> player) must invert to shooter direction */
    const float zeroVec[3] = { 0.0f, 0.0f, 0.0f };
    const float frontTravel[3] = { 0.0f, 0.0f, 1.0f };
    const float rearTravel[3] = { 0.0f, 0.0f, -1.0f };
    float toShooter[3] = {0};
    assert(gevrJnBulletTravelToShooter(zeroVec, toShooter) == 0);
    assert(gevrJnBulletTravelToShooter(frontTravel, toShooter) == 1);
    assert(fabsf(toShooter[0]) < 1e-4f && fabsf(toShooter[1]) < 1e-4f && fabsf(toShooter[2] + 1.0f) < 1e-4f);
    assert(gevrJnCheckBlock(GEVR_JN_JEDI, 1, 0, fwd, toShooter, refl) == GEVR_JN_BLOCK_DEFLECT);
    assert(refl[2] < -0.9f);
    assert(gevrJnBulletTravelToShooter(rearTravel, toShooter) == 1);
    assert(fabsf(toShooter[0]) < 1e-4f && fabsf(toShooter[1]) < 1e-4f && fabsf(toShooter[2] - 1.0f) < 1e-4f);
    assert(gevrJnCheckBlock(GEVR_JN_JEDI, 1, 0, fwd, toShooter, refl) == GEVR_JN_BLOCK_NONE);

    /* Tracked 3D VR blade / shield orientation blocking */
    const float bladeAcross[3] = { 0.0f, 1.0f, 0.0f };
    const float bladeInline[3] = { 0.0f, 0.0f, -1.0f };
    const float toFront[3] = { 0.0f, 0.0f, -1.0f };
    const float toRear[3] = { 0.0f, 0.0f, 1.0f };
    const float aimFwd[3] = { 0.0f, 0.0f, -1.0f };
    const float shieldAim[3] = { 0.2f, 0.0f, -0.98f };
    const float shieldSide[3] = { 1.0f, 0.0f, 0.0f };
    assert(gevrJnCheckVrBladeBlock(GEVR_JN_JEDI, 0, 0, fwd, bladeAcross, aimFwd, toFront, refl) == GEVR_JN_BLOCK_NONE);
    assert(gevrJnCheckVrBladeBlock(GEVR_JN_JEDI, 1, 0, fwd, bladeAcross, aimFwd, toFront, refl) == GEVR_JN_BLOCK_DEFLECT);
    assert(refl[2] < -0.9f);
    assert(gevrJnCheckVrBladeBlock(GEVR_JN_JEDI, 1, 0, fwd, bladeInline, aimFwd, toFront, refl) == GEVR_JN_BLOCK_NONE);
    assert(gevrJnCheckVrBladeBlock(GEVR_JN_JEDI, 1, 0, fwd, bladeAcross, aimFwd, toRear, refl) == GEVR_JN_BLOCK_NONE);
    assert(gevrJnCheckVrBladeBlock(GEVR_JN_NINJA, 1, 1, fwd, bladeAcross, shieldAim, toFront, refl) == GEVR_JN_BLOCK_DEFLECT);
    assert(refl[2] < -0.9f);
    assert(gevrJnCheckVrBladeBlock(GEVR_JN_NINJA, 1, 1, fwd, bladeAcross, shieldSide, toFront, refl) == GEVR_JN_BLOCK_NONE);
    assert(gevrJnCheckVrBladeBlock(GEVR_JN_NINJA, 1, 0, fwd, bladeAcross, aimFwd, toFront, refl) == GEVR_JN_BLOCK_ABSORB);

    /* Tracked 3D blade segment vs NPC upright cylinder hit tests */
    const float cylBase[3] = { 0.0f, 0.0f, -100.0f };
    float hitPt[3] = {0};
    const float hSegA[3] = { -60.0f, 100.0f, -100.0f }, hSegB[3] = { 60.0f, 100.0f, -100.0f };
    assert(gevrJnSegmentHitCylinder(hSegA, hSegB, cylBase, 40.0f, 175.0f, hitPt) == 1);
    assert(fabsf(hitPt[0]) < 1e-3f && fabsf(hitPt[1] - 100.0f) < 1e-3f && fabsf(hitPt[2] + 100.0f) < 1e-3f);
    const float vSegA[3] = { 10.0f, -50.0f, -100.0f }, vSegB[3] = { 10.0f, 250.0f, -100.0f };
    assert(gevrJnSegmentHitCylinder(vSegA, vSegB, cylBase, 40.0f, 175.0f, hitPt) == 1);
    assert(hitPt[1] >= -12.01f && hitPt[1] <= 187.01f);
    const float outRadA[3] = { 60.0f, 100.0f, -100.0f }, outRadB[3] = { 120.0f, 100.0f, -100.0f };
    assert(gevrJnSegmentHitCylinder(outRadA, outRadB, cylBase, 40.0f, 175.0f, hitPt) == 0);
    const float hiA[3] = { -60.0f, 220.0f, -100.0f }, hiB[3] = { 60.0f, 220.0f, -100.0f };
    assert(gevrJnSegmentHitCylinder(hiA, hiB, cylBase, 40.0f, 175.0f, hitPt) == 0);
    const float loA[3] = { -60.0f, -40.0f, -100.0f }, loB[3] = { 60.0f, -40.0f, -100.0f };
    assert(gevrJnSegmentHitCylinder(loA, loB, cylBase, 40.0f, 175.0f, hitPt) == 0);
}

static void test_procedural_audio(void)
{
    short buf[1024];

    /* Inactive with no triggers leaves buffer untouched */
    gevrJnAudioReset();
    memset(buf, 0, sizeof(buf));
    gevrJnAudioMix(buf, 512, 22050);
    for (int i = 0; i < 1024; i++) assert(buf[i] == 0);

    /* Active Jedi saber hum + swing Doppler + Force Lightning + thrown boomerang produces bounded nonzero PCM */
    gevrJnAudioSetState(1, 1, 1.0f, 0.9f, 1, 1, 1, 1);
    memset(buf, 0, sizeof(buf));
    gevrJnAudioMix(buf, 512, 22050);
    long long energy = 0;
    for (int i = 0; i < 1024; i++) {
        assert(buf[i] >= -32767 && buf[i] <= 32767);
        energy += abs(buf[i]);
    }
    assert(energy > 50000);

    /* Every one-shot procedural SFX produces bounded nonzero energy and decays cleanly */
    for (int sfx = GEVR_JN_SFX_IGNITE; sfx <= GEVR_JN_SFX_SHADOW_STEP; sfx++) {
        gevrJnAudioReset();
        gevrJnAudioTrigger(sfx, 1.0f);
        memset(buf, 0, sizeof(buf));
        gevrJnAudioMix(buf, 512, 22050);
        long long e = 0;
        for (int i = 0; i < 1024; i++) {
            assert(buf[i] >= -32767 && buf[i] <= 32767);
            e += abs(buf[i]);
        }
        assert(e > 10000);
        for (int pass = 0; pass < 30; pass++) {
            memset(buf, 0, sizeof(buf));
            gevrJnAudioMix(buf, 512, 22050);
        }
        memset(buf, 0, sizeof(buf));
        gevrJnAudioMix(buf, 512, 22050);
        for (int i = 0; i < 1024; i++) assert(buf[i] == 0);
    }
}

int main(void)
{
    test_palette_and_metrics();
    test_triggers_and_slash();
    test_vr_wrist_and_ignition();
    test_thrown_saber_and_boomerang();
    test_choke_limbs_and_thrown_body();
    test_block_and_deflect();
    test_procedural_audio();
    puts("jedi & ninja core: all passed");
    return 0;
}
