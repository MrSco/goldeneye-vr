"""Production GE-X registry, switches, converter and animation pose checks.

Usage: python port/tests/test_gex_weapons.py [GE-X.z64]
Optional ROM-derived samples stay in a temporary directory.
"""
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile

ROOT=Path(__file__).resolve().parents[2]


def function(source, signature):
    start=source.index(signature); opening=source.index('{',start)
    end,depth=opening+1,1
    while depth:
        depth+=(source[end]=='{')-(source[end]=='}'); end+=1
    return source[start:end]


HARNESS=r'''
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdbool.h>
#include "bondtypes.h"
#include "gevr_gexweapon.h"
#include "gevr_gexmodel.h"
#include "gevr_model.h"
#include "gevr_pdanim.h"
#include "game/matrixmath.h"
float VrReloadGrab[2][3], VrGexForeHold[3], VrGexPp7Grab[3], VrGexPp7Support[3];
float VrGexGunOff[3], VrGexPp7GunOff[3];
float VrGexKf7MagOff[3], VrGexPp7MagOff[3];
float VrGexPp7SupportRot[3], VrGexWeaponFits[64][9][3];
float VrGexKf7WellOff[3],VrGexPp7WellOff[3];
static int gevrScopeFitting,gevrReloadFitting,gevrOffHandFitting,gevrHeldMagFitting,gevrMuzzleFitting,gevrGunHandFitting;
static int gevrWellFitting,gevrInstalledMagFitting;
static int gevrGunFitActive,g_gevrStereo=1,fitReload,fitScope=-1;
#define LOGI(...) ((void)0)
s32 gevrScopeFitIndex(void) { return fitScope; }
s32 gevrReloadFitAvailable(void) { return fitReload; }
s32 gevrMuzzleFitAvailable(void) { return 1; }
static const GexWeaponDef *active;
struct GevrScope { s32 item; f32 x,y,z,r; s32 nearOnly; };
static const struct GevrScope s_gevrScopes[]={{ITEM_SNIPERRIFLE,13.5f,126.5f,-128,23.5f,0},{ITEM_LASER,0,100,-80,16,0}};
static float VrScopeFit[2][4][4],s_gevrScopeTrim[4],scopeSize=1;
static int VrLeftHandedMode;
#define GEVR_VIEWMODEL_CM 0.85f
#define GEVR_GRIP_TO_ORIGIN_CM 12.0f
#define GEVR_SCOPE_LENS_SCALE 2.0f
static f32 gevrGunSizeFactor(void) { return scopeSize; }
static void gevrGunOff(s32 hand,f32 out[3]) { memcpy(out,gevrGexGunFit(active->item),3*sizeof(f32)); }
static u8 *rom; static u32 romSize;
static const char *modelPath;
static s32 visible[64];
static f32 s_gevrGexFire[MAX_PLAYER_COUNT][2], s_gevrGexReadyFrame[MAX_PLAYER_COUNT][2];
static s32 s_gevrGexFiring[MAX_PLAYER_COUNT][2];
static s32 g_ClockTimer=1;
#define GEVR_GEX_RHAND_ARM 1
#define GEVR_GEX_RHAND_WRIST 2
#define GEVR_GEX_LHAND_ARM 17
#define GEVR_GEX_LHAND_FIRST 17
#define GEVR_GEX_LHAND_LAST 32
#define GEVR_GEX_LHAND_WRIST 18
#define GEVR_GEX_PALM_Z 50.0f
enum { GEVR_GEXMAG_IN, GEVR_GEXMAG_GRIPPED, GEVR_GEXMAG_INHAND, GEVR_GEXMAG_OUT };
static Mtxf s_gevrGexLastGun[2];
static s32 s_gevrGexMagPointsValid;
static f32 s_gevrGexWellAt[3],s_gevrGexHeldAt[3];
static Mtxf s_gevrGexOffR2, s_gevrGexOffChain[33], s_gevrGexOffEmpty[33];
static ModelNode *s_gevrGexOffFrom;
static s32 s_gevrGexOffStale;
static Mtxf offController;
static f32 offPalm[3];
static f32 s_gevrGexForeAt[3];
static f32 s_gevrGexForeOff[3];
static s32 s_gevrGexForeValid;
s32 gevrStereoOffHandMatrix(Mtxf *out) { *out=offController; return 1; }
s32 gevrGexHeldPalm(f32 out[3]) { memcpy(out,offPalm,sizeof(offPalm)); return 1; }
static int get_cur_playernum(void) { return 0; }
static void gevrGexFallTick(s32 hand) {}
s32 gevrGexHeld(s32 hand) { return active!=NULL; }
const GexWeaponDef *gevrGexWeaponForHand(s32 hand) { return active; }
const GexWeaponDef *gevrGexForHeader(ModelFileHeader *h) { (void)h; return active; }
void *modelGetNodeRwData(Model *m, ModelNode *n) { (void)m; return &visible[n->Data->Switch.RwDataIndex]; }
void sysLogPrintf(s32 level,const char *fmt,...) {}
void sysFatalError(const char *fmt,...) { abort(); }
s32 gevrHandPatchWants(const char *n) { return 0; }
s32 gevrHandPatchDropsTexture(const char *n,u32 t) { return 0; }
void gevrHandPatchNoteNode(u32 s,u32 d) {}
void gevrHandPatchNoteMarker(u32 t,u32 w) {}
const u8 *gevrGexTextureData(s32 id,u32 *len) { return NULL; }
const u8 *gevrGexRom(u32 *len) { *len=romSize; return rom; }
u8 *readFile(const char *path,u32 *len) {
    FILE *f=fopen(path,"rb"); assert(f);
    fseek(f,0,SEEK_END); *len=ftell(f); rewind(f);
    u8 *out=malloc(*len); assert(out && fread(out,1,*len,f)==*len); fclose(f); return out;
}
u8 *gevrGexFileLoad(const char *name,u32 *len) { return modelPath ? readFile(modelPath,len) : NULL; }
/* PRODUCTION */
static void reloadGrip(ModelFileHeader *hdr) {
    Mtxf ident,held[64],poses[64],empty[33],inverse,relative,expected,mag;
    matrix_4x4_set_identity(&ident);
    gevrGexPoseWalk(hdr,&ident,gevrGexHoldAnim(active),active->holdFrame,held);
    for (int mirror=0;mirror<2;mirror++) for (int turn=0;turn<2;turn++) for (int size=0;size<2;size++) {
        float k=size ? 0.085f : 0.017f, angle=turn ? 0.8f : 0;
        float sign=mirror ? -1 : 1;
        matrix_4x4_set_identity(&offController);
        offController.m[0][0]=sign*k*cosf(angle); offController.m[0][2]=sign*k*sinf(angle);
        offController.m[1][1]=k;
        offController.m[2][0]=-k*sinf(angle); offController.m[2][2]=k*cosf(angle);
        offController.m[3][0]=100; offController.m[3][1]=-30; offController.m[3][2]=60;
        offPalm[0]=30; offPalm[1]=40; offPalm[2]=-80;
        gevrGexPoseWalk(hdr,&ident,gevrGexRestAnim(active),0,poses);
        gevrGexLeftHandTo(hdr,poses,offPalm);
        mag=poses[active->heldMatrix];
        assert(gevrGexOffSteady(hdr,poses));
        assert(gevrGexOffHandPose(empty,33));
        assert(!memcmp(&poses[17],&empty[17],sizeof(Mtxf)));
        if (!active->compact && !active->trackedMagWrist) continue;
        /* The old translation-only fix left the PP7 wrist inverted by 172 degrees. */
        for (int row=0;row<4;row++) for (int a=0;a<4;a++)
            assert(fabsf(poses[18].m[row][a]-empty[18].m[row][a])<0.0001f);
        gevrGexRigidInverse(&held[18],&inverse);
        for (int j=19;j<=32;j++) {
            matrix_4x4_multiply(&inverse,&held[j],&relative);
            matrix_4x4_multiply(&empty[18],&relative,&expected);
            for (int row=0;row<4;row++) for (int a=0;a<4;a++)
                assert(fabsf(poses[j].m[row][a]-expected.m[row][a])<0.0001f);
        }
        f32 top[3],local[3],wanted[3],actual[3];
        gevrGexMtxPoint(&held[active->gripMatrix ? active->gripMatrix : active->heldMatrix],active->gripMatrix ? active->magTop : active->heldTop,top);
        gevrGexMtxPoint(&inverse,top,local);
        gevrGexMtxPoint(&empty[18],local,wanted);
        gevrGexMtxPoint(&poses[active->heldMatrix],active->heldTop,actual);
        for (int a=0;a<3;a++) assert(fabsf(actual[a]-wanted[a])<0.0001f);
        for (int row=0;row<3;row++) for (int a=0;a<3;a++)
            assert(poses[active->heldMatrix].m[row][a]==mag.m[row][a]);
    }
    /* A held-mag fit moves the actual seating top, while hand joints and
     * magazine orientation stay fixed; offsets follow the off controller. */
    for (int mirror=0;mirror<2;mirror++) for (int turn=0;turn<2;turn++) {
        float angle=turn ? 0.8f : 0, sign=mirror ? -1 : 1;
        matrix_4x4_set_identity(&offController);
        offController.m[0][0]=sign*0.085f*cosf(angle); offController.m[0][2]=sign*0.085f*sinf(angle);
        offController.m[1][1]=0.085f;
        offController.m[2][0]=-0.085f*sinf(angle); offController.m[2][2]=0.085f*cosf(angle);
        float *fit=gevrGexHeldMagFit(active->item);
        fit[0]=1.25f; fit[1]=-2.5f; fit[2]=3.75f;
        gevrGexLeftHandTo(hdr,poses,offPalm);
        assert(gevrGexOffSteady(hdr,poses));
        Mtxf before[64]; memcpy(before,poses,sizeof(before));
        f32 oldTop[3],newTop[3];
        gevrGexMtxPoint(&poses[active->heldMatrix],active->heldTop,oldTop);
        gevrGexHeldMagFitTo(active,&poses[active->heldMatrix]);
        gevrGexMtxPoint(&poses[active->heldMatrix],active->heldTop,newTop);
        for (int a=0;a<3;a++) {
            float offset=-fit[0]*sign*(a==0 ? cosf(angle) : a==2 ? sinf(angle) : 0)
                + (a==1 ? fit[1] : 0) -fit[2]*(a==0 ? -sinf(angle) : a==2 ? cosf(angle) : 0);
            assert(fabsf(newTop[a]-oldTop[a]-offset)<0.0001f);
        }
        for (int j=17;j<=32;j++) assert(!memcmp(&poses[j],&before[j],sizeof(Mtxf)));
        for (int row=0;row<3;row++) for (int a=0;a<4;a++)
            assert(poses[active->heldMatrix].m[row][a]==before[active->heldMatrix].m[row][a]);
        memset(fit,0,3*sizeof(float));
    }
    if (active->item != ITEM_AK47) {
        gevrGexPoseWalk(hdr,&ident,gevrGexRestAnim(active),0,poses);
        gevrGexForeFrom(poses);
        gevrGexPistolSupportPose(hdr,poses);
        Mtxf support[64]; memcpy(support,poses,sizeof(support));
        /* Controller translation and rotation cannot pivot a supported pistol hand. */
        memset(&offController,0,sizeof(offController));
        offPalm[0]=offPalm[1]=offPalm[2]=1000;
        gevrGexPistolSupportPose(hdr,poses);
        for (int j=17;j<=32;j++) assert(!memcmp(&poses[j],&support[j],sizeof(Mtxf)));
        f32 palm[3],local[3]={0,0,50};
        gevrGexMtxPoint(&poses[18],local,palm);
        for (int a=0;a<3;a++) assert(fabsf(palm[a]-s_gevrGexForeAt[a])<0.0001f);
        for (int mirror=0;mirror<2;mirror++) for (int size=0;size<2;size++) {
            Mtxf world;
            float k=size ? 0.085f : 0.017f, angle=0.8f, sign=mirror ? -1 : 1;
            matrix_4x4_set_identity(&world);
            world.m[0][0]=sign*k*cosf(angle); world.m[0][2]=sign*k*sinf(angle);
            world.m[1][1]=k; world.m[2][0]=-k*sinf(angle); world.m[2][2]=k*cosf(angle);
            world.m[3][0]=100; world.m[3][1]=-30; world.m[3][2]=60;
            gevrGexPoseWalk(hdr,&world,gevrGexRestAnim(active),0,poses);
            gevrGexForeFrom(poses);
            gevrGexPistolSupportPose(hdr,poses);
            for (int j=17;j<=32;j++) {
                matrix_4x4_multiply(&world,&support[j],&expected);
                for (int row=0;row<4;row++) for (int a=0;a<4;a++)
                    assert(fabsf(poses[j].m[row][a]-expected.m[row][a])<0.0001f);
            }
        }
        gevrGexPoseWalk(hdr,&ident,gevrGexRestAnim(active),0,poses);
        gevrGexForeFrom(poses);
        float *supportRot=gevrGexSupportRotFit(active->item);
        supportRot[0]=30; supportRot[1]=-45; supportRot[2]=90;
        gevrGexPistolSupportPose(hdr,poses);
        assert(fabsf(poses[18].m[0][0]-support[18].m[0][0])>0.1f);
        gevrGexMtxPoint(&poses[18],local,palm);
        for (int a=0;a<3;a++) assert(fabsf(palm[a]-s_gevrGexForeAt[a])<0.0001f);
        memcpy(support,poses,sizeof(support));
        offController.m[3][0]=5000;
        gevrGexPistolSupportPose(hdr,poses);
        for (int j=17;j<=32;j++) assert(!memcmp(&poses[j],&support[j],sizeof(Mtxf)));
        memset(supportRot,0,3*sizeof(float));
    }
}
static void switches(void) {
    if (!gevrGexHasAmmo(active)) return;
    ModelFileHeader hdr={0}; Model model={0}; ModelNode nodes[8]={0};
    union ModelRoData records[8]={0}; ModelNode *table[64]={0};
    assert(active->numParts<=8);
    hdr.numSwitches=40+active->numParts; hdr.Switches=table;
    for (int i=0;i<active->numParts;i++) {
        nodes[i].Data=&records[i]; records[i].Switch.RwDataIndex=40+i;
        table[40+i]=&nodes[i];
    }
    gevrGexShowMagazines(&hdr,&model,1,0);
    assert(visible[40]==1 && visible[41]==0);
    for (int i=2;i<active->numParts;i++) assert(visible[40+i]==active->visible[i]);
    gevrGexShowMagazines(&hdr,&model,0,1);
    assert(visible[40]==0 && visible[41]==1);
    for (int i=2;i<active->numParts;i++) assert(visible[40+i]==active->visible[i]);
    if (active->loaderRounds>1) {
        /* one round drawn per round the loader carries, never the empty part 0 */
        assert(active->numParts==active->loaderRounds+1 && gevrGexOpensWhileHeld(active));
        for (int n=0;n<=active->loaderRounds;n++) {
            gevrGexShowMagazines(&hdr,&model,0,n);
            for (int i=1;i<active->numParts;i++) assert(visible[40+i]==(i<=n));
        }
    }
    if (active->reload.openHide>0) {
        const GexReloadDef *clips[]={&active->reload,&active->dualReload};
        for(int c=0;c<2;c++) {
            const GexReloadDef *clip=clips[c];
            gevrGexShowChamber(&hdr,&model,gevrGexChamberOpen(clip,clip->openShow-1,0)); assert(!visible[42] && !visible[43]);
            gevrGexShowChamber(&hdr,&model,gevrGexChamberOpen(clip,clip->openShow,0)); assert(visible[42] && visible[43]);
            gevrGexShowChamber(&hdr,&model,gevrGexChamberOpen(clip,clip->openHide,0)); assert(!visible[42] && !visible[43]);
            gevrGexShowChamber(&hdr,&model,gevrGexChamberOpen(clip,-1,1)); assert(visible[42] && visible[43]);
            gevrGexShowChamber(&hdr,&model,gevrGexChamberOpen(clip,active->reload.ammoFrame,0)); assert(visible[42]==(c==0));
        }
    }
}
static void wellFit(ModelFileHeader *hdr) {
    Mtxf world,poses[64],before[64];
    for (int mirror=0;mirror<2;mirror++) for (int size=0;size<2;size++) {
        float k=size ? 0.085f : 0.017f, angle=0.8f, sign=mirror ? -1 : 1;
        matrix_4x4_set_identity(&world);
        world.m[0][0]=sign*k*cosf(angle); world.m[0][2]=sign*k*sinf(angle);
        world.m[1][1]=k; world.m[2][0]=-k*sinf(angle); world.m[2][2]=k*cosf(angle);
        world.m[3][0]=100; world.m[3][1]=-30; world.m[3][2]=60;
        gevrGexPoseWalk(hdr,&world,gevrGexRestAnim(active),0,poses);
        memcpy(before,poses,sizeof(before));
        const Mtxf *mag=&poses[active->magMatrix],*gun=&poses[active->gunMatrix];
        f32 well[3],base[3],top[3];
        gevrGexWellPoint(active,mag,gun,well);
        gevrGexMtxPoint(mag,active->magWell,base);
        gevrGexMtxPoint(mag,active->magTop,top);
        for (int a=0;a<3;a++) assert(fabsf(well[a]-base[a])<0.0001f);
        float d2=0; for (int a=0;a<3;a++) d2+=(well[a]-top[a])*(well[a]-top[a]);
        if (active->pistol && gevrGexHasMagazine(active)) assert(sqrtf(d2)/k>110); /* entrance is below seated tip, at handle bottom */
        else assert(d2==0); /* KF7's working target stays unchanged */
        float *fit=gevrGexWellFit(active->item);
        fit[0]=1.25f; fit[1]=-2.5f; fit[2]=3.75f;
        gevrGexWellPoint(active,mag,gun,well);
        for (int a=0;a<3;a++) {
            float expected=(-fit[0]*gun->m[0][a]+fit[1]*gun->m[1][a]-fit[2]*gun->m[2][a])/0.085f;
            assert(fabsf(well[a]-base[a]-expected)<0.0001f);
        }
        s_gevrGexLastGun[0]=*gun; s_gevrGexMagPointsValid=3;
        for (int a=0;a<3;a++) { s_gevrGexWellAt[a]=well[a]; s_gevrGexHeldAt[a]=well[a]+(a+1)*k; }
        gevrReloadFitSetWell();
        gevrGexWellPoint(active,mag,gun,well);
        for (int a=0;a<3;a++) assert(fabsf(well[a]-s_gevrGexHeldAt[a])<0.0001f);
        assert(!memcmp(poses,before,sizeof(poses))); /* target fit does not move any meshes */
        f32 target[3]; memcpy(target,well,sizeof(target));
        float *visual=gevrGexInstalledMagFit(active->item);
        if (!gevrGexHasMagazine(active)) {
            visual[0]=2; visual[1]=-3; visual[2]=4;
            gevrGexWellPoint(active,mag,gun,well);
            for(int a=0;a<3;a++) assert(fabsf(well[a]-target[a])<0.0001f);
            memset(visual,0,3*sizeof(float)); memset(fit,0,3*sizeof(float)); continue;
        }
        visual[0]=-2.25f; visual[1]=1.5f; visual[2]=0.75f;
        gevrGexInstalledMagFitTo(active,&poses[active->magMatrix],gun);
        for (int a=0;a<3;a++) {
            f32 delta=(-visual[0]*gun->m[0][a]+visual[1]*gun->m[1][a]-visual[2]*gun->m[2][a])/0.085f;
            assert(fabsf(poses[active->magMatrix].m[3][a]-before[active->magMatrix].m[3][a]-delta)<0.0001f);
        }
        for (int j=0;j<64;j++) if(j!=active->magMatrix) assert(!memcmp(&poses[j],&before[j],sizeof(Mtxf)));
        for (int j=0;j<3;j++) assert(!memcmp(poses[active->magMatrix].m[j],before[active->magMatrix].m[j],4*sizeof(f32)));
        gevrGexWellPoint(active,mag,gun,well);
        for (int a=0;a<3;a++) assert(fabsf(well[a]-target[a])<0.0001f);
        memset(visual,0,3*sizeof(float)); memset(fit,0,3*sizeof(float));
    }
}
static ModelNode *nodes[512]; static int count;
static void collect(u8 *data,ModelNode *n) {
    while (n) {
        assert(count<512); nodes[count++]=n;
        if (n->Child) collect(data,(ModelNode *)(data+((uintptr_t)n->Child&0xffffff)));
        n=n->Next ? (ModelNode *)(data+((uintptr_t)n->Next&0xffffff)) : NULL;
    }
}
static void *pointer(u8 *data,void *value) { return value ? data+((uintptr_t)value&0xffffff) : NULL; }
static u32 be32(u8 *p) { return (u32)p[0]<<24|(u32)p[1]<<16|(u32)p[2]<<8|p[3]; }
static int be16(u8 *p) { return (int)((s16)((u16)p[0]<<8|p[1])); }
static u8 *partVertices(u8 *source,int wanted) {
    u32 table=be32(source+8)&0xffffff; int count=be16(source+12);
    for (int i=0;i<count;i++) if (be16(source+table+4*count+2*i)==wanted) {
        u32 node=be32(source+table+4*i)&0xffffff;
        u32 child=be32(source+node+20)&0xffffff;
        u32 record=be32(source+child+4)&0xffffff;
        return source+(be32(source+record+12)&0xffffff);
    }
    abort();
}
int main(int argc,char **argv) {
    const int items[]={ITEM_AK47,ITEM_WPPK,ITEM_WPPKSIL,ITEM_TT33,ITEM_SKORPION,ITEM_UZI,ITEM_MP5K,ITEM_MP5KSIL,ITEM_SPECTRE,ITEM_M16,ITEM_FNP90,ITEM_SNIPERRIFLE,ITEM_LASER,ITEM_SHOTGUN,ITEM_AUTOSHOT,ITEM_ROCKETLAUNCH,ITEM_GOLDENGUN,ITEM_RUGER,ITEM_GRENADELAUNCH,ITEM_SILVERWPPK,ITEM_GOLDWPPK};
    for (unsigned i=0;i<sizeof(items)/sizeof(items[0]);i++) { active=gevrGexWeaponGet(items[i]); assert(active); switches(); }
    assert(gevrGexWeaponGet(ITEM_WPPK)->magMatrix==38);
    assert(gevrGexWeaponGet(ITEM_WPPK)->heldMatrix==42);
    assert(gevrGexHeldMagFit(ITEM_WPPK)==gevrGexHeldMagFit(ITEM_WPPKSIL));
    /* the bonus DY357s are the PP7's rig: one family of fits */
    for (int b=ITEM_SILVERWPPK;b<=ITEM_GOLDWPPK;b++) {
        assert(gevrGexGunFit(b)==gevrGexGunFit(ITEM_WPPK) && gevrGexGrabFit(b)==gevrGexGrabFit(ITEM_WPPK));
        assert(gevrGexSupportFit(b)==gevrGexSupportFit(ITEM_WPPK) && gevrGexSupportRotFit(b)==gevrGexSupportRotFit(ITEM_WPPK));
        assert(gevrGexHeldMagFit(b)==gevrGexHeldMagFit(ITEM_WPPK) && gevrGexWellFit(b)==gevrGexWellFit(ITEM_WPPK));
        assert(gevrGexInstalledMagFit(b)==gevrGexInstalledMagFit(ITEM_WPPK));
    }
    assert(gevrGexHeldMagFit(ITEM_WPPK)!=gevrGexHeldMagFit(ITEM_AK47));
    assert(gevrGexWellFit(ITEM_WPPK)==gevrGexWellFit(ITEM_WPPKSIL));
    assert(gevrGexWellFit(ITEM_WPPK)!=gevrGexWellFit(ITEM_AK47));
    assert(gevrGexInstalledMagFit(ITEM_WPPK)==gevrGexInstalledMagFit(ITEM_WPPKSIL));
    assert(gevrGexInstalledMagFit(ITEM_MP5K)==gevrGexInstalledMagFit(ITEM_MP5KSIL));
    assert(gevrGexInstalledMagFit(ITEM_M16)!=gevrGexInstalledMagFit(ITEM_SPECTRE));
    assert(gevrGexWeaponGet(ITEM_WPPK)->muzzle[2]<gevrGexWeaponGet(ITEM_WPPKSIL)->muzzle[2]);
    assert(!gevrGexWeaponGet(ITEM_KNIFE));
    assert(!gevrGexHasMagazine(gevrGexWeaponGet(ITEM_LASER)));
    assert(gevrGexGunFit(ITEM_MP5K)==gevrGexGunFit(ITEM_MP5KSIL));
    assert(gevrGexSupportRotFit(ITEM_UZI)!=gevrGexSupportRotFit(ITEM_SKORPION));
    assert(gevrGexGrabFit(ITEM_TT33)!=gevrGexGrabFit(ITEM_WPPK));
    /* Real input X cycling and the HUD must include magazine fit with reload
     * disabled, and the preview must stop when leaving fit or stereo. */
    active=gevrGexWeaponGet(ITEM_WPPK);
    fitReload=0;
    cycleFit(); assert(gevrOffHandFitting);
    assert(strstr(gevrFitNextLine(3),"HELD AMMO"));
    cycleFit(); assert(gevrHeldMagFitting);
    gevrGunFitActive=1; assert(gevrGexMagazineFitting());
    s32 drawn=0; assert(gevrGexOffHandConsumed(GEVR_GEXMAG_IN,&drawn) && drawn);
    gevrGunFitActive=2; assert(!gevrGexMagazineFitting());
    gevrGunFitActive=1; g_gevrStereo=0; assert(!gevrGexMagazineFitting()); g_gevrStereo=1;
    cycleFit(); assert(gevrWellFitting && !gevrHeldMagFitting);
    assert(gevrGexMagazineFitting());
    assert(strstr(gevrFitNextLine(4),"AMMO INSERTION"));
    assert(strstr(gevrFitNextLine(5),"INSTALLED MAGAZINE"));
    cycleFit(); assert(gevrInstalledMagFitting && !gevrWellFitting);
    assert(!gevrGexMagazineFitting() && gevrGexInstalledMagazineFitting());
    gevrGunFitActive=2; assert(!gevrGexInstalledMagazineFitting()); gevrGunFitActive=1;
    g_gevrStereo=0; assert(!gevrGexInstalledMagazineFitting()); g_gevrStereo=1;
    assert(strstr(gevrFitNextLine(6),"BARREL TIP"));
    cycleFit(); assert(gevrMuzzleFitting && !gevrInstalledMagFitting);
    assert(!gevrGexMagazineFitting());
    assert(strstr(gevrFitNextLine(7),"GUN HAND"));
    drawn=0; assert(!gevrGexOffHandConsumed(GEVR_GEXMAG_IN,&drawn) && !drawn);
    drawn=0; assert(gevrGexOffHandConsumed(GEVR_GEXMAG_INHAND,&drawn) && drawn);
    cycleFit(); assert(gevrGunHandFitting && !gevrMuzzleFitting);
    assert(strstr(gevrFitNextLine(8),"FIT THE GUN\n"));
    cycleFit(); assert(!gevrGunHandFitting && !gevrMuzzleFitting && !gevrOffHandFitting);
    active=gevrGexWeaponGet(ITEM_LASER); fitScope=1;
    cycleFit(); assert(gevrScopeFitting);
    cycleFit(); assert(gevrOffHandFitting);
    cycleFit(); assert(gevrMuzzleFitting && !gevrHeldMagFitting && !gevrWellFitting && !gevrInstalledMagFitting);
    cycleFit(); assert(gevrGunHandFitting && !gevrMuzzleFitting);
    cycleFit(); assert(!gevrGunHandFitting);
    active=gevrGexWeaponGet(ITEM_GOLDENGUN); fitScope=-1;
    cycleFit(); assert(gevrOffHandFitting);
    cycleFit(); assert(gevrHeldMagFitting);
    cycleFit(); assert(gevrWellFitting);
    cycleFit(); assert(gevrMuzzleFitting && !gevrInstalledMagFitting);
    cycleFit(); assert(gevrGunHandFitting && !gevrMuzzleFitting);
    cycleFit(); assert(!gevrGunHandFitting);
    { u32 len; u16 matrices,textures; assert(!gevrGexBuildModel("missing",0,NULL,NULL,&len,&matrices,&textures)); }
    if (argc<4) { puts("PASS: registry and production attachment/magazine visibility"); return 0; }
    active=gevrGexWeaponGet(atoi(argv[3])); modelPath=argv[1]; rom=readFile(argv[2],&romSize);
    s_gevrGexFire[0][0]=-1; s_gevrGexReadyFrame[0][0]=-1; s_gevrGexFiring[0][0]=0;
    if (active->fireAnim == 0) {
        gevrGexTick(0,1,1); gevrGexTick(0,1,1); assert(s_gevrGexFire[0][0]==-1);
    } else if (active->pistol) {
        gevrGexTick(0,1,0); assert(s_gevrGexFire[0][0]==-1); /* no accepted shot */
        gevrGexTick(0,1,1); assert(s_gevrGexFire[0][0]==0);
        gevrGexTick(0,1,0); assert(s_gevrGexFire[0][0]==1);
        gevrGexTick(0,1,1); assert(s_gevrGexFire[0][0]==0); /* repeated semi-auto shot */
        gevrGexMagazineReady(0); assert(s_gevrGexReadyFrame[0][0]==(gevrGexHasAmmo(active) && active->reload.anim>0 ? active->reload.ammoFrame : -1));
        gevrGexTick(0,1,1); assert(s_gevrGexReadyFrame[0][0]==-1);
    } else {
        gevrGexTick(0,1,1); assert(s_gevrGexFire[0][0]==0);
        gevrGexTick(0,1,1); assert(s_gevrGexFire[0][0]==1); /* KF7 burst keeps playing */
    }
    if (active->pistol && gevrGexHasMagazine(active)) {
        u32 size; u8 *source=readFile(modelPath,&size);
        u8 *installed=partVertices(source,42),*held=partVertices(source,43);
        for (int v=0;v<16;v++) for (int a=0;a<3;a++) {
            f32 p=active->heldToMag[3][a];
            for (int k=0;k<3;k++) p+=be16(held+12*v+2*k)*active->heldToMag[k][a];
            assert(fabsf(p-be16(installed+12*v+2*a))<1.5f);
        }
        for (int a=0;a<3;a++) {
            f32 p=active->heldToMag[3][a];
            for (int k=0;k<3;k++) p+=active->heldTop[k]*active->heldToMag[k][a];
            assert(fabsf(p-active->magTop[a])<0.001f);
        }
        free(source);
    }
    s32 parts[64]; for (int i=0;i<64;i++) parts[i]=-1;
    parts[1]=90; int ns=40+active->numParts;
    for (int i=0;i<active->numParts;i++) parts[40+i]=active->parts[i];
    u32 length=0; u16 matrices=0,textures=0;
    u8 *cart=gevrGexBuildModel(active->model,ns,parts,active->texturePairs,&length,&matrices,&textures);
    assert(cart && matrices<=64); u8 *host=calloc(1,0xf000); assert(length<=0xf000);
    memcpy(host,cart,length); free(cart);
    u32 converted=gevrModelConvert(host,length,0xf000,ns,textures,active->model); assert(converted);
    ModelFileHeader hdr={0}; hdr.numSwitches=ns; hdr.numMatrices=matrices;
    ModelNode *root=(ModelNode *)(host+ns*sizeof(uintptr_t)+textures*sizeof(ModelFileTextures));
    count=0; collect(host,root);
    for (int i=0;i<count;i++) {
        ModelNode *n=nodes[i]; n->Data=pointer(host,n->Data); n->Parent=pointer(host,n->Parent);
        n->Next=pointer(host,n->Next); n->Prev=pointer(host,n->Prev); n->Child=pointer(host,n->Child);
    }
    hdr.RootNode=root;
    ModelNode *table[64]={0};
    for (int i=0;i<ns;i++) table[i]=pointer(host,((ModelNode **)host)[i]);
    hdr.Switches=table;
    for (int i=0;i<active->numParts;i++) if(active->parts[i]>=0) assert(table[40+i]);
    Mtxf base,poses[64]; matrix_4x4_set_identity(&base);
    gevrGexPoseWalk(&hdr,&base,gevrGexRestAnim(active),0,poses);
    for (int i=0;i<matrices-1;i++) assert(isfinite(poses[i].m[3][0]));
    /* Every rest pose must keep the actual gun at its screen placement. */
    Mtxf anchor,screen[64]; gevrGexScreenAnchor(&hdr,&anchor);
    gevrGexPoseWalk(&hdr,&anchor,gevrGexRestAnim(active),0,screen);
    for (int a=0;a<3;a++) assert(fabsf(screen[active->gunMatrix].m[3][a]-active->screenOffset[a])<0.01f);
    if (active->hasScope) {
        const struct GevrScope *sc=&s_gevrScopes[active->item==ITEM_LASER];
        f32 ring[3]; gevrGexMtxPoint(&anchor,active->scopeRoot,ring);
        assert(fabsf(ring[0]-sc->x)<0.01f && fabsf(ring[1]-sc->y)<0.01f && fabsf(ring[2]-sc->z)<0.01f);
        for (int mirror=0;mirror<2;mirror++) for(int size=0;size<2;size++) {
            VrLeftHandedMode=mirror; scopeSize=size ? 2 : 1;
            f32 lens[4],*fit=VrScopeFit[1][sc-s_gevrScopes],*off=gevrGexGunFit(active->item);
            fit[0]=2; fit[1]=-3; fit[2]=4; fit[3]=0.5f;
            gevrScopeLensPlace(0,sc,lens);
            const f32 unit=0.00085f*scopeSize,sign=mirror ? -1 : 1;
            assert(fabsf(lens[0]-sign*((off[0]+2)*scopeSize/100-active->scopeRoot[0]*unit))<0.00001f);
            assert(fabsf(lens[1]-((off[1]-3)*scopeSize/100+active->scopeRoot[1]*unit))<0.00001f);
            assert(fabsf(lens[2]-((12+off[2]+4)*scopeSize/100-active->scopeRoot[2]*unit))<0.00001f);
            assert(fabsf(lens[3]-(4*sc->r*unit+0.5f*scopeSize/100))<0.00001f);
            memset(fit,0,4*sizeof(f32));
        }
    }
    if (gevrGexHasAmmo(active)) { reloadGrip(&hdr); wellFit(&hdr); }
    {
        /* Weapon switch: a new gun marks the off hand's cache stale. The last
         * empty hand keeps drawing (no GoldenEye arm blink) until it is rebuilt,
         * even from a gun loaded at the same buffer address. */
        Mtxf pose[64]; s_gevrGexOffFrom=NULL; s_gevrGexOffStale=0;
        gevrGexOffCache(&hdr); assert(s_gevrGexOffFrom==hdr.RootNode);
        Mtxf chain=s_gevrGexOffChain[GEVR_GEX_LHAND_LAST];
        memset(s_gevrGexOffChain,0,sizeof(s_gevrGexOffChain));
        s_gevrGexOffStale=1;
        assert(gevrGexOffHandPose(pose,matrices));
        gevrGexOffCache(&hdr);
        assert(!s_gevrGexOffStale && !memcmp(&chain,&s_gevrGexOffChain[GEVR_GEX_LHAND_LAST],sizeof(chain)));
    }
    {
        /* Gun hand fit: the right hand alone moves and turns about its palm; the
         * gun and the left hand stay; mirroring and the gun's size carry through. */
        f32 *h=gevrGexHandFit(active->item),*rt=gevrGexHandRotFit(active->item),keep[6];
        const f32 palmL[3]={0,0,GEVR_GEX_PALM_Z};
        memcpy(keep,h,3*sizeof(f32)); memcpy(keep+3,rt,3*sizeof(f32));
        for (int mirror=0;mirror<2;mirror++) {
            Mtxf base2,before[64],after[64]; f32 p0[3],p1[3],ex[3];
            matrix_4x4_set_identity(&base2);
            base2.m[0][0]=(mirror?-1:1)*0.05f; base2.m[1][1]=base2.m[2][2]=0.05f;
            base2.m[3][0]=7; base2.m[3][1]=-3; base2.m[3][2]=11;
            gevrGexPoseWalk(&hdr,&base2,gevrGexRestAnim(active),0,before);
            memcpy(after,before,sizeof(after)); memset(h,0,3*sizeof(f32)); memset(rt,0,3*sizeof(f32));
            gevrGexHandFitTo(active,after); assert(!memcmp(after,before,sizeof(after)));
            h[0]=1; h[1]=2; h[2]=3; gevrGexHandFitTo(active,after);
            gevrGexMtxPoint(&before[GEVR_GEX_RHAND_WRIST],palmL,p0); gevrGexMtxPoint(&after[GEVR_GEX_RHAND_WRIST],palmL,p1);
            for (int a=0;a<3;a++) ex[a]=(-1*before[33].m[0][a]+2*before[33].m[1][a]-3*before[33].m[2][a])/0.085f;
            for (int a=0;a<3;a++) assert(fabsf(p1[a]-p0[a]-ex[a])<0.01f);
            for (int j=GEVR_GEX_LHAND_FIRST;j<matrices-1;j++) assert(!memcmp(&after[j],&before[j],sizeof(Mtxf)));
            memcpy(after,before,sizeof(after)); memset(h,0,3*sizeof(f32)); rt[0]=20; rt[1]=-35; rt[2]=10;
            gevrGexHandFitTo(active,after);
            gevrGexMtxPoint(&after[GEVR_GEX_RHAND_WRIST],palmL,p1);
            for (int a=0;a<3;a++) assert(fabsf(p1[a]-p0[a])<0.01f);
            f32 moved=0;
            for (int j=1;j<GEVR_GEX_LHAND_FIRST;j++) {
                for (int k=1;k<GEVR_GEX_LHAND_FIRST;k++) {
                    f32 a2=0,b2=0;
                    for (int a=0;a<3;a++) { f32 da=before[j].m[3][a]-before[k].m[3][a], db=after[j].m[3][a]-after[k].m[3][a]; a2+=da*da; b2+=db*db; }
                    assert(fabsf(sqrtf(a2)-sqrtf(b2))<0.01f);
                }
                for (int r=0;r<3;r++) {
                    f32 la=0,lb=0; for (int a=0;a<3;a++) { la+=before[j].m[r][a]*before[j].m[r][a]; lb+=after[j].m[r][a]*after[j].m[r][a]; }
                    assert(fabsf(sqrtf(la)-sqrtf(lb))<1e-4f);
                }
                for (int a=0;a<3;a++) { f32 m=fabsf(after[j].m[3][a]-before[j].m[3][a]); if (m>moved) moved=m; }
            }
            assert(moved>0.1f);
            for (int j=GEVR_GEX_LHAND_FIRST;j<matrices-1;j++) assert(!memcmp(&after[j],&before[j],sizeof(Mtxf)));
        }
        memcpy(h,keep,3*sizeof(f32)); memcpy(rt,keep+3,3*sizeof(f32));
    }
    if (active->payloadProp>0) {
        /* The grenade launcher: no clip of its own, the hand posed by the
         * borrowed one; the target is a chamber mouth on the drum's rear face
         * (matrix 34), and the held tip is GoldenEye's round's nose (+95) scaled. */
        assert(active->reload.anim==0 && active->holdAnim>0 && gevrPdAnimNumFrames(active->holdAnim)>active->holdFrame);
        assert(fabsf(active->heldTop[2]-95.0f*active->payloadScale)<0.01f);
        assert(fabsf(122.0f*active->payloadScale*0.085f-4.0f)<0.1f);   /* a 40 mm round */
        Mtxf inv; f32 at[3],drum[3];
        gevrGexRigidInverse(&poses[active->gunMatrix],&inv);
        for(int a=0;a<3;a++) at[a]=poses[34].m[3][a];
        gevrGexMtxPoint(&inv,at,drum);
        f32 r=sqrtf((active->magWell[0]-drum[0])*(active->magWell[0]-drum[0])+(active->magWell[1]-drum[1])*(active->magWell[1]-drum[1]));
        assert(r>37 && r<57 && fabsf(active->magWell[2]-drum[2])<1.0f && active->magWell[1]<drum[1]);
    }
    if (active->loaderRounds>1) {
        /* Held, the cylinder swings out to the loader's entrance; the gun and
         * the loader's own matrix stay where tracking put them. */
        Mtxf open[64],inv; f32 shut[3],out[3],d0=0,d1=0;
        memcpy(open,poses,sizeof(open));
        gevrGexPoseReady(&hdr,open,active->holdFrame);
        assert(!memcmp(&open[active->gunMatrix],&poses[active->gunMatrix],sizeof(Mtxf)));
        assert(!memcmp(&open[active->heldMatrix],&poses[active->heldMatrix],sizeof(Mtxf)));
        gevrGexRigidInverse(&poses[active->gunMatrix],&inv);
        f32 at[3]; for(int a=0;a<3;a++) at[a]=poses[39].m[3][a]; gevrGexMtxPoint(&inv,at,shut);
        for(int a=0;a<3;a++) at[a]=open[39].m[3][a]; gevrGexMtxPoint(&inv,at,out);
        for(int a=0;a<3;a++){ d0+=(shut[a]-active->magWell[a])*(shut[a]-active->magWell[a]); d1+=(out[a]-active->magWell[a])*(out[a]-active->magWell[a]); }
        assert(d1<12*12 && d0>d1 && fabsf(out[0]-shut[0])>10);
    }
    if (active->reload.openHide>0) {
        Mtxf before[64]; memcpy(before,poses,sizeof(before));
        gevrGexPoseReady(&hdr,poses,active->holdFrame);
        assert(!memcmp(&poses[33],&before[33],sizeof(Mtxf)));
        assert(!memcmp(&poses[42],&before[42],sizeof(Mtxf)));
        assert(fabsf(poses[38].m[3][2]-before[38].m[3][2])>20);
    }
    /* Animation joint 41 really writes PP7's matrix 38, not matrix 39. */
    if (active->item==ITEM_WPPK || active->item==ITEM_WPPKSIL) {
        f32 p[3]; for (int a=0;a<3;a++) p[a]=poses[38].m[3][a];
        assert(fabsf(p[1]-53.46f)<1.0f);
        gevrGexPoseWalk(&hdr,&base,active->reload.anim,active->holdFrame,poses);
        assert(fabsf(poses[42].m[3][0])>1.0f);
        Mtxf anchor;
        gevrGexScreenAnchor(&hdr,&anchor);
        gevrGexPoseWalk(&hdr,&anchor,gevrGexRestAnim(active),0,poses);
        for (int a=0;a<3;a++) assert(fabsf(poses[33].m[3][a]-active->screenOffset[a])<0.01f);
        Mtxf before[64]; memcpy(before,poses,sizeof(before));
        gevrGexPoseReady(&hdr,poses,60);
        for (int i=0;i<=33;i++) assert(!memcmp(&poses[i],&before[i],sizeof(Mtxf)));
        assert(!memcmp(&poses[38],&before[38],sizeof(Mtxf)));
        assert(!memcmp(&poses[42],&before[42],sizeof(Mtxf)));
        assert(fabsf(poses[37].m[3][2]-before[37].m[3][2])>20);
    }
    printf("PASS: %s: %d nodes, %d matrices, host model %u / 61440 bytes, production poses\n",active->model,count,matrices,converted);
    free(host); free(rom); return 0;
}
'''

