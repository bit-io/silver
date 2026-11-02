#!/bin/sh
# Uruchamia testy silnika Silver (src/engine_tests.h#) interpreterem H# przez tools/hrun.
# Użycie: tools/run_engine_tests.sh [filtr-nazwy-testu]
# Zmienne: HRUN (ścieżka do binarki hrun), H_STD (katalog std H#, domyślnie /usr/lib/HackerOS/H#/std)
set -e
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
HRUN="${HRUN:-$ROOT/tools/hrun/target/release/hrun}"
[ -x "$HRUN" ] || { echo "brak $HRUN — zbuduj: (cd tools/hrun && cargo build --release)" >&2; exit 2; }
F="$(mktemp -d)"
trap 'rm -rf "$F"' EXIT
# płaski układ modułów (silnik rozwiązuje `mod x` względem katalogu pliku testowego)
for f in "$ROOT"/src/*.h# "$ROOT"/src/engine/*.h# "$ROOT"/src/js/*.h#; do
  [ -f "$f" ] || continue
  b="$(basename "$f")"; [ "$b" = "mod.h#" ] && continue
  cp "$f" "$F/$b"
done
"$HRUN" test "$F/engine_tests.h#" $1 2>&1 | grep -v '^warn\|  Searched\|  Expected'
