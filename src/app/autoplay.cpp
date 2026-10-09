// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
// Unattended test mode (tools/ps5/autotest.sh); see autoplay.h.
#include "app/autoplay.h"

#include "app/ps5_app.h"
#include "display/display.h"
#include "display/gpu.h"
#include "media/decoder.h"
#include "platform/platform.h"
#include "ui/canvas.h"
#include "util/log.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace xc::app {

Autoplay g_autoplay;
std::atomic<bool> g_syntheticA{false};

namespace {
// The home screen was saved: the tests that start from it may go.
bool uiSaved = false;
bool settingsShot = false;  // "settingstest": save the screen now
uint64_t homeSince = 0, launchSince = 0;
bool launchSaved = false;
}  // namespace

void loadAutoplay() {
    std::string text;
    if (!platform::readFile(platform::dataDir() + "/autoplay.txt", text)) return;
    char title[128] = {};
    char option[64] = {};
    int seconds = 0;
    if (std::sscanf(text.c_str(), "%127s %d %63s", title, &seconds, option) < 1) return;
    std::string options = option;  // comma-separated
    for (size_t start = 0; start <= options.size();) {
        size_t comma = options.find(',', start);
        if (comma == std::string::npos) comma = options.size();
        std::string opt = options.substr(start, comma - start);
        start = comma + 1;
        if (opt == "nosimd") media::disableSimd();
        if (opt == "dump") g_autoplay.dump = true;
        if (opt == "repeat") g_autoplay.runs = 2;
        if (opt == "idle") g_autoplay.idle = true;
        if (opt == "rumbletest") input::setRumble(200, 200, 1500);
        if (opt == "triggertest") input::setTriggerRumble(255, 128, 3000);
        if (opt == "vibetest") g_autoplay.vibeTest = true;
        if (opt == "vktest") g_autoplay.vkTest = true;
        if (opt == "cpudisplay") g_autoplay.cpuDisplay = true;
        if (opt == "hwdecode") g_autoplay.hwDecode = true;
        if (opt == "droptest") g_autoplay.dropTest = true;
        if (opt == "ai") g_autoplay.upscaler = 1;
        if (opt == "restore") g_autoplay.restore = true;
        if (opt.rfind("swap=", 0) == 0) display::gpu::setSwapImages(std::atoi(opt.c_str() + 5));
        if (opt == "nopace") display::gpu::setPresentWait(false);
        if (opt == "detailtest") g_autoplay.detailTest = true;
        if (opt == "pad") g_autoplay.pad = true;
        if (opt == "consoles") g_autoplay.consoles = true;
        if (opt == "consolestab") g_autoplay.consolesTab = true;
        if (opt == "settingstest") g_autoplay.settingsTest = true;
        if (opt == "badca") g_autoplay.badCa = true;
        if (opt == "consolesempty") g_autoplay.consolesTab = g_autoplay.consolesEmpty = true;
        if (opt == "librarytest") g_autoplay.libraryTest = true;
        if (opt == "imetest") g_autoplay.imeTest = true;
        if (opt == "menutest") g_autoplay.menuTest = true;
        if (opt.rfind("res=", 0) == 0) g_autoplay.resolution = opt.substr(4);
        if (opt.rfind("sharp=", 0) == 0) g_autoplay.sharpness = std::atoi(opt.c_str() + 6);
        if (opt.rfind("deband=", 0) == 0) g_autoplay.deband = std::atoi(opt.c_str() + 7);
        if (opt.rfind("threads=", 0) == 0) g_autoplay.decodeThreads = std::atoi(opt.c_str() + 8);
    }
    g_autoplay.title = title;
    g_autoplay.seconds = seconds > 0 ? seconds : 60;
    XC_LOGI("AUTOPLAY %s for %ds %s", title, g_autoplay.seconds, option);
    platform::probeNetworking();
}

void autoplayFinished(const std::string& result) {
    if (g_autoplay.title.empty()) return;
    if (--g_autoplay.runs > 0) {
        XC_LOGI("AUTOPLAY next run: %s", result.c_str());
        g_command = kPlay;
    } else {
        XC_LOGI("AUTOPLAY END: %s", result.c_str());
    }
}

