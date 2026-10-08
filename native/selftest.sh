#!/usr/bin/env sh
# Buduje statyczny samodzielny test shima: native/build/silver_selftest
# Wymaga wcześniej: sh native/build-static-deps.sh && sh native/build.sh
set -eu
cd "$(dirname "$0")"
OUT="$(pwd)/build"
[ -f "$OUT/libsilvershim.a" ] && [ -f "$OUT/static-libs.txt" ] || {
    echo "[selftest] najpierw: sh build-static-deps.sh && sh build.sh" >&2; exit 1; }
# shellcheck disable=SC2046
${CC:-cc} -static -O1 selftest.c "$OUT/libsilvershim.a" -L"$OUT/deps/lib" $(cat "$OUT/static-libs.txt") -o "$OUT/silver_selftest"
echo "[selftest] zbudowano: $OUT/silver_selftest"
echo "[selftest] uruchom NA HOŚCIE:  SILVER_DEBUG=1 $OUT/silver_selftest"
