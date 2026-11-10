#ifndef SILVER_SVGFX_H
#define SILVER_SVGFX_H

typedef struct { float *p; int w, h; int lin; } SfxImg;          /* RGBA premultiplikowane 0..1; lin: 1 = przestrzeń liniowa */
typedef struct { float a, b, c, d, e, f; } SfxM;                 /* [a c e; b d f] jak w SVG */
typedef struct { int pw, ph; float sc, ox, oy; int have; } SfxCtx;
typedef struct { int x0, y0, x1, y1; } SfxRect;
#define SFX_MAXARENA 1024
typedef struct { SfxImg *list[SFX_MAXARENA]; int n; } SfxArena;

static SfxImg *sfx_new(SfxArena *ar, int w, int h, int lin) {
    if (ar->n >= SFX_MAXARENA || w < 1 || h < 1) return NULL;
    SfxImg *im = (SfxImg *)calloc(1, sizeof *im);
    if (!im) return NULL;
    im->p = (float *)calloc((size_t)w * (size_t)h * 4, sizeof(float));
    if (!im->p) { free(im); return NULL; }
    im->w = w; im->h = h; im->lin = lin;
    ar->list[ar->n++] = im;
    return im;
}
static void sfx_arena_free(SfxArena *ar) {
    for (int i = 0; i < ar->n; i++) { free(ar->list[i]->p); free(ar->list[i]); }
    ar->n = 0;
}
static SfxImg *sfx_clone(SfxArena *ar, const SfxImg *s) {
    SfxImg *o = sfx_new(ar, s->w, s->h, s->lin);
    if (o) memcpy(o->p, s->p, (size_t)s->w * s->h * 4 * sizeof(float));
    return o;
}
static float sfx_clamp01(float v) { return v < 0 ? 0 : v > 1 ? 1 : v; }
static float sfx_s2l(float c) { return c <= 0.04045f ? c / 12.92f : powf((c + 0.055f) / 1.055f, 2.4f); }
static float sfx_l2s(float c) { return c <= 0.0031308f ? c * 12.92f : 1.055f * powf(c, 1.0f / 2.4f) - 0.055f; }

/* zmiana przestrzeni kolorów obrazu (na nieprzemnożonych składowych) */
static void sfx_space(SfxImg *im, int lin) {
    if (im->lin == lin) return;
    long n = (long)im->w * im->h;
    for (long i = 0; i < n; i++) {
        float *q = im->p + i * 4, a = q[3];
        if (a <= 0.0f) continue;
        for (int k = 0; k < 3; k++) {
            float c = sfx_clamp01(q[k] / a);
            c = lin ? sfx_s2l(c) : sfx_l2s(c);
            q[k] = c * a;
        }
    }
    im->lin = lin;
}
static SfxImg *sfx_in_space(SfxArena *ar, SfxImg *im, int lin) {
    if (im->lin == lin) return im;
    SfxImg *c = sfx_clone(ar, im);
    if (c) sfx_space(c, lin);
    return c;
}

/* ---------- transformacje ---------- */
static SfxM sfx_ident(void) { SfxM m = {1, 0, 0, 1, 0, 0}; return m; }
static SfxM sfx_mul(SfxM x, SfxM y) {          /* x*y: najpierw y, potem x */
    SfxM r;
    r.a = x.a * y.a + x.c * y.b; r.b = x.b * y.a + x.d * y.b;
    r.c = x.a * y.c + x.c * y.d; r.d = x.b * y.c + x.d * y.d;
    r.e = x.a * y.e + x.c * y.f + x.e; r.f = x.b * y.e + x.d * y.f + x.f;
    return r;
}
static void sfx_apply(SfxM m, float x, float y, float *ox, float *oy) { *ox = m.a * x + m.c * y + m.e; *oy = m.b * x + m.d * y + m.f; }
static int sfx_args(const char *s, const char **end, float *v, int max) {
    int n = 0;
    while (*s && *s != ')') {
        while (*s == ' ' || *s == ',' || *s == '\t' || *s == '\n') s++;
        if (*s == ')' || !*s) break;
        char *e; double d = strtod(s, &e);
        if (e == s) { s++; continue; }
        if (n < max) v[n++] = (float)d;
        s = e;
    }
    *end = s;
    return n;
}
static SfxM sfx_parse_transform(const char *s) {
    SfxM m = sfx_ident();
    while (s && *s) {
        while (*s == ' ' || *s == ',' || *s == '\t' || *s == '\n') s++;
        if (!*s) break;
        const char *par = strchr(s, '(');
        if (!par) break;
        char name[24]; int nl = (int)(par - s); if (nl > 23) nl = 23;
        memcpy(name, s, nl); name[nl] = 0;
        for (int i = nl - 1; i >= 0 && (name[i] == ' '); i--) name[i] = 0;
        float v[6] = {0}; const char *end;
        int n = sfx_args(par + 1, &end, v, 6);
        SfxM t = sfx_ident();
        if (!strcmp(name, "matrix") && n == 6) { t.a = v[0]; t.b = v[1]; t.c = v[2]; t.d = v[3]; t.e = v[4]; t.f = v[5]; }
        else if (!strcmp(name, "translate")) { t.e = n > 0 ? v[0] : 0; t.f = n > 1 ? v[1] : 0; }
        else if (!strcmp(name, "scale")) { t.a = n > 0 ? v[0] : 1; t.d = n > 1 ? v[1] : t.a; }
        else if (!strcmp(name, "rotate")) {
            float r = (n > 0 ? v[0] : 0) * 3.14159265f / 180.0f, c = cosf(r), sn = sinf(r);
            t.a = c; t.b = sn; t.c = -sn; t.d = c;
            if (n >= 3) { t.e = v[1] - c * v[1] + sn * v[2]; t.f = v[2] - sn * v[1] - c * v[2]; }
        }
        else if (!strcmp(name, "skewX")) { t.c = tanf((n > 0 ? v[0] : 0) * 3.14159265f / 180.0f); }
        else if (!strcmp(name, "skewY")) { t.b = tanf((n > 0 ? v[0] : 0) * 3.14159265f / 180.0f); }
        m = sfx_mul(m, t);
        s = (*end == ')') ? end + 1 : end;
    }
    return m;
}

