# Changelog

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
