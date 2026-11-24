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
# 0.5 — WAYLAND JEST DOMYŚLNYM BACKENDEM. Statyczny glibc nie potrafi dlopen-ować bibliotek,
# więc SDL jest budowany z WLINKOWANYM NA STAŁE Waylandem (libwayland-client/-cursor/-egl
# i libxkbcommon budowane tu ze źródeł jako .a) ORAZ z X11 (jako opcja / zapas).
# Wybór w czasie działania: SILVER_BACKEND=wayland|x11|auto (domyślnie wayland, a gdy brak
# kompozytora Wayland — X11). Bez Waylanda: SILVER_WAYLAND=0 (wtedy tylko X11, jak w 0.4).
# Bez PulseAudio/ALSA/KMS (wymagają dlopen).
#
# Źródła zależności Wayland: oficjalne wydania (gitlab.freedesktop.org) i GitHub (xkbcommon).
# Offline / za proxy:  WAYLAND_TARBALL=/ścieżka/wayland-*.tar.{gz,xz}
#                      WAYLAND_PROTOCOLS_TARBALL=…   XKBCOMMON_DIR=/ścieżka/do/źródeł
# Wymaga: meson, ninja, pkg-config, libexpat/libffi (statyczne .a), bison, flex.
#
# Zmienne: SDL2_VER, SDL2_TTF_VER, SDL2_IMAGE_VER, JOBS, CC
set -eu
cd "$(dirname "$0")"
HERE="$(pwd)"
OUT="$HERE/build"
DEPS="$OUT/deps"
SRC="$OUT/deps-src"
STAMP="static-v4-wayland"   # zmień przy modyfikacji flag configure → wymusza przebudowę
SDL2_VER="${SDL2_VER:-2.30.9}"
SDL2_TTF_VER="${SDL2_TTF_VER:-2.22.0}"
SDL2_IMAGE_VER="${SDL2_IMAGE_VER:-2.8.2}"
WAYLAND_VER="${WAYLAND_VER:-1.23.1}"
WAYLAND_PROTOCOLS_VER="${WAYLAND_PROTOCOLS_VER:-1.38}"
XKBCOMMON_VER="${XKBCOMMON_VER:-1.7.0}"
# SILVER_WAYLAND: 1 (domyślnie) = Wayland WYMAGANY — brak narzędzi/źródeł kończy się błędem (nie ma cichego zejścia do X11);
#                 auto = spróbuj, a gdy się nie da, zbuduj samo X11 (stare zachowanie, z ostrzeżeniem); 0 = tylko X11.
SILVER_WAYLAND="${SILVER_WAYLAND:-1}"
JOBS="${JOBS:-$(nproc 2>/dev/null || echo 2)}"
export CC="${CC:-cc}"

if [ -f "$DEPS/lib/libSDL2.a" ] && [ -f "$DEPS/lib/libSDL2_ttf.a" ] \
   && [ -f "$DEPS/lib/libSDL2_image.a" ] && [ -f "$OUT/static-libs.txt" ] \
   && [ "$(cat "$DEPS/.stamp" 2>/dev/null)" = "$STAMP" ] \
   && { [ "$SILVER_WAYLAND" = "0" ] || [ "$(cat "$OUT/wayland.flag" 2>/dev/null)" = "1" ] || [ "$SILVER_WAYLAND" = "auto" ]; } \
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

# pobierz tarball: curl → wget; zwraca 0 gdy plik istnieje
dl() { # url plik
    if command -v curl >/dev/null 2>&1; then curl -fsSL "$1" -o "$2" 2>/dev/null && return 0; fi
    if command -v wget >/dev/null 2>&1; then wget -q "$1" -O "$2" 2>/dev/null && return 0; fi
    rm -f "$2"; return 1
}

# SDL*: tarball z GitHuba, a gdy się nie da (proxy/403) — płytki git clone
fetch() { # nazwa repo tag katalog
    [ -f "$SRC/$4/configure" ] && return 0
    echo "[silver] pobieram $1 $3…"
    mkdir -p "$SRC/$4"
    if dl "https://codeload.github.com/libsdl-org/$2/tar.gz/refs/tags/$3" "$SRC/$4.tgz" \
       && tar xzf "$SRC/$4.tgz" -C "$SRC/$4" --strip-components=1 2>/dev/null; then return 0; fi
    command -v git >/dev/null 2>&1 || { echo "[silver] BŁĄD: nie mogę pobrać $1 (brak curl/wget/git)" >&2; exit 1; }
    rm -rf "$SRC/$4"
    GIT_LFS_SKIP_SMUDGE=1 git clone -q --depth 1 --branch "$3" "https://github.com/libsdl-org/$2" "$SRC/$4" >/dev/null 2>&1 \
        || { echo "[silver] BŁĄD: nie mogę pobrać $1 $3" >&2; exit 1; }
}

fetch SDL2       SDL       "release-$SDL2_VER"       SDL2
fetch SDL2_ttf   SDL_ttf   "release-$SDL2_TTF_VER"   SDL2_ttf
fetch SDL2_image SDL_image "release-$SDL2_IMAGE_VER" SDL2_image


