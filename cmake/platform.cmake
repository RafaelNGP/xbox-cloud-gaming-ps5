# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 RafaelNGP
# The system the build is for (docs/multiplatform.md): XC_PLATFORM is ps5,
# linux or windows, decided by the toolchain (cmake/ps5.toolchain.cmake sets
# XCLOUD_PS5, which stays as it was). Targets that link xc_platform get
# XC_PLATFORM_PS5, XC_PLATFORM_LINUX or XC_PLATFORM_WINDOWS, and
# XC_PLATFORM_DESKTOP on the last two. Backends are picked by the sources an
# app links, not by these: they are for what can't be split that way.
if(XCLOUD_PS5)
    set(XC_PLATFORM ps5)
elseif(XC_TARGET_PS4)
    set(XC_PLATFORM ps4)
elseif(WIN32)
    set(XC_PLATFORM windows)
else()
    set(XC_PLATFORM linux)  # other Unix-likes build as Linux (untested)
endif()
message(STATUS "Platform: ${XC_PLATFORM}")

add_library(xc_platform INTERFACE)
string(TOUPPER "${XC_PLATFORM}" XC_PLATFORM_UPPER)
target_compile_definitions(xc_platform INTERFACE XC_PLATFORM_${XC_PLATFORM_UPPER}=1)
if(NOT XC_PLATFORM STREQUAL "ps5" AND NOT XC_PLATFORM STREQUAL "ps4")
    target_compile_definitions(xc_platform INTERFACE XC_PLATFORM_DESKTOP=1)
endif()
