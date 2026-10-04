#!/usr/bin/env sh
# Buduje szablony frontendowe (TypeScript+Preact, Svelte 5) prawdziwym npm/esbuild i
# uruchamia je w QuickJS na lustrze DOM Silvera (bundle_runner). Wymaga node + npm + sieci.
set -eu
cd "$(dirname "$0")"
QJS="${QUICKJS_DIR:-../build/quickjs-src}"
[ -f ../build/libsilverjs.a ] || (cd .. && QUICKJS_DIR="$QJS" ./build.sh)
cc -O1 -Wall bundle_runner.c -o bundle_runner ../build/libsilverjs.a -lm -lpthread
TMP="${TMPDIR:-/tmp}/silver-js-test"
rm -rf "$TMP"; mkdir -p "$TMP"
for t in ts_preact svelte; do
    echo "== $t"
    cp -r "../../templates/$t/frontend" "$TMP/$t"
    ( cd "$TMP/$t" && npm install --no-audit --no-fund >/dev/null 2>&1 && npm run build >/dev/null 2>&1 )
    ./bundle_runner "$TMP/$t/dist/app.js" checks/notes.js root
done
echo "JS-TEST: OK"
