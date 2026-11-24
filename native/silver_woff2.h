#ifndef SILVER_WOFF2_H
#define SILVER_WOFF2_H
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#ifdef SILVER_WOFF2
#include <brotli/decode.h>

typedef struct { uint32_t tag, flags, orig_len, transform_len, src_off, out_off, out_len; int transformed; unsigned char *data; size_t data_len; } W2Tab;
typedef struct { unsigned char *p; size_t len, cap; int err; } W2Buf;
typedef struct { const unsigned char *p; size_t len, pos; int err; } W2Rd;

static const char *const W2_KNOWN[63] = {
    "cmap", "head", "hhea", "hmtx", "maxp", "name", "OS/2", "post", "cvt ", "fpgm", "glyf", "loca", "prep", "CFF ", "VORG", "EBDT", "EBLC", "gasp", "hdmx", "kern",
    "LTSH", "PCLT", "VDMX", "vhea", "vmtx", "BASE", "GDEF", "GPOS", "GSUB", "EBSC", "JSTF", "MATH", "CBDT", "CBLC", "COLR", "CPAL", "SVG ", "sbix", "acnt", "avar",
    "bdat", "bloc", "bsln", "cvar", "fdsc", "feat", "fmtx", "fvar", "gvar", "hsty", "just", "lcar", "mort", "morx", "opbd", "prop", "trak", "Zapf", "Silf", "Glat",
    "Gloc", "Feat", "Sill" };

static uint32_t w2_tag(const char *s) { return ((uint32_t)(unsigned char)s[0] << 24) | ((uint32_t)(unsigned char)s[1] << 16) | ((uint32_t)(unsigned char)s[2] << 8) | (unsigned char)s[3]; }
static uint32_t w2_rd32(const unsigned char *p) { return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3]; }
static uint32_t w2_rd16(const unsigned char *p) { return ((uint32_t)p[0] << 8) | p[1]; }
static void w2_wr32(unsigned char *p, uint32_t v) { p[0] = (unsigned char)(v >> 24); p[1] = (unsigned char)(v >> 16); p[2] = (unsigned char)(v >> 8); p[3] = (unsigned char)v; }
static void w2_wr16(unsigned char *p, uint32_t v) { p[0] = (unsigned char)(v >> 8); p[1] = (unsigned char)v; }

static int rd_u8(W2Rd *r) { if (r->pos >= r->len) { r->err = 1; return 0; } return r->p[r->pos++]; }
static int rd_u16(W2Rd *r) { int a = rd_u8(r), b = rd_u8(r); return (a << 8) | b; }
static uint32_t rd_u32(W2Rd *r) { uint32_t a = (uint32_t)rd_u16(r), b = (uint32_t)rd_u16(r); return (a << 16) | b; }
static uint32_t rd_base128(W2Rd *r) {
    uint32_t acc = 0;
    for (int i = 0; i < 5; i++) {
        int b = rd_u8(r);
        if (r->err) return 0;
        if (i == 0 && b == 0x80) { r->err = 1; return 0; }
        if (acc & 0xFE000000u) { r->err = 1; return 0; }
        acc = (acc << 7) | (uint32_t)(b & 0x7F);
        if (!(b & 0x80)) return acc;
    }
    r->err = 1;
    return 0;
}
static int rd_255u16(W2Rd *r) {
    int c = rd_u8(r);
    if (c == 253) return rd_u16(r);
    if (c == 255) return rd_u8(r) + 253;
    if (c == 254) return rd_u8(r) + 506;
    return c;
}
static W2Rd rd_sub(W2Rd *r, size_t n) {         /* podstrumień o długości n z bieżącej pozycji */
    W2Rd s = { r->p + r->pos, 0, 0, 0 };
    if (n > r->len - r->pos) { r->err = 1; return s; }
    s.len = n; r->pos += n;
    return s;
}

