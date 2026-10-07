# Budowanie aplikacji na Silverze (Linux)

```sh
# 1. zależności systemowe
sudo apt install build-essential pkg-config git libsdl2-dev libsdl2-ttf-dev libsdl2-image-dev

# 2. biblioteki natywne (z katalogu biblioteki silver, np. cache/libs/silver)
sh native/build.sh

# 3. środowisko linkera (LIBRARY_PATH + PKG_CONFIG_PATH [+ HSHARP_LDFLAGS])
. native/env.sh

# 4. kompilacja aplikacji (z katalogu projektu) — DYNAMICZNIE
h# compile src/main.h# --dynamic
```

## Dlaczego `--dynamic`?
Domyslne `h# compile` linkuje z `-static`. Pelna statyka z glibc + SDL2 (X11/Wayland/audio)
zwykle sie nie udaje (`attempted static link of dynamic object libc.so.6`). Z `--dynamic`
SDL2 jest linkowane dynamicznie, a archiwa Silvera (`.a`) statycznie.

## Typowe bledy
| Komunikat | Przyczyna | Naprawa |
|---|---|---|
| `cannot find -lsilvershim` | linker nie widzi `native/build` | `. native/env.sh` |
| `undefined reference to SDL_*`, `TTF_*`, `IMG_*` | brak SDL2 na linii linkera | `. native/env.sh` (plik `.pc`) |
| `undefined reference to trunc/fmod/log...` | `-lm` przed archiwum (stary h#) | `.pc` albo przebudowany h# |
| `module 'ffi_shim' not found` | ostrzezenie starego resolvera modulow | nieszkodliwe; naprawione w h# z tej paczki |
