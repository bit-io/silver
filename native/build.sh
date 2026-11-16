#!/usr/bin/env sh
# Buduje statyczne biblioteki natywne Silver: libsilvershim.a, libsilverjs.a,
# libsilverdialogs.a → native/build/ i składa native/build/bit/ (to widzi `bit`).
# Wołane automatycznie przez `bit build` (patrz [build] native w Bit.hk); można też uruchomić ręcznie.
#
# 0.5: QuickJS (quickjs-ng 0.10.1) jest WBUDOWANY w repo (native/third_party/quickjs-ng) —
#      JS jest zawsze linkowany statycznie, bez git clone / sieci / zmiennych środowiskowych.
#
# Zmienne środowiskowe:
#   CC / AR           kompilator C / archiwizator (domyślnie cc / ar)
#   QUICKJS_DIR       własne źródła QuickJS (domyślnie wbudowane third_party/quickjs-ng)
#   SILVER_STATIC     1 (domyślnie): zbuduj statyczny SDL2 (X11 + Wayland) i linkuj w pełni statycznie
#                     0: tylko warianty dynamiczne (`h# compile --dynamic`)
#   SILVER_WAYLAND    1 (domyślnie): statyczny SDL z Waylandem, 0: tylko X11 (patrz build-static-deps.sh)
#   SILVER_TLS        1 (domyślnie, gdy jest OpenSSL .a): wss:// w WebSocket; 0: bez TLS
set -eu
cd "$(dirname "$0")"
OUT=build
mkdir -p "$OUT"
CC="${CC:-cc}"
AR="${AR:-ar}"
CFLAGS="${CFLAGS:--O2 -fPIC -Wall -Wextra -Wno-unused-parameter}"
VERSION="$(cat ../VERSION 2>/dev/null || echo 0.5.0)"

if [ "${SILVER_STATIC:-1}" = "1" ] && [ ! -f "$OUT/deps/lib/libSDL2.a" ]; then
    sh ./build-static-deps.sh
fi

if [ -x "$OUT/deps/bin/sdl2-config" ]; then
    SDL_CFLAGS="-I$OUT/deps/include $("$OUT/deps/bin/sdl2-config" --cflags)"
else
    SDL_CFLAGS=$(sdl2-config --cflags 2>/dev/null || pkg-config --cflags sdl2 SDL2_ttf SDL2_image 2>/dev/null || echo "")
fi

SHAPE_CFLAGS=""; SHAPE_LIBS=""
if command -v pkg-config >/dev/null 2>&1 && pkg-config --exists harfbuzz freetype2 2>/dev/null; then
    SHAPE_CFLAGS="-DSILVER_SHAPING=1 $(pkg-config --cflags harfbuzz freetype2)"
    SHAPE_LIBS="$(pkg-config --libs harfbuzz freetype2)"
    echo "[silver] kształtowanie tekstu: HarfBuzz + FreeType"
else
    echo "[silver] UWAGA: brak harfbuzz/freetype2 (pkg-config) — font-variant-alternates, font-language-override, font-palette i font-optical-sizing bez efektu (apt install libharfbuzz-dev libfreetype-dev)"
fi

echo "[silver] libsilvershim.a (SDL2)"
# shellcheck disable=SC2086
$CC $CFLAGS $SDL_CFLAGS $SHAPE_CFLAGS -DSILVER_VERSION="\"$VERSION\"" -c silver_shim.c -o "$OUT/silver_shim.o"
$AR rcs "$OUT/libsilvershim.a" "$OUT/silver_shim.o"
if [ -x "$OUT/deps/bin/sdl2-config" ]; then
    echo "[silver] libsilvershim_static.a (SDL2, domyślnie renderer programowy)"
    # shellcheck disable=SC2086
    $CC $CFLAGS $SDL_CFLAGS -DSILVER_VERSION="\"$VERSION\"" -DSILVER_STATIC_BUILD=1 -c silver_shim.c -o "$OUT/silver_shim_static.o"
    $AR rcs "$OUT/libsilvershim_static.a" "$OUT/silver_shim_static.o"
fi

echo "[silver] libsilverdialogs.a"
# shellcheck disable=SC2086
$CC $CFLAGS -c silver_dialogs_shim.c -o "$OUT/silver_dialogs_shim.o"
$AR rcs "$OUT/libsilverdialogs.a" "$OUT/silver_dialogs_shim.o"

