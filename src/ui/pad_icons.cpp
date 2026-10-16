// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
#include "ui/pad_icons.h"

#include <algorithm>
#include <string>
#include <utility>

namespace xc::ui {

Color playerColor(int index) {
    switch (index) {
    case 0: return rgba(0, 112, 220);
    case 1: return rgba(220, 50, 50);
    case 2: return rgba(40, 170, 80);
    default: return rgba(225, 90, 175);
    }
}

void drawKeyboardGlyph(Canvas& c, int x, int y, int w, Color color) {
    int h = w * 10 / 24, t = std::max(2, w / 14), r = std::max(2, w / 10);
    c.strokeRect({x, y, w, h}, color, t, r);
    int key = std::max(2, w / 12), gap = (w - 2 * t - 2 * key) / 6;
    for (int row = 0; row < 2; ++row)  // two rows of six keys
        for (int k = 0; k < 6; ++k)
            c.fillRect({x + t + key + k * gap, y + t + key / 2 + row * (key + key / 2 + 1), std::max(1, gap - key / 2), key},
                       color, 1);
    c.fillRect({x + w / 4, y + h - t - key - key / 2, w / 2, key}, color, 1);  // the space bar
}

int padIconHeight(int w) { return w * 7 / 10 + 4 + w * 3 / 10; }

void drawPadIcon(Canvas& c, const Font& font, int x, int y, int w, int index, bool connected, Color background) {
    const float W = static_cast<float>(w), H = W * 0.7f, X = static_cast<float>(x), Y = static_cast<float>(y);
    auto px = [&](float f) { return X + f * W; };
    auto py = [&](float f) { return Y + f * H; };
    // Opaque even when dim: overlapping translucent parts would show seams.
    Color body = connected ? playerColor(index) : rgba(70, 70, 70);
    // A DualSense from the front: the body, its two grips, and the controls
    // cut out of it in the background's colour.
    c.fillRect({static_cast<int>(px(0.05f)), static_cast<int>(py(0.04f)), static_cast<int>(W * 0.90f),
                static_cast<int>(H * 0.56f)},
               body, static_cast<int>(H * 0.24f));
    c.fillRect({static_cast<int>(px(0.24f)), static_cast<int>(py(0.36f)), static_cast<int>(W * 0.52f),
                static_cast<int>(H * 0.36f)},
               body, static_cast<int>(H * 0.14f));
    for (int side = 0; side < 2; ++side) {
        float x0 = side ? 0.74f : 0.26f, x1 = side ? 0.86f : 0.14f;
        for (int k = 0; k <= 10; ++k) {  // a capsule, out and down
            float t = k / 10.0f;
            c.fillCircle(px(x0 + (x1 - x0) * t), py(0.42f + 0.40f * t), W * (0.13f - 0.015f * t), body);
        }
    }
    c.fillRect({static_cast<int>(px(0.355f)), static_cast<int>(py(0.12f)), static_cast<int>(W * 0.29f),
                static_cast<int>(H * 0.22f)},
               background, static_cast<int>(W * 0.035f));  // touchpad
    float dx = px(0.19f), dy = py(0.30f), arm = W * 0.075f, bar = W * 0.045f;  // D-pad
    c.fillRect({static_cast<int>(dx - arm), static_cast<int>(dy - bar / 2), static_cast<int>(2 * arm), static_cast<int>(bar)},
               background, 1);
    c.fillRect({static_cast<int>(dx - bar / 2), static_cast<int>(dy - arm), static_cast<int>(bar), static_cast<int>(2 * arm)},
               background, 1);
    float bx = px(0.81f), by = py(0.30f), off = W * 0.06f;  // the four buttons
    for (auto [ox, oy] : {std::pair{0.0f, -1.0f}, {0.0f, 1.0f}, {-1.0f, 0.0f}, {1.0f, 0.0f}})
        c.fillCircle(bx + ox * off, by + oy * off, W * 0.027f, background);
    for (float sx : {0.37f, 0.63f}) {  // the sticks
        c.fillCircle(px(sx), py(0.58f), W * 0.085f, background);
        c.fillCircle(px(sx), py(0.58f), W * 0.05f, body);
    }
    c.fillCircle(px(0.5f), py(0.58f), W * 0.02f, background);  // PS button

    std::string n = std::to_string(index + 1);
    int npx = w * 3 / 10;
    font.draw(c, n, x + (w - font.measure(n, npx)) / 2, y + static_cast<int>(H) + 4, npx,
              connected ? rgba(255, 255, 255) : rgba(110, 110, 110));
}

void drawSlotIcon(Canvas& c, const Font& font, int x, int y, int w, int index, const PadSlot& slot, Color background,
                  const Font* icons) {
    if (!slot.keyboard && !icons) {
        drawPadIcon(c, font, x, y, w, index, slot.connected, background);
        return;
    }
    // The console's own controller or keyboard-and-mouse icon (else a drawn
    // keyboard), in the player's colour (dim when free), numbered below.
    bool on = slot.connected || slot.keyboard;
    Color col = on ? playerColor(index) : rgba(70, 70, 70);
    if (icons) {
        const char* glyph = slot.keyboard ? kIconKeyboardMouse : kIconGamepad;
        int px2 = w * 9 / 10;  // as wide as a pad, centred on its height
        icons->draw(c, glyph, x + (w - icons->measure(glyph, px2)) / 2, y - w / 10, px2, col);
    } else {
        int kh = w * 10 / 24;
        int top = y + (w * 7 / 10 - kh) / 2;
        drawKeyboardGlyph(c, x, top, w * 3 / 4, col);
        c.fillRect({x + w * 3 / 4 + w / 16, top + kh / 6, w / 6, kh * 5 / 6}, col, w / 12);  // the mouse
    }
    std::string n = std::to_string(index + 1);
    int npx = w * 3 / 10;
    font.draw(c, n, x + (w - font.measure(n, npx)) / 2, y + w * 7 / 10 + 4, npx,
              on ? rgba(255, 255, 255) : rgba(110, 110, 110));
}

int drawPadRow(Canvas& c, const Font& font, int x, int y, int w, int gap, const PadSlots& pads, Color background,
               const Font* icons) {
    for (int i = 0; i < static_cast<int>(pads.size()); ++i)
        drawSlotIcon(c, font, x + i * (w + gap), y, w, i, pads[i], background, icons);
    return static_cast<int>(pads.size()) * (w + gap) - gap;
}

}  // namespace xc::ui
