"""Pose small gadget switch tables without reading past their allocation."""
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
source = (ROOT / 'src/game/gevr_heldgun.c').read_text()
start = source.index('static void gevrHeldGunSwitch(')
end = source.index('/* ------------------------------------------------------------- the fit */', start)
production = source[start:end]
fixture = r'''
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef int s32;
typedef struct {float m[4][4];} Mtxf;
typedef struct {float p[3];} coord3d;
union ModelRwData {struct {int unk00;} Raw;};
typedef struct {int index;coord3d *Data;union ModelRwData rw;} ModelNode;
typedef struct {int numSwitches,numMatrices;ModelNode**Switches;void*Skeleton;} ModelFileHeader;
typedef struct {int unused;} Model;
static int skeleton_gun_revolver;
static union ModelRwData*modelGetNodeRwData(Model*m,ModelNode*n){(void)m;return &n->rw;}
static void matrix_4x4_copy(const Mtxf*a,Mtxf*b){*b=*a;}
static void matrix_4x4_set_identity_and_position(const coord3d*p,Mtxf*m){(void)p;memset(m,0,sizeof(*m));}
static void matrix_4x4_multiply(const Mtxf*a,const Mtxf*b,Mtxf*out){(void)b;*out=*a;}
static int modelFindNodeMtxIndex(const ModelNode*n,int i){(void)i;return n->index;}
/* PRODUCTION */
int main(){
    Model model={0};Mtxf root={0};
    for(int n=0;n<=36;n++)for(int revolver=0;revolver<2;revolver++){
        ModelNode**switches=calloc(n?n:1,sizeof(*switches));
        ModelNode*nodes=calloc(n?n:1,sizeof(*nodes));coord3d pos={0};
        for(int k=0;k<n;k++){nodes[k].index=0;nodes[k].Data=&pos;nodes[k].rw.Raw.unk00=99;switches[k]=nodes+k;}
        ModelFileHeader h={n,1,switches,revolver?&skeleton_gun_revolver:NULL};
        gevrHeldGunStaticPose(&model,&h);
        for(int k=0;k<n;k++){
            int expected=(k==1 || (k>=8 && k<=13) || k==17 || k==35)?0:
                ((k>=14 && k<=15) || (k>=18 && k<=27))?1:99;
            assert(nodes[k].rw.Raw.unk00==expected);
        }
        Mtxf*matrices=calloc(1,sizeof(*matrices));
        gevrHeldGunMatrices(&h,&root,matrices);
        // Missing switch entries are valid too.
        for(int k=0;k<n;k++)switches[k]=NULL;
        gevrHeldGunStaticPose(&model,&h);gevrHeldGunMatrices(&h,&root,matrices);
        free(matrices);free(nodes);free(switches);
    }
    puts("PASS: gadget/gun switch tables of length 0..36 and small matrix arrays, including missing nodes, under ASan/UBSan");
}
'''
with tempfile.TemporaryDirectory(prefix='gevr-slot-pose-') as directory:
    temp = Path(directory)
    source, exe = temp / 'pose.c', temp / 'pose'
    source.write_text(fixture.replace('/* PRODUCTION */', production))
    subprocess.run([shutil.which('gcc') or 'gcc', '-std=c11', '-O2', '-Wall', '-Werror',
                    '-fsanitize=address,undefined', '-fno-omit-frame-pointer', '-no-pie',
                    str(source), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
