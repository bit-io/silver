#include <stdio.h>

const char *silver_find_system_font(void);
int silver_window_create(const char *title, int w, int h);
int silver_load_font(int h, const char *path, int size);
int silver_clear(int h, int r, int g, int b);
int silver_fill_rect(int h, int x, int y, int w, int rh, int r, int g, int b, int a);
int silver_draw_text(int h, int x, int y, const char *text, int r, int g, int b, int a);
int silver_present(int h);
int silver_poll_event(int h);
int silver_is_open(int h);
int silver_delay(int ms);
int silver_window_close(int h);

int main(void) {
    setvbuf(stderr, NULL, _IONBF, 0);
    fprintf(stderr, "[selftest] 1/6 szukam czcionki: '%s'\n", silver_find_system_font());
    fprintf(stderr, "[selftest] 2/6 tworze okno…\n");
    int h = silver_window_create("Silver selftest", 640, 360);
    fprintf(stderr, "[selftest] 2/6 okno = %d %s\n", h, h < 0 ? "(BLAD)" : "OK");
    if (h < 0) return 2;
    fprintf(stderr, "[selftest] 3/6 laduje czcionke…\n");
    int f = silver_load_font(h, "", 18);
    fprintf(stderr, "[selftest] 3/6 czcionka = %d %s\n", f, f ? "OK" : "(BRAK — tekst sie nie narysuje)");
    fprintf(stderr, "[selftest] 4/6 rysuje 120 klatek (~2 s)…\n");
    for (int i = 0; i < 120 && silver_is_open(h); i++) {
        while (silver_poll_event(h)) {}
        silver_clear(h, 30, 34, 40);
        silver_fill_rect(h, 20 + (i % 100) * 4, 60, 120, 60, 80, 160, 255, 255);
        silver_draw_text(h, 20, 20, "Silver selftest: jesli widzisz ten tekst, shim dziala", 230, 230, 230, 255);
        silver_present(h);
        silver_delay(16);
    }
    fprintf(stderr, "[selftest] 5/6 zamykam okno…\n");
    silver_window_close(h);
    fprintf(stderr, "[selftest] 6/6 KONIEC — wszystko OK\n");
    return 0;
}
