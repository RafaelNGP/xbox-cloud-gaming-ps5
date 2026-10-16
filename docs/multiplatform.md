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
| The app        | `xcloud_app` → `eboot.bin`                                 | `psbox-headless` (M0), SDL3 app (M1)          | SDL3 app (M2)          |
| Tools / tests  |                                                            | `xcloud-cli`, `xcloud-tests`                  | under wine (M2)        |

`cmake/platform.cmake` sets `XC_PLATFORM` and the `xc_platform` interface
target (linked by `xcloud_core`, so every target gets it):
`XC_PLATFORM_PS5`, `XC_PLATFORM_LINUX` or `XC_PLATFORM_WINDOWS`, and
`XC_PLATFORM_DESKTOP` on the last two. They are for the rare spot that can't
be split into backends; `XCLOUD_PS5` still works as before.

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
  desktop data folder (`~/.local/share/xcloud-ps5`, or `XCLOUD_DATA_DIR`) like
  `xcloud-cli`; Ctrl+C closes it.
- `xcloud-cli` and `xcloud-tests` stay SDL-free; the tests link the null backends.

## Backend map

| Area                  | Header                    | PS5                                   | Desktop (planned)          | Null                      |
|-----------------------|---------------------------|---------------------------------------|----------------------------|---------------------------|
| Display               | `display/display.h`       | `display_ps5.cpp` (+ `gpu.cpp`, RADV) | `display_sdl.cpp` (M1)     | `display_null.cpp`        |
| Controllers           | `input/controller.h`      | `controller_ps5.cpp`                  | `controller_sdl.cpp` (M1)  | `controller_null.cpp`     |
| USB keyboard / mouse  | `input/kbm.h`             | `kbm_ps5.cpp`                         | `kbm_sdl.cpp` (M1)         | `kbm_null.cpp`            |
| Audio out / in        | `media/audio_out.h`, `audio_in.h` | `audio_out.cpp`, `audio_in.cpp` (`#if`, legacy) | SDL audio (M1) | the same files without the PS5 parts (no device) |
| Hardware H.264        | `media/hw_decoder.h`      | `hw_decoder_ps5.cpp` (libSceVideodec2)| FFmpeg hwaccel (later)     | `hw_decoder_null.cpp`     |
| Platform layer        | `platform/platform.h`     | `platform/ps5/*` + `posix_common.cpp` | Linux: `platform/host/platform_host.cpp` + `posix_common.cpp`; Windows: win32 (M2) | |
| Shell                 | `app/shell.h`             | `shell_ps5.cpp`                       | desktop shell (M1)         | `shell_null.cpp`          |

Portable, shared by every backend: `controller_common.cpp`, `kbm_map.cpp`,
`key_map.cpp`, `keyboard_nav.cpp`, `gyro.cpp`, the UI (`src/ui`), decoding
(`media/decoder.cpp`), the protocol (`src/stream`, `src/xcloud`, `src/auth`).

The shell's hooks, in the order `app_core.cpp` calls them:
`beforeSettings()` (PS5: an update cut short rolled back, a store update's
files brought back), `beforeDisplay(settings)` (PS5: splash screen hidden,
the 120 Hz video-out setup), `iconFontPaths()`, `pumpEvents()` each pass
(false once the user closes the app; never on the PS5), `canSelfUpdate()`,
`isInstalledCopy(dir)` and `restartOrExit()` around an update.

## Capabilities: PS5-only features on other platforms

A feature only one platform has is asked about, not compiled out of the
shared code:

- `display::caps()` returns a `DisplayCaps`: `upscalers` (FSR 1 / Anime4K),
  `deband`, `sharpen`, `highRefreshSwitch` (120 Hz per stream),
  `presentTiming`. On the PS5 it is what the GPU path supports; the null
  display says false to all. The desktop UI hides the options it can't
  honour (M1); the calls themselves are always safe (no-ops).
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
- **M1, Linux app on SDL3**: a window; an `SDL_Renderer` display with a single
  render thread and a video mailbox; `controller_sdl` with the DualSense's
  effects, light bar, gyro and touchpad (and the `controller_common` split);
  `kbm_sdl` with relative mouse; SDL audio, with a portable AudioQueue
  splitting `audio_out.cpp` / `audio_in.cpp`; the XDG data folder; toasts for
  `platform::notify`; the UI hiding what the capabilities rule out.
- **M2, Windows**: cross build with llvm-mingw and the SDL3 mingw package,
  tests under wine; a Winsock socket layer; atomic writes with
  `MoveFileExW`; a crash guard on SEH (splitting `crash_guard.cpp`).
- **M3**: CI jobs per platform, packaging, notices for the new libraries.
- **Later**: a Vulkan desktop display reusing `gpu.cpp` (FSR, Anime4K),
  FFmpeg hardware decoding, a desktop updater.

## Rules for contributors

1. No `sce*` call, PS5 SDK header (`<ps5platform/...>`) or `#if` on
   `XCLOUD_PS5` / `XC_PLATFORM_PS5` / `__PROSPERO__` outside the PS5 backends.
   `tools/check-platform-boundaries.sh` enforces it (CI runs it) and lists the
   allowed files; the legacy ones (`audio_out.cpp`, `audio_in.cpp`,
   `crash_guard.cpp`) carry a TODO naming the milestone that splits them.
2. Something new that differs by platform: free functions in a portable
   header, one file per backend (`*_ps5.cpp`, `*_null.cpp`, later
   `*_sdl.cpp`), and the build lists which one each app links. A PS5-only
   feature gets a null backend that is a safe no-op, and a capability if the
   UI should hide it.
3. Keep the line `project(xcloud_ps5 VERSION X.Y.Z` as it is (scripts read it),
   and `XCLOUD_PS5` as the PS5 build's switch.
4. Before calling a change done, build and check every platform:
   ```bash
   ninja -C build-host && build-host/xcloud-tests     # desktop: psbox-headless, cli, tests
   ninja -C build-ps5 xcloud_app && tools/ps5/link.sh build-ps5
   bash tools/check-platform-boundaries.sh
   ```
   A development link saves its symbols in `symbols/vX.Y.Z-dev`; only
   `tools/release.sh` writes `symbols/vX.Y.Z`, the release's, which decode
   users' crash reports (`tools/symbolize.sh`). A change to the PS5's UI is
   still checked on the console with its screenshots (`AGENTS.md`).
