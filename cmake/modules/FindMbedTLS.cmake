# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 RafaelNGP
# Resolves find_package(MbedTLS) inside deps/libdatachannel (and libSRTP) to
# the in-tree mbedTLS from cmake/deps/mbedtls.cmake instead of a system copy.
if(NOT TARGET xc_mbedtls)
    message(FATAL_ERROR "include cmake/deps/mbedtls.cmake first")
endif()
set(MbedTLS_FOUND TRUE)
set(MBEDTLS_FOUND TRUE)
set(MbedTLS_VERSION 3.6.2)
set(MBEDTLS_INCLUDE_DIRS "${CMAKE_SOURCE_DIR}/deps/mbedtls/include")
set(MBEDTLS_LIBRARIES mbedtls mbedx509 mbedcrypto)
foreach(c MbedTLS MbedX509 MbedCrypto)
    if(NOT TARGET MbedTLS::${c})
        add_library(MbedTLS::${c} INTERFACE IMPORTED GLOBAL)
    endif()
endforeach()
set_target_properties(MbedTLS::MbedTLS PROPERTIES INTERFACE_LINK_LIBRARIES "mbedtls;mbedx509;mbedcrypto")
set_target_properties(MbedTLS::MbedX509 PROPERTIES INTERFACE_LINK_LIBRARIES "mbedx509;mbedcrypto")
set_target_properties(MbedTLS::MbedCrypto PROPERTIES INTERFACE_LINK_LIBRARIES "mbedcrypto")
