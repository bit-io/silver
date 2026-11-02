#ifndef SILVER_FX_H
#define SILVER_FX_H

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define FX_A(p) ((int)(((p) >> 24) & 255))
#define FX_R(p) ((int)(((p) >> 16) & 255))
#define FX_G(p) ((int)(((p) >> 8) & 255))
#define FX_B(p) ((int)((p) & 255))
#define FX_PACK(a, r, g, b) ((((uint32_t)(a)) << 24) | (((uint32_t)(r)) << 16) | (((uint32_t)(g)) << 8) | ((uint32_t)(b)))

static inline int fx_clamp255(int v) { return v < 0 ? 0 : v > 255 ? 255 : v; }
static inline float fx_clampf(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }

/* ---------------------------------------------------------------- rozmycie */

/* jedno przejście rozmycia pudełkowego o promieniu r (okno 2r+1, poza buforem = przezroczystość) */
static inline void fx_box_pass(const uint32_t *src, uint32_t *dst, int w, int h, int r, int vertical) {
    if (r < 1) { memcpy(dst, src, (size_t)w * (size_t)h * 4); return; }
    int len = vertical ? h : w, lines = vertical ? w : h;
    int stride = vertical ? w : 1, lstride = vertical ? 1 : w;
    int win = 2 * r + 1;
    for (int l = 0; l < lines; l++) {
        const uint32_t *s = src + (size_t)l * lstride;
        uint32_t *d = dst + (size_t)l * lstride;
        long sa = 0, sr = 0, sg = 0, sb = 0;
        for (int i = -r; i <= r; i++) {
            if (i >= 0 && i < len) {
                uint32_t p = s[(size_t)i * stride];
                sa += FX_A(p); sr += FX_R(p); sg += FX_G(p); sb += FX_B(p);
            }
        }
        for (int i = 0; i < len; i++) {
            d[(size_t)i * stride] = FX_PACK((int)((sa + win / 2) / win), (int)((sr + win / 2) / win),
                                            (int)((sg + win / 2) / win), (int)((sb + win / 2) / win));
            int out_i = i - r, in_i = i + r + 1;
            if (out_i >= 0 && out_i < len) {
                uint32_t p = s[(size_t)out_i * stride];
                sa -= FX_A(p); sr -= FX_R(p); sg -= FX_G(p); sb -= FX_B(p);
            }
            if (in_i >= 0 && in_i < len) {
                uint32_t p = s[(size_t)in_i * stride];
                sa += FX_A(p); sr += FX_R(p); sg += FX_G(p); sb += FX_B(p);
            }
        }
    }
}

/* rozmycie Gaussa przybliżone trzema przejściami pudełkowymi (odchylenie standardowe sigma, px) */
static inline int fx_gauss_blur(uint32_t *px, int w, int h, float sigma) {
    if (sigma <= 0.3f || w < 1 || h < 1) return 1;
    uint32_t *tmp = (uint32_t *)malloc((size_t)w * (size_t)h * 4);
    if (!tmp) return 0;
    float ideal = sqrtf(12.0f * sigma * sigma / 3.0f + 1.0f);
    int wl = (int)floorf(ideal);
    if (wl % 2 == 0) wl--;
    if (wl < 1) wl = 1;
    int wu = wl + 2;
    float m_ideal = (12.0f * sigma * sigma - 3.0f * wl * wl - 12.0f * wl - 9.0f) / (-4.0f * wl - 4.0f);
    int m = (int)lroundf(m_ideal);
    for (int i = 0; i < 3; i++) {
        int size = i < m ? wl : wu;
        int r = (size - 1) / 2;
        fx_box_pass(px, tmp, w, h, r, 0);
        fx_box_pass(tmp, px, w, h, r, 1);
    }
    free(tmp);
    return 1;
}

/* ------------------------------------------------------ macierze kolorów */