static void wb_put(W2Buf *b, const void *d, size_t n) {
    if (b->err) return;
    if (b->len + n > b->cap) {
        size_t nc = b->cap ? b->cap : 4096;
        while (nc < b->len + n) nc *= 2;
        unsigned char *np = (unsigned char *)realloc(b->p, nc);
        if (!np) { b->err = 1; return; }
        b->p = np; b->cap = nc;
    }
    if (n) memcpy(b->p + b->len, d, n);
    b->len += n;
}
static void wb_u8(W2Buf *b, int v) { unsigned char c = (unsigned char)v; wb_put(b, &c, 1); }
static void wb_u16(W2Buf *b, int v) { unsigned char c[2]; w2_wr16(c, (uint32_t)v & 0xFFFF); wb_put(b, c, 2); }
static void wb_pad4(W2Buf *b) { while (b->len & 3) wb_u8(b, 0); }

static int w2_with_sign(int flag, int base) { return (flag & 1) ? base : -base; }

/* trójki → punkty (x,y,on). Zwraca 1 przy sukcesie. */
static int w2_triplets(W2Rd *flags, W2Rd *glyph, int n, int *xs, int *ys, unsigned char *on) {
    int x = 0, y = 0;
    for (int i = 0; i < n; i++) {
        int f = rd_u8(flags);
        if (flags->err) return 0;
        on[i] = (f >> 7) ? 0 : 1;
        f &= 0x7F;
        int nb = f < 84 ? 1 : (f < 120 ? 2 : (f < 124 ? 3 : 4));
        if (glyph->pos + (size_t)nb > glyph->len) return 0;
        const unsigned char *d = glyph->p + glyph->pos;
        glyph->pos += (size_t)nb;
        int dx, dy;
        if (f < 10) { dx = 0; dy = w2_with_sign(f, ((f & 14) << 7) + d[0]); }
        else if (f < 20) { dx = w2_with_sign(f, (((f - 10) & 14) << 7) + d[0]); dy = 0; }
        else if (f < 84) { int b0 = f - 20, b1 = d[0]; dx = w2_with_sign(f, 1 + (b0 & 0x30) + (b1 >> 4)); dy = w2_with_sign(f >> 1, 1 + ((b0 & 0x0c) << 2) + (b1 & 0x0f)); }
        else if (f < 120) { int b0 = f - 84; dx = w2_with_sign(f, 1 + ((b0 / 12) << 8) + d[0]); dy = w2_with_sign(f >> 1, 1 + (((b0 % 12) >> 2) << 8) + d[1]); }
        else if (f < 124) { int b2 = d[1]; dx = w2_with_sign(f, (d[0] << 4) + (b2 >> 4)); dy = w2_with_sign(f >> 1, ((b2 & 0x0f) << 8) + d[2]); }
        else { dx = w2_with_sign(f, (d[0] << 8) + d[1]); dy = w2_with_sign(f >> 1, (d[2] << 8) + d[3]); }
        x += dx; y += dy;
        xs[i] = x; ys[i] = y;
    }
    return 1;
}

