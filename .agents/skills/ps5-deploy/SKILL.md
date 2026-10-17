---
name: ps5-deploy
description: Fast and safe packaging, FTP transfer, console hygiene, and foreground launch of xCloud-PS5 on the PS5 console.
---

# PS5 Deploy Skill

Use this skill when you need to deploy the latest build of xCloud-PS5 to the PlayStation 5 console safely and cleanly.

## Execution Procedure

Run the unified developer tool:

```bash
python3 tools/ps5/ps5_dev.py deploy
```

This command automatically:
1. Stops any running instance (`PPSA99810`) via `ps5_console.py kill`.
2. Packages the title folder with active account credentials (`XC_INCLUDE_ACCOUNT=1 bash tools/ps5/package.sh build-ps5`).
3. Sends the updated package to the console over FTP.
4. Ensures console hygiene by purging any lingering `autoplay.txt`.
5. Launches the title in foreground interactive mode for the user.
