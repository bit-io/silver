#!/usr/bin/env sh
# Buduje NAME.app (+ opcjonalnie .dmg) dla macOS:
#   make-app.sh <nazwa> <binarka> [wersja] [ikona.icns]
# Zmienne: SIGN_ID  ("Developer ID Application: …") → codesign (hardened runtime)
#          NOTARY_PROFILE (profil `xcrun notarytool store-credentials`) → notaryzacja + staple
#          MAKE_DMG=1 → tworzy .dmg przez hdiutil
set -eu
NAME="${1:?użycie: make-app.sh <nazwa> <binarka> [wersja] [ikona.icns]}"
BIN="${2:?brak binarki}"
VERSION="${3:-0.2.0}"
ICON="${4:-}"
FRONTEND_DIR="${FRONTEND_DIR:-frontend}"
APP="cache/bundle/$NAME.app"
rm -rf "$APP"
mkdir -p "$APP/Contents/MacOS" "$APP/Contents/Resources" "$APP/Contents/Frameworks"
cp "$BIN" "$APP/Contents/MacOS/$NAME-bin"
[ -d "$FRONTEND_DIR" ] && cp -r "$FRONTEND_DIR" "$APP/Contents/Resources/frontend" \
    && rm -rf "$APP/Contents/Resources/frontend/node_modules" "$APP/Contents/Resources/frontend/src"
for f in ${EXTRA_FILES:-}; do cp -r "$f" "$APP/Contents/Resources/"; done
[ -n "$ICON" ] && [ -f "$ICON" ] && cp "$ICON" "$APP/Contents/Resources/AppIcon.icns"

# launcher: katalog roboczy = Resources (ścieżki względne frontend/…)
cat > "$APP/Contents/MacOS/$NAME" <<LAUNCH
#!/usr/bin/env sh
DIR="\$(cd "\$(dirname "\$0")" && pwd)"
cd "\$DIR/../Resources" && exec "\$DIR/$NAME-bin" "\$@"
LAUNCH
chmod +x "$APP/Contents/MacOS/$NAME"

cat > "$APP/Contents/Info.plist" <<PLIST
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0"><dict>
  <key>CFBundleName</key><string>$NAME</string>
  <key>CFBundleDisplayName</key><string>$NAME</string>
  <key>CFBundleIdentifier</key><string>${BUNDLE_ID:-com.example.$NAME}</string>
  <key>CFBundleVersion</key><string>$VERSION</string>
  <key>CFBundleShortVersionString</key><string>$VERSION</string>
  <key>CFBundleExecutable</key><string>$NAME</string>
  <key>CFBundleIconFile</key><string>AppIcon</string>
  <key>CFBundlePackageType</key><string>APPL</string>
  <key>NSHighResolutionCapable</key><true/>
  <key>LSMinimumSystemVersion</key><string>11.0</string>
</dict></plist>
PLIST

# dołącz dylib-y (SDL2 itd.), jeśli jest dylibbundler (brew install dylibbundler)
if command -v dylibbundler >/dev/null 2>&1; then
    dylibbundler -od -b -x "$APP/Contents/MacOS/$NAME-bin" -d "$APP/Contents/Frameworks/" -p @executable_path/../Frameworks/ >/dev/null
else
    echo "[bundle] uwaga: brak dylibbundler — aplikacja będzie wymagać zainstalowanego SDL2 (brew install sdl2 sdl2_ttf sdl2_image)"
fi

if [ -n "${SIGN_ID:-}" ]; then
    echo "[bundle] podpisywanie ($SIGN_ID)…"
    codesign --force --deep --options runtime --timestamp --sign "$SIGN_ID" "$APP"
fi
if [ "${MAKE_DMG:-0}" = "1" ]; then
    DMG="cache/bundle/$NAME-$VERSION.dmg"
    rm -f "$DMG"
    hdiutil create -volname "$NAME" -srcfolder "$APP" -ov -format UDZO "$DMG" >/dev/null
    [ -n "${SIGN_ID:-}" ] && codesign --force --sign "$SIGN_ID" "$DMG"
    if [ -n "${SIGN_ID:-}" ] && [ -n "${NOTARY_PROFILE:-}" ]; then
        echo "[bundle] notaryzacja (to może potrwać)…"
        xcrun notarytool submit "$DMG" --keychain-profile "$NOTARY_PROFILE" --wait
        xcrun stapler staple "$DMG"
    fi
    echo "[bundle] gotowe: $DMG"
else
    echo "[bundle] gotowe: $APP"
fi
