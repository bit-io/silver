#!/usr/bin/env sh
# Buduje statyczne biblioteki natywne Silver: libsilvershim.a, libsilverjs.a,
# libsilverdialogs.a → native/build/. Wołane automatycznie przez `bit build`
# (patrz [build] native w Bit.hk); można też uruchomić ręcznie.
#
# Zmienne środowiskowe:
#   CC            kompilator C (domyślnie cc)
#   AR            archiwizator (domyślnie ar)
#   QUICKJS_DIR   katalog z quickjs.h (+ źródłami quickjs.c) albo z gotową
#                 libquickjs.a; gdy brak, skrypt pobierze quickjs-ng z GitHuba
#   QUICKJS_REF   tag/gałąź quickjs-ng do pobrania (domyślnie master)
set -eu
cd "$(dirname "$0")"
OUT=build
mkdir -p "$OUT"
CC="${CC:-cc}"
AR="${AR:-ar}"
CFLAGS="${CFLAGS:--O2 -fPIC -Wall -Wextra -Wno-unused-parameter}"

# SILVER_STATIC=1 → przy braku statycznych SDL2/SDL2_ttf/SDL2_image zbuduj je (native/build/deps)
if [ "${SILVER_STATIC:-0}" = "1" ] && [ ! -f "$OUT/deps/lib/libSDL2.a" ]; then
    sh ./build-static-deps.sh
fi

# nagłówki: preferuj statyczny SDL2 z native/build/deps (jeśli zbudowany), inaczej systemowy
if [ -x "$OUT/deps/bin/sdl2-config" ]; then
    SDL_CFLAGS="-I$OUT/deps/include $("$OUT/deps/bin/sdl2-config" --cflags)"
else
SDL_CFLAGS=$(sdl2-config --cflags 2>/dev/null || pkg-config --cflags sdl2 SDL2_ttf SDL2_image 2>/dev/null || echo "")
fi

echo "[silver] libsilvershim.a (SDL2)"
# shellcheck disable=SC2086
$CC $CFLAGS $SDL_CFLAGS -c silver_shim.c -o "$OUT/silver_shim.o"
$AR rcs "$OUT/libsilvershim.a" "$OUT/silver_shim.o"
# wariant dla linkowania w pełni statycznego: domyślnie renderer programowy (patrz silver_shim.c)
if [ -x "$OUT/deps/bin/sdl2-config" ]; then
    echo "[silver] libsilvershim_static.a (SDL2, domyślnie renderer programowy)"
    # shellcheck disable=SC2086
    $CC $CFLAGS $SDL_CFLAGS -DSILVER_STATIC_BUILD=1 -c silver_shim.c -o "$OUT/silver_shim_static.o"
    $AR rcs "$OUT/libsilvershim_static.a" "$OUT/silver_shim_static.o"
fi

echo "[silver] libsilverdialogs.a"
# shellcheck disable=SC2086
$CC $CFLAGS -c silver_dialogs_shim.c -o "$OUT/silver_dialogs_shim.o"
$AR rcs "$OUT/libsilverdialogs.a" "$OUT/silver_dialogs_shim.o"

echo "[silver] libsilverjs.a (QuickJS)"
QJS="${QUICKJS_DIR:-}"
if [ -z "$QJS" ] || [ ! -f "$QJS/quickjs.h" ]; then
    if [ -f "$OUT/quickjs-src/quickjs.h" ]; then
        QJS="$OUT/quickjs-src"
    elif [ -f /usr/include/quickjs/quickjs.h ]; then
        QJS=/usr/include/quickjs
    elif command -v git >/dev/null 2>&1; then
        echo "[silver] pobieram quickjs-ng…"
        git clone --depth 1 ${QUICKJS_REF:+--branch "$QUICKJS_REF"} \
            https://github.com/quickjs-ng/quickjs.git "$OUT/quickjs-src" >/dev/null 2>&1
        QJS="$OUT/quickjs-src"
    else
        echo "[silver] BŁĄD: ustaw QUICKJS_DIR albo zainstaluj git" >&2
        exit 1
    fi
