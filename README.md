# Silver 0.2

Framework okienkowy w stylu Tauri **bez WebView**: frontend w HTML/CSS/JS (albo TypeScript, Preact, Svelte),
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

## Co potrafi (0.2)

| Obszar | Zakres |
|---|---|
| **HTML** | parser stosowy: encje, komentarze, `<!DOCTYPE>`, atrybuty w `"…"`/`'…'`/bez cudzysłowów, tagi void, `<script>`/`<style>`/`<link>`, formularze (`input`, `textarea`, `select`, checkbox, radio, `button`, `label`, `form`), `data-*` |
| **CSS** | kolory (148 nazw, `#rgb`, `rgb()/hsl()`), `%`/`em`/`rem`/`vw`/`vh`/`calc()`, `var(--x)`, `!important`, `@media`, selektory atrybutów/kombinatorów/pseudo-klas, `::before/::after`, `border-radius`, `box-shadow`, gradienty, `transition`, `overflow`, `position`, `box-sizing`, … → [docs/css.md](docs/css.md) |
| **Układ** | blok, inline, inline-block, zawijanie tekstu, flexbox, grid, `position`, listy, tabele (uproszczone), przewijanie kontenerów |
| **Interakcja** | edycja pól (kursor, zaznaczenie, schowek, UTF-8, IME), Tab, kursory CSS, drag & drop plików, `on-click/input/change/submit/keydown/hover` |
| **JS** | QuickJS ES2020+: `await`, timery, `fetch`, `console`, lustro DOM, zdarzenia, polyfille → [docs/javascript.md](docs/javascript.md) |
| **TypeScript / frameworki** | esbuild + npm; `silver.d.ts`; sprawdzone: TypeScript, Preact, Svelte 4/5 → [docs/typescript.md](docs/typescript.md) |
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

Środowisko, w którym powstało 0.2, nie miało kompilatora H# (LLVM), więc:

- **Sprawdzone realnie:** parsowanie i sprawdzanie typów całej biblioteki parserem i type-checkerem H#;
  138 testów silnika (DOM, CSS, style, layout, szablony, edycja, IPC) wykonanych interpreterem H# — 133 przechodzi, 5 (enum w polu struktury, `jsonval`) wymaga skompilowanego H#;
  warstwa natywna (SDL2 pod Xvfb, QuickJS, DOM, polyfille) i budowa frontendów
  TypeScript/Preact/Svelte prawdziwym npm+esbuild uruchomiona w QuickJS (`native/tests/run.sh`, `run-js.sh`).
- **Niesprawdzone:** skompilowanie i uruchomienie całej aplikacji H# z oknem (`h# compile`), działanie na
  macOS/Windows (CI buduje shimy, nie uruchamia okien), skrypty pakujące poza `.deb`.
  Oczekuj pierwszych poprawek po pierwszym `bit build`.
