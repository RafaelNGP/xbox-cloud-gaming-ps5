#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 PSBox Cloud Gaming contributors
# Linker for third-party configure scripts (FFmpeg): links their test
# programs as a PIE against the payload SDK's stub libraries, so a function
# check passes only when the console really exports the symbol. The result
# is never run. Flags meant for a GNU toolchain (-l, -Wl, -pthread) are dropped.
: "${PS5_SDK:?}"
set -- "$@"
out=a.out
objs=
while [ $# -gt 0 ]; do
    case $1 in
        -o) out=$2; shift ;;
        -o*) out=${1#-o} ;;
        -l*|-L*|-Wl,*|-pthread|-m*|-f*|-O*|-g*|-std=*|-isysroot|-isystem|-target|--target=*|-D*|-I*|-W*) 
            case $1 in -isysroot|-isystem|-target) shift ;; esac ;;
        *.o|*.a) objs="$objs $1" ;;
    esac
    shift
done
# shellcheck disable=SC2086
exec "$PS5_SDK/bin/prospero-lld" -pie -e main -o "$out" $objs "$PS5_SDK"/target/lib/*.so
