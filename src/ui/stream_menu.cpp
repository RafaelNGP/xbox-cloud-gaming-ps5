// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
#include "ui/stream_menu.h"

#include "ui/strings.h"

#include <algorithm>
#include <cstdio>

namespace xc::ui {

namespace {

constexpr Color kPanel = rgba(24, 24, 24);
constexpr Color kWhite = rgba(255, 255, 255);
constexpr Color kGray = rgba(190, 190, 190);
constexpr Color kDim = rgba(140, 140, 140);
constexpr Color kDark = rgba(20, 20, 20);
constexpr Color kGreen = rgba(16, 124, 16);

// Left to right on the resolution row: SettingsChoice::resolution values.
constexpr int kResolutionOrder[] = {1, 0, 2};
const char* resolutionName(int r) { return r == 1 ? "720p" : r == 2 ? tr(Str::ResBest) : "1080p"; }

std::string fmt(const char* f, double v) {
    char b[32];
    std::snprintf(b, sizeof b, f, v);
    return b;
}

}  // namespace

void StreamMenu::setProfileValues(int resolution, int sharpness, int deband, int upscaler,
                                  int triggerStrength, int deadzone, bool circleConfirms,
                                  bool hasCustomProfile) {
    resolution_ = applied_ = resolution;
    sharpness_ = sharpness;
    deband_ = deband;
    upscaler_ = upscaler;
    triggerStrength_ = triggerStrength;
    deadzone_ = deadzone;
    circleConfirms_ = circleConfirms;
    hasCustomProfile_ = hasCustomProfile;
}

void StreamMenu::open(int resolution, bool stats, int sharpness, int deband, int upscaler, bool homeConsole,
                      bool allow1440, bool hasCustomProfile, int triggerStrength, int deadzone,
                      bool circleConfirms) {
    open_ = true;
    homeConsole_ = homeConsole;
    allow1440_ = allow1440;
    stats_ = stats;
    sharpness_ = sharpness;
    deband_ = deband;
    upscaler_ = upscaler;
    hasCustomProfile_ = hasCustomProfile;
    triggerStrength_ = triggerStrength;
    deadzone_ = deadzone;
    circleConfirms_ = circleConfirms;
    selected_ = XboxButton;
    resolution_ = applied_ = resolution;
    resolutionAsked_ = false;
}

MenuAction StreamMenu::handle(const NavInput& in) {
    if (!open_) return MenuAction::None;
    if (in.back || in.options) {
        open_ = false;
        return MenuAction::Close;
    }
    if (in.up) selected_ = (selected_ + ItemCount - 1) % ItemCount;
    if (in.down) selected_ = (selected_ + 1) % ItemCount;
    if (selected_ == Profile && (in.left || in.right || in.accept)) {
        hasCustomProfile_ = !hasCustomProfile_;
        return MenuAction::ProfileToggle;
    }
    if (selected_ == Resolution && (in.left || in.right)) {
        int pos = 0;
        int n = 3;  // 720p, 1080p, Best (Auto)
        for (int i = 0; i < n; ++i)
            if (kResolutionOrder[i] == resolution_) pos = i;
        pos = in.left ? (pos + n - 1) % n : (pos + 1) % n;
        resolution_ = kResolutionOrder[pos];
    }
    if ((selected_ == Sharpness && (in.left || in.right)) || (selected_ == Sharpness && in.accept)) {
        sharpness_ = in.left ? (sharpness_ + 3) % 4 : (sharpness_ + 1) % 4;
        return MenuAction::Sharpness;
    }
    if (selected_ == Upscaler && (in.left || in.right || in.accept)) {
        // FSR, FSR + clean-up, AI, AI + clean-up (values 0, 2, 1, 3).
        static constexpr int kOrder[4] = {0, 2, 1, 3};
        int pos = 0;
        for (int i = 0; i < 4; ++i)
            if (kOrder[i] == upscaler_) pos = i;
        upscaler_ = kOrder[in.left ? (pos + 3) % 4 : (pos + 1) % 4];
        return MenuAction::Upscaler;
    }
    if (selected_ == Deband && (in.left || in.right || in.accept)) {
        // Off, low, high, auto (follows the bitrate).
        deband_ = in.left ? (deband_ + 3) % 4 : (deband_ + 1) % 4;
        return MenuAction::Deband;
    }
    if (selected_ == Triggers && (in.left || in.right || in.accept)) {
        triggerStrength_ = in.left ? (triggerStrength_ + 4) % 5 : (triggerStrength_ + 1) % 5;
        return MenuAction::Triggers;
    }
    if (selected_ == DeadzoneItem && (in.left || in.right)) {
        if (in.left && deadzone_ > 0) deadzone_ -= 5;
        else if (in.right && deadzone_ < 30) deadzone_ += 5;
        return MenuAction::Deadzone;
    }
    if (selected_ == ConfirmItem && (in.left || in.right || in.accept)) {
        circleConfirms_ = !circleConfirms_;
        return MenuAction::ConfirmButton;
    }
    if (selected_ == Stats && (in.left || in.right)) {
        stats_ = !stats_;
        return MenuAction::Stats;
    }
    if (!in.accept) return MenuAction::None;
    switch (selected_) {
    case XboxButton: open_ = false; return MenuAction::XboxButton;
    case Stats: stats_ = !stats_; return MenuAction::Stats;
    case Resolution:
        applied_ = resolution_;
        resolutionAsked_ = true;
        return MenuAction::Resolution;
    case Leave: open_ = false; return MenuAction::Leave;
    default: return MenuAction::None;
    }
}

Canvas StreamMenu::renderMenu(const StreamInfo& info) const {
    Canvas c(kMenuW, kMenuH);
    c.clear(kPanel);
    c.fillRect({0, 0, kMenuW, 6}, kGreen);
    constexpr int kPad = 40;
    fonts_.bold.draw(c, tr(Str::MenuTitle), kPad, 38, 32, kWhite);

    // The items: the selected one white, like the Xbox guide.
    constexpr int kRowH = 46, kTop = 96, kPx = 22;
    for (int i = 0; i < ItemCount; ++i) {
        Rect row{kPad - 16, kTop + i * kRowH, kMenuW - 2 * (kPad - 16), kRowH - 6};
        bool sel = i == selected_;
        if (sel) c.fillRect(row, kWhite, 8);
        Color fg = sel ? kDark : kWhite;
        int ty = fonts_.semibold.centeredY(row.y, row.h, kPx);
        const char* label = i == XboxButton     ? tr(Str::MenuXboxButton)
                            : i == Profile      ? tr(Str::Profile)
                            : i == Stats        ? tr(Str::MenuStats)
                            : i == Sharpness    ? tr(Str::MenuSharpness)
                            : i == Deband       ? tr(Str::MenuDeband)
                            : i == Upscaler     ? tr(Str::MenuUpscaler)
                            : i == Resolution   ? tr(Str::MenuResolution)
                            : i == Triggers     ? tr(Str::TriggerRumble)
                            : i == DeadzoneItem ? tr(Str::Deadzone)
                            : i == ConfirmItem  ? tr(Str::ConfirmButton)
                            : homeConsole_      ? tr(Str::MenuEndStream)
                                                : tr(Str::MenuLeave);
        fonts_.semibold.draw(c, label, row.x + 16, ty, kPx, fg);
        std::string value;
        if (i == Profile) {
            value = tr(hasCustomProfile_ ? Str::ProfileCustom : Str::ProfileDefault);
            if (sel) value = "< " + value + " >";
        }
        if (i == Stats) value = tr(stats_ ? Str::On : Str::Off);
        if (i == Sharpness) {
            static constexpr Str kLevels[] = {Str::SharpOff, Str::SharpLow, Str::SharpMedium, Str::SharpHigh};
            value = tr(kLevels[sharpness_]);
            if (sel) value = "< " + value + " >";
        }
        if (i == Upscaler) {
            value = upscaler_ == 3   ? tr(Str::UpscalerAiClean)
                    : upscaler_ == 2 ? tr(Str::UpscalerFsrClean)
                    : upscaler_ == 1 ? tr(Str::UpscalerAi)
                                     : "FSR";
            if (sel) value = "< " + value + " >";
        }
        if (i == Deband) {
            static constexpr Str kLevels[] = {Str::SharpOff, Str::SharpLow, Str::SharpHigh};
            value = deband_ == 3 ? trf(Str::DebandAuto, tr(kLevels[std::clamp(debandInUse_, 0, 2)]))
                                 : tr(kLevels[std::clamp(deband_, 0, 2)]);
            if (sel) value = "< " + value + " >";
        }
        if (i == Resolution) {
            value = resolutionName(resolution_);
            if (sel) value = "< " + value + " >";
        }
        if (i == Triggers) {
            static constexpr Str kStrengths[] = {Str::Deactivated, Str::TriggerLight, Str::TriggerMedium,
                                                 Str::TriggerStrong, Str::TriggerMax};
            value = tr(kStrengths[std::clamp(triggerStrength_, 0, 4)]);
            if (sel) value = "< " + value + " >";
        }
        if (i == DeadzoneItem) {
            value = std::to_string(deadzone_) + " %";
            if (sel) value = "< " + value + " >";
        }
        if (i == ConfirmItem) {
            value = circleConfirms_ ? "○" : "×";
            if (sel) value = "< " + value + " >";
        }
        if (!value.empty()) {
            int w = fonts_.semibold.measure(value, kPx);
            fonts_.semibold.draw(c, value, row.x + row.w - 16 - w, ty, kPx, sel ? kDark : kGray);
        }
    }
    int y = kTop + ItemCount * kRowH + 6;
    if (resolutionAsked_) {
        fonts_.regular.draw(c, tr(Str::ResolutionNote), kPad, y, 18, kDim);
        y += 26;
    }
    y += 10;

    // The connection.
    c.fillRect({kPad, y, kMenuW - 2 * kPad, 2}, rgba(60, 60, 60));
    y += 18;
    auto line = [&](Str label, const std::string& value) {
        fonts_.regular.draw(c, tr(label), kPad, y, 20, kGray);
        int w = fonts_.semibold.measure(value, 20);
        fonts_.semibold.draw(c, value, kMenuW - kPad - w, y, 20, kWhite);
        y += 30;
    };
    const std::string none = "-";
    line(Str::StatRegion, info.region.empty() ? none : info.region);
    line(Str::StatLatency, info.rttMs > 0 ? std::to_string(info.rttMs) + " ms" : none);
    line(Str::StatBitrate, info.mbps > 0 ? fmt("%.1f Mbps", info.mbps) : none);
    std::string frames = info.fps > 0 ? fmt("%.0f fps", info.fps) : none;
    if (info.width > 0) frames = std::to_string(info.width) + "x" + std::to_string(info.height) + "  " + frames;
    line(Str::StatFrameRate, frames);
    line(Str::StatLoss, fmt("%.1f %%", info.lossPct));
    line(Str::StatDecode, info.decodeMs > 0 ? fmt("%.1f ms", info.decodeMs) : none);
    line(Str::StatOnScreen, info.onScreenMs > 0 ? fmt("%.1f ms", info.onScreenMs) : none);

    // The controllers: numbered pads, each connected one with its user.
    y += 8;
    fonts_.regular.draw(c, tr(Str::Controllers), kPad, y, 22, kGray);
    y += 38;
    const int slotW = (kMenuW - 2 * kPad) / static_cast<int>(pads_.size());
    for (int i = 0; i < static_cast<int>(pads_.size()); ++i) {
        int sx = kPad + i * slotW;
        constexpr int kIconW = 60;
        drawPadIcon(c, fonts_.bold, sx + (slotW - kIconW) / 2, y, kIconW, i, pads_[i].connected, kPanel);
        if (!pads_[i].connected || pads_[i].name.empty()) continue;
        auto name = fonts_.regular.wrap(pads_[i].name, 16, slotW - 8, 1);
        if (!name.empty())
            fonts_.regular.draw(c, name[0], sx + (slotW - fonts_.regular.measure(name[0], 16)) / 2,
                                y + padIconHeight(kIconW) + 4, 16, kGray);
    }

    // Cross selects, Circle goes back to the game (swapped with Circle
    // confirming).
    int hy = kMenuH - 56, x = kPad;
    {
        // The touchpad's gestures, small, above the button hints.
        auto lines = fonts_.regular.wrap(tr(Str::MenuGestureHint), 18, kMenuW - 2 * kPad, 2);
        int gy = hy - 20 - static_cast<int>(lines.size()) * 26;
        for (const auto& l : lines) {
            fonts_.regular.draw(c, l, kPad, gy, 18, kDim);
            gy += 26;
        }
    }
    auto icon = [&](bool cross) {
        c.fillCircle(x + 14, hy + 13, 14, rgba(255, 255, 255, 40));
        if (cross) {
            c.line(x + 8, hy + 7, x + 20, hy + 19, 2.5f, rgba(124, 178, 232));
            c.line(x + 20, hy + 7, x + 8, hy + 19, 2.5f, rgba(124, 178, 232));
        } else {
            c.strokeArc(x + 14, hy + 13, 6.5f, 2.5f, 0, 6.2832f, rgba(255, 102, 102));
        }
    };
    icon(!circleConfirms_);
    x = fonts_.semibold.draw(c, tr(Str::Select), x + 38, hy, 22, kGray) + 36;
    icon(circleConfirms_);
    fonts_.semibold.draw(c, tr(Str::Back), x + 38, hy, 22, kGray);
    return c;
}

Canvas StreamMenu::renderStats(const StreamInfo& info) const {
    std::string text;
    if (info.height > 0) text += std::to_string(info.height) + "p  ";
    text += fmt("%.0f fps", info.fps) + "  " + fmt("%.1f Mbps", info.mbps);
    if (info.rttMs > 0) text += "  " + std::to_string(info.rttMs) + " ms";
    text += "  " + fmt("%.1f%%", info.lossPct);
    constexpr int kPx = 20, kH = 36;
    int w = fonts_.semibold.measure(text, kPx) + 28;
    Canvas c(w, kH);
    c.clear(kPanel);
    fonts_.semibold.draw(c, text, 14, fonts_.semibold.centeredY(0, kH, kPx), kPx, kWhite);
    return c;
}

}  // namespace xc::ui
