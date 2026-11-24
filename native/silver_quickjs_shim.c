#include <quickjs.h>
#include "silver_api_embed.h"
#include "silver_vfs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <errno.h>
#include "silver_net.h"      /* 0.5: gniazda (POSIX/Winsock), TLS, klient HTTP(S), wątki */

static int64_t now_ms(void) { return net_now_ms(); }

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
    int     used, id, state;          /* state: 0 łączenie, 1 handshake wysłany, 2 otwarte, 3 zamykanie */
    net_sock_t fd;
    char    key[32];
    char    host[256];
    int     tls;                      /* 0.5: wss:// (SILVER_TLS) */
    void   *ssl;                      /* SSL* */
    char    sni[256];
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
    uint64_t gen;          /* 0.5: unikalna generacja (zadania sieciowe i Workery wskazują właściciela) */
} SilverJsCtx;

static SilverJsCtx g_ctx[SILVER_MAX_CTX];
static uint64_t g_gen_counter = 0;
static JSValue native_fetch_start(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv);
static JSValue native_fetch_cancel(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv);
static JSValue native_read_text(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv);
static JSValue native_worker_spawn(JSContext *ctx, JSValueConst t, int argc, JSValueConst *argv);
static JSValue native_worker_post(JSContext *ctx, JSValueConst t, int argc, JSValueConst *argv);
static JSValue native_worker_terminate(JSContext *ctx, JSValueConst t, int argc, JSValueConst *argv);
static JSValue native_utf8_decode(JSContext *ctx, JSValueConst t, int argc, JSValueConst *argv);
static JSValue native_utf8_encode(JSContext *ctx, JSValueConst t, int argc, JSValueConst *argv);
static void fetch_deliver(SilverJsCtx *sc);
static void wk_pump(SilverJsCtx *sc);
static void wk_shutdown(SilverJsCtx *sc);
static int wk_any(SilverJsCtx *sc);
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
static char *read_whole_file(const char *path, size_t *out_len);
static void set_fn(JSContext *ctx, JSValue global, const char *name, JSCFunction *fn, int nargs);
static void enter_js(SilverJsCtx *sc);
static void leave_js(SilverJsCtx *sc);
static void report_exception(SilverJsCtx *sc, const char *where);
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
    sc->gen = ++g_gen_counter;
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
    set_fn(ctx, sc->global, "__silver_fetch_start",   native_fetch_start, 7);
    set_fn(ctx, sc->global, "__silver_fetch_cancel",  native_fetch_cancel, 1);
    set_fn(ctx, sc->global, "__silver_read_text",     native_read_text, 1);
    set_fn(ctx, sc->global, "__silver_utf8_decode",   native_utf8_decode, 1);
    set_fn(ctx, sc->global, "__silver_utf8_encode",   native_utf8_encode, 1);
    set_fn(ctx, sc->global, "__silver_worker_spawn",  native_worker_spawn, 5);
    set_fn(ctx, sc->global, "__silver_worker_post",   native_worker_post, 2);
    set_fn(ctx, sc->global, "__silver_worker_terminate", native_worker_terminate, 1);
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
    FILE *f = vfs_fopen_rb(path);          /* 0.5: najpierw zasoby wbudowane w binarkę */
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

static void ws_free(SilverWs *w) {
#ifdef SILVER_TLS
    if (w->ssl) { SSL_shutdown((SSL *)w->ssl); SSL_free((SSL *)w->ssl); w->ssl = NULL; }
#endif
    if (w->fd != NET_INVALID) net_close(w->fd);
    free(w->rbuf); free(w->msg);
    memset(w, 0, sizeof *w);
    w->fd = NET_INVALID;
}


/* nieblokujące I/O z opcjonalnym TLS: >0 bajty, 0 EOF, -1 EAGAIN, -2 błąd */
static net_ssize_t ws_io_recv(SilverWs *w, void *buf, size_t n) {
#ifdef SILVER_TLS
    if (w->ssl) {
        int r = SSL_read((SSL *)w->ssl, buf, (int)n);
        if (r > 0) return r;
        int e = SSL_get_error((SSL *)w->ssl, r);
        if (e == SSL_ERROR_WANT_READ || e == SSL_ERROR_WANT_WRITE) return -1;
        if (e == SSL_ERROR_ZERO_RETURN) return 0;
        return -2;
    }
#endif
    net_ssize_t r = recv(w->fd, (char *)buf, (int)n, 0);
    if (r >= 0) return r;
    if (net_would_block()) return -1;
    return -2;
}

