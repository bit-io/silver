#!/usr/bin/env sh
# `bit test`: typy H# (jeśli jest h#), test natywny, testy frameworków JS (jeśli jest node+npm).
set -eu
cd "$(dirname "$0")/../.."
if command -v h# >/dev/null 2>&1; then
    echo "== h# check"
    h# check src/lib.h# src/cli/silver_cli.h# src/engine_tests.h#
else
    echo "== h# check pominięte (brak h# w PATH)"
fi
echo "== natywny test dymny"
sh native/tests/run.sh
echo "== efekty pikseli (silver_fx.h) — testy jednostkowe"
cc -O1 -Wall -Wextra -o native/tests/fx_unit native/tests/fx_unit.c -lm
native/tests/fx_unit
echo "== efekty warstw w shimie (renderer programowy, wymaga X: xvfb-run)"
cc -O1 -o native/tests/fx_gfx native/tests/fx_gfx.c native/silver_shim.c $(sdl2-config --cflags --libs) -lSDL2_ttf -lSDL2_image -lm
SILVER_RENDERER=software native/tests/fx_gfx
if command -v node >/dev/null 2>&1 && command -v npm >/dev/null 2>&1; then
    echo "== frameworki JS w QuickJS"
    sh native/tests/run-js.sh
else
    echo "== testy frameworków pominięte (brak node/npm)"
fi
echo "ALL: OK"