# --- JS: QuickJS wbudowany w repo -------------------------------------------------------------
echo "[silver] libsilverjs.a (QuickJS wbudowany)"
QJS="${QUICKJS_DIR:-third_party/quickjs-ng}"
if [ ! -f "$QJS/quickjs.h" ]; then
    # Nie trzeba kopiować QuickJS ręcznie: gdy w repo brakuje third_party/quickjs-ng, pobieramy przypięty tag.
    QJS_REF="${QUICKJS_REF:-v0.10.1}"
    QJS="$OUT/quickjs-ng-src"
    if [ ! -f "$QJS/quickjs.h" ]; then
        echo "[silver] brak third_party/quickjs-ng — pobieram quickjs-ng $QJS_REF…"
        rm -rf "$QJS"; mkdir -p "$OUT"
        if command -v git >/dev/null 2>&1 && git clone -q --depth 1 --branch "$QJS_REF" https://github.com/quickjs-ng/quickjs.git "$QJS" 2>/dev/null; then :
        else
            rm -rf "$QJS"; mkdir -p "$QJS"
            TARURL="https://github.com/quickjs-ng/quickjs/archive/refs/tags/$QJS_REF.tar.gz"
            if command -v curl >/dev/null 2>&1; then curl -fsSL "$TARURL" | tar xz --strip-components=1 -C "$QJS"
            elif command -v wget >/dev/null 2>&1; then wget -qO- "$TARURL" | tar xz --strip-components=1 -C "$QJS"
            fi
        fi
    fi
    [ -f "$QJS/quickjs.h" ] || { echo "[silver] BŁĄD: nie udało się pobrać quickjs-ng (git/curl/wget + dostęp do github.com) — ustaw QUICKJS_DIR" >&2; exit 1; }
fi
[ -f ../src/js/silver_api.js ] && command -v python3 >/dev/null 2>&1 && python3 gen_api_embed.py >/dev/null

# TLS (wss://): statyczny OpenSSL, jeśli jest dostępny
TLS_CFLAGS=""; TLS_LIBS=""
if [ "${SILVER_TLS:-1}" = "1" ]; then
    for d in /usr/lib/x86_64-linux-gnu /usr/lib/aarch64-linux-gnu /usr/lib64 /usr/lib /usr/local/lib; do
        if [ -f "$d/libssl.a" ] && [ -f "$d/libcrypto.a" ]; then TLS_OK="$d"; break; fi
    done
    if [ -n "${TLS_OK:-}" ] && [ -f /usr/include/openssl/ssl.h -o -f /usr/include/x86_64-linux-gnu/openssl/ssl.h ]; then
        TLS_CFLAGS="-DSILVER_TLS=1"
        TLS_LIBS="-lssl -lcrypto -ldl -lpthread"
        echo "[silver] TLS (wss://): OpenSSL statyczny z $TLS_OK"
    else
        echo "[silver] UWAGA: brak libssl.a/libcrypto.a (apt install libssl-dev) — wss:// niedostępne"
    fi
fi
# shellcheck disable=SC2086
$CC $CFLAGS $TLS_CFLAGS -I"$QJS" -c silver_quickjs_shim.c -o "$OUT/silver_quickjs_shim.o"
OBJS="$OUT/silver_quickjs_shim.o"
for f in quickjs libregexp libunicode cutils dtoa xsum; do
    [ -f "$QJS/$f.c" ] || continue
    # shellcheck disable=SC2086
    $CC -O2 -fPIC -D_GNU_SOURCE -I"$QJS" -w -c "$QJS/$f.c" -o "$OUT/qjs_$f.o"
    OBJS="$OBJS $OUT/qjs_$f.o"
done
# shellcheck disable=SC2086
$AR rcs "$OUT/libsilverjs.a" $OBJS
echo "$TLS_LIBS" > "$OUT/js-libs.txt"

# --- pliki pkg-config -----------------------------------------------------------------------
ABS_OUT="$(cd "$OUT" && pwd)"
PC="$OUT/pkgconfig"
mkdir -p "$PC"
write_pc() { # nazwa opis libs
    {
        echo "prefix=$ABS_OUT"
        echo "libdir=$ABS_OUT"
        echo "Name: $1"
        echo "Description: $2"
        echo "Version: $VERSION"
        echo "Libs: -L\${libdir} -l$1 $3"
    } > "$PC/$1.pc"
}
write_pc silvershim    "Silver: warstwa okienna SDL2" "-lSDL2_image -lSDL2_ttf -lSDL2 $SHAPE_LIBS -lz -lm"
write_pc silverjs      "Silver: QuickJS + mostek JS"  "$TLS_LIBS -lm -lpthread"
write_pc silverdialogs "Silver: natywne dialogi"      ""

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
            echo "Version: $VERSION"
            echo "Libs: -L\${libdir} -L$ABS_OUT/deps/lib -l$2 $4"
        } > "$PCS/$1.pc"
    }
    write_pc_static silvershim    silvershim_static "Silver: warstwa okienna SDL2" "$STATIC_LIBS"
    write_pc_static silverjs      silverjs          "Silver: QuickJS + mostek JS"  "$TLS_LIBS -lm -lpthread"
    write_pc_static silverdialogs silverdialogs     "Silver: natywne dialogi"      ""
    echo "[silver] pkg-config (static): $PCS"
else
    echo "[silver] (statyczny SDL2 niezbudowany: sh native/build-static-deps.sh — wymagane do h# compile bez --dynamic)"
fi

sh ./pack-bit.sh

echo "[silver] gotowe: $(ls $OUT/*.a | tr '\n' ' ')"
echo "[silver] backend wideo: $( [ -f "$OUT/wayland.flag" ] && [ "$(cat "$OUT/wayland.flag")" = 1 ] && echo 'Wayland (domyślny) + X11 (opcja)' || echo 'X11 (statyczny SDL bez Waylanda albo wariant dynamiczny: Wayland z systemowego SDL2)')"
echo "[silver] pkg-config: . native/env.sh  (albo export PKG_CONFIG_PATH=$ABS_OUT/pkgconfig)"
