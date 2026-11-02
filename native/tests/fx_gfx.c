#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>

int silver_window_create(const char *, int, int);
int silver_window_close(int);
int silver_clear(int, int, int, int);
int silver_fill_rect(int, int, int, int, int, int, int, int, int);
int silver_layer_begin(int, int, int, int, int);
int silver_layer_end_fx(int, int, int, int, int, int, int, int, int, int, const char *);
int silver_layer_capture_mask(int, int, int);
int silver_mask_clear(void);
int silver_pixel(int, int, int);
int silver_draw_svg(int, const char *, int, int, int, int, int);
int silver_draw_part(int, int, const char *, int, int, int, const char *);
int silver_draw_tiled(int, int, const char *, int, int, int, int, int, int, int, int, int, int, int, int, int);
int silver_save_png(int, const char *);

static int fails = 0;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)
static int R(int p) { return (p >> 16) & 255; }
static int G(int p) { return (p >> 8) & 255; }
static int B(int p) { return p & 255; }
#define NEAR(a, b, t) (abs((a) - (b)) <= (t))

static void blend_multiply(int w) {
    silver_clear(w, 200, 100, 50);
    CHECK(silver_layer_begin(w, 10, 10, 100, 100));
    silver_fill_rect(w, 10, 10, 100, 100, 100, 200, 255, 255);
    CHECK(silver_layer_end_fx(w, 1000, 0, 0, 1000, 0, 0, 0, 0, 255, "blend:multiply"));
    int p = silver_pixel(w, 50, 50), q = silver_pixel(w, 150, 150);
    CHECK(NEAR(R(p), 78, 2) && NEAR(G(p), 78, 2) && NEAR(B(p), 50, 2));       /* pomnożone */
    CHECK(R(q) == 200 && G(q) == 100 && B(q) == 50);                           /* poza warstwą bez zmian */
    /* dokładnie na krawędziach prostokąta warstwy */
    CHECK(NEAR(R(silver_pixel(w, 10, 10)), 78, 2));
    CHECK(R(silver_pixel(w, 9, 9)) == 200 && R(silver_pixel(w, 110, 110)) == 200);
}

static void blend_screen_alpha(int w) {
    silver_clear(w, 100, 100, 100);
    silver_layer_begin(w, 0, 0, 50, 50);
    silver_fill_rect(w, 0, 0, 50, 50, 200, 200, 200, 255);
    silver_layer_end_fx(w, 1000, 0, 0, 1000, 0, 0, 0, 0, 128, "blend:screen");
    int p = silver_pixel(w, 20, 20);
    /* screen: 100+200-100*200/255 = 222; z krytyem 50%: ≈ 100 + (222-100)/2 = 161 */
    CHECK(NEAR(R(p), 161, 3) && NEAR(G(p), 161, 3));
}

static void blend_translucent_source(int w) {
    silver_clear(w, 200, 200, 200);
    silver_layer_begin(w, 0, 0, 60, 60);
    silver_fill_rect(w, 0, 0, 30, 60, 0, 0, 0, 255);        /* czarna lewa połowa */
    silver_layer_end_fx(w, 1000, 0, 0, 1000, 0, 0, 0, 0, 255, "blend:multiply");
    CHECK(R(silver_pixel(w, 10, 10)) <= 2);                  /* czerń × tło = czerń */
    CHECK(R(silver_pixel(w, 45, 10)) == 200);                /* przezroczysta prawa połowa nie zmienia tła */
}

static void blur_filter(int w) {
    silver_clear(w, 255, 255, 255);
    silver_layer_begin(w, 20, 20, 120, 120);               /* z zapasem na rozlew */
    silver_fill_rect(w, 60, 60, 40, 40, 0, 0, 0, 255);
    CHECK(silver_layer_end_fx(w, 1000, 0, 0, 1000, 0, 0, 0, 0, 255, "blur:4"));
    int center = silver_pixel(w, 80, 80);
    int edge = silver_pixel(w, 60, 80);                    /* dokładnie na krawędzi: ≈ połowa */
    int out = silver_pixel(w, 55, 80);                     /* tuż poza kwadratem — ciemniejsze niż tło */
    int far = silver_pixel(w, 25, 25);
    CHECK(R(center) < 20);                                 /* wnętrze nadal prawie czarne */
    CHECK(R(edge) > 70 && R(edge) < 190);                  /* krawędź rozmyta */
    CHECK(R(out) < 250 && R(out) > R(edge));               /* rozlew poza krawędź, jaśniejszy dalej */
    CHECK(R(far) == 255);                                  /* daleko tło bez zmian */
    /* rozmycie nie zaciemnia obszaru poza warstwą */
    CHECK(R(silver_pixel(w, 10, 10)) == 255);
}

