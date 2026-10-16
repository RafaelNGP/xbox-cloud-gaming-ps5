#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 RafaelNGP
# Keeps the PS5's system calls inside the PS5 backends, so the shared code
# builds for Linux and Windows too (docs/multiplatform.md). Fails, naming
# each line, when a file outside the list below has:
#   - a PS5 system function (sceSomething),
#   - a PS5 SDK header (#include <ps5platform/...>, <orbis/...>),
#   - an #if on the PS5 (XCLOUD_PS5, XC_PLATFORM_PS5, __PROSPERO__).
# Mentions in // comments don't count. CI runs it on every push; locally:
#   bash tools/check-platform-boundaries.sh
set -euo pipefail

root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
cd "$root"

# The files allowed to talk to the PS5 directly (globs on the path from the
# repository's root; * also crosses folders).
allowed=(
    'src/platform/ps5/*'   # the PS5's platform layer
    'src/*_ps5.cpp'        # PS5 backends: display, controller, kbm, hw_decoder, shell...
    'src/display/gpu.cpp'  # RADV on the PS5 (the GPU path only the PS5 builds)
    'ps5/*'                # link stubs and compat headers for the PS5 SDK
    'tests/*_ps5*'         # tests that fake PS5 calls (none yet)
    # Legacy: these still pick their PS5 code with #if. Each comes off the
    # list once it is split into a PS5 backend and a portable part.
    'src/media/audio_out.cpp'  # TODO(M1): portable AudioQueue + audio_out_ps5.cpp / an SDL backend
    'src/media/audio_in.cpp'   # TODO(M1): audio_in_ps5.cpp / an SDL backend
    'src/app/crash_guard.cpp'  # TODO(M2): crash_guard_ps5.cpp / POSIX / Windows (SEH)
)

is_allowed() {
    local f=$1 pattern
    for pattern in "${allowed[@]}"; do
        # shellcheck disable=SC2053  # the right side is a glob on purpose
        [[ $f == $pattern ]] && return 0
    done
    return 1
}

# Perl regular expressions; (?:(?!//).)*? keeps a match out of // comments.
code='^(?:(?!//).)*?'
patterns=(
    "${code}\\bsce[A-Z][A-Za-z0-9_]*"
    '^\s*#\s*include\s*[<"](ps5platform|orbis)/'
    '^\s*#\s*(if|ifdef|ifndef|elif)\b.*\b(XCLOUD_PS5|XC_PLATFORM_PS5|__PROSPERO__)\b'
)

found=0
while IFS= read -r -d '' file; do
    file=${file#./}
    is_allowed "$file" && continue
    for pattern in "${patterns[@]}"; do
        if hits=$(grep -nP -- "$pattern" "$file"); then
            while IFS= read -r hit; do
                echo "$file:$hit"
            done <<< "$hits"
            found=1
        fi
    done
done < <(find src tests ps5 -type f \( -name '*.c' -o -name '*.cc' -o -name '*.cpp' -o -name '*.h' -o -name '*.hpp' \) -print0 | sort -z)

if (( found )); then
    echo "PS5 code outside the PS5 backends (above). Move it into a *_ps5.cpp backend behind a" >&2
    echo "portable header, or see docs/multiplatform.md; tools/check-platform-boundaries.sh lists" >&2
    echo "the files allowed to have it." >&2
    exit 1
fi
echo "platform boundaries: clean"
