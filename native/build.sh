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

SDL_CFLAGS=$(sdl2-config --cflags 2>/dev/null || pkg-config --cflags sdl2 SDL2_ttf SDL2_image 2>/dev/null || echo "")

echo "[silver] libsilvershim.a (SDL2)"
# shellcheck disable=SC2086
$CC $CFLAGS $SDL_CFLAGS -c silver_shim.c -o "$OUT/silver_shim.o"
$AR rcs "$OUT/libsilvershim.a" "$OUT/silver_shim.o"

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
echo "[silver] gotowe: $(ls $OUT/*.a | tr '\n' ' ')"
