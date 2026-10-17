#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 RafaelNGP
"""
Real-time live log streamer for xCloud-PS5 over FTP.
Monitors /data/psbox/xcloud.log on the PlayStation 5 console without killing
or disturbing active stream sessions.
"""

import argparse
import io
import os
import re
import sys
import time
from ftplib import FTP, error_perm, error_reply

DEFAULT_HOST = os.environ.get("PS5_HOST", "192.168.15.17")
DEFAULT_PORT = int(os.environ.get("FTP_PORT", "2121"))

# ANSI Color Codes
RESET = "\033[0m"
BOLD = "\033[1m"
DIM = "\033[2m"
RED = "\033[31m"
GREEN = "\033[32m"
YELLOW = "\033[33m"
BLUE = "\033[34m"
MAGENTA = "\033[35m"
CYAN = "\033[36m"
WHITE = "\033[37m"
B_RED = "\033[1;31m"
B_GREEN = "\033[1;32m"
B_YELLOW = "\033[1;33m"
B_CYAN = "\033[1;36m"
B_MAGENTA = "\033[1;35m"


def colorize_log_line(line: str, use_color: bool = True) -> str:
    if not use_color:
        return line

    # Error lines
    if " [E] " in line or line.startswith("[E] ") or "ERROR" in line or "error:" in line:
        return f"{B_RED}{line}{RESET}"

    # Warning lines
    if " [W] " in line or line.startswith("[W] ") or "WARN" in line:
        return f"{B_YELLOW}{line}{RESET}"

    # Autoplay / Autotest lines
    if "AUTOPLAY" in line or "AUTOTEST" in line:
        return f"{B_GREEN}{line}{RESET}"

    # Stream metrics
    if " [I] stream:" in line:
        # Highlight key metrics inside stream line
        # e.g.: 10788 kbps (remb 40000); 0 rumble; decode 5.5/18.1 ms, draw 11.2/11.8 ms, 0 late; rtt 17 ms; 1920x1080; on screen 47.3/57.2 ms
        styled = line
        # Tag
        styled = styled.replace(" [I] stream:", f" {B_CYAN}[I] stream:{RESET}")
        # Bitrate
        styled = re.sub(r"(\b\d+\s+kbps\b)", rf"{B_GREEN}\1{RESET}", styled)
        # REMB
        styled = re.sub(r"(\(remb\s+\d+\))", rf"{CYAN}\1{RESET}", styled)
        # Latency / RTT
        styled = re.sub(r"(\brtt\s+\d+\s+ms\b)", rf"{B_YELLOW}\1{RESET}", styled)
        styled = re.sub(r"(\bon screen\s+[\d\./]+\s+ms\b)", rf"{B_YELLOW}\1{RESET}", styled)
        # Drops / Skips / Nacks
        styled = re.sub(r"(\b\d+\s+skipped\b)", rf"{YELLOW}\1{RESET}", styled)
        styled = re.sub(r"(\b\d+\s+dropped\b)", rf"{YELLOW}\1{RESET}", styled)
        styled = re.sub(r"(\b\d+\s+nacks\b)", rf"{YELLOW}\1{RESET}", styled)
        return styled

    # Audio metrics
    if " [I] audio" in line:
        styled = line.replace(" [I] audio out:", f" {B_MAGENTA}[I] audio out:{RESET}")
        styled = styled.replace(" [I] audio in:", f" {B_MAGENTA}[I] audio in:{RESET}")
        return styled

    # Standard info
    if " [I] " in line:
        return f"{WHITE}{line}{RESET}"

    return line


def connect_ftp(host: str, port: int, timeout: int = 5) -> FTP:
    ftp = FTP()
    ftp.connect(host, port, timeout=timeout)
    ftp.login()
    ftp.cwd("/data/psbox")
    return ftp


