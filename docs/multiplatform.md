# Multiplatform: PS5, Linux, Windows

PSBox is one app with three targets. The PS5 stays the primary one; Linux
and Windows run the same app core with their own backends. This page is
the map: what lives where, how a platform-only feature is handled, and the
plan to get there.

## Goals

- One app core (start-up, main loop, worker thread, streaming) for every
  platform; only the backends and a thin "shell" differ.
- Backends are picked **at link time**: a portable header of free functions,
  one source file per platform, the build lists which one an app links
  (the style of `src/platform/platform.h`). No virtual classes, no `#if` in
  the shared code.
- Every change keeps every platform building. A PS5-only feature never breaks
  the build or crashes elsewhere: there it is a no-op, or its option is
  hidden.
- The PS5 binary does what it did before each refactor.

## Platforms

|                | PS5                                                        | Linux                                         | Windows                |
|----------------|------------------------------------------------------------|-----------------------------------------------|------------------------|
| `XC_PLATFORM`  | `ps5` (`cmake/ps5.toolchain.cmake` sets `XCLOUD_PS5`)       | `linux`                                       | `windows` (M2)         |
| Build          | `build-ps5`: `ninja xcloud_app`, then `tools/ps5/link.sh`  | `build-host`: `ninja`                         | llvm-mingw cross (M2)  |
| The app        | `xcloud_app` → `eboot.bin`                                 | `psbox` (SDL3, M1), `psbox-headless`          | SDL3 app (M2)          |
| Tools / tests  |                                                            | `xcloud-cli`, `xcloud-tests`                  | under wine (M2)        |

`cmake/platform.cmake` sets `XC_PLATFORM` and the `xc_platform` interface
target (linked by `xcloud_core`, so every target gets it):
`XC_PLATFORM_PS5`, `XC_PLATFORM_LINUX` or `XC_PLATFORM_WINDOWS`, and
`XC_PLATFORM_DESKTOP` on the last two. The desktop ones are for the rare spot
that can't be split into backends; a test for the PS5 (`XC_PLATFORM_PS5`,
`XCLOUD_PS5`, "not desktop") stays in the PS5 backends. `XCLOUD_PS5` still
works as before.

## The app core

| File                                   | What                                                                     |
|----------------------------------------|--------------------------------------------------------------------------|
| `src/app/app_core.{h,cpp}`             | `coreStartup()`, `coreFrame()` (one pass of the main loop), `coreShutdown()`, `runApp()` |
| `src/app/app_state.{h,cpp}`            | the state the threads share (`g_ui`, `g_settings`, worker commands...)   |
| `worker.cpp`, `stream_screen.cpp`, `stream_player.cpp`, `price_loop.cpp`, `autoplay.cpp` | the worker thread, the streaming screen, the stream, prices, the unattended tests |
| `src/app/shell.h`                      | what only some platforms do at start-up and in the app's life            |
| `src/app/ps5_main.cpp`                 | the PS5's `main()`: `runApp()`, which never returns there                |
| `src/app/headless_main.cpp`            | `psbox-headless`'s `main()`                                              |

CMake builds the core once as the `xcloud_appcore` object library. Each app
links those objects with its backends:

- `xcloud_app` (PS5): core + PS5 backends + `shell_ps5.cpp` + `ps5_main.cpp`.
  `tools/ps5/link.sh` links `libxcloud_app.a` whole and a fixed list of other
  archives: PS5 code goes into `xcloud_app` (or a library already on that list).
- `psbox-headless` (desktop): core + `xcloud_backends_null` + `shell_null.cpp`
  + `headless_main.cpp`. No window, controller, sound or hardware decoder, no
  SDL: it proves the shared app builds and starts off the PS5. It uses the
  desktop data folder like `xcloud-cli`; Ctrl+C closes it.
