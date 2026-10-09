#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 RafaelNGP
# Builds and checks a release, the same way every time; publishes nothing.
#
#   tools/release.sh <X.Y.Z> [out-dir] [--console]
#
# Stops at the first problem. Before building: a clean tree, the version in
# CMakeLists.txt and ps5/sce_sys/param.json (contentVersion 0X.YZZ.000, above
# the last release's), no tag yet, and no token or private file in the
# changes since the last release. Then: PS5 build -> link -> package WITHOUT
# the account -> zip + sha256 + signature, and checks inside the zip (no private files,
# README.txt and param.json at this version, no token in eboot.bin, the
# version compiled in, the licenses). With --console (PS5_HOST set), a cloud
# and an own-Xbox stream on the console. Last, it prints the commands that
# publish the release and the catalog record.
#
# The in-app updater installs only a zip signed by the project's key:
# PSBOX_SIGNING_KEY (default ~/.config/psbox-release/signing-key.pem, kept
# by the maintainer, never in the repository) signs it, and must be the key
# whose public half src/app/updater.cpp carries.
set -euo pipefail
root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
cd "$root"

version=${1:?usage: tools/release.sh <X.Y.Z> [out-dir] [--console]}
shift
out=""
console=0
for a in "$@"; do
    case $a in
        --console) console=1 ;;
        *) out=$a ;;
    esac
done
out=${out:-$root/build-ps5/release-v$version}
key=${PSBOX_SIGNING_KEY:-$HOME/.config/psbox-release/signing-key.pem}
tag=v$version
fail() { echo "release: $*" >&2; exit 1; }
ok() { echo "  ok  $*"; }

[[ $version =~ ^([0-9]+)\.([0-9]+)\.([0-9]+)$ ]] || fail "version must be X.Y.Z"
major=${BASH_REMATCH[1]} minor=${BASH_REMATCH[2]} patch=${BASH_REMATCH[3]}
content=$(printf '%02d.%d%02d.000' "$major" "$minor" "$patch")
tokens='eyJ[A-Za-z0-9_-]{20,}|refresh_token"? *[:=] *"[A-Za-z0-9]|XBL3\.0 x=[0-9]'
private='(^|/)account\.json$|\.log$|\.ppm$|\.aus$|\.h264$|(^|/)autoplay\.txt$|(^|/)settings\.json$|(^|/)library\.json$|(^|/)prices\.json$'

echo "== checks before building $tag"
[[ -z $(git status --porcelain) ]] || fail "uncommitted changes (commit or stash them first)"
ok "clean tree on $(git rev-parse --abbrev-ref HEAD) at $(git rev-parse --short HEAD)"
grep -q "project(xcloud_ps5 VERSION $version " CMakeLists.txt || fail "CMakeLists.txt is not at VERSION $version"
grep -q "\"contentVersion\": \"$content\"" ps5/sce_sys/param.json || fail "param.json contentVersion is not $content"
ok "version $version, contentVersion $content"
git rev-parse -q --verify "refs/tags/$tag" >/dev/null && fail "tag $tag exists already"
git ls-remote --exit-code --tags origin "$tag" >/dev/null 2>&1 && fail "tag $tag exists on origin already"
last=$(git describe --tags --abbrev=0 2>/dev/null || true)
if [[ -n $last ]]; then
    prev=$(git show "$last:ps5/sce_sys/param.json" | grep -o '"contentVersion": "[^"]*"' | cut -d'"' -f4)
    [[ $content > $prev ]] || fail "contentVersion $content is not above $last's $prev"
    ok "above $last ($prev)"
    if git diff "$last"..HEAD | grep -nE "^\+.*($tokens)"; then fail "a token in the changes since $last"; fi
    ok "no token in the changes since $last"
fi
if git ls-files | grep -nE "$private"; then fail "private files are tracked"; fi
ok "no private file tracked"
[[ -r $key ]] || fail "no signing key at $key (PSBOX_SIGNING_KEY)"
pub=$(openssl pkey -in "$key" -pubout 2>/dev/null) || fail "$key is not a private key"
while read -r line; do
    [[ $line == -----* ]] || grep -qF "\"$line\n\"" src/app/updater.cpp || fail "$key is not the key src/app/updater.cpp trusts"
done <<<"$pub"
ok "signing key matches the app's"