/* zapisuje prosty glif w formacie 'glyf' */
static void w2_store_simple(W2Buf *out, int ncont, const int *endpts, int npts, const int *xs, const int *ys, const unsigned char *on,
                            int bbox[4], const unsigned char *instr, int ilen, int overlap) {
    wb_u16(out, ncont);
    for (int i = 0; i < 4; i++) wb_u16(out, bbox[i]);
    for (int i = 0; i < ncont; i++) wb_u16(out, endpts[i]);
    wb_u16(out, ilen);
    wb_put(out, instr, (size_t)ilen);
    unsigned char *fl = (unsigned char *)malloc((size_t)npts + 1);
    if (!fl) { out->err = 1; return; }
    int lx = 0, ly = 0;
    for (int i = 0; i < npts; i++) {
        int dx = xs[i] - lx, dy = ys[i] - ly, f = on[i] ? 1 : 0;
        if (i == 0 && overlap) f |= 0x40;
        if (dx == 0) f |= 0x10; else if (dx > -256 && dx < 256) f |= 0x02 | (dx > 0 ? 0x10 : 0);
        if (dy == 0) f |= 0x20; else if (dy > -256 && dy < 256) f |= 0x04 | (dy > 0 ? 0x20 : 0);
        fl[i] = (unsigned char)f; lx = xs[i]; ly = ys[i];
    }
    for (int i = 0; i < npts;) {                       /* flagi z kompresją powtórzeń */
        int j = i + 1;
        while (j < npts && fl[j] == fl[i] && j - i < 256) j++;
        int rep = j - i - 1;
        if (rep > 0) { wb_u8(out, fl[i] | 0x08); wb_u8(out, rep); i = j; } else { wb_u8(out, fl[i]); i++; }
    }
    lx = 0;
    for (int i = 0; i < npts; i++) { int dx = xs[i] - lx; lx = xs[i]; if (dx == 0) continue; if (dx > -256 && dx < 256) wb_u8(out, dx < 0 ? -dx : dx); else wb_u16(out, dx); }
    ly = 0;
    for (int i = 0; i < npts; i++) { int dy = ys[i] - ly; ly = ys[i]; if (dy == 0) continue; if (dy > -256 && dy < 256) wb_u8(out, dy < 0 ? -dy : dy); else wb_u16(out, dy); }
    free(fl);
}