/* ---------- rasteryzacja pojedynczego dokumentu SVG (NanoSVG) ---------- */
static SfxImg *sfx_raster(SfxArena *ar, const char *doc, SfxCtx *cx) {
    char *copy = strdup(doc);
    if (!copy) return NULL;
    NSVGimage *img = nsvgParse(copy, "px", 96.0f);
    free(copy);
    if (!img) return NULL;
    NSVGrasterizer *rast = nsvgCreateRasterizer();
    unsigned char *rgba = (unsigned char *)calloc((size_t)cx->pw * (size_t)cx->ph, 4);
    if (!rast || !rgba || img->width <= 0 || img->height <= 0) {
        if (rast) nsvgDeleteRasterizer(rast);
        free(rgba); nsvgDelete(img);
        return NULL;
    }
    float sx = (float)cx->pw / img->width, sy = (float)cx->ph / img->height;
    float sc = sx < sy ? sx : sy;
    float ox = ((float)cx->pw - img->width * sc) * 0.5f, oy = ((float)cx->ph - img->height * sc) * 0.5f;
    nsvgRasterize(rast, img, ox, oy, sc, rgba, cx->pw, cx->ph, cx->pw * 4);
    cx->sc = sc; cx->ox = ox; cx->oy = oy; cx->have = 1;
    SfxImg *o = sfx_new(ar, cx->pw, cx->ph, 0);
    if (o) {
        long n = (long)cx->pw * cx->ph;
        for (long i = 0; i < n; i++) {
            const unsigned char *q = rgba + i * 4;
            float a = q[3] / 255.0f;
            o->p[i * 4 + 0] = q[0] / 255.0f * a; o->p[i * 4 + 1] = q[1] / 255.0f * a;
            o->p[i * 4 + 2] = q[2] / 255.0f * a; o->p[i * 4 + 3] = a;
        }
    }
    free(rgba); nsvgDeleteRasterizer(rast); nsvgDelete(img);
    return o;
}

/* ---------- pary klucz=wartość ---------- */
#define SFX_ESC '\x1b'
static int sfx_kv(const char *s, const char *key, char *out, size_t n) {
    size_t kl = strlen(key);
    while (s && *s) {
        const char *e = strchr(s, SFX_ESC);
        size_t len = e ? (size_t)(e - s) : strlen(s);
        if (len > kl && !strncmp(s, key, kl) && s[kl] == '=') {
            size_t vl = len - kl - 1; if (vl >= n) vl = n - 1;
            memcpy(out, s + kl + 1, vl); out[vl] = 0;
            return 1;
        }
        if (!e) break;
        s = e + 1;
    }
    if (n) out[0] = 0;
    return 0;
}
static float sfx_kf(const char *s, const char *key, float def) {
    char b[64];
    if (!sfx_kv(s, key, b, sizeof b) || !b[0]) return def;
    return (float)atof(b);
}
static int sfx_color3(const char *s, const char *key, float rgb[3], float def) {
    char b[64];
    rgb[0] = rgb[1] = rgb[2] = def;
    if (!sfx_kv(s, key, b, sizeof b) || !b[0]) return 0;
    int r = 0, g = 0, bl = 0;
    if (sscanf(b, "%d,%d,%d", &r, &g, &bl) == 3) { rgb[0] = r / 255.0f; rgb[1] = g / 255.0f; rgb[2] = bl / 255.0f; return 1; }
    return 0;
}

