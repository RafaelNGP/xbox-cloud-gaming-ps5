# Roadmap

Where PSBox Cloud Gaming goes next, most valuable first. Each item says why it
matters, what "done" looks like, and how it is measured on the console.
Status: **next** (being worked on), **planned**, **later**, **dropped**.

Last update: 2026-10-08, after v0.5.0 (items 1-3 and more on the way to v0.6.0).

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

## Next

| Item | Why | Size |
| --- | --- | --- |
| Anime4K Restore + Upscale | Cleans compression noise before Anime4K sharpens it | small |
| TLS errors (-110) | Seen at start (title list, prices) and on images, here and in #10 | small |
| Adaptive block smoothing | Stronger below ~4 Mbps, off above ~10 Mbps, automatically | small |
| Voice chat (microphone) | xCloud has a chat channel; needs PS5 audio capture | large |
| USB keyboard and mouse | Some xCloud games accept them | medium |
| Split `ps5_main.cpp` | 1,100+ lines mixing the stream screen, autoplay and settings | medium |
| CI on GitHub | Host build and unit tests on every PR | small |
| One-command release | Package, checksums, release and catalog record, with the secret scan | small |

## Dropped (and why)

- **More picture filters**: with FSR and deband, what remains is the bits the
  server doesn't send.
- **Higher tiers on this account**: 1080p HQ, 1440p, H.265, a Tizen-TV
  identity and `rateControlBitrateUpdate` were all refused by the service
  (measured, see the v0.4.0 / v0.5.0 PRs). The app still asks for HQ.
- **HDR**: xCloud doesn't stream HDR.

## Done

- v0.5.0: picture on the GPU, FSR 1 upscaling to 4K, block smoothing.
- v0.4.0: game menu, PS5 keyboard in games, frame metadata reports, CAS
  sharpness, trigger vibration, local multiplayer, controller settings.
- v0.3.0 and earlier: Game Pass / Your games tabs, search, prices, regions,
  RTCP feedback, rumble.
