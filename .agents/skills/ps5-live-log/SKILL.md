---
name: ps5-live-log
description: Stream and monitor real-time live console logs from the PS5 over FTP without restarting the title, with colorized metrics and pattern filtering.
---

# PS5 Live Log Streaming Skill

Use this skill to observe, follow, and analyze `xcloud.log` from the PlayStation 5 in real time over FTP without interrupting or restarting running game streams or UI sessions.

## Quick Usage

### 1. Unified Dev Tool
```bash
# Print last 60 lines with syntax highlighting
python3 tools/ps5/ps5_dev.py log -n 60

# Stream and follow live log in real time (like tail -f)
python3 tools/ps5/ps5_dev.py log -f

# Follow live log and filter only stream metrics
python3 tools/ps5/ps5_dev.py log -f --filter "stream:"

# Follow live log and filter audio metrics or warnings
python3 tools/ps5/ps5_dev.py log -f --filter "audio|WARN|ERR"
```

### 2. Standalone Streamer (`log_stream.py`)
```bash
# Direct execution with custom host/port
python3 tools/ps5/log_stream.py --host 192.168.15.17 --port 2121 -f

# Filter with regex
python3 tools/ps5/log_stream.py -f --filter "rtt \d+ ms"
```

## Key Capabilities & Features

1. **Zero Session Disturbance**:
   - Uses FTP REST offset retrieval (`retrbinary` with `rest=offset`).
   - Does NOT kill, restart, or lock `eboot.bin` or `/data/psbox/xcloud.log`.
   - Transfers only newly appended bytes on each poll.

2. **Automatic Metric Highlighting**:
   - `[I] stream:` lines highlighted in Cyan with bold bitrate (`kbps`), REMB caps, RTT latency (`rtt XX ms`), on-screen latency, and dropped/skipped counts.
   - `[I] audio out:` and `[I] audio in:` lines highlighted in Magenta.
   - `[W]` warnings highlighted in Yellow.
   - `[E]` errors highlighted in Bold Red.
   - `AUTOPLAY` / `AUTOTEST` events highlighted in Bold Green.

3. **Robust Reconnection**:
   - Handles transient FTP connection drops or timeouts automatically.
   - Detects log truncations / app restarts and resets the read offset cleanly.
