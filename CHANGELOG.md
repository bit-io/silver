# Changelog

## Unreleased
- `SILVER_DEBUG=3`: zrzut pierwszych 14 pudełek layoutu (węzeł, tag, klasa, rodzaj, rozmiar, `display`, tekst) i
  pierwszych 40 węzłów DOM, pierwszych 8 reguł arkusza (selektor, liczba deklaracji) i deklaracji dopasowanych do `body` na stderr — do diagnozy „DOM urósł, a layout niemal pusty”.
- `src/app.h#` (`pump_js`): pętla odbioru wywołań JS sterowana liczbą oczekujących wywołań
  (`js_pending_invokes`) zamiast porównania napisu z `""`; ślady `pump_js: poll #N` na stderr (SILVER_DEBUG>=2).
  Powód: w skompilowanym programie `js_poll_invoke` nie był w ogóle wołany, mimo trzech oczekujących wywołań.
- Diagnostyka (`SILVER_DEBUG>=2`): ślad `[silver-js] tick #N` w `js_tick` i `h#: pump_js: …` przy niepustej kolejce
  wywołań JS — rozróżnia „pump_js się nie wykonuje” od „wykonuje się, ale nie odbiera”.
- `js_debug` (FFI C + `js_bridge.h#`): ślad z kodu H# na niebuforowany stderr (SILVER_DEBUG>=2); użyty w
  `handle_js_invoke` i `apply_dom_ops`. Dodany też ślad `js_reject` w shimie QuickJS.
- `native/silver_quickjs_shim.c`: przy `SILVER_DEBUG>=2` ślad komunikacji H# <-> JS na niebuforowanym stderr
  (`[silver-js] emit/poll_invoke/resolve`). Stdout z `h#` jest buforowany, więc logi `log::info` przychodzą późno.
- `native/silver_quickjs_shim.c`: raportowanie nieobsłużonych odrzuceń Promise na stderr
  (`[silver-js] nieobsłużone odrzucenie Promise: …`). Wcześniej wyjątek w `async` (np. start Svelte/React)
  znikał bez śladu i aplikacja po prostu się nie montowała. Raport po opróżnieniu kolejki zadań, więc
  `.catch()` podpięte w tej samej turze nie daje fałszywych alarmów.
- `src/app.h#` (`SILVER_DEBUG`): wynik `js_load_file`, kolejka wywołań JS przy pustym `js_poll_invoke`,
  liczba części rozdzielonego wywołania (przed wczesnym wyjściem).
- `src/app.h#` (`SILVER_DEBUG`): log każdej komendy JS → H# (`silver[debug]: JS -> H# invoke '<cmd>'`).
- **Naprawa (poważna): pusty ekran przy `app::load_js` bez `<script>` w HTML.** `start_js_if_needed` kończyła się
  od razu, gdy kontekst JS już istniał (a `load_js` tworzy go wcześniej), więc `DOMContentLoaded`/`load` nigdy
  nie trafiały do JS, a `document.readyState` zostawał `"loading"`. Bundle czekający na te zdarzenia
  (Svelte/Vite/React) nie montował interfejsu. Nowe pole `App.js_ready`; zdarzenia idą raz na kontekst.
- `src/app.h#`: diagnostyka silnika pod `SILVER_DEBUG` (stdout): po `load_html` liczba bajtów HTML, węzłów DOM
  (po parse / po template), reguł CSS i tytuł; po `relayout` viewport, węzły DOM i liczba pudełek layoutu.
  Dodatkowo stan JS (`<script src>`, inline, `load_js`, handle), treść HTML i liczba reguł CSS przy layoucie.
  Służy do ustalenia, na którym etapie znika zawartość przy pustym oknie.
- `src/engine/jsonval.h#` (`stringify`, `get`, `obj_set`): typowane zmienne `let e: JsonEntry = entries[i]`.
  Wcześniej kompilator h# nie znał typu `entries[i]` (element wariantu enum) i zgadywał strukturę po nazwie
  pola `.val` iterując po HashMap (losowa kolejność). `.val` jest też w `CssDecl`, `AttrSel` (inny indeks!)
  i `Track`, więc odczyt mógł trafić w złe pole → śmieci/crash zależnie od kompilacji.
- `SILVER_DEBUG=2`: licznik wywołań rysowania na klatkę (diagnostyka pustego ekranu).
- Naprawa SIGSEGV w `SDL_CreateRenderer` w buildzie statycznym na X11: SDL próbował framebuffera z
  akcelerowanej tekstury (dlopen GL); shim ustawia teraz `SDL_FRAMEBUFFER_ACCELERATION=0` i renderer `software`.
  Dodano `native/selftest.c` + `native/selftest.sh` (samodzielny test okna/czcionki/rysowania).
- `silver_shim.c`: odnajdywanie czcionek także poza Debianem (Fedora/Bazzite/Arch: rekurencyjny skan
  `/usr/share/fonts`, `~/.local/share/fonts`), `SILVER_FONT`, `SILVER_RENDERER`, `SILVER_DEBUG`; ostrzeżenie przy cichym
  fallbacku na `dummy`/`offscreen`; build statyczny domyślnie renderuje programowo — wykrywane w czasie działania (słaby symbol `_DYNAMIC`),
  więc działa też gdy linker weźmie zwykły `libsilvershim.a` (np. brak pkg-config).
- Linkowanie w pełni statyczne: `native/build-static-deps.sh` (statyczne SDL2/SDL2_ttf/SDL2_image),
  `. native/env.sh static`, `pkgconfig-static/`, komenda `bit native-static`. Naprawia
  `cannot find -lSDL2 / -lSDL2_ttf / -lSDL2_image` przy `h# compile` bez `--dynamic`.