/* ---------- operacje na pikselach ---------- */
static SfxRect sfx_alpha_bbox(const SfxImg *im) {
    SfxRect r = { im->w, im->h, 0, 0 };
    for (int y = 0; y < im->h; y++)
        for (int x = 0; x < im->w; x++)
            if (im->p[((long)y * im->w + x) * 4 + 3] > 0.002f) {
                if (x < r.x0) r.x0 = x; if (y < r.y0) r.y0 = y;
                if (x + 1 > r.x1) r.x1 = x + 1; if (y + 1 > r.y1) r.y1 = y + 1;
            }
    return r;
}
static void sfx_blur_axis(const float *src, float *dst, int w, int h, int horiz, float sigma) {
    int r = (int)ceilf(sigma * 3.0f); if (r > 150) r = 150; if (r < 1) r = 1;
    float *k = (float *)malloc(sizeof(float) * (2 * r + 1));
    if (!k) { memcpy(dst, src, (size_t)w * h * 4 * sizeof(float)); return; }
    float sum = 0;
    for (int i = -r; i <= r; i++) { k[i + r] = expf(-(float)(i * i) / (2.0f * sigma * sigma)); sum += k[i + r]; }
    for (int i = 0; i <= 2 * r; i++) k[i] /= sum;
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            float acc[4] = {0, 0, 0, 0};
            for (int i = -r; i <= r; i++) {
                int xx = horiz ? x + i : x, yy = horiz ? y : y + i;
                if (xx < 0 || yy < 0 || xx >= w || yy >= h) continue;
                const float *q = src + ((long)yy * w + xx) * 4;
                acc[0] += q[0] * k[i + r]; acc[1] += q[1] * k[i + r]; acc[2] += q[2] * k[i + r]; acc[3] += q[3] * k[i + r];
            }
            float *o = dst + ((long)y * w + x) * 4;
            o[0] = acc[0]; o[1] = acc[1]; o[2] = acc[2]; o[3] = acc[3];
        }
    free(k);
}
static SfxImg *sfx_blur(SfxArena *ar, const SfxImg *s, float sx, float sy) {
    SfxImg *a = sfx_clone(ar, s);
    if (!a) return NULL;
    if (sx > 0.01f) {
        SfxImg *b = sfx_new(ar, s->w, s->h, s->lin);
        if (!b) return a;
        sfx_blur_axis(a->p, b->p, s->w, s->h, 1, sx); a = b;
    }
    if (sy > 0.01f) {
        SfxImg *b = sfx_new(ar, s->w, s->h, s->lin);
        if (!b) return a;
        sfx_blur_axis(a->p, b->p, s->w, s->h, 0, sy); a = b;
    }
    return a;
}
static SfxImg *sfx_shift(SfxArena *ar, const SfxImg *s, int dx, int dy) {
    SfxImg *o = sfx_new(ar, s->w, s->h, s->lin);
    if (!o) return NULL;
    for (int y = 0; y < s->h; y++) {
        int sy = y - dy; if (sy < 0 || sy >= s->h) continue;
        for (int x = 0; x < s->w; x++) {
            int sx = x - dx; if (sx < 0 || sx >= s->w) continue;
            memcpy(o->p + ((long)y * s->w + x) * 4, s->p + ((long)sy * s->w + sx) * 4, 4 * sizeof(float));
        }
    }
    return o;
}
static SfxImg *sfx_alpha_only(SfxArena *ar, const SfxImg *s) {
    SfxImg *o = sfx_new(ar, s->w, s->h, s->lin);
    if (!o) return NULL;
    long n = (long)s->w * s->h;
    for (long i = 0; i < n; i++) o->p[i * 4 + 3] = s->p[i * 4 + 3];
    return o;
}
static void sfx_over(SfxImg *dst, const SfxImg *src) {          /* dst = src nad dst (premultiplikowane, ta sama przestrzeń) */
    long n = (long)dst->w * dst->h;
    for (long i = 0; i < n; i++) {
        const float *s = src->p + i * 4; float *d = dst->p + i * 4, ia = 1.0f - s[3];
        d[0] = s[0] + d[0] * ia; d[1] = s[1] + d[1] * ia; d[2] = s[2] + d[2] * ia; d[3] = s[3] + d[3] * ia;
    }
}
static void sfx_mul_alpha_img(SfxImg *a, const float *cov) {   /* a *= cov[i] */
    long n = (long)a->w * a->h;
    for (long i = 0; i < n; i++) { float c = cov[i]; a->p[i * 4] *= c; a->p[i * 4 + 1] *= c; a->p[i * 4 + 2] *= c; a->p[i * 4 + 3] *= c; }
}

/* ---------- filtry ---------- */
typedef struct { char name[48]; SfxImg *img; } SfxNamed;

static float sfx_light_sv(float x) { return x; }

