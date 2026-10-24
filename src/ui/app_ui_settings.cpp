// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
#include "ui/app_ui_internal.h"

namespace xc::ui {

bool AppUi::settingsLightBar(int& mode, Color& colour) const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (screen_ != Screen::Settings) return false;
    mode = pickerOpen_ ? 1 : settings_.lightBarMode;
    colour = pickerOpen_ ? pickerColour() : settings_.lightBarColour;
    return true;
}

bool AppUi::settingsTriggerFeel(int& strength, int& hzIndex, int& resistance, bool& pulses, int& profile) const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (screen_ != Screen::Settings && !gameSettingsOpen_) return false;
    strength = settings_.triggerStrength;
    hzIndex = settings_.triggerHz;
    resistance = settings_.triggerResistance;
    pulses = settings_.triggerPulses;
    profile = settings_.triggerProfile;
    return true;
}

bool AppUi::triggerTest(float& l2, float& r2) const {
    std::lock_guard<std::mutex> lock(mutex_);
    if ((screen_ != Screen::Settings && !gameSettingsOpen_) || tester_ != Tester::Triggers) return false;
    l2 = testL2_;
    r2 = testR2_;
    return true;
}

void AppUi::openTester(Tester t) {
    // Caller holds mutex_. Circle puts back what was there.
    tester_ = t;
    testerRow_ = 0;
    testerStick_ = 0;
    testerBackup_ = settings_;
    keysRow_ = keysCol_ = 0;
    captureSince_ = 0;
    keyRefused_ = 0;
    dirty_ = true;
}

bool AppUi::capturingKey() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return tester_ == Tester::Keys && captureSince_ && (screen_ == Screen::Settings || gameSettingsOpen_);
}

void AppUi::captureKey(uint8_t code) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (tester_ != Tester::Keys || !captureSince_ || keysRow_ >= kKeysPerColumn) return;
    dirty_ = true;
    if (input::isReservedKey(code)) {  // still waiting: another key
        keyRefused_ = code;
        return;
    }
    auto action = static_cast<input::KeyAction>((keysCol_ / 2) * kKeysPerColumn + keysRow_);
    input::assignKey(settings_.keyMap, action, keysCol_ % 2, code);
    captureSince_ = 0;
    keyRefused_ = 0;
}

void AppUi::handleKeysTester(const NavInput& in) {
    // Caller holds mutex_. Each change applies at once; Circle goes back
    // with them (as Settings itself), "Reset to default" puts the first back.
    auto& map = settings_.keyMap;
    if (captureSince_) {
        // Waiting for a key (captureKey): Circle or 10 s give up.
        if (in.back || (in.nowMs > captureSince_ && in.nowMs - captureSince_ > 10000))
            captureSince_ = 0, keyRefused_ = 0, dirty_ = true;
        return;
    }
    constexpr int kRows = kKeysPerColumn + 1;
    if (in.up) keysRow_ = (keysRow_ + kRows - 1) % kRows, dirty_ = true;
    if (in.down) keysRow_ = (keysRow_ + 1) % kRows, dirty_ = true;
    if (keysRow_ < kKeysPerColumn) {
        if (in.left && keysCol_ > 0) --keysCol_, dirty_ = true;
        if (in.right && keysCol_ < 3) ++keysCol_, dirty_ = true;
    }
    size_t action = static_cast<size_t>((keysCol_ / 2) * kKeysPerColumn + keysRow_);
    if (in.accept) {
        if (keysRow_ == kKeysPerColumn)
            map = input::defaultKeyMap();
        else
            captureSince_ = in.nowMs ? in.nowMs : 1;
        keyRefused_ = 0;
        dirty_ = true;
    }
    if (in.square && keysRow_ < kKeysPerColumn) map[action][static_cast<size_t>(keysCol_ % 2)] = 0, dirty_ = true;
    if (in.back) {
        if (gameSettingsOpen_) {
            if (map != activeGameProfile_.keyMap) activeGameProfile_.keyMap = map, activeGameHasCustom_ = true;
            settings_ = testerBackup_;
        }
        tester_ = Tester::None;
        dirty_ = true;
    }
}

void AppUi::openPadTester() {
    // Caller holds mutex_. A copy of the map and touchpad shortcuts in force
    // (Settings', or the game's with its settings open) is what the screen edits.
    tester_ = Tester::Pad;
    padWork_ = gameSettingsOpen_ ? activeGameProfile_.padMap : settings_.padMap;
    if (!input::padMapValid(padWork_)) padWork_ = input::defaultPadMap();
    touchpadWork_ = gameSettingsOpen_ ? activeGameProfile_.touchpadShortcuts : settings_.touchpadShortcuts;
    padRow_ = padCol_ = 0;
    padCaptureSince_ = 0;
    padTesterTab_ = 0;
    touchpadRow_ = 0;
    touchpadDropdownOpen_ = false;
    touchpadDropdownIndex_ = 0;
    touchpadDropdownTop_ = 0;
    dirty_ = true;
}

void AppUi::handlePadTester(const NavInput& in) {
    if (touchpadDropdownOpen_) {
        constexpr int kMaxVisible = 8;
        constexpr int n = input::kTouchpadActions;
        if (in.up) {
            touchpadDropdownIndex_ = (touchpadDropdownIndex_ + n - 1) % n;
            if (touchpadDropdownIndex_ < touchpadDropdownTop_) touchpadDropdownTop_ = touchpadDropdownIndex_;
            else if (touchpadDropdownIndex_ >= touchpadDropdownTop_ + kMaxVisible)
                touchpadDropdownTop_ = touchpadDropdownIndex_ - kMaxVisible + 1;
            dirty_ = true;
        }
        if (in.down) {
            touchpadDropdownIndex_ = (touchpadDropdownIndex_ + 1) % n;
            if (touchpadDropdownIndex_ < touchpadDropdownTop_) touchpadDropdownTop_ = touchpadDropdownIndex_;
            else if (touchpadDropdownIndex_ >= touchpadDropdownTop_ + kMaxVisible)
                touchpadDropdownTop_ = touchpadDropdownIndex_ - kMaxVisible + 1;
            dirty_ = true;
        }
        if (in.accept) {
            touchpadWork_.set(touchpadRow_, static_cast<input::TouchpadAction>(touchpadDropdownIndex_));
            touchpadDropdownOpen_ = false;
            dirty_ = true;
        }
        if (in.back) {
            touchpadDropdownOpen_ = false;
            dirty_ = true;
        }
        return;
    }

    if (caps_.touchpad && (in.l1 || in.r1)) {
        padTesterTab_ = 1 - padTesterTab_;
        dirty_ = true;
        return;
    }

    if (padTesterTab_ == 1) {
        constexpr int kRows = input::kTouchpadTriggers + 1;  // 0..5 triggers, 6 Reset to default
        if (in.up) touchpadRow_ = (touchpadRow_ + kRows - 1) % kRows, dirty_ = true;
        if (in.down) touchpadRow_ = (touchpadRow_ + 1) % kRows, dirty_ = true;
        if (touchpadRow_ < input::kTouchpadTriggers) {
            if (in.accept) {
                touchpadDropdownOpen_ = true;
                touchpadDropdownIndex_ = static_cast<int>(touchpadWork_.get(touchpadRow_));
                constexpr int kMaxVisible = 8;
                touchpadDropdownTop_ = std::clamp(touchpadDropdownIndex_ - 3, 0, std::max(0, input::kTouchpadActions - kMaxVisible));
                dirty_ = true;
            }
            if (in.square) {
                touchpadWork_.set(touchpadRow_, input::defaultTouchpadShortcuts().get(touchpadRow_));
                dirty_ = true;
            }
        } else if (in.accept) {
            touchpadWork_ = input::defaultTouchpadShortcuts();
            dirty_ = true;
        }
        if (in.back) {
            dirty_ = true;
            if (!input::padMapValid(padWork_)) {
                padTesterTab_ = 0;
                return;
            }
            if (gameSettingsOpen_) {
                if (padWork_ != activeGameProfile_.padMap) activeGameProfile_.padMap = padWork_, activeGameHasCustom_ = true;
                if (touchpadWork_ != activeGameProfile_.touchpadShortcuts) activeGameProfile_.touchpadShortcuts = touchpadWork_, activeGameHasCustom_ = true;
            } else {
                settings_.padMap = padWork_;
                settings_.touchpadShortcuts = touchpadWork_;
            }
            tester_ = Tester::None;
        }
        return;
    }

    const bool mapSwap = gameSettingsOpen_ ? activeGameProfile_.circleConfirms : settings_.circleConfirms;
    constexpr int kPerColumn = input::kPadFns / 2, kButtonsRow = kPerColumn;
    const auto fn = static_cast<input::PadFn>(padCol_ * kPerColumn + std::min(padRow_, kPerColumn - 1));
    if (padCaptureSince_) {
        if (in.padPressed) {
            uint8_t slot = 0;
            while (!(in.padPressed & (1u << slot))) ++slot;
            const uint8_t onPad = input::physicalSource(slot, circleConfirms_);
            input::assignPadSource(padWork_, fn, input::physicalSource(onPad, mapSwap));
            padCaptureSince_ = 0;
            dirty_ = true;
        } else if (in.nowMs > padCaptureSince_ && in.nowMs - padCaptureSince_ > 5000) {
            padCaptureSince_ = 0;
            dirty_ = true;
        }
        return;
    }
    if (in.up) padRow_ = (padRow_ + kButtonsRow) % (kButtonsRow + 1), dirty_ = true;
    if (in.down) padRow_ = (padRow_ + 1) % (kButtonsRow + 1), dirty_ = true;
    if (in.left || in.right) padCol_ = 1 - padCol_, dirty_ = true;
    if (in.accept) {
        dirty_ = true;
        if (padRow_ < kButtonsRow) {
            padCaptureSince_ = in.nowMs ? in.nowMs : 1;
        } else if (padCol_ == 0) {
            padWork_ = input::defaultPadMap();  // Reset to default (kept on the way out)
        } else {
            tester_ = Tester::None;  // Discard changes: what was saved stays
            return;
        }
    }
    if (in.square && padRow_ < kButtonsRow) input::assignPadSource(padWork_, fn, input::kPadNone), dirty_ = true;
    if (in.back) {
        dirty_ = true;
        if (!input::padMapValid(padWork_)) return;  // incomplete: the warning says what's missing
        if (gameSettingsOpen_) {
            if (padWork_ != activeGameProfile_.padMap) activeGameProfile_.padMap = padWork_, activeGameHasCustom_ = true;
            if (touchpadWork_ != activeGameProfile_.touchpadShortcuts) activeGameProfile_.touchpadShortcuts = touchpadWork_, activeGameHasCustom_ = true;
        } else {
            settings_.padMap = padWork_;
            settings_.touchpadShortcuts = touchpadWork_;
        }
        tester_ = Tester::None;
    }
}

void AppUi::openTouchpadTester() {
    openPadTester();
    padTesterTab_ = 1;
}

void AppUi::handleTouchpadTester(const NavInput& in) {
    handlePadTester(in);
}

void AppUi::handleTester(const NavInput& in) {
    // Caller holds mutex_. Changes apply as they are made (the pad follows);
    // Cross keeps them, Circle puts the old ones back.
    if (tester_ == Tester::Keys) return handleKeysTester(in);
    if (tester_ == Tester::Pad) return handlePadTester(in);
    if (tester_ == Tester::Touchpad) return handleTouchpadTester(in);
    if (tester_ == Tester::Sticks) {
        // L1 / R1 pick the stick, the D-pad (only: the sticks are what is
        // being tried) changes its dead zone.
        if (in.l1 || in.r1) testerStick_ = in.r1 ? 1 : 0, dirty_ = true;
        int& zone = settings_.deadzone[testerStick_];
        int dz = std::clamp(zone + (in.dpadRight ? 1 : 0) - (in.dpadLeft ? 1 : 0), 0, kMaxDeadzone);
        if (dz != zone) zone = dz, dirty_ = true;
        const float now[4] = {in.rawLX, in.rawLY, in.rawRX, in.rawRY};
        for (int i = 0; i < 4; ++i)
            if (now[i] != testSticks_[i]) testSticks_[i] = now[i], dirty_ = true;
    } else {
        if (in.up && testerRow_ > 0) --testerRow_, dirty_ = true;
        if (in.down && testerRow_ < triggerTesterRows() - 1) ++testerRow_, dirty_ = true;
        int delta = (in.right ? 1 : 0) - (in.left ? 1 : 0);
        if (delta && testerRow_ == 0)
            settings_.triggerStrength = std::clamp(settings_.triggerStrength + delta, 0, input::kTriggerStrengths - 1);
        if (delta && testerRow_ == 1) settings_.triggerHz = std::clamp(settings_.triggerHz + delta, 0, 2);
        if (delta && testerRow_ == 2)
            settings_.triggerResistance = std::clamp(settings_.triggerResistance + delta, 0, input::kTriggerResistances - 1);
        if (delta && testerRow_ == 3) settings_.triggerPulses = !settings_.triggerPulses;
        if (delta && testerRow_ == 4) settings_.triggerProfile = std::clamp(settings_.triggerProfile + delta, 0, input::kTriggerProfiles - 1);
        if (delta) dirty_ = true;
        if (in.l2Analog != testL2_ || in.r2Analog != testR2_) testL2_ = in.l2Analog, testR2_ = in.r2Analog, dirty_ = true;
    }
    if (in.back) {
        settings_.deadzone[0] = testerBackup_.deadzone[0], settings_.deadzone[1] = testerBackup_.deadzone[1];
        settings_.triggerStrength = testerBackup_.triggerStrength, settings_.triggerHz = testerBackup_.triggerHz;
        settings_.triggerResistance = testerBackup_.triggerResistance, settings_.triggerPulses = testerBackup_.triggerPulses;
        settings_.triggerProfile = testerBackup_.triggerProfile;
    }
    if (in.accept || in.back) {
        if (gameSettingsOpen_) {
            if (in.accept) {
                activeGameProfile_.deadzoneLeft = settings_.deadzone[0];
                activeGameProfile_.deadzoneRight = settings_.deadzone[1];
                activeGameProfile_.triggerStrength = settings_.triggerStrength;
                activeGameProfile_.triggerHz = settings_.triggerHz;
                activeGameProfile_.triggerResistance = settings_.triggerResistance;
                activeGameProfile_.triggerPulses = settings_.triggerPulses;
                activeGameProfile_.triggerProfile = settings_.triggerProfile;
                activeGameHasCustom_ = true;
            }
            settings_ = testerBackup_;
        }
        tester_ = Tester::None;
        testL2_ = testR2_ = 0;
        dirty_ = true;
    }
}

