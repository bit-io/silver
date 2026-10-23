# JavaScript w Silver

Silnik: QuickJS (quickjs-ng), ES2020+. API (`console`, timery, `fetch`, `silver`, DOM, polyfille) jest **wbudowane**
w `libsilverjs.a` — nie dołączasz żadnych plików. Skrypty ładujesz przez `app::load_js([...])` albo `<script src>` /
`<script>` w HTML.

## Most do H#
```js
const r = await silver.invoke('greet', { name: 'Ala' });   // Promise; odrzucony przy błędzie komendy
const off = silver.listen('moje-zdarzenie', (payload) => { … });   // app::emit(app, "moje-zdarzenie", json)
```
Zdarzenia systemowe: `silver://task` (`{id, output}`), `silver://drop` (`{path|text, x, y}`), `silver://focus`, `silver://blur`, `silver://link`.

`silver.window.*` (tytuł, pełny ekran, rozmiar, ikona, powiadomienia…), `silver.dialog.*` (`open`, `openMultiple`, `save`,
`folder`, `message`, `confirm`), `silver.clipboard.readText/writeText`. Typy: `types/silver.d.ts`.

## DOM (lustro)
JS widzi **kopię** drzewa DOM (snapshot przy starcie). Odczyty są synchroniczne; zapisy trafiają wsadowo do H#
(jedno wywołanie na mikrozadanie) i przerysowują widok.

Obsługiwane: `document.getElementById/querySelector(All)/getElementsBy*`, `createElement/createTextNode/createComment/
createDocumentFragment`, `body/head/documentElement/activeElement/title`; na elementach: `textContent`, `innerText`, `innerHTML`
(parsowany lokalnie, odczyt synchroniczny), `outerHTML`, `value`, `checked`, `disabled`, `hidden`, `id`, `className`, `classList`,
`style` (kebab/camelCase, `cssText`), `dataset`, atrybuty (+NS), `children/childNodes/parent*/…Sibling`, `append/prepend/
insertBefore/appendChild/removeChild/remove/replaceWith/before/after/cloneNode`, `closest/matches/contains`, `focus/blur/click`,
`scrollTop`, `<template>.content`.

Selektory (podzbiór CSS): tag, `*`, `#id`, `.klasa`, `[attr]`, `[attr=v|~=|^=|$=|*=||=]`, kombinatory ` `, `>`, `+`, `~`,
`:first-child/:last-child/:only-child/:nth-child()/:empty/:checked/:disabled/:enabled/:focus/:not()`, listy po przecinku.

### Zdarzenia
`click`, `dblclick`, `mousedown/up`, `mouseenter/leave`, `contextmenu`, `keydown/keyup` (`key`, `code`, `ctrlKey`, `shiftKey`, `altKey`,
`metaKey`, `repeat`), `input`, `change`, `submit`, `focus`, `blur`, `scroll`, `drop`, `resize`, `DOMContentLoaded`, `load`.
Bąbelkowanie do `document` i `window`, `stopPropagation`, `{ once: true }`.

`preventDefault()` działa **synchronicznie** dla `click` (pomija przełączenie checkboxa/radio, nawigację linku, wysłanie formularza przyciskiem)
i `keydown` (pomija wpisanie znaku, Tab, Enter/Spację, przewijanie klawiszami): H# wywołuje JS bezpośrednio i odczytuje, czy zdarzenie
zostało anulowane. Dla pozostałych typów zdarzeń `preventDefault()` jest przyjmowane bez efektu (nie mają domyślnej akcji w Silverze).

## Układ dostępny z JS (cache ostatniej klatki)
Pierwsze wywołanie `getBoundingClientRect`, `offsetWidth` itp. (albo `new ResizeObserver/IntersectionObserver().observe()`) włącza
wysyłanie prostokątów z H# do JS po każdym układzie (tylko po zmianie). Do tego momentu — i dla elementów jeszcze nieułożonych — zwracane są zera,
więc przy pierwszym odczycie tuż po zmianie DOM wartość pochodzi z poprzedniej klatki.
`getBoundingClientRect()/getClientRects()`, `offsetWidth/Height/Left/Top/Parent`, `clientWidth/Height/Left/Top`, `scrollWidth/Height`,
`window.scrollY/pageYOffset`. `getComputedStyle(el)` zwraca prawdziwe wartości (display, position, kolory jako `rgb()/rgba()`, rozmiary czcionki,
marginesy, paddingi, szerokości ramek, `opacity`, `z-index`, `transform`, …) dla elementów, o które JS już raz zapytał (pierwszy odczyt zwraca
styl inline i wartości domyślne; kolejne — z układu).

## Obserwatory
`MutationObserver` (`childList`, `attributes`, `characterData`, `subtree`, `attributeOldValue`, `attributeFilter`, `takeRecords`),
`ResizeObserver` (rozmiar zawartości i ramki), `IntersectionObserver` (`root`, `rootMargin`, `threshold`, odświeżany przy układzie, zmianie rozmiaru okna i przewijaniu dokumentu).

