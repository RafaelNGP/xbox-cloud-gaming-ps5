// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
#include "ui/stream_menu.h"

#include "ui/strings.h"

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
const char* resolutionName(int r) { return r == 1 ? "720p" : r == 2 ? "1440p" : "1080p"; }

std::string fmt(const char* f, double v) {
    char b[32];
    std::snprintf(b, sizeof b, f, v);
    return b;
}

}  // namespace

void StreamMenu::open(int resolution, bool stats, int sharpness) {
    open_ = true;
    stats_ = stats;
    sharpness_ = sharpness;
    selected_ = Resume;
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
    if (selected_ == Resolution && (in.left || in.right)) {
        int pos = 0;
        for (int i = 0; i < 3; ++i)
            if (kResolutionOrder[i] == resolution_) pos = i;
        pos = in.left ? (pos + 2) % 3 : (pos + 1) % 3;
        resolution_ = kResolutionOrder[pos];
    }
    if ((selected_ == Sharpness && (in.left || in.right)) || (selected_ == Sharpness && in.accept)) {
        sharpness_ = in.left ? (sharpness_ + 3) % 4 : (sharpness_ + 1) % 4;
        return MenuAction::Sharpness;
    }
    if (selected_ == Stats && (in.left || in.right)) {
        stats_ = !stats_;
        return MenuAction::Stats;
    }
    if (!in.accept) return MenuAction::None;
    switch (selected_) {
    case Resume: open_ = false; return MenuAction::Close;
    case Stats: stats_ = !stats_; return MenuAction::Stats;
    case Resolution:
        applied_ = resolution_;
        resolutionAsked_ = true;
        return MenuAction::Resolution;
    case Refresh: return MenuAction::Refresh;
    case Leave: open_ = false; return MenuAction::Leave;
    default: return MenuAction::None;
    }
}

Canvas StreamMenu::renderMenu(const StreamInfo& info) const {
    Canvas c(kMenuW, kMenuH);
    c.clear(kPanel);
    c.fillRect({0, 0, kMenuW, 6}, kGreen);
    constexpr int kPad = 40;
    fonts_.bold.draw(c, tr(Str::MenuTitle), kPad, 40, 34, kWhite);

    // The items: the selected one white, like the Xbox guide.
    constexpr int kRowH = 60, kTop = 110, kPx = 24;
    for (int i = 0; i < ItemCount; ++i) {
        Rect row{kPad - 16, kTop + i * kRowH, kMenuW - 2 * (kPad - 16), kRowH - 8};
        bool sel = i == selected_;
        if (sel) c.fillRect(row, kWhite, 8);
        Color fg = sel ? kDark : kWhite;
        int ty = fonts_.semibold.centeredY(row.y, row.h, kPx);
        const char* label = i == Resume       ? tr(Str::MenuResume)
                            : i == Stats      ? tr(Str::MenuStats)
                            : i == Sharpness  ? tr(Str::MenuSharpness)
                            : i == Resolution ? tr(Str::MenuResolution)
                            : i == Refresh    ? tr(Str::MenuRefresh)
                                              : tr(Str::MenuLeave);
        fonts_.semibold.draw(c, label, row.x + 16, ty, kPx, fg);
        std::string value;
        if (i == Stats) value = tr(stats_ ? Str::On : Str::Off);
        if (i == Sharpness) {
            static constexpr Str kLevels[] = {Str::SharpOff, Str::SharpLow, Str::SharpMedium, Str::SharpHigh};
            value = tr(kLevels[sharpness_]);
            if (sel) value = "< " + value + " >";
        }
        if (i == Resolution) {
            value = resolutionName(resolution_);
            if (sel) value = "< " + value + " >";
        }
        if (!value.empty()) {
            int w = fonts_.semibold.measure(value, kPx);
            fonts_.semibold.draw(c, value, row.x + row.w - 16 - w, ty, kPx, sel ? kDark : kGray);
        }
    }
    int y = kTop + ItemCount * kRowH;
    if (resolutionAsked_) fonts_.regular.draw(c, tr(Str::ResolutionNote), kPad, y, 18, kDim);
    y += 40;

    // The connection.
    c.fillRect({kPad, y, kMenuW - 2 * kPad, 2}, rgba(60, 60, 60));
    y += 22;
    auto line = [&](Str label, const std::string& value) {
        fonts_.regular.draw(c, tr(label), kPad, y, 22, kGray);
        int w = fonts_.semibold.measure(value, 22);
        fonts_.semibold.draw(c, value, kMenuW - kPad - w, y, 22, kWhite);
        y += 36;
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

    // Cross selects, Circle goes back to the game.
    int hy = kMenuH - 56, x = kPad;
    c.fillCircle(x + 14, hy + 13, 14, rgba(255, 255, 255, 40));
    c.line(x + 8, hy + 7, x + 20, hy + 19, 2.5f, rgba(124, 178, 232));
    c.line(x + 20, hy + 7, x + 8, hy + 19, 2.5f, rgba(124, 178, 232));
    x = fonts_.semibold.draw(c, tr(Str::Select), x + 38, hy, 22, kGray) + 36;
    c.fillCircle(x + 14, hy + 13, 14, rgba(255, 255, 255, 40));
    c.strokeArc(x + 14, hy + 13, 6.5f, 2.5f, 0, 6.2832f, rgba(255, 102, 102));
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
