#!/usr/bin/env sh
# Uzycie:
#   . native/env.sh            (z katalogu silver, np. cache/libs/silver)  -> link DYNAMICZNY SDL2
#   . native/env.sh static     -> link W PELNI STATYCZNY (h# compile bez --dynamic)
#                                 wymaga wczesniej: sh native/build-static-deps.sh
# Ustawia zmienne tak, aby `h# compile` zlinkowalo Silver (SDL2 + QuickJS + dialogi).
# Skrypt MUSI byc wczytany (source), a nie uruchomiony - inaczej zmienne znikna z powloka.
case "$0" in
    *env.sh)
        echo "[silver] BLAD: uruchomiono jako program - zmienne nie trafia do Twojej powloki." >&2
        echo "         Uzyj kropki (source):   . $0 ${1:-static}" >&2
        exit 1 ;;
esac
_d="$(cd "$(dirname "${BASH_SOURCE:-$0}")" && pwd)"
_mode="${1:-${SILVER_LINK:-dynamic}}"
if [ "$_mode" = "static" ]; then
    if [ ! -f "$_d/build/deps/lib/libSDL2.a" ] || [ ! -f "$_d/build/static-libs.txt" ]; then
        echo "[silver] BLAD: brak statycznego SDL2 - uruchom najpierw:  sh $_d/build-static-deps.sh" >&2
        unset _d _mode
        return 1 2>/dev/null || exit 1
    fi
    if [ ! -f "$_d/build/pkgconfig-static/silvershim.pc" ]; then
        # biblioteki .a Silvera jeszcze nie maja statycznych plikow .pc - dobuduj
        sh "$_d/build.sh" >/dev/null || { unset _d _mode; return 1 2>/dev/null || exit 1; }
    fi
    export LIBRARY_PATH="$_d/build/deps/lib:$_d/build${LIBRARY_PATH:+:$LIBRARY_PATH}"
    export PKG_CONFIG_PATH="$_d/build/pkgconfig-static:$_d/build/deps/lib/pkgconfig${PKG_CONFIG_PATH:+:$PKG_CONFIG_PATH}"
    export HSHARP_LDFLAGS="$(cat "$_d/build/static-libs.txt")"
    if ! command -v pkg-config >/dev/null 2>&1; then
        echo "[silver] UWAGA: brak pkg-config - h# zlinkuje -lsilvershim z native/build (zadziala dzieki HSHARP_LDFLAGS);" >&2
        echo "         zalecane: apt install pkg-config" >&2
    fi
    unset _d _mode
    echo "[silver] env OK (STATIC) - teraz: h# compile src/main.h#"
else
    export LIBRARY_PATH="$_d/build${LIBRARY_PATH:+:$LIBRARY_PATH}"
    export PKG_CONFIG_PATH="$_d/build/pkgconfig${PKG_CONFIG_PATH:+:$PKG_CONFIG_PATH}"
    # Czytane przez przebudowany kompilator h# (patch w codegen.rs); stary h# ignoruje.
    export HSHARP_LDFLAGS="-lSDL2_image -lSDL2_ttf -lSDL2 -lm"
    unset _d _mode
    echo "[silver] env OK - teraz: h# compile src/main.h# --dynamic"
fi
