#!/usr/bin/env sh
# Buduje STATYCZNE biblioteki SDL2, SDL2_ttf i SDL2_image (libSDL2.a, libSDL2_ttf.a,
# libSDL2_image.a) → native/build/deps/.  Potrzebne, gdy `h# compile` linkuje w pełni
# statycznie (domyślnie, bez --dynamic): pakiety `libsdl2-dev` itd. nie dostarczają
# kompletu plików .a (np. brak libSDL2_ttf.a / libSDL2_image.a), stąd błąd
#   cannot find -lSDL2 / -lSDL2_ttf / -lSDL2_image
#
# Użycie:
#   sh native/build-static-deps.sh
#   . native/env.sh static
#   h# compile src/main.h#            # statycznie
#
# OpenGL/EGL, `offscreen` i `dummy` są wkompilowane. UWAGA: statyczny glibc nie potrafi
# dlopen-ować sterowników GL hosta (Mesa → libLLVM) — próba użycia GL kończy się SIGSEGV /
# "double free or corruption". Dlatego shim zbudowany dla statyku (libsilvershim_static.a)
# domyślnie używa renderera programowego; GL włączasz świadomie: SILVER_RENDERER=accelerated.
# `offscreen`/`dummy` działają bez problemu (bez dlopen).
#
# Ograniczenia pełnego statyku (glibc): SDL nie może wtedy ładować bibliotek przez dlopen,
# dlatego SDL jest zbudowany z X11 wlinkowanym na stałe (działa też pod XWayland),
# bez Wayland/KMS/PulseAudio/ALSA (te wymagają dlopen).
#
# Zmienne: SDL2_VER, SDL2_TTF_VER, SDL2_IMAGE_VER, JOBS, CC
set -eu
cd "$(dirname "$0")"
HERE="$(pwd)"
OUT="$HERE/build"
DEPS="$OUT/deps"
SRC="$OUT/deps-src"
STAMP="static-v3-gl"   # zmień przy modyfikacji flag configure → wymusza przebudowę
SDL2_VER="${SDL2_VER:-2.30.9}"
SDL2_TTF_VER="${SDL2_TTF_VER:-2.22.0}"
SDL2_IMAGE_VER="${SDL2_IMAGE_VER:-2.8.2}"
JOBS="${JOBS:-$(nproc 2>/dev/null || echo 2)}"
export CC="${CC:-cc}"

if [ -f "$DEPS/lib/libSDL2.a" ] && [ -f "$DEPS/lib/libSDL2_ttf.a" ] \
   && [ -f "$DEPS/lib/libSDL2_image.a" ] && [ -f "$OUT/static-libs.txt" ] \
   && [ "$(cat "$DEPS/.stamp" 2>/dev/null)" = "$STAMP" ] \
   && [ "${FORCE:-0}" != "1" ]; then
    echo "[silver] statyczne SDL2 już zbudowane ($DEPS) — FORCE=1 aby przebudować"
    exit 0
fi

# --- zależności systemowe w wersji statycznej (.a) -----------------------------
missing=""
need_a() { # nazwa_pliku
    for d in /usr/lib /usr/lib64 /usr/lib/x86_64-linux-gnu /usr/lib/aarch64-linux-gnu /usr/local/lib; do
        [ -f "$d/$1" ] && return 0
    done
    missing="$missing $1"
}
for a in libfreetype.a libpng.a libz.a libX11.a libxcb.a libXext.a libXau.a libXdmcp.a; do need_a "$a"; done
if [ -n "$missing" ]; then
    echo "[silver] BŁĄD: brak statycznych bibliotek:$missing" >&2
    echo "         Debian/Ubuntu: apt install libfreetype-dev libpng-dev zlib1g-dev libbz2-dev libbrotli-dev \\" >&2
    echo "                        libx11-dev libxext-dev libxau-dev libxdmcp-dev libxcb1-dev" >&2
    exit 1
fi
command -v make >/dev/null 2>&1 || { echo "[silver] BŁĄD: brak make" >&2; exit 1; }

mkdir -p "$SRC" "$DEPS"

fetch() { # nazwa repo tag katalog
    [ -f "$SRC/$4/configure" ] && return 0
    echo "[silver] pobieram $1 $3…"
    mkdir -p "$SRC/$4"
    if command -v curl >/dev/null 2>&1; then
        curl -fsSL "https://codeload.github.com/libsdl-org/$2/tar.gz/refs/tags/$3" -o "$SRC/$4.tgz"
    else
        wget -q "https://codeload.github.com/libsdl-org/$2/tar.gz/refs/tags/$3" -O "$SRC/$4.tgz"
    fi
    tar xzf "$SRC/$4.tgz" -C "$SRC/$4" --strip-components=1
}
fetch SDL2       SDL       "release-$SDL2_VER"       SDL2
fetch SDL2_ttf   SDL_ttf   "release-$SDL2_TTF_VER"   SDL2_ttf
fetch SDL2_image SDL_image "release-$SDL2_IMAGE_VER" SDL2_image

