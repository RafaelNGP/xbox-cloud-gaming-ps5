#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 RafaelNGP
# One-shot build script for all platforms (Desktop, PS5, PS4) and boundary validation.
set -euo pipefail

root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
cd "$root"

echo "=== Building Desktop (SDL3 & No-SDL) ==="
ninja -C build-desktop
ninja -C build-desktop-nosdl

echo "=== Building PS5 (Vulkan & eboot.bin) ==="
ninja -C build-ps5 xcloud_app
bash tools/ps5/link.sh build-ps5

echo "=== Building PS4 (OpenOrbis & PKG) ==="
ninja -C build-ps4
bash tools/ps4/package.sh build-ps4

echo "=== Checking Platform Boundaries ==="
bash tools/check-platform-boundaries.sh

echo "=== All platform builds and boundary checks passed! ==="
