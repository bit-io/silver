# Obsługiwany HTML i CSS

## HTML
Tagi: strukturalne (`div`, `p`, `section`, `header`, `main`, …), nagłówki, listy (`ul/ol/li` z markerami), `table/tr/td/th`
(uproszczone), formularze (`form`, `input`, `textarea`, `select/option`, `button`, `label`), `img`, `a`, `pre`, `code`,
`blockquote`, `hr`, `br`, inline (`span`, `b`, `strong`, `i`, `em`, `u`, `s`, `small`, `mark`, `sub`, `sup`).
`<style>`, `<link rel="stylesheet">`, `<script>`, `<title>`, komentarze, entity (`&amp; &nbsp; &#233; &#xE9;` + popularne).
Atrybuty Silvera: `on-click`, `on-input`, `on-change`, `on-submit`, `on-keydown`, `on-hover`; szablony `{{klucz}}`, `each`, `as`, `if`, `if-not`.

## Wartości
Kolory: 148 nazw, `#rgb/#rgba/#rrggbb/#rrggbbaa`, `rgb()/rgba()/hsl()/hsla()`, `transparent`, `currentcolor`.
Długości: `px`, `%`, `em`, `rem`, `vw`, `vh`, `vmin`, `vmax`, `pt`, `pc`, `cm`, `mm`, `in`, `calc()` (`+ - * /`, nawiasy), `min()/max()/clamp()` (px).
`var(--x, fallback)`, `!important`, `inherit/initial/unset`.

## Właściwości
- **Model pudełkowy:** `width/height`, `min-/max-*`, `padding*`, `margin*` (w tym `auto`), `border*` (per krawędź, kolor, szerokość),
  `border-radius` (per narożnik), `box-sizing`, `outline`, `box-shadow` (pierwszy cień, bez `inset`), `opacity`.
- **Wyświetlanie:** `display: block|inline|inline-block|none|flex|inline-flex|grid|list-item`, `visibility`, `overflow(-x/-y)`,
  `position: static|relative|absolute|fixed` + `top/right/bottom/left/inset`, `cursor`.
- **Flex:** `flex-direction`, `flex-wrap`, `flex-flow`, `flex`, `flex-grow/shrink/basis`, `order`, `justify-content`, `align-items`,
  `align-self`, `gap/row-gap/column-gap`.
- **Grid:** `grid-template-columns/rows` (`px`, `%`, `fr`, `auto`, `repeat(n|auto-fill, …)`, `minmax`), `grid-column: span n`, `gap`.
- **Tekst:** `color`, `font-size`, `font-weight`, `font-style`, `font`, `line-height`, `text-align`, `text-decoration`, `text-transform`,
  `white-space`, `word-break/overflow-wrap`, `text-overflow: ellipsis`.
- **Tło:** `background(-color/-image/-size)`, `linear-gradient()` (dwa kolory, pion/poziom), `url()`, `object-fit` dla `<img>`.
- **Dynamika:** `transition` (kolor tekstu, tło, przezroczystość), `::before/::after` (reguły), `content`.

## Selektory
tag, `*`, `.klasa`, `#id`, `[attr]` i `[attr=|~=|^=|$=|*=||=]`, kombinatory ` `/`>`/`+`/`~` (z nawracaniem), grupy po przecinku,
`:hover :active :focus :focus-within :first-child :last-child :only-child :first-of-type :last-of-type :nth-child() :nth-last-child()
:nth-of-type() :empty :root :disabled :enabled :checked :link :not() :is() :where() :placeholder-shown`.
Specyficzność: pełna (id, klasy/atrybuty/pseudo, tagi) + kolejność źródłowa; `!important` i style inline.

## `@`-reguły
`@media` (`min/max-width/height`, `prefers-color-scheme`, `orientation`, `and`, przecinek, `not`), `@supports`/`@layer` (traktowane jak prawda);
`@import`, `@charset`, `@font-face`, `@keyframes` — ignorowane.

## Znane uproszczenia
Brak `float`, `transform`, `@keyframes`, `@font-face`; jeden font systemowy (pogrubienie/kursywa syntetyczne); marginesy
zwijają się tylko między rodzeństwem; `z-index` nie zmienia kolejności; `text-decoration` dziedziczy; `opacity` działa na kolory
elementu (nie na całą grupę). Pełna lista: [ROADMAP.md](../ROADMAP.md).
