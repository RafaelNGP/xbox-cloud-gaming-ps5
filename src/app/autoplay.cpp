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
const char* pickerShot = nullptr;
bool confirmShot = false, confirmHome = false;
const char* tuneShot = nullptr;  // "tunetest": save this screen next  // "confirmtest": save confirm2.ppm / check the home screen  // "pickertest": save this screen next, once drawn
const char* gridShot = nullptr;  // "gridtest": save grid screenshot
const char* gameSettingsShot = nullptr; // "gamesettingstest": save screenshot
bool acceptUpdate = false;  // "updatetest": Cross on the pop-up, next pass ("updateskip": Circle)
}  // namespace

void loadAutoplay() {
    std::string text;
    if (!platform::readFile(platform::dataDir() + "/autoplay.txt", text)) return;
    char title[128] = {};
    char option[512] = {};
    int seconds = 0;
    if (std::sscanf(text.c_str(), "%127s %d %511s", title, &seconds, option) < 1) return;
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
        if (opt.rfind("updatefeed=", 0) == 0) g_autoplay.updateFeed = opt.substr(11);
        if (opt == "testca") g_autoplay.testCa = true;
        if (opt == "updatetest") g_autoplay.updateTest = true;
        if (opt == "updateskip") g_autoplay.updateSkip = true;
        if (opt == "settingsupdate") g_autoplay.settingsUpdate = true;
        if (opt == "pickertest") g_autoplay.pickerTest = true;
        if (opt == "menushot") g_autoplay.menuShot = true;
        if (opt == "confirmtest") g_autoplay.confirmTest = true;
        if (opt == "tunetest") g_autoplay.tuneTest = true;
        if (opt == "searchtest") g_autoplay.searchTest = true;
        if (opt == "quicktest") g_autoplay.quickTest = true;
        if (opt == "scrolltest") g_autoplay.scrollTest = true;
        if (opt.rfind("locktitle=", 0) == 0) g_autoplay.lockTitle = opt.substr(10);
        if (opt == "gridtest") g_autoplay.gridTest = true;
        if (opt == "gamesettingstest") g_autoplay.gameSettingsTest = true;
        if (opt == "norestart") g_autoplay.noRestart = true;
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
    if (g_autoplay.tuneTest && uiSaved) {
        // Settings, down to the dead zone, its tester: the left stick inside
        // its dead zone, the right one out; left 15 -> 20 %, R1, right 15 ->
        // 12 % (saved), Circle. Down to the trigger vibration, its tester:
        // strong, high frequency, strong resistance, force pulses; L2 alone,
        // then R2 alone (saved), Circle.
        static uint64_t since = 0;
        uint64_t now = platform::nowMs();
        if (!since) since = now;
        uint64_t t = now - since;
        auto in = [&](uint64_t at, uint64_t len) { return t >= at && t < at + len; };
        auto pulses = [&](uint64_t from, int n) {
            for (int i = 0; i < n; ++i)
                if (in(from + 300u * static_cast<unsigned>(i), 120)) return true;
            return false;
        };
        pad.btnOptions = in(500, 150);
        pad.dpadDown = pulses(1000, 3) || in(8000, 120) || in(9400, 120) || in(10200, 120) || in(11600, 120);
        pad.btnA = in(2000, 150) || in(8500, 150);
        pad.btnB = in(7500, 150) || in(16500, 150);
        pad.btnR1 = in(4600, 150);
        pad.dpadRight = pulses(3000, 5) || in(9000, 120) || in(9800, 120) || pulses(10600, 3) || in(12000, 120);
        pad.dpadLeft = pulses(5000, 3);
        if (in(2500, 5000)) pad.rawLeftX = 0.1f, pad.rawLeftY = 0.06f, pad.rawRightX = 0.7f, pad.rawRightY = -0.4f;
        if (in(12500, 1500)) pad.triggerL2 = 0.5f;  // each trigger alone: the log shows each one
        if (in(14500, 1500)) pad.triggerR2 = 0.6f;
        if (in(6500, 20)) tuneShot = "sticks.ppm";
        if (in(15500, 20)) tuneShot = "triggers.ppm";
        if (in(17500, 20)) {
            g_autoplay.tuneTest = false;
            XC_LOGI("AUTOPLAY END: tune test");
        }
    }
    if (g_autoplay.gameSettingsTest && uiSaved) {
        static uint64_t since = 0;
        static int step = 0;
        uint64_t now = platform::nowMs();
        if (!since) since = now;
        uint64_t t = now - since;

        // Step 0: Open details
        if (step == 0 && t >= 500) {
            ui::GameTile tile;
            bool found = (!g_autoplay.title.empty() && g_ui->findTile(g_autoplay.title, tile)) ||
                         g_ui->firstTile(tile) || g_ui->purchasableAt(0, tile);
            if (found) {
                g_ui->showDetails(tile);
                step = 1;
            }
        }
        // Step 1: Wait for details to draw, then take screenshot
        if (step == 1 && t >= 2500) {
            gameSettingsShot = "detail.ppm";
            step = 2;
        }
        // Step 2: Press Options to open Game Settings
        if (step == 2 && t >= 3200 && t < 3400) {
            pad.btnOptions = true;
        }
        if (step == 2 && t >= 3500) {
            step = 3;
        }
        // Step 3: Take screenshot of default Game Settings modal
        if (step == 3 && t >= 4500) {
            gameSettingsShot = "game_settings.ppm";
            step = 4;
        }
        // Step 4: D-pad navigation and changes
        if (step == 4 && t >= 5200 && t < 5400) {
            pad.dpadDown = true;
        }
        if (step == 4 && t >= 6000 && t < 6200) {
            pad.dpadRight = true;
        }
        if (step == 4 && t >= 6800 && t < 7000) {
            pad.dpadDown = true;
        }
        if (step == 4 && t >= 7600 && t < 7800) {
            pad.dpadRight = true;
        }
        if (step == 4 && t >= 8400 && t < 8600) {
            pad.dpadDown = true;
        }
        if (step == 4 && t >= 9000) {
            step = 5;
        }
        // Step 5: Take screenshot of Custom Game Settings modal
        if (step == 5 && t >= 9800) {
            gameSettingsShot = "game_settings_custom.ppm";
            step = 6;
        }
        // Step 6: Press Circle (btnB) to save and close
        if (step == 6 && t >= 10600 && t < 10800) {
            pad.btnB = true;
        }
        if (step == 6 && t >= 11000) {
            step = 7;
        }
        // Step 7: Take screenshot of detail after closing modal
        if (step == 7 && t >= 12000) {
            gameSettingsShot = "detail_after.ppm";
            step = 8;
        }
        // Step 8: Finish
        if (step == 8 && t >= 13000) {
            g_autoplay.gameSettingsTest = false;
            XC_LOGI("AUTOPLAY END: game settings test");
            step = 9;
        }
    }
    if (g_autoplay.scrollTest && uiSaved) {
        // Game Pass: right stick down, then right; R1 to My games: right
        // stick down; a finger moving up the touchpad, then down. The focus
        // is logged after each.
        static uint64_t since = 0;
        uint64_t now = platform::nowMs();
        if (!since) since = now;
        uint64_t t = now - since;
        auto in = [&](uint64_t at, uint64_t len) { return t >= at && t < at + len; };
        if (in(1000, 150)) pad.rightStickY = 1.0f;
        if (in(2500, 150)) pad.rightStickX = 1.0f;
        if (in(4000, 150)) pad.btnR1 = true;
        if (in(5500, 150)) pad.rightStickY = 1.0f;
        if (in(7000, 400)) pad.touching = true, pad.touchX = 0.5f, pad.touchY = 0.8f - 0.5f * static_cast<float>(t - 7000) / 400;
        if (in(8500, 400)) pad.touching = true, pad.touchX = 0.5f, pad.touchY = 0.3f + 0.5f * static_cast<float>(t - 8500) / 400;
        for (uint64_t at : {800, 2000, 3500, 5000, 6500, 8000, 9500})
            if (in(at, 20)) XC_LOGI("scroll: at %llu ms: %s", static_cast<unsigned long long>(at), g_ui->focusDescription().c_str());
        if (in(10000, 20)) {
            g_autoplay.scrollTest = false;
            XC_LOGI("AUTOPLAY END: scroll test");
        }
    }
    if (g_autoplay.confirmTest && uiSaved) {
        // The buttons as the hands press them, swapped as input::poll swaps
        // them: Settings, down to the confirm button; Cross opens its list,
        // down, Cross (held over the change) picks Circle; Circle now opens
        // the list again (saved), Circle picks; Cross now leaves Settings.
        static uint64_t since = 0;
        uint64_t now = platform::nowMs();
        if (!since) since = now;
        uint64_t t = now - since;
        auto in = [&](uint64_t at, uint64_t len) { return t >= at && t < at + len; };
        bool cross = in(2700, 150) || in(3700, 500) || in(6800, 150), circle = in(4800, 150) || in(6000, 150);
        bool swapped = input::circleConfirms();
        pad.btnA = swapped ? circle : cross;
        pad.btnB = swapped ? cross : circle;
        pad.btnOptions = in(500, 150);
        pad.dpadDown = in(1000, 120) || in(1300, 120) || in(1600, 120) || in(1900, 120) || in(2200, 120) || in(3200, 120);
        if (in(5500, 20)) confirmShot = true;
        if (in(8000, 20)) confirmHome = true;
    }
    if (g_autoplay.gridTest && uiSaved) {
        static uint64_t since = 0;
        uint64_t now = platform::nowMs();
        if (!since) since = now;
        uint64_t t = now - since;
        auto in = [&](uint64_t at, uint64_t len) { return t >= at && t < at + len; };
        pad.dpadDown = in(1000, 150) || in(2200, 150) || in(3400, 150) || in(4600, 150) || in(5800, 150) ||
                       in(8500, 150) || in(10000, 150);
        pad.dpadRight = in(11200, 150);
        pad.dpadUp = in(13000, 150) || in(14500, 150) || in(16000, 150);
        if (in(7500, 20)) gridShot = "grid1.ppm";
        if (in(12000, 20)) gridShot = "grid2.ppm";
        if (in(17500, 20)) {
            gridShot = "grid_back.ppm";
            g_autoplay.gridTest = false;
            XC_LOGI("AUTOPLAY END: grid test");
        }
    }
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
    if (g_autoplay.settingsUpdate && uiSaved && !g_ui->updateOffered()) {
        // Settings, down to Updates (the last row), Cross.
        static uint64_t since = 0;
        static int step = 0;
        if (!since) since = now;
        if (step < 10 && now - since >= 500u + 300u * static_cast<unsigned>(step)) {
            nav = ui::NavInput{};
            if (step == 0) nav.options = true;
            if (step >= 1 && step <= 8) nav.down = true;
            if (step == 9) nav.accept = true;
            XC_LOGI("autoplay: settings update step %d", step);
            ++step;
        }
    }
    if (g_autoplay.menuShot && g_ui->screen() == ui::Screen::Streaming) {
        static uint64_t since = 0;
        static int step = 0;
        if (!since) since = now;
        if (step == 0 && now - since >= 7000) menuCombo = true, ++step;
        else if (step == 1 && now - since >= 13000) nav = ui::NavInput{}, nav.back = true, ++step;
    }
    if (g_autoplay.pickerTest && uiSaved) {
        // Settings; down to the confirm button, its list (saved), closed;
        // down to the light bar, its list, Custom colour; the stick right and
        // up for 1.5 s, L2 (darker); saved; Cross; Settings saved.
        static uint64_t since = 0;
        if (!since) since = now;
        uint64_t t = now - since;
        static int step = 0;
        const uint64_t at[] = {500, 1000, 1300, 1600, 1900, 2200, 2700, 4500, 5000, 5500, 6000, 6500, 7000, 9000, 10500, 11000, 12500};
        if (t >= 7500 && t < 9000) nav.stickX = 1.0f, nav.stickY = -0.6f;
        if (step < 17 && t >= at[step]) {
            nav = ui::NavInput{};
            nav.nowMs = now;
            switch (step) {
            case 0: nav.options = true; break;
            case 1: case 2: case 3: case 4: case 5: case 9: case 11: nav.down = true; break;
            case 6: case 10: case 12: case 15: nav.accept = true; break;
            case 7: pickerShot = "confirm.ppm"; break;
            case 8: nav.back = true; break;  // the confirm button's list closes, unchanged
            case 13: nav.l2 = true; break;   // after the stick: darker
            case 14: pickerShot = "picker.ppm"; break;
            case 16: pickerShot = "settings.ppm"; break;
            }
            XC_LOGI("autoplay: picker step %d", step);
            ++step;
        }
    }
    if (acceptUpdate) {
        acceptUpdate = false;
        nav = ui::NavInput{};
        (g_autoplay.updateSkip ? nav.back : nav.accept) = true;
        XC_LOGI("autoplay: update %s", g_autoplay.updateSkip ? "not now" : "now");
    }
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
    if (tuneShot) {
        saveCanvas(tuneShot);
        tuneShot = nullptr;
    }
    if (gridShot) {
        saveCanvas(gridShot);
        gridShot = nullptr;
    }
    if (gameSettingsShot) {
        saveCanvas(gameSettingsShot);
        gameSettingsShot = nullptr;
    }
    if (confirmShot) {
        confirmShot = false;
        saveCanvas("confirm2.ppm");
    }
    if (confirmHome) {
        confirmHome = false;
        g_autoplay.confirmTest = false;
        saveCanvas("home.ppm");
        XC_LOGI("AUTOPLAY END: confirm test, on the %s screen", g_ui->screen() == ui::Screen::Home ? "home" : "WRONG");
    }
    if (g_autoplay.quickTest && uiSaved) {
        g_autoplay.quickTest = false;
        ui::GameTile t;
        for (const char* id : {"HOGWARTSLEGACYXBOXSERIESXSVERSION", "HOGWARTSLEGACYXBOXONEVERSION", "RUSTCONSOLEEDITIONXS",
                               "RUSTCONSOLEEDITION", "CALLOFDUTYVANGUARDXBOXSERIESXS", "CALLOFDUTYVANGUARD", "PGATOUR2K23",
                               "PGATOUR2K23XBOXONE"})
            XC_LOGI("quick: %s badge %s", id, g_ui->findTile(id, t) ? t.platform.c_str() : "(not found)");
        if (!g_autoplay.lockTitle.empty())
            XC_LOGI("quick: %s %s", g_autoplay.lockTitle.c_str(),
                    !g_ui->findTile(g_autoplay.lockTitle, t) ? "not found" : t.playable ? "playable" : "LOCKED");
        g_ui->showFilteredSearch(ui::Tab::Library, false, 0, "MOBA", 0);
        size_t mine = g_ui->searchResults(500).size();
        g_ui->showFilteredSearch(ui::Tab::GamePass, false, 0, "MOBA", 0);
        XC_LOGI("quick: MOBA games: %zu in My games, %zu in Game Pass", mine, g_ui->searchResults(500).size());
        g_ui->showHome();
        XC_LOGI("AUTOPLAY END: quick test");
    }
    if (g_autoplay.searchTest && uiSaved) {
        // "Your games" by lowest price, then on sale, once the prices are in
        // (the background fetch): each saved, its first results logged.
        static uint64_t since = 0;
        static int step = 0;
        if (!since) since = now;
        auto logResults = [](const char* what) {
            int i = 0;
            for (const auto& [name, p] : g_ui->searchResults(12))
                XC_LOGI("search %s %2d: %.2f (was %.2f) %s", what, ++i, p.list, p.msrp, name.c_str());
        };
        if (step == 0 && now - since > 20000) g_ui->showFilteredSearch(ui::Tab::Library, true, 0, "", 0), ++step;
        else if (step == 1 && now - since > 24000) saveCanvas("cheapest.ppm"), logResults("lowest"), ++step;
        // Game Pass, online co-op and spoken in the app's language, once the
        // details have had time to arrive.
        else if (step == 2 && now - since > 60000) g_ui->showFilteredSearch(ui::Tab::GamePass, false, 3, "", 2), ++step;
        else if (step == 3 && now - since > 63000) saveCanvas("filters.ppm"), logResults("co-op, dubbed"), ++step;
        else if (step == 4) g_ui->showFilteredSearch(ui::Tab::GamePass, false, 0, "", 0, 1), ++step;  // the genres' list
        else if (step == 5 && now - since > 66000) {
            saveCanvas("genres.ppm");
            g_autoplay.searchTest = false;
            XC_LOGI("AUTOPLAY END: search test");
        }
    }
    if (pickerShot) {
        saveCanvas(pickerShot);
        if (std::string(pickerShot) == "settings.ppm") XC_LOGI("AUTOPLAY END: picker test");
        pickerShot = nullptr;
    }
    if (g_autoplay.title == "UPDATE") {
        // The update test: the pop-up saved and accepted, the progress
        // saved; the run ends on the home screen without one (the new
        // version, or none offered).
        static uint64_t offeredAt = 0, updatingAt = 0, plainHomeAt = 0;
        bool offered = g_ui->updateOffered();
        if (offered && !offeredAt) offeredAt = now;
        if (offered && (g_autoplay.updateTest || g_autoplay.updateSkip) && now - offeredAt > 2500 && !acceptUpdate) {
            saveCanvas("update.ppm");
            acceptUpdate = true;
        }
        if (g_ui->screen() == ui::Screen::Updating && !updatingAt) updatingAt = now;
        if (updatingAt && updatingAt != 1 && now - updatingAt > 1500) {
            saveCanvas("updating.ppm");
            updatingAt = 1;
        }
        bool plainHome = g_ui->screen() == ui::Screen::Home && !offered;
        if (!plainHome) plainHomeAt = 0;
        else if (!plainHomeAt) plainHomeAt = now;
        else if (plainHomeAt != 1 && now - plainHomeAt > 25000) {
            plainHomeAt = 1;
            XC_LOGI("AUTOPLAY END: update test, running v%s", XC_APP_VERSION);
        }
    }
}

}  // namespace xc::app
