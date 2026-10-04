#!/usr/bin/env sh
# `bit test`: typy H# (jeśli jest h#), test natywny, testy frameworków JS (jeśli jest node+npm).
set -eu
cd "$(dirname "$0")/../.."
if command -v h# >/dev/null 2>&1; then
    echo "== h# check"
    h# check src/lib.h# src/cli/silver_cli.h# src/engine_tests.h#
else
    echo "== h# check pominięte (brak h# w PATH)"
fi
echo "== natywny test dymny"
sh native/tests/run.sh
if command -v node >/dev/null 2>&1 && command -v npm >/dev/null 2>&1; then
    echo "== frameworki JS w QuickJS"
    sh native/tests/run-js.sh
else
    echo "== testy frameworków pominięte (brak node/npm)"
fi
echo "ALL: OK"
