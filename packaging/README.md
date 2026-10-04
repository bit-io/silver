# Pakowanie

Skrypty kopiowane do projektów przez `silver new` i wołane przez `silver bundle`:

```
linux/make-appimage.sh <nazwa> <binarka> [wersja] [ikona.png]
linux/make-deb.sh      <nazwa> <binarka> [wersja] [opis]
macos/make-app.sh      <nazwa> <binarka> [wersja] [ikona.icns]     # SIGN_ID, NOTARY_PROFILE, MAKE_DMG=1
windows/make-installer.ps1 -Name … -Bin … -Version …               # SIGN_PFX, SIGN_PASSWORD, SDL2_BIN
```
Szczegóły: [docs/packaging.md](../docs/packaging.md).
