# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 RafaelNGP
# libavcodec/libswresample/libavutil (H.264 + Opus decoders) from deps/ffmpeg, built by
# tools/build-ffmpeg.sh into <build>/ffmpeg; run here when missing.

set(XC_FFMPEG_DIR "${CMAKE_BINARY_DIR}/ffmpeg")
if(NOT EXISTS "${XC_FFMPEG_DIR}/lib/libswresample.a")
    if(XCLOUD_PS5)
        set(_target ps5)
    else()
        set(_target host)
    endif()
    message(STATUS "Building FFmpeg (${_target}), this takes a minute...")
    execute_process(
        COMMAND "${CMAKE_SOURCE_DIR}/tools/build-ffmpeg.sh" ${_target} "${CMAKE_BINARY_DIR}"
        RESULT_VARIABLE _rc)
    if(NOT _rc EQUAL 0)
        message(FATAL_ERROR "tools/build-ffmpeg.sh failed")
    endif()
endif()

add_library(xc_ffmpeg INTERFACE)
target_include_directories(xc_ffmpeg INTERFACE "${XC_FFMPEG_DIR}/include")
target_link_libraries(xc_ffmpeg INTERFACE "${XC_FFMPEG_DIR}/lib/libavcodec.a"
    "${XC_FFMPEG_DIR}/lib/libswresample.a" "${XC_FFMPEG_DIR}/lib/libavutil.a")
if(NOT XCLOUD_PS5)
    target_link_libraries(xc_ffmpeg INTERFACE m)
endif()
