# Roadmap

Where PSBox Cloud Gaming goes next, most valuable first. Each item says why it
matters, what "done" looks like, and how it is measured on the console.
Status: **next** (being worked on), **planned**, **later**, **dropped**.

Last update: 2026-10-08, after v0.5.0.

## Now

### 1. Lower input-to-picture latency — next
A frame is shown 13-48 ms after it arrives (samples in `xcloud.log`): the
swapchain holds up to two finished frames (3 images, FIFO) and the video queue
keeps frames when decoding falls behind. In fast games (Fortnite aiming) that
delay is what you feel.

- Present the newest frame as soon as it is drawn: fewer swapchain images or
  mailbox presentation, and drop stale frames instead of queueing them.
- Done when the median "shown after arrival" falls by ≥ 10 ms with no more
  late or skipped frames than today (log sample every 600 frames, autotest).

### 2. Hardware video decoding — next (probe first)
H.264 is decoded by FFmpeg on the CPU (~6 ms a frame at 1080p). The PS5's GPU
(Navi 21 class) has a video decoder that RADV exposes through Vulkan Video,
if the PS5 port enables it.

- Probe: does the device report `VK_KHR_video_decode_queue` /
  `VK_KHR_video_decode_h264` and a decode queue family? If not: dropped.
- If yes: decode into GPU images, feed them straight to the deband / FSR
  passes (no plane copies), CPU decoding kept as the fallback.
- Done when decoding takes ≤ 2 ms a frame and 1440p would fit the frame budget.

### 3. Reconnect after a network drop — next
When the connection drops, the stream ends and the app returns home, though
the cloud session stays alive on the server for a few minutes.

- Detect the drop (data channels closed / no RTP for N seconds), show
  "Reconnecting...", and connect to the same session again (new WebRTC
  negotiation) without restarting the game.
- Done when unplugging the network for ~10 s brings the same game back.

## Next

| Item | Why | Size |
| --- | --- | --- |
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
