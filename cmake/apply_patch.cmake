# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 RafaelNGP
# xc_apply_patch(<checkout dir> <patch file>): applies a patch kept in this repo to a
# submodule checkout, once. Already applied: nothing to do. Neither applied nor
# applicable (the submodule moved, or holds other edits): configure stops, so a
# release can never be built without the patch.
find_package(Git REQUIRED)

function(xc_apply_patch dir patch)
    execute_process(COMMAND "${GIT_EXECUTABLE}" apply --reverse --check "${patch}"
                    WORKING_DIRECTORY "${dir}" RESULT_VARIABLE applied
                    OUTPUT_QUIET ERROR_QUIET)
    if(applied EQUAL 0)
        return()
    endif()
    execute_process(COMMAND "${GIT_EXECUTABLE}" apply --whitespace=nowarn "${patch}"
                    WORKING_DIRECTORY "${dir}" RESULT_VARIABLE rc
                    ERROR_VARIABLE err)
    if(NOT rc EQUAL 0)
        message(FATAL_ERROR "cannot apply ${patch} to ${dir}:\n${err}\n"
                            "Reset that submodule (git -C ${dir} checkout .) and configure again.")
    endif()
    message(STATUS "Applied ${patch}")
endfunction()
