#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 RafaelNGP
# Keeps the PS5's system calls inside the PS5 backends, so the shared code
# builds for Linux and Windows too (docs/multiplatform.md). Fails, naming
# each line, when a file outside the list below has:
#   - a PS5 system function (sceSomething),
#   - a PS5 SDK header (#include <ps5platform/...>, <orbis/...>),
#   - a PS5 switch: XCLOUD_PS5, XC_PLATFORM_PS5, __PROSPERO__ or __ORBIS__
#     anywhere, or an #if on __FreeBSD__ or on "not XC_PLATFORM_DESKTOP".
# Comments don't count (a "//" inside a string doesn't start one). CI runs
# it on every push; locally:
#   bash tools/check-platform-boundaries.sh
set -euo pipefail

root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
cd "$root"

# The files allowed to talk to the PS5 directly (globs on the path from the
# repository's root; * also crosses folders). A test that fakes PS5 calls
# goes here by its exact name.
allowed=(
    'src/platform/ps5/*'   # the PS5's platform layer
    'src/*_ps5.cpp'        # PS5 backends: display, controller, kbm, hw_decoder, shell...
    'src/display/gpu.cpp'  # RADV on the PS5 (the GPU path only the PS5 builds)
    'ps5/*'                # link stubs and compat headers for the PS5 SDK
    # Legacy: these still pick their PS5 code with #if. Each comes off the
    # list once it is split into a PS5 backend and a portable part.
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

files=()
while IFS= read -r -d '' file; do
    file=${file#./}
    is_allowed "$file" || files+=("$file")
done < <(find src tests ps5 -type f \( -name '*.c' -o -name '*.cc' -o -name '*.cpp' -o -name '*.h' -o -name '*.hpp' \) -print0 | sort -z)

# Each file with its comments blanked (newlines kept, so the line numbers
# hold). String and character literals are stepped over whole, so what they
# hold can't open or close a comment; their text is still checked.
# shellcheck disable=SC2016  # Perl code, not shell
scan='
use strict;
use warnings;
my @rules = (
    qr/\bsce[A-Z][A-Za-z0-9_]*/,
    qr/^\s*#\s*include\s*[<"](?:ps5platform|orbis)\//,
    qr/\b(?:XCLOUD_PS5|XC_PLATFORM_PS5|__PROSPERO__|__ORBIS__)\b/,
    qr/^\s*#\s*(?:if|ifdef|ifndef|elif|elifdef|elifndef)\b.*\b__FreeBSD__\b/,
    qr/^\s*#\s*(?:ifndef|elifndef)\s+XC_PLATFORM_DESKTOP\b/,
    qr/^\s*#\s*(?:if|elif)\b.*!\s*(?:defined\s*\(?\s*)?XC_PLATFORM_DESKTOP\b/,
);
my $found = 0;
for my $file (@ARGV) {
    open(my $fh, "<", $file) or die "$file: $!\n";
    my $src = do { local $/; <$fh> };
    close $fh;
    my @lines = split /\n/, $src, -1;
    (my $code = $src) =~ s{
        ( (?<!\w)(?:u8|[uUL])?R"([^()\\\s]{0,16})\(.*?\)\2" | "(?:\\.|[^"\\\n])*" | \x27(?:\\.|[^\x27\\\n])*\x27 )
      | ( //[^\n]* | /\*.*?\*/ )
    }{ defined $3 ? ($3 =~ s/[^\n]/ /gr) : $1 }gsex;
    my $n = 0;
    for my $line (split /\n/, $code, -1) {
        ++$n;
        next unless grep { $line =~ $_ } @rules;
        print "$file:$n:$lines[$n - 1]\n";
        $found = 1;
    }
}
exit $found;
'

if (( ${#files[@]} )) && ! perl -e "$scan" -- "${files[@]}"; then
    echo "PS5 code outside the PS5 backends (above). Move it into a *_ps5.cpp backend behind a" >&2
    echo "portable header, or see docs/multiplatform.md; tools/check-platform-boundaries.sh lists" >&2
    echo "the files allowed to have it." >&2
    exit 1
fi
echo "platform boundaries: clean"
