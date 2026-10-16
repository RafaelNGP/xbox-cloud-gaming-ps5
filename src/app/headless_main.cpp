// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
// psbox-headless: the app (app_core.cpp) on a desktop with the null
// backends and shell_null.cpp: no window, controller, sound or hardware
// decoder. It keeps the shared app building and starting off the PS5 until
// the desktop app exists (docs/multiplatform.md). Ctrl+C closes it.
#include "app/app_core.h"

#include <cstdio>
#include <unistd.h>

int main() {
    int code = xc::app::runApp();
    // Not a return from main: the worker's threads still run, and the
    // static destructors would free what they use (as on the PS5).
    std::fflush(nullptr);
    _exit(code);
}
