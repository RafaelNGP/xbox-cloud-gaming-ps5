#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 RafaelNGP
# Signs the Linux tarball that .github/workflows/release-linux.yml attached
# to a published release, on the release machine: the signing key never goes
# to GitHub. Downloads the tarball and its checksum, checks the checksum,
# signs it as tools/release.sh signs the PS5 zip, checks the signature with
# the key the app trusts, and uploads the .sig.
#
#   tools/desktop/sign-linux-release.sh vX.Y.Z
#
# PSBOX_SIGNING_KEY: the private key (default ~/.config/psbox-release/signing-key.pem).
set -euo pipefail

tag=${1:?usage: tools/desktop/sign-linux-release.sh vX.Y.Z}
[[ $tag == v* ]] || tag="v$tag"
root=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
key=${PSBOX_SIGNING_KEY:-$HOME/.config/psbox-release/signing-key.pem}
fail() { echo "sign-linux-release: $*" >&2; exit 1; }

[[ -r $key ]] || fail "no signing key at $key"
pub=$(openssl pkey -in "$key" -pubout 2>/dev/null) || fail "$key is not a private key"
while read -r line; do
    [[ $line == -----* ]] || grep -qF "\"$line\n\"" "$root/src/app/updater.cpp" ||
        fail "$key is not the key src/app/updater.cpp trusts"
done <<<"$pub"

name=PSBox-$tag-linux-x86_64.tar.gz
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
gh release download "$tag" -p "$name" -p "$name.sha256" -D "$work" ||
    fail "$name is not on release $tag yet (the 'Release (Linux)' workflow attaches it)"
(cd "$work" && sha256sum -c "$name.sha256" >/dev/null) || fail "$name doesn't match its checksum"

openssl dgst -sha256 -sign "$key" -out "$work/$name.sig" "$work/$name"
openssl dgst -sha256 -verify <(echo "$pub") -signature "$work/$name.sig" "$work/$name" >/dev/null ||
    fail "the signature doesn't verify"
gh release upload "$tag" "$work/$name.sig" --clobber
echo "signed and uploaded $name.sig to $tag"
gh release view "$tag" --json assets -q '.assets[].name' | grep -F "$name"
