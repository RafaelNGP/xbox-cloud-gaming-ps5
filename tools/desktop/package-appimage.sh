#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 RafaelNGP
# Builds the Linux package of the desktop app, an AppImage (Linux and the
# Steam Deck), and checks it. Releases ship this, never a tarball.
#
#   tools/desktop/package-appimage.sh <build-dir> [out-dir]
#       -> <out-dir>/PSBox-<version>-linux-x86_64.AppImage   (out-dir: <build-dir>/pkg)
#
# Configures <build-dir> with the SDL3 app required (a new one: Ninja,
# RelWithDebInfo, clang when installed; CMAKE_PREFIX_PATH finds an SDL3 of
# your own), builds it and installs it into an AppDir with the install()
# rules of cmake/desktop_package.cmake (one PSBox/ folder: psbox, the bundled
# SDL3, assets, desktop entry, icon, README and licenses). Checks the folder:
# the files it must hold, none it must not (sign-in, logs, captures, keys),
# psbox's RUNPATH and bundled SDL3, no debug info. Packs it with appimagetool
# and runs the AppImage's psbox --selftest with LD_LIBRARY_PATH unset.
# XC_VERSION names it (default v<CMakeLists version>-dev); its debug info
# goes to symbols/<version>/psbox.debug.
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
version=${XC_VERSION:-v$project-dev}
appimage=$out/PSBox-$version-linux-x86_64.AppImage

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
cmake --build "$build" -j"$(nproc)"
ok "built psbox ($version)"

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
appdir=$work/AppDir
echo "== install into the AppDir"
cmake --install "$build" --prefix "$appdir" >/dev/null
pkg=$appdir/PSBox
[[ -x $pkg/psbox ]] || fail "the install made no PSBox/psbox"

echo "== checks in the package"
list=$(cd "$appdir" && find PSBox | sort)
private='(^|/)account\.json$|(^|/)autoplay\.txt$|\.log$|\.ppm$|\.aus$|\.h264$|\.pem$|\.key$|\.debug$|(^|/)settings\.json$|(^|/)library\.json$|(^|/)prices\.json$'
# The CA bundle is the one .pem that belongs there.
if grep -vx 'PSBox/assets/cacert.pem' <<<"$list" | grep -nE "$private"; then fail "a private file in the package"; fi
ok "no private file ($(cd "$appdir" && find PSBox -type f | wc -l) files)"
for f in psbox lib/libSDL3.so.0 psbox.desktop psbox.png README-linux.txt assets/cacert.pem assets/fonts/Inter-Regular.ttf \
         licenses/LICENSE licenses/THIRD_PARTY_NOTICES.md licenses/FFmpeg-LGPL-2.1.txt licenses/FFmpeg-NOTE.txt \
         licenses/SDL3-LICENSE.txt licenses/Inter-OFL.txt licenses/mbedtls-LICENSE.txt licenses/libdatachannel-MPL-2.0.txt \
         licenses/usrsctp-LICENSE.md licenses/libsrtp-LICENSE.txt licenses/plog-LICENSE.txt licenses/stb-LICENSE.txt \
         licenses/FSR1-MIT.txt licenses/Anime4K-MIT.txt; do
    grep -qx "PSBox/$f" <<<"$list" || fail "missing PSBox/$f"
done
ok "app, SDL3, assets, desktop entry, icon, README and licenses in place"
exe=$pkg/psbox
readelf -d "$exe" | grep -E 'RUNPATH|RPATH' | grep -qF '[$ORIGIN/lib]' || fail "psbox's RUNPATH is not \$ORIGIN/lib"
readelf -d "$exe" | grep -qF '[libSDL3.so.0]' || fail "psbox doesn't ask for libSDL3.so.0"
if readelf -S "$exe" | grep -qE '\.debug_(info|line)'; then fail "psbox still carries its debug info"; fi
[[ -s $root/symbols/$version/psbox.debug ]] || fail "symbols/$version/psbox.debug was not saved"
grep -q "version $version" "$pkg/README-linux.txt" || fail "README-linux.txt is not at $version"
ok "RUNPATH \$ORIGIN/lib, NEEDED libSDL3.so.0, stripped (symbols at symbols/$version/psbox.debug)"
sdl=$(env -u LD_LIBRARY_PATH ldd "$exe" | awk '/libSDL3\.so\.0/ {print $3}')
[[ $sdl == "$pkg/lib/libSDL3.so.0" ]] || fail "psbox loads SDL3 from '$sdl', not its own lib/"
ok "the bundled SDL3 is the one loaded"

# The AppDir's own files: the launcher (psbox finds SDL3 and its assets
# beside itself), the desktop entry and the icon.
cat >"$appdir/AppRun" <<'EOF'
#!/bin/sh
HERE=$(dirname "$(readlink -f "$0")")
exec "$HERE/PSBox/psbox" "$@"
EOF
chmod +x "$appdir/AppRun"
cp "$pkg/psbox.desktop" "$appdir/psbox.desktop"
cp "$pkg/psbox.png" "$appdir/psbox.png"
cp "$pkg/psbox.png" "$appdir/.DirIcon"

echo "== pack"
tool=${APPIMAGETOOL:-}
if [[ -z $tool ]]; then
    if command -v appimagetool >/dev/null; then
        tool=$(command -v appimagetool)
    else
        tool=$HOME/.cache/psbox-tool/appimagetool
        if [[ ! -x $tool ]]; then
            mkdir -p "$(dirname "$tool")"
            curl -sSLf -o "$tool.part" \
                https://github.com/AppImage/appimagetool/releases/download/continuous/appimagetool-x86_64.AppImage ||
                fail "appimagetool could not be downloaded"
            chmod +x "$tool.part"
            mv "$tool.part" "$tool"
        fi
    fi
fi
rm -f "$appimage"
# APPIMAGE_EXTRACT_AND_RUN: no FUSE needed (CI runners have none).
ARCH=x86_64 APPIMAGE_EXTRACT_AND_RUN=1 "$tool" --no-appstream "$appdir" "$appimage" >"$work/pack.out" 2>&1 ||
    { tail -20 "$work/pack.out"; fail "appimagetool failed"; }
[[ -s $appimage ]] || fail "appimagetool made no $appimage"
chmod +x "$appimage"
ok "$appimage ($(du -h "$appimage" | cut -f1))"

echo "== self-test of the AppImage"
mkdir -p "$work/data" "$work/selftest"
if ! env -u LD_LIBRARY_PATH -u XCLOUD_ASSETS APPIMAGE_EXTRACT_AND_RUN=1 SDL_VIDEO_DRIVER=offscreen SDL_AUDIO_DRIVER=dummy \
        XCLOUD_DATA_DIR="$work/data" "$appimage" --selftest "$work/selftest" >"$work/selftest.out" 2>&1; then
    tail -20 "$work/selftest.out"
    fail "psbox --selftest failed from the AppImage"
fi
ok "psbox --selftest passed from the AppImage ($(find "$work/selftest" -name '*.png' | wc -l) screenshots)"

echo "$appimage"