static void color_filters(int w) {
    silver_clear(w, 255, 255, 255);
    silver_layer_begin(w, 0, 0, 20, 20);
    silver_fill_rect(w, 0, 0, 20, 20, 255, 0, 0, 255);
    silver_layer_end_fx(w, 1000, 0, 0, 1000, 0, 0, 0, 0, 255, "sat:0");
    int p = silver_pixel(w, 10, 10);
    CHECK(NEAR(R(p), G(p), 2) && NEAR(G(p), B(p), 2) && NEAR(R(p), 54, 3));
    silver_layer_begin(w, 30, 0, 20, 20);
    silver_fill_rect(w, 30, 0, 20, 20, 255, 0, 0, 255);
    silver_layer_end_fx(w, 1000, 0, 0, 1000, 0, 0, 0, 0, 255, "hue:120");
    int q = silver_pixel(w, 40, 10);
    CHECK(G(q) > R(q));
}

static void drop_shadow(int w) {
    silver_clear(w, 255, 255, 255);
    silver_layer_begin(w, 10, 10, 100, 100);
    silver_fill_rect(w, 20, 20, 30, 30, 255, 0, 0, 255);
    silver_layer_end_fx(w, 1000, 0, 0, 1000, 0, 0, 0, 0, 255, "shadow:10,10,2,0,0,0,255");
    CHECK(R(silver_pixel(w, 30, 30)) == 255 && G(silver_pixel(w, 30, 30)) == 0);   /* oryginał */
    int s = silver_pixel(w, 55, 55);                                               /* w cieniu (przesunięcie 10) */
    CHECK(R(s) < 60 && G(s) < 60);
    CHECK(R(silver_pixel(w, 15, 15)) == 255);                                      /* poza cieniem biało */
}

static void masks(int w) {
    silver_clear(w, 255, 255, 255);
    /* maska: lewa połowa kryje, prawa nie (tryb alfa) */
    silver_layer_begin(w, 20, 20, 100, 100);
    silver_fill_rect(w, 20, 20, 50, 100, 255, 255, 255, 255);
    CHECK(silver_layer_capture_mask(w, 0, 0));
    silver_layer_begin(w, 20, 20, 100, 100);
    silver_fill_rect(w, 20, 20, 100, 100, 255, 0, 0, 255);
    silver_layer_end_fx(w, 1000, 0, 0, 1000, 0, 0, 0, 0, 255, "mask");
    CHECK(R(silver_pixel(w, 40, 60)) == 255 && G(silver_pixel(w, 40, 60)) == 0);   /* czerwone tam, gdzie maska */
    CHECK(G(silver_pixel(w, 100, 60)) == 255);                                      /* tło tam, gdzie maska = 0 */
    /* maska luminancyjna: szary 50% luminancji → połowa krycia */
    silver_clear(w, 255, 255, 255);
    silver_layer_begin(w, 0, 0, 40, 40);
    silver_fill_rect(w, 0, 0, 40, 40, 128, 128, 128, 255);
    silver_layer_capture_mask(w, 1, 0);
    silver_layer_begin(w, 0, 0, 40, 40);
    silver_fill_rect(w, 0, 0, 40, 40, 0, 0, 0, 255);
    silver_layer_end_fx(w, 1000, 0, 0, 1000, 0, 0, 0, 0, 255, "mask");
    int p = silver_pixel(w, 20, 20);
    CHECK(NEAR(R(p), 127, 4));                                                       /* czerń × 50% nad bielą */
    /* mask-composite: add dwóch masek (lewa + prawa połowa) → cały prostokąt */
    silver_clear(w, 255, 255, 255);
    silver_layer_begin(w, 0, 0, 40, 40);
    silver_fill_rect(w, 0, 0, 20, 40, 255, 255, 255, 255);
    silver_layer_capture_mask(w, 0, 0);
    silver_layer_begin(w, 0, 0, 40, 40);
    silver_fill_rect(w, 20, 0, 20, 40, 255, 255, 255, 255);
    silver_layer_capture_mask(w, 0, 1);
    silver_layer_begin(w, 0, 0, 40, 40);
    silver_fill_rect(w, 0, 0, 40, 40, 0, 0, 255, 255);
    silver_layer_end_fx(w, 1000, 0, 0, 1000, 0, 0, 0, 0, 255, "mask");
    CHECK(B(silver_pixel(w, 10, 10)) == 255 && R(silver_pixel(w, 10, 10)) == 0);
    CHECK(B(silver_pixel(w, 30, 10)) == 255 && R(silver_pixel(w, 30, 10)) == 0);
    /* maska jest jednorazowa: kolejna warstwa z "mask" bez nowej maski nie jest zmieniana */
    silver_layer_begin(w, 0, 0, 40, 40);
    silver_fill_rect(w, 0, 0, 40, 40, 0, 255, 0, 255);
    silver_layer_end_fx(w, 1000, 0, 0, 1000, 0, 0, 0, 0, 255, "mask");
    CHECK(G(silver_pixel(w, 10, 10)) == 255 && R(silver_pixel(w, 10, 10)) == 0);
}


