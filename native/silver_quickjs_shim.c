#include <quickjs.h>
#include "silver_api_embed.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <errno.h>
#ifndef _WIN32
#  include <sys/types.h>
#  include <sys/socket.h>
#  include <netdb.h>
#  include <unistd.h>
#  include <fcntl.h>
#  include <netinet/in.h>
#  include <netinet/tcp.h>
#endif

#if defined(_WIN32)
#  include <windows.h>
static int64_t now_ms(void) {
    LARGE_INTEGER f, c;
    QueryPerformanceFrequency(&f); QueryPerformanceCounter(&c);
    return (int64_t)(c.QuadPart * 1000 / f.QuadPart);
}
#else
#  include <time.h>
static int64_t now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}
#endif

#define SILVER_MAX_CTX        8
#define SILVER_QUEUE_LEN      4096
#define SILVER_MAX_TIMERS     1024
#define SILVER_DEFAULT_BUDGET 3000          /* ms na jedno wejście do JS */
#define SILVER_MEM_LIMIT      (256u * 1024u * 1024u)
#define SILVER_STACK_LIMIT    (1024u * 1024u)

typedef struct {
    char *call_id;
    char *cmd;
    char *args_json;
} PendingInvoke;

typedef struct {
    int     used;
    int     id;          /* id z JS */
    int64_t due;         /* ms (monotonic) */
    int     interval;    /* >0 → powtarzaj co tyle ms */
} Timer;

/* 0.3: klient WebSocket (ws://, bez TLS) obsługiwany z pętli js_tick */
#define SILVER_MAX_WS 16
typedef struct {
    int     used, id, fd, state;      /* state: 0 łączenie, 1 handshake wysłany, 2 otwarte, 3 zamykanie */
    char    key[32];
    char    host[256];
    char   *rbuf; size_t rlen, rcap;
    char   *msg;  size_t mlen; int msg_op;
    int64_t t0;
} SilverWs;

typedef struct {
    int        used;
    JSRuntime *rt;
    JSContext *ctx;
    JSValue    global;

    PendingInvoke queue[SILVER_QUEUE_LEN];
    int head, tail, count;

    Timer   timers[SILVER_MAX_TIMERS];
    int64_t deadline;      /* 0 = brak limitu */
    int     budget_ms;
    int     interrupted;   /* ustawiane przez interrupt handler */
    char    last_error[2048];
    char   *poll_buf;      /* ważny do następnego js_poll_invoke */
    SilverWs ws[SILVER_MAX_WS];
} SilverJsCtx;

static SilverJsCtx g_ctx[SILVER_MAX_CTX];
static int eval_buf(SilverJsCtx *sc, const char *src, size_t len, const char *name);
static int eval_module_buf(SilverJsCtx *sc, const char *src, size_t len, const char *name);
static int looks_like_module(const char *src, size_t len, const char *name);
static void ws_poll(SilverJsCtx *sc);
static int call_global3(SilverJsCtx *sc, const char *fname, const char *a, const char *b, const char *c);
static int ws_any_open(SilverJsCtx *sc);
static JSValue native_ws_open(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv);
static JSValue native_ws_send(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv);
static JSValue native_ws_close(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv);
static char *mod_normalize(JSContext *ctx, const char *base, const char *name, void *opaque);
static JSModuleDef *mod_loader(JSContext *ctx, const char *name, void *opaque);

static SilverJsCtx *by_ctx(JSContext *ctx) {
    for (int i = 0; i < SILVER_MAX_CTX; i++)
        if (g_ctx[i].used && g_ctx[i].ctx == ctx) return &g_ctx[i];
    return NULL;
}
#define VALID(h) ((h) >= 0 && (h) < SILVER_MAX_CTX && g_ctx[(h)].used)

/* ---- raportowanie wyjątków ---- */
static void report_exception(SilverJsCtx *sc, const char *where) {
    JSContext *ctx = sc->ctx;
    JSValue exc = JS_GetException(ctx);
    const char *msg = JS_ToCString(ctx, exc);
    const char *stack = NULL;
    JSValue st = JS_UNDEFINED;
    if (JS_IsObject(exc)) {
        st = JS_GetPropertyStr(ctx, exc, "stack");
        if (!JS_IsUndefined(st)) stack = JS_ToCString(ctx, st);
    }
    /* 0.3: jeśli JS udostępnia __silver_format_error(stack), przemapuj stos przez source mapy */
    char *mapped = NULL;
    if (stack) {
        JSValue fe = JS_GetPropertyStr(ctx, sc->global, "__silver_format_error");
        if (JS_IsFunction(ctx, fe)) {
            JSValue arg = JS_NewString(ctx, stack);
            JSValue rv = JS_Call(ctx, fe, sc->global, 1, &arg);
            if (!JS_IsException(rv) && JS_IsString(rv)) {
                const char *ms = JS_ToCString(ctx, rv);
                if (ms) { mapped = strdup(ms); JS_FreeCString(ctx, ms); }
            } else if (JS_IsException(rv)) {
                JSValue e2 = JS_GetException(ctx);
                JS_FreeValue(ctx, e2);
            }
            JS_FreeValue(ctx, rv);
            JS_FreeValue(ctx, arg);
        }
        JS_FreeValue(ctx, fe);
    }
    snprintf(sc->last_error, sizeof(sc->last_error), "%s: %s%s%s", where,
             msg ? msg : "(wyjątek)", stack ? "\n" : "", mapped ? mapped : (stack ? stack : ""));
    free(mapped);
    fprintf(stderr, "[silver-js] %s\n", sc->last_error);
    if (msg) JS_FreeCString(ctx, msg);
    if (stack) JS_FreeCString(ctx, stack);
    JS_FreeValue(ctx, st);
    JS_FreeValue(ctx, exc);
}

/* quickjs (klasyczny) przekazuje tu JSRuntime*, nowszy quickjs-ng - JSContext*.
 * Pierwszy argument i tak nie jest uzywany, wiec deklarujemy go jako void*
 * (ten sam ABI w obu wariantach) i rzutujemy przy rejestracji - dzieki temu
 * kod buduje sie z OBOMA wersjami biblioteki (bez -Wincompatible-pointer-types). */
static int interrupt_cb(void *unused_rt_or_ctx, void *opaque) {
    (void)unused_rt_or_ctx;
    SilverJsCtx *sc = (SilverJsCtx *)opaque;
    if (sc->deadline && now_ms() > sc->deadline) { sc->interrupted = 1; return 1; }
    return 0;
}

static void enter_js(SilverJsCtx *sc) {
    sc->interrupted = 0;
    sc->deadline = sc->budget_ms > 0 ? now_ms() + sc->budget_ms : 0;
}

/* wykonaj wszystkie oczekujące mikrozadania (Promise.then / await) */
static void drain_jobs(SilverJsCtx *sc) {
    for (;;) {
        JSContext *jctx = NULL;
        int r = JS_ExecutePendingJob(sc->rt, &jctx);
        if (r == 0) break;
        if (r < 0) { report_exception(sc, "zadanie Promise"); }
    }
}

