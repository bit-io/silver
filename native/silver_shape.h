#ifndef SILVER_SHAPE_H
#define SILVER_SHAPE_H
#ifdef SILVER_SHAPING
#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_COLOR_H
#include FT_TRUETYPE_TABLES_H
#include FT_MULTIPLE_MASTERS_H
#include <hb.h>
#include <hb-ot.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#define SHP_FACES 8
typedef struct { char path[512]; FT_Face ft; hb_blob_t *blob; hb_face_t *hbf; unsigned used; int has_opsz; float opsz_min, opsz_max; } ShpFace;
static FT_Library g_shp_ft;
static ShpFace g_shp_faces[SHP_FACES];
static unsigned g_shp_clock;

typedef struct { unsigned char *argb; int w, h; } ShpBitmap;      /* ARGB8888, alfa niepremultiplikowana, malloc */

/* prefiks \x02 … \x03; zwraca wskaźnik na właściwy tekst albo NULL, gdy brak prefiksu */
static const char *shp_split(const char *text, char *payload, size_t n) {
    if (!text || text[0] != '\x02') return NULL;
    const char *e = strchr(text + 1, '\x03');
    if (!e) return NULL;
    size_t l = (size_t)(e - (text + 1)); if (l >= n) l = n - 1;
    memcpy(payload, text + 1, l); payload[l] = 0;
    return e + 1;
}

static ShpFace *shp_face(const char *path) {
    if (!g_shp_ft && FT_Init_FreeType(&g_shp_ft)) return NULL;
    int oldest = 0;
    for (int i = 0; i < SHP_FACES; i++) {
        if (g_shp_faces[i].ft && !strcmp(g_shp_faces[i].path, path)) { g_shp_faces[i].used = ++g_shp_clock; return &g_shp_faces[i]; }
        if (!g_shp_faces[i].ft) { oldest = i; break; }
        if (g_shp_faces[i].used < g_shp_faces[oldest].used) oldest = i;
    }
    ShpFace *f = &g_shp_faces[oldest];
    if (f->ft) { FT_Done_Face(f->ft); hb_face_destroy(f->hbf); hb_blob_destroy(f->blob); memset(f, 0, sizeof *f); }
    if (FT_New_Face(g_shp_ft, path, 0, &f->ft)) { memset(f, 0, sizeof *f); return NULL; }
    f->blob = hb_blob_create_from_file(path);
    f->hbf = hb_face_create(f->blob, 0);
    strncpy(f->path, path, sizeof f->path - 1);
    f->used = ++g_shp_clock;
    hb_ot_var_axis_info_t ax;
    unsigned n = 1;
    f->has_opsz = hb_ot_var_has_data(f->hbf) && hb_ot_var_find_axis_info(f->hbf, HB_TAG('o', 'p', 's', 'z'), &ax);
    if (f->has_opsz) { f->opsz_min = ax.min_value; f->opsz_max = ax.max_value; }
    (void)n;
    return f;
}

/* czy plik jest czcionką zmienną z osią opsz (wtedy shim kształtuje tekst automatycznie — font-optical-sizing: auto) */
static int shp_font_has_opsz(const char *path) {
    ShpFace *f = path ? shp_face(path) : NULL;
    return f ? f->has_opsz : 0;
}

/* "ss01" 1, swsh, "liga" off  →  hb_feature_t[] */
static int shp_features(const char *s, hb_feature_t *out, int max) {
    int n = 0;
    while (s && *s && n < max) {
        while (*s == ' ' || *s == ',' || *s == '\t') s++;
        if (!*s) break;
        char tag[5] = "    "; int tl = 0;
        if (*s == '"' || *s == '\'') { char q = *s++; while (*s && *s != q && tl < 4) tag[tl++] = *s++; while (*s && *s != q) s++; if (*s) s++; }
        else while (*s && *s != ' ' && *s != ',' && tl < 4) tag[tl++] = *s++;
        while (*s == ' ' || *s == '\t') s++;
        unsigned val = 1;
        if (!strncmp(s, "off", 3)) { val = 0; s += 3; } else if (!strncmp(s, "on", 2)) { val = 1; s += 2; }
        else if (isdigit((unsigned char)*s)) { val = (unsigned)strtoul(s, (char **)&s, 10); }
        while (*s && *s != ',') s++;
        if (tl == 4) { out[n].tag = HB_TAG(tag[0], tag[1], tag[2], tag[3]); out[n].value = val; out[n].start = 0; out[n].end = (unsigned)-1; n++; }
    }
    return n;
}

