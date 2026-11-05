#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 RafaelNGP
#
# Master Release Publisher - Tri-Platform Guaranteed (PS5 + PS4 + Linux)
#
# Workflow:
#   1. Runs tools/release.sh to build PS5 and PS4 packages, perform security/hygiene checks.
#   2. Verifies notes.md is bilingual and up to date.
#   3. Tags the commit and pushes to origin.
#   4. Creates GitHub release with PS5 (PPSA99810.zip*) and PS4 (PSBoxCloudGaming[PS4]-vX.Y.Z.pkg*).
#   5. Monitors GitHub Actions until the Linux AppImage is built on Ubuntu 24.04 and attached.
#   6. Automatically signs the Linux AppImage using tools/desktop/sign-linux-release.sh.
#   7. Executes tools/release-verify.sh --verify-sigs to enforce 100% compliance.
#
# Usage:
#   tools/publish-release.sh <X.Y.Z> [--skip-build] [--yes]
#
set -euo pipefail

root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
cd "$root"

raw_version="${1:?usage: tools/publish-release.sh <X.Y.Z> [--skip-build] [--yes]}"
shift || true

skip_build=0
auto_confirm=0
for arg in "$@"; do
    case "$arg" in
        --skip-build) skip_build=1 ;;
        --yes|-y)     auto_confirm=1 ;;
        *) echo "warning: unknown argument '$arg'" >&2 ;;
    esac
done

version="${raw_version#v}"
tag="v$version"
out="$root/build-ps5/release-$tag"

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
BOLD='\033[1m'
NC='\033[0m'

echo -e "${BOLD}${BLUE}======================================================${NC}"
echo -e "${BOLD}${BLUE}  PSBox Cloud Gaming: Tri-Platform Release Publisher  ${NC}"
echo -e "${BOLD}${BLUE}  Target: $tag (PS5 + PS4 + Linux AppImage)            ${NC}"
echo -e "${BOLD}${BLUE}======================================================${NC}"

# Check release notes
[[ -f "$root/notes.md" ]] || { echo -e "${RED}error: notes.md not found${NC}" >&2; exit 1; }
grep -q "<!-- LANG:EN -->" "$root/notes.md" || { echo -e "${RED}error: notes.md missing <!-- LANG:EN -->${NC}" >&2; exit 1; }
grep -q "<!-- LANG:PT -->" "$root/notes.md" || { echo -e "${RED}error: notes.md missing <!-- LANG:PT -->${NC}" >&2; exit 1; }

# Phase 1: Build & Package PS5 and PS4
if [[ $skip_build -eq 0 ]]; then
    echo
    echo -e "${BOLD}Phase 1: Building and packaging PS5 and PS4...${NC}"
    tools/release.sh "$version" "$out"
else
    echo
    echo -e "${YELLOW}Phase 1: Skipping build (reusing $out)...${NC}"
fi

# Verify local artifacts exist
[[ -s "$out/PPSA99810.zip" ]] || { echo -e "${RED}error: $out/PPSA99810.zip missing${NC}" >&2; exit 1; }
[[ -s "$out/PPSA99810.zip.sha256" ]] || { echo -e "${RED}error: $out/PPSA99810.zip.sha256 missing${NC}" >&2; exit 1; }
[[ -s "$out/PPSA99810.zip.sig" ]] || { echo -e "${RED}error: $out/PPSA99810.zip.sig missing${NC}" >&2; exit 1; }
ps4_pkg="$out/PSBoxCloudGaming[PS4]-$tag.pkg"
[[ -s "$ps4_pkg" ]] || { echo -e "${RED}error: $ps4_pkg missing${NC}" >&2; exit 1; }
[[ -s "$ps4_pkg.sha256" ]] || { echo -e "${RED}error: $ps4_pkg.sha256 missing${NC}" >&2; exit 1; }

echo
echo -e "${GREEN}Local artifacts for PS5 and PS4 are ready and validated.${NC}"

# Confirmation prompt
if [[ $auto_confirm -eq 0 ]]; then
    echo
    read -rp "Publish release $tag to GitHub now? [y/N] " confirm
    if [[ ! "$confirm" =~ ^[yY]([eE][sS])?$ ]]; then
        echo "Aborted by user."
        exit 0
    fi
