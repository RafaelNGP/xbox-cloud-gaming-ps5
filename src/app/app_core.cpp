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
#include "app/library.h"
#include "app/report.h"
#include "app/settings.h"
#include "app/shell.h"
#include "app/stream_player.h"
#include "app/stream_screen.h"
#include "app/web_server.h"
#include "display/display.h"
#include "input/controller.h"
#include "input/input_ext.h"
#include "input/kbm.h"
#include "input/keyboard_nav.h"
#include "input/pad_map.h"
#include "input/pad_usb.h"
#include "ui/input_mode.h"
#include "ui/pad_glyphs.h"
#include "input/tuning.h"
#include "net/http.h"
#include "media/audio_out.h"
#include "media/audio_queue.h"
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
    if (logText.empty()) {
        logText = log::recentLogs();
    }
    std::vector<std::string> privateWords = {g_ui->gamertag()};
    for (int i = 0; i < input::kMaxPads; ++i) privateWords.push_back(input::padUserName(i));
    for (auto& name : g_ui->consoleNames()) privateWords.push_back(std::move(name));
    report::Info info;
    std::string whatText = what.empty() ? "Reported from Settings" : what;
    if (!platform::dataDirWritable() || !platform::dataDirNote().empty()) {
        whatText += " [storage=" + platform::dataDir() + " writable=" + (platform::dataDirWritable() ? "yes" : "no");
        if (!platform::dataDirNote().empty()) whatText += " note=" + platform::dataDirNote();
        whatText += "]";
    }
    info.what = report::sanitize(whatText, privateWords);
    info.title = what.empty() ? "Problem report" : report::shorten(info.what, 70);
    info.release = XC_APP_VERSION;
    info.firmware = platform::firmwareVersion();
    info.platform = platform::platformName();
    std::string platHeader = "[Platform: " + info.platform + "]\n";
    info.log = platHeader + report::sanitize(report::logExcerpt(logText, previousSession), privateWords);
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
    media::setAudioHapticsLevel(g_settings.audioHaptics);
    if (g_ui) g_ui->setCircleConfirms(g_settings.circleConfirms);
}

