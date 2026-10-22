// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
// app/shell.h on the PS5.
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

extern "C" int sceSystemServiceHideSplashScreen(void);
extern "C" const char* sceKernelGetFsSandboxRandomWord(void);

namespace xc::app::shell {

void beforeSettings() {
    // An update cut short: the old files went back, and the old eboot.bin
    // is the one to run.
    if (recoverUpdate(platform::appDir())) platform::restartApp();
    importStoreLeftovers(platform::dataDir(), "/data/prosperostore/previous/PPSA99810");
}

void beforeDisplay(const Settings& settings) {
    sceSystemServiceHideSplashScreen();
    // 120 Hz output: param.json declares it (the console grants the mode only
    // then), and RADV asks for it when the param.json it reads says so. Off
    // (the default: OLED TVs may flicker under VRR in the menus), RADV reads
    // one that doesn't. Decided once per start, as RADV opens VideoOut once.
    // With the patched RADV the output starts at 60 Hz and each game's stream
    // turns 120 Hz on (StreamScreen); without it, the setting decides here.
    if (!settings.highRefresh || display::canSwitchRefresh()) {
        std::string path = platform::dataDir() + "/videoout-60hz.json";
        platform::writeFileAtomic(path, "{\"attribute3\": 0}\n");
        ::setenv("PS5_VIDEOOUT_PARAM_JSON", path.c_str(), 1);
    }
    XC_LOGI("display: 120 Hz output %s", !settings.highRefresh ? "off"
                                         : display::canSwitchRefresh() ? "in games" : "on");
}

// A display that didn't come up isn't the end here (as before): the app runs on.
bool afterDisplay(bool) { return true; }

std::vector<std::string> iconFontPaths() {
    const char* word = sceKernelGetFsSandboxRandomWord();
    std::string sandbox = std::string("/") + (word ? word : "");
    return {"/system_ex/common_ex/font2/PS4Icon.ttf", sandbox + "/common_ex/font2/PS4Icon.ttf",
            sandbox + "_ex/common_ex/font2/PS4Icon.ttf"};
}

// The system closes the app (from the home screen): nothing to read here.
bool pumpEvents() { return true; }
bool hasFocus() { return true; }

bool canSelfUpdate() { return true; }
// Never called: the PS5's loop doesn't end.
void beforeShutdown() {}
Caps caps() { return {true, false, true}; }
void setFullscreen(bool) {}
bool fullscreenSwitched(bool&) { return false; }
// No browser to send a link to: the screens show a QR code instead.
bool openUrl(const std::string&) { return false; }
bool setTextInput(bool) { return false; }  // the USB keyboard keeps moving through the screens
std::string takeTextInput() { return {}; }
void setMouseHandler(std::function<void(const MouseEvent&)>) {}

bool isInstalledCopy(const std::string& dir) {
    // /app0 is the folder the system started the app from.
    struct stat here {}, running {};
    return ::stat((dir + "/eboot.bin").c_str(), &here) == 0 && ::stat("/app0/eboot.bin", &running) == 0 &&
           here.st_ino == running.st_ino;
}

void restartOrExit() {
    if (!g_autoplay.noRestart) platform::restartApp();
    // It couldn't start again by itself: closed, the next start is the new version.
    platform::notify(ui::tr(ui::Str::UpdateReopen), "update installed, restart failed: closing");
    platform::sleepMs(2000);
    // Not std::exit: its destructors would free what the background
    // threads still use (a crash, then a false "closed unexpectedly").
    // The log is already on disk (flushed by line).
    _exit(0);
}

}  // namespace xc::app::shell