static void leave_js(SilverJsCtx *sc) {
    drain_jobs(sc);
    sc->deadline = 0;
}

/* ---- natywne funkcje wstrzykiwane do globalThis ---- */

/* __silver_native_invoke(callId, cmd, json) -> bool (false = kolejka pełna) */
static JSValue native_invoke(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    (void)this_val;
    SilverJsCtx *sc = by_ctx(ctx);
    if (!sc || argc < 3) return JS_FALSE;
    if (sc->count >= SILVER_QUEUE_LEN) return JS_FALSE;

    const char *call_id = JS_ToCString(ctx, argv[0]);
    const char *cmd     = JS_ToCString(ctx, argv[1]);
    const char *args    = JS_ToCString(ctx, argv[2]);
    PendingInvoke *slot = &sc->queue[sc->tail];
    slot->call_id   = strdup(call_id ? call_id : "");
    slot->cmd       = strdup(cmd ? cmd : "");
    slot->args_json = strdup(args ? args : "{}");
    if (call_id) JS_FreeCString(ctx, call_id);
    if (cmd)     JS_FreeCString(ctx, cmd);
    if (args)    JS_FreeCString(ctx, args);
    if (!slot->call_id || !slot->cmd || !slot->args_json) {
        free(slot->call_id); free(slot->cmd); free(slot->args_json);
        memset(slot, 0, sizeof(*slot));
        return JS_FALSE;
    }
    sc->tail = (sc->tail + 1) % SILVER_QUEUE_LEN;
    sc->count++;
    return JS_TRUE;
}

/* __silver_console(level, text) */
static JSValue native_console(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    (void)this_val;
    if (argc < 2) return JS_UNDEFINED;
    const char *lvl = JS_ToCString(ctx, argv[0]);
    const char *txt = JS_ToCString(ctx, argv[1]);
    fprintf(stderr, "[js:%s] %s\n", lvl ? lvl : "log", txt ? txt : "");
    if (lvl) JS_FreeCString(ctx, lvl);
    if (txt) JS_FreeCString(ctx, txt);
    return JS_UNDEFINED;
}

/* __silver_timer_set(id, delay_ms, repeat_bool) / __silver_timer_clear(id) */
static JSValue native_timer_set(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    (void)this_val;
    SilverJsCtx *sc = by_ctx(ctx);
    if (!sc || argc < 3) return JS_FALSE;
    int32_t id = 0, delay = 0;
    JS_ToInt32(ctx, &id, argv[0]);
    JS_ToInt32(ctx, &delay, argv[1]);
    int repeat = JS_ToBool(ctx, argv[2]);
    if (delay < 0) delay = 0;
    if (repeat && delay < 1) delay = 1;
    for (int i = 0; i < SILVER_MAX_TIMERS; i++) {
        if (sc->timers[i].used && sc->timers[i].id == id) {
            sc->timers[i].due = now_ms() + delay;
            sc->timers[i].interval = repeat ? delay : 0;
            return JS_TRUE;
        }
    }
    for (int i = 0; i < SILVER_MAX_TIMERS; i++) {
        if (!sc->timers[i].used) {
            sc->timers[i].used = 1;
            sc->timers[i].id = id;
            sc->timers[i].due = now_ms() + delay;
            sc->timers[i].interval = repeat ? delay : 0;
            return JS_TRUE;
        }
    }
    return JS_FALSE;
}

static JSValue native_timer_clear(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    (void)this_val;
    SilverJsCtx *sc = by_ctx(ctx);
    if (!sc || argc < 1) return JS_UNDEFINED;
    int32_t id = 0;
    JS_ToInt32(ctx, &id, argv[0]);
    for (int i = 0; i < SILVER_MAX_TIMERS; i++)
        if (sc->timers[i].used && sc->timers[i].id == id) sc->timers[i].used = 0;
    return JS_UNDEFINED;
}

/* __silver_now() -> ms (monotonic, dla performance.now/Date fallback) */
static JSValue native_now(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    (void)this_val; (void)argc; (void)argv;
    return JS_NewFloat64(ctx, (double)now_ms());
}

static void set_fn(JSContext *ctx, JSValue global, const char *name, JSCFunction *fn, int nargs) {
    JS_SetPropertyStr(ctx, global, name, JS_NewCFunction(ctx, fn, name, nargs));
}

/* ---- js_init(win_handle) -> uchwyt kontekstu (>=0) lub -1 ---- */
/* Nieobsłużone odrzucenia Promise (np. wyjątek w async function przy starcie Svelte/React) były
 * w QuickJS całkowicie ciche — aplikacja po prostu się nie montowała. Raportujemy je na stderr. */
#ifdef JS_BOOL
typedef JS_BOOL silver_js_bool;
#else
#include <stdbool.h>
typedef bool silver_js_bool;
#endif
/* SILVER_DEBUG>=2 → ślad komunikacji H# <-> JS na niebuforowanym stderr. */
static int jsdbg(void) {
    static int v = -1;
    if (v < 0) { const char *e = getenv("SILVER_DEBUG"); v = (e && atoi(e) >= 2) ? 1 : 0; }
    return v;
}

/* Wywoływane z H# (extern js_debug): niebuforowany ślad na stderr, tylko przy SILVER_DEBUG>=2. */
int js_debug(const char *msg) {
    if (jsdbg()) fprintf(stderr, "[silver-js] h#: %s\n", msg ? msg : "");
    return 1;
}

#define SILVER_MAX_REJ 16
static struct { int used; JSContext *ctx; JSValue promise; JSValue reason; } g_rej[SILVER_MAX_REJ];

/* QuickJS woła callback przy odrzuceniu (is_handled=0) i ponownie, gdy ktoś podepnie obsługę
 * (is_handled=1). Odkładamy więc odrzucenia na listę i raportujemy dopiero po opróżnieniu kolejki
 * zadań (js_tick), jeśli wciąż nikt ich nie obsłużył — bez fałszywych alarmów dla .catch(). */
static void promise_rejection_cb(JSContext *ctx, JSValueConst promise, JSValueConst reason,
                                 silver_js_bool is_handled, void *opaque) {
    (void)opaque;
    if (is_handled) {
        for (int i = 0; i < SILVER_MAX_REJ; i++)
            if (g_rej[i].used && g_rej[i].ctx == ctx &&
                JS_VALUE_GET_PTR(g_rej[i].promise) == JS_VALUE_GET_PTR(promise)) {
                JS_FreeValue(ctx, g_rej[i].promise);
                JS_FreeValue(ctx, g_rej[i].reason);
                g_rej[i].used = 0;
            }
        return;
    }
    for (int i = 0; i < SILVER_MAX_REJ; i++)
        if (!g_rej[i].used) {
            g_rej[i].used = 1; g_rej[i].ctx = ctx;
            g_rej[i].promise = JS_DupValue(ctx, promise);
            g_rej[i].reason = JS_DupValue(ctx, reason);
            return;
        }
}

