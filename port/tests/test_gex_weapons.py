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
float VrGexPp7SupportRot[3], VrGexWeaponFits[64][6][3];
float VrGexKf7WellOff[3],VrGexPp7WellOff[3];
static int gevrScopeFitting,gevrReloadFitting,gevrOffHandFitting,gevrHeldMagFitting,gevrMuzzleFitting;
static int gevrWellFitting;
static int gevrGunFitActive,g_gevrStereo=1,fitReload,fitScope=-1;
#define LOGI(...) ((void)0)
s32 gevrScopeFitIndex(void) { return fitScope; }
s32 gevrReloadFitAvailable(void) { return fitReload; }
s32 gevrMuzzleFitAvailable(void) { return 1; }
static const GexWeaponDef *active;
static u8 *rom; static u32 romSize;
static const char *modelPath;
static s32 visible[64];
static f32 s_gevrGexFire[MAX_PLAYER_COUNT][2], s_gevrGexReadyFrame[MAX_PLAYER_COUNT][2];
static s32 s_gevrGexFiring[MAX_PLAYER_COUNT][2];
static s32 g_ClockTimer=1;
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
    gevrGexPoseWalk(hdr,&ident,active->reload.anim,active->holdFrame,held);
    for (int mirror=0;mirror<2;mirror++) for (int turn=0;turn<2;turn++) for (int size=0;size<2;size++) {
        float k=size ? 0.085f : 0.017f, angle=turn ? 0.8f : 0;
        float sign=mirror ? -1 : 1;
        matrix_4x4_set_identity(&offController);
        offController.m[0][0]=sign*k*cosf(angle); offController.m[0][2]=sign*k*sinf(angle);
        offController.m[1][1]=k;
        offController.m[2][0]=-k*sinf(angle); offController.m[2][2]=k*cosf(angle);
        offController.m[3][0]=100; offController.m[3][1]=-30; offController.m[3][2]=60;
        offPalm[0]=30; offPalm[1]=40; offPalm[2]=-80;
        gevrGexPoseWalk(hdr,&ident,active->fireAnim,0,poses);
        gevrGexLeftHandTo(hdr,poses,offPalm);
        mag=poses[active->heldMatrix];
        assert(gevrGexOffSteady(hdr,poses));
        assert(gevrGexOffHandPose(empty,33));
        assert(!memcmp(&poses[17],&empty[17],sizeof(Mtxf)));
        if (!active->compact) continue;
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
        gevrGexMtxPoint(&held[active->heldMatrix],active->heldTop,top);
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
        gevrGexPoseWalk(hdr,&ident,active->fireAnim,0,poses);
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
            gevrGexPoseWalk(hdr,&world,active->fireAnim,0,poses);
            gevrGexForeFrom(poses);
            gevrGexPistolSupportPose(hdr,poses);
            for (int j=17;j<=32;j++) {
                matrix_4x4_multiply(&world,&support[j],&expected);
                for (int row=0;row<4;row++) for (int a=0;a<4;a++)
                    assert(fabsf(poses[j].m[row][a]-expected.m[row][a])<0.0001f);
            }
        }
        gevrGexPoseWalk(hdr,&ident,active->fireAnim,0,poses);
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
    ModelFileHeader hdr={0}; Model model={0}; ModelNode nodes[6]={0};
    union ModelRoData records[6]={0}; ModelNode *table[64]={0};
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
}
static void wellFit(ModelFileHeader *hdr) {
    Mtxf world,poses[64],before[64];
    for (int mirror=0;mirror<2;mirror++) for (int size=0;size<2;size++) {
        float k=size ? 0.085f : 0.017f, angle=0.8f, sign=mirror ? -1 : 1;
        matrix_4x4_set_identity(&world);
        world.m[0][0]=sign*k*cosf(angle); world.m[0][2]=sign*k*sinf(angle);
        world.m[1][1]=k; world.m[2][0]=-k*sinf(angle); world.m[2][2]=k*cosf(angle);
        world.m[3][0]=100; world.m[3][1]=-30; world.m[3][2]=60;
        gevrGexPoseWalk(hdr,&world,active->fireAnim,0,poses);
        memcpy(before,poses,sizeof(before));
        const Mtxf *mag=&poses[active->magMatrix],*gun=&poses[active->gunMatrix];
        f32 well[3],base[3],top[3];
        gevrGexWellPoint(active,mag,gun,well);
        gevrGexMtxPoint(mag,active->magWell,base);
        gevrGexMtxPoint(mag,active->magTop,top);
        for (int a=0;a<3;a++) assert(fabsf(well[a]-base[a])<0.0001f);
        float d2=0; for (int a=0;a<3;a++) d2+=(well[a]-top[a])*(well[a]-top[a]);
        if (active->pistol) assert(sqrtf(d2)/k>110); /* entrance is below seated tip, at handle bottom */
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
        memset(fit,0,3*sizeof(float));
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
    const int items[]={ITEM_AK47,ITEM_WPPK,ITEM_WPPKSIL,ITEM_TT33,ITEM_SKORPION,ITEM_UZI,ITEM_MP5K,ITEM_MP5KSIL,ITEM_SPECTRE,ITEM_M16};
    for (unsigned i=0;i<sizeof(items)/sizeof(items[0]);i++) { active=gevrGexWeaponGet(items[i]); assert(active); switches(); }
    assert(gevrGexWeaponGet(ITEM_WPPK)->magMatrix==38);
    assert(gevrGexWeaponGet(ITEM_WPPK)->heldMatrix==42);
    assert(gevrGexHeldMagFit(ITEM_WPPK)==gevrGexHeldMagFit(ITEM_WPPKSIL));
    assert(gevrGexHeldMagFit(ITEM_WPPK)!=gevrGexHeldMagFit(ITEM_AK47));
    assert(gevrGexWellFit(ITEM_WPPK)==gevrGexWellFit(ITEM_WPPKSIL));
    assert(gevrGexWellFit(ITEM_WPPK)!=gevrGexWellFit(ITEM_AK47));
    assert(gevrGexWeaponGet(ITEM_WPPK)->muzzle[2]<gevrGexWeaponGet(ITEM_WPPKSIL)->muzzle[2]);
    assert(!gevrGexWeaponGet(ITEM_FNP90));
    assert(gevrGexGunFit(ITEM_MP5K)==gevrGexGunFit(ITEM_MP5KSIL));
    assert(gevrGexSupportRotFit(ITEM_UZI)!=gevrGexSupportRotFit(ITEM_SKORPION));
    assert(gevrGexGrabFit(ITEM_TT33)!=gevrGexGrabFit(ITEM_WPPK));
    /* Real input X cycling and the HUD must include magazine fit with reload
     * disabled, and the preview must stop when leaving fit or stereo. */
    active=gevrGexWeaponGet(ITEM_WPPK);
    fitReload=0;
    cycleFit(); assert(gevrOffHandFitting);
    assert(strstr(gevrFitNextLine(3),"HELD MAGAZINE"));
    cycleFit(); assert(gevrHeldMagFitting);
    gevrGunFitActive=1; assert(gevrGexMagazineFitting());
    s32 drawn=0; assert(gevrGexOffHandConsumed(GEVR_GEXMAG_IN,&drawn) && drawn);
    gevrGunFitActive=2; assert(!gevrGexMagazineFitting());
    gevrGunFitActive=1; g_gevrStereo=0; assert(!gevrGexMagazineFitting()); g_gevrStereo=1;
    cycleFit(); assert(gevrWellFitting && !gevrHeldMagFitting);
    assert(gevrGexMagazineFitting());
    assert(strstr(gevrFitNextLine(4),"MAGAZINE WELL"));
    cycleFit(); assert(gevrMuzzleFitting && !gevrWellFitting);
    assert(!gevrGexMagazineFitting());
    drawn=0; assert(!gevrGexOffHandConsumed(GEVR_GEXMAG_IN,&drawn) && !drawn);
    drawn=0; assert(gevrGexOffHandConsumed(GEVR_GEXMAG_INHAND,&drawn) && drawn);
    cycleFit(); assert(!gevrMuzzleFitting && !gevrOffHandFitting);
    { u32 len; u16 matrices,textures; assert(!gevrGexBuildModel("missing",0,NULL,NULL,&len,&matrices,&textures)); }
    if (argc<4) { puts("PASS: registry and production attachment/magazine visibility"); return 0; }
    active=gevrGexWeaponGet(atoi(argv[3])); modelPath=argv[1]; rom=readFile(argv[2],&romSize);
    s_gevrGexFire[0][0]=-1; s_gevrGexReadyFrame[0][0]=-1; s_gevrGexFiring[0][0]=0;
    if (active->pistol) {
        gevrGexTick(0,1,0); assert(s_gevrGexFire[0][0]==-1); /* no accepted shot */
        gevrGexTick(0,1,1); assert(s_gevrGexFire[0][0]==0);
        gevrGexTick(0,1,0); assert(s_gevrGexFire[0][0]==1);
        gevrGexTick(0,1,1); assert(s_gevrGexFire[0][0]==0); /* repeated semi-auto shot */
        gevrGexMagazineReady(0); assert(s_gevrGexReadyFrame[0][0]==active->reload.ammoFrame);
        gevrGexTick(0,1,1); assert(s_gevrGexReadyFrame[0][0]==-1);
    } else {
        gevrGexTick(0,1,1); assert(s_gevrGexFire[0][0]==0);
        gevrGexTick(0,1,1); assert(s_gevrGexFire[0][0]==1); /* KF7 burst keeps playing */
    }
    if (active->pistol) {
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
    for (int i=0;i<active->numParts;i++) assert(table[40+i]);
    Mtxf base,poses[64]; matrix_4x4_set_identity(&base);
    gevrGexPoseWalk(&hdr,&base,active->fireAnim,0,poses);
    for (int i=0;i<matrices-1;i++) assert(isfinite(poses[i].m[3][0]));
    reloadGrip(&hdr);
    wellFit(&hdr);
    /* Animation joint 41 really writes PP7's matrix 38, not matrix 39. */
    if (active->item==ITEM_WPPK || active->item==ITEM_WPPKSIL) {
        f32 p[3]; for (int a=0;a<3;a++) p[a]=poses[38].m[3][a];
        assert(fabsf(p[1]-53.46f)<1.0f);
        gevrGexPoseWalk(&hdr,&base,active->reload.anim,active->holdFrame,poses);
        assert(fabsf(poses[42].m[3][0])>1.0f);
        Mtxf anchor;
        gevrGexScreenAnchor(&hdr,&anchor);
        gevrGexPoseWalk(&hdr,&anchor,active->fireAnim,0,poses);
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
math=(ROOT/"src/game/matrixmath.c").read_text()
production=[function(math,s) for s in ("void matrix_4x4_multiply(","void matrix_4x4_set_identity(",
    "void matrix_4x4_set_rotation_around_xyz(")]
production.extend(function(gun,s) for s in ("static void gevrGexAnimPart(","static void gevrGexPoseWalk(","static void gevrGexShowMagazines(","static void gevrGexScreenAnchor("))
production.extend(function(gun,s) for s in ("static void gevrGexRigidInverse(const Mtxf *g, Mtxf *inv)\n{", "static void gevrGexPoseReady("))
production.extend(function(gun,s) for s in ("void gevrGexTick(", "void gevrGexMagazineReady("))
production.extend(function(gun,s) for s in ("static void gevrGexMtxPoint(", "static void gevrGexLeftHandTo(",
    "static void gevrGexOffCache(ModelFileHeader *gunhdr)\n{", "static s32 gevrGexOffHandPose(",
    "static void gevrGexPistolMagGrip(", "static s32 gevrGexOffSteady("))
production.extend(function(gun,s) for s in ("static void gevrGexHeldMagFitTo(", "static void gevrGexPistolSupportPose("))
production.append(function(gun,"static void gevrGexForeFrom("))
production.append(function(gun,"static s32 gevrGexMagazineFitting("))
production.extend(function(gun,s) for s in ("static s32 gevrGexOffHandConsumed(", "static void gevrGexWellPoint(", "void gevrReloadFitSetWell("))
view=(ROOT/"src/game/bondview2.c").read_text()
production.append(function(view,"static const char *gevrFitNextLine("))
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
        for model,item in (('Gak47Z',8),('GwppkZ',4),('GwppkZ',5),('Gtt33Z',6),('GskorpionZ',7),('GuziZ',9),('Gmp5kZ',10),('Gcmp150Z',11),('GcycloneZ',12),('Gm16Z',13)):
            sample=temp/'model.bin'; sample.write_bytes(rom.load(model))
            subprocess.run([str(exe),str(sample),str(Path(sys.argv[1]).resolve()),str(item)],check=True)
