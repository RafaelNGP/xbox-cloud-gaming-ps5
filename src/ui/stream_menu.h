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
    bool isHwDecode = false;
    int width = 0, height = 0;  // of the decoded picture
    double qp = -1;             // the pictures' average quantiser (lower: finer); -1 unknown
    bool degraded = false;      // frames lost / video stalled / round trip far up, in the last seconds
    int playtimeHours = -1;     // cloud playtime remaining (-1 unknown/uncapped)
    uint64_t playtimeSeconds = 0; // total playtime for this title in seconds
    std::vector<float> frametimes;  // presentation frametimes (ms)
};

enum StatsIndicator : uint32_t {
    StatsFps       = 1 << 0,  // "1080p 60 fps"
    StatsBitrate   = 1 << 1,  // "12.5 Mbps"
    StatsLatency   = 1 << 2,  // "18 ms"
    StatsLoss      = 1 << 3,  // "0.0%"
    StatsGraph     = 1 << 4,  // Frametime sparkline histogram
    StatsDelay     = 1 << 5,  // "Screen 24.0 ms  Dec 5.3 ms"
    StatsQp        = 1 << 6,  // "QP 29"
    StatsMic       = 1 << 7,  // "MIC"
    StatsDefaultMask = StatsFps | StatsBitrate | StatsLatency | StatsLoss | StatsGraph,
};

// Close: the menu went away (Circle, OPTIONS, or the Xbox button): a fresh key frame is asked for.
enum class MenuAction {
    None, Close, Leave, Resolution, Stats, Sharpness, Deband, Upscaler, XboxButton,
    ProfileToggle, Triggers, Deadzone, ConfirmButton, MicToggle, Gyro, PadSpeakerVolume,
    TouchpadShortcuts
};

class StreamMenu {
public:
    static constexpr int kMenuW = 620, kMenuH = 1064;
    static constexpr int kMenuX = 80, kMenuY = (1080 - kMenuH) / 2;
    static constexpr int kStatsX = 32, kStatsY = 28;
    // The connection warning, alone (the statistics line off), top right.
    static constexpr int kWarnRight = 1888, kWarnY = 28;

    explicit StreamMenu(const Fonts& fonts) : fonts_(fonts) {}

    // `resolution` as SettingsChoice::resolution (0 = 1080p, 1 = 720p, 2 = 1440p).
    // `sharpness` 0 = off, 1..3 = low, medium, high; `deband` 0..2 = off,
    // low, high.
    // `upscaler` 0 = FSR, 1 = Anime4K.
    // `homeConsole`: the user's own Xbox ("End the stream" instead of "Leave the game").
    // `allow1440`: offered only once a stream delivered it.
    void open(int resolution, bool stats, int sharpness = 0, int deband = 1, int upscaler = 0, bool homeConsole = false,
              bool allow1440 = true, bool hasCustomProfile = false, int triggerStrength = 2, int deadzone = 15,
              bool circleConfirms = false, float micLevel = 0.0f, bool micMuted = false, int padSpeakerVolume = 100);
    void close() { open_ = false; statsCustomizing_ = false; touchpadCustomizing_ = false; }
    bool isOpen() const { return open_; }
    MenuAction handle(const NavInput& in);
    int resolution() const { return resolution_; }
    bool statsOn() const { return stats_; }
    void setStatsOn(bool s) { stats_ = s; }
    uint32_t statsMask() const { return statsMask_; }
    void setStatsMask(uint32_t mask) { statsMask_ = mask; }
    bool isCustomizingStats() const { return statsCustomizing_; }
    bool isCustomizingTouchpad() const { return touchpadCustomizing_; }
    const input::TouchpadShortcuts& touchpadShortcuts() const { return touchpadShortcuts_; }
    void setTouchpadShortcuts(const input::TouchpadShortcuts& ts) { touchpadShortcuts_ = ts; }
    void setFrametimes(const std::vector<float>& ft) const { frametimes_ = ft; }
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
    int padSpeakerVolume() const { return padSpeakerVolume_; }
    void setPadSpeakerVolume(int vol) { padSpeakerVolume_ = vol; }
    // Gyro aiming: mode (input::GyroMode) and intensity 1..input::kGyroLevels.
    int gyroMode() const { return gyroMode_; }
    int gyroLevel() const { return gyroLevel_; }
    void setGyro(int mode, int level) { gyroMode_ = mode, gyroLevel_ = level; }
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
    // What the display and the pads can do (UiCaps, all by default as on
    // the PS5): the upscaler, sharpness and block smoothing need the
    // display's; trigger vibration and gyro aiming a pad with them; the
    // footer names the buttons when no pad has a touchpad.
    void setCapabilities(const UiCaps& caps);
    const UiCaps& capabilities() const { return caps_; }

    Canvas renderMenu(const StreamInfo& info) const;
    Canvas renderStats(const StreamInfo& info) const;
    void recordFrametime(float ms) const;
    // "Unstable connection", on its own (when the statistics line is off).
    Canvas renderWarning() const;

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
        GyroItem,
        GyroLevelItem,
        PadSpeakerVolumeItem,
        TouchpadShortcutsItem,
        Leave,
        ItemCount
    };

    // Whether `item` is in the menu with caps_.
    bool itemShown(int item) const;

    const Fonts& fonts_;
    UiCaps caps_;
    bool open_ = false, stats_ = false;
    uint32_t statsMask_ = StatsDefaultMask;
    bool statsCustomizing_ = false;
    int statsCustomIndex_ = 0;
    bool touchpadCustomizing_ = false;
    int touchpadCustomIndex_ = 0;
    input::TouchpadShortcuts touchpadShortcuts_ = input::defaultTouchpadShortcuts();
    int selected_ = XboxButton;
    int resolution_ = 0, applied_ = 0;
    int sharpness_ = 0, deband_ = 1, upscaler_ = 0, debandInUse_ = 1;
    bool resolutionAsked_ = false;
    bool circleConfirms_ = false;
    bool homeConsole_ = false, allow1440_ = true;
    bool hasCustomProfile_ = false;
    int triggerStrength_ = 2;
    int deadzone_ = 15;
    int gyroMode_ = 0, gyroLevel_ = 5;
    bool micMuted_ = false;
    float micLevel_ = 0.0f;
    int padSpeakerVolume_ = 100;
    PadSlots pads_{};
    mutable std::vector<float> frametimes_;
};

}  // namespace xc::ui