/* drop=1: tylko zwolnij (zamykanie kontekstu), drop=0: wypisz i zwolnij */
static void flush_rejections(JSContext *ctx, int drop) {
    for (int i = 0; i < SILVER_MAX_REJ; i++) {
        if (!g_rej[i].used || g_rej[i].ctx != ctx) continue;
        if (!drop) {
            JSValue reason = g_rej[i].reason;
            const char *msg = JS_ToCString(ctx, reason);
            const char *stack = NULL;
            JSValue st = JS_UNDEFINED;
            if (JS_IsObject(reason)) {
                st = JS_GetPropertyStr(ctx, reason, "stack");
                if (!JS_IsUndefined(st)) stack = JS_ToCString(ctx, st);
            }
            fprintf(stderr, "[silver-js] nieobsłużone odrzucenie Promise: %s%s%s\n",
                    msg ? msg : "(brak komunikatu)", stack ? "\n" : "", stack ? stack : "");
            if (msg) JS_FreeCString(ctx, msg);
            if (stack) JS_FreeCString(ctx, stack);
            JS_FreeValue(ctx, st);
        }
        JS_FreeValue(ctx, g_rej[i].promise);
        JS_FreeValue(ctx, g_rej[i].reason);
        g_rej[i].used = 0;
    }
}

int js_init(int win_handle) {
    (void)win_handle;
    int slot = -1;
    for (int i = 0; i < SILVER_MAX_CTX; i++)
        if (!g_ctx[i].used) { slot = i; break; }
    if (slot < 0) return -1;

    JSRuntime *rt = JS_NewRuntime();
    if (!rt) return -1;
    JS_SetMemoryLimit(rt, SILVER_MEM_LIMIT);
    JS_SetMaxStackSize(rt, SILVER_STACK_LIMIT);
    JSContext *ctx = JS_NewContext(rt);
    if (!ctx) { JS_FreeRuntime(rt); return -1; }

    SilverJsCtx *sc = &g_ctx[slot];
    memset(sc, 0, sizeof(*sc));
    sc->used = 1; sc->rt = rt; sc->ctx = ctx;
    sc->budget_ms = SILVER_DEFAULT_BUDGET;
    JS_SetInterruptHandler(rt, (JSInterruptHandler *)interrupt_cb, sc);
    JS_SetHostPromiseRejectionTracker(rt, promise_rejection_cb, NULL);

    sc->global = JS_GetGlobalObject(ctx);
    set_fn(ctx, sc->global, "__silver_native_invoke", native_invoke, 3);
    set_fn(ctx, sc->global, "__silver_console",       native_console, 2);
    set_fn(ctx, sc->global, "__silver_timer_set",     native_timer_set, 3);
    set_fn(ctx, sc->global, "__silver_timer_clear",   native_timer_clear, 1);
    set_fn(ctx, sc->global, "__silver_now",           native_now, 0);
    set_fn(ctx, sc->global, "__silver_ws_open",       native_ws_open, 2);
    set_fn(ctx, sc->global, "__silver_ws_send",       native_ws_send, 2);
    set_fn(ctx, sc->global, "__silver_ws_close",      native_ws_close, 2);
    JS_SetModuleLoaderFunc(rt, mod_normalize, mod_loader, NULL);

    /* wbudowane API (console, timery, fetch, silver.*, DOM) */
    if (!eval_buf(sc, (const char *)silver_api_js, silver_api_js_len, "silver_api.js")) {
        fprintf(stderr, "[silver-js] błąd inicjalizacji API: %s\n", sc->last_error);
        JS_FreeValue(ctx, sc->global);
        JS_FreeContext(ctx);
        JS_FreeRuntime(rt);
        memset(sc, 0, sizeof(*sc));
        return -1;
    }
    return slot;
}

/* budżet czasu jednego wejścia do JS (ms); 0 = bez limitu */
int js_set_budget(int h, int ms) {
    if (!VALID(h)) return 0;
    g_ctx[h].budget_ms = ms < 0 ? 0 : ms;
    return 1;
}

int js_set_memory_limit_mb(int h, int mb) {
    if (!VALID(h) || mb < 1) return 0;
    JS_SetMemoryLimit(g_ctx[h].rt, (size_t)mb * 1024u * 1024u);
    return 1;
}

static char *read_whole_file(const char *path, size_t *out_len) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (len < 0) { fclose(f); return NULL; }
    char *buf = (char *)malloc((size_t)len + 1);
    if (!buf) { fclose(f); return NULL; }
    size_t n = fread(buf, 1, (size_t)len, f);
    fclose(f);
    buf[n] = '\0';
    if (out_len) *out_len = n;
    return buf;
}

static int eval_buf(SilverJsCtx *sc, const char *src, size_t len, const char *name) {
    enter_js(sc);
    JSValue res = JS_Eval(sc->ctx, src, len, name, JS_EVAL_TYPE_GLOBAL);
    int ok = !JS_IsException(res);
    if (!ok) report_exception(sc, name);
    JS_FreeValue(sc->ctx, res);
    leave_js(sc);
    return ok && !sc->interrupted;
}

/* 0.3: po załadowaniu skryptu rejestruje jego source mapę (inline data: URL albo plik z
 * //# sourceMappingURL=..., albo sąsiedni "<plik>.map"), by błędy TypeScriptu miały czytelne stosy. */
static void register_source_map(SilverJsCtx *sc, const char *path, const char *src, size_t len) {
    const char *tag = "sourceMappingURL=";
    const char *found = NULL;
    size_t tl = strlen(tag);
    size_t from = len > 4096 ? len - 4096 : 0;   /* komentarz jest na końcu pliku */
    for (size_t i = from; i + tl < len; i++)
        if (strncmp(src + i, tag, tl) == 0) found = src + i + tl;
    char *url = NULL;
    if (found) {
        const char *e = found;
        while (e < src + len && *e != '\n' && *e != '\r' && *e != ' ' && *e != '*') e++;
        size_t ul = (size_t)(e - found);
        url = (char *)malloc(ul + 1);
        if (url) { memcpy(url, found, ul); url[ul] = 0; }
    }
    if (url && strncmp(url, "data:application/json;base64,", 29) == 0) {
        call_global3(sc, "__silver_register_map", path, url + 29, "1");
        free(url);
        return;
    }
    char *mapfile = NULL;
    if (url && url[0]) {
        const char *slash = strrchr(path, '/');
        size_t dl = slash ? (size_t)(slash - path) + 1 : 0;
        mapfile = (char *)malloc(dl + strlen(url) + 1);
        if (mapfile) { memcpy(mapfile, path, dl); strcpy(mapfile + dl, url); }
    } else {
        mapfile = (char *)malloc(strlen(path) + 5);
        if (mapfile) { strcpy(mapfile, path); strcat(mapfile, ".map"); }
    }
    free(url);
    if (!mapfile) return;
    size_t ml = 0;
    char *json = read_whole_file(mapfile, &ml);
    free(mapfile);
    if (json) { call_global3(sc, "__silver_register_map", path, json, "0"); free(json); }
}

