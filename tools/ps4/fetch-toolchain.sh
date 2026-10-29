#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 RafaelNGP
# Download the OpenOrbis PS4 Toolchain release into $1 (default: ./ps4-toolchain)
set -euo pipefail

DEST="${1:-$(pwd)/ps4-toolchain}"
LLVM_VER="${2:-18}"
URL="https://github.com/OpenOrbis/OpenOrbis-PS4-Toolchain/releases/latest/download/toolchain-llvm-${LLVM_VER}.tar.gz"

if [ -f "$DEST/link.x" ] && [ -f "$DEST/lib/crt1.o" ]; then
    echo "OpenOrbis toolchain already present at $DEST"
    exit 0
fi

mkdir -p "$DEST"
echo "Downloading $URL"
curl -sSL --fail -o "$DEST/toolchain.tar.gz" "$URL"
echo "Extracting to $DEST"
tar -xzf "$DEST/toolchain.tar.gz" -C "$DEST" --strip-components=2
rm -f "$DEST/toolchain.tar.gz"

if [ ! -f "$DEST/link.x" ] || [ ! -f "$DEST/lib/crt1.o" ]; then
    echo "error: $DEST does not contain link.x / lib/crt1.o after extraction" >&2
    exit 1
fi
chmod +x "$DEST"/bin/linux/* 2>/dev/null || true

# Patch upstream OpenOrbis ABI bug: pthread_*attr_t were defined as 32-bit int instead of 64-bit pointer,
# corrupting adjacent memory and causing SIGSEGV in pthread_mutex_init/pthread_rwlock_init.
if [ -f "$DEST/include/bits/alltypes.h" ]; then
    sed -i 's/typedef struct { unsigned __attr; } pthread_mutexattr_t;/typedef struct { void *__attr; } pthread_mutexattr_t;/' "$DEST/include/bits/alltypes.h"
    sed -i 's/typedef struct { unsigned __attr; } pthread_condattr_t;/typedef struct { void *__attr; } pthread_condattr_t;/' "$DEST/include/bits/alltypes.h"
    sed -i 's/typedef struct { unsigned __attr; } pthread_barrierattr_t;/typedef struct { void *__attr; } pthread_barrierattr_t;/' "$DEST/include/bits/alltypes.h"
    sed -i 's/typedef struct { unsigned __attr\[2\]; } pthread_rwlockattr_t;/typedef struct { void *__attr; } pthread_rwlockattr_t;/' "$DEST/include/bits/alltypes.h"
fi

echo "OpenOrbis toolchain installed at $DEST."
