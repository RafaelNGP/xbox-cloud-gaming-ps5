// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
#include "ui/pad_icons.h"

namespace xc::ui {

Color playerColor(int index) {
    switch (index) {
    case 0: return rgba(0, 112, 220);
    case 1: return rgba(220, 50, 50);
    case 2: return rgba(40, 170, 80);
    default: return rgba(225, 90, 175);
    }
}

void drawPadIcon(Canvas& c, const Font& font, int x, int y, int w, int index, bool connected) {
    int h = w * 2 / 3;
    // Opaque even when dim: overlapping translucent parts would show seams.
    Color body = connected ? playerColor(index) : rgba(52, 52, 52);
    // A rounded body with two grips hanging from its lower corners.
    int bodyH = h * 3 / 4;
    float gripR = w * 0.19f;
    c.fillCircle(x + w * 0.22f, y + h - gripR, gripR, body);
    c.fillCircle(x + w * 0.78f, y + h - gripR, gripR, body);
    c.fillRect({x, y, w, bodyH}, body, bodyH / 2);
    std::string n = std::to_string(index + 1);
    int px = bodyH * 9 / 10;
    font.draw(c, n, x + (w - font.measure(n, px)) / 2, font.centeredY(y, bodyH, px), px,
              connected ? rgba(255, 255, 255) : rgba(120, 120, 120));
}

int drawPadRow(Canvas& c, const Font& font, int x, int y, int w, int gap, const PadSlots& pads) {
    for (int i = 0; i < static_cast<int>(pads.size()); ++i) drawPadIcon(c, font, x + i * (w + gap), y, w, i, pads[i].connected);
    return static_cast<int>(pads.size()) * (w + gap) - gap;
}

}  // namespace xc::ui
