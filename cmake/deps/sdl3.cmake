# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 RafaelNGP
# SDL3 for the desktop app (psbox): the window, renderer, pads, keyboard,
# mouse and sound. XCLOUD_DESKTOP_APP:
#   AUTO (default)  the app is built when SDL3 (3.2 or newer) is found
#   ON              SDL3 is required: configuring fails without it
#   OFF             SDL is never looked for (psbox-headless, xcloud-cli and
#                   xcloud-tests never need it either way)
# Sets XC_DESKTOP_APP and, when on, the SDL3::SDL3 target.
set(XCLOUD_DESKTOP_APP AUTO CACHE STRING "Build the SDL3 desktop app psbox: AUTO, ON or OFF")
set_property(CACHE XCLOUD_DESKTOP_APP PROPERTY STRINGS AUTO ON OFF)

string(TOUPPER "${XCLOUD_DESKTOP_APP}" _xc_desktop)
set(XC_DESKTOP_APP OFF)
if(XCLOUD_PS5 OR _xc_desktop MATCHES "^(OFF|0|NO|FALSE|N)$")
    message(STATUS "Desktop app (psbox): off")
elseif(_xc_desktop MATCHES "^(ON|1|YES|TRUE|Y)$")
    find_package(SDL3 3.2 CONFIG REQUIRED)
    set(XC_DESKTOP_APP ON)
else()
    find_package(SDL3 3.2 CONFIG QUIET)
    if(TARGET SDL3::SDL3)
        set(XC_DESKTOP_APP ON)
    else()
        message(STATUS "Desktop app (psbox): SDL3 not found, not built (XCLOUD_DESKTOP_APP=ON to require it)")
    endif()
endif()
if(XC_DESKTOP_APP)
    message(STATUS "Desktop app (psbox): SDL3 ${SDL3_VERSION}")
endif()
unset(_xc_desktop)
