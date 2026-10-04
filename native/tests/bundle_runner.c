#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
int js_init(int); int js_load_file(int, const char *); int js_eval(int, const char *, const char *);
int js_emit(int, const char *, const char *); int js_tick(int); const char *js_poll_invoke(int);
int js_resolve(int, const char *, const char *); const char *js_last_error(int); int js_shutdown(int);

static char SNAP[512];

static char *slurp(const char *p) {
    FILE *f = fopen(p, "rb"); if (!f) return NULL;
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    char *b = malloc(n + 1); if (fread(b, 1, n, f) != (size_t)n) { fclose(f); return NULL; } b[n] = 0; fclose(f); return b;
}
static void drain(int j) {
    const char *inv;
    while ((inv = js_poll_invoke(j))[0]) {
        char t[200000]; snprintf(t, sizeof t, "%s", inv);
        char *c1 = strchr(t, 1); if (c1) *c1 = 0;
        js_resolve(j, t, "{\"notes\":[],\"todos\":[],\"ok\":true}");
    }
}
int main(int argc, char **argv) {
    if (argc < 3) { fprintf(stderr, "użycie: bundle_runner bundle.js check.js\n"); return 2; }
    const char *cid = argc > 3 ? argv[3] : "app";
    snprintf(SNAP, sizeof SNAP,
      "[{\"i\":0,\"p\":-1,\"t\":\"root\",\"k\":[1],\"a\":{}},"
      "{\"i\":1,\"p\":0,\"t\":\"html\",\"k\":[2],\"a\":{}},"
      "{\"i\":2,\"p\":1,\"t\":\"body\",\"k\":[3],\"a\":{}},"
      "{\"i\":3,\"p\":2,\"t\":\"div\",\"k\":[],\"a\":{\"id\":\"%s\"}}]", cid);
    int j = js_init(0);
    if (j < 0) return 1;
    js_emit(j, "__dom_snapshot", SNAP);
    if (!js_load_file(j, argv[1])) { printf("BUNDLE: błąd ładowania: %s\n", js_last_error(j)); return 1; }
    for (int i = 0; i < 5; i++) { js_tick(j); drain(j); usleep(2000); }
    char *chk = slurp(argv[2]);
    if (!chk || !js_eval(j, chk, argv[2])) { printf("BUNDLE: błąd kontroli: %s\n", js_last_error(j)); return 1; }
    for (int i = 0; i < 5; i++) { js_tick(j); drain(j); usleep(2000); }
    js_shutdown(j);
    printf("BUNDLE: OK (%s)\n", argv[1]);
    return 0;
}
