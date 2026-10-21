// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
// Unattended test mode (tools/ps5/autotest.sh); see autoplay.h.
#include "app/autoplay.h"
#include "ui/input_mode.h"

#include "app/app_core.h"
#include "app/app_state.h"
#include "display/display.h"
#include "media/audio_in.h"
#include "media/decoder.h"
#include "platform/platform.h"
#include "ui/canvas.h"
#include "ui/stream_menu.h"
#include "ui/strings.h"
#include "util/log.h"

#include <cerrno>
#include <cmath>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <optional>

namespace xc::app {

Autoplay g_autoplay;
std::atomic<bool> g_syntheticA{false};

namespace {
// The home screen was saved: the tests that start from it may go.
bool uiSaved = false;
bool settingsShot = false;  // "settingstest": save the screen now
bool donationShot = false;  // "donationtest": save the screen now
const char* keysShot = nullptr;  // "keystest": the screen to save now
uint64_t homeSince = 0, launchSince = 0;
bool launchSaved = false;
const char* pickerShot = nullptr;
bool confirmShot = false, confirmHome = false;
const char* tuneShot = nullptr;  // "tunetest": save this screen next  // "confirmtest": save confirm2.ppm / check the home screen  // "pickertest": save this screen next, once drawn
const char* gridShot = nullptr;  // "gridtest": save grid screenshot
const char* gameSettingsShot = nullptr; // "gamesettingstest": save screenshot
std::string gameSettingsKey;                 // the game whose profile the test changes
std::optional<GameProfile> gameSettingsBefore;  // its profile before (none: it had none)
std::optional<Settings> globalSettingsBefore;   // global settings before
std::optional<input::KeyMap> keysBefore;        // "keystest": the keys before, put back at the end
std::optional<input::PadMap> padMapBefore;      // "padmaptest": the controller buttons before, put back at the end
const char* regionShot = nullptr;  // "regiontest": save this screen next
bool acceptUpdate = false;  // "updatetest": Cross on the pop-up, next pass ("updateskip": Circle)
std::optional<ui::SettingsChoice> confirmBefore;  // "confirmtest": the settings before, put back at the end

// Settings' rows, walked the same way by every test: the rows this system
// shows (ui::visibleSettingsRows with its capabilities), from the top (the
// language). Presses of down from there to row `row` (its id); -1 when it
// isn't shown here.
int downsTo(int row) { return ui::settingsRowPosition(g_ui->caps(), row); }

// True when Settings is up with row `row` focused. Otherwise the test stops
// before it presses Cross or saves anything (these are the user's real
// settings): logged as a failure, and as the end of the run for the scripts.
bool onSettingsRow(int row, const char* test) {
    bool open = g_ui->screen() == ui::Screen::Settings;
    int focused = g_ui->settingsFocus();
    if (open && focused == row) return true;
    XC_LOGE("AUTOPLAY FAIL: %s: expected row %d, focused %d%s", test, row, focused, open ? "" : " (Settings not open)");
    XC_LOGI("AUTOPLAY END: %s failed, stopped before changing anything", test);
    return false;
}

// Down to a row of Settings: a press every other pass, `downs` of them;
// true once done.
struct SettingsWalk {
    int done = 0;
    bool step(ui::NavInput& nav, int downs) {
        if (done >= downs * 2) return true;
        if (done % 2 == 0) nav.down = true;
        return ++done >= downs * 2;
    }
};
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
        if (opt == "kbhints") ui::g_keyboardHints = g_autoplay.kbHints = true;  // the hints and slots as with a keyboard (screenshots)
        if (opt.rfind("loss=", 0) == 0) g_autoplay.videoLoss = std::atoi(opt.c_str() + 5);
        if (opt == "ai") g_autoplay.upscaler = 1;
        if (opt == "restore") g_autoplay.restore = true;
        if (opt.rfind("swap=", 0) == 0) display::setSwapImages(std::atoi(opt.c_str() + 5));
        if (opt == "nopace") display::setPresentWait(false);
        if (opt == "detailtest" || opt == "detailstest") g_autoplay.detailTest = true;
        if (opt == "pad") g_autoplay.pad = true;
        if (opt == "consoles") g_autoplay.consoles = true;
        if (opt == "consolestab") g_autoplay.consolesTab = true;
        if (opt == "settingstest") g_autoplay.settingsTest = true;
        if (opt == "keystest") g_autoplay.keysTest = true;
        if (opt == "padmaptest") g_autoplay.padMapTest = true;
        if (opt == "badca") g_autoplay.badCa = true;
        if (opt == "consolesempty") g_autoplay.consolesTab = g_autoplay.consolesEmpty = true;
        if (opt == "librarytest") g_autoplay.libraryTest = true;
        if (opt == "imetest") g_autoplay.imeTest = true;
        if (opt == "menutest") g_autoplay.menuTest = true;
        if (opt.rfind("res=", 0) == 0) g_autoplay.resolution = opt.substr(4);
        if (opt.rfind("sharp=", 0) == 0) g_autoplay.sharpness = std::atoi(opt.c_str() + 6);
        if (opt.rfind("deband=", 0) == 0) g_autoplay.deband = std::atoi(opt.c_str() + 7);
        if (opt.rfind("bitrate=", 0) == 0) g_autoplay.bitrateMbps = std::atoi(opt.c_str() + 8);
        if (opt.rfind("mbps=", 0) == 0) g_autoplay.bitrateMbps = std::atoi(opt.c_str() + 5);
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
        if (opt == "crashtest") g_autoplay.crashTest = true;
        if (opt == "quicktest") g_autoplay.quickTest = true;
        if (opt == "scrolltest") g_autoplay.scrollTest = true;
        if (opt.rfind("locktitle=", 0) == 0) g_autoplay.lockTitle = opt.substr(10);
        if (opt == "gridtest") g_autoplay.gridTest = true;
        if (opt == "gamesettingstest") g_autoplay.gameSettingsTest = true;
        if (opt == "publishertest") g_autoplay.publisherTest = true;
        if (opt == "friendstest") g_autoplay.friendsTest = true;
        if (opt == "streammenutest") g_autoplay.streamMenuTest = true;
        if (opt == "regiontest") g_autoplay.regionTest = true;
        if (opt == "tlscheck") g_autoplay.tlsCheck = true;
        if (opt == "donationtest") g_autoplay.donationTest = true;
        if (opt == "norestart") g_autoplay.noRestart = true;
        if (opt.rfind("threads=", 0) == 0) g_autoplay.decodeThreads = std::atoi(opt.c_str() + 8);
    }
    g_autoplay.title = title;
    g_autoplay.seconds = seconds > 0 ? seconds : 60;
    XC_LOGI("AUTOPLAY %s for %ds %s", title, g_autoplay.seconds, option);
    platform::probeNetworking();
}

