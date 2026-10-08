#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 RafaelNGP
# Regenerates the PS5 home-screen art from src/ui/brand.cpp:
#   sce_sys/icon0.png (512x512), pic0.dds (selected-app background) and
#   pic1.dds (launch transition), 3840x2160 BC7 DDS via PS5_Vulkan's
#   tools/prepare-assets.sh (bc7enc: run its tools/setup-asset-dependencies.sh once).
#
#   tools/ps5/home-art.sh [build-host-dir]
set -euo pipefail

root=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
vk=${PS5_VULKAN:-$(cd "$root/../PS5_Vulkan" && pwd)}
cli=${1:-$root/build-host}/xcloud-cli
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

cd "$root"
"$cli" render-icon ps5/sce_sys/icon0.png 512
"$cli" render-pic0 "$tmp/pic0.png" 3840
"$cli" render-pic1 "$tmp/pic1.png" 3840
mkdir -p "$tmp/out" && cp ps5/sce_sys/icon0.png "$tmp/out/"  # validated alongside
bash "$vk/tools/prepare-assets.sh" --selection-background "$tmp/pic0.png" \
    --launch-background "$tmp/pic1.png" --output-directory "$tmp/out"
cp "$tmp/out/pic0.dds" "$tmp/out/pic1.dds" ps5/sce_sys/
echo "home art updated in ps5/sce_sys"
