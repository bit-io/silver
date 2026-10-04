#!/usr/bin/env sh
# Buduje AppImage aplikacji Silver (bundluje SDL2 + zasoby frontendu).
#   make-appimage.sh <nazwa> <binarka> [wersja] [ikona.png]
# Zmienne: FRONTEND_DIR (domyślnie frontend), EXTRA_FILES ("plik1 plik2"),
#          APPIMAGETOOL (ścieżka; domyślnie pobierany z GitHuba do cache/).
set -eu
NAME="${1:?użycie: make-appimage.sh <nazwa> <binarka> [wersja] [ikona.png]}"
BIN="${2:?brak ścieżki do binarki}"
VERSION="${3:-0.2.0}"
ICON="${4:-}"
FRONTEND_DIR="${FRONTEND_DIR:-frontend}"
OUT="cache/bundle"
APPDIR="$OUT/$NAME.AppDir"
rm -rf "$APPDIR"
mkdir -p "$APPDIR/usr/bin" "$APPDIR/usr/lib" "$APPDIR/usr/share/$NAME"

cp "$BIN" "$APPDIR/usr/bin/$NAME"
chmod +x "$APPDIR/usr/bin/$NAME"
[ -d "$FRONTEND_DIR" ] && cp -r "$FRONTEND_DIR" "$APPDIR/usr/share/$NAME/frontend" \
    && rm -rf "$APPDIR/usr/share/$NAME/frontend/node_modules" "$APPDIR/usr/share/$NAME/frontend/src"
for f in ${EXTRA_FILES:-}; do cp -r "$f" "$APPDIR/usr/share/$NAME/"; done

# biblioteki współdzielone (bez glibc i sterowników GL/X, które muszą pochodzić z hosta)
ldd "$BIN" | awk '/=> \// { print $3 }' | while read -r lib; do
    case "$(basename "$lib")" in
        libc.so*|libm.so*|libdl.so*|libpthread.so*|librt.so*|ld-linux*|libGL.so*|libGLX*|libEGL*|libX11*|libxcb*|libXext*|libdrm*|libwayland*) ;;
        *) cp -L "$lib" "$APPDIR/usr/lib/" ;;
    esac
done

# font zapasowy, gdy system go nie ma (silver_find_system_font szuka m.in. DejaVu)
for ttf in /usr/share/fonts/truetype/dejavu/DejaVuSans.ttf /usr/share/fonts/dejavu/DejaVuSans.ttf; do
    [ -f "$ttf" ] && mkdir -p "$APPDIR/usr/share/fonts/truetype/dejavu" && cp "$ttf" "$APPDIR/usr/share/fonts/truetype/dejavu/" && break
done

cat > "$APPDIR/AppRun" <<RUN
#!/usr/bin/env sh
HERE="\$(dirname "\$(readlink -f "\$0")")"
export LD_LIBRARY_PATH="\$HERE/usr/lib:\${LD_LIBRARY_PATH:-}"
cd "\$HERE/usr/share/$NAME"      # ścieżki względne (frontend/…) działają jak w trybie dev
exec "\$HERE/usr/bin/$NAME" "\$@"
RUN
chmod +x "$APPDIR/AppRun"

cat > "$APPDIR/$NAME.desktop" <<DESK
[Desktop Entry]
Type=Application
Name=$NAME
Exec=$NAME
Icon=$NAME
Categories=Utility;
Terminal=false
DESK
if [ -n "$ICON" ] && [ -f "$ICON" ]; then cp "$ICON" "$APPDIR/$NAME.png"; else
    # minimalna ikona 1x1 (PNG), żeby appimagetool nie narzekał
    printf '\211PNG\r\n\032\n\000\000\000\rIHDR\000\000\000\001\000\000\000\001\010\006\000\000\000\037\025\304\211\000\000\000\rIDATx\234c\370\377\377?\000\005\376\002\376\247\065\201\204\000\000\000\000IEND\256B`\202' > "$APPDIR/$NAME.png"
fi

TOOL="${APPIMAGETOOL:-}"
if [ -z "$TOOL" ]; then
    if command -v appimagetool >/dev/null 2>&1; then TOOL=appimagetool
    else
        TOOL="cache/appimagetool-x86_64.AppImage"
        if [ ! -x "$TOOL" ]; then
            echo "[bundle] pobieram appimagetool…"
            mkdir -p cache
            curl -fsSL -o "$TOOL" https://github.com/AppImage/appimagetool/releases/download/continuous/appimagetool-x86_64.AppImage
            chmod +x "$TOOL"
        fi
    fi
fi
ARCH=x86_64 "$TOOL" --appimage-extract-and-run "$APPDIR" "$OUT/$NAME-$VERSION-x86_64.AppImage" 2>&1 | tail -3 \
    || ARCH=x86_64 "$TOOL" "$APPDIR" "$OUT/$NAME-$VERSION-x86_64.AppImage"
echo "[bundle] gotowe: $OUT/$NAME-$VERSION-x86_64.AppImage"
