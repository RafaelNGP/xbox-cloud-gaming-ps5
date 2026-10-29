#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 RafaelNGP
# One unattended shadPS4 emulator run: build, stage, launch in autoplay mode,
# wait for result, stop the emulator, collect logs and convert PPM screenshots to PNG.
#
#   tools/ps4/autotest.sh [titleOrTest] [seconds] [options]
#   Example:
#     tools/ps4/autotest.sh AUTOTEST 30 librarytest
#     tools/ps4/autotest.sh AUTOTEST 30 settingstest
#     tools/ps4/autotest.sh BALATRO 30 dump
set -euo pipefail

root=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
test_title="${1:-AUTOTEST}"
seconds="${2:-30}"
test_opt="${3:-librarytest}"

out="$root/build-ps4/autotest"
mkdir -p "$out"
rm -f "$out"/*.ppm "$out"/*.png "$out"/xcloud.log

SHADPS4_BIN="${SHADPS4_BIN:-/home/notebook-storm/Downloads/shadps4/Shadps4-sdl.AppImage}"
if [ ! -x "$SHADPS4_BIN" ]; then
    echo "error: shadPS4 executable not found at $SHADPS4_BIN" >&2
    exit 1
fi

SHADPS4_DATA="${SHADPS4_DATA:-$HOME/.local/share/shadPS4}"
PSBOX_DATA="$SHADPS4_DATA/data/psbox"
STAGE_DIR="$SHADPS4_DATA/game_data/CUSA00100"
mkdir -p "$PSBOX_DATA" "$STAGE_DIR"

echo "==> Building PS4 target"
ninja -C "$root/build-ps4"

echo "==> Packaging PS4 target"
XC_INCLUDE_ACCOUNT=1 bash "$root/tools/ps4/package.sh" "$root/build-ps4" >/dev/null

echo "==> Staging to shadPS4 ($STAGE_DIR)"
cp -rf "$root/build-ps4/pkg_stage/." "$STAGE_DIR/"
if [ -f "$root/build-ps4/pkg_stage/account.json" ]; then
    cp -f "$root/build-ps4/pkg_stage/account.json" "$PSBOX_DATA/account.json"
fi

# Clean old dumps
for f in ui.ppm library.ppm splash.ppm detail.ppm settings.ppm screen.ppm error.ppm frame.ppm frame2.ppm xcloud.log; do
    rm -f "$PSBOX_DATA/$f"
done
rm -f "$PSBOX_DATA/autoplay.txt"

# Write test directive
echo "$test_title $seconds $test_opt" > "$PSBOX_DATA/autoplay.txt"
echo "==> Configured autoplay: $(cat "$PSBOX_DATA/autoplay.txt")"

# Launch shadPS4
echo "==> Launching shadPS4..."
"$SHADPS4_BIN" -g "$STAGE_DIR/eboot.bin" > "$out/shadps4.log" 2>&1 &
sleep 2

cleanup() {
    echo "==> Cleaning up..."
    pkill -TERM -f "shadps4" 2>/dev/null || true
    sleep 1
    pkill -9 -f "shadps4" 2>/dev/null || true
    rm -f "$PSBOX_DATA/autoplay.txt"
}
trap cleanup EXIT

deadline=$(( $(date +%s) + seconds + 90 ))
result="timeout"

echo "==> Waiting for test completion (deadline ${seconds}s + margin)..."
while (( $(date +%s) < deadline )); do
    sleep 3
    if ! pgrep -f "shadps4" >/dev/null 2>&1; then
        result="exited_early"
        break
    fi
    if [ -f "$PSBOX_DATA/xcloud.log" ] && grep -q "AUTOPLAY END" "$PSBOX_DATA/xcloud.log" 2>/dev/null; then
        result="finished"
        break
    fi
done

echo "==> Test result: $result"
sleep 2

# Copy results
if [ -f "$PSBOX_DATA/xcloud.log" ]; then
    cp -f "$PSBOX_DATA/xcloud.log" "$out/xcloud.log"
fi

python3 - <<PY
import os, glob
from PIL import Image

psbox_data = "$PSBOX_DATA"
out_dir = "$out"

for ppm in glob.glob(os.path.join(psbox_data, "*.ppm")):
    base = os.path.basename(ppm)
    dest_ppm = os.path.join(out_dir, base)
    dest_png = os.path.splitext(dest_ppm)[0] + ".png"
    try:
        im = Image.open(ppm)
        im.save(dest_png)
        print(f"Captured & converted: {dest_png}")
    except Exception as e:
        print(f"Failed converting {ppm}: {e}")
PY

echo "==> PS4 / shadPS4 autotest complete. Artifacts in $out"