static int ws_send_all(SilverWs *w, const unsigned char *d, size_t n) {
    size_t off = 0;
    int spins = 0;
    net_sock_t fd = w->fd;
    while (off < n) {
        net_ssize_t r; int again;
#ifdef SILVER_TLS
        if (w->ssl) {
            int sr = SSL_write((SSL *)w->ssl, d + off, (int)(n - off));
            r = sr;
            int e = sr > 0 ? 0 : SSL_get_error((SSL *)w->ssl, sr);
            again = (sr <= 0) && (e == SSL_ERROR_WANT_READ || e == SSL_ERROR_WANT_WRITE);
            if (sr <= 0 && !again) return 0;
        } else
#endif
        {
            r = send(fd, (const char *)d + off, (int)(n - off), NET_SEND_FLAGS);
            again = (r < 0 && net_would_block());
        }
        if (r > 0) { off += (size_t)r; spins = 0; continue; }
        if (again && spins++ < 2000) {
            fd_set wf; FD_ZERO(&wf); FD_SET(fd, &wf);
            struct timeval tv = { 0, 5000 };
            select((int)fd + 1, NULL, &wf, NULL, &tv);
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
    int ok = ws_send_all(w, f, p + len);
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

static void ws_send_handshake(SilverJsCtx *sc, SilverWs *w) {
    char req[1400];
                const char *slash = strchr(w->host, '/');
                char hostonly[256]; size_t hl = slash ? (size_t)(slash - w->host) : strlen(w->host);
                memcpy(hostonly, w->host, hl); hostonly[hl] = 0;
                snprintf(req, sizeof req,
                    "GET %s HTTP/1.1\r\nHost: %s\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
                    "Sec-WebSocket-Key: %s\r\nSec-WebSocket-Version: 13\r\n\r\n",
                    slash ? slash : "/", hostonly, w->key);
                if (!ws_send_all(w, (const unsigned char *)req, strlen(req))) { ws_fail(sc, w, "błąd wysyłania handshake"); return; }
                w->state = 1; w->t0 = now_ms();
            }

static void ws_poll(SilverJsCtx *sc) {
    for (int i = 0; i < SILVER_MAX_WS; i++) {
        SilverWs *w = &sc->ws[i];
        if (!w->used) continue;
        if (w->state < 0) {
#ifdef SILVER_TLS
            ws_fail(sc, w, "nie można połączyć (adres lub schemat)");
#else
            ws_fail(sc, w, "wss:// (TLS) niedostępne: zbuduj z OpenSSL (libssl-dev, SILVER_TLS=1)");
#endif
            continue;
        }
        if (w->state == 0) {
            fd_set wf; FD_ZERO(&wf); FD_SET(w->fd, &wf);
            struct timeval tv = { 0, 0 };
            if (select((int)w->fd + 1, NULL, &wf, NULL, &tv) > 0) {
                int err = 0; socklen_t el = sizeof err;
                getsockopt(w->fd, SOL_SOCKET, SO_ERROR, (char *)&err, &el);
                if (err) { ws_fail(sc, w, "nie można połączyć"); continue; }
#ifdef SILVER_TLS
                if (w->tls) {
                    SSL_CTX *tc = net_tls_ctx();
                    SSL *ssl = tc ? SSL_new(tc) : NULL;
                    if (!ssl) { ws_fail(sc, w, "TLS: brak kontekstu"); continue; }
                    SSL_set_fd(ssl, w->fd);
                    SSL_set_tlsext_host_name(ssl, w->sni);
                    SSL_set1_host(ssl, w->sni);
                    SSL_set_connect_state(ssl);
                    w->ssl = ssl; w->state = 4; w->t0 = now_ms();
                    continue;
                }
#endif
                ws_send_handshake(sc, w);
} else if (now_ms() - w->t0 > 10000) { ws_fail(sc, w, "przekroczono czas łączenia"); }
            continue;
        }
#ifdef SILVER_TLS
        if (w->state == 4) {            /* uścisk dłoni TLS (nieblokujący) */
            int hr = SSL_do_handshake((SSL *)w->ssl);
            if (hr == 1) { ws_send_handshake(sc, w); }
            else {
                int e = SSL_get_error((SSL *)w->ssl, hr);
                if (e != SSL_ERROR_WANT_READ && e != SSL_ERROR_WANT_WRITE) {
                    long vr = SSL_get_verify_result((SSL *)w->ssl);
                    char m[200];
                    if (vr != X509_V_OK) snprintf(m, sizeof m, "TLS: certyfikat odrzucony (%s)", X509_verify_cert_error_string(vr));
                    else snprintf(m, sizeof m, "TLS: błąd uścisku dłoni");
                    ws_fail(sc, w, m);
                } else if (now_ms() - w->t0 > 10000) ws_fail(sc, w, "TLS: przekroczono czas");
            }
            continue;
        }
#endif
        /* odczyt dostępnych danych */
        int eof = 0;
        for (int round = 0; round < 8 && w->used; round++) {
            if (w->rcap - w->rlen < 4096) {
                size_t nc = w->rcap ? w->rcap * 2 : 8192;
                char *nb = (char *)realloc(w->rbuf, nc);
                if (!nb) { ws_fail(sc, w, "brak pamięci"); break; }
                w->rbuf = nb; w->rcap = nc;
            }
            net_ssize_t r = ws_io_recv(w, w->rbuf + w->rlen, w->rcap - w->rlen - 1);
            if (r > 0) { w->rlen += (size_t)r; continue; }
            if (r == 0) { eof = 1; break; }   /* najpierw sparsuj ramki (np. close), potem obsłuż EOF */
            if (r == -1) break;
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
    w->used = 1; w->id = id; w->fd = NET_INVALID; w->t0 = now_ms(); net_sock_init();
    int is_wss = strncmp(url, "wss://", 6) == 0;
    if (strncmp(url, "ws://", 5) != 0 && !is_wss) { w->state = -1; JS_FreeCString(ctx, url); return JS_TRUE; }
#ifndef SILVER_TLS
    if (is_wss) { w->state = -1; JS_FreeCString(ctx, url); return JS_TRUE; }
#endif
    w->tls = is_wss;
    const char *hp = url + (is_wss ? 6 : 5);
    snprintf(w->host, sizeof w->host, "%s", hp);
    char hostbuf[256]; const char *slash = strchr(hp, '/');
    size_t hl = slash ? (size_t)(slash - hp) : strlen(hp);
    if (hl >= sizeof hostbuf) hl = sizeof hostbuf - 1;
    memcpy(hostbuf, hp, hl); hostbuf[hl] = 0;
    char port[8]; snprintf(port, sizeof port, "%s", is_wss ? "443" : "80");
    char *colon = strrchr(hostbuf, ':');
    if (colon && !strchr(colon, ']')) { *colon = 0; snprintf(port, sizeof port, "%s", colon + 1); }
    snprintf(w->sni, sizeof w->sni, "%s", hostbuf);
    unsigned char rnd[16];
    for (int i = 0; i < 16; i++) rnd[i] = (unsigned char)(rand() & 0xff);
    b64enc(rnd, 16, w->key);
    struct addrinfo hints, *res = NULL;
    memset(&hints, 0, sizeof hints);
    hints.ai_socktype = SOCK_STREAM;
    int gai = getaddrinfo(hostbuf, port, &hints, &res);
    if (gai != 0 || !res) { w->state = -1; JS_FreeCString(ctx, url); return JS_TRUE; }
    net_sock_t fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (fd == NET_INVALID) { freeaddrinfo(res); w->state = -1; JS_FreeCString(ctx, url); return JS_TRUE; }
    net_set_nonblock(fd);
    int one = 1; setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, (const char *)&one, sizeof one);
    int cr = connect(fd, res->ai_addr, (int)res->ai_addrlen);
    freeaddrinfo(res);
    if (cr != 0 && !net_in_progress()) { net_close(fd); w->state = -1; JS_FreeCString(ctx, url); return JS_TRUE; }
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


/* ====================================================================
 *  0.5: fetch asynchroniczny (wątki + silver_net.h), odczyt plików tekstowych
 * ==================================================================== */
/* __silver_fetch_start(id, method, url, headersText, body, timeoutMs, maxRedirects) -> bool
 * body: string (UTF-8) albo ArrayBuffer. Wynik wraca przez __silver_fetch_done(...) z js_tick. */
static JSValue native_fetch_start(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    (void)this_val;
    SilverJsCtx *sc = by_ctx(ctx);
    if (!sc || argc < 7) return JS_FALSE;
    int32_t id = 0, tmo = 30000, red = 20;
    JS_ToInt32(ctx, &id, argv[0]); JS_ToInt32(ctx, &tmo, argv[5]); JS_ToInt32(ctx, &red, argv[6]);
    const char *method = JS_ToCString(ctx, argv[1]);
    const char *url = JS_ToCString(ctx, argv[2]);
    const char *hdrs = JS_ToCString(ctx, argv[3]);
    const unsigned char *body = NULL; size_t blen = 0;
    const char *bstr = NULL;
    if (JS_IsString(argv[4])) { bstr = JS_ToCStringLen(ctx, &blen, argv[4]); body = (const unsigned char *)bstr; }
    else if (JS_IsObject(argv[4])) {
        size_t sz = 0; uint8_t *ab = JS_GetArrayBuffer(ctx, &sz, argv[4]);
        if (ab) { body = ab; blen = sz; } else { JSValue ex = JS_GetException(ctx); JS_FreeValue(ctx, ex); }
    }
    int ok = (method && url) ? net_job_start(sc->gen, id, method, url, hdrs ? hdrs : "", body, blen, tmo, red) : 0;
    if (method) JS_FreeCString(ctx, method);
    if (url) JS_FreeCString(ctx, url);
    if (hdrs) JS_FreeCString(ctx, hdrs);
    if (bstr) JS_FreeCString(ctx, bstr);
    return ok ? JS_TRUE : JS_FALSE;
}

/* __silver_fetch_cancel(id) */
static JSValue native_fetch_cancel(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    (void)this_val;
    SilverJsCtx *sc = by_ctx(ctx);
    if (!sc || argc < 1) return JS_UNDEFINED;
    int32_t id = 0; JS_ToInt32(ctx, &id, argv[0]);
    net_job_cancel(sc->gen, id);
    return JS_UNDEFINED;
}


/* __silver_utf8_decode(ArrayBuffer|TypedArray) -> string ; __silver_utf8_encode(string) -> ArrayBuffer */
static JSValue native_utf8_decode(JSContext *ctx, JSValueConst t, int argc, JSValueConst *argv) {
    (void)t;
    if (argc < 1) return JS_NewString(ctx, "");
    size_t sz = 0; uint8_t *p = JS_GetArrayBuffer(ctx, &sz, argv[0]);
    if (!p) { JSValue ex = JS_GetException(ctx); JS_FreeValue(ctx, ex); return JS_UNDEFINED; }
    if (sz >= 3 && p[0] == 0xEF && p[1] == 0xBB && p[2] == 0xBF) { p += 3; sz -= 3; }   /* BOM */
    return JS_NewStringLen(ctx, (const char *)p, sz);
}
static JSValue native_utf8_encode(JSContext *ctx, JSValueConst t, int argc, JSValueConst *argv) {
    (void)t;
    size_t n = 0;
    const char *s = argc > 0 ? JS_ToCStringLen(ctx, &n, argv[0]) : NULL;
    if (!s) return JS_UNDEFINED;
    JSValue r = JS_NewArrayBufferCopy(ctx, (const uint8_t *)s, n);
    JS_FreeCString(ctx, s);
    return r;
}

/* __silver_read_text(path) -> string | null (plik lokalny albo zasób wbudowany) */
static JSValue native_read_text(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    (void)this_val;
    if (argc < 1) return JS_NULL;
    const char *path = JS_ToCString(ctx, argv[0]);
    if (!path) return JS_NULL;
    size_t len = 0;
    char *src = read_whole_file(path, &len);
    JS_FreeCString(ctx, path);
    if (!src) return JS_NULL;
    JSValue r = JS_NewStringLen(ctx, src, len);
    free(src);
    return r;
}

static void fetch_deliver(SilverJsCtx *sc) {
    NetJob *j;
    while ((j = net_job_take_done(sc->gen)) != NULL) {
        JSContext *ctx = sc->ctx;
        JSValue fn = JS_GetPropertyStr(ctx, sc->global, "__silver_fetch_done");
        if (JS_IsFunction(ctx, fn)) {
            JSValue a[8];
            int okr = j->rc == 0;
            a[0] = JS_NewInt32(ctx, j->id);
            a[1] = JS_NewInt32(ctx, okr ? j->resp.status : 0);
            a[2] = JS_NewString(ctx, okr ? j->resp.status_text : "");
            a[3] = JS_NewString(ctx, okr && j->resp.headers ? j->resp.headers : "");
            a[4] = JS_NewArrayBufferCopy(ctx, okr && j->resp.body ? j->resp.body : (const uint8_t *)"", okr && j->resp.body ? j->resp.body_len : 0);
            a[5] = JS_NewString(ctx, okr && j->resp.final_url ? j->resp.final_url : j->url);
            a[6] = JS_NewBool(ctx, okr ? j->resp.redirected : 0);
            a[7] = JS_NewString(ctx, okr ? "" : (j->resp.err[0] ? j->resp.err : "błąd sieci"));
            enter_js(sc);
            JSValue ret = JS_Call(ctx, fn, sc->global, 8, a);
            if (JS_IsException(ret)) report_exception(sc, "__silver_fetch_done");
            JS_FreeValue(ctx, ret);
            for (int i = 0; i < 8; i++) JS_FreeValue(ctx, a[i]);
            leave_js(sc);
        }
        JS_FreeValue(ctx, fn);
        net_job_free(j);
    }
}

/* ====================================================================
 *  0.5: Workery w osobnych wątkach — własny JSRuntime/JSContext na wątek.
 *  Komunikacja: dwie kolejki tekstów (serializacja z silver_serial.js), odbierane z js_tick.
 * ==================================================================== */
#define SILVER_MAX_WORKERS     64
#define SILVER_WK_MAX_TIMERS   256

typedef struct WMsg { struct WMsg *next; int kind; char *data; } WMsg;   /* kind: 0 wiadomość, 1 błąd, 2 koniec */
typedef struct SilverWorker {
    uint64_t owner; int id;
    volatile int stop, closing, finished, orphan;
    net_mutex_t mu; net_cond_t cv;
    WMsg *in_h, *in_t, *out_h, *out_t;
    char *code, *name, *base; int is_module;
    JSRuntime *rt; JSContext *ctx;
    Timer timers[SILVER_WK_MAX_TIMERS];
} SilverWorker;

static SilverWorker *g_wk[SILVER_MAX_WORKERS];
static net_mutex_t g_wk_lock;
static int g_wk_ready = 0;

static void wq_push(SilverWorker *w, int out, int kind, const char *data, size_t len) {
    WMsg *m = (WMsg *)calloc(1, sizeof *m);
    if (!m) return;
    m->kind = kind;
    m->data = (char *)malloc(len + 1);
    if (!m->data) { free(m); return; }
    memcpy(m->data, data, len); m->data[len] = 0;
    net_mutex_lock(&w->mu);
    WMsg **h = out ? &w->out_h : &w->in_h, **t = out ? &w->out_t : &w->in_t;
    if (*t) (*t)->next = m; else *h = m;
    *t = m;
    net_cond_signal(&w->cv);
    net_mutex_unlock(&w->mu);
}

static void wq_free_list(WMsg *m) { while (m) { WMsg *n = m->next; free(m->data); free(m); m = n; } }

static void worker_free(SilverWorker *w) {
    wq_free_list(w->in_h); wq_free_list(w->out_h);
    free(w->code); free(w->name); free(w->base);
    free(w);
}

static SilverWorker *wk_of(JSContext *ctx) { return (SilverWorker *)JS_GetContextOpaque(ctx); }

static JSValue wn_post(JSContext *ctx, JSValueConst t, int argc, JSValueConst *argv) {
    (void)t;
    SilverWorker *w = wk_of(ctx);
    if (!w || argc < 1) return JS_UNDEFINED;
    size_t n = 0; const char *s = JS_ToCStringLen(ctx, &n, argv[0]);
    if (s) { wq_push(w, 1, 0, s, n); JS_FreeCString(ctx, s); }
    return JS_UNDEFINED;
}
static JSValue wn_error(JSContext *ctx, JSValueConst t, int argc, JSValueConst *argv) {
    (void)t;
    SilverWorker *w = wk_of(ctx);
    if (!w || argc < 1) return JS_UNDEFINED;
    int handled = argc > 1 && JS_ToBool(ctx, argv[1]);
    size_t n = 0; const char *s = JS_ToCStringLen(ctx, &n, argv[0]);
    if (s) {
        fprintf(stderr, "[worker %s] %s\n", w->name ? w->name : "", s);
        if (!handled) wq_push(w, 1, 1, s, n);
        JS_FreeCString(ctx, s);
    }
    return JS_UNDEFINED;
}
static JSValue wn_log(JSContext *ctx, JSValueConst t, int argc, JSValueConst *argv) {
    (void)t;
    SilverWorker *w = wk_of(ctx);
    if (argc < 2) return JS_UNDEFINED;
    const char *lvl = JS_ToCString(ctx, argv[0]), *txt = JS_ToCString(ctx, argv[1]);
    fprintf(stderr, "[js:%s] [worker %s] %s\n", lvl ? lvl : "log", w && w->name ? w->name : "", txt ? txt : "");
    if (lvl) JS_FreeCString(ctx, lvl);
    if (txt) JS_FreeCString(ctx, txt);
    return JS_UNDEFINED;
}
static JSValue wn_now(JSContext *ctx, JSValueConst t, int argc, JSValueConst *argv) {
    (void)t; (void)argc; (void)argv;
    return JS_NewFloat64(ctx, (double)now_ms());
}
static JSValue wn_close(JSContext *ctx, JSValueConst t, int argc, JSValueConst *argv) {
    (void)t; (void)argc; (void)argv;
    SilverWorker *w = wk_of(ctx);
    if (w) w->closing = 1;
    return JS_UNDEFINED;
}
static JSValue wn_timer_set(JSContext *ctx, JSValueConst t, int argc, JSValueConst *argv) {
    (void)t;
    SilverWorker *w = wk_of(ctx);
    if (!w || argc < 3) return JS_FALSE;
    int32_t id = 0, delay = 0;
    JS_ToInt32(ctx, &id, argv[0]); JS_ToInt32(ctx, &delay, argv[1]);
    int repeat = JS_ToBool(ctx, argv[2]);
    if (delay < 0) delay = 0;
    if (repeat && delay < 1) delay = 1;
    for (int i = 0; i < SILVER_WK_MAX_TIMERS; i++)
        if (!w->timers[i].used) {
            w->timers[i].used = 1; w->timers[i].id = id;
            w->timers[i].due = now_ms() + delay; w->timers[i].interval = repeat ? delay : 0;
            return JS_TRUE;
        }
    return JS_FALSE;
}
static JSValue wn_timer_clear(JSContext *ctx, JSValueConst t, int argc, JSValueConst *argv) {
    (void)t;
    SilverWorker *w = wk_of(ctx);
    if (!w || argc < 1) return JS_UNDEFINED;
    int32_t id = 0; JS_ToInt32(ctx, &id, argv[0]);
    for (int i = 0; i < SILVER_WK_MAX_TIMERS; i++) if (w->timers[i].used && w->timers[i].id == id) w->timers[i].used = 0;
    return JS_UNDEFINED;
}

static int is_http_url(const char *u) { return strncasecmp(u, "http://", 7) == 0 || strncasecmp(u, "https://", 8) == 0; }

/* __w_read_text(url) -> string | null */
static JSValue wn_read_text(JSContext *ctx, JSValueConst t, int argc, JSValueConst *argv) {
    (void)t;
    SilverWorker *w = wk_of(ctx);
    if (argc < 1) return JS_NULL;
    const char *url = JS_ToCString(ctx, argv[0]);
    if (!url) return JS_NULL;
    JSValue ret = JS_NULL;
    if (is_http_url(url)) {
        NetResp r;
        if (net_http_request("GET", url, "", NULL, 0, 20000, 10, w ? &w->stop : NULL, &r) == 0 && r.status >= 200 && r.status < 300 && r.body)
            ret = JS_NewStringLen(ctx, (const char *)r.body, r.body_len);
        net_resp_free(&r);
    } else {
        size_t len = 0; char *src = read_whole_file(url, &len);
        if (src) { ret = JS_NewStringLen(ctx, src, len); free(src); }
    }
    JS_FreeCString(ctx, url);
    return ret;
}

/* __w_fetch(method, url, headers, body, timeoutMs, maxRedirects) -> {status,statusText,headers,body,url,redirected} | {error} */
static JSValue wn_fetch(JSContext *ctx, JSValueConst t, int argc, JSValueConst *argv) {
    (void)t;
    SilverWorker *w = wk_of(ctx);
    JSValue o = JS_NewObject(ctx);
    if (argc < 6) { JS_SetPropertyStr(ctx, o, "error", JS_NewString(ctx, "argumenty")); return o; }
    int32_t tmo = 30000, red = 20; JS_ToInt32(ctx, &tmo, argv[4]); JS_ToInt32(ctx, &red, argv[5]);
    const char *method = JS_ToCString(ctx, argv[0]), *url = JS_ToCString(ctx, argv[1]), *hdrs = JS_ToCString(ctx, argv[2]);
    const unsigned char *body = NULL; size_t blen = 0; const char *bstr = NULL;
    if (JS_IsString(argv[3])) { bstr = JS_ToCStringLen(ctx, &blen, argv[3]); body = (const unsigned char *)bstr; }
    else if (JS_IsObject(argv[3])) { size_t sz = 0; uint8_t *ab = JS_GetArrayBuffer(ctx, &sz, argv[3]); if (ab) { body = ab; blen = sz; } else { JSValue ex = JS_GetException(ctx); JS_FreeValue(ctx, ex); } }
    if (method && url && is_http_url(url)) {
        NetResp r;
        if (net_http_request(method, url, hdrs ? hdrs : "", body, blen, tmo, red, w ? &w->stop : NULL, &r) == 0) {
            JS_SetPropertyStr(ctx, o, "status", JS_NewInt32(ctx, r.status));
            JS_SetPropertyStr(ctx, o, "statusText", JS_NewString(ctx, r.status_text));
            JS_SetPropertyStr(ctx, o, "headers", JS_NewString(ctx, r.headers ? r.headers : ""));
            JS_SetPropertyStr(ctx, o, "body", JS_NewArrayBufferCopy(ctx, r.body ? r.body : (const uint8_t *)"", r.body ? r.body_len : 0));
            JS_SetPropertyStr(ctx, o, "url", JS_NewString(ctx, r.final_url ? r.final_url : url));
            JS_SetPropertyStr(ctx, o, "redirected", JS_NewBool(ctx, r.redirected));
        } else JS_SetPropertyStr(ctx, o, "error", JS_NewString(ctx, r.err));
        net_resp_free(&r);
    } else if (url) {                                  /* plik lokalny / zasób wbudowany */
        size_t len = 0; char *src = read_whole_file(url, &len);
        if (src) {
            JS_SetPropertyStr(ctx, o, "status", JS_NewInt32(ctx, 200));
            JS_SetPropertyStr(ctx, o, "statusText", JS_NewString(ctx, "OK"));
            JS_SetPropertyStr(ctx, o, "headers", JS_NewString(ctx, ""));
            JS_SetPropertyStr(ctx, o, "body", JS_NewArrayBufferCopy(ctx, (const uint8_t *)src, len));
            JS_SetPropertyStr(ctx, o, "url", JS_NewString(ctx, url));
            JS_SetPropertyStr(ctx, o, "redirected", JS_FALSE);
            free(src);
        } else JS_SetPropertyStr(ctx, o, "error", JS_NewString(ctx, "nie znaleziono pliku"));
    }
    if (method) JS_FreeCString(ctx, method);
    if (url) JS_FreeCString(ctx, url);
    if (hdrs) JS_FreeCString(ctx, hdrs);
    if (bstr) JS_FreeCString(ctx, bstr);
    return o;
}

static int worker_interrupt(void *unused, void *opaque) {
    (void)unused;
    return ((SilverWorker *)opaque)->stop;
}

static void wk_report(SilverWorker *w, const char *where) {
    JSContext *ctx = w->ctx;
    JSValue exc = JS_GetException(ctx);
    const char *msg = JS_ToCString(ctx, exc);
    const char *stack = NULL; JSValue st = JS_UNDEFINED;
    if (JS_IsObject(exc)) { st = JS_GetPropertyStr(ctx, exc, "stack"); if (!JS_IsUndefined(st)) stack = JS_ToCString(ctx, st); }
    if (!w->stop) {
        char buf[4096];
        snprintf(buf, sizeof buf, "%s%s%s%s", msg ? msg : "(wyjątek)", stack ? "\n" : "", stack ? stack : "", where && where[0] ? "" : "");
        fprintf(stderr, "[worker %s] %s: %s\n", w->name ? w->name : "", where ? where : "", buf);
        wq_push(w, 1, 1, buf, strlen(buf));
    }
    if (msg) JS_FreeCString(ctx, msg);
    if (stack) JS_FreeCString(ctx, stack);
    JS_FreeValue(ctx, st); JS_FreeValue(ctx, exc);
}

static void wk_drain(SilverWorker *w) {
    for (;;) {
        JSContext *jc = NULL;
        int r = JS_ExecutePendingJob(w->rt, &jc);
        if (r == 0) break;
        if (r < 0) wk_report(w, "zadanie Promise");
    }
}

static void wk_call(SilverWorker *w, const char *fname, const char *a, const char *b, int nargs) {
    JSContext *ctx = w->ctx;
    JSValue g = JS_GetGlobalObject(ctx);
    JSValue fn = JS_GetPropertyStr(ctx, g, fname);
    if (JS_IsFunction(ctx, fn)) {
        JSValue args[2] = { JS_NewString(ctx, a ? a : ""), JS_NewString(ctx, b ? b : "") };
        JSValue ret = JS_Call(ctx, fn, g, nargs, args);
        if (JS_IsException(ret)) wk_report(w, fname);
        JS_FreeValue(ctx, ret);
        JS_FreeValue(ctx, args[0]); JS_FreeValue(ctx, args[1]);
    }
    JS_FreeValue(ctx, fn); JS_FreeValue(ctx, g);
    wk_drain(w);
}

static void *worker_main(void *arg) {
    SilverWorker *w = (SilverWorker *)arg;
    JSRuntime *rt = JS_NewRuntime();
    JSContext *ctx = rt ? JS_NewContext(rt) : NULL;
    if (!ctx) {
        if (rt) JS_FreeRuntime(rt);
        wq_push(w, 1, 1, "Worker: brak pamięci na środowisko JS", 38);
        goto done;
    }
    w->rt = rt; w->ctx = ctx;
    JS_SetMemoryLimit(rt, SILVER_MEM_LIMIT);
    JS_SetMaxStackSize(rt, SILVER_STACK_LIMIT);
    JS_SetInterruptHandler(rt, (JSInterruptHandler *)worker_interrupt, w);
    JS_SetModuleLoaderFunc(rt, mod_normalize, mod_loader, NULL);
    JS_SetContextOpaque(ctx, w);
    JSValue g = JS_GetGlobalObject(ctx);
    set_fn(ctx, g, "__w_post", wn_post, 1);
    set_fn(ctx, g, "__w_error", wn_error, 2);
    set_fn(ctx, g, "__w_log", wn_log, 2);
    set_fn(ctx, g, "__w_now", wn_now, 0);
    set_fn(ctx, g, "__w_close", wn_close, 0);
    set_fn(ctx, g, "__w_timer_set", wn_timer_set, 3);
    set_fn(ctx, g, "__w_timer_clear", wn_timer_clear, 1);
    set_fn(ctx, g, "__w_read_text", wn_read_text, 1);
    set_fn(ctx, g, "__w_fetch", wn_fetch, 6);
    set_fn(ctx, g, "__silver_utf8_decode", native_utf8_decode, 1);
    set_fn(ctx, g, "__silver_utf8_encode", native_utf8_encode, 1);
    JS_FreeValue(ctx, g);

    JSValue r = JS_Eval(ctx, (const char *)silver_worker_js, silver_worker_js_len, "silver_worker.js", JS_EVAL_TYPE_GLOBAL);
    if (JS_IsException(r)) { wk_report(w, "silver_worker.js"); JS_FreeValue(ctx, r); goto cleanup; }
    JS_FreeValue(ctx, r);
    wk_call(w, "__w_init", w->name ? w->name : "", w->base ? w->base : "", 2);

    if (w->is_module) {
        JSValue fv = JS_Eval(ctx, w->code, strlen(w->code), w->base ? w->base : "worker.mjs", JS_EVAL_TYPE_MODULE | JS_EVAL_FLAG_COMPILE_ONLY);
        if (JS_IsException(fv)) wk_report(w, "moduł Workera");
        else {
            JSValue res = JS_EvalFunction(ctx, fv);
            if (JS_IsException(res)) wk_report(w, "moduł Workera");
            JS_FreeValue(ctx, res);
        }
    } else {
        JSValue res = JS_Eval(ctx, w->code, strlen(w->code), w->base && w->base[0] ? w->base : "worker.js", JS_EVAL_TYPE_GLOBAL);
        if (JS_IsException(res)) wk_report(w, "skrypt Workera");
        JS_FreeValue(ctx, res);
    }
    wk_drain(w);

    while (!w->stop && !w->closing) {
        WMsg *batch = NULL;
        net_mutex_lock(&w->mu);
        if (!w->in_h) {
            int64_t now = now_ms(), wait = 100;
            for (int i = 0; i < SILVER_WK_MAX_TIMERS; i++)
                if (w->timers[i].used) { int64_t d = w->timers[i].due - now; if (d < 0) d = 0; if (d < wait) wait = d; }
            if (wait > 0) net_cond_wait_ms(&w->cv, &w->mu, (int)wait);
        }
        batch = w->in_h; w->in_h = w->in_t = NULL;
        net_mutex_unlock(&w->mu);
        for (WMsg *m = batch; m && !w->stop && !w->closing; m = m->next) wk_call(w, "__w_deliver", m->data, NULL, 1);
        wq_free_list(batch);
        int64_t now = now_ms();
        for (int i = 0; i < SILVER_WK_MAX_TIMERS && !w->stop && !w->closing; i++) {
            Timer *t = &w->timers[i];
            if (!t->used || t->due > now) continue;
            int id = t->id;
            if (t->interval > 0) t->due = now + t->interval; else t->used = 0;
            char idb[24]; snprintf(idb, sizeof idb, "%d", id);
            wk_call(w, "__w_fire_timer", idb, NULL, 1);
        }
        wk_drain(w);
    }
cleanup:
    JS_FreeContext(ctx);
    JS_FreeRuntime(rt);
    w->ctx = NULL; w->rt = NULL;
done:
    wq_push(w, 1, 2, "", 0);
    net_mutex_lock(&g_wk_lock);
    w->finished = 1;
    int orphan = w->orphan;
    net_mutex_unlock(&g_wk_lock);
    if (orphan) worker_free(w);
    return NULL;
}

static void wk_lock_ensure(void) { if (!g_wk_ready) { net_mutex_init(&g_wk_lock); g_wk_ready = 1; } }

static int wk_index(SilverJsCtx *sc, int id) {
    for (int i = 0; i < SILVER_MAX_WORKERS; i++) if (g_wk[i] && g_wk[i]->owner == sc->gen && g_wk[i]->id == id) return i;
    return -1;
}

/* __silver_worker_spawn(id, code, name, baseUrl, isModule) -> bool */
static JSValue native_worker_spawn(JSContext *ctx, JSValueConst t, int argc, JSValueConst *argv) {
    (void)t;
    SilverJsCtx *sc = by_ctx(ctx);
    if (!sc || argc < 5) return JS_FALSE;
    wk_lock_ensure();
    int slot = -1;
    for (int i = 0; i < SILVER_MAX_WORKERS; i++) if (!g_wk[i]) { slot = i; break; }
    if (slot < 0) return JS_FALSE;
    int32_t id = 0; JS_ToInt32(ctx, &id, argv[0]);
    SilverWorker *w = (SilverWorker *)calloc(1, sizeof *w);
    if (!w) return JS_FALSE;
    const char *code = JS_ToCString(ctx, argv[1]), *name = JS_ToCString(ctx, argv[2]), *base = JS_ToCString(ctx, argv[3]);
    w->owner = sc->gen; w->id = id; w->is_module = JS_ToBool(ctx, argv[4]);
    w->code = strdup(code ? code : ""); w->name = strdup(name ? name : ""); w->base = strdup(base ? base : "");
    if (code) JS_FreeCString(ctx, code);
    if (name) JS_FreeCString(ctx, name);
    if (base) JS_FreeCString(ctx, base);
    net_mutex_init(&w->mu); net_cond_init(&w->cv);
    g_wk[slot] = w;
    if (!net_thread_start(worker_main, w)) { g_wk[slot] = NULL; worker_free(w); return JS_FALSE; }
    return JS_TRUE;
}

/* __silver_worker_post(id, text) -> bool */
static JSValue native_worker_post(JSContext *ctx, JSValueConst t, int argc, JSValueConst *argv) {
    (void)t;
    SilverJsCtx *sc = by_ctx(ctx);
    if (!sc || argc < 2) return JS_FALSE;
    int32_t id = 0; JS_ToInt32(ctx, &id, argv[0]);
    int i = wk_index(sc, id);
    if (i < 0 || g_wk[i]->stop) return JS_FALSE;
    size_t n = 0; const char *s = JS_ToCStringLen(ctx, &n, argv[1]);
    if (!s) return JS_FALSE;
    wq_push(g_wk[i], 0, 0, s, n);
    JS_FreeCString(ctx, s);
    return JS_TRUE;
}

/* __silver_worker_terminate(id) */
static JSValue native_worker_terminate(JSContext *ctx, JSValueConst t, int argc, JSValueConst *argv) {
    (void)t;
    SilverJsCtx *sc = by_ctx(ctx);
    if (!sc || argc < 1) return JS_UNDEFINED;
    int32_t id = 0; JS_ToInt32(ctx, &id, argv[0]);
    int i = wk_index(sc, id);
    if (i >= 0) { SilverWorker *w = g_wk[i]; w->stop = 1; net_mutex_lock(&w->mu); net_cond_signal(&w->cv); net_mutex_unlock(&w->mu); }
    return JS_UNDEFINED;
}

static int wk_any(SilverJsCtx *sc) {
    for (int i = 0; i < SILVER_MAX_WORKERS; i++) if (g_wk[i] && g_wk[i]->owner == sc->gen) return 1;
    return 0;
}

/* odbiera komunikaty od Workerów (z js_tick) i sprząta zakończone */
static void wk_pump(SilverJsCtx *sc) {
    if (!g_wk_ready) return;
    for (int i = 0; i < SILVER_MAX_WORKERS; i++) {
        SilverWorker *w = g_wk[i];
        if (!w || w->owner != sc->gen) continue;
        int exited = 0;
        for (;;) {
            net_mutex_lock(&w->mu);
            WMsg *m = w->out_h;
            if (m) { w->out_h = m->next; if (!w->out_h) w->out_t = NULL; }
            net_mutex_unlock(&w->mu);
            if (!m) break;
            if (m->kind == 2) exited = 1;
            JSContext *ctx = sc->ctx;
            JSValue fn = JS_GetPropertyStr(ctx, sc->global, "__silver_worker_event");
            if (JS_IsFunction(ctx, fn)) {
                JSValue a[3] = { JS_NewInt32(ctx, w->id), JS_NewString(ctx, m->kind == 0 ? "message" : m->kind == 1 ? "error" : "exit"), JS_NewString(ctx, m->data) };
                enter_js(sc);
                JSValue ret = JS_Call(ctx, fn, sc->global, 3, a);
                if (JS_IsException(ret)) report_exception(sc, "__silver_worker_event");
                JS_FreeValue(ctx, ret);
                for (int k = 0; k < 3; k++) JS_FreeValue(ctx, a[k]);
                leave_js(sc);
            }
            JS_FreeValue(ctx, fn);
            free(m->data); free(m);
            if (g_wk[i] != w) break;                    /* ktoś zmienił rejestr w obsłudze zdarzenia */
        }
        if (exited && g_wk[i] == w) {
            net_mutex_lock(&g_wk_lock);
            int fin = w->finished;
            net_mutex_unlock(&g_wk_lock);
            if (fin) { g_wk[i] = NULL; worker_free(w); }
        }
    }
}

/* zamyka wszystkie Workery kontekstu (js_shutdown) */
static void wk_shutdown(SilverJsCtx *sc) {
    if (!g_wk_ready) return;
    for (int i = 0; i < SILVER_MAX_WORKERS; i++) {
        SilverWorker *w = g_wk[i];
        if (!w || w->owner != sc->gen) continue;
        w->stop = 1;
        net_mutex_lock(&w->mu); net_cond_signal(&w->cv); net_mutex_unlock(&w->mu);
    }
    int64_t until = now_ms() + 1500;
    for (int i = 0; i < SILVER_MAX_WORKERS; i++) {
        SilverWorker *w = g_wk[i];
        if (!w || w->owner != sc->gen) continue;
        for (;;) {
            net_mutex_lock(&g_wk_lock);
            int fin = w->finished;
            if (!fin && now_ms() >= until) w->orphan = 1;     /* wątek sam się posprząta po zakończeniu */
            net_mutex_unlock(&g_wk_lock);
            if (fin) { worker_free(w); break; }
            if (w->orphan) break;
            net_sleep_ms(2);
        }
        g_wk[i] = NULL;
    }
}

/* ====================================================================
 *  0.3: moduły ES (import/export) — loader z systemu plików
 * ==================================================================== */
static char *mod_normalize(JSContext *ctx, const char *base, const char *name, void *opaque) {
    (void)opaque;
    /* 0.5: moduły po URL — import('https://…') oraz importy względne wewnątrz takiego modułu */
    if (base && is_http_url(base) && (name[0] == '.' || name[0] == '/') && !is_http_url(name)) {
        NetUrl bu;
        if (net_url_parse(base, &bu)) {
            char *j = net_url_join(&bu, name);
            net_url_free(&bu);
            if (j) { char *r = js_strdup(ctx, j); free(j); return r; }
        }
    }
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
    char *src = NULL;
    char *alt = NULL;
    if (is_http_url(name)) {
        const char *off = getenv("SILVER_NO_REMOTE_IMPORT");
        if (off && off[0] == '1') { JS_ThrowReferenceError(ctx, "import po URL wyłączony (SILVER_NO_REMOTE_IMPORT=1): '%s'", name); return NULL; }
        NetResp r;
        if (net_http_request("GET", name, "Accept: text/javascript, application/javascript, */*\n", NULL, 0, 20000, 10, NULL, &r) != 0
            || r.status < 200 || r.status >= 300 || !r.body) {
            JS_ThrowReferenceError(ctx, "nie można pobrać modułu '%s' (%s)", name, r.err[0] ? r.err : "błąd HTTP");
            net_resp_free(&r);
            return NULL;
        }
        src = (char *)r.body; len = r.body_len; r.body = NULL;       /* nb_add gwarantuje końcowe \0 */
        net_resp_free(&r);
    } else src = read_whole_file(name, &len);
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
    fetch_deliver(sc);
    wk_pump(sc);
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
    if ((net_jobs_pending(sc->gen) > 0 || wk_any(sc)) && (best < 0 || best > 8)) best = 8;   /* wyniki fetch i komunikaty Workerów */
    return (int)best;
}

int js_has_jobs(int h) { return VALID(h) ? (JS_IsJobPending(g_ctx[h].rt) ? 1 : 0) : 0; }

/* 0.5 (testy): kopiuje globalną zmienną JS (jako tekst) do bufora; zwraca 1, gdy istnieje */
int js_get_global_string(int h, const char *name, char *out, int cap) {
    if (!VALID(h) || !name || cap < 1) return 0;
    SilverJsCtx *sc = &g_ctx[h];
    JSValue v = JS_GetPropertyStr(sc->ctx, sc->global, name);
    int ok = 0;
    if (!JS_IsUndefined(v)) {
        const char *c = JS_ToCString(sc->ctx, v);
        if (c) { snprintf(out, (size_t)cap, "%s", c); JS_FreeCString(sc->ctx, c); ok = 1; }
    }
    JS_FreeValue(sc->ctx, v);
    return ok;
}

const char *js_last_error(int h) { return VALID(h) ? g_ctx[h].last_error : ""; }

int js_shutdown(int h) {
    if (!VALID(h)) return 0;
    SilverJsCtx *sc = &g_ctx[h];
    while (sc->count > 0) js_poll_invoke(h);
    ws_close_all(sc);
    wk_shutdown(sc);
    net_jobs_discard(sc->gen);
    free(sc->poll_buf);
    JS_FreeValue(sc->ctx, sc->global);
    flush_rejections(sc->ctx, 1);
    JS_FreeContext(sc->ctx);
    JS_FreeRuntime(sc->rt);
    memset(sc, 0, sizeof(*sc));
    return 1;
}
