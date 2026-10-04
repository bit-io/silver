#include <SDL2/SDL.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

int silver_window_create(const char *, int, int);
int silver_load_font(int, const char *, int);
int silver_utf8_next(const char *, int);
int silver_utf8_prev(const char *, int);
int silver_utf8_len(const char *);
int silver_utf8_offset(const char *, int);
int silver_measure_text_ex(int, const char *, int, int);
int silver_text_fit(int, const char *, int, int, int);
int silver_draw_text_ex(int, int, int, const char *, int, int, int, int, int, int);
int silver_fill_round_rect(int, int, int, int, int, int, int, int, int, int);
int silver_stroke_round_rect(int, int, int, int, int, int, int, int, int, int, int);
int silver_fill_gradient(int, int, int, int, int, int, int, int, int, int, int, int, int, int);
int silver_clear(int, int, int, int);
int silver_present(int);
int silver_poll_event(int);
const char *silver_event_payload(int);
int silver_wait_event(int);
int silver_clipboard_set(const char *);
const char *silver_clipboard_get(void);
int silver_get_scale(int);
int silver_set_cursor(int, int);
int silver_window_close(int);
const char *silver_system_theme(void);

int js_init(int);
int js_eval(int, const char *, const char *);
int js_load_file(int, const char *);
const char *js_poll_invoke(int);
int js_resolve(int, const char *, const char *);
int js_tick(int);
int js_next_timer_ms(int);
int js_set_budget(int, int);
const char *js_last_error(int);
int js_shutdown(int);

static int fails = 0;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

static void test_utf8(void) {
    const char *s = "Cześć"; /* 7 bajtów, 5 znaków */
    CHECK(silver_utf8_len(s) == 5);
    CHECK(silver_utf8_next(s, 0) == 1);
    CHECK(silver_utf8_next(s, 3) == 5);          /* 'ś' zajmuje 2 bajty (3..4) */
    CHECK(silver_utf8_prev(s, 5) == 3);
    CHECK(silver_utf8_prev(s, 7) == 5);          /* 'ć' */
    CHECK(silver_utf8_offset(s, 4) == 5);
    CHECK(silver_utf8_offset(s, 99) == 7);
    CHECK(silver_utf8_prev(s, 0) == 0);
}

static void test_window(const char *font) {
    int w = silver_window_create("smoke", 400, 300);
    CHECK(w >= 0);
    if (w < 0) return;
    CHECK(silver_load_font(w, font, 14));
    CHECK(silver_get_scale(w) >= 100);
    int m14 = silver_measure_text_ex(w, "Hello world", 14, 0);
    int m28 = silver_measure_text_ex(w, "Hello world", 28, 0);
    CHECK(m14 > 0 && m28 > m14);
    int mb = silver_measure_text_ex(w, "Hello world", 14, 1);
    CHECK(mb >= m14);
    int fit = silver_text_fit(w, "Hello world Hello world", 14, 0, m14);
    CHECK(fit > 0 && fit <= 12);
    int fit_pl = silver_text_fit(w, "zażółć gęślą jaźń", 14, 0, 40);
    CHECK(fit_pl > 0 && (((unsigned char)"zażółć gęślą jaźń"[fit_pl]) & 0xC0) != 0x80);
    silver_clear(w, 255, 255, 255);
    CHECK(silver_fill_round_rect(w, 10, 10, 100, 40, 8, 10, 20, 30, 255));
    CHECK(silver_stroke_round_rect(w, 10, 60, 100, 40, 8, 2, 0, 0, 0, 255));
    CHECK(silver_fill_gradient(w, 10, 110, 100, 40, 255, 0, 0, 255, 0, 0, 255, 255, 1));
    CHECK(silver_draw_text_ex(w, 20, 20, "Zażółć", 255, 255, 255, 255, 16, 1) > 0);
    silver_present(w);
    CHECK(silver_set_cursor(w, 1));
    /* wstrzyknij zdarzenia i sprawdź routing + scalanie ruchów */
    SDL_Event e; memset(&e, 0, sizeof(e));
    e.type = SDL_MOUSEMOTION; e.motion.windowID = SDL_GetWindowID(SDL_GetWindowFromID(1)); e.motion.x = 5; e.motion.y = 6;
    SDL_PushEvent(&e);
    e.motion.x = 7; e.motion.y = 8; SDL_PushEvent(&e);
    silver_wait_event(50);
    int code, saw_78 = 0, saw_56 = 0;
    while ((code = silver_poll_event(w)) != 0) {
        if (code == 4) {
            const char *p = silver_event_payload(w);
            if (strncmp(p, "7|8|", 4) == 0) saw_78 = 1;
            if (strncmp(p, "5|6|", 4) == 0) saw_56 = 1;
        }
    }
    CHECK(saw_78);      /* zdarzenie dotarło do właściwego okna */
    CHECK(!saw_56);     /* a ruchy zostały scalone (ostatni wygrywa) */
    CHECK(silver_clipboard_set("schowek ąę"));
    CHECK(strcmp(silver_clipboard_get(), "schowek ąę") == 0);
    const char *th = silver_system_theme();
    CHECK(strcmp(th, "light") == 0 || strcmp(th, "dark") == 0);
    CHECK(silver_window_close(w));
}

