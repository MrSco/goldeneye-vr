#include <ultra64.h>
#include <memp.h>
#include "title.h"
#include "initintromatrices.h"

void alloc_intro_matrices(void)
{
    D_8002A7D0 = 0;
    matrixBufferRareLogo0     = s_matrixBufferRareLogo0;
    matrixBufferGunbarrel0    = s_matrixBufferGunbarrel0;
    matrixBufferRareLogo1     = s_matrixBufferRareLogo1;
    matrixBufferRareLogo2     = s_matrixBufferRareLogo2;
    matrixBufferGunbarrel1    = s_matrixBufferGunbarrel1;
    matrixBufferIntroBackdrop = s_matrixBufferIntroBackdrop;
    matrixBufferIntroBond     = s_matrixBufferIntroBond;
}


