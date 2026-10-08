#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <SDL2/SDL_image.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <dirent.h>
#include <sys/stat.h>
#include <string.h>
#include <math.h>
#include <stdarg.h>

#define SILVER_MAX_WINDOWS   8
#define SILVER_EV_QUEUE      512
#define SILVER_EV_PAYLOAD    1024
#define SILVER_MAX_FONTS     16
#define SILVER_TEXT_CACHE    256
#define SILVER_IMAGE_CACHE   128

typedef struct { int code; char payload[SILVER_EV_PAYLOAD]; } SilverEv;

typedef struct { int size; TTF_Font *font; } SilverFontSlot;

typedef struct {
    char        *key;
    SDL_Texture *tex;
    int          w, h;       /* piksele fizyczne */
    Uint32       used;
} SilverTextEntry;

typedef struct {
    char        *path;
    SDL_Texture *tex;
    int          w, h;
    Uint32       used;
} SilverImage;

typedef struct {
    int           used;
    int           open;
    SDL_Window   *win;
    SDL_Renderer *ren;
    Uint32        win_id;
    char         *font_path;
    int           font_base;           /* rozmiar logiczny */
    SilverFontSlot fonts[SILVER_MAX_FONTS];
    int           font_count;
    int           scale_pct;           /* 100 = brak HiDPI, 200 = Retina */
    SilverTextEntry texts[SILVER_TEXT_CACHE];
    SilverImage   images[SILVER_IMAGE_CACHE];
    SilverEv      queue[SILVER_EV_QUEUE];
    int           q_head, q_count;
    SilverEv      cur;                 /* ostatnio zwrócone zdarzenie */
} SilverWindow;

static SilverWindow g_windows[SILVER_MAX_WINDOWS];
static int          g_sdl_ready = 0;
static SDL_Cursor  *g_cursors[9];
static char        *g_clip_buf = NULL;

#define VALID(h) ((h) >= 0 && (h) < SILVER_MAX_WINDOWS && g_windows[(h)].used)

static int ensure_sdl(void) {
    if (g_sdl_ready) return 1;
    SDL_SetHint(SDL_HINT_VIDEO_ALLOW_SCREENSAVER, "1");
    SDL_SetHint(SDL_HINT_IME_SHOW_UI, "1");
    if (SDL_Init(SDL_INIT_VIDEO) != 0) return 0;
    if (TTF_Init() != 0) return 0;
    IMG_Init(IMG_INIT_PNG | IMG_INIT_JPG | IMG_INIT_WEBP);
    SDL_EventState(SDL_DROPFILE, SDL_ENABLE);
    SDL_EventState(SDL_DROPTEXT, SDL_ENABLE);
    g_sdl_ready = 1;
    return 1;
}

/* ====================================================================
 *  UTF-8 — H# operuje na bajtach, więc granice znaków liczy shim
 * ==================================================================== */
int silver_utf8_next(const char *s, int pos) {
    if (!s) return 0;
    int n = (int)strlen(s);
    if (pos < 0) pos = 0;
    if (pos >= n) return n;
    pos++;
    while (pos < n && (((unsigned char)s[pos]) & 0xC0) == 0x80) pos++;
    return pos;
}

int silver_utf8_prev(const char *s, int pos) {
    if (!s) return 0;
    int n = (int)strlen(s);
    if (pos > n) pos = n;
    if (pos <= 0) return 0;
    pos--;
    while (pos > 0 && (((unsigned char)s[pos]) & 0xC0) == 0x80) pos--;
    return pos;
}

int silver_utf8_len(const char *s) {
    if (!s) return 0;
    int c = 0;
    for (; *s; s++) if ((((unsigned char)*s) & 0xC0) != 0x80) c++;
    return c;
}

/* offset bajtowy początku znaku o numerze nchars (obcięty do długości) */
int silver_utf8_offset(const char *s, int nchars) {
    if (!s) return 0;
    int n = (int)strlen(s), pos = 0;
    while (nchars > 0 && pos < n) { pos = silver_utf8_next(s, pos); nchars--; }
    return pos;
}

/* Czy proces jest zlinkowany w pełni statycznie? W statycznym (nie-PIE) wykonywalnym nie ma
 * sekcji dynamicznej, więc słaby symbol _DYNAMIC jest NULL. Dzięki temu wybór renderera nie
 * zależy od tego, którą wersję libsilvershim.a linker faktycznie wziął. */
extern char _DYNAMIC[] __attribute__((weak));
static int silver_is_static_exe(void) {
#ifdef SILVER_STATIC_BUILD
    return 1;
#else
    char *volatile dyn = _DYNAMIC;   /* volatile: kompilator nie może założyć, że adres != NULL */
    return dyn == NULL;
#endif
}

/* ====================================================================
 *  Fonty i skala
 * ==================================================================== */
static const char *k_font_candidates[] = {
    "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
    "/usr/share/fonts/dejavu/DejaVuSans.ttf",
    "/usr/share/fonts/TTF/DejaVuSans.ttf",
    "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
    "/usr/share/fonts/liberation/LiberationSans-Regular.ttf",
    "/usr/share/fonts/noto/NotoSans-Regular.ttf",
    "/usr/share/fonts/truetype/noto/NotoSans-Regular.ttf",
    "/usr/share/fonts/google-noto/NotoSans-Regular.ttf",
    "/System/Library/Fonts/Helvetica.ttc",
    "/System/Library/Fonts/Supplemental/Arial.ttf",
    "/Library/Fonts/Arial.ttf",
    "C:\\Windows\\Fonts\\segoeui.ttf",
    "C:\\Windows\\Fonts\\arial.ttf",
    NULL
};

/* Fallback: przeszukaj katalogi z fontami (Fedora/Bazzite/Arch/NixOS… mają inne ścieżki niż
 * Debian). Szukamy po kolei znanych nazw plików, na końcu dowolnego *.ttf z "Sans" w nazwie. */
static char g_font_found[1024];

static int has_ext_ttf(const char *n) {
    size_t l = strlen(n);
    return l > 4 && (strcmp(n + l - 4, ".ttf") == 0 || strcmp(n + l - 4, ".otf") == 0 ||
                     strcmp(n + l - 4, ".TTF") == 0);
}