static inline void fx_unpremul(uint32_t p, float *r, float *g, float *b, float *a) {
    int ia = FX_A(p);
    *a = ia / 255.0f;
    if (ia == 0) { *r = *g = *b = 0; return; }
    *r = FX_R(p) / (float)ia; *g = FX_G(p) / (float)ia; *b = FX_B(p) / (float)ia;
    if (*r > 1) *r = 1;
    if (*g > 1) *g = 1;
    if (*b > 1) *b = 1;
}

static inline uint32_t fx_premul(float r, float g, float b, float a) {
    a = fx_clampf(a, 0, 1);
    return FX_PACK(fx_clamp255((int)lroundf(a * 255.0f)), fx_clamp255((int)lroundf(fx_clampf(r, 0, 1) * a * 255.0f)),
                   fx_clamp255((int)lroundf(fx_clampf(g, 0, 1) * a * 255.0f)),
                   fx_clamp255((int)lroundf(fx_clampf(b, 0, 1) * a * 255.0f)));
}

/* filter: saturate(s) — s = 1.0 bez zmian (macierz z Filter Effects) */
static inline void fx_saturate(uint32_t *px, long n, float s) {
    for (long i = 0; i < n; i++) {
        float r, g, b, a;
        fx_unpremul(px[i], &r, &g, &b, &a);
        if (a <= 0) continue;
        float nr = (0.213f + 0.787f * s) * r + (0.715f - 0.715f * s) * g + (0.072f - 0.072f * s) * b;
        float ng = (0.213f - 0.213f * s) * r + (0.715f + 0.285f * s) * g + (0.072f - 0.072f * s) * b;
        float nb = (0.213f - 0.213f * s) * r + (0.715f - 0.715f * s) * g + (0.072f + 0.928f * s) * b;
        px[i] = fx_premul(nr, ng, nb, a);
    }
}

/* filter: hue-rotate(deg) */
static inline void fx_hue_rotate(uint32_t *px, long n, float deg) {
    float rad = deg * 3.14159265358979f / 180.0f, c = cosf(rad), s = sinf(rad);
    for (long i = 0; i < n; i++) {
        float r, g, b, a;
        fx_unpremul(px[i], &r, &g, &b, &a);
        if (a <= 0) continue;
        float nr = (0.213f + c * 0.787f - s * 0.213f) * r + (0.715f - c * 0.715f - s * 0.715f) * g + (0.072f - c * 0.072f + s * 0.928f) * b;
        float ng = (0.213f - c * 0.213f + s * 0.143f) * r + (0.715f + c * 0.285f + s * 0.140f) * g + (0.072f - c * 0.072f - s * 0.283f) * b;
        float nb = (0.213f - c * 0.213f - s * 0.787f) * r + (0.715f - c * 0.715f + s * 0.715f) * g + (0.072f + c * 0.928f + s * 0.072f) * b;
        px[i] = fx_premul(nr, ng, nb, a);
    }
}

/* filter: drop-shadow(dx dy sigma kolor) — cień z kanału alfa pod oryginałem */
static inline int fx_drop_shadow(uint32_t *px, int w, int h, int dx, int dy, float sigma, int cr, int cg, int cb, int ca) {
    uint32_t *sh = (uint32_t *)calloc((size_t)w * (size_t)h, 4);
    if (!sh) return 0;
    for (int y = 0; y < h; y++) {
        int sy = y - dy;
        if (sy < 0 || sy >= h) continue;
        for (int x = 0; x < w; x++) {
            int sx = x - dx;
            if (sx < 0 || sx >= w) continue;
            int a = FX_A(px[(size_t)sy * w + sx]);
            if (!a) continue;
            int aa = a * ca / 255;
            sh[(size_t)y * w + x] = FX_PACK(aa, cr * aa / 255, cg * aa / 255, cb * aa / 255);
        }
    }
    fx_gauss_blur(sh, w, h, sigma);
    for (long i = 0; i < (long)w * h; i++) {
        uint32_t s = px[i], d = sh[i];
        int ia = 255 - FX_A(s);
        px[i] = FX_PACK(fx_clamp255(FX_A(s) + FX_A(d) * ia / 255), fx_clamp255(FX_R(s) + FX_R(d) * ia / 255),
                        fx_clamp255(FX_G(s) + FX_G(d) * ia / 255), fx_clamp255(FX_B(s) + FX_B(d) * ia / 255));
    }
    free(sh);
    return 1;
}

