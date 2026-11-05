#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 RafaelNGP
#
# Release Verifier - Enforces the Tri-Platform Release Standard
# Every published release (vX.Y.Z) MUST contain assets for:
#   1. PlayStation 5 (PPSA99810.zip + .sha256 + .sig)
#   2. PlayStation 4 (PSBoxCloudGaming[PS4]-vX.Y.Z.pkg + .sha256)
#   3. Linux AppImage (PSBox-vX.Y.Z-linux-x86_64.AppImage + .sha256 + .sig)
#
# Usage:
#   tools/release-verify.sh <vX.Y.Z|X.Y.Z> [--verify-sigs]
#
set -euo pipefail

root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
raw_tag="${1:?usage: tools/release-verify.sh <vX.Y.Z|X.Y.Z> [--verify-sigs]}"
verify_sigs=0
if [[ "${2:-}" == "--verify-sigs" || "${1:-}" == "--verify-sigs" ]]; then
    verify_sigs=1
fi

tag="$raw_tag"
[[ "$tag" == v* ]] || tag="v$tag"
version="${tag#v}"

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
BOLD='\033[1m'
NC='\033[0m'

pass() { echo -e "  [${GREEN}✓${NC}] $*"; }
warn() { echo -e "  [${YELLOW}!${NC}] $*"; }
fail() { echo -e "  [${RED}✗${NC}] $*"; }

echo -e "${BOLD}${BLUE}=== Verifying Release $tag (Tri-Platform Standard) ===${NC}"

# Fetch release metadata from GitHub
json=$(gh release view "$tag" --json tagName,isDraft,isPrerelease,body,assets 2>&1) || {
    echo -e "${RED}Error: Release $tag not found on GitHub or gh CLI failed.${NC}" >&2
    echo "$json" >&2
    exit 1
}

# 1. Draft / Pre-release check
is_draft=$(echo "$json" | jq -r '.isDraft')
is_prerelease=$(echo "$json" | jq -r '.isPrerelease')

if [[ "$is_draft" == "true" ]]; then
    fail "Release $tag is marked as DRAFT"
    exit 1
fi
pass "Release $tag is published (not draft)"

# 2. Bilingual Release Notes Check
body=$(echo "$json" | jq -r '.body // ""')
errors=0

if grep -q "<!-- LANG:EN -->" <<<"$body" && grep -q "<!-- LANG:PT -->" <<<"$body"; then
    pass "Release notes: Bilingual markers (EN/PT) present"
else
    warn "Release notes: Missing <!-- LANG:EN --> or <!-- LANG:PT --> markers"
fi

if grep -iq "PS5" <<<"$body" && grep -iq "PS4" <<<"$body" && grep -iq "Linux" <<<"$body"; then
    pass "Release notes: Installation instructions present for PS5, PS4, and Linux"
else
    warn "Release notes: Missing explicit installation instructions for all 3 platforms"
fi

# Parse assets
asset_names=$(echo "$json" | jq -r '.assets[].name')

find_asset() {
    local pattern="$1"
    echo "$asset_names" | grep -E "$pattern" | head -1 || true
}

get_asset_size() {
    local name="$1"
    echo "$json" | jq -r --arg n "$name" '.assets[] | select(.name == $n) | .size // 0'
}

get_asset_state() {
    local name="$1"
    echo "$json" | jq -r --arg n "$name" '.assets[] | select(.name == $n) | .state // ""'
}

echo
echo -e "${BOLD}Target 1: PlayStation 5 (PS5)${NC}"
ps5_zip=$(find_asset '^PPSA99810\.zip$')
ps5_sha=$(find_asset '^PPSA99810\.zip\.sha256$')
ps5_sig=$(find_asset '^PPSA99810\.zip\.sig$')

if [[ -n "$ps5_zip" ]]; then
    sz=$(get_asset_size "$ps5_zip")
    st=$(get_asset_state "$ps5_zip")
    mb=$(awk -v s="$sz" 'BEGIN { printf "%.2f", s / 1048576 }')
    if (( sz > 10000000 )) && [[ "$st" == "uploaded" ]]; then
        pass "PPSA99810.zip ($mb MB, state: $st)"
    else
        fail "PPSA99810.zip is too small or not fully uploaded ($sz bytes, state: $st)"
        errors=$((errors + 1))
    fi
else
    fail "PPSA99810.zip is MISSING"
    errors=$((errors + 1))
fi

if [[ -n "$ps5_sha" ]]; then
    pass "PPSA99810.zip.sha256 present"
else
    fail "PPSA99810.zip.sha256 is MISSING"
    errors=$((errors + 1))
fi

if [[ -n "$ps5_sig" ]]; then
    sz=$(get_asset_size "$ps5_sig")
    if (( sz > 0 )); then
        pass "PPSA99810.zip.sig present ($sz bytes)"
    else
        fail "PPSA99810.zip.sig is empty"
        errors=$((errors + 1))
    fi
else
    fail "PPSA99810.zip.sig is MISSING (Console updater requires .sig!)"
    errors=$((errors + 1))
fi

echo
echo -e "${BOLD}Target 2: PlayStation 4 (PS4)${NC}"
# GitHub sanitizes [PS4] to .PS4., so we match both literal [PS4] and .PS4.
ps4_pkg=$(find_asset '^(PSBoxCloudGaming(\[PS4\]|\.PS4\.)-v'"$version"'\.pkg|PSBoxCloudGaming-v'"$version"'-ps4\.pkg)$')
ps4_sha=$(find_asset '^(PSBoxCloudGaming(\[PS4\]|\.PS4\.)-v'"$version"'\.pkg\.sha256|PSBoxCloudGaming-v'"$version"'-ps4\.pkg\.sha256)$')

