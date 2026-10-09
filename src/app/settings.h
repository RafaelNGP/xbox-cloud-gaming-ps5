// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
// User settings, kept in <dataDir>/settings.json.
#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace xc::app {

struct Settings {
    std::string language = "en";     // ui::languageCode()
    std::string resolution = "1080p";  // "1080p", "720p" or "1440p" (experimental)
    std::string region;                // gssv region name; empty = automatic
    // Round trip to the stream server measured in past sessions, per region (ms).
    std::map<std::string, int> regionRtt;
    // Games hidden with Square (product ids), and the "Your games" order
    // ("recent", "az", "console").
    std::vector<std::string> hidden;
    std::string librarySort = "recent";
    // The statistics line over the game (in-game menu).
    bool streamStats = false;
    // Sharpening of the game picture: 0 off, 1..3 low, medium, high.
    int sharpness = 0;
    // Smoothing of compression blocks (GPU display): 0 off, 1 low, 2 high,
    // 3 auto (stronger as the bitrate falls).
    int deband = 3;
    // Upscaling to the 4K display (GPU): 0 FSR 1, 1 Anime4K, 2 FSR and 3
    // Anime4K after Anime4K Restore (the clean-up of compression artefacts).
    int upscaler = 2;
    // Controller: stick dead zone (percent), vibration in the triggers,
    // Circle as the confirm button (Xbox A).
    int deadzoneLeft = 15, deadzoneRight = 15;  // percent, each stick's own
    // Trigger vibration: strength 0 (off) .. 4 (max), its frequency as an
    // index into input::kTriggerHz (input/tuning.h).
    int triggerStrength = 2;
    int triggerHz = 1;
    int triggerResistance = 0;   // 0 off .. 3 strong (the triggers' weight)
    bool triggerPulses = false;  // pulses of the resistance instead of the motor vibrating
    bool circleConfirms = false;
    // The DualSense light bar: 0 the colour of the game in focus, 1
    // lightBarColour, 2 off.
    int lightBarMode = 0;
    uint32_t lightBarColour = 0xFFDC7000;  // as ui::Color: 0xAABBGGRR, opaque (blue)
    int gestureHints = 0;
    // The release the user said "Not now" to: not offered again on start.
    std::string skippedUpdate;
    // The tallest picture each kind of stream delivered when asked for its
    // top tier, and when (unix s): 1440p is only offered once one did.
    int maxHeightCloud = 0, maxHeightHome = 0;
    int64_t probedCloud = 0, probedHome = 0;  // streams that showed the touchpad gestures' hint (three do)

    bool load(const std::string& path);
    bool save(const std::string& path) const;
};

}  // namespace xc::app
