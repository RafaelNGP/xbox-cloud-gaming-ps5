// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
// The PS5's entry point: the app (app_core.cpp) on the main thread, with the
// PS5 backends and shell_ps5.cpp. Never returns: the app is closed from the
// home screen.
#include "app/app_core.h"

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;
    return xc::app::runApp();
}
