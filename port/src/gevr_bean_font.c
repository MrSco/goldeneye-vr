/* Bean's bfont atlases, repainted onto native glyphs without changing menu
 * layout, wrapping or clipping. Record layout follows DAB's gebeanFontOpen
 * (adef1dc2, MIT; see port/licenses). Atlases remain full resolution. */
#include <bondtypes.h>
#include "gevr_bean.h"
#include "gevr_bean_int.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct beanFont {
    unsigned char *atlas, *records;
    int w, h, count, tried;
};
static struct beanFont fonts[2];
static unsigned be32(const unsigned char *p) {
    return (unsigned)p[0]<<24 | (unsigned)p[1]<<16 | (unsigned)p[2]<<8 | p[3];
}
static unsigned be16(const unsigned char *p) { return (unsigned)p[0]<<8 | p[1]; }
static int fits(unsigned offset, unsigned size, unsigned length) {
    return offset <= length && size <= length - offset;
}

static void loadFont(struct beanFont *f, int gothic) {
    static const char *roots[] = {"../texture-packs/ge007-bean/files", "added-content/xbla/files", "xbla/files"};
    char path[256];
    FILE *file = NULL;
    struct gevrCaff c;
    unsigned char *bytes;
    const unsigned char *record = NULL;
    unsigned length = 0, glyphs, pointers, count;
    long size;
    f->tried = 1;
    for (unsigned i=0; i<sizeof(roots)/sizeof(roots[0]) && !file; i++) {
        snprintf(path, sizeof(path), "%s/misc/%s/default.dt", roots[i], gothic ? "doc0" : "alps3");
        file = fopen(path, "rb");
    }
    if (!file) return;
    if (fseek(file, 0, SEEK_END) || (size=ftell(file)) < 0x54 || size > 16*1024*1024) { fclose(file); return; }
    rewind(file);
    bytes = malloc(size);
    if (!bytes || fread(bytes, 1, size, file) != (size_t)size) { free(bytes); fclose(file); return; }
    fclose(file);
    if (!gevrCaffOpen(&c, bytes, (unsigned)size)) { free(bytes); return; }
    for (unsigned i=0; i<c.numfiles; i++) {
        unsigned n;
        const unsigned char *p = gevrCaffBlob(&c, i, &n);
        if (p && n >= 0x54 && !memcmp(p, "bfont\0", 6)) { record=p; length=n; break; }
    }
    if (!record) goto done;
    count=be32(record+0x30); pointers=be32(record+0x50);
    if (!count || count > 4096 || !fits(pointers, 16, length)) goto done;
    glyphs=be32(record+pointers+8);
    if (!fits(glyphs, count*20, length)) goto done;
    for (unsigned i=0; i<c.numfiles && !f->atlas; i++) {
        unsigned n;
        const unsigned char *p=gevrCaffBlob(&c, i, &n);
        if (p && n >= 7 && !memcmp(p, "texture", 7))
            f->atlas=gevrBeanTexDecodeRgba(&c, i, &f->w, &f->h);
    }
    if (!f->atlas) goto done;
    f->records=malloc(count*20);
    if (!f->records) { free(f->atlas); f->atlas=NULL; goto done; }
    memcpy(f->records, record+glyphs, count*20);
    f->count=count;
done:
    gevrCaffClose(&c);
    free(bytes);
}

void gevrBeanFontReset(void) {
    for (int i=0; i<2; i++) { free(fonts[i].atlas); free(fonts[i].records); }
    memset(fonts, 0, sizeof(fonts));
}

void gevrBeanFontRepaint(struct fontchar *native, int gothic, int character) {
    const unsigned char *already;
    int width, height;
    if (!gevrBeanIsEnabled() || !native || !native->pixeldata || character < 0x21 || character > 0x7e
            || native->width <= 0 || native->height <= 0) return;
    if (gevrBeanRepaint(native->pixeldata, &already, &width, &height)) return;
    struct beanFont *f=&fonts[!!gothic];
    if (!f->tried) loadFont(f, gothic);
    if (!f->atlas) return;
    for (int i=0; i<f->count; i++) {
        const unsigned char *g=f->records+i*20;
        if (be16(g+18) != (unsigned)character) continue;
        int x=(int)((int16_t)be16(g+4)/32768.0f*f->w);
        int y=(int)((int16_t)be16(g+6)/32768.0f*f->h);
        int w=g[2], h=g[3];
        if (!w || !h || x < 0 || y < 0 || x+w > f->w || y+h > f->h) return;
        /* Texture rectangles use padded native widths. Keep the same ratio
         * of ink to padding when replacing their small I8 tile with RGBA. */
        int padded=(native->width+7)&~7;
        int pw=(w*padded+native->width-1)/native->width;
        unsigned char *rgba=calloc((size_t)pw*h, 4);
        if (!rgba) return;
        /* The atlas decodes bottom row first and y counts from its top; the
         * native glyph's row 0 is its top, so the glyph is written top down.
         * Writing it bottom up as well drew every letter upside down. */
        for (int row=0; row<h; row++) for (int col=0; col<w; col++) {
            const unsigned char *src=f->atlas+((f->h-1-y-row)*f->w+x+col)*4;
            unsigned char *dst=rgba+(row*pw+col)*4;
            dst[0]=dst[1]=dst[2]=255; dst[3]=src[3];
        }
        gevrBeanRepaintBind(native->pixeldata, rgba, pw, h);
        return;
    }
}