/* js_load_file(handle, path) -> bool. Kolejność: silver_api.js, potem app.js */
int js_load_file(int h, const char *path) {
    if (!VALID(h) || !path) return 0;
    size_t len = 0;
    char *src = read_whole_file(path, &len);
    if (!src) {
        snprintf(g_ctx[h].last_error, sizeof(g_ctx[h].last_error), "nie można otworzyć %s", path);
        return 0;
    }
    register_source_map(&g_ctx[h], path, src, len);
    int ok = looks_like_module(src, len, path) ? eval_module_buf(&g_ctx[h], src, len, path) : eval_buf(&g_ctx[h], src, len, path);
    free(src);
    return ok;
}

/* js_eval(handle, code, name) -> bool (kod z pamięci, np. wbudowane API) */
int js_eval(int h, const char *code, const char *name) {
    if (!VALID(h) || !code) return 0;
    size_t cl = strlen(code);
    const char *nm = name && name[0] ? name : "<eval>";
    return looks_like_module(code, cl, nm) ? eval_module_buf(&g_ctx[h], code, cl, nm) : eval_buf(&g_ctx[h], code, cl, nm);
}

/* js_poll_invoke(handle) -> "" | "call_id\x01cmd\x01json" */
const char *js_poll_invoke(int h) {
    if (!VALID(h)) return "";
    SilverJsCtx *sc = &g_ctx[h];
    if (sc->count == 0) return "";
    PendingInvoke *slot = &sc->queue[sc->head];
    if (jsdbg()) fprintf(stderr, "[silver-js] poll_invoke: wydaję '%s' (id=%s, %zuB), w kolejce %d\n",
                         slot->cmd, slot->call_id, strlen(slot->args_json), sc->count);
    size_t n = strlen(slot->call_id) + strlen(slot->cmd) + strlen(slot->args_json) + 3;
    free(sc->poll_buf);
    sc->poll_buf = (char *)malloc(n);
    if (sc->poll_buf)
        snprintf(sc->poll_buf, n, "%s\x01%s\x01%s", slot->call_id, slot->cmd, slot->args_json);
    free(slot->call_id); free(slot->cmd); free(slot->args_json);
    memset(slot, 0, sizeof(*slot));
    sc->head = (sc->head + 1) % SILVER_QUEUE_LEN;
    sc->count--;
    return sc->poll_buf ? sc->poll_buf : "";
}

int js_pending_invokes(int h) { return VALID(h) ? g_ctx[h].count : 0; }

static int call_global2(SilverJsCtx *sc, const char *fname, const char *a, const char *b) {
    JSContext *ctx = sc->ctx;
    JSValue fn = JS_GetPropertyStr(ctx, sc->global, fname);
    if (!JS_IsFunction(ctx, fn)) { JS_FreeValue(ctx, fn); return 0; }
    JSValue args[2];
    args[0] = JS_NewString(ctx, a ? a : "");
    args[1] = JS_NewString(ctx, b ? b : "null");
    enter_js(sc);
    JSValue ret = JS_Call(ctx, fn, sc->global, 2, args);
    int ok = !JS_IsException(ret);
    if (!ok) report_exception(sc, fname);
    JS_FreeValue(ctx, ret);
    JS_FreeValue(ctx, args[0]);
    JS_FreeValue(ctx, args[1]);
    JS_FreeValue(ctx, fn);
    leave_js(sc);
    return ok && !sc->interrupted;
}

/* Jak call_global2, ale zwraca prawdziwość wartości zwróconej przez funkcję JS (-1 przy błędzie).
 * Używane do synchronicznego preventDefault(): H# pyta JS, czy zdarzenie zostało anulowane. */
static int call_global2_ret(SilverJsCtx *sc, const char *fname, const char *a, const char *b) {
    JSContext *ctx = sc->ctx;
    JSValue fn = JS_GetPropertyStr(ctx, sc->global, fname);
    if (!JS_IsFunction(ctx, fn)) { JS_FreeValue(ctx, fn); return -1; }
    JSValue args[2];
    args[0] = JS_NewString(ctx, a ? a : "");
    args[1] = JS_NewString(ctx, b ? b : "null");
    enter_js(sc);
    JSValue ret = JS_Call(ctx, fn, sc->global, 2, args);
    int result = -1;
    if (JS_IsException(ret)) report_exception(sc, fname);
    else result = JS_ToBool(ctx, ret) ? 1 : 0;
    JS_FreeValue(ctx, ret);
    JS_FreeValue(ctx, args[0]);
    JS_FreeValue(ctx, args[1]);
    JS_FreeValue(ctx, fn);
    leave_js(sc);
    return sc->interrupted ? -1 : result;
}

/* js_resolve(handle, call_id, result_json) → rozwiązuje Promise w JS */
int js_resolve(int h, const char *call_id, const char *result_json) {
    if (!VALID(h)) return 0;
    if (jsdbg()) fprintf(stderr, "[silver-js] resolve: id=%s (%zuB)\n", call_id, strlen(result_json));
    return call_global2(&g_ctx[h], "__silver_resolve", call_id, result_json);
}

/* js_reject(handle, call_id, message) → odrzuca Promise w JS */
int js_reject(int h, const char *call_id, const char *message) {
    if (!VALID(h)) return 0;
    if (jsdbg()) fprintf(stderr, "[silver-js] reject: id=%s msg=%s\n", call_id, message ? message : "");
    return call_global2(&g_ctx[h], "__silver_reject", call_id, message);
}

/* js_emit(handle, event, payload_json) → silver.listen(...) */
int js_emit(int h, const char *event_name, const char *payload_json) {
    if (!VALID(h)) return 0;
    if (jsdbg()) fprintf(stderr, "[silver-js] emit: '%s' (%zuB)\n", event_name, strlen(payload_json));
    return call_global2(&g_ctx[h], "__silver_dispatch_event", event_name, payload_json);
}

/* js_emit_ret(handle, event, payload_json) → 1, gdy obsługa zdarzenia zwróciła prawdę
 * (JS: zdarzenie anulowane przez preventDefault), 0 gdy nie, -1 przy błędzie. */
int js_emit_ret(int h, const char *event_name, const char *payload_json) {
    if (!VALID(h)) return -1;
    return call_global2_ret(&g_ctx[h], "__silver_dispatch_event", event_name, payload_json);
}


/* ====================================================================
 *  0.3: WebSocket (ws://) — natywny klient, obsługiwany z js_tick
 * ==================================================================== */
static int call_global3(SilverJsCtx *sc, const char *fname, const char *a, const char *b, const char *c) {
    JSContext *ctx = sc->ctx;
    JSValue fn = JS_GetPropertyStr(ctx, sc->global, fname);
    if (!JS_IsFunction(ctx, fn)) { JS_FreeValue(ctx, fn); return 0; }
    JSValue args[3];
    args[0] = JS_NewString(ctx, a ? a : "");
    args[1] = JS_NewString(ctx, b ? b : "");
    args[2] = JS_NewString(ctx, c ? c : "");
    enter_js(sc);
    JSValue ret = JS_Call(ctx, fn, sc->global, 3, args);
    int ok = !JS_IsException(ret);
    if (!ok) report_exception(sc, fname);
    JS_FreeValue(ctx, ret);
    for (int i = 0; i < 3; i++) JS_FreeValue(ctx, args[i]);
    JS_FreeValue(ctx, fn);
    leave_js(sc);
    return ok;
}

