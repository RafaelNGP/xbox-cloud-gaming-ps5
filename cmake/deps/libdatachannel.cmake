# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 RafaelNGP
# libdatachannel (WebRTC: ICE via libjuice, DTLS-SRTP, SCTP data channels)
# built static on top of the vendored mbedTLS. Include after mbedtls.cmake.

set(USE_MBEDTLS ON CACHE BOOL "" FORCE)
set(NO_WEBSOCKET ON CACHE BOOL "" FORCE)
set(NO_EXAMPLES ON CACHE BOOL "" FORCE)
set(NO_TESTS ON CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(BUILD_WITH_WARNINGS OFF CACHE BOOL "" FORCE)

# cmake/modules/FindMbedTLS.cmake maps find_package(MbedTLS) to deps/mbedtls.
list(PREPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake/modules")
find_package(MbedTLS REQUIRED)

# PS4 needs a few edits to libdatachannel and libjuice (ps4/patches/README.md). They
# are kept in this repo and applied here, so a clean checkout builds the same thing.
if(XC_TARGET_PS4)
    include(${CMAKE_SOURCE_DIR}/cmake/apply_patch.cmake)
    xc_apply_patch(${CMAKE_SOURCE_DIR}/deps/libdatachannel/deps/libjuice
                   ${CMAKE_SOURCE_DIR}/ps4/patches/libjuice.patch)
    xc_apply_patch(${CMAKE_SOURCE_DIR}/deps/libdatachannel
                   ${CMAKE_SOURCE_DIR}/ps4/patches/libdatachannel.patch)
endif()

add_subdirectory(deps/libdatachannel ${CMAKE_BINARY_DIR}/libdatachannel EXCLUDE_FROM_ALL)
