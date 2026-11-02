#include <stdio.h>
#include "../silver_fx.h"

static int fails = 0;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)
#define NEAR(a, b, t) (abs((int)(a) - (int)(b)) <= (t))

static uint32_t opaque(int r, int g, int b) { return FX_PACK(255, r, g, b); }

static void test_blend_modes(void) {
    uint32_t d, s = opaque(100, 200, 255);
    d = opaque(200, 100, 50); fx_blend(&d, &s, 1, FX_MULTIPLY, 255);
    CHECK(NEAR(FX_R(d), 78, 1) && NEAR(FX_G(d), 78, 1) && NEAR(FX_B(d), 50, 1) && FX_A(d) == 255);
    d = opaque(200, 100, 50); fx_blend(&d, &s, 1, FX_SCREEN, 255);
    CHECK(NEAR(FX_R(d), 222, 1) && NEAR(FX_G(d), 222, 1) && NEAR(FX_B(d), 255, 1));
    d = opaque(200, 100, 50); fx_blend(&d, &s, 1, FX_DIFFERENCE, 255);
    CHECK(NEAR(FX_R(d), 100, 1) && NEAR(FX_G(d), 100, 1) && NEAR(FX_B(d), 205, 1));
    d = opaque(200, 100, 50); fx_blend(&d, &s, 1, FX_DARKEN, 255);
    CHECK(NEAR(FX_R(d), 100, 1) && NEAR(FX_G(d), 100, 1) && NEAR(FX_B(d), 50, 1));
    d = opaque(200, 100, 50); fx_blend(&d, &s, 1, FX_LIGHTEN, 255);
    CHECK(NEAR(FX_R(d), 200, 1) && NEAR(FX_G(d), 200, 1) && NEAR(FX_B(d), 255, 1));
    d = opaque(200, 100, 50); fx_blend(&d, &s, 1, FX_NORMAL, 255);
    CHECK(NEAR(FX_R(d), 100, 1) && NEAR(FX_G(d), 200, 1) && NEAR(FX_B(d), 255, 1));
    /* połowa krycia źródła: wynik w połowie drogi między tłem a multiply */
    d = opaque(200, 100, 50); fx_blend(&d, &s, 1, FX_MULTIPLY, 128);
    CHECK(NEAR(FX_R(d), 139, 2) && NEAR(FX_G(d), 89, 2));
    /* kolor czarny multiply daje czerń, biały — bez zmian */
    uint32_t blk = opaque(0, 0, 0), wht = opaque(255, 255, 255);
    d = opaque(10, 20, 30); fx_blend(&d, &blk, 1, FX_MULTIPLY, 255);
    CHECK(FX_R(d) == 0 && FX_G(d) == 0 && FX_B(d) == 0);
    d = opaque(10, 20, 30); fx_blend(&d, &wht, 1, FX_MULTIPLY, 255);
    CHECK(FX_R(d) == 10 && FX_G(d) == 20 && FX_B(d) == 30);
    /* przezroczyste źródło nie zmienia tła */
    uint32_t clear = 0;
    d = opaque(10, 20, 30); fx_blend(&d, &clear, 1, FX_SCREEN, 255);
    CHECK(d == opaque(10, 20, 30));
    CHECK(fx_blend_mode_from_name("multiply") == FX_MULTIPLY);
    CHECK(fx_blend_mode_from_name("luminosity") == FX_LUMINOSITY);
    CHECK(fx_blend_mode_from_name("nieznany") == FX_NORMAL);
}

static void test_nonseparable(void) {
    uint32_t d, s;
    /* luminosity: kolor tła, jasność źródła — szare źródło o jasności 0.5 na czerwonym tle */
    d = opaque(255, 0, 0); s = opaque(128, 128, 128); fx_blend(&d, &s, 1, FX_LUMINOSITY, 255);
    int lum = (int)(0.3f * FX_R(d) + 0.59f * FX_G(d) + 0.11f * FX_B(d));
    CHECK(NEAR(lum, 128, 3));
    /* color: odcień/nasycenie ze źródła, jasność z tła */
    d = opaque(128, 128, 128); s = opaque(255, 0, 0); fx_blend(&d, &s, 1, FX_COLOR, 255);
    CHECK(FX_R(d) > FX_G(d) && FX_R(d) > FX_B(d));
    /* saturation z szarego źródła daje szarość */
    d = opaque(255, 0, 0); s = opaque(100, 100, 100); fx_blend(&d, &s, 1, FX_SATURATION, 255);
    CHECK(NEAR(FX_R(d), FX_G(d), 2) && NEAR(FX_G(d), FX_B(d), 2));
}