static SfxImg *sfx_lighting(SfxArena *ar, const SfxImg *in, const char *prim, int specular, int lin, SfxM ctm) {
    SfxImg *o = sfx_new(ar, in->w, in->h, lin);
    if (!o) return NULL;
    float ss = sfx_kf(prim, "ss", 1.0f), kd = sfx_kf(prim, "kd", 1.0f), ks = sfx_kf(prim, "ks", 1.0f), se = sfx_kf(prim, "se", 1.0f);
    if (se < 1.0f) se = 1.0f; if (se > 128.0f) se = 128.0f;
    float lc[3]; sfx_color3(prim, "lc", lc, 1.0f);
    if (lin) for (int k = 0; k < 3; k++) lc[k] = sfx_s2l(lc[k]);
    char ls[160];
    int has = sfx_kv(prim, "ls", ls, sizeof ls);
    char kind[16] = ""; float p[9] = {0};
    if (has) {
        char *c = strchr(ls, ',');
        if (c) { *c = 0; strncpy(kind, ls, 15); const char *e = c + 1; for (int i = 0; i < 9; i++) { char *e2; p[i] = (float)strtod(e, &e2); if (e2 == e) break; e = (*e2 == ',') ? e2 + 1 : e2; } }
        else strncpy(kind, ls, 15);
    }
    float lscale = sqrtf(fabsf(ctm.a * ctm.d - ctm.b * ctm.c));
    float Lx = 0, Ly = 0, Lz = 0, Sx = 0, Sy = 0, Sz = 0, spec_exp = 1, cone = -1;
    float Ldx = 0, Ldy = 0, Ldz = 1;
    int point = !strcmp(kind, "point"), spot = !strcmp(kind, "spot"), dist = !strcmp(kind, "distant");
    if (dist) {
        float az = p[0] * 3.14159265f / 180.0f, el = p[1] * 3.14159265f / 180.0f;
        Ldx = cosf(az) * cosf(el); Ldy = sinf(az) * cosf(el); Ldz = sinf(el);
    } else if (point || spot) {
        sfx_apply(ctm, p[0], p[1], &Lx, &Ly); Lz = p[2] * lscale;
        if (spot) {
            float px, py; sfx_apply(ctm, p[3], p[4], &px, &py); float pz = p[5] * lscale;
            Sx = px - Lx; Sy = py - Ly; Sz = pz - Lz;
            float sl = sqrtf(Sx * Sx + Sy * Sy + Sz * Sz); if (sl > 0) { Sx /= sl; Sy /= sl; Sz /= sl; }
            spec_exp = p[6] > 0 ? p[6] : 1; cone = p[7] > 0 ? cosf(p[7] * 3.14159265f / 180.0f) : -1;
        }
    } else return o;
    int w = in->w, h = in->h;
#define SFX_A(xx, yy) (in->p[((long)((yy) < 0 ? 0 : (yy) >= h ? h - 1 : (yy)) * w + ((xx) < 0 ? 0 : (xx) >= w ? w - 1 : (xx))) * 4 + 3])
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            float gx = 0.25f * ((SFX_A(x + 1, y - 1) + 2 * SFX_A(x + 1, y) + SFX_A(x + 1, y + 1)) - (SFX_A(x - 1, y - 1) + 2 * SFX_A(x - 1, y) + SFX_A(x - 1, y + 1)));
            float gy = 0.25f * ((SFX_A(x - 1, y + 1) + 2 * SFX_A(x, y + 1) + SFX_A(x + 1, y + 1)) - (SFX_A(x - 1, y - 1) + 2 * SFX_A(x, y - 1) + SFX_A(x + 1, y - 1)));
            float nx = -ss * gx, ny = -ss * gy, nz = 1.0f, nl = sqrtf(nx * nx + ny * ny + 1.0f);
            nx /= nl; ny /= nl; nz /= nl;
            float lx = Ldx, ly = Ldy, lz = Ldz, col[3] = { lc[0], lc[1], lc[2] };
            if (point || spot) {
                float z = ss * SFX_A(x, y);
                lx = Lx - (x + 0.5f); ly = Ly - (y + 0.5f); lz = Lz - z;
                float ll = sqrtf(lx * lx + ly * ly + lz * lz); if (ll > 0) { lx /= ll; ly /= ll; lz /= ll; }
                if (spot) {
                    float t = -(lx * Sx + ly * Sy + lz * Sz);
                    if (t <= 0 || (cone >= 0 && t < cone)) { col[0] = col[1] = col[2] = 0; }
                    else { float f = powf(t, spec_exp); col[0] *= f; col[1] *= f; col[2] *= f; }
                }
            }
            float *q = o->p + ((long)y * w + x) * 4;
            if (!specular) {
                float nd = nx * lx + ny * ly + nz * lz; if (nd < 0) nd = 0;
                q[0] = sfx_clamp01(kd * nd * col[0]); q[1] = sfx_clamp01(kd * nd * col[1]); q[2] = sfx_clamp01(kd * nd * col[2]); q[3] = 1.0f;
            } else {
                float hx = lx, hy = ly, hz = lz + 1.0f, hl = sqrtf(hx * hx + hy * hy + hz * hz);
                if (hl > 0) { hx /= hl; hy /= hl; hz /= hl; }
                float nh = nx * hx + ny * hy + nz * hz; if (nh < 0) nh = 0;
                float f = ks * powf(nh, se);
                float r = sfx_clamp01(f * col[0]), g = sfx_clamp01(f * col[1]), b = sfx_clamp01(f * col[2]);
                float a = r > g ? (r > b ? r : b) : (g > b ? g : b);
                q[0] = r; q[1] = g; q[2] = b; q[3] = a;
            }
        }
#undef SFX_A
    (void)sfx_light_sv;
    return o;
}

static SfxImg *sfx_res_lookup(SfxNamed *res, int nres, const char *name) {
    for (int i = nres - 1; i >= 0; i--) if (!strcmp(res[i].name, name)) return res[i].img;
    return NULL;
}