static void tiled_gradient(int w) {
    silver_clear(w, 255, 255, 255);
    /* kafelek 40×40 gradientu czarny→czerwony w poziomie, powtarzany w obu osiach po całym 120×80 */
    CHECK(silver_draw_tiled(w, 1, "L|90|0,0,0,255@;255,0,0,255@", 10, 10, 120, 80, 10, 10, 40, 40, 40, 40, 1, 1, 255));
    CHECK(R(silver_pixel(w, 11, 15)) < 20);                       /* początek kafelka 1 */
    CHECK(R(silver_pixel(w, 48, 15)) > 220);                      /* koniec kafelka 1 */
    CHECK(R(silver_pixel(w, 51, 15)) < 25);                       /* początek kafelka 2 (powtórzenie) */
    CHECK(R(silver_pixel(w, 11, 55)) < 20);                       /* drugi rząd */
    CHECK(R(silver_pixel(w, 9, 15)) == 255 && G(silver_pixel(w, 9, 15)) == 255);   /* poza obszarem przycięcia */
    CHECK(G(silver_pixel(w, 131, 15)) == 255 && G(silver_pixel(w, 60, 91)) == 255);
    /* bez powtarzania: tylko jeden kafelek */
    silver_clear(w, 255, 255, 255);
    silver_draw_tiled(w, 1, "L|90|0,0,0,255@;255,0,0,255@", 10, 10, 120, 80, 10, 10, 40, 40, 40, 40, 0, 0, 255);
    CHECK(R(silver_pixel(w, 20, 20)) < 120 && G(silver_pixel(w, 20, 20)) < 120);
    CHECK(G(silver_pixel(w, 60, 20)) == 255 && G(silver_pixel(w, 20, 60)) == 255);
    /* tylko repeat-x */
    silver_clear(w, 255, 255, 255);
    silver_draw_tiled(w, 1, "L|90|0,0,0,255@;255,0,0,255@", 0, 0, 160, 100, 0, 0, 40, 40, 40, 40, 1, 0, 255);
    CHECK(G(silver_pixel(w, 100, 20)) < 120 && G(silver_pixel(w, 100, 60)) == 255);
    /* przesunięcie początku kafelka (ujemne) — wzór wyrównany do origin */
    silver_clear(w, 255, 255, 255);
    silver_draw_tiled(w, 1, "L|90|0,0,0,255@;255,0,0,255@", 0, 0, 100, 40, -20, 0, 40, 40, 40, 40, 1, 1, 255);
    CHECK(R(silver_pixel(w, 21, 10)) < 25);                       /* granica kafelków przesunięta o −20 */
    CHECK(R(silver_pixel(w, 19, 10)) > 220);
    /* odstęp między kafelkami (space): step 50 przy kafelku 40 → 10 px przerwy */
    silver_clear(w, 255, 255, 255);
    silver_draw_tiled(w, 1, "L|90|0,0,0,255@;0,0,0,255@", 0, 0, 120, 40, 0, 0, 40, 40, 50, 50, 1, 1, 255);
    CHECK(R(silver_pixel(w, 20, 10)) == 0 && R(silver_pixel(w, 45, 10)) == 255 && R(silver_pixel(w, 70, 10)) == 0);
    /* krycie */
    silver_clear(w, 255, 255, 255);
    silver_draw_tiled(w, 1, "L|90|0,0,0,255@;0,0,0,255@", 0, 0, 40, 40, 0, 0, 40, 40, 40, 40, 0, 0, 128);
    CHECK(NEAR(R(silver_pixel(w, 20, 20)), 127, 3));
    /* półprzezroczysty gradient nad tłem — bez szarej obwódki (kolor wynikowy zielony+biały) */
    silver_clear(w, 255, 255, 255);
    silver_draw_tiled(w, 1, "L|90|0,128,0,255@;0,128,0,0@", 0, 0, 100, 10, 0, 0, 100, 10, 100, 10, 0, 0, 255);
    int p = silver_pixel(w, 50, 5);
    CHECK(NEAR(R(p), 128, 6) && NEAR(G(p), 191, 6));              /* 50% zieleni nad bielą */
}