static void ws_event(SilverJsCtx *sc, int id, const char *type, const char *data) {
    char idb[24];
    snprintf(idb, sizeof idb, "%d", id);
    call_global3(sc, "__silver_ws_event", idb, type, data ? data : "");
}

/* --- SHA-1 + Base64 (tylko do weryfikacji Sec-WebSocket-Accept) --- */
typedef struct { uint32_t h[5]; uint64_t len; unsigned char buf[64]; size_t n; } Sha1;
#define ROL(x, n) (((x) << (n)) | ((x) >> (32 - (n))))
static void sha1_block(Sha1 *c, const unsigned char *p) {
    uint32_t w[80];
    for (int i = 0; i < 16; i++) w[i] = ((uint32_t)p[i*4] << 24) | ((uint32_t)p[i*4+1] << 16) | ((uint32_t)p[i*4+2] << 8) | p[i*4+3];
    for (int i = 16; i < 80; i++) w[i] = ROL(w[i-3] ^ w[i-8] ^ w[i-14] ^ w[i-16], 1);
    uint32_t a = c->h[0], b = c->h[1], cc = c->h[2], d = c->h[3], e = c->h[4];
    for (int i = 0; i < 80; i++) {
        uint32_t f, k;
        if (i < 20) { f = (b & cc) | (~b & d); k = 0x5A827999; }
        else if (i < 40) { f = b ^ cc ^ d; k = 0x6ED9EBA1; }
        else if (i < 60) { f = (b & cc) | (b & d) | (cc & d); k = 0x8F1BBCDC; }
        else { f = b ^ cc ^ d; k = 0xCA62C1D6; }
        uint32_t t = ROL(a, 5) + f + e + k + w[i];
        e = d; d = cc; cc = ROL(b, 30); b = a; a = t;
    }
    c->h[0] += a; c->h[1] += b; c->h[2] += cc; c->h[3] += d; c->h[4] += e;
}
static void sha1_init(Sha1 *c) {
    c->h[0] = 0x67452301; c->h[1] = 0xEFCDAB89; c->h[2] = 0x98BADCFE; c->h[3] = 0x10325476; c->h[4] = 0xC3D2E1F0;
    c->len = 0; c->n = 0;
}
static void sha1_update(Sha1 *c, const unsigned char *d, size_t n) {
    c->len += n;
    while (n) {
        size_t take = 64 - c->n; if (take > n) take = n;
        memcpy(c->buf + c->n, d, take); c->n += take; d += take; n -= take;
        if (c->n == 64) { sha1_block(c, c->buf); c->n = 0; }
    }
}
static void sha1_final(Sha1 *c, unsigned char out[20]) {
    uint64_t bits = c->len * 8;
    unsigned char pad = 0x80;
    sha1_update(c, &pad, 1);
    unsigned char z = 0;
    while (c->n != 56) sha1_update(c, &z, 1);
    unsigned char lb[8];
    for (int i = 0; i < 8; i++) lb[i] = (unsigned char)(bits >> (56 - 8 * i));
    sha1_update(c, lb, 8);
    for (int i = 0; i < 5; i++) { out[i*4] = c->h[i] >> 24; out[i*4+1] = c->h[i] >> 16; out[i*4+2] = c->h[i] >> 8; out[i*4+3] = c->h[i]; }
}
static void b64enc(const unsigned char *in, size_t n, char *out) {
    static const char T[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    size_t o = 0;
    for (size_t i = 0; i < n; i += 3) {
        uint32_t v = (uint32_t)in[i] << 16 | (i + 1 < n ? (uint32_t)in[i+1] << 8 : 0) | (i + 2 < n ? in[i+2] : 0);
        out[o++] = T[v >> 18]; out[o++] = T[(v >> 12) & 63];
        out[o++] = i + 1 < n ? T[(v >> 6) & 63] : '=';
        out[o++] = i + 2 < n ? T[v & 63] : '=';
    }
    out[o] = 0;
}

#ifndef _WIN32
static void ws_free(SilverWs *w) {
    if (w->fd >= 0) close(w->fd);
    free(w->rbuf); free(w->msg);
    memset(w, 0, sizeof *w);
    w->fd = -1;
}

static int ws_send_all(int fd, const unsigned char *d, size_t n) {
    size_t off = 0;
    int spins = 0;
    while (off < n) {
        ssize_t r = send(fd, d + off, n - off, 0);
        if (r > 0) { off += (size_t)r; spins = 0; continue; }
        if (r < 0 && (errno == EAGAIN || errno == EWOULDBLOCK) && spins++ < 2000) {
            fd_set wf; FD_ZERO(&wf); FD_SET(fd, &wf);
            struct timeval tv = { 0, 5000 };
            select(fd + 1, NULL, &wf, NULL, &tv);
            continue;
        }
        return 0;
    }
    return 1;
}

/* wysyła ramkę klienta (zamaskowaną) */
static int ws_send_frame(SilverWs *w, int op, const unsigned char *data, size_t len) {
    unsigned char *f = (unsigned char *)malloc(len + 14);
    if (!f) return 0;
    size_t p = 0;
    f[p++] = 0x80 | (op & 0x0f);
    if (len < 126) f[p++] = 0x80 | (unsigned char)len;
    else if (len < 65536) { f[p++] = 0x80 | 126; f[p++] = (unsigned char)(len >> 8); f[p++] = (unsigned char)len; }
    else { f[p++] = 0x80 | 127; for (int i = 7; i >= 0; i--) f[p++] = (unsigned char)((uint64_t)len >> (8 * i)); }
    unsigned char mask[4];
    for (int i = 0; i < 4; i++) mask[i] = (unsigned char)(rand() & 0xff);
    memcpy(f + p, mask, 4); p += 4;
    for (size_t i = 0; i < len; i++) f[p + i] = data[i] ^ mask[i & 3];
    int ok = ws_send_all(w->fd, f, p + len);
    free(f);
    return ok;
}

static int ws_stricase_has(const char *hay, const char *needle) {
    size_t nl = strlen(needle);
    for (; *hay; hay++) if (strncasecmp(hay, needle, nl) == 0) return 1;
    return 0;
}

static void ws_fail(SilverJsCtx *sc, SilverWs *w, const char *msg) {
    ws_event(sc, w->id, "error", msg);
    ws_event(sc, w->id, "close", "1006");
    ws_free(w);
}

static void ws_handshake_check(SilverJsCtx *sc, SilverWs *w) {
    if (w->rlen < 4) return;
    w->rbuf[w->rlen < w->rcap ? w->rlen : w->rcap - 1] = 0;
    char *end = strstr(w->rbuf, "\r\n\r\n");
    if (!end) { if (w->rlen > 16384) ws_fail(sc, w, "odpowiedź handshake zbyt duża"); return; }
    size_t hdr_len = (size_t)(end - w->rbuf) + 4;
    if (strncmp(w->rbuf, "HTTP/1.1 101", 12) != 0) { ws_fail(sc, w, "serwer odrzucił upgrade WebSocket"); return; }
    /* Sec-WebSocket-Accept = base64(sha1(key + GUID)) */
    char src[96];
    snprintf(src, sizeof src, "%s258EAFA5-E914-47DA-95CA-C5AB0DC85B11", w->key);
    Sha1 c; unsigned char dg[20]; char acc[40];
    sha1_init(&c); sha1_update(&c, (const unsigned char *)src, strlen(src)); sha1_final(&c, dg);
    b64enc(dg, 20, acc);
    char *hdr = (char *)malloc(hdr_len + 1);
    if (!hdr) { ws_fail(sc, w, "brak pamięci"); return; }
    memcpy(hdr, w->rbuf, hdr_len); hdr[hdr_len] = 0;
    char want[80];
    snprintf(want, sizeof want, "sec-websocket-accept: %s", acc);
    int ok = ws_stricase_has(hdr, want);
    free(hdr);
    if (!ok) { ws_fail(sc, w, "nieprawidłowy Sec-WebSocket-Accept"); return; }
    memmove(w->rbuf, w->rbuf + hdr_len, w->rlen - hdr_len);
    w->rlen -= hdr_len;
    w->state = 2;
    ws_event(sc, w->id, "open", "");
}

static void ws_parse_frames(SilverJsCtx *sc, SilverWs *w) {
    while (w->used && w->rlen >= 2) {
        const unsigned char *p = (const unsigned char *)w->rbuf;
        int fin = p[0] & 0x80, op = p[0] & 0x0f, masked = p[1] & 0x80;
        uint64_t len = p[1] & 0x7f;
        size_t off = 2;
        if (len == 126) { if (w->rlen < 4) return; len = ((uint64_t)p[2] << 8) | p[3]; off = 4; }
        else if (len == 127) { if (w->rlen < 10) return; len = 0; for (int i = 0; i < 8; i++) len = (len << 8) | p[2 + i]; off = 10; }
        unsigned char mk[4] = {0, 0, 0, 0};
        if (masked) { if (w->rlen < off + 4) return; memcpy(mk, p + off, 4); off += 4; }
        if (len > (64u << 20)) { ws_fail(sc, w, "ramka zbyt duża"); return; }
        if (w->rlen < off + (size_t)len) return;
        unsigned char *payload = (unsigned char *)malloc((size_t)len + 1);
        if (!payload) { ws_fail(sc, w, "brak pamięci"); return; }
        for (size_t i = 0; i < (size_t)len; i++) payload[i] = p[off + i] ^ mk[i & 3];
        payload[len] = 0;
        size_t consumed = off + (size_t)len;
        memmove(w->rbuf, w->rbuf + consumed, w->rlen - consumed);
        w->rlen -= consumed;
        if (op == 8) {
            char code[16] = "1005";
            if (len >= 2) snprintf(code, sizeof code, "%d", (payload[0] << 8) | payload[1]);
            if (w->state != 3) ws_send_frame(w, 8, payload, len >= 2 ? 2 : 0);
            free(payload);
            ws_event(sc, w->id, "close", code);
            ws_free(w);
            return;
        } else if (op == 9) {
            ws_send_frame(w, 10, payload, (size_t)len);
        } else if (op == 10) {
            /* pong: ignorujemy */
        } else if (op == 1 || op == 2 || op == 0) {
            if (op != 0) { free(w->msg); w->msg = NULL; w->mlen = 0; w->msg_op = op; }
            char *nm = (char *)realloc(w->msg, w->mlen + (size_t)len + 1);
            if (!nm) { free(payload); ws_fail(sc, w, "brak pamięci"); return; }
            w->msg = nm;
            memcpy(w->msg + w->mlen, payload, (size_t)len);
            w->mlen += (size_t)len;
            w->msg[w->mlen] = 0;
            if (fin) {
                char *deliver = w->msg;
                w->msg = NULL; w->mlen = 0;
                ws_event(sc, w->id, w->msg_op == 2 ? "binary" : "message", deliver);
                free(deliver);
            }
        }
        free(payload);
    }
}

static void ws_poll(SilverJsCtx *sc) {
    for (int i = 0; i < SILVER_MAX_WS; i++) {
        SilverWs *w = &sc->ws[i];
        if (!w->used) continue;
        if (w->state < 0) { ws_fail(sc, w, "wss:// (TLS) nie jest obsługiwane w tej wersji"); continue; }
        if (w->state == 0) {
            fd_set wf; FD_ZERO(&wf); FD_SET(w->fd, &wf);
            struct timeval tv = { 0, 0 };
            if (select(w->fd + 1, NULL, &wf, NULL, &tv) > 0) {
                int err = 0; socklen_t el = sizeof err;
                getsockopt(w->fd, SOL_SOCKET, SO_ERROR, &err, &el);
                if (err) { ws_fail(sc, w, "nie można połączyć"); continue; }
                char req[1400];
                const char *slash = strchr(w->host, '/');
                char hostonly[256]; size_t hl = slash ? (size_t)(slash - w->host) : strlen(w->host);
                memcpy(hostonly, w->host, hl); hostonly[hl] = 0;
                snprintf(req, sizeof req,
                    "GET %s HTTP/1.1\r\nHost: %s\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
                    "Sec-WebSocket-Key: %s\r\nSec-WebSocket-Version: 13\r\n\r\n",
                    slash ? slash : "/", hostonly, w->key);
                if (!ws_send_all(w->fd, (const unsigned char *)req, strlen(req))) { ws_fail(sc, w, "błąd wysyłania handshake"); continue; }
                w->state = 1; w->t0 = now_ms();
            } else if (now_ms() - w->t0 > 10000) { ws_fail(sc, w, "przekroczono czas łączenia"); }
            continue;
        }
        /* odczyt dostępnych danych */
        int eof = 0;
        for (int round = 0; round < 8 && w->used; round++) {
            if (w->rcap - w->rlen < 4096) {
                size_t nc = w->rcap ? w->rcap * 2 : 8192;
                char *nb = (char *)realloc(w->rbuf, nc);
                if (!nb) { ws_fail(sc, w, "brak pamięci"); break; }
                w->rbuf = nb; w->rcap = nc;
            }
            ssize_t r = recv(w->fd, w->rbuf + w->rlen, w->rcap - w->rlen - 1, 0);
            if (r > 0) { w->rlen += (size_t)r; continue; }
            if (r == 0) { eof = 1; break; }   /* najpierw sparsuj ramki (np. close), potem obsłuż EOF */
            if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) break;
            ws_fail(sc, w, "błąd odczytu"); break;
        }
        if (!w->used) continue;
        if (w->state == 1) ws_handshake_check(sc, w);
        if (w->used && w->state >= 2) ws_parse_frames(sc, w);
        if (w->used && eof) { ws_event(sc, w->id, "close", "1006"); ws_free(w); }
    }
}

