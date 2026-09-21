#ifndef _INTRO_LOGOS_H_
#define _INTRO_LOGOS_H_
#include <ultra64.h>

extern u8 *barrelDisplayListPtr;
extern Gfx *gunbarrelgfxListPointer;
extern Mtx *matrixBufferRareLogo0;
extern Mtx *matrixBufferGunbarrel0;
extern Mtx *matrixBufferRareLogo1;
extern Mtx *matrixBufferRareLogo2;
extern Mtx *matrixBufferGunbarrel1;
extern Mtx *matrixBufferIntroBackdrop;
extern Mtx *matrixBufferIntroBond;

extern Mtx s_matrixBufferRareLogo0[2];
extern Mtx s_matrixBufferGunbarrel0[1];
extern Mtx s_matrixBufferRareLogo1[2];
extern Mtx s_matrixBufferRareLogo2[2];
extern Mtx s_matrixBufferGunbarrel1[2];
extern Mtx s_matrixBufferIntroBackdrop[2];
extern Mtx s_matrixBufferIntroBond[2];

extern f32 x;
extern f32 y;
extern f32 titleTransitionX;
extern f32 titleTransitionY;
extern s16 word_CODE_bss_80069584;
extern u8 *dword_CODE_bss_80069588;
extern u8 *dword_CODE_bss_8006958C;
extern u8 *virtualaddress;
extern s32 gunbarrelTimer;

extern u32 D_8002A7D0;

Gfx *titleRenderFolderMenuBackground(Gfx *gdl, s32 xOffset, struct FolderSelectColour *topColour, struct FolderSelectColour *bottomColour);
#endif
