#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 PSBox Cloud Gaming contributors
# Upload the packaged app to the console over FTP, or fetch its log.
#
#   PS5_HOST=<ip> tools/ps5/deploy.sh [build-dir]       upload <build-dir>/pkg/<TITLE_ID>
#   PS5_HOST=<ip> tools/ps5/deploy.sh --log [out-file]  download xcloud.log
#
# Uses PS5_Vulkan's FTP uploader (temporary names, eboot.bin and param.json
# published last). Fully close the app on the console before uploading.
set -euo pipefail

root=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
vk=${PS5_VULKAN:-$(cd "$root/../../WoW-PS5/deps/PS5_Vulkan" && pwd)}
title=$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["titleId"])' "$root/ps5/sce_sys/param.json")
: "${PS5_HOST:?set PS5_HOST to the console IP}"
export PS5_HOST

if [[ ${1:-} == --log ]]; then
    out=${2:-$root/xcloud.log}
    python3 - "$PS5_HOST" "${FTP_PORT:-2121}" "/data/homebrew/$title/xcloud.log" "$out" <<'PY'
import sys
from ftplib import FTP
host, port, remote, out = sys.argv[1], int(sys.argv[2]), sys.argv[3], sys.argv[4]
ftp = FTP()
ftp.connect(host, port, timeout=15)
ftp.login()
with open(out, "wb") as f:
    ftp.retrbinary(f"RETR {remote}", f.write)
ftp.quit()
print(f"saved {out}")
PY
    exit 0
fi

build=$(cd "${1:-$root/build-ps5}" && pwd)
python3 "$vk/tools/deploy-title-folder.py" "$build/pkg/$title"
