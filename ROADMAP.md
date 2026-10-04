# Roadmap

Czego jeszcze nie ma w 0.2 — uczciwa lista, od najważniejszych.

## Do zrobienia najpierw
- [ ] **Pierwszy pełny `bit build` + uruchomienie okna** na prawdziwym H# (reszta była sprawdzana częściami — patrz README).
- [ ] Testy interpretera H# nie obejmują: dopasowania enumów w polach struktur oraz wywołań `fn` z pól struktur
      (2 testy `ipc` i 3 testy `jsonval` — łącznie 5 ze 138 — wymagają skompilowanego H#; w interpreterze przechodzi 133).
- [ ] Testy integracyjne okna (zrzuty ekranu pod Xvfb porównywane z wzorcami).

## JavaScript / frameworki
- [ ] `getBoundingClientRect`, `offsetWidth/Height`, `getComputedStyle` z prawdziwymi wartościami z układu
      (dziś zwracają zera / styl inline). Wymaga synchronicznego udostępnienia układu JS-owi (cache z ostatniej klatki).
- [ ] `MutationObserver`/`ResizeObserver`/`IntersectionObserver` działające (dziś atrapy).
- [ ] `preventDefault()` działające — zdarzenia są asynchroniczne; potrzebny tryb synchronicznej wymiany dla wybranych typów.
- [ ] ES modules (`import` w plikach ładowanych przez `load_js`) — dziś tylko bundle IIFE.
- [ ] `Intl`, `localStorage` trwałe (dziś w pamięci), `Worker`, `WebSocket`, nagłówki w `fetch`.
- [ ] Vue, Solid, React — niesprawdzone (Preact i Svelte 4/5 sprawdzone).
- [ ] Source mapy dla błędów TypeScript w konsoli.

## CSS i układ
- [ ] `float`, `display: table` (dziś `tr` = flex, `td` = elastyczna komórka), `position: sticky`, `z-index` (kolejność = kolejność dokumentu).
- [ ] `@keyframes` / `animation`, `transition` dla wymiarów i transformacji, `transform`, `filter`, `clip-path`.
- [ ] `@font-face`, wiele krojów (dziś jeden font systemowy + pogrubienie/kursywa syntetyczne), `letter-spacing`, `text-shadow`.
- [ ] Zwijanie marginesów rodzic↔dziecko (jest tylko między rodzeństwem), `align-content`, `grid-template-areas`, `grid-row`.
- [ ] Zaawansowane `::before/::after` (dziś reguły są parsowane, generowanie treści ograniczone), `:nth-child(… of S)`.
- [ ] Bidi/RTL, łamanie wierszy wg UAX #14, shaping złożonych pism.

## System i platformy
- [ ] Tray, natywne menu, natywne dialogi bez zewnętrznych narzędzi (dziś zenity/kdialog/osascript/PowerShell).
- [ ] Dostępność (accessibility tree, czytniki ekranu).
- [ ] Wayland: testy (SDL2 powinien działać), IME na każdej platformie.
- [ ] Android/iOS.

## Narzędzia
- [ ] `silver dev` z automatycznym odświeżaniem po zmianach w `src/*.h#` (przebudowa binarki).
- [ ] Automatyczne aktualizacje aplikacji, podpisywanie Linux (AppImage + gpg).
- [ ] Profilowanie układu (`SILVER_DEBUG=layout`).