# --- SDL2 ---------------------------------------------------------------------
echo "[silver] SDL2 $SDL2_VER (statyczny, X11 wlinkowany)"
rm -rf "$SRC/SDL2/b"; mkdir -p "$SRC/SDL2/b"
( cd "$SRC/SDL2/b" && CFLAGS="-O2 -fPIC" ../configure --prefix="$DEPS" \
    --enable-static --disable-shared \
    --disable-x11-shared \
    --disable-video-x11-xcursor --disable-video-x11-xinput --disable-video-x11-xfixes \
    --disable-video-x11-xrandr --disable-video-x11-scrnsaver --disable-video-x11-xshape \
    --disable-video-x11-xdbe \
    --disable-video-wayland --disable-video-kmsdrm --disable-video-vulkan \
    --enable-video-opengl --enable-video-opengles --enable-video-opengles2 --disable-video-opengles1 \
    --enable-video-offscreen --enable-video-dummy \
    --disable-pulseaudio --disable-alsa --disable-jack --disable-pipewire --disable-sndio \
    --disable-esd --disable-arts --disable-nas \
    --disable-libudev --disable-dbus --disable-ime --disable-ibus --disable-fcitx \
    --disable-hidapi --disable-libsamplerate >/dev/null \
  && make -j"$JOBS" >/dev/null && make install >/dev/null )

export PATH="$DEPS/bin:$PATH"
export PKG_CONFIG_PATH="$DEPS/lib/pkgconfig${PKG_CONFIG_PATH:+:$PKG_CONFIG_PATH}"

# --- SDL2_ttf (FreeType z systemu, bez HarfBuzz) -------------------------------
echo "[silver] SDL2_ttf $SDL2_TTF_VER"
rm -rf "$SRC/SDL2_ttf/b"; mkdir -p "$SRC/SDL2_ttf/b"
( cd "$SRC/SDL2_ttf/b" && CFLAGS="-O2 -fPIC" ../configure --prefix="$DEPS" \
    --with-sdl-prefix="$DEPS" --enable-static --disable-shared \
    --disable-harfbuzz --disable-freetype-builtin --disable-sdltest >/dev/null \
  && make -j"$JOBS" >/dev/null && make install >/dev/null )

# --- SDL2_image (PNG przez libpng, reszta przez wbudowane stb_image) ------------
echo "[silver] SDL2_image $SDL2_IMAGE_VER"
rm -rf "$SRC/SDL2_image/b"; mkdir -p "$SRC/SDL2_image/b"
( cd "$SRC/SDL2_image/b" && CFLAGS="-O2 -fPIC" ../configure --prefix="$DEPS" \
    --with-sdl-prefix="$DEPS" --enable-static --disable-shared --disable-sdltest \
    --disable-png-shared --disable-jpg-shared \
    --disable-tif --disable-webp --disable-avif --disable-jxl --disable-lbm \
    --disable-pnm --disable-qoi --disable-xcf --disable-xpm --disable-xv >/dev/null \
  && make -j"$JOBS" >/dev/null && make install >/dev/null )

# --- pełna lista bibliotek do linkowania statycznego ----------------------------
# kolejność: moduły SDL → SDL2 → zależności (freetype/png/…) → X11 → libc
sdl_static=$("$DEPS/bin/sdl2-config" --static-libs 2>/dev/null | sed "s#-L[^ ]*##g; s#-lSDL2\\b##" || true)
ft=$(pkg-config --static --libs freetype2 2>/dev/null || echo "-lfreetype -lpng16 -lz -lbz2 -lbrotlidec -lbrotlicommon")
png=$(pkg-config --static --libs libpng 2>/dev/null || echo "-lpng16 -lz")
x11=$(pkg-config --static --libs x11 2>/dev/null || echo "-lX11 -lxcb -lXau -lXdmcp")
chain="-lSDL2_image -lSDL2_ttf -lSDL2 $sdl_static $ft $png -lXext $x11 -lm -ldl -lpthread -lrt"
# bez -L (ścieżki dodaje LIBRARY_PATH); duplikaty usuwamy zostawiając OSTATNIE wystąpienie,
# bo przy linkowaniu statycznym biblioteka musi stać ZA tym, co jej używa (np. -lm na końcu)
echo "$chain" | tr ' ' '\n' | grep -v '^-L' | grep -v '\.a$' | grep -v '^$' \
    | awk '{t[NR]=$0; last[$0]=NR} END {for(i=1;i<=NR;i++) if(last[t[i]]==i) printf "%s ", t[i]}' > "$OUT/static-libs.txt"
echo >> "$OUT/static-libs.txt"
echo "$STAMP" > "$DEPS/.stamp"
echo "[silver] gotowe: $(ls "$DEPS"/lib/*.a | tr '\n' ' ')"
echo "[silver] flagi statyczne: $(cat "$OUT/static-libs.txt")"
echo "[silver] teraz: . native/env.sh static   &&   h# compile src/main.h#"