fi
# shellcheck disable=SC2086
[ -f ../src/js/silver_api.js ] && command -v python3 >/dev/null 2>&1 && python3 gen_api_embed.py >/dev/null
$CC $CFLAGS -I"$QJS" -c silver_quickjs_shim.c -o "$OUT/silver_quickjs_shim.o"
OBJS="$OUT/silver_quickjs_shim.o"
if [ -f "$QJS/quickjs.c" ]; then
    for f in quickjs libregexp libunicode dtoa; do
        [ -f "$QJS/$f.c" ] || continue
        # shellcheck disable=SC2086
        $CC -O2 -fPIC -D_GNU_SOURCE -I"$QJS" -w -c "$QJS/$f.c" -o "$OUT/qjs_$f.o"
        OBJS="$OBJS $OUT/qjs_$f.o"
    done
fi
# shellcheck disable=SC2086
$AR rcs "$OUT/libsilverjs.a" $OBJS
# --- pliki pkg-config (*.pc) --------------------------------------------------
# Kompilator H# dla `extern static [c, "nazwa"]` najpierw pyta pkg-config.
# Dzieki plikom .pc linker dostaje od razu KOMPLET bibliotek we wlasciwej
# kolejnosci (SDL2/SDL2_ttf/SDL2_image/libm ZA archiwum .a) - bez tego link
# konczyl sie "undefined reference to SDL_* / trunc / fmod ...".
# Uzycie:  . native/env.sh   (ustawia PKG_CONFIG_PATH i LIBRARY_PATH)
ABS_OUT="$(cd "$OUT" && pwd)"
PC="$OUT/pkgconfig"
mkdir -p "$PC"
write_pc() { # nazwa opis libs
    {
        echo "prefix=$ABS_OUT"
        echo "libdir=$ABS_OUT"
        echo "Name: $1"
        echo "Description: $2"
        echo "Version: 0.2.0"
        echo "Libs: -L\${libdir} -l$1 $3"
    } > "$PC/$1.pc"
}
write_pc silvershim    "Silver: warstwa okienna SDL2" "-lSDL2_image -lSDL2_ttf -lSDL2 -lm"
write_pc silverjs      "Silver: QuickJS + mostek JS"  "-lm -lpthread"
write_pc silverdialogs "Silver: natywne dialogi"      ""

# --- pkg-config dla linkowania W PEŁNI STATYCZNEGO (h# compile bez --dynamic) -----
# Osobny katalog: pkgconfig-static/. Zawiera komplet zależności SDL2 (freetype, png, X11, libc…)
# z native/build/static-libs.txt (tworzy go build-static-deps.sh). Aktywuje: . native/env.sh static
if [ -f "$OUT/static-libs.txt" ]; then
    PCS="$OUT/pkgconfig-static"
    mkdir -p "$PCS"
    STATIC_LIBS="$(cat "$OUT/static-libs.txt")"
    write_pc_static() { # nazwa_pc nazwa_biblioteki opis libs
        {
            echo "prefix=$ABS_OUT"
            echo "libdir=$ABS_OUT"
            echo "Name: $1"
            echo "Description: $3 (static)"
            echo "Version: 0.2.0"
            echo "Libs: -L\${libdir} -L$ABS_OUT/deps/lib -l$2 $4"
        } > "$PCS/$1.pc"
    }
    write_pc_static silvershim    silvershim_static "Silver: warstwa okienna SDL2" "$STATIC_LIBS"
    write_pc_static silverjs      silverjs          "Silver: QuickJS + mostek JS"  "-lm -lpthread"
    write_pc_static silverdialogs silverdialogs     "Silver: natywne dialogi"      ""
    echo "[silver] pkg-config (static): $PCS"
else
    echo "[silver] (statyczny SDL2 niezbudowany: sh native/build-static-deps.sh — wymagane do h# compile bez --dynamic)"
fi

echo "[silver] gotowe: $(ls $OUT/*.a | tr '\n' ' ')"
echo "[silver] pkg-config: . native/env.sh  (albo export PKG_CONFIG_PATH=$ABS_OUT/pkgconfig)"