/* ------------------------------------------------------------------ maski */

/* mnoży piksele (premultiplied) przez maskę 0..255 */
static inline void fx_apply_mask(uint32_t *px, const uint8_t *mask, long n) {
    for (long i = 0; i < n; i++) {
        int m = mask[i];
        uint32_t p = px[i];
        px[i] = FX_PACK(FX_A(p) * m / 255, FX_R(p) * m / 255, FX_G(p) * m / 255, FX_B(p) * m / 255);
    }
}

/* z bufora warstwy maski: mode 0 = alfa, 1 = luminancja (CSS mask-mode) */
static inline void fx_mask_from_pixels(const uint32_t *px, uint8_t *mask, long n, int mode) {
    for (long i = 0; i < n; i++) {
        uint32_t p = px[i];
        if (mode == 1) {
            /* piksele są przemnożone alfą, więc luminancja premultiplied już zawiera alfę */
            int l = (int)lroundf(0.2126f * FX_R(p) + 0.7152f * FX_G(p) + 0.0722f * FX_B(p));
            mask[i] = (uint8_t)fx_clamp255(l);
        } else {
            mask[i] = (uint8_t)FX_A(p);
        }
    }
}

/* mask-composite wg CSS: `acc` = warstwy poniżej (destination), `next` = bieżąca warstwa wyżej (source).
 * 0 replace, 1 add (source-over), 2 subtract (source-out), 3 intersect (source-in), 4 exclude (xor) */
static inline void fx_mask_combine(uint8_t *acc, const uint8_t *next, long n, int op) {
    for (long i = 0; i < n; i++) {
        int a = acc[i], b = next[i], r;
        switch (op) {
        case 1: r = b + a * (255 - b) / 255; break;                       /* source-over */
        case 2: r = b * (255 - a) / 255; break;                           /* subtract: source poza destination */
        case 3: r = a * b / 255; break;                                   /* intersect */
        case 4: r = a * (255 - b) / 255 + b * (255 - a) / 255; break;     /* exclude */
        default: r = b; break;
        }
        acc[i] = (uint8_t)fx_clamp255(r);
    }
}

/* ------------------------------------------------------ tryby mieszania */

enum {
    FX_NORMAL = 0, FX_MULTIPLY, FX_SCREEN, FX_OVERLAY, FX_DARKEN, FX_LIGHTEN, FX_COLOR_DODGE, FX_COLOR_BURN,
    FX_HARD_LIGHT, FX_SOFT_LIGHT, FX_DIFFERENCE, FX_EXCLUSION, FX_HUE, FX_SATURATION, FX_COLOR, FX_LUMINOSITY,
    FX_PLUS_LIGHTER
};

static inline int fx_blend_mode_from_name(const char *n) {
    static const char *names[] = { "normal", "multiply", "screen", "overlay", "darken", "lighten", "color-dodge",
                                   "color-burn", "hard-light", "soft-light", "difference", "exclusion", "hue",
                                   "saturation", "color", "luminosity", "plus-lighter" };
    if (!n) return FX_NORMAL;
    for (int i = 0; i < (int)(sizeof(names) / sizeof(names[0])); i++)
        if (strcmp(n, names[i]) == 0) return i;
    return FX_NORMAL;
}

