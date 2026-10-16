// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
// User settings, kept in <dataDir>/settings.json.
#pragma once

#include "input/key_map.h"

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace xc::app {

struct GameProfile {
    // Picture
    int sharpness = 0;           // 0 off, 1..3 low, medium, high
    int deband = 3;              // 0 off, 1 low, 2 high, 3 auto
    int upscaler = 2;            // 0 FSR 1, 1 Anime4K, 2 FSR + clean, 3 Anime4K + clean
    int resolution = 0;          // 0 1080p, 1 720p, 2 1440p
    // Controller: dead zone (percent), triggers, confirm button
    int deadzoneLeft = 15, deadzoneRight = 15;
    int triggerStrength = 2;     // 0 off .. 4 max
    int triggerHz = 1;           // 0..2
    int triggerResistance = 0;   // 0 off .. 3 strong
    bool triggerPulses = false;
    bool circleConfirms = false;
    // Display
    bool highRefresh = false;    // 120 Hz output (VRR) while streaming
    // Keyboard and mouse as a controller: which keys play what
    input::KeyMap keyMap = input::defaultKeyMap();
    // Gyro aiming: 0 off, 1 always, 2 while L2 is held; intensity 1..10
    int gyroMode = 0;
    int gyroLevel = 5;
};

// Key to identify a game profile: productId first, else titleId.
inline std::string gameProfileKey(const std::string& productId, const std::string& titleId) {
    return !productId.empty() ? productId : titleId;
}

struct Settings {
    std::string language = "en";     // ui::languageCode()
    std::string resolution = "1080p";  // "1080p", "720p" or "1440p" (experimental)
    std::string region;                // gssv region name; empty = automatic
    // "Sign in as another country" (xcloud::loginCountryAddress): "" off.
    std::string loginCountry;
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
    // Voice chat & Audio routing
    bool micEnabled = true;
    bool highRefresh = false;  // 119.88 Hz output (VRR where the console turns it on) while streaming
    int maxBitrateMbps = 0;    // the stream's bitrate cap: 0 automatic (the tier's), else 20, 15, 10 or 5
    int gameVolume = 100;      // percent: 25, 50, 75, 100 or 150
    input::KeyMap keyMap = input::defaultKeyMap();  // the keys when the keyboard is a player's controller
    int keyboardPlayer = 0;    // the player a USB keyboard and mouse play as (0..3; keys 1-4): 0 native with player 1, else that player's controller
    int gyroMode = 0;          // gyro aiming (input/gyro.h): 0 off, 1 always, 2 while L2 is held
    int gyroLevel = 5;         // its intensity, 1..10
    int micGain = 1;        // 0: 50%, 1: 100%, 2: 150%
    int audioRoute = 0;     // 0: TV/HDMI (Main), 1: DualSense Speaker / Headset (Pad)
    // The release the user said "Not now" to: not offered again on start.
    std::string skippedUpdate;
    // The tallest picture each kind of stream delivered when asked for its
    // top tier, and when (unix s): 1440p is only offered once one did.
    int maxHeightCloud = 0, maxHeightHome = 0;
    int64_t probedCloud = 0, probedHome = 0;  // streams that showed the touchpad gestures' hint (three do)

    // Per-game profiles overriding the general settings (productId or titleId -> profile).
    std::map<std::string, GameProfile> perGame;

    GameProfile defaultProfile() const;
    GameProfile profileForGame(const std::string& productId, const std::string& titleId, bool* hasCustom = nullptr) const;

    bool load(const std::string& path);
    bool save(const std::string& path) const;
};

}  // namespace xc::app
