"""Execute both production joint walkers and body-instance deduplication."""
from pathlib import Path
import os,shutil,subprocess,tempfile
ROOT=Path(__file__).resolve().parents[2]
def extract(source,name,start=0):
 a=source.index(name+'(',start);a=source.rindex('\n',0,a)+1;b=source.index('{',a);depth=1;e=b+1
 while depth:
  depth+=(source[e]=='{')-(source[e]=='}');e+=1
 return source[a:e]
source=(ROOT/'src/game/objecthandler.c').read_text()
pose=(ROOT/'port/src/gevr_bean_pose.inc.h').read_text()
helper=extract(pose,'gevrBeanWholeJointGdl')
prelude='''#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <assert.h>
#include <ultra64.h>
#include <bondtypes.h>
#include <bondconstants.h>
#define return_null() return
static int bodyCalls[2],nativeCalls;
static Model models[2];static Gfx body[1];
void *gevrBeanWholeModelGdl(Model *model){bodyCalls[model-models]++;return body;}
uintptr_t osVirtualToPhysical(void *p){return (uintptr_t)p;}
#define osSyncPrintf printf
float getjointsize(Model *m,ModelNode *n){return 1;}
void sub_GAME_7F074534(ModelRenderData *d,Model *m,ModelNode *n){nativeCalls++;}
'''
main='''int main(void){
 ModelFileHeader header={0};
#if defined(VERSION_US) || defined(VERSION_JP)
 header.isLoaded=1;
#endif
 models[0].obj=models[1].obj=&header;
 ModelNode nodes[4]={0};ModelHitEntry entries[4]={0};Gfx commands[64];
 for(int i=0;i<4;i++){
  nodes[i].Opcode=MODELNODE_OPCODE_HEADER;entries[i].rootnode=nodes+i;
  entries[i].model=models+(i==1);entries[i].next=i<3 ? entries+i+1:NULL;
 }
 for(int flags=1;flags<=3;flags++){
  ModelRenderData data={0};data.gdl=commands;data.flags=flags;
  bodyCalls[0]=bodyCalls[1]=nativeCalls=0;drawjointlist(&data,entries);
  assert(nativeCalls==4);
  assert(bodyCalls[0]==(flags&1) && bodyCalls[1]==(flags&1));
  assert(data.gdl-commands==4+2*(flags&1));
 }
 ModelRenderData data={0};data.gdl=commands;data.flags=1;data.unk18=1;
 bodyCalls[0]=bodyCalls[1]=nativeCalls=0;drawjointlist(&data,entries);
 assert(!nativeCalls && !bodyCalls[0] && !bodyCalls[1] && data.gdl==commands);
 puts("Production joint walker: interleaved instances, OPA/XLU gates and native visibility preserved");
}'''
with tempfile.TemporaryDirectory() as temp:
 p=Path(temp)
 for branch,start in [('US',0),('EU',source.index('#if defined(VERSION_EU)\nvoid drawjointlist('))]:
  f=p/'test.c';f.write_text(prelude+helper+extract(source,'drawjointlist',start)+main)
  exe=p/'test.exe'
  subprocess.run([shutil.which('gcc') or 'cc','-std=c11','-O2','-fms-extensions','-DPLATFORM_64BIT=1','-D_LANGUAGE_C','-DGEVR','-DVERSION_'+branch,*['-I'+str(ROOT/x) for x in ['.','include','src','src/game','port/include']],str(f),'-o',str(exe)],check=True)
  subprocess.run([str(exe)],check=True)
