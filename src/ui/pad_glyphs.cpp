// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
#include "ui/pad_glyphs.h"

#include "ui/font.h"

#include <algorithm>

namespace xc::ui {

GlyphStyle glyphStyleFor(input::PadKind kind) {
    switch (kind) {
    case input::PadKind::Xbox:
    case input::PadKind::Switch:
    case input::PadKind::Generic: return GlyphStyle::Xbox;
    default: return GlyphStyle::PlayStation;
    }
}

const char* buttonName(GlyphStyle style, PadButton b) {
    static const char* const kPs[] = {"Cross", "Circle", "Square", "Triangle", "Options", "Touchpad",
                                      "L1",    "R1",     "L2",     "R2",       "L3",      "R3"};
    static const char* const kXbox[] = {"A", "B", "X", "Y", "Menu", "View", "LB", "RB", "LT", "RT", "LS", "RS"};
    int i = static_cast<int>(b);
    if (i < 0 || i > static_cast<int>(PadButton::R3)) return "";
    return style == GlyphStyle::Xbox ? kXbox[i] : kPs[i];
}

Color faceColor(GlyphStyle style, PadButton b) {
    if (style == GlyphStyle::Xbox) {
        switch (b) {
        case PadButton::Cross: return rgba(84, 178, 58);      // A
        case PadButton::Circle: return rgba(222, 62, 54);     // B
        case PadButton::Square: return rgba(48, 120, 222);    // X
        case PadButton::Triangle: return rgba(236, 184, 30);  // Y
        default: return 0;
        }
    }
    switch (b) {
    case PadButton::Cross: return rgba(124, 178, 232);
    case PadButton::Circle: return rgba(255, 102, 102);
    case PadButton::Square: return rgba(240, 130, 200);
    case PadButton::Triangle: return rgba(64, 226, 160);
    default: return 0;
    }
}

void drawFaceButton(Canvas& c, const Font& letters, GlyphStyle style, PadButton b, float cx, float cy, float r) {
    const float k = r / 14.0f, t = 2.5f * k;
    const Color col = faceColor(style, b);
    if (style == GlyphStyle::Xbox) {
        c.fillCircle(cx, cy, r, col);
        const char* name = buttonName(style, b);
        int px = std::max(10, static_cast<int>(r * 1.25f));
        int top = static_cast<int>(cy - r), h = static_cast<int>(2 * r);
        Color ink = b == PadButton::Triangle ? rgba(30, 30, 30) : rgba(255, 255, 255);
        letters.draw(c, name, static_cast<int>(cx) - letters.measure(name, px) / 2, letters.centeredY(top, h, px), px, ink);
        return;
    }
    c.fillCircle(cx, cy, r, rgba(255, 255, 255, 40));
    if (b == PadButton::Cross) {
        c.line(cx - 6 * k, cy - 6 * k, cx + 6 * k, cy + 6 * k, t, col);
        c.line(cx + 6 * k, cy - 6 * k, cx - 6 * k, cy + 6 * k, t, col);
    } else if (b == PadButton::Circle) {
        c.strokeArc(cx, cy, 6.5f * k, t, 0, 6.2832f, col);
    } else if (b == PadButton::Square) {
        int h = static_cast<int>(6 * k);
        c.strokeRect({static_cast<int>(cx) - h, static_cast<int>(cy) - h, 2 * h, 2 * h}, col, std::max(2, static_cast<int>(2 * k)));
    } else {
        c.line(cx, cy - 7 * k, cx - 7 * k, cy + 5 * k, t, col);
        c.line(cx, cy - 7 * k, cx + 7 * k, cy + 5 * k, t, col);
        c.line(cx - 7 * k, cy + 5 * k, cx + 7 * k, cy + 5 * k, t, col);
    }
}

void drawXboxMenuButton(Canvas& c, float cx, float cy, float r, Color color) {
    const float k = r / 14.0f;
    c.fillCircle(cx, cy, r, rgba(255, 255, 255, 40));
    for (int i = -1; i <= 1; ++i) c.line(cx - 6 * k, cy + i * 4 * k, cx + 6 * k, cy + i * 4 * k, 2 * k, color);
}

void drawXboxViewButton(Canvas& c, float cx, float cy, float r, Color color) {
    const float k = r / 14.0f;
    c.fillCircle(cx, cy, r, rgba(255, 255, 255, 40));
    const int s = static_cast<int>(8 * k), th = std::max(2, static_cast<int>(2 * k));
    c.strokeRect({static_cast<int>(cx - 6 * k), static_cast<int>(cy - 6 * k), s, s}, color, th, 1);
    c.fillRect({static_cast<int>(cx - 2 * k), static_cast<int>(cy - 2 * k), s, s}, color, 1);
}

}  // namespace xc::ui
