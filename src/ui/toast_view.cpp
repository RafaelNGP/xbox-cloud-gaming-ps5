// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
// The in-app notices' look (ui/toast_view.h).
#include "ui/toast_view.h"

#include <algorithm>

namespace xc::ui {

namespace {

constexpr int kPx = 24, kLineH = 34, kPadX = 28, kPadY = 16, kMaxTextW = 640, kGap = 12;
constexpr Color kPanel = rgba(40, 40, 40, 240);
constexpr Color kAccent = rgba(16, 124, 16);  // the app's green, down the left edge
constexpr Color kText = rgba(255, 255, 255);

std::vector<std::string> lines(const Fonts& fonts, const std::string& text) {
    return fonts.semibold.wrap(text, kPx, kMaxTextW, 3);
}

int widest(const Fonts& fonts, const std::vector<std::string>& ls) {
    int w = 0;
    for (const auto& l : ls) w = std::max(w, fonts.semibold.measure(l, kPx));
    return w;
}

}  // namespace

void drawToasts(Canvas& c, const Fonts& fonts, const std::vector<std::string>& texts, int right, int top) {
    int y = top;
    for (const auto& text : texts) {
        auto ls = lines(fonts, text);
        if (ls.empty()) continue;
        int w = widest(fonts, ls) + 2 * kPadX, h = static_cast<int>(ls.size()) * kLineH + 2 * kPadY;
        Rect r{right - w, y, w, h};
        c.fillRect({r.x + 4, r.y + 6, r.w, r.h}, rgba(0, 0, 0, 110), 18);  // shadow
        c.fillRect(r, kPanel, 18);
        c.fillRect({r.x, r.y + 14, 5, r.h - 28}, kAccent, 2);
        int ty = r.y + kPadY;
        for (const auto& l : ls) {
            fonts.semibold.draw(c, l, r.x + kPadX, ty, kPx, kText);
            ty += kLineH;
        }
        y += h + kGap;
    }
}

Canvas renderToastPanel(const Fonts& fonts, const std::vector<std::string>& texts) {
    // One opaque panel (an overlay is a single rectangle): the notices one
    // under the other, a thin line between them.
    std::vector<std::vector<std::string>> all;
    int w = 0, h = 2 * kPadY;
    for (const auto& text : texts) {
        auto ls = lines(fonts, text);
        if (ls.empty()) continue;
        w = std::max(w, widest(fonts, ls));
        h += static_cast<int>(ls.size()) * kLineH + (all.empty() ? 0 : kGap);
        all.push_back(std::move(ls));
    }
    Canvas c(std::max(1, w + 2 * kPadX), std::max(1, h));
    c.clear(rgba(30, 30, 30));
    c.fillRect({0, 0, 5, c.height()}, kAccent);
    int y = kPadY;
    for (size_t i = 0; i < all.size(); ++i) {
        if (i) {
            c.fillRect({kPadX, y + kGap / 2 - 1, c.width() - 2 * kPadX, 1}, rgba(255, 255, 255, 40));
            y += kGap;
        }
        for (const auto& l : all[i]) {
            fonts.semibold.draw(c, l, kPadX, y, kPx, kText);
            y += kLineH;
        }
    }
    return c;
}

}  // namespace xc::ui
