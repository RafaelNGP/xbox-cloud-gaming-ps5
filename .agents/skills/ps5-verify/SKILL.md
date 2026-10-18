---
name: ps5-verify
description: Comprehensive multi-platform verification (Desktop SDL3, Desktop No-SDL, PS5, unit tests, selftests, and platform boundaries).
---

# PS5 Multi-Platform Verification Skill

Use this skill before committing code, creating PRs, or deploying to ensure zero regressions across Desktop and PS5 platforms.

## Execution Procedure

Run the multi-platform verification suite:

```bash
python3 tools/ps5/ps5_dev.py verify
```

This verifies sequentially or in parallel:
1. `build-desktop` (SDL3 app, tests, and headless `--selftest`)
2. `build-desktop-nosdl` (headless desktop engine and tests)
3. `build-ps5` (PS5 Vulkan application and `link.sh` eboot.bin staging)
4. `check-platform-boundaries.sh` (ensuring clean platform isolation and no leaky SCE calls)
