// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
// What is drawn over a running game: the in-game menu (OPTIONS + TOUCHPAD)
// and the statistics line. Each is drawn into its own small canvas, which
// display::setOverlay() lays over the video.
#pragma once

#include "ui/app_ui.h"
#include "ui/canvas.h"
#include "ui/font.h"
#include "ui/pad_icons.h"

#include <string>

namespace xc::ui {

// The stream's numbers over the last second.
struct StreamInfo {
    std::string region;
    int rttMs = -1;
    double fps = 0, mbps = 0, lossPct = 0, decodeMs = 0;
    double onScreenMs = 0;  // network arrival to on the TV (GPU path); 0 = unknown
    int width = 0, height = 0;  // of the decoded picture
};

// Close: the menu went away (Circle, OPTIONS, or the Xbox button): a fresh key frame is asked for.
enum class MenuAction {
    None, Close, Leave, Resolution, Stats, Sharpness, Deband, Upscaler, XboxButton,
    ProfileToggle, Triggers, Deadzone, ConfirmButton, MicToggle
};

class StreamMenu {
public:
    static constexpr int kMenuW = 620, kMenuH = 1064;
    static constexpr int kMenuX = 80, kMenuY = (1080 - kMenuH) / 2;
    static constexpr int kStatsX = 32, kStatsY = 28;

    explicit StreamMenu(const Fonts& fonts) : fonts_(fonts) {}

    // `resolution` as SettingsChoice::resolution (0 = 1080p, 1 = 720p, 2 = 1440p).
    // `sharpness` 0 = off, 1..3 = low, medium, high; `deband` 0..2 = off,
    // low, high.
    // `upscaler` 0 = FSR, 1 = Anime4K.
    // `homeConsole`: the user's own Xbox ("End the stream" instead of "Leave the game").
    // `allow1440`: offered only once a stream delivered it.
    void open(int resolution, bool stats, int sharpness = 0, int deband = 1, int upscaler = 0, bool homeConsole = false,
              bool allow1440 = true, bool hasCustomProfile = false, int triggerStrength = 2, int deadzone = 15,
              bool circleConfirms = false, float micLevel = 0.0f, bool micMuted = false);
    void close() { open_ = false; }
    bool isOpen() const { return open_; }
    MenuAction handle(const NavInput& in);
    int resolution() const { return resolution_; }
    bool statsOn() const { return stats_; }
    int sharpness() const { return sharpness_; }
    int deband() const { return deband_; }
    int upscaler() const { return upscaler_; }
    bool hasCustomProfile() const { return hasCustomProfile_; }
    void setHasCustomProfile(bool custom) { hasCustomProfile_ = custom; }
    int triggerStrength() const { return triggerStrength_; }
    void setTriggerStrength(int strength) { triggerStrength_ = strength; }
    int deadzone() const { return deadzone_; }
    void setDeadzone(int deadzone) { deadzone_ = deadzone; }
    bool circleConfirms() const { return circleConfirms_; }
    void setCircleConfirms(bool cc) { circleConfirms_ = cc; }
    bool micMuted() const { return micMuted_; }
    void setMicMuted(bool muted) { micMuted_ = muted; }
    float micLevel() const { return micLevel_; }
    void setMicLevel(float lvl) { micLevel_ = lvl; }
    // The block smoothing level "auto" picked for the bitrate (shown as "Auto (low)").
    void setDebandInUse(int level) { debandInUse_ = level; }
    // Circle confirms: the hints swap their buttons.
    // Update active profile values without resetting cursor position.
    void setProfileValues(int resolution, int sharpness, int deband, int upscaler,
                          int triggerStrength, int deadzone, bool circleConfirms,
                          bool hasCustomProfile);
    // The controllers in use, under the connection.
    void setPads(const PadSlots& pads) { pads_ = pads; }

    Canvas renderMenu(const StreamInfo& info) const;
    Canvas renderStats(const StreamInfo& info) const;

private:
    enum Item {
        XboxButton,
        Profile,
        Stats,
        Upscaler,
        Sharpness,
        Deband,
        Resolution,
        Triggers,
        DeadzoneItem,
        ConfirmItem,
        Leave,
        ItemCount
    };

    const Fonts& fonts_;
    bool open_ = false, stats_ = false;
    int selected_ = XboxButton;
    int resolution_ = 0, applied_ = 0;
    int sharpness_ = 0, deband_ = 1, upscaler_ = 0, debandInUse_ = 1;
    bool resolutionAsked_ = false;
    bool circleConfirms_ = false;
    bool homeConsole_ = false, allow1440_ = true;
    bool hasCustomProfile_ = false;
    int triggerStrength_ = 2;
    int deadzone_ = 15;
    bool micMuted_ = false;
    float micLevel_ = 0.0f;
    PadSlots pads_{};
};

}  // namespace xc::ui
