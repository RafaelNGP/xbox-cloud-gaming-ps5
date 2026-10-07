#!/usr/bin/env bash
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

# Ship the signed-in account from the host build, if any, to skip the first sign-in.
if [[ -f "$root/build-host/data/account.json" ]]; then
    cp "$root/build-host/data/account.json" "$app/account.json"
fi

echo "App packaged in: $app"
ls -lh "$app"