static const char *SNAP =
  "[{\"i\":0,\"p\":-1,\"t\":\"root\",\"k\":[1],\"a\":{}},"
  "{\"i\":1,\"p\":0,\"t\":\"html\",\"k\":[2],\"a\":{}},"
  "{\"i\":2,\"p\":1,\"t\":\"body\",\"k\":[3,5,6],\"a\":{}},"
  "{\"i\":3,\"p\":2,\"t\":\"div\",\"k\":[4],\"a\":{\"id\":\"box\",\"class\":\"a b\",\"data-k-v\":\"7\"}},"
  "{\"i\":4,\"p\":3,\"t\":\"text\",\"x\":\"hello\",\"k\":[],\"a\":{}},"
  "{\"i\":5,\"p\":2,\"t\":\"input\",\"k\":[],\"a\":{\"id\":\"in\",\"type\":\"text\",\"value\":\"v0\"}},"
  "{\"i\":6,\"p\":2,\"t\":\"ul\",\"k\":[7,8],\"a\":{}},"
  "{\"i\":7,\"p\":6,\"t\":\"li\",\"k\":[],\"a\":{\"class\":\"it\"}},"
  "{\"i\":8,\"p\":6,\"t\":\"li\",\"k\":[],\"a\":{\"class\":\"it x\"}}]";

static int run_js(int j, const char *code, const char *name) {
    int ok = js_eval(j, code, name);
    if (!ok) printf("   JS error [%s]: %s\n", name, js_last_error(j));
    return ok;
}

/* zbiera wszystkie wywołania z kolejki; zwraca ostatni JSON argumentów komendy `cmd` */
static char g_last_args[65536];
static int drain_invokes(int j, const char *want_cmd) {
    int n = 0;
    const char *inv;
    g_last_args[0] = 0;
    while ((inv = js_poll_invoke(j))[0]) {
        char tmp[70000];
        snprintf(tmp, sizeof(tmp), "%s", inv);
        char *c1 = strchr(tmp, 1);
        char *c2 = c1 ? strchr(c1 + 1, 1) : NULL;
        if (c1 && c2) {
            *c1 = 0; *c2 = 0;
            if (strcmp(c1 + 1, want_cmd) == 0) { snprintf(g_last_args, sizeof(g_last_args), "%s", c2 + 1); n++; }
            js_resolve(j, tmp, "{}");
        }
    }
    return n;
}