static inline float fx_blend_sep(int mode, float cb, float cs) {
    switch (mode) {
    case FX_MULTIPLY: return cb * cs;
    case FX_SCREEN: return cb + cs - cb * cs;
    case FX_OVERLAY: return fx_blend_sep(FX_HARD_LIGHT, cs, cb);
    case FX_DARKEN: return cb < cs ? cb : cs;
    case FX_LIGHTEN: return cb > cs ? cb : cs;
    case FX_COLOR_DODGE:
        if (cb == 0) return 0;
        if (cs >= 1) return 1;
        return fminf(1.0f, cb / (1.0f - cs));
    case FX_COLOR_BURN:
        if (cb >= 1) return 1;
        if (cs <= 0) return 0;
        return 1.0f - fminf(1.0f, (1.0f - cb) / cs);
    case FX_HARD_LIGHT:
        if (cs <= 0.5f) return cb * 2.0f * cs;
        return cb + (2.0f * cs - 1.0f) - cb * (2.0f * cs - 1.0f);
    case FX_SOFT_LIGHT: {
        if (cs <= 0.5f) return cb - (1.0f - 2.0f * cs) * cb * (1.0f - cb);
        float d = cb <= 0.25f ? ((16.0f * cb - 12.0f) * cb + 4.0f) * cb : sqrtf(cb);
        return cb + (2.0f * cs - 1.0f) * (d - cb);
    }
    case FX_DIFFERENCE: return fabsf(cb - cs);
    case FX_EXCLUSION: return cb + cs - 2.0f * cb * cs;
    case FX_PLUS_LIGHTER: return fminf(1.0f, cb + cs);
    default: return cs;
    }
}

static inline float fx_lum(const float c[3]) { return 0.3f * c[0] + 0.59f * c[1] + 0.11f * c[2]; }

static inline void fx_clip_color(float c[3]) {
    float l = fx_lum(c), n = fminf(c[0], fminf(c[1], c[2])), x = fmaxf(c[0], fmaxf(c[1], c[2]));
    if (n < 0) for (int i = 0; i < 3; i++) c[i] = l + (c[i] - l) * l / (l - n);
    if (x > 1) for (int i = 0; i < 3; i++) c[i] = l + (c[i] - l) * (1 - l) / (x - l);
}

static inline void fx_set_lum(const float c[3], float l, float out[3]) {
    float d = l - fx_lum(c);
    for (int i = 0; i < 3; i++) out[i] = c[i] + d;
    fx_clip_color(out);
}

static inline float fx_sat(const float c[3]) { return fmaxf(c[0], fmaxf(c[1], c[2])) - fminf(c[0], fminf(c[1], c[2])); }

static inline void fx_set_sat(const float c[3], float s, float out[3]) {
    int imax = 0, imin = 0;
    for (int i = 1; i < 3; i++) { if (c[i] > c[imax]) imax = i; if (c[i] < c[imin]) imin = i; }
    if (imax == imin) { out[0] = out[1] = out[2] = 0; return; }
    int imid = 3 - imax - imin;
    float cmax = c[imax], cmid = c[imid], cmin = c[imin];
    out[imax] = s;
    out[imid] = (cmid - cmin) * s / (cmax - cmin);
    out[imin] = 0;
}

static inline void fx_blend_nonsep(int mode, const float cb[3], const float cs[3], float out[3]) {
    float t[3];
    switch (mode) {
    case FX_HUE: fx_set_sat(cs, fx_sat(cb), t); fx_set_lum(t, fx_lum(cb), out); break;
    case FX_SATURATION: fx_set_sat(cb, fx_sat(cs), t); fx_set_lum(t, fx_lum(cb), out); break;
    case FX_COLOR: fx_set_lum(cs, fx_lum(cb), out); break;
    default: fx_set_lum(cb, fx_lum(cs), out); break;   /* luminosity */
    }
}

/* Składa `src` (premultiplied, krycie dodatkowo × alpha/255) na `dst` (premultiplied, wynik w dst)
 * wg trybu mieszania W3C Compositing: Co·αo = (1−αs)·αb·Cb + (1−αb)·αs·Cs + αs·αb·B(Cb,Cs). */