void guardSettingsForAutoplay(const std::string& settingsPath) {
    const std::string backup = settingsPath.substr(0, settingsPath.rfind('/') + 1) + "settings.autoplay-backup.json";
    std::string text;
    bool haveBackup = platform::readFile(backup, text);
    if (!g_autoplay.title.empty()) {
        if (haveBackup) {
            XC_LOGI("autoplay: the settings from before the tests are kept in %s", backup.c_str());
            return;
        }
        // An empty backup: there was no settings.json (it goes again then).
        std::string current;
        if (!platform::readFile(settingsPath, current)) current.clear();
        if (platform::writeFileAtomic(backup, current))
            XC_LOGI("autoplay: settings saved to %s, put back at the next start without autoplay.txt", backup.c_str());
        else
            XC_LOGW("autoplay: could not save the settings to %s", backup.c_str());
        return;
    }
    if (!haveBackup) return;
    bool ok = text.empty() ? (std::remove(settingsPath.c_str()) == 0 || errno == ENOENT)
                           : platform::writeFileAtomic(settingsPath, text);
    if (ok) {
        std::remove(backup.c_str());
        XC_LOGI("autoplay: the settings from before the tests put back (%s)", text.empty() ? "none then: defaults" : backup.c_str());
    } else {
        XC_LOGW("autoplay: could not put back the settings from %s (kept for the next start)", backup.c_str());
    }
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
        // The user's dead zones and trigger feel come back at the end (the
        // test saves its own).
        static std::optional<ui::SettingsChoice> before;
        static int downs = -1;
        if (!before) before = g_ui->settings(), downs = downsTo(3);
        // Down to the dead zone (its row as this system shows them), the
        // trigger vibration right under it. The timeline below was made for
        // 7 presses: what comes after them moves with their number.
        const int64_t shift = 130 * (static_cast<int64_t>(std::max(downs, 0)) - 7);
        auto at = [&](uint64_t ms) { return static_cast<uint64_t>(static_cast<int64_t>(ms) + shift); };
        auto quick = [&](uint64_t from, int n) {
            for (int i = 0; i < n; ++i)
                if (in(from + 130u * static_cast<unsigned>(i), 60)) return true;
            return false;
        };
        // Before each Cross: on the row it is meant for, else stop here.
        static bool deadzoneChecked = false, triggersChecked = false;
        bool stop = false;
        if (!deadzoneChecked && t >= at(1950)) deadzoneChecked = true, stop = !onSettingsRow(3, "tune test");
        if (!triggersChecked && t >= at(8450)) triggersChecked = true, stop = !onSettingsRow(4, "tune test");
        if (stop) {
            g_ui->setSettings(*before);
            saveSettingsChoice(*before);  // the dead zone's own changes (saved by its tester) undone
            g_ui->showHome();
            g_autoplay.tuneTest = false;
            pad = input::ControllerState{};
        } else {
            pad.btnOptions = in(500, 150);
            pad.dpadDown = quick(1000, downs) || in(at(8000), 120) || in(at(9400), 120) || in(at(10200), 120) ||
                           in(at(11600), 120);
            pad.btnA = in(at(2000), 150) || in(at(8500), 150);
            pad.btnB = in(at(7500), 150) || in(at(16500), 150);
            pad.btnR1 = in(at(4600), 150);
            pad.dpadRight = pulses(at(3000), 5) || in(at(9000), 120) || in(at(9800), 120) || pulses(at(10600), 3) ||
                            in(at(12000), 120);
            pad.dpadLeft = pulses(at(5000), 3);
            if (in(at(2500), 5000)) pad.rawLeftX = 0.1f, pad.rawLeftY = 0.06f, pad.rawRightX = 0.7f, pad.rawRightY = -0.4f;
            if (in(at(12500), 1500)) pad.triggerL2 = 0.5f;  // each trigger alone: the log shows each one
            if (in(at(14500), 1500)) pad.triggerR2 = 0.6f;
            if (in(at(6500), 20)) tuneShot = "sticks.ppm";
            if (in(at(15500), 20)) tuneShot = "triggers.ppm";
            if (in(at(17500), 20)) {
                g_ui->setSettings(*before);
                saveSettingsChoice(*before);
                g_autoplay.tuneTest = false;
                XC_LOGI("AUTOPLAY END: tune test (dead zones and triggers put back as they were)");
            }
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
                // The test changes and saves this game's profile: what it was
                // is put back at the end (it is the user's real settings).
                std::lock_guard<std::mutex> lock(g_settingsMutex);
                gameSettingsKey = app::gameProfileKey(tile.productId, tile.titleId);
                auto it = g_settings.perGame.find(gameSettingsKey);
                gameSettingsBefore = it != g_settings.perGame.end() ? std::optional<GameProfile>(it->second) : std::nullopt;
                globalSettingsBefore = g_settings;
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
            {
                std::lock_guard<std::mutex> lock(g_settingsMutex);
                if (gameSettingsBefore)
                    g_settings.perGame[gameSettingsKey] = *gameSettingsBefore;
                else
                    g_settings.perGame.erase(gameSettingsKey);
                if (globalSettingsBefore) {
                    g_settings.resolution = globalSettingsBefore->resolution;
                }
                g_settings.save(settingsPath());
                g_ui->setPerGameSettings(g_settings.perGame);
                refreshSettingsUiLocked();
            }
            XC_LOGI("autoplay: the game's profile put back as it was");
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
        // The user's confirm button is put back at the end (autoplayScreens).
        static uint64_t since = 0;
        static int downs = -1;
        static bool checked = false;
        uint64_t now = platform::nowMs();
        if (!since) since = now, downs = downsTo(5), confirmBefore = g_ui->settings();
        uint64_t t = now - since;
        auto in = [&](uint64_t at, uint64_t len) { return t >= at && t < at + len; };
        // Down to the confirm button (its row as this system shows them), a
        // press every 300 ms; the rest follows from t0.
        const uint64_t t0 = 1000 + 300u * static_cast<unsigned>(std::max(downs, 0));
        auto after = [&](uint64_t at, uint64_t len) { return in(t0 + at, len); };
        if (!checked && t >= t0 + 100) {
            checked = true;
            if (!onSettingsRow(5, "confirm test")) {
                g_autoplay.confirmTest = false;  // no Cross: nothing changed
                confirmBefore.reset();
                g_ui->showHome();
            }
        }
        if (g_autoplay.confirmTest) {
            bool cross = after(200, 150) || after(1200, 500) || after(4300, 150);
            bool circle = after(2300, 150) || after(3500, 150);
            bool swapped = input::circleConfirms();
            pad.btnA = swapped ? circle : cross;
            pad.btnB = swapped ? cross : circle;
            pad.btnOptions = in(500, 150);
            bool down = after(700, 120);
            for (int i = 0; i < downs; ++i) down |= in(1000 + 300u * static_cast<unsigned>(i), 120);
            pad.dpadDown = down;
            if (after(3000, 20)) confirmShot = true;
            if (after(5500, 20)) confirmHome = true;
        }
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
    if (g_autoplay.friendsTest && uiSaved) {
        static uint64_t since = 0;
        uint64_t now = platform::nowMs();
        if (!since) since = now;
        uint64_t t = now - since;
        auto in = [&](uint64_t at, uint64_t len) { return t >= at && t < at + len; };
        pad.dpadDown = in(2000, 150);
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
    if (g_autoplay.regionTest && uiSaved) {
        // The refused sign-in's screens, as the user goes through them; Circle
        // leaves each, so the sign-in country stays as it was.
        static uint64_t since = 0;
        static int step = 0;
        if (!since) since = now;
        static const unsigned kAt[] = {500, 1500, 2000, 2400, 3200, 3700, 4200, 5000, 5500, 6500};
        if (step < 10 && now - since >= kAt[step]) {
            nav = ui::NavInput{};
            switch (step) {
                case 0: g_ui->showRegionRefused(ui::tr(ui::Str::RegionRefused)); break;
                case 1: regionShot = "region.ppm"; break;
                case 2: nav.square = true; break;
                case 3: nav.down = true; break;
                case 4: regionShot = "region_picker.ppm"; break;
                case 5: nav.back = true; break;
                case 6: nav.options = true; break;
                case 7: regionShot = "region_settings.ppm"; break;
                case 8: nav.back = true; break;
                case 9:
                    regionShot = "region_back.ppm";
                    g_autoplay.regionTest = false;
                    XC_LOGI("AUTOPLAY END: region test, back on the %s screen",
                            g_ui->screen() == ui::Screen::Error ? "error" : "WRONG");
                    break;
            }
            ++step;
        }
    }
    if (g_autoplay.settingsUpdate && uiSaved && !g_ui->updateOffered()) {
        // Settings, down to Updates (the last row), Cross.
        static uint64_t since = 0;
        static int step = 0;
        static SettingsWalk walk;
        if (!since) since = now;
        if (step == 0 && now - since >= 500) {
            nav = ui::NavInput{};
            nav.options = true;
            ++step;
        } else if (step == 1 && now - since >= 800) {
            nav = ui::NavInput{};
            if (walk.step(nav, downsTo(7))) ++step;
        } else if (step == 2 && now - since >= 3500) {
            nav = ui::NavInput{};
            if (onSettingsRow(7, "settings update test")) nav.accept = true;
            else g_autoplay.settingsUpdate = false, g_ui->showHome();
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
        // The light bar's change is never saved: the screen's copy is put
        // back at the end, and Settings left without saving.
        static uint64_t since = 0;
        static std::optional<ui::SettingsChoice> before;
        static SettingsWalk walk;
        if (!since) since = now, before = g_ui->settings();
        uint64_t t = now - since;
        static int step = 0;
        enum { Options, Walk, CheckConfirm, Accept, Shot, Back, Down, CheckLightBar, L2, End };
        struct Step { uint64_t at; int what; const char* shot; };
        static const Step steps[] = {
            {500, Options, nullptr},       {1000, Walk, nullptr},  {2600, CheckConfirm, nullptr},
            {2700, Accept, nullptr},       {4500, Shot, "confirm.ppm"},
            {5000, Back, nullptr},  // the confirm button's list closes, unchanged
            {5500, Down, nullptr},         {5900, CheckLightBar, nullptr}, {6000, Accept, nullptr},
            {6500, Down, nullptr},  // in the light bar's list: Custom colour
            {7000, Accept, nullptr},       {9000, L2, nullptr},  // after the stick: darker
            {10500, Shot, "picker.ppm"},   {11000, Accept, nullptr}, {12500, Shot, "settings.ppm"},
            {13500, End, nullptr}};
        if (t >= 7500 && t < 9000) nav.stickX = 1.0f, nav.stickY = -0.6f;
        if (step < static_cast<int>(std::size(steps)) && t >= steps[step].at) {
            nav = ui::NavInput{};
            nav.nowMs = now;
            bool next = true;
            auto abort = [&] {
                g_autoplay.pickerTest = false;
                g_ui->setSettings(*before);
                g_ui->showHome();
            };
            switch (steps[step].what) {
            case Options: nav.options = true; break;
            case Walk: next = walk.step(nav, downsTo(5)); break;
            case CheckConfirm:
                if (!onSettingsRow(5, "picker test")) abort();
                break;
            case CheckLightBar:
                if (!onSettingsRow(6, "picker test")) abort();
                break;
            case Accept: nav.accept = true; break;
            case Back: nav.back = true; break;
            case Down: nav.down = true; break;
            case L2: nav.l2 = true; break;
            case Shot: pickerShot = steps[step].shot; break;
            case End:
                abort();  // the same way out: nothing kept
                XC_LOGI("AUTOPLAY END: picker test (the light bar put back as it was)");
                break;
            }
            if (next) {
                XC_LOGI("autoplay: picker step %d", step);
                ++step;
            }
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

    if (g_autoplay.keysTest && uiSaved) {
        // Settings down to the keyboard keys, their screen, a reserved key
        // and then G chosen for the D-pad down's first key; back to the
        // default keys before leaving (Settings saves them).
        static uint64_t since = 0;
        static int step = 0;
        if (!since) since = now;
        struct Step { uint64_t at; int what; };
        enum { Options, Walk, Check, Down, Up, Accept, Back, Shot, Reserved, KeyG, End };
        static const Step steps[] = {
            {500, Options}, {1500, Walk}, {5100, Check}, {5200, Shot},
            {5800, Accept}, {7000, Shot}, {7500, Down}, {8000, Accept}, {8600, Reserved}, {9400, Shot},
            {10000, KeyG}, {10800, Shot}, {11400, Up}, {11800, Up}, {12400, Accept}, {13000, Back},
            {14000, Shot}, {14600, Back}, {15200, End}};
        static const char* shots[] = {"keys_settings.ppm", "keys.ppm", "keys_capture.ppm", "keys_after.ppm",
                                      "keys_settings_after.ppm"};
        static int shot = 0;
        static SettingsWalk walk;
        if (!keysBefore) {
            std::lock_guard<std::mutex> lock(g_settingsMutex);
            keysBefore = g_settings.keyMap;
        }
        if (step < static_cast<int>(std::size(steps)) && now - since >= steps[step].at) {
            bool next = true;
            switch (steps[step].what) {
            case Options: nav.options = true; break;
            case Walk: next = walk.step(nav, downsTo(12)); break;  // the keyboard keys
            case Check:
                if (!onSettingsRow(12, "keys test")) {
                    g_autoplay.keysTest = false;  // nothing chosen yet: nothing to put back
                    g_ui->showHome();
                }
                break;
            case Down: nav.down = true; break;
            case Up: nav.up = true; break;
            case Accept: nav.accept = true; break;
            case Back: nav.back = true; break;
            case Shot: keysShot = shots[shot++]; break;
            case Reserved: g_ui->captureKey(0x7B); break;
            case KeyG: g_ui->captureKey('G'); break;
            case End: {
                // The user's own keys back (the test ends on the defaults,
                // which Settings saved on the way out).
                ui::SettingsChoice choice = g_ui->settings();
                choice.keyMap = *keysBefore;
                g_ui->setSettings(choice);
                {
                    std::lock_guard<std::mutex> lock(g_settingsMutex);
                    g_settings.keyMap = *keysBefore;
                    g_settings.save(settingsPath());
                }
                g_autoplay.keysTest = false;
                XC_LOGI("AUTOPLAY END: keys test (the keys put back as they were)");
                break;
            }
            }
            if (next) ++step;
        }
    }

    if (g_autoplay.padMapTest && uiSaved) {
        // Settings down to Controller buttons, their screen, Circle captured
        // for A (A and B swap), B cleared and Circle refused (incomplete),
        // then "Discard changes": nothing is saved. The user's map is put
        // back at the end all the same.
        static uint64_t since = 0;
        static int step = 0;
        if (!since) since = now;
        struct Step { uint64_t at; int what; };
        enum { Options, Downs, Check, Down, Up, Right, Accept, Back, Square, Shot, PressCircle, End };
        static const Step steps[] = {
            {500, Options}, {1500, Downs}, {5100, Check}, {5200, Accept}, {6400, Shot}, {7000, Accept}, {7600, Shot},
            {8200, PressCircle}, {8800, Down}, {9200, Square}, {9800, Back}, {10600, Shot}, {11200, Up},
            {11500, Up}, {12000, Right}, {12500, Accept}, {13300, Back}, {14000, End}};
        static const char* shots[] = {"padmap.ppm", "padmap_capture.ppm", "padmap_incomplete.ppm"};
        static int shot = 0;
        static SettingsWalk walk;
        if (!padMapBefore) {
            std::lock_guard<std::mutex> lock(g_settingsMutex);
            padMapBefore = g_settings.padMap;
        }
        if (step < static_cast<int>(std::size(steps)) && now - since >= steps[step].at) {
            bool next = true;
            switch (steps[step].what) {
            case Options: nav.options = true; break;
            case Downs: next = walk.step(nav, downsTo(14)); break;  // the controller buttons
            case Check:
                if (!onSettingsRow(14, "padmap test")) {
                    g_autoplay.padMapTest = false;  // nothing changed yet
                    g_ui->showHome();
                }
                break;
            case Down: nav.down = true; break;
            case Up: nav.up = true; break;
            case Right: nav.right = true; break;
            case Accept: nav.accept = true; break;
            case Back: nav.back = true; break;
            case Square: nav.square = true; break;
            case Shot: keysShot = shots[shot++]; break;
            case PressCircle: nav.padPressed = 1u << static_cast<int>(input::PadFn::B); break;
            case End: {
                ui::SettingsChoice choice = g_ui->settings();
                choice.padMap = *padMapBefore;
                g_ui->setSettings(choice);
                {
                    std::lock_guard<std::mutex> lock(g_settingsMutex);
                    g_settings.padMap = *padMapBefore;
                    g_settings.save(settingsPath());
                }
                g_autoplay.padMapTest = false;
                XC_LOGI("AUTOPLAY END: padmap test (the controller buttons put back as they were)");
                break;
            }
            }
            if (next) ++step;
        }
    }

    if (g_autoplay.settingsTest && uiSaved) {
        // Settings, down to the resolution, open its list; saved 2 s later.
        static uint64_t since = 0;
        static int step = 0;
        static std::optional<ui::SettingsChoice> settingsBefore;
        if (!since) {
            since = now;
            settingsBefore = g_ui->settings();
        }
        static SettingsWalk walk;
        const uint64_t at[] = {500, 1500, 2500, 4500, 5200};
        if (step < 5 && now - since >= at[step]) {
            bool next = true;
            if (step == 0) nav.options = true;
            if (step == 1) next = walk.step(nav, downsTo(1));  // the resolution
            if (step == 2) {
                if (onSettingsRow(1, "settings test")) {
                    nav.accept = true;
                } else {
                    g_autoplay.settingsTest = false;
                    g_ui->showHome();
                }
            }
            if (step == 3) {
                settingsShot = true;  // saved below, once drawn
            }
            if (step == 4) {
                if (settingsBefore) {
                    g_ui->setSettings(*settingsBefore);
                    saveSettingsChoice(*settingsBefore);
                }
                g_ui->showHome();
                g_autoplay.settingsTest = false;
                XC_LOGI("AUTOPLAY END: settings test (settings put back as they were)");
            }
            if (next) ++step;
        }
    }

    if (g_autoplay.donationTest && uiSaved) {
        static uint64_t since = 0;
        static int step = 0;
        if (!since) since = now;
        static SettingsWalk walk;
        const uint64_t at[] = {500, 1500, 3200, 4800, 7000, 8500};
        if (step < 6 && now - since >= at[step]) {
            bool next = true;
            if (step == 0) nav.options = true;
            if (step == 1) next = walk.step(nav, downsTo(19));
            if (step == 2) {
                if (onSettingsRow(19, "donation test")) {
                    settingsShot = true;
                } else {
                    g_autoplay.donationTest = false;
                    g_ui->showHome();
                }
            }
            if (step == 3) {
                nav.accept = true;
            }
            if (step == 4) {
                donationShot = true;
            }
            if (step == 5) {
                g_ui->showHome();
                g_autoplay.donationTest = false;
                XC_LOGI("AUTOPLAY END: donation test");
            }
            if (next) ++step;
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
    static bool splashSaved = false;
    if (!g_autoplay.title.empty() && !splashSaved && g_ui->screen() == ui::Screen::Splash) {
        static uint64_t splashSince = 0;
        if (!splashSince) splashSince = now;
        if (now - splashSince > 200) {
            splashSaved = true;
            saveCanvas("splash.ppm");
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
    if (g_ui->reportOpen() && !g_autoplay.crashTest) {
        ui::NavInput in{};
        in.back = true;
        g_ui->handle(in);
    }
    if (g_autoplay.detailTest && uiSaved) {
        // A game to buy far down the list (no prefetched details): its
        // page must fill in on its own. Or the game named, when known.
        static uint64_t openedAt = 0;
        static uint64_t achAt = 0;
        static bool detailShot = false;
        ui::GameTile tile;
        if (!openedAt && (g_ui->findTile(g_autoplay.title, tile) || g_ui->findTile("FORTNITE", tile) ||
                          g_ui->purchasableAt(40, tile) || g_ui->firstTile(tile))) {
            XC_LOGI("autoplay: opening %s (%s)", tile.name.c_str(), tile.productId.c_str());
            ui::SettingsChoice c = g_ui->settings();
            if (!tile.productId.empty()) c.titlePlaytime[tile.productId] = 5520;
            if (!tile.titleId.empty()) c.titlePlaytime[tile.titleId] = 5520;
            g_ui->setSettings(c);
            if (!tile.xboxTitleId.empty()) {
                xcloud::TitleAchievements ach;
                ach.totalCount = 30;
                ach.unlockedCount = 12;
                ach.totalGamerscore = 1000;
                ach.unlockedGamerscore = 420;
                for (int i = 0; i < 15; ++i) {
                    xcloud::Achievement a;
                    a.id = std::to_string(i + 1);
                    a.name = "Achievement " + std::to_string(i + 1);
                    a.description = "Complete challenge " + std::to_string(i + 1);
                    a.gamerscore = (i + 1) * 20;
                    a.unlocked = (i < 6);
                    a.isSecret = (i == 10 || i == 11);
                    ach.items.push_back(a);
                }
                g_ui->setTitleAchievements(tile.xboxTitleId, ach);
            }
            g_ui->showDetails(tile);
            openedAt = now;
        } else if (openedAt && !detailShot && now - openedAt > 6000) {
            saveCanvas("detail.ppm");
            detailShot = true;
            ui::NavInput in{};
            in.right = true;
            g_ui->handle(in);
            achAt = now;
        } else if (achAt && now - achAt > 1500 && now - achAt <= 3000) {
            static bool focusShot = false;
            if (!focusShot) {
                saveCanvas("detail_focus_ach.ppm");
                focusShot = true;
                ui::NavInput in{};
                in.accept = true;
                g_ui->handle(in);
            }
        } else if (achAt && now - achAt > 4000) {
            saveCanvas("achievements.ppm");
            g_autoplay.detailTest = false;
            XC_LOGI("AUTOPLAY END: detail test");
        }
    }
    if (keysShot) {
        saveCanvas(keysShot);
        keysShot = nullptr;
    }
    if (settingsShot) {
        settingsShot = false;
        saveCanvas("settings.ppm");
        XC_LOGI("AUTOPLAY END: settings test");
    }
    if (donationShot) {
        donationShot = false;
        saveCanvas("donation.ppm");
        XC_LOGI("AUTOPLAY END: donation screenshot saved");
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
        if (!homeSince) {
            homeSince = now;
            if (g_autoplay.detailTest) {
                ui::GameTile first;
                if (g_ui->firstTile(first)) {
                    ui::SettingsChoice c = g_ui->settings();
                    if (!first.productId.empty()) c.titlePlaytime[first.productId] = 5520;
                    if (!first.titleId.empty()) c.titlePlaytime[first.titleId] = 5520;
                    g_ui->setSettings(c);
                }
            }
        }
        uint64_t waitMs = g_autoplay.friendsTest ? 8000 : 5000;
        if (now - homeSince > waitMs) {
            uiSaved = true;
            saveCanvas("ui.ppm");
            if (g_autoplay.title == "AUTOTEST" && !g_autoplay.crashTest && g_ui->reportOpen())
                XC_LOGI("AUTOPLAY END: crash report shown");
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
    if (regionShot) {
        saveCanvas(regionShot);
        regionShot = nullptr;
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
        bool home = g_ui->screen() == ui::Screen::Home;
        if (confirmBefore) {
            // The user's own confirm button back (the test saved Circle).
            g_ui->setSettings(*confirmBefore);
            saveSettingsChoice(*confirmBefore);
            confirmBefore.reset();
        }
        XC_LOGI("AUTOPLAY END: confirm test, on the %s screen (the confirm button put back as it was)",
                home ? "home" : "WRONG");
    }
    if (g_autoplay.crashTest && uiSaved) {
        XC_LOGI("AUTOPLAY END: crash test (SIGSEGV now)");
        volatile int* nullPtr = nullptr;
        asm volatile("" : "+r"(nullPtr));
        *nullPtr = 1;
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
    if (g_autoplay.publisherTest && uiSaved) {
        static uint64_t since = 0;
        static int step = 0;
        if (!since) since = now;
        if (step == 0 && now - since > 2000) {
            // Abre busca no Game Pass com o dropdown de editora aberto (linha 1, índice 2)
            g_ui->showFilteredSearch(ui::Tab::GamePass, false, 0, "", 0, 2);
            ++step;
        } else if (step == 1 && now - since > 4000) {
            saveCanvas("publisher_menu.ppm");
            // Filtra por Xbox Game Studios / Bethesda (índice 1)
            g_ui->chooseFilter(ui::AppUi::Filter::Publisher, 1);
            ++step;
        } else if (step == 2 && now - since > 6000) {
            saveCanvas("publisher_results.ppm");
            // Filtra também por plano Ultimate (índice 1) para exibir badges de plano nos cards
            g_ui->chooseFilter(ui::AppUi::Filter::Plan, 1);
            ++step;
        } else if (step == 3 && now - since > 8000) {
            saveCanvas("plan_results.ppm");
            g_autoplay.publisherTest = false;
            XC_LOGI("AUTOPLAY END: publisher test");
        }
    }
    if (g_autoplay.friendsTest && uiSaved) {
        static uint64_t since = 0;
        if (!since) since = now;
        if (now - since > 5000) {
            saveCanvas("friends_row.ppm");
            g_autoplay.friendsTest = false;
            XC_LOGI("AUTOPLAY END: friends test");
        }
    }
    if (g_autoplay.streamMenuTest && uiSaved) {
        static uint64_t since = 0;
        static int step = 0;
        if (!since) since = now;
        if (step == 0 && now - since > 2000) {
            static media::AudioIn s_audioInProbe;
            bool ok = s_audioInProbe.open([](const int16_t*, size_t){});
            XC_LOGI("AUTOPLAY: AudioIn probe open result: %s (recording: %s)",
                    ok ? "SUCCESS" : "FAILED", s_audioInProbe.isRecording() ? "YES" : "NO");

            ui::StreamMenu menu(g_ui->fonts());
            menu.open(0, false, 1, 1, 0, false, true, false, 2, 15, false, 0.65f, false);
            menu.setGyro(2, 7);  // gyro aiming while aiming, intensity 7, its row selected
            for (int i = 0; i < 10; ++i) {
                ui::NavInput down;
                down.down = true;
                menu.handle(down);
            }
            ui::StreamInfo info;
            info.region = "East US";
            info.rttMs = 18;
            info.fps = 60.0;
            info.mbps = 14.5;
            info.lossPct = 0.0;
            info.decodeMs = 2.1;
            info.onScreenMs = 12.4;
            info.width = 1920;
            info.height = 1080;
            ui::PadSlots pads{};
            pads[0].connected = true;
            pads[0].name = "Player 1";
            menu.setPads(pads);
            ui::Canvas menuCanvas = menu.renderMenu(info);
            std::string ppm = "P6\n" + std::to_string(menuCanvas.width()) + " " + std::to_string(menuCanvas.height()) + "\n255\n";
            ppm.reserve(ppm.size() + static_cast<size_t>(menuCanvas.width() * menuCanvas.height() * 3));
            for (size_t i = 0; i < static_cast<size_t>(menuCanvas.width() * menuCanvas.height()); ++i) {
                uint32_t p = menuCanvas.data()[i];
                ppm += static_cast<char>(p & 0xFF);
                ppm += static_cast<char>((p >> 8) & 0xFF);
                ppm += static_cast<char>((p >> 16) & 0xFF);
            }
            platform::writeFileAtomic(platform::dataDir() + "/stream_menu.ppm", ppm);
            XC_LOGI("AUTOPLAY: stream_menu.ppm saved");
            ++step;
        } else if (step == 1 && now - since > 4000) {
            ui::StreamMenu menu(g_ui->fonts());
            menu.open(0, false, 1, 1, 0, false, true, false, 2, 15, false, 0.0f, true);
            ui::StreamInfo info;
            info.region = "East US";
            info.rttMs = 18;
            info.fps = 60.0;
            info.mbps = 14.5;
            info.lossPct = 0.0;
            info.decodeMs = 2.1;
            info.onScreenMs = 12.4;
            info.width = 1920;
            info.height = 1080;
            ui::PadSlots pads{};
            pads[0].connected = true;
            pads[0].name = "Player 1";
            menu.setPads(pads);
            ui::Canvas menuCanvas = menu.renderMenu(info);
            std::string ppm = "P6\n" + std::to_string(menuCanvas.width()) + " " + std::to_string(menuCanvas.height()) + "\n255\n";
            ppm.reserve(ppm.size() + static_cast<size_t>(menuCanvas.width() * menuCanvas.height() * 3));
            for (size_t i = 0; i < static_cast<size_t>(menuCanvas.width() * menuCanvas.height()); ++i) {
                uint32_t p = menuCanvas.data()[i];
                ppm += static_cast<char>(p & 0xFF);
                ppm += static_cast<char>((p >> 8) & 0xFF);
                ppm += static_cast<char>((p >> 16) & 0xFF);
            }
            platform::writeFileAtomic(platform::dataDir() + "/stream_menu_muted.ppm", ppm);
            g_autoplay.streamMenuTest = false;
            XC_LOGI("AUTOPLAY END: stream menu test");
            ++step;
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
