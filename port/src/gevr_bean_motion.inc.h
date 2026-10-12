/* Texture scroll entries in the release rendergraph (DAB beanTexAnimation,
 * MIT, port/licenses/dabs-mod.txt). Rates are repeats per game second. */
static void readTextureScroll(struct gevrBeanMesh *mesh, const uint8_t *data, uint32_t dlen,
                              const uint8_t *stream, uint32_t slen)
{
    if (dlen < 0xe0 || memcmp(data, "rendergraph", 12) || slen < 0x28) return;
    uint32_t block = be32(data + 0xdc);
    if (!block || !fits(block, 16, dlen)) return;
    uint32_t n = be32(data + block + 8), entries = be32(data + block + 12);
    if (!n || n > 256 || !fits(entries, (uint64_t)n * 0x64, dlen)) return;
    uint32_t material[8], count = 0, end = be32(stream + 4);
    if (end > slen) end = slen;
    uint8_t seen[256] = {0};
    for (uint32_t pc = 0x24; fits(pc, 4, end);) {
        uint32_t tag = be32(stream + pc), size = tag >> 16, type = (tag >> 8) & 255;
        if (size < 4 || !fits(pc, size, end)) break;
        if (type == 0x2d && size >= 20) {
            count = 0;
            for (uint32_t k = be16(stream + pc + 10); 12 + 8*k + 8 <= size && count < 8; k++)
                material[count++] = be32(stream + pc + 12 + k*8);
        } else if (type == 5 && size >= 12 && count > 0) {
            uint32_t ptr = be32(stream + pc + 4);
            for (uint32_t e = 0; e < n; e++) {
                const uint8_t *entry = data + entries + e*0x64;
                uint32_t list = be32(entry + 0x18), refs = be32(entry + 0x20);
                if (refs > 64 || !fits(list, refs*4, dlen)) continue;
                for (uint32_t r = 0; r < refs; r++) {
                    if (be32(data + list + r*4) != ptr) continue;
                    int t = count == 1 ? (int)material[0] : -1;
                    uint32_t attr = be32(entry + 8);
                    /* A multi-input material animates the unique transparency
                     * picture, not its bump map or reflection (DAB). */
                    if (count > 1 && fits(attr, 13, dlen) && !memcmp(data + attr, "transparency", 13)) {
                        for (uint32_t k = 0; k < count; k++) {
                            const uint8_t *rgba; int w, h, alpha = 0;
                            if (material[k] >= (uint32_t)mesh->ntex ||
                                !gevrBeanTexture(mesh->standin[material[k]], &rgba, &w, &h)) continue;
                            for (int px = 0; px < w*h && !alpha; px++) alpha = rgba[px*4+3] < 255;
                            if (alpha) { if (t != -1) { t = -2; break; } t = material[k]; }
                        }
                    }
                    if (t < 0 || t >= mesh->ntex || seen[t]) break;
                    float s = bef(entry + 0x2c), v = bef(entry + 0x30);
                    if (!isfinite(s) || fabsf(s) >= 100) s = 0;
                    if (!isfinite(v) || fabsf(v) >= 100) v = 0;
                    gevrBeanTexScrollSet(mesh->standin[t], s, -v); /* emitted t runs upward */
                    seen[t] = 1;
                    if (s || v) GEVR_BEAN_LOG("gevr bean motion: texture %u scroll %.3f %.3f repeats/s", t, s, -v);
                    break;
                }
            }
        }
        pc += size;
    }
}
