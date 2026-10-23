# Roadmap

Where PSBox Cloud Gaming goes next, most valuable first. Each item says why it
matters, what "done" looks like, and how it is measured on the console.
Status: **next** (being worked on), **planned**, **later**, **dropped**.

Last update: 2026-10-23, v1.5.3.

## Now

### 1. Lower input-to-picture latency — done (v0.6.0)
Measured with `VK_KHR_present_wait`, from a frame's network arrival to the
moment it is on the TV: **~23 ms on average** at 60 Hz (decode ~6 ms, GPU
~3 ms, waiting for the next vblank ~8 ms on average). The earlier "13-48 ms"
stopped at `present()`, not at the screen.

- Each frame is submitted once the one before is on the screen (no queued
  frames: the wait for a free image fell from 1-3 ms to 0.01 ms), and a frame
  is skipped when a newer one is already waiting. Skipped frames in a 50 s
  run: 85 → 35.
- The game menu shows "On screen after" (that measurement, live).
- **Next step, 120 Hz output** (halves the vblank wait, ~4 ms): the title
  declares it with `attribute3 = 0x80040`; on our console the display still
  offered only 59.94 Hz. To check: the TV's 120 Hz support and the PS5's
  120 Hz output setting, then re-registering the app.

### 2. Hardware video decoding — works, parked
RADV has no Vulkan Video on the PS5, but the system's own decoder
(`libSceVideodec2`, sysmodule 207) works from the app: H.264 1080p decodes to
NV12 (pitch 2048, coded 1088 rows) in **~6 ms**, the same as FFmpeg on the
CPU, as BlackBearReloaded's research measured too. So no latency gain at
1080p; it only frees a CPU core.

- Kept in `src/media/hw_decoder.*` (autoplay `hwdecode` runs it alongside and
  logs it), with the PS5's own structure layouts and memory types.
- Becomes the way to go if 1440p / 4K or HEVC streams are ever granted.

### 3. Reconnect after a network drop — done (v0.6.0)
As the xbox.com client does: when the connection drops (a data channel or
WebRTC fails, or no video packet for 3 s), the stream tries again up to 20
times a second apart; each time it checks that the cloud session is still
`Provisioned` / `ReadyToConnect` and opens a new WebRTC connection on it.
The game goes on where it was. The server ending the session (idle kick,
the game closed) still ends the stream.

- Console test (autoplay `droptest`, the connection dropped at 20 s):
  reconnected on the first attempt, ~8 s of frozen picture, then 60 fps again.
- To check by hand: a real outage (cable out / Wi-Fi off for ~10 s).

### Also in v0.6.0
- **DualSense light bar** in the colour of the game in focus (Settings).
- **Anime4K x2** as an upscaler beside FSR (game menu: Upscaling): crisper
  outlines and text, but it also sharpens compression noise; ~1-2 ms more.
  Next to try: Anime4K's Restore network before the upscale.
