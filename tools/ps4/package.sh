#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 RafaelNGP
# Turn a built xCloud PS4 ELF into eboot.bin and an installable PS4 .pkg using the
# OpenOrbis tools (create-fself, create-gp4, PkgTool.Core).
#
#   tools/ps4/package.sh <build-dir> [out-dir]
set -euo pipefail

BUILD_DIR_ARG="${1:?usage: package.sh <build-dir> [out-dir]}"
BUILD_DIR="$(cd "$BUILD_DIR_ARG" && pwd)"
OUT_DIR="${2:-$BUILD_DIR/pkg}"
mkdir -p "$OUT_DIR"
OUT_DIR="$(cd "$OUT_DIR" && pwd)"
REPO_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"

if [ -z "${OO_PS4_TOOLCHAIN:-}" ]; then
    if [ -d "$REPO_DIR/ps4-toolchain" ]; then
        export OO_PS4_TOOLCHAIN="$REPO_DIR/ps4-toolchain"
    else
        echo "error: OO_PS4_TOOLCHAIN not set and ps4-toolchain not found" >&2
        exit 1
    fi
fi

case "$(uname -s)" in
    Linux)  TOOLS="$OO_PS4_TOOLCHAIN/bin/linux" ;;
    Darwin) TOOLS="$OO_PS4_TOOLCHAIN/bin/macos" ;;
    *)      echo "unsupported host $(uname -s)" >&2; exit 1 ;;
esac

export DOTNET_SYSTEM_GLOBALIZATION_INVARIANT=1
if [ -n "${XC_PS4_LIBSSL11_DIR:-}" ]; then
    export LD_LIBRARY_PATH="${XC_PS4_LIBSSL11_DIR}${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
elif [ "$(uname -s)" = "Linux" ] && ! ldconfig -p 2>/dev/null | grep -q "libssl\.so\.1\.1"; then
    _shim="${TMPDIR:-/tmp}/xcloud-openssl11-shim"
    if [ ! -f "$_shim/libssl.so.1.1" ]; then
        mkdir -p "$_shim"
        cat > "$_shim/shim.c" <<'SHIM_C'
extern void ERR_new(void);
extern void ERR_set_debug(const char *file, int line, const char *func);
extern void ERR_set_error(int lib, int reason, const char *fmt, ...);
void ERR_put_error(int lib, int func, int reason, const char *file, int line);
void ERR_put_error(int lib, int func, int reason, const char *file, int line) {
    (void)func; ERR_new(); ERR_set_debug(file, line, 0); ERR_set_error(lib, reason, 0);
}
SHIM_C
        cat > "$_shim/tramp.s" <<'SHIM_S'
.text
.globl EVP_MD_size
.type EVP_MD_size, @function
EVP_MD_size:
    jmp EVP_MD_get_size@PLT
.size EVP_MD_size, .-EVP_MD_size
.globl SSL_get_peer_certificate
.type SSL_get_peer_certificate, @function
SSL_get_peer_certificate:
    jmp SSL_get1_peer_certificate@PLT
.size SSL_get_peer_certificate, .-SSL_get_peer_certificate
.section .note.GNU-stack,"",@progbits
SHIM_S
        if ! ( cc -shared -fPIC -O2 -o "$_shim/libssl.so.1.1" \
                  "$_shim/shim.c" "$_shim/tramp.s" -Wl,-soname,libssl.so.1.1 \
                  -Wl,--no-as-needed -l:libssl.so.3 -l:libcrypto.so.3 && \
               cc -shared -fPIC -O2 -o "$_shim/libcrypto.so.1.1" \
                  "$_shim/shim.c" "$_shim/tramp.s" -Wl,-soname,libcrypto.so.1.1 \
                  -Wl,--no-as-needed -l:libcrypto.so.3 ) 2>/dev/null; then
            echo "note: could not build the OpenSSL 1.1 shim; set XC_PS4_LIBSSL11_DIR if PkgTool fails" >&2
        fi
    fi
    [ -f "$_shim/libssl.so.1.1" ] && export LD_LIBRARY_PATH="$_shim${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