fi

# Phase 2: Git Tag
echo
echo -e "${BOLD}Phase 2: Tagging and pushing $tag...${NC}"
if git rev-parse -q --verify "refs/tags/$tag" >/dev/null; then
    echo "Note: tag $tag already exists locally."
else
    git tag -a "$tag" -m "PSBox Cloud Gaming $tag"
fi
git push origin "$tag"

# Phase 3: GitHub Release Creation (PS5 + PS4)
echo
echo -e "${BOLD}Phase 3: Creating GitHub Release $tag with PS5 and PS4 assets...${NC}"
# Use [[]PS4] escaping to prevent Go filepath.Glob from treating [PS4] as a regex character set
gh release create "$tag" \
    "$out/PPSA99810.zip" \
    "$out/PPSA99810.zip.sha256" \
    "$out/PPSA99810.zip.sig" \
    "$out/PSBoxCloudGaming[[]PS4]-$tag.pkg" \
    "$out/PSBoxCloudGaming[[]PS4]-$tag.pkg.sha256" \
    --target "$(git rev-parse HEAD)" \
    --title "PSBox Cloud Gaming $tag" \
    --notes-file "$root/notes.md" 2>/dev/null || {
    echo "Release $tag might already exist; updating assets..."
    gh release upload "$tag" \
        "$out/PPSA99810.zip" \
        "$out/PPSA99810.zip.sha256" \
        "$out/PPSA99810.zip.sig" \
        "$out/PSBoxCloudGaming[[]PS4]-$tag.pkg" \
        "$out/PSBoxCloudGaming[[]PS4]-$tag.pkg.sha256" \
        --clobber
    gh release edit "$tag" --notes-file "$root/notes.md"
}

# Phase 4: Wait for Linux AppImage from GitHub Actions
echo
echo -e "${BOLD}Phase 4: Waiting for Linux AppImage build (CI: .github/workflows/release-linux.yml)...${NC}"
linux_name="PSBox-$tag-linux-x86_64.AppImage"
timeout_secs=900 # 15 minutes max
elapsed=0
interval=15

while true; do
    assets_json=$(gh release view "$tag" --json assets 2>/dev/null || echo '{"assets":[]}')
    has_appimage=$(echo "$assets_json" | jq -r --arg n "$linux_name" '.assets[] | select(.name == $n and .state == "uploaded") | .name')
    if [[ -n "$has_appimage" ]]; then
        echo -e "${GREEN}Linux AppImage is uploaded on GitHub: $linux_name${NC}"
        break
    fi
    
    if (( elapsed >= timeout_secs )); then
        echo -e "${RED}Timed out waiting for Linux AppImage after $((timeout_secs / 60)) minutes!${NC}" >&2
        echo "You can check GitHub Actions workflow status and re-run:" >&2
        echo "  gh workflow view release-linux.yml" >&2
        echo "  tools/desktop/sign-linux-release.sh $tag" >&2
        echo "  tools/release-verify.sh $tag --verify-sigs" >&2
        exit 1
    fi
    
    echo "Waiting for $linux_name to be uploaded by CI... (${elapsed}s elapsed)"
    sleep "$interval"
    elapsed=$((elapsed + interval))
done

# Phase 5: Cryptographic Signature of Linux AppImage
echo
echo -e "${BOLD}Phase 5: Signing Linux AppImage with local release key...${NC}"
tools/desktop/sign-linux-release.sh "$tag"

# Phase 6: Final Tri-Platform Audit
echo
echo -e "${BOLD}Phase 6: Executing final release audit...${NC}"
tools/release-verify.sh "$tag" --verify-sigs

echo
echo -e "${BOLD}${GREEN}======================================================${NC}"
echo -e "${BOLD}${GREEN}  RELEASE $tag PUBLISHED AND VERIFIED SUCCESSFULLY!   ${NC}"
echo -e "${BOLD}${GREEN}  All 3 platforms (PS5, PS4, Linux) are complete.     ${NC}"
echo -e "${BOLD}${GREEN}======================================================${NC}"