static inline void fx_blend(uint32_t *dst, const uint32_t *src, long n, int mode, int alpha) {
    float k = alpha / 255.0f;
    for (long i = 0; i < n; i++) {
        float sr, sg, sb, sa, br, bg, bb, ba;
        fx_unpremul(src[i], &sr, &sg, &sb, &sa);
        sa *= k;
        if (sa <= 0.0f) continue;
        fx_unpremul(dst[i], &br, &bg, &bb, &ba);
        float cb[3] = { br, bg, bb }, cs[3] = { sr, sg, sb }, bl[3];
        if (mode >= FX_HUE && mode <= FX_LUMINOSITY) fx_blend_nonsep(mode, cb, cs, bl);
        else for (int c = 0; c < 3; c++) bl[c] = fx_blend_sep(mode, cb[c], cs[c]);
        float ao = sa + ba * (1.0f - sa);
        float out[3];
        for (int c = 0; c < 3; c++) {
            float v = (1.0f - sa) * ba * cb[c] + (1.0f - ba) * sa * cs[c] + sa * ba * bl[c];
            out[c] = v;   /* już przemnożone αo */
        }
        dst[i] = FX_PACK(fx_clamp255((int)lroundf(ao * 255.0f)), fx_clamp255((int)lroundf(out[0] * 255.0f)),
                         fx_clamp255((int)lroundf(out[1] * 255.0f)), fx_clamp255((int)lroundf(out[2] * 255.0f)));
    }
}

/* ============================================================ gradienty CSS
 * Specyfikacja (tekst): "<RODZAJ>|<PARAMETRY>|<PRZYSTANKI>"
 *   RODZAJ:  L linear, RL repeating-linear, R radial, RR repeating-radial, C conic, RC repeating-conic
 *   linear:  PARAMETRY = "<kąt°>" albo "to:tl|tr|bl|br"
 *   radial:  "<circle|ellipse>;<closest-side|closest-corner|farthest-side|farthest-corner|rx[,ry]>;<cx>,<cy>"
 *            (długości: 12px albo 40%, środek: px albo %)
 *   conic:   "<kąt startu°>;<cx>,<cy>"
 *   PRZYSTANKI: "r,g,b,a@poz;r,g,b,a@poz;h@poz…"  poz: 12px | 40% | 30deg | pusty;  "h" = podpowiedź (color hint)
 * Kolory są interpolowane w przestrzeni przemnożonej alfą (domyślne zachowanie CSS).
 */
typedef struct { float t, r, g, b, a; int hint; int has_pos; float raw; char unit; } FxStop;

static inline float fx_parse_len(const char *s, char *unit) {
    char *end = NULL;
    float v = (float)strtod(s, &end);
    *unit = 'x';
    if (end && *end == '%') *unit = '%';
    else if (end && strncmp(end, "deg", 3) == 0) *unit = 'd';
    else if (end && strncmp(end, "turn", 4) == 0) { *unit = 'd'; v *= 360.0f; }
    else if (end && strncmp(end, "rad", 3) == 0) { *unit = 'd'; v *= 180.0f / 3.14159265358979f; }
    else if (end && strncmp(end, "grad", 4) == 0) { *unit = 'd'; v *= 0.9f; }
    return v;
}

