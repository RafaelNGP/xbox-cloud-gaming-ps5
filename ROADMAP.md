# Roadmap

Where PSBox Cloud Gaming goes next, most valuable first. Each item says why it
matters, what "done" looks like, and how it is measured on the console.
Status: **next** (being worked on), **planned**, **later**, **dropped**.

Last update: 2026-10-14, v0.9.6.

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

## Next

| Item | Why | Size |
| --- | --- | --- |
| User data in `/download0` | The store's advice: user data outside the app folder survives any replacement of it (and an app on a USB drive). Console needed: is `/download0` writable under ShadowMountPlus, and where FTP sees it (logs, `autoplay.txt`, the tools). Then a one-time move with the old folder still read | small |
| Real 120 Hz output | Declaring it in `param.json` isn't enough: `sceVideoOutIsOutputSupported` / `sceVideoOutConfigureOutput(handle, 15, ...)` switch the output (ProsperoLight, PS5MC), and only with the console set to 1440p output (PS5_Vulkan 252bf912). Our `attribute3` VRR bit (0x80000) is refused on some firmwares: 0x40 (120 Hz) + 0x40000 (VRR). Console needed | small |
| VRR for the stream | `sceVideoOutVrrUnpegFromFixedRate` would absorb the 60 fps stream's jitter (ProsperoLight's "Paced + VRR"); needs an extended libSceVideoOut stub | medium |
| **next** Cloud playtime left | From November 2026 cloud play is capped each month (Ultimate 15 h, Premium 10 h, Essential 5 h). Source found (xbox.com/play): `GET contentaccess.exp.xboxservices.com/all/v1?market=..&offering=xgpuweb` with the `http://mp.microsoft.com/` XSTS (already used for the plan), protobuf answer: `remainingGameplayTimeInSeconds` / `maxGameplayTimeInSeconds` / monthly reset. A minimal protobuf reader, the hours left on Home and in the game menu, a warning near the end; and GSSV's `MonthlyLimitExceeded` (0x87DD043A) shown as such | small–medium |
| **next** Catalog by Game Pass plan | Rows and tabs show only what the account's plan plays; the search gets a **Plan** filter (My plan / Ultimate / Premium / Essential / All) to see what an upgrade or downgrade changes; other plans' games marked (badge, "Included with Game Pass Ultimate" on the page). Data already downloaded: catalog.gamepass.com v3 `PassMetadataByPassProductId` (full hydration): Essential `CFQ7TTC0K5DJ`, Premium `CFQ7TTC0P85B`, otherwise Ultimate (EA Play `CFQ7TTC0K5DH`, Ubisoft+ Classics, PC, day-one); Ultimate plays all, Premium plays Premium + Essential, Essential only Essential. The free-to-play list (87 games, BR) is open to every plan and to no plan, even the 34 also tagged with a plan (partly free: Halo Infinite, the Call of Duty launchers, Warzone; "Fortnite" tagged Ultimate next to the untagged "Fortnite Battle Royale"). Audit 2026-10-15, 894 cloud games: 126 Essential, 440 Premium, 328 Ultimate only. The plan itself comes from the store (v0.9.7). A non-Ultimate account is needed to check the default view | medium |
| Free play with ads | Microsoft's ad-supported offering (`xgpuwebf2p` login and sessions, its own list): play without a subscription, with the service's limits; the ads are never skipped (as green-nx, Nubix, XStreaming) | small–medium |
| Re-test 1440p and 1080p HQ | Microsoft offers 1440p to Ultimate since Nov 2025 (on consoles since Feb 2026); our "refused" measures are from v0.4/v0.5. If granted, the hardware decoder matters again | tiny |
| Game Pass trial badge (later) | The store's answer says `isTrial`; a TRIAL mark next to the plan. Not now | small |
| Lower audio latency, lost audio filled | The audio queue may reach 120 ms; Moonlight keeps 40–50 ms. Keep ~40 ms, fill lost packets with Opus' concealment / in-band FEC (offered already, never used) | small |
| Unreliable input channel (protocol v9) | The official client sends input on an unordered channel, so a lost packet doesn't hold the ones after it; needs a capture of its frames | medium |
| Controller the server stops hearing | After a lag spike the server may keep receiving the pad and ignore it (green-nx #61/#67): notice it and announce the pad again; don't resend an unchanged pad 125 times a second | small |
| Bitrate cap, stream volume | A maximum bitrate (`b=AS` in the SDP) for Wi-Fi; game volume and boost in the game menu (Better xCloud, Greenlight, XStreaming) | small |
| Network indicator and session summary | A small warning when the connection degrades, and a summary at the end (loss, delay, resolution), also in reports (Moonlight, GeForce NOW) | small |
| Rejoin a running session | After a crash or restart, a game still running in the cloud is joined again instead of a new session (`/v5/sessions/cloud/active`, XStreaming) | small |
| Rest mode | End the stream before the console sleeps; on resume refresh the sign-in, controllers and audio (`sceSystemServiceReceiveEvent`, ezRemote) | small |
| Button remapping, gyro aim, touchpad mouse | Per-game profiles: remapping (Edge paddles too), the DualSense gyro as the right stick (JoyShockMapper-style), the touchpad as a mouse once keyboard & mouse are in | medium |
| Favourites, queue time on cards, game language | A favourites row; the estimated queue time on a game's page before Play (`/v1/waittime/{titleId}`); the game's language apart from the app's | small |
| Home-screen tile per game | The PS5 Forwarder Format (ProsperoLight): a tile that starts one cloud game directly | small |
| Crash address in reports | The fault's offset in the code and a script that names the function (ProsperoLight / ProsperoEden), for the unexpected-close report | small |
| USB keyboard & mouse + badges (v1.0+) | Some xCloud games accept native keyboard and mouse input; deferred to v1.0+ | medium |
| Dedicated voice chat output routing | Route voice chat audio independently to DualSense controller speaker or headset while keeping game audio on TV (if xCloud exposes separate voice track) | medium |

## Dropped (and why)

- **More picture filters**: with FSR and deband, what remains is the bits the
  server doesn't send.
- **Higher tiers on this account**: 1080p HQ, 1440p, H.265, a Tizen-TV
  identity and `rateControlBitrateUpdate` were all refused by the service
  (measured, see the v0.4.0 / v0.5.0 PRs). The app still asks for HQ. To re-test: since
  Nov 2025 Microsoft offers 1440p to Ultimate (see Next).
- **HDR**: xCloud doesn't stream HDR.
- **1440p only for Ultimate**: not limited in the app; "Best" asks for the
  most and the service gives what the plan allows.
- **Surround, frame generation, FSR 4**: xCloud's audio is stereo as far as
  known; generated frames add delay; FSR 4 needs motion vectors and depth a
  video stream doesn't have.

## Done

- "All games" (Game Pass) as an expanded vertical grid: uncapped 550+ games in 6 centered columns, 2D D-pad navigation, smooth vertical row scrolling and clean clipping under header and tabs.
- v0.5.0: picture on the GPU, FSR 1 upscaling to 4K, block smoothing.
- v0.4.0: game menu, PS5 keyboard in games, frame metadata reports, CAS
  sharpness, trigger vibration, local multiplayer, controller settings.
- v0.3.0 and earlier: Game Pass / Your games tabs, search, prices, regions,
  RTCP feedback, rumble.
