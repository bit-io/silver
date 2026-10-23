# Obsługiwany HTML i CSS (0.3)

## HTML
Tagi: strukturalne (`div`, `p`, `section`, `header`, `main`, …), nagłówki, listy (`ul/ol/li`, atrybuty `start`, `reversed`, `value`),
**tabele** (`table/thead/tbody/tfoot/caption/tr/td/th`, `colspan`, `rowspan`), formularze (`form`, `input`, `textarea`,
`select/option`, `button`, `label`), `img`, `a`, `pre`, `code`, `blockquote`, `hr`, `br`, inline (`span`, `b`, `strong`, `i`, `em`,
`u`, `s`, `small`, `mark`, `sub`, `sup`). `<style>`, `<link rel="stylesheet">`, `<script>`, `<title>`, komentarze, entity.
Atrybuty Silvera: `on-click`, `on-input`, `on-change`, `on-submit`, `on-keydown`, `on-hover`; szablony `{{klucz}}`, `each`, `as`, `if`, `if-not`.

## Wartości
Kolory: 148 nazw, `#rgb/#rgba/#rrggbb/#rrggbbaa`, `rgb()/rgba()/hsl()/hsla()`, `transparent`, `currentcolor`.
Długości: `px`, `%`, `em`, `rem`, `vw`, `vh`, `vmin`, `vmax`, `pt`, `pc`, `cm`, `mm`, `in`, `calc()`, `min()/max()/clamp()`.
`var(--x, fallback)`, `!important`.
**Słowa kluczowe całej kaskady** `inherit`, `initial`, `unset`, `revert` działają dla **każdej** obsługiwanej właściwości
(także skrótowych), `all: initial|inherit|unset` też. Prefiksy `-webkit-`, `-moz-`, `-ms-`, `-o-` są zdejmowane.
**Właściwości logiczne** (tryb poziomy; `direction: rtl` zamienia start↔end w osi inline): `margin/padding/inset/scroll-margin/scroll-padding-inline|block(-start|-end)`,
`border-inline|block(-start|-end)` oraz `-width/-style/-color`, `border-start-start-radius` i pokrewne, `inline-size`, `block-size`, `min/max-inline/block-size`,
`overflow-inline|block`.

## Pokrycie
**Wszystkie 474 właściwości CSS** z `tools/css_properties.txt` są **parsowane i walidowane** według gramatyki (`src/engine/props.h#`, tabela generowana
przez `tools/gen_props.py`): poprawna wartość trafia do stylu obliczonego (`xp`) z dziedziczeniem, `initial/inherit/unset` i `@supports`; niepoprawna
jest ignorowana jak w przeglądarce. **267 z 474 (56%)** ma realny wpływ na układ, rysowanie lub zachowanie — lista w `tools/css_rendered.txt`.
Z ~118 najczęściej używanych właściwości **116 (98%)** jest renderowanych; tylko `user-select` i `resize` są parsowane bez efektu
(Silver nie ma zaznaczania tekstu dokumentu ani uchwytu zmiany rozmiaru). Liczby: `python3 tools/css_coverage.py`.
Uwaga: to pokrycie *właściwości*, nie zgodność ze specyfikacją — wiele z nich działa w wariancie uproszczonym (patrz niżej).

## Właściwości
Legenda: **R** = renderowane, **P** = parsowane i dostępne w stylu obliczonym (np. dla `getComputedStyle`), bez wpływu na rysowanie.

- **Model pudełkowy (R):** `width/height`, `min-/max-*`, `padding*`, `margin*` (w tym `auto`), `border*` (per krawędź),
  `border-radius`, `box-sizing`, `aspect-ratio`, `box-shadow` (pierwszy cień, bez `inset`), `opacity` (**grupowa**: mnoży się przez potomków).
- **Obramowania (R):** `border-style` per krawędź: `solid`, `dashed`, `dotted`, `double`, `none/hidden` (`groove/ridge/inset/outset` jako `solid`);
  `outline` + `outline-style` + `outline-offset` (rysowany zawsze, nie tylko przy fokusie).
