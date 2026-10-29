# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 RafaelNGP
# CMake toolchain file for building xCloud-PS5 for PlayStation 4 with OpenOrbis

set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR x86_64)
set(CMAKE_CROSSCOMPILING TRUE)
set(XC_TARGET_PS4 ON CACHE BOOL "Building for PlayStation 4 (OpenOrbis)" FORCE)

if(NOT DEFINED OO_PS4_TOOLCHAIN)
    if(DEFINED ENV{OO_PS4_TOOLCHAIN})
        set(OO_PS4_TOOLCHAIN "$ENV{OO_PS4_TOOLCHAIN}")
    elseif(EXISTS "${CMAKE_CURRENT_LIST_DIR}/../ps4-toolchain/link.x")
        get_filename_component(OO_PS4_TOOLCHAIN "${CMAKE_CURRENT_LIST_DIR}/../ps4-toolchain" ABSOLUTE)
    else()
        message(FATAL_ERROR "OO_PS4_TOOLCHAIN is not set. Run tools/ps4/fetch-toolchain.sh first.")
    endif()
endif()
set(OO_PS4_TOOLCHAIN "${OO_PS4_TOOLCHAIN}" CACHE PATH "OpenOrbis PS4 Toolchain root")

if(NOT EXISTS "${OO_PS4_TOOLCHAIN}/link.x" OR NOT EXISTS "${OO_PS4_TOOLCHAIN}/lib/crt1.o")
    message(FATAL_ERROR "OO_PS4_TOOLCHAIN=${OO_PS4_TOOLCHAIN} does not look like an OpenOrbis toolchain (missing link.x or lib/crt1.o)")
endif()

# Fix upstream OpenOrbis ABI bug in alltypes.h (32-bit int instead of 64-bit pointer for pthread_*attr_t)
if(EXISTS "${OO_PS4_TOOLCHAIN}/include/bits/alltypes.h")
    file(READ "${OO_PS4_TOOLCHAIN}/include/bits/alltypes.h" _alltypes_content)
    if(_alltypes_content MATCHES "unsigned __attr; } pthread_mutexattr_t;")
        string(REPLACE "typedef struct { unsigned __attr; } pthread_mutexattr_t;" "typedef struct { void *__attr; } pthread_mutexattr_t;" _alltypes_content "${_alltypes_content}")
        string(REPLACE "typedef struct { unsigned __attr; } pthread_condattr_t;" "typedef struct { void *__attr; } pthread_condattr_t;" _alltypes_content "${_alltypes_content}")
        string(REPLACE "typedef struct { unsigned __attr; } pthread_barrierattr_t;" "typedef struct { void *__attr; } pthread_barrierattr_t;" _alltypes_content "${_alltypes_content}")
        string(REPLACE "typedef struct { unsigned __attr[2]; } pthread_rwlockattr_t;" "typedef struct { void *__attr; } pthread_rwlockattr_t;" _alltypes_content "${_alltypes_content}")
        file(WRITE "${OO_PS4_TOOLCHAIN}/include/bits/alltypes.h" "${_alltypes_content}")
    endif()
endif()

find_program(XC_PS4_CLANG   NAMES clang-18 clang REQUIRED)
find_program(XC_PS4_CLANGXX NAMES clang++-18 clang++ REQUIRED)
find_program(XC_PS4_LLD     NAMES ld.lld-18 ld.lld REQUIRED)
find_program(XC_PS4_AR      NAMES llvm-ar-18 llvm-ar ar REQUIRED)
find_program(XC_PS4_RANLIB  NAMES llvm-ranlib-18 llvm-ranlib ranlib REQUIRED)

set(CMAKE_C_COMPILER   "${XC_PS4_CLANG}")
set(CMAKE_CXX_COMPILER "${XC_PS4_CLANGXX}")
set(CMAKE_AR           "${XC_PS4_AR}")
set(CMAKE_RANLIB       "${XC_PS4_RANLIB}")
set(CMAKE_LINKER       "${XC_PS4_LLD}")

set(CMAKE_C_COMPILER_TARGET   x86_64-pc-freebsd12-elf)
set(CMAKE_CXX_COMPILER_TARGET x86_64-pc-freebsd12-elf)
set(CMAKE_SYSROOT "${OO_PS4_TOOLCHAIN}")

set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

set(XC_PS4_COMMON_FLAGS
    "-fPIC -ffunction-sections -fdata-sections -funwind-tables -fexceptions -msse4.2 -mpopcnt -mtune=btver2 -D_GNU_SOURCE -D__ORBIS__ -D__PS4__ -DXC_TARGET_PS4=1")
set(CMAKE_C_FLAGS_INIT   "${XC_PS4_COMMON_FLAGS} -isystem \"${CMAKE_CURRENT_LIST_DIR}/../ps4/compat\" -isystem \"${OO_PS4_TOOLCHAIN}/include\"")
set(CMAKE_CXX_FLAGS_INIT "${XC_PS4_COMMON_FLAGS} -fcxx-exceptions -isystem \"${CMAKE_CURRENT_LIST_DIR}/../ps4/compat\" -isystem \"${OO_PS4_TOOLCHAIN}/include/c++/v1\" -isystem \"${OO_PS4_TOOLCHAIN}/include\"")

set(CMAKE_EXE_LINKER_FLAGS_INIT "")
set(CMAKE_C_LINK_EXECUTABLE
    "${XC_PS4_LLD} -m elf_x86_64 -pie --script \"${OO_PS4_TOOLCHAIN}/link.x\" --eh-frame-hdr --error-limit=0 -L\"${OO_PS4_TOOLCHAIN}/lib\" <LINK_FLAGS> <OBJECTS> -o <TARGET> --start-group <LINK_LIBRARIES> --end-group -lc -lkernel \"${OO_PS4_TOOLCHAIN}/lib/crt1.o\"")
set(CMAKE_CXX_LINK_EXECUTABLE
    "${XC_PS4_LLD} -m elf_x86_64 -pie --script \"${OO_PS4_TOOLCHAIN}/link.x\" --eh-frame-hdr --error-limit=0 -L\"${OO_PS4_TOOLCHAIN}/lib\" <LINK_FLAGS> <OBJECTS> -o <TARGET> --start-group <LINK_LIBRARIES> --end-group -lc++ -lc -lkernel \"${OO_PS4_TOOLCHAIN}/lib/crt1.o\"")

set(BUILD_SHARED_LIBS OFF)
set(CMAKE_FIND_ROOT_PATH "${OO_PS4_TOOLCHAIN}")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