/* depth-limited DFS; pass 0 = dokładne nazwy preferowane, pass 1 = dowolny *Sans*.ttf */
static int scan_fonts(const char *dir, int depth, int pass) {
    static const char *prefer[] = { "DejaVuSans.ttf", "LiberationSans-Regular.ttf",
                                    "NotoSans-Regular.ttf", "Roboto-Regular.ttf",
                                    "Cantarell-Regular.otf", "Ubuntu-R.ttf", NULL };
    DIR *d = opendir(dir);
    if (!d) return 0;
    struct dirent *de;
    int found = 0;
    while (!found && (de = readdir(d)) != NULL) {
        if (de->d_name[0] == '.') continue;
        char path[1024];
        if (snprintf(path, sizeof path, "%s/%s", dir, de->d_name) >= (int)sizeof path) continue;
        struct stat st;
        if (stat(path, &st) != 0) continue;
        if (S_ISDIR(st.st_mode)) {
            if (depth > 0 && scan_fonts(path, depth - 1, pass)) found = 1;
        } else if (S_ISREG(st.st_mode)) {
            if (pass == 0) {
                for (int i = 0; prefer[i]; i++)
                    if (strcmp(de->d_name, prefer[i]) == 0) { found = 1; break; }
            } else if (has_ext_ttf(de->d_name) && strstr(de->d_name, "Sans") &&
                       !strstr(de->d_name, "Mono") && !strstr(de->d_name, "Bold") &&
                       !strstr(de->d_name, "Italic") && !strstr(de->d_name, "Oblique")) {
                found = 1;
            }
            if (found) { snprintf(g_font_found, sizeof g_font_found, "%s", path); }
        }
    }
    closedir(d);
    return found;
}

const char *silver_find_system_font(void) {
    const char *env = getenv("SILVER_FONT");
    if (env && env[0]) {
        FILE *f = fopen(env, "rb");
        if (f) { fclose(f); snprintf(g_font_found, sizeof g_font_found, "%s", env); return g_font_found; }
    }
    for (int i = 0; k_font_candidates[i]; i++) {
        FILE *f = fopen(k_font_candidates[i], "rb");
        if (f) { fclose(f); return k_font_candidates[i]; }
    }
    static const char *roots[] = { "/usr/share/fonts", "/usr/local/share/fonts",
                                   "/run/host/usr/share/fonts", "/run/host/fonts", NULL };
    for (int pass = 0; pass < 2; pass++) {
        for (int i = 0; roots[i]; i++)
            if (scan_fonts(roots[i], 4, pass)) return g_font_found;
        const char *home = getenv("HOME");
        if (home && home[0]) {
            char hp[900];
            snprintf(hp, sizeof hp, "%s/.local/share/fonts", home);
            if (scan_fonts(hp, 4, pass)) return g_font_found;
            snprintf(hp, sizeof hp, "%s/.fonts", home);
            if (scan_fonts(hp, 4, pass)) return g_font_found;
        }
    }
    return "";
}

static void flush_fonts(SilverWindow *w) {
    for (int i = 0; i < w->font_count; i++)
        if (w->fonts[i].font) TTF_CloseFont(w->fonts[i].font);
    w->font_count = 0;
}

static void flush_texts(SilverWindow *w) {
    for (int i = 0; i < SILVER_TEXT_CACHE; i++) {
        if (w->texts[i].tex) SDL_DestroyTexture(w->texts[i].tex);
        free(w->texts[i].key);
        memset(&w->texts[i], 0, sizeof(SilverTextEntry));
    }
}

static void update_scale(SilverWindow *w) {
    int ow = 0, oh = 0, ww = 0, wh = 0;
    SDL_GetRendererOutputSize(w->ren, &ow, &oh);
    SDL_GetWindowSize(w->win, &ww, &wh);
    int pct = (ww > 0 && ow > 0) ? (int)((ow * 100.0f) / ww + 0.5f) : 100;
    if (pct < 100) pct = 100;
    if (pct != w->scale_pct) {
        w->scale_pct = pct;
        flush_fonts(w);
        flush_texts(w);
    }
    SDL_RenderSetScale(w->ren, pct / 100.0f, pct / 100.0f);
}

/* font o rozmiarze logicznym `size` (0 = bazowy) w pikselach fizycznych */
static TTF_Font *get_font(SilverWindow *w, int size) {
    if (!w->font_path) return NULL;
    if (size <= 0) size = w->font_base;
    int phys = (size * w->scale_pct + 50) / 100;
    if (phys < 4) phys = 4;
    for (int i = 0; i < w->font_count; i++)
        if (w->fonts[i].size == phys) return w->fonts[i].font;
    if (w->font_count >= SILVER_MAX_FONTS) {
        TTF_CloseFont(w->fonts[0].font);
        memmove(&w->fonts[0], &w->fonts[1], sizeof(SilverFontSlot) * (SILVER_MAX_FONTS - 1));
        w->font_count--;
    }
    TTF_Font *f = TTF_OpenFont(w->font_path, phys);
    if (!f) return NULL;
    w->fonts[w->font_count].size = phys;
    w->fonts[w->font_count].font = f;
    w->font_count++;
    return f;
}

/* ====================================================================
 *  Okna
 * ==================================================================== */
