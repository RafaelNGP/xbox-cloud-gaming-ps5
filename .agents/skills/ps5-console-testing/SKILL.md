---
name: ps5-console-testing
description: >-
  Provides procedures, scripts, and runbooks to build, deploy, execute, and verify PS5 applications (like xCloud-PS5) on a PS5 console using ps5vkctl, deploy.sh, and headless autoplay.txt screenshot verification.
---

# PS5 Console Testing & Visual Verification Workflow

This skill guides agents on building, deploying, controlling, and autonomously verifying UI and graphics on a real PlayStation 5 console without requiring manual user testing.

---

## Environment & Prerequisites

- **Console IP**: Defined by environment variable `PS5_HOST` (typically `192.168.15.17`).
- **Resident Payload**: `ps5vkctl` listening on port `9111` on the PS5.
- **FTP Server**: Port `2121` on the PS5 (root: `/data/homebrew/<TITLE_ID>/`).
- **Title ID**: `PPSA99810` (for xCloud-PS5).
- **Vulkan Engine & Tools Path**: `../PS5_Vulkan/tools/` (or symlink in workspace).

---

## 1. Terminating Active Instances

Before deploying or touching files on the console, ensure the application is stopped to avoid file locks (especially `eboot.bin`):

```bash
PS5_HOST=192.168.15.17 python3 ../PS5_Vulkan/tools/ps5_console.py kill PPSA99810
```

---

## 2. Compiling and Packaging

Compile the target executable, link dependencies, and package into the staging directory:

```bash
ninja -C build-ps5 xcloud_app
bash tools/ps5/link.sh build-ps5
XC_INCLUDE_ACCOUNT=1 bash tools/ps5/package.sh build-ps5
```

---

## 3. Deploying to the Console

Deploy the packaged title folder directly over FTP using the PS5 deployment helper:

```bash
PS5_HOST=192.168.15.17 ./tools/ps5/deploy.sh build-ps5
```

---

## 4. Headless Autoplay Navigation & Screenshot Configuration

The application supports automated scripted actions configured via `/data/homebrew/PPSA99810/autoplay.txt`.

### Format of `autoplay.txt`:
```text
<TITLE_OR_SCREEN> <SECONDS> <OPTIONS>
```

### Common Autoplay Commands:
- `AUTOTEST 30 gamesettingstest`: Navigates to game details, opens Game Settings via Options button, tests d-pad and custom overrides, and captures:
  - `detail.ppm`: Details screen with Options button hint.
  - `game_settings.ppm`: Default Game Settings modal.
  - `game_settings_custom.ppm`: Modified per-game profile modal.
  - `detail_after.ppm`: Details screen after saving and closing.
- `GRID 30 gridtest`: Navigates down into the expanded All Games grid, performs D-Pad navigation, and captures:
  - `ui.ppm`: Initial home screen UI snapshot.
  - `grid1.ppm`: Expanded grid visible state.
  - `grid2.ppm`: Secondary scroll position in grid.
  - `grid_back.ppm`: Grid after return navigation.
- `BALATRO 30 dump`: Launches a stream session for Balatro, dumping frames for decoder analysis.
- `AUTOTEST 30 librarytest`: Navigates to Library tab and dumps `library.ppm`.
- `AUTOTEST 30 settingstest`: Navigates to Settings tab and dumps `settings.ppm`.

> [!IMPORTANT]
> Always use `AUTOTEST` as the title parameter for UI-only tests (such as `gamesettingstest`, `librarytest`, `settingstest`). Using arbitrary title strings can cause `worker.cpp` to attempt starting a cloud stream session concurrently with your UI test.

> [!NOTE]
> The PS5 resident FTP server (`ftpsrv` on port 2121) does not support `NLST` (`ftp.nlst()` raises `502 Command not recognized`). Always specify explicit file lists for deletion/download and catch `(error_perm, error_reply)` as `DELE` may reply with `226`.

### Uploading `autoplay.txt` and Clearing Old Screenshots via Python:

```bash
python3 - <<'PY'
import sys, io
from ftplib import FTP, error_perm, error_reply

host = "192.168.15.17"
port = 2121
remote_dir = "/data/homebrew/PPSA99810"

ftp = FTP()
ftp.connect(host, port, timeout=15)
ftp.login()
ftp.cwd(remote_dir)

# Clean up previous dumps
ppm_files = [
    "ui.ppm", "grid1.ppm", "grid2.ppm", "grid_back.ppm", "launch.ppm",
    "library.ppm", "settings.ppm", "error.ppm",
    "detail.ppm", "game_settings.ppm", "game_settings_custom.ppm", "detail_after.ppm"
]
for f in ppm_files:
    try:
        ftp.sendcmd(f"DELE {f}")
    except (error_perm, error_reply):
        pass

# Upload new test directive
ftp.storbinary("STOR autoplay.txt", io.BytesIO(b"AUTOTEST 30 gamesettingstest\n"))
ftp.quit()
print("Autoplay test configured successfully.")
PY
```

