/* Read-only census using the production CAFF reader, texture decoder and loader.
 * JSON goes to stdout; copyrighted asset bytes never leave the input files. */
#include "../src/gevr_bean_load.c"

void videoFreeCachedTexture(const void *addr) { (void)addr; }

struct faceKey { int64_t p[9]; int winding; };
static int faceCmp(const void *aa, const void *bb)
{
    const struct faceKey *a=aa, *b=bb;
    for(int k=0;k<9;k++) if(a->p[k]!=b->p[k]) return a->p[k]<b->p[k] ? -1:1;
    return 0;
}

/* Exact coincident non-decal geometry, within 0.001 file units. This is a
 * candidate census, not a visibility proof or a substitute for DAB's broader
 * plane/coverage tests. Opposite faces can be intentional two-faced sheets. */
static void coincident(const struct gevrBeanMesh *m)
{
    struct faceKey *keys=calloc(m->ntris,sizeof(*keys));
    if(!keys) return;
    int n=0,groups=0,opposed=0,faces=0;
    for(int i=0;i<m->ntris;i++) {
        const struct gevrBeanTri *t=m->tris+i;
        float normal[3];
        if((t->flags&GEVR_BEAN_F_DECAL) || triNormalArea(t->p,normal)<=0) continue;
        struct faceKey *key=keys+n++;
        for(int k=0;k<9;k++) key->p[k]=(int64_t)llround(t->p[k]*1000.0);
        key->winding=1;
        for(int a=0;a<2;a++) for(int b=a+1;b<3;b++) {
            int order=0;
            for(int k=0;k<3;k++) if(key->p[a*3+k]!=key->p[b*3+k]) { order=key->p[a*3+k]>key->p[b*3+k];break; }
            if(order) {
                for(int k=0;k<3;k++) {int64_t v=key->p[a*3+k];key->p[a*3+k]=key->p[b*3+k];key->p[b*3+k]=v;}
                key->winding=-key->winding;
            }
        }
    }
    qsort(keys,n,sizeof(*keys),faceCmp);
    for(int a=0;a<n;) {
        int b=a+1,signs=keys[a].winding>0 ? 1:2;
        while(b<n && !faceCmp(keys+a,keys+b)) { signs |= keys[b].winding>0 ? 1:2;b++; }
        if(b-a>1) {groups++;faces+=b-a;opposed+=signs==3;}
        a=b;
    }
    printf(",\"coincidentNonDecalGroups\":%d,\"coincidentNonDecalFaces\":%d,\"oppositeWindingGroups\":%d",groups,faces,opposed);
    free(keys);
}

static void string(const char *s)
{
    putchar('"');
    for (; *s; s++) {
        unsigned char c = (unsigned char)*s;
        if (c == '"' || c == '\\') putchar('\\');
        if (c >= 32) putchar(c);
    }
    putchar('"');
}