- `psbox` (desktop, SDL3): core + `xcloud_backends_desktop` (`display_sdl.cpp`,
  `shell_desktop.cpp`, the self-test, `hw_decoder_null.cpp`) +
  `xcloud_backends_sdl_io` (controllers, keyboard / mouse, sound) +
  `desktop_main.cpp`. See "M1: the Linux app" below.
- `xcloud-cli` and `xcloud-tests` stay SDL-free; the tests link the null backends.

## Backend map

| Area                  | Header                    | PS5                                   | Desktop (planned)          | Null                      |
|-----------------------|---------------------------|---------------------------------------|----------------------------|---------------------------|
| Display               | `display/display.h`       | `display_ps5.cpp` (+ `gpu.cpp`, RADV) | `display_sdl.cpp`          | `display_null.cpp`        |
| Controllers           | `input/controller.h`, `input_ext.h` | `controller_ps5.cpp`        | `controller_sdl.cpp`, `sdl_input.cpp` | `controller_null.cpp` |
| USB keyboard / mouse  | `input/kbm.h`             | `kbm_ps5.cpp`                         | `kbm_sdl.cpp`              | `kbm_null.cpp`            |
| Audio out / in        | `media/audio_out.h`, `audio_in.h` | `audio_out_ps5.cpp`, `audio_in_ps5.cpp` | `audio_out_sdl.cpp`, `audio_in_sdl.cpp` | `audio_*_null.cpp` (no device) |
| Hardware H.264        | `media/hw_decoder.h`      | `hw_decoder_ps5.cpp` (libSceVideodec2)| FFmpeg hwaccel (later)     | `hw_decoder_null.cpp`     |
| Platform layer        | `platform/platform.h`     | `platform/ps5/*` + `posix_common.cpp` | Linux: `platform/linux/platform_linux.cpp` + `posix_common.cpp`; Windows: win32 (M2) | |
| Shell                 | `app/shell.h`             | `shell_ps5.cpp`                       | `shell_desktop.cpp`        | `shell_null.cpp`          |

Portable, shared by every backend: `controller_common.cpp`, `sdl_mapping.cpp`
(pure tables), `media/audio_queue.cpp`, `audio_in_common.cpp`, `kbm_map.cpp`,
`key_map.cpp`, `keyboard_nav.cpp`, `gyro.cpp`, the UI (`src/ui`), decoding
(`media/decoder.cpp`), the protocol (`src/stream`, `src/xcloud`, `src/auth`).

The shell's hooks, in the order `app_core.cpp` calls them:
`beforeSettings()` (PS5: an update cut short rolled back, a store update's
files brought back), `beforeDisplay(settings)` (PS5: splash screen hidden,
the 120 Hz video-out setup), `afterDisplay(haveDisplay)` (the desktop: its
window to the input backend), `iconFontPaths()`, `pumpEvents()` each pass
(false once the user closes the app; never on the PS5), `hasFocus()`,
`canSelfUpdate()`, `isInstalledCopy(dir)` and `restartOrExit()` around an
update, `beforeShutdown()` (desktop only). `caps()` (console / openUrl /
selfUpdate) words the screens; `openUrl(url)` opens the browser (false on the
PS5).

## Capabilities: PS5-only features on other platforms

A feature only one platform has is asked about, not compiled out of the
shared code:

- `display::caps()` returns a `DisplayCaps`: `upscalers` (FSR 1 / Anime4K),
  `deband`, `sharpen`, `highRefreshSwitch` (120 Hz per stream),
  `presentTiming`. On the PS5 it is what the GPU path supports; the null
  display says false to all. The desktop UI hides the options it can't
  honour; the calls themselves are always safe (no-ops).
- `input::padCaps(i)` / `anyPadCaps()` (`input_ext.h`): rumble, trigger
  rumble, adaptive triggers, light bar, gyro, touchpad. The PS5 always
  answers a DualSense with everything, so its UI never changes.