static hb_language_t shp_language(const char *l) {
    if (!l || !l[0]) return HB_LANGUAGE_INVALID;
    int bcp = strchr(l, '-') != NULL || (strlen(l) == 2 && islower((unsigned char)l[0]));
    if (!bcp && strlen(l) <= 4) {
        char t[5] = "    ";
        for (int i = 0; l[i] && i < 4; i++) t[i] = (char)toupper((unsigned char)l[i]);
        hb_language_t lg = hb_ot_tag_to_language(HB_TAG(t[0], t[1], t[2], t[3]));
        if (lg != HB_LANGUAGE_INVALID) return lg;
    }
    return hb_language_from_string(l, -1);
}

typedef struct { char feat[512], lang[32], pal[16], ovr[512], opt[16]; } ShpSpec;
static void shp_parse_payload(const char *p, ShpSpec *s) {
    memset(s, 0, sizeof *s);
    char *fields[5] = { s->feat, s->lang, s->pal, s->ovr, s->opt };
    size_t caps[5] = { sizeof s->feat, sizeof s->lang, sizeof s->pal, sizeof s->ovr, sizeof s->opt };
    for (int i = 0; i < 5; i++) {
        const char *e = strchr(p, '|');
        size_t l = e ? (size_t)(e - p) : strlen(p); if (l >= caps[i]) l = caps[i] - 1;
        memcpy(fields[i], p, l); fields[i][l] = 0;
        if (!e) break;
        p = e + 1;
    }
}

typedef struct { hb_buffer_t *buf; hb_font_t *font; unsigned n; hb_glyph_info_t *gi; hb_glyph_position_t *gp; } ShpRun;
static int shp_shape(ShpFace *f, int px, const ShpSpec *sp, const char *text, ShpRun *r) {
    r->font = hb_font_create(f->hbf);
    hb_font_set_scale(r->font, px * 64, px * 64);
    hb_ot_font_set_funcs(r->font);
    if (f->has_opsz && strcmp(sp->opt, "none") != 0) {            /* font-optical-sizing: auto — opsz = rozmiar w px (CSS px = pt) */
        hb_variation_t v = { HB_TAG('o', 'p', 's', 'z'), (float)px };
        hb_font_set_variations(r->font, &v, 1);
    }
    r->buf = hb_buffer_create();
    hb_buffer_add_utf8(r->buf, text, -1, 0, -1);
    if (sp->lang[0]) hb_buffer_set_language(r->buf, shp_language(sp->lang));
    hb_buffer_guess_segment_properties(r->buf);
    hb_feature_t feats[48];
    int nf = shp_features(sp->feat, feats, 48);
    hb_shape(r->font, r->buf, nf ? feats : NULL, (unsigned)nf);
    r->gi = hb_buffer_get_glyph_infos(r->buf, &r->n);
    r->gp = hb_buffer_get_glyph_positions(r->buf, &r->n);
    return 1;
}
static void shp_run_free(ShpRun *r) { hb_buffer_destroy(r->buf); hb_font_destroy(r->font); }

/* szerokość w pikselach fizycznych */
static int shp_measure(const char *font_path, int px, const char *payload, const char *text) {
    ShpFace *f = shp_face(font_path);
    if (!f) return -1;
    ShpSpec sp; shp_parse_payload(payload, &sp);
    ShpRun r;
    shp_shape(f, px, &sp, text, &r);
    long adv = 0;
    for (unsigned i = 0; i < r.n; i++) adv += r.gp[i].x_advance;
    shp_run_free(&r);
    return (int)((adv + 63) / 64);
}

/* Typy palet (bit 0 = jasne tło, bit 1 = ciemne tło) czytane wprost z tablicy CPAL v1 — FreeType 2.13 odczytuje
 * tablicę typów jako uint16 zamiast uint32 i zwraca błędne flagi. Zwraca -1 przy braku danych. */
static long shp_palette_type(FT_Face ft, int idx) {
    FT_ULong len = 0;
    if (FT_Load_Sfnt_Table(ft, FT_MAKE_TAG('C', 'P', 'A', 'L'), 0, NULL, &len) || len < 16) return -1;
    unsigned char *b = (unsigned char *)malloc(len);
    if (!b) return -1;
    long res = -1;
    if (!FT_Load_Sfnt_Table(ft, FT_MAKE_TAG('C', 'P', 'A', 'L'), 0, b, &len)) {
        unsigned ver = (unsigned)(b[0] << 8 | b[1]), np = (unsigned)(b[4] << 8 | b[5]);
        size_t off = 12 + 2 * (size_t)np;
        if (ver >= 1 && off + 12 <= len && (unsigned)idx < np) {
            unsigned long t = (unsigned long)b[off] << 24 | (unsigned long)b[off + 1] << 16 | (unsigned long)b[off + 2] << 8 | b[off + 3];
            if (t && t + 4 * (size_t)np <= len) {
                const unsigned char *q = b + t + 4 * (size_t)idx;
                res = (long)((unsigned long)q[0] << 24 | (unsigned long)q[1] << 16 | (unsigned long)q[2] << 8 | q[3]);
            }
        }
    }
    free(b);
    return res;
}