/* rekonstrukcja glyf + loca; xmin_out[numGlyphs] = xMin każdego glifu (dla hmtx). 1 = ok */
static int w2_rebuild_glyf(const unsigned char *src, size_t len, W2Buf *glyf, W2Buf *loca, int *numGlyphs_out, int **xmin_out) {
    W2Rd r = { src, len, 0, 0 };
    rd_u16(&r);                                      /* reserved */
    int optflags = rd_u16(&r), ng = rd_u16(&r), idx_fmt = rd_u16(&r);
    uint32_t sz[7];
    for (int i = 0; i < 7; i++) sz[i] = rd_u32(&r);
    if (r.err || ng <= 0) return 0;
    W2Rd s_nc = rd_sub(&r, sz[0]), s_np = rd_sub(&r, sz[1]), s_fl = rd_sub(&r, sz[2]), s_gl = rd_sub(&r, sz[3]), s_co = rd_sub(&r, sz[4]), s_bb = rd_sub(&r, sz[5]), s_in = rd_sub(&r, sz[6]);
    W2Rd s_ov = { 0, 0, 0, 0 };
    if (optflags & 1) { s_ov = rd_sub(&r, (size_t)((ng + 7) >> 3)); }
    if (r.err) return 0;
    size_t bbm = (size_t)(((ng + 31) >> 5) << 2);
    if (bbm > s_bb.len) return 0;
    const unsigned char *bbit = s_bb.p;
    s_bb.pos = bbm;
    int *xmin = (int *)calloc((size_t)ng, sizeof(int));
    uint32_t *offs = (uint32_t *)calloc((size_t)ng + 1, sizeof(uint32_t));
    int *xs = NULL, *ys = NULL, *endpts = NULL; unsigned char *on = NULL;
    int cap = 0, ccap = 0, ok = 0;
    if (!xmin || !offs) goto done;
    for (int g = 0; g < ng; g++) {
        offs[g] = (uint32_t)glyf->len;
        int nc = (int16_t)rd_u16(&s_nc);
        if (s_nc.err) goto done;
        int have_bbox = (bbit[g >> 3] & (0x80 >> (g & 7))) != 0;
        if (nc == 0) { if (have_bbox) goto done; continue; }
        int bbox[4] = { 0, 0, 0, 0 };
        if (nc > 0) {
            if (nc > ccap) { free(endpts); endpts = (int *)malloc(sizeof(int) * (size_t)nc); ccap = nc; if (!endpts) goto done; }
            int npts = 0;
            for (int c = 0; c < nc; c++) { int n = rd_255u16(&s_np); if (s_np.err) goto done; npts += n; endpts[c] = npts - 1; }
            if (npts > 65535) goto done;
            if (npts > cap) { free(xs); free(ys); free(on); xs = (int *)malloc(sizeof(int) * (size_t)npts); ys = (int *)malloc(sizeof(int) * (size_t)npts); on = (unsigned char *)malloc((size_t)npts); cap = npts; if (!xs || !ys || !on) goto done; }
            if (!w2_triplets(&s_fl, &s_gl, npts, xs, ys, on)) goto done;
            int ilen = rd_255u16(&s_gl);
            if (s_gl.err || s_in.pos + (size_t)ilen > s_in.len) goto done;
            const unsigned char *instr = s_in.p + s_in.pos; s_in.pos += (size_t)ilen;
            if (have_bbox) { for (int i = 0; i < 4; i++) bbox[i] = (int16_t)rd_u16(&s_bb); if (s_bb.err) goto done; }
            else {
                int x0 = xs[0], x1 = xs[0], y0 = ys[0], y1 = ys[0];
                for (int i = 1; i < npts; i++) { if (xs[i] < x0) x0 = xs[i]; if (xs[i] > x1) x1 = xs[i]; if (ys[i] < y0) y0 = ys[i]; if (ys[i] > y1) y1 = ys[i]; }
                bbox[0] = x0; bbox[1] = y0; bbox[2] = x1; bbox[3] = y1;
            }
            int overlap = (optflags & 1) && s_ov.p && (s_ov.p[g >> 3] & (0x80 >> (g & 7)));
            w2_store_simple(glyf, nc, endpts, npts, xs, ys, on, bbox, instr, ilen, overlap);
            xmin[g] = bbox[0];
        } else {                                        /* złożony */
            if (!have_bbox) goto done;
            for (int i = 0; i < 4; i++) bbox[i] = (int16_t)rd_u16(&s_bb);
            wb_u16(glyf, -1);
            for (int i = 0; i < 4; i++) wb_u16(glyf, bbox[i]);
            size_t start = s_co.pos; int have_instr = 0, more = 1;
            while (more) {
                int fl = rd_u16(&s_co); rd_u16(&s_co);
                int skip = (fl & 1) ? 4 : 2;
                if (fl & 0x08) skip += 2; else if (fl & 0x40) skip += 4; else if (fl & 0x80) skip += 8;
                if (s_co.pos + (size_t)skip > s_co.len) goto done;
                s_co.pos += (size_t)skip;
                if (fl & 0x100) have_instr = 1;
                more = (fl & 0x20) != 0;
                if (s_co.err) goto done;
            }
            wb_put(glyf, s_co.p + start, s_co.pos - start);
            if (have_instr) {
                int ilen = rd_255u16(&s_gl);
                if (s_gl.err || s_in.pos + (size_t)ilen > s_in.len) goto done;
                wb_u16(glyf, ilen); wb_put(glyf, s_in.p + s_in.pos, (size_t)ilen); s_in.pos += (size_t)ilen;
            }
            xmin[g] = bbox[0];
        }
        wb_pad4(glyf);
        if (glyf->err) goto done;
    }
    offs[ng] = (uint32_t)glyf->len;
    for (int g = 0; g <= ng; g++) { if (idx_fmt == 0) wb_u16(loca, (int)(offs[g] >> 1)); else { unsigned char b[4]; w2_wr32(b, offs[g]); wb_put(loca, b, 4); } }
    ok = !glyf->err && !loca->err;
    *numGlyphs_out = ng; *xmin_out = xmin; xmin = NULL;
done:
    free(xmin); free(offs); free(xs); free(ys); free(on); free(endpts);
    return ok;
}