/* region filtra w pikselach; bbox = obwiednia alfy źródła (przybliżenie obwiedni elementu) */
static SfxRect sfx_filter_region(const char *header, const SfxImg *src, SfxCtx *cx, SfxM ctm) {
    SfxRect full = { 0, 0, src->w, src->h };
    char u[16], xs[32], ys[32], ws[32], hs[32];
    sfx_kv(header, "u", u, sizeof u);
    sfx_kv(header, "x", xs, sizeof xs); sfx_kv(header, "y", ys, sizeof ys);
    sfx_kv(header, "w", ws, sizeof ws); sfx_kv(header, "h", hs, sizeof hs);
    int user = !strcmp(u, "user");
    SfxM tot = sfx_mul((SfxM){ cx->sc, 0, 0, cx->sc, cx->ox, cx->oy }, ctm);
    if (!user) {
        SfxRect bb = sfx_alpha_bbox(src);
        if (bb.x1 <= bb.x0 || bb.y1 <= bb.y0) { SfxRect e = {0, 0, 0, 0}; return e; }
        float bw = (float)(bb.x1 - bb.x0), bh = (float)(bb.y1 - bb.y0);
#define SFX_FR(s, def) (!(s)[0] ? (def) : (strchr((s), '%') ? (float)atof(s) / 100.0f : (float)atof(s)))
        float fx = SFX_FR(xs, -0.1f), fy = SFX_FR(ys, -0.1f), fw = SFX_FR(ws, 1.2f), fh = SFX_FR(hs, 1.2f);
        SfxRect r = { (int)floorf(bb.x0 + fx * bw), (int)floorf(bb.y0 + fy * bh), (int)ceilf(bb.x0 + (fx + fw) * bw), (int)ceilf(bb.y0 + (fy + fh) * bh) };
        if (r.x0 < 0) r.x0 = 0; if (r.y0 < 0) r.y0 = 0; if (r.x1 > src->w) r.x1 = src->w; if (r.y1 > src->h) r.y1 = src->h;
        return r;
    }
    if (!xs[0] || !ys[0] || !ws[0] || !hs[0] || strchr(xs, '%') || strchr(ws, '%')) return full;
    float x = (float)atof(xs), y = (float)atof(ys), w = (float)atof(ws), h = (float)atof(hs);
    float px[4], py[4];
    sfx_apply(tot, x, y, &px[0], &py[0]); sfx_apply(tot, x + w, y, &px[1], &py[1]);
    sfx_apply(tot, x, y + h, &px[2], &py[2]); sfx_apply(tot, x + w, y + h, &px[3], &py[3]);
    float x0 = px[0], x1 = px[0], y0 = py[0], y1 = py[0];
    for (int i = 1; i < 4; i++) { if (px[i] < x0) x0 = px[i]; if (px[i] > x1) x1 = px[i]; if (py[i] < y0) y0 = py[i]; if (py[i] > y1) y1 = py[i]; }
    SfxRect r = { (int)floorf(x0), (int)floorf(y0), (int)ceilf(x1), (int)ceilf(y1) };
    if (r.x0 < 0) r.x0 = 0; if (r.y0 < 0) r.y0 = 0; if (r.x1 > src->w) r.x1 = src->w; if (r.y1 > src->h) r.y1 = src->h;
    return r;
}