- **Wyświetlanie (R):** `display: block|inline|inline-block|none|flex|inline-flex|grid|inline-grid|list-item|table|table-row|table-cell|…|flow-root`,
  `visibility`, `overflow(-x/-y)`, `position: static|relative|absolute|fixed|sticky` + `top/right/bottom/left/inset`, **`z-index`**
  (kolejność rysowania i trafień; do 3 zagnieżdżonych kontekstów), **`float: left|right` + `clear`**, `pointer-events`, `cursor`.
  **`display: contents`** — element nie tworzy pudełka, jego dzieci trafiają do rodzica (style i dziedziczenie bez zmian); elementy zastępowane i `br` z `contents` zachowują się jak `none`.
- **Float (R):** floaty w kontenerze blokowym, opływanie wierszy tekstu (także w zagnieżdżonych blokach), `clear`, zawieranie floatów
  przez kontenery z `overflow`/`display: flow-root`/flex/grid/inline-block (BFC), „clearfix” przez `::after`.
  *Uproszczenia:* elementy blokowe z tłem nie są przycinane do floatu (jak w przeglądarkach), a pozycja floatu po marginesach rodzeństwa liczona jest po układzie tego rodzeństwa.
- **Margines (R):** zwijanie rodzic↔pierwsze/ostatnie dziecko (gdy rodzic nie ma padding/border/wysokości) oraz między rodzeństwem.
- **Tabele (R):** pełny algorytm kolumn (min/max-content, `width` w px i %), `colspan`, `rowspan`, `thead` nad ciałem i `tfoot` pod,
  `caption`, `border-spacing`, `border-collapse: collapse` (nakładanie ramek), `table-layout: fixed`, `vertical-align` komórek,
  tła wierszy i grup wierszy, tabela bez `width` dopasowuje się do treści.
- **Flex (R):** `flex-direction`, `flex-wrap`, `flex-flow`, `flex`, `flex-grow/shrink/basis`, `order`, `justify-content`, `align-items`,
  `align-self`, **`align-content`** (przy wieloliniowym flexie ze zdefiniowaną wysokością: `center`, `flex-end`, `space-between/around/evenly`; domyślne `normal` bez rozciągania linii),
  `gap/row-gap/column-gap`.
- **Grid (R):** `grid-template-columns/rows` (`px`, `%`, `fr`, `auto`, `repeat`, `minmax`), **`grid-template-areas` + `grid-area`**,
  **`grid-row/grid-column`** (`2`, `span 2`, `2 / span 3`, `2 / 4`), pełne auto-placement (`grid-auto-flow: row|dense`),
  `grid-auto-rows`, span w obu osiach, **`justify-items/justify-self`** (`center`, `start`, `end`, `stretch`), `place-items/place-content/place-self` (P dla `place-content`).
  **`grid-auto-flow: column`** (+ `dense`) z kolumnami niejawnymi wg `grid-auto-columns`.
  *Brak:* nazwane linie, `align-content` dla grid, wiersze `fr` bez wysokości kontenera (traktowane jak `auto`).
- **Wiele kolumn (R):** `column-count`, `column-width`, `columns`, `column-gap`, `column-rule(-width/-style/-color)`, `column-fill`, `break-inside: avoid`,
  `break-before/after: column`, `box-decoration-break` (slice/clone). Treść jest równoważona wierszami/blokami; pudełka przecinające granicę są dzielone.
  *Brak:* `column-span: all`.
- **Tekst (R):** `color`, `font-size`, `font-weight` (numeryczne 100–900), `font-style`, `font`, `line-height`, `text-align`, `text-decoration`
  (**kolor, styl `solid/double/dotted/dashed/wavy`, grubość**), `text-transform`, `white-space`, `word-break/overflow-wrap`,
  `text-overflow: ellipsis`, **`text-align: start|end|justify`** (zależne od `direction`), **`text-align-last`**, **`text-wrap: nowrap|balance`**, `white-space-collapse`,
  **`line-clamp` / `-webkit-line-clamp`**, `tab-size`, `text-underline-offset/position`, `line-break: anywhere`, **`letter-spacing`, `word-spacing`, `text-indent`, `text-shadow`** (pierwszy cień; rozmycie przybliżone), **`vertical-align`**
  (`top/middle/bottom/sub/super/text-top/text-bottom`, długość; w komórkach tabel — wyrównanie zawartości), **`direction: rtl`** (kolejność słów w wierszu odwrócona, `start/end`, logiczne marginesy; bez kształtowania pisma arabskiego i bez bidi mieszanych przebiegów), `hyphens` (P).