if [[ -n "$ps4_pkg" ]]; then
    sz=$(get_asset_size "$ps4_pkg")
    st=$(get_asset_state "$ps4_pkg")
    mb=$(awk -v s="$sz" 'BEGIN { printf "%.2f", s / 1048576 }')
    if (( sz > 5000000 )) && [[ "$st" == "uploaded" ]]; then
        pass "$ps4_pkg ($mb MB, state: $st)"
    else
        fail "$ps4_pkg is too small or not fully uploaded ($sz bytes, state: $st)"
        errors=$((errors + 1))
    fi
else
    fail "PS4 PKG package is MISSING (expected PSBoxCloudGaming[PS4]-$tag.pkg)"
    errors=$((errors + 1))
fi

if [[ -n "$ps4_sha" ]]; then
    pass "$ps4_sha present"
else
    fail "PS4 PKG sha256 checksum is MISSING"
    errors=$((errors + 1))
fi

echo
echo -e "${BOLD}Target 3: Linux Desktop (AppImage)${NC}"
linux_app=$(find_asset '^PSBox-v'"$version"'-linux-x86_64\.AppImage$')
linux_sha=$(find_asset '^PSBox-v'"$version"'-linux-x86_64\.AppImage\.sha256$')
linux_sig=$(find_asset '^PSBox-v'"$version"'-linux-x86_64\.AppImage\.sig$')

if [[ -n "$linux_app" ]]; then
    sz=$(get_asset_size "$linux_app")
    st=$(get_asset_state "$linux_app")
    mb=$(awk -v s="$sz" 'BEGIN { printf "%.2f", s / 1048576 }')
    if (( sz > 4000000 )) && [[ "$st" == "uploaded" ]]; then
        pass "$linux_app ($mb MB, state: $st)"
    else
        fail "$linux_app is too small or not fully uploaded ($sz bytes, state: $st)"
        errors=$((errors + 1))
    fi
else
    fail "Linux AppImage is MISSING (expected PSBox-$tag-linux-x86_64.AppImage)"
    echo -e "      ${YELLOW}Hint: Run the 'Release (Linux)' workflow via:${NC}"
    echo -e "      gh workflow run release-linux.yml -f tag=$tag"
    errors=$((errors + 1))
fi

if [[ -n "$linux_sha" ]]; then
    pass "$linux_sha present"
else
    fail "Linux AppImage sha256 checksum is MISSING"
    errors=$((errors + 1))
fi

if [[ -n "$linux_sig" ]]; then
    sz=$(get_asset_size "$linux_sig")
    if (( sz > 0 )); then
        pass "$linux_sig present ($sz bytes)"
    else
        fail "$linux_sig is empty"
        errors=$((errors + 1))
    fi
else
    fail "Linux AppImage signature (.sig) is MISSING"
    echo -e "      ${YELLOW}Hint: Sign the downloaded AppImage using:${NC}"
    echo -e "      tools/desktop/sign-linux-release.sh $tag"
    errors=$((errors + 1))
fi

# Optional cryptographic signature verification
if [[ $verify_sigs -eq 1 && $errors -eq 0 ]]; then
    echo
    echo -e "${BOLD}Cryptographic Signature Verification${NC}"
    work=$(mktemp -d)
    trap 'rm -rf "$work"' EXIT
    
    # Extract public key from updater.cpp
    pubkey="$work/pubkey.pem"
    awk '/BEGIN PUBLIC KEY/,/END PUBLIC KEY/' "$root/src/app/updater.cpp" | sed 's/^[[:space:]]*"//; s/\\n".*//' > "$pubkey"
    
    if [[ -s "$pubkey" ]]; then
        # Verify PS5 signature
        gh release download "$tag" -p "PPSA99810.zip" -p "PPSA99810.zip.sig" -D "$work" >/dev/null 2>&1
        if openssl dgst -sha256 -verify "$pubkey" -signature "$work/PPSA99810.zip.sig" "$work/PPSA99810.zip" >/dev/null 2>&1; then
            pass "PS5 package cryptographic signature verified"
        else
            fail "PS5 package signature DOES NOT verify against updater.cpp public key!"
            errors=$((errors + 1))
        fi
        
        # Verify Linux signature
        gh release download "$tag" -p "$linux_app" -p "$linux_sig" -D "$work" >/dev/null 2>&1
        if openssl dgst -sha256 -verify "$pubkey" -signature "$work/$linux_sig" "$work/$linux_app" >/dev/null 2>&1; then
            pass "Linux AppImage cryptographic signature verified"
        else
            fail "Linux AppImage signature DOES NOT verify against updater.cpp public key!"
            errors=$((errors + 1))
        fi
    else
        warn "Could not extract public key from src/app/updater.cpp"
    fi
fi

echo
if [[ $errors -eq 0 ]]; then
    echo -e "${BOLD}${GREEN}======================================================${NC}"
    echo -e "${BOLD}${GREEN}  RELEASE $tag VERIFICATION PASSED (TRI-PLATFORM OK)  ${NC}"
    echo -e "${BOLD}${GREEN}  ✓ PS5   ✓ PS4   ✓ Linux AppImage                    ${NC}"
    echo -e "${BOLD}${GREEN}======================================================${NC}"
    exit 0
else
    echo -e "${BOLD}${RED}======================================================${NC}"
    echo -e "${BOLD}${RED}  RELEASE $tag VERIFICATION FAILED ($errors ERRORS FOUND)   ${NC}"
    echo -e "${BOLD}${RED}  One or more mandatory platform assets are missing!  ${NC}"
    echo -e "${BOLD}${RED}======================================================${NC}"
    exit 1
fi