## Moduły ES
Pliki ładowane przez `load_js`/`<script src>` wykrywane są jako moduły (`import`/`export` na początku linii albo rozszerzenie `.mjs`):
względne importy (`./x`, `../y`, z dopisaniem `.js`/`.mjs`/`index.js`), `export default`, `import * as`, dynamiczny `import()`, `await` na najwyższym poziomie.
Wciąż działają też zwykłe skrypty i bundle IIFE. Brak: importów po URL i `import.meta.url`.

## Shadow DOM i Custom Elements
`element.attachShadow({mode})`, `shadowRoot`, `<slot>` (domyślny i nazwany), `slot.assignedNodes()`, `host.children/childNodes` zwracają light DOM.
Silnik **nie ma enkapsulacji CSS** (style z shadow root są globalne, `:host` nie jest rozpoznawane, brak treści zapasowej slotu, brak `slotchange`).
`customElements.define/get/whenDefined`, `class X extends HTMLElement`, `connectedCallback`, `disconnectedCallback`,
`attributeChangedCallback` + `observedAttributes`, upgrade już istniejących elementów. Brak: `extends` wbudowanych, `formAssociated`.

## Środowisko
`console.*`, `setTimeout/setInterval/clear*`, `requestAnimationFrame`, `queueMicrotask`, `performance.now`, `matchMedia`,
`Event/CustomEvent/EventTarget`, `URL/URLSearchParams` (+ `URL.createObjectURL` dla `Blob`), `TextEncoder/Decoder`, `AbortController`
(`reason` = `AbortError`/`TimeoutError`), `atob/btoa`, `structuredClone`, `process.env.NODE_ENV`.
- **`fetch`:** nagłówki żądania i odpowiedzi (`Headers`, `Request`, `Response`, `clone/json/text/blob/arrayBuffer`), dowolna metoda, `AbortSignal`, adresy
  `blob:` oraz **pliki lokalne** (ścieżka względna wobec katalogu HTML albo `file://`). Żądania HTTP(S) przechodzą przez `curl` (jeśli jest w `PATH`,
  bez powłoki) — stąd nagłówki i przekierowania; bez `curl` działa uproszczony klient (GET/POST/PUT/DELETE, bez nagłówków). Żądanie blokuje pętlę na czas trwania.
- **`localStorage` jest trwały** (plik JSON w `$XDG_DATA_HOME/silver/<aplikacja>/localStorage.json`, zapis z opóźnieniem 250 ms i przy zamknięciu);
  `sessionStorage` żyje w pamięci. Proxy: `localStorage.klucz = 'x'`, `Object.keys(localStorage)`.
- **`Intl`:** `NumberFormat` (dziesiętne, procent, waluta, `compact`), `DateTimeFormat` (`dateStyle/timeStyle` i opcje składowe), `PluralRules`
  (pl, en, fr; zwykłe i porządkowe), `Collator` (polskie znaki, `numeric`, `sensitivity`), `ListFormat`, `RelativeTimeFormat`, `Segmenter` (uproszczony);
  lokalizacje `pl`, `en`, `de`, `fr`, `es`, `it`; `toLocaleString/DateString/TimeString` i `localeCompare` korzystają z tych implementacji.
  To podzbiór bez pełnych danych CLDR.
- **`Worker`:** działa w tym samym wątku (bez izolacji i równoległości): `postMessage/onmessage`, kolejkowanie do czasu załadowania skryptu,
  `terminate`, skrypt z pliku lub `blob:`; `importScripts` niedostępne.
- **`WebSocket`:** natywny klient `ws://` (handshake z weryfikacją `Sec-WebSocket-Accept`, ramki tekstowe/binarne, fragmentacja, ping/pong,
  zamknięcie z kodem). **`wss://` (TLS) nie jest obsługiwane** — zdarzenia `error` + `close` (1006). Brak POSIX-owych gniazd w buildzie Windows.
- **Source mapy:** przy ładowaniu skryptu rejestrowana jest mapa z `//# sourceMappingURL=` (inline `data:` lub plik) albo sąsiedni `<plik>.map`;
  stosy błędów w konsoli i logu wskazują wtedy pliki `.ts`/`.tsx`. `silver.registerSourceMap(nazwa, json)` rejestruje mapę ręcznie.

## Limity i bezpieczeństwo
Pamięć 256 MB, stos 1 MB, budżet czasu 3 s na jedno wejście do JS (pętla nieskończona jest przerywana), kolejka 4096 wywołań
(przepełnienie odrzuca Promise). Komendy z uprawnieniem wymagają `app::allow_js`. Błędy skryptów → stderr + log H#.

## Frameworki sprawdzone na prawdziwych bundlach
TypeScript, Preact, Svelte 4/5 oraz (od 0.3) **React 18, Vue 3 i Solid** — render, kliknięcia, pole kontrolowane, listy, efekty
(`native/tests/run-ext.sh`, npm + esbuild + QuickJS na lustrze DOM). Wymagało to m.in. `window.event`, właściwości `on*`,
`ownerDocument`, `input.type`, zapisywalnego `currentTarget`.

## Czego nie ma
Izolowane wątki dla `Worker`, `wss://`, enkapsulacja CSS w Shadow DOM, pełne `Intl`, `import()` po URL. Szczegóły: [ROADMAP.md](../ROADMAP.md).
