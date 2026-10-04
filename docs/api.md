# API H# (Silver 0.2)

```h#
use "bit -> silver" from "silver"     ;; jedna linia: moduły app::, ui::, data::, dialog::, ipc::, window::, edit::
```
(Moduły są dostępne wprost po nazwie, np. `app::new(...)`, `ui::only(...)`. Silnik: `dom::`, `css::`, `layout::`…)

## app
**Tworzenie i pętla**

| Funkcja | Opis |
|---|---|
| `new(title, w, h) -> App` | okno + domyślny font systemowy |
| `run(app) -> App` | pętla jednego okna |
| `run_all([App]) -> [App]` | wiele okien (każde własny DOM/komendy/JS), do zamknięcia ostatniego |
| `step(app, wait_ms) -> App` | jedna iteracja (własna pętla) |
| `quit(app)` | zakończ pętlę |
| `on_tick(app, hook)` · `set_tick_interval(app, ms)` | cykliczny hook (domyślnie 100 ms; 0 = tylko przy zdarzeniach) |
| `on_event(app, hook)` | hook `(app, nazwa, json) -> app`: `click`, `keydown`, `resize`, `drop_file`, `link`, `task`, `ready`, `window_focus`, … |

**Zasoby**: `load_html(path)`, `load_html_str(html)`, `load_css(path)`, `load_css_str(css)`, `load_js([paths])`,
`with_font(ttf, size)`, `set_theme("auto"|"light"|"dark")`, `enable_hot_reload()` (HTML+CSS+JS, śledzi mtime).

**Komendy**: `command(name, handler)`, `command_html_only`, `command_js_only`, `command_cap(name, capability, handler)`,
`allow_js(capability)`, `call(app, cmd, json)`. Handler: `fn(string) -> string` (JSON → JSON).
Argumenty z HTML: `{"id","node","tag","value","checked","name","data":{…}}` (+ `key` dla `on-keydown`).

**Stan szablonów**: `set_state(key, value)`, `set_state_json(key, raw_json)`, `state(key)`, `rerender()`.
W HTML: `{{klucz}}` (tekst i atrybuty), `<li each="lista" as="el">{{el.pole}} {{index}}</li>`, `if="klucz"` / `if-not="klucz"`.

**DOM z H#** (po `attr_id`): `get_text`, `set_text`, `set_html`, `set_attr`, `remove_attr`, `add_class`, `remove_class`,
`toggle_class`, `remove_element`, `input_value`, `set_input_value`, `is_checked`, `set_checked`, `focus`, `blur`,
`query(selector)`, `query_all(selector)`, `node_by_id`.

**System**: `set_title`, `set_fullscreen`, `set_min_size`, `set_icon`, `scroll_to`, `clipboard_get/set`, `notify`, `is_dark`,
`emit(app, event, json)` (do `silver.listen`), `spawn_task(app, id, shell_cmd, done_command)`.

## ui — dyrektywy UI z handlerów
Handler nie ma dostępu do `App`, ale może zwrócić dyrektywy wykonywane po powrocie:
```h#
fn on_save(args: string) -> string is
    let r: string = data::set_bool(data::object(), "ok", true)
    return ui::with(r, ui::set_text("status", "Zapisano"))
end
```
`set_text`, `set_value`, `set_html`, `set_attr`, `add_class`, `remove_class`, `set_checked`, `set_state`, `set_title`,
`focus`, `scroll_top`, `notify`, `reload`, `close`; `with(result, dyrektywa)`, `only(dyrektywa)`.

## ipc — uprawnienia i walidacja
```h#
r = ipc::register_cap(r, "read_file", read_file, ipc::Origin::Both, "fs")   ;; wymaga uprawnienia "fs"
r = ipc::grant(r, "js", "fs")                                                ;; JS dostaje "fs"
r = ipc::register_full(r, "save", save, ipc::Origin::JsOnly, "", ["name"])   ;; wymagane klucze args
```
Kod z HTML ma wszystkie uprawnienia; JS tylko nadane. Błędy: `{"error": "...", "code": "not_found|forbidden|invalid_args"}`
(dla JS → odrzucony Promise).

## window
Zdarzenia (`InputEvent`: `kind`, `x`, `y`, `code`, `text`, `mods`, `clicks`), kody klawiszy (`key_enter()`…),
modyfikatory (`mod_shift()`…), `set_cursor`, `clipboard_*`, `notify`, `system_theme`, rozmiar/pozycja/pełny ekran/ikona/przezroczystość.

## data
Małe operacje na JSON (`get_str/int/bool/raw`, `set_*`, `items`, `keys`, `remove_key`) — wartości zagnieżdżone jako surowy JSON.

## dialog
`open_file`, `open_file_in`, `open_files` (ścieżki rozdzielone `\n`), `save_file`, `save_file_as`, `open_folder`, `info`, `warning`, `error`, `confirm`.
Linux: `zenity` albo `kdialog`; macOS: `osascript`; Windows: PowerShell. Anulowanie → `""`/`false`.

## edit
Czyste operacje edycji (UTF-8 na granicach znaków): `insert`, `backspace`, `delete_forward`, `move_left/right/home/end`,
`select_all`, `select_word_at`, `move_vertical`, `clamp_insert` — używane przez pola formularzy, dostępne dla własnych widgetów.

## Pola formularzy i zdarzenia HTML
`on-click`, `on-input`, `on-change`, `on-submit` (na `<form>`), `on-keydown`, `on-hover`. Enter w polu wysyła formularz;
Tab przechodzi między polami; `maxlength`, `placeholder`, `disabled`, `readonly`, `type=password|number|checkbox|radio`.
