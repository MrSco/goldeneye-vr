/* Cartridge-to-host conversion for GoldenEye's level metadata and collision
 * tiles. Per-room compressed display lists are separate, not native Gfx. */
#include "gevr_stage.h"
#include <stdlib.h>
#include <string.h>

static uint32_t read32(const uint8_t *p) {
    return (uint32_t)p[0]<<24 | (uint32_t)p[1]<<16 | (uint32_t)p[2]<<8 | p[3];
}
static uint16_t read16(const uint8_t *p) { return (uint16_t)p[0]<<8 | p[1]; }
static void swap32(uint8_t *p) { uint32_t v=read32(p); memcpy(p,&v,4); }
static void swap16(uint8_t *p) { uint16_t v=read16(p); memcpy(p,&v,2); }
static size_t align16(size_t n) { return (n+15)&~(size_t)15; }
static uint32_t offset(uint32_t n) { return n&0xffffff; }

uint32_t gevrBgHeaderSize(const uint8_t h[64]) {
    uint32_t rooms=offset(read32(h+4));
    if (read32(h)!=0 || rooms+28>64) return 0;
    return (offset(read32(h+rooms+24))+15)&~15u;
}

size_t gevrConvertBg(uint8_t *data, size_t size, size_t capacity) {
    size_t nr=1, np=0, i, j, ro, po, eo, fo, roomsDst, portalsDst, end;
    uint8_t *src;
    if (size<64 || capacity<size || read32(data)!=0) return 0;
    ro=offset(read32(data+4)); po=offset(read32(data+8));
    eo=offset(read32(data+12)); fo=offset(read32(data+16));
    if (ro<20 || po<20) return 0;
    /* Include room zero and the final terminator entry. */
    while (ro+(nr+1)*24<=size && read32(data+ro+nr*24+4)) ++nr;
    if (ro+(nr+1)*24>size) return 0;
    ++nr;
    while (po+(np+1)*8<=size && read32(data+po+np*8)) ++np;
    if (po+(np+1)*8>size) return 0;
    ++np;
    roomsDst=align16(size); portalsDst=align16(roomsDst+nr*sizeof(struct gevrBgRoom));
    end=portalsDst+np*sizeof(struct gevrBgPortal);
    if (end>capacity || end>=0x1000000) return 0;
    src=malloc(size);
    if (!src) return 0;
    memcpy(src,data,size);
    memset(data+size,0,end-size);
    for (i=0;i<nr;++i) {
        const uint8_t *s=src+ro+i*24;
        struct gevrBgRoom *d=(struct gevrBgRoom *)(data+roomsDst)+i;
        d->points=(void *)(uintptr_t)read32(s);
        d->primary=(void *)(uintptr_t)read32(s+4);
        d->secondary=(void *)(uintptr_t)read32(s+8);
        for(j=0;j<3;++j) { uint32_t v=read32(s+12+j*4); memcpy(&d->pos[j],&v,4); }
    }
    for (i=0;i<np;++i) {
        const uint8_t *s=src+po+i*8;
        struct gevrBgPortal *d=(struct gevrBgPortal *)(data+portalsDst)+i;
        uint32_t p=read32(s), ofs=offset(p);
        d->points=(void *)(uintptr_t)p;
        d->room1=s[4]; d->room2=s[5]; d->flags1=s[6]; d->flags2=s[7];
        if(p) {
            if(ofs+4>size || ofs+4+src[ofs]*12>size) { free(src); return 0; }
            /* Read from immutable source: portals may share a polygon. */
            for(j=0;j<src[ofs]*3u;++j) {
                uint32_t v=read32(src+ofs+4+j*4); memcpy(data+ofs+4+j*4,&v,4);
            }
        }
    }
    if(eo) {
        for(i=eo;i+8<=size && src[i];i+=8) swap32(data+i+4);
        if(i+8>size) { free(src); return 0; }
    }
    /* Optional per-room scale array. */
    if(fo) {
        if(fo+(nr-1)*4>size) { free(src); return 0; }
        for(i=0;i<nr-1;++i) swap32(data+fo+i*4);
    }
    for(i=0;i<5;++i) { uint32_t v=read32(src+i*4); memcpy(data+i*4,&v,4); }
    { uint32_t v=0x0f000000|(uint32_t)roomsDst; memcpy(data+4,&v,4);
      v=0x0f000000|(uint32_t)portalsDst; memcpy(data+8,&v,4); }
    free(src);
    return end;
}

size_t gevrConvertStan(uint8_t *data, size_t size, size_t capacity) {
    size_t count=0, i, j, prefixSize, shift, end, tile, first;
    uint8_t *src;
    void **rooms;
    if(size<12 || read32(data)!=0) return 0;
    while(4+(count+1)*4<=size && read32(data+4+count*4)) ++count;
    if(!count || 4+(count+1)*4>size) return 0;
    first=read32(data+4);
    if(first!=4+(count+1)*4) return 0;
    prefixSize=offsetof(struct gevrStanPrefix,firstroom)+(count+1)*sizeof(void *);
    /* Preserve original tile offsets for the 16-bit relative link indices.
     * Include space before firstroom for its conventional -0x80 link base. */
    shift=align16(prefixSize+128); end=shift+size;
    if(end>capacity) return 0;
    src=malloc(size);
    if(!src) return 0;
    memcpy(src,data,size); memmove(data+shift,src,size); memset(data,0,shift);
    rooms=(void **)(data+offsetof(struct gevrStanPrefix,firstroom));
    for(i=0;i<count;++i) {
        uint32_t ofs=read32(src+4+i*4);
        if(ofs<first || ofs>=size) { free(src); return 0; }
        rooms[i]=data+shift+ofs;
    }
    for(tile=first;tile+8<=size && read32(src+tile);) {
        uint8_t *d=data+shift+tile;
        unsigned points=read16(src+tile+6)>>12;
        size_t bytes=8+8*(points<3?3:points);
        if(points>10 || tile+bytes>size) { free(src); return 0; }
        /*
         * Header must stay matchable via stanMatchTileName, which reads the
         * tile as StandTilePoint*: LE u16 at +0 == stanIdHi, byte at +2 ==
         * stanIdLo, room at +3. N64 stores those three id bytes then room;
         * only the first u16 needs an endian swap.
         */
        memcpy(d, src+tile, bytes);
        swap16(d+0);
        swap16(d+4);
        swap16(d+6);
        for(j=8;j<bytes;j+=2) swap16(d+j);
        tile+=bytes;
    }
    if(tile+8>size) { free(src); return 0; }
    free(src);
    return end;
}
