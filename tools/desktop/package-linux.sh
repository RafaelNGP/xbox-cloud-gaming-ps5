#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 RafaelNGP
# Builds the Linux package of the desktop app and checks it.
#
#   tools/desktop/package-linux.sh <build-dir> [out-dir]
#       -> <out-dir>/PSBox-<version>-linux-x86_64.tar.gz   (out-dir: <build-dir>/pkg)
#
# Configures <build-dir> with the SDL3 app required (a new one: Ninja,
# RelWithDebInfo, clang when installed; CMAKE_PREFIX_PATH finds an SDL3 of
# your own), builds it, packs it with CPack (cmake/desktop_package.cmake)
# and checks the tarball: the files it must hold, none it must not (sign-in,
# logs, captures, keys), psbox's RUNPATH and bundled SDL3, no debug info,
# and psbox --selftest run from the unpacked folder with LD_LIBRARY_PATH
# unset. XC_VERSION names it (default v<CMakeLists version>-dev); its
# debug info goes to symbols/<version>/psbox.debug.
set -euo pipefail

build=${1:?usage: package-linux.sh <build-dir> [out-dir]}
root=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
mkdir -p "$build"
build=$(cd "$build" && pwd)
out=${2:-$build/pkg}
mkdir -p "$out"
out=$(cd "$out" && pwd)
fail() { echo "package-linux: $*" >&2; exit 1; }
ok() { echo "  ok  $*"; }

project=$(sed -n 's/^project(xcloud_ps5 VERSION \([0-9.]*\) .*/\1/p' "$root/CMakeLists.txt")
version=${XC_VERSION:-v$project-dev}
name=PSBox-$version-linux-x86_64
tarball=$out/$name.tar.gz

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

echo "== pack"
rm -f "$tarball"
(cd "$build" && cpack --config CPackConfig.cmake -B "$out" >/dev/null)
rm -rf "$out/_CPack_Packages"
[[ -s $tarball ]] || fail "CPack made no $tarball"
ok "$tarball ($(du -h "$tarball" | cut -f1))"

echo "== checks in the package"
list=$(tar tzf "$tarball")
private='(^|/)account\.json$|(^|/)autoplay\.txt$|\.log$|\.ppm$|\.aus$|\.h264$|\.pem$|\.key$|\.debug$|(^|/)settings\.json$|(^|/)library\.json$|(^|/)prices\.json$'
# The CA bundle is the one .pem that belongs there.
if grep -vx 'PSBox/assets/cacert.pem' <<<"$list" | grep -nE "$private"; then fail "a private file in the package"; fi
if grep -vE '^PSBox(/|$)' <<<"$list"; then fail "entries outside PSBox/"; fi
ok "no private file, everything under PSBox/ ($(grep -vc '/$' <<<"$list") files)"
for f in psbox lib/libSDL3.so.0 psbox.desktop psbox.png README-linux.txt assets/cacert.pem assets/fonts/Inter-Regular.ttf \
         licenses/LICENSE licenses/THIRD_PARTY_NOTICES.md licenses/FFmpeg-LGPL-2.1.txt licenses/FFmpeg-NOTE.txt \
         licenses/SDL3-LICENSE.txt licenses/Inter-OFL.txt licenses/mbedtls-LICENSE.txt licenses/libdatachannel-MPL-2.0.txt \
         licenses/usrsctp-LICENSE.md licenses/libsrtp-LICENSE.txt licenses/plog-LICENSE.txt licenses/stb-LICENSE.txt; do
    grep -qx "PSBox/$f" <<<"$list" || fail "missing PSBox/$f"
done
ok "app, SDL3, assets, desktop entry, icon, README and licenses in place"

unpack=$(mktemp -d)
trap 'rm -rf "$unpack"' EXIT
tar xzf "$tarball" -C "$unpack"
exe=$unpack/PSBox/psbox
readelf -d "$exe" | grep -E 'RUNPATH|RPATH' | grep -qF '[$ORIGIN/lib]' || fail "psbox's RUNPATH is not \$ORIGIN/lib"
readelf -d "$exe" | grep -qF '[libSDL3.so.0]' || fail "psbox doesn't ask for libSDL3.so.0"
if readelf -S "$exe" | grep -qE '\.debug_(info|line)'; then fail "psbox still carries its debug info"; fi
[[ -s $root/symbols/$version/psbox.debug ]] || fail "symbols/$version/psbox.debug was not saved"
grep -q "version $version" "$unpack/PSBox/README-linux.txt" || fail "README-linux.txt is not at $version"
ok "RUNPATH \$ORIGIN/lib, NEEDED libSDL3.so.0, stripped (symbols at symbols/$version/psbox.debug)"
sdl=$(env -u LD_LIBRARY_PATH ldd "$exe" | awk '/libSDL3\.so\.0/ {print $3}')
[[ $sdl == "$unpack/PSBox/lib/libSDL3.so.0" ]] || fail "psbox loads SDL3 from '$sdl', not its own lib/"
ok "the bundled SDL3 is the one loaded"

before=$(cd "$unpack" && find PSBox | sort)
mkdir -p "$unpack/data" "$unpack/selftest"
if ! (cd "$unpack" && env -u LD_LIBRARY_PATH -u XCLOUD_ASSETS SDL_VIDEO_DRIVER=offscreen SDL_AUDIO_DRIVER=dummy \
        XCLOUD_DATA_DIR="$unpack/data" PSBox/psbox --selftest "$unpack/selftest" >"$unpack/selftest.out" 2>&1); then
    tail -20 "$unpack/selftest.out"
    fail "psbox --selftest failed from the unpacked folder"
fi
[[ $before == "$(cd "$unpack" && find PSBox | sort)" ]] || fail "the self-test wrote into the PSBox folder"
ok "psbox --selftest passed from the unpacked folder ($(find "$unpack/selftest" -name '*.png' | wc -l) screenshots)"
echo "$tarball"
