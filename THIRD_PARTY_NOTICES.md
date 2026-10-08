# Third-party notices

PSBox Cloud Gaming is licensed under the GNU General Public License v3.0 or
later (see `LICENSE`). It is built from, and its PS5 binary (`eboot.bin`)
statically links, the components below. Each keeps its own license; their
full texts ship with their sources (the paths given here) and, for the
release package, under `licenses/` next to `eboot.bin`.

Under the GPL, LGPL and MPL, anyone who receives the binary is entitled to
the corresponding source: it is this repository at the release tag, with its
submodules (`git clone --recursive`), plus the PS5 toolchain inputs listed at
the end.

## Linked into `eboot.bin`

| Component | Version | License | Source / license text |
| --- | --- | --- | --- |
| Mbed TLS | 3.6.2 | Apache-2.0 (dual-licensed Apache-2.0 OR GPL-2.0-or-later) | `deps/mbedtls`, `deps/mbedtls/LICENSE` |
| libdatachannel | 0.23.1 | MPL-2.0 | `deps/libdatachannel`, `LICENSE` there |
| libjuice | b509b6c (libdatachannel submodule) | MPL-2.0 | `deps/libdatachannel/deps/libjuice` |
| usrsctp | fec583d (libdatachannel submodule) | BSD-3-Clause | `deps/libdatachannel/deps/usrsctp/LICENSE.md` |
| libSRTP | ee1a77c (libdatachannel submodule) | BSD-3-Clause | `deps/libdatachannel/deps/libsrtp/LICENSE` |
| plog | 94899e0 (libdatachannel submodule) | MIT | `deps/libdatachannel/deps/plog/LICENSE` |
| FFmpeg (libavcodec, libswresample, libavutil; H.264 and Opus decoders only) | n7.1.2 | LGPL-2.1-or-later (built without `--enable-gpl`/`--enable-nonfree`) | `deps/ffmpeg`, `deps/ffmpeg/COPYING.LGPLv2.1`; build flags in `tools/build-ffmpeg.sh` |
| stb_truetype, stb_image, stb_image_resize2 | upstream master (2026) | MIT or public domain (Unlicense), at your choice | `extern/stb`, `extern/stb/LICENSE` |
| QR Code generator library (C) | upstream master (2026) | MIT | `extern/qrcodegen` (license in the headers) |
| PS5 app runtime: `app_crt.cpp`, `app_cpp_runtime.cpp` (BlackBearReloaded, ps5-native-app-boilerplate) | from PS5_Vulkan | GPL-3.0-or-later | PS5_Vulkan `tooling/native/` |
| PS5 payload SDK platform layer (`libps5platform.a`) and headers | SDK v0.42 + PS5_Vulkan fork | GPL-3.0-or-later | <https://github.com/ps5-payload-dev/sdk> |
| LLVM libc++, libc++abi, libunwind, compiler-rt builtins (from the payload SDK) | SDK v0.42 | Apache-2.0 WITH LLVM-exception | the payload SDK |
| Mesa: RADV Vulkan driver with ACO and NIR, PS5 winsys (PS5_Mesa fork, built by PS5_Vulkan `tools/build-radv.sh`) | Mesa 26.2.0, PS5_Mesa 0b2d6d1 | MIT (individual files per their SPDX identifiers) | <https://github.com/mihawk-99/PS5_Mesa>, `docs/license.rst` and `licenses/` there |

## Shipped in the app package

| File | License |
| --- | --- |
| `sce_module/libc.prx` (generated runtime module, BlackBearReloaded) | GPL-3.0-or-later; see PS5_Vulkan `runtime/README.md` |
| `assets/fonts/Inter-*.ttf` (Inter 4.1, Rasmus Andersson) | SIL Open Font License 1.1 (`assets/fonts/OFL.txt`) |
| `assets/cacert.pem` (Mozilla CA certificate data, from <https://curl.se/ca/cacert.pem>, 2026-09-25) | MPL-2.0 |
| `sce_sys/icon0.png` and the in-app logo | Project artwork, GPL-3.0-or-later (`src/ui/brand.cpp`) |

## Used at build time only

| Tool | License |
| --- | --- |
| LLVM/Clang/lld | Apache-2.0 WITH LLVM-exception |
| NASM (assembles FFmpeg's x86 SIMD code) | BSD-2-Clause |
| PS5_Vulkan tooling (`link.sh` recipe, `ps5-native-tool` ELF/FSELF writer, FTP deploy script) | GPL-3.0-or-later |

## Protocol references

The xCloud protocol handling follows the open-source clients
[xbox-xcloud-player](https://github.com/unknownskl/xbox-xcloud-player) and
[Greenlight](https://github.com/unknownskl/greenlight) (MIT). No code from them
is included; they were read as documentation of the web client's behaviour.

## Trademarks

Xbox, Xbox Cloud Gaming and Game Pass are trademarks of the Microsoft group
of companies. PlayStation and PS5 are trademarks of Sony Interactive
Entertainment Inc. This project is not affiliated with, endorsed by or
sponsored by Microsoft or Sony. Game names and box art shown in the app are
loaded at runtime from Microsoft's public catalog and belong to their owners;
none are distributed with this project.

## AMD FidelityFX Super Resolution 1.0 (FSR 1)

`extern/fsr1/ffx_a.h` and `extern/fsr1/ffx_fsr1.h`, compiled into the shaders in
`src/display/shaders/` (EASU upscaling, RCAS sharpening). MIT License,
Copyright (c) 2021 Advanced Micro Devices, Inc.: see `extern/fsr1/LICENSE.txt`.
