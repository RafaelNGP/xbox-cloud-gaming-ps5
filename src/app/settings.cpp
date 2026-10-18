// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
#include "app/settings.h"

#include "platform/platform.h"
#include "util/json.h"
#include "util/log.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>

namespace xc::app {

namespace {

// "keyMap": each input's two keys, in KeyAction order (flat). Anything else
// (an older file, another count) keeps `fallback`.
input::KeyMap readKeyMap(const json::Value& v, const input::KeyMap& fallback) {
    const auto& items = v.items();
    if (items.size() != static_cast<size_t>(input::kKeyActions) * 2) return fallback;
    input::KeyMap m{};
    for (size_t i = 0; i < items.size(); ++i)
        m[i / 2][i % 2] = static_cast<uint8_t>(std::clamp<int64_t>(items[i].asInt(0), 0, 255));
    return m;
}

json::Value keyMapJson(const input::KeyMap& m) {
    json::Value v = json::Value::array();
    for (const auto& keys : m)
        for (uint8_t k : keys) v.push(static_cast<int>(k));
    return v;
}

// "padMap": absent keeps `fallback`; present but not a complete map of
// this version (an edited or damaged file) gives the default map.
input::PadMap readPadMapOr(const json::Value& v, const input::PadMap& fallback, const char* where) {
    if (v.isNull()) return fallback;
    input::PadMap m;
    if (!input::readPadMap(v, m)) XC_LOGW("settings: controller buttons of %s not valid; back to the default", where);
    return m;
}

}  // namespace

bool Settings::load(const std::string& path) {
    std::string text;
    if (!platform::readFile(path, text)) return false;
    auto j = json::parse(text);
    if (!j) return false;
    language = (*j)["language"].str(language);
    resolution = (*j)["resolution"].str(resolution);
    region = (*j)["region"].str(region);
    loginCountry = (*j)["loginCountry"].str(loginCountry);
    if (resolution != "720p" && resolution != "1440p" && resolution != "best") resolution = "1080p";
    hidden.clear();
    for (const auto& id : (*j)["hidden"].items())
        if (!id.str().empty()) hidden.push_back(id.str());
    favorites.clear();
    for (const auto& id : (*j)["favorites"].items())
        if (!id.str().empty()) favorites.push_back(id.str());
    librarySort = (*j)["librarySort"].str(librarySort);
    streamStats = (*j)["streamStats"].asBool(streamStats);
    sharpness = static_cast<int>(std::clamp<int64_t>((*j)["sharpness"].asInt(sharpness), 0, 3));
    deband = static_cast<int>(std::clamp<int64_t>((*j)["deband"].asInt(deband), 0, 3));
    upscaler = static_cast<int>(std::clamp<int64_t>((*j)["upscaler"].asInt(upscaler), 0, 3));
    // "deadzone": one for both sticks, until each could have its own.
    int both = static_cast<int>(std::clamp<int64_t>((*j)["deadzone"].asInt(15), 0, 50));
    deadzoneLeft = static_cast<int>(std::clamp<int64_t>((*j)["deadzoneLeft"].asInt(both), 0, 50));
    deadzoneRight = static_cast<int>(std::clamp<int64_t>((*j)["deadzoneRight"].asInt(both), 0, 50));
    // "triggerRumble" (on/off) until the strength could be chosen.
    triggerStrength = (*j)["triggerRumble"].asBool(true) ? 2 : 0;
    triggerStrength = static_cast<int>(std::clamp<int64_t>((*j)["triggerStrength"].asInt(triggerStrength), 0, 4));
    triggerHz = static_cast<int>(std::clamp<int64_t>((*j)["triggerHz"].asInt(triggerHz), 0, 2));
    triggerResistance = static_cast<int>(std::clamp<int64_t>((*j)["triggerResistance"].asInt(0), 0, 3));
    triggerPulses = (*j)["triggerPulses"].asBool(false);
    triggerProfile = static_cast<int>(std::clamp<int64_t>((*j)["triggerProfile"].asInt(0), 0, 2));
    circleConfirms = (*j)["circleConfirms"].asBool(circleConfirms);
    // "lightBar" (on/off) until the colour could be chosen.
    lightBarMode = (*j)["lightBar"].asBool(true) ? 0 : 2;
    lightBarMode = static_cast<int>(std::clamp<int64_t>((*j)["lightBarMode"].asInt(lightBarMode), 0, 2));
    if (std::string hex = (*j)["lightBarColour"].str(); hex.size() == 7 && hex[0] == '#') {
        uint32_t rgb = static_cast<uint32_t>(std::strtoul(hex.c_str() + 1, nullptr, 16));
        lightBarColour = 0xFF000000u | (rgb >> 16 & 0xFF) | (rgb & 0xFF00) | (rgb & 0xFF) << 16;
    }
    gestureHints = static_cast<int>((*j)["gestureHints"].asInt(gestureHints));
    micEnabled = (*j)["micEnabled"].asBool(micEnabled);
    highRefresh = (*j)["highRefresh"].asBool(highRefresh);
    desktopFullscreen = (*j)["desktopFullscreen"].asBool(desktopFullscreen);
    maxBitrateMbps = static_cast<int>(std::clamp<int64_t>((*j)["maxBitrateMbps"].asInt(maxBitrateMbps), 0, 40));
    gameVolume = static_cast<int>(std::clamp<int64_t>((*j)["gameVolume"].asInt(gameVolume), 0, 200));
    keyMap = readKeyMap((*j)["keyMap"], input::defaultKeyMap());
    padMap = readPadMapOr((*j)["padMap"], input::defaultPadMap(), "the settings");
    keyboardPlayer = static_cast<int>(std::clamp<int64_t>((*j)["keyboardPlayer"].asInt(keyboardPlayer), 0, 3));
    gyroMode = static_cast<int>(std::clamp<int64_t>((*j)["gyroMode"].asInt(gyroMode), 0, 2));
    gyroLevel = static_cast<int>(std::clamp<int64_t>((*j)["gyroLevel"].asInt(gyroLevel), 1, 10));
    touchpadMouse = (*j)["touchpadMouse"].asBool(touchpadMouse);
    micGain = static_cast<int>(std::clamp<int64_t>((*j)["micGain"].asInt(micGain), 0, 2));
    audioRoute = static_cast<int>(std::clamp<int64_t>((*j)["audioRoute"].asInt(audioRoute), 0, 1));
    skippedUpdate = (*j)["skippedUpdate"].str();
    maxHeightCloud = static_cast<int>((*j)["maxHeightCloud"].asInt(0));
    maxHeightHome = static_cast<int>((*j)["maxHeightHome"].asInt(0));
    probedCloud = (*j)["probedCloud"].asInt(0);
    probedHome = (*j)["probedHome"].asInt(0);
    // Files from before v0.8.0 (no version): what still holds the old
    // defaults moves to the new ones (block smoothing low -> auto, FSR ->
    // FSR + clean-up); anything chosen otherwise stays.
    if ((*j)["settingsVersion"].asInt(1) < 2) {
        if (deband == 1) deband = 3;
        if (upscaler == 0) upscaler = 2;
        XC_LOGI("settings: from before v0.8.0; block smoothing %d, upscaling %d", deband, upscaler);
    }
    if ((*j)["settingsVersion"].asInt(1) < 3) {
        if (sharpness == 0) sharpness = 2;
        XC_LOGI("settings: upgraded default sharpness to %d (FidelityFX RCAS)", sharpness);
    }
    regionRtt.clear();
    for (const auto& [name, ms] : (*j)["regionRtt"].members())
        if (ms.asInt() > 0) regionRtt[name] = static_cast<int>(ms.asInt());
    perGame.clear();
    for (const auto& [id, obj] : (*j)["perGame"].members()) {
        GameProfile p;
        p.sharpness = static_cast<int>(std::clamp<int64_t>(obj["sharpness"].asInt(sharpness), 0, 3));
        p.deband = static_cast<int>(std::clamp<int64_t>(obj["deband"].asInt(deband), 0, 3));
        p.upscaler = static_cast<int>(std::clamp<int64_t>(obj["upscaler"].asInt(upscaler), 0, 3));
        int resDef = resolution == "720p" ? 1 : resolution == "1440p" ? 2 : 0;
        p.resolution = static_cast<int>(std::clamp<int64_t>(obj["resolution"].asInt(resDef), 0, 2));
        p.deadzoneLeft = static_cast<int>(std::clamp<int64_t>(obj["deadzoneLeft"].asInt(deadzoneLeft), 0, 50));
        p.deadzoneRight = static_cast<int>(std::clamp<int64_t>(obj["deadzoneRight"].asInt(deadzoneRight), 0, 50));
        p.triggerStrength = static_cast<int>(std::clamp<int64_t>(obj["triggerStrength"].asInt(triggerStrength), 0, 4));
        p.triggerHz = static_cast<int>(std::clamp<int64_t>(obj["triggerHz"].asInt(triggerHz), 0, 2));
        p.triggerResistance = static_cast<int>(std::clamp<int64_t>(obj["triggerResistance"].asInt(triggerResistance), 0, 3));
        p.triggerPulses = obj["triggerPulses"].asBool(triggerPulses);
        p.triggerProfile = static_cast<int>(std::clamp<int64_t>(obj["triggerProfile"].asInt(triggerProfile), 0, 2));
        p.circleConfirms = obj["circleConfirms"].asBool(circleConfirms);
        p.highRefresh = obj["highRefresh"].asBool(highRefresh);
        p.keyMap = readKeyMap(obj["keyMap"], keyMap);
        p.padMap = readPadMapOr(obj["padMap"], padMap, id.c_str());
        p.gyroMode = static_cast<int>(std::clamp<int64_t>(obj["gyroMode"].asInt(gyroMode), 0, 2));
        p.gyroLevel = static_cast<int>(std::clamp<int64_t>(obj["gyroLevel"].asInt(gyroLevel), 1, 10));
        p.touchpadMouse = obj["touchpadMouse"].asBool(touchpadMouse);
        p.locale = obj["locale"].str();
        perGame[id] = p;
    }
    return true;
}

float Settings::micGainFactor() const {
    static constexpr float kFactors[] = {0.5f, 1.0f, 1.5f};
    return kFactors[std::clamp(micGain, 0, 2)];
}

GameProfile Settings::defaultProfile() const {
    GameProfile p;
    p.sharpness = sharpness;
    p.deband = deband;
    p.upscaler = upscaler;
    p.resolution = resolution == "720p" ? 1 : resolution == "1440p" ? 2 : 0;
    p.deadzoneLeft = deadzoneLeft;
    p.deadzoneRight = deadzoneRight;
    p.triggerStrength = triggerStrength;
    p.triggerHz = triggerHz;
    p.triggerResistance = triggerResistance;
    p.triggerPulses = triggerPulses;
    p.triggerProfile = triggerProfile;
    p.circleConfirms = circleConfirms;
    p.highRefresh = highRefresh;
    p.keyMap = keyMap;
    p.padMap = padMap;
    p.gyroMode = gyroMode;
    p.gyroLevel = gyroLevel;
    p.touchpadMouse = touchpadMouse;
    return p;
}

GameProfile Settings::profileForGame(const std::string& productId, const std::string& titleId, bool* hasCustom) const {
    if (!productId.empty()) {
        auto it = perGame.find(productId);
        if (it != perGame.end()) {
            if (hasCustom) *hasCustom = true;
            return it->second;
        }
    }
    if (!titleId.empty()) {
        auto it = perGame.find(titleId);
        if (it != perGame.end()) {
            if (hasCustom) *hasCustom = true;
            return it->second;
        }
    }
    if (hasCustom) *hasCustom = false;
    return defaultProfile();
}

bool Settings::save(const std::string& path) const {
    json::Value v = json::Value::object();
    v.set("language", language);
    v.set("resolution", resolution);
    v.set("region", region);
    v.set("loginCountry", loginCountry);
    json::Value rtt = json::Value::object();
    for (const auto& [name, ms] : regionRtt) rtt.set(name, ms);
    v.set("regionRtt", rtt);
    json::Value hiddenIds = json::Value::array();
    for (const auto& id : hidden) hiddenIds.push(id);
    v.set("hidden", hiddenIds);
    json::Value favIds = json::Value::array();
    for (const auto& id : favorites) favIds.push(id);
    v.set("favorites", favIds);
    v.set("librarySort", librarySort);
    v.set("streamStats", streamStats);
    v.set("sharpness", sharpness);
    v.set("deband", deband);
    v.set("upscaler", upscaler);
    v.set("deadzoneLeft", deadzoneLeft);
    v.set("deadzoneRight", deadzoneRight);
    v.set("triggerStrength", triggerStrength);
    v.set("triggerHz", triggerHz);
    v.set("triggerResistance", triggerResistance);
    v.set("triggerPulses", triggerPulses);
    v.set("triggerProfile", triggerProfile);
    v.set("circleConfirms", circleConfirms);
    v.set("lightBarMode", lightBarMode);
    char hex[8];
    std::snprintf(hex, sizeof hex, "#%02X%02X%02X", static_cast<unsigned>(lightBarColour & 0xFF),
                  static_cast<unsigned>(lightBarColour >> 8 & 0xFF), static_cast<unsigned>(lightBarColour >> 16 & 0xFF));
    v.set("lightBarColour", std::string(hex));
    v.set("gestureHints", gestureHints);
    v.set("micEnabled", micEnabled);
    v.set("highRefresh", highRefresh);
    v.set("desktopFullscreen", desktopFullscreen);
    v.set("maxBitrateMbps", maxBitrateMbps);
    v.set("gameVolume", gameVolume);
    v.set("keyMap", keyMapJson(keyMap));
    v.set("padMap", input::padMapJson(padMap));
    v.set("keyboardPlayer", keyboardPlayer);
    v.set("gyroMode", gyroMode);
    v.set("gyroLevel", gyroLevel);
    v.set("touchpadMouse", touchpadMouse);
    v.set("micGain", micGain);
    v.set("audioRoute", audioRoute);
    v.set("skippedUpdate", skippedUpdate);
    v.set("maxHeightCloud", maxHeightCloud);
    v.set("maxHeightHome", maxHeightHome);
    v.set("probedCloud", probedCloud);
    v.set("probedHome", probedHome);
    json::Value pg = json::Value::object();
    for (const auto& [id, p] : perGame) {
        json::Value obj = json::Value::object();
        obj.set("sharpness", p.sharpness);
        obj.set("deband", p.deband);
        obj.set("upscaler", p.upscaler);
        obj.set("resolution", p.resolution);
        obj.set("deadzoneLeft", p.deadzoneLeft);
        obj.set("deadzoneRight", p.deadzoneRight);
        obj.set("triggerStrength", p.triggerStrength);
        obj.set("triggerHz", p.triggerHz);
        obj.set("triggerResistance", p.triggerResistance);
        obj.set("triggerPulses", p.triggerPulses);
        obj.set("triggerProfile", p.triggerProfile);
        obj.set("circleConfirms", p.circleConfirms);
        obj.set("highRefresh", p.highRefresh);
        obj.set("keyMap", keyMapJson(p.keyMap));
        obj.set("padMap", input::padMapJson(p.padMap));
        obj.set("gyroMode", p.gyroMode);
        obj.set("gyroLevel", p.gyroLevel);
        obj.set("touchpadMouse", p.touchpadMouse);
        if (!p.locale.empty()) obj.set("locale", p.locale);
        pg.set(id, obj);
    }
    v.set("perGame", pg);
    v.set("settingsVersion", 3);
    if (platform::writeFileAtomic(path, v.dump())) return true;
    XC_LOGW("settings: could not save %s", path.c_str());  // every caller's warning
    return false;
}

}  // namespace xc::app