int main(int argc, char **argv)
{
    if (argc < 2) return 2;
    FILE *f = fopen(argv[1], "rb");
    if (!f) return 2;
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    rewind(f);
    uint8_t *bytes = malloc((size_t)size);
    if (!bytes || fread(bytes, 1, (size_t)size, f) != (size_t)size) return 2;
    fclose(f);
    struct gevrCaff c;
    if (!gevrCaffOpen(&c, bytes, (uint32_t)size)) return 3;
    int texfile[256], nt = 0;
    uint32_t dl = 0, sl = 0;
    const uint8_t *d = gevrCaffBlob(&c, gevrCaffFind(&c, 1, ".data"), &dl);
    const uint8_t *st = gevrCaffBlob(&c, gevrCaffFind(&c, 1, ".stream"), &sl);
    printf("{\"textures\":[");
    for (uint32_t i = 0; i < c.numfiles; i++) {
        uint32_t len = 0;
        const uint8_t *b = gevrCaffBlob(&c, (int)i, &len);
        if (!b || len < 0x40 || memcmp(b, "texture\0", 8)) continue;
        if (nt) putchar(',');
        if (nt < 256) texfile[nt] = (int)i;
        nt++;
        int w = 0, h = 0;
        uint8_t *rgba = gevrBeanTexDecodeRgba(&c, (int)i, &w, &h);
        uint64_t sum[4] = {0};
        int soft = 0, clear = 0, magenta = 0;
        if (rgba) for (int p = 0; p < w*h; p++) {
            for (int k = 0; k < 4; k++) sum[k] += rgba[p*4+k];
            clear += rgba[p*4+3] < 255;
            soft += rgba[p*4+3] > 0 && rgba[p*4+3] < 240;
            magenta += rgba[p*4] >= rgba[p*4+1]+64 && rgba[p*4+2] >= rgba[p*4+1]+64 &&
                abs(rgba[p*4]-rgba[p*4+2]) < 48;
        }
        printf("{\"name\":"); string(gevrCaffAssetName(&c, c.files[i].asset));
        printf(",\"type\":%u,\"format\":%u,\"frames\":%u,\"headerSize\":[%u,%u],\"decodedSize\":[%d,%d],\"decoded\":%s,\"alphaPixels\":%d,\"softPixels\":%d,\"placeholder\":%s,\"meanRGBA\":[",
            be32(b+0x1c), b[0x1b]&63, be32(b+0x38), be16(b+0x24), be16(b+0x26), w,h,
            rgba ? "true" : "false", clear, soft,
            rgba && w<=16 && h<=16 && magenta*10>=w*h*9 ? "true" : "false");
        for (int k=0;k<4;k++) printf("%s%.3f",k?",":"",w*h ? (double)sum[k]/(w*h) : 0.0);
        printf("]}"); free(rgba);
    }
    printf("],\"motion\":[");
    if (d && dl>=0xe0 && !memcmp(d,"rendergraph",12)) {
        uint32_t block = be32(d+0xdc);
        if (block && fits(block,16,dl)) {
            uint32_t n=be32(d+block+8), entries=be32(d+block+12);
            if (n<=256 && fits(entries,(uint64_t)n*0x64,dl)) for(uint32_t e=0;e<n;e++) {
                const uint8_t *p=d+entries+e*0x64;
                if(e) putchar(',');
                printf("{\"attribute\":");
                uint32_t attr=be32(p+8);
                string(attr<dl && memchr(d+attr,0,dl-attr) ? (const char *)d+attr : "");
                printf(",\"scroll\":[%.8g,%.8g],\"rotation\":%.8g,\"frames\":%u,\"secondsPerFrame\":%.8g,\"runs\":%s,\"references\":[",
                    bef(p+0x2c),bef(p+0x30),bef(p+0x40),be32(p+0x4c),bef(p+0x50),be32(p+0x58)?"true":"false");
                uint32_t list=be32(p+0x18),nr=be32(p+0x20);
                if(nr<=64 && fits(list,nr*4,dl)) for(uint32_t r=0;r<nr;r++) printf("%s%u",r?",":"",be32(d+list+r*4));
                printf("]}");
            }
        }
    }
    printf("],\"materials\":[");
    int first=1;
    uint32_t shader=0;
    if(st && sl>=0x28 && nt<=256) {
        uint32_t end=be32(st+4); if(end>sl) end=sl;
        /* Flat census includes alternative pieces; active draws below use the
         * production loader's switch walk and actual emitted geometry. */
        for(uint32_t pc=0x24;fits(pc,4,end);) {
            uint32_t tag=be32(st+pc),sz=tag>>16,type=(tag>>8)&255;
            if(sz<4 || !fits(pc,sz,end)) break;
            if(type==2 && sz>=8) shader=be32(st+pc+4);
            if(type==0x2d && sz>=20) {
                if(!first) putchar(','); first=0;
                uint32_t generic=materialTex(&c,st,pc,sz,sl,texfile,nt);
                printf("{\"offset\":%u,\"shader\":%u,\"selected\":%u,\"genericSelected\":%u,\"extra\":%u,\"inputs\":[",pc,shader,surfaceTex(&c,st,pc,sz,texfile,nt,argv[1],generic),generic,be16(st+pc+10));
                for(uint32_t k=0;12+8*k+8<=sz;k++) printf("%s[%u,%u,%u]",k?",":"",be32(st+pc+12+8*k),be32(st+pc+16+8*k)>>16,be32(st+pc+16+8*k)&65535);
                printf("]}");
            }
            pc+=sz;
        }
    }
    printf("]");
    gevrCaffClose(&c); free(bytes);
    if(argc>2) {
        struct gevrBeanMesh *m=NULL;
        int ok=gevrBeanLoad(argv[1],1,&m);
        printf(",\"meshLoaded\":%s",ok && m ? "true":"false");
        if(m) {
            int missing=0, invalid=0, zero=0, tinted=0, hist[256]={0};
            int flash=0, slide=0, decal=0, reflected=0;
            for(int t=0;t<m->ntex;t++) missing+=!m->standin[t];
            for(int i=0;i<m->ntris;i++) {
                const struct gevrBeanTri *t=m->tris+i;
                reflected += t->reflection!=NULL;
                flash += !!(t->flags & GEVR_BEAN_F_FLASH);
                slide += !!(t->flags & GEVR_BEAN_F_SLIDE);
                decal += !!(t->flags & GEVR_BEAN_F_DECAL);
                invalid+=t->tex<0 || t->tex>=m->ntex;
                if(t->tex>=0 && t->tex<256) hist[t->tex]++;
                zero+=!t->alpha && (t->rgba[3] || t->rgba[7] || t->rgba[11]);
                int colour=0;for(int k=0;k<3;k++) colour |= t->rgba[k*4]!=255 || t->rgba[k*4+1]!=255 || t->rgba[k*4+2]!=255;
                tinted+=colour;
            }
            printf(",\"triangles\":%d,\"missingBindings\":%d,\"invalidTextureFaces\":%d,\"clearedFaces\":%d,\"tintedFaces\":%d,\"flashFaces\":%d,\"slideFaces\":%d,\"decalFaces\":%d,\"textureFaces\":[",m->ntris,missing,invalid,zero,tinted,flash,slide,decal);
            for(int t=0;t<m->ntex;t++) printf("%s%d",t?",":"",hist[t]);
            printf("]");
            printf(",\"normalReflectionFaces\":%d",reflected);
            if(strstr(argv[1],"/background/") || strstr(argv[1],"\\background\\")) coincident(m);
            gevrBeanMeshFree(m); gevrBeanTexReset();
        }
    }
    printf("}\n");
    return 0;
}