void AppUi::openColourPicker(int previousMode) {
    // Caller holds mutex_. Starts on the colour saved.
    float h, s, v;
    colourHsv(settings_.lightBarColour, h, s, v);
    pickU_ = s * std::cos(h * 6.2832f);
    pickV_ = s * std::sin(h * 6.2832f);
    pickValue_ = std::max(kMinValue, v);
    pickerPrevMode_ = previousMode;
    pickerLastMs_ = 0;
    pickerOpen_ = true;
    dirty_ = true;
}

Color AppUi::pickerColour() const {
    float h = std::atan2(pickV_, pickU_) / 6.2832f;
    return hsvColour(h < 0 ? h + 1 : h, std::min(1.0f, std::hypot(pickU_, pickV_)), pickValue_);
}

void AppUi::handleColourPicker(const NavInput& in) {
    // Caller holds mutex_. The stick moves the cursor smoothly, the D-pad a
    // step at a time; L2 / R2 change the brightness.
    float dt = pickerLastMs_ && in.nowMs > pickerLastMs_ ? std::min(0.1f, (in.nowMs - pickerLastMs_) / 1000.0f) : 0;
    pickerLastMs_ = in.nowMs;
    float u = pickU_, v = pickV_;
    auto axis = [](float a) { return std::abs(a) < 0.2f ? 0.0f : a; };
    u += axis(in.stickX) * 1.1f * dt;
    v += axis(in.stickY) * 1.1f * dt;
    constexpr float kStep = 0.05f;
    if (in.left) u -= kStep;
    if (in.right) u += kStep;
    if (in.up) v -= kStep;
    if (in.down) v += kStep;
    if (float d = std::hypot(u, v); d > 1) u /= d, v /= d;
    if (u != pickU_ || v != pickV_) pickU_ = u, pickV_ = v, dirty_ = true;
    if (in.l2) pickValue_ = std::max(kMinValue, pickValue_ - 0.1f), dirty_ = true;
    if (in.r2) pickValue_ = std::min(1.0f, pickValue_ + 0.1f), dirty_ = true;
    if (in.accept) {
        settings_.lightBarColour = pickerColour();
        settings_.lightBarMode = 1;
        pickerOpen_ = false;
        dirty_ = true;
    } else if (in.back) {
        settings_.lightBarMode = pickerPrevMode_;
        pickerOpen_ = false;
        dirty_ = true;
    }
}

void AppUi::setGyroDefaults(int mode, int level) {
    std::lock_guard<std::mutex> lock(mutex_);
    gyroMode_ = mode;
    gyroLevel_ = level;
    settings_.gyroMode = mode;
    settings_.gyroLevel = level;
    dirty_ = true;
}

void AppUi::setPerGameSettings(const std::map<std::string, app::GameProfile>& perGame) {
    std::lock_guard<std::mutex> lock(mutex_);
    perGameSettings_ = perGame;
    dirty_ = true;
}

void AppUi::openGameSettings() {
    gameSettingsOpen_ = true;
    gameSettingsRow_ = 0;
    activeGameMouseSupported_ = detail_.mouseKeyboard;
    std::string key = app::gameProfileKey(detail_.productId, detail_.titleId);
    auto it = perGameSettings_.find(key);
    if (it != perGameSettings_.end()) {
        activeGameHasCustom_ = true;
        activeGameProfile_ = it->second;
    } else {
        activeGameHasCustom_ = false;
        activeGameProfile_.sharpness = settings_.sharpness;
        activeGameProfile_.deband = settings_.deband;
        activeGameProfile_.upscaler = settings_.upscaler;
        activeGameProfile_.resolution = settings_.resolution;
        activeGameProfile_.deadzoneLeft = settings_.deadzone[0];
        activeGameProfile_.deadzoneRight = settings_.deadzone[1];
        activeGameProfile_.triggerStrength = settings_.triggerStrength;
        activeGameProfile_.triggerHz = settings_.triggerHz;
        activeGameProfile_.triggerResistance = settings_.triggerResistance;
        activeGameProfile_.triggerPulses = settings_.triggerPulses;
        activeGameProfile_.audioHaptics = settings_.audioHaptics;
        activeGameProfile_.circleConfirms = settings_.circleConfirms;
        activeGameProfile_.highRefresh = settings_.highRefresh;
        activeGameProfile_.keyMap = settings_.keyMap;
        activeGameProfile_.padMap = settings_.padMap;
        activeGameProfile_.gyroMode = gyroMode_;
        activeGameProfile_.gyroLevel = gyroLevel_;
        activeGameProfile_.touchpadMouse = settings_.touchpadMouse;
        activeGameProfile_.touchpadShortcuts = settings_.touchpadShortcuts;
    }
}

void AppUi::closeGameSettings() {
    gameSettingsOpen_ = false;
    tester_ = Tester::None;
}

void AppUi::setRegions(std::vector<std::string> regions, const std::string& defaultRegion) {
    std::lock_guard<std::mutex> lock(mutex_);
    regions_ = std::move(regions);
    defaultRegion_ = defaultRegion;
    dirty_ = true;
}

std::vector<std::string> AppUi::settingOptions(int row) const {
    // Caller holds mutex_.
    std::vector<std::string> out;
    auto withMs = [&](std::string label, const std::string& region) {
        auto ms = regionMs_.find(region);
        if (ms != regionMs_.end()) label += "  \xE2\x80\xA2  " + std::to_string(ms->second) + " ms";
        return label;
    };
    if (row == 0) {
        for (int i = 0; i < static_cast<int>(Language::Count); ++i) out.push_back(languageName(static_cast<Language>(i)));
    } else if (row == 1) {
        out = {tr(Str::Res720), tr(Str::Res1080), tr(Str::ResBest)};  // kResolutionOrder
    } else if (row == 3) {
        // Left and right stick (the tester changes them).
        out = {std::to_string(settings_.deadzone[0]) + " %  \xC2\xB7  " + std::to_string(settings_.deadzone[1]) + " %"};
    } else if (row == 4) {
        out = {tr(Str::Deactivated), tr(Str::TriggerLight), tr(Str::TriggerMedium), tr(Str::TriggerStrong),
               tr(Str::TriggerMax)};
    } else if (row == 5) {
        out = {"", ""};  // Cross, Circle: drawn as the buttons
    } else if (row == 6) {
        out = {tr(Str::LightBarGame), tr(Str::LightBarCustom), tr(Str::Deactivated)};
    } else if (row == 7) {
        // Updates: Cross acts, no list.
    } else if (row == 9) {
        out = {tr(Str::Deactivated), tr(Str::HighRefreshOn)};
    } else if (row == 13) {
        out = {tr(Str::DisplayWindow), tr(Str::DisplayFullscreen)};
    } else if (row == 10) {
        out.push_back(tr(Str::BitrateAuto));
        for (int mbps : kBitrateCaps) out.push_back(std::to_string(mbps) + " Mbps");
    } else if (row == 11) {
        for (int v : kVolumes) out.push_back(std::to_string(v) + " %");
    } else if (row == 16) {
        out = {tr(Str::Deactivated), tr(Str::Activated)};
    } else if (row == 12) {
        // The keys' own screen changes them.
        out = {tr(settings_.keyMap == input::defaultKeyMap() ? Str::ProfileDefault : Str::ProfileCustom)};
    } else if (row == 14) {
        // So do the controller buttons'.
        out = {tr(settings_.padMap == input::defaultPadMap() ? Str::ProfileDefault : Str::ProfileCustom)};
    } else if (row == 22) {
        out = {tr(Str::Deactivated), tr(Str::Activated)};
    } else if (row == 15) {
        out = {tr(Str::Off), tr(Str::GyroAlways), tr(Str::GyroAiming)};
    } else if (row == 17) {
        out = {tr(Str::RunCheck)};
    } else if (row == 18) {
        out = {tr(Str::RunTest)};
    } else if (row == 19) {
        out = {tr(Str::SupportKoFi)};
    } else if (row == 20) {
        out = {tr(Str::VideoDecoderHw), tr(Str::VideoDecoderSw)};
    } else if (row == 21) {
        out = {tr(Str::Off), tr(Str::SharpLow), tr(Str::SharpMedium), tr(Str::SharpHigh)};
    } else if (row == 23) {
        out = {tr(Str::ResetSettings)};
    } else if (row == 8) {
        out.push_back(loginCountryOffText());
        for (const char* country : kLoginCountries) out.push_back(loginCountryName(country));
    } else {
        // Automatic first, then the regions in the login's order.
        out.push_back(withMs(trf(Str::RegionAuto, defaultRegion_.empty() ? "-" : prettyRegion(defaultRegion_)),
                             defaultRegion_));
        for (const auto& r : regions_) out.push_back(withMs(prettyRegion(r), r));
    }
    return out;
}

int AppUi::settingSelected(int row) const {
    // Caller holds mutex_.
    if (row == 0) return settings_.language;
    if (row == 1) {
        for (int i = 0; i < 3; ++i)
            if (kResolutionOrder[i] == settings_.resolution) return i;
    }
    if (row == 3 || row == 12 || row == 14 || row == 17 || row == 18 || row == 19 || row == 23) return 0;
    if (row == 4) return settings_.triggerStrength;
    if (row == 5) return settings_.circleConfirms ? 1 : 0;
    if (row == 6) return settings_.lightBarMode;
    if (row == 7) return 0;
    if (row == 9) return settings_.highRefresh ? 1 : 0;
    if (row == 13) return settings_.fullscreen ? 1 : 0;
    if (row == 15) return std::clamp(settings_.gyroMode, 0, 2);
    if (row == 16) return settings_.queueReadyAlert ? 1 : 0;
    if (row == 20) return std::clamp(settings_.videoDecoder, 0, 1);
    if (row == 21) return std::clamp(settings_.audioHaptics, 0, 3);
    if (row == 22) return settings_.previewFeatures ? 1 : 0;
    if (row == 10) {
        if (settings_.maxBitrateMbps <= 0) return 0;
        int closest = 1;
        int minDiff = 1000;
        for (size_t i = 0; i < std::size(kBitrateCaps); ++i) {
            int diff = std::abs(kBitrateCaps[i] - settings_.maxBitrateMbps);
            if (diff < minDiff) {
                minDiff = diff;
                closest = static_cast<int>(i) + 1;
            }
        }
        return closest;
    }
    if (row == 11) {
        for (size_t i = 0; i < std::size(kVolumes); ++i)
            if (kVolumes[i] == settings_.gameVolume) return static_cast<int>(i);
        return 3;  // 100 %
    }
    if (row == 8) {
        for (size_t i = 0; i < std::size(kLoginCountries); ++i)
            if (settings_.loginCountry == kLoginCountries[i]) return static_cast<int>(i) + 1;
        return 0;
    }
    for (size_t i = 0; i < regions_.size(); ++i)
        if (regions_[i] == settings_.region) return static_cast<int>(i) + 1;
    return 0;
}

void AppUi::applySetting(int row, int index) {
    // Caller holds mutex_.
    if (row == 0) {
        settings_.language = index;
        setLanguage(static_cast<Language>(index));  // the UI switches right away
    } else if (row == 1) {
        settings_.resolution = kResolutionOrder[index];
    } else if (row == 3 || row == 12 || row == 14 || row == 17 || row == 18 || row == 23) {
        (void)index;  // the tester sets each stick's, the keys' and buttons' screens theirs
    } else if (row == 4) {
        settings_.triggerStrength = index;
    } else if (row == 5) {
        settings_.circleConfirms = index == 1;
        circleConfirms_ = settings_.circleConfirms;  // in force at once: the next press already uses it
    } else if (row == 6) {
        settings_.lightBarMode = index;
    } else if (row == 7) {
    } else if (row == 9) {
        settings_.highRefresh = index == 1;
    } else if (row == 13) {
        // In force at once (handle() sends the settings): the window follows.
        if (settings_.fullscreen != (index == 1)) displayModeChanged_ = true;
        settings_.fullscreen = index == 1;
    } else if (row == 15) {
        settings_.gyroMode = index;
        gyroMode_ = index;
    } else if (row == 10) {
        settings_.maxBitrateMbps = index == 0 ? 0 : kBitrateCaps[index - 1];
    } else if (row == 11) {
        settings_.gameVolume = kVolumes[index];
    } else if (row == 16) {
        settings_.queueReadyAlert = (index == 1);
    } else if (row == 20) {
        settings_.videoDecoder = index;
    } else if (row == 21) {
        settings_.audioHaptics = std::clamp(index, 0, 3);
    } else if (row == 22) {
        settings_.previewFeatures = (index == 1);
    } else if (row == 8) {
        settings_.loginCountry = index == 0 ? std::string() : kLoginCountries[index - 1];
    } else {
        settings_.region = index == 0 ? std::string() : regions_[static_cast<size_t>(index - 1)];
    }
    dirty_ = true;
}

