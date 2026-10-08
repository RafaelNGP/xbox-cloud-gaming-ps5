#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 RafaelNGP
# Compiles src/display/shaders/*.comp to SPIR-V arrays (src/display/shaders/
# *.spv.h, committed: the build doesn't need glslang). Run after editing one.
set -euo pipefail
root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
dir=$root/src/display/shaders
for src in "$dir"/*.comp; do
    name=$(basename "$src" .comp)
    glslangValidator -V --target-env vulkan1.3 -I"$root/extern/fsr1" --vn "kShader_$name" \
        "$src" -o "$dir/$name.spv.h" > /dev/null
    echo "$name.spv.h"
done
