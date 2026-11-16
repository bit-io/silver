#!/usr/bin/env sh
# Składa katalog native/build/bit dla `bit`. `bit` linkuje WYŁĄCZNIE statycznie i przekazuje linkerowi
# tylko LIBRARY_PATH = native-lib-path z Bit.hk (bez pkg-config i bez HSHARP_LDFLAGS), więc
# KAŻDY plik lib*.a w tym katalogu jest SKRYPTEM LINKERA (GROUP) wskazującym archiwum rdzeniowe
# (ścieżką bezwzględną) i jego zależności:
#   libsilvershim.a    GROUP ( shim_static SDL2_image SDL2_ttf SDL2 [wayland] freetype png X11 … )
#   libsilverjs.a      GROUP ( silverjs_core  [ssl crypto]  -lm -lpthread )       ← 0.5: JS zawsze statyczny
#   libsilverdialogs.a GROUP ( silverdialogs_core )
# Dzięki temu samo `bit build` wystarcza — bez `. env.sh static`, bez zmiennych środowiskowych,
# a `extern static [c, "silverjs"]` zawsze rozwiązuje się do archiwum .a (a nie do .so).
# Bez statycznego SDL2 (SILVER_STATIC=0): kopiuje warianty dynamiczne (dla h# compile --dynamic).
set -eu
cd "$(dirname "$0")/build"
mkdir -p bit
B="$(pwd)/bit"
JSLIBS="$(cat js-libs.txt 2>/dev/null || true)"
# rdzenie (nazwy *_core.a) — skrypty GROUP wskazują je ścieżką bezwzględną
cp -f libsilverjs.a bit/libsilverjs_core.a
cp -f libsilverdialogs.a bit/libsilverdialogs_core.a
echo "GROUP ( $B/libsilverjs_core.a $JSLIBS -lm -lpthread )" > bit/libsilverjs.a
echo "GROUP ( $B/libsilverdialogs_core.a )" > bit/libsilverdialogs.a
if [ ! -f static-libs.txt ] || [ ! -f deps/lib/libSDL2.a ] || [ ! -f libsilvershim_static.a ]; then
    cp -f libsilvershim.a bit/libsilvershim.a
    echo "[silver] bit/: wariant dynamiczny (brak statycznego SDL2)"
    exit 0
fi
cp -f libsilvershim_static.a deps/lib/libSDL2.a deps/lib/libSDL2_ttf.a deps/lib/libSDL2_image.a bit/
# Wayland (0.5): statyczne libwayland-*/libxkbcommon z deps/lib trafiają obok, bo GROUP szuka -l tylko w LIBRARY_PATH
for w in libwayland-client.a libwayland-cursor.a libwayland-egl.a libxkbcommon.a; do
    [ -f "deps/lib/$w" ] && cp -f "deps/lib/$w" bit/
done
# Nasze biblioteki (shim + SDL2*) podajemy ŚCIEŻKAMI BEZWZGLĘDNYMI: -l wewnątrz skryptu GROUP szukałoby najpierw
# w katalogach systemowych (np. systemowe libSDL2_image.a z libjpeg) zamiast w tym katalogu. Reszta zależności
# (freetype, png, wayland, X11…) zostaje jako -l: biblioteki z deps/lib są dołączane przez -L w skrypcie.
rest="$(sed 's/-lSDL2_image//; s/-lSDL2_ttf//; s/-lSDL2\b//; s/-pthread//g' static-libs.txt)"
echo "GROUP ( $B/libsilvershim_static.a $B/libSDL2_image.a $B/libSDL2_ttf.a $B/libSDL2.a $rest )" > bit/libsilvershim.a
echo "[silver] bit/: libsilvershim.a (skrypt linkera: shim + statyczny SDL2 [+Wayland] + zależności), libsilverjs.a (statyczny QuickJS) — link przez samo LIBRARY_PATH"
