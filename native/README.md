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
