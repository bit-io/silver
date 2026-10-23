# Silver 0.3

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

## Co potrafi (0.3)

| Obszar | Zakres |
|---|---|
| **HTML** | parser stosowy: encje, komentarze, `<!DOCTYPE>`, atrybuty w `"…"`/`'…'`/bez cudzysłowów, tagi void, `<script>`/`<style>`/`<link>`, formularze (`input`, `textarea`, `select`, checkbox, radio, `button`, `label`, `form`), `data-*` |
| **CSS** | kolory, jednostki, `calc()/clamp()`, `var()`, `inherit/initial/unset` dla każdej właściwości, właściwości logiczne, `@media` (zakresy, `or`, `not`), `@supports`, `@layer`, `@import`, `@font-face`, **`@keyframes`/`animation`**, `transition` geometrii, **`transform`**, `filter`, `clip-path`, **`float`**, **`z-index`**, `position: sticky`, `aspect-ratio`, `letter-spacing`, `text-shadow`, ramki `dashed/dotted`, `::before/::after` z `attr()`/`counter()`, `:has()`, `:nth-child(… of S)`, … → [docs/css.md](docs/css.md) |
| **Układ** | blok z `float` i zwijaniem marginesów, inline, inline-block, flexbox (`align-content`), grid (obszary, linie, auto-placement), **prawdziwe tabele** (`colspan`, `rowspan`), `position`, listy, przewijanie kontenerów |
| **Interakcja** | edycja pól (kursor, zaznaczenie, schowek, UTF-8, IME), Tab, kursory CSS, drag & drop plików, `on-click/input/change/submit/keydown/hover` |
| **JS** | QuickJS ES2020+: moduły ES, `await`, timery, `fetch` z nagłówkami, lustro DOM z układem (`getBoundingClientRect`, `getComputedStyle`), obserwatory, synchroniczne `preventDefault`, `Intl`, trwały `localStorage`, `Worker`, `WebSocket`, Shadow DOM, Custom Elements, source mapy → [docs/javascript.md](docs/javascript.md) |
| **TypeScript / frameworki** | esbuild + npm; `silver.d.ts`; sprawdzone: TypeScript, Preact, Svelte 4/5, React 18, Vue 3, Solid → [docs/typescript.md](docs/typescript.md) |
| **Backend H#** | komendy z uprawnieniami (`capabilities`), dyrektywy UI (`ui.h#`), szablony `{{klucz}}`/`if`/`each`, zadania w tle, wiele okien, cykliczny `on_tick` |
| **System** | schowek, powiadomienia, dialogi plików, ikona, pełny ekran, motyw jasny/ciemny |
| **Dystrybucja** | `silver bundle`: AppImage, `.deb`, `.app`/`.dmg`, Windows `.zip`/instalator → [docs/packaging.md](docs/packaging.md) |

## CLI

```
silver new <nazwa> [--template <szablon>] [--ts] [--svelte]
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
- [native/README.md](native/README.md) — warstwa natywna (SDL2, QuickJS, dialogi)
- [ROADMAP.md](ROADMAP.md) — czego jeszcze nie ma · [CHANGELOG.md](CHANGELOG.md)

## Co jest zweryfikowane, a co nie

Środowisko, w którym powstało 0.3, nie miało kompilatora H# (LLVM), więc:

- **Sprawdzone realnie:**
  - parsowanie i sprawdzanie typów całej biblioteki parserem i type-checkerem H# (w tym `app.h#`, `paint.h#`);
  - **287 testów silnika** (DOM, CSS, style, układ, tabele, grid, kolumny, `@container`, zagnieżdżanie, transformacje, animacje, pseudo-elementy, szablony, edycja, IPC)
    wykonanych interpreterem H#: 283 przechodzi, 4 (enum w polu struktury w `ipc`, UTF-8 przez FFI w edycji) wymaga skompilowanego H#;
  - **warstwy transformacji, `backdrop blur` i dekoder WOFF** sprawdzone na pikselach / bajtach (`native/tests/run-layer.sh`, `run-woff.sh`; renderer programowy SDL);
  - warstwa natywna: SDL2 pod Xvfb (`native/tests/run.sh`), rodziny czcionek na prawdziwym SDL_ttf (`fonts_test.c`), QuickJS z DOM, polyfillami, modułami ES,
    **WebSocket względem prawdziwego serwera**, 20 kontroli API JS, frontendy TypeScript/Preact/Svelte oraz **React 18, Vue 3, Solid** zbudowane
    prawdziwym npm + esbuild i uruchomione w QuickJS (`native/tests/run-all.sh`, `run-js.sh`, `run-ext.sh`).
- **Niesprawdzone:** skompilowanie i uruchomienie całej aplikacji H# z oknem (`h# compile`), więc **rysowanie** (`paint.h#`: m.in. ramki `dashed/dotted`,
  nowe: obrócone poddrzewa, `text-underline-offset`, `background-origin/clip`, `object-position`, `caret-color`, `scrollbar-*`, płynne przewijanie, kotwice `#id`; warstwa natywna pod nimi jest przetestowana,
  ale połączenie H# → shim nie było uruchomione,
  `text-shadow`, `letter-spacing`, tło z powtarzaniem, `sticky`) i **integracja animacji w `app.h#`** przeszły parser i type-checker, ale nigdy nie zostały
  wyrenderowane; działanie na macOS/Windows (CI buduje shimy, nie uruchamia okien), skrypty pakujące poza `.deb`, `curl` jako klient `fetch` w pakietach.
  Oczekuj pierwszych poprawek po pierwszym `bit build`.