- **Free-to-play games** (#10): xbox.com's free-to-play row; one not on the
  account yet shows FREE, with the store's QR code to get it, and Play.
  The server refuses a free game until it is got once (`NoEntitlement`);
  the app explains that and tries again. Got on the phone, it starts at once.
- **Windows outside the game** (Marvel Rivals' NetEase terms): touch input
  is announced on while the game is out of focus; the page then shows its
  own pad cursor. A touchpad pointer was tried and dropped (the page
  ignored the touches, and it got in the way of the account picker).

### Remote Play (own Xbox) — done (v0.7.0)
The xhome offering takes the same Xbox token: "My consoles" lists the
account's consoles (`/v6/servers/home`), and a home session is started
with the console's serverId; the stream is the cloud one. Series X on the
same network: 1080p, 9-17 Mbps, 7-10 ms round trip. A sleeping Xbox wakes
through the session itself (~10 s); one that doesn't answer
(`WaitingForServerToRegister`) is asked again, then the app says what to
check. Also in v0.7.0: the Xbox button (game menu, or a swipe up on the
touchpad), "End the stream", a new session after a drop, and notes when
the Xbox ends or turns off the stream.

- Resolution: what each kind of stream delivers is measured (1080p for
  both here; the browser gets 1080p from this Xbox too), and 1440p is only
  offered once delivered. The own Xbox is always asked for its top tier:
  ~15.7 Mbps instead of ~9.5.

### Picture clean-up — done (v0.8.0)
- **Anime4K Restore (S)** before the upscale, for FSR and Anime4K alike
  ("+ clean-up" in the game menu, the default with FSR): on Fortnite at
  ~5 Mbps, edges 50% stronger with the same noise in flat areas, and no
  measurable latency (on screen 26-29 ms either way).
- **Auto block smoothing**: high below ~5 Mbps, low up to ~10, off above,
  with 1 Mbps margins and 3 s before a change (the default).
- Settings from before v0.8.0 that still hold the old defaults move to
  the new ones, once.

### Network reliability — done (v0.8.0)
Mbed TLS built with its own locking (MBEDTLS_THREADING_C): concurrent
HTTPS requests no longer fail with -0x006E ("This is a bug in the
library"), and a GET with no answer is retried once. Host stress test
(`xcloud-cli tls-stress`): 960 parallel requests, 0 failures (before: 2
runs in 3 failed).

### Safer changes and releases — done (v0.9.0)
- CI (`.github/workflows/ci.yml`): the desktop build and unit tests on
  every push and pull request, and a scan of each pull request for tokens
  and private files.
- `tools/release.sh <X.Y.Z> [out] [--console]`: checks the version,
  contentVersion, tag and changes, builds and packages without the
  account, checks inside the zip and eboot.bin, optionally streams on the
  console, and prints the publish commands and the catalog record. It
  publishes nothing.

### `ps5_main.cpp` split — done (v0.9.0)
It had grown to 1,586 lines. Now: `ps5_main.cpp` (start-up and the home
screen's loop), `worker.cpp` (sign-in, library, consoles, playing),
`stream_screen.cpp` (the game's menu, gestures, controllers, overlay),
`price_loop.cpp`, `autoplay.cpp` (the unattended tests) and
`auto_deband.cpp`, whose "auto" block smoothing now has unit tests.

### In-app updates — done (v0.9.0)
After sign-in the app asks for the latest release and offers a newer one
(a pop-up with "Update now" / "Not now", and Settings > Updates). The zip
must be signed (ECDSA P-256) with the maintainer's key, which
`tools/release.sh` uses; a package that isn't that version or isn't newer
is refused. Files are swapped in place with a backup (an update cut short
is rolled back at the next start), then the app restarts itself
(`sceSystemServiceLoadExec`, from the main thread).

### Controls and search — done (v0.9.0)
- Controller icons as DualSense silhouettes; the light bar's colour from
  the game, a colour picker (live on the pad), or off; the confirm button
  shown as its symbol, in force from the next press.
- Trigger vibration tester: strength, frequency, resistance (the DualSense
  pushes back) and force pulses; the console takes trigger modes 0..3 only.
- Each stick's own dead zone, radial with rescaling, set in a stick tester.
- Search: Mode, Genre, Language (in the app's language) and Console lists;
  Lowest price by the discounted price. Modes and languages come from the
  store's full details, fetched once in the background and cached.

### Fast scrolling, inertia, fresher catalog and "All games" grid — done (v0.9.1)
- The right stick pages through the lists (3 rows up / down, 6 cards
  sideways), and so does a swipe on the touchpad, with smooth inertia.
- "All games" (Game Pass) expands into a 6-column vertical grid: all 550+
  titles accessible with 2D D-pad navigation, smooth vertical row scrolling,
  and clean viewport clipping under the headers and tabs.
- Cached game details older than 30 days are fetched again, 40 per start,
  oldest first.

### Per-game settings — done (v0.9.2)
- Custom profile per game (picture: sharpness, deband, upscaler, resolution;
  triggers: strength, Hz, resistance, pulses; dead zones: left, right; controls:
  circleConfirms).
- Overrides general settings for that game only; games without one seamlessly
  use global settings.
- Configurable directly from the game details screen (`Screen::Details` via Options button) and in-game from the stream overlay menu (`StreamMenu`).
- Interactive stick and trigger testers accessible directly within the modal.
- "Restaurar padrão" (Reset to default) option cleanly restores global settings.

### Publisher filter, non-game add-on exclusion and 120 Hz display declaration — done (v0.9.3)
- Search filter by Publisher: 14 major publishers and parent groups (Xbox/Bethesda, EA, Ubisoft, Square Enix, Warner Bros, 2K, Capcom, SEGA, Bandai Namco, THQ Nordic, Focus, Devolver, Annapurna, Team17) plus Indies & Others, fully localized in all 6 languages.
- Non-game add-ons and digital extras (artbooks, soundtracks, upgrade packs) filtered out from library, shelves, and search pools.
- PS5 120 Hz high frame rate output declaration (`attribute3 = 524352` / `0x80040`) enabled in `param.json` for supported HDMI 2.1 displays with graceful fallback to 59.94 Hz.

### Friends playing now, UI decluttering and Gamerscore precision — done (v0.9.4)
- "Amigos jogando agora" dynamic Home shelf powered by Xbox Live PeopleHub v2 API, querying online friends and mapped to store products.
- Strict entitlement verification: only games that the user has access to (`playable == true`) appear; if no friends are in accessible games, the row has zero visual footprint.
- Multiple friends playing the same game are grouped onto a single card with circular gamerpic avatars, gamertags and `+N` badge indicator.
- Presence pill badge positioned in the bottom-left corner of cards in the friends row; redundant "GAME PASS" badge removed across all cards.
- Solid uniform 360° Gamerscore circle rendering via `Canvas::strokeCircle(...)` and pixel-perfect centering of the `'G'` glyph.

### Voice chat & DualSense hardware microphone integration — done (v0.9.5)
- Native PS5 audio input capture (`libSceAudioIn`) bound to active user ID (`input::padUserId`) at 48 kHz mono S16.
- Physical DualSense hardware mute button integration (`sceAudioInGetSilentState`): orange LED hardware muting controls audio transmission and in-game UI state in real-time.
- Real-time in-game VU meter: rounded sound-level pill in the `StreamMenu` with perceptual scaling and studio ballistic decay (~350 ms falloff) displaying live green voice activity, matching orange mute status when muted on the controller.
- DualSense vibration suppression during mic capture (`scePadSetVibrationTriggerEffectWeakWhileEmbeddedMicInUse`) prevents motor rumble noise bleed.
- Native FFmpeg Opus voice encoder (48 kHz, 1 ch, 24 kbps, 20 ms frames) streamed via WebRTC audio packets to cloud session.
- Fixed audio output muting by locking to primary `SCE_AUDIO_OUT_PORT_TYPE_MAIN` (port 0).
- Cleaned up StreamMenu: removed obsolete manual microphone and audio routing toggles.

### Reports, built-in certificates, region and store compatibility — done (v0.9.6)
- Built-in certificates: when `assets/cacert.pem` can't be read (#20, #38), a copy inside `eboot.bin` is used (reproduced #38's -15872 on the console: signs in).
- "Report a problem": Triangle on error screens and in Settings shows a QR code that opens the GitHub bug form filled in (version, firmware, the error, the log's important lines), private data removed (gamertag, PS5 user names, console names, IPs, tokens).
- Unexpected-close detection: a crash (signal or watchdog) leaves a marker; the next start offers the report. Closing from the PS menu doesn't.
- "Sign in as another country" (opt-in): offered only when xCloud refuses the sign-in; `X-Forwarded-For` on the login request only; Settings > Sign-in country. The real refusal is still to be checked with an account from an unsupported country.
- ProsperoStore updates keep the sign-in and settings (brought back from the store's copy of the old folder, once); release zips as 0777.
- Privacy and robustness: console names and raw service answers out of the log; sign-out clears the last account's library; the updater keeps its backup when a rollback fails; cut-short HTTP answers, 5xx/429 at sign-in, endless title paging, failed prices and images handled; request pacing to Microsoft's services; corrupt `settings.json` kept aside.
- The firmware is read from the kernel (13.60, not 13.590.001).

### Sign-in that stays, the console's clock, plan on the profile — done (v0.9.7)
- The user's data (sign-in, settings, caches, log) lives in `/data/psbox`, outside the app's folder: uninstalling, reinstalling or updating from ProsperoStore keeps it; the first start moves it from older versions. A data folder that can't be written falls back and says so; a sign-in that can't be saved is told. The updater writes where the app runs (`appDir`).
- Refused certificates are explained: the console's clock (expired / not yet valid, with the console's date) or something on the network answering for Microsoft's sites; a notification when the clock breaks some content even though sign-in works (#45).
- Profile: a drawn gamerscore badge and the Game Pass plan (`GAME PASS [ULTIMATE]`) from the store service (`http://mp.microsoft.com/` XSTS, `entitlements/bulk`), "GAME PASS" from the titles otherwise.
- Search keyboard: up / down go to the item under the focused one.
- Issues: rule-based triage (labels and one comment for known problems).

### Stream robustness, 120 Hz, network warning, rest mode — done (v0.9.8)
- Video: no more freeze of seconds after a burst of loss (the resync stopped at a frame with its own hole); a key frame asked again while none comes. Teardown: libdatachannel's callbacks cleared before the session goes. Audio: an empty packet no longer silences the rest of the session. The decoder no longer drops a packet it refused as full; microphone open/close serialized; per-frame error logs throttled.
- Controller: an unchanged pad resent every 50 ms (not ~125/s); after the picture stalled 1.5 s the pads are announced again.
- "Best" asks for 1440p exactly as xbox.com does (device info, session body, `/streaming/title/constrain`, physical size). The service still sends 1080p (H.264 4.2) at ~11–13 Mbps in motion; Tizen's "1080HQ" gave the same (p90 12.9 Mbps both): Microsoft's cap, not the request.
- Jump back in: the account's own games too (bought, free-to-play), not only Game Pass.
- Game page: the estimated wait to start the game beside Play (`/v1/waittime/{titleId}`).
- 120 Hz output (VRR), off by default, in Settings and per game, only while a game streams (the menus stay at 60 Hz: OLED flicker). param.json `0x40 | 0x40000`; RADV patched (`tools/ps5/patches`): a VRR output's idle vblank isn't taken for 60 Hz (reported as mihawk-99/PS5_Mesa#4), and the output switches at a stream's start and end.
- Network: "Unstable connection" while frames are lost, video stalls or the round trip jumps; a session summary at the end of each game (notification, log, reports).
- Audio queue cut back to 40 ms past 60 ms (it held up to 120 ms); Settings: maximum bitrate (Auto / 20 / 15 / 10 / 5 Mbps) and game volume (25–150 %).
- Rest mode: a stream reconnects on its own when the console wakes (tested); the crash guard no longer takes the suspended app for a stall.

### Keyboard & mouse, key remapping, gyro aiming, crash addresses — done (v1.0.0)
- USB keyboard and mouse (libSceKeyboard, libSceMouse loaded at run time): native mouse and keyboard to the games the cloud lists as MKB (229 of ~2750); for the others, the keyboard and mouse as a player's controller (keys 1-4 choose the player; WASD, mouse as the right stick, the wheel as Triangle). F12 opens the game menu, Home is the Xbox button.
- The app's screens by keyboard (arrows / WASD, Enter, Esc, Tab, Q / E tabs...), the hints switching to key caps as soon as a key is used; the players' row shows the keyboard in its player's slot.
- Keyboard keys: their own screen (Settings and each game's settings): two keys or mouse buttons per input, reserved keys refused, back to the defaults.
- Gyro aiming (DualSense): off / always / while L2 aims, intensity 1-10, in the game menu, saved in general or per game; left / right is the turn about the room's vertical (gravity), so a tilted pad aims as far sideways as up / down.
- Cards: the console's keyboard-and-mouse icon on MKB games, its controller icon on the others; a Mouse & keyboard search filter; the players' pads drawn with the console's controller icon.
- Settings grouped (language and sign-in country; stream; sound; controller and keyboard; updates last); the game settings likewise.
- Sign-in: the xCloud login is tried again on a 5xx (six tries over ~25 s); the renewed refresh token is saved at once. The queue's wait from `/v1/waittime/{titleId}` while queued; the pictures' quantiser (QP) in the statistics line.
- Crash reports: a fatal signal records the faulting instruction's offset in eboot (`pc=eboot+0x...`), the accessed address (`addr=0x...`) and the caller return addresses (`bt=...`), shown on the report panel and in the GitHub issue link; `tools/release.sh` keeps `symbols/vX.Y.Z/eboot.elf` (a development link with `tools/ps5/link.sh`: `symbols/vX.Y.Z-dev`) and `tools/symbolize.sh` translates offsets back to function and `file:line`.

### Multiplatform: Linux app, shared core — done (v1.1.0)
- One app core for every platform (`docs/multiplatform.md`): the PS5 code in `*_ps5.cpp` backends, null backends for tools and tests, `tools/check-platform-boundaries.sh` in CI.
- PSBox on Linux (`psbox`, SDL3): window (fullscreen by default, Display mode in Settings), video and menus, DualSense (adaptive triggers, light bar, gyro, touchpad; hot-plug), Xbox and generic pads (Xbox glyphs), keyboard and mouse (held during a game), typing in the search, audio and microphone, the options the PC can't honour hidden, Circle / Esc asks before quitting.
- Linux tarball built and attached by CI on each release (ubuntu-24.04); signed on the release machine (`tools/desktop/sign-linux-release.sh`).
- PS5: microphone gain fixed (50 % muted it, 150 % gave 200 %); the autoplay tests put the user's settings back; development links keep their symbols apart from a release's.

### Plan filter, remapping, desktop Vulkan, 1-click F2P claim, smart triggers & low-latency pipeline — done (v1.2.0–v1.3.2)
- Catalog by Game Pass plan (Essential / Premium / Ultimate / All filter, plan badges, `PassMetadataByPassProductId` hydration) and a Favourites row at the top of the library (Triangle on the game page).
- Per-game stream language selector (`GameProfile::locale`), passed to `gssv.startSession(...)`.
- 1-click free-to-play acquisition (`claimFreeProduct`): claims free Store licenses directly from the game details page without a QR code or external browser, refreshing the library in place.
- Input & DualSense: full DualSense button remapping (including Edge back paddles), touchpad as mouse for MKB games, accurate `physicalPhysicality` bitmask, unreliable/unordered input channel (protocol v9), 250 Hz DualSense polling, Smart Trigger profiles (*Standard*, *Shooter*, *Racing*) and sub-bass Audio-to-Haptics.
- Audio & video: PCM waveform extrapolation PLC (`AudioDecoder::conceal`) with 12 ms RTP gap detection, desktop Vulkan display (`display/gpu.cpp` shared with SDL3 Linux for FSR 1, Anime4K, RCAS, deband), PS5 swapchain flip-rate unpegging (`wsi_videoout_set_flip_rate(0)`), and an in-game performance HUD with a 60-frame frametime sparkline (`L3 + R3`).
- Performance & resilience: dynamic Zen 2 thread pinning, zero-allocation RTP ring buffer, instant frame-1 boot, HTTP keep-alive connection pooling, out-of-order X.509 TLS chain sorting, NanoDNS / missing-Gamertag diagnostics, and client support for Microsoft's monthly cloud playtime cap (`fetchPlaytimeLimit`, waiting for Microsoft to enable the quota).

### Queue-ready chime, manual refresh, playtime, sign-in diagnostics, network test, achievements & wishlist — done (v1.3.3)
- **Queue-ready chime**: plays a pleasant chime (chunked 20 ms audio pulses via `AudioQueue`), triggers controller haptic vibration (`input::setRumble(160, 160, 400)`), and emits an in-app notice (`QueueReadyAlert`) when cloud queue waiting ends and the stream provisions.
- **Refresh the library by hand**: L3 button on the Home screen performs an in-place delta sync (`kReloadLibrary` / `Action::RefreshLibrary`), re-evaluating title list and entitlements without restarting the app.
- **Playtime per game on card & details page**: formats elapsed gameplay duration in hours and minutes, displaying a subtle badge on the card's top-left corner and a rounded pill with playtime on the game details page.
- **Sign-in diagnostics ("Check my sign-in")**: comprehensive XSTS error diagnostics for suspended/banned accounts (`2148916227`), missing profile (`2148916233`), unsupported regions (`2148916235`), adult/terms verification (`2148916236`/`2148916237`), and child family accounts (`2148916238`). Interactive 5-stage diagnostic tester modal in Settings (*Storage*, *Clock & TLS*, *Microsoft Account*, *Xbox Live & XSTS*, *xCloud & Plan*).
- **Network test in Settings**: probe modal measuring regional latency (RTT min/avg, jitter, packet loss %) across up to 8 regions, UDP/NAT STUN reachability, and bandwidth speed estimation with optimal region and bitrate recommendations.
- **Achievements on game page**: queries Xbox Live Achievements v2 contract (`achievements.xboxlive.com`), displays an official Gamerscore badge pill with unlocked count and score on Details, and provides a scrollable in-modal achievement browser (`L3` on Details) with checkmarks, descriptions, secret masks, and Gamerscore rewards.
- **Store wishlist**: toggle titles on Details with `R3`, persists to `settings.json`, renders a dedicated "Lista de desejos" / "Wishlist" shelf in Library immediately after Favorites, and polls background price/catalog loops to emit notifications when wishlist games go on sale or enter Game Pass.

### Web control, age ratings, AppImage package, bitrate slider & boot chime — done (v1.4.0)
- **Local Web UI (port 8080)**: embedded asynchronous micro HTTP server with right-side QR code in Settings, remote keyboard for TV search, interactive controller/keyboard remapping with live sync, and direct sanitized `xcloud.log` download.
- **Official age rating badges**: ClassInd (Brazil), ESRB (US), and PEGI (Europe) color-coded badges and content descriptors on the game details page.
- **Universal Linux AppImage**: standalone bundle (`tools/desktop/package-appimage.sh`) packaging SDL3 runtime, desktop integration, and full compatibility with Steam Deck / SteamOS.
- **Bitrate slider & DualSense speaker volume**: inline slider for bitrate caps (5 to 50 Mbps / Auto) and controller speaker volume slider (0% to 100% / Mute) in StreamMenu and Settings.
- **Direct free-to-play claim & boot sound**: in-console 1-click Store entitlement claim for free trials and F2P games; official boot chime (`ps5/sce_sys/snd0.at9`) on app launch.

## Next

| Priority | Item | Why | Size |
| :---: | --- | --- | --- |
| **1** | **Hardware video decoding (PS5 & other platforms)** (done) | Integrated `libSceVideodec2` (sysmodule 207) into the active streaming pipeline (`StreamPlayer`) on PS5 freeing a Zen 2 CPU core, reducing jitter/thermals, and mapping NV12 to Vulkan textures; VA-API / NVDEC via FFmpeg hwaccel enabled on desktop/Steam Deck | medium–large |
| **2** | **Expanded WebUI features & in-stream Game Menu** (done) | Expanded local HTTP server (port 8080): **Streaming Lock** (transparent overlay and HTTP 409 rejection blocking search/filters while playing), **Virtual Touch Gamepad** on mobile browsers (full Xbox buttons, D-pad, dual virtual analog sticks, triggers/bumpers, View/Menu/Nexus, P1 main & P2 local co-op hot-join), full general settings (bitrate slider, 120 Hz, dead zones, light bar, trigger vibration, queue alert), real-time in-stream `StreamMenu` control (CAS sharpness, FSR 1/Anime4K upscalers, deband, pad speaker volume, smart trigger profiles), and live session telemetry (FPS/RTT/loss) | medium |
| **2b** | **WebSocket RFC 6455 in WebUI** (done) | Full WebSocket handshake and binary/JSON framing on port 8080 (`/ws`): sub-5ms touch gamepad input latency with TCP_NODELAY, live 60 fps telemetry stream to mobile browsers, and seamless fallback to HTTP REST | medium |
| **3** | **Mouse navigation in desktop UI** (done) | On desktop/Steam Deck menus, mouse hover updates focus, left click triggers cards/buttons/tabs/options, mouse wheel scrolls shelves and the 550+ title vertical grid; strictly desktop-only (`XCLOUD_DESKTOP_APP`), inactive on PS5 | small–medium |
| **4a** | **Native Windows build** (done) | Cross-compilation support with `llvm-mingw` (or native MSVC/CMake), Windows platform backend (`src/platform/windows/`), SDL3/Vulkan backend, XInput & DualSense integration, standalone packaging script (`tools/desktop/package-windows.sh`) | medium |
| **4b** | **Native Android build** (done) | Android NDK (C++20) + Java/Kotlin wrapper scaffolding (`android/`), hardware video decoding via `AMediaCodec` (`src/media/hw_decoder_android.cpp`), and Bluetooth gamepad support | large |
| **5** | **Adaptive Voice Chat (DualSense mic & speaker with fallback)** (done) | Optional voice routing: when using DualSense, route incoming voice chat to controller speaker while game audio stays on TV; transparent fallback for generic Xbox controllers, USB/P2 headsets or PC to use standard system mic and main audio mix without blocking | medium |
| **5b** | **Multi-Controller USB Driver on PS5 (UHID)** (done) | Direct `/dev/uhid*` driver on jailbroken PS5 supporting 8BitDo Ultimate 2.4G (`0x2DC8:0x3106`), Xbox 360 wired/wireless receiver (`0x045E:0x028E`/`0x0719`), Xbox One/Series USB (`0x045E:0x02D1`/`0x02DD`/`0x0B12`), DualShock 4 USB (`0x054C:0x05C4`/`0x09CC`), and generic D-Input arcade fightsticks without requiring `libScePad` | medium |
| **5c** | **Advanced Audio-to-Haptics on DualSense** (done) | Real-time 120 Hz IIR biquad sub-bass filter on 48 kHz PCM stream with anti-denormal RMS envelope detector, translating explosions and rumble to DualSense voice-coil haptics; settings option (*Off*, *Low*, *Medium*, *High*) | small–medium |
| **5d** | **Offline Cache & Instant-Resume Library** (done) | Persistent `library_cache.json` in user data directory; instant 0.01s initial load of Home shelves and metadata before network sync, resilient against network dropouts with sign-out hygiene | small |
| **6** | Cloud playtime left (done in client; waiting for Microsoft) | From November 2026 cloud play is capped each month (Ultimate 15 h, Premium 10 h, Essential 5 h). Client support is ready (`AuthManager::fetchPlaytimeLimit`), waiting for Microsoft to enable quota | small–medium |
| **7** | Home-screen tile per game (later) | The PS5 Forwarder Format (ProsperoLight): generate standalone tiles on the PS5 home screen that directly launch a cloud game (deprioritized to end of roadmap) | small |

### Technical Feasibility Study: 8BitDo Ultimate 2.4G USB Dongle on PS5

**Context & Hardware Architecture**:
The 8BitDo Ultimate 2.4G wireless controller connects via its bundled 2.4 GHz USB RF adapter (`VID 0x2DC8`, `PID 0x3106`) or over Bluetooth. In standard operation, the 2.4G dongle acts as an X-Input or D-Input USB composite HID device.

**PlayStation 5 Subsystem & Security Analysis**:
1. **FreeBSD / Prospero Kernel Subsystem**:
   - The PS5 kernel (`sys/dev/usb`) detects and enumerates the 8BitDo 2.4G USB dongle as a generic USB device (`ugen` / `uhid`).
   - The kernel creates device nodes under `/dev/uhid*` and exposes raw USB transfer endpoints.
2. **Sony Userland (`libScePad`) Whitelist & Security Enforcement**:
   - Sony's userland input framework (`libScePad` and `libScePadCustom`) strictly limits pad handles to official Sony hardware: DualSense (`VID 0x054C`, `PID 0x0CE6`), DualSense Edge (`PID 0x0DF2`), and third-party controllers equipped with an authorized Sony cryptographic hardware coprocessor.
   - The console's security coprocessor performs periodic encrypted challenge-response authentication handshakes with the controller.
   - Unlicensed USB controllers and third-party 2.4G dongles fail this handshake or are ignored by `libScePad`, which returns invalid pad handles.
3. **Bluetooth Pairing Limitations**:
   - PS5 Bluetooth stack enforces Sony-specific EAP authentication handshakes and BD_ADDR filtering during device pairing.
   - Pairing generic Bluetooth gamepads or 8BitDo controllers in Bluetooth mode directly to the PS5 fails during discovery/handshake.

**Viable Implementation Paths**:
- **Path 1: WebUI Virtual Gamepad (Zero Extra Hardware, Zero Jailbreak Dependency) — *Implemented in v1.4.0***:
  - Users can pair the 8BitDo controller (via Bluetooth or USB OTG) to their smartphone, tablet, or laptop, open `http://<ps5-ip>:8080/`, and control the PS5 stream directly over Wi-Fi.
  - Supports Player 1 (main control) or Player 2 (seamless co-op hot-joining in active streams).
- **Path 2: Hardware Converter Adapters (Zero Code, 100% Native DualSense Emulation)**:
  - Hardware translation converters (such as Brook Wingman XE 2, Brook FGC, or Mayflash Magic-S Pro 2) handle the Sony hardware challenge-response authentication while interfacing with the 8BitDo 2.4G dongle.
  - The PS5 sees a fully authorized DualSense device with full input fidelity.
- **Path 3: Direct Userland USB HID Driver (`uhid` / `libusb`) on Jailbroken PS5 — *Planned (Priority 5b)***:
  - On jailbroken consoles (Prospero 3.xx–4.xx), homebrew userland can bypass `libScePad` entirely by opening `/dev/uhid*` directly or using `libusb`.
  - A lightweight driver module (`src/input/pad_usb.cpp`) will read raw HID reports from the 8BitDo dongle in D-Input mode (16-byte report: 2 analog sticks, 2 triggers, 14 digital buttons), map them into `input::ControllerState`, and merge them directly into `input::mergePads` alongside or in place of DualSense.

### Technical Feasibility Study: Multi-Phone WebUI Controllers (P1..P4)

**Context & Architecture**:
The embedded lightweight HTTP server (`src/app/web_server.cpp`) serves a zero-install touch gamepad (`/gamepad` or `/`) to local network clients (iOS Safari, Android Chrome, desktop browsers). Expanding this architecture to support concurrent multi-phone controllers allows up to 4 simultaneous players without requiring multiple physical DualSense or USB controllers.

**1. Session & Player Slot Management (P1..P4)**:
- **Slot Selection**: The WebUI client provides a player selector (`P1`, `P2`, `P3`, `P4`) or automatically claims the first free slot via query param `?player=N` or initial handshake (`GET /api/gamepad/claim`).
- **Slot State Matrix**: The host maintains an array of `VirtualGamepadState[kMaxPads]` (4 slots), protected by per-slot atomic timestamps and mutexes.
- **Heartbeat & Inactivity Eviction**: Each virtual pad maintains `lastSeenMs`. If a connected phone stops transmitting for > 15 seconds (browser minimized, incoming call, Wi-Fi sleep), the slot is automatically freed and the UI player indicator reflects disconnection.

**2. Network Transport & Latency Optimization**:
- **HTTP POST vs WebSocket**: Currently, gamepad states are sent via low-overhead JSON HTTP POST (`/api/gamepad`). At 60 Hz input polling, persistent HTTP/1.1 `keep-alive` connections achieve 2–6 ms latency on typical 5 GHz home Wi-Fi.
- **Binary Report Framing**: Upgrading input payload to a 12-byte binary format (`[slot:1, buttons:2, lt:1, rt:1, lx:2, ly:2, rx:2, ry:2]`) sent via WebSocket or HTTP chunks minimizes serialization overhead and JSON parsing on the PS5 CPU.
- **Input Concurrency**: Worker threads decode incoming packets and update `g_virtualGamepad[slot]`. The render/stream frame loop polls all slots concurrently with zero mutex contention using double-buffering or `std::atomic`.

**3. In-Game Local Co-op Compatibility**:
- xCloud supports local co-op titles (e.g. *It Takes Two*, *Overcooked: All You Can Eat*, *Halo: The Master Chief Collection*, *A Way Out*, *EA Sports FC*).
- PSBox merges virtual pads directly into Xbox WebRTC input channels (`ChannelType::Input`), ensuring cloud instances recognize separate controller indices seamlessly.

---

### PSBox Layout Studio (Virtual Controller Visual Customizer)

**Purpose & Architecture**:
A browser-based visual layout editor embedded directly into the WebUI under the **Mapeamento** (Remap) section, accessible via a dedicated sub-tab with a mobile phone icon and the label **"Celular"** (alongside "Controle" and "Teclado"). It eliminates hardcoded CSS guesswork and empowers users to visually customize and persist touch gamepad layouts directly from their mobile browser or desktop.

**Core Features & Integration**:
1. **Interactive Visual Canvas**:
   - WYSIWYG interactive canvas mirroring target device viewport resolutions (iPhone, Galaxy, iPad, landscape and portrait aspect ratios).
   - Real-time draggable, scalable, and rotatable UI widgets: Analog Sticks (LS/RS), D-Pad, ABXY cluster, Bumpers (LB/RB), Analog Triggers (LT/RT), System buttons (Nexus/Xbox, View, Menu), and Player selector bar.
2. **Dual-Orientation Custom Presets**:
   - Independent custom layout presets for Portrait and Landscape orientations (`portrait-custom`, `landscape-custom`), as well as built-in presets (`default-phone`, `compact-thumb`, `tablet-split`, `claw-grip`).
   - One-click "Restaurar padrão" (Reset to default) button to revert touch positions to factory ergonomics.
3. **Seamless JSON Layout Export & Live Persistence**:
   - Export and import layout profiles formatted in structured JSON (`{ portrait: { ... }, landscape: { ... } }`).
   - Saving from the Layout Studio instantly persists custom layout configurations to the client local storage and updates the active **Controle** (Gamepad) tab live without requiring a host restart or browser page reload.
   - Code generation mode exports formatted CSS coordinate rules (`left`, `top`, `width`, `height`, `transform: scale()`) ready for direct inclusion in `src/app/web_server.cpp`.
4. **Ergonomic Safe Zones, Snapping & Haptics**:
   - Visual safe-area guides (notch, Dynamic Island, navigation bar insets).
   - Configurable snap grids, thumb radius arcs, boundary clamping, and Web Vibration API haptic pulses for real-time touch simulation.


## Dropped (and why)

- **Discord Rich Presence (desktop)**: dropped (Xbox Live / Game Pass ecosystem already provides native activity presence).
- **First-run guide / Onboarding**: dropped per user decision (simple, intuitive navigation preferred without forced wizards).
- **Data used per session**: not worth a feature (decided 2026-10-18).
- **Saving the last 30 s as a clip, screenshots of the game**: dropped (decided 2026-10-18).
- **Several Xbox accounts at once / switching between them**: not offered.
- **"Leaving soon" / "new" badges on the cards**: not wanted on the cards.
- **Turning the Xbox on from the app**: not needed: a console in standby (ConnectedStandby) is already woken by the stream request (the app waits up to 60 s, `ConsoleDidNotWake` otherwise); one switched fully off can't be woken remotely.
- **GeForce Now**: another service with its own protocol, close to a new app, and a different terms-of-service risk.

- **More picture filters**: with FSR and deband, what remains is the bits the
  server doesn't send.
- **Higher tiers on this account**: 1080p HQ, 1440p, H.265, a Tizen-TV
  identity and `rateControlBitrateUpdate` were all refused by the service
  (measured, see the v0.4.0 / v0.5.0 PRs). Re-tested 2026-10-15 with the
  request exactly as xbox.com makes it: still 1920x1080 (H.264 4.2) at
  ~11–13 Mbps in motion, the same with Tizen's 1080HQ. "Best" keeps asking
  for 1440p for when Microsoft grants it. Measured by the pictures' QP
  (2026-10-15, FH5 / Starfield / Cyberpunk, Brazil and Australia regions): the
  service caps cloud 1080p at ~12–13 Mbps (QP ~24–27, up to ~33 in the
  heaviest motion). Tried, no gain: H.264 High (the session's
  `clientStreamingConfigOverrides` says `preferMainH264Profile`),
  transport-cc with real feedback (accepted, same bitrate), `b=AS` and
  `x-google-*-bitrate`, 4K-browser / Mac-Chrome (XboxTV's, which got 1440p in
  Australia) / Xbox-console identities (the console one got 720p),
  `VideoPreference` + `a=max-fr`, 1440 asked at once; the 5 s key frames cost
  ~2 % of the bits. 1440p seems to depend on the account / market.
- **HDR**: xCloud doesn't stream HDR.
- **1440p only for Ultimate**: not limited in the app; "Best" asks for the
  most and the service gives what the plan allows.
- **Rejoin a running session after a crash**: `/v5/sessions/cloud/active`
  answers 404 for cloud; the session can be reached again only with the
  previous streaming token saved on the console. Not done: a session left
  open after a crash could get in the way of the player's game, and the
  in-game reconnect already covers a drop (2026-10-15).
- **Surround, frame generation, FSR 4**: xCloud's audio is stereo as far as
  known; generated frames add delay; FSR 4 needs motion vectors and depth a
  video stream doesn't have.

## Done

- **v1.5.2**:
  - Universal store acquisition engine without loading spinner loop, immediate local persistence (`mine`/`owned_`), and recent-first sorting on boot.
  - Universal cross-gen deduplication (Xbox Series X|S prioritized over Xbox One) and removal of PC-only titles.
  - Strict compliance with Microsoft SIGL shelf feeds and enriched Call of Duty artwork/metadata.
  - Splash screen locks user inputs until authentication completes, preventing premature interactions.
  - Eliminated false "24 new games" notification toast and sanitized logs with duplicate suppression.
  - WebUI virtual controller ABXY buttons kept static and stable; search cursor focus stabilization; friends shelf hero art.
- **v1.5.1**:
  - Restored full "My Games" / owned games list (`owned_`), resolving regression where only Call of Duty and Fortnite appeared.
  - Eliminated duplicate Call of Duty entries in user library while keeping Modern Warfare 4 and Warzone playable.
  - Fixed manual library refresh (<kbd>L3</kbd>) ensuring full online synchronization and clear notification feedback.
  - Implemented automatic local cache self-healing against corrupted `library_cache.json`.
- **v1.5.0**:
  - Reorganized Settings menu with network/connectivity priority.
  - 1-Click direct free license acquisition on `Screen::Error` for unowned free games and launchers (Call of Duty BO6 Launcher, FFXIV Online Free Trial).
  - Instant delta-sync library update (<kbd>L3</kbd> and login) without cache flushing or UI freeze.
  - Search "Grátis" filter refactor excluding already owned games and unclaimable token products.
  - Red "Restaurar configurações padrões" button with 5-second hold confirmation.
  - WebUI 2.0 with fullscreen landscape and portrait virtual touch controller supporting Players 1-4 with dynamic console icon feedback.
  - Cleaned console notifications removing duplicate toasts.
- "All games" (Game Pass) as an expanded vertical grid: uncapped 550+ games in 6 centered columns, 2D D-pad navigation, smooth vertical row scrolling and clean clipping under header and tabs.
- v0.5.0: picture on the GPU, FSR 1 upscaling to 4K, block smoothing.
- v0.4.0: game menu, PS5 keyboard in games, frame metadata reports, CAS
  sharpness, trigger vibration, local multiplayer, controller settings.
- v0.3.0 and earlier: Game Pass / Your games tabs, search, prices, regions,
  RTCP feedback, rumble.