static int ws_any_open(SilverJsCtx *sc) {
    for (int i = 0; i < SILVER_MAX_WS; i++) if (sc->ws[i].used) return 1;
    return 0;
}

static void ws_close_all(SilverJsCtx *sc) {
    for (int i = 0; i < SILVER_MAX_WS; i++) if (sc->ws[i].used) ws_free(&sc->ws[i]);
}

/* __silver_ws_open(id, url) -> bool */
static JSValue native_ws_open(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    (void)this_val;
    SilverJsCtx *sc = by_ctx(ctx);
    if (!sc || argc < 2) return JS_FALSE;
    int32_t id = 0;
    JS_ToInt32(ctx, &id, argv[0]);
    const char *url = JS_ToCString(ctx, argv[1]);
    if (!url) return JS_FALSE;
    SilverWs *w = NULL;
    for (int i = 0; i < SILVER_MAX_WS; i++) if (!sc->ws[i].used) { w = &sc->ws[i]; break; }
    if (!w) { JS_FreeCString(ctx, url); return JS_FALSE; }
    memset(w, 0, sizeof *w);
    w->used = 1; w->id = id; w->fd = -1; w->t0 = now_ms();
    if (strncmp(url, "ws://", 5) != 0) { w->state = -1; JS_FreeCString(ctx, url); return JS_TRUE; }
    const char *hp = url + 5;
    snprintf(w->host, sizeof w->host, "%s", hp);
    char hostbuf[256]; const char *slash = strchr(hp, '/');
    size_t hl = slash ? (size_t)(slash - hp) : strlen(hp);
    if (hl >= sizeof hostbuf) hl = sizeof hostbuf - 1;
    memcpy(hostbuf, hp, hl); hostbuf[hl] = 0;
    char port[8] = "80";
    char *colon = strrchr(hostbuf, ':');
    if (colon) { *colon = 0; snprintf(port, sizeof port, "%s", colon + 1); }
    unsigned char rnd[16];
    for (int i = 0; i < 16; i++) rnd[i] = (unsigned char)(rand() & 0xff);
    b64enc(rnd, 16, w->key);
    struct addrinfo hints, *res = NULL;
    memset(&hints, 0, sizeof hints);
    hints.ai_socktype = SOCK_STREAM;
    int gai = getaddrinfo(hostbuf, port, &hints, &res);
    if (gai != 0 || !res) { w->state = -1; JS_FreeCString(ctx, url); return JS_TRUE; }
    int fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (fd < 0) { freeaddrinfo(res); w->state = -1; JS_FreeCString(ctx, url); return JS_TRUE; }
    fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK);
    int one = 1; setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one);
    int cr = connect(fd, res->ai_addr, res->ai_addrlen);
    freeaddrinfo(res);
    if (cr < 0 && errno != EINPROGRESS) { close(fd); w->state = -1; JS_FreeCString(ctx, url); return JS_TRUE; }
    w->fd = fd; w->state = 0;
    JS_FreeCString(ctx, url);
    return JS_TRUE;
}