static int shp_select_palette(FT_Face ft, const ShpSpec *sp, int fr, int fg, int fb, FT_Color *local, int max_local) {
    FT_Palette_Data pd;
    if (FT_Palette_Data_Get(ft, &pd) || pd.num_palettes == 0) return 0;
    int idx = 0;
    if (!strcmp(sp->pal, "light") || !strcmp(sp->pal, "dark")) {
        unsigned want = !strcmp(sp->pal, "light") ? 1u : 2u;
        for (int i = 0; i < pd.num_palettes; i++) { long t = shp_palette_type(ft, i); if (t >= 0 && ((unsigned)t & want)) { idx = i; break; } }
    } else if (sp->pal[0] && isdigit((unsigned char)sp->pal[0])) {
        idx = atoi(sp->pal); if (idx >= pd.num_palettes) idx = 0;
    }
    FT_Color *pal = NULL;
    if (FT_Palette_Select(ft, (FT_UShort)idx, &pal) || !pal) return 0;
    int n = pd.num_palette_entries < max_local ? pd.num_palette_entries : max_local;
    for (int i = 0; i < n; i++) local[i] = pal[i];
    const char *s = sp->ovr;
    while (*s) {
        char *e; long i = strtol(s, &e, 10);
        if (e == s || *e != ':') break;
        long r = strtol(e + 1, &e, 10), g = *e == ',' ? strtol(e + 1, &e, 10) : 0, b = *e == ',' ? strtol(e + 1, &e, 10) : 0;
        long a = *e == ',' ? strtol(e + 1, &e, 10) : 255;
        FT_Color c = { (FT_Byte)b, (FT_Byte)g, (FT_Byte)r, (FT_Byte)a };
        if (i >= 0 && i < n) { local[i] = c; pal[i] = c; }
        s = (*e == ';') ? e + 1 : e;
    }
    FT_Color fc = { (FT_Byte)fb, (FT_Byte)fg, (FT_Byte)fr, 255 };
    FT_Palette_Set_Foreground_Color(ft, fc);
    return n;
}

/* kładzie bitmapę glifu (GRAY: kolor jednolity; BGRA: własne kolory) na akumulator premultiplikowany */
static void shp_blit(float *acc, int w, int h, const FT_Bitmap *bm, int gx, int gy, int cr, int cg, int cb, int ca) {
    for (unsigned row = 0; row < bm->rows; row++)
        for (unsigned col = 0; col < bm->width; col++) {
            int x = gx + (int)col, y = gy + (int)row;
            if (x < 0 || y < 0 || x >= w || y >= h) continue;
            float sr, sg, sb, sa;
            if (bm->pixel_mode == FT_PIXEL_MODE_BGRA) {
                const unsigned char *q = bm->buffer + row * bm->pitch + col * 4;          /* BGRA premultiplikowane */
                sb = q[0] / 255.0f; sg = q[1] / 255.0f; sr = q[2] / 255.0f; sa = q[3] / 255.0f;
            } else if (bm->pixel_mode == FT_PIXEL_MODE_GRAY) {
                sa = bm->buffer[row * bm->pitch + col] / 255.0f * (ca / 255.0f);
                sr = cr / 255.0f * sa; sg = cg / 255.0f * sa; sb = cb / 255.0f * sa;
            } else continue;
            float *d = acc + ((size_t)y * w + x) * 4, ia = 1.0f - sa;
            d[0] = sr + d[0] * ia; d[1] = sg + d[1] * ia; d[2] = sb + d[2] * ia; d[3] = sa + d[3] * ia;
        }
}

/* rysuje tekst do bitmapy; `ascent`/`height` z SDL_ttf (to samo pudełko wiersza co zwykły tekst);
 * deco: 4 = podkreślenie, 8 = przekreślenie. Zwraca 1 przy sukcesie. */