void AppUi::changeSetting(int delta) {
    // Caller holds mutex_. Left / right: the previous / next choice.
    int n = static_cast<int>(settingOptions(settingsRow_).size());
    if (n <= 0) return;
    if (settingsRow_ == 10) {
        // Bitrate slider: clamp between 0 (Automatic) and n - 1 (50 Mbps)
        int cur = settingSelected(10);
        int next = std::clamp(cur + delta, 0, n - 1);
        if (next != cur) applySetting(10, next);
        return;
    }
    applySetting(settingsRow_, (settingSelected(settingsRow_) + delta + n) % n);
}

void AppUi::handleGameSettings(const NavInput& in, UiEvent& ev) {
    if (tester_ != Tester::None) {
        handleTester(in);
        if (tester_ == Tester::None) {
            std::string key = app::gameProfileKey(detail_.productId, detail_.titleId);
            if (activeGameHasCustom_) perGameSettings_[key] = activeGameProfile_;
            else perGameSettings_.erase(key);
            ev.action = Action::GameSettingsChanged;
            ev.game = detail_;
            ev.hasGameProfile = activeGameHasCustom_;
            ev.gameProfile = activeGameProfile_;
        }
        return;
    }
    // The rows on screen (gameSettingsRow_ is a position among them).
    const auto rows = visibleGameSettingsRows(caps_, activeGameHasCustom_, activeGameMouseSupported_);
    const int rowCount = static_cast<int>(rows.size());
    if (in.up) {
        gameSettingsRow_ = (gameSettingsRow_ + rowCount - 1) % rowCount;
        dirty_ = true;
    }
    if (in.down) {
        gameSettingsRow_ = (gameSettingsRow_ + 1) % rowCount;
        dirty_ = true;
    }
    const int rowId = rows[static_cast<size_t>(std::clamp(gameSettingsRow_, 0, rowCount - 1))].row;
    if (in.left || in.right) {
        int dir = in.right ? 1 : -1;
        switch (rowId) {
        case 0:
            activeGameHasCustom_ = !activeGameHasCustom_;
            dirty_ = true;
            break;
        case 1: { // Upscaler (0 FSR, 2 FSR+clean, 1 AI, 3 AI+clean)
            static constexpr int kOrder[4] = {0, 2, 1, 3};
            int pos = 0;
            for (int i = 0; i < 4; ++i) if (kOrder[i] == activeGameProfile_.upscaler) pos = i;
            activeGameProfile_.upscaler = kOrder[(pos + 4 + dir) % 4];
            activeGameHasCustom_ = true;
            dirty_ = true;
            break;
        }
        case 2: // Sharpness (0..3)
            activeGameProfile_.sharpness = std::clamp(activeGameProfile_.sharpness + dir, 0, 3);
            activeGameHasCustom_ = true;
            dirty_ = true;
            break;
        case 3: // Deband (0..3)
            activeGameProfile_.deband = std::clamp(activeGameProfile_.deband + dir, 0, 3);
            activeGameHasCustom_ = true;
            dirty_ = true;
            break;
        case 4: { // Resolution (1 = 720p, 0 = 1080p, 2 = Best (Auto))
            static constexpr int kOrder[3] = {1, 0, 2};
            int pos = 0;
            for (int i = 0; i < 3; ++i) if (kOrder[i] == activeGameProfile_.resolution) pos = i;
            activeGameProfile_.resolution = kOrder[(pos + 3 + dir) % 3];
            activeGameHasCustom_ = true;
            dirty_ = true;
            break;
        }
        case 5: // Deadzone (0..30)
            activeGameProfile_.deadzoneLeft = std::clamp(activeGameProfile_.deadzoneLeft + dir * 5, 0, 30);
            activeGameProfile_.deadzoneRight = activeGameProfile_.deadzoneLeft;
            activeGameHasCustom_ = true;
            dirty_ = true;
            break;
        case 6: // Triggers (0..4)
            activeGameProfile_.triggerStrength = std::clamp(activeGameProfile_.triggerStrength + dir, 0, 4);
            activeGameHasCustom_ = true;
            dirty_ = true;
            break;
        case 16: // Audio-to-Haptics (0..3)
            activeGameProfile_.audioHaptics = std::clamp(activeGameProfile_.audioHaptics + dir, 0, 3);
            activeGameHasCustom_ = true;
            dirty_ = true;
            break;
        case 7: // Confirm button
            activeGameProfile_.circleConfirms = !activeGameProfile_.circleConfirms;
            activeGameHasCustom_ = true;
            dirty_ = true;
            break;
        case 8: // 120 Hz output (VRR)
            activeGameProfile_.highRefresh = !activeGameProfile_.highRefresh;
            activeGameHasCustom_ = true;
            dirty_ = true;
            break;
        case 12: // Gyro aiming (0 off, 1 always, 2 while aiming)
            activeGameProfile_.gyroMode = (activeGameProfile_.gyroMode + 3 + dir) % 3;
            activeGameHasCustom_ = true;
            dirty_ = true;
            break;
        case 13: // Touchpad as mouse (in games with mouse support)
            activeGameProfile_.touchpadMouse = !activeGameProfile_.touchpadMouse;
            activeGameHasCustom_ = true;
            dirty_ = true;
            break;
        case 14: { // Game Language
            static const std::pair<const char*, const char*> kLocales[] = {
                {"", ""}, {"en-US", "English (US)"}, {"pt-BR", "Português (Brasil)"},
                {"es-ES", "Español"}, {"fr-FR", "Français"}, {"de-DE", "Deutsch"},
                {"it-IT", "Italiano"}, {"ja-JP", "日本語"}
            };
            int pos = 0;
            for (int k = 0; k < 8; ++k) if (kLocales[k].first == activeGameProfile_.locale) pos = k;
            activeGameProfile_.locale = kLocales[(pos + 8 + dir) % 8].first;
            activeGameHasCustom_ = true;
            dirty_ = true;
            break;
        }
        case 15: // Video decoder (0 Hardware, 1 Software)
            activeGameProfile_.videoDecoder = (activeGameProfile_.videoDecoder == 0 ? 1 : 0);
            activeGameHasCustom_ = true;
            dirty_ = true;
            break;
        default: break;
        }
    }
    if (in.accept) {
        switch (rowId) {
        case 0:
            activeGameHasCustom_ = !activeGameHasCustom_;
            dirty_ = true;
            break;
        case 1: {
            static constexpr int kOrder[4] = {0, 2, 1, 3};
            int pos = 0;
            for (int i = 0; i < 4; ++i) if (kOrder[i] == activeGameProfile_.upscaler) pos = i;
            activeGameProfile_.upscaler = kOrder[(pos + 1) % 4];
            activeGameHasCustom_ = true;
            dirty_ = true;
            break;
        }
        case 2:
            activeGameProfile_.sharpness = (activeGameProfile_.sharpness + 1) % 4;
            activeGameHasCustom_ = true;
            dirty_ = true;
            break;
        case 3:
            activeGameProfile_.deband = (activeGameProfile_.deband + 1) % 4;
            activeGameHasCustom_ = true;
            dirty_ = true;
            break;
        case 4: {
            static constexpr int kOrder[3] = {1, 0, 2};
            int pos = 0;
            for (int i = 0; i < 3; ++i) if (kOrder[i] == activeGameProfile_.resolution) pos = i;
            activeGameProfile_.resolution = kOrder[(pos + 1) % 3];
            activeGameHasCustom_ = true;
            dirty_ = true;
            break;
        }
        case 5: { // Open stick tester
            tester_ = Tester::Sticks;
            testerBackup_ = settings_;
            settings_.deadzone[0] = activeGameProfile_.deadzoneLeft;
            settings_.deadzone[1] = activeGameProfile_.deadzoneRight;
            testerStick_ = 0;
            dirty_ = true;
            break;
        }
        case 6: { // Open trigger tester
            tester_ = Tester::Triggers;
            testerBackup_ = settings_;
            settings_.triggerStrength = activeGameProfile_.triggerStrength;
            settings_.triggerHz = activeGameProfile_.triggerHz;
            settings_.triggerResistance = activeGameProfile_.triggerResistance;
            settings_.triggerPulses = activeGameProfile_.triggerPulses;
            settings_.triggerProfile = activeGameProfile_.triggerProfile;
            testerRow_ = 0;
            dirty_ = true;
            break;
        }
        case 16:
            activeGameProfile_.audioHaptics = (activeGameProfile_.audioHaptics + 1) % 4;
            activeGameHasCustom_ = true;
            dirty_ = true;
            break;
        case 7:
            activeGameProfile_.circleConfirms = !activeGameProfile_.circleConfirms;
            activeGameHasCustom_ = true;
            dirty_ = true;
            break;
        case 8:
            activeGameProfile_.highRefresh = !activeGameProfile_.highRefresh;
            activeGameHasCustom_ = true;
            dirty_ = true;
            break;
        case 12:
            activeGameProfile_.gyroMode = (activeGameProfile_.gyroMode + 1) % 3;
            activeGameHasCustom_ = true;
            dirty_ = true;
            break;
        case 13:
            activeGameProfile_.touchpadMouse = !activeGameProfile_.touchpadMouse;
            activeGameHasCustom_ = true;
            dirty_ = true;
            break;
        case 14: {
            static const std::pair<const char*, const char*> kLocales[] = {
                {"", ""}, {"en-US", "English (US)"}, {"pt-BR", "Português (Brasil)"},
                {"es-ES", "Español"}, {"fr-FR", "Français"}, {"de-DE", "Deutsch"},
                {"it-IT", "Italiano"}, {"ja-JP", "日本語"}
            };
            int pos = 0;
            for (int k = 0; k < 8; ++k) if (kLocales[k].first == activeGameProfile_.locale) pos = k;
            activeGameProfile_.locale = kLocales[(pos + 1) % 8].first;
            activeGameHasCustom_ = true;
            dirty_ = true;
            break;
        }
        case 15:
            activeGameProfile_.videoDecoder = (activeGameProfile_.videoDecoder == 0 ? 1 : 0);
            activeGameHasCustom_ = true;
            dirty_ = true;
            break;
        case 9: // Keyboard keys: their own screen, on this game's
            openTester(Tester::Keys);
            settings_.keyMap = activeGameProfile_.keyMap;
            break;
        case 11: // Controller buttons: their own screen, on this game's
            openPadTester();
            break;
        case 17: // Touchpad shortcuts: their own screen, on this game's
            openTouchpadTester();
            break;
        case 10: // Reset to default
            activeGameHasCustom_ = false;
            activeGameProfile_.locale.clear();
            activeGameProfile_.keyMap = settings_.keyMap;
            activeGameProfile_.padMap = settings_.padMap;
            activeGameProfile_.touchpadShortcuts = settings_.touchpadShortcuts;
            activeGameProfile_.sharpness = settings_.sharpness;
            activeGameProfile_.deband = settings_.deband;
            activeGameProfile_.upscaler = settings_.upscaler;
            activeGameProfile_.resolution = settings_.resolution;
            activeGameProfile_.deadzoneLeft = settings_.deadzone[0];
            activeGameProfile_.deadzoneRight = settings_.deadzone[1];
            activeGameProfile_.triggerStrength = settings_.triggerStrength;
            activeGameProfile_.triggerHz = settings_.triggerHz;
            activeGameProfile_.triggerResistance = settings_.triggerResistance;
            activeGameProfile_.triggerPulses = settings_.triggerPulses;
            activeGameProfile_.triggerProfile = settings_.triggerProfile;
            activeGameProfile_.audioHaptics = settings_.audioHaptics;
            activeGameProfile_.circleConfirms = settings_.circleConfirms;
            activeGameProfile_.highRefresh = settings_.highRefresh;
            activeGameProfile_.gyroMode = gyroMode_;
            activeGameProfile_.gyroLevel = gyroLevel_;
            activeGameProfile_.touchpadMouse = settings_.touchpadMouse;
            gameSettingsRow_ = 0;
            dirty_ = true;
            break;
        default: break;
        }
    }
    if ((in.left || in.right || in.accept) && tester_ == Tester::None) {
        std::string key = app::gameProfileKey(detail_.productId, detail_.titleId);
        if (activeGameHasCustom_) {
            perGameSettings_[key] = activeGameProfile_;
        } else {
            perGameSettings_.erase(key);
        }
        ev.action = Action::GameSettingsChanged;
        ev.game = detail_;
        ev.hasGameProfile = activeGameHasCustom_;
        ev.gameProfile = activeGameProfile_;
    }
    if (in.back || in.options) {
        gameSettingsOpen_ = false;
        std::string key = app::gameProfileKey(detail_.productId, detail_.titleId);
        if (activeGameHasCustom_) {
            perGameSettings_[key] = activeGameProfile_;
        } else {
            perGameSettings_.erase(key);
        }
        ev.action = Action::GameSettingsChanged;
        ev.game = detail_;
        ev.hasGameProfile = activeGameHasCustom_;
        ev.gameProfile = activeGameProfile_;
        dirty_ = true;
    }
}