echo "== build"
buildlog=$(mktemp)
cmake --build build-ps5 -j"$(nproc)" >"$buildlog" 2>&1 || { cat "$buildlog"; fail "the PS5 build failed"; }
tools/ps5/link.sh build-ps5 >"$buildlog" 2>&1 || { cat "$buildlog"; fail "linking eboot.bin failed"; }
rm -f "$buildlog"
ok "PS5 build linked"
rm -rf "$out"
mkdir -p "$out"
env -u XC_INCLUDE_ACCOUNT XC_VERSION="$tag" tools/ps5/package.sh build-ps5 "$out" >/dev/null
(cd "$out" && zip -qr PPSA99810.zip PPSA99810 && sha256sum PPSA99810.zip > PPSA99810.zip.sha256)
sha=$(cut -d' ' -f1 "$out/PPSA99810.zip.sha256")
ok "$out/PPSA99810.zip ($(du -h "$out/PPSA99810.zip" | cut -f1)), sha256 $sha"
openssl dgst -sha256 -sign "$key" -out "$out/PPSA99810.zip.sig" "$out/PPSA99810.zip"
openssl dgst -sha256 -verify <(echo "$pub") -signature "$out/PPSA99810.zip.sig" "$out/PPSA99810.zip" >/dev/null ||
    fail "the signature doesn't verify"
ok "signed: $out/PPSA99810.zip.sig"

echo "== checks in the package"
zip=$out/PPSA99810.zip
if unzip -Z1 "$zip" | grep -nE "$private"; then fail "a private file in the zip"; fi
ok "no private file ($(unzip -Z1 "$zip" | grep -vc '/$') files)"
unzip -p "$zip" PPSA99810/README.txt | head -1 | grep -q "version $tag" || fail "README.txt is not at $tag"
unzip -p "$zip" PPSA99810/sce_sys/param.json | grep -q "\"contentVersion\": \"$content\"" || fail "the packaged param.json is not $content"
ok "README.txt $tag, param.json $content"
eboot=$out/PPSA99810/eboot.bin
if grep -aqE "$tokens" "$eboot"; then fail "eboot.bin holds something like a token"; fi
grep -aq "$version" "$eboot" || fail "eboot.bin doesn't carry version $version (stale build?)"
ok "eboot.bin: no token, version $version compiled in"
for f in assets/cacert.pem licenses/THIRD_PARTY_NOTICES.md licenses/FFmpeg-LGPL-2.1.txt licenses/Anime4K-MIT.txt LICENSE; do
    unzip -Z1 "$zip" | grep -qx "PPSA99810/$f" || fail "missing PPSA99810/$f"
done
ok "certificates and licenses in place"

if [[ $console == 1 ]]; then
    echo "== console (${PS5_HOST:?set PS5_HOST for --console})"
    for run in "FORTNITE 25" "XHOME 25"; do
        result=$(tools/ps5/autotest.sh $run 2>&1 || true)  # whole output first: grep -q would cut it short
        grep -q 'RESULT: finished' <<<"$result" || { echo "$result" | tail -5; fail "console run '$run' didn't finish"; }
        log=build-ps5/autotest/xcloud.log
        grep -q 'first frame presented' "$log" || fail "console run '$run': no picture"
        grep -q 'TLS handshake' "$log" && fail "console run '$run': TLS errors"
        ok "console: $run streamed ($(grep -o '[0-9]* frames shown' "$log" | tail -1))"
    done
fi

echo "== all checks passed; to publish (with release notes in notes.md):"
echo "  gh release create $tag \"$zip\" \"$out/PPSA99810.zip.sha256\" \"$out/PPSA99810.zip.sig\" --target $(git rev-parse HEAD) --title \"PSBox Cloud Gaming $tag\" --notes-file notes.md"
echo "== catalog record (apps/PPSA99810.json):"
echo "  \"version\": \"$version\","
echo "  \"artifact_url\": \"https://github.com/RafaelNGP/xbox-cloud-gaming-ps5/releases/download/$tag/PPSA99810.zip\","
echo "  \"sha256\": \"$sha\","
echo "  \"icon_url\": \"https://raw.githubusercontent.com/RafaelNGP/xbox-cloud-gaming-ps5/$tag/ps5/sce_sys/icon0.png\""
