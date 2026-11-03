// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
// How the PS4 build ends the process.
#pragma once

#include <unistd.h>

extern "C" int sceSystemServiceLoadExec(const char* path, const char* argv[]);

namespace xc::platform::ps4 {

// Leaves through the system, as XCloud4 does: libc's _exit path faults on this firmware
// (CE-34878-0 when closing). LoadExec("exit") ends the title; _exit only if it returns.
[[noreturn]] inline void exitTitle(int rc) {
    sceSystemServiceLoadExec("exit", nullptr);
    for (int i = 0; i < 100; ++i) usleep(100000);
    _exit(rc);
}

}  // namespace xc::platform::ps4
