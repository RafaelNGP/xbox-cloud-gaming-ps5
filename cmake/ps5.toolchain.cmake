# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 RafaelNGP
# CMake toolchain file for building xCloud-PS5 as a native PlayStation 5 app.

set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR x86_64)
set(CMAKE_CROSSCOMPILING TRUE)
set(XCLOUD_PS5 ON CACHE BOOL "Building for PlayStation 5" FORCE)

if(NOT DEFINED PS5_VULKAN)
    if(DEFINED ENV{PS5_VULKAN})
        set(PS5_VULKAN "$ENV{PS5_VULKAN}")
    else()
        get_filename_component(PS5_VULKAN "${CMAKE_CURRENT_LIST_DIR}/../../PS5_Vulkan" ABSOLUTE)
    endif()
endif()
set(PS5_VULKAN "${PS5_VULKAN}" CACHE PATH "PS5_Vulkan checkout")
set(PS5_SDK "${PS5_VULKAN}/.deps/native/ps5-payload-sdk" CACHE PATH "Pinned PS5 payload SDK")
set(PS5_RADV "${PS5_VULKAN}/.deps/native/radv-release" CACHE PATH "RADV release build")

if(NOT EXISTS "${PS5_SDK}/bin/prospero-lld" OR NOT EXISTS "${PS5_SDK}/target/include")
    message(FATAL_ERROR "PS5_SDK=${PS5_SDK} is not a built payload SDK")
endif()

find_program(PS5_CLANG   NAMES clang REQUIRED)
find_program(PS5_CLANGXX NAMES clang++ REQUIRED)
find_program(PS5_AR      NAMES llvm-ar ar REQUIRED)
find_program(PS5_RANLIB  NAMES llvm-ranlib ranlib REQUIRED)

set(CMAKE_C_COMPILER   "${PS5_CLANG}")
set(CMAKE_CXX_COMPILER "${PS5_CLANGXX}")
set(CMAKE_AR           "${PS5_AR}")
set(CMAKE_RANLIB       "${PS5_RANLIB}")
set(CMAKE_C_COMPILER_TARGET   x86_64-sie-ps5)
set(CMAKE_CXX_COMPILER_TARGET x86_64-sie-ps5)

set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

# Keep build-machine paths (assert/log file names) out of the binary.
get_filename_component(XC_SOURCE_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
set(XC_PATH_MAP "\"-ffile-prefix-map=${XC_SOURCE_ROOT}=.\" \"-ffile-prefix-map=$ENV{HOME}=~\"")

set(PS5_COMMON_FLAGS
    "${XC_PATH_MAP} -fPIC -ffunction-sections -fdata-sections -funwind-tables -fexceptions -march=znver2 -fvisibility-nodllstorageclass=default -fno-stack-protector -fno-plt -femulated-tls -fdenormal-fp-math=ieee -isysroot \"${PS5_SDK}\" -D_GNU_SOURCE -DXCLOUD_PS5=1 \"-include${CMAKE_CURRENT_LIST_DIR}/../ps5/compat/ps5_lfs.h\"")
set(CMAKE_C_FLAGS_INIT   "${PS5_COMMON_FLAGS} -isystem \"${PS5_SDK}/target/include\"")
set(CMAKE_CXX_FLAGS_INIT "${PS5_COMMON_FLAGS} -fcxx-exceptions -frtti -isystem \"${PS5_SDK}/target/include/c++/v1\" -isystem \"${PS5_SDK}/target/include\"")

set(BUILD_SHARED_LIBS OFF)
set(CMAKE_FIND_ROOT_PATH "${PS5_SDK}/target")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