void AppUi::drawSettings(Canvas& c) {
    drawBackground(c);
    drawTopBar(c);
    fonts_.bold.draw(c, tr(Str::Settings), kMargin, 116, 46, kWhite);
    const char* labels[kSettingRows] = {tr(Str::Language),      tr(Str::Resolution),    tr(Str::Region),
                                        tr(Str::Deadzone),      tr(Str::TriggerRumble), tr(Str::ConfirmButton),
                                        tr(Str::LightBar),      tr(Str::Updates),       tr(Str::LoginCountry),
                                        tr(Str::HighRefresh),   tr(Str::BitrateCap),    tr(Str::GameVolume),
                                        tr(Str::KeyboardKeys),  tr(Str::DisplayMode),   tr(Str::ControllerButtons),
                                        tr(Str::Gyro),          tr(Str::QueueReadyNotification),
                                        tr(Str::CheckSignIn),   tr(Str::NetworkTest),   tr(Str::SupportProject),
                                        tr(Str::VideoDecoder),  tr(Str::AudioHaptics),  tr(Str::PreviewFeatures),
                                        tr(Str::ResetSettings)};
    constexpr int kRowW = 1140, kRowH = 30, kGap = 2, kGroupGap = 4, kRowPx = 22;
    Rect rows[kSettingRows];  // by row id
    int y = 184;
    for (const auto& [i, groupStart] : visibleSettingsRows(caps_)) {  // what caps_ rules out is hidden
        if (groupStart) y += kGroupGap;
        Rect r{kMargin, y, kRowW, kRowH};
        rows[i] = r;
        bool focused = i == settingsRow_;
        int ty = fonts_.semibold.centeredY(r.y, r.h, kRowPx);

        if (i == 23) {
            // Restore factory defaults button: red background
            Color redBg = focused ? rgba(220, 38, 38, 255) : rgba(185, 28, 28, 220);
            c.fillRect(r, redBg, 12);
            if (focused && !dropdownOpen_) c.strokeRect({r.x - 4, r.y - 4, r.w + 8, r.h + 8}, kWhite, 3, 16);

            // Progress fill animation when holding Cross/accept
            if (focused && resetSettingsHoldStart_ > 0) {
                uint64_t elapsed = (lastRender_ > resetSettingsHoldStart_) ? (lastRender_ - resetSettingsHoldStart_) : 0;
                float progress = std::min(1.0f, static_cast<float>(elapsed) / 5000.0f);
                int fillW = static_cast<int>(r.w * progress);
                if (fillW > 0) {
                    c.fillRect({r.x, r.y, fillW, r.h}, rgba(255, 255, 255, 70), 12);
                }
            }
            int textW = fonts_.bold.measure(labels[i], kRowPx);
            int textX = r.x + (r.w - textW) / 2;
            fonts_.bold.draw(c, labels[i], textX, ty, kRowPx, kWhite);
        } else {
            c.fillRect(r, focused ? rgba(255, 255, 255, 36) : rgba(255, 255, 255, 14), 12);
            if (focused && !dropdownOpen_) c.strokeRect({r.x - 4, r.y - 4, r.w + 8, r.h + 8}, kWhite, 3, 16);
            fonts_.semibold.draw(c, labels[i], r.x + 32, ty, kRowPx, kWhite);
            float cx = static_cast<float>(r.x + r.w - 40), cy = static_cast<float>(r.y + r.h / 2);
            if (i != 7 && i != 17 && i != 18 && i != 19) {
                c.line(cx - 8, cy - 3, cx, cy + 4, 2.5f, focused ? kWhite : kGray);
                c.line(cx, cy + 4, cx + 8, cy - 3, 2.5f, focused ? kWhite : kGray);
            }
            int vx = r.x + r.w - 68;

            if (i == 7) {
                // Updates: the state; Cross updates when there is a newer version.
                std::string value = updateChecking_ ? tr(Str::UpdateChecking)
                                    : !updateAvailable_.empty()
                                        ? trf(caps_.selfUpdate ? Str::UpdateReady : Str::UpdateReadyPc, updateAvailable_)
                                        : trf(Str::UpToDate, "v" + updateCurrent_);
                Color color = !updateAvailable_.empty() && !updateChecking_ ? rgba(120, 220, 120) : focused ? kWhite : kGray;
                int w = fonts_.regular.measure(value, kRowPx);
                fonts_.regular.draw(c, value, vx - w, fonts_.regular.centeredY(r.y, r.h, kRowPx), kRowPx, color);
            } else if (i == 17 || i == 18 || i == 19) {
                // Action items (Check sign-in, Network test, Support)
                std::string label = (i == 17) ? tr(Str::RunCheck) : (i == 18) ? tr(Str::RunTest) : tr(Str::SupportKoFi);
                int w = fonts_.regular.measure(label, kRowPx);
                fonts_.regular.draw(c, label, vx - w, fonts_.regular.centeredY(r.y, r.h, kRowPx), kRowPx,
                                    focused ? kWhite : kGray);
            } else if (i == 5) {
                // The confirm button, as the button itself.
                drawFaceButton(c, fonts_.bold, settings_.circleConfirms ? kIconCircle : kIconCross, vx - 16.0f, cy, 16);
            } else {
                auto options = settingOptions(i);
                int sel = settingSelected(i);
                std::string value = sel < static_cast<int>(options.size()) ? options[static_cast<size_t>(sel)] : "";
                int w = fonts_.regular.measure(value, kRowPx);
                fonts_.regular.draw(c, value, vx - w, fonts_.regular.centeredY(r.y, r.h, kRowPx), kRowPx, focused ? kWhite : kGray);
                if (i == 6 && settings_.lightBarMode == 1) {  // the colour chosen
                    c.fillCircle(vx - w - 26.0f, cy, 11, settings_.lightBarColour);
                    c.strokeArc(vx - w - 26.0f, cy, 11, 2, 0, 6.2832f, rgba(255, 255, 255, 120));
                }
            }
        }
        y += kRowH + kGap;
    }
    static constexpr Str kSettingDescs[kSettingRows] = {
        Str::SettingDescLanguage,
        Str::SettingDescResolution,
        Str::SettingDescRegion,
        Str::SettingDescDeadzone,
        Str::SettingDescTriggerRumble,
        Str::SettingDescConfirmButton,
        Str::SettingDescLightBar,
        Str::SettingDescUpdates,
        Str::SettingDescLoginCountry,
        Str::SettingDescHighRefresh,
        Str::SettingDescBitrateCap,
        Str::SettingDescGameVolume,
        Str::SettingDescKeyboardKeys,
        Str::SettingDescDisplayMode,
        Str::SettingDescControllerButtons,
        Str::SettingDescGyro,
        Str::SettingDescQueueReady,
        Str::SettingDescCheckSignIn,
        Str::SettingDescNetworkTest,
        Str::SettingDescSupportProject,
        Str::SettingDescVideoDecoder,
        Str::SettingDescAudioHaptics,
        Str::SettingDescPreviewFeatures,
        Str::SettingDescResetSettings,
    };
    if (settingsRow_ >= 0 && settingsRow_ < kSettingRows) {
        int dy = std::max(y + 6, 922);
        const char* desc = tr(kSettingDescs[settingsRow_]);
        for (const auto& line : fonts_.regular.wrap(desc, 22, kRowW, 3)) {
            fonts_.regular.draw(c, line, kMargin, dy, 22, kDim);
            dy += 26;
        }
    }

    // Web UI / Remote QR panel on the right side (x = 1310..1850)
    if (tester_ == Tester::None && !pickerOpen_) {
        std::string qrUrl = !webUrl_.empty() ? webUrl_ : "http://" + platform::localIPv4() + ":8080";
        Rect panel{1310, 184, 540, 724};
        c.fillRect(panel, rgba(255, 255, 255, 14), 16);
        c.strokeRect(panel, rgba(255, 255, 255, 30), 1, 16);

        int py = panel.y + 30;
        int titleW = fonts_.bold.measure(tr(Str::WebControlTitle), 28);
        fonts_.bold.draw(c, tr(Str::WebControlTitle), panel.x + (panel.w - titleW) / 2, py, 28, kWhite);
        py += 40;

        auto descLines = fonts_.regular.wrap(tr(Str::WebControlDesc), 17, panel.w - 56, 5);
        for (const auto& l : descLines) {
            int lw = fonts_.regular.measure(l, 17);
            fonts_.regular.draw(c, l, panel.x + (panel.w - lw) / 2, py, 17, kGray);
            py += 22;
        }

        uint8_t qrBuf[qrcodegen_BUFFER_LEN_MAX], tmpBuf[qrcodegen_BUFFER_LEN_MAX];
        int qrTarget = 260;
        int qrCardW = 300;
        if (qrcodegen_encodeText(qrUrl.c_str(), tmpBuf, qrBuf, qrcodegen_Ecc_LOW, qrcodegen_VERSION_MIN, qrcodegen_VERSION_MAX, qrcodegen_Mask_AUTO, true)) {
            int n = qrcodegen_getSize(qrBuf);
            int mod = qrTarget / (n + 2);
            qrCardW = mod * (n + 2) + 40;
        }
        int qrRight = panel.x + (panel.w + qrCardW) / 2;

        Rect pill{panel.x + 36, panel.y + panel.h - 42 - 18, panel.w - 72, 42};

        // Center QR vertically in available space between py and pill.y
        int availH = pill.y - py;
        int qrTop = py + (availH - qrCardW) / 2;
        drawQr(c, qrUrl, qrRight, qrTop, qrTarget, qrcodegen_Ecc_LOW);

        c.fillRect(pill, rgba(0, 0, 0, 70), 12);
        c.strokeRect(pill, rgba(255, 255, 255, 30), 1, 12);

        int urlW = fonts_.semibold.measure(qrUrl, 20);
        fonts_.semibold.draw(c, qrUrl, pill.x + (pill.w - urlW) / 2, fonts_.semibold.centeredY(pill.y, pill.h, 20), 20, rgba(140, 230, 160));
    }

    if (pickerOpen_) {
        drawColourPicker(c);
        return;
    }
    if (tester_ == Tester::Sticks) return drawStickTester(c);
    if (tester_ == Tester::Triggers) return drawTriggerTester(c);
    if (tester_ == Tester::Keys) return drawKeysTester(c);
    if (tester_ == Tester::Pad) return drawPadTester(c);
    if (tester_ == Tester::Touchpad) return drawTouchpadTester(c);
    if (tester_ == Tester::SignInCheck) return drawSignInCheck(c, lastRender_);
    if (tester_ == Tester::NetworkTest) return drawNetworkTest(c, lastRender_);
    if (tester_ == Tester::Donation) return drawDonationModal(c);
    if (dropdownOpen_) {
        // The list under (or, near the bottom, over) the row, right-aligned
        // with it; scrolls past kVisible choices.
        auto options = settingOptions(settingsRow_);
        int sel = settingSelected(settingsRow_);
        constexpr int kItemH = 62, kVisible = 8, kListW = 640;
        int n = static_cast<int>(options.size());
        int visible = std::min(n, kVisible);
        if (dropdownIndex_ < dropdownTop_) dropdownTop_ = dropdownIndex_;
        if (dropdownIndex_ >= dropdownTop_ + visible) dropdownTop_ = dropdownIndex_ - visible + 1;
        const Rect& row = rows[settingsRow_];
        Rect panel{row.x + row.w - kListW, row.y + row.h + 8, kListW, visible * kItemH + 16};
        if (panel.y + panel.h > kH - 100) panel.y = std::max(120, kH - 100 - panel.h);
        c.fillRect({panel.x + 6, panel.y + 10, panel.w, panel.h}, rgba(0, 0, 0, 120), 18);  // shadow
        c.fillRect(panel, rgba(38, 38, 38), 18);
        c.strokeRect(panel, rgba(255, 255, 255, 50), 2, 18);
        for (int k = 0; k < visible; ++k) {
            int i = dropdownTop_ + k;
            Rect item{panel.x + 8, panel.y + 8 + k * kItemH, panel.w - 16, kItemH};
            bool on = i == dropdownIndex_;
            if (on) c.fillRect(item, kWhite, 12);
            Color text = on ? kBg : (i == sel ? kWhite : kGray);
            if (settingsRow_ == 5)
                drawFaceButton(c, fonts_.bold, i == 1 ? kIconCircle : kIconCross, item.x + 44.0f, item.y + item.h / 2.0f, 20);
            else
                fonts_.semibold.draw(c, options[static_cast<size_t>(i)], item.x + 24,
                                     fonts_.semibold.centeredY(item.y, item.h, 26), 26, text);
            if (settingsRow_ == 6 && i == 1)
                c.fillCircle(item.x + item.w - 84.0f, item.y + item.h / 2.0f, 12, settings_.lightBarColour);
            if (i == sel) {  // a check mark on the current choice
                float cx = static_cast<float>(item.x + item.w - 36), cy = static_cast<float>(item.y + item.h / 2);
                c.line(cx - 10, cy, cx - 3, cy + 7, 3, on ? kBg : kGreen);
                c.line(cx - 3, cy + 7, cx + 10, cy - 7, 3, on ? kBg : kGreen);
            }
        }
        // Scroll hints: more choices above / below.
        if (dropdownTop_ > 0) {
            float cx = static_cast<float>(panel.x + panel.w / 2), cy = static_cast<float>(panel.y - 14);
            c.line(cx - 10, cy + 4, cx, cy - 4, 3, kGray);
            c.line(cx, cy - 4, cx + 10, cy + 4, 3, kGray);
        }
        if (dropdownTop_ + visible < n) {
            float cx = static_cast<float>(panel.x + panel.w / 2), cy = static_cast<float>(panel.y + panel.h + 14);
            c.line(cx - 10, cy - 4, cx, cy + 4, 3, kGray);
            c.line(cx, cy + 4, cx + 10, cy - 4, 3, kGray);
        }
        drawHints(c, {{kIconCross, tr(Str::Select)}, {kIconCircle, tr(Str::Back)}});
    } else {
        if (settingsRow_ == 23) {
            drawHints(c, {{kIconCross, tr(Str::HoldToReset)}, {kIconTriangle, tr(Str::ReportProblem)}, {kIconCircle, tr(Str::Back)}});
        } else if (settingsRow_ == 16) {
            drawHints(c, {{kIconCross, tr(Str::Change)}, {kIconSquare, tr(Str::TestSound)}, {kIconTriangle, tr(Str::ReportProblem)}, {kIconCircle, tr(Str::Back)}});
        } else if (settingsRow_ == 10) {
            drawHints(c, {{kIconDpadLR, tr(Str::Adjust)}, {kIconTriangle, tr(Str::ReportProblem)}, {kIconCircle, tr(Str::Back)}});
        } else {
            drawHints(c, {{kIconCross, tr(Str::Change)}, {kIconTriangle, tr(Str::ReportProblem)}, {kIconCircle, tr(Str::Back)}});
        }
    }
}