- **Czcionki (R):** `font-family` + **`@font-face`** (kroje `regular/bold/italic/bold-italic`; pliki **TTF/OTF/TTC** oraz **WOFF 1.0** (dekodowany w warstwie natywnej do SFNT; **WOFF2 nieobsługiwany** — wymaga Brotli),
  przy liście `src` wybierany jest najlepszy format: TTF/OTF > WOFF > reszta). Rodzina dobierana jest po **pierwszej nazwie** z listy; rodziny ogólne
  (`sans-serif`, `monospace`, …) używają czcionki domyślnej. Pogrubienie/kursywa są syntetyczne tylko wtedy, gdy brakuje prawdziwego kroju.
- **Listy (R):** `list-style` / `-type` (`disc`, `circle`, `square`, `decimal`, `decimal-leading-zero`, `lower/upper-alpha|latin|roman`, `lower-greek`, `none`) i `-position` (`inside|outside`).
- **Tło (R):** `background(-color/-image/-size)`, `background-repeat` (`repeat`, `no-repeat`, `repeat-x/y`), `background-position(-x/-y)`, `background-origin`, `background-clip` (`border|padding|content-box`),
  `object-position`, `image-rendering` (`pixelated`/`crisp-edges` → najbliższy sąsiad),
  `linear-gradient()` (dwa kolory, pion/poziom), `url()`, `object-fit` dla `<img>`.
- **Efekty (R):** `transform` (`translate/translateX/Y` w px i %, `scale/scaleX/Y`; poddrzewo przesuwane i skalowane razem, `transform-origin`),
  **`rotate()`, `skew*()`, `matrix()`** (funkcje składane w kolejności, osobne `rotate`/`scale`/`translate`; poddrzewo rysowane jako warstwa przekształcana macierzą, trafienia przez
  macierz odwrotną; zagnieżdżone obrócone elementy — tylko najbardziej wewnętrzna transformacja), **`backdrop-filter: blur()`**, `isolation`, `will-change` (konteksty stackowania),
  `content-visibility: hidden` + `contain-intrinsic-size`, `contain` (BFC i przycinanie), `appearance: none` dla checkbox/radio,
  `filter` (`grayscale`, `brightness`, `contrast`, `invert`, `sepia`, `opacity` — jako przekształcenie kolorów poddrzewa; obrazy nie są filtrowane),
  `clip-path` (`inset()` dokładnie; `circle()/ellipse()/polygon()` jako prostokąt otaczający).
- **Animacje (R):** `@keyframes` (`from/to/%`, wiele przystanków) + `animation` (`name`, `duration`, `delay`, `iteration-count`/`infinite`,
  `direction`, `fill-mode`, `timing-function` — `ease*`, `linear`, `cubic-bezier()`, `steps()`); animowane są długości, `padding/margin/border`,
  `width/height`, `top/left/…`, `border-radius`, `font-size`, `transform`, `filter`, `box-shadow`, **kolory i `opacity`**.
  **Przejścia** (`transition`): kolor tekstu/tła/`opacity` (dotychczasowy mechanizm) oraz geometria, `transform`, `filter`, `box-shadow`
  (lista `transition-property`, `duration`, `delay`, `timing-function`). `animation-play-state` jest zapamiętywany, ale nie wstrzymuje animacji.
- **Przewijanie (R):** `scroll-behavior: smooth` (skok do kotwicy `#id`, `scrollIntoView`), `scroll-margin-*`/`scroll-padding-*` (oś pionowa: góra),
  `scrollbar-width`, `scrollbar-color`, `accent-color`, `caret-color`. *Bez efektu (P):* `scroll-snap-*`, `overscroll-behavior`, `user-select`, `resize`, `background-attachment`.
