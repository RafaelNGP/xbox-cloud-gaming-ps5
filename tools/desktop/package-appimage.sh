#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 RafaelNGP
# Packages PSBox Cloud Gaming as a standalone universal AppImage for Linux and Steam Deck.
#
#   tools/desktop/package-appimage.sh <build-dir> [out-dir]
#       -> <out-dir>/PSBox-<version>-x86_64.AppImage
#
set -euo pipefail

build=${1:?usage: package-appimage.sh <build-dir> [out-dir]}
root=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
mkdir -p "$build"
build=$(cd "$build" && pwd)
out=${2:-$build/pkg}
mkdir -p "$out"
out=$(cd "$out" && pwd)

fail() { echo "package-appimage: $*" >&2; exit 1; }
ok() { echo "  ok  $*"; }

project=$(sed -n 's/^project(xcloud_ps5 VERSION \([0-9.]*\) .*/\1/p' "$root/CMakeLists.txt")
version=${XC_VERSION:-v$project}
name=PSBox-$version-x86_64
appimage_target=$out/$name.AppImage

echo "== configure and build ($build)"
args=(-DXCLOUD_DESKTOP_APP=ON "-DXC_PACKAGE_VERSION=$version")
[[ -n ${CMAKE_PREFIX_PATH:-} ]] && args+=("-DCMAKE_PREFIX_PATH=$CMAKE_PREFIX_PATH")
if [[ ! -f $build/CMakeCache.txt ]]; then
    args+=(-G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo)
    if [[ -z ${CC:-} ]] && command -v clang >/dev/null && command -v clang++ >/dev/null; then
        args+=(-DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++)
    fi
fi
cmake -S "$root" -B "$build" "${args[@]}" >/dev/null
cmake --build "$build" --target psbox -j"$(nproc)"
ok "built psbox ($version)"

# Render icon if needed
if [[ ! -f "$build/psbox.png" ]]; then
    cmake --build "$build" --target xcloud-cli -j"$(nproc)" >/dev/null
    "$build/xcloud-cli" render-icon "$build/psbox.png"
fi

appdir=$(mktemp -d /tmp/psbox-appdir-XXXXXX)
trap 'rm -rf "$appdir"' EXIT

echo "== prepare AppDir structure"
mkdir -p "$appdir/usr/bin"
mkdir -p "$appdir/usr/lib"
mkdir -p "$appdir/usr/share/psbox/assets/fonts"
mkdir -p "$appdir/usr/share/applications"
mkdir -p "$appdir/usr/share/icons/hicolor/256x256/apps"
mkdir -p "$appdir/usr/share/licenses/psbox"

# Copy binary
cp "$build/psbox" "$appdir/usr/bin/psbox"
strip --strip-unneeded "$appdir/usr/bin/psbox" 2>/dev/null || true

# Copy bundled SDL3
sdl_lib=$(ldd "$build/psbox" | awk '/libSDL3\.so/ {print $3}' | head -n 1)
if [[ -n "$sdl_lib" && -f "$sdl_lib" ]]; then
    cp -L "$sdl_lib" "$appdir/usr/lib/libSDL3.so.0"
    (cd "$appdir/usr/lib" && ln -sf libSDL3.so.0 libSDL3.so)
fi

# Assets
cp "$root/assets/cacert.pem" "$appdir/usr/share/psbox/assets/"
cp "$root/assets/fonts/Inter-Regular.ttf" "$appdir/usr/share/psbox/assets/fonts/"
[[ -f "$root/assets/fonts/Inter-SemiBold.ttf" ]] && cp "$root/assets/fonts/Inter-SemiBold.ttf" "$appdir/usr/share/psbox/assets/fonts/"
[[ -f "$root/assets/fonts/Inter-Bold.ttf" ]] && cp "$root/assets/fonts/Inter-Bold.ttf" "$appdir/usr/share/psbox/assets/fonts/"

# Metadata & desktop integration
cp "$root/linux/psbox.desktop" "$appdir/psbox.desktop"
cp "$root/linux/psbox.desktop" "$appdir/usr/share/applications/psbox.desktop"
cp "$build/psbox.png" "$appdir/psbox.png"
cp "$build/psbox.png" "$appdir/.DirIcon"
cp "$build/psbox.png" "$appdir/usr/share/icons/hicolor/256x256/apps/psbox.png"

# Licenses
cp "$root/LICENSE" "$appdir/usr/share/licenses/psbox/"
cp "$root/THIRD_PARTY_NOTICES.md" "$appdir/usr/share/licenses/psbox/"

# AppRun script (Steam Deck / Gamescope / Linux universal launcher)
cat << 'EOF' > "$appdir/AppRun"
#!/usr/bin/env bash
set -e

# Resolve AppDir directory
HERE="$(dirname "$(readlink -f "${0}")")"

export PATH="${HERE}/usr/bin:${PATH}"
export LD_LIBRARY_PATH="${HERE}/usr/lib:${HERE}/lib:${LD_LIBRARY_PATH:-}"
export XCLOUD_ASSETS="${HERE}/usr/share/psbox/assets"

# Support Steam Deck / SteamOS Game Mode
if [ -z "${XDG_DATA_HOME:-}" ]; then
    export XDG_DATA_HOME="${HOME}/.local/share"
fi

# Pass arguments directly to psbox
exec "${HERE}/usr/bin/psbox" "$@"
EOF
chmod +x "$appdir/AppRun"
ok "AppDir populated at $appdir"

echo "== obtain appimagetool"
TOOL_DIR="$HOME/.cache/psbox-tool"
mkdir -p "$TOOL_DIR"
APPIMAGETOOL=""
if command -v appimagetool >/dev/null 2>&1; then
    APPIMAGETOOL=$(command -v appimagetool)
elif [[ -x "$TOOL_DIR/appimagetool" ]]; then
    APPIMAGETOOL="$TOOL_DIR/appimagetool"
else
    echo "  downloading appimagetool..."
    curl -sL -o "$TOOL_DIR/appimagetool" https://github.com/AppImage/appimagetool/releases/download/continuous/appimagetool-x86_64.AppImage
    chmod +x "$TOOL_DIR/appimagetool"
    APPIMAGETOOL="$TOOL_DIR/appimagetool"
fi

echo "== package AppImage ($appimage_target)"
rm -f "$appimage_target"
(
    export ARCH=x86_64
    export APPIMAGE_EXTRACT_AND_RUN=1
    "$APPIMAGETOOL" "$appdir" "$appimage_target" >/dev/null
)

[[ -s "$appimage_target" ]] || fail "Failed to generate $appimage_target"
chmod +x "$appimage_target"
ok "$appimage_target ($(du -h "$appimage_target" | cut -f1))"

echo "== verify AppImage execution"
mkdir -p "$build/appimage-test-data"
if ! env -u LD_LIBRARY_PATH APPIMAGE_EXTRACT_AND_RUN=1 SDL_VIDEO_DRIVER=offscreen SDL_AUDIO_DRIVER=dummy \
     XCLOUD_DATA_DIR="$build/appimage-test-data" "$appimage_target" --help >/dev/null 2>&1; then
    fail "AppImage execution failed test verification"
fi
ok "AppImage starts and responds correctly"

echo "$appimage_target"
