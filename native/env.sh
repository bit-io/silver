#!/usr/bin/env sh
# Uzycie:  . native/env.sh      (z katalogu silver, np. cache/libs/silver)
# Ustawia zmienne tak, aby `h# compile` zlinkowalo Silver (SDL2 + QuickJS + dialogi).
_d="$(cd "$(dirname "${BASH_SOURCE:-$0}")" && pwd)"
export LIBRARY_PATH="$_d/build${LIBRARY_PATH:+:$LIBRARY_PATH}"
export PKG_CONFIG_PATH="$_d/build/pkgconfig${PKG_CONFIG_PATH:+:$PKG_CONFIG_PATH}"
# Czytane przez przebudowany kompilator h# (patch w codegen.rs); stary h# ignoruje.
export HSHARP_LDFLAGS="-lSDL2_image -lSDL2_ttf -lSDL2 -lm"
unset _d
echo "[silver] env OK - teraz: h# compile src/main.h# --dynamic"