- `app::uiCaps()` gathers the three into `ui::UiCaps`; Settings, a game's
  settings and the in-game menu hide the rows it rules out
  (`ui::visibleSettingsRows()`, unit-tested: with the PS5's caps every row,
  in the same order and groups). A hidden row's setting is kept as it is.
- `shell::canSelfUpdate()`: only the PS5 installs updates. Elsewhere a newer
  release is noted in the log and never offered.
- Null backends are honest: no pad means `pollPad()` reports nothing
  connected, `HwDecoder::init()` fails (FFmpeg decodes), the settings a
  backend keeps (dead zones, confirm button) are still kept so their getters
  answer.

## Milestones

- **M0** (done): `cmake/platform.cmake`; the PS5 code of display, controller,
  kbm and the hardware decoder in `*_ps5.cpp`, null backends beside them;
  the GPU's switches behind `display::` and `display::caps()`; the app core
  and shell split; `psbox-headless`; `tools/check-platform-boundaries.sh`
  in CI; the microphone gain fix.
- **M1, Linux app on SDL3** (done, below): a window; an `SDL_Renderer` display with a single
  render thread and a video mailbox; `controller_sdl` with the DualSense's
  effects, light bar, gyro and touchpad (and the `controller_common` split);
  `kbm_sdl` with relative mouse; SDL audio, with a portable AudioQueue
  splitting `audio_out.cpp` / `audio_in.cpp`; the XDG data folder; toasts for
  `platform::notify`; the UI hiding what the capabilities rule out.
  The core gains three things with it: a `display::pump()` step in
  `coreFrame()`, on the menu and the streaming branch, so the video mailbox
  is drawn on the main thread (a no-op on the PS5 and null); a wait that
  wakes on a new frame or an input event in place of the fixed 8 ms sleep
  (the PS5 keeps the sleep); and an orderly quit: before `coreShutdown()`
  closes the backends, the worker leaves its loop (a quit command), the
  stream and the price loop stop and the xCloud session is closed, so no
  thread draws, rumbles or plays into a closed backend.
- **M2, Windows**: cross build with llvm-mingw and the SDL3 mingw package,
  tests under wine; a Winsock socket layer; atomic writes with
  `MoveFileExW`; a crash guard on SEH (splitting `crash_guard.cpp`).
- **M3**: CI jobs per platform, packaging, notices for the new libraries.
- **Later**: a Vulkan desktop display reusing `gpu.cpp` (FSR, Anime4K),
  FFmpeg hardware decoding, a desktop updater.

## M1: the Linux app

`psbox` is built when CMake finds SDL3 3.2 or newer (`XCLOUD_DESKTOP_APP`:
`AUTO` by default, `ON` to require it, `OFF` to never look; `cmake/deps/sdl3.cmake`).
`psbox-headless`, `xcloud-cli` and `xcloud-tests` never link SDL.

```bash
cmake -S . -B build-desktop -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo \
      -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++
ninja -C build-desktop
build-desktop/psbox                     # --fullscreen, --windowed (this run), --data-dir <dir>
SDL_VIDEO_DRIVER=offscreen SDL_AUDIO_DRIVER=dummy build-desktop/psbox --selftest /tmp/psbox-selftest
```

- **Display** (`display_sdl.cpp`): fullscreen at the desktop's own mode by
  default, else a 1600x900 resizable window: Settings > Display mode (a row
  only a desktop shows, `UiCaps::desktopWindow`; `desktopFullscreen` in
  settings.json), F11 / Alt+Enter switch it and the setting follows,
  `--fullscreen` / `--windowed` choose for one run. An `SDL_Renderer` with vsync, the 1920x1080 picture
  letterboxed. SDL renders on the main thread only: the drawing threads fill a
  back frame, `present()` puts it in a one-slot mailbox (the newest wins; the
  video thread never waits for vsync) and `display::pump()`, each pass of the
  main loop, uploads it (YUV into a BT.709 limited IYUV texture; the menu as
  ABGR8888), blends the overlay and presents. `idleWait()` wakes on a new
  frame. `readBackRgb()` works on the main thread only. No upscalers,
  sharpening, block smoothing or 120 Hz (`caps()`).