---

## 5. Launching Remotely

Launch the title on the PS5:

```bash
PS5_HOST=192.168.15.17 python3 ../PS5_Vulkan/tools/ps5_console.py launch PPSA99810
```

Wait ~10-15 seconds for the test actions to complete and frame dumps to be written to disk.

---

## 6. Retrieving Screenshots and Visual Inspection

Download the captured PPM screenshots, convert to PNG, and inspect:

```bash
python3 - <<'PY'
import os
from ftplib import FTP, error_perm
from PIL import Image

host = "192.168.15.17"
port = 2121
remote_dir = "/data/homebrew/PPSA99810"
out_dir = "/tmp/ps5_shots"
os.makedirs(out_dir, exist_ok=True)

ftp = FTP()
ftp.connect(host, port, timeout=15)
ftp.login()
ftp.cwd(remote_dir)

names = ["ui.ppm", "grid1.ppm", "grid2.ppm", "grid_back.ppm"]
for name in names:
    try:
        local_ppm = os.path.join(out_dir, name)
        with open(local_ppm, "wb") as f:
            ftp.retrbinary(f"RETR {name}", f.write)
        # Convert to PNG
        local_png = os.path.splitext(local_ppm)[0] + ".png"
        Image.open(local_ppm).save(local_png)
        print(f"Captured: {local_png}")
    except error_perm:
        print(f"File {name} not found on console.")
ftp.quit()
PY
```

### Inspecting Images:
Call the `view_file` tool on the resulting PNG files (e.g. `/tmp/ps5_shots/grid1.png`) to visually inspect layout, text rendering, borders, and clipping.

---

## 7. Mandatory Post-Test Cleanup

Always delete `autoplay.txt` so the application behaves normally when launched by the user:

```bash
python3 - <<'PY'
from ftplib import FTP, error_perm, error_reply

ftp = FTP()
ftp.connect("192.168.15.17", 2121, timeout=15)
ftp.login()
ftp.cwd("/data/homebrew/PPSA99810")
try:
    ftp.sendcmd("DELE autoplay.txt")
    print("Deleted autoplay.txt successfully.")
except (error_perm, error_reply):
    pass
ftp.quit()
PY
```

If ready for the user, restart the application in normal mode:

```bash
PS5_HOST=192.168.15.17 python3 ../PS5_Vulkan/tools/ps5_console.py kill PPSA99810
PS5_HOST=192.168.15.17 python3 ../PS5_Vulkan/tools/ps5_console.py launch PPSA99810
```

---

## 8. Official Signed Release Workflow

To build, verify, sign, and publish an official release (`vX.Y.Z`):

1. **Update Versions**:
   - `CMakeLists.txt`: `project(xcloud_ps5 VERSION X.Y.Z LANGUAGES C CXX)`
   - `ps5/sce_sys/param.json`: `"contentVersion": "0X.YZZ.000"` (e.g. `00.902.000` for version `0.9.2`)
   - `ROADMAP.md`: update completed features with the new version tag.

2. **Git Branch & PR Flow**:
   - Create and switch to release branch: `git checkout -b release/vX.Y.Z`
   - Stage and commit with descriptive release message: `git commit -m "vX.Y.Z: ..."`
   - Push branch: `git push -u origin release/vX.Y.Z`
   - Create PR: `gh pr create --base main --head release/vX.Y.Z --title "Release vX.Y.Z" --body "..."`
   - Merge PR: `gh pr merge <PR_NUMBER> --merge`
   - Return to main and sync: `git checkout main && git pull origin main`

3. **Execute Automated Release Script**:
   - Requires private release signing key at `~/.config/psbox-release/signing-key.pem`
   ```bash
   tools/release.sh X.Y.Z
   ```
   This script performs:
   - Git cleanliness and uncommitted changes check.
   - Version match checks across `CMakeLists.txt` and `param.json`.
   - Security audit (ensures no tokens or private cache files are included in the build).
   - Building and packaging without developer account data (`XC_INCLUDE_ACCOUNT` disabled).
   - Packaging `PPSA99810.zip`, computing `PPSA99810.zip.sha256`.
   - Cryptographic signature `PPSA99810.zip.sig` with the project's RSA key (verified against `updater.cpp`).

4. **Publish to GitHub Releases**:
   - Prepare release notes (`notes.md`)
   ```bash
   gh release create vX.Y.Z \
     "build-ps5/release-vX.Y.Z/PPSA99810.zip" \
     "build-ps5/release-vX.Y.Z/PPSA99810.zip.sha256" \
     "build-ps5/release-vX.Y.Z/PPSA99810.zip.sig" \
     --target $(git rev-parse HEAD) \
     --title "PSBox Cloud Gaming vX.Y.Z" \
     --notes-file notes.md
   ```

