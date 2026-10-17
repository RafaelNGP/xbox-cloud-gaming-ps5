// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
// The app every platform runs (app_core.h): coreStartup() sets things up,
// then each coreFrame() reads the pad, drives the UI (ui::AppUi) and draws
// it, on the main thread. The rest lives beside it: worker.cpp (all the
// network work, on its own thread), stream_screen.cpp (the game's menu,
// gestures and controllers while streaming), price_loop.cpp (store prices)
// and autoplay.cpp (the unattended tests). What only some platforms do is
// behind app/shell.h.
#include "app/app_core.h"

#include "app/app_state.h"
#include "app/autoplay.h"
#include "app/crash_guard.h"
#include "app/report.h"
#include "app/settings.h"
#include "app/shell.h"
#include "app/stream_screen.h"
#include "display/display.h"
#include "input/controller.h"
#include "input/kbm.h"
#include "input/keyboard_nav.h"
#include "ui/input_mode.h"
#include "input/tuning.h"
#include "net/http.h"
#include "platform/platform.h"
#include "ui/app_ui.h"
#include "ui/strings.h"
#include "util/log.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <optional>
#include <sys/stat.h>
#include <mutex>
#include <string>
#include <thread>
#include <utility>

namespace xc::app {

namespace {

std::unique_ptr<ui::ImageCache> g_images;
crash::Previous g_lastCrash;
std::string crashMarkerPath() { return platform::dataDir() + "/crash.marker"; }

// "Report a problem": the bug form's link, filled in, as a QR code. `what`
// is the error shown (empty from Settings); `previousSession` takes the log
// of the session that went down instead of this one.
void openReport(const std::string& what, bool previousSession, const std::string& heading, const std::string& crash = {}) {
    std::string logText;
    platform::readFile(platform::dataDir() + "/xcloud.log", logText);
    std::vector<std::string> privateWords = {g_ui->gamertag()};
    for (int i = 0; i < input::kMaxPads; ++i) privateWords.push_back(input::padUserName(i));
    for (auto& name : g_ui->consoleNames()) privateWords.push_back(std::move(name));
    report::Info info;
    info.what = report::sanitize(what.empty() ? "Reported from Settings" : what, privateWords);
    info.title = what.empty() ? "Problem report" : report::shorten(info.what, 70);
    info.release = XC_APP_VERSION;
    info.firmware = platform::firmwareVersion();
    info.log = report::sanitize(report::logExcerpt(logText, previousSession), privateWords);
    info.crash = report::sanitize(crash, privateWords);
    std::string url = report::issueUrl(info);
    std::string detail = info.crash;
    if (!detail.empty() && !info.release.empty()) detail += " (v" + info.release + ")";
    XC_LOGI("report: QR code shown (%zu characters)%s%s", url.size(), detail.empty() ? "" : " — ", detail.c_str());
    g_ui->showReport(url, heading, detail);
}

// Dead zone, trigger vibration and confirm button from g_settings.
void applyControllerSettings() {
    std::lock_guard<std::mutex> lock(g_settingsMutex);
    input::setDeadzone(g_settings.deadzoneLeft / 100.0f, g_settings.deadzoneRight / 100.0f);
    input::setTriggerFeel(g_settings.triggerStrength, g_settings.triggerHz, g_settings.triggerResistance, g_settings.triggerPulses);
    input::setCircleConfirms(g_settings.circleConfirms);
    if (g_ui) g_ui->setCircleConfirms(g_settings.circleConfirms);
}

// Saves what Settings edits (and applies the controller's part); true when
// the language changed.
bool saveSettings(const ui::SettingsChoice& choice) {
    bool languageChanged;
    {
        std::lock_guard<std::mutex> lock(g_settingsMutex);
        std::string code = ui::languageCode(static_cast<ui::Language>(choice.language));
        languageChanged = code != g_settings.language;
        g_settings.language = code;
        g_settings.resolution = choice.resolution == 1 ? "720p" : choice.resolution == 2 ? "best" : "1080p";
        g_settings.region = choice.region;
        g_settings.loginCountry = choice.loginCountry;
        g_settings.deadzoneLeft = choice.deadzone[0];
        g_settings.deadzoneRight = choice.deadzone[1];
        g_settings.triggerStrength = choice.triggerStrength;
        g_settings.triggerHz = choice.triggerHz;
        g_settings.triggerResistance = choice.triggerResistance;
        g_settings.triggerPulses = choice.triggerPulses;
        g_settings.circleConfirms = choice.circleConfirms;
        g_settings.lightBarMode = choice.lightBarMode;
        g_settings.lightBarColour = choice.lightBarColour;
        g_settings.sharpness = choice.sharpness;
        g_settings.deband = choice.deband;
        g_settings.upscaler = choice.upscaler;
        g_settings.highRefresh = choice.highRefresh;
        g_settings.maxBitrateMbps = choice.maxBitrateMbps;
        g_settings.gameVolume = choice.gameVolume;
        g_settings.keyMap = choice.keyMap;
        g_settings.save(settingsPath());  // warns itself
        XC_LOGI("settings saved: language %s, %s, region %s", code.c_str(), g_settings.resolution.c_str(),
                g_settings.region.empty() ? "auto" : g_settings.region.c_str());
    }
    applyControllerSettings();
    return languageChanged;
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

// What main() kept on its stack when it ran the loop itself: made by
// coreStartup(), carried from one pass to the next.
struct Core {
    explicit Core(bool withDisplay) : haveDisplay(withDisplay) {}
    bool frame();

    // The slot showing the keyboard (-1 none): the keyboard's player, while
    // the keyboard was used after that player's own controller (or it has none).
    int keyboardIconSlot() const {
        if (!kbmConnected) return -1;
        int player;
        {
            std::lock_guard<std::mutex> lock(g_settingsMutex);
            player = g_settings.keyboardPlayer;
        }
        if (player < 0 || player >= input::kMaxPads) return -1;
        bool keyboardLast = !input::padConnected(player) || keyboardActiveAt > input::padActiveAt(player);
        return keyboardLast ? player : -1;
    }
    static uint64_t now0() { return platform::nowMs(); }

    const bool haveDisplay;
    ui::Fonts fonts;                            // the UI keeps a reference: lives as long as the app
    std::optional<ui::Canvas> canvas;           // made once the UI has its settings
    std::optional<StreamScreen> streamScreen;   // the game's menu and what is laid over it; made once the worker runs
    input::ControllerState prev{}, pad{};
    bool appMenuKeyBefore = false, homeKeyBefore = false;  // F12 / Home held at the last pass
    uint8_t mouseButtonsBefore = 0;      // the mouse buttons at the last pass (a press chooses a key)
    std::vector<uint8_t> swallowedKeys;  // keys pressed to be chosen, kept from the screens until let go
    bool kbmConnected = false;                              // a USB keyboard or mouse in use
    uint64_t keyboardActiveAt = 0;                          // when a key or the mouse was last used (ms)
    int keyboardIconShown = -2;  // as last drawn in the players' row
    Repeater up, down, left, right, dpadLeft, dpadRight;
    // Smooth scrolling: rows / cards still to move (fractions carry over),
    // the touchpad's last point, and its speed for the glide after release.
    struct {
        float rows = 0, cards = 0;
        bool touching = false;
        float x = 0, y = 0;
        float vRows = 0, vCards = 0;  // per second
        uint64_t lastMs = 0;
    } scroll;
    uint64_t padsCheckedAt = 0;
    unsigned padChecks = 0;
    uint64_t lightBarAt = 0;
    bool lightBarSet = false;  // false from the menu/keyboard until the buttons are let go
    ui::Screen stageScreen = ui::Screen::Splash;
};
std::unique_ptr<Core> g_core;

}  // namespace

bool coreStartup() {
    platform::init();
    log::setFile((platform::dataDir() + "/xcloud.log").c_str());
    if (!platform::dataDirNote().empty())
        XC_LOGI("data folder %s: %s", platform::dataDir().c_str(), platform::dataDirNote().c_str());
    XC_LOGI("=== PSBox Cloud Gaming starting ===");
    XC_LOGI("firmware %s, PSBox %s", platform::firmwareVersion().c_str(), XC_APP_VERSION);
    // Did the last session go down? (A crash leaves a marker; being closed
    // from the system's menu doesn't.) Offered as a report once signed in.
    g_lastCrash = crash::takePrevious(crashMarkerPath());
    if (g_lastCrash.found)
        XC_LOGW("the last session went down: %s (%s)%s%s", g_lastCrash.kind.c_str(), g_lastCrash.stage.c_str(),
                g_lastCrash.crash.empty() ? "" : " — ", g_lastCrash.crash.c_str());
    crash::install(crashMarkerPath());
    loadAutoplay();
    shell::beforeSettings();  // the PS5: an update cut short rolled back, a store update's files brought back
    struct stat settingsStat;
    if (!g_settings.load(settingsPath()) && ::stat(settingsPath().c_str(), &settingsStat) == 0) {
        // Unreadable (cut short?): kept aside, not overwritten by the defaults.
        std::string bad = settingsPath() + ".bad";
        std::rename(settingsPath().c_str(), bad.c_str());
        XC_LOGW("settings: %s could not be read; defaults used, the file kept as %s", settingsPath().c_str(), bad.c_str());
    }
    ui::setLanguage(ui::languageFromCode(g_settings.language));
    XC_LOGI("settings: language %s, %s, region %s", g_settings.language.c_str(), g_settings.resolution.c_str(),
            g_settings.region.empty() ? "auto" : g_settings.region.c_str());
    shell::beforeDisplay(g_settings);  // the PS5: the splash screen hidden, the video mode (120 Hz) settled

    if (g_autoplay.vkTest) {
        bool ok = display::bringUpTest(300);
        XC_LOGI("AUTOPLAY END: vktest %s", ok ? "ran" : "failed");
        while (shell::pumpEvents()) platform::sleepMs(1000);  // the PS5 stays here, as it never quits
        return false;
    }
    bool haveDisplay = display::init(!g_autoplay.cpuDisplay);
    if (!input::init()) XC_LOGE("controller init failed");
    input::kbmInit(input::padUserId(0));  // USB keyboards and mice (logs what it found)

    g_core = std::make_unique<Core>(haveDisplay);
    ui::Fonts& fonts = g_core->fonts;
    if (!fonts.load(platform::assetDir() + "/fonts")) XC_LOGE("fonts missing in %s", platform::assetDir().c_str());
    {
        // The console's icon font (keyboard and mouse...): read where the
        // system keeps it, not shipped with the app.
        bool ok = fonts.loadIcons(shell::iconFontPaths());
        XC_LOGI("system icon font: %s", ok ? "loaded" : "not found (drawn icons)");
    }
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
        choice.resolution = g_settings.resolution == "720p" ? 1 : (g_settings.resolution == "1440p" || g_settings.resolution == "best") ? 2 : 0;
        choice.region = g_settings.region;
        choice.loginCountry = g_settings.loginCountry;
        choice.deadzone[0] = std::clamp(g_settings.deadzoneLeft, 0, ui::kMaxDeadzone);
        choice.deadzone[1] = std::clamp(g_settings.deadzoneRight, 0, ui::kMaxDeadzone);
        choice.triggerStrength = g_settings.triggerStrength;
        choice.triggerHz = g_settings.triggerHz;
        choice.triggerResistance = g_settings.triggerResistance;
        choice.triggerPulses = g_settings.triggerPulses;
        choice.circleConfirms = g_settings.circleConfirms;
        choice.lightBarMode = g_settings.lightBarMode;
        choice.lightBarColour = g_settings.lightBarColour;
        choice.sharpness = g_settings.sharpness;
        choice.deband = g_settings.deband;
        choice.upscaler = g_settings.upscaler;
        choice.highRefresh = g_settings.highRefresh;
        choice.maxBitrateMbps = g_settings.maxBitrateMbps;
        choice.gameVolume = g_settings.gameVolume;
        choice.keyMap = g_settings.keyMap;
        g_ui->setAllow1440(allow1440Locked());
        g_ui->setUpdateState(XC_APP_VERSION, {}, false);  // Settings shows this version before any check
        g_ui->setSettings(choice);
        g_ui->setPerGameSettings(g_settings.perGame);
        g_ui->setGyroDefaults(g_settings.gyroMode, g_settings.gyroLevel);
        applyControllerSettings();
        g_ui->setRegionLatency(g_settings.regionRtt);
        g_ui->setPrefs(g_settings.hidden, g_settings.librarySort == "az"        ? ui::LibrarySort::AZ
                                          : g_settings.librarySort == "console" ? ui::LibrarySort::Console
                                                                                : ui::LibrarySort::Recent);
    }
    g_core->canvas.emplace(display::kWidth, display::kHeight);