// Saves what Settings edits (and applies the controller's part); true when
// the language changed.
bool saveSettings(const ui::SettingsChoice& choice) {
    bool languageChanged, fullscreenChanged;
    bool queueAlertToggledOn = false;
    {
        std::lock_guard<std::mutex> lock(g_settingsMutex);
        std::string language = g_settings.language;
        bool fullscreen = g_settings.desktopFullscreen;
        bool prevQueue = g_settings.queueReadyAlert;
        ui::applyChoice(choice, g_settings);
        queueAlertToggledOn = !prevQueue && g_settings.queueReadyAlert;
        languageChanged = g_settings.language != language;
        fullscreenChanged = g_settings.desktopFullscreen != fullscreen;
        g_settings.save(settingsPath());  // warns itself
        XC_LOGI("settings saved: language %s, %s, region %s", g_settings.language.c_str(), g_settings.resolution.c_str(),
                g_settings.region.empty() ? "auto" : g_settings.region.c_str());
    }
    applyControllerSettings();
    if (queueAlertToggledOn) {
        XC_LOGI("settings: queue-ready alert toggled on; playing test chime and rumble");
        playQueueReadyAlert();
    }
    if (g_ui) g_ui->setGyroDefaults(choice.gyroMode, choice.gyroLevel);
    if (fullscreenChanged) shell::setFullscreen(choice.fullscreen);  // a desktop's window (main thread)
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
    bool quitAsked_ = false;  // desktop: "Close PSBox?" confirmed on the home screen
    explicit Core(bool withDisplay) : haveDisplay(withDisplay) {}
    bool frame();
    void dispatchUiAction(const ui::UiEvent& ev);

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
        uint64_t padT = input::padConnected(player) ? input::padActiveAt(player) : 0;
        uint64_t phoneT = virtualGamepadActiveAt(player);
        bool keyboardLast = (keyboardActiveAt > padT && keyboardActiveAt > phoneT) ||
                            (!input::padConnected(player) && phoneT == 0);
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
    std::array<uint64_t, input::kMaxPads> padActiveBefore_{};
    std::array<uint64_t, input::kMaxPads> vpadActiveBefore_{};
    uint64_t kbActiveBefore_ = 0;
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

bool saveSettingsChoice(const ui::SettingsChoice& choice) { return saveSettings(choice); }

// What the screens offer (ui::UiCaps): the pads' features (input_ext.h),
// the display's and the platform's. The PS5 offers every option as it
// always has: its pads are DualSense, and what its display can't honour at
// the moment does nothing there, as before (an upscaler on the CPU
// fallback; 120 Hz then decided at start-up).
ui::UiCaps uiCaps() {
    const shell::Caps sc = shell::caps();
    const display::DisplayCaps dc = display::caps();
    const input::PadCaps pc = input::anyPadCaps();
    ui::UiCaps c;
    c.lightBar = pc.lightBar;
    c.triggerRumble = pc.triggerRumble;
    c.adaptiveTriggers = pc.adaptiveTriggers;
    c.gyro = pc.gyro;
    c.touchpad = pc.touchpad;
    c.upscalers = sc.console || dc.upscalers;
    c.sharpen = sc.console || dc.sharpen;
    c.deband = sc.console || dc.deband;
    c.highRefresh = sc.console || dc.highRefreshSwitch;
    c.console = sc.console;
    c.openUrl = sc.openUrl;
    c.selfUpdate = sc.selfUpdate;
    c.desktopWindow = sc.window;
    return c;
}

bool coreStartup() {
    platform::init();
    platform::setThreadAffinity(platform::ThreadRole::Main, "xcloud_main");
    bool logOk = log::setFile((platform::dataDir() + "/xcloud.log").c_str());
    if (!platform::dataDirNote().empty())
        XC_LOGI("data folder %s: %s", platform::dataDir().c_str(), platform::dataDirNote().c_str());
    if (!logOk)
        XC_LOGW("could not open xcloud.log in %s: log kept in memory", platform::dataDir().c_str());
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
    guardSettingsForAutoplay(settingsPath());  // the tests never leave the user's settings changed
    struct stat settingsStat;
    if (!g_settings.load(settingsPath()) && ::stat(settingsPath().c_str(), &settingsStat) == 0) {
        // Unreadable (cut short?): kept aside, not overwritten by the defaults.
        std::string bad = settingsPath() + ".bad";
        std::rename(settingsPath().c_str(), bad.c_str());
        XC_LOGW("settings: %s could not be read; defaults used, the file kept as %s", settingsPath().c_str(), bad.c_str());
    }
    ui::setLanguage(ui::languageFromCode(g_settings.language));
    media::setPadSpeakerVolume(g_settings.padSpeakerVolume);
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
    if (!shell::afterDisplay(haveDisplay)) return false;  // the desktop: its window to the input backend
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
    g_ui->showSplash(ui::tr(ui::Str::SigningIn));
    startPriceLoop();
    setWebServerHooks({
        /*getSettings=*/[](json::Value& root) {
            std::lock_guard<std::mutex> lock(g_settingsMutex);
            root.set("settingsVersion", static_cast<int64_t>(g_settingsVersion.load()));
            root.set("language", g_settings.language);
            root.set("resolution", g_settings.resolution);
            root.set("maxBitrateMbps", g_settings.maxBitrateMbps);
            root.set("highRefresh", g_settings.highRefresh);
            root.set("deadzoneLeft", g_settings.deadzoneLeft);
            root.set("deadzoneRight", g_settings.deadzoneRight);
            root.set("lightBarMode", g_settings.lightBarMode);
            char hex[8];
            std::snprintf(hex, sizeof hex, "#%02X%02X%02X", static_cast<unsigned>(g_settings.lightBarColour & 0xFF),
                          static_cast<unsigned>(g_settings.lightBarColour >> 8 & 0xFF), static_cast<unsigned>(g_settings.lightBarColour >> 16 & 0xFF));
            root.set("lightBarColour", std::string(hex));
            root.set("triggerStrength", g_settings.triggerStrength);
            root.set("audioHaptics", g_settings.audioHaptics);
            root.set("queueReadyAlert", g_settings.queueReadyAlert);
            root.set("videoDecoder", g_settings.videoDecoder);
            root.set("voiceRouting", g_settings.voiceRouting);
            root.set("padSpeakerVolume", g_settings.padSpeakerVolume);
            root.set("sharpness", g_settings.sharpness);
            root.set("upscaler", g_settings.upscaler);
            root.set("deband", g_settings.deband);
            root.set("triggerProfile", g_settings.triggerProfile);
            root.set("gyroMode", g_settings.gyroMode);
            root.set("gyroLevel", g_settings.gyroLevel);
            root.set("touchpadMouse", g_settings.touchpadMouse);
            root.set("padMap", input::padMapJson(g_settings.padMap));
            root.set("keyMap", keyMapToJson(g_settings.keyMap));
            root.set("touchpadShortcuts", input::touchpadShortcutsJson(g_settings.touchpadShortcuts));
        },
        /*updateSettings=*/[](const json::Value& root) -> bool {
            bool changed = false;
            int padSpeakerVol = -1;
            {
                std::lock_guard<std::mutex> lock(g_settingsMutex);
                if (root.has("voiceRouting")) {
                    g_settings.voiceRouting = static_cast<int>(std::clamp<int64_t>(root["voiceRouting"].asInt(g_settings.voiceRouting), 0, 1));
                    media::audioSetVoiceRouting(g_settings.voiceRouting);
                    changed = true;
                }
                if (root.has("maxBitrateMbps")) {
                    g_settings.maxBitrateMbps = static_cast<int>(std::clamp<int64_t>(root["maxBitrateMbps"].asInt(g_settings.maxBitrateMbps), 0, 50));
                    changed = true;
                }
                if (root.has("highRefresh")) {
                    g_settings.highRefresh = root["highRefresh"].asBool(g_settings.highRefresh);
                    changed = true;
                }
                if (root.has("deadzoneLeft")) {
                    g_settings.deadzoneLeft = static_cast<int>(std::clamp<int64_t>(root["deadzoneLeft"].asInt(g_settings.deadzoneLeft), 0, 50));
                    changed = true;
                }
                if (root.has("deadzoneRight")) {
                    g_settings.deadzoneRight = static_cast<int>(std::clamp<int64_t>(root["deadzoneRight"].asInt(g_settings.deadzoneRight), 0, 50));
                    changed = true;
                }
                if (root.has("lightBarMode")) {
                    g_settings.lightBarMode = static_cast<int>(std::clamp<int64_t>(root["lightBarMode"].asInt(g_settings.lightBarMode), 0, 2));
                    changed = true;
                }
                if (root.has("lightBarColour") && root["lightBarColour"].isString()) {
                    std::string hexStr = root["lightBarColour"].str();
                    if (hexStr.size() == 7 && hexStr[0] == '#') {
                        uint32_t rgb = static_cast<uint32_t>(std::strtoul(hexStr.c_str() + 1, nullptr, 16));
                        g_settings.lightBarColour = 0xFF000000u | (rgb >> 16 & 0xFF) | (rgb & 0xFF00) | (rgb & 0xFF) << 16;
                        changed = true;
                    }
                }
                if (root.has("triggerStrength")) {
                    g_settings.triggerStrength = static_cast<int>(std::clamp<int64_t>(root["triggerStrength"].asInt(g_settings.triggerStrength), 0, 4));
                    changed = true;
                }
                if (root.has("triggerProfile")) {
                    g_settings.triggerProfile = static_cast<int>(std::clamp<int64_t>(root["triggerProfile"].asInt(g_settings.triggerProfile), 0, 2));
                    input::setTriggerProfile(g_settings.triggerProfile);
                    changed = true;
                }
                if (root.has("audioHaptics")) {
                    g_settings.audioHaptics = static_cast<int>(std::clamp<int64_t>(root["audioHaptics"].asInt(g_settings.audioHaptics), 0, 3));
                    media::setAudioHapticsLevel(g_settings.audioHaptics);
                    changed = true;
                }
                if (root.has("queueReadyAlert")) {
                    g_settings.queueReadyAlert = root["queueReadyAlert"].asBool(g_settings.queueReadyAlert);
                    changed = true;
                }
                if (root.has("videoDecoder")) {
                    g_settings.videoDecoder = static_cast<int>(std::clamp<int64_t>(root["videoDecoder"].asInt(g_settings.videoDecoder), 0, 1));
                    changed = true;
                }
                if (root.has("padMap")) {
                    input::PadMap newPad;
                    if (input::readPadMap(root["padMap"], newPad)) {
                        g_settings.padMap = newPad;
                        changed = true;
                    }
                }
                if (root.has("keyMap")) {
                    g_settings.keyMap = keyMapFromJson(root["keyMap"], g_settings.keyMap);
                    changed = true;
                }
                if (root.has("touchpadShortcuts")) {
                    input::TouchpadShortcuts newTs;
                    if (input::readTouchpadShortcuts(root["touchpadShortcuts"], newTs)) {
                        g_settings.touchpadShortcuts = newTs;
                        for (auto& [k, prof] : g_settings.perGame) {
                            prof.touchpadShortcuts = newTs;
                        }
                        changed = true;
                    }
                }
                if (root.has("padSpeakerVolume")) {
                    g_settings.padSpeakerVolume = static_cast<int>(std::clamp<int64_t>(root["padSpeakerVolume"].asInt(g_settings.padSpeakerVolume), 0, 100));
                    padSpeakerVol = g_settings.padSpeakerVolume;
                    changed = true;
                }
                if (root.has("sharpness")) {
                    g_settings.sharpness = static_cast<int>(std::clamp<int64_t>(root["sharpness"].asInt(g_settings.sharpness), 0, 3));
                    changed = true;
                }
                if (root.has("upscaler")) {
                    g_settings.upscaler = static_cast<int>(std::clamp<int64_t>(root["upscaler"].asInt(g_settings.upscaler), 0, 3));
                    changed = true;
                }
                if (root.has("deband")) {
                    g_settings.deband = static_cast<int>(std::clamp<int64_t>(root["deband"].asInt(g_settings.deband), 0, 3));
                    changed = true;
                }
                if (root.has("gyroMode")) {
                    g_settings.gyroMode = static_cast<int>(std::clamp<int64_t>(root["gyroMode"].asInt(g_settings.gyroMode), 0, 2));
                    changed = true;
                }
                if (root.has("gyroLevel")) {
                    g_settings.gyroLevel = static_cast<int>(std::clamp<int64_t>(root["gyroLevel"].asInt(g_settings.gyroLevel), 1, 10));
                    changed = true;
                }
                if (root.has("touchpadMouse")) {
                    g_settings.touchpadMouse = root["touchpadMouse"].asBool(g_settings.touchpadMouse);
                    changed = true;
                }
                if (changed) {
                    g_settings.save(settingsPath());
                    refreshSettingsUiLocked();
                }
            }
            if (changed) {
                applyControllerSettings();
                if (padSpeakerVol >= 0) {
                    media::setPadSpeakerVolume(padSpeakerVol);
                }
                if (g_core && g_core->streamScreen) {
                    g_core->streamScreen->syncFromSettings();
                }
                platform::notify("Configurações atualizadas (WebUI)");
                if (g_ui && !shell::caps().console) {
                    g_ui->showToast("Configurações atualizadas");
                }
            }
            return true;
        },
        /*getSearchQuery=*/[]() -> std::string {
            return g_ui ? g_ui->searchQuery() : "";
        },
        /*setSearchQuery=*/[](const std::string& q) {
            if (g_ui) {
                g_ui->setSearchQuery(q);
            }
        },
        /*setSearchParams=*/[](const std::string* q, const std::string* tab, const json::Value* filters) {
            if (!g_ui) return;
            ui::Tab targetTab = g_ui->tab();
            const ui::Tab* tabPtr = nullptr;
            if (tab) {
                if (*tab == "gamepass" || *tab == "0") {
                    targetTab = ui::Tab::GamePass;
                    tabPtr = &targetTab;
                } else if (*tab == "library" || *tab == "meus_jogos" || *tab == "1") {
                    targetTab = ui::Tab::Library;
                    tabPtr = &targetTab;
                }
            }
            std::vector<std::pair<ui::AppUi::Filter, int>> filterList;
            if (filters && filters->isObject()) {
                auto checkField = [&](const char* f1, const char* f2, ui::AppUi::Filter filter) {
                    if (filters->has(f1)) {
                        const auto& val = (*filters)[f1];
                        filterList.push_back({filter, val.isBool() ? (val.asBool() ? 1 : 0) : val.asInt()});
                    } else if (f2 && filters->has(f2)) {
                        const auto& val = (*filters)[f2];
                        filterList.push_back({filter, val.isBool() ? (val.asBool() ? 1 : 0) : val.asInt()});
                    }
                };
                checkField("wishlist", "filterWishlist", ui::AppUi::Filter::Wishlist);
                checkField("free", "filterFree", ui::AppUi::Filter::Free);
                checkField("cheapest", "filterCheapest", ui::AppUi::Filter::Cheapest);
                checkField("mouseKeyboard", "filterMouseKeyboard", ui::AppUi::Filter::MouseKeyboard);
                checkField("plan", "filterPlan", ui::AppUi::Filter::Plan);
                checkField("console", "filterConsole", ui::AppUi::Filter::Console);
                checkField("mode", "filterMode", ui::AppUi::Filter::Mode);
                checkField("language", "filterLanguage", ui::AppUi::Filter::Language);
            }
            g_ui->setSearchState(q, tabPtr, (filters || !filterList.empty()) ? &filterList : nullptr);
        },
        /*getSearchParams=*/[](std::string& outQuery, std::string& outTab, json::Value& outFilters) {
            if (!g_ui) return;
            outQuery = g_ui->searchQuery();
            outTab = (g_ui->tab() == ui::Tab::Library) ? "library" : "gamepass";
            outFilters = json::Value::object();
            outFilters.set("wishlist", g_ui->filterSelectedPublic(ui::AppUi::Filter::Wishlist) != 0);
            outFilters.set("free", g_ui->filterSelectedPublic(ui::AppUi::Filter::Free) != 0);
            outFilters.set("cheapest", g_ui->filterSelectedPublic(ui::AppUi::Filter::Cheapest) != 0);
            outFilters.set("mouseKeyboard", g_ui->filterSelectedPublic(ui::AppUi::Filter::MouseKeyboard) != 0);
            outFilters.set("plan", g_ui->filterSelectedPublic(ui::AppUi::Filter::Plan));
            outFilters.set("console", g_ui->filterSelectedPublic(ui::AppUi::Filter::Console));
            outFilters.set("mode", g_ui->filterSelectedPublic(ui::AppUi::Filter::Mode));
            outFilters.set("language", g_ui->filterSelectedPublic(ui::AppUi::Filter::Language));
        },
        /*isStreaming=*/[]() -> bool {
            return (g_ui && (g_ui->screen() == ui::Screen::Streaming || g_ui->screen() == ui::Screen::Launching)) ||
                   (g_player && g_player->running());
        },
        /*injectGamepad=*/nullptr,
        /*getTelemetry=*/[](json::Value& stats) {
            std::lock_guard<std::mutex> lock(g_infoMutex);
            stats.set("fps", g_streamInfo.fps);
            stats.set("rttMs", g_streamInfo.rttMs);
            stats.set("mbps", g_streamInfo.mbps);
            stats.set("lossPct", g_streamInfo.lossPct);
            stats.set("qp", g_streamInfo.qp);
            stats.set("decodeMs", g_streamInfo.decodeMs);
            stats.set("isHwDecode", g_streamInfo.isHwDecode);
            stats.set("width", g_streamInfo.width);
            stats.set("height", g_streamInfo.height);
        },
        /*getStreamMenuState=*/[](json::Value& menu) {
            ui::GameTile tile;
            {
                std::lock_guard<std::mutex> argLock(g_argMutex);
                tile = g_playTile;
            }
            std::lock_guard<std::mutex> lock(g_settingsMutex);
            bool hasCustom = false;
            GameProfile prof = g_settings.profileForGame(tile.productId, tile.titleId, &hasCustom);
            menu.set("sharpness", prof.sharpness);
            menu.set("upscaler", prof.upscaler);
            menu.set("deband", prof.deband);
            menu.set("padSpeakerVolume", g_settings.padSpeakerVolume);
            menu.set("triggerProfile", prof.triggerProfile);
            menu.set("touchpadShortcuts", input::touchpadShortcutsJson(prof.touchpadShortcuts));
            menu.set("hasCustomProfile", hasCustom);
        },
        /*updateStreamMenu=*/[](const json::Value& update) -> bool {
            bool changed = false;
            int padSpeakerVol = -1;
            {
                ui::GameTile tile;
                {
                    std::lock_guard<std::mutex> argLock(g_argMutex);
                    tile = g_playTile;
                }
                std::string key = gameProfileKey(tile.productId, tile.titleId);

                std::lock_guard<std::mutex> lock(g_settingsMutex);
                bool hasCustom = !key.empty() && g_settings.perGame.count(key);

                if (update.has("sharpness")) {
                    int val = static_cast<int>(std::clamp<int64_t>(update["sharpness"].asInt(g_settings.sharpness), 0, 3));
                    if (hasCustom) g_settings.perGame[key].sharpness = val;
                    else g_settings.sharpness = val;
                    changed = true;
                }
                if (update.has("upscaler")) {
                    int val = static_cast<int>(std::clamp<int64_t>(update["upscaler"].asInt(g_settings.upscaler), 0, 3));
                    if (hasCustom) g_settings.perGame[key].upscaler = val;
                    else g_settings.upscaler = val;
                    changed = true;
                }
                if (update.has("deband")) {
                    int val = static_cast<int>(std::clamp<int64_t>(update["deband"].asInt(g_settings.deband), 0, 3));
                    if (hasCustom) g_settings.perGame[key].deband = val;
                    else g_settings.deband = val;
                    changed = true;
                }
                if (update.has("padSpeakerVolume")) {
                    g_settings.padSpeakerVolume = static_cast<int>(std::clamp<int64_t>(update["padSpeakerVolume"].asInt(g_settings.padSpeakerVolume), 0, 100));
                    padSpeakerVol = g_settings.padSpeakerVolume;
                    changed = true;
                }
                if (update.has("triggerProfile")) {
                    int val = static_cast<int>(std::clamp<int64_t>(update["triggerProfile"].asInt(g_settings.triggerProfile), 0, 2));
                    if (hasCustom) g_settings.perGame[key].triggerProfile = val;
                    else g_settings.triggerProfile = val;
                    changed = true;
                }
                if (update.has("touchpadShortcuts")) {
                    input::TouchpadShortcuts newTs;
                    if (input::readTouchpadShortcuts(update["touchpadShortcuts"], newTs)) {
                        if (hasCustom) g_settings.perGame[key].touchpadShortcuts = newTs;
                        else g_settings.touchpadShortcuts = newTs;
                        changed = true;
                    }
                }
                if (changed) {
                    g_settings.save(settingsPath());
                    refreshSettingsUiLocked();
                    if (g_ui) g_ui->setPerGameSettings(g_settings.perGame);
                }
            }
            if (changed) {
                if (padSpeakerVol >= 0) {
                    media::setPadSpeakerVolume(padSpeakerVol);
                }
                if (g_core && g_core->streamScreen) {
                    g_core->streamScreen->syncFromSettings();
                }
                platform::notify("Configurações do jogo atualizadas (WebUI)");
            }
            return true;
        },
        /*streamAction=*/[](const std::string& action) {
            if (action == "xbox_guide" || action == "guide") {
                g_xboxButtonUntil = platform::nowMs() + 300;
            } else if (action == "end_session" || action == "leave") {
                g_cancel = true;
            }
        }
    });
    startWebServer();
    g_ui->setWebUrl(webServerUrl());
    {
        g_ui->setAllow1440(allow1440Locked());
        g_ui->setUpdateState(XC_APP_VERSION, {}, false);  // Settings shows this version before any check
        g_ui->setSettings(ui::choiceFromSettings(g_settings));
        g_ui->setPerGameSettings(g_settings.perGame);
        g_ui->setGyroDefaults(g_settings.gyroMode, g_settings.gyroLevel);
        applyControllerSettings();
        g_ui->setRegionLatency(g_settings.regionRtt);
        g_ui->setPrefs(g_settings.hidden, g_settings.librarySort == "az"        ? ui::LibrarySort::AZ
                                          : g_settings.librarySort == "console" ? ui::LibrarySort::Console
                                                                                : ui::LibrarySort::Recent,
                       g_settings.favorites, g_settings.wishlist);

        // Offline Cache / Instant-Resume: immediately load cached library before network
        std::string cacheFile = platform::dataDir() + "/library_cache.json";
        struct stat st;
        if (::stat(cacheFile.c_str(), &st) != 0) {
            cacheFile = platform::dataDir() + "/library.json";
        }
        auto initialLib = std::make_shared<Library>();
        initialLib->setCachePath(cacheFile);
        initialLib->setPlaytime(g_settings.titlePlaytime);
        initialLib->setFavorites(g_settings.favorites);
        initialLib->setWishlist(g_settings.wishlist);
        if (initialLib->loadCache(true)) {
            setInitialLibrary(initialLib);
            if (!initialLib->rows().empty()) {
                g_ui->setRows(initialLib->rows());
            }
            g_ui->setOwned(initialLib->owned(), initialLib->purchasable(), initialLib->ownedKnown());
            g_ui->setMembership(initialLib->gamePassMember());
            g_ui->setSearchPools(initialLib->gamePassSearchPool(), initialLib->librarySearchPool());
            XC_LOGI("instant-resume: loaded offline cache (%zu rows, %zu owned) in 0.01s",
                    initialLib->rows().size(), initialLib->owned().size());
        }
    }
    g_ui->setCaps(uiCaps());
    g_core->canvas.emplace(display::kWidth, display::kHeight);
    shell::setMouseHandler([](const shell::MouseEvent& mev) {
        if (!g_ui || g_ui->screen() == ui::Screen::Streaming) return;
        ui::UiEvent ev;
        if (mev.kind == shell::MouseEvent::Kind::Move) {
            ev = g_ui->onMouseMove(mev.x, mev.y);
        } else if (mev.kind == shell::MouseEvent::Kind::ButtonDown) {
            ev = g_ui->onMouseDown(mev.x, mev.y, mev.button);
        } else if (mev.kind == shell::MouseEvent::Kind::Wheel) {
            ev = g_ui->onMouseWheel(mev.wheelX, mev.wheelY);
        }
        if (ev.action != ui::Action::None && g_core) {
            g_core->dispatchUiAction(ev);
        }
    });

    if (!net::initTls(g_autoplay.badCa ? platform::assetDir() + "/missing.pem" : platform::caBundlePath())) {
        // The step that failed, and where the log is: what a bug report needs.
        platform::notify("PSBox: secure connections unavailable");
        g_ui->showError("Secure connections could not be set up: " + net::tlsInitError() + ". Log: " +
                        platform::dataDir() + "/xcloud.log");
        if (g_autoplay.badCa) XC_LOGI("AUTOPLAY END: TLS setup error shown");
    } else {
        if (g_autoplay.badCa) XC_LOGI("AUTOPLAY END: missing CA bundle, built-in copy %s", net::usingBuiltinCa() ? "used" : "NOT used");
        if (g_autoplay.testCa) net::addTrustedCa(platform::dataDir() + "/test-ca.pem");
        startWorker();
    }
    g_core->streamScreen.emplace(fonts);
    g_core->streamScreen->setCaps(uiCaps());
    crash::startWatchdog(15000);  // a main loop stuck for 15 s leaves a marker
    return true;
}

void Core::dispatchUiAction(const ui::UiEvent& ev) {
    switch (ev.action) {
        case ui::Action::Play: {
            std::lock_guard<std::mutex> lock(g_argMutex);
            g_playTile = ev.game;
            g_command = kPlay;
            break;
        }
        case ui::Action::ClaimFreeGame: {
            std::lock_guard<std::mutex> lock(g_argMutex);
            g_playTile = ev.game;
            g_command = kClaimFree;
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
            g_settings.favorites = ev.favorites;
            g_settings.wishlist = ev.wishlist;
            g_settings.librarySort = ev.librarySort == ui::LibrarySort::AZ        ? "az"
                                     : ev.librarySort == ui::LibrarySort::Console ? "console"
                                                                                   : "recent";
            g_settings.save(settingsPath());  // warns itself
            XC_LOGI("prefs saved: %zu hidden, %zu favorites, %zu wishlist, sort %s", g_settings.hidden.size(),
                    g_settings.favorites.size(), g_settings.wishlist.size(), g_settings.librarySort.c_str());
            updateLibraryFavorites(g_settings.favorites);
            updateLibraryWishlist(g_settings.wishlist);
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
        case ui::Action::Quit: quitAsked_ = true; break;  // desktop only (the PS5 never asks)
        case ui::Action::OpenUrl: {
            // A desktop: the sign-in page, the report form, the release page.
            bool ok = shell::openUrl(ev.url);
            XC_LOGI("browser: %s (a link of %zu characters)", ok ? "opened" : "could not be opened", ev.url.size());
            break;
        }
        case ui::Action::RefreshLibrary: {
            XC_LOGI("library: manual refresh requested");
            g_command = kReloadLibrary;
            if (g_ui) {
                g_ui->showToast(ui::tr(ui::Str::RefreshingLibrary));
                g_ui->setLoading(true, 0, 0);
            }
            platform::notify(ui::tr(ui::Str::RefreshingLibrary));
            break;
        }
        case ui::Action::CheckSignIn: {
            XC_LOGI("sign-in check: requested from Settings");
            int idle = kNone;
            if (!g_command.compare_exchange_strong(idle, kCheckSignIn)) XC_LOGW("sign-in check: worker is busy");
            break;
        }
        case ui::Action::RunNetworkTest: {
            XC_LOGI("network test: requested from Settings");
            int idle = kNone;
            if (!g_command.compare_exchange_strong(idle, kNetworkTest)) XC_LOGW("network test: worker is busy");
            break;
        }
        case ui::Action::TestQueueAlert: {
            XC_LOGI("settings: manual queue-ready alert test requested");
            playQueueReadyAlert();
            break;
        }
        case ui::Action::ResetSettings: {
            XC_LOGI("settings: restoring factory default settings");
            bool langChanged = false;
            {
                std::lock_guard<std::mutex> lock(g_settingsMutex);
                std::string prevLang = g_settings.language;
                g_settings = app::Settings{};
                g_settings.save(settingsPath());
                langChanged = g_settings.language != prevLang;
                refreshSettingsUiLocked();
                if (g_ui) g_ui->setPerGameSettings(g_settings.perGame);
            }
            ui::setLanguage(ui::languageFromCode(g_settings.language));
            applyControllerSettings();
            media::setPadSpeakerVolume(g_settings.padSpeakerVolume);
            if (g_core && g_core->streamScreen) {
                g_core->streamScreen->syncFromSettings();
            }
            updateLibraryFavorites(g_settings.favorites);
            updateLibraryWishlist(g_settings.wishlist);
            platform::notify(ui::tr(ui::Str::SettingsResetNotice));
            if (langChanged) g_command = kReloadLibrary;
            break;
        }
        case ui::Action::None: break;
    }
}

bool Core::frame() {
    if (!shell::pumpEvents() || quitAsked_) return false;
    if (bool on; shell::fullscreenSwitched(on)) {
        // F11 / Alt+Enter on a desktop: the Display mode setting follows.
        {
            std::lock_guard<std::mutex> lock(g_settingsMutex);
            g_settings.desktopFullscreen = on;
            g_settings.save(settingsPath());  // warns itself
        }
        g_ui->setFullscreen(on);
    }
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
            platform::notify(ui::trf(shell::caps().console ? ui::Str::ClockWrongNotice : ui::Str::ClockWrongNoticePc,
                                     net::todayText()),
                             "the console's clock looks wrong (notification)");
        }
    }
    // After an update: the new version, started in place of this one.
    if (g_restartWanted) shell::restartOrExit();
    // Gyro aiming moves the aim in a game, not the app's menu.
    input::setGyroActive(g_ui->screen() == ui::Screen::Streaming && !streamScreen->menuOpen());
    input::poll(pad);
    autoplayPad(pad);
    input::ControllerState vpad0;
    bool vconn0 = false;
    if (virtualGamepadState(0, vpad0, vconn0) && vconn0) {
        input::mergePads(pad, vpad0);
    }
    input::ControllerState usbPad;
    if (input::pollUsbPad(usbPad)) {
        input::mergePads(pad, usbPad);
    }
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
        // The controller's glyphs: those of the pad driving the menus (the
        // first connected one, which input::poll() reads), kept while none
        // is connected. The PS5's pads are DualSense: always PlayStation.
        const input::PadKind kind = input::anyPadCaps().kind;
        const ui::GlyphStyle style = ui::glyphStyleFor(kind);
        if (kind != input::PadKind::None && style != ui::g_glyphStyle) {
            ui::g_glyphStyle = style;
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
    // On a desktop the digits are typed for other things too: Ctrl + 1-4 there.
    static const bool playerKeysNeedCtrl = !shell::caps().console;
    bool ctrlHeld = std::binary_search(kbm.held.begin(), kbm.held.end(), uint8_t{0x11});
    if ((!inGame || streamScreen->menuOpen()) && !capturing && (!playerKeysNeedCtrl || ctrlHeld)) {
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
    // The home search's keys focused: a desktop's keyboard types into the
    // query (the shell's text input); what it types (letters, Space, Enter,
    // Backspace...) then doesn't move through the screens, until let go.
    // Arrows, Esc, Tab and Page Up / Down still do. The PS5 has no text
    // input: its keyboard moves through the screens as before.
    const bool typing = shell::setTextInput(!inGame && !capturing && g_ui->wantsTextInput());
    if (std::string typed = shell::takeTextInput(); !typed.empty()) g_ui->typeText(typed);
    if (typing) {
        static constexpr uint8_t kStillNavigate[] = {0x09, 0x1B, 0x21, 0x22, 0x25, 0x26, 0x27, 0x28};
        for (uint8_t vk : kbm.held)
            if (std::find(std::begin(kStillNavigate), std::end(kStillNavigate), vk) == std::end(kStillNavigate) &&
                std::find(swallowedKeys.begin(), swallowedKeys.end(), vk) == swallowedKeys.end())
                swallowedKeys.push_back(vk);
    }
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
    if (keyboardActiveAt != kbActiveBefore_) {
        kbActiveBefore_ = keyboardActiveAt;
        padsCheckedAt = 0;  // immediate update on keyboard input
    }
    for (int i = 0; i < input::kMaxPads; ++i) {
        uint64_t pAct = input::padActiveAt(i);
        if (pAct != padActiveBefore_[static_cast<size_t>(i)]) {
            padActiveBefore_[static_cast<size_t>(i)] = pAct;
            padsCheckedAt = 0;  // immediate update on pad input
        }
        uint64_t vAct = virtualGamepadActiveAt(i);
        if (vAct != vpadActiveBefore_[static_cast<size_t>(i)]) {
            vpadActiveBefore_[static_cast<size_t>(i)] = vAct;
            padsCheckedAt = 0;  // immediate update on phone input or player switch
        }
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
        int kbPlayer = -1;
        {
            std::lock_guard<std::mutex> lock(g_settingsMutex);
            kbPlayer = g_settings.keyboardPlayer;
        }
        ui::PadSlots slots;
        for (int i = 0; i < input::kMaxPads; ++i) {
            bool padConn = input::padConnected(i);
            std::string padName = input::padUserName(i);

            bool vpadConn = false;
            input::ControllerState vpadSt{};
            virtualGamepadState(i, vpadSt, vpadConn);

            uint64_t now = platform::nowMs();
            uint64_t padT = padConn ? input::padActiveAt(i) : 0;
            uint64_t kbT = (kbmConnected && i == kbPlayer) ? keyboardActiveAt : 0;
            uint64_t vpadActive = virtualGamepadActiveAt(i);
            uint64_t phoneT = (vpadConn && (now >= vpadActive) && (now - vpadActive < 15000)) ? vpadActive : 0;

            bool connected = padConn || vpadConn || (kbmConnected && i == kbPlayer);

            ui::PadDevice dev = ui::PadDevice::Gamepad;
            if (phoneT > 0 && phoneT >= padT && phoneT >= kbT) {
                dev = ui::PadDevice::Phone;
            } else if (kbT > 0 && kbT >= padT && kbT >= phoneT) {
                dev = ui::PadDevice::Keyboard;
            } else if (padT > 0) {
                dev = ui::PadDevice::Gamepad;
            } else {
                if (padConn) {
                    dev = ui::PadDevice::Gamepad;
                } else if (vpadConn) {
                    dev = ui::PadDevice::Phone;
                } else if (kbmConnected && i == kbPlayer) {
                    dev = ui::PadDevice::Keyboard;
                }
            }

            slots[static_cast<size_t>(i)].connected = connected;
            slots[static_cast<size_t>(i)].name = std::move(padName);
            slots[static_cast<size_t>(i)].device = dev;
            slots[static_cast<size_t>(i)].keyboard = (dev == ui::PadDevice::Keyboard);
        }
        g_ui->setPads(slots);
        streamScreen->setPads(slots);
        // The pads that came or went may change what the screens offer.
        ui::UiCaps caps = uiCaps();
        g_ui->setCaps(caps);
        streamScreen->setCaps(caps);
    }

    ui::NavInput nav;
    nav.up = up.update(pad.dpadUp || pad.leftStickY < -0.6f, now);
    nav.down = down.update(pad.dpadDown || pad.leftStickY > 0.6f, now);
    nav.left = left.update(pad.dpadLeft || pad.leftStickX < -0.6f, now);
    nav.right = right.update(pad.dpadRight || pad.leftStickX > 0.6f, now);
    nav.accept = pad.btnA && !prev.btnA;
    nav.acceptHeld = pad.btnA;
    nav.back = pad.btnB && !prev.btnB;
    nav.options = pad.btnOptions && !prev.btnOptions;
    nav.l1 = pad.btnL1 && !prev.btnL1;
    nav.r1 = pad.btnR1 && !prev.btnR1;
    nav.square = pad.btnX && !prev.btnX;    // Square (Xbox X)
    nav.triangle = pad.btnY && !prev.btnY;  // Triangle (Xbox Y)
    nav.r3 = pad.btnR3 && !prev.btnR3;
    nav.l3 = pad.btnL3 && !prev.btnL3;
    nav.l2 = pad.triggerL2 > 0.5f && prev.triggerL2 <= 0.5f;
    nav.r2 = pad.triggerR2 > 0.5f && prev.triggerR2 <= 0.5f;
    nav.touchpad = pad.btnTouchpad;
    nav.stickX = pad.leftStickX;
    nav.stickY = pad.leftStickY;
    nav.dpadLeft = dpadLeft.update(pad.dpadLeft, now);
    nav.dpadRight = dpadRight.update(pad.dpadRight, now);
    nav.rawLX = pad.rawLeftX, nav.rawLY = pad.rawLeftY, nav.rawRX = pad.rawRightX, nav.rawRY = pad.rawRightY;
    nav.l2Analog = pad.triggerL2, nav.r2Analog = pad.triggerR2;
    nav.padPressed = input::padButtons(pad) & ~input::padButtons(prev);  // the pad as it is: never through the game's map
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
        display::idleWait(4);  // ~250 Hz input (the PS5 sleeps 4 ms; matching DualSense native 250 Hz rate)
        display::pump();       // the desktop shows the newest video frame here
        return true;
    }
    streamScreen->idle();

    if (g_ui->screen() == ui::Screen::Splash) {
        // Discard controller navigation input while the loading/splash screen is active.
        nav = ui::NavInput{};
    }

    ui::UiEvent ev = g_ui->handle(nav);
    int strength, hz, resistance, profile = 0;
    bool pulses;
    if (g_ui->settingsTriggerFeel(strength, hz, resistance, pulses, profile)) {
        // Settings: the triggers' feel as chosen there, at once; their
        // tester makes them vibrate as far as they are pressed.
        input::setTriggerFeel(strength, hz, resistance, pulses, profile);
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
    dispatchUiAction(ev);
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
    display::pump();  // the desktop shows the menu here (paced by its vsync); nothing on the PS5
    return true;
}

bool coreFrame() { return g_core->frame(); }

void coreShutdown() {
    // Not on the PS5, whose loop never ends. Before the backends close, the
    // threads that use them stop: the worker leaves its loop (a stream
    // stopped and its xCloud session closed, so the account's slot is free;
    // the library's loading ended), then the price loop. Each wait is
    // bounded (a request may hang on the network); the caller leaves with
    // _exit() afterwards, so no static destructor races a thread still
    // running.
    XC_LOGI("=== PSBox Cloud Gaming closing ===");
    stopWebServer();
    const uint64_t start = platform::nowMs();
    requestQuit();
    bool workerDone = waitForWorker(5000);
    uint64_t spent = platform::nowMs() - start;
    bool pricesDone = stopPriceLoop(spent < 5000 ? static_cast<uint32_t>(5000 - spent) : 200);
    if (!workerDone || !pricesDone)
        XC_LOGW("closing: %s%s%s still busy, left to the exit", workerDone ? "" : "the worker",
                !workerDone && !pricesDone ? " and " : "", pricesDone ? "" : "the price loop");
    shell::beforeShutdown();
    input::shutdown();
    display::shutdown();
    platform::shutdown();
    XC_LOGI("closed in %llu ms", static_cast<unsigned long long>(platform::nowMs() - start));
    log::setFile(nullptr);  // the log closed (every line was already flushed)
}

int runApp() {
    if (!coreStartup()) return 1;
    while (coreFrame()) {
    }
    coreShutdown();
    return 0;
}

}  // namespace xc::app