fi

TITLE="${XC_PS4_TITLE:-PSBox Cloud Gaming - PS4}"
VERSION="${XC_PS4_VERSION:-01.54}"
# PS5 backward-compatibility checks every PS4 PKG against Sony's Category1 whitelist.
# Using a whitelisted Title ID (like CUSA00100) allows the PS4 PKG to run on PS5 backward compatibility.
TITLE_ID="${XC_PS4_TITLE_ID:-PSBX00001}"
CONTENT_ID="${XC_PS4_CONTENT_ID:-IV0000-${TITLE_ID}_00-PSBOXCLOUDGAME00}"
FSELF_PAID="${XC_PS4_FSELF_PAID:-0x3800000000000011}"

ELF="$BUILD_DIR/xcloud_ps4.elf"
[ -f "$ELF" ] || { echo "error: $ELF not found (build with cmake/ps4.toolchain.cmake first)" >&2; exit 1; }

STAGE_DIR="$BUILD_DIR/pkg_stage"
rm -rf -- "$STAGE_DIR" "$OUT_DIR"
mkdir -p "$STAGE_DIR/sce_sys" "$STAGE_DIR/sce_module" "$STAGE_DIR/assets" "$OUT_DIR"

echo "==> eboot.bin"
if [ -n "${XC_PS4_FSELF_AUTH_INFO:-}" ]; then
    "$TOOLS/create-fself" -in="$ELF" -out="$STAGE_DIR/temp.oelf" --eboot "$STAGE_DIR/eboot.bin" --paid "$FSELF_PAID" --authinfo "$XC_PS4_FSELF_AUTH_INFO"
else
    "$TOOLS/create-fself" -in="$ELF" -out="$STAGE_DIR/temp.oelf" --eboot "$STAGE_DIR/eboot.bin" --paid "$FSELF_PAID"
fi
rm -f "$STAGE_DIR/temp.oelf"

echo "==> sce_sys"
cp "$REPO_DIR/ps4/sce_sys/icon0.png" "$STAGE_DIR/sce_sys/icon0.png"
if [ -f "$REPO_DIR/ps4/sce_sys/pic1.png" ]; then
    cp "$REPO_DIR/ps4/sce_sys/pic1.png" "$STAGE_DIR/sce_sys/pic1.png"
fi
if [ -f "$REPO_DIR/ps4/sce_sys/keystone" ]; then
    cp "$REPO_DIR/ps4/sce_sys/keystone" "$STAGE_DIR/sce_sys/keystone"
fi

if [ -f "$OO_PS4_TOOLCHAIN/samples/hello_world/sce_sys/about/right.sprx" ]; then
    mkdir -p "$STAGE_DIR/sce_sys/about"
    cp "$OO_PS4_TOOLCHAIN/samples/hello_world/sce_sys/about/right.sprx" "$STAGE_DIR/sce_sys/about/right.sprx"
fi

# OpenOrbis runtime requires stub modules in sce_module/ to satisfy system loader verification
for m in libc.prx libSceFios2.prx; do
    if [ -f "$REPO_DIR/ps4/sce_module/$m" ]; then
        cp "$REPO_DIR/ps4/sce_module/$m" "$STAGE_DIR/sce_module/$m"
    elif [ -f "$OO_PS4_TOOLCHAIN/samples/hello_world/sce_module/$m" ]; then
        cp "$OO_PS4_TOOLCHAIN/samples/hello_world/sce_module/$m" "$STAGE_DIR/sce_module/$m"
    fi
done