- **Shell** (`shell_desktop.cpp`): SDL starts before the display, every event
  goes to `input::sdlInputEvent()`, focus lost lets the mouse go, closing the
  window or Ctrl+C / SIGTERM closes the app in order (the worker leaves its
  loop, a stream and its xCloud session stop, then the backends close, then
  `_Exit`). Notices are toasts in the window; links (sign-in page, report
  form, the release page from Settings > Updates) open in the browser; the
  app never updates itself.
- **Input / sound** (`controller_sdl.cpp`, `kbm_sdl.cpp`, `audio_*_sdl.cpp`):
  gamepads hot-plugged into the first free slot, DualSense trigger effects,
  light bar, gyro and touchpad where SDL reaches them (a PlayStation pad
  opened on SDL's evdev driver, as can happen when it is plugged in after
  the start, is moved to its HIDAPI instance on `/dev/hidrawN`, SDL's
  gamepads started again once if needed; one pad never takes two slots);
  a pad's touchpad moving the desktop's pointer is not the mouse; the mouse is held
  (relative mode) only while a game has the keyboard and mouse and its menu
  is closed; Ctrl + 1-4 picks the keyboard's player (plain digits on the PS5).
- **Files**: the data folder (sign-in, settings, caches, `xcloud.log`) is
  `$XCLOUD_DATA_DIR`, else `$XDG_DATA_HOME/psbox` (`~/.local/share/psbox`;
  an old `~/.local/share/xcloud-ps5` is moved there once); `account.json` is
  readable by the user only. Assets (fonts, certificates) come from
  `$XCLOUD_ASSETS`, else `assets/` beside the executable (the build copies
  it there), else `./assets`.
- **Self-test** (`--selftest <dir>`): window and renderer, the input hooks
  (no pad: slot 0 empty), the sign-in and Settings screens with this
  machine's capabilities, colour bars through the mailbox alone and under
  the game menu (pixels checked), sound out (200 ms) and the microphone
  opened and closed; PNGs in `<dir>`, exit 1 on any failure. No account or
  network.

## Rules for contributors

1. No `sce*` call, PS5 SDK header (`<ps5platform/...>`), `XCLOUD_PS5` /
   `XC_PLATFORM_PS5` / `__PROSPERO__` / `__ORBIS__`, or `#if` on `__FreeBSD__`
   or on "not `XC_PLATFORM_DESKTOP`" outside the PS5 backends.
   `tools/check-platform-boundaries.sh` enforces it (CI runs it; comments
   don't count) and lists the allowed files; the legacy one
   (`crash_guard.cpp`) carries a TODO naming the milestone that splits it.
2. Something new that differs by platform: free functions in a portable
   header, one file per backend (`*_ps5.cpp`, `*_null.cpp`, later
   `*_sdl.cpp`), and the build lists which one each app links. A PS5-only
   feature gets a null backend that is a safe no-op, and a capability if the
   UI should hide it.
3. Keep the line `project(xcloud_ps5 VERSION X.Y.Z` as it is (scripts read it),
   and `XCLOUD_PS5` as the PS5 build's switch.
4. Before calling a change done, build and check every platform:
   ```bash
   ninja -C build-host && build-host/xcloud-tests     # desktop: psbox (with SDL3), psbox-headless, cli, tests
   ninja -C build-ps5 xcloud_app && tools/ps5/link.sh build-ps5
   bash tools/check-platform-boundaries.sh
   ```
   A development link saves its symbols in `symbols/vX.Y.Z-dev`; only
   `tools/release.sh` writes `symbols/vX.Y.Z`, the release's, which decode
   users' crash reports (`tools/symbolize.sh`). A change to the PS5's UI is
   still checked on the console with its screenshots (`AGENTS.md`).
