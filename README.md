# PSBox Cloud Gaming

A native Xbox Cloud Gaming (xCloud) client for jailbroken PS5 consoles. Sign in
with your Microsoft account, browse the cloud catalog in a UI modeled on
xbox.com/play, and stream games at 1080p60 with sound, using the DualSense as
an Xbox controller.

- Sign-in with a device code (or QR code) shown on the TV; no password is
  ever typed on the console.
- Home screen with the same rows as xbox.com/play: Jump back in, Recently
  added, Most popular on cloud, Leaving soon and All games.
- WebRTC streaming (libdatachannel on Mbed TLS) with H.264 and Opus decoded by
  FFmpeg on the CPU.
- English UI; text is centralized in `src/ui/strings.cpp` for translation.

Requires a homebrew-enabled PS5 that can run directory-style apps (for
example through ShadowMountPlus) and a subscription that includes cloud
gaming.

## Building

Clone with the submodules (Mbed TLS, libdatachannel, FFmpeg):

```bash
git clone --recursive https://github.com/RafaelNGP/xbox-cloud-gaming-ps5.git
cd xbox-cloud-gaming-ps5
```

Requirements: CMake 3.20+, Ninja, Clang/LLVM (clang, clang++, llvm-ar,
llvm-ranlib), Python 3 and curl. NASM is built automatically if missing.
FFmpeg is configured and built by `tools/build-ffmpeg.sh` the first time
CMake runs.

### Desktop build (Linux)

Used to develop and check the protocol and the UI on a PC.

```bash
cmake -S . -B build-host -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
ninja -C build-host
build-host/xcloud-tests                       # unit tests
build-host/xcloud-cli login                   # sign in
build-host/xcloud-cli stream BALATRO 30       # stream a title for 30 s
build-host/xcloud-cli ui-preview /tmp/ui      # render every screen to PNG
```

### PS5 build

The console build uses the toolchain of
[PS5_Vulkan](https://github.com/mihawk-99/PS5_Vulkan) (the PS5 payload SDK, the PS5 linker
recipe and `ps5-native-tool`). By default it is expected at
`../../WoW-PS5/deps/PS5_Vulkan`; set `PS5_VULKAN` to point elsewhere.

```bash
cmake -S . -B build-ps5 -G Ninja -DCMAKE_TOOLCHAIN_FILE=cmake/ps5.toolchain.cmake -DCMAKE_BUILD_TYPE=Release
ninja -C build-ps5 xcloud_app
tools/ps5/link.sh build-ps5          # -> build-ps5/eboot.bin
tools/ps5/package.sh build-ps5       # -> build-ps5/pkg/PPSA99810/
```

## Release

1. Build the PS5 app as above. `tools/ps5/package.sh` produces the complete
   app folder `build-ps5/pkg/PPSA99810/` (eboot, `sce_sys`, `sce_module`,
   assets and the license texts under `licenses/`). It contains no account
   data.
2. Tag the release and attach a zip of that folder:

   ```bash
   git tag v0.1.0 && git push origin v0.1.0
   (cd build-ps5/pkg && zip -r ../PSBox-Cloud-Gaming-v0.1.0.zip PPSA99810)
   ```

3. To install, copy the whole `PPSA99810` folder to `/data/homebrew/` on the
   console (or use `PS5_HOST=<console-ip> tools/ps5/deploy.sh`, which uploads
   it over FTP) and launch it from the home screen once the loader has
   registered it.

The binary is GPL-licensed, so every release must point to the source it was
built from: the tagged commit of this repository with its submodules.

## License

GPL-3.0-or-later; see `LICENSE`. The PS5 binary statically links the PS5 app
runtime and payload SDK platform layer, which are GPL-3.0-or-later, so the
project as distributed must be too. All other components are compatible
(Mbed TLS, libdatachannel, FFmpeg under the LGPL, stb, qrcodegen, the Inter
font, Mozilla's CA bundle); `THIRD_PARTY_NOTICES.md` lists each one with its
version and license.

This is an unofficial project, not affiliated with, endorsed or sponsored by
Microsoft or Sony. Xbox, Xbox Cloud Gaming and Game Pass are trademarks of the
Microsoft group of companies; PlayStation and PS5 are trademarks of Sony
Interactive Entertainment. Game names and artwork are loaded from Microsoft's
public catalog at runtime and are not distributed with this project.
Microsoft may change or block the services this client relies on at any time.