# --- Wayland (statycznie): libwayland-client/-cursor/-egl + libxkbcommon + protokoły ---------
WAYLAND_OK=0
if [ "$SILVER_WAYLAND" = "1" ]; then
    export PKG_CONFIG_PATH="$DEPS/lib/pkgconfig:$DEPS/share/pkgconfig${PKG_CONFIG_PATH:+:$PKG_CONFIG_PATH}"
    export PATH="$DEPS/bin:$PATH"
    wl_missing=""
    for t in meson ninja pkg-config bison; do command -v $t >/dev/null 2>&1 || wl_missing="$wl_missing $t"; done
    need_a libexpat.a; need_a libffi.a
    if [ -n "$wl_missing" ] || [ -n "$missing" ]; then
        echo "[silver] UWAGA: brak narzędzi/bibliotek do zbudowania Waylanda:$wl_missing $missing" >&2
        echo "         Debian/Ubuntu: apt install meson ninja-build pkg-config libexpat1-dev libffi-dev bison flex" >&2
        if [ "$SILVER_WAYLAND" = "1" ]; then
            echo "[silver] BŁĄD: Wayland jest wymagany (domyślnie), a brakuje narzędzi — inaczej aplikacje działałyby przez X11/XWayland." >&2
            echo "         Doinstaluj pakiety wyżej i uruchom ponownie. Świadomie bez Waylanda: SILVER_WAYLAND=0, z zapasem na X11: SILVER_WAYLAND=auto" >&2
            exit 1
        fi
        echo "         → budowa BEZ Waylanda (tylko X11; SILVER_WAYLAND=$SILVER_WAYLAND)" >&2
        missing=""
    else
        fetch_src() { # nazwa katalog url_xz url_gz [tarball_env]
            [ -f "$SRC/$2/meson.build" ] && return 0
            mkdir -p "$SRC/$2"
            if [ -n "${5:-}" ] && [ -f "$5" ]; then tar xf "$5" -C "$SRC/$2" --strip-components=1 && return 0; fi
            for u in "$3" "$4"; do
                [ -z "$u" ] && continue
                if dl "$u" "$SRC/$2.tar" && tar xf "$SRC/$2.tar" -C "$SRC/$2" --strip-components=1 2>/dev/null; then return 0; fi
            done
            return 1
        }
        WL_URL="https://gitlab.freedesktop.org/wayland/wayland/-/releases/$WAYLAND_VER/downloads/wayland-$WAYLAND_VER.tar.xz"
        WP_URL="https://gitlab.freedesktop.org/wayland/wayland-protocols/-/releases/$WAYLAND_PROTOCOLS_VER/downloads/wayland-protocols-$WAYLAND_PROTOCOLS_VER.tar.xz"
        echo "[silver] Wayland $WAYLAND_VER (statyczny)"
        # 0.5: zapasowe źródła (gdy gitlab.freedesktop.org jest niedostępny / za proxy): orig.tar archiwum Ubuntu
        WL_URL2="http://archive.ubuntu.com/ubuntu/pool/main/w/wayland/wayland_1.22.0.orig.tar.gz"
        WP_URL2="http://archive.ubuntu.com/ubuntu/pool/main/w/wayland-protocols/wayland-protocols_1.34.orig.tar.xz"
        if fetch_src wayland wayland "$WL_URL" "$WL_URL2" "${WAYLAND_TARBALL:-}" \
           && fetch_src wayland-protocols wayland-protocols "$WP_URL" "$WP_URL2" "${WAYLAND_PROTOCOLS_TARBALL:-}"; then
            rm -rf "$SRC/wayland/b"
            ( meson setup "$SRC/wayland/b" "$SRC/wayland" --prefix="$DEPS" --libdir=lib --default-library=static \
                -Ddocumentation=false -Dtests=false -Ddtd_validation=false -Dscanner=true -Dlibraries=true >/dev/null \
              && ninja -C "$SRC/wayland/b" >/dev/null && ninja -C "$SRC/wayland/b" install >/dev/null ) \
              || { echo "[silver] BŁĄD: budowa Waylanda nie powiodła się" >&2; exit 1; }
            rm -rf "$SRC/wayland-protocols/b"
            ( meson setup "$SRC/wayland-protocols/b" "$SRC/wayland-protocols" --prefix="$DEPS" --libdir=lib -Dtests=false >/dev/null \
              && ninja -C "$SRC/wayland-protocols/b" install >/dev/null ) \
              || { echo "[silver] BŁĄD: wayland-protocols" >&2; exit 1; }
            # xkbcommon: źródła z GitHuba (tag) albo XKBCOMMON_DIR
            XKB="${XKBCOMMON_DIR:-$SRC/libxkbcommon}"
            if [ ! -f "$XKB/meson.build" ]; then
                echo "[silver] pobieram libxkbcommon $XKBCOMMON_VER…"
                rm -rf "$SRC/libxkbcommon"; mkdir -p "$SRC/libxkbcommon"
                # 0.5: najpierw tarball z GitHuba (bez gita), potem płytki git clone
                if dl "https://github.com/xkbcommon/libxkbcommon/archive/refs/tags/xkbcommon-$XKBCOMMON_VER.tar.gz" "$SRC/libxkbcommon.tgz" \
                   && tar xzf "$SRC/libxkbcommon.tgz" -C "$SRC/libxkbcommon" --strip-components=1 2>/dev/null; then :
                else
                    rm -rf "$SRC/libxkbcommon"
                    command -v git >/dev/null 2>&1 \
                      && GIT_LFS_SKIP_SMUDGE=1 git clone -q --depth 1 --branch "xkbcommon-$XKBCOMMON_VER" \
                           https://github.com/xkbcommon/libxkbcommon "$SRC/libxkbcommon" >/dev/null 2>&1 \
                      || { echo "[silver] BŁĄD: nie mogę pobrać libxkbcommon (ustaw XKBCOMMON_DIR)" >&2; exit 1; }
                fi
                XKB="$SRC/libxkbcommon"
            fi
            echo "[silver] libxkbcommon (statyczny)"
            rm -rf "$XKB/b"
            ( meson setup "$XKB/b" "$XKB" --prefix="$DEPS" --libdir=lib --default-library=static \
                -Denable-x11=false -Denable-wayland=false -Denable-docs=false -Denable-tools=false \
                -Denable-xkbregistry=false -Dxkb-config-root=/usr/share/X11/xkb -Dx-locale-root=/usr/share/X11/locale >/dev/null \
              && ninja -C "$XKB/b" >/dev/null && ninja -C "$XKB/b" install >/dev/null ) \
              || { echo "[silver] BŁĄD: budowa libxkbcommon nie powiodła się" >&2; exit 1; }
            WAYLAND_OK=1
        else
            if [ "$SILVER_WAYLAND" = "1" ]; then
                echo "[silver] BŁĄD: nie mogę pobrać źródeł Waylanda (ustaw WAYLAND_TARBALL / WAYLAND_PROTOCOLS_TARBALL albo SILVER_WAYLAND=auto|0)" >&2
                exit 1
            fi
            echo "[silver] UWAGA: nie mogę pobrać źródeł Waylanda — budowa tylko z X11 (ustaw WAYLAND_TARBALL / WAYLAND_PROTOCOLS_TARBALL)" >&2
        fi
    fi
