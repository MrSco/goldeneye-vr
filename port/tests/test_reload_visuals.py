"""Check magazine-only guide rendering and translucent materials without a ROM."""
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]


def function(source, signature):
    start = source.index(signature)
    end = source.index("{", start) + 1
    depth = 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]


HARNESS = r'''
#include <ultra64.h>
#include <bondtypes.h>
#include <bondconstants.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>
#include "gevr_gexweapon.h"
#include "gevr_hand_reload.h"
float VrReloadGrab[2][3], VrGexForeHold[3], VrGexPp7Grab[3], VrGexPp7Support[3];
float VrGexGunOff[3], VrGexPp7GunOff[3], VrGexKf7MagOff[3], VrGexPp7MagOff[3];
float VrGexWeaponFits[64][10][3], VrGexBatFit[7][3], VrGexKf7WellOff[3], VrGexPp7WellOff[3], VrGexPp7SupportRot[3];
uintptr_t osVirtualToPhysical(void *ptr) { return (uintptr_t)ptr; }
static struct TestPlayer { struct { Model weaponModel; } hands[2]; } player;
static struct TestPlayer *g_CurrentPlayer = &player;
static const GexWeaponDef *def;
static Mtxf s_gevrGexLastMag[2], s_gevrGexLastGun[2], rendered, propPose;
static s32 s_gevrGexLastValid[2] = {1,1};
static int visits, switches, meshes, props;
static ModelNode *expectedMesh;   /* NULL: any */
static s32 expectedMatrix;
static u32 expectedTint = GEVR_RELOAD_TINT|0x50;
static ModelRenderData propRd;
/* gun.c: the grenade launcher's GoldenEye prop, drawn on the held matrix given */
static Gfx *gevrGexDrawProp(Gfx *gdl, ModelRenderData *templ, const GexWeaponDef *d, const Mtxf *held)
{ (void)d; props++; propPose=*held; propRd=*templ; return gdl; }
int gevrHandReloadActive(void) { return 1; }
/* a Bean gun's magazine at the belt (gun.c): GoldenEye X's guns here, so never drawn */
static int beanBelts;
Gfx *gevrBeanMagDrawBelt(Gfx *gdl, ModelRenderData *templ, s32 hand, const Mtxf *beltPose, u32 tint)
{ (void)templ; (void)hand; (void)beltPose; (void)tint; beanBelts++; return gdl; }
const GexWeaponDef *gevrGexWeaponForHand(int hand) { (void)hand; return def; }
static const GexWeaponDef *gevrGexForHeader(ModelFileHeader *hdr) { (void)hdr; return def; }
void *dynAllocate(s32 size) { return calloc(1,size); }
static void matrix_4x4_set_identity(Mtxf *m) {
    memset(m,0,sizeof(*m)); for (int i=0;i<4;i++) m->m[i][i]=1;
}
union ModelRwData *modelGetNodeRwData(Model *m, ModelNode *node) {
    return (union ModelRwData *)(m->datas + node->Data->Switch.RwDataIndex);
}
static void bondviewTransformManyPosToViewMatrix(RenderPosView *pos,s32 count) { (void)pos; (void)count; }
void modelApplyToggleRelations(Model *model,ModelNode *node);
void sub_GAME_7F074534(ModelRenderData *rd,Model *model,ModelNode *node) {
    visits++;
    if (node->Opcode == MODELNODE_OPCODE_SWITCH) { switches++; modelApplyToggleRelations(model,node); }
    else {
        assert(expectedMesh==NULL || node==expectedMesh); meshes++;
        rendered=((Mtxf *)model->render_pos)[expectedMatrix];
        assert(rd->envcolour.word==expectedTint);
    }
}
/* PRODUCTION */
int main(void) {
    def=gevrGexWeaponGet(ITEM_WPPK);
    assert(def && def->numParts>=2);
    expectedMatrix=def->magMatrix;
    ModelNode magSwitch={0}, heldSwitch={0}, magMesh={0}, heldMesh={0}, gunMesh={0};
    union ModelRoData magData={0}, heldData={0};
    ModelNode *sw[64]={&magSwitch,&heldSwitch};
    ModelFileHeader hdr={0}; hdr.Switches=sw; hdr.numSwitches=def->numParts; hdr.numMatrices=64; hdr.numRecords=2;
    u32 original[]={0,1};
    magSwitch.Opcode=heldSwitch.Opcode=MODELNODE_OPCODE_SWITCH;
    magSwitch.Data=&magData; heldSwitch.Data=&heldData;
    magData.Switch.Controls=&magMesh; heldData.Switch.Controls=&heldMesh;
    heldData.Switch.RwDataIndex=1;
    magSwitch.Next=&gunMesh;   /* the bounded traversal must not draw the gun */
    magMesh.Parent=&magSwitch; heldMesh.Parent=&heldSwitch; expectedMesh=&magMesh;
    player.hands[0].weaponModel.obj=&hdr; player.hands[0].weaponModel.datas=original;
    matrix_4x4_set_identity(&s_gevrGexLastMag[0]);
    s_gevrGexLastMag[0].m[0][0]=s_gevrGexLastMag[0].m[1][1]=s_gevrGexLastMag[0].m[2][2]=0.12f;
    s_gevrGexLastMag[0].m[3][0]=12;
    Gfx commands[64]; ModelRenderData rd={0};
    gevrGexDrawMagazineGuide(commands,&rd,0,NULL,GEVR_RELOAD_TINT|0x50);
    assert(visits==2 && switches==1 && meshes==1);
    assert(!memcmp(&rendered,&s_gevrGexLastMag[0],sizeof(Mtxf)));
    assert(original[0]==0 && original[1]==1 && magSwitch.Child==NULL);   /* live magazine stays absent */
    Mtxf belt; matrix_4x4_set_identity(&belt);
    belt.m[0][0]=0; belt.m[0][2]=-1; belt.m[2][0]=1; belt.m[2][2]=0;
    belt.m[3][0]=-22; belt.m[3][1]=-65; belt.m[3][2]=-10;
    gevrGexDrawMagazineGuide(commands,&rd,0,&belt,GEVR_RELOAD_TINT|0x50);
    for(int i=0;i<3;i++) {
        float centre=rendered.m[3][i];
        for(int j=0;j<3;j++) {
            assert(fabsf(rendered.m[j][i]-belt.m[j][i]*0.12f)<0.0001f);
            centre+=def->magCentre[j]*rendered.m[j][i];
        }
        assert(fabsf(centre-belt.m[3][i])<0.0001f);
    }
    rd.gdl=commands; rd.PropType=GEVR_MODEL_RELOAD_GHOST; rd.envcolour.word=GEVR_RELOAD_TINT|0x50; rd.zbufferenabled=1;
    assert(modelApplyReloadGhost(&rd));
    assert(commands[2].words.w1==(GEVR_RELOAD_TINT|0x50));
    assert((commands[4].words.w1 & Z_UPD)==0);   /* transparent guide never writes solid depth */
    assert((commands[4].words.w1 & FORCE_BL)!=0);
    rd.PropType=PROP_TYPE_CHR+1; assert(!modelApplyReloadGhost(&rd));
    rd=(ModelRenderData){0};
    /* a single-round gun's payload at the belt: its held part on the held matrix, at the
     * gun's size, turned as it sits in the gun and centred on the belt point */
    {
        static const f32 at[3]={0.5f,-0.5f,20.5f};
        const u32 tint=GEVR_RELOAD_READY_TINT|0xFF;
        assert(gevrGexDrawPayloadGuide(commands,&rd,0,&belt,tint,1)==commands);   /* a magazine gun: none */
        def=gevrGexWeaponGet(ITEM_SHOTGUN); assert(def->singleRound && def->numParts==2);
        hdr.numSwitches=def->numParts;   /* its switches: {none, the held shell} */
        matrix_4x4_set_identity(&s_gevrGexLastGun[0]);
        for (int i=0;i<3;i++) s_gevrGexLastGun[0].m[i][i]=0.12f;
        assert(gevrGexDrawPayloadGuide(commands,&rd,0,&belt,tint,1)==commands);   /* no payload found: none */
        s_gevrGexPayloadAt[ITEM_SHOTGUN].state=1; memcpy(s_gevrGexPayloadAt[ITEM_SHOTGUN].centre,at,sizeof(at));
        visits=switches=meshes=0; expectedMesh=&heldMesh; expectedMatrix=def->heldMatrix; expectedTint=tint;
        gevrGexDrawPayloadGuide(commands,&rd,0,&belt,tint,1);
        assert(meshes==1 && switches==1);
        for (int i=0;i<3;i++) {
            float c=rendered.m[3][i];
            for (int k=0;k<3;k++) {
                float r=0; for (int a=0;a<3;a++) r+=0.12f*def->heldToMag[k][a]*belt.m[a][i];
                assert(fabsf(rendered.m[k][i]-r)<0.0001f);
                c+=at[k]*rendered.m[k][i];
            }
            assert(fabsf(c-belt.m[3][i])<0.0001f);
        }
        assert(original[0]==0 && original[1]==1 && heldSwitch.Child==NULL);   /* live switches untouched */
    }
    /* the Cougar's speedloader carries the rounds a grip takes, up to its six */
    {
        ModelNode rsw[7]={0}, rmesh[7]={0}, *rtab[64]={0}; union ModelRoData rdata[7]={0}; u32 rdatas[8]={0};
        ModelFileHeader rhdr={0}; rhdr.Switches=rtab; rhdr.numSwitches=7; rhdr.numMatrices=64; rhdr.numRecords=8;
        for (int i=0;i<7;i++) {
            rsw[i].Opcode=MODELNODE_OPCODE_SWITCH; rsw[i].Data=&rdata[i]; rdata[i].Switch.Controls=&rmesh[i];
            rdata[i].Switch.RwDataIndex=i; rmesh[i].Parent=&rsw[i]; rtab[i]=&rsw[i];
        }
        def=gevrGexWeaponGet(ITEM_RUGER); assert(def->loaderRounds==6 && def->numParts==7);
        player.hands[0].weaponModel.obj=&rhdr; player.hands[0].weaponModel.datas=rdatas;
        s_gevrGexPayloadAt[ITEM_RUGER].state=1; expectedMesh=NULL; expectedMatrix=def->heldMatrix;
        for (int r=1;r<=8;r++) { meshes=0; gevrGexDrawPayloadGuide(commands,&rd,0,&belt,expectedTint,r); assert(meshes==(r>6 ? 6 : r)); }
        for (int i=0;i<8;i++) assert(rdatas[i]==0);
        player.hands[0].weaponModel.obj=&hdr; player.hands[0].weaponModel.datas=original;
    }
    /* the grenade launcher's round is its GoldenEye prop, tinted as the belt's magazine */
    {
        def=gevrGexWeaponGet(ITEM_GRENADELAUNCH); assert(def->payloadProp>0);
        s_gevrGexPayloadAt[ITEM_GRENADELAUNCH].state=1;
        meshes=0; gevrGexDrawPayloadGuide(commands,&rd,0,&belt,expectedTint,1);
        assert(props==1 && meshes==0 && propRd.envcolour.word==expectedTint && propRd.PropType==PROP_TYPE_CHR+1);
        for (int i=0;i<3;i++) {
            assert(fabsf(propPose.m[3][i]-belt.m[3][i])<0.0001f);   /* its origin on the belt point */
            for (int k=0;k<3;k++) {
                float r=0; for (int a=0;a<3;a++) r+=0.12f*def->heldToMag[k][a]*belt.m[a][i];
                assert(fabsf(propPose.m[k][i]-r)<0.0001f);
            }
        }
    }
    puts("PASS: magazine subtree only, installed/belt poses, live switches preserved, ghost alpha and translucent depth, belt payloads");
}
'''
gun = (ROOT / "src/game/gun.c").read_text()
model = (ROOT / "src/game/model.c").read_text()
# the belt draws a single-round gun's payload where it draws a magazine
belt = function((ROOT / "src/game/gevr_bodyslots.c").read_text(), "static Gfx *gevrReloadBeltDraw(")
assert "gevrReloadBeltRound(ctrl, &rounds, &hover)" in belt and "gevrGexDrawPayloadGuide(gdl, &rd, hand, &belt, tint, rounds)" in belt
production = [function(model, "void modelApplyToggleRelations("), function(model, "static bool modelApplyReloadGhost(")]
production += [line for line in gun.splitlines()
               if line.startswith(("typedef struct { s8 state; f32 centre[3]; } GevrGexPayloadAt;", "static GevrGexPayloadAt s_gevrGexPayloadAt["))]
production += [function(gun, s) for s in ("static void gevrGexCollapse(", "static Gfx *gevrGexDrawPartGuide(",
                                          "Gfx *gevrGexDrawMagazineGuide(", "Gfx *gevrGexDrawPayloadGuide(")]
with tempfile.TemporaryDirectory(prefix="gevr-reload-visuals-") as temp:
    source, exe = Path(temp) / "test.c", Path(temp) / "test.exe"
    source.write_text(HARNESS.replace("/* PRODUCTION */", "\n".join(production)))
    subprocess.run([shutil.which("gcc") or "gcc", "-std=c11", "-O2", "-D_LANGUAGE_C", "-DGEVR", "-DVERSION_US",
                    "-I"+str(ROOT), "-I"+str(ROOT/"include"), "-I"+str(ROOT/"src"), "-I"+str(ROOT/"port/include"),
                    str(source), str(ROOT/"port/src/gevr_gexweapon.c"), "-lm", "-o", str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
