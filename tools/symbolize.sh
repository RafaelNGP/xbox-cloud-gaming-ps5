#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 RafaelNGP
# Translates crash report offsets (pc=eboot+0x...) into function and file:line.
#
#   tools/symbolize.sh [vX.Y.Z] <offset-or-crash-line...>
#
# Examples:
#   tools/symbolize.sh v0.9.8 0x1a2b3c
#   tools/symbolize.sh v0.9.8 "crash: SIGSEGV pc=eboot+0x1a2b3c addr=0x0"
#   tools/symbolize.sh 0x1a2b3c
set -euo pipefail

root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
vk=${PS5_VULKAN:-$(cd "$root/../PS5_Vulkan" 2>/dev/null && pwd || true)}
sdk=${PS5_SDK:-${vk:+$vk/.deps/native/ps5-payload-sdk}}

addr2line=""
if [[ -n $sdk && -x "$sdk/bin/prospero-llvm-config" ]]; then
    bindir=$("$sdk/bin/prospero-llvm-config" --bindir 2>/dev/null || true)
    [[ -n $bindir && -x "$bindir/llvm-addr2line" ]] && addr2line="$bindir/llvm-addr2line"
fi
[[ -n $addr2line ]] || addr2line=$(command -v llvm-addr2line || command -v addr2line || true)
[[ -n $addr2line ]] || { echo "symbolize: llvm-addr2line not found" >&2; exit 1; }

if (( $# == 0 )); then
    echo "usage: tools/symbolize.sh [vX.Y.Z] <offset-or-crash-line...>" >&2
    exit 1
fi

ver=""
if [[ $1 =~ ^v?[0-9]+\.[0-9]+\.[0-9]+$ ]]; then
    ver=$1
    shift
fi
if [[ -z $ver ]]; then
    # Look for (vX.Y.Z) in the arguments, or fall back to CMakeLists.txt.
    for arg in "$@"; do
        if [[ $arg =~ \(v([0-9]+\.[0-9]+\.[0-9]+)\) ]]; then
            ver="v${BASH_REMATCH[1]}"
            break
        fi
    done
    [[ -n $ver ]] || ver="v$(sed -n 's/^project(xcloud_ps5 VERSION \([0-9.]*\).*/\1/p' "$root/CMakeLists.txt")"
fi
[[ $ver == v* ]] || ver="v$ver"

elf="$root/symbols/$ver/eboot.elf"
if [[ ! -f $elf && -f "$root/build-ps5/ps5-link/llvm-pie.elf" ]]; then
    elf="$root/build-ps5/ps5-link/llvm-pie.elf"
fi
[[ -f $elf ]] || { echo "symbolize: symbol file not found at $root/symbols/$ver/eboot.elf" >&2; exit 1; }

offsets=()
for arg in "$@"; do
    if [[ $arg == *eboot+0x* ]]; then
        while [[ $arg =~ eboot\+(0x[0-9a-fA-F]+) ]]; do
            offsets+=("${BASH_REMATCH[1]}")
            arg=${arg#*"eboot+${BASH_REMATCH[1]}"}
        done
    elif [[ $arg =~ ^(pc=)?(eboot\+)?(0x[0-9a-fA-F]+|[0-9a-fA-F]+)$ ]]; then
        off=${BASH_REMATCH[3]}
        [[ $off == 0x* ]] || off="0x$off"
        offsets+=("$off")
    fi
done

if (( ${#offsets[@]} == 0 )); then
    echo "symbolize: no eboot offset found in arguments" >&2
    exit 1
fi

exec "$addr2line" -f -C -e "$elf" "${offsets[@]}"