Rect AppUi::drawModal(Canvas& c, int w, int h, const std::string& title) {
    c.fillRect({0, 0, kW, kH}, rgba(0, 0, 0, 160));
    Rect panel{(kW - w) / 2, (kH - h) / 2 - 30, w, h};
    c.fillRect({panel.x + 8, panel.y + 12, panel.w, panel.h}, rgba(0, 0, 0, 120), 26);  // shadow
    c.fillRect(panel, kPanel, 24);
    c.strokeRect(panel, rgba(255, 255, 255, 50), 2, 24);
    fonts_.bold.draw(c, title, panel.x + 56, panel.y + 44, 40, kWhite);
    c.fillRect({0, kH - 80, kW, 80}, kBg);  // Settings' hints, under the panel's
    drawHints(c, {{kIconCross, tr(Str::Select)}, {kIconCircle, tr(Str::Back)}});
    return panel;
}

void AppUi::drawStickTester(Canvas& c) {
    Rect panel = drawModal(c, 1240, 740, tr(Str::Deadzone));
    constexpr int R = 180;
    const Color raw = rgba(170, 170, 170), game = rgba(90, 220, 110);
    for (int s = 0; s < 2; ++s) {
        const float dz = settings_.deadzone[s] / 100.0f;
        const bool chosen = s == testerStick_;
        const float cx = panel.x + (s ? 930.0f : 310.0f), cy = panel.y + 170.0f + R;
        // Its dead zone above it, in white on the stick being adjusted.
        std::string value = std::to_string(settings_.deadzone[s]) + " %";
        fonts_.bold.draw(c, value, static_cast<int>(cx) - fonts_.bold.measure(value, 34) / 2, static_cast<int>(cy) - R - 56, 34,
                         chosen ? kWhite : kDim);
        c.fillCircle(cx, cy, R, rgba(255, 255, 255, chosen ? 22 : 10));
        c.line(cx - R, cy, cx + R, cy, 1, rgba(255, 255, 255, 30));
        c.line(cx, cy - R, cx, cy + R, 1, rgba(255, 255, 255, 30));
        c.strokeArc(cx, cy, R, chosen ? 4.0f : 2.0f, 0, 6.2832f, chosen ? kWhite : rgba(255, 255, 255, 70));
        // The dead zone: nothing reaches the game in there.
        if (dz > 0) {
            c.fillCircle(cx, cy, dz * R, rgba(230, 70, 70, 60));
            c.strokeArc(cx, cy, dz * R, 2, 0, 6.2832f, rgba(230, 90, 90, 180));
        }
        float x = testSticks_[2 * s], y = testSticks_[2 * s + 1], gx = x, gy = y;
        input::radialDeadzone(gx, gy, dz);
        c.fillCircle(cx + x * R, cy + y * R, 9, raw);
        c.fillCircle(cx + gx * R, cy + gy * R, 12, game);
        const char* name = tr(s ? Str::StickRight : Str::StickLeft);
        fonts_.semibold.draw(c, name, static_cast<int>(cx) - fonts_.semibold.measure(name, 26) / 2, static_cast<int>(cy) + R + 22,
                             26, kWhite);
        // How far each one is, in percent of the travel.
        auto pct = [](float a, float b) { return std::to_string(static_cast<int>(std::lround(std::min(1.0f, std::hypot(a, b)) * 100))) + " %"; };
        std::string line = pct(x, y) + "  \xE2\x86\x92  " + pct(gx, gy);
        int tw = fonts_.regular.measure(line, 24);
        fonts_.regular.draw(c, line, static_cast<int>(cx) - tw / 2, static_cast<int>(cy) + R + 58, 24, kGray);
    }
    // The legend, then the help.
    int ly = panel.y + panel.h - 92, lx = panel.x + 56;
    c.fillCircle(lx + 9.0f, ly + 13.0f, 9, raw);
    fonts_.regular.draw(c, tr(Str::StickPosition), lx + 28, ly, 24, kGray);
    lx += 28 + fonts_.regular.measure(tr(Str::StickPosition), 24) + 48;
    c.fillCircle(lx + 12.0f, ly + 13.0f, 12, game);
    fonts_.regular.draw(c, tr(Str::StickGameGets), lx + 32, ly, 24, kGray);
    fonts_.regular.draw(c, tr(Str::StickTestHelp), panel.x + 56, ly + 44, 22, kDim);
}

void AppUi::drawTriggerTester(Canvas& c) {
    // Without adaptive triggers, no resistance, style or profile rows: the panel shorter by them.
    const int rows = triggerTesterRows(), lessY = (5 - rows) * 84;
    Rect panel = drawModal(c, 1100, 890 - lessY, tr(Str::TriggerRumble));
    // The choices, as Settings' rows: up / down picks one, left / right changes it.
    const char* strengths[] = {tr(Str::Deactivated), tr(Str::TriggerLight), tr(Str::TriggerMedium), tr(Str::TriggerStrong),
                               tr(Str::TriggerMax)};
    const char* freqs[] = {tr(Str::FrequencyLow), tr(Str::TriggerMedium), tr(Str::FrequencyHigh)};
    static constexpr Str kProfileStrs[] = {Str::TriggerProfileStd, Str::TriggerProfileShooter, Str::TriggerProfileRacing};
    const char* labels[] = {tr(Str::TriggerIntensity), tr(Str::TriggerFrequency), tr(Str::TriggerResistance),
                            tr(Str::TriggerStyle), tr(Str::TriggerProfile)};
    // Pulses go at their own, slower rates.
    int hz = settings_.triggerPulses ? input::kTriggerPulseHz[settings_.triggerHz] : input::kTriggerHz[settings_.triggerHz];
    int prof = std::clamp(settings_.triggerProfile, 0, 2);
    const std::string values[] = {strengths[settings_.triggerStrength],
                                  std::string(freqs[settings_.triggerHz]) + "  (" + std::to_string(hz) + " Hz)",
                                  strengths[settings_.triggerResistance],  // off, light, medium, strong
                                  tr(settings_.triggerPulses ? Str::StylePulses : Str::StyleVibration),
                                  tr(kProfileStrs[prof])};
    for (int i = 0; i < rows; ++i) {
        Rect r{panel.x + 56, panel.y + 120 + i * 84, panel.w - 112, 70};
        bool on = i == testerRow_;
        c.fillRect(r, on ? rgba(255, 255, 255, 36) : rgba(255, 255, 255, 14), 16);
        if (on) c.strokeRect({r.x - 5, r.y - 5, r.w + 10, r.h + 10}, kWhite, 3, 20);
        fonts_.semibold.draw(c, labels[i], r.x + 36, fonts_.semibold.centeredY(r.y, r.h, 30), 30, kWhite);
        std::string v = "\xE2\x80\xB9  " + values[i] + "  \xE2\x80\xBA";  // ‹ value ›
        int w = fonts_.regular.measure(v, 30);
        fonts_.regular.draw(c, v, r.x + r.w - 36 - w, fonts_.regular.centeredY(r.y, r.h, 30), 30, on ? kWhite : kGray);
    }
    // L2 and R2: how far each is pressed, and the level it sends to the trigger.
    const float pressed[2] = {testL2_, testR2_};
    for (int t = 0; t < 2; ++t) {
        constexpr int kBarW = 90, kBarH = 150;
        int bx = panel.x + panel.w / 2 + (t ? 80 : -80 - kBarW), by = panel.y + 560 - lessY;
        c.fillRect({bx, by, kBarW, kBarH}, rgba(255, 255, 255, 20), 16);
        int fill = static_cast<int>(kBarH * std::clamp(pressed[t], 0.0f, 1.0f));
        int level = input::triggerAmplitude(static_cast<uint8_t>(std::lround(pressed[t] * 255)), settings_.triggerStrength);
        if (fill > 0) c.fillRect({bx, by + kBarH - fill, kBarW, fill}, level ? rgba(90, 220, 110) : rgba(120, 120, 120), 16);
        std::string name = buttonName(g_glyphStyle, t ? PadButton::R2 : PadButton::L2);
        fonts_.bold.draw(c, name, bx + (kBarW - fonts_.bold.measure(name, 26)) / 2, by + kBarH + 14, 26, kWhite);
        std::string lv = trf(Str::TriggerLevel, std::to_string(level) + " / 8");
        fonts_.regular.draw(c, lv, bx + (kBarW - fonts_.regular.measure(lv, 22)) / 2, by + kBarH + 48, 22, kGray);
    }
    auto help = fonts_.regular.wrap(tr(Str::TriggerTestHelp), 22, panel.w - 112, 2);
    for (size_t i = 0; i < help.size(); ++i)
        fonts_.regular.draw(c, help[i], panel.x + 56, panel.y + panel.h - 70 + static_cast<int>(i) * 30, 22, kDim);
}