gun=(ROOT/"src/game/gun.c").read_text()
# A model reset must not drop the off hand's cache (the original arm blinked on switch).
assert 'rebuildCache) s_gevrGexOffFrom = NULL' not in gun
math=(ROOT/"src/game/matrixmath.c").read_text()
production=[function(math,s) for s in ("void matrix_4x4_multiply(","void matrix_4x4_set_identity(",
    "void matrix_4x4_set_rotation_around_xyz(")]
production.extend(function(gun,s) for s in ("static void gevrGexAnimPart(","static void gevrGexPoseWalk(","static void gevrGexShowMagazines(","static void gevrGexScreenAnchor(","static void gevrGexShowChamber("))
production.extend(function(gun,s) for s in ("static void gevrGexRigidInverse(const Mtxf *g, Mtxf *inv)\n{", "static void gevrGexPoseReady("))
production.extend(function(gun,s) for s in ("void gevrGexTick(", "void gevrGexMagazineReady("))
production.extend(function(gun,s) for s in ("static void gevrGexMtxPoint(", "static void gevrGexLeftHandTo(",
    "static void gevrGexOffCache(ModelFileHeader *gunhdr)\n{", "static s32 gevrGexOffHandPose(",
    "static void gevrGexPistolMagGrip(", "static s32 gevrGexOffSteady("))
