// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
// psbox, the desktop app (SDL3): the app core (app_core.cpp) with the SDL
// display, input and sound backends and shell_desktop.cpp. SDL_main.h gives
// Windows its GUI entry point (M2); elsewhere this is main().
#include "app/app_core.h"
#include "app/desktop_selftest.h"
#include "display/display_sdl.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

namespace {

void usage(std::FILE* to) {
    std::fprintf(to,
                 "usage: psbox [--data-dir <dir>] [--fullscreen | --windowed] [--selftest <dir>]\n"
                 "  --data-dir <dir>   sign-in, settings and caches there (default $XCLOUD_DATA_DIR,\n"
                 "                     else $XDG_DATA_HOME/psbox)\n"
                 "  --fullscreen       start fullscreen (F11 or Alt+Enter switches)\n"
                 "  --windowed         start in a window (the default)\n"
                 "  --selftest <dir>   check the window, screens, input and sound; screenshots in <dir>\n");
}

}  // namespace

int main(int argc, char* argv[]) {
    std::string selftest;
    for (int i = 1; i < argc; ++i) {
        const char* a = argv[i];
        bool hasValue = i + 1 < argc;
        if (!std::strcmp(a, "--data-dir") && hasValue) {
            SDL_setenv_unsafe("XCLOUD_DATA_DIR", argv[++i], 1);  // platform::dataDir() reads it
        } else if (!std::strcmp(a, "--fullscreen")) {
            xc::display::sdlStartFullscreen(true);
        } else if (!std::strcmp(a, "--windowed")) {
            xc::display::sdlStartFullscreen(false);
        } else if (!std::strcmp(a, "--selftest") && hasValue) {
            selftest = argv[++i];
        } else if (!std::strcmp(a, "--help") || !std::strcmp(a, "-h")) {
            usage(stdout);
            return 0;
        } else {
            std::fprintf(stderr, "psbox: unknown argument '%s'\n", a);
            usage(stderr);
            return 2;
        }
    }
    int code = selftest.empty() ? xc::app::runApp() : xc::app::desktopSelftest(selftest);
    // Not a return from main: the worker's threads may still run, and the
    // static destructors would free what they use (as on the PS5).
    std::fflush(nullptr);
    std::_Exit(code);
}
