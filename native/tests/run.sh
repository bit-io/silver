#!/usr/bin/env sh
# Kompiluje i uruchamia test dymny natywnej warstwy (wymaga SDL2, SDL2_ttf,
# SDL2_image, quickjs-ng oraz Xvfb, jeśli nie ma prawdziwego ekranu).
set -eu
cd "$(dirname "$0")"
QJS="${QUICKJS_DIR:-../build/quickjs-src}"
[ -f ../build/libsilvershim.a ] || (cd .. && QUICKJS_DIR="$QJS" ./build.sh)
cc -O1 -g smoke.c -o smoke ../build/libsilvershim.a ../build/libsilverjs.a \
   $(sdl2-config --cflags --libs) -lSDL2_ttf -lSDL2_image -lm -lpthread
if [ -z "${DISPLAY:-}" ]; then
    xvfb-run -a ./smoke "$@"
else
    ./smoke "$@"
fi