- **Pozostałe (P):** wszystkie właściwości SVG (`fill`, `stroke*`, `cx/cy/r/d`, …), `mask*`, `mix-blend-mode`, `border-image*`, `writing-mode` (tylko poziomy), `zoom`,
  `anchor-*`/`position-*`, animacje sterowane przewijaniem, `font-variant-*`, `font-stretch`, `hyphens`, `quotes`, `list-style-image`.
- **Treść generowana (R):** `::before/::after` z `content`: napisy z ucieczkami CSS (`"\2022"`), `attr()`, `counter()/counters()`,
  `open-quote/close-quote`; `counter-reset/increment/set` z zakresami (zagnieżdżone listy). Element pseudo trafia w hit-testingu do swojego właściciela.

## Selektory
tag, `*`, `.klasa`, `#id`, `[attr]` i `[attr=|~=|^=|$=|*=||=]`, kombinatory ` `/`>`/`+`/`~` (z nawracaniem), grupy po przecinku,
`:hover :active :focus :focus-visible :focus-within :first-child :last-child :only-child :first-of-type :last-of-type :only-of-type
:nth-child() :nth-last-child() :nth-of-type() :nth-last-of-type() :empty :root :scope :disabled :enabled :checked :required :optional
:read-write :default :indeterminate :valid :invalid :link :any-link :placeholder-shown :lang() :dir() :defined`.
Nowe w 0.3: **`:nth-child(2n+1 of S)`** i `:nth-last-child(… of S)`, **`:has()`** (potomek, `> dziecko`, `+ następny`, `~ późniejsze rodzeństwo`),
**pełne selektory złożone wewnątrz `:is()`, `:where()`, `:not()`** (np. `p:not(.box > p)`).
Specyficzność: pełna + kolejność źródłowa; `!important` i style inline.

## `@`-reguły
- `@media`: `min/max-width/height`, składnia zakresów `(width >= 600px)`, `(400px <= width <= 900px)`, `and`, `or`, `not`, przecinek,
  `orientation`, `aspect-ratio` (`min-/max-`), `resolution`, `prefers-color-scheme`, `prefers-reduced-motion`, `hover`, `pointer`, `display-mode`.
- `@supports`: prawdziwa ewaluacja (`(prop: value)`, `not`, `and`, `or`, `selector()`) względem listy właściwości silnika.
- `@layer`: kolejność warstw jak w specyfikacji (bez warstwy > późniejsza warstwa; dla `!important` odwrotnie), warstwy zagnieżdżone.
- `@import` (tylko pliki lokalne, z warunkiem media, do 8 poziomów), `@font-face`, `@keyframes`, `@charset` (ignorowany).
- **`@container`** z prawdziwą ewaluacją: `container-type`, `container-name`, `container: nazwa / typ`, warunki szerokości/wysokości względem najbliższego kontenera
  (dwa przebiegi układu: pomiar kontenerów, potem style). Brak jednostek `cqw/cqh` i `style()`. `@scope` — treść włączana bez ewaluacji. `@property`, `@page` — ignorowane.
- **Zagnieżdżanie CSS** (`&`, selektory potomne bez `&`, zagnieżdżone `@media/@supports/@container/@layer`, deklaracje przed i po regułach zagnieżdżonych).

## Znane uproszczenia
`position: sticky` wewnątrz kontenerów przewijanych (działa względem przewijania dokumentu), `:host` w Shadow DOM, WOFF2,
listy zapasowych krojów (`font-family` używa pierwszej nazwy), `zoom`, pełne Bidi (mieszane kierunki w wierszu) i shaping złożonych pism, pisma pionowe,
zagnieżdżone obrócone elementy (działa tylko najbardziej wewnętrzna rotacja), `column-span`, tryby mieszania (`mix-blend-mode`). Pełna lista: [ROADMAP.md](../ROADMAP.md).