static void test_blur(void) {
    enum { W = 61, H = 61 };
    static uint32_t px[W * H];
    memset(px, 0, sizeof(px));
    for (int y = 26; y < 35; y++) for (int x = 26; x < 35; x++) px[y * W + x] = opaque(255, 255, 255);
    long before = 0, after = 0;
    for (int i = 0; i < W * H; i++) before += FX_A(px[i]);
    CHECK(fx_gauss_blur(px, W, H, 3.0f));
    for (int i = 0; i < W * H; i++) after += FX_A(px[i]);
    /* rozmycie zachowuje (z grubsza) sumę krycia, a krawędzie się rozlewają */
    CHECK(after > before * 95 / 100 && after < before * 105 / 100);
    CHECK(FX_A(px[30 * W + 30]) > 170 && FX_A(px[30 * W + 30]) < 215);   /* środek nadal prawie pełny */
    CHECK(FX_A(px[30 * W + 24]) > 0 && FX_A(px[30 * W + 24]) < 120);     /* rozlew poza krawędź */
    CHECK(FX_A(px[30 * W + 26]) > FX_A(px[30 * W + 24]));                /* spadek od środka */
    CHECK(FX_A(px[30 * W + 5]) == 0);                                    /* daleko pusto */
    /* symetria */
    CHECK(NEAR(FX_A(px[30 * W + 24]), FX_A(px[30 * W + 36]), 1));
    CHECK(NEAR(FX_A(px[24 * W + 30]), FX_A(px[36 * W + 30]), 1));
    /* sigma ~0 nic nie zmienia */
    static uint32_t one[9];
    memset(one, 0, sizeof(one)); one[4] = opaque(10, 20, 30);
    fx_gauss_blur(one, 3, 3, 0.0f);
    CHECK(one[4] == opaque(10, 20, 30) && one[0] == 0);
}

static void test_color_matrices(void) {
    uint32_t p = opaque(255, 0, 0);
    fx_saturate(&p, 1, 0.0f);   /* pełne odbarwienie */
    CHECK(NEAR(FX_R(p), FX_G(p), 2) && NEAR(FX_G(p), FX_B(p), 2) && NEAR(FX_R(p), 54, 3));
    p = opaque(255, 0, 0); fx_saturate(&p, 1, 1.0f);
    CHECK(NEAR(FX_R(p), 255, 1) && FX_G(p) <= 1 && FX_B(p) <= 1);
    p = opaque(255, 0, 0); fx_hue_rotate(&p, 1, 120.0f);   /* czerwony → zielonkawy */
    CHECK(FX_G(p) > FX_R(p));
    p = opaque(255, 0, 0); fx_hue_rotate(&p, 1, 360.0f);
    CHECK(NEAR(FX_R(p), 255, 2) && FX_G(p) <= 2);
    uint32_t half = FX_PACK(128, 128, 0, 0);               /* półprzezroczysty czerwony */
    fx_saturate(&half, 1, 0.0f);
    CHECK(FX_A(half) == 128);                              /* alfa nietknięta */
}

static void test_masks(void) {
    uint32_t px[4] = { opaque(200, 100, 50), opaque(200, 100, 50), opaque(200, 100, 50), opaque(200, 100, 50) };
    uint8_t m[4] = { 255, 128, 0, 64 };
    fx_apply_mask(px, m, 4);
    CHECK(px[0] == opaque(200, 100, 50));
    CHECK(NEAR(FX_A(px[1]), 128, 1) && NEAR(FX_R(px[1]), 100, 1));
    CHECK(px[2] == 0);
    CHECK(NEAR(FX_A(px[3]), 64, 1));
    uint32_t src[3] = { FX_PACK(255, 255, 255, 255), FX_PACK(255, 0, 0, 0), FX_PACK(128, 128, 128, 128) };
    uint8_t out[3];
    fx_mask_from_pixels(src, out, 3, 0);
    CHECK(out[0] == 255 && out[1] == 255 && out[2] == 128);      /* alfa */
    fx_mask_from_pixels(src, out, 3, 1);
    CHECK(out[0] == 255 && out[1] == 0 && NEAR(out[2], 128, 1)); /* luminancja */
    uint8_t acc[4] = { 255, 255, 0, 128 }, nxt[4] = { 255, 0, 255, 128 };
    uint8_t a1[4]; memcpy(a1, acc, 4); fx_mask_combine(a1, nxt, 4, 1);
    CHECK(a1[0] == 255 && a1[1] == 255 && a1[2] == 255 && NEAR(a1[3], 192, 1));  /* add */
    memcpy(a1, acc, 4); fx_mask_combine(a1, nxt, 4, 2);
    CHECK(a1[0] == 0 && a1[1] == 0 && a1[2] == 255 && NEAR(a1[3], 64, 1));        /* subtract: source poza destination */
    memcpy(a1, acc, 4); fx_mask_combine(a1, nxt, 4, 3);
    CHECK(a1[0] == 255 && a1[1] == 0 && a1[2] == 0 && NEAR(a1[3], 64, 1));        /* intersect */
    memcpy(a1, acc, 4); fx_mask_combine(a1, nxt, 4, 4);
    CHECK(a1[0] == 0 && a1[1] == 255 && a1[2] == 255);                            /* exclude */
    memcpy(a1, acc, 4); fx_mask_combine(a1, nxt, 4, 0);
    CHECK(memcmp(a1, nxt, 4) == 0);                                               /* replace */
}

