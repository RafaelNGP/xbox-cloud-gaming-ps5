// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
// PS5 front end: main() sets things up, then the main thread reads the pad,
// drives the UI (ui::AppUi) and draws it. The rest lives beside it:
// worker.cpp (all the network work, on its own thread), stream_screen.cpp
// (the game's menu, gestures and controllers while streaming),
// price_loop.cpp (store prices) and autoplay.cpp (the unattended tests).
#include "app/autoplay.h"
#include "app/ps5_app.h"
#include "app/settings.h"
#include "app/stream_screen.h"
#include "display/display.h"
#include "display/gpu.h"
#include "input/controller.h"
#include "net/http.h"
#include "platform/platform.h"
#include "ui/app_ui.h"
#include "ui/strings.h"
#include "util/log.h"

#include <cmath>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

extern "C" int sceSystemServiceHideSplashScreen(void);

namespace xc::app {

std::unique_ptr<ui::AppUi> g_ui;
std::mutex g_settingsMutex;
Settings g_settings;
std::string settingsPath() { return platform::dataDir() + "/settings.json"; }
bool allow1440Locked() { return std::max(g_settings.maxHeightCloud, g_settings.maxHeightHome) >= 1440; }
std::atomic<int> g_command{kSignIn};
std::atomic<bool> g_cancel{false};
std::mutex g_argMutex;
ui::GameTile g_playTile;
std::mutex g_playerMutex;
StreamPlayer* g_player = nullptr;
std::mutex g_infoMutex;
ui::StreamInfo g_streamInfo;
std::atomic<uint32_t> g_infoSeq{0};
std::atomic<bool> g_playingHome{false};
std::atomic<bool> g_tierPicked{false};
std::atomic<uint64_t> g_xboxButtonUntil{0};

}  // namespace xc::app

using namespace xc;
using namespace xc::app;