def stream_logs(
    host: str = DEFAULT_HOST,
    port: int = DEFAULT_PORT,
    lines: int = 50,
    follow: bool = False,
    filter_pattern: str | None = None,
    use_color: bool = True,
    poll_interval: float = 0.5,
):
    try:
        filter_re = re.compile(filter_pattern, re.IGNORECASE) if filter_pattern else None
    except re.error as e:
        print(f"{B_RED}Invalid filter regex pattern '{filter_pattern}': {e}{RESET}", file=sys.stderr)
        return 1

    try:
        ftp = connect_ftp(host, port)
    except Exception as e:
        print(f"{B_RED}Failed to connect to PS5 FTP ({host}:{port}): {e}{RESET}", file=sys.stderr)
        return 1

    try:
        try:
            file_size = ftp.size("xcloud.log")
        except Exception:
            file_size = None

        if file_size is None or file_size == 0:
            print(f"{YELLOW}xcloud.log is empty or not yet created on PS5.{RESET}")
            offset = 0
        else:
            # Read last ~64KB or full file to get initial lines
            fetch_bytes = min(file_size, max(65536, lines * 400))
            offset_init = file_size - fetch_bytes
            bio = io.BytesIO()
            ftp.retrbinary("RETR xcloud.log", bio.write, rest=offset_init)
            initial_text = bio.getvalue().decode("utf-8", errors="replace")
            initial_lines = initial_text.splitlines()

            # If we started at a nonzero offset, discard first partial line
            if offset_init > 0 and len(initial_lines) > 1:
                initial_lines = initial_lines[1:]

            display_lines = initial_lines[-lines:] if lines > 0 else initial_lines
            for line in display_lines:
                if filter_re and not filter_re.search(line):
                    continue
                print(colorize_log_line(line, use_color))
            sys.stdout.flush()

            offset = file_size

        if not follow:
            ftp.quit()
            return 0

        # Follow mode loop
        remainder = ""
        while True:
            time.sleep(poll_interval)
            try:
                try:
                    cur_size = ftp.size("xcloud.log")
                except Exception:
                    # Reconnect on drop
                    try:
                        ftp.close()
                    except Exception:
                        pass
                    ftp = connect_ftp(host, port)
                    cur_size = ftp.size("xcloud.log")

                if cur_size is None:
                    continue

                if cur_size < offset:
                    # Log was truncated/restarted
                    if use_color:
                        print(f"\n{B_YELLOW}--- Log rotated or restarted ---{RESET}\n")
                    else:
                        print("\n--- Log rotated or restarted ---\n")
                    offset = 0
                    remainder = ""

                if cur_size > offset:
                    bio = io.BytesIO()
                    ftp.retrbinary("RETR xcloud.log", bio.write, rest=offset)
                    offset = cur_size
                    chunk = remainder + bio.getvalue().decode("utf-8", errors="replace")
                    if "\n" in chunk:
                        parts = chunk.split("\n")
                        remainder = parts[-1]
                        for line in parts[:-1]:
                            line = line.rstrip("\r")
                            if filter_re and not filter_re.search(line):
                                continue
                            print(colorize_log_line(line, use_color))
                        sys.stdout.flush()
                    else:
                        remainder = chunk
            except KeyboardInterrupt:
                break
            except Exception as e:
                # Silently retry on transient FTP connection reset
                time.sleep(1)
                try:
                    ftp.close()
                except Exception:
                    pass
                try:
                    ftp = connect_ftp(host, port)
                except Exception:
                    pass

    except KeyboardInterrupt:
        pass
    finally:
        try:
            ftp.quit()
        except Exception:
            pass

    return 0


def main():
    parser = argparse.ArgumentParser(
        description="Stream live xCloud-PS5 logs in real time from PS5 over FTP."
    )
    parser.add_argument("--host", default=DEFAULT_HOST, help="PS5 IP (default: $PS5_HOST or 192.168.15.17)")
    parser.add_argument("--port", type=int, default=DEFAULT_PORT, help="FTP Port (default: $FTP_PORT or 2121)")
    parser.add_argument("-n", "--lines", type=int, default=50, help="Initial number of lines to display (default: 50)")
    parser.add_argument("-f", "--follow", "--tail", action="store_true", dest="follow", help="Follow new log lines in real time")
    parser.add_argument("--filter", dest="filter_pattern", default=None, help="Regex/substring filter for log lines")
    parser.add_argument("--no-color", action="store_true", help="Disable colored output")
    parser.add_argument("--poll-interval", type=float, default=0.5, help="Polling interval in seconds (default: 0.5)")

    args = parser.parse_args()
    use_color = sys.stdout.isatty() and not args.no_color
    rc = stream_logs(
        host=args.host,
        port=args.port,
        lines=args.lines,
        follow=args.follow,
        filter_pattern=args.filter_pattern,
        use_color=use_color,
        poll_interval=args.poll_interval,
    )
    sys.exit(rc)


if __name__ == "__main__":
    main()
