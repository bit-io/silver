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

> **Ograniczenie:** zdarzenia dochodzą asynchronicznie, więc `preventDefault()` jest przyjmowane, ale nie ma efektu.

## Środowisko
`console.*`, `setTimeout/setInterval/clear*`, `requestAnimationFrame`, `queueMicrotask`, `performance.now`, `fetch` (GET/POST/PUT/DELETE,
bez nagłówków; blokuje pętlę na czas żądania), `localStorage` (w pamięci), `matchMedia`, `getComputedStyle` (styl inline),
`Event/CustomEvent/EventTarget`, `URL/URLSearchParams`, `TextEncoder/Decoder`, `AbortController`, `atob/btoa`, `structuredClone`,
`process.env.NODE_ENV`, obserwatory (atrapy).

## Limity i bezpieczeństwo
Pamięć 256 MB, stos 1 MB, budżet czasu 3 s na jedno wejście do JS (pętla nieskończona jest przerywana), kolejka 4096 wywołań
(przepełnienie odrzuca Promise). Komendy z uprawnieniem wymagają `app::allow_js`. Błędy skryptów → stderr + log H#.

## Czego nie ma
Rozmiary z układu (`getBoundingClientRect`, `offsetWidth`… zwracają 0), moduły ES, `Intl`, `Worker`, `WebSocket`, Shadow DOM,
działające obserwatory. Szczegóły: [ROADMAP.md](../ROADMAP.md).
