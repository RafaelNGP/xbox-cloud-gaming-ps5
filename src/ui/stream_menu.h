// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
// What is drawn over a running game: the in-game menu (OPTIONS + TOUCHPAD)
// and the statistics line. Each is drawn into its own small canvas, which
// display::setOverlay() lays over the video.
#pragma once

#include "ui/app_ui.h"
#include "ui/canvas.h"
#include "ui/font.h"

#include <string>

namespace xc::ui {

// The stream's numbers over the last second.
struct StreamInfo {
    std::string region;
    int rttMs = -1;
    double fps = 0, mbps = 0, lossPct = 0, decodeMs = 0;
    int width = 0, height = 0;  // of the decoded picture
};

enum class MenuAction { None, Close, Leave, Refresh, Resolution, Stats, Sharpness };

class StreamMenu {
public:
    static constexpr int kMenuW = 620, kMenuH = 840;
    static constexpr int kMenuX = 80, kMenuY = (1080 - kMenuH) / 2;
    static constexpr int kStatsX = 32, kStatsY = 28;

    explicit StreamMenu(const Fonts& fonts) : fonts_(fonts) {}

    // `resolution` as SettingsChoice::resolution (0 = 1080p, 1 = 720p, 2 = 1440p).
    // `sharpness` 0 = off, 1..3 = low, medium, high.
    void open(int resolution, bool stats, int sharpness = 0);
    void close() { open_ = false; }
    bool isOpen() const { return open_; }
    MenuAction handle(const NavInput& in);
    int resolution() const { return resolution_; }
    bool statsOn() const { return stats_; }
    int sharpness() const { return sharpness_; }

    Canvas renderMenu(const StreamInfo& info) const;
    Canvas renderStats(const StreamInfo& info) const;

private:
    enum Item { Resume, Stats, Sharpness, Resolution, Refresh, Leave, ItemCount };

    const Fonts& fonts_;
    bool open_ = false, stats_ = false;
    int selected_ = Resume;
    int resolution_ = 0, applied_ = 0;
    int sharpness_ = 0;
    bool resolutionAsked_ = false;
};

}  // namespace xc::ui
