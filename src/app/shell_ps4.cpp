// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
#include "app/shell.h"

#include "app/autoplay.h"
#include "app/updater.h"
#include "display/display.h"
#include "platform/platform.h"
#include "ui/strings.h"
#include "util/log.h"

#include <cstdlib>
#include <sys/stat.h>
#include <unistd.h>

#include <csignal>

extern "C" int sceSystemServiceHideSplashScreen(void);

namespace {
volatile std::sig_atomic_t g_shutdownSignal = 0;

void onSignal(int sig) {
    g_shutdownSignal = sig;
}

void installSignalHandlers() {
    struct sigaction sa {};
    sa.sa_handler = onSignal;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGTERM, &sa, nullptr);
    sigaction(SIGINT, &sa, nullptr);
    sigaction(SIGHUP, &sa, nullptr);
    std::signal(SIGPIPE, SIG_IGN);
}
}  // namespace

namespace xc::app::shell {

void beforeSettings() {
    installSignalHandlers();
    if (recoverUpdate(platform::appDir())) platform::restartApp();
    importStoreLeftovers(platform::dataDir(), "/data/pkg/previous/PSBX00001");
}

void beforeDisplay(const Settings&) {
    sceSystemServiceHideSplashScreen();
}

bool afterDisplay(bool) { return true; }

std::vector<std::string> iconFontPaths() {
    return {"/system/common/font/PS4Icon.ttf",
            "/system/common/font2/PS4Icon.ttf",
            "/system_ex/common_ex/font2/PS4Icon.ttf"};
}

bool pumpEvents() {
    if (g_shutdownSignal != 0) {
        XC_LOGI("shell: received signal %d, initiating graceful shutdown", static_cast<int>(g_shutdownSignal));
        return false;
    }
    return true;
}
bool hasFocus() { return true; }
bool canSelfUpdate() { return false; }
void beforeShutdown() {}
Caps caps() {
    Caps c;
    c.console = true;
    c.openUrl = false;
    c.selfUpdate = false;
    c.window = false;
    return c;
}
void setFullscreen(bool) {}
bool fullscreenSwitched(bool&) { return false; }
bool openUrl(const std::string&) { return false; }
bool setTextInput(bool) { return false; }
std::string takeTextInput() { return {}; }
void setMouseHandler(std::function<void(const MouseEvent&)>) {}

bool isInstalledCopy(const std::string& dir) {
    struct stat here {}, running {};
    return ::stat((dir + "/eboot.bin").c_str(), &here) == 0 && ::stat("/app0/eboot.bin", &running) == 0 &&
           here.st_ino == running.st_ino;
}

void restartOrExit() {
    if (!g_autoplay.noRestart) platform::restartApp();
    platform::notify(ui::tr(ui::Str::UpdateReopen), "update installed, restart failed: closing");
    platform::sleepMs(2000);
    _exit(0);
}

}  // namespace xc::app::shell