production.extend(function(gun,s) for s in ("static void gevrGexHeldMagFitTo(", "static void gevrGexPistolSupportPose("))
production.append(function(gun,"static void gevrGexForeFrom("))
production.append(function(gun,"static void gevrGexHandFitTo(const GexWeaponDef *def, Mtxf *rwmtx)\n{"))
production.extend(function(gun,s) for s in ("static void gevrGexInstalledMagFitTo(", "static s32 gevrGexInstalledMagazineFitting(", "static s32 gevrGexMagazineFitting("))
production.extend(function(gun,s) for s in ("static s32 gevrGexOffHandConsumed(", "static void gevrGexWellPoint(", "void gevrReloadFitSetWell("))
view=(ROOT/"src/game/bondview2.c").read_text()
production.append(function(view,"static const char *gevrFitNextLine("))
production.append(function(view,"static void gevrScopeLensPlace("))
input_source=(ROOT/"port/src/input.c").read_text()
cycle=function(input_source,"if (x && !xHeld && gadget < 0)")
production.append('static void cycleFit(void) { const int gex=active!=NULL,scope=fitScope; '+cycle[cycle.index('{')+1:-1]+' }')
with tempfile.TemporaryDirectory(prefix="gex-weapons-") as directory:
    temp=Path(directory); file=temp/"test.c"; exe=temp/"test.exe"
    file.write_text(HARNESS.replace('/* PRODUCTION */','\n'.join(production)))
    subprocess.run([shutil.which('gcc') or 'cc','-std=c11','-O2','-D_LANGUAGE_C','-DGEVR','-DVERSION_US',
                    '-I'+str(ROOT),'-I'+str(ROOT/'include'),'-I'+str(ROOT/'src'),'-I'+str(ROOT/'port/include'),
                    str(file),str(ROOT/'port/src/gevr_gexweapon.c'),str(ROOT/'port/src/gevr_gexmodel.c'),
                    str(ROOT/'port/src/gevr_model.c'),str(ROOT/'port/src/gevr_pdanim.c'),'-lm','-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True)
    if len(sys.argv)>1:
        sys.path.insert(0,str(ROOT/'tools/gex'))
        from pdrom import PdRom
        rom=PdRom(sys.argv[1])
        for model,item in (('Gak47Z',8),('GwppkZ',4),('GwppkZ',5),('Gtt33Z',6),('GskorpionZ',7),('GuziZ',9),('Gmp5kZ',10),('Gcmp150Z',11),('GcycloneZ',12),('Gm16Z',13),('Gfnp90Z',14),('GsniperrifleZ',17),('GdysuperdragonZ',22),('GshotgunZ',15),('Grcp120Z',16),('GdyrocketZ',25),('Gleegun1Z',19),('GmaianpistolZ',18),('GdydevastatorZ',24),('Gdy357Z',20),('Gdy357trentZ',21)):
            sample=temp/'model.bin'; sample.write_bytes(rom.load(model))
            subprocess.run([str(exe),str(sample),str(Path(sys.argv[1]).resolve()),str(item)],check=True)
