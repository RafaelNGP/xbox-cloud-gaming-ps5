// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 PSBox Cloud Gaming contributors
#include "ui/brand.h"

#include <cmath>
#include <vector>

namespace xc::ui {

namespace {

// A quadratic Bezier stroke stamped with discs: thin in the middle, wide at
// both ends, like a brush swoosh.
void swoosh(Canvas& c, float x0, float y0, float cx, float cy, float x1, float y1, float thin, float wide,
            Color color) {
    constexpr int kSteps = 160;
    for (int i = 0; i <= kSteps; ++i) {
        float t = static_cast<float>(i) / kSteps;
        float u = 1 - t;
        float x = u * u * x0 + 2 * u * t * cx + t * t * x1;
        float y = u * u * y0 + 2 * u * t * cy + t * t * y1;
        float edge = std::pow(std::fabs(2 * t - 1), 1.4f);
        c.fillCircle(x, y, thin + (wide - thin) * edge, color);
    }
}

}  // namespace

void drawBrandMark(Canvas& c, float cx, float cy, float r, Color color) {
    // Render white-on-black into a scratch canvas, then use it as a coverage
    // mask, so the cut-outs are transparent over any background.
    int size = static_cast<int>(std::ceil(r * 2)) + 4;
    Canvas m(size, size);
    m.clear(rgba(0, 0, 0));
    float mx = size / 2.0f, my = size / 2.0f;
    m.fillCircle(mx, my, r, rgba(255, 255, 255));
    // Two swooshes from beyond the sphere's upper corners to its lower ones,
    // bowing upwards; they pinch where they cross.
    constexpr float bow = 0.30f, endX = 0.75f;
    float thin = r * 0.05f, wide = r * 0.20f;
    Color cut = rgba(0, 0, 0);
    swoosh(m, mx - r * 1.05f, my - r * 0.95f, mx + r * bow, my - r * bow, mx + r * endX, my + r * 1.05f, thin, wide,
           cut);
    swoosh(m, mx + r * 1.05f, my - r * 0.95f, mx - r * bow, my - r * bow, mx - r * endX, my + r * 1.05f, thin, wide,
           cut);
    std::vector<uint8_t> mask(static_cast<size_t>(size) * size);
    for (size_t i = 0; i < mask.size(); ++i) mask[i] = static_cast<uint8_t>(m.data()[i] & 0xFF);
    c.drawMask(mask.data(), size, size, size, static_cast<int>(std::lround(cx - mx)),
               static_cast<int>(std::lround(cy - my)), color);
}

void drawAppIcon(Canvas& c, const Fonts& fonts) {
    const int s = c.width();
    // Xbox-green field, lighter at the top.
    c.gradientV({0, 0, s, s}, rgba(36, 160, 36), rgba(8, 82, 8));
    drawBrandMark(c, s * 0.5f, s * 0.40f, s * 0.27f);
    const char* word = "PSBox";
    int px = s / 7;
    int w = fonts.bold.measure(word, px);
    fonts.bold.draw(c, word, (s - w) / 2, static_cast<int>(s * 0.74f), px, rgba(255, 255, 255));
}

}  // namespace xc::ui