void runDecodeBench() {
    std::string data;
    if (!platform::readFile(platform::dataDir() + "/sample.h264", data)) {
        XC_LOGE("AUTOPLAY END: no sample.h264");
        return;
    }
    auto aus = media::splitAccessUnits(reinterpret_cast<const uint8_t*>(data.data()), data.size());
    XC_LOGI("bench: %zu bytes, %zu access units", data.size(), aus.size());
    media::VideoDecoder dec;
    if (!dec.init(1)) {
        XC_LOGE("AUTOPLAY END: decoder init failed");
        return;
    }
    media::Picture pic;
    int pictures = 0;
    uint64_t t0 = platform::nowMs();
    for (const auto& [off, len] : aus)
        if (dec.decode(reinterpret_cast<const uint8_t*>(data.data()) + off, len, pic)) ++pictures;
    uint64_t ms = platform::nowMs() - t0;
    XC_LOGI("AUTOPLAY END: bench %d pictures (%dx%d) in %llu ms = %.2f ms/picture", pictures, pic.width,
            pic.height, static_cast<unsigned long long>(ms), pictures ? double(ms) / pictures : 0.0);
}

void autoplayPad(input::ControllerState& pad) {
    // Autoplay runs unattended: the physical pad must not interfere
    // (unless "pad": someone is playing along, and each press is logged).
    if (!g_autoplay.title.empty() && !g_autoplay.pad) pad = input::ControllerState{};
    if (!g_autoplay.pad) return;
    static std::string lastPressed;
    std::string pressed;
    auto add = [&](bool on, const char* name) {
        if (on) pressed += pressed.empty() ? name : std::string(" ") + name;
    };
    add(pad.btnA, "cross");
    add(pad.btnB, "circle");
    add(pad.btnX, "square");
    add(pad.btnY, "triangle");
    add(pad.dpadUp, "up");
    add(pad.dpadDown, "down");
    add(pad.dpadLeft, "left");
    add(pad.dpadRight, "right");
    add(pad.btnL1, "L1");
    add(pad.btnR1, "R1");
    add(pad.btnOptions, "options");
    add(pad.btnTouchpad, "touchpad");
    add(pad.triggerL2 > 0.5f, "L2");
    add(pad.triggerR2 > 0.5f, "R2");
    add(std::abs(pad.leftStickX) > 0.5f || std::abs(pad.leftStickY) > 0.5f, "lstick");
    add(std::abs(pad.rightStickX) > 0.5f || std::abs(pad.rightStickY) > 0.5f, "rstick");
    if (pressed != lastPressed && !pressed.empty()) XC_LOGI("pad: %s", pressed.c_str());
    lastPressed = pressed;
}

void autoplayNav(ui::NavInput& nav, bool& menuCombo, uint64_t now) {
    if (g_autoplay.menuTest && g_ui->screen() == ui::Screen::Streaming) {
        // Menu, down five times to the resolution, left to 720p, accept; at
        // 35 s right (back to 1080p) and accept.
        static uint64_t since = 0;
        static int step = 0;
        if (!since) since = now;
        const uint64_t at[] = {12000, 13000, 13200, 13400, 13600, 13800, 14000, 14500, 35000, 35500};
        if (step < 10 && now - since >= at[step]) {
            nav = ui::NavInput{};
            if (step == 0) menuCombo = true;
            if (step >= 1 && step <= 5) nav.down = true;
            if (step == 6) nav.left = true;
            if (step == 7 || step == 9) nav.accept = true;
            if (step == 8) nav.right = true;
            XC_LOGI("autoplay: menu step %d", step);
            ++step;
        }
    }

    if (g_autoplay.settingsTest && uiSaved) {
        // Settings, down to the resolution, open its list; saved 2 s later.
        static uint64_t since = 0;
        static int step = 0;
        if (!since) since = now;
        const uint64_t at[] = {500, 1500, 2500, 4500};
        if (step < 4 && now - since >= at[step]) {
            if (step == 0) nav.options = true;
            if (step == 1) nav.down = true;
            if (step == 2) nav.accept = true;
            if (step == 3) {
                settingsShot = true;  // saved below, once drawn
                g_autoplay.settingsTest = false;
            }
            ++step;
        }
    }
}

