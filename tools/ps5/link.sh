#!/usr/bin/env bash
# Link xCloud-PS5 into a native PS5 eboot (FSELF)
set -euo pipefail

build=$(cd "${1:?usage: link.sh <build-dir>}" && pwd)
root=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
vk=${PS5_VULKAN:-$(cd "$root/../../WoW-PS5/deps/PS5_Vulkan" && pwd)}
sdk=$vk/.deps/native/ps5-payload-sdk
archive=${RADV_ARCHIVE:-$vk/.deps/native/radv-release/lib/libvulkan_radeon.ps5.a}
native=$vk/tooling/native
tool=$vk/build/host/ps5-native-tool
work=$build/ps5-link

mkdir -p "$work/obj" "$work/stubs"
cc() { PS5_PAYLOAD_SDK="$sdk" sh "$vk/tooling/prospero-clang18" "$@"; }

cc -std=c++20 -O2 -fno-exceptions -fno-rtti -ffunction-sections -fdata-sections \
    -c "$native/app_crt.cpp" -o "$work/obj/app_crt.o"
cc -std=c++20 -O2 -fno-exceptions -fno-rtti -ffunction-sections -fdata-sections \
    -c "$native/app_cpp_runtime.cpp" -o "$work/obj/app_cpp_runtime.o"

# System modules the SDK has no stub library for: link-time names only.
stubs=()
stub() {  # stub <library> <source>
    cc -std=c11 -O2 -fPIC -c "$2" -o "$work/obj/$1_stub.o"
    "$sdk/bin/prospero-lld" --shared -soname "$1.prx" -o "$work/stubs/$1.so" "$work/obj/$1_stub.o"
    stubs+=("$work/stubs/$1.so")
}
stub libSceAgc "$vk/vendor/ps5/sdk/stubs/agc_canary_link_stub.c"
stub libSceAgcDriver "$vk/vendor/ps5/sdk/stubs/agc_driver_canary_link_stub.c"
for source in "$root"/ps5/stubs/*.c; do
    name=$(basename "$source" .c)
    stub "$name" "$source"
done

# shellcheck source=/dev/null
source "$vk/tools/radv-link.sh"
radv_link_recipe "$vk" "$sdk" "$archive" || exit 2
# The recipe binds getaddrinfo/freeaddrinfo to the platform's ps5_* stubs,
# which always fail (EAI_FAIL). Drop those so src/platform/ps5/libc_compat.c
# (numeric parsing + sceNetResolver) provides them.
filtered=()
for flag in "${radv_link_flags[@]}"; do
    case $flag in
        --defsym=getaddrinfo=*|--defsym=freeaddrinfo=*) ;;
        *) filtered+=("$flag") ;;
    esac
done
radv_link_flags=("${filtered[@]}")

libs=("$build/libxcloud_ui.a"
      "$build/libxcloud_stream.a"
      "$build/libxcloud_media.a"
      "$build/libxcloud_core.a"
      "$build/ffmpeg/lib/libavcodec.a"
      "$build/ffmpeg/lib/libswresample.a"
      "$build/ffmpeg/lib/libavutil.a"
      "$build/libdatachannel/libdatachannel-static.a"
      "$build/libdatachannel/deps/libjuice/libjuice-static.a"
      "$build/libdatachannel/deps/libsrtp/libsrtp2.a"
      "$build/libdatachannel/deps/usrsctp/usrsctplib/libusrsctp.a"
      "$build/mbedtls/library/libmbedtls.a"
      "$build/mbedtls/library/libmbedx509.a"
      "$build/mbedtls/library/libmbedcrypto.a")

"$sdk/bin/prospero-lld" "${radv_linker_script[@]}" --eh-frame-hdr "${radv_link_flags[@]}" \
    --version-script "$native/app-symbols.map" --exclude-libs=ALL \
    -e _start -o "$work/llvm-pie.elf" "$root/ps5/xcloud-ps5.ld" \
    "$work/obj/app_crt.o" "$work/obj/app_cpp_runtime.o" \
    --whole-archive "$build/libxcloud_app.a" --no-whole-archive \
    --start-group "${libs[@]}" --end-group \
    "${stubs[@]}" "${radv_link_inputs[@]}" \
    --as-needed "$sdk"/target/lib/*.so

stub_args=()
for s in "${stubs[@]}"; do stub_args+=(--stub "$s"); done
"$tool" link --in "$work/llvm-pie.elf" --out "$work/eboot.elf" \
    --stub-dir "$sdk/target/lib" "${stub_args[@]}" \
    --module-sdk 0x02000009 --companion-sdk 0x08050001 --file-name eboot.elf
"$tool" self --sign --in "$work/eboot.elf" --out "$build/eboot.bin" --magic 0x1D3D154F
"$tool" self --inspect --file "$build/eboot.bin" > /dev/null
echo "eboot.bin built: $build/eboot.bin ($(stat -c %s "$build/eboot.bin") bytes)"
