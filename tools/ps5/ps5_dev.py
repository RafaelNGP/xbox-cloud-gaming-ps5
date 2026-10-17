#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 RafaelNGP
"""
Unified developer tool for xCloud-PS5.
Usage:
  python3 tools/ps5/ps5_dev.py verify
  python3 tools/ps5/ps5_dev.py deploy
  python3 tools/ps5/ps5_dev.py test [target] [--duration SEC] [--interactive]
  python3 tools/ps5/ps5_dev.py log [--lines N] [--tail]
"""

import argparse
import io
import json
import os
import re
import subprocess
import sys
import time
from ftplib import FTP, error_perm, error_reply
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent.parent
PS5_VULKAN = Path(os.environ.get("PS5_VULKAN", ROOT.parent / "PS5_Vulkan")).resolve()
DEFAULT_HOST = os.environ.get("PS5_HOST", "192.168.15.17")
DEFAULT_PORT = int(os.environ.get("FTP_PORT", "2121"))
TITLE_ID = "PPSA99810"

# ANSI colors
GREEN = "\033[92m"
RED = "\033[91m"
YELLOW = "\033[93m"
CYAN = "\033[96m"
BOLD = "\033[1m"
RESET = "\033[0m"


def run_cmd(cmd, cwd=ROOT, capture=False, check=True):
    if isinstance(cmd, str):
        shell = True
    else:
        shell = False
    if capture:
        res = subprocess.run(cmd, cwd=cwd, shell=shell, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    else:
        res = subprocess.run(cmd, cwd=cwd, shell=shell)
    if check and res.returncode != 0:
        if capture:
            print(res.stdout)
        raise subprocess.CalledProcessError(res.returncode, cmd)
    return res


def console_ctl(action, title_id=TITLE_ID, host=DEFAULT_HOST):
    script = PS5_VULKAN / "tools" / "ps5_console.py"
    if not script.exists():
        print(f"{YELLOW}Warning: ps5_console.py not found at {script}{RESET}")
        return False
    env = os.environ.copy()
    env["PS5_HOST"] = host
    res = subprocess.run([sys.executable, str(script), action, title_id], env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    return res.returncode == 0


def get_ftp(host=DEFAULT_HOST, port=DEFAULT_PORT):
    ftp = FTP()
    ftp.connect(host, port, timeout=10)
    ftp.login()
    return ftp


def clean_ps5_hygiene(host=DEFAULT_HOST, port=DEFAULT_PORT):
    """Ensures autoplay.txt does not linger on console in either path."""
    try:
        ftp = get_ftp(host, port)
        for base_dir in [f"/data/homebrew/{TITLE_ID}", "/data/psbox"]:
            try:
                ftp.cwd(base_dir)
                try:
                    ftp.sendcmd("DELE autoplay.txt")
                except (error_perm, error_reply):
                    pass
            except (error_perm, error_reply):
                pass
        ftp.quit()
    except Exception as e:
        print(f"{YELLOW}Note: FTP cleanup error: {e}{RESET}")


def cmd_verify(args):
    print(f"\n{BOLD}{CYAN}=== xCloud-PS5 Multi-Platform Verification ==={RESET}\n")
    steps = [
        ("Desktop (SDL3) Build", "ninja -C build-desktop"),
        ("Desktop (SDL3) Unit Tests", "build-desktop/xcloud-tests"),
        ("Desktop (SDL3) Selftest", "SDL_VIDEO_DRIVER=offscreen SDL_AUDIO_DRIVER=dummy build-desktop/psbox --selftest /tmp/psbox-selftest"),
        ("Desktop (No-SDL) Build", "ninja -C build-desktop-nosdl"),
        ("Desktop (No-SDL) Unit Tests", "build-desktop-nosdl/xcloud-tests"),
        ("PS5 Build", "ninja -C build-ps5 xcloud_app"),
        ("PS5 Link Staged eboot.bin", "bash tools/ps5/link.sh build-ps5"),
        ("Platform Boundaries Check", "bash tools/check-platform-boundaries.sh"),
    ]
    results = []
    all_ok = True
    for name, cmd in steps:
        sys.stdout.write(f"  {name:35} ... ")
        sys.stdout.flush()
        t0 = time.time()
        res = run_cmd(cmd, cwd=ROOT, capture=True, check=False)
        dt = time.time() - t0
        if res.returncode == 0:
            print(f"{GREEN}[PASS]{RESET} ({dt:.1f}s)")
            results.append((name, True, ""))
        else:
            print(f"{RED}[FAIL]{RESET} ({dt:.1f}s)")
            results.append((name, False, res.stdout))
            all_ok = False
            break

    print("\n" + "=" * 50)
    if all_ok:
        print(f"{BOLD}{GREEN}ALL PLATFORM BUILDS, TESTS & BOUNDARIES PASSED!{RESET}")
        return 0
    else:
        print(f"{BOLD}{RED}VERIFICATION FAILED! Output from failed step:{RESET}\n")
        for name, ok, output in results:
            if not ok:
                print(f"--- {name} ---\n{output}")
        return 1


def cmd_deploy(args):
    host = args.host or DEFAULT_HOST
    print(f"\n{BOLD}{CYAN}=== Deploying xCloud-PS5 to {host} ==={RESET}\n")
    # 1. Kill running app
    print("  Stopping running instance on console...")
    console_ctl("kill", TITLE_ID, host)
    time.sleep(1)

    # 2. Package
    print("  Packaging build-ps5 with account credentials...")
    run_cmd("XC_INCLUDE_ACCOUNT=1 bash tools/ps5/package.sh build-ps5", cwd=ROOT)

    # 3. Deploy
    print("  Uploading package to PS5 over FTP...")
    run_cmd(f"PS5_HOST={host} ./tools/ps5/deploy.sh build-ps5", cwd=ROOT)

    # 4. Clean autoplay
    print("  Ensuring console hygiene (removing autoplay.txt)...")
    clean_ps5_hygiene(host, args.port)

    # 5. Launch
    print(f"  Launching {TITLE_ID} in foreground on PS5...")
    console_ctl("launch", TITLE_ID, host)
    print(f"\n{BOLD}{GREEN}Deployment completed successfully! App is active on TV.{RESET}\n")
    return 0


def convert_ppm_to_png(ppm_path: Path):
    try:
        from PIL import Image
        png_path = ppm_path.with_suffix(".png")
        with Image.open(ppm_path) as img:
            img.save(png_path, "PNG")
        return png_path
    except ImportError:
        return None


def cmd_test(args):
    host = args.host or DEFAULT_HOST
    port = args.port or DEFAULT_PORT
    target = args.target
    duration = args.duration
    out_dir = ROOT / "build-ps5" / "autotest"
    out_dir.mkdir(parents=True, exist_ok=True)

    print(f"\n{BOLD}{CYAN}=== Autonomous PS5 Test: {target} ({duration}s) ==={RESET}\n")

    # Format autoplay command
    # If target doesn't already have AUTOTEST or BENCH or game name:
    if target.startswith("AUTOTEST") or target.startswith("BENCH"):
        autoplay_cmd = target
    else:
        autoplay_cmd = f"AUTOTEST {duration} {target}"

    print("  1. Stopping app on console...")
    console_ctl("kill", TITLE_ID, host)
    time.sleep(1)

    # Clean local shots
    for old_file in out_dir.glob("*.ppm"):
        old_file.unlink(missing_ok=True)
    for old_file in out_dir.glob("*.png"):
        old_file.unlink(missing_ok=True)
    (out_dir / "xcloud.log").unlink(missing_ok=True)

    print("  2. Packaging fresh build...")
    run_cmd("XC_INCLUDE_ACCOUNT=1 bash tools/ps5/package.sh build-ps5", cwd=ROOT)
    run_cmd(f"PS5_HOST={host} ./tools/ps5/deploy.sh build-ps5", cwd=ROOT)

    print("  3. Configuring test on PS5 via FTP...")
    ftp = get_ftp(host, port)
    try:
        ftp.cwd("/data/psbox")
    except error_perm:
        ftp.mkd("/data/psbox")
        ftp.cwd("/data/psbox")

    # Delete old artifacts on PS5
    for name in ("xcloud.log", "frame.ppm", "frame2.ppm", "ui.ppm", "launch.ppm", "detail.ppm",
                 "settings.ppm", "game_settings.ppm", "game_settings_custom.ppm", "library.ppm"):
        try:
            ftp.sendcmd(f"DELE {name}")
        except (error_perm, error_reply):
            pass

    # Write autoplay.txt
    ftp.storbinary("STOR autoplay.txt", io.BytesIO(autoplay_cmd.encode() + b"\n"))
    ftp.quit()

    print(f"  4. Launching test on console ({autoplay_cmd})...")
    console_ctl("launch", TITLE_ID, host)

    # Wait loop
    print(f"  5. Waiting {duration}s for autonomous test to complete...")
    deadline = time.time() + duration + 15
    finished = False
    while time.time() < deadline:
        time.sleep(3)
        try:
            f = get_ftp(host, port)
            f.cwd("/data/psbox")
            bio = io.BytesIO()
            f.retrbinary("RETR xcloud.log", bio.write)
            f.quit()
            log_content = bio.getvalue().decode("utf-8", errors="replace")
            if "AUTOPLAY END" in log_content:
                finished = True
                break
        except Exception:
            pass

    print(f"  6. Test finished: {'SUCCESS' if finished else 'TIMEOUT'}. Downloading results...")
    # Fetch all PPMs and log
    ppm_names = ["ui.ppm", "launch.ppm", "frame.ppm", "frame2.ppm", "detail.ppm", "settings.ppm",
                 "game_settings.ppm", "game_settings_custom.ppm", "library.ppm", "error.ppm"]
    ftp = get_ftp(host, port)
    ftp.cwd("/data/psbox")
    for name in ppm_names + ["xcloud.log"]:
        try:
            target_path = out_dir / name
            with open(target_path, "wb") as f_out:
                ftp.retrbinary(f"RETR {name}", f_out.write)
        except (error_perm, error_reply):
            pass
    ftp.quit()

    # 7. Convert PPM to PNG
    converted_pngs = []
    for ppm in out_dir.glob("*.ppm"):
        png = convert_ppm_to_png(ppm)
        if png:
            converted_pngs.append(png)

    # 8. Clean autoplay immediately
    clean_ps5_hygiene(host, port)
    console_ctl("kill", TITLE_ID, host)

    # 9. Summary and Metrics
    log_file = out_dir / "xcloud.log"
    print("\n" + "=" * 60)
    print(f"{BOLD}{CYAN}TEST REPORT: {target}{RESET}")
    print("=" * 60)
    if converted_pngs:
        print(f"\n{BOLD}Captured Screenshots ({len(converted_pngs)}):{RESET}")
        for png in converted_pngs:
            print(f"  - {png}")

    if log_file.exists():
        log_text = log_file.read_text(errors="replace")
        stream_lines = [line for line in log_text.splitlines() if " [I] stream:" in line]
        audio_lines = [line for line in log_text.splitlines() if " [I] audio out:" in line]
        warnings = [line for line in log_text.splitlines() if " [W] " in line]

        if stream_lines:
            last_stream = stream_lines[-1]
            print(f"\n{BOLD}Final Stream Metrics:{RESET}")
            print(f"  {last_stream}")
        if audio_lines:
            last_audio = audio_lines[-1]
            print(f"\n{BOLD}Final Audio Metrics:{RESET}")
            print(f"  {last_audio}")
        if warnings:
            print(f"\n{BOLD}{YELLOW}Warnings Detected ({len(warnings)}):{RESET}")
            for w in warnings[-5:]:
                print(f"  {w}")
    print("=" * 60)

    # Restart interactive if requested
    if args.interactive:
        print(f"\n  Restarting app in interactive mode for user...")
        console_ctl("launch", TITLE_ID, host)

    return 0 if finished else 1


def cmd_log(args):
    host = args.host or DEFAULT_HOST
    port = args.port or DEFAULT_PORT
    ftp = get_ftp(host, port)
    ftp.cwd("/data/psbox")
    bio = io.BytesIO()
    ftp.retrbinary("RETR xcloud.log", bio.write)
    ftp.quit()
    lines = bio.getvalue().decode("utf-8", errors="replace").splitlines()
    n = args.lines or 50
    for line in lines[-n:]:
        print(line)
    return 0


def main():
    parser = argparse.ArgumentParser(description="xCloud-PS5 Developer & QA Suite")
    parser.add_argument("--host", default=DEFAULT_HOST, help="PS5 Console IP")
    parser.add_argument("--port", type=int, default=DEFAULT_PORT, help="PS5 FTP Port (default 2121)")

    subparsers = parser.add_subparsers(dest="subcommand", required=True)

    # verify
    subparsers.add_parser("verify", help="Run full 3-platform compilation, tests, and boundary checks")

    # deploy
    subparsers.add_parser("deploy", help="Package with account, kill previous, upload to PS5 and launch")

    # test
    test_p = subparsers.add_parser("test", help="Run autonomous UI or streaming test and capture shots/metrics")
    test_p.add_argument("target", help="Test name (e.g. gamesettingstest, settingstest, crashtest, or game title BALATRO)")
    test_p.add_argument("--duration", type=int, default=30, help="Test duration in seconds (default 30)")
    test_p.add_argument("--interactive", action="store_true", default=True, help="Relaunch app interactively after test")
    test_p.add_argument("--no-interactive", dest="interactive", action="store_false", help="Leave app closed after test")

    # log
    log_p = subparsers.add_parser("log", help="Fetch and print tail of console log")
    log_p.add_argument("--lines", "-n", type=int, default=60, help="Number of lines to display")

    args = parser.parse_args()

    if args.subcommand == "verify":
        sys.exit(cmd_verify(args))
    elif args.subcommand == "deploy":
        sys.exit(cmd_deploy(args))
    elif args.subcommand == "test":
        sys.exit(cmd_test(args))
    elif args.subcommand == "log":
        sys.exit(cmd_log(args))


if __name__ == "__main__":
    main()
