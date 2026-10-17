---
name: ps5-stream-test
description: Autonomous PS5 UI and stream test execution, automatic PPM to PNG conversion, visual verification, and network/audio log analysis.
---

# PS5 Stream & UI Autonomous Test Skill

Use this skill to execute automated headless tests on the real PS5 console without requiring manual controller input.

## Execution Procedure

Run the test suite with a specific target:

```bash
# Example 1: UI test (screens, details, settings)
python3 tools/ps5/ps5_dev.py test gamesettingstest --duration 20

# Example 2: Stream game test (e.g. Balatro or Fortnite)
python3 tools/ps5/ps5_dev.py test BALATRO --duration 45

# Example 3: Smoke crash test
python3 tools/ps5/ps5_dev.py test crashtest --duration 30
```

## What this handles automatically:
1. Deploys fresh code and configures `autoplay.txt`.
2. Launches the test remotely on the console.
3. Downloads all captured `.ppm` screenshots to `build-ps5/autotest/`.
4. Automatically converts all `.ppm` files to `.png` with PIL.
5. Deletes `autoplay.txt` immediately (mandatory console hygiene).
6. Downloads `xcloud.log`, parses FPS, bitrate, packet loss, NACKs, dropped frames, decoder timings, and audio underruns.
7. Displays a consolidated report and relaunches the app in interactive mode.
