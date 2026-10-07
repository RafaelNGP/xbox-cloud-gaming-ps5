cmake_minimum_required(VERSION 3.20)
project(xcloud_ps5 VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# XCLOUD_PS5 is set by cmake/ps5.toolchain.cmake.
option(XCLOUD_PS5 "Build for the PS5 (payload SDK)" OFF)

# --- Portable core: auth, HTTPS, xCloud protocol -----------------------------
add_library(xcloud_core STATIC
    src/util/json.cpp
    src/util/log.cpp
    src/net/http.cpp
    src/auth/msa.cpp
    src/auth/xbox_live.cpp
    src/auth/auth_manager.cpp
    src/xcloud/gssv.cpp
    src/platform/posix_common.cpp
)
target_include_directories(xcloud_core PUBLIC src)
if(NOT MSVC)
    target_compile_options(xcloud_core PRIVATE -Wall -Wextra -Wno-unused-parameter)
endif()

# mbedTLS and libdatachannel are built from deps/ for both targets.
include(cmake/deps/mbedtls.cmake)
target_link_libraries(xcloud_core PUBLIC xc_mbedtls)
if(NOT XCLOUD_PS5)
    find_package(Threads REQUIRED)
endif()
include(cmake/deps/libdatachannel.cmake)

# --- WebRTC streaming: SDP/ICE, data channels, input reports ----------------
add_library(xcloud_stream STATIC
    src/stream/input_packet.cpp
    src/stream/stream_session.cpp
)
target_link_libraries(xcloud_stream PUBLIC xcloud_core PRIVATE datachannel-static)
if(NOT MSVC)
    target_compile_options(xcloud_stream PRIVATE -Wall -Wextra -Wno-unused-parameter)
endif()

# --- Media: H.264 / Opus decoding -------------------------------------------
include(cmake/deps/ffmpeg.cmake)
add_library(xcloud_media STATIC src/media/decoder.cpp src/media/audio_out.cpp)
target_link_libraries(xcloud_media PUBLIC xcloud_core PRIVATE xc_ffmpeg)

if(XCLOUD_PS5)
    target_sources(xcloud_core PRIVATE src/platform/ps5/platform_ps5.cpp src/platform/ps5/libc_compat.c)

    # The app itself: linked into eboot.bin by tools/ps5/link.sh.
    add_library(xcloud_app STATIC
        src/app/ps5_main.cpp
        src/app/stream_player.cpp
        src/display/display.cpp
        src/input/controller.cpp
    )
    target_link_libraries(xcloud_app PUBLIC xcloud_stream xcloud_media)
else()
    target_link_libraries(xcloud_core PUBLIC Threads::Threads)
    target_sources(xcloud_core PRIVATE src/platform/host/platform_host.cpp)

    add_executable(xcloud-cli src/app/cli_main.cpp)
    target_link_libraries(xcloud-cli PRIVATE xcloud_stream xcloud_media xc_ffmpeg)

    enable_testing()
    add_executable(xcloud-tests tests/test_main.cpp)
    target_link_libraries(xcloud-tests PRIVATE xcloud_stream)
    add_test(NAME unit COMMAND xcloud-tests)
endif()
