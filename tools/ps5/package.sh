#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 PSBox Cloud Gaming contributors
# Package the PS5 app folder (ShadowMountPlus directory title)
#
#   tools/ps5/package.sh <build-dir> [out-dir]     -> <out-dir>/<TITLE_ID>/
set -euo pipefail

build=$(cd "${1:?usage: package.sh <build-dir> [out-dir]}" && pwd)
root=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
vk=${PS5_VULKAN:-$(cd "$root/../../WoW-PS5/deps/PS5_Vulkan" && pwd)}
param=$root/ps5/sce_sys/param.json
title=$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["titleId"])' "$param")
out=${2:-$build/pkg}
app=$out/$title

[[ -f $build/eboot.bin ]] || { echo "missing $build/eboot.bin (run tools/ps5/link.sh)" >&2; exit 2; }

rm -rf -- "$app"
mkdir -p "$app/sce_sys" "$app/sce_module" "$app/assets"

cp "$build/eboot.bin" "$app/eboot.bin"
cp "$param" "$app/sce_sys/param.json"
cp "$root/ps5/sce_sys/icon0.png" "$app/sce_sys/icon0.png"
cp "$vk/runtime/libc.prx" "$app/sce_module/libc.prx"
cp -r "$root/assets/." "$app/assets/"

# License texts for everything in the package (see THIRD_PARTY_NOTICES.md).
mkdir -p "$app/licenses"
cp "$root/LICENSE" "$root/THIRD_PARTY_NOTICES.md" "$app/licenses/"
cp "$root/assets/fonts/OFL.txt" "$app/licenses/Inter-OFL.txt"
cp "$root/deps/mbedtls/LICENSE" "$app/licenses/mbedtls-LICENSE.txt"
cp "$root/deps/libdatachannel/LICENSE" "$app/licenses/libdatachannel-MPL-2.0.txt"
cp "$root/deps/libdatachannel/deps/usrsctp/LICENSE.md" "$app/licenses/usrsctp-LICENSE.md"
cp "$root/deps/libdatachannel/deps/libsrtp/LICENSE" "$app/licenses/libsrtp-LICENSE.txt"
cp "$root/deps/libdatachannel/deps/plog/LICENSE" "$app/licenses/plog-LICENSE.txt"
cp "$root/deps/ffmpeg/COPYING.LGPLv2.1" "$app/licenses/FFmpeg-LGPL-2.1.txt"
cp "$root/extern/stb/LICENSE" "$app/licenses/stb-LICENSE.txt"

# Development only: XC_INCLUDE_ACCOUNT=1 ships the host build's signed-in
# account (a refresh token!) to skip the console sign-in. Never for releases.
if [[ ${XC_INCLUDE_ACCOUNT:-0} == 1 && -f "$root/build-host/data/account.json" ]]; then
    cp "$root/build-host/data/account.json" "$app/account.json"
fi

echo "App packaged in: $app"
ls -lh "$app"