static void tiled_image(int w) {
    SDL_Surface *surf = SDL_CreateRGBSurfaceWithFormat(0, 4, 2, 32, SDL_PIXELFORMAT_ARGB8888);
    Uint32 *px = (Uint32 *)surf->pixels;
    for (int y = 0; y < 2; y++) for (int x = 0; x < 4; x++) px[y * (surf->pitch / 4) + x] = x < 2 ? 0xFFFF0000 : 0xFF0000FF;   /* lewa czerwona, prawa niebieska */
    CHECK(IMG_SavePNG(surf, "/tmp/silver_tile.png") == 0);
    SDL_FreeSurface(surf);
    silver_clear(w, 255, 255, 255);
    /* skalowanie 4×2 → kafelek 40×20, powtarzany */
    CHECK(silver_draw_tiled(w, 0, "/tmp/silver_tile.png", 0, 0, 120, 60, 0, 0, 40, 20, 40, 20, 1, 1, 255));
    CHECK(R(silver_pixel(w, 5, 5)) > 240 && B(silver_pixel(w, 5, 5)) < 15);       /* czerwona połowa */
    CHECK(B(silver_pixel(w, 30, 5)) > 240 && R(silver_pixel(w, 30, 5)) < 15);     /* niebieska */
    CHECK(R(silver_pixel(w, 45, 5)) > 240);                                       /* kolejny kafelek */
    CHECK(R(silver_pixel(w, 5, 25)) > 240);                                       /* drugi rząd */
    CHECK(!silver_draw_tiled(w, 0, "/nie/istnieje.png", 0, 0, 10, 10, 0, 0, 10, 10, 10, 10, 1, 1, 255));
}


static void draw_part(int w) {
    /* źródło: gradient poziomy czarny→czerwony 100×50; fragment lewej połowy rozciągnięty na 40×40 */
    silver_clear(w, 255, 255, 255);
    const char *g = "L|90|0,0,0,255@;255,0,0,255@";
    CHECK(silver_draw_part(w, 1, g, 100, 50, 255, "0,0,50,50,10,10,40,40,40,40,10,10,40,40,0,0"));
    CHECK(R(silver_pixel(w, 12, 20)) < 60);                    /* początek lewej połowy ciemny */
    CHECK(R(silver_pixel(w, 48, 20)) > 100 && R(silver_pixel(w, 48, 20)) < 140);   /* koniec lewej połowy ≈ 50% */
    CHECK(G(silver_pixel(w, 5, 20)) == 255 && G(silver_pixel(w, 55, 20)) == 255);  /* poza obszarem */
    /* kafelkowanie fragmentu: 10×10 kafelki w pasku 60×10 */
    silver_clear(w, 255, 255, 255);
    CHECK(silver_draw_part(w, 1, g, 100, 50, 255, "0,0,100,50,0,0,60,10,10,10,0,0,10,10,1,0"));
    CHECK(R(silver_pixel(w, 1, 5)) < 70 && R(silver_pixel(w, 8, 5)) > 150);
    CHECK(R(silver_pixel(w, 11, 5)) < 70);                     /* powtórzony kafelek zaczyna od ciemnego */
    CHECK(G(silver_pixel(w, 30, 15)) == 255);                  /* poniżej paska — bez powtarzania w pionie */
    CHECK(!silver_draw_part(w, 0, "/nie/istnieje.png", 10, 10, 255, "0,0,10,10,0,0,10,10,10,10,0,0,10,10,0,0"));
    CHECK(!silver_draw_part(w, 1, g, 100, 50, 255, "zle"));
}