static SilverWs *ws_find(SilverJsCtx *sc, int id) {
    for (int i = 0; i < SILVER_MAX_WS; i++) if (sc->ws[i].used && sc->ws[i].id == id) return &sc->ws[i];
    return NULL;
}

/* __silver_ws_send(id, text) -> bool */
static JSValue native_ws_send(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    (void)this_val;
    SilverJsCtx *sc = by_ctx(ctx);
    if (!sc || argc < 2) return JS_FALSE;
    int32_t id = 0; JS_ToInt32(ctx, &id, argv[0]);
    SilverWs *w = ws_find(sc, id);
    if (!w || w->state != 2) return JS_FALSE;
    size_t len = 0;
    const char *txt = JS_ToCStringLen(ctx, &len, argv[1]);
    if (!txt) return JS_FALSE;
    int ok = ws_send_frame(w, 1, (const unsigned char *)txt, len);
    JS_FreeCString(ctx, txt);
    return ok ? JS_TRUE : JS_FALSE;
}

/* __silver_ws_close(id, code) */
static JSValue native_ws_close(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    (void)this_val;
    SilverJsCtx *sc = by_ctx(ctx);
    if (!sc || argc < 1) return JS_UNDEFINED;
    int32_t id = 0, code = 1000; JS_ToInt32(ctx, &id, argv[0]);
    if (argc > 1) JS_ToInt32(ctx, &code, argv[1]);
    SilverWs *w = ws_find(sc, id);
    if (!w) return JS_UNDEFINED;
    if (w->state == 2) {
        unsigned char cb[2] = { (unsigned char)(code >> 8), (unsigned char)code };
        ws_send_frame(w, 8, cb, 2);
        w->state = 3;
    } else {
        ws_event(sc, w->id, "close", "1006");
        ws_free(w);
    }
    return JS_UNDEFINED;
}
#else
static void ws_poll(SilverJsCtx *sc) { (void)sc; }
static int ws_any_open(SilverJsCtx *sc) { (void)sc; return 0; }
static void ws_close_all(SilverJsCtx *sc) { (void)sc; }
static JSValue native_ws_open(JSContext *ctx, JSValueConst t, int argc, JSValueConst *argv) { (void)ctx; (void)t; (void)argc; (void)argv; return JS_FALSE; }
static JSValue native_ws_send(JSContext *ctx, JSValueConst t, int argc, JSValueConst *argv) { (void)ctx; (void)t; (void)argc; (void)argv; return JS_FALSE; }
static JSValue native_ws_close(JSContext *ctx, JSValueConst t, int argc, JSValueConst *argv) { (void)ctx; (void)t; (void)argc; (void)argv; return JS_UNDEFINED; }
#endif

/* ====================================================================
 *  0.3: moduły ES (import/export) — loader z systemu plików
 * ==================================================================== */
static char *mod_normalize(JSContext *ctx, const char *base, const char *name, void *opaque) {
    (void)opaque;
    if (name[0] != '.') return js_strdup(ctx, name);
    size_t dl = 0;
    const char *slash = strrchr(base, '/');
    if (slash) dl = (size_t)(slash - base) + 1;
    size_t cap = dl + strlen(name) + 2;
    char *joined = (char *)malloc(cap);
    if (!joined) return NULL;
    memcpy(joined, base, dl);
    strcpy(joined + dl, name);
    /* zwija segmenty ./ i ../ */
    char *out = (char *)malloc(cap);
    if (!out) { free(joined); return NULL; }
    size_t o = 0;
    char *seg = joined;
    int absolute = joined[0] == '/';
    size_t seg_starts[256]; int depth = 0;
    if (absolute) out[o++] = '/';
    while (*seg) {
        char *end = strchr(seg, '/');
        size_t sl = end ? (size_t)(end - seg) : strlen(seg);
        if (sl == 0 || (sl == 1 && seg[0] == '.')) {
            /* pomiń */
        } else if (sl == 2 && seg[0] == '.' && seg[1] == '.') {
            if (depth > 0) { depth--; o = seg_starts[depth]; }
            else if (!absolute) { memcpy(out + o, "../", 3); o += 3; }
        } else {
            if (depth < 256) seg_starts[depth++] = o;
            memcpy(out + o, seg, sl); o += sl;
            if (end) out[o++] = '/';
        }
        if (!end) break;
        seg = end + 1;
    }
    out[o] = 0;
    char *res = js_strdup(ctx, out);
    free(joined); free(out);
    return res;
}