void AppUi::drawPadTester(Canvas& c) {
    Rect panel = drawModal(c, 1500, 860, tr(Str::ControllerButtons));

    // Top tab selector: [ Botões ]  [ Touchpad ]
    if (caps_.touchpad) {
        int tabW = 280, tabH = 38, tabGap = 20;
        int totalTabsW = 2 * tabW + tabGap;
        int tabStartX = panel.x + (panel.w - totalTabsW) / 2;
        int tabY = panel.y + 60;

        auto shoulder = [&](const char* name, int cx) {
            Rect r{cx - 22, tabY + 5, 44, 28};
            c.fillRect(r, rgba(255, 255, 255, 40), 8);
            fonts_.bold.draw(c, name, cx - fonts_.bold.measure(name, 16) / 2, fonts_.bold.centeredY(r.y, r.h, 16), 16, kGray);
        };
        shoulder(g_keyboardHints ? "Q" : buttonName(g_glyphStyle, PadButton::L1), tabStartX - 32);

        for (int t = 0; t < 2; ++t) {
            Rect trRect{tabStartX + t * (tabW + tabGap), tabY, tabW, tabH};
            bool active = (padTesterTab_ == t);
            c.fillRect(trRect, active ? rgba(255, 255, 255, 40) : rgba(255, 255, 255, 14), 12);
            if (active) c.strokeRect(trRect, kWhite, 2, 12);
            const char* tabLabel = t == 0 ? tr(Str::ControllerButtons) : tr(Str::TouchpadShortcuts);
            int tw = fonts_.semibold.measure(tabLabel, 22);
            fonts_.semibold.draw(c, tabLabel, trRect.x + (trRect.w - tw) / 2,
                                 fonts_.semibold.centeredY(trRect.y, trRect.h, 22), 22, active ? kWhite : kGray);
        }

        shoulder(g_keyboardHints ? "E" : buttonName(g_glyphStyle, PadButton::R1), tabStartX + totalTabsW + 32);
    }

    if (padTesterTab_ == 1) {
        constexpr int kTouchRowH = 56, kTouchGap = 8;
        const input::TouchpadAction vals[6] = {
            touchpadWork_.swipeUp,    touchpadWork_.swipeDown,
            touchpadWork_.swipeLeft,  touchpadWork_.swipeRight,
            touchpadWork_.clickLeft,  touchpadWork_.clickRight,
        };
        int y = panel.y + 116;
        for (int i = 0; i < 6; ++i) {
            Rect r{panel.x + 80, y, panel.w - 160, kTouchRowH};
            bool on = (touchpadRow_ == i);
            c.fillRect(r, on ? rgba(255, 255, 255, 36) : rgba(255, 255, 255, 14), 14);
            if (on && !touchpadDropdownOpen_) c.strokeRect({r.x - 4, r.y - 4, r.w + 8, r.h + 8}, kWhite, 2, 18);
            int ty = fonts_.semibold.centeredY(r.y, r.h, 26);
            fonts_.semibold.draw(c, touchpadTriggerLabel(i), r.x + 32, ty, 26, kWhite);

            float cx = static_cast<float>(r.x + r.w - 40), cy = static_cast<float>(r.y + r.h / 2);
            c.line(cx - 8, cy - 3, cx, cy + 4, 2.5f, on ? kWhite : kGray);
            c.line(cx, cy + 4, cx + 8, cy - 3, 2.5f, on ? kWhite : kGray);

            std::string val = touchpadActionLabel(static_cast<int>(vals[i]));
            int vw = fonts_.regular.measure(val, 26);
            int vx = r.x + r.w - 68;
            fonts_.regular.draw(c, val, vx - vw, fonts_.regular.centeredY(r.y, r.h, 26), 26, on ? kWhite : kGray);

            y += kTouchRowH + kTouchGap;
        }

        Rect reset{panel.x + 80, y + 8, panel.w - 160, kTouchRowH};
        bool resetOn = (touchpadRow_ == 6);
        c.fillRect(reset, resetOn ? rgba(255, 255, 255, 36) : rgba(255, 255, 255, 14), 14);
        if (resetOn && !touchpadDropdownOpen_) c.strokeRect({reset.x - 4, reset.y - 4, reset.w + 8, reset.h + 8}, kWhite, 2, 18);
        const char* resetLabel = tr(Str::ResetToDefault);
        fonts_.semibold.draw(c, resetLabel, reset.x + (reset.w - fonts_.semibold.measure(resetLabel, 24)) / 2,
                             fonts_.semibold.centeredY(reset.y, reset.h, 24), 24, resetOn ? kWhite : rgba(240, 100, 100));

        int hy = reset.y + kTouchRowH + 22;
        auto help = fonts_.regular.wrap(tr(Str::SettingDescTouchpadShortcuts), 22, panel.w - 160, 2);
        for (size_t i = 0; i < help.size(); ++i)
            fonts_.regular.draw(c, help[i], panel.x + 80, hy + static_cast<int>(i) * 28, 22, kDim);

        if (touchpadDropdownOpen_) {
            constexpr int kListW = 500, kItemH = 46, kMaxVisible = 8;
            constexpr int n = input::kTouchpadActions;
            int visible = std::min(n, kMaxVisible);
            int anchorY = panel.y + 116 + touchpadRow_ * (kTouchRowH + kTouchGap);
            Rect dropPanel{panel.x + panel.w - 80 - kListW, anchorY + kTouchRowH + 6, kListW, visible * kItemH + 16};
            if (dropPanel.y + dropPanel.h > kH - 100) dropPanel.y = std::max(120, anchorY - dropPanel.h - 6);
            c.fillRect({dropPanel.x + 6, dropPanel.y + 10, dropPanel.w, dropPanel.h}, rgba(0, 0, 0, 160), 18);
            c.fillRect(dropPanel, rgba(38, 38, 38), 18);
            c.strokeRect(dropPanel, rgba(255, 255, 255, 60), 2, 18);
            int curAction = static_cast<int>(touchpadWork_.get(touchpadRow_));
            for (int k = 0; k < visible; ++k) {
                int itemIdx = touchpadDropdownTop_ + k;
                if (itemIdx >= n) break;
                Rect item{dropPanel.x + 8, dropPanel.y + 8 + k * kItemH, dropPanel.w - 16, kItemH};
                bool isFocused = (itemIdx == touchpadDropdownIndex_);
                bool isSelected = (itemIdx == curAction);
                if (isFocused) c.fillRect(item, kWhite, 12);
                Color textCol = isFocused ? kBg : (isSelected ? kWhite : kGray);
                std::string itemText = touchpadActionLabel(itemIdx);
                fonts_.semibold.draw(c, itemText, item.x + 24, fonts_.semibold.centeredY(item.y, item.h, 24), 24, textCol);
                if (isSelected) {
                    float markX = static_cast<float>(item.x + item.w - 36), markY = static_cast<float>(item.y + item.h / 2);
                    c.line(markX - 10, markY, markX - 3, markY + 7, 3, isFocused ? kBg : kGreen);
                    c.line(markX - 3, markY + 7, markX + 10, markY - 7, 3, isFocused ? kBg : kGreen);
                }
            }
            if (touchpadDropdownTop_ > 0) {
                float scx = static_cast<float>(dropPanel.x + dropPanel.w / 2), scy = static_cast<float>(dropPanel.y - 14);
                c.line(scx - 10, scy + 4, scx, scy - 4, 3, kGray);
                c.line(scx, scy - 4, scx + 10, scy + 4, 3, kGray);
            }
            if (touchpadDropdownTop_ + visible < n) {
                float scx = static_cast<float>(dropPanel.x + dropPanel.w / 2), scy = static_cast<float>(dropPanel.y + dropPanel.h + 14);
                c.line(scx - 10, scy - 4, scx, scy + 4, 3, kGray);
                c.line(scx, scy + 4, scx + 10, scy - 4, 3, kGray);
            }
        }

        c.fillRect({0, kH - 80, kW, 80}, kBg);
        if (touchpadDropdownOpen_)
            drawHints(c, {{kIconCross, tr(Str::Select)}, {kIconCircle, tr(Str::Back)}});
        else if (resetOn)
            drawHints(c, {{kIconCross, tr(Str::Select)}, {kIconL1R1, tr(Str::Sections)}, {kIconCircle, tr(Str::Back)}});
        else
            drawHints(c, {{kIconCross, tr(Str::Change)}, {kIconSquare, tr(Str::ClearKey)}, {kIconL1R1, tr(Str::Sections)}, {kIconCircle, tr(Str::Back)}});
        return;
    }

    // Each Xbox button the game gets (as an Xbox pad shows it), then the
    // button on this pad that plays it (in the pad's own glyphs). Left
    // column the face buttons, shoulders and triggers; right the rest.
    constexpr int kPerColumn = input::kPadFns / 2;
    constexpr int kRowH = 52, kGap = 8, kChipW = 300, kColGap = 40;
    constexpr const char* kArrows[4] = {" \xE2\x86\x91", " \xE2\x86\x93", " \xE2\x86\x90", " \xE2\x86\x92"};
    static constexpr PadButton kButtons[12] = {PadButton::Cross, PadButton::Circle, PadButton::Square, PadButton::Triangle,
                                               PadButton::L1,    PadButton::R1,     PadButton::L2,     PadButton::R2,
                                               PadButton::L3,    PadButton::R3,     PadButton::Touchpad, PadButton::Options};
    const GlyphStyle style = g_glyphStyle;
    const bool mapSwap = gameSettingsOpen_ ? activeGameProfile_.circleConfirms : settings_.circleConfirms;
    const bool capturing = padCaptureSince_ != 0;
    const std::string dpad = tr(Str::DPad);
    // A button (a PadFn index) drawn at (x, cy), left-aligned, in `s`'s glyphs.
    auto drawButton = [&](int i, GlyphStyle s, int x, int cy, Color color) {
        const int ty = fonts_.semibold.centeredY(cy - kRowH / 2, kRowH, 24);
        if (i >= 12) {
            fonts_.semibold.draw(c, dpad + kArrows[i - 12], x, ty, 24, color);
        } else if (i < 4) {
            ui::drawFaceButton(c, fonts_.bold, s, kButtons[i], x + 16.0f, static_cast<float>(cy), 16);
            if (s == GlyphStyle::PlayStation)
                fonts_.semibold.draw(c, buttonName(s, kButtons[i]), x + 44, ty, 24, color);
        } else {
            fonts_.semibold.draw(c, buttonName(s, kButtons[i]), x, ty, 24, color);
        }
    };
    const int colW = (panel.w - 112 - kColGap) / 2;
    for (int f = 0; f < input::kPadFns; ++f) {
        const int col = f / kPerColumn, row = f % kPerColumn;
        Rect r{panel.x + 56 + col * (colW + kColGap), panel.y + 106 + row * (kRowH + kGap), colW, kRowH};
        const bool on = row == padRow_ && col == padCol_;
        const int cy = r.y + r.h / 2;
        c.fillRect(r, on ? rgba(255, 255, 255, 30) : rgba(255, 255, 255, 12), 14);
        if (on && !capturing) c.strokeRect({r.x - 4, r.y - 4, r.w + 8, r.h + 8}, kWhite, 2, 18);
        drawButton(f, GlyphStyle::Xbox, r.x + 22, cy, kWhite);
        Rect chip{r.x + r.w - 10 - kChipW, r.y + 7, kChipW, r.h - 14};
        fonts_.regular.draw(c, "\xE2\x86\x90", chip.x - 44, fonts_.regular.centeredY(r.y, r.h, 24), 24, kDim);
        const uint8_t src = padWork_[static_cast<size_t>(f)];
        const bool none = src >= input::kPadFns, changed = src != f;
        c.fillRect(chip, none ? rgba(240, 100, 100, 40) : changed ? rgba(90, 170, 255, 60) : rgba(255, 255, 255, 26), 10);
        if (on && capturing) {
            c.strokeRect({chip.x - 4, chip.y - 4, chip.w + 8, chip.h + 8}, rgba(90, 220, 110), 3, 13);
            fonts_.semibold.draw(c, "...", chip.x + 20, fonts_.semibold.centeredY(chip.y, chip.h, 24), 24, kWhite);
        } else if (none) {
            fonts_.semibold.draw(c, "\xE2\x80\x94", chip.x + 20, fonts_.semibold.centeredY(chip.y, chip.h, 24), 24,
                                 rgba(240, 100, 100));
        } else {
            drawButton(input::physicalSource(src, mapSwap), style, chip.x + 16, cy, kWhite);
        }
    }
    // Reset to default and Discard changes, side by side under the columns.
    const int by = panel.y + 106 + kPerColumn * (kRowH + kGap) + 8;
    for (int col = 0; col < 2; ++col) {
        Rect b{panel.x + 56 + col * (colW + kColGap), by, colW, kRowH};
        const bool on = padRow_ == kPerColumn && padCol_ == col;
        c.fillRect(b, on ? rgba(255, 255, 255, 36) : rgba(255, 255, 255, 14), 14);
        if (on) c.strokeRect({b.x - 4, b.y - 4, b.w + 8, b.h + 8}, kWhite, 2, 18);
        const char* label = tr(col == 0 ? Str::ResetToDefault : Str::DiscardChanges);
        fonts_.semibold.draw(c, label, b.x + (b.w - fonts_.semibold.measure(label, 24)) / 2,
                             fonts_.semibold.centeredY(b.y, b.h, 24), 24, on ? kWhite : rgba(240, 100, 100));
    }
    // Waiting for a press: say for what. Else what's missing (Circle won't
    // leave then), or the help.
    int hy = by + kRowH + 34;
    const uint32_t missing = input::padMapMissing(padWork_);
    if (capturing) {
        const auto fn = static_cast<input::PadFn>(padCol_ * kPerColumn + std::min(padRow_, kPerColumn - 1));
        std::string ask = trf(Str::PadPressButton, input::padFnName(fn));
        fonts_.bold.draw(c, ask, panel.x + (panel.w - fonts_.bold.measure(ask, 28)) / 2, hy, 28, kWhite);
    } else if (missing || !input::padMapValid(padWork_)) {
        std::string names;
        for (int f = 0; f < input::kPadFns; ++f)
            if (missing & (1u << f)) names += (names.empty() ? "" : ", ") + std::string(input::padFnName(static_cast<input::PadFn>(f)));
        auto lines = fonts_.semibold.wrap(trf(Str::PadMissing, names), 24, panel.w - 112, 2);
        for (size_t i = 0; i < lines.size(); ++i)
            fonts_.semibold.draw(c, lines[i], panel.x + 56, hy + static_cast<int>(i) * 32, 24, rgba(240, 100, 100));
    } else {
        auto help = fonts_.regular.wrap(tr(Str::PadHelp), 22, panel.w - 112, 2);
        for (size_t i = 0; i < help.size(); ++i)
            fonts_.regular.draw(c, help[i], panel.x + 56, hy + static_cast<int>(i) * 30, 22, kDim);
    }
    c.fillRect({0, kH - 80, kW, 80}, kBg);  // its own hints over drawModal's
    if (capturing)
        return;
    if (padRow_ == kPerColumn)
        drawHints(c, {{kIconCross, tr(Str::Select)}, {kIconL1R1, tr(Str::Sections)}, {kIconCircle, tr(Str::Back)}});
    else
        drawHints(c, {{kIconCross, tr(Str::ChangeKey)}, {kIconSquare, tr(Str::ClearKey)}, {kIconL1R1, tr(Str::Sections)}, {kIconCircle, tr(Str::Back)}});
}

void AppUi::drawTouchpadTester(Canvas& c) {
    padTesterTab_ = 1;
    drawPadTester(c);
}

void AppUi::drawSignInCheck(Canvas& c, uint64_t nowMs) {
    Rect panel = drawModal(c, 1180, 680, tr(Str::CheckSignIn));
    if (signInCheck_.running) {
        drawSpinner(c, static_cast<float>(panel.x + panel.w - 76), static_cast<float>(panel.y + 64), 14.0f, nowMs);
        animating_ = true;
    }
    int y = panel.y + 115;
    constexpr int kRowH = 62, kGap = 8;
    for (size_t i = 0; i < signInCheck_.stages.size() && i < 6; ++i) {
        const auto& st = signInCheck_.stages[i];
        Rect r{panel.x + 56, y, panel.w - 112, kRowH};
        c.fillRect(r, rgba(255, 255, 255, 14), 14);
        float cx = static_cast<float>(r.x + 36), cy = static_cast<float>(r.y + r.h / 2);
        if (st.pending) {
            drawSpinner(c, cx, cy, 12.0f, nowMs);
            animating_ = true;
        } else if (st.ok) {
            c.fillCircle(cx, cy, 14.0f, kGreen);
            c.line(cx - 6, cy, cx - 2, cy + 5, 2.5f, kWhite);
            c.line(cx - 2, cy + 5, cx + 7, cy - 5, 2.5f, kWhite);
        } else {
            c.fillCircle(cx, cy, 14.0f, rgba(220, 65, 65));
            c.line(cx - 5, cy - 5, cx + 5, cy + 5, 2.5f, kWhite);
            c.line(cx - 5, cy + 5, cx + 5, cy - 5, 2.5f, kWhite);
        }
        int ty = fonts_.semibold.centeredY(r.y, r.h, 24);
        fonts_.semibold.draw(c, st.name, r.x + 70, ty, 24, kWhite);
        if (!st.detail.empty()) {
            int maxW = r.w - 380;
            std::string det = fonts_.regular.fit(st.detail, 22, maxW);
            int dw = fonts_.regular.measure(det, 22);
            Color detCol = st.pending ? kDim : (st.ok ? kGray : rgba(255, 130, 130));
            fonts_.regular.draw(c, det, r.x + r.w - 28 - dw, fonts_.regular.centeredY(r.y, r.h, 22), 22, detCol);
        }
        y += kRowH + kGap;
    }
    c.fillRect({0, kH - 80, kW, 80}, kBg);
    if (signInCheck_.running)
        drawHints(c, {{kIconCircle, tr(Str::Back)}});
    else
        drawHints(c, {{kIconCross, tr(Str::TryAgain)}, {kIconCircle, tr(Str::Back)}});
}