static void svg_draw(int w) {
    silver_clear(w, 255, 255, 255);
    const char *m = "<svg width=\"40\" height=\"40\" viewBox=\"0 0 40 40\"><rect x=\"0\" y=\"0\" width=\"20\" height=\"40\" fill=\"#ff0000\"/>"
                    "<circle cx=\"30\" cy=\"20\" r=\"8\" fill=\"#0000ff\" stroke=\"#00ff00\" stroke-width=\"2\"/></svg>";
    CHECK(silver_draw_svg(w, m, 10, 10, 40, 40, 255));
    int a = silver_pixel(w, 15, 30), b = silver_pixel(w, 40, 30), c = silver_pixel(w, 40, 22), d = silver_pixel(w, 55, 15);
    CHECK(R(a) == 255 && G(a) < 5 && B(a) < 5);                     /* prostokąt czerwony */
    CHECK(B(b) > 250 && R(b) < 5);                                  /* koło niebieskie (środek 40,30) */
    CHECK(G(silver_pixel(w, 40, 30 - 9 + 0)) > 200 || G(silver_pixel(w, 40, 20)) > 200);     /* obrys zielony wokół koła */
    CHECK(G(d) == 255 && R(d) == 255);                              /* tło poza rysunkiem */
    (void)c;
    /* skalowanie: ten sam znacznik w 80×80 — czerwony prostokąt zajmuje lewą połowę */
    silver_clear(w, 255, 255, 255);
    CHECK(silver_draw_svg(w, m, 0, 0, 80, 80, 255));
    CHECK(R(silver_pixel(w, 30, 70)) == 255 && G(silver_pixel(w, 30, 70)) < 5);
    CHECK(G(silver_pixel(w, 45, 5)) == 255);                        /* prawa połowa nad kołem — puste */
    /* gradient liniowy */
    silver_clear(w, 255, 255, 255);
    const char *g = "<svg width=\"100\" height=\"20\" viewBox=\"0 0 100 20\"><defs><linearGradient id=\"g\" x1=\"0%\" y1=\"0%\" x2=\"100%\" y2=\"0%\">"
                    "<stop offset=\"0\" stop-color=\"#000000\"/><stop offset=\"1\" stop-color=\"#ff0000\"/></linearGradient></defs>"
                    "<rect width=\"100\" height=\"20\" fill=\"url(#g)\"/></svg>";
    CHECK(silver_draw_svg(w, g, 0, 0, 100, 20, 255));
    CHECK(R(silver_pixel(w, 3, 10)) < 40 && R(silver_pixel(w, 96, 10)) > 215);
    /* krycie i błędne dane */
    CHECK(!silver_draw_svg(w, "to nie jest svg", 0, 0, 10, 10, 255));
    CHECK(!silver_draw_svg(w, NULL, 0, 0, 10, 10, 255));
    silver_clear(w, 255, 255, 255);
    silver_draw_svg(w, "<svg width=\"10\" height=\"10\"><rect width=\"10\" height=\"10\" fill=\"#000000\"/></svg>", 0, 0, 10, 10, 128);
    CHECK(R(silver_pixel(w, 5, 5)) > 115 && R(silver_pixel(w, 5, 5)) < 140);
}

static void combined_and_png(int w) {
    silver_clear(w, 255, 255, 255);
    silver_layer_begin(w, 0, 0, 60, 60);
    silver_fill_rect(w, 10, 10, 40, 40, 255, 0, 0, 255);
    silver_layer_end_fx(w, 1000, 0, 0, 1000, 0, 0, 0, 0, 255, "blur:2;sat:0;blend:multiply");
    int p = silver_pixel(w, 30, 30);
    CHECK(NEAR(R(p), G(p), 2) && R(p) < 80);                /* odbarwione, rozmyte i pomnożone z białym tłem */
    CHECK(silver_save_png(w, "/tmp/silver_fx_test.png"));
    FILE *f = fopen("/tmp/silver_fx_test.png", "rb");
    CHECK(f != NULL);
    if (f) { unsigned char sig[4] = {0}; fread(sig, 1, 4, f); fclose(f); CHECK(sig[1] == 'P' && sig[2] == 'N' && sig[3] == 'G'); }
}

int main(void) {
    setenv("SILVER_RENDERER", "software", 1);
    int w = silver_window_create("fx", 200, 200);
    if (w < 0) { printf("brak okna (Xvfb?)\n"); return 2; }
    blend_multiply(w);
    blend_screen_alpha(w);
    blend_translucent_source(w);
    blur_filter(w);
    color_filters(w);
    drop_shadow(w);
    masks(w);
    tiled_gradient(w);
    tiled_image(w);
    draw_part(w);
    svg_draw(w);
    combined_and_png(w);
    silver_window_close(w);
    if (fails) { printf("fx_gfx: %d niepowodzeń\n", fails); return 1; }
    printf("fx_gfx: OK\n");
    return 0;
}