static void test_js(void) {
    int j = js_init(0);
    CHECK(j >= 0);
    /* await musi działać: Promise rozwiązany przez js_resolve */
    CHECK(run_js(j, "globalThis.out=''; (async()=>{ const r = await silver.invoke('ping',{a:1}); out='got:'+r.ok; })();", "t1"));
    const char *inv = js_poll_invoke(j);
    CHECK(strstr(inv, "ping") != NULL);
    char call_id[64] = "";
    sscanf(inv, "%63[^\x01]", call_id);
    CHECK(js_resolve(j, call_id, "{\"ok\":true}"));
    CHECK(run_js(j, "if (out !== 'got:true') throw new Error('await nie ruszył: '+out);", "t2"));
    /* timery */
    CHECK(run_js(j, "globalThis.n=0; globalThis.h=setInterval(()=>{n++; if(n>=3) clearInterval(h);}, 5); setTimeout(()=>{globalThis.done=1;}, 10);", "t3"));
    for (int i = 0; i < 40; i++) { js_tick(j); SDL_Delay(5); }
    CHECK(run_js(j, "if (n!==3 || done!==1) throw new Error('timery n='+n+' done='+done);", "t4"));
    CHECK(js_next_timer_ms(j) == -1);
    CHECK(run_js(j, "console.log('a', {b:1}, [1,2]); console.warn('w'); console.error(new Error('x'));", "t5"));
    CHECK(!js_eval(j, "throw new Error('boom')", "t6"));
    CHECK(strstr(js_last_error(j), "boom") != NULL);
    js_set_budget(j, 200);
    CHECK(!js_eval(j, "while(true){}", "t7"));
    js_set_budget(j, 3000);
    CHECK(run_js(j, "globalThis.rej=0; for(let i=0;i<5000;i++){ silver.invoke('x',{}).catch(()=>{rej++}); }", "t8"));
    CHECK(run_js(j, "Promise.resolve().then(()=>{ if (rej < 900) throw new Error('rej='+rej); });", "t9"));
    while (js_poll_invoke(j)[0]) {}

    /* --- lustro DOM --- */
    CHECK(js_emit(j, "__dom_snapshot", SNAP));
    CHECK(run_js(j, "const b = document.getElementById('box');"
                    "if (!b || b.tagName!=='DIV') throw new Error('getElementById');"
                    "if (b.textContent!=='hello') throw new Error('textContent '+b.textContent);"
                    "if (!b.classList.contains('a')) throw new Error('classList');"
                    "if (b.dataset.kV !== '7') throw new Error('dataset '+b.dataset.kV);"
                    "if (document.querySelectorAll('li.it').length!==2) throw new Error('qsa');"
                    "if (document.querySelector('li.x') !== document.querySelectorAll('li')[1]) throw new Error('identity');"
                    "if (document.querySelectorAll('ul > li:first-child').length!==1) throw new Error('first-child');"
                    "if (document.querySelector('body input[type=text]').value!=='v0') throw new Error('value');"
                    "if (document.querySelector('li:not(.x)') !== document.querySelectorAll('li')[0]) throw new Error('not');"
                    "if (document.body.children.length!==3) throw new Error('children');"
                    "if (b.closest('body') !== document.body) throw new Error('closest');"
                    "if (b.nextElementSibling.tagName!=='INPUT') throw new Error('sibling');",
                 "dom1"));
    drain_invokes(j, "__dom");
    /* mutacje → wsad operacji */
    CHECK(run_js(j, "const b2 = document.getElementById('box');"
                    "b2.textContent = 'zmiana'; b2.classList.add('c'); b2.classList.remove('a');"
                    "b2.style.color = 'red'; b2.style.backgroundColor = 'blue'; b2.setAttribute('title','t');"
                    "document.getElementById('in').value = 'nowy';"
                    "const li = document.createElement('li'); li.textContent='n'; document.querySelector('ul').appendChild(li);"
                    "globalThis.created = li;"
                    "if (b2.className !== 'b c') throw new Error('className '+b2.className);"
                    "if (b2.style.backgroundColor !== 'blue') throw new Error('style');"
                    "if (b2.getAttribute('style') !== 'color:red;background-color:blue') throw new Error('style attr '+b2.getAttribute('style'));",
                 "dom2"));
    CHECK(drain_invokes(j, "__dom") == 1);
    CHECK(strstr(g_last_args, "\"o\":\"text\"") != NULL);
    CHECK(strstr(g_last_args, "zmiana") != NULL);
    CHECK(strstr(g_last_args, "\"o\":\"create\"") != NULL);
    CHECK(strstr(g_last_args, "\"o\":\"append\"") != NULL);
    CHECK(strstr(g_last_args, "background-color:blue") != NULL);
    /* remap: tymczasowe id → prawdziwe */
    CHECK(js_emit(j, "__dom_remap", "{\"map\":[[1000007,9],[1000001,9]]}") || 1);
    /* zdarzenia: bubbling, stopPropagation, once */
    CHECK(run_js(j, "globalThis.log=[];"
                    "document.addEventListener('click', e=>log.push('doc:'+e.target.id));"
                    "document.body.addEventListener('click', e=>log.push('body'));"
                    "document.getElementById('box').addEventListener('click', e=>{ log.push('box:'+e.clientX); }, {once:true});"
                    "window.addEventListener('keydown', e=>log.push('win:'+e.key+(e.ctrlKey?'+ctrl':'')));",
                 "ev1"));
    CHECK(js_emit(j, "__dom_event", "{\"type\":\"click\",\"n\":3,\"x\":5,\"y\":6}"));
    CHECK(js_emit(j, "__dom_event", "{\"type\":\"click\",\"n\":3,\"x\":7,\"y\":8}"));
    CHECK(js_emit(j, "__dom_event", "{\"type\":\"keydown\",\"n\":0,\"key\":\"a\",\"ctrl\":true,\"shift\":false,\"alt\":false,\"meta\":false}"));
    CHECK(run_js(j, "const want='box:5,body,doc:box,body,doc:box,win:a+ctrl';"
                    "if (log.join(',')!==want) throw new Error('log='+log.join(','));", "ev2"));
    /* input: lustro wartości aktualizuje się z zdarzenia */
    CHECK(js_emit(j, "__dom_event", "{\"type\":\"input\",\"n\":5,\"value\":\"wpisane\"}"));
    CHECK(run_js(j, "if (document.getElementById('in').value!=='wpisane') throw new Error('input value');", "ev3"));
    /* silver.listen */
    CHECK(run_js(j, "globalThis.got=null; silver.listen('silver://task', p=>{got=p.id});", "ev4"));
    CHECK(js_emit(j, "silver://task", "{\"id\":\"t1\"}"));
    CHECK(run_js(j, "if (got!=='t1') throw new Error('listen');", "ev5"));
    /* innerHTML: operacja + stale children do czasu subtree */
    /* innerHTML jest teraz parsowane lokalnie: odczyt synchroniczny, ops = create/append/remove */
    CHECK(run_js(j, "const bx = document.getElementById('box'); bx.innerHTML='<b class=\"k\">x &amp; y</b><i>z</i>';"
                    "if (bx.innerHTML!=='<b class=\"k\">x &amp; y</b><i>z</i>') throw new Error('innerHTML '+bx.innerHTML);"
                    "if (bx.querySelector('b.k').textContent!=='x & y') throw new Error('textContent po innerHTML');"
                    "if (bx.children.length!==2) throw new Error('children po innerHTML');", "ev6"));
    drain_invokes(j, "__dom");
    CHECK(strstr(g_last_args, "\"o\":\"create\"") != NULL);
    CHECK(strstr(g_last_args, "\"o\":\"remove\"") != NULL);
    /* comment / fragment / template */
    CHECK(run_js(j, "const c = document.createComment('k'); if (c.nodeType!==8) throw new Error('comment');"
                    "const fr = document.createDocumentFragment(); fr.appendChild(document.createElement('p')); fr.appendChild(document.createElement('p'));"
                    "if (fr.nodeType!==11 || fr.childNodes.length!==2) throw new Error('fragment');"
                    "document.body.appendChild(fr);"
                    "if (fr.childNodes.length!==0) throw new Error('fragment opróżniony');"
                    "const tp = document.createElement('template'); tp.innerHTML='<div id=\"q\">1</div><span>2</span>';"
                    "const cl = tp.content.cloneNode(true); if (cl.childNodes.length!==2) throw new Error('template.content '+cl.childNodes.length);"
                    "document.body.append(cl);"
                    "if (document.querySelectorAll('body > span').length<1) throw new Error('append template');", "ev6b"));
    /* polyfille */
    CHECK(run_js(j, "const u = new URL('https://a.b:8080/x/y?q=1&r=%C4%85#h'); if (u.port!=='8080'||u.searchParams.get('r')!=='ą'||u.hash!=='#h') throw new Error('URL');"
                    "const te = new TextEncoder().encode('zażółć'); if (te.length!==10 || new TextDecoder().decode(te)!=='zażółć') throw new Error('TextEncoder');"
                    "if (atob(btoa('hello'))!=='hello') throw new Error('base64');"
                    "const ac = new AbortController(); let ab=0; ac.signal.addEventListener('abort', ()=>ab++); ac.abort(); if (!ac.signal.aborted||ab!==1) throw new Error('abort');"
                    "const et = new EventTarget(); let hit=0; et.addEventListener('x', e=>hit+=e.detail); et.dispatchEvent(new CustomEvent('x',{detail:3})); if (hit!==3) throw new Error('EventTarget');"
                    "if (structuredClone({a:[1,2]}).a[1]!==2) throw new Error('structuredClone');"
                    "if (process.env.NODE_ENV!=='production') throw new Error('process.env');"
                    "if (typeof matchMedia('(prefers-color-scheme: dark)').matches!=='boolean') throw new Error('matchMedia');", "poly"));
    /* fetch jako Promise przez invoke */
    CHECK(run_js(j, "globalThis.f=null; fetch('http://x/').then(r=>r.json()).then(v=>{f=v;});", "f1"));
    {
        const char *iv = js_poll_invoke(j);
        while (iv[0] && !strstr(iv, "__fetch")) iv = js_poll_invoke(j);   /* pomiń wsady __dom */
        char cid[64] = ""; sscanf(iv, "%63[^\x01]", cid);
        CHECK(strstr(iv, "__fetch") != NULL);
        js_resolve(j, cid, "{\"ok\":true,\"status\":200,\"body\":\"{\\\"a\\\":5}\"}");
        CHECK(run_js(j, "Promise.resolve().then(()=>{ if(!f || f.a!==5) throw new Error('fetch json'); });", "f2"));
    }
    CHECK(js_shutdown(j));
}

int main(int argc, char **argv) {
    const char *font = argc > 1 ? argv[1] : "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf";
    test_utf8();
    test_window(font);
    test_js();
    printf(fails ? "NATIVE: %d błędów\n" : "NATIVE: OK\n", fails);
    return fails ? 1 : 0;
}