void AppUi::drawNetworkTest(Canvas& c, uint64_t nowMs) {
    Rect panel = drawModal(c, 1180, 740, tr(Str::NetworkTest));
    if (networkTest_.running) {
        drawSpinner(c, static_cast<float>(panel.x + panel.w - 76), static_cast<float>(panel.y + 64), 14.0f, nowMs);
        animating_ = true;
    }
    Rect natBox{panel.x + 56, panel.y + 114, panel.w - 112, 56};
    c.fillRect(natBox, rgba(255, 255, 255, 18), 14);
    fonts_.semibold.draw(c, "UDP / STUN (NAT)", natBox.x + 28, fonts_.semibold.centeredY(natBox.y, natBox.h, 24), 24, kWhite);
    std::string natVal = networkTest_.natStatus.empty() ? "..." : networkTest_.natStatus;
    int nw = fonts_.regular.measure(natVal, 24);
    Color natCol = natVal.find("OK") != std::string::npos || natVal.find("Reachable") != std::string::npos
                       ? rgba(110, 225, 120)
                       : (networkTest_.running ? kDim : rgba(255, 180, 90));
    fonts_.regular.draw(c, natVal, natBox.x + natBox.w - 28 - nw, fonts_.regular.centeredY(natBox.y, natBox.h, 24), 24, natCol);

    int y = panel.y + 188;
    constexpr int kRowH = 54, kGap = 8;
    for (size_t i = 0; i < networkTest_.regions.size() && i < 8; ++i) {
        const auto& reg = networkTest_.regions[i];
        Rect r{panel.x + 56, y, panel.w - 112, kRowH};
        c.fillRect(r, i == 0 && reg.rttMs >= 0 ? rgba(16, 124, 16, 45) : rgba(255, 255, 255, 12), 12);
        std::string label = prettyRegion(reg.name);
        if (i == 0 && reg.rttMs >= 0) label += "  \xE2\x98\x85";
        fonts_.semibold.draw(c, label, r.x + 28, fonts_.semibold.centeredY(r.y, r.h, 24), 24, kWhite);
        std::string val;
        Color valCol = kDim;
        if (reg.rttMs >= 0) {
            val = "RTT " + std::to_string(reg.rttMs) + " ms   \xC2\xB7   Jitter " +
                  std::to_string(std::max(0, reg.jitterMs)) + " ms";
            valCol = reg.rttMs < 60 ? rgba(110, 225, 120) : (reg.rttMs < 120 ? rgba(255, 200, 80) : rgba(255, 120, 120));
        } else {
            val = networkTest_.running ? "..." : "--";
        }
        int vw = fonts_.regular.measure(val, 22);
        fonts_.regular.draw(c, val, r.x + r.w - 28 - vw, fonts_.regular.centeredY(r.y, r.h, 22), 22, valCol);
        y += kRowH + kGap;
    }
    c.fillRect({0, kH - 80, kW, 80}, kBg);
    if (networkTest_.running)
        drawHints(c, {{kIconCircle, tr(Str::Back)}});
    else
        drawHints(c, {{kIconCross, tr(Str::TryAgain)}, {kIconCircle, tr(Str::Back)}});
}

void AppUi::drawDonationModal(Canvas& c) {
    Rect panel = drawModal(c, 1260, 680, tr(Str::SupportTitle));

    // Calculate dimensions for QR code (larger target = 420px)
    constexpr int kQrTarget = 420;
    int qrTotalH = 460;
    int contentTop = panel.y + 90;
    int contentH = panel.h - 110;
    int centerY = contentTop + contentH / 2;

    int qrTop = centerY - qrTotalH / 2;
    Rect qrBox = drawQr(c, "https://ko-fi.com/RafaelNGP", panel.x + panel.w - 60, qrTop, kQrTarget, qrcodegen_Ecc_MEDIUM);
    if (qrBox.w > 0) {
        std::string scanText = tr(Str::ScanToDonate);
        int tw = fonts_.semibold.measure(scanText, 22);
        fonts_.semibold.draw(c, scanText, qrBox.x + (qrBox.w - tw) / 2, qrBox.y + qrBox.h + 16, 22, kWhite);
    }

    // Left column: message and link vertically centered aligned with QR code
    int textW = 600;
    int textX = panel.x + 60;
    auto lines = fonts_.regular.wrap(tr(Str::SupportMessage), 26, textW, 10);
    constexpr int kLineH = 38;
    int textContentH = static_cast<int>(lines.size()) * kLineH + 24 + 26;
    int textY = centerY - textContentH / 2;

    for (const auto& line : lines) {
        fonts_.regular.draw(c, line, textX, textY, 26, rgba(230, 230, 230));
        textY += kLineH;
    }

    textY += 24;
    fonts_.semibold.draw(c, "https://ko-fi.com/RafaelNGP", textX, textY, 24, rgba(255, 215, 80));

    c.fillRect({0, kH - 80, kW, 80}, kBg);
    drawHints(c, {{kIconCircle, tr(Str::Back)}});
}

void AppUi::drawKeysTester(Canvas& c) {
    Rect panel = drawModal(c, 1500, 860, tr(Str::KeyboardKeys));
    // Each input: its name (the PlayStation button, the Xbox one beside),
    // then its two keys. Left column the sticks, D-pad, A and B; right the rest.
    constexpr const char* kUp = " \xE2\x86\x91", *kDown = " \xE2\x86\x93", *kLeft = " \xE2\x86\x90", *kRight = " \xE2\x86\x92";
    const std::string stick = tr(Str::StickLeft), dpad = tr(Str::DPad);
    const std::string names[input::kKeyActions] = {
        stick + kUp, stick + kDown, stick + kLeft, stick + kRight, dpad + kUp, dpad + kDown, dpad + kLeft, dpad + kRight,
        "A", "B", "X", "Y", "L1  (LB)", "R1  (RB)", "L2  (LT)", "R2  (RT)", "L3", "R3", "View", "Menu"};
    const int faceIcon[4] = {kIconCross, kIconCircle, kIconSquare, kIconTriangle};  // A B X Y
    // An Xbox pad: its own names alone.
    const bool xbox = g_glyphStyle == GlyphStyle::Xbox;
    std::string xboxNames[input::kKeyActions];
    if (xbox) {
        static constexpr PadButton kShoulders[6] = {PadButton::L1, PadButton::R1, PadButton::L2,
                                                    PadButton::R2, PadButton::L3, PadButton::R3};
        for (int i = 0; i < 6; ++i)
            xboxNames[static_cast<int>(input::KeyAction::LB) + i] = buttonName(GlyphStyle::Xbox, kShoulders[i]);
    }
    constexpr int kRowH = 52, kGap = 8, kChipW = 170, kColGap = 40;
    const int colW = (panel.w - 112 - kColGap) / 2;
    const bool capturing = captureSince_ != 0;
    for (int a = 0; a < input::kKeyActions; ++a) {
        const int col = a / kKeysPerColumn, row = a % kKeysPerColumn;
        Rect r{panel.x + 56 + col * (colW + kColGap), panel.y + 106 + row * (kRowH + kGap), colW, kRowH};
        const bool rowOn = row == keysRow_ && col == keysCol_ / 2;
        c.fillRect(r, rowOn ? rgba(255, 255, 255, 30) : rgba(255, 255, 255, 12), 14);
        int ty = fonts_.semibold.centeredY(r.y, r.h, 24);
        if (a >= static_cast<int>(input::KeyAction::A) && a <= static_cast<int>(input::KeyAction::Y)) {
            int k = a - static_cast<int>(input::KeyAction::A);
            drawFaceButton(c, fonts_.bold, faceIcon[k], r.x + 38.0f, r.y + r.h / 2.0f, 16);
            if (!xbox) fonts_.semibold.draw(c, "(" + names[a] + ")", r.x + 66, ty, 24, kGray);
        } else {
            fonts_.semibold.draw(c, xbox && !xboxNames[a].empty() ? xboxNames[a] : names[a], r.x + 22, ty, 24, kWhite);
        }
        for (int slot = 0; slot < 2; ++slot) {
            Rect chip{r.x + r.w - 10 - (2 - slot) * kChipW - (1 - slot) * 10, r.y + 7, kChipW, r.h - 14};
            const bool on = rowOn && keysCol_ % 2 == slot;
            uint8_t code = settings_.keyMap[static_cast<size_t>(a)][static_cast<size_t>(slot)];
            std::string text = on && capturing ? "..." : code ? input::keyName(code) : "\xE2\x80\x94";  // an em dash: none
            c.fillRect(chip, on ? kWhite : rgba(255, 255, 255, code ? 26 : 10), 10);
            if (on && capturing) c.strokeRect({chip.x - 4, chip.y - 4, chip.w + 8, chip.h + 8}, rgba(90, 220, 110), 3, 13);
            int tw = fonts_.semibold.measure(text, 22);
            fonts_.semibold.draw(c, text, chip.x + (chip.w - tw) / 2, fonts_.semibold.centeredY(chip.y, chip.h, 22), 22,
                                 on ? kBg : code ? kWhite : kDim);
        }
    }
    // Reset to default, across both columns.
    Rect reset{panel.x + 56, panel.y + 106 + kKeysPerColumn * (kRowH + kGap) + 8, panel.w - 112, kRowH};
    const bool resetOn = keysRow_ == kKeysPerColumn;
    c.fillRect(reset, resetOn ? rgba(255, 255, 255, 36) : rgba(255, 255, 255, 14), 14);
    if (resetOn) c.strokeRect({reset.x - 4, reset.y - 4, reset.w + 8, reset.h + 8}, kWhite, 2, 18);
    const char* resetLabel = tr(Str::ResetToDefault);
    fonts_.semibold.draw(c, resetLabel, reset.x + (reset.w - fonts_.semibold.measure(resetLabel, 24)) / 2,
                         fonts_.semibold.centeredY(reset.y, reset.h, 24), 24, resetOn ? kWhite : rgba(240, 100, 100));
    // Waiting for a key: say so (and why a key was refused); else the help.
    int hy = panel.y + panel.h - 74;
    if (capturing) {
        std::string ask = tr(Str::PressKey);
        fonts_.bold.draw(c, ask, panel.x + (panel.w - fonts_.bold.measure(ask, 28)) / 2, hy - 4, 28, kWhite);
        if (keyRefused_) {
            std::string why = trf(Str::KeyReserved, input::keyName(keyRefused_));
            fonts_.regular.draw(c, why, panel.x + (panel.w - fonts_.regular.measure(why, 22)) / 2, hy + 34, 22,
                                rgba(240, 100, 100));
        }
    } else {
        auto help = fonts_.regular.wrap(tr(Str::KeysHelp), 22, panel.w - 112, 2);
        for (size_t i = 0; i < help.size(); ++i)
            fonts_.regular.draw(c, help[i], panel.x + 56, hy + static_cast<int>(i) * 30, 22, kDim);
    }
    c.fillRect({0, kH - 80, kW, 80}, kBg);  // its own hints over drawModal's
    if (capturing)
        drawHints(c, {{kIconCircle, tr(Str::Cancel)}});
    else if (resetOn)
        drawHints(c, {{kIconCross, tr(Str::Select)}, {kIconCircle, tr(Str::Back)}});
    else
        drawHints(c, {{kIconCross, tr(Str::ChangeKey)}, {kIconSquare, tr(Str::ClearKey)}, {kIconCircle, tr(Str::Back)}});
}

void AppUi::drawColourPicker(Canvas& c) {
    c.fillRect({0, 0, kW, kH}, rgba(0, 0, 0, 160));
    constexpr int kPanelW = 1000, kPanelH = 540;
    Rect panel{(kW - kPanelW) / 2, (kH - kPanelH) / 2 - 20, kPanelW, kPanelH};
    c.fillRect({panel.x + 8, panel.y + 12, panel.w, panel.h}, rgba(0, 0, 0, 120), 26);  // shadow
    c.fillRect(panel, kPanel, 24);
    c.strokeRect(panel, rgba(255, 255, 255, 50), 2, 24);
    fonts_.bold.draw(c, tr(Str::LightBarColour), panel.x + 56, panel.y + 44, 40, kWhite);

    // The wheel: angle = hue, distance from the centre = saturation, at the
    // brightness chosen (its pixels kept until that changes).
    const int R = kWheelR, cx = panel.x + 56 + R, cy = panel.y + 120 + R;
    if (wheelValue_ != pickValue_ || wheel_.empty()) {
        wheel_.assign(static_cast<size_t>(4 * R * R), 0);
        for (int y = 0; y < 2 * R; ++y)
            for (int x = 0; x < 2 * R; ++x) {
                float u = (x + 0.5f - R) / R, v = (y + 0.5f - R) / R, d = std::hypot(u, v);
                if (d > 1.0f + 1.0f / R) continue;
                float h = std::atan2(v, u) / 6.2832f;
                Color col = hsvColour(h < 0 ? h + 1 : h, std::min(1.0f, d), pickValue_);
                // Edge coverage, for a smooth rim.
                uint32_t a = static_cast<uint32_t>(std::clamp((1.0f - d) * R + 0.5f, 0.0f, 1.0f) * 255);
                wheel_[static_cast<size_t>(y * 2 * R + x)] = (col & 0x00FFFFFF) | a << 24;
            }
        wheelValue_ = pickValue_;
    }
    uint32_t* px = c.data();
    for (int y = 0; y < 2 * R; ++y)
        for (int x = 0; x < 2 * R; ++x) {
            uint32_t w = wheel_[static_cast<size_t>(y * 2 * R + x)], a = w >> 24;
            if (!a) continue;
            uint32_t& d = px[static_cast<size_t>((cy - R + y) * kW + (cx - R + x))];
            auto mix = [&](int shift) {
                uint32_t s = w >> shift & 0xFF, t = d >> shift & 0xFF;
                return ((s * a + t * (255 - a)) / 255) << shift;
            };
            d = mix(0) | mix(8) | mix(16) | 0xFF000000u;
        }
    float kx = cx + pickU_ * R, ky = cy + pickV_ * R;  // the cursor
    c.strokeArc(kx, ky, 15, 3, 0, 6.2832f, rgba(0, 0, 0, 160));
    c.strokeArc(kx, ky, 12, 3, 0, 6.2832f, kWhite);

    // The colour chosen, and the brightness under it.
    const int sx = panel.x + 56 + 2 * R + 80, sw = panel.x + panel.w - 56 - sx;
    Color chosen = pickerColour();
    c.fillRect({sx, cy - R + 10, sw, 170}, chosen, 20);
    c.strokeRect({sx, cy - R + 10, sw, 170}, rgba(255, 255, 255, 60), 2, 20);
    Rect bar{sx, cy + 30, sw, 18};
    float h = std::atan2(pickV_, pickU_) / 6.2832f, s = std::min(1.0f, std::hypot(pickU_, pickV_));
    c.gradientH(bar, hsvColour(h < 0 ? h + 1 : h, s, kMinValue), hsvColour(h < 0 ? h + 1 : h, s, 1));
    float mx = bar.x + (pickValue_ - kMinValue) / (1 - kMinValue) * bar.w;
    c.fillCircle(mx, bar.y + bar.h / 2.0f, 14, kWhite);
    c.fillCircle(mx, bar.y + bar.h / 2.0f, 9, chosen);
    // The help, a line per control (its parts are four spaces apart).
    std::string help = tr(Str::ColourPickerHelp);
    int hy = cy + 90;
    for (size_t start = 0; start < help.size();) {
        size_t gap = help.find("    ", start);
        std::string part = help.substr(start, gap == std::string::npos ? std::string::npos : gap - start);
        for (const auto& line : fonts_.regular.wrap(part, 22, sw, 2)) {
            fonts_.regular.draw(c, line, sx, hy, 22, kGray);
            hy += 32;
        }
        if (gap == std::string::npos) break;
        start = help.find_first_not_of(' ', gap);
        if (start == std::string::npos) break;
    }

    c.fillRect({0, kH - 80, kW, 80}, kBg);
    drawHints(c, {{kIconCross, tr(Str::Select)}, {kIconCircle, tr(Str::Back)}});
}