static uint32_t w2_checksum(const unsigned char *d, size_t n) {
    uint32_t s = 0; size_t i = 0;
    for (; i + 4 <= n; i += 4) s += w2_rd32(d + i);
    if (i < n) { unsigned char t[4] = { 0, 0, 0, 0 }; memcpy(t, d + i, n - i); s += w2_rd32(t); }
    return s;
}

/* Dekoduje WOFF2 z pamięci; *out malloc'owany (SFNT). 1 = ok. */
static int silver_woff2_decode(const unsigned char *in, size_t n, unsigned char **out, size_t *outn) {
    if (n < 48 || memcmp(in, "wOF2", 4) != 0) return 0;
    uint32_t flavor = w2_rd32(in + 4), total_sfnt = w2_rd32(in + 16), comp_size = w2_rd32(in + 20);
    int nt = (int)w2_rd16(in + 12);
    if (flavor == w2_tag("ttcf") || nt <= 0 || nt > 128 || total_sfnt > 256u * 1024u * 1024u) return 0;
    W2Rd r = { in, n, 48, 0 };
    W2Tab *tabs = (W2Tab *)calloc((size_t)nt, sizeof(W2Tab));
    unsigned char *dec = NULL, *res = NULL;
    int *xmin = NULL, ng = 0, ok = 0;
    W2Buf glyf = { 0 }, loca = { 0 }, hmtx = { 0 };
    if (!tabs) return 0;
    for (int i = 0; i < nt; i++) {
        int fl = rd_u8(&r), ti = fl & 0x3F, tv = (fl >> 6) & 3;
        W2Tab *t = &tabs[i];
        if (ti == 63) { t->tag = rd_u32(&r); } else t->tag = w2_tag(W2_KNOWN[ti]);
        t->flags = (uint32_t)fl;
        t->orig_len = rd_base128(&r);
        int is_glyf = t->tag == w2_tag("glyf"), is_loca = t->tag == w2_tag("loca"), is_hmtx = t->tag == w2_tag("hmtx");
        t->transformed = (is_glyf || is_loca) ? (tv == 0) : (is_hmtx ? (tv == 1) : (tv != 0));
        t->transform_len = t->transformed ? rd_base128(&r) : t->orig_len;
        if (r.err || (is_loca && t->transformed && t->transform_len != 0)) goto done;
    }
    size_t raw_total = 0;
    for (int i = 0; i < nt; i++) { raw_total += tabs[i].transform_len; if (raw_total > 512u * 1024u * 1024u) goto done; }
    if (r.pos + comp_size > n) goto done;
    dec = (unsigned char *)malloc(raw_total ? raw_total : 1);
    if (!dec) goto done;
    size_t dsz = raw_total;
    if (BrotliDecoderDecompress(comp_size, in + r.pos, &dsz, dec) != BROTLI_DECODER_RESULT_SUCCESS || dsz != raw_total) goto done;
    size_t off = 0;
    for (int i = 0; i < nt; i++) { tabs[i].data = dec + off; tabs[i].data_len = tabs[i].transform_len; off += tabs[i].transform_len; }
    /* glyf/loca */
    int gi = -1, li = -1, hi = -1, hh = -1, mx = -1;
    for (int i = 0; i < nt; i++) {
        if (tabs[i].tag == w2_tag("glyf")) gi = i; else if (tabs[i].tag == w2_tag("loca")) li = i;
        else if (tabs[i].tag == w2_tag("hmtx")) hi = i; else if (tabs[i].tag == w2_tag("hhea")) hh = i; else if (tabs[i].tag == w2_tag("maxp")) mx = i;
    }
    if (gi >= 0 && tabs[gi].transformed) {
        if (li < 0 || !w2_rebuild_glyf(tabs[gi].data, tabs[gi].data_len, &glyf, &loca, &ng, &xmin)) goto done;
        tabs[gi].data = glyf.p; tabs[gi].data_len = glyf.len;
        tabs[li].data = loca.p; tabs[li].data_len = loca.len; tabs[li].transformed = 0;
    }
    if (hi >= 0 && tabs[hi].transformed) {
        if (!xmin || hh < 0 || tabs[hh].data_len < 36) goto done;
        int nhm = (int)w2_rd16(tabs[hh].data + 34);
        W2Rd h = { tabs[hi].data, tabs[hi].data_len, 0, 0 };
        int hf = rd_u8(&h), lsb_omit = hf & 1, lsb2_omit = hf & 2;
        if (nhm <= 0 || nhm > ng) goto done;
        int *adv = (int *)malloc(sizeof(int) * (size_t)nhm); if (!adv) goto done;
        for (int i = 0; i < nhm; i++) adv[i] = rd_u16(&h);
        for (int i = 0; i < ng && !h.err; i++) {
            if (i < nhm) { wb_u16(&hmtx, adv[i]); }
            int lsb;
            if (i < nhm) lsb = lsb_omit ? xmin[i] : (int16_t)rd_u16(&h); else lsb = lsb2_omit ? xmin[i] : (int16_t)rd_u16(&h);
            wb_u16(&hmtx, lsb);
        }
        free(adv);
        if (h.err || hmtx.err) goto done;
        tabs[hi].data = hmtx.p; tabs[hi].data_len = hmtx.len; tabs[hi].transformed = 0;
    }
    (void)mx;
    /* składanie SFNT (tabele posortowane po tagu) */
    int *order = (int *)malloc(sizeof(int) * (size_t)nt);
    if (!order) goto done;
    for (int i = 0; i < nt; i++) order[i] = i;
    for (int i = 1; i < nt; i++) { int k = order[i], j = i - 1; while (j >= 0 && tabs[order[j]].tag > tabs[k].tag) { order[j + 1] = order[j]; j--; } order[j + 1] = k; }
    size_t sz = 12 + (size_t)nt * 16;
    for (int i = 0; i < nt; i++) sz += (tabs[i].data_len + 3) & ~(size_t)3;
    res = (unsigned char *)calloc(1, sz);
    if (!res) { free(order); goto done; }
    int sr = 1, es = 0; while (sr * 2 <= nt) { sr *= 2; es++; }
    w2_wr32(res, flavor); w2_wr16(res + 4, (uint32_t)nt); w2_wr16(res + 6, (uint32_t)sr * 16); w2_wr16(res + 8, (uint32_t)es); w2_wr16(res + 10, (uint32_t)(nt * 16 - sr * 16));
    size_t pos = 12 + (size_t)nt * 16, head_off = 0;
    for (int k = 0; k < nt; k++) {
        W2Tab *t = &tabs[order[k]];
        unsigned char *rec = res + 12 + (size_t)k * 16;
        w2_wr32(rec, t->tag); w2_wr32(rec + 8, (uint32_t)pos); w2_wr32(rec + 12, (uint32_t)t->data_len);
        memcpy(res + pos, t->data, t->data_len);
        if (t->tag == w2_tag("head") && t->data_len >= 12) { head_off = pos; w2_wr32(res + pos + 8, 0); }
        w2_wr32(rec + 4, w2_checksum(res + pos, t->data_len));
        pos += (t->data_len + 3) & ~(size_t)3;
    }
    free(order);
    if (head_off) w2_wr32(res + head_off + 8, 0xB1B0AFBAu - w2_checksum(res, sz));
    *out = res; *outn = sz; res = NULL; ok = 1;
done:
    free(res); free(dec); free(glyf.p); free(loca.p); free(hmtx.p); free(xmin); free(tabs);
    return ok;
}
#else
static int silver_woff2_decode(const unsigned char *in, size_t n, unsigned char **out, size_t *outn) { (void)in; (void)n; (void)out; (void)outn; return 0; }
#endif
#endif /* SILVER_WOFF2_H */