static int shp_render(const char *font_path, int px, const char *payload, const char *text, int cr, int cg, int cb,
                      int ascent, int height, int deco, ShpBitmap *out) {
    ShpFace *f = shp_face(font_path);
    if (!f) return 0;
    ShpSpec sp; shp_parse_payload(payload, &sp);
    ShpRun r;
    shp_shape(f, px, &sp, text, &r);
    long adv = 0;
    for (unsigned i = 0; i < r.n; i++) adv += r.gp[i].x_advance;
    int w = (int)((adv + 63) / 64); if (w < 1) w = 1;
    int h = height > 0 ? height : px + px / 4;
    float *acc = (float *)calloc((size_t)w * h * 4, sizeof(float));      /* premultiplikowane r,g,b,a 0..1 */
    if (!acc) { shp_run_free(&r); return 0; }
    FT_Set_Pixel_Sizes(f->ft, 0, (FT_UInt)px);
    if (f->has_opsz) {                                              /* ustaw osie (opsz wg rozmiaru albo domyślne) — twarz jest współdzielona */
        FT_MM_Var *mm = NULL;
        if (!FT_Get_MM_Var(f->ft, &mm) && mm) {
            FT_Fixed coords[16]; unsigned na = mm->num_axis < 16 ? mm->num_axis : 16;
            for (unsigned a = 0; a < na; a++) {
                coords[a] = mm->axis[a].def;
                if (mm->axis[a].tag == FT_MAKE_TAG('o', 'p', 's', 'z') && strcmp(sp.opt, "none") != 0) {
                    float v = (float)px; if (v < f->opsz_min) v = f->opsz_min; if (v > f->opsz_max) v = f->opsz_max;
                    coords[a] = (FT_Fixed)(v * 65536.0f);
                }
            }
            FT_Set_Var_Design_Coordinates(f->ft, na, coords);
            FT_Done_MM_Var(g_shp_ft, mm);
        }
    }
    FT_Color local[256];
    int npal = shp_select_palette(f->ft, &sp, cr, cg, cb, local, 256);
    long pen = 0;
    for (unsigned i = 0; i < r.n; i++) {
        int ox = (int)((pen + r.gp[i].x_offset) / 64), oy = (int)(r.gp[i].y_offset / 64);
        FT_LayerIterator it; it.num_layers = 0; it.layer = 0; it.p = NULL;
        FT_UInt lg, ci; int had_layers = 0;
        while (FT_Get_Color_Glyph_Layer(f->ft, r.gi[i].codepoint, &lg, &ci, &it)) {          /* COLR v0: warstwy sam */
            had_layers = 1;
            if (FT_Load_Glyph(f->ft, lg, FT_LOAD_NO_HINTING | FT_LOAD_RENDER)) continue;
            FT_GlyphSlot g = f->ft->glyph;
            int lr = cr, lgc = cg, lb = cb, la = 255;
            if (ci != 0xFFFF && (int)ci < npal) { lr = local[ci].red; lgc = local[ci].green; lb = local[ci].blue; la = local[ci].alpha; }
            shp_blit(acc, w, h, &g->bitmap, ox + g->bitmap_left, ascent - g->bitmap_top - oy, lr, lgc, lb, la);
        }
        if (!had_layers && !FT_Load_Glyph(f->ft, r.gi[i].codepoint, FT_LOAD_NO_HINTING | FT_LOAD_COLOR | FT_LOAD_RENDER)) {
            FT_GlyphSlot g = f->ft->glyph;
            shp_blit(acc, w, h, &g->bitmap, ox + g->bitmap_left, ascent - g->bitmap_top - oy, cr, cg, cb, 255);
        }
        pen += r.gp[i].x_advance;
    }
    shp_run_free(&r);
    int th = px / 14; if (th < 1) th = 1;
    for (int pass = 0; pass < 2; pass++) {
        if (!(deco & (pass ? 8 : 4))) continue;
        int y0 = pass ? ascent - px * 3 / 10 : ascent + th;
        for (int y = y0; y < y0 + th && y < h; y++) if (y >= 0)
            for (int x = 0; x < w; x++) { float *d = acc + ((size_t)y * w + x) * 4; d[0] = cr / 255.0f; d[1] = cg / 255.0f; d[2] = cb / 255.0f; d[3] = 1.0f; }
    }
    out->argb = (unsigned char *)malloc((size_t)w * h * 4);
    out->w = w; out->h = h;
    if (!out->argb) { free(acc); return 0; }
    for (long i = 0; i < (long)w * h; i++) {
        const float *d = acc + i * 4; float a = d[3];
        float rr = a > 0 ? d[0] / a : 0, gg = a > 0 ? d[1] / a : 0, bb = a > 0 ? d[2] / a : 0;
        unsigned char *o = out->argb + i * 4;      /* bajty w kolejności pamięci little-endian dla ARGB8888: B,G,R,A */
        o[0] = (unsigned char)((bb > 1 ? 1 : bb) * 255.0f + 0.5f); o[1] = (unsigned char)((gg > 1 ? 1 : gg) * 255.0f + 0.5f);
        o[2] = (unsigned char)((rr > 1 ? 1 : rr) * 255.0f + 0.5f); o[3] = (unsigned char)((a > 1 ? 1 : a) * 255.0f + 0.5f);
    }
    free(acc);
    return 1;
}
#endif /* SILVER_SHAPING */
#endif
