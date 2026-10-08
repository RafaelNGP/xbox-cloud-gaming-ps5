// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
#include "app/settings.h"

#include "platform/platform.h"
#include "util/json.h"

#include <algorithm>

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
    deband = static_cast<int>(std::clamp<int64_t>((*j)["deband"].asInt(deband), 0, 2));
    upscaler = static_cast<int>(std::clamp<int64_t>((*j)["upscaler"].asInt(upscaler), 0, 1));
    deadzone = static_cast<int>(std::clamp<int64_t>((*j)["deadzone"].asInt(deadzone), 0, 50));
    triggerRumble = (*j)["triggerRumble"].asBool(triggerRumble);
    circleConfirms = (*j)["circleConfirms"].asBool(circleConfirms);
    lightBar = (*j)["lightBar"].asBool(lightBar);
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
    v.set("lightBar", lightBar);
    return platform::writeFileAtomic(path, v.dump());
}

}  // namespace xc::app
