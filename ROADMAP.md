# Roadmap

Czego jeszcze nie ma w 0.3 — uczciwa lista, od najważniejszych. Zamknięte w 0.3: `float`, tabele, `z-index`, `sticky`, `transform`
(translate/scale), `filter`, `clip-path`, `@keyframes`/`animation`, przejścia geometrii, `@font-face` i kilka krojów, `letter-spacing`,
`text-shadow`, zwijanie marginesów rodzic↔dziecko, `align-content`, `grid-template-areas`/`grid-row`, generowanie treści `::before/::after`,
`:nth-child(… of S)`, `:has()`, `@import`; w JS: układ dla `getBoundingClientRect`/`getComputedStyle`, obserwatory, synchroniczne `preventDefault`,
moduły ES, `Intl`, trwały `localStorage`, `Worker`, `WebSocket`, nagłówki `fetch`, Shadow DOM, Custom Elements, source mapy, React/Vue/Solid.

## Do zrobienia najpierw
- [ ] **Pierwszy pełny `bit build` + uruchomienie okna** na prawdziwym H# (silnik, układ i most JS są sprawdzone częściami — patrz README).
      Dotyczy zwłaszcza nowego kodu rysowania (`paint.h#`: ramki `dashed/dotted`, `text-shadow`, `letter-spacing`, tło z powtarzaniem, `sticky`)
      i integracji animacji w `app.h#`, które przeszły parser i type-checker, ale nigdy nie były wyrenderowane.
- [ ] Testy interpretera H# nie obejmują dopasowania enumów w polach struktur oraz UTF-8 przez FFI (4 testy z 287: 2 `ipc`, 2 edycja UTF-8).
- [ ] Testy integracyjne okna (zrzuty ekranu pod Xvfb porównywane z wzorcami) — także dla nowych funkcji CSS.
- [ ] CI uruchamiające `native/tests/run-all.sh` (dziś `ci.yml` wykonuje tylko budowę shimów i testy natywne).

## JavaScript
- [ ] `wss://` (TLS) dla `WebSocket`; `Worker` w osobnym wątku/kontekście QuickJS (dziś ten sam wątek, bez izolacji).
- [ ] Pełne `Intl` (dane CLDR, strefy czasowe, `formatToParts`), `import()` po URL, `import.meta`.
- [ ] Shadow DOM z enkapsulacją CSS (`:host`, `::slotted`), treścią zapasową slotu i `slotchange`; `formAssociated`/`extends` w Custom Elements.
- [ ] `fetch`: strumieniowanie, `FormData`, ciasteczka, odpowiedź binarna bez pośrednictwa tekstu; wywołania nie blokujące pętli.
- [ ] `getComputedStyle` dla elementów, o które JS nie pytał wcześniej, bez opóźnienia o jedną klatkę.
- [ ] Source mapy dla skryptów wstrzykiwanych inaczej niż przez `load_js`/`<script src>`.

## CSS i układ
- [ ] Transformacje 3D (`perspective`, `transform-style`), zagnieżdżone obrócone elementy (dziś działa tylko najbardziej wewnętrzna rotacja), `zoom`.
- [ ] `position: sticky` w kontenerach przewijanych, `@property`, `@scope` z ewaluacją, jednostki kontenerowe `cqw/cqh`, `@container style()`.
- [ ] Nazwane linie siatki, `align-content` dla grid, wiersze `fr` z wysokością kontenera, `masonry`.
- [ ] Wiele kolumn: `column-span: all`, równoważenie z dzieleniem pojedynczych wierszy tekstu wewnątrz bloków.
- [ ] Nadal tylko parsowane (P): SVG (`fill`, `stroke*`, …), `mask*`, `mix-blend-mode`, `border-image*`, `scroll-snap-*`, `overscroll-behavior`, `user-select`, `resize`,
      `font-variant-*`, `font-stretch`, `quotes`, `list-style-image`, `writing-mode` pionowy, `anchor-*`/`position-*`, animacje sterowane przewijaniem.
- [ ] Float: dokładne pozycjonowanie względem marginesów rodzeństwa; floaty wewnątrz komórek tabel i flexa.
- [ ] Animacje: `animation-play-state` (pauza), przejścia pojedynczych właściwości o różnych czasach (dziś jeden czas i funkcja na regułę), animacje `rotate`.
- [ ] Czcionki: WOFF2 (wymaga Brotli; WOFF 1.0 jest), lista zapasowych krojów (dziś pierwsza nazwa z `font-family`), `font-variant`, czcionki zmienne, `font` skrótowo z rodziną.
- [ ] Rozmycie `text-shadow`/`box-shadow` (dziś przybliżenie), `filter: blur()` (jest `backdrop-filter: blur()`).
- [ ] Pełne Bidi (mieszane kierunki w jednym wierszu; RTL w podstawowym wariancie jest), łamanie wierszy wg UAX #14, shaping złożonych pism.

## System i platformy
- [ ] Tray, natywne menu, natywne dialogi bez zewnętrznych narzędzi (dziś zenity/kdialog/osascript/PowerShell).
- [ ] Dostępność (accessibility tree, czytniki ekranu).
- [ ] Wayland: testy (SDL2 powinien działać), IME na każdej platformie; WebSocket i `curl` na Windows.
- [ ] Android/iOS.

## Narzędzia
- [ ] `silver dev` z automatycznym odświeżaniem po zmianach w `src/*.h#` (przebudowa binarki).
- [ ] Automatyczne aktualizacje aplikacji, podpisywanie Linux (AppImage + gpg).
- [ ] Profilowanie układu (`SILVER_DEBUG=layout`).