int silver_window_create(const char *title, int w, int h) {
    if (!ensure_sdl()) return -1;
    int slot = -1;
    for (int i = 0; i < SILVER_MAX_WINDOWS; i++)
        if (!g_windows[i].used) { slot = i; break; }
    if (slot < 0) return -1;

    /* Ostrzeż, gdy SDL cicho wybrał sterownik bez widocznego okna (np. brak DISPLAY → dummy). */
    {
        const char *vd = SDL_GetCurrentVideoDriver();
        if (vd && (strcmp(vd, "dummy") == 0 || strcmp(vd, "offscreen") == 0) &&
            !getenv("SDL_VIDEODRIVER"))
            fprintf(stderr, "[silver] UWAGA: wybrano sterownik '%s' — okno nie będzie widoczne "
                            "(X11 niedostępne? DISPLAY='%s')\n", vd,
                    getenv("DISPLAY") ? getenv("DISPLAY") : "");
        /* 'offscreen' w SDL 2.30 nie ma framebufferu dla renderera programowego (segfault
         * w SDL_CreateRenderer) — działa tylko z GL/EGL, którego statyczny glibc nie obsłuży. */
        if (vd && strcmp(vd, "offscreen") == 0) {
            const char *rp = getenv("SILVER_RENDERER");
            if (silver_is_static_exe() && !(rp && strcmp(rp, "accelerated") == 0)) {
                fprintf(stderr, "[silver] BŁĄD: sterownik 'offscreen' wymaga OpenGL/EGL "
                                "(SILVER_RENDERER=accelerated), a w buildzie statycznym domyślnie "
                                "renderujemy programowo. Użyj SDL_VIDEODRIVER=dummy lub x11.\n");
                return -1;
            }
        }
    }

    if (getenv("SILVER_DEBUG"))
        fprintf(stderr, "[silver] okno: video=%s static=%d renderer-pref=%s\n",
                SDL_GetCurrentVideoDriver() ? SDL_GetCurrentVideoDriver() : "?",
                silver_is_static_exe(), getenv("SILVER_RENDERER") ? getenv("SILVER_RENDERER") : "(auto)");
    SDL_Window *win = SDL_CreateWindow(
        title ? title : "", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        w, h, SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    if (!win) return -1;

    /* Wybór renderera. SILVER_RENDERER=software|accelerated nadpisuje domyślne zachowanie.
     * Domyślnie: akcelerowany (GL), a w buildzie W PEŁNI STATYCZNYM (-DSILVER_STATIC_BUILD)
     * programowy — statyczny glibc nie potrafi dlopen-ować sterowników GL hosta (Mesa/libLLVM)
     * i kończy się to SIGSEGV / "double free or corruption". */
    const char *rpref = getenv("SILVER_RENDERER");
    int want_sw = 0;
    if (silver_is_static_exe()) want_sw = 1;
    if (rpref && strcmp(rpref, "software") == 0) want_sw = 1;
    if (rpref && strcmp(rpref, "accelerated") == 0) want_sw = 0;

    /* Renderer programowy: wyłącz też framebuffer z akcelerowanej tekstury. Domyślnie SDL przy
     * SDL_GetWindowSurface próbuje najpierw renderera GL (dlopen sterowników hosta) — w buildzie
     * statycznym to SIGSEGV w SDL_CreateRenderer. */
    if (want_sw) {
        SDL_SetHint(SDL_HINT_FRAMEBUFFER_ACCELERATION, "0");
        SDL_SetHint(SDL_HINT_RENDER_DRIVER, "software");
    }

    SDL_Renderer *ren = NULL;
    if (!want_sw)
        ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!ren) ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_SOFTWARE);
    if (!ren) { SDL_DestroyWindow(win); return -1; }

    if (getenv("SILVER_DEBUG")) {
        SDL_RendererInfo ri; const char *rn = "?";
        if (SDL_GetRendererInfo(ren, &ri) == 0) rn = ri.name;
        fprintf(stderr, "[silver] video=%s renderer=%s\n", SDL_GetCurrentVideoDriver(), rn);
    }
    SilverWindow *sw = &g_windows[slot];
    memset(sw, 0, sizeof(*sw));
    sw->used = 1; sw->open = 1; sw->win = win; sw->ren = ren;
    sw->win_id = SDL_GetWindowID(win);
    sw->scale_pct = 100;
    sw->font_base = 14;
    update_scale(sw);
    return slot;
}

/* path == "" → automatyczne wyszukanie fontu systemowego */
int silver_load_font(int h, const char *path, int size) {
    if (!VALID(h)) return 0;
    SilverWindow *w = &g_windows[h];
    const char *p = (path && path[0]) ? path : silver_find_system_font();
    if (getenv("SILVER_DEBUG"))
        fprintf(stderr, "[silver] font: %s (zadany: '%s', rozmiar %d)\n",
                p[0] ? p : "BRAK CZCIONKI — ustaw SILVER_FONT=/sciezka/do/font.ttf",
                path ? path : "", size);
    if (!p[0]) return 0;
    char *copy = strdup(p);
    if (!copy) return 0;
    flush_fonts(w);
    flush_texts(w);
    free(w->font_path);
    w->font_path = copy;
    w->font_base = size > 0 ? size : 14;
    return get_font(w, 0) != NULL;
}

int silver_is_open(int h) { return VALID(h) ? g_windows[h].open : 0; }

int silver_get_width(int h) {
    if (!VALID(h)) return 0;
    int w = 0, hh = 0; SDL_GetWindowSize(g_windows[h].win, &w, &hh); return w;
}
int silver_get_height(int h) {
    if (!VALID(h)) return 0;
    int w = 0, hh = 0; SDL_GetWindowSize(g_windows[h].win, &w, &hh); return hh;
}
/* 100 = 1x, 200 = 2x (HiDPI/Retina) */
int silver_get_scale(int h) { return VALID(h) ? g_windows[h].scale_pct : 100; }

int silver_set_title(int h, const char *t) {
    if (!VALID(h)) return 0;
    SDL_SetWindowTitle(g_windows[h].win, t ? t : ""); return 1;
}
int silver_set_min_size(int h, int w, int hh) {
    if (!VALID(h)) return 0;
    SDL_SetWindowMinimumSize(g_windows[h].win, w, hh); return 1;
}
int silver_set_max_size(int h, int w, int hh) {
    if (!VALID(h)) return 0;
    SDL_SetWindowMaximumSize(g_windows[h].win, w, hh); return 1;
}
int silver_set_size(int h, int w, int hh) {
    if (!VALID(h)) return 0;
    SDL_SetWindowSize(g_windows[h].win, w, hh); return 1;
}
int silver_set_position(int h, int x, int y) {
    if (!VALID(h)) return 0;
    SDL_SetWindowPosition(g_windows[h].win, x, y); return 1;
}
int silver_get_x(int h) {
    if (!VALID(h)) return 0;
    int x = 0, y = 0; SDL_GetWindowPosition(g_windows[h].win, &x, &y); return x;
}
int silver_get_y(int h) {
    if (!VALID(h)) return 0;
    int x = 0, y = 0; SDL_GetWindowPosition(g_windows[h].win, &x, &y); return y;
}
int silver_set_fullscreen(int h, int on) {
    if (!VALID(h)) return 0;
    return SDL_SetWindowFullscreen(g_windows[h].win, on ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0) == 0;
}
int silver_is_fullscreen(int h) {
    if (!VALID(h)) return 0;
    return (SDL_GetWindowFlags(g_windows[h].win) & SDL_WINDOW_FULLSCREEN_DESKTOP) ? 1 : 0;
}
int silver_set_resizable(int h, int on) {
    if (!VALID(h)) return 0;
    SDL_SetWindowResizable(g_windows[h].win, on ? SDL_TRUE : SDL_FALSE); return 1;
}
int silver_set_bordered(int h, int on) {
    if (!VALID(h)) return 0;
    SDL_SetWindowBordered(g_windows[h].win, on ? SDL_TRUE : SDL_FALSE); return 1;
}
int silver_set_always_on_top(int h, int on) {
#if SDL_VERSION_ATLEAST(2, 0, 16)
    if (!VALID(h)) return 0;
    SDL_SetWindowAlwaysOnTop(g_windows[h].win, on ? SDL_TRUE : SDL_FALSE); return 1;
#else
    (void)h; (void)on; return 0;
#endif
}
int silver_set_opacity(int h, int pct) {
    if (!VALID(h)) return 0;
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
    return SDL_SetWindowOpacity(g_windows[h].win, pct / 100.0f) == 0;
}
int silver_maximize(int h) { if (!VALID(h)) return 0; SDL_MaximizeWindow(g_windows[h].win); return 1; }
int silver_minimize(int h) { if (!VALID(h)) return 0; SDL_MinimizeWindow(g_windows[h].win); return 1; }
int silver_restore(int h)  { if (!VALID(h)) return 0; SDL_RestoreWindow(g_windows[h].win);  return 1; }
int silver_raise(int h)    { if (!VALID(h)) return 0; SDL_RaiseWindow(g_windows[h].win);    return 1; }
int silver_show(int h)     { if (!VALID(h)) return 0; SDL_ShowWindow(g_windows[h].win);     return 1; }
int silver_hide(int h)     { if (!VALID(h)) return 0; SDL_HideWindow(g_windows[h].win);     return 1; }

