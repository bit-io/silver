# Warstwa natywna

Trzy statyczne biblioteki (linkowane przez `bit`; **bez `.so` i bez `LD_LIBRARY_PATH`**):

| Plik | Źródło | Zawartość |
|---|---|---|
| `libsilvershim.a` | `silver_shim.c` | okno, zdarzenia (kolejki per okno), rysowanie, fonty, cache tekstu/obrazów, HiDPI, schowek, kursory, UTF-8, motyw |
| `libsilverjs.a` | `silver_quickjs_shim.c` (+ quickjs-ng) | most JS ↔ H#: kolejka wywołań, timery, mikrozadania, limity; wbudowane `silver_polyfills.js` + `silver_api.js` |
| `libsilverdialogs.a` | `silver_dialogs_shim.c` | dialogi plików/komunikatów (zenity/kdialog, osascript, PowerShell) |

## Budowa
`bit build` robi to sam (`[build] native => sh native/build.sh`). Ręcznie:
```sh
sh native/build.sh                      # → native/build/*.a
QUICKJS_DIR=/ścieżka/do/quickjs sh native/build.sh   # własne źródła QuickJS (inaczej pobiera quickjs-ng z GitHuba)
```
Zmienne: `CC`, `AR`, `CFLAGS`, `QUICKJS_DIR`, `QUICKJS_REF`. Zależności: SDL2, SDL2_ttf, SDL2_image (`sdl2-config`/`pkg-config`).

Po edycji `src/js/*.js` wygeneruj osadzone API: `python3 native/gen_api_embed.py` (build.sh robi to automatycznie, gdy jest python3).

## Linkowanie w pełni statyczne (h# compile bez `--dynamic`)
Błąd `cannot find -lSDL2 / -lSDL2_ttf / -lSDL2_image` oznacza brak bibliotek `.a`. Rozwiązanie:
```sh
sh native/build-static-deps.sh     # buduje statyczne SDL2/ttf/image → native/build/deps (pobiera źródła z GitHuba)
sh native/build.sh                 # generuje pkgconfig-static/
. native/env.sh static             # LIBRARY_PATH, PKG_CONFIG_PATH, HSHARP_LDFLAGS dla trybu statycznego
h# compile src/main.h#             # statycznie
. native/env.sh                    # (nowa powłoka) powrót do trybu dynamicznego: h# compile ... --dynamic
```
Wymaga pakietów dev ze statycznymi `.a`: `libfreetype-dev libpng-dev zlib1g-dev libbz2-dev libbrotli-dev libx11-dev libxext-dev libxau-dev libxdmcp-dev libxcb1-dev`.
Pełny statyk z glibc nie pozwala na `dlopen`, więc SDL ma X11 wlinkowany na stałe (działa też pod XWayland), bez Wayland/KMS/Pulse/ALSA.
Statyczny SDL ma wkompilowane OpenGL/EGL oraz sterowniki `offscreen` i `dummy`. Statyczny glibc nie umie jednak
`dlopen`-ować sterowników GL hosta (Mesa → libLLVM) — próba użycia GL kończyła się `SIGSEGV` / `double free or
corruption` w `SDL_CreateRenderer`. Dlatego w trybie statycznym (`libsilvershim_static.a`) renderer jest domyślnie
programowy; GL włączysz świadomie: `SILVER_RENDERER=accelerated build/main` (może się wywalić — zależy od sterowników hosta).
`SILVER_RENDERER=software` wymusza renderer programowy także w buildzie dynamicznym.
Po zmianie flag skrypt sam przebuduje zależności (stamp), albo wymuś: `FORCE=1 sh native/build-static-deps.sh`.
Samodzielny test shima (bez h# i bez aplikacji): `sh native/selftest.sh` buduje `native/build/silver_selftest`;
uruchom go na docelowej maszynie: `SILVER_DEBUG=1 native/build/silver_selftest` — wypisuje 6 kroków (czcionka, okno,
font, 120 klatek, zamknięcie), więc widać, na którym ewentualnie pada proces.
Zmienne środowiskowe shima: `SILVER_FONT=/ścieżka/font.ttf` (własna czcionka; domyślnie szukanie w
`/usr/share/fonts`, `~/.local/share/fonts` rekurencyjnie), `SILVER_RENDERER=software|accelerated`,
`SILVER_DEBUG=1` (na stderr: sterownik wideo, renderer, użyta czcionka). `offscreen` w SDL 2.30 działa tylko z GL/EGL,
więc w buildzie statycznym shim odmawia go z czytelnym błędem (zamiast segfaulta SDL); `dummy` działa.
Ostrzeżenia linkera o `getaddrinfo`/`getpwuid` są nieszkodliwe, jeśli uruchamiasz na tej samej wersji glibc.

## Testy
```sh
sh native/tests/run.sh       # test dymny: UTF-8, okno, fonty, rysowanie, zdarzenia, schowek, QuickJS, DOM, polyfille (Xvfb)
sh native/tests/run-js.sh    # szablony TS/Svelte zbudowane npm+esbuild, uruchomione w QuickJS
```

## Funkcje (wybór)
Okno: `silver_window_create`, `…_set_min_size/fullscreen/icon/opacity`, `silver_get_scale` (100 = 1x, 200 = HiDPI).
Zdarzenia: `silver_wait_event(ms)`, `silver_poll_event(h)`, `silver_event_payload(h)` — kody i format payloadu w nagłówku `silver_shim.c`.
Rysowanie: `silver_fill_round_rect`, `silver_stroke_round_rect`, `silver_fill_gradient`, `silver_clip_set`, `silver_draw_text_ex`
(rozmiar + styl), `silver_text_fit` (zawijanie), `silver_draw_image`. UTF-8: `silver_utf8_prev/next/len/offset`.
JS: `js_init` (ładuje wbudowane API), `js_load_file`, `js_eval`, `js_poll_invoke`, `js_resolve/reject`, `js_emit`, `js_tick`,
`js_next_timer_ms`, `js_set_budget`, `js_last_error`.