void AppUi::drawUpdating(Canvas& c, uint64_t nowMs) {
    drawBackground(c);
    drawLogo(c, kW / 2.0f, 330, 56);
    drawCentered(c, fonts_.bold, trf(Str::UpdatingTo, updatingTo_), 440, 48, kWhite);
    drawCentered(c, fonts_.regular, updateStatus_, 520, 28, kGray);
    if (updateFraction_ >= 0) {
        constexpr int kBarW = 720;
        Rect bar{(kW - kBarW) / 2, 590, kBarW, 12};
        c.fillRect(bar, rgba(255, 255, 255, 40), 6);
        c.fillRect({bar.x, bar.y, static_cast<int>(kBarW * std::min(1.0f, updateFraction_)), bar.h}, kGreen, 6);
    } else {
        drawSpinner(c, kW / 2.0f, 610, 24, nowMs);
    }
}

void AppUi::drawSignOutHold(Canvas& c, uint64_t nowMs) {
    if (!signOutHoldStart_) return;
    float progress = std::min(1.0f, static_cast<float>(nowMs - signOutHoldStart_) / kSignOutHoldMs);
    int secondsLeft = static_cast<int>((kSignOutHoldMs - std::min<uint64_t>(kSignOutHoldMs, nowMs - signOutHoldStart_) + 999) / 1000);
    std::string text = trf(Str::SigningOutIn, std::to_string(secondsLeft));
    int tw = fonts_.semibold.measure(text, 28);
    Rect panel{(kW - (tw + 170)) / 2, kH - 250, tw + 170, 110};
    c.fillRect(panel, rgba(30, 30, 30, 240), 55);
    c.strokeRect(panel, rgba(255, 255, 255, 60), 2, 55);
    float cx = panel.x + 62, cy = panel.y + panel.h / 2.0f;
    c.strokeArc(cx, cy, 30, 6, 0, 6.2832f, rgba(255, 255, 255, 50));
    // Progress ring from 12 o'clock, clockwise, in red as it nears the end.
    Color ring = progress < 0.8f ? kWhite : rgba(255, 110, 110);
    if (progress > 0.01f) c.strokeArc(cx, cy, 30, 6, -1.5708f, 6.2832f * progress, ring);
    fonts_.bold.draw(c, std::to_string(secondsLeft), static_cast<int>(cx) - fonts_.bold.measure(std::to_string(secondsLeft), 26) / 2,
                     static_cast<int>(cy) - 16, 26, kWhite);
    fonts_.semibold.draw(c, text, panel.x + 120, panel.y + 38, 28, kWhite);
    animating_ = true;
}

void AppUi::drawGameSettings(Canvas& c) {
    if (tester_ == Tester::Sticks) {
        drawStickTester(c);
        return;
    }
    if (tester_ == Tester::Triggers) {
        drawTriggerTester(c);
        return;
    }
    if (tester_ == Tester::Keys) {
        drawKeysTester(c);
        return;
    }
    if (tester_ == Tester::Pad) {
        drawPadTester(c);
        return;
    }
    if (tester_ == Tester::Touchpad) {
        drawTouchpadTester(c);
        return;
    }
    std::string title = tr(Str::GameSettings);
    if (!detail_.name.empty()) {
        std::string full = title + "  \xE2\x80\xA2  " + detail_.name;
        if (fonts_.bold.measure(full, 40) <= 960) {
            title = full;
        } else {
            std::string cut = detail_.name;
            while (!cut.empty() && fonts_.bold.measure(title + "  \xE2\x80\xA2  " + cut + "...", 40) > 960) {
                do {
                    cut.pop_back();
                } while (!cut.empty() && (static_cast<uint8_t>(cut.back()) & 0xC0) == 0x80);
            }
            title = title + "  \xE2\x80\xA2  " + cut + "...";
        }
    }
    constexpr int kModalW = 1140, kModalH = 960;
    constexpr int kRowH = 40, kGap = 4, kGroupGap = 7;
    // The rows caps_ allows; the panel loses the height of those it hides
    // (the same as ever with all of them, "Reset to default" or not).
    const auto rows = visibleGameSettingsRows(caps_, activeGameHasCustom_, activeGameMouseSupported_);
    const auto most = visibleGameSettingsRows(caps_, true, activeGameMouseSupported_), all = visibleGameSettingsRows(UiCaps{}, true, true);
    auto groups = [](const std::vector<SettingsRowPos>& v) {
        return static_cast<int>(std::count_if(v.begin(), v.end(), [](const SettingsRowPos& r) { return r.groupStart; }));
    };
    const int modalH = kModalH - static_cast<int>(all.size() - most.size()) * (kRowH + kGap) -
                       (groups(all) - groups(most)) * kGroupGap;
    Rect panel = drawModal(c, kModalW, modalH, title);
    int y = panel.y + 92;

    for (int pos = 0; pos < static_cast<int>(rows.size()); ++pos) {
        const int i = rows[static_cast<size_t>(pos)].row;
        if (rows[static_cast<size_t>(pos)].groupStart) y += kGroupGap;
        Rect r{panel.x + 50, y, panel.w - 100, kRowH};
        bool focused = (pos == gameSettingsRow_);
        c.fillRect(r, focused ? rgba(255, 255, 255, 36) : rgba(255, 255, 255, 14), 14);
        if (focused) c.strokeRect({r.x - 4, r.y - 4, r.w + 8, r.h + 8}, kWhite, 2, 18);

        int ty = fonts_.semibold.centeredY(r.y, r.h, 26);

        if (i == 10) {
            const char* resetLabel = tr(Str::ResetToDefault);
            int tw = fonts_.semibold.measure(resetLabel, 26);
            fonts_.semibold.draw(c, resetLabel, r.x + (r.w - tw) / 2, ty, 26, focused ? kWhite : rgba(240, 100, 100));
            y += kRowH + kGap;
            continue;
        }

        const char* label = "";
        std::string value;

        switch (i) {
        case 0:
            label = tr(Str::Profile);
            value = tr(activeGameHasCustom_ ? Str::ProfileCustom : Str::ProfileDefault);
            break;
        case 1:
            label = tr(Str::MenuUpscaler);
            value = activeGameProfile_.upscaler == 3 ? tr(Str::UpscalerAiClean)
                  : activeGameProfile_.upscaler == 2 ? tr(Str::UpscalerFsrClean)
                  : activeGameProfile_.upscaler == 1 ? tr(Str::UpscalerAi)
                                                     : "FSR";
            break;
        case 2: {
            label = tr(Str::MenuSharpness);
            static constexpr Str kLevels[] = {Str::SharpOff, Str::SharpLow, Str::SharpMedium, Str::SharpHigh};
            value = tr(kLevels[std::clamp(activeGameProfile_.sharpness, 0, 3)]);
            break;
        }
        case 3: {
            label = tr(Str::MenuDeband);
            static constexpr Str kLevels[] = {Str::SharpOff, Str::SharpLow, Str::SharpHigh};
            value = activeGameProfile_.deband == 3 ? "Auto"
                                                   : tr(kLevels[std::clamp(activeGameProfile_.deband, 0, 2)]);
            break;
        }
        case 4:
            label = tr(Str::Resolution);
            value = activeGameProfile_.resolution == 1 ? "720p" : activeGameProfile_.resolution == 2 ? tr(Str::ResBest) : "1080p";
            break;
        case 5:
            label = tr(Str::Deadzone);
            value = std::to_string(activeGameProfile_.deadzoneLeft) + " %";
            break;
        case 6: {
            label = tr(Str::TriggerRumble);
            static constexpr Str kStrengths[] = {Str::Deactivated, Str::TriggerLight, Str::TriggerMedium,
                                                 Str::TriggerStrong, Str::TriggerMax};
            value = tr(kStrengths[std::clamp(activeGameProfile_.triggerStrength, 0, 4)]);
            break;
        }
        case 16: {
            label = tr(Str::AudioHaptics);
            static constexpr Str kHapticsLevels[] = {Str::Off, Str::SharpLow, Str::SharpMedium, Str::SharpHigh};
            value = tr(kHapticsLevels[std::clamp(activeGameProfile_.audioHaptics, 0, 3)]);
            break;
        }
        case 7:
            label = tr(Str::ConfirmButton);
            break;
        case 8:
            label = tr(Str::HighRefresh);
            value = tr(activeGameProfile_.highRefresh ? Str::HighRefreshOn : Str::Deactivated);
            break;
        case 9:
            label = tr(Str::KeyboardKeys);
            value = tr(activeGameProfile_.keyMap == input::defaultKeyMap() ? Str::ProfileDefault : Str::ProfileCustom);
            break;
        case 11:
            label = tr(Str::ControllerButtons);
            value = tr(activeGameProfile_.padMap == input::defaultPadMap() ? Str::ProfileDefault : Str::ProfileCustom);
            break;
        case 17:
            label = tr(Str::TouchpadShortcuts);
            value = tr(activeGameProfile_.touchpadShortcuts == input::defaultTouchpadShortcuts() ? Str::ProfileDefault : Str::ProfileCustom);
            break;
        case 12:
            label = tr(Str::Gyro);
            value = tr(activeGameProfile_.gyroMode == 1 ? Str::GyroAlways
                     : activeGameProfile_.gyroMode == 2 ? Str::GyroAiming
                     : Str::Off);
            break;
        case 13:
            label = tr(Str::TouchpadMouse);
            value = tr(activeGameProfile_.touchpadMouse ? Str::Activated : Str::Deactivated);
            break;
        case 14: {
            static const std::pair<const char*, const char*> kLocales[] = {
                {"", ""}, {"en-US", "English (US)"}, {"pt-BR", "Português (Brasil)"},
                {"es-ES", "Español"}, {"fr-FR", "Français"}, {"de-DE", "Deutsch"},
                {"it-IT", "Italiano"}, {"ja-JP", "日本語"}
            };
            label = tr(Str::GameLanguage);
            if (activeGameProfile_.locale.empty()) {
                value = tr(Str::ProfileDefault);
            } else {
                for (const auto& [code, name] : kLocales) {
                    if (activeGameProfile_.locale == code) {
                        value = name;
                        break;
                    }
                }
                if (value.empty()) value = activeGameProfile_.locale;
            }
            break;
        }
        case 15:
            label = tr(Str::VideoDecoder);
            value = activeGameProfile_.videoDecoder == 1 ? tr(Str::VideoDecoderSw) : tr(Str::VideoDecoderHw);
            break;
        default: break;
        }

        fonts_.semibold.draw(c, label, r.x + 28, ty, 26, kWhite);

        if (i == 7) {
            float cx = static_cast<float>(r.x + r.w - 50), cy = static_cast<float>(r.y + r.h / 2);
            if (focused) {
                fonts_.regular.draw(c, "\xE2\x80\xB9", static_cast<int>(cx) - 40, fonts_.regular.centeredY(r.y, r.h, 26), 26, kWhite);
                fonts_.regular.draw(c, "\xE2\x80\xBA", static_cast<int>(cx) + 28, fonts_.regular.centeredY(r.y, r.h, 26), 26, kWhite);
            }
            drawFaceButton(c, fonts_.bold, activeGameProfile_.circleConfirms ? kIconCircle : kIconCross, cx, cy, 16);
        } else {
            std::string disp = focused ? ("\xE2\x80\xB9  " + value + "  \xE2\x80\xBA") : value;
            int vw = fonts_.regular.measure(disp, 26);
            fonts_.regular.draw(c, disp, r.x + r.w - 28 - vw, fonts_.regular.centeredY(r.y, r.h, 26), 26,
                                focused ? kWhite : kGray);
        }

        y += kRowH + kGap;
    }
}

}  // namespace xc::ui