int silver_set_icon(int h, const char *path) {
    if (!VALID(h) || !path || !path[0]) return 0;
    SDL_Surface *s = IMG_Load(path);
    if (!s) return 0;
    SDL_SetWindowIcon(g_windows[h].win, s);
    SDL_FreeSurface(s);
    return 1;
}

int silver_window_close(int h) {
    if (!VALID(h)) return 0;
    SilverWindow *w = &g_windows[h];
    flush_fonts(w);
    flush_texts(w);
    for (int i = 0; i < SILVER_IMAGE_CACHE; i++) {
        if (w->images[i].tex) SDL_DestroyTexture(w->images[i].tex);
        free(w->images[i].path);
    }
    free(w->font_path);
    SDL_DestroyRenderer(w->ren);
    SDL_DestroyWindow(w->win);
    memset(w, 0, sizeof(*w));
    return 1;
}

int silver_delay(int ms) { SDL_Delay((Uint32)(ms > 0 ? ms : 0)); return 1; }
int silver_ticks(void)   { return (int)(SDL_GetTicks() & 0x7fffffff); }

/* ====================================================================
 *  Zdarzenia: SDL → kolejki per okno
 *  Kody: 0 brak, 1 QUIT/CLOSE, 2 MOUSE_DOWN, 3 MOUSE_UP, 4 MOUSE_MOVE,
 *        5 KEY_DOWN, 6 RESIZE, 7 TEXT_INPUT, 8 MOUSE_WHEEL,
 *        9 FOCUS_GAINED, 10 FOCUS_LOST, 11 KEY_UP, 12 DROP_FILE,
 *        13 TEXT_EDITING (IME), 14 DROP_TEXT, 15 EXPOSED, 16 LEAVE
 *  Payloady (pola rozdzielone '|'):
 *    2/3  x|y|button|clicks|mods      4 x|y|buttons|mods
 *    5/11 mods|repeat|keycode|scancode
 *    6    w|h|scale_pct               7 0|0|tekst
 *    8    dx|dy|mods|x|y (dx/dy w px) 12/14 0|0|ścieżka / tekst
 *    13   start|length|tekst
 *  mods: 1 shift, 2 ctrl, 4 alt, 8 gui (cmd/win)
 * ==================================================================== */
static int mods_now(void) {
    SDL_Keymod m = SDL_GetModState();
    int r = 0;
    if (m & KMOD_SHIFT) r |= 1;
    if (m & KMOD_CTRL)  r |= 2;
    if (m & KMOD_ALT)   r |= 4;
    if (m & KMOD_GUI)   r |= 8;
    return r;
}

static SilverWindow *by_window_id(Uint32 id) {
    for (int i = 0; i < SILVER_MAX_WINDOWS; i++)
        if (g_windows[i].used && g_windows[i].win_id == id) return &g_windows[i];
    return NULL;
}

static void push_ev(SilverWindow *w, int code, const char *fmt, ...) {
    if (!w) return;
    char buf[SILVER_EV_PAYLOAD];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    /* scalanie ruchów myszy — ostatni wygrywa */
    if (code == 4 && w->q_count > 0) {
        SilverEv *last = &w->queue[(w->q_head + w->q_count - 1) % SILVER_EV_QUEUE];
        if (last->code == 4) { memcpy(last->payload, buf, sizeof(buf)); return; }
    }
    if (w->q_count >= SILVER_EV_QUEUE) {
        /* pełna: zgub najstarsze (nigdy nie blokuj pętli) */
        w->q_head = (w->q_head + 1) % SILVER_EV_QUEUE;
        w->q_count--;
    }
    SilverEv *e = &w->queue[(w->q_head + w->q_count) % SILVER_EV_QUEUE];
    e->code = code;
    memcpy(e->payload, buf, sizeof(buf));
    w->q_count++;
}