/* parsuje przystanki; zwraca ich liczbę. Pozycje bezwzględne zamienia na t wg `scale` (długość promienia/linii w px lub 360 dla conic). */
static inline int fx_parse_stops(const char *spec, FxStop *st, int maxn, float scale, int conic) {
    int n = 0;
    const char *p = spec;
    while (*p && n < maxn) {
        const char *e = p;
        while (*e && *e != ';') e++;
        char tok[96];
        int len = (int)(e - p);
        if (len > 95) len = 95;
        memcpy(tok, p, (size_t)len); tok[len] = 0;
        p = *e ? e + 1 : e;
        if (!len) continue;
        FxStop s = { 0, 0, 0, 0, 1, 0, 0, 0, 'x' };
        char *at = strchr(tok, '@');
        if (at) *at = 0;
        if (tok[0] == 'h' && tok[1] == 0) {
            s.hint = 1;
        } else {
            int r = 0, g = 0, b = 0; float a = 255;
            sscanf(tok, "%d,%d,%d,%f", &r, &g, &b, &a);
            float aa = a / 255.0f;
            s.r = r / 255.0f * aa; s.g = g / 255.0f * aa; s.b = b / 255.0f * aa; s.a = aa;
        }
        if (at && at[1]) {
            char unit;
            float v = fx_parse_len(at + 1, &unit);
            s.has_pos = 1; s.raw = v; s.unit = unit;
            if (unit == '%') s.t = v / 100.0f;
            else if (unit == 'd') s.t = conic ? v / 360.0f : 0;
            else s.t = scale > 0 ? v / scale : 0;
        }
        st[n++] = s;
    }
    return n;
}

/* uzupełnia brakujące pozycje wg specyfikacji (pierwszy 0, ostatni 1, reszta rozłożona równo; pozycje nie maleją) */
static inline void fx_fix_stops(FxStop *st, int n) {
    if (n < 1) return;
    if (!st[0].has_pos) { st[0].t = 0; st[0].has_pos = 1; }
    if (!st[n - 1].has_pos) { st[n - 1].t = 1; st[n - 1].has_pos = 1; }
    float prev = st[0].t;
    for (int i = 1; i < n; i++) {
        if (st[i].has_pos) { if (st[i].t < prev) st[i].t = prev; prev = st[i].t; }
    }
    int i = 0;
    while (i < n) {
        if (st[i].has_pos) { i++; continue; }
        int j = i;
        while (j < n && !st[j].has_pos) j++;
        float a = st[i - 1].t, b = st[j].t;
        for (int k = i; k < j; k++) st[k].t = a + (b - a) * (float)(k - i + 1) / (float)(j - i + 1);
        i = j;
    }
}

static inline void fx_stop_color(const FxStop *st, int n, float t, float out[4]) {
    if (n == 0) { out[0] = out[1] = out[2] = out[3] = 0; return; }
    if (t <= st[0].t) { out[0] = st[0].r; out[1] = st[0].g; out[2] = st[0].b; out[3] = st[0].a; return; }
    for (int i = 0; i + 1 < n; i++) {
        if (t < st[i + 1].t || i + 2 == n) {
            const FxStop *a = &st[i], *b = &st[i + 1];
            int ai = i, bi = i + 1;
            /* podpowiedź (hint) między dwoma kolorami przesuwa środek przejścia */
            float hint_t = -1;
            if (a->hint) { ai = i - 1; hint_t = a->t; }
            if (b->hint) { bi = i + 2 < n ? i + 2 : i + 1; hint_t = b->t; }
            const FxStop *ca = (ai >= 0 && !st[ai].hint) ? &st[ai] : a;
            const FxStop *cb = (bi < n && !st[bi].hint) ? &st[bi] : b;
            float t0 = ca->t, t1 = cb->t;
            if (t >= t1) { out[0] = cb->r; out[1] = cb->g; out[2] = cb->b; out[3] = cb->a; return; }
            if (t1 <= t0) { out[0] = cb->r; out[1] = cb->g; out[2] = cb->b; out[3] = cb->a; return; }
            float f = (t - t0) / (t1 - t0);
            if (hint_t >= 0) {
                float h = (hint_t - t0) / (t1 - t0);
                if (h <= 0) f = 1; else if (h >= 1) f = 0;
                else f = powf(f, logf(0.5f) / logf(h));
            }
            out[0] = ca->r + (cb->r - ca->r) * f; out[1] = ca->g + (cb->g - ca->g) * f;
            out[2] = ca->b + (cb->b - ca->b) * f; out[3] = ca->a + (cb->a - ca->a) * f;
            return;
        }
    }
    const FxStop *l = &st[n - 1];
    out[0] = l->r; out[1] = l->g; out[2] = l->b; out[3] = l->a;
}