static void test_drop_shadow(void) {
    enum { W = 40, H = 40 };
    static uint32_t px[W * H];
    memset(px, 0, sizeof(px));
    for (int y = 10; y < 20; y++) for (int x = 10; x < 20; x++) px[y * W + x] = opaque(255, 0, 0);
    CHECK(fx_drop_shadow(px, W, H, 6, 6, 1.0f, 0, 0, 0, 255));
    CHECK(px[15 * W + 15] == opaque(255, 0, 0));                 /* oryginał na wierzchu */
    CHECK(FX_A(px[23 * W + 23]) > 200 && FX_R(px[23 * W + 23]) < 30);   /* cień czarny w przesunięciu */
    CHECK(FX_A(px[5 * W + 5]) == 0);                              /* poza cieniem pusto */
}


static int gr(const char *spec, int w, int h, uint32_t *out) { return fx_gradient_render(out, w, h, spec); }

static void test_gradients(void) {
    static uint32_t g[100 * 100];
    /* liniowy 90° (w prawo): czerwony → niebieski */
    CHECK(gr("L|90|255,0,0,255@;0,0,255,255@", 100, 10, g));
    CHECK(FX_R(g[5 * 100 + 1]) > 240 && FX_B(g[5 * 100 + 1]) < 15);
    CHECK(NEAR(FX_R(g[5 * 100 + 50]), 128, 6) && NEAR(FX_B(g[5 * 100 + 50]), 128, 6));
    CHECK(FX_B(g[5 * 100 + 98]) > 240 && FX_R(g[5 * 100 + 98]) < 15);
    /* 180° (w dół) — zmienia się tylko w pionie */
    CHECK(gr("L|180|255,255,255,255@;0,0,0,255@", 10, 100, g));
    CHECK(FX_R(g[2 * 10 + 3]) > 240 && FX_R(g[97 * 10 + 3]) < 15 && FX_R(g[50 * 10 + 0]) == FX_R(g[50 * 10 + 9]));
    /* 0° (w górę) odwrotnie */
    CHECK(gr("L|0|255,255,255,255@;0,0,0,255@", 10, 100, g));
    CHECK(FX_R(g[2 * 10 + 3]) < 15 && FX_R(g[97 * 10 + 3]) > 240);
    /* wiele przystanków z pozycjami: czerwony 0, zielony 25%, niebieski 100% */
    CHECK(gr("L|90|255,0,0,255@0%;0,255,0,255@25%;0,0,255,255@100%", 100, 4, g));
    CHECK(FX_G(g[2 * 100 + 25]) > 240 && FX_R(g[2 * 100 + 25]) < 15);
    CHECK(NEAR(FX_G(g[2 * 100 + 62]), 128, 8) && NEAR(FX_B(g[2 * 100 + 62]), 128, 8));
    /* twarde przystanki (te same pozycje) — ostra krawędź */
    CHECK(gr("L|90|255,0,0,255@50%;0,0,255,255@50%", 100, 4, g));
    CHECK(FX_R(g[2 * 100 + 48]) > 240 && FX_B(g[2 * 100 + 52]) > 240);
    /* pozycje w px */
    CHECK(gr("L|90|255,0,0,255@20px;0,0,255,255@80px", 100, 4, g));
    CHECK(FX_R(g[2 * 100 + 10]) > 240 && FX_B(g[2 * 100 + 90]) > 240 && NEAR(FX_R(g[2 * 100 + 50]), 128, 6));
    /* podpowiedź (hint): środek przejścia przesunięty na 25% — w 25% kolor pośredni 50/50 */
    CHECK(gr("L|90|0,0,0,255@;h@25%;255,255,255,255@", 100, 4, g));
    CHECK(NEAR(FX_R(g[2 * 100 + 25]), 128, 10));
    /* "to top right" w kwadracie: lewy-dolny róg = start, prawy-górny = koniec */
    CHECK(gr("L|to:tr|255,255,255,255@;0,0,0,255@", 50, 50, g));
    CHECK(FX_R(g[48 * 50 + 1]) > 240 && FX_R(g[1 * 50 + 48]) < 15 && NEAR(FX_R(g[25 * 50 + 25]), 128, 8));
    /* repeating-linear: okres 20px, wzór powtarza się */
    CHECK(gr("RL|90|255,0,0,255@0px;0,0,255,255@20px", 100, 4, g));
    CHECK(NEAR(FX_R(g[2 * 100 + 5]), FX_R(g[2 * 100 + 25]), 6) && NEAR(FX_R(g[2 * 100 + 5]), FX_R(g[2 * 100 + 85]), 6));
    /* półprzezroczysty koniec: interpolacja w przestrzeni przemnożonej — bez szarej obwódki */
    CHECK(gr("L|90|255,0,0,255@;255,0,0,0@", 100, 4, g));
    CHECK(FX_A(g[2 * 100 + 50]) > 100 && FX_A(g[2 * 100 + 50]) < 156);
    CHECK(FX_G(g[2 * 100 + 50]) == 0 && FX_B(g[2 * 100 + 50]) == 0 && FX_R(g[2 * 100 + 50]) == FX_A(g[2 * 100 + 50]));
    /* radialny: koło farthest-corner w środku — środek biały, róg czarny */
    CHECK(gr("R|circle;farthest-corner;50%,50%|255,255,255,255@;0,0,0,255@", 100, 100, g));
    CHECK(FX_R(g[50 * 100 + 50]) > 240 && FX_R(g[1 * 100 + 1]) < 15);
    CHECK(NEAR(FX_R(g[50 * 100 + 50 + 20]), FX_R(g[50 * 100 + 50 - 21]), 3) && NEAR(FX_R(g[50 * 100 + 50 + 20]), FX_R(g[70 * 100 + 50]), 3));
    /* closest-side: na krawędzi środkowej boku już „koniec” gradientu */
    CHECK(gr("R|circle;closest-side;50%,50%|255,255,255,255@;0,0,0,255@", 100, 100, g));
    CHECK(FX_R(g[50 * 100 + 98]) < 15 && FX_R(g[1 * 100 + 1]) < 15);
    /* elipsa w prostokącie 200×100: poziomo i pionowo ta sama „odległość” w jednostkach elipsy */
    static uint32_t ge[200 * 100];
    CHECK(gr("R|ellipse;farthest-side;50%,50%|255,255,255,255@;0,0,0,255@", 200, 100, ge));
    CHECK(NEAR(FX_R(ge[50 * 200 + 100 + 50]), FX_R(ge[(50 + 25) * 200 + 100]), 6));
    /* radialny z wyraźnym promieniem 30px */
    CHECK(gr("R|circle;30px;50%,50%|255,0,0,255@;0,0,255,255@", 100, 100, g));
    CHECK(FX_B(g[50 * 100 + 90]) > 240 && FX_R(g[50 * 100 + 50]) > 240);
    /* środek przesunięty: at 0 0 */
    CHECK(gr("R|circle;farthest-corner;0px,0px|255,255,255,255@;0,0,0,255@", 100, 100, g));
    CHECK(FX_R(g[0]) > 240 && FX_R(g[99 * 100 + 99]) < 20);
    /* conic od góry zgodnie z zegarem: prawa strona (90°) = 25% → w połowie między czerwonym a niebieskim dla 0..360 */
    CHECK(gr("C|0;50%,50%|255,0,0,255@0deg;0,0,255,255@360deg", 100, 100, g));
    CHECK(NEAR(FX_R(g[50 * 100 + 95]), 191, 8) && NEAR(FX_B(g[50 * 100 + 95]), 64, 8));    /* 90° → 25% */
    CHECK(NEAR(FX_R(g[95 * 100 + 50]), 128, 8));                                           /* dół = 180° */
    CHECK(NEAR(FX_B(g[50 * 100 + 5]), 191, 8));                                            /* lewo = 270° */
    /* conic from 90deg: start przesunięty — prawa strona to teraz początek (czerwony) */
    CHECK(gr("C|90;50%,50%|255,0,0,255@0deg;0,0,255,255@360deg", 100, 100, g));
    CHECK(FX_R(g[50 * 100 + 95]) > 240 && FX_B(g[50 * 100 + 95]) < 15);
    /* błędne wejście */
    CHECK(!gr("zly spec", 10, 10, g));
    CHECK(!gr(NULL, 10, 10, g));
}

int main(void) {
    test_blend_modes();
    test_nonseparable();
    test_blur();
    test_color_matrices();
    test_masks();
    test_drop_shadow();
    test_gradients();
    if (fails) { printf("fx_unit: %d niepowodzeń\n", fails); return 1; }
    printf("fx_unit: OK\n");
    return 0;
}