namespace {

std::unique_ptr<ui::ImageCache> g_images;

// The dead zone setting (percent) as an index into ui::kDeadzonePercent.
int deadzoneIndex(int percent) {
    int best = 0;
    for (int i = 0; i < static_cast<int>(std::size(ui::kDeadzonePercent)); ++i)
        if (std::abs(ui::kDeadzonePercent[i] - percent) < std::abs(ui::kDeadzonePercent[best] - percent)) best = i;
    return best;
}

// Dead zone, trigger vibration and confirm button from g_settings.
void applyControllerSettings() {
    std::lock_guard<std::mutex> lock(g_settingsMutex);
    input::setDeadzone(g_settings.deadzone / 100.0f);
    input::setTriggerRumbleEnabled(g_settings.triggerRumble);
    input::setCircleConfirms(g_settings.circleConfirms);
    if (g_ui) g_ui->setCircleConfirms(g_settings.circleConfirms);
}

// --- Input ---------------------------------------------------------------------

// D-pad / left stick with key repeat: first press, then every 110 ms after
// 350 ms held.
class Repeater {
public:
    bool update(bool held, uint64_t now) {
        if (!held) {
            since_ = 0;
            return false;
        }
        if (!since_) {
            since_ = next_ = now;
            next_ += 350;
            return true;
        }
        if (now >= next_) {
            next_ = now + 110;
            return true;
        }
        return false;
    }

private:
    uint64_t since_ = 0, next_ = 0;
};

}  // namespace

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;

    platform::init();
    log::setFile((platform::dataDir() + "/xcloud.log").c_str());
    XC_LOGI("=== PSBox Cloud Gaming starting ===");
    loadAutoplay();
    g_settings.load(settingsPath());
    ui::setLanguage(ui::languageFromCode(g_settings.language));
    XC_LOGI("settings: language %s, %s, region %s", g_settings.language.c_str(), g_settings.resolution.c_str(),
            g_settings.region.empty() ? "auto" : g_settings.region.c_str());
    sceSystemServiceHideSplashScreen();

    if (g_autoplay.vkTest) {
        bool ok = display::gpu::init();
        if (ok) display::gpu::probe(300);
        XC_LOGI("AUTOPLAY END: vktest %s", ok ? "ran" : "failed");
        for (;;) platform::sleepMs(1000);
    }
    bool haveDisplay = display::init(!g_autoplay.cpuDisplay);
    if (!input::init()) XC_LOGE("controller init failed");

    ui::Fonts fonts;
    if (!fonts.load(platform::assetDir() + "/fonts")) XC_LOGE("fonts missing in %s", platform::assetDir().c_str());
    g_images = std::make_unique<ui::ImageCache>(
        [] {
            if (g_ui) g_ui->invalidate();
        },
        160u << 20, platform::dataDir() + "/imgcache");
    g_ui = std::make_unique<ui::AppUi>(fonts, *g_images);
    startPriceLoop();
    {
        ui::SettingsChoice choice;
        choice.language = static_cast<int>(ui::language());
        choice.resolution = g_settings.resolution == "720p" ? 1 : g_settings.resolution == "1440p" ? 2 : 0;
        choice.region = g_settings.region;
        choice.deadzone = deadzoneIndex(g_settings.deadzone);
        choice.triggerRumble = g_settings.triggerRumble;
        choice.circleConfirms = g_settings.circleConfirms;
        choice.lightBar = g_settings.lightBar;
        g_ui->setAllow1440(allow1440Locked());
        g_ui->setSettings(choice);
        applyControllerSettings();
        g_ui->setRegionLatency(g_settings.regionRtt);
        g_ui->setPrefs(g_settings.hidden, g_settings.librarySort == "az"        ? ui::LibrarySort::AZ
                                          : g_settings.librarySort == "console" ? ui::LibrarySort::Console
                                                                                : ui::LibrarySort::Recent);
    }
    ui::Canvas canvas(display::kWidth, display::kHeight);

    if (!net::initTls(g_autoplay.badCa ? platform::assetDir() + "/missing.pem" : platform::caBundlePath())) {
        // The step that failed, and where the log is: what a bug report needs.
        platform::notify("PSBox: secure connections unavailable");
        g_ui->showError("Secure connections could not be set up: " + net::tlsInitError() + ". Log: " +
                        platform::dataDir() + "/xcloud.log");
        if (g_autoplay.badCa) XC_LOGI("AUTOPLAY END: TLS setup error shown");
    } else {
        std::thread(worker).detach();
    }

    // Never return from main: the app is closed from the home screen.
    input::ControllerState prev{}, pad{};
    Repeater up, down, left, right;
    StreamScreen streamScreen(fonts);  // the game's menu and what is laid over it
    uint64_t padsCheckedAt = 0;
    unsigned padChecks = 0;
    uint64_t lightBarAt = 0;
    bool lightBarSet = false;  // false from the menu/keyboard until the buttons are let go
    for (;;) {
        input::poll(pad);
        autoplayPad(pad);
        uint64_t now = platform::nowMs();
        if (now - lightBarAt >= 100) {  // the light bar follows the game in focus
            lightBarAt = now;
            bool on;
            {
                std::lock_guard<std::mutex> lock(g_settingsMutex);
                on = g_settings.lightBar;
            }
            ui::Color c;
            if (!on) {
                if (lightBarSet) input::resetLightBar(0);
                lightBarSet = false;
            } else if (g_ui->accentColor(c)) {
                input::setLightBar(static_cast<uint8_t>(c), static_cast<uint8_t>(c >> 8), static_cast<uint8_t>(c >> 16));
                lightBarSet = true;
            }
        }
        if (now - padsCheckedAt >= 500) {  // other players signing in or out
            padsCheckedAt = now;
            if (++padChecks % 2 == 0) input::refreshPads();
            // Off the stream nobody reads pads 1..3: poll them for their state.
            if (g_ui->screen() != ui::Screen::Streaming) {
                input::ControllerState ignored;
                for (int i = 1; i < input::kMaxPads; ++i) input::pollPad(i, ignored);
            }
            ui::PadSlots slots;
            for (int i = 0; i < input::kMaxPads; ++i)
                slots[static_cast<size_t>(i)] = {input::padConnected(i), input::padUserName(i)};
            g_ui->setPads(slots);
            streamScreen.setPads(slots);
        }

        ui::NavInput nav;
        nav.up = up.update(pad.dpadUp || pad.leftStickY < -0.6f, now);
        nav.down = down.update(pad.dpadDown || pad.leftStickY > 0.6f, now);
        nav.left = left.update(pad.dpadLeft || pad.leftStickX < -0.6f, now);
        nav.right = right.update(pad.dpadRight || pad.leftStickX > 0.6f, now);
        nav.accept = pad.btnA && !prev.btnA;
        nav.back = pad.btnB && !prev.btnB;
        nav.options = pad.btnOptions && !prev.btnOptions;
        nav.l1 = pad.btnL1 && !prev.btnL1;
        nav.r1 = pad.btnR1 && !prev.btnR1;
        nav.square = pad.btnX && !prev.btnX;    // Square (Xbox X)
        nav.triangle = pad.btnY && !prev.btnY;  // Triangle (Xbox Y)
        nav.r3 = pad.btnR3 && !prev.btnR3;
        nav.l2 = pad.triggerL2 > 0.5f && prev.triggerL2 <= 0.5f;
        nav.r2 = pad.triggerR2 > 0.5f && prev.triggerR2 <= 0.5f;
        nav.touchpad = pad.btnTouchpad;
        nav.nowMs = now;
        bool menuCombo = pad.btnOptions && pad.btnTouchpad && !(prev.btnOptions && prev.btnTouchpad);
        prev = pad;
        autoplayNav(nav, menuCombo, now);
        if (g_ui->screen() == ui::Screen::Streaming) {
            streamScreen.update(pad, nav, menuCombo);
            platform::sleepMs(8);  // ~120 Hz input
            continue;
        }
        streamScreen.idle();

        ui::UiEvent ev = g_ui->handle(nav);
        switch (ev.action) {
            case ui::Action::Play: {
                std::lock_guard<std::mutex> lock(g_argMutex);
                g_playTile = ev.game;
                g_command = kPlay;
                break;
            }
            case ui::Action::SignOut:
                g_cancel = true;
                g_command = kSignOut;
                break;
            case ui::Action::Retry: g_command = kSignIn; break;
            case ui::Action::ConsolesShown: {
                int idle = kNone;
                g_command.compare_exchange_strong(idle, kConsoles);  // not over a pending command
                break;
            }
            case ui::Action::CancelLaunch: g_cancel = true; break;
            case ui::Action::PrefsChanged: {
                std::lock_guard<std::mutex> lock(g_settingsMutex);
                g_settings.hidden = ev.hidden;
                g_settings.librarySort = ev.librarySort == ui::LibrarySort::AZ        ? "az"
                                         : ev.librarySort == ui::LibrarySort::Console ? "console"
                                                                                       : "recent";
                if (!g_settings.save(settingsPath())) XC_LOGW("could not save settings");
                XC_LOGI("prefs saved: %zu hidden, sort %s", g_settings.hidden.size(), g_settings.librarySort.c_str());
                break;
            }
            case ui::Action::SettingsChanged: {
                bool languageChanged;
                {
                    std::lock_guard<std::mutex> lock(g_settingsMutex);
                    std::string code = ui::languageCode(static_cast<ui::Language>(ev.settings.language));
                    languageChanged = code != g_settings.language;
                    g_settings.language = code;
                    g_settings.resolution = ev.settings.resolution == 1   ? "720p"
                                            : ev.settings.resolution == 2 ? "1440p"
                                                                          : "1080p";
                    g_settings.region = ev.settings.region;
                    g_settings.deadzone = ui::kDeadzonePercent[ev.settings.deadzone];
                    g_settings.triggerRumble = ev.settings.triggerRumble;
                    g_settings.circleConfirms = ev.settings.circleConfirms;
                    g_settings.lightBar = ev.settings.lightBar;
                    if (!g_settings.save(settingsPath())) XC_LOGW("could not save settings");
                    XC_LOGI("settings saved: language %s, %s, region %s", code.c_str(), g_settings.resolution.c_str(),
                            g_settings.region.empty() ? "auto" : g_settings.region.c_str());
                }
                applyControllerSettings();
                // Row titles and game details come from the catalog in the
                // chosen language.
                if (languageChanged) g_command = kReloadLibrary;
                break;
            }
            case ui::Action::None: break;
        }

        autoplayScreens(canvas, now);

        if (haveDisplay && g_ui->needsRedraw(now)) {
            uint64_t t0 = platform::nowMs();
            g_ui->render(canvas, now);
            uint64_t t1 = platform::nowMs();
            static int logged = 0;
            if (logged < 5 && g_ui->screen() == ui::Screen::Home) {
                ++logged;
                XC_LOGI("ui render %llu ms", static_cast<unsigned long long>(t1 - t0));
            }
            std::lock_guard<std::mutex> lock(display::frameMutex());
            if (g_ui->screen() != ui::Screen::Streaming) {
                display::drawRgba(canvas.data());
                display::present();  // paced by the next draw waiting for a free buffer
            }
        } else {
            platform::sleepMs(8);
        }
    }
}
