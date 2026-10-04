#!/usr/bin/env sh
# Buduje pakiet .deb: make-deb.sh <nazwa> <binarka> [wersja] [opis]
set -eu
NAME="${1:?użycie: make-deb.sh <nazwa> <binarka> [wersja] [opis]}"
BIN="${2:?brak ścieżki do binarki}"
VERSION="${3:-0.2.0}"
DESC="${4:-Aplikacja Silver}"
FRONTEND_DIR="${FRONTEND_DIR:-frontend}"
ARCH="$(dpkg --print-architecture 2>/dev/null || echo amd64)"
ROOT="cache/bundle/deb/${NAME}_${VERSION}_${ARCH}"
rm -rf "$ROOT"
mkdir -p "$ROOT/DEBIAN" "$ROOT/usr/bin" "$ROOT/usr/lib/$NAME" "$ROOT/usr/share/applications"

install -m755 "$BIN" "$ROOT/usr/lib/$NAME/$NAME"
[ -d "$FRONTEND_DIR" ] && cp -r "$FRONTEND_DIR" "$ROOT/usr/lib/$NAME/frontend" \
    && rm -rf "$ROOT/usr/lib/$NAME/frontend/node_modules" "$ROOT/usr/lib/$NAME/frontend/src"
for f in ${EXTRA_FILES:-}; do cp -r "$f" "$ROOT/usr/lib/$NAME/"; done

# wrapper ustawia katalog roboczy na katalog zasobów (ścieżki względne frontend/…)
cat > "$ROOT/usr/bin/$NAME" <<WRAP
#!/usr/bin/env sh
cd /usr/lib/$NAME && exec ./$NAME "\$@"
WRAP
chmod 755 "$ROOT/usr/bin/$NAME"

cat > "$ROOT/usr/share/applications/$NAME.desktop" <<DESK
[Desktop Entry]
Type=Application
Name=$NAME
Exec=$NAME
Terminal=false
Categories=Utility;
DESK

SIZE="$(du -sk "$ROOT/usr" | cut -f1)"
cat > "$ROOT/DEBIAN/control" <<CTL
Package: $NAME
Version: $VERSION
Section: utils
Priority: optional
Architecture: $ARCH
Installed-Size: $SIZE
Depends: libsdl2-2.0-0, libsdl2-ttf-2.0-0, libsdl2-image-2.0-0, fonts-dejavu-core | fonts-liberation
Recommends: zenity | kdialog, libnotify-bin
Maintainer: ${MAINTAINER:-You <you@example.com>}
Description: $DESC
 Aplikacja zbudowana z Silver (silnik HTML/CSS/JS bez WebView).
CTL
dpkg-deb --build --root-owner-group "$ROOT" "cache/bundle/${NAME}_${VERSION}_${ARCH}.deb" >/dev/null
echo "[bundle] gotowe: cache/bundle/${NAME}_${VERSION}_${ARCH}.deb"