    if (!net::initTls(g_autoplay.badCa ? platform::assetDir() + "/missing.pem" : platform::caBundlePath())) {
        // The step that failed, and where the log is: what a bug report needs.
        platform::notify("PSBox: secure connections unavailable");
        g_ui->showError("Secure connections could not be set up: " + net::tlsInitError() + ". Log: " +
                        platform::dataDir() + "/xcloud.log");
        if (g_autoplay.badCa) XC_LOGI("AUTOPLAY END: TLS setup error shown");
    } else {
        if (g_autoplay.badCa) XC_LOGI("AUTOPLAY END: missing CA bundle, built-in copy %s", net::usingBuiltinCa() ? "used" : "NOT used");
        if (g_autoplay.testCa) net::addTrustedCa(platform::dataDir() + "/test-ca.pem");
        std::thread(worker).detach();
    }
    g_core->streamScreen.emplace(fonts);
    crash::startWatchdog(15000);  // a main loop stuck for 15 s leaves a marker
    return true;
}

bool Core::frame() {
    if (!shell::pumpEvents()) return false;
    crash::heartbeat();
    if (ui::Screen sc = g_ui->screen(); sc != stageScreen) {
        // What the app is doing, for a crash marker (the worker names
        // the stream itself).
        stageScreen = sc;
        static const char* const kNames[] = {"splash", "sign-in", "home", "game page", "launching",
                                             "streaming", "error", "settings", "updating"};
        size_t i = static_cast<size_t>(sc);
        if (sc != ui::Screen::Streaming && sc != ui::Screen::Launching && i < std::size(kNames)) crash::setStage(kNames[i]);
        // After a crash: its report, once the app is up again.
        if (g_lastCrash.found && !g_autoplay.crashTest && (sc == ui::Screen::Home || sc == ui::Screen::Error)) {
            g_lastCrash.found = false;
            openReport("Closed unexpectedly: " + g_lastCrash.kind + " while " + g_lastCrash.stage, true,
                       ui::tr(ui::Str::CrashedLastTime), g_lastCrash.crash);
        }
    }
    // A Microsoft service refused for the console's clock: said once, even
    // if signing in worked (only some content then fails to load). Only
    // for refusals while the app is in use: those of the sign-in (its
    // error already says it, and the date may be fixed before Retry) are
    // left behind; prices and images ask again on the home screen.
    static bool clockTold = false;
    static unsigned clockSeen = 0;
    if (!clockTold && net::clockRefusals() != clockSeen) {
        clockSeen = net::clockRefusals();
        ui::Screen now = g_ui->screen();
        if (now == ui::Screen::Home || now == ui::Screen::Details || now == ui::Screen::Settings) {
            clockTold = true;
            platform::notify(ui::trf(ui::Str::ClockWrongNotice, net::todayText()), "the console's clock looks wrong (notification)");
        }
    }
    // After an update: the new version, started in place of this one.
    if (g_restartWanted) shell::restartOrExit();
    // Gyro aiming moves the aim in a game, not the app's menu.
    input::setGyroActive(g_ui->screen() == ui::Screen::Streaming && !streamScreen->menuOpen());
    input::poll(pad);
    autoplayPad(pad);
    // USB keyboard: the app's screens (and its menu in a game) as with the
    // controller; in a game otherwise, its keys and the mouse go to the game.
    input::KbmInput kbm = input::kbmPoll();
    bool inGame = g_ui->screen() == ui::Screen::Streaming;
    {
        // The hints follow the device last used: a key or the mouse
        // switches them to the keyboard's keys, the controller back.
        bool keyPressed = std::any_of(kbm.keys.begin(), kbm.keys.end(), [](const input::KeyEvent& k) { return k.pressed; });
        bool padUsed = pad.btnA || pad.btnB || pad.btnX || pad.btnY || pad.dpadUp || pad.dpadDown || pad.dpadLeft ||
                       pad.dpadRight || pad.btnL1 || pad.btnR1 || pad.btnOptions || pad.btnTouchpad ||
                       std::abs(pad.leftStickX) > 0.5f || std::abs(pad.leftStickY) > 0.5f ||
                       std::abs(pad.rightStickX) > 0.5f || std::abs(pad.rightStickY) > 0.5f;
        bool keyboard = ui::g_keyboardHints;
        if (keyPressed || kbm.moved) keyboard = true, keyboardActiveAt = now0();
        else if (padUsed) keyboard = false;
        if (keyboard != ui::g_keyboardHints) {
            ui::g_keyboardHints = keyboard;
            g_ui->invalidate();
            streamScreen->redrawMenu();
        }
    }
    // Keys 1-4 (off a game, or with the app's menu open in one): the
    // player the keyboard and mouse play as. 1 native with player 1;
    // 2-4 that player's controller (3 or 4 with two players: just its slot).
    // The keyboard keys' screen waiting for a key: the next key or mouse
    // button pressed is the one chosen (not a move through the screens);
    // keys pressed for it stay out of the screens until let go.
    bool capturing = g_ui->capturingKey();
    if (capturing) {
        for (const auto& k : kbm.keys)
            if (k.pressed) g_ui->captureKey(k.vk), swallowedKeys.push_back(k.vk);
        for (int b = 0; b < 5; ++b)
            if ((kbm.buttons & ~mouseButtonsBefore) & (1 << b)) g_ui->captureKey(static_cast<uint8_t>(input::kMouseLeft + b));
    }
    mouseButtonsBefore = kbm.buttons;
    std::erase_if(swallowedKeys, [&](uint8_t vk) { return !std::binary_search(kbm.held.begin(), kbm.held.end(), vk); });
    if ((!inGame || streamScreen->menuOpen()) && !capturing) {
        for (const auto& k : kbm.keys) {
            if (!k.pressed || k.vk < '1' || k.vk > '4') continue;
            int player = k.vk - '1';
            {
                std::lock_guard<std::mutex> lock(g_settingsMutex);
                g_settings.keyboardPlayer = player;
                g_settings.save(settingsPath());
            }
            platform::notify(ui::trf(ui::Str::KeyboardPlayer, std::to_string(player + 1)),
                             "keyboard: player " + std::to_string(player + 1));
            padsCheckedAt = 0;  // the players' row at once
        }
    }
    // Home: the Xbox button in a game (as the touchpad swiped up).
    bool homeKey = std::binary_search(kbm.held.begin(), kbm.held.end(), uint8_t{0x24});
    if (inGame && homeKey && !homeKeyBefore && !streamScreen->menuOpen()) g_xboxButtonUntil = now0() + 250;
    homeKeyBefore = homeKey;
    bool appMenuKey = std::binary_search(kbm.held.begin(), kbm.held.end(), input::kVkAppMenu);
    bool appMenuPressed = appMenuKey && !appMenuKeyBefore;
    appMenuKeyBefore = appMenuKey;
    if ((!inGame || streamScreen->menuOpen()) && !capturing) {
        std::vector<uint8_t> navKeys;
        for (uint8_t vk : kbm.held)
            if (std::find(swallowedKeys.begin(), swallowedKeys.end(), vk) == swallowedKeys.end()) navKeys.push_back(vk);
        input::keyboardToPad(navKeys, pad);
    }
    kbmConnected = kbm.keyboard || kbm.mouse || g_autoplay.kbHints;
    if (int shown = keyboardIconSlot(); shown != keyboardIconShown) {
        keyboardIconShown = shown;
        padsCheckedAt = 0;  // the players' row at once, as the device in use changes
    }
    streamScreen->setKbm(std::move(kbm));
    {
        // The triggers' resistance: in a game and in its Settings tester only.
        float l2, r2;
        input::setTriggerResistanceActive(g_ui->screen() == ui::Screen::Streaming || g_ui->triggerTest(l2, r2));
    }
    uint64_t now = platform::nowMs();
    if (now - lightBarAt >= 50) {  // the light bar: the game in focus, the user's colour or off
        lightBarAt = now;
        int mode;
        ui::Color c;
        if (!g_ui->settingsLightBar(mode, c)) {  // Settings shows a choice at once, before it is saved
            std::lock_guard<std::mutex> lock(g_settingsMutex);
            mode = g_settings.lightBarMode;
            c = g_settings.lightBarColour;
        }
        if (mode == 2) {
            if (lightBarSet) input::resetLightBar(0);
            lightBarSet = false;
        } else if (mode == 1 || g_ui->accentColor(c)) {
            static ui::Color logged = 0;
            if (!g_autoplay.title.empty() && c != logged) XC_LOGI("light bar: #%06X", c & 0xFFFFFF), logged = c;
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
        // The keyboard: in a slot of its own as another player's
        // controller (Settings), else on player 1's while it's the device in use.
        if (int kb = keyboardIconSlot(); kb >= 0) {
            slots[static_cast<size_t>(kb)].keyboard = true;
            slots[static_cast<size_t>(kb)].connected = true;
        }
        g_ui->setPads(slots);
        streamScreen->setPads(slots);
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
    nav.stickX = pad.leftStickX;
    nav.stickY = pad.leftStickY;
    nav.dpadLeft = dpadLeft.update(pad.dpadLeft, now);
    nav.dpadRight = dpadRight.update(pad.dpadRight, now);
    nav.rawLX = pad.rawLeftX, nav.rawLY = pad.rawLeftY, nav.rawRX = pad.rawRightX, nav.rawRY = pad.rawRightY;
    nav.l2Analog = pad.triggerL2, nav.r2Analog = pad.triggerR2;
    // Smooth scrolling. The right stick: a speed that follows how far it
    // is pushed (up to ~12 rows / 16 cards a second). The touchpad,
    // touched (not pressed): the list follows the finger (its height ~6
    // rows, its width ~8 cards; the finger up scrolls up), then glides on
    // and slows down after release, like two fingers on a laptop's.
    {
        float dt = scroll.lastMs ? std::min(0.1f, (now - scroll.lastMs) / 1000.0f) : 0.0f;
        scroll.lastMs = now;
        auto speed = [](float v, float top) {
            float m = (std::abs(v) - 0.15f) / 0.85f;
            return m <= 0 ? 0.0f : (v < 0 ? -1.0f : 1.0f) * top * m * m;
        };
        scroll.rows += speed(pad.rightStickY, 12) * dt;
        scroll.cards += speed(pad.rightStickX, 16) * dt;
        constexpr float kRowsPerPad = 6, kCardsPerPad = 8;
        bool touch = pad.touching && !pad.btnTouchpad && g_ui->screen() == ui::Screen::Home;
        if (touch && scroll.touching) {
            float dr = (pad.touchY - scroll.y) * kRowsPerPad, dc = (pad.touchX - scroll.x) * kCardsPerPad;
            scroll.rows += dr;
            scroll.cards += dc;
            if (dt > 0) {  // the finger's speed, smoothed
                scroll.vRows = 0.6f * scroll.vRows + 0.4f * dr / dt;
                scroll.vCards = 0.6f * scroll.vCards + 0.4f * dc / dt;
            }
        } else if (!touch && dt > 0) {  // the glide, slowing down
            scroll.rows += scroll.vRows * dt;
            scroll.cards += scroll.vCards * dt;
            float decay = std::exp(-4.0f * dt);
            scroll.vRows *= decay;
            scroll.vCards *= decay;
            if (std::abs(scroll.vRows) < 0.8f) scroll.vRows = 0;
            if (std::abs(scroll.vCards) < 0.8f) scroll.vCards = 0;
        }
        if (touch && !scroll.touching) scroll.vRows = scroll.vCards = 0;  // a new touch stops a glide
        scroll.touching = touch;
        scroll.x = pad.touchX, scroll.y = pad.touchY;
        if (g_ui->screen() != ui::Screen::Home) scroll.rows = scroll.cards = scroll.vRows = scroll.vCards = 0;
        nav.scrollRows = static_cast<int>(scroll.rows);
        nav.scrollCards = static_cast<int>(scroll.cards);
        scroll.rows -= static_cast<float>(nav.scrollRows);
        scroll.cards -= static_cast<float>(nav.scrollCards);
    }
    nav.nowMs = now;
    bool menuCombo = (pad.btnOptions && pad.btnTouchpad && !(prev.btnOptions && prev.btnTouchpad)) ||
                     (inGame && appMenuPressed);
    prev = pad;
    autoplayNav(nav, menuCombo, now);
    if (g_ui->screen() == ui::Screen::Streaming) {
        streamScreen->update(pad, nav, menuCombo);
        platform::sleepMs(8);  // ~120 Hz input
        return true;
    }
    streamScreen->idle();

    ui::UiEvent ev = g_ui->handle(nav);
    int strength, hz, resistance;
    bool pulses;
    if (g_ui->settingsTriggerFeel(strength, hz, resistance, pulses)) {
        // Settings: the triggers' feel as chosen there, at once; their
        // tester makes them vibrate as far as they are pressed.
        input::setTriggerFeel(strength, hz, resistance, pulses);
        float l2, r2;
        static bool testing = false;
        input::setTriggerLogging(g_ui->triggerTest(l2, r2));
        if (g_ui->triggerTest(l2, r2)) {
            input::setTriggerRumble(static_cast<uint8_t>(std::lround(l2 * 255)), static_cast<uint8_t>(std::lround(r2 * 255)), 500);
            testing = true;
        } else if (testing) {
            input::setTriggerRumble(0, 0, 0);
            testing = false;
        }
    }
    if (bool circle = g_ui->circleConfirms(); circle != input::circleConfirms()) {
        // The confirm button just changed in Settings: the pad follows at
        // once, and a button still held from choosing it isn't a new press.
        input::setCircleConfirms(circle);
        std::swap(prev.btnA, prev.btnB);
        XC_LOGI("confirm button: %s", circle ? "circle" : "cross");
    }
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
            g_settings.save(settingsPath());  // warns itself
            XC_LOGI("prefs saved: %zu hidden, sort %s", g_settings.hidden.size(), g_settings.librarySort.c_str());
            break;
        }
        case ui::Action::GameSettingsChanged: {
            std::lock_guard<std::mutex> lock(g_settingsMutex);
            std::string key = app::gameProfileKey(ev.game.productId, ev.game.titleId);
            if (ev.hasGameProfile) {
                g_settings.perGame[key] = ev.gameProfile;
                XC_LOGI("per-game profile saved for %s (%s)", ev.game.name.c_str(), key.c_str());
            } else {
                g_settings.perGame.erase(key);
                XC_LOGI("per-game profile reset to default for %s (%s)", ev.game.name.c_str(), key.c_str());
            }
            g_settings.save(settingsPath());  // warns itself
            break;
        }
        case ui::Action::SettingsChanged:
            // Row titles and game details come from the catalog in the
            // chosen language.
            if (saveSettings(ev.settings)) g_command = kReloadLibrary;
            break;
        case ui::Action::UpdateNow: {
            if (g_ui->screen() == ui::Screen::Settings) saveSettings(ev.settings);
            int idle = kNone;
            if (!g_command.compare_exchange_strong(idle, kUpdate)) XC_LOGW("update: the worker is busy");
            break;
        }
        case ui::Action::UpdateLater: skipOfferedUpdate(); break;
        case ui::Action::LoginCountry:
            // A new sign-in, as the country chosen (or the console's own).
            saveSettings(ev.settings);
            XC_LOGI("sign-in country: %s", ev.settings.loginCountry.empty() ? "off" : ev.settings.loginCountry.c_str());
            g_command = kSignIn;
            break;
        case ui::Action::Report: openReport(ev.reportWhat, false, ui::tr(ui::Str::ReportProblem)); break;
        case ui::Action::None: break;
    }

    autoplayScreens(*canvas, now);

    if (haveDisplay && g_ui->needsRedraw(now)) {
        uint64_t t0 = platform::nowMs();
        g_ui->render(*canvas, now);
        uint64_t t1 = platform::nowMs();
        static int logged = 0;
        if (logged < 5 && g_ui->screen() == ui::Screen::Home) {
            ++logged;
            XC_LOGI("ui render %llu ms", static_cast<unsigned long long>(t1 - t0));
        }
        std::lock_guard<std::mutex> lock(display::frameMutex());
        if (g_ui->screen() != ui::Screen::Streaming) {
            display::drawRgba(canvas->data());
            display::present();  // paced by the next draw waiting for a free buffer
        }
    } else {
        platform::sleepMs(8);
    }
    return true;
}

bool coreFrame() { return g_core->frame(); }

void coreShutdown() {
    // Not on the PS5, whose loop never ends. The worker isn't stopped (it
    // may be waiting on the network): the caller leaves with _exit(). The
    // desktop app stops it and the stream first (docs/multiplatform.md, M1).
    XC_LOGI("=== PSBox Cloud Gaming closing ===");
    input::shutdown();
    display::shutdown();
    platform::shutdown();
}

int runApp() {
    if (!coreStartup()) return 1;
    while (coreFrame()) {
    }
    coreShutdown();
    return 0;
}

}  // namespace xc::app
