#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 RafaelNGP
#
# Release Verifier - Enforces the Tri-Platform Standard (PS5, PS4, Linux AppImage)
# Verifies the exact 10-asset bundle and release notes tables:
#
#   1. PSBoxCloudGaming[PS5]-vX.Y.Z.zip
#   2. PSBoxCloudGaming[PS5]-vX.Y.Z.zip.sig
#   3. PSBoxCloudGaming[PS5]-vX.Y.Z.zip.sha256
#   4. PPSA99810.zip
#   5. PPSA99810.zip.sig
#   6. PPSA99810.zip.sha256
#   7. PSBoxCloudGaming[PS4]-vX.Y.Z.pkg
#   8. PSBoxCloudGaming[PS4]-vX.Y.Z.pkg.sha256
#   9. PSBoxCloudGaming[Linux]-vX.Y.Z.AppImage
#  10. PSBoxCloudGaming[Linux]-vX.Y.Z.AppImage.sha256
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

if grep -q "Attached Files (English)" <<<"$body" && grep -q "Arquivos Anexados (Português)" <<<"$body"; then
    pass "Release notes: Standard Attached Files tables (EN/PT) present"
else
    fail "Release notes: Missing 'Attached Files (English)' or 'Arquivos Anexados (Português)' table"
    errors=$((errors + 1))
fi

if grep -iq "PlayStation 5" <<<"$body" && grep -iq "PlayStation 4" <<<"$body" && grep -iq "Linux" <<<"$body"; then
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

check_file() {
    local label="$1"
    local pattern="$2"
    local min_size="$3"
    local asset
    asset=$(find_asset "$pattern")
    if [[ -n "$asset" ]]; then
        local sz st mb
        sz=$(get_asset_size "$asset")
        st=$(get_asset_state "$asset")
        mb=$(awk -v s="$sz" 'BEGIN { printf "%.2f", s / 1048576 }')
        if (( sz >= min_size )) && [[ "$st" == "uploaded" ]]; then
            pass "$asset ($mb MB, state: $st)"
            return 0
        else
            fail "$asset is too small or incomplete ($sz bytes, state: $st)"
            errors=$((errors + 1))
            return 1
        fi
    else
        fail "$label is MISSING (matching pattern: $pattern)"
        errors=$((errors + 1))
        return 1
    fi
}

echo
echo -e "${BOLD}Target 1: PlayStation 5 (PS5) - Named Package & In-App Updater Canonical${NC}"
check_file "PSBoxCloudGaming[PS5]-$tag.zip" '^(PSBoxCloudGaming(\[PS5\]|\.PS5\.)-v'"$version"'\.zip)$' 10000000
check_file "PSBoxCloudGaming[PS5]-$tag.zip.sig" '^(PSBoxCloudGaming(\[PS5\]|\.PS5\.)-v'"$version"'\.zip\.sig)$' 60
check_file "PSBoxCloudGaming[PS5]-$tag.zip.sha256" '^(PSBoxCloudGaming(\[PS5\]|\.PS5\.)-v'"$version"'\.zip\.sha256)$' 60
check_file "PPSA99810.zip (canonical)" '^PPSA99810\.zip$' 10000000
check_file "PPSA99810.zip.sig (canonical)" '^PPSA99810\.zip\.sig$' 60
check_file "PPSA99810.zip.sha256 (canonical)" '^PPSA99810\.zip\.sha256$' 60

echo
echo -e "${BOLD}Target 2: PlayStation 4 (PS4) - FPKG & Checksum${NC}"
check_file "PSBoxCloudGaming[PS4]-$tag.pkg" '^(PSBoxCloudGaming(\[PS4\]|\.PS4\.)-v'"$version"'\.pkg|PSBoxCloudGaming-v'"$version"'-ps4\.pkg)$' 5000000
check_file "PSBoxCloudGaming[PS4]-$tag.pkg.sha256" '^(PSBoxCloudGaming(\[PS4\]|\.PS4\.)-v'"$version"'\.pkg\.sha256|PSBoxCloudGaming-v'"$version"'-ps4\.pkg\.sha256)$' 60

echo
echo -e "${BOLD}Target 3: Linux Desktop & Steam Deck - AppImage & Checksum${NC}"
check_file "PSBoxCloudGaming[Linux]-$tag.AppImage" '^(PSBoxCloudGaming(\[Linux\]|\.Linux\.)-v'"$version"'\.AppImage|PSBox-v'"$version"'-linux-x86_64\.AppImage)$' 4000000
check_file "PSBoxCloudGaming[Linux]-$tag.AppImage.sha256" '^(PSBoxCloudGaming(\[Linux\]|\.Linux\.)-v'"$version"'\.AppImage\.sha256|PSBox-v'"$version"'-linux-x86_64\.AppImage\.sha256)$' 60

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
            pass "PS5 package (PPSA99810.zip) cryptographic signature verified"
        else
            fail "PS5 package signature DOES NOT verify against updater.cpp public key!"
            errors=$((errors + 1))
        fi
        
        # Verify Linux signature if present
        linux_app=$(find_asset '^PSBox-v'"$version"'-linux-x86_64\.AppImage$')
        linux_sig=$(find_asset '^PSBox-v'"$version"'-linux-x86_64\.AppImage\.sig$')
        if [[ -n "$linux_app" && -n "$linux_sig" ]]; then
            gh release download "$tag" -p "$linux_app" -p "$linux_sig" -D "$work" >/dev/null 2>&1
            if openssl dgst -sha256 -verify "$pubkey" -signature "$work/$linux_sig" "$work/$linux_app" >/dev/null 2>&1; then
                pass "Linux AppImage cryptographic signature verified"
            else
                fail "Linux AppImage signature DOES NOT verify against updater.cpp public key!"
                errors=$((errors + 1))
            fi
        fi
    else
        warn "Could not extract public key from src/app/updater.cpp"
    fi
fi

echo
if [[ $errors -eq 0 ]]; then
    echo -e "${BOLD}${GREEN}======================================================${NC}"
    echo -e "${BOLD}${GREEN}  RELEASE $tag VERIFICATION PASSED (TRI-PLATFORM OK)  ${NC}"
    echo -e "${BOLD}${GREEN}  ✓ PS5 (Named + Canonical)                           ${NC}"
    echo -e "${BOLD}${GREEN}  ✓ PS4 (FPKG + SHA256)                               ${NC}"
    echo -e "${BOLD}${GREEN}  ✓ Linux (AppImage + SHA256)                         ${NC}"
    echo -e "${BOLD}${GREEN}  ✓ Bilingual Attached Files Manifest Tables Verified ${NC}"
    echo -e "${BOLD}${GREEN}======================================================${NC}"
    exit 0
else
    echo -e "${BOLD}${RED}======================================================${NC}"
    echo -e "${BOLD}${RED}  RELEASE $tag VERIFICATION FAILED ($errors ERRORS FOUND)   ${NC}"
    echo -e "${BOLD}${RED}  One or more mandatory assets/tables are missing!    ${NC}"
    echo -e "${BOLD}${RED}======================================================${NC}"
    exit 1
fi