SFO="$STAGE_DIR/sce_sys/param.sfo"
"$TOOLS/PkgTool.Core" sfo_new "$SFO"
"$TOOLS/PkgTool.Core" sfo_setentry "$SFO" APP_TYPE --type Integer --maxsize 4 --value 1
"$TOOLS/PkgTool.Core" sfo_setentry "$SFO" APP_VER --type Utf8 --maxsize 8 --value "$VERSION"
"$TOOLS/PkgTool.Core" sfo_setentry "$SFO" ATTRIBUTE --type Integer --maxsize 4 --value 0
"$TOOLS/PkgTool.Core" sfo_setentry "$SFO" CATEGORY --type Utf8 --maxsize 4 --value 'gd'
"$TOOLS/PkgTool.Core" sfo_setentry "$SFO" CONTENT_ID --type Utf8 --maxsize 48 --value "$CONTENT_ID"
"$TOOLS/PkgTool.Core" sfo_setentry "$SFO" DOWNLOAD_DATA_SIZE --type Integer --maxsize 4 --value 0
"$TOOLS/PkgTool.Core" sfo_setentry "$SFO" SYSTEM_VER --type Integer --maxsize 4 --value 0
"$TOOLS/PkgTool.Core" sfo_setentry "$SFO" TITLE --type Utf8 --maxsize 128 --value "$TITLE"
"$TOOLS/PkgTool.Core" sfo_setentry "$SFO" TITLE_ID --type Utf8 --maxsize 12 --value "$TITLE_ID"
"$TOOLS/PkgTool.Core" sfo_setentry "$SFO" VERSION --type Utf8 --maxsize 8 --value "$VERSION"

echo "==> runtime assets"
cp -r "$REPO_DIR/assets/." "$STAGE_DIR/assets/"
rm -rf "$STAGE_DIR/assets/media_showcase"

mkdir -p "$STAGE_DIR/licenses"
cp "$REPO_DIR/LICENSE" "$STAGE_DIR/LICENSE"
cp "$REPO_DIR/THIRD_PARTY_NOTICES.md" "$STAGE_DIR/licenses/"
cp "$REPO_DIR/assets/fonts/OFL.txt" "$STAGE_DIR/licenses/Inter-OFL.txt"
cp "$REPO_DIR/deps/mbedtls/LICENSE" "$STAGE_DIR/licenses/mbedtls-LICENSE.txt"
cp "$REPO_DIR/deps/libdatachannel/LICENSE" "$STAGE_DIR/licenses/libdatachannel-MPL-2.0.txt"
cp "$REPO_DIR/deps/ffmpeg/COPYING.LGPLv2.1" "$STAGE_DIR/licenses/FFmpeg-LGPL-2.1.txt"
cp "$REPO_DIR/extern/stb/LICENSE" "$STAGE_DIR/licenses/stb-LICENSE.txt"

# Development only: XC_INCLUDE_ACCOUNT=1 ships the account.json
if [[ ${XC_INCLUDE_ACCOUNT:-0} == 1 && -f "$REPO_DIR/build-host/data/account.json" ]]; then
    cp "$REPO_DIR/build-host/data/account.json" "$STAGE_DIR/account.json"
fi

echo "==> building PKG"
( cd "$STAGE_DIR" && \
  FILES="$(find . -type f ! -name '*.gp4' ! -name '*.pkg' ! -name '*.oelf' ! -name '*.elf' | sed 's#^\./##' | tr '\n' ' ')" && \
  "$TOOLS/create-gp4" -out pkg.gp4 --content-id="$CONTENT_ID" --files "$FILES" && \
  python3 "$REPO_DIR/tools/ps4/gp4_rootdir.py" pkg.gp4 . && \
  "$TOOLS/PkgTool.Core" pkg_build pkg.gp4 "$OUT_DIR" )

mv -f "$OUT_DIR/${CONTENT_ID}.pkg" "$OUT_DIR/PSBox Cloud Gaming - PS4.pkg"

echo
echo "Package generated in: $OUT_DIR"
ls -lh "$OUT_DIR"
