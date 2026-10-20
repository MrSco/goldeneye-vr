#ifndef GEVR_BEAN_FOG_H
#define GEVR_BEAN_FOG_H
#include <math.h>
#include <stdint.h>

/* DAB retail/CE tables and transition rules, MIT (port/licenses/dabs-mod.txt).
 * Coordinates remain GoldenEye's: no Perfect Dark recentering offset. */
static inline int gevrBeanFogRange(int stage, float height, int community, float *end, uint8_t rgb[3])
{
    static const struct { int stage, end; uint32_t rgb; } rows[] = {
        {22,3500,0x000008},{23,10000,0},{24,3000,0},{25,1500,0x000008},
        {27,10000,0x100000},{28,15000,0},{29,7500,0x101820},{30,5000,0x000008},
        {31,5000,0x280000},{32,20000,0x103060},{33,15000,0x85adca},{34,5000,0x102010},
        {35,55000,0x85adca},{36,45000,0xa49682},{37,2500,0x182000},{38,6000,0x181828},
        {39,6000,0x080008},{41,30000,0x6080a0},{43,10000,0x201010}
    };
    if (!isfinite(height)) return 0;
    for (unsigned i=0;i<sizeof(rows)/sizeof(rows[0]);i++) if(rows[i].stage==stage) {
        *end=(float)rows[i].end;
        uint32_t c=rows[i].rgb;
        if(stage==33) {
            float t=(9161-height)/(9161-2782);
            *end+=65000*fminf(1,fmaxf(0,t));
        }
        /* The Community Edition (gevr_bean_ce.c): its table's distances and,
         * with its domes, its colours (DAB gebeanstage.c fogTableLoad(), read off
         * its patched default.xex); Surface 2's is its sf2 dome's horizon
         * (DAB gebeansky.c ceHorizons[]). */
        if(community) {
            switch(stage) {
            case 22:*end=5000;c=0x090708;break; case 24:case 25:*end=4500;break;
            case 37:*end=4500;c=0x797c79;break; case 38:*end=22500;c=0x103060;break;
            case 41:c=0x6e8196;break; case 43:*end=6500;c=0x575f5d;break;
            }
        }
        rgb[0]=(uint8_t)(c>>16);rgb[1]=(uint8_t)(c>>8);rgb[2]=(uint8_t)c;
        return 1;
    }
    return 0;
}
static inline int gevrBeanFogLineTransition(int stage, float height, float depth, int community,
        float share, float fraction, const uint8_t target[3], float line[2], uint8_t rgb[3])
{
    float end;
    if(!isfinite(depth) || depth<=0 || !isfinite(share) || share<=0 ||
       !gevrBeanFogRange(stage,height,community,&end,rgb)) return 0;
    end=-100+(end+100)*share;
    fraction=fminf(1,fmaxf(0,fraction));
    if(target) for(int i=0;i<3;i++) rgb[i]=(uint8_t)(rgb[i]+fraction*((float)target[i]-rgb[i]));
    line[0]=255/((end+100)*depth);line[1]=100*255/(end+100);
    return 1;
}
static inline int gevrBeanFogLine(int stage, float height, float depth, float line[2], uint8_t rgb[3])
{
    return gevrBeanFogLineTransition(stage,height,depth,0,1,0,0,line,rgb);
}
#endif