void autoplayScreens(const ui::Canvas& canvas, uint64_t now) {
    // Autoplay: save the rendered home and loading screens.
    auto saveCanvas = [&](const char* name) {
        std::string ppm = "P6\n1920 1080\n255\n";
        ppm.reserve(ppm.size() + 1920u * 1080u * 3u);
        for (size_t i = 0; i < 1920u * 1080u; ++i) {
            uint32_t p = canvas.data()[i];
            ppm += static_cast<char>(p & 0xFF);
            ppm += static_cast<char>((p >> 8) & 0xFF);
            ppm += static_cast<char>((p >> 16) & 0xFF);
        }
        XC_LOGI("ui snapshot %s: %s", name,
                platform::writeFileAtomic(platform::dataDir() + "/" + name, ppm) ? "ok" : "failed");
    };
    if (!g_autoplay.title.empty() && !launchSaved && g_ui->screen() == ui::Screen::Launching) {
        if (!launchSince) launchSince = now;
        if (now - launchSince > 8000) {
            launchSaved = true;
            saveCanvas("launch.ppm");
        }
    }
    if (g_autoplay.libraryTest && uiSaved) {
        static uint64_t openedAt = 0;
        static int saved = 0;
        if (!openedAt) {
            ui::NavInput r1;
            r1.r1 = true;
            g_ui->handle(r1);
            openedAt = now;
        } else if (saved == 0 && now - openedAt > 4000) {
            saveCanvas("library.ppm");
            saved = 1;
        } else if (saved == 1 && now - openedAt > 25000) {
            saveCanvas("library2.ppm");
            g_autoplay.libraryTest = false;
            XC_LOGI("AUTOPLAY END: library test");
        }
    }
    if (g_autoplay.vibeTest) {
        // 10 s to pick the pad up, then each motor alone for 6 s, 3 s apart.
        static uint64_t since = 0;
        static int step = -1;
        if (!since) since = now;
        int want = now - since < 10000 ? -1 : static_cast<int>((now - since - 10000) / 9000);
        bool on = want >= 0 && (now - since - 10000) % 9000 < 6000;
        static const char* const kSteps[] = {"1/5: large motor (left grip, strong)", "2/5: small motor (right grip, fine)",
                                             "3/5: left trigger (L2)", "4/5: right trigger (R2)",
                                             "5/5: both triggers"};
        if (want != step && on && want < 5) {
            step = want;
            input::setRumble(step == 0 ? 255 : 0, step == 1 ? 255 : 0, 6000);
            input::setTriggerRumble(step == 2 || step == 4 ? 255 : 0, step == 3 || step == 4 ? 255 : 0, 6000);
            platform::notify(std::string("Vibration test ") + kSteps[step]);
            XC_LOGI("vibetest %s", kSteps[step]);
        }
        if (want >= 5) {
            g_autoplay.vibeTest = false;
            XC_LOGI("AUTOPLAY END: vibration test");
        }
    }
    if (g_autoplay.imeTest && uiSaved) {
        static uint64_t openedAt = 0;
        std::string typed;
        if (!openedAt) {
            openedAt = now;
            bool ok = platform::openSystemKeyboard("PSBox test", "hello", 64);
            XC_LOGI("autoplay: system keyboard %s", ok ? "opened" : "unavailable");
            if (!ok) g_autoplay.imeTest = false;
        } else if (auto st = platform::pollSystemKeyboard(typed); st != platform::KeyboardStatus::Open) {
            XC_LOGI("AUTOPLAY END: keyboard %s, %zu bytes", st == platform::KeyboardStatus::Accepted ? "accepted" : "closed",
                    typed.size());
            g_autoplay.imeTest = false;
        }
    }
    if (!g_autoplay.title.empty() && g_ui->screen() == ui::Screen::Error) {
        // The error the run ended on, as shown (error.ppm), once drawn.
        static uint64_t errorSince = 0;
        if (!errorSince) errorSince = now;
        else if (errorSince != 1 && now - errorSince > 500) {
            saveCanvas("error.ppm");
            errorSince = 1;
        }
    }
    if (g_autoplay.detailTest && uiSaved) {
        // A game to buy far down the list (no prefetched details): its
        // page must fill in on its own. Or the game named, when known.
        static uint64_t openedAt = 0;
        ui::GameTile tile;
        if (!openedAt && (g_ui->findTile(g_autoplay.title, tile) || g_ui->purchasableAt(40, tile))) {
            XC_LOGI("autoplay: opening %s (%s)", tile.name.c_str(), tile.productId.c_str());
            g_ui->showDetails(tile);
            openedAt = now;
        } else if (openedAt && now - openedAt > 8000) {
            saveCanvas("detail.ppm");
            g_autoplay.detailTest = false;
            XC_LOGI("AUTOPLAY END: detail test");
        }
    }
    if (settingsShot) {
        settingsShot = false;
        saveCanvas("settings.ppm");
        XC_LOGI("AUTOPLAY END: settings test");
    }
    if (g_autoplay.consolesTab && uiSaved) {
        static uint64_t shownAt = 0;
        if (!shownAt) {
            g_ui->showTab(ui::Tab::Consoles);
            if (g_autoplay.consolesEmpty) g_ui->setConsoles({}, true);
            shownAt = now;
        } else if (now - shownAt > 3000) {
            saveCanvas("consoles.ppm");
            g_autoplay.consolesTab = false;
            XC_LOGI("AUTOPLAY END: consoles tab");
        }
    }
    if (!g_autoplay.title.empty() && !uiSaved && g_ui->screen() == ui::Screen::Home) {
        if (!homeSince) homeSince = now;
        if (now - homeSince > 5000) {
            uiSaved = true;
            saveCanvas("ui.ppm");
        }
    }
}

}  // namespace xc::app