## 0.2.0

Wydanie rozbudowujące silnik, warstwę natywną, narzędzia i dokumentację. Nic z 0.1 nie
zostało usunięte — API wsteczne zachowane (`layout::layout`, `style::resolve`,
`paint::hit_test`, `app::command`, `app::run`… działają jak dawniej).

### Menedżer pakietów: bit
- `Bytes.hk` → `Bit.hk` (nowa składnia: komentarze `!`, `-> klucz => wartość`, `[layout]`,
  `[build] link => static`, `native`/`native-lib-path`/`native-skip-if`, `[lib]`, `[commands]`).
- Wszędzie `bit` zamiast `bytes`: `bit add silver`, `bit run`, `bit test`, `-> silver => ^0.2`.
- Część natywna buduje się automatycznie przez `bit build` (`sh native/build.sh`).

### Silnik JS (QuickJS)
- `await` działa (kolejka mikrozadań drenowana po każdym wejściu do JS).
- `console.*`, `setTimeout`/`setInterval`/`requestAnimationFrame`, `fetch`, `performance.now`.
- Lustro DOM: `document.querySelector(All)`, `getElementById`, `createElement`, `Element`
  (`textContent`, `innerHTML`, `value`, `classList`, `style`, `dataset`, atrybuty, drzewo),
  `addEventListener` z bąbelkowaniem, `document`/`window` events, `template`, `DocumentFragment`,
  komentarze, parser HTML po stronie JS.
- Polyfille: `Event`/`CustomEvent`/`EventTarget`, `URL`, `TextEncoder`, `AbortController`, `atob`/`btoa`,
  `structuredClone`, `process.env`, obserwatory (atrapy), `matchMedia`.
- Limity: pamięć 256 MB, stos 1 MB, budżet czasu na wejście (pętla nieskończona jest przerywana),
  dynamiczna kolejka wywołań (przepełnienie odrzuca Promise zamiast ciszy), wyjątki w callbackach raportowane.
- `silver.window / dialog / clipboard`, uprawnienia (capabilities) dla komend wołanych z JS.

### TypeScript / npm / Svelte
- `silver build/dev` uruchamiają `npm install` + `npm run build` / `npm run watch` (esbuild).
- `types/silver.d.ts` — typy globalnego `silver`.
- Szablony: `ts_preact` (TSX + Preact), `svelte` (Svelte 5, runes). Zweryfikowane w QuickJS
  (też Svelte 4 i Preact 10): `native/tests/run-js.sh`.

### Silnik układu i stylów
- Zawijanie tekstu (słowa, łamanie długich słów, `text-align`, `white-space`, `text-transform`, ellipsis),
  inline/inline-block, flexbox (wrap, grow/shrink/basis, justify, align, gap, marginesy auto),
  grid (`fr`, `repeat`, `minmax`, span, gap), `position: relative|absolute|fixed`, `overflow` z przewijaniem.
- CSS: kolory (148 nazw, `#rgb`, `rgb()`, `hsl()`), jednostki (`%`, `em`, `rem`, `vw`, `vh`, `calc()`),
  `var(--x)`, `!important`, `@media` (szerokość, `prefers-color-scheme`), selektory atrybutów i
  kombinatorów z nawracaniem, pseudo-klasy, `::before`/`::after` (reguły), `border-radius`, `box-shadow`,
  gradienty, obrazy tła, `font-weight/style`, `line-height`, `text-decoration`, `box-sizing`, `min/max-*`,
  `opacity`, `cursor`, `outline`, `transition` (kolor, tło, przezroczystość).
- Styl liczony raz na węzeł; układ cache'owany, liczony tylko po zmianach.

### Interakcja
- Cykliczny `on_tick` (`set_tick_interval`), pętla oparta o czekanie (brak sztywnego 16 ms).
- Edycja pól: kursor, zaznaczenie, strzałki/Home/End/Delete, Ctrl+A/C/X/V, UTF-8, IME, textarea,
  checkbox, radio, select, Tab, Enter/submit, `maxlength`.
- Zdarzenia HTML: `on-click`, `on-input`, `on-change`, `on-submit`, `on-keydown`, `on-hover`
  (argumenty JSON: `id`, `value`, `checked`, `data-*`).
- Przewijanie kontenerów i strony (z limitem), kursory CSS, drag & drop plików, schowek.
- API okna: ikona, min/max rozmiar, pełny ekran, dekoracje, powiadomienia; `app::run_all` (wiele okien).
- Dyrektywy UI zwracane z handlerów (`ui.h#`), zadania w tle (`app::spawn_task`), szablony z `if`/`each`.

### Warstwa natywna
- Cache tekstur tekstu (LRU) i obrazów (LRU, pełne ścieżki), kolejki zdarzeń per okno, HiDPI,
  zaokrąglone prostokąty/gradienty/clip, funkcje UTF-8, motyw systemowy na Linux/macOS/Windows.
- Dialogi bez zewnętrznych zależności (zenity/kdialog, osascript, PowerShell), wybór wielu plików.

### Narzędzia
- CLI: `silver new/list/dev/build/run/bundle/doctor/version`, szablony wbudowane.
- Pakowanie: AppImage, `.deb`, `.app`/`.dmg` (podpis + notaryzacja), Windows `.zip`/instalator.
- CI: native (Linux/macOS/Windows), frontend (npm+esbuild w QuickJS), H# (bit).
- Testy: 138 testów silnika (`src/engine_tests.h#`), test dymny natywny, testy frameworków w QuickJS.

## 0.1
Pierwsza wersja: parser HTML, CSS, layout blokowy i flex, IPC, SDL2 shim.
