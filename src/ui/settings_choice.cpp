// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
// Settings' screen <-> settings.json (app_ui.h).
#include "ui/app_ui.h"
#include "ui/strings.h"

#include <algorithm>

namespace xc::ui {

SettingsChoice choiceFromSettings(const app::Settings& s) {
    SettingsChoice c;
    c.language = static_cast<int>(languageFromCode(s.language));
    c.resolution = s.resolution == "720p" ? 1 : (s.resolution == "1440p" || s.resolution == "best") ? 2 : 0;
    c.region = s.region;
    c.loginCountry = s.loginCountry;
    c.deadzone[0] = std::clamp(s.deadzoneLeft, 0, kMaxDeadzone);
    c.deadzone[1] = std::clamp(s.deadzoneRight, 0, kMaxDeadzone);
    c.triggerStrength = s.triggerStrength;
    c.triggerHz = s.triggerHz;
    c.triggerResistance = s.triggerResistance;
    c.triggerPulses = s.triggerPulses;
    c.triggerProfile = s.triggerProfile;
    c.circleConfirms = s.circleConfirms;
    c.lightBarMode = s.lightBarMode;
    c.lightBarColour = s.lightBarColour;
    c.sharpness = s.sharpness;
    c.deband = s.deband;
    c.upscaler = s.upscaler;
    c.highRefresh = s.highRefresh;
    c.maxBitrateMbps = s.maxBitrateMbps;
    c.gameVolume = s.gameVolume;
    c.keyMap = s.keyMap;
    c.padMap = s.padMap;
    c.gyroMode = s.gyroMode;
    c.gyroLevel = s.gyroLevel;
    c.fullscreen = s.desktopFullscreen;
    c.touchpadMouse = s.touchpadMouse;
    return c;
}

void applyChoice(const SettingsChoice& c, app::Settings& s) {
    s.language = languageCode(static_cast<Language>(c.language));
    s.resolution = c.resolution == 1 ? "720p" : c.resolution == 2 ? "best" : "1080p";
    s.region = c.region;
    s.loginCountry = c.loginCountry;
    s.deadzoneLeft = c.deadzone[0];
    s.deadzoneRight = c.deadzone[1];
    s.triggerStrength = c.triggerStrength;
    s.triggerHz = c.triggerHz;
    s.triggerResistance = c.triggerResistance;
    s.triggerPulses = c.triggerPulses;
    s.triggerProfile = c.triggerProfile;
    s.circleConfirms = c.circleConfirms;
    s.lightBarMode = c.lightBarMode;
    s.lightBarColour = c.lightBarColour;
    s.sharpness = c.sharpness;
    s.deband = c.deband;
    s.upscaler = c.upscaler;
    s.highRefresh = c.highRefresh;
    s.maxBitrateMbps = c.maxBitrateMbps;
    s.gameVolume = c.gameVolume;
    s.keyMap = c.keyMap;
    if (input::padMapValid(c.padMap)) s.padMap = c.padMap;  // the screen only lets a complete one out
    s.gyroMode = c.gyroMode;
    s.gyroLevel = c.gyroLevel;
    s.desktopFullscreen = c.fullscreen;
    s.touchpadMouse = c.touchpadMouse;
}

}  // namespace xc::ui