static SfxImg *sfx_run_filter(SfxArena *ar, const char *spec, SfxImg *src, SfxCtx *cx, SfxM ctm) {
    /* spec: header FS prim FS prim ... */
    char *copy = strdup(spec);
    if (!copy) return NULL;
    char *header = copy, *rest = strchr(copy, '\x1c');
    if (rest) *rest++ = 0;
    SfxM tot = sfx_mul((SfxM){ cx->sc, 0, 0, cx->sc, cx->ox, cx->oy }, ctm);
    float scx = hypotf(tot.a, tot.b), scy = hypotf(tot.c, tot.d);
    SfxNamed res[96]; int nres = 0;
    SfxImg *prev = src, *src_alpha = NULL;
    while (rest && *rest) {
        char *next = strchr(rest, '\x1c');
        if (next) *next++ = 0;
        const char *p = rest;
        char t[40], b1[64], b2[64], rn[48];
        sfx_kv(p, "t", t, sizeof t);
        char cif[16]; sfx_kv(p, "cif", cif, sizeof cif);
        int lin = strcmp(cif, "srgb") != 0;
        sfx_kv(p, "in", b1, sizeof b1); sfx_kv(p, "in2", b2, sizeof b2); sfx_kv(p, "result", rn, sizeof rn);
        SfxImg *in1 = prev, *in2 = prev;
        const char *names[2] = { b1, b2 }; SfxImg **dst[2] = { &in1, &in2 };
        for (int q = 0; q < 2; q++) {
            if (!names[q][0]) *dst[q] = prev;
            else if (!strcmp(names[q], "SourceGraphic")) *dst[q] = src;
            else if (!strcmp(names[q], "SourceAlpha")) { if (!src_alpha) src_alpha = sfx_alpha_only(ar, src); *dst[q] = src_alpha ? src_alpha : src; }
            else { SfxImg *f = sfx_res_lookup(res, nres, names[q]); *dst[q] = f ? f : prev; }
        }
        SfxImg *out = NULL;
        int w = src->w, h = src->h;
        if (!strcmp(t, "feflood")) {
            out = sfx_new(ar, w, h, lin);
            float c[3]; sfx_color3(p, "fc", c, 0.0f);
            float op = sfx_kf(p, "fo", 1.0f); op = sfx_clamp01(op);
            if (out) {
                if (lin) for (int k = 0; k < 3; k++) c[k] = sfx_s2l(c[k]);
                long n = (long)w * h;
                for (long i = 0; i < n; i++) { out->p[i * 4] = c[0] * op; out->p[i * 4 + 1] = c[1] * op; out->p[i * 4 + 2] = c[2] * op; out->p[i * 4 + 3] = op; }
            }
        } else if (!strcmp(t, "feoffset")) {
            SfxImg *a = sfx_in_space(ar, in1, lin);
            float dx = sfx_kf(p, "dx", 0), dy = sfx_kf(p, "dy", 0);
            int pdx = (int)lroundf(dx * tot.a + dy * tot.c), pdy = (int)lroundf(dx * tot.b + dy * tot.d);
            if (a) out = sfx_shift(ar, a, pdx, pdy);
        } else if (!strcmp(t, "fegaussianblur")) {
            SfxImg *a = sfx_in_space(ar, in1, lin);
            char sd[40]; sfx_kv(p, "sd", sd, sizeof sd);
            float sx = 0, sy = 0; int n = sscanf(sd, "%f %f", &sx, &sy); if (n == 1) sy = sx;
            if (a) { if (n >= 1 && (sx > 0 || sy > 0)) out = sfx_blur(ar, a, sx * scx, sy * scy); else out = sfx_clone(ar, a); }
        } else if (!strcmp(t, "fedropshadow")) {
            SfxImg *a = sfx_in_space(ar, in1, lin);
            char sd[40]; sfx_kv(p, "sd", sd, sizeof sd);
            float sx = 2, sy = 2; if (sd[0]) { int n = sscanf(sd, "%f %f", &sx, &sy); if (n == 1) sy = sx; }
            float dx = sfx_kf(p, "dx", 2), dy = sfx_kf(p, "dy", 2);
            float c[3]; sfx_color3(p, "fc", c, 0.0f); float op = sfx_clamp01(sfx_kf(p, "fo", 1.0f));
            if (lin) for (int k = 0; k < 3; k++) c[k] = sfx_s2l(c[k]);
            if (a) {
                SfxImg *al = sfx_alpha_only(ar, a);
                SfxImg *bl = al ? sfx_blur(ar, al, sx * scx, sy * scy) : NULL;
                int pdx = (int)lroundf(dx * tot.a + dy * tot.c), pdy = (int)lroundf(dx * tot.b + dy * tot.d);
                SfxImg *sh = bl ? sfx_shift(ar, bl, pdx, pdy) : NULL;
                if (sh) {
                    long n = (long)w * h;
                    for (long i = 0; i < n; i++) { float al2 = sh->p[i * 4 + 3] * op; sh->p[i * 4] = c[0] * al2; sh->p[i * 4 + 1] = c[1] * al2; sh->p[i * 4 + 2] = c[2] * al2; sh->p[i * 4 + 3] = al2; }
                    sfx_over(sh, a);
                    out = sh;
                }
            }
        } else if (!strcmp(t, "femerge")) {
            out = sfx_new(ar, w, h, lin);
            char nodes[512]; sfx_kv(p, "nodes", nodes, sizeof nodes);
            if (out) {
                char *s = nodes;
                for (;;) {
                    char *e = strchr(s, ','); if (e) *e = 0;
                    SfxImg *im = prev;
                    if (s[0]) {
                        if (!strcmp(s, "SourceGraphic")) im = src;
                        else if (!strcmp(s, "SourceAlpha")) { if (!src_alpha) src_alpha = sfx_alpha_only(ar, src); im = src_alpha ? src_alpha : src; }
                        else { SfxImg *f = sfx_res_lookup(res, nres, s); if (f) im = f; }
                    }
                    SfxImg *c2 = sfx_in_space(ar, im, lin);
                    if (c2) sfx_over(out, c2);
                    if (!e) break;
                    s = e + 1;
                }
            }
        } else if (!strcmp(t, "feblend")) {
            SfxImg *a = sfx_in_space(ar, in1, lin), *b = sfx_in_space(ar, in2, lin);
            char mode[24]; sfx_kv(p, "mode", mode, sizeof mode);
            out = sfx_new(ar, w, h, lin);
            if (out && a && b) {
                long n = (long)w * h;
                for (long i = 0; i < n; i++) {
                    const float *A = a->p + i * 4, *B = b->p + i * 4; float *R = out->p + i * 4;
                    for (int k = 0; k < 3; k++) {
                        float ca = A[k], cb = B[k], aa = A[3], ab = B[3];
                        if (!strcmp(mode, "multiply")) R[k] = (1 - aa) * cb + (1 - ab) * ca + ca * cb;
                        else if (!strcmp(mode, "screen")) R[k] = cb + ca - ca * cb;
                        else if (!strcmp(mode, "darken")) { float x1 = (1 - aa) * cb + ca, x2 = (1 - ab) * ca + cb; R[k] = x1 < x2 ? x1 : x2; }
                        else if (!strcmp(mode, "lighten")) { float x1 = (1 - aa) * cb + ca, x2 = (1 - ab) * ca + cb; R[k] = x1 > x2 ? x1 : x2; }
                        else R[k] = (1 - aa) * cb + ca;
                    }
                    R[3] = 1 - (1 - A[3]) * (1 - B[3]);
                }
            }
        } else if (!strcmp(t, "fecomposite")) {
            SfxImg *a = sfx_in_space(ar, in1, lin), *b = sfx_in_space(ar, in2, lin);
            char op[16]; sfx_kv(p, "op", op, sizeof op);
            float k1 = sfx_kf(p, "k1", 0), k2 = sfx_kf(p, "k2", 0), k3 = sfx_kf(p, "k3", 0), k4 = sfx_kf(p, "k4", 0);
            out = sfx_new(ar, w, h, lin);
            if (out && a && b) {
                long n = (long)w * h;
                for (long i = 0; i < n; i++) {
                    const float *A = a->p + i * 4, *B = b->p + i * 4; float *R = out->p + i * 4;
                    for (int k = 0; k < 4; k++) {
                        float fa = A[k], fb = B[k], aa = A[3], ab = B[3];
                        if (!strcmp(op, "in")) R[k] = fa * ab;
                        else if (!strcmp(op, "out")) R[k] = fa * (1 - ab);
                        else if (!strcmp(op, "atop")) R[k] = fa * ab + fb * (1 - aa);
                        else if (!strcmp(op, "xor")) R[k] = fa * (1 - ab) + fb * (1 - aa);
                        else if (!strcmp(op, "arithmetic")) R[k] = sfx_clamp01(k1 * fa * fb + k2 * fa + k3 * fb + k4);
                        else R[k] = fa + fb * (1 - aa);
                    }
                    if (!strcmp(op, "arithmetic")) for (int k = 0; k < 3; k++) if (R[k] > R[3]) R[k] = R[3];
                }
            }
        } else if (!strcmp(t, "fecolormatrix")) {
            SfxImg *a = sfx_in_space(ar, in1, lin);
            char ty[24], vs[400]; sfx_kv(p, "type", ty, sizeof ty); sfx_kv(p, "v", vs, sizeof vs);
            float m[20] = {1,0,0,0,0, 0,1,0,0,0, 0,0,1,0,0, 0,0,0,1,0};
            float v[20]; int nv = 0; { const char *s = vs; while (*s && nv < 20) { char *e; double d = strtod(s, &e); if (e == s) { s++; continue; } v[nv++] = (float)d; s = e; } }
            int lum = !strcmp(ty, "luminancetoalpha");
            if (!strcmp(ty, "saturate")) {
                float s = nv > 0 ? v[0] : 1;
                float t2[20] = { 0.213f + 0.787f * s, 0.715f - 0.715f * s, 0.072f - 0.072f * s, 0, 0,
                                 0.213f - 0.213f * s, 0.715f + 0.285f * s, 0.072f - 0.072f * s, 0, 0,
                                 0.213f - 0.213f * s, 0.715f - 0.715f * s, 0.072f + 0.928f * s, 0, 0, 0, 0, 0, 1, 0 };
                memcpy(m, t2, sizeof m);
            } else if (!strcmp(ty, "huerotate")) {
                float r = (nv > 0 ? v[0] : 0) * 3.14159265f / 180.0f, c = cosf(r), s = sinf(r);
                float t2[20] = { 0.213f + c * 0.787f - s * 0.213f, 0.715f - c * 0.715f - s * 0.715f, 0.072f - c * 0.072f + s * 0.928f, 0, 0,
                                 0.213f - c * 0.213f + s * 0.143f, 0.715f + c * 0.285f + s * 0.140f, 0.072f - c * 0.072f - s * 0.283f, 0, 0,
                                 0.213f - c * 0.213f - s * 0.787f, 0.715f - c * 0.715f + s * 0.715f, 0.072f + c * 0.928f + s * 0.072f, 0, 0, 0, 0, 0, 1, 0 };
                memcpy(m, t2, sizeof m);
            } else if (lum) {
                float t2[20] = {0,0,0,0,0, 0,0,0,0,0, 0,0,0,0,0, 0.2125f, 0.7154f, 0.0721f, 0, 0};
                memcpy(m, t2, sizeof m);
            } else if (nv == 20) memcpy(m, v, sizeof m);
            out = sfx_new(ar, w, h, lin);
            if (out && a) {
                long n = (long)w * h;
                for (long i = 0; i < n; i++) {
                    const float *A = a->p + i * 4; float al = A[3];
                    float r = al > 0 ? A[0] / al : 0, g = al > 0 ? A[1] / al : 0, b = al > 0 ? A[2] / al : 0;
                    float nr = sfx_clamp01(m[0]*r + m[1]*g + m[2]*b + m[3]*al + m[4]);
                    float ng = sfx_clamp01(m[5]*r + m[6]*g + m[7]*b + m[8]*al + m[9]);
                    float nb = sfx_clamp01(m[10]*r + m[11]*g + m[12]*b + m[13]*al + m[14]);
                    float na = sfx_clamp01(m[15]*r + m[16]*g + m[17]*b + m[18]*al + m[19]);
                    float *R = out->p + i * 4; R[0] = nr * na; R[1] = ng * na; R[2] = nb * na; R[3] = na;
                }
            }
        } else if (!strcmp(t, "fediffuselighting") || !strcmp(t, "fespecularlighting")) {
            SfxImg *a = sfx_in_space(ar, in1, lin);
            if (a) out = sfx_lighting(ar, a, p, !strcmp(t, "fespecularlighting"), lin, tot);
        }
        if (!out) { SfxImg *a = sfx_in_space(ar, in1, lin); out = a ? sfx_clone(ar, a) : NULL; }
        if (!out) { free(copy); return NULL; }
        out->lin = lin;
        if (rn[0] && nres < 96) { strncpy(res[nres].name, rn, 47); res[nres].name[47] = 0; res[nres].img = out; nres++; }
        prev = out;
        rest = next;
    }
    SfxImg *fin = (prev == src) ? sfx_clone(ar, src) : prev;
    if (!fin) { free(copy); return NULL; }
    sfx_space(fin, 0);
    SfxRect rg = sfx_filter_region(header, src, cx, ctm);
    for (int y = 0; y < fin->h; y++)
        for (int x = 0; x < fin->w; x++)
            if (x < rg.x0 || x >= rg.x1 || y < rg.y0 || y >= rg.y1) { float *q = fin->p + ((long)y * fin->w + x) * 4; q[0] = q[1] = q[2] = q[3] = 0; }
    free(copy);
    return fin;
}

