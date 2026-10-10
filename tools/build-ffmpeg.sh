#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 RafaelNGP
# Builds the slice of FFmpeg the client needs (H.264 + Opus decoders,
# libavcodec/libavutil only) as static libraries.
#
#   tools/build-ffmpeg.sh host <build-dir>   -> <build-dir>/ffmpeg/{include,lib}
#   tools/build-ffmpeg.sh ps5  <build-dir>
set -euo pipefail

target=${1:?usage: build-ffmpeg.sh host|ps5 <build-dir>}
build=$(mkdir -p "${2:?build dir}" && cd "$2" && pwd)
root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
prefix=$build/ffmpeg
# FFmpeg's build cannot handle whitespace in paths ("PS5 Ports"): build in a
# cache directory reached through space-free names, then copy the result.
cache=${XDG_CACHE_HOME:-$HOME/.cache}/xcloud-ps5/ffmpeg-$target
mkdir -p "$cache"
ln -sfn "$root/deps/ffmpeg" "$cache/src"
src=$cache/src
work=$cache/build
stage=$cache/install

common=(
    --prefix="$stage" --enable-static --disable-shared --enable-pic
    --disable-programs --disable-doc --disable-network --disable-autodetect
    --disable-everything --disable-avdevice --disable-avformat --disable-avfilter
    --disable-swscale --disable-postproc
    --enable-decoder=h264,opus --enable-encoder=opus --enable-parser=h264,opus
    # (the Opus decoder needs libswresample)
    --disable-debug
)

# NASM for the x86 SIMD decoders (2-3x faster H.264); built once if missing.
nasm=$(command -v nasm || true)
if [[ -z $nasm ]]; then
    nasm=${XDG_CACHE_HOME:-$HOME/.cache}/xcloud-ps5/nasm/bin/nasm
    if [[ ! -x $nasm ]]; then
        tools=$(dirname "$(dirname "$nasm")")/..
        (cd "$tools" && curl -sSLf -o nasm.tar.xz https://www.nasm.us/pub/nasm/releasebuilds/2.16.03/nasm-2.16.03.tar.xz &&
         tar xf nasm.tar.xz && cd nasm-2.16.03 && ./configure --prefix="$(dirname "$(dirname "$nasm")")" >/dev/null &&
         make -j"$(nproc)" >/dev/null && make install >/dev/null 2>&1 || true)
    fi
fi
common+=(--x86asmexe="$nasm")

mkdir -p "$work"
cd "$work"
if [[ $target == ps5 ]]; then
    vk=${PS5_VULKAN:-$(cd "$root/../PS5_Vulkan" && pwd)}
    # FFmpeg's configure splits the flags on spaces: the SDK goes through a
    # link in the cache, whose path has none.
    ln -sfn "$vk/.deps/native/ps5-payload-sdk" "$cache/ps5-payload-sdk"
    export PS5_SDK=$cache/ps5-payload-sdk
    cp "$root/ps5/compat/ps5_lfs.h" "$cache/ps5_lfs.h"
    ln -sfn "$root/tools/ps5/configure-ld.sh" "$cache/configure-ld.sh"
    flags="-target x86_64-sie-ps5 -fPIC -march=znver2 -fvisibility-nodllstorageclass=default -fno-stack-protector -fno-plt -femulated-tls -fdenormal-fp-math=ieee -isysroot $PS5_SDK -isystem $PS5_SDK/target/include -D_GNU_SOURCE -include $cache/ps5_lfs.h -ffile-prefix-map=$cache=ffmpeg -ffile-prefix-map=$HOME=~"
    "$src/configure" "${common[@]}" \
        --enable-cross-compile --target-os=freebsd --arch=x86_64 \
        --cc=clang --ar=llvm-ar --ranlib=llvm-ranlib --nm=llvm-nm \
        --ld="$cache/configure-ld.sh" \
        --extra-cflags="$flags" --extra-ldflags="" \
        --disable-pthreads --enable-pthreads
else
    "$src/configure" "${common[@]}" --cc=clang
fi
make -j"$(nproc)" >/dev/null
make install >/dev/null
rm -rf "$prefix"
cp -r "$stage" "$prefix"
echo "FFmpeg installed in $prefix"
