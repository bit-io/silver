#!/usr/bin/env sh
# Składa katalog native/build/bit dla `bit`. `bit` linkuje WYŁĄCZNIE statycznie i przekazuje linkerowi
# tylko LIBRARY_PATH = native-lib-path z Bit.hk (bez pkg-config i bez HSHARP_LDFLAGS), więc
# libsilvershim.a w tym katalogu jest SKRYPTEM LINKERA:  GROUP ( -lsilvershim_static -lSDL2 -lfreetype … )
#  - link pliku wykonywalnego (-static)  → ld bierze statyczne .a (SDL2 z tego katalogu, reszta systemowa),
#  - link biblioteki współdzielonej (silver.hlib) → te same -l rozwiązują się do .so / bibliotek PIC.
# Dzięki temu samo `bit build` wystarcza — bez `. env.sh static`, bez zmiennych środowiskowych.
# Bez statycznego SDL2 (SILVER_STATIC=0): kopiuje warianty dynamiczne (dla h# compile --dynamic).
set -eu
cd "$(dirname "$0")/build"
mkdir -p bit
cp -f libsilverjs.a libsilverdialogs.a bit/
if [ ! -f static-libs.txt ] || [ ! -f deps/lib/libSDL2.a ] || [ ! -f libsilvershim_static.a ]; then
    cp -f libsilvershim.a bit/libsilvershim.a
    echo "[silver] bit/: wariant dynamiczny (brak statycznego SDL2)"
    exit 0
fi
cp -f libsilvershim_static.a deps/lib/libSDL2.a deps/lib/libSDL2_ttf.a deps/lib/libSDL2_image.a bit/
# Nasze biblioteki (shim + SDL2*) podajemy ŚCIEŻKAMI BEZWZGLĘDNYMI: -l wewnątrz skryptu GROUP szukałoby najpierw
# w katalogach systemowych (np. systemowe libSDL2_image.a z libjpeg) zamiast w tym katalogu. Reszta zależności
# (freetype, png, X11…) zostaje jako -l: statyczna .a przy -static, współdzielona przy linku .so.
B="$(pwd)/bit"
rest="$(sed 's/-lSDL2_image//; s/-lSDL2_ttf//; s/-lSDL2\b//' static-libs.txt)"
echo "GROUP ( $B/libsilvershim_static.a $B/libSDL2_image.a $B/libSDL2_ttf.a $B/libSDL2.a $rest )" > bit/libsilvershim.a
echo "[silver] bit/: libsilvershim.a (skrypt linkera: shim + statyczny SDL2 + zależności) — link przez samo LIBRARY_PATH"