static inline float fx_axis_val(const char *tok, float full) {
    char unit; float v = fx_parse_len(tok, &unit);
    return unit == '%' ? v * full / 100.0f : v;
}

/* Rysuje gradient o rozmiarze w×h do `out` (ARGB przemnożone). Zwraca 1 przy sukcesie. */
static inline int fx_gradient_render(uint32_t *out, int w, int h, const char *spec) {
    if (!spec || w < 1 || h < 1) return 0;
    char buf[2048];
    strncpy(buf, spec, sizeof(buf) - 1); buf[sizeof(buf) - 1] = 0;
    char *k1 = strchr(buf, '|');
    if (!k1) return 0;
    *k1 = 0;
    char *par = k1 + 1;
    char *k2 = strchr(par, '|');
    if (!k2) return 0;
    *k2 = 0;
    char *stops_s = k2 + 1;
    int repeating = buf[0] == 'R' && buf[1] != 0;
    char kind = repeating ? buf[1] : buf[0];
    FxStop st[64];
    int n;
    float fw = (float)w, fh = (float)h;
    if (kind == 'L') {
        float ang;
        float dx, dy;
        if (strncmp(par, "to:", 3) == 0) {
            if (strcmp(par + 3, "tr") == 0) { dx = fh; dy = -fw; }
            else if (strcmp(par + 3, "tl") == 0) { dx = -fh; dy = -fw; }
            else if (strcmp(par + 3, "br") == 0) { dx = fh; dy = fw; }
            else { dx = -fh; dy = fw; }
            float m = sqrtf(dx * dx + dy * dy);
            dx /= m; dy /= m;
        } else {
            ang = (float)atof(par) * 3.14159265358979f / 180.0f;
            dx = sinf(ang); dy = -cosf(ang);
        }
        float len = fabsf(fw * dx) + fabsf(fh * dy);
        if (len < 1e-3f) len = 1;
        n = fx_parse_stops(stops_s, st, 64, len, 0);
        fx_fix_stops(st, n);
        float t0 = st[0].t, period = n > 0 ? st[n - 1].t - st[0].t : 1;
        for (int y = 0; y < h; y++)
            for (int x = 0; x < w; x++) {
                float t = (((float)x + 0.5f - fw / 2) * dx + ((float)y + 0.5f - fh / 2) * dy) / len + 0.5f;
                if (repeating && period > 1e-6f) { t = t0 + fmodf(t - t0, period); if (t < t0) t += period; }
                float c[4];
                fx_stop_color(st, n, t, c);
                out[(size_t)y * w + x] = fx_premul(c[0] / (c[3] > 0 ? c[3] : 1), c[1] / (c[3] > 0 ? c[3] : 1), c[2] / (c[3] > 0 ? c[3] : 1), c[3]);
            }
        return 1;
    }
    /* radial / conic: wspólny środek */
    char shape[16] = "ellipse", size[64] = "farthest-corner", cpos[64] = "50%,50%";
    float from = 0;
    if (kind == 'R') {
        char *a = par, *b = strchr(a, ';');
        if (b) {
            *b = 0; b++;
            char *c = strchr(b, ';');
            if (c) { *c = 0; c++; strncpy(cpos, c, sizeof(cpos) - 1); }
            strncpy(size, b, sizeof(size) - 1);
        }
        strncpy(shape, a, sizeof(shape) - 1);
    } else {
        char *a = par, *b = strchr(a, ';');
        if (b) { *b = 0; b++; strncpy(cpos, b, sizeof(cpos) - 1); }
        from = (float)atof(a);
    }
    float cx, cy;
    {
        char *comma = strchr(cpos, ',');
        char xs[32], ys[32];
        if (comma) { *comma = 0; strncpy(xs, cpos, 31); xs[31] = 0; strncpy(ys, comma + 1, 31); ys[31] = 0; }
        else { strncpy(xs, "50%", 31); strncpy(ys, "50%", 31); }
        cx = fx_axis_val(xs, fw); cy = fx_axis_val(ys, fh);
    }
    if (kind == 'R') {
        int circle = strcmp(shape, "circle") == 0;
        float rx, ry;
        float l = cx, r = fw - cx, t = cy, b = fh - cy;
        float mnx = fminf(fabsf(l), fabsf(r)), mny = fminf(fabsf(t), fabsf(b));
        float mxx = fmaxf(fabsf(l), fabsf(r)), mxy = fmaxf(fabsf(t), fabsf(b));
        if (strcmp(size, "closest-side") == 0) {
            if (circle) rx = ry = fminf(mnx, mny); else { rx = mnx; ry = mny; }
        } else if (strcmp(size, "farthest-side") == 0) {
            if (circle) rx = ry = fmaxf(mxx, mxy); else { rx = mxx; ry = mxy; }
        } else if (strcmp(size, "closest-corner") == 0 || strcmp(size, "farthest-corner") == 0) {
            int far = size[0] == 'f';
            float ex = far ? mxx : mnx, ey = far ? mxy : mny;
            if (circle) rx = ry = sqrtf(ex * ex + ey * ey);
            else {
                /* elipsa o proporcjach closest/farthest-side przechodząca przez narożnik */
                float sx = far ? mxx : mnx, sy = far ? mxy : mny;
                float k = sqrtf(2.0f);
                rx = sx * k; ry = sy * k;
                if (sx < 1e-6f) { rx = 1e-3f; }
                if (sy < 1e-6f) { ry = 1e-3f; }
            }
        } else {
            char *comma = strchr(size, ',');
            if (comma) { *comma = 0; rx = fx_axis_val(size, fw); ry = fx_axis_val(comma + 1, fh); }
            else { rx = fx_axis_val(size, fw); ry = circle ? rx : rx; }
            if (!comma && !circle) ry = fx_axis_val(size, fh);
        }
        if (rx < 1e-3f) rx = 1e-3f;
        if (ry < 1e-3f) ry = 1e-3f;
        n = fx_parse_stops(stops_s, st, 64, rx, 0);
        fx_fix_stops(st, n);
        float t0 = st[0].t, period = n > 0 ? st[n - 1].t - st[0].t : 1;
        for (int y = 0; y < h; y++)
            for (int x = 0; x < w; x++) {
                float ux = ((float)x + 0.5f - cx) / rx, uy = ((float)y + 0.5f - cy) / ry;
                float t = sqrtf(ux * ux + uy * uy);
                if (repeating && period > 1e-6f) { t = t0 + fmodf(t - t0, period); if (t < t0) t += period; }
                float c[4];
                fx_stop_color(st, n, t, c);
                float ia = c[3] > 0 ? c[3] : 1;
                out[(size_t)y * w + x] = fx_premul(c[0] / ia, c[1] / ia, c[2] / ia, c[3]);
            }
        return 1;
    }
    /* conic */
    n = fx_parse_stops(stops_s, st, 64, 360.0f, 1);
    fx_fix_stops(st, n);
    float t0 = st[0].t, period = n > 0 ? st[n - 1].t - st[0].t : 1;
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            float ang = atan2f((float)x + 0.5f - cx, -((float)y + 0.5f - cy)) * 180.0f / 3.14159265358979f;  /* 0° = góra, zgodnie z zegarem */
            ang -= from;
            ang = fmodf(ang, 360.0f);
            if (ang < 0) ang += 360.0f;
            float t = ang / 360.0f;
            if (repeating && period > 1e-6f) { t = t0 + fmodf(t - t0, period); if (t < t0) t += period; }
            float c[4];
            fx_stop_color(st, n, t, c);
            float ia = c[3] > 0 ? c[3] : 1;
            out[(size_t)y * w + x] = fx_premul(c[0] / ia, c[1] / ia, c[2] / ia, c[3]);
        }
    return 1;
}

#endif /* SILVER_FX_H */
