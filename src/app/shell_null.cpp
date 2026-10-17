// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
// app/shell.h for a desktop build without a window (psbox-headless): no
// splash screen or video mode to set up, no system icon font, no updating
// itself. Ctrl+C or SIGTERM closes the app.
#include "app/shell.h"

#include <atomic>
#include <csignal>
#include <cstdio>
#include <unistd.h>

namespace xc::app::shell {

namespace {
std::atomic<bool> g_quit{false};
void onQuit(int) { g_quit = true; }
}  // namespace

void beforeSettings() {}
void beforeDisplay(const Settings&) {}
bool afterDisplay(bool) { return true; }
std::vector<std::string> iconFontPaths() { return {}; }

bool pumpEvents() {
    static const bool hooked = [] {
        std::signal(SIGINT, onQuit);
        std::signal(SIGTERM, onQuit);
        return true;
    }();
    (void)hooked;
    return !g_quit;
}

bool hasFocus() { return true; }
bool canSelfUpdate() { return false; }
bool isInstalledCopy(const std::string&) { return false; }
void beforeShutdown() {}
Caps caps() { return {false, false, false}; }
void setFullscreen(bool) {}
bool fullscreenSwitched(bool&) { return false; }
bool openUrl(const std::string&) { return false; }
bool setTextInput(bool) { return false; }
std::string takeTextInput() { return {}; }

void restartOrExit() {
    // Never asked (no update is installed here); closed as the PS5 does.
    std::fflush(nullptr);
    _exit(0);
}

}  // namespace xc::app::shell
