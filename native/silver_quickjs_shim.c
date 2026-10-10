#include <quickjs.h>
#include "silver_api_embed.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

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
} SilverJsCtx;

static SilverJsCtx g_ctx[SILVER_MAX_CTX];
static int eval_buf(SilverJsCtx *sc, const char *src, size_t len, const char *name);

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
    snprintf(sc->last_error, sizeof(sc->last_error), "%s: %s%s%s", where,
             msg ? msg : "(wyjątek)", stack ? "\n" : "", stack ? stack : "");
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

/* js_load_file(handle, path) -> bool. Kolejność: silver_api.js, potem app.js */
int js_load_file(int h, const char *path) {
    if (!VALID(h) || !path) return 0;
    size_t len = 0;
    char *src = read_whole_file(path, &len);
    if (!src) {
        snprintf(g_ctx[h].last_error, sizeof(g_ctx[h].last_error), "nie można otworzyć %s", path);
        return 0;
    }
    int ok = eval_buf(&g_ctx[h], src, len, path);
    free(src);
    return ok;
}

/* js_eval(handle, code, name) -> bool (kod z pamięci, np. wbudowane API) */
int js_eval(int h, const char *code, const char *name) {
    if (!VALID(h) || !code) return 0;
    return eval_buf(&g_ctx[h], code, strlen(code), name && name[0] ? name : "<eval>");
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
    return (int)best;
}

int js_has_jobs(int h) { return VALID(h) ? (JS_IsJobPending(g_ctx[h].rt) ? 1 : 0) : 0; }

const char *js_last_error(int h) { return VALID(h) ? g_ctx[h].last_error : ""; }

int js_shutdown(int h) {
    if (!VALID(h)) return 0;
    SilverJsCtx *sc = &g_ctx[h];
    while (sc->count > 0) js_poll_invoke(h);
    free(sc->poll_buf);
    JS_FreeValue(sc->ctx, sc->global);
    flush_rejections(sc->ctx, 1);
    JS_FreeContext(sc->ctx);
    JS_FreeRuntime(sc->rt);
    memset(sc, 0, sizeof(*sc));
    return 1;
}
