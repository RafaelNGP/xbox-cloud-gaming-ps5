# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 RafaelNGP
cmake_minimum_required(VERSION 3.20)
project(xcloud_ps5 VERSION 1.5.4 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# XCLOUD_PS5 is set by cmake/ps5.toolchain.cmake.
option(XCLOUD_PS5 "Build for the PS5 (payload SDK)" OFF)
option(XC_TARGET_PS4 "Build for the PS4 (OpenOrbis)" OFF)
include(cmake/platform.cmake)

# --- Portable core: auth, HTTPS, xCloud protocol -----------------------------
# The CA bundle inside the binary too: used when assets/cacert.pem can't be read.
set(XC_BUILTIN_CA "${CMAKE_CURRENT_BINARY_DIR}/generated/builtin_ca.cpp")
add_custom_command(
    OUTPUT "${XC_BUILTIN_CA}"
    COMMAND ${CMAKE_COMMAND} -DIN=${CMAKE_CURRENT_SOURCE_DIR}/assets/cacert.pem -DOUT=${XC_BUILTIN_CA}
            -DNAME=xc_builtin_ca_bundle -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed_file.cmake
    DEPENDS assets/cacert.pem cmake/embed_file.cmake
    COMMENT "Embedding assets/cacert.pem")

add_library(xcloud_core STATIC
    ${XC_BUILTIN_CA}
    src/util/json.cpp
    src/util/log.cpp
    src/net/http.cpp
    src/auth/msa.cpp
    src/auth/xbox_live.cpp
    src/auth/auth_manager.cpp
    src/xcloud/gssv.cpp
    src/xcloud/catalog.cpp
    src/xcloud/titlehub.cpp
    src/xcloud/regions.cpp
    src/xcloud/prices.cpp
    src/xcloud/purchase.cpp
    src/input/controller_common.cpp
    src/input/kbm_map.cpp
    src/input/key_map.cpp
    src/input/pad_map.cpp
    src/input/keyboard_nav.cpp
    src/input/pad_usb.cpp
    src/input/sdl_mapping.cpp
    src/xcloud/social.cpp
    src/xcloud/achievements.cpp
    src/platform/toasts.cpp
    src/input/gyro.cpp
)
target_include_directories(xcloud_core PUBLIC src)
if(NOT MSVC)
    target_compile_options(xcloud_core PUBLIC -g1 -fno-omit-frame-pointer PRIVATE -Wall -Wextra -Wno-unused-parameter)
endif()

# mbedTLS and libdatachannel are built from deps/ for both targets.
include(cmake/deps/mbedtls.cmake)
target_link_libraries(xcloud_core PUBLIC xc_mbedtls xc_platform)
if(NOT XCLOUD_PS5 AND NOT XC_TARGET_PS4)
    find_package(Threads REQUIRED)
endif()
include(cmake/deps/libdatachannel.cmake)
if(XC_TARGET_PS4)
    target_compile_definitions(datachannel-static PUBLIC HAVE_SCONN_LEN HAVE_SA_LEN __Userspace__)
    target_compile_definitions(usrsctp PUBLIC HAVE_SCONN_LEN HAVE_SA_LEN __Userspace__)
endif()

# --- WebRTC streaming: SDP/ICE, data channels, input reports ----------------
add_library(xcloud_stream STATIC
    src/stream/h264_receiver.cpp
    src/stream/input_packet.cpp
    src/stream/rtcp_reporter.cpp
    src/stream/stream_session.cpp
)
target_link_libraries(xcloud_stream PUBLIC xcloud_core PRIVATE datachannel-static)
if(NOT MSVC)
    target_compile_options(xcloud_stream PRIVATE -Wall -Wextra -Wno-unused-parameter)
endif()

# --- Media: H.264 / Opus decoding -------------------------------------------
include(cmake/deps/ffmpeg.cmake)
# The audio queue and the microphone's framing are shared; the sound
# devices are backends (audio_*_ps5.cpp, audio_*_sdl.cpp, audio_*_null.cpp).
add_library(xcloud_media STATIC src/media/decoder.cpp src/media/audio_queue.cpp src/media/audio_in_common.cpp
                                src/media/audio_encoder.cpp)
target_link_libraries(xcloud_media PUBLIC xcloud_core PRIVATE xc_ffmpeg)

# --- Menus: software-rendered UI, Game Pass library ---------------------------
add_library(xcloud_ui STATIC
    src/ui/strings.cpp
    src/ui/canvas.cpp
    src/ui/font.cpp
    src/ui/image_cache.cpp
    src/ui/app_ui.cpp
    src/ui/app_ui_search.cpp
    src/ui/app_ui_settings.cpp
    src/ui/app_ui_input.cpp
    src/ui/app_ui_render.cpp
    src/ui/settings_choice.cpp
    src/ui/stream_menu.cpp
    src/ui/toast_view.cpp
    src/ui/pad_icons.cpp
    src/ui/pad_glyphs.cpp
    src/ui/accent_color.cpp
    src/ui/brand.cpp
    src/app/library.cpp
    src/app/library_sync.cpp
    src/app/settings.cpp
    src/app/update_check.cpp
    src/app/report.cpp
    src/app/crash_guard.cpp
    src/app/updater.cpp
    src/app/auto_deband.cpp
    src/app/web_server.cpp
    src/app/web_server_html.cpp
    src/app/web_server_gamepad.cpp
    extern/qrcodegen/qrcodegen.c
)
target_compile_definitions(xcloud_ui PUBLIC XC_APP_VERSION="${PROJECT_VERSION}")
target_include_directories(xcloud_ui PUBLIC extern/stb extern/qrcodegen)
target_link_libraries(xcloud_ui PUBLIC xcloud_core)

# --- The app: start-up, main loop, worker thread, streaming --------------------
# The same objects in every front end, each linking them with its platform's
# backends and shell (docs/multiplatform.md).
add_library(xcloud_appcore OBJECT
    src/app/app_state.cpp
    src/app/app_core.cpp
    src/app/worker.cpp
    src/app/worker_stream.cpp
    src/app/worker_diagnostics.cpp
    src/app/stream_screen.cpp
    src/app/price_loop.cpp
    src/app/autoplay.cpp
    src/app/stream_player.cpp
)
target_link_libraries(xcloud_appcore PUBLIC xcloud_stream xcloud_media xcloud_ui)
if(NOT MSVC)
    # Code every platform shares: its warnings show on every build and in CI.
    target_compile_options(xcloud_appcore PRIVATE -Wall -Wextra -Wno-unused-parameter)
endif()

# The commit each build came from, written to the log at start-up: a tester's log
# then says which package produced it.
set(XC_BUILD_ID_HEADER "${CMAKE_BINARY_DIR}/generated/build_id.h")
set(XC_BUILD_ID_ARGS -DSRC=${CMAKE_SOURCE_DIR} -DOUT=${XC_BUILD_ID_HEADER} -P ${CMAKE_SOURCE_DIR}/cmake/build_id.cmake)
execute_process(COMMAND ${CMAKE_COMMAND} ${XC_BUILD_ID_ARGS})
add_custom_target(xcloud_build_id COMMAND ${CMAKE_COMMAND} ${XC_BUILD_ID_ARGS}
                  BYPRODUCTS ${XC_BUILD_ID_HEADER} VERBATIM)
add_dependencies(xcloud_appcore xcloud_build_id)
target_include_directories(xcloud_appcore PRIVATE ${CMAKE_BINARY_DIR}/generated)

if(XCLOUD_PS5)
    target_sources(xcloud_core PRIVATE src/platform/ps5/platform_ps5.cpp src/platform/ps5/libc_compat.c src/platform/ps5/net_probe.cpp src/platform/ps5/keyboard_ps5.cpp)

    # The app itself, with the PS5's backends: linked into eboot.bin by
    # tools/ps5/link.sh.
    add_library(xcloud_app STATIC
        $<TARGET_OBJECTS:xcloud_appcore>
        src/app/ps5_main.cpp
        src/app/shell_ps5.cpp
        src/display/display_ps5.cpp
        src/display/gpu.cpp
        src/media/hw_decoder_ps5.cpp
        src/input/controller_ps5.cpp
        src/input/kbm_ps5.cpp
        src/media/audio_out_ps5.cpp
        src/media/audio_in_ps5.cpp
    )
    target_sources(xcloud_core PRIVATE src/platform/posix_common.cpp)
    target_include_directories(xcloud_app PRIVATE "${PS5_RADV}/include")
    # The USB keyboard / mouse backend kept the warnings it had in xcloud_core.
    set_source_files_properties(src/input/kbm_ps5.cpp PROPERTIES COMPILE_OPTIONS "-Wall;-Wextra;-Wno-unused-parameter")
    target_link_libraries(xcloud_app PUBLIC xcloud_stream xcloud_media xcloud_ui)
elseif(XC_TARGET_PS4)
    target_sources(xcloud_core PRIVATE
        src/platform/ps4/platform_ps4.cpp
        src/platform/ps4/keyboard_ps4.cpp
        src/platform/ps4/condition_variable_ps4.cpp
        src/platform/posix_common.cpp
    )

    add_executable(xcloud_ps4.elf
        $<TARGET_OBJECTS:xcloud_appcore>
        src/app/ps4_main.cpp
        src/app/shell_ps4.cpp
        src/display/display_ps4.cpp
        src/media/hw_decoder_ps4.cpp
        src/input/controller_ps4.cpp
        src/input/kbm_ps4.cpp
        src/media/audio_out_ps4.cpp
        src/media/audio_in_ps4.cpp
    )
    target_link_libraries(xcloud_ps4.elf PRIVATE
        xcloud_stream
        xcloud_media
        xcloud_ui
        xcloud_core
        xc_ffmpeg
        datachannel-static
        -lSceVideodec2
        -lSceVideoOut
        -lScePad
        -lSceAudioOut
        -lSceAudioIn
        -lSceKeyboard
        -lSceMouse
        -lSceNet
        -lSceNetCtl
        -lSceRandom
        -lSceImeDialog
        -lSceCommonDialog
        -lSceUserService
        -lSceSystemService
        -lSceSysmodule
        -lkernel
    )
else()
    target_link_libraries(xcloud_core PUBLIC Threads::Threads)
    if(WIN32 OR CMAKE_SYSTEM_NAME STREQUAL "Windows")
        target_link_libraries(xcloud_core PUBLIC ws2_32 crypt32 iphlpapi winmm secur32)
        target_sources(xcloud_core PRIVATE src/platform/windows/platform_windows.cpp)
    else()
        target_sources(xcloud_core PRIVATE src/platform/posix_common.cpp src/platform/linux/platform_linux.cpp)
    endif()

    # No screen, pads, USB keyboard or hardware decoder: what a desktop
    # build links in place of the PS5's backends (docs/multiplatform.md).
    add_library(xcloud_backends_null OBJECT
        src/display/display_null.cpp
        src/input/controller_null.cpp
        src/input/kbm_null.cpp
        src/media/hw_decoder_null.cpp
        src/media/audio_out_null.cpp
        src/media/audio_in_null.cpp
    )
    target_link_libraries(xcloud_backends_null PUBLIC xcloud_core)
    if(NOT MSVC)
        target_compile_options(xcloud_backends_null PRIVATE -Wall -Wextra -Wno-unused-parameter)
    endif()

    # The app with the null backends and no window: keeps the shared app
    # building and starting on the desktop (no SDL; the desktop app is next).
    add_executable(psbox-headless
        $<TARGET_OBJECTS:xcloud_appcore>
        src/app/headless_main.cpp
        src/app/shell_null.cpp
    )
    target_link_libraries(psbox-headless PRIVATE xcloud_backends_null xcloud_stream xcloud_media xcloud_ui xc_ffmpeg
                                                 datachannel-static)
    if(NOT MSVC)
        target_compile_options(psbox-headless PRIVATE -Wall -Wextra -Wno-unused-parameter)
    endif()

    add_executable(xcloud-cli src/app/cli_main.cpp)
    target_link_libraries(xcloud-cli PRIVATE xcloud_stream xcloud_media xcloud_ui xcloud_backends_null xc_ffmpeg)

    enable_testing()
    add_executable(xcloud-tests tests/test_main.cpp)
    target_link_libraries(xcloud-tests PRIVATE xcloud_stream xcloud_media xcloud_ui xcloud_backends_null xc_ffmpeg datachannel-static)
    add_test(NAME unit COMMAND xcloud-tests)
endif()

# --- The desktop app on SDL3 (psbox) ----------------------------------------
# cmake/deps/sdl3.cmake decides (XCLOUD_DESKTOP_APP); xcloud-cli, xcloud-tests
# and psbox-headless stay SDL-free either way.
if(NOT XCLOUD_PS5 AND NOT XC_TARGET_PS4)
    include(cmake/deps/sdl3.cmake)
endif()
if(XC_DESKTOP_APP)
    # The SDL backends of controller.h, kbm.h, input_ext.h, audio_out.h and
    # audio_in.h, in place of the null ones (controller_null, kbm_null,
    # audio_*_null: not both).
    add_library(xcloud_backends_sdl_io OBJECT
        src/input/sdl_input.cpp
        src/input/controller_sdl.cpp
        src/input/kbm_sdl.cpp
        src/media/audio_out_sdl.cpp
        src/media/audio_in_sdl.cpp
    )
    target_link_libraries(xcloud_backends_sdl_io PUBLIC xcloud_core xcloud_media SDL3::SDL3)

    # The window, its Vulkan / SDL_Renderer display and events, the self-test;
    # no hardware decoder yet (FFmpeg decodes).
    add_library(xcloud_backends_desktop OBJECT
        src/display/display_sdl.cpp
        src/display/gpu.cpp
        src/app/shell_desktop.cpp
        src/app/desktop_selftest.cpp
        src/media/hw_decoder_null.cpp
    )
    target_link_libraries(xcloud_backends_desktop PUBLIC xcloud_core xcloud_media xcloud_ui SDL3::SDL3)
    if(NOT MSVC)
        target_compile_options(xcloud_backends_sdl_io PRIVATE -Wall -Wextra -Wno-unused-parameter)
        target_compile_options(xcloud_backends_desktop PRIVATE -Wall -Wextra -Wno-unused-parameter)
    endif()

    # WIN32: a GUI app on Windows (SDL_main.h gives it its entry point).
    add_executable(psbox WIN32
        $<TARGET_OBJECTS:xcloud_appcore>
        src/app/desktop_main.cpp
    )
    target_link_libraries(psbox PRIVATE xcloud_backends_desktop xcloud_backends_sdl_io xcloud_stream xcloud_media
                                        xcloud_ui xc_ffmpeg datachannel-static SDL3::SDL3)
    if(NOT MSVC)
        target_compile_options(psbox PRIVATE -Wall -Wextra -Wno-unused-parameter)
    endif()
    # The fonts and certificates beside the executable (platform::assetDir()).
    add_custom_command(TARGET psbox POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/assets" "$<TARGET_FILE_DIR:psbox>/assets"
        COMMENT "Copying assets/ beside psbox")

    # install() rules and CPack: the Linux tarball (tools/desktop/package-linux.sh).
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        include(cmake/desktop_package.cmake)
    endif()
endif()