static void route(SDL_Event *e) {
    SilverWindow *w;
    switch (e->type) {
    case SDL_QUIT:
        for (int i = 0; i < SILVER_MAX_WINDOWS; i++)
            if (g_windows[i].used) push_ev(&g_windows[i], 1, "0|0|0");
        break;
    case SDL_MOUSEBUTTONDOWN:
        push_ev(by_window_id(e->button.windowID), 2, "%d|%d|%d|%d|%d",
                e->button.x, e->button.y, e->button.button, e->button.clicks, mods_now());
        break;
    case SDL_MOUSEBUTTONUP:
        push_ev(by_window_id(e->button.windowID), 3, "%d|%d|%d|%d|%d",
                e->button.x, e->button.y, e->button.button, e->button.clicks, mods_now());
        break;
    case SDL_MOUSEMOTION:
        push_ev(by_window_id(e->motion.windowID), 4, "%d|%d|%u|%d",
                e->motion.x, e->motion.y, (unsigned)e->motion.state, mods_now());
        break;
    case SDL_MOUSEWHEEL: {
        int dx = e->wheel.x, dy = e->wheel.y;
        if (e->wheel.direction == SDL_MOUSEWHEEL_FLIPPED) { dx = -dx; dy = -dy; }
        int mx = 0, my = 0; SDL_GetMouseState(&mx, &my);
        push_ev(by_window_id(e->wheel.windowID), 8, "%d|%d|%d|%d|%d",
                dx * 24, dy * 24, mods_now(), mx, my);
        break;
    }
    case SDL_KEYDOWN:
        push_ev(by_window_id(e->key.windowID), 5, "%d|%d|%d|%d",
                mods_now(), (int)e->key.repeat, (int)e->key.keysym.sym, (int)e->key.keysym.scancode);
        break;
    case SDL_KEYUP:
        push_ev(by_window_id(e->key.windowID), 11, "%d|%d|%d|%d",
                mods_now(), 0, (int)e->key.keysym.sym, (int)e->key.keysym.scancode);
        break;
    case SDL_TEXTINPUT:
        push_ev(by_window_id(e->text.windowID), 7, "0|0|%s", e->text.text);
        break;
    case SDL_TEXTEDITING:
        push_ev(by_window_id(e->edit.windowID), 13, "%d|%d|%s",
                e->edit.start, e->edit.length, e->edit.text);
        break;
    case SDL_DROPFILE:
        w = by_window_id(e->drop.windowID);
        if (!w) { for (int i = 0; i < SILVER_MAX_WINDOWS; i++) if (g_windows[i].used) { w = &g_windows[i]; break; } }
        if (e->drop.file) { push_ev(w, 12, "0|0|%s", e->drop.file); SDL_free(e->drop.file); }
        break;
    case SDL_DROPTEXT:
        w = by_window_id(e->drop.windowID);
        if (!w) { for (int i = 0; i < SILVER_MAX_WINDOWS; i++) if (g_windows[i].used) { w = &g_windows[i]; break; } }
        if (e->drop.file) { push_ev(w, 14, "0|0|%s", e->drop.file); SDL_free(e->drop.file); }
        break;
    case SDL_WINDOWEVENT:
        w = by_window_id(e->window.windowID);
        if (!w) break;
        switch (e->window.event) {
        case SDL_WINDOWEVENT_CLOSE:
            push_ev(w, 1, "0|0|0"); break;
        case SDL_WINDOWEVENT_SIZE_CHANGED:
        case SDL_WINDOWEVENT_DISPLAY_CHANGED:
            update_scale(w);
            push_ev(w, 6, "%d|%d|%d", silver_get_width((int)(w - g_windows)),
                    silver_get_height((int)(w - g_windows)), w->scale_pct);
            break;
        case SDL_WINDOWEVENT_FOCUS_GAINED: push_ev(w, 9, "0|0|0"); break;
        case SDL_WINDOWEVENT_FOCUS_LOST:   push_ev(w, 10, "0|0|0"); break;
        case SDL_WINDOWEVENT_EXPOSED:
        case SDL_WINDOWEVENT_RESTORED:
        case SDL_WINDOWEVENT_SHOWN:        push_ev(w, 15, "0|0|0"); break;
        case SDL_WINDOWEVENT_LEAVE:        push_ev(w, 16, "0|0|0"); break;
        default: break;
        }
        break;
    default: break;
    }
}

static int pump(int timeout_ms) {
    SDL_Event e;
    int got = 0;
    if (timeout_ms > 0) {
        if (SDL_WaitEventTimeout(&e, timeout_ms)) { route(&e); got = 1; }
    }
    while (SDL_PollEvent(&e)) { route(&e); got = 1; }
    return got;
}

/* Czeka (maks. ms) na jakiekolwiek zdarzenie i rozdziela je do okien.
 * Zwraca true, jeśli cokolwiek przyszło. Pętla aplikacji woła to zamiast
 * sztywnego silver_delay(16) — brak zdarzeń = brak zużycia CPU. */
int silver_wait_event(int ms) {
    if (!g_sdl_ready) return 0;
    return pump(ms);
}

int silver_poll_event(int h) {
    if (!VALID(h)) return 0;
    SilverWindow *w = &g_windows[h];
    if (w->q_count == 0) pump(0);
    if (w->q_count == 0) return 0;
    w->cur = w->queue[w->q_head];
    w->q_head = (w->q_head + 1) % SILVER_EV_QUEUE;
    w->q_count--;
    if (w->cur.code == 1) w->open = 0;
    return w->cur.code;
}

const char *silver_event_payload(int h) {
    return VALID(h) ? g_windows[h].cur.payload : "";
}

/* ====================================================================
 *  Rysowanie (współrzędne logiczne)
 * ==================================================================== */
int silver_clear(int h, int r, int g, int b) {
    if (!VALID(h)) return 0;
    SDL_SetRenderDrawColor(g_windows[h].ren, r, g, b, 255);
    SDL_RenderClear(g_windows[h].ren);
    return 1;
}