/* ---------- cały znacznik → RGBA8 (niepremultiplikowane) ---------- */
static unsigned char *sfx_render(const char *markup, int pw, int ph) {
    if ((long)pw * ph > 6000000L) return NULL;
    SfxCtx cx = { pw, ph, 1, 0, 0, 0 };
    SfxArena canvas_ar = { {0}, 0 };
    SfxImg *canvas = sfx_new(&canvas_ar, pw, ph, 0);
    if (!canvas) return NULL;
    char *all = strdup(markup);
    if (!all) { sfx_arena_free(&canvas_ar); return NULL; }
    char *op = all;
    while (op && *op) {
        char *next = strchr(op, '\x1e');
        if (next) *next++ = 0;
        /* pola oddzielone US */
        char *fld[16]; int nf = 0; char *s = op;
        while (nf < 16) { fld[nf++] = s; char *u = strchr(s, '\x1f'); if (!u) break; *u = 0; s = u + 1; }
        SfxArena ar = { {0}, 0 };
        SfxImg *layer = NULL;
        if (nf >= 2 && !strcmp(fld[0], "draw")) {
            layer = sfx_raster(&ar, fld[1], &cx);
        } else if (nf >= 4 && !strcmp(fld[0], "fx")) {
            const char *steps = fld[1];
            SfxM ctm = sfx_parse_transform(fld[2]);
            layer = sfx_raster(&ar, fld[3], &cx);
            int aux = 4;
            char *sc = strdup(steps);
            char *st = sc;
            while (layer && st && *st) {
                char *nx = strchr(st, '\x1d'); if (nx) *nx++ = 0;
                if (st[0] == 'F') {
                    SfxImg *f = sfx_run_filter(&ar, st + 1, layer, &cx, ctm);
                    if (f) layer = f;
                } else if (st[0] == 'C' && aux < nf) {
                    SfxImg *m = sfx_raster(&ar, fld[aux++], &cx);
                    if (m) {
                        float *cov = (float *)malloc(sizeof(float) * (size_t)pw * ph);
                        if (cov) { for (long i = 0; i < (long)pw * ph; i++) cov[i] = m->p[i * 4 + 3]; sfx_mul_alpha_img(layer, cov); free(cov); }
                    }
                } else if (st[0] == 'M' && aux < nf) {
                    int alpha_t = strncmp(st + 1, "alpha", 5) == 0, linear = strstr(st + 1, "linear") != NULL;
                    SfxImg *m = sfx_raster(&ar, fld[aux++], &cx);
                    if (m) {
                        float *cov = (float *)malloc(sizeof(float) * (size_t)pw * ph);
                        if (cov) {
                            if (linear && !alpha_t) sfx_space(m, 1);
                            for (long i = 0; i < (long)pw * ph; i++) {
                                const float *q = m->p + i * 4; float a = q[3];
                                if (alpha_t) cov[i] = a;
                                else cov[i] = 0.2125f * q[0] + 0.7154f * q[1] + 0.0721f * q[2];   /* premultiplikowane: luminancja * alfa */
                            }
                            sfx_mul_alpha_img(layer, cov); free(cov);
                        }
                    }
                } else if (st[0] == 'O') {
                    float o = sfx_clamp01((float)atof(st + 1));
                    for (long i = 0; i < (long)pw * ph * 4; i++) layer->p[i] *= o;
                }
                st = nx;
            }
            free(sc);
        }
        if (layer) { SfxImg *l = layer; if (l->lin) sfx_space(l, 0); sfx_over(canvas, l); }
        sfx_arena_free(&ar);
        op = next;
    }
    free(all);
    unsigned char *out = (unsigned char *)malloc((size_t)pw * ph * 4);
    if (out) {
        for (long i = 0; i < (long)pw * ph; i++) {
            const float *q = canvas->p + i * 4; float a = q[3];
            float r = a > 0 ? q[0] / a : 0, g = a > 0 ? q[1] / a : 0, b = a > 0 ? q[2] / a : 0;
            out[i * 4] = (unsigned char)(sfx_clamp01(r) * 255.0f + 0.5f); out[i * 4 + 1] = (unsigned char)(sfx_clamp01(g) * 255.0f + 0.5f);
            out[i * 4 + 2] = (unsigned char)(sfx_clamp01(b) * 255.0f + 0.5f); out[i * 4 + 3] = (unsigned char)(sfx_clamp01(a) * 255.0f + 0.5f);
        }
    }
    sfx_arena_free(&canvas_ar);
    return out;
}
#endif
