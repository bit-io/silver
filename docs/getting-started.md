# Pierwsze kroki

## Wymagania
- `bit` i kompilator `h#` (patrz ich README),
- kompilator C (`gcc`/`clang`), `git`,
- SDL2 + SDL2_ttf + SDL2_image (Linux: `apt install libsdl2-dev libsdl2-ttf-dev libsdl2-image-dev`; macOS: `brew install sdl2 sdl2_ttf sdl2_image`),
- opcjonalnie `node`/`npm` (TypeScript, Svelte), `zenity` lub `kdialog` (dialogi na Linuksie).

`silver doctor` sprawdza to wszystko.

## Nowy projekt
```sh
silver new moja-apka
cd moja-apka
bit            # = bit install → bit build → uruchomienie (sekcja [default-build])
```
Struktura:
```
Bit.hk                manifest bit (zależność: -> silver => ^0.2)
src/main.h#           backend H#
frontend/index.html   widok
frontend/style.css    style
frontend/app.js       JS (opcjonalnie)
packaging/            skrypty AppImage / deb / macOS / Windows
```

## Jak to działa
1. `app::new` tworzy okno SDL2. `app::load_html/css` parsują widok do **żywego DOM-u** (id węzłów stabilne).
2. Pętla `app::run` czeka na zdarzenia, przelicza układ **tylko po zmianach** i rysuje.
3. Zdarzenia wejściowe (mysz, klawiatura, tekst, IME, przewijanie, upuszczanie plików) trafiają do DOM-u,
   atrybutów `on-*` (komendy H#) i do JS (`addEventListener`).
4. `on_tick` wołany jest cyklicznie (`app::set_tick_interval`, domyślnie 100 ms) — zegary i stan z innych wątków.

## Ścieżki zasobów
Ścieżki (`frontend/…`, obrazki) są względne względem katalogu roboczego. Skrypty `silver bundle`
ustawiają go na katalog zasobów aplikacji.

## Import
```h#
use "bit -> silver" from "silver"      ;; po `bit add silver`; daje moduły app::, ui::, data::, dialog::, ipc::, window::, edit::
```

## Pułapki H#
- W literałach napisów `{` i `}` oznaczają interpolację — literalne klamry **podwajaj**: `"{{\"a\": 1}}"`.
  (Dotyczy też CSS/JSON/JS w stringach; szablony CLI robią to generatorem.)
- Parser H# potrafi się wywrócić na napisie, który ma jednocześnie interpolację i znaki spoza ASCII — unikaj tego mieszania.
- `fn(string) -> string` jako komenda nie widzi `App`; zmiany widoku zwracaj dyrektywami `ui::…` albo przez stan + `on_tick`.
