#ifndef SILVER_VFS_H
#define SILVER_VFS_H
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

typedef struct { char *path; unsigned char *data; size_t len; } SilverVfsEnt;
typedef struct { SilverVfsEnt *e; int n, cap; int embedded_first; int init; } SilverVfsState;

SilverVfsState g_silver_vfs __attribute__((weak));

static void vfs_init_once(void) {
    if (g_silver_vfs.init) return;
    g_silver_vfs.init = 1;
    g_silver_vfs.embedded_first = 1;
    const char *m = getenv("SILVER_ASSETS");               /* disk | embedded | auto */
    if (m && strcmp(m, "disk") == 0) g_silver_vfs.embedded_first = 0;
}

/* normalizacja: bez file://, bez ./, z rozwiązanym .., bez wiodącego / ; ścieżki pod cwd stają się względne */
static void vfs_norm(const char *p, char *out, size_t cap) {
    char tmp[2048];
    if (!p) p = "";
    if (strncmp(p, "file://", 7) == 0) p += 7;
    char cwd[1024];
    size_t cl = 0;
    if (p[0] == '/' && getcwd(cwd, sizeof cwd)) { cl = strlen(cwd); if (strncmp(p, cwd, cl) == 0 && p[cl] == '/') p += cl + 1; else cl = 0; }
    snprintf(tmp, sizeof tmp, "%s", p);
    const char *segs[128]; int ns = 0;
    char *save = NULL;
    for (char *t = strtok_r(tmp, "/", &save); t && ns < 128; t = strtok_r(NULL, "/", &save)) {
        if (strcmp(t, ".") == 0 || !t[0]) continue;
        if (strcmp(t, "..") == 0) { if (ns > 0) ns--; continue; }
        segs[ns++] = t;
    }
    size_t o = 0;
    for (int i = 0; i < ns; i++) {
        size_t sl = strlen(segs[i]);
        if (o + sl + 2 >= cap) break;
        if (o) out[o++] = '/';
        memcpy(out + o, segs[i], sl); o += sl;
    }
    out[o] = 0;
}

static int vfs_b64val(int c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+' || c == '-') return 62;
    if (c == '/' || c == '_') return 63;
    return -1;
}

/* dodaj wpis (base64 → bajty). Zwraca 1 przy sukcesie. */
static int vfs_add_b64(const char *path, const char *b64) {
    vfs_init_once();
    if (!path || !b64) return 0;
    size_t n = strlen(b64);
    unsigned char *d = (unsigned char *)malloc(n / 4 * 3 + 4);
    if (!d) return 0;
    size_t o = 0; unsigned acc = 0; int bits = 0;
    for (size_t i = 0; i < n; i++) {
        int v = vfs_b64val((unsigned char)b64[i]);
        if (v < 0) continue;
        acc = (acc << 6) | (unsigned)v; bits += 6;
        if (bits >= 8) { bits -= 8; d[o++] = (unsigned char)((acc >> bits) & 0xff); }
    }
    d[o] = 0;                                              /* NUL na końcu: pliki tekstowe jako C-string */
    char norm[1024]; vfs_norm(path, norm, sizeof norm);
    for (int i = 0; i < g_silver_vfs.n; i++)
        if (strcmp(g_silver_vfs.e[i].path, norm) == 0) { free(g_silver_vfs.e[i].data); g_silver_vfs.e[i].data = d; g_silver_vfs.e[i].len = o; return 1; }
    if (g_silver_vfs.n == g_silver_vfs.cap) {
        int nc = g_silver_vfs.cap ? g_silver_vfs.cap * 2 : 64;
        SilverVfsEnt *ne = (SilverVfsEnt *)realloc(g_silver_vfs.e, (size_t)nc * sizeof *ne);
        if (!ne) { free(d); return 0; }
        g_silver_vfs.e = ne; g_silver_vfs.cap = nc;
    }
    g_silver_vfs.e[g_silver_vfs.n].path = strdup(norm);
    g_silver_vfs.e[g_silver_vfs.n].data = d;
    g_silver_vfs.e[g_silver_vfs.n].len = o;
    g_silver_vfs.n++;
    return 1;
}

static const SilverVfsEnt *vfs_find_raw(const char *path) {
    if (!g_silver_vfs.n || !path || !path[0]) return NULL;
    char norm[1024]; vfs_norm(path, norm, sizeof norm);
    if (!norm[0]) return NULL;
    for (int i = 0; i < g_silver_vfs.n; i++) if (strcmp(g_silver_vfs.e[i].path, norm) == 0) return &g_silver_vfs.e[i];
    /* ścieżka bezwzględna spoza cwd (np. katalog zasobów .deb/AppImage): dopasuj po sufiksie */
    size_t nl = strlen(norm);
    const SilverVfsEnt *best = NULL; size_t bl = 0;
    for (int i = 0; i < g_silver_vfs.n; i++) {
        size_t pl = strlen(g_silver_vfs.e[i].path);
        if (pl < nl && norm[nl - pl - 1] == '/' && strcmp(norm + nl - pl, g_silver_vfs.e[i].path) == 0 && pl > bl) { best = &g_silver_vfs.e[i]; bl = pl; }
    }
    return best;
}

/* wpis do użycia przy odczycie `path`, albo NULL (czytaj z dysku). Tryb disk-first: plik na dysku wygrywa. */
static const SilverVfsEnt *vfs_get(const char *path) {
    vfs_init_once();
    const SilverVfsEnt *e = vfs_find_raw(path);
    if (!e) return NULL;
    if (!g_silver_vfs.embedded_first) { struct stat st; if (path && stat(path, &st) == 0) return NULL; }
    return e;
}

/* fopen "rb" z VFS (fmemopen) albo z dysku */
static FILE *vfs_fopen_rb(const char *path) {
    const SilverVfsEnt *e = vfs_get(path);
    if (e) return fmemopen(e->data, e->len ? e->len : 1, "rb");
    return fopen(path, "rb");
}

/* sprawdza istnienie (VFS albo dysk) */
static int vfs_exists(const char *path) {
    if (vfs_get(path)) return 1;
    struct stat st; return path && stat(path, &st) == 0;
}
#endif