fi
echo "$WAYLAND_OK" > "$OUT/wayland.flag"

# --- SDL2 ---------------------------------------------------------------------
if [ "$WAYLAND_OK" = "1" ]; then WL_FLAGS="--enable-video-wayland --disable-wayland-shared --disable-libdecor"; BK="X11 + Wayland wlinkowane"; else WL_FLAGS="--disable-video-wayland"; BK="X11 wlinkowany"; fi
echo "[silver] SDL2 $SDL2_VER (statyczny, $BK)"
rm -rf "$SRC/SDL2/b"; mkdir -p "$SRC/SDL2/b"
( cd "$SRC/SDL2/b" && CFLAGS="-O2 -fPIC" ../configure --prefix="$DEPS" \
    --enable-static --disable-shared \
    --disable-x11-shared \
    --disable-video-x11-xcursor --disable-video-x11-xinput --disable-video-x11-xfixes \
    --disable-video-x11-xrandr --disable-video-x11-scrnsaver --disable-video-x11-xshape \
    --disable-video-x11-xdbe \
    $WL_FLAGS --disable-video-kmsdrm --disable-video-vulkan \
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
wl=""
if [ "$WAYLAND_OK" = "1" ]; then
    wl=$(pkg-config --static --libs wayland-client wayland-cursor wayland-egl xkbcommon 2>/dev/null || echo "-lwayland-cursor -lwayland-egl -lwayland-client -lxkbcommon -lffi -lm")
fi
chain="-lSDL2_image -lSDL2_ttf -lSDL2 $sdl_static $wl $ft $png -lXext $x11 -lm -ldl -lpthread -lrt"
# bez -L (ścieżki dodaje LIBRARY_PATH); duplikaty usuwamy zostawiając OSTATNIE wystąpienie,
# bo przy linkowaniu statycznym biblioteka musi stać ZA tym, co jej używa (np. -lm na końcu)
echo "$chain" | tr ' ' '\n' | grep -v '^-L' | grep -v '\.a$' | grep -v '^$' \
    | awk '{t[NR]=$0; last[$0]=NR} END {for(i=1;i<=NR;i++) if(last[t[i]]==i) printf "%s ", t[i]}' > "$OUT/static-libs.txt"
echo >> "$OUT/static-libs.txt"
echo "$STAMP" > "$DEPS/.stamp"
echo "[silver] gotowe: $(ls "$DEPS"/lib/*.a | tr '\n' ' ')"
echo "[silver] flagi statyczne: $(cat "$OUT/static-libs.txt")"
echo "[silver] teraz: . native/env.sh static   &&   h# compile src/main.h#"
