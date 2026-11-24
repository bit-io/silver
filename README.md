# Silver 0.5

Framework okienkowy w stylu Tauri **bez WebView**: frontend w HTML/CSS/JS (albo TypeScript, React, Vue, Solid, Preact, Svelte),
backend w [H#](https://github.com/HackerOS-Linux-System/H-Sharp). Własny silnik układu i rysowania na SDL2,
JavaScript przez QuickJS. Pakiety zarządzane przez [bit](https://github.com/bit-io/bit).

```
frontend (HTML + CSS + JS/TS)  ──silver.invoke / on-click──▶  backend H# (app::command)
        ▲                                                           │
        └────────── zdarzenia (app::emit, silver.listen) ◀──────────┘
```

## Szybki start

```sh
bit add silver                          # w istniejącym projekcie H#
use "bit -> silver" from "silver"      # w kodzie → app::, ui::, data::, dialog::, …

silver new moja-apka               # nowy projekt (szablon vanilla)
silver new notatki --ts            # TypeScript + Preact + esbuild
silver new notatki --svelte        # Svelte 5 + esbuild
silver list                        # wszystkie szablony
cd moja-apka && bit                # instaluje zależności, buduje, uruchamia
```

Minimalna aplikacja:

```h#
use "bit -> silver" from "silver"

fn greet(args: string) -> string is
    return "{{\"message\": \"Cześć z H#!\"}}"     ;; UWAGA: w literałach H# klamry podwajamy
end

fn main() is
    let mut a = app::new("Hello", 800, 600)
    a = app::load_html(a, "frontend/index.html")
    a = app::load_css(a, "frontend/style.css")
    a = app::command(a, "greet", greet)
    a = app::load_js(a, ["frontend/app.js"])      ;; opcjonalnie
    a = app::enable_hot_reload(a)                 ;; HTML + CSS + JS
    app::run(a)
end
```

```js
// frontend/app.js
document.querySelector('#btn').addEventListener('click', async () => {
  const r = await silver.invoke('greet');       // await działa
  document.querySelector('#out').textContent = r.message;
});
```

## Co potrafi (0.5)

| Obszar | Zakres |
|---|---|
| **HTML** | parser stosowy: encje, komentarze, `<!DOCTYPE>`, atrybuty w `"…"`/`'…'`/bez cudzysłowów, tagi void, `<script>`/`<style>`/`<link>`, formularze (`input`, `textarea`, `select`, checkbox, radio, `button`, `label`, `form`), `data-*` |
| **CSS** | kolory, jednostki, `calc()/clamp()`, `var()`, `inherit/initial/unset` dla każdej właściwości, właściwości logiczne, `@media` (zakresy, `or`, `not`), `@supports`, `@layer`, `@import`, `@font-face`, **`@keyframes`/`animation`**, `transition` geometrii, **`transform`**, `filter`, `clip-path`, **`float`**, **`z-index`**, `position: sticky`, `aspect-ratio`, `letter-spacing`, `text-shadow`, ramki `dashed/dotted`, `::before/::after` z `attr()`/`counter()`, `:has()`, `:nth-child(… of S)`, … → [docs/css.md](docs/css.md) |
| **Układ** | blok z `float` i zwijaniem marginesów, inline, inline-block, flexbox (`align-content`), grid (obszary, linie, auto-placement), **prawdziwe tabele** (`colspan`, `rowspan`), `position`, listy, przewijanie kontenerów |
| **Interakcja** | edycja pól (kursor, zaznaczenie, schowek, UTF-8, IME), Tab, kursory CSS, drag & drop plików, `on-click/input/change/submit/keydown/hover` |
| **JS** | QuickJS ES2020+: moduły ES, `await`, timery, `fetch` z nagłówkami, lustro DOM z układem (`getBoundingClientRect`, `getComputedStyle`), obserwatory, synchroniczne `preventDefault`, `Intl`, trwały `localStorage`, `Worker`, `WebSocket`, Shadow DOM, Custom Elements, source mapy → [docs/javascript.md](docs/javascript.md) |
| **TypeScript / frameworki** | esbuild + npm; typy z npm (`@hackeros/silver-types`); sprawdzone: TypeScript, Preact, Svelte 4/5, React 18, Vue 3, Solid → [docs/typescript.md](docs/typescript.md) |
| **Backend H#** | komendy z uprawnieniami (`capabilities`), dyrektywy UI (`ui.h#`), szablony `{{klucz}}`/`if`/`each`, zadania w tle, wiele okien, cykliczny `on_tick` |
| **System** | schowek, powiadomienia, dialogi plików, ikona, pełny ekran, motyw jasny/ciemny |
| **Dystrybucja** | `silver bundle`: AppImage, `.deb`, `.app`/`.dmg`, Windows `.zip`/instalator → [docs/packaging.md](docs/packaging.md) |

## CLI

```
silver new <nazwa> [--template <szablon>] [--ts] [--svelte]
silver embed [--dir frontend]   # wbuduj frontend w binarkę (src/assets_gen.h#)
silver run --x11 | --backend <wayland|wayland-only|x11|auto>
silver list | doctor | version
silver dev                 # frontend --watch + aplikacja z hot reload
silver build | run         # npm (jeśli jest frontend/package.json) + bit build | bit run
silver bundle <appimage|deb|macos|dmg|windows>
```

## Szablony

| Szablon | Co pokazuje |
|---|---|
| `vanilla` | najprostszy start: HTML/CSS + trochę JS |
| `counter` | stan w H# + cykliczny `on_tick` + JS (`async/await`, skróty klawiszowe) |
| `kitchen_sink` | przegląd silnika: formularze, flex, grid, przewijanie, motyw ciemny, dyrektywy UI |
| `todo_js` | aplikacja prawie w całości w JS: DOM, delegacja zdarzeń, dialogi, schowek, zapis przez H# |
| `dashboard` | pulpit: grid, wykres z `div`-ów w JS, `requestAnimationFrame`, `fetch`, zadania w tle |
| `ts_preact` | TypeScript + TSX + Preact, typowany kontrakt komend, hooki |
| `svelte` | Svelte 5 (runes) |

## Dokumentacja

- [docs/getting-started.md](docs/getting-started.md) — instalacja, pierwszy projekt, struktura
- [docs/api.md](docs/api.md) — API H#: `app`, `window`, `ipc`, `ui`, `data`, `dialog`, `edit`
- [docs/javascript.md](docs/javascript.md) — środowisko JS, lustro DOM, zdarzenia, ograniczenia
- [docs/typescript.md](docs/typescript.md) — TypeScript, npm, Preact, Svelte
- [docs/css.md](docs/css.md) — obsługiwany CSS i HTML
- [docs/packaging.md](docs/packaging.md) — AppImage, deb, macOS, Windows
- [docs/backends.md](docs/backends.md) — Wayland (domyślnie) / X11, `SILVER_BACKEND`, dekoracje okien
- [docs/embedding.md](docs/embedding.md) — frontend wbudowany w binarkę (`silver embed`)
- [docs/npm.md](docs/npm.md) — typy `@hackeros/silver-types` i publikacja na npm
- [native/README.md](native/README.md) — warstwa natywna (SDL2, QuickJS, dialogi)
- [ROADMAP.md](ROADMAP.md) — czego jeszcze nie ma · [CHANGELOG.md](CHANGELOG.md) · testy natywne: `sh native/tests/run-all.sh`

## Co jest zweryfikowane, a co nie (0.5)

**Sprawdzone realnie** (kompilator H# i `bit 0.3.0` były dostępne):

- pełny `bit build` biblioteki i aplikacji (szablon `counter`, zależność przez ścieżkę) → jedna statyczna binarka z wbudowanym frontendem;
- start z pustego katalogu (bez `frontend/`) na **Waylandzie** (weston headless) i na **X11** (Xvfb), wybór backendu `SILVER_BACKEND`;
- `silver embed` → `src/assets_gen.h#` → binarka serwuje HTML/CSS/JS/obrazy z pamięci (bez plików obok);
- **599 testów silnika** (`sh tools/run_engine_tests.sh`; kolejnych 12 wymaga fixtur/HarfBuzz — `SILVER_TEST_FULL=1`);
- test natywny (`sh native/tests/run.sh`), type-check `@hackeros/silver-types` (`tsc`), `.deb` z jedną binarką.

**Sprawdzone w iteracji „0.5 rozbudowa”** (kompilator H# **nie** był dostępny; testowano warstwę C/JS: `sh native/tests/run-all.sh`):

- klient HTTP/HTTPS w C (chunked, przekierowania, timeout, TLS z weryfikacją), `fetch` bez blokowania pętli, **`wss://` na prawdziwym serwerze TLS**;
- **Workery w osobnych wątkach** (komunikacja, `importScripts`, timery, `fetch`, błędy, `terminate` pętli nieskończonej), `import()` po URL;
- **dekoder WOFF2** zgodny z fontTools na ponad 20 tys. glifów; sonda GL `SILVER_RENDERER=auto` (dynamicznie i w pełni statycznie, Xvfb);
- Shadow DOM (scoping CSS, `:host`, `::slotted`, fallback slotu, `slotchange`), `Intl` z danymi CLDR (porównanie z ICU), `structuredClone`;
- pełny **statyczny build SDL2 + Wayland + xkbcommon** z zapasowych źródeł (Ubuntu 24.04), kompilacja shima i klienta sieci pod **MinGW (Windows)**.

**Wayland:** jeśli aplikacje z `bit build --release` startują na X11, patrz `docs/backends.md` (sekcja „Aplikacje startują na X11…”): przebuduj warstwę natywną
(`rm -rf native/build && sh native/build.sh`, potrzebne `meson ninja bison libexpat1-dev libffi-dev`). Statyczny build ma teraz prezenter `wl_shm` (okno Wayland bez OpenGL).

**Nadal niesprawdzone / ograniczenia** — patrz [ROADMAP.md](ROADMAP.md): zmiany w silniku H# (jednostki `cq*`, `style()`) nie były kompilowane;
statyczny SDL nie ma libdecor (na GNOME/Wayland użyj `frameless` + `app-region` albo budowy dynamicznej); macOS i Windows nie były *uruchamiane*
(od teraz robi to CI: `.github/workflows/ci.yml`); AppImage zbudowano i uruchomiono (Linux), ale skrypty .app/.dmg/Windows oraz podpisywanie i notaryzacja nie były uruchamiane
(`build.yml` je weryfikuje przy ręcznym starcie).
