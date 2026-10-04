# Dystrybucja

`silver new` kopiuje do projektu `packaging/`. `silver bundle <cel>` buduje wydanie (`bit build`) i woła odpowiedni skrypt.

| Cel | Wynik | Wymaga |
|---|---|---|
| `appimage` | `cache/bundle/<nazwa>-<wersja>-x86_64.AppImage` | `ldd`, `curl` (pobiera appimagetool) |
| `deb` | `cache/bundle/<nazwa>_<wersja>_<arch>.deb` | `dpkg-deb`; zależności: libsdl2*, czcionka, opcjonalnie zenity/libnotify |
| `macos` / `dmg` | `<nazwa>.app` / `.dmg` | macOS; `dylibbundler` (opcjonalnie), `SIGN_ID`, `NOTARY_PROFILE` → podpis + notaryzacja |
| `windows` | `.zip` (+ instalator Inno Setup jeśli jest `iscc`) | PowerShell; `SDL2*.dll` obok binarki; `SIGN_PFX` → Authenticode |

Zasoby (`frontend/` bez `node_modules` i źródeł) trafiają obok binarki; launcher ustawia katalog roboczy na katalog zasobów,
więc ścieżki względne działają jak w trybie dev. Dodatkowe pliki: `EXTRA_FILES="plik1 katalog2" silver bundle deb`.

> Zweryfikowano budowę `.deb`. Skrypty AppImage, macOS i Windows nie były uruchamiane w środowisku, w którym powstało 0.2.
