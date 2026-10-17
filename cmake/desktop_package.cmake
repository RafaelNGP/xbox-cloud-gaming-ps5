# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 RafaelNGP
# The Linux package of the desktop app (psbox): install() rules and a CPack
# TGZ, PSBox-<XC_PACKAGE_VERSION>-linux-x86_64.tar.gz, holding one folder:
#
#   PSBox/psbox                  stripped, RUNPATH $ORIGIN/lib
#   PSBox/lib/libSDL3.so.0       the SDL3 it was built with
#   PSBox/assets/                fonts, certificates (the repository's assets/)
#   PSBox/licenses/              LICENSE, THIRD_PARTY_NOTICES.md, third-party texts
#   PSBox/psbox.desktop, psbox.png, README-linux.txt
#
# Only files named here go in: never a data folder (account.json, logs).
# The debug info stripped from psbox is kept in symbols/<XC_PACKAGE_VERSION>/
# psbox.debug (git-ignored), like the PS5's eboot.elf. tools/desktop/
# package-linux.sh configures, builds, packs and checks it.
set(XC_PACKAGE_VERSION "v${PROJECT_VERSION}-dev" CACHE STRING
    "Version in the desktop package's name (tools/release.sh sets vX.Y.Z)")

set(_pkg PSBox)
set(_src "${CMAKE_CURRENT_SOURCE_DIR}")

# The app and its RUNPATH: the bundled SDL3 first, LD_LIBRARY_PATH not needed.
set_target_properties(psbox PROPERTIES INSTALL_RPATH "\$ORIGIN/lib")
install(TARGETS psbox RUNTIME DESTINATION ${_pkg})

# Debug info to symbols/<version>/psbox.debug, then stripped from the copy
# being installed (the build tree's psbox keeps it).
set(XC_SYMBOLS_DIR "${_src}/symbols/${XC_PACKAGE_VERSION}")
if(CMAKE_OBJCOPY AND CMAKE_STRIP)
    install(CODE "
        set(_exe \"\$ENV{DESTDIR}\${CMAKE_INSTALL_PREFIX}/${_pkg}/psbox\")
        file(MAKE_DIRECTORY \"${XC_SYMBOLS_DIR}\")
        execute_process(COMMAND \"${CMAKE_OBJCOPY}\" --only-keep-debug \"\${_exe}\" \"${XC_SYMBOLS_DIR}/psbox.debug\"
                        RESULT_VARIABLE _r1)
        execute_process(COMMAND \"${CMAKE_STRIP}\" --strip-debug \"\${_exe}\" RESULT_VARIABLE _r2)
        execute_process(COMMAND \"${CMAKE_OBJCOPY}\" \"--add-gnu-debuglink=${XC_SYMBOLS_DIR}/psbox.debug\" \"\${_exe}\"
                        RESULT_VARIABLE _r3)
        if(_r1 OR _r2 OR _r3)
            message(FATAL_ERROR \"psbox: saving its symbols or stripping it failed\")
        endif()
        message(STATUS \"psbox symbols: ${XC_SYMBOLS_DIR}/psbox.debug\")
    ")
else()
    message(WARNING "objcopy/strip not found: the packaged psbox keeps its debug info")
endif()

# SDL3, bundled under the name psbox asks for (its SONAME).
if(TARGET SDL3::SDL3-shared)
    install(FILES "$<TARGET_FILE:SDL3::SDL3-shared>" DESTINATION ${_pkg}/lib RENAME libSDL3.so.0)
else()
    message(WARNING "SDL3 is not a shared library here: nothing to bundle in ${_pkg}/lib")
endif()

install(DIRECTORY "${_src}/assets/" DESTINATION ${_pkg}/assets)

# The icon, drawn by xcloud-cli like the PS5's icon0.png.
set(_icon "${CMAKE_CURRENT_BINARY_DIR}/psbox.png")
add_custom_command(OUTPUT "${_icon}"
    COMMAND xcloud-cli render-icon "${_icon}" 256
    WORKING_DIRECTORY "${_src}"
    DEPENDS xcloud-cli src/ui/brand.cpp
    COMMENT "Drawing psbox.png")
add_custom_target(psbox_icon ALL DEPENDS "${_icon}")

configure_file("${_src}/linux/README-linux.txt" "${CMAKE_CURRENT_BINARY_DIR}/README-linux.txt" @ONLY)
install(FILES "${_src}/linux/psbox.desktop" "${_icon}" "${CMAKE_CURRENT_BINARY_DIR}/README-linux.txt"
        DESTINATION ${_pkg})

# The licenses of what psbox links or the package carries (the PS5 runtime,
# Mesa, FSR and Anime4K are not in it).
install(FILES "${_src}/LICENSE" "${_src}/THIRD_PARTY_NOTICES.md" DESTINATION ${_pkg}/licenses)
install(FILES "${_src}/linux/FFmpeg-NOTE.txt" DESTINATION ${_pkg}/licenses)
set(_lic
    "assets/fonts/OFL.txt=Inter-OFL.txt"
    "deps/mbedtls/LICENSE=mbedtls-LICENSE.txt"
    "deps/libdatachannel/LICENSE=libdatachannel-MPL-2.0.txt"
    "deps/libdatachannel/deps/usrsctp/LICENSE.md=usrsctp-LICENSE.md"
    "deps/libdatachannel/deps/libsrtp/LICENSE=libsrtp-LICENSE.txt"
    "deps/libdatachannel/deps/plog/LICENSE=plog-LICENSE.txt"
    "deps/ffmpeg/COPYING.LGPLv2.1=FFmpeg-LGPL-2.1.txt"
    "extern/stb/LICENSE=stb-LICENSE.txt")
foreach(_pair IN LISTS _lic)
    string(REPLACE "=" ";" _pair "${_pair}")
    list(GET _pair 0 _from)
    list(GET _pair 1 _to)
    install(FILES "${_src}/${_from}" DESTINATION ${_pkg}/licenses RENAME "${_to}")
endforeach()
# SDL3's zlib license: beside its CMake package (a source or CI build), else
# the distribution's copy.
find_file(XC_SDL3_LICENSE NAMES LICENSE.txt
    HINTS "${SDL3_DIR}/../../../share/licenses/SDL3" "${SDL3_DIR}/../../share/licenses/SDL3"
          /usr/share/licenses/SDL3 /usr/share/doc/libsdl3 /usr/share/doc/SDL3
    NO_DEFAULT_PATH)
if(XC_SDL3_LICENSE)
    install(FILES "${XC_SDL3_LICENSE}" DESTINATION ${_pkg}/licenses RENAME SDL3-LICENSE.txt)
else()
    message(WARNING "SDL3's LICENSE.txt not found (XC_SDL3_LICENSE=<path>): the package will lack it")
endif()

set(CPACK_GENERATOR TGZ)
set(CPACK_PACKAGE_NAME PSBox)
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_FILE_NAME "PSBox-${XC_PACKAGE_VERSION}-linux-x86_64")
set(CPACK_INCLUDE_TOPLEVEL_DIRECTORY OFF)
set(CPACK_ARCHIVE_COMPONENT_INSTALL OFF)
set(CPACK_STRIP_FILES OFF)
include(CPack)
unset(_pkg)
unset(_src)
