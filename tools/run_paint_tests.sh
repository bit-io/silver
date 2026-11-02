#!/bin/sh
# Testy rysowania end-to-end: H# (layout + paint) → prawdziwy shim SDL pod Xvfb → odczyt pikseli.
# Wymaga: libsdl2-dev libsdl2-ttf-dev libsdl2-image-dev xvfb oraz zbudowanego tools/hrun.
# Użycie: tools/run_paint_tests.sh [filtr]
set -e
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
HRUN="${HRUN:-$ROOT/tools/hrun/target/release/hrun}"
[ -x "$HRUN" ] || { echo "brak $HRUN" >&2; exit 2; }
T="$(mktemp -d)"; trap 'rm -rf "$T"' EXIT
gcc -O1 -shared -fPIC "$ROOT/native/silver_shim.c" -o "$T/libsilvershim.so" $(sdl2-config --cflags --libs) -lSDL2_ttf -lSDL2_image -lm
for f in "$ROOT"/src/*.h# "$ROOT"/src/engine/*.h# "$ROOT"/src/js/*.h#; do
  [ -f "$f" ] || continue; b="$(basename "$f")"; [ "$b" = "mod.h#" ] && continue; cp "$f" "$T/$b"
done
mkdir -p /tmp/silver_testdata && cp "$ROOT"/tools/testdata/* /tmp/silver_testdata/
SILVER_RENDERER=software HRUN_SHIM="$T/libsilvershim.so" xvfb-run -a "$HRUN" test "$T/paint_tests.h#" $1 2>&1 | grep -v '^warn\|  Searched\|  Expected'
