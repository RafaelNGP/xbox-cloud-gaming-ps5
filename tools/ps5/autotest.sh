#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 RafaelNGP
# One unattended console run: build, deploy, launch in autoplay mode, wait for
# the result, close the app and fetch its log (and frame.ppm when present).
#
#   PS5_HOST=<ip> tools/ps5/autotest.sh [titleId] [seconds] [option]
#   (option: nosimd = FFmpeg without SIMD; output in build-ps5/autotest)
#
# Needs PS5_Vulkan's ps5vkctl payload running on the console (launch/kill).
set -euo pipefail

root=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
vk=${PS5_VULKAN:-$(cd "$root/../../WoW-PS5/deps/PS5_Vulkan" && pwd)}
title_id=$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["titleId"])' "$root/ps5/sce_sys/param.json")
game=${1:-BALATRO}
seconds=${2:-45}
out=$root/build-ps5/autotest
option=${3:-}  # e.g. nosimd
: "${PS5_HOST:?set PS5_HOST to the console IP}"
export PS5_HOST
ctl() { (cd "$vk" && python3 tools/ps5_console.py "$@"); }

mkdir -p "$out"
rm -f "$out"/*.ppm "$out"/xcloud.log "$out"/stream.aus
ninja -C "$root/build-ps5" xcloud_app >/dev/null
bash "$root/tools/ps5/link.sh" "$root/build-ps5" | tail -1
XC_INCLUDE_ACCOUNT=1 bash "$root/tools/ps5/package.sh" "$root/build-ps5" >/dev/null

ctl kill "$title_id" >/dev/null 2>&1 || true
sleep 2
python3 "$vk/tools/deploy-title-folder.py" "$root/build-ps5/pkg/$title_id" | tail -1

# Fresh log, the autoplay request, no stale frame.
python3 - "$PS5_HOST" "${FTP_PORT:-2121}" "/data/homebrew/$title_id" "$game $seconds $option" <<'PY'
import io, sys
from ftplib import FTP, error_perm, error_reply
host, port, d, line = sys.argv[1], int(sys.argv[2]), sys.argv[3], sys.argv[4]
f = FTP(); f.connect(host, port, timeout=15); f.login(); f.cwd(d)
for name in ("xcloud.log", "frame.ppm", "frame2.ppm", "stream.aus", "ui.ppm", "launch.ppm", "screen.ppm", "detail.ppm", "library.ppm", "library2.ppm", "error.ppm"):
    try: f.sendcmd(f"DELE {name}")
    except (error_perm, error_reply): pass
f.storbinary("STOR autoplay.txt", io.BytesIO(line.encode() + b"\n"))
import os
sample = os.environ.get("XC_SAMPLE")
if line.startswith("BENCH") and sample:
    with open(sample, "rb") as src: f.storbinary("STOR sample.h264", src)
f.quit()
PY

ctl launch "$title_id" | tail -1
deadline=$(( $(date +%s) + seconds + 180 ))
fetch() {
    python3 - "$PS5_HOST" "${FTP_PORT:-2121}" "/data/homebrew/$title_id" "$out" "$@" <<'PY'
import os, sys
from ftplib import FTP, error_perm
host, port, d, out, names = sys.argv[1], int(sys.argv[2]), sys.argv[3], sys.argv[4], sys.argv[5:]
f = FTP(); f.connect(host, port, timeout=15); f.login(); f.cwd(d)
for name in names:
    try:
        with open(os.path.join(out, name + ".part"), "wb") as o: f.retrbinary(f"RETR {name}", o.write)
        os.replace(os.path.join(out, name + ".part"), os.path.join(out, name))
    except error_perm:
        try: os.remove(os.path.join(out, name + ".part"))
        except FileNotFoundError: pass
f.quit()
PY
}
result=timeout
while (( $(date +%s) < deadline )); do
    sleep 5
    fetch xcloud.log || continue
    if grep -q "AUTOPLAY END" "$out/xcloud.log" 2>/dev/null; then result=finished; break; fi
    # The process is gone without finishing: a crash.
    if ! ctl payload 2>/dev/null | grep -q "$title_id"; then
        status=$( (cd "$vk" && python3 - <<'PY'
import socket, os
s = socket.create_connection((os.environ["PS5_HOST"], 9111), timeout=5)
s.sendall(b"status\n"); print(s.recv(4096).decode(errors="replace").strip())
PY
) 2>/dev/null || true)
        if [[ $status != *"$title_id"* ]]; then result=crashed; break; fi
    fi
done
fetch xcloud.log frame.ppm frame2.ppm stream.aus ui.ppm launch.ppm screen.ppm detail.ppm library.ppm library2.ppm error.ppm || true
ctl kill "$title_id" >/dev/null 2>&1 || true
# Remove the request so a manual launch behaves normally.
python3 - "$PS5_HOST" "${FTP_PORT:-2121}" "/data/homebrew/$title_id" <<'PY'
import sys
from ftplib import FTP, error_perm, error_reply
f = FTP(); f.connect(sys.argv[1], int(sys.argv[2]), timeout=15); f.login(); f.cwd(sys.argv[3])
try: f.sendcmd("DELE autoplay.txt")
except (error_perm, error_reply): pass
f.quit()
PY
echo "RESULT: $result"
rm -f "$out"/frame*.png
for f in "$out"/*.ppm; do [[ -f $f ]] && echo "frame: $f"; done
echo "log: $out/xcloud.log"
