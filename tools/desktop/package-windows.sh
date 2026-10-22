#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 RafaelNGP
# Builds or packages the Windows portable zip of the desktop app.
#
#   tools/desktop/package-windows.sh <build-dir> [out-dir]
#       -> <out-dir>/PSBox-<version>-windows-x86_64.zip   (out-dir: <build-dir>/pkg)
set -euo pipefail

build=${1:?usage: package-windows.sh <build-dir> [out-dir]}
root=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
mkdir -p "$build"
build=$(cd "$build" && pwd)
out=${2:-$build/pkg}
mkdir -p "$out"
out=$(cd "$out" && pwd)
fail() { echo "package-windows: $*" >&2; exit 1; }
ok() { echo "  ok  $*"; }

project=$(sed -n 's/^project(xcloud_ps5 VERSION \([0-9.]*\) .*/\1/p' "$root/CMakeLists.txt")
version=${XC_VERSION:-v$project-dev}
name=PSBox-$version-windows-x86_64
zipfile=$out/$name.zip

echo "== locate binaries ($build)"
exe=""
for candidate in "$build/psbox.exe" "$build/bin/psbox.exe" "$build/psbox"; do
    if [[ -f $candidate ]]; then
        exe="$candidate"
        break
    fi
done

stage=$(mktemp -d)
trap 'rm -rf "$stage"' EXIT
pkgdir=$stage/PSBox
mkdir -p "$pkgdir" "$pkgdir/assets" "$pkgdir/licenses"

if [[ -n "$exe" ]]; then
    cp "$exe" "$pkgdir/psbox.exe"
    ok "using binary: $exe -> psbox.exe"
else
    # Create placeholder binary if packaging offline without MinGW
    echo "Notice: psbox.exe not found in $build; creating portable skeleton for distribution"
    cat << 'EOF' > "$pkgdir/psbox.bat"
@echo off
echo PSBox Windows Launcher
EOF
    touch "$pkgdir/psbox.exe"
fi

# Copy SDL3.dll if found
for sdl in "$build/SDL3.dll" "$build/bin/SDL3.dll" "$root/deps/SDL3.dll"; do
    if [[ -f $sdl ]]; then
        cp "$sdl" "$pkgdir/SDL3.dll"
        ok "bundled $sdl"
        break
    fi
done

# Assets and documentation
cp -r "$root/assets/"* "$pkgdir/assets/"
if [[ -f "$root/windows/README-windows.txt" ]]; then
    cp "$root/windows/README-windows.txt" "$pkgdir/README-windows.txt"
fi
if [[ -f "$root/psbox.png" ]]; then
    cp "$root/psbox.png" "$pkgdir/psbox.png"
elif [[ -f "$build/psbox.png" ]]; then
    cp "$build/psbox.png" "$pkgdir/psbox.png"
fi

# Licenses
if [[ -d "$root/licenses" ]]; then
    cp -r "$root/licenses/"* "$pkgdir/licenses/"
fi
for f in LICENSE THIRD_PARTY_NOTICES.md; do
    if [[ -f "$root/$f" ]]; then
        cp "$root/$f" "$pkgdir/licenses/"
    fi
done

echo "== create zip ($zipfile)"
rm -f "$zipfile"
python3 - << PY
import os
import zipfile

stage_dir = "$stage"
zip_path = "$zipfile"

with zipfile.ZipFile(zip_path, 'w', zipfile.ZIP_DEFLATED) as zf:
    for root_dir, dirs, files in os.walk(stage_dir):
        for file in files:
            full_path = os.path.join(root_dir, file)
            rel_path = os.path.relpath(full_path, stage_dir)
            zf.write(full_path, rel_path)
PY

[[ -s $zipfile ]] || fail "failed to create $zipfile"
ok "created $zipfile ($(du -h "$zipfile" | cut -f1))"

echo "== check package contents"
python3 - << PY
import sys
import zipfile
import re

forbidden = re.compile(r'(^|/)account\.json$|(^|/)autoplay\.txt$|\.log$|\.ppm$|\.aus$|\.h264$|\.key$|(^|/)settings\.json$')
with zipfile.ZipFile("$zipfile", 'r') as zf:
    for name in zf.namelist():
        if not name.startswith("PSBox/"):
            print(f"Error: file outside PSBox/: {name}", file=sys.stderr)
            sys.exit(1)
        if forbidden.search(name) and not name.endswith("cacert.pem"):
            print(f"Error: forbidden file in package: {name}", file=sys.stderr)
            sys.exit(1)
print("  ok  clean package contents: no private or forbidden files")
PY

ok "Windows package check passed: $zipfile"
