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

bool Settings::load(const std::string& path) {
    std::string text;
    if (!platform::readFile(path, text)) return false;
    auto j = json::parse(text);
    if (!j) return false;
    language = (*j)["language"].str(language);
    resolution = (*j)["resolution"].str(resolution);
    region = (*j)["region"].str(region);
    if (resolution != "720p" && resolution != "1440p") resolution = "1080p";
    hidden.clear();
    for (const auto& id : (*j)["hidden"].items())
        if (!id.str().empty()) hidden.push_back(id.str());
    librarySort = (*j)["librarySort"].str(librarySort);
    streamStats = (*j)["streamStats"].asBool(streamStats);
    sharpness = static_cast<int>(std::clamp<int64_t>((*j)["sharpness"].asInt(sharpness), 0, 3));
    deband = static_cast<int>(std::clamp<int64_t>((*j)["deband"].asInt(deband), 0, 3));
    upscaler = static_cast<int>(std::clamp<int64_t>((*j)["upscaler"].asInt(upscaler), 0, 3));
    deadzone = static_cast<int>(std::clamp<int64_t>((*j)["deadzone"].asInt(deadzone), 0, 50));
    triggerRumble = (*j)["triggerRumble"].asBool(triggerRumble);
    circleConfirms = (*j)["circleConfirms"].asBool(circleConfirms);
    // "lightBar" (on/off) until the colour could be chosen.
    lightBarMode = (*j)["lightBar"].asBool(true) ? 0 : 2;
    lightBarMode = static_cast<int>(std::clamp<int64_t>((*j)["lightBarMode"].asInt(lightBarMode), 0, 2));
    if (std::string hex = (*j)["lightBarColour"].str(); hex.size() == 7 && hex[0] == '#') {
        uint32_t rgb = static_cast<uint32_t>(std::strtoul(hex.c_str() + 1, nullptr, 16));
        lightBarColour = 0xFF000000u | (rgb >> 16 & 0xFF) | (rgb & 0xFF00) | (rgb & 0xFF) << 16;
    }
    gestureHints = static_cast<int>((*j)["gestureHints"].asInt(gestureHints));
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
    regionRtt.clear();
    for (const auto& [name, ms] : (*j)["regionRtt"].members())
        if (ms.asInt() > 0) regionRtt[name] = static_cast<int>(ms.asInt());
    return true;
}

bool Settings::save(const std::string& path) const {
    json::Value v = json::Value::object();
    v.set("language", language);
    v.set("resolution", resolution);
    v.set("region", region);
    json::Value rtt = json::Value::object();
    for (const auto& [name, ms] : regionRtt) rtt.set(name, ms);
    v.set("regionRtt", rtt);
    json::Value hiddenIds = json::Value::array();
    for (const auto& id : hidden) hiddenIds.push(id);
    v.set("hidden", hiddenIds);
    v.set("librarySort", librarySort);
    v.set("streamStats", streamStats);
    v.set("sharpness", sharpness);
    v.set("deband", deband);
    v.set("upscaler", upscaler);
    v.set("deadzone", deadzone);
    v.set("triggerRumble", triggerRumble);
    v.set("circleConfirms", circleConfirms);
    v.set("lightBarMode", lightBarMode);
    char hex[8];
    std::snprintf(hex, sizeof hex, "#%02X%02X%02X", static_cast<unsigned>(lightBarColour & 0xFF),
                  static_cast<unsigned>(lightBarColour >> 8 & 0xFF), static_cast<unsigned>(lightBarColour >> 16 & 0xFF));
    v.set("lightBarColour", std::string(hex));
    v.set("gestureHints", gestureHints);
    v.set("skippedUpdate", skippedUpdate);
    v.set("maxHeightCloud", maxHeightCloud);
    v.set("maxHeightHome", maxHeightHome);
    v.set("probedCloud", probedCloud);
    v.set("probedHome", probedHome);
    v.set("settingsVersion", 2);
    return platform::writeFileAtomic(path, v.dump());
}

}  // namespace xc::app