static JSModuleDef *mod_loader(JSContext *ctx, const char *name, void *opaque) {
    (void)opaque;
    size_t len = 0;
    char *src = read_whole_file(name, &len);
    char *alt = NULL;
    if (!src) {
        static const char *suffixes[] = { ".js", ".mjs", "/index.js", "/index.mjs" };
        size_t nl = strlen(name);
        for (int si = 0; si < 4 && !src; si++) {
            alt = (char *)realloc(alt, nl + 12);
            if (!alt) break;
            snprintf(alt, nl + 12, "%s%s", name, suffixes[si]);
            src = read_whole_file(alt, &len);
        }
    }
    if (!src) {
        JS_ThrowReferenceError(ctx, "nie można załadować modułu '%s'", name);
        free(alt);
        return NULL;
    }
    JSValue fv = JS_Eval(ctx, src, len, name, JS_EVAL_TYPE_MODULE | JS_EVAL_FLAG_COMPILE_ONLY);
    free(src); free(alt);
    if (JS_IsException(fv)) return NULL;
    JSModuleDef *m = (JSModuleDef *)JS_VALUE_GET_PTR(fv);
    JS_FreeValue(ctx, fv);
    return m;
}

/* Czy źródło wygląda na moduł ES (import/export na początku linii) lub plik ma rozszerzenie .mjs */
static int looks_like_module(const char *src, size_t len, const char *name) {
    size_t nl = name ? strlen(name) : 0;
    if (nl > 4 && strcmp(name + nl - 4, ".mjs") == 0) return 1;
    size_t i = 0;
    int line_start = 1;
    while (i < len) {
        if (line_start) {
            size_t j = i;
            while (j < len && (src[j] == ' ' || src[j] == '\t')) j++;
            if (j + 7 < len && strncmp(src + j, "import ", 7) == 0) return 1;
            if (j + 8 < len && strncmp(src + j, "import{", 7) == 0) return 1;
            if (j + 7 < len && strncmp(src + j, "export ", 7) == 0) return 1;
            if (j + 7 < len && strncmp(src + j, "export{", 7) == 0) return 1;
        }
        line_start = src[i] == '\n';
        i++;
        if (i > 200000 && !line_start) { /* ogranicz skanowanie gigantycznych bundli jednowierszowych */
            while (i < len && src[i] != '\n') i++;
        }
    }
    return 0;
}

static int eval_module_buf(SilverJsCtx *sc, const char *src, size_t len, const char *name) {
    JSContext *ctx = sc->ctx;
    enter_js(sc);
    JSValue fv = JS_Eval(ctx, src, len, name, JS_EVAL_TYPE_MODULE | JS_EVAL_FLAG_COMPILE_ONLY);
    int ok = !JS_IsException(fv);
    if (!ok) { report_exception(sc, name); leave_js(sc); return 0; }
    JSValue res = JS_EvalFunction(ctx, fv);
    if (JS_IsException(res)) {
        report_exception(sc, name);
        ok = 0;
    } else {
        /* quickjs-ng zwraca Promise; wykonaj oczekujące zadania i sprawdź odrzucenie */
        JSContext *jctx;
        while (JS_ExecutePendingJob(sc->rt, &jctx) > 0) { }
        if (JS_IsObject(res) && JS_PromiseState(ctx, res) == JS_PROMISE_REJECTED) {
            JSValue reason = JS_PromiseResult(ctx, res);
            JS_Throw(ctx, reason);
            report_exception(sc, name);
            ok = 0;
        }
    }
    JS_FreeValue(ctx, res);
    leave_js(sc);
    return ok && !sc->interrupted;
}

/* js_tick(handle) → odpala wymagalne timery i drenuje Promise.
 * Zwraca liczbę odpalonych timerów. Wołaj raz na klatkę. */
int js_tick(int h) {
    if (!VALID(h)) return 0;
    SilverJsCtx *sc = &g_ctx[h];
    if (jsdbg()) {
        static int n_tick = 0;
        n_tick++;
        if (n_tick <= 5 || n_tick % 500 == 0)
            fprintf(stderr, "[silver-js] tick #%d (h=%d, w kolejce wywołań: %d)\n", n_tick, h, sc->count);
    }
    int64_t now = now_ms();
    int fired = 0;
    for (int i = 0; i < SILVER_MAX_TIMERS; i++) {
        Timer *t = &sc->timers[i];
        if (!t->used || t->due > now) continue;
        int id = t->id;
        if (t->interval > 0) {
            t->due = now + t->interval;
        } else {
            t->used = 0;
        }
        char idbuf[24];
        snprintf(idbuf, sizeof(idbuf), "%d", id);
        call_global2(sc, "__silver_fire_timer", idbuf, "null");
        fired++;
    }
    ws_poll(sc);
    enter_js(sc);
    leave_js(sc);
    flush_rejections(sc->ctx, 0);
    return fired;
}

/* ms do najbliższego timera; -1 = brak, 0 = już wymagalny */
int js_next_timer_ms(int h) {
    if (!VALID(h)) return -1;
    SilverJsCtx *sc = &g_ctx[h];
    int64_t best = -1, now = now_ms();
    for (int i = 0; i < SILVER_MAX_TIMERS; i++) {
        if (!sc->timers[i].used) continue;
        int64_t d = sc->timers[i].due - now;
        if (d < 0) d = 0;
        if (best < 0 || d < best) best = d;
    }
    if (ws_any_open(sc) && (best < 0 || best > 15)) best = 15;   /* odpytuj gniazda WebSocket */
    return (int)best;
}

int js_has_jobs(int h) { return VALID(h) ? (JS_IsJobPending(g_ctx[h].rt) ? 1 : 0) : 0; }

const char *js_last_error(int h) { return VALID(h) ? g_ctx[h].last_error : ""; }

int js_shutdown(int h) {
    if (!VALID(h)) return 0;
    SilverJsCtx *sc = &g_ctx[h];
    while (sc->count > 0) js_poll_invoke(h);
    ws_close_all(sc);
    free(sc->poll_buf);
    JS_FreeValue(sc->ctx, sc->global);
    flush_rejections(sc->ctx, 1);
    JS_FreeContext(sc->ctx);
    JS_FreeRuntime(sc->rt);
    memset(sc, 0, sizeof(*sc));
    return 1;
}
