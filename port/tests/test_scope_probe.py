"""The explicit scope probe alone supplies a synthetic right-hand pose."""
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
source = (ROOT / 'src/game/bondview2.c').read_text(encoding='utf-8')
start = source.index('static s32 gevrGripAxesRaw(')
opening = source.index('{', start)
depth, end = 1, opening + 1
while depth:
    depth += (source[end] == '{') - (source[end] == '}')
    end += 1
production = source[start:end]
fixture = r'''
#include <assert.h>
#include <math.h>
typedef int s32; typedef float f32;
#define TRUE 1
#define FALSE 0
#define GEVR_UNITS_PER_METRE 100.0f
static float D_800364CC = .2f;
static int s_gevrScopeProbe, tracked;
static int gevrVrGripPoseCamera(int ctrl,float *pos,float *q) {
    (void)ctrl; pos[0]=.25f;pos[1]=pos[2]=0;
    q[0]=q[1]=q[2]=0;q[3]=1;return tracked;
}
PRODUCTION
int main(void) {
    float p[3],r[3],u[3],b[3];
    assert(!gevrGripAxesRaw(1,p,r,u,b));
    s_gevrScopeProbe=1;
    assert(!gevrGripAxesRaw(0,p,r,u,b));
    assert(gevrGripAxesRaw(1,p,r,u,b));
    assert(p[0]==0 && fabsf(p[1]+2)<1e-6f && fabsf(p[2]+6)<1e-6f);
    for(int i=0;i<3;i++) {
        assert(r[i]==(i==0) && u[i]==(i==1) && b[i]==(i==2));
    }
    s_gevrScopeProbe=0;tracked=1;
    assert(gevrGripAxesRaw(1,p,r,u,b));
    assert(p[0]==5 && r[0]==1 && u[2]==-1 && b[1]==1);
    tracked=0;assert(!gevrGripAxesRaw(1,p,r,u,b));
}
'''.replace('PRODUCTION', production)
with tempfile.TemporaryDirectory(prefix='gevr-scope-probe-') as tmp:
    code, exe = Path(tmp)/'test.c', Path(tmp)/'test'
    code.write_text(fixture, encoding='utf-8')
    subprocess.run([shutil.which('gcc') or 'cc', '-std=c11', str(code), '-lm', '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
print('Scope probe: explicit opt-in, right hand only, scaled pose and tracked/default behavior passed')