static void set_col(SilverWindow *w, int r, int g, int b, int a) {
    SDL_SetRenderDrawBlendMode(w->ren, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(w->ren, r, g, b, a);
}

int silver_fill_rect(int h, int x, int y, int w, int rh, int r, int g, int b, int a) {
    if (!VALID(h) || w <= 0 || rh <= 0) return 0;
    set_col(&g_windows[h], r, g, b, a);
    SDL_Rect rect = { x, y, w, rh };
    SDL_RenderFillRect(g_windows[h].ren, &rect);
    return 1;
}

int silver_stroke_rect(int h, int x, int y, int w, int rh, int r, int g, int b, int a) {
    if (!VALID(h) || w <= 0 || rh <= 0) return 0;
    set_col(&g_windows[h], r, g, b, a);
    SDL_Rect rect = { x, y, w, rh };
    SDL_RenderDrawRect(g_windows[h].ren, &rect);
    return 1;
}

/* wcięcie wiersza `j` (0..hh-1) zaokrąglonego prostokąta o promieniu rad */
static int row_inset(int j, int hh, int rad) {
    if (rad <= 0) return 0;
    int dy;
    if (j < rad) dy = rad - j;
    else if (j >= hh - rad) dy = j - (hh - rad) + 1;
    else return 0;
    float fy = (float)dy - 0.5f;
    float fr = (float)rad;
    float inset = fr - sqrtf(fmaxf(fr * fr - fy * fy, 0.0f));
    return (int)ceilf(inset - 0.25f);
}

static int clamp_radius(int rad, int w, int hh) {
    int m = (w < hh ? w : hh) / 2;
    if (rad > m) rad = m;
    return rad < 0 ? 0 : rad;
}

int silver_fill_round_rect(int h, int x, int y, int w, int hh, int rad,
                           int r, int g, int b, int a) {
    if (!VALID(h) || w <= 0 || hh <= 0) return 0;
    SilverWindow *sw = &g_windows[h];
    rad = clamp_radius(rad, w, hh);
    set_col(sw, r, g, b, a);
    if (rad == 0) {
        SDL_Rect rc = { x, y, w, hh };
        SDL_RenderFillRect(sw->ren, &rc);
        return 1;
    }
    for (int j = 0; j < rad; j++) {
        int in = row_inset(j, hh, rad);
        SDL_Rect rc = { x + in, y + j, w - 2 * in, 1 };
        SDL_RenderFillRect(sw->ren, &rc);
    }
    if (hh - 2 * rad > 0) {
        SDL_Rect mid = { x, y + rad, w, hh - 2 * rad };
        SDL_RenderFillRect(sw->ren, &mid);
    }
    for (int j = hh - rad; j < hh; j++) {
        int in = row_inset(j, hh, rad);
        SDL_Rect rc = { x + in, y + j, w - 2 * in, 1 };
        SDL_RenderFillRect(sw->ren, &rc);
    }
    return 1;
}

/* obramowanie o grubości bw; bez nakładania się pikseli (poprawna alfa) */
int silver_stroke_round_rect(int h, int x, int y, int w, int hh, int rad, int bw,
                             int r, int g, int b, int a) {
    if (!VALID(h) || w <= 0 || hh <= 0 || bw <= 0) return 0;
    SilverWindow *sw = &g_windows[h];
    rad = clamp_radius(rad, w, hh);
    if (bw * 2 >= w || bw * 2 >= hh) return silver_fill_round_rect(h, x, y, w, hh, rad, r, g, b, a);
    set_col(sw, r, g, b, a);
    if (rad == 0) {
        SDL_Rect t = { x, y, w, bw }, bt = { x, y + hh - bw, w, bw };
        SDL_Rect l = { x, y + bw, bw, hh - 2 * bw }, rt = { x + w - bw, y + bw, bw, hh - 2 * bw };
        SDL_RenderFillRect(sw->ren, &t);  SDL_RenderFillRect(sw->ren, &bt);
        SDL_RenderFillRect(sw->ren, &l);  SDL_RenderFillRect(sw->ren, &rt);
        return 1;
    }
    int irad = rad - bw; if (irad < 0) irad = 0;
    int iw = w - 2 * bw, ih = hh - 2 * bw;
    for (int j = 0; j < hh; j++) {
        if (j >= rad && j < hh - rad) {
            if (j == rad) { /* środek: dwa pionowe paski na całą wysokość */
                int len = hh - 2 * rad;
                SDL_Rect l = { x, y + rad, bw, len }, rt = { x + w - bw, y + rad, bw, len };
                SDL_RenderFillRect(sw->ren, &l); SDL_RenderFillRect(sw->ren, &rt);
            }
            continue;
        }
        int oin = row_inset(j, hh, rad);
        int ox0 = x + oin, ox1 = x + w - oin;
        int ij = j - bw;
        if (ij >= 0 && ij < ih) {
            int iin = row_inset(ij, ih, irad);
            int ix0 = x + bw + iin, ix1 = x + bw + iw - iin;
            if (ix0 > ox0) { SDL_Rect a1 = { ox0, y + j, ix0 - ox0, 1 }; SDL_RenderFillRect(sw->ren, &a1); }
            if (ox1 > ix1) { SDL_Rect a2 = { ix1, y + j, ox1 - ix1, 1 }; SDL_RenderFillRect(sw->ren, &a2); }
        } else {
            SDL_Rect full = { ox0, y + j, ox1 - ox0, 1 };
            SDL_RenderFillRect(sw->ren, &full);
        }
    }
    return 1;
}

/* liniowy gradient dwukolorowy; vertical=1: góra→dół, 0: lewo→prawo */
int silver_fill_gradient(int h, int x, int y, int w, int hh,
                         int r1, int g1, int b1, int a1,
                         int r2, int g2, int b2, int a2, int vertical) {
    if (!VALID(h) || w <= 0 || hh <= 0) return 0;
    SilverWindow *sw = &g_windows[h];
    int n = vertical ? hh : w;
    for (int i = 0; i < n; i++) {
        float t = n > 1 ? (float)i / (float)(n - 1) : 0.0f;
        int r = (int)(r1 + (r2 - r1) * t + 0.5f), g = (int)(g1 + (g2 - g1) * t + 0.5f);
        int b = (int)(b1 + (b2 - b1) * t + 0.5f), a = (int)(a1 + (a2 - a1) * t + 0.5f);
        set_col(sw, r, g, b, a);
        SDL_Rect rc = vertical ? (SDL_Rect){ x, y + i, w, 1 } : (SDL_Rect){ x + i, y, 1, hh };
        SDL_RenderFillRect(sw->ren, &rc);
    }
    return 1;
}

int silver_clip_set(int h, int x, int y, int w, int hh) {
    if (!VALID(h)) return 0;
    SDL_Rect rc = { x, y, w < 0 ? 0 : w, hh < 0 ? 0 : hh };
    return SDL_RenderSetClipRect(g_windows[h].ren, &rc) == 0;
}
int silver_clip_clear(int h) {
    if (!VALID(h)) return 0;
    return SDL_RenderSetClipRect(g_windows[h].ren, NULL) == 0;
}

/* ---- tekst ---- */
static Uint32 fnv(const char *s) {
    Uint32 h = 2166136261u;
    while (*s) { h ^= (unsigned char)*s++; h *= 16777619u; }
    return h;
}

static SilverTextEntry *text_entry(SilverWindow *w, TTF_Font *font, const char *text,
                                   int size, int style, int r, int g, int b) {
    size_t kl = strlen(text) + 48;
    char *key = (char *)malloc(kl);
    if (!key) return NULL;
    snprintf(key, kl, "%d|%d|%d,%d,%d|%s", size, style, r, g, b, text);
    (void)fnv;
    static Uint32 clock_ = 0;
    clock_++;
    int free_slot = -1, oldest = 0;
    for (int i = 0; i < SILVER_TEXT_CACHE; i++) {
        SilverTextEntry *e = &w->texts[i];
        if (e->key && strcmp(e->key, key) == 0) { e->used = clock_; free(key); return e; }
        if (!e->key && free_slot < 0) free_slot = i;
        if (e->key && e->used < w->texts[oldest].used) oldest = i;
    }
    int slot = free_slot >= 0 ? free_slot : oldest;
    SilverTextEntry *e = &w->texts[slot];
    if (e->key) { SDL_DestroyTexture(e->tex); free(e->key); memset(e, 0, sizeof(*e)); }

    TTF_SetFontStyle(font, (style & 1 ? TTF_STYLE_BOLD : 0) | (style & 2 ? TTF_STYLE_ITALIC : 0) |
                           (style & 4 ? TTF_STYLE_UNDERLINE : 0) | (style & 8 ? TTF_STYLE_STRIKETHROUGH : 0));
    SDL_Color col = { (Uint8)r, (Uint8)g, (Uint8)b, 255 };
    SDL_Surface *surf = TTF_RenderUTF8_Blended(font, text, col);
    if (!surf) { free(key); return NULL; }
    SDL_Texture *tex = SDL_CreateTextureFromSurface(w->ren, surf);
    e->w = surf->w; e->h = surf->h;
    SDL_FreeSurface(surf);
    if (!tex) { free(key); memset(e, 0, sizeof(*e)); return NULL; }
    SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
    e->key = key; e->tex = tex; e->used = clock_;
    return e;
}

/* style: 1 bold, 2 italic, 4 underline, 8 strikethrough; size 0 = bazowy.
 * Zwraca wysokość tekstu (px logiczne). */
int silver_draw_text_ex(int h, int x, int y, const char *text, int r, int g, int b, int a,
                        int size, int style) {
    if (!VALID(h) || !text || !text[0]) return 0;
    SilverWindow *w = &g_windows[h];
    TTF_Font *font = get_font(w, size);
    if (!font) return 0;
    SilverTextEntry *e = text_entry(w, font, text, size, style, r, g, b);
    if (!e) return 0;
    float fw = e->w * 100.0f / w->scale_pct, fh = e->h * 100.0f / w->scale_pct;
    SDL_SetTextureAlphaMod(e->tex, (Uint8)a);
    SDL_FRect dst = { (float)x, (float)y, fw, fh };
    SDL_RenderCopyF(w->ren, e->tex, NULL, &dst);
    return (int)(fh + 0.5f);
}

int silver_draw_text(int h, int x, int y, const char *text, int r, int g, int b, int a) {
    return silver_draw_text_ex(h, x, y, text, r, g, b, a, 0, 0);
}

int silver_measure_text_ex(int h, const char *text, int size, int style) {
    if (!VALID(h) || !text) return 0;
    SilverWindow *w = &g_windows[h];
    TTF_Font *font = get_font(w, size);
    if (!font) return 0;
    TTF_SetFontStyle(font, (style & 1 ? TTF_STYLE_BOLD : 0) | (style & 2 ? TTF_STYLE_ITALIC : 0));
    int tw = 0, th = 0;
    TTF_SizeUTF8(font, text, &tw, &th);
    return (tw * 100 + w->scale_pct - 1) / w->scale_pct;
}

int silver_measure_text(int h, const char *text) {
    return silver_measure_text_ex(h, text, 0, 0);
}

int silver_font_height(int h, int size) {
    if (!VALID(h)) return 0;
    SilverWindow *w = &g_windows[h];
    TTF_Font *font = get_font(w, size);
    if (!font) return 0;
    return (TTF_FontHeight(font) * 100 + w->scale_pct - 1) / w->scale_pct;
}

/* największy prefiks `text` (offset bajtowy, na granicy znaku), który mieści
 * się w max_w px — do zawijania wierszy bez pomiaru znak po znaku. */
int silver_text_fit(int h, const char *text, int size, int style, int max_w) {
    if (!VALID(h) || !text) return 0;
    int n = (int)strlen(text);
    if (silver_measure_text_ex(h, text, size, style) <= max_w) return n;
    int lo = 0, hi = silver_utf8_len(text);       /* w znakach */
    while (lo < hi) {
        int mid = (lo + hi + 1) / 2;
        int off = silver_utf8_offset(text, mid);
        char *tmp = (char *)malloc((size_t)off + 1);
        if (!tmp) return 0;
        memcpy(tmp, text, (size_t)off); tmp[off] = '\0';
        int wd = silver_measure_text_ex(h, tmp, size, style);
        free(tmp);
        if (wd <= max_w) lo = mid; else hi = mid - 1;
    }
    return silver_utf8_offset(text, lo);
}

int silver_present(int h) {
    if (!VALID(h)) return 0;
    SDL_RenderPresent(g_windows[h].ren);
    return 1;
}

/* ---- input tekstowy / IME ---- */
int silver_start_text_input(int h) { if (!VALID(h)) return 0; SDL_StartTextInput(); return 1; }
int silver_stop_text_input(int h)  { if (!VALID(h)) return 0; SDL_StopTextInput();  return 1; }
int silver_set_text_input_rect(int h, int x, int y, int w, int hh) {
    if (!VALID(h)) return 0;
    SDL_Rect r = { x, y, w, hh };
    SDL_SetTextInputRect(&r);
    return 1;
}

/* ---- obrazki: cache LRU kluczowany pełną ścieżką ---- */
static SilverImage *find_or_load_image(int h, const char *path) {
    SilverWindow *w = &g_windows[h];
    static Uint32 clock_ = 0;
    clock_++;
    int free_slot = -1, oldest = 0;
    for (int i = 0; i < SILVER_IMAGE_CACHE; i++) {
        SilverImage *im = &w->images[i];
        if (im->path && strcmp(im->path, path) == 0) { im->used = clock_; return im; }
        if (!im->path && free_slot < 0) free_slot = i;
        if (im->path && im->used < w->images[oldest].used) oldest = i;
    }
    SDL_Surface *surf = IMG_Load(path);
    if (!surf) return NULL;
    SDL_Texture *tex = SDL_CreateTextureFromSurface(w->ren, surf);
    int iw = surf->w, ih = surf->h;
    SDL_FreeSurface(surf);
    if (!tex) return NULL;
    int slot = free_slot >= 0 ? free_slot : oldest;
    SilverImage *im = &w->images[slot];
    if (im->path) { SDL_DestroyTexture(im->tex); free(im->path); }
    im->path = strdup(path);
    im->tex = tex; im->w = iw; im->h = ih; im->used = clock_;
    return im;
}

int silver_image_width(int h, const char *path) {
    if (!VALID(h) || !path) return 0;
    SilverImage *im = find_or_load_image(h, path);
    return im ? im->w : 0;
}
int silver_image_height(int h, const char *path) {
    if (!VALID(h) || !path) return 0;
    SilverImage *im = find_or_load_image(h, path);
    return im ? im->h : 0;
}
/* usuwa obrazek z cache (np. po zmianie pliku na dysku) */
int silver_image_forget(int h, const char *path) {
    if (!VALID(h) || !path) return 0;
    SilverWindow *w = &g_windows[h];
    for (int i = 0; i < SILVER_IMAGE_CACHE; i++) {
        if (w->images[i].path && strcmp(w->images[i].path, path) == 0) {
            SDL_DestroyTexture(w->images[i].tex);
            free(w->images[i].path);
            memset(&w->images[i], 0, sizeof(SilverImage));
            return 1;
        }
    }
    return 0;
}

int silver_draw_image(int h, const char *path, int x, int y, int w, int rh, int a) {
    if (!VALID(h) || !path) return 0;
    SilverImage *im = find_or_load_image(h, path);
    if (!im) return 0;
    SDL_SetTextureBlendMode(im->tex, SDL_BLENDMODE_BLEND);
    SDL_SetTextureAlphaMod(im->tex, (Uint8)a);
    SDL_Rect dst = { x, y, w, rh };
    SDL_RenderCopy(g_windows[h].ren, im->tex, NULL, &dst);
    return 1;
}

/* ====================================================================
 *  Kursor, schowek, powiadomienia, motyw
 * ==================================================================== */
/* 0 strzałka, 1 rączka, 2 I-beam, 3 krzyżyk, 4 zegar, 5 przesuwanie,
 * 6 ↕, 7 ↔, 8 zakaz */
int silver_set_cursor(int h, int kind) {
    if (!VALID(h) || kind < 0 || kind > 8) return 0;
    static const SDL_SystemCursor map[9] = {
        SDL_SYSTEM_CURSOR_ARROW, SDL_SYSTEM_CURSOR_HAND, SDL_SYSTEM_CURSOR_IBEAM,
        SDL_SYSTEM_CURSOR_CROSSHAIR, SDL_SYSTEM_CURSOR_WAIT, SDL_SYSTEM_CURSOR_SIZEALL,
        SDL_SYSTEM_CURSOR_SIZENS, SDL_SYSTEM_CURSOR_SIZEWE, SDL_SYSTEM_CURSOR_NO };
    if (!g_cursors[kind]) g_cursors[kind] = SDL_CreateSystemCursor(map[kind]);
    if (!g_cursors[kind]) return 0;
    SDL_SetCursor(g_cursors[kind]);
    return 1;
}

const char *silver_clipboard_get(void) {
    if (!g_sdl_ready || !SDL_HasClipboardText()) return "";
    char *t = SDL_GetClipboardText();
    free(g_clip_buf);
    g_clip_buf = t ? strdup(t) : NULL;
    if (t) SDL_free(t);
    return g_clip_buf ? g_clip_buf : "";
}

int silver_clipboard_set(const char *text) {
    if (!g_sdl_ready) return 0;
    return SDL_SetClipboardText(text ? text : "") == 0;
}

/* cytat dla powłoki: 'a'\''b' */
static void shq(const char *in, char *out, size_t cap) {
    size_t o = 0;
    if (o < cap) out[o++] = '\'';
    for (; in && *in && o + 5 < cap; in++) {
        if (*in == '\'') { memcpy(out + o, "'\\''", 4); o += 4; }
        else out[o++] = *in;
    }
    if (o < cap) out[o++] = '\'';
    out[o < cap ? o : cap - 1] = '\0';
}

/* Powiadomienie systemowe — Linux: notify-send, macOS: osascript,
 * Windows: PowerShell (balloon). Zwraca true, gdy polecenie wystartowało. */
int silver_notify(const char *title, const char *body) {
    char t[1024], b[2048], cmd[4600];
#if defined(__APPLE__)
    char script[3200];
    snprintf(script, sizeof(script), "display notification %s with title %s", "\"\"", "\"\"");
    (void)script;
    shq(title ? title : "", t, sizeof(t));
    shq(body ? body : "", b, sizeof(b));
    snprintf(cmd, sizeof(cmd),
        "osascript -e 'on run argv' -e 'display notification (item 2 of argv) with title (item 1 of argv)' -e 'end run' %s %s >/dev/null 2>&1",
        t, b);
#elif defined(_WIN32)
    /* podwajanie apostrofów w PowerShellu */
    char tt[1024], bb[2048]; size_t i, o;
    for (i = 0, o = 0; title && title[i] && o + 2 < sizeof(tt); i++) { if (title[i] == '\'') tt[o++] = '\''; tt[o++] = title[i]; } tt[o] = 0;
    for (i = 0, o = 0; body && body[i] && o + 2 < sizeof(bb); i++) { if (body[i] == '\'') bb[o++] = '\''; bb[o++] = body[i]; } bb[o] = 0;
    snprintf(cmd, sizeof(cmd),
        "powershell -NoProfile -Command \"Add-Type -AssemblyName System.Windows.Forms; $n=New-Object System.Windows.Forms.NotifyIcon; $n.Icon=[System.Drawing.SystemIcons]::Information; $n.Visible=$true; $n.ShowBalloonTip(5000,'%s','%s',[System.Windows.Forms.ToolTipIcon]::Info)\"",
        tt, bb);
    (void)t; (void)b;
#else
    shq(title ? title : "", t, sizeof(t));
    shq(body ? body : "", b, sizeof(b));
    snprintf(cmd, sizeof(cmd), "notify-send -- %s %s >/dev/null 2>&1", t, b);
#endif
    return system(cmd) == 0;
}

static int str_has_ci(const char *hay, const char *needle) {
    size_t hn = strlen(hay), nn = strlen(needle);
    if (nn > hn) return 0;
    for (size_t i = 0; i + nn <= hn; i++) {
        size_t k = 0;
        while (k < nn && (hay[i + k] | 0x20) == (needle[k] | 0x20)) k++;
        if (k == nn) return 1;
    }
    return 0;
}

static int run_capture(const char *cmd, char *out, size_t cap) {
    out[0] = '\0';
#if defined(_WIN32)
    FILE *p = _popen(cmd, "r");
#else
    FILE *p = popen(cmd, "r");
#endif
    if (!p) return 0;
    size_t n = fread(out, 1, cap - 1, p);
    out[n] = '\0';
#if defined(_WIN32)
    _pclose(p);
#else
    pclose(p);
#endif
    return 1;
}

/* "dark" | "light". Wynik jest cache'owany 2 s (nie forkuje co klatkę). */
const char *silver_system_theme(void) {
    static char buf[16] = "light";
    static Uint32 last = 0;
    Uint32 now = g_sdl_ready ? SDL_GetTicks() : 0;
    if (last != 0 && now - last < 2000) return buf;
    last = now ? now : 1;
    int dark = 0;
    char out[512];
#if defined(__APPLE__)
    if (run_capture("defaults read -g AppleInterfaceStyle 2>/dev/null", out, sizeof(out)))
        dark = str_has_ci(out, "dark");
#elif defined(_WIN32)
    if (run_capture("reg query HKCU\\Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize /v AppsUseLightTheme 2>nul", out, sizeof(out)))
        dark = str_has_ci(out, "0x0");
#else
    const char *gtk = getenv("GTK_THEME");
    if (gtk && str_has_ci(gtk, "dark")) dark = 1;
    if (!dark && run_capture("gsettings get org.gnome.desktop.interface color-scheme 2>/dev/null", out, sizeof(out)) && out[0])
        dark = str_has_ci(out, "dark");
    if (!dark && run_capture("gsettings get org.gnome.desktop.interface gtk-theme 2>/dev/null", out, sizeof(out)) && out[0])
        dark = str_has_ci(out, "dark");
    if (!dark && run_capture("kreadconfig5 --group General --key ColorScheme 2>/dev/null", out, sizeof(out)) && out[0])
        dark = str_has_ci(out, "dark");
#endif
    strcpy(buf, dark ? "dark" : "light");
    return buf;
}
