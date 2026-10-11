#include "../src/gevr_bean_font.c"
#include <assert.h>
static int enabled=1;
int gevrBeanIsEnabled(void) { return enabled; }
void videoFreeCachedTexture(const void *p) { (void)p; }
int main(int argc, char **argv) {
    unsigned char nativePixels[94][256]={{0}};
    struct fontchar glyph={.index=0, .width=5, .height=7, .pixeldata=nativePixels[0]};
    /* Native index is an ordinal, not ASCII. The atlas is bottom row first
     * and the native glyph top row first; keep padding outside the glyph. */
    struct beanFont *f=&fonts[0];
    f->tried=1; f->w=f->h=4; f->count=1;
    f->records=calloc(20,1); f->atlas=calloc(16,4);
    f->records[2]=3; f->records[3]=2; f->records[19]='H';
    for(int row=0;row<2;row++) for(int col=0;col<3;col++) f->atlas[((3-row)*4+col)*4+3]=40+row*100+col;
    gevrBeanFontRepaint(&glyph,0,'H');
    const unsigned char *rgba; int w,h;
    assert(gevrBeanRepaint(glyph.pixeldata,&rgba,&w,&h) && w==5 && h==2);
    assert(rgba[3]==40 && rgba[w*4+3]==140 && rgba[3*4+3]==0);   /* the glyph's top row first */
    gevrBeanRepaintForget(); enabled=0;
    gevrBeanFontRepaint(&glyph,0,'H'); assert(!gevrBeanRepaint(glyph.pixeldata,&rgba,&w,&h));
    enabled=1; f->records[4]=0x7f; f->records[5]=0xff;
    gevrBeanFontRepaint(&glyph,0,'H'); assert(!gevrBeanRepaint(glyph.pixeldata,&rgba,&w,&h));
    gevrBeanFontReset();
    if(argc>1) {
        for(int gothic=0;gothic<2;gothic++) {
            loadFont(&fonts[gothic],gothic);
            assert(fonts[gothic].atlas && fonts[gothic].count > 94);
            for(int character=33;character<=126;character++) {
                glyph.pixeldata=nativePixels[character-33];
                gevrBeanFontRepaint(&glyph,gothic,character);
                assert(gevrBeanRepaint(glyph.pixeldata,&rgba,&w,&h));
                assert(w>=3 && h>=3);
                int ink=0;
                for(int p=0;p<w*h;p++) ink+=rgba[p*4+3]!=0;
                assert(ink>0);
                /* the right way up, by the letters' own shapes: T and V are
                 * widest at the top, A is narrowest there */
                if(character=='T'||character=='V'||character=='A') {
                    int first=-1,last=-1;
                    for(int r=0;r<h;r++) for(int c=0;c<w;c++) if(rgba[(r*w+c)*4+3]>128){ if(first<0) first=r; last=r; }
                    int span[2]={0,0};
                    for(int k=0;k<2;k++){ int r=k?last:first, lo=w, hi=-1;
                        for(int c=0;c<w;c++) if(rgba[(r*w+c)*4+3]>128){ if(c<lo) lo=c; if(c>hi) hi=c; }
                        span[k]=hi-lo; }
                    if(character=='A') assert(span[0]<span[1]); else assert(span[0]>span[1]);
                }
            }
            gevrBeanRepaintForget();
        }
    }
    gevrBeanFontReset(); gevrBeanRepaintForget();
    puts("Bean font atlas crop, bounds, toggling and glyph checks passed");
    (void)argv;
}
